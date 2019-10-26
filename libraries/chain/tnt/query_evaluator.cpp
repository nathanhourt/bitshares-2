/*
 * Copyright (c) 2019 Contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <graphene/chain/tnt/query_evaluator.hpp>
#include <graphene/chain/database.hpp>

#include <graphene/protocol/tnt/lookups.hpp>
#include <graphene/protocol/operations.hpp>

namespace graphene { namespace chain { namespace tnt {
namespace qrys = ptnt::queries;
namespace TL = fc::typelist;
template<typename...> using make_void = void;

struct set_attachment_connection_inspector {
   ptnt::connection s;

   static set_attachment_connection_inspector with_connection(const ptnt::connection& s) {
      return set_attachment_connection_inspector{s};
   }

   // Supported attachment types:
   void operator()(ptnt::asset_flow_meter& meter) const { meter.destination = s; }
   void operator()(ptnt::tap_opener& opener) const { opener.destination = s; }

   // Unsupported attachment types:
   [[noreturn]] void reject() const {
      FC_THROW_EXCEPTION(fc::assert_exception,
                         "Cannot set connection on unsupported attachment type. Please report this error.");
   }
   [[noreturn]] void operator()(ptnt::attachment_connect_authority&) const { reject(); }

   // Generic attachment unwrapper:
   void operator()(ptnt::tank_attachment& generic_attachment) {
      TL::runtime::dispatch(ptnt::tank_attachment::list(), generic_attachment.which(),
                                  [this, &generic_attachment](auto t) {
         (*this)(generic_attachment.get<typename decltype(t)::type>());
      });
   }
};

struct callback_table {
   std::function<void(const authority&)> require_authority;
   ptnt::tank_lookup_function lookup_tank;
   std::function<const database&()> get_database;
};

struct reset_meter_evaluator {
   using query_type = qrys::reset_meter;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Check authority
      const auto& reset_auth = query.get_target(tank.schematic).reset_authority;
      if (reset_auth.valid())
         callbacks.require_authority(*reset_auth);
      else
         callbacks.require_authority(*tank.schematic.taps.at(0).open_authority);
      // Check meter is not already at zero
      const auto* state = tank.get_state(query.accessory_address);
      FC_ASSERT(state != nullptr && state->metered_amount > 0,
                "Cannot reset a meter which has not had any asset flow through it yet");
   }
   void apply(tank_object& tank) {
      auto* state = tank.get_state(query.accessory_address);
      state->metered_amount = 0;
   }
};

struct reconnect_attachment_evaluator {
   using query_type = qrys::reconnect_attachment;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      const auto& reconnector = query.get_target(tank.schematic);
      // Check required authority
      callbacks.require_authority(reconnector.connect_authority);

      // Get the target attachment
      FC_ASSERT(tank.schematic.attachments.count(reconnector.attachment_id) > 0,
                "LOGIC ERROR: reconnect_attachment query references nonexistent attachment: ${ID}. "
                "Please report this error.", ("ID", reconnector.attachment_id));
      const auto& attachment = tank.schematic.attachments.at(reconnector.attachment_id);

      // Get the asset type the target attachment releases
      auto attachment_asset = TL::runtime::dispatch(ptnt::tank_attachment::list(), attachment.which(),
                                                    [&attachment](auto t) {
         return ptnt::attachment_get_asset_received(attachment.get<typename decltype(t)::type>());
      });
      FC_ASSERT(attachment_asset.valid(),
                "LOGIC ERROR: attachment_connect_authority target cannot receive asset. Please report this error.");

      // Get the asset type the new connection receives
      ptnt::lookup_utilities lookups(tank.schematic, callbacks.lookup_tank);
      auto connection_asset = lookups.get_connection_asset(query.query_content.new_connection);
      if (connection_asset.is_type<asset_id_type>())
         FC_ASSERT(connection_asset.get<asset_id_type>() == *attachment_asset,
                   "Cannot reconnect attachment: New connection receives different asset type (${S}) than attachment "
                   "releases (${A})", ("S", connection_asset.get<asset_id_type>())("A", *attachment_asset));
      else
         FC_ASSERT(connection_asset.is_type<ptnt::any_asset>(),
                   "Cannot reconnect attachment: New connection is invalid. "
                   "Error determining connection asset type: ${E}",
                   ("E", connection_asset));
   }
   void apply(tank_object& tank) {
      auto& attachment = tank.schematic.attachments.at(query.get_target(tank.schematic).attachment_id);
      set_attachment_connection_inspector::with_connection(query.query_content.new_connection)(attachment);
   }
};

struct create_review_request_evaluator {
   using query_type = qrys::create_request_for_review;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Check open authority is set and present
      const auto& review_req = query.get_target(tank.schematic);
      const auto& open_authority = tank.schematic.taps.at(query.accessory_address.tap_ID).open_authority;
      FC_ASSERT(open_authority.valid(), "Cannot create request to open tap: tap open authority is not set");
      callbacks.require_authority(*open_authority);
      // Check max pending requests limit
      const auto* state = tank.get_state(query.accessory_address);
      if (state != nullptr)
         FC_ASSERT(state->pending_requests.size() < review_req.request_limit,
                   "Cannot create new request to open tap: maximum request limit has already been reached");
   }
   void apply(tank_object& tank) {
      ptnt::review_requirement::request_type new_request;
      new_request.request_amount = query.query_content.request_amount;
      new_request.request_comment = query.query_content.comment;

      auto& state = tank.get_or_create_state(query.accessory_address);
      state.pending_requests[state.request_counter++] = std::move(new_request);
   }
};

struct review_request_evaluator {
   using query_type = qrys::review_request_to_open;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Check reviewer authority is present
      callbacks.require_authority(query.get_target(tank.schematic).reviewer);
      // Check state exists
      const auto* state = tank.get_state(query.accessory_address);
      FC_ASSERT(state != nullptr, "Cannot process review of request to open tap: no requests have been made");
      // Check referenced request exists
      auto request_itr = state->pending_requests.find(query.query_content.request_ID);
      FC_ASSERT(request_itr != state->pending_requests.end(),
                "Cannot process review of request to open tap: No request with specified ID exists");
      // Check request is not already approved
      FC_ASSERT(request_itr->second.approved == false,
                "Cannot process review of request to open tap: Referenced request is already approved");
   }
   void apply(tank_object& tank) {
      if (query.query_content.approved == true)
         tank.get_state(query.accessory_address)->pending_requests.at(query.query_content.request_ID).approved = true;
      else
         tank.get_state(query.accessory_address)->pending_requests.erase(query.query_content.request_ID);
   }
};

struct cancel_review_request_evaluator {
   using query_type = qrys::cancel_request_for_review;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Check state exists
      const auto* state = tank.get_state(query.accessory_address);
      FC_ASSERT(state != nullptr, "Cannot process cancelation of request to open tap: no requests have been made");
      // Check open authority is set and present
      const auto& open_authority = tank.schematic.taps.at(query.accessory_address.tap_ID).open_authority;
      FC_ASSERT(open_authority.valid(), "Cannot cancel request to open tap: tap open authority is not set");
      callbacks.require_authority(*open_authority);
      // Check referenced request exists
      auto request_itr = state->pending_requests.find(query.query_content.request_ID);
      FC_ASSERT(request_itr != state->pending_requests.end(),
                "Cannot process cancelation of request to open tap: No request with specified ID exists");
   }
   void apply(tank_object& tank) {
      tank.get_state(query.accessory_address)->pending_requests.erase(query.query_content.request_ID);
   }
};

struct consume_reviewed_request_evaluator {
   using query_type = qrys::consume_approved_request_to_open;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Check state exists
      const auto* state = tank.get_state(query.accessory_address);
      FC_ASSERT(state != nullptr, "Cannot process consumption of request to open tap: no requests have been made");
      // Check open authority is set and present
      const auto& open_authority = tank.schematic.taps.at(query.accessory_address.tap_ID).open_authority;
      FC_ASSERT(open_authority.valid(), "Cannot consume request to open tap: tap open authority is not set");
      callbacks.require_authority(*open_authority);
      // Check referenced request exists
      auto request_itr = state->pending_requests.find(query.query_content.request_ID);
      FC_ASSERT(request_itr != state->pending_requests.end(),
                "Cannot process consumption of request to open tap: No request with specified ID exists");
      // Check request is approved
      FC_ASSERT(request_itr->second.approved == true,
                "Cannot process consumption of request to open tap: Referenced request is not approved");
   }
   void apply(tank_object&) {
      // Apply does nothing; we leave the consumed request in the state and the tap flow logic will delete it.
   }
};

struct documentation_evaluator {
   using query_type = qrys::documentation_string;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object&) { /* No checks */ }
   void apply(tank_object&) { /* No changes */ }
   // This evaluator does nothing; the tap flow logic will check that this query was processed before opening the tap
};

struct create_delay_request_evaluator {
   using query_type = qrys::create_request_for_delay;
   const ptnt::targeted_query<query_type>& query;
   const callback_table callbacks;
   void evaluate(const tank_object& tank) {
      // Check open authority is set and present
      const auto& delay_req = query.get_target(tank.schematic);
      const auto& open_authority = tank.schematic.taps.at(query.accessory_address.tap_ID).open_authority;
      FC_ASSERT(open_authority.valid(), "Cannot create request to open tap: tap open authority is not set");
      callbacks.require_authority(*open_authority);
      // Check max pending requests limit
      const auto* state = tank.get_state(query.accessory_address);
      if (state != nullptr)
         FC_ASSERT(state->pending_requests.size() < delay_req.request_limit,
                   "Cannot create new request to open tap: maximum request limit has already been reached");
   }
   void apply(tank_object& tank) {
      const auto& delay_req = query.get_target(tank.schematic);
      ptnt::delay_requirement::request_type new_request;
      new_request.request_amount = query.query_content.request_amount;
      new_request.request_comment = query.query_content.comment;
      new_request.delay_period_end = callbacks.get_database().head_block_time() + delay_req.delay_period_sec;

      auto& state = tank.get_or_create_state(query.accessory_address);
      state.pending_requests[state.request_counter++] = std::move(new_request);
   }
};

struct veto_request_evaluator {
   using query_type = qrys::veto_request_in_delay;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Check state exists
      const auto* state = tank.get_state(query.accessory_address);
      FC_ASSERT(state != nullptr, "Cannot process veto of request to open tap: no requests have been made");
      // Check veto authority is set and present
      const auto& veto_authority = query.get_target(tank.schematic).veto_authority;
      FC_ASSERT(veto_authority.valid(), "Cannot veto request to open tap: no veto authority is defined");
      callbacks.require_authority(*veto_authority);
      // Check referenced request exists
      auto request_itr = state->pending_requests.find(query.query_content.request_ID);
      FC_ASSERT(request_itr != state->pending_requests.end(),
                "Cannot process veto of request to open tap: No request with specified ID exists");
      // Check request is not already matured
      FC_ASSERT(request_itr->second.delay_period_end < callbacks.get_database().head_block_time(),
                "Cannot process veto of request which has already matured");
   }
   void apply(tank_object& tank) {
      tank.get_state(query.accessory_address)->pending_requests.erase(query.query_content.request_ID);
   }
};

struct cancel_delay_request_evaluator {
   using query_type = qrys::cancel_request_in_delay;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Check state exists
      const auto* state = tank.get_state(query.accessory_address);
      FC_ASSERT(state != nullptr, "Cannot process cancelation of request to open tap: no requests have been made");
      // Check open authority is set and present
      const auto& open_authority = tank.schematic.taps.at(query.accessory_address.tap_ID).open_authority;
      FC_ASSERT(open_authority.valid(), "Cannot cancel request to open tap: tap open authority is not set");
      callbacks.require_authority(*open_authority);
      // Check referenced request exists
      auto request_itr = state->pending_requests.find(query.query_content.request_ID);
      FC_ASSERT(request_itr != state->pending_requests.end(),
                "Cannot process cancelation of request to open tap: No request with specified ID exists");
   }
   void apply(tank_object& tank) {
      tank.get_state(query.accessory_address)->pending_requests.erase(query.query_content.request_ID);
   }
};

struct consume_matured_request_evaluator {
   using query_type = qrys::consume_matured_request_to_open;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Check state exists
      const auto* state = tank.get_state(query.accessory_address);
      FC_ASSERT(state != nullptr, "Cannot process consumption of request to open tap: no requests have been made");
      // Check open authority is set and present
      const auto& open_authority = tank.schematic.taps.at(query.accessory_address.tap_ID).open_authority;
      FC_ASSERT(open_authority.valid(), "Cannot consume request to open tap: tap open authority is not set");
      callbacks.require_authority(*open_authority);
      // Check referenced request exists
      auto request_itr = state->pending_requests.find(query.query_content.request_ID);
      FC_ASSERT(request_itr != state->pending_requests.end(),
                "Cannot process consumption of request to open tap: No request with specified ID exists");
      // Check request is matured
      FC_ASSERT(request_itr->second.delay_period_end >= callbacks.get_database().head_block_time(),
                "Cannot consume request to open tap: request has not matured yet");
   }
   void apply(tank_object&) {
      // Apply does nothing; we leave the consumed request in the state and the tap flow logic will delete it.
   }
};

struct reveal_preimage_evaluator {
   using query_type = qrys::reveal_hash_preimage;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Authority is conferred by a valid preimage; no particular authority is required
      const auto& req = query.get_target(tank.schematic);
      if (req.preimage_size.valid())
         FC_ASSERT(query.query_content.preimage.size() == *req.preimage_size,
                   "Rejecting hash preimage: preimage size is not correct");
      auto check_hash = [hash_variant=std::cref(req.hash), preimage=std::cref(query.query_content.preimage)](auto t) {
         using Hash = typename decltype(t)::type;
         const auto& hash = hash_variant.get().get<Hash>();
         FC_ASSERT(Hash::hash(preimage.get()) == hash, "Preimage does not hash to expected value");
      };
      TL::runtime::dispatch(ptnt::hash_preimage_requirement::hash_type::list(), req.hash.which(), check_hash);
   }
   void apply(tank_object&) {
      // Apply does nothing; the tap flow logic will check that this query was processed before opening the tap
   }
};

struct redeem_ticket_evaluator {
   using query_type = qrys::redeem_ticket_to_open;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Authority is conferred by a valid signature; no particular authority is required
      // Check ticket number
      const auto* state = tank.get_state(query.accessory_address);
      if (state == nullptr)
         FC_ASSERT(query.query_content.ticket.ticket_number == 0,
                   "Ticket number is invalid: first ticket should have ticket number 0");
      else
         FC_ASSERT(query.query_content.ticket.ticket_number == state->tickets_consumed,
                   "Ticket number is invalid; expected ticket number ${N}", ("N", state->tickets_consumed));

      const auto& requirement = query.get_target(tank.schematic);
      auto ticket_hash = fc::sha256::hash(query.query_content.ticket);
      public_key_type ticket_signature_key = fc::ecc::public_key(query.query_content.ticket_signature, ticket_hash);
      FC_ASSERT(ticket_signature_key == requirement.ticket_signer,
                "Cannot redeem ticket: Ticket signature is not valid");
   }
   void apply(tank_object& tank) {
      auto& state = tank.get_or_create_state(query.accessory_address);
      ++state.tickets_consumed;
   }
};

struct reset_exchange_evaluator {
   using query_type = qrys::reset_exchange_requirement;
   const ptnt::targeted_query<query_type>& query;
   const callback_table& callbacks;
   void evaluate(const tank_object& tank) {
      // Check required authority
      const auto& req = query.get_target(tank.schematic);
      if (req.reset_authority.valid())
         callbacks.require_authority(*req.reset_authority);
      else
         callbacks.require_authority(*tank.schematic.taps.at(0).open_authority);
      // Check that the exchange has been used
      const auto* exchange_state = tank.get_state(query.accessory_address);
      FC_ASSERT(exchange_state != nullptr && exchange_state->amount_released > 0,
                "Cannot reset exchange requirement: requirement has not yet released asset");
      // Check that the meter is at zero
      tank_id_type meter_tank_id = tank.id;
      if (req.meter_id.tank_id.valid())
         meter_tank_id = *req.meter_id.tank_id;
      ptnt::tank_accessory_address<ptnt::asset_flow_meter> meter_address{req.meter_id.attachment_id};
      const auto* meter_state = meter_tank_id(callbacks.get_database()).get_state(meter_address);
      if (meter_state != nullptr)
         FC_ASSERT(meter_state->metered_amount == 0,
                   "Cannot reset exchange requirement: exchange meter is not at zero");
   }
   void apply(tank_object& tank) {
      auto& exchange_state = tank.get_or_create_state(query.accessory_address);
      exchange_state.amount_released = 0;
   }
};

} } } // namespace graphene::chain::tnt

// We interrupt this file to bring you... typename reflectors!
namespace tnt = graphene::chain::tnt;
FC_REFLECT_TYPENAME(tnt::reset_meter_evaluator)
FC_REFLECT_TYPENAME(tnt::reconnect_attachment_evaluator)
FC_REFLECT_TYPENAME(tnt::create_review_request_evaluator)
FC_REFLECT_TYPENAME(tnt::review_request_evaluator)
FC_REFLECT_TYPENAME(tnt::cancel_review_request_evaluator)
FC_REFLECT_TYPENAME(tnt::consume_reviewed_request_evaluator)
FC_REFLECT_TYPENAME(tnt::documentation_evaluator)
FC_REFLECT_TYPENAME(tnt::create_delay_request_evaluator)
FC_REFLECT_TYPENAME(tnt::veto_request_evaluator)
FC_REFLECT_TYPENAME(tnt::cancel_delay_request_evaluator)
FC_REFLECT_TYPENAME(tnt::consume_matured_request_evaluator)
FC_REFLECT_TYPENAME(tnt::reveal_preimage_evaluator)
FC_REFLECT_TYPENAME(tnt::redeem_ticket_evaluator)
FC_REFLECT_TYPENAME(tnt::reset_exchange_evaluator)
// And now back to our regularly scheduled programming :)

namespace graphene { namespace chain { namespace tnt {

// This list must be exactly parallel to protocol::tnt::query_type_list
using query_evaluators = TL::list<reset_meter_evaluator, reconnect_attachment_evaluator,
                                  create_review_request_evaluator, review_request_evaluator,
                                  cancel_review_request_evaluator, consume_reviewed_request_evaluator,
                                  documentation_evaluator, create_delay_request_evaluator, veto_request_evaluator,
                                  cancel_delay_request_evaluator, consume_matured_request_evaluator,
                                  reveal_preimage_evaluator, redeem_ticket_evaluator, reset_exchange_evaluator>;
template<typename Evaluator> struct to_query_type { using type = typename Evaluator::query_type; };
static_assert(std::is_same<TL::transform<query_evaluators, to_query_type>, ptnt::query_type_list>::value,
              "Some query types do not have evaluators defined. Define evaluators above for the new query types.");
using query_evaluator_type = TL::apply<query_evaluators, static_variant>;

struct query_evaluator_impl {
   bool has_applied = false;
   vector<query_evaluator_type> evaluators;
   std::multimap<ptnt::tank_accessory_address_type, const_ref<ptnt::tank_query_type>,
                 ptnt::accessory_address_lt<>> accessory_queries;
   vector<const_ref<ptnt::tank_query_type>> tank_queries;
   optional<const_ref<tank_object>> query_tank;

   template<typename Query, typename Target = typename Query::target_type,
            std::enable_if_t<std::is_same<Target, ptnt::tank_query>::value, bool> = true>
   void record_query(const ptnt::targeted_query<Query>&, const ptnt::tank_query_type& gq) {
      tank_queries.push_back(gq);
   }
   template<typename Query, typename Target = typename Query::target_type,
            std::enable_if_t<!std::is_same<Target, ptnt::tank_query>::value, bool> = true>
   void record_query(const ptnt::targeted_query<Query>& q, const ptnt::tank_query_type& gq) {
      accessory_queries.insert(std::make_pair(q.accessory_address, std::cref(gq)));
   }
};

query_evaluator::query_evaluator() {
   my = std::make_unique<query_evaluator_impl>();
}

query_evaluator::~query_evaluator() = default;

void query_evaluator::set_query_tank(const tank_object& tank) {
   FC_ASSERT(!my->query_tank.valid(), "The query tank must not be changed! Use a new query_evaluator instead");
   my->query_tank = tank;
}

vector<authority> query_evaluator::evaluate_query(const protocol::tnt::tank_query_type& query, const database& db) {
   FC_ASSERT(my->has_applied == false,
             "LOGIC ERROR: Cannot evaluate new queries after queries have already been applied. "
             "Please report this error.");
   FC_ASSERT(my->query_tank.valid(), "set_query_tank must be called prior to evaluating queries!");
   const tank_object& tank = my->query_tank->get();

   callback_table callbacks;
   std::vector<authority> used_authorities;
   callbacks.require_authority = [&used_authorities](const authority& auth) {
      used_authorities.push_back(auth);
   };
   callbacks.lookup_tank = [&db](tank_id_type id) -> const ptnt::tank_schematic* {
      try { return &id(db).schematic; }
      catch (fc::exception&) { return nullptr; }
   };
   callbacks.get_database = [&db]() -> const database& { return db; };

   my->evaluators.push_back(TL::runtime::dispatch(query_evaluators(), query.which(),
                                                  [this, &query, &tank, &callbacks](auto t) -> query_evaluator_type {
      using evaluator_type = typename decltype(t)::type;
      using query_type = ptnt::targeted_query<typename evaluator_type::query_type>;
      const auto& specific_query = query.get<query_type>();
      evaluator_type eval{specific_query, callbacks};
      eval.evaluate(tank);
      my->record_query(specific_query, query);
      return eval;
   }));

   return used_authorities;
}

void query_evaluator::apply_queries(tank_object& tank) {
   for (auto& evaluator : my->evaluators)
      TL::runtime::dispatch(query_evaluators(), evaluator.which(), [&evaluator, &tank](auto t) {
         evaluator.get<typename decltype(t)::type>().apply(tank);
      });
}

vector<const_ref<ptnt::tank_query_type>> query_evaluator::get_tank_queries() const {
   return my->tank_queries;
}

vector<const_ref<ptnt::tank_query_type>> query_evaluator::get_target_queries(
         const protocol::tnt::tank_accessory_address_type& address) const {
   auto range = my->accessory_queries.equal_range(address);
   vector<const_ref<ptnt::tank_query_type>> result;
   std::transform(range.first, range.second, std::back_inserter(result), [](const auto& aq) { return aq.second; });
   return result;
}

} } } // namespace graphene::chain::tnt
