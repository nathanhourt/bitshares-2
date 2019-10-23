/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/tnt/tap_requirement_utility.hpp>

namespace graphene { namespace chain { namespace tnt {

struct tap_requirement_utility_impl {
   cow_db_wrapper& db;
   const query_evaluator& queries;
   ptnt::tap_id_type tap_ID;
   bool max_release_run = false;
   optional<ptnt::index_type> most_restrictive_requirement;

   // A few requirement types have a flow limit based on consuming permissions to open the tap. These requirements
   // should support opening the tap multiple times, as long as all openings release no more than the total permitted
   // limit. These requirements use this map to store the amount remaining from the limit between tap openings.
   map<ptnt::index_type, ptnt::asset_flow_limit> remaining_limits;
   void adjust_limit(ptnt::index_type index, share_type amount) {
      auto& limit = remaining_limits[index];
      if (limit.is_type<share_type>()) {
         FC_ASSERT(limit.get<share_type>() >= amount,
                   "LOGIC ERROR: Release amount is greater than remaining limit. Please report this error.");
         limit.get<share_type>() -= amount;
      }
   }
   map<ptnt::index_type, bool> adjusted_states;

   tap_requirement_utility_impl(cow_db_wrapper& db, const query_evaluator& queries, ptnt::tap_id_type&& id)
      : db(db), queries(queries), tap_ID(std::move(id)) {}
};

tap_requirement_utility::tap_requirement_utility(cow_db_wrapper& db, ptnt::tap_id_type tap_ID,
                                                 const query_evaluator& queries) {
   FC_ASSERT(tap_ID.tank_id.valid(), "INTERNAL ERROR: Tap ID given to tap_requirement_utility must specify tank ID. "
                                     "Please report this error.");
   my = std::make_unique<tap_requirement_utility_impl>(db, queries, std::move(tap_ID));
}

tap_requirement_utility::tap_requirement_utility(tap_requirement_utility&&) = default;
tap_requirement_utility& tap_requirement_utility::operator=(tap_requirement_utility&&) = default;
tap_requirement_utility::~tap_requirement_utility() = default;

// Get the maximum amount a particular tap_requirement will allow to be released
class max_release_inspector {
   tap_requirement_utility_impl& data;
   const tank_object& tank;
   max_release_inspector(tap_requirement_utility_impl& data, const tank_object& t)
      : data(data), tank(t) {}

   template<typename Requirement>
   struct Req {
      const Requirement& req;
      ptnt::tank_accessory_address<Requirement> address;
   };

   ptnt::asset_flow_limit operator()(const Req<ptnt::immediate_flow_limit>& req) const { return req.req.limit; }
   ptnt::asset_flow_limit operator()(const Req<ptnt::cumulative_flow_limit>& req) const {
      const auto* state = tank.get_state(req.address);
      if (state == nullptr)
         return req.req.limit;
      return req.req.limit - state->amount_released;
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::periodic_flow_limit>& req) const {
      const auto* state = tank.get_state(req.address);
      if (state == nullptr)
         return req.req.limit;
      auto period_num = req.req.period_num_at_time(tank.creation_date, data.db.get_db().head_block_time());
      if (state->period_num == period_num)
         return req.req.limit - state->amount_released;
      return req.req.limit;
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::time_lock>& req) const {
      auto unlocked_now = req.req.unlocked_at_time(data.db.get_db().head_block_time());
      if (unlocked_now)
         return ptnt::unlimited_flow();
      return share_type(0);
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::minimum_tank_level>& req) const {
      if (tank.balance <= req.req.minimum_level)
         return share_type(0);
      return tank.balance - req.req.minimum_level;
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::documentation_requirement>&) const {
      auto tank_queries = data.queries.get_tank_queries();
      for (const ptnt::tank_query_type& q : tank_queries)
         if (q.is_type<ptnt::targeted_query<ptnt::queries::documentation_string>>())
            return ptnt::unlimited_flow();
      return share_type(0);
   }
   // Delay requirement and review requirement (collectively, the "Request Requirements") have exactly identical
   // implementations, just different types, so unify them into a single function
   using request_requirements = ptnt::TL::list<ptnt::review_requirement, ptnt::delay_requirement>;
   using request_consume_queries = ptnt::TL::list<ptnt::queries::consume_approved_request_to_open,
                                                  ptnt::queries::consume_matured_request_to_open>;
   template<typename RR, std::enable_if_t<ptnt::TL::contains<request_requirements, RR>(), bool> = true>
   ptnt::asset_flow_limit operator()(const Req<RR>& req) const {
      using consume_query = ptnt::targeted_query<ptnt::TL::at<request_consume_queries,
                                                              ptnt::TL::index_of<request_requirements, RR>()>>;

      // Check if we're opening for the second time in the same operation; if so, return the amount leftover after
      // previous openings
      if (data.remaining_limits.count(req.address.requirement_index))
         return data.remaining_limits[req.address.requirement_index];

      // This is the first opening in this operation. Count up how much is allowed to release by consuming requests
      const auto* state = tank.get_state(req.address);
      if (state == nullptr) {
         data.remaining_limits[req.address.requirement_index] = share_type(0);
         return share_type(0);
      }
      auto my_queries = data.queries.get_target_queries(req.address);
      share_type limit = 0;

      for (const ptnt::tank_query_type& query : my_queries)
         if (query.is_type<consume_query>()) {
            const auto& request = state->pending_requests.at(query.get<consume_query>().query_content.request_ID);
            if (request.request_amount.template is_type<ptnt::unlimited_flow>()) {
               data.remaining_limits[req.address.requirement_index] = ptnt::unlimited_flow();
               return ptnt::unlimited_flow();
            }
            limit += request.request_amount.template get<share_type>();
         }

      data.remaining_limits[req.address.requirement_index] = limit;
      return std::move(limit);
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::hash_preimage_requirement>& req) const {
      auto my_queries = data.queries.get_target_queries(req.address);
      for (const ptnt::tank_query_type& query : my_queries)
         if (query.is_type<ptnt::targeted_query<ptnt::queries::reveal_hash_preimage>>())
            return ptnt::unlimited_flow();
      return share_type(0);
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::ticket_requirement>& req) const {
      // Check if we're opening for the second time in the same operation; if so, return the amount leftover after
      // previous openings
      if (data.remaining_limits.count(req.address.requirement_index))
         return data.remaining_limits[req.address.requirement_index];

      // This is the first opening in this operation. Release amount is specified by a redeemed ticket
      auto my_queries = data.queries.get_target_queries(req.address);
      for (const ptnt::tank_query_type& query : my_queries) {
         if (query.is_type<ptnt::targeted_query<ptnt::queries::redeem_ticket_to_open>>()) {
            const auto& q = query.get<ptnt::targeted_query<ptnt::queries::redeem_ticket_to_open>>().query_content;
            data.remaining_limits[req.address.requirement_index] = q.ticket.max_withdrawal;
            if (q.ticket.max_withdrawal.is_type<ptnt::unlimited_flow>())
               return ptnt::unlimited_flow();
            return q.ticket.max_withdrawal.get<share_type>();
         }
      }

      data.remaining_limits[req.address.requirement_index] = share_type(0);
      return share_type(0);
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::exchange_requirement>& req) const {
      const auto* state = tank.get_state(req.address);
      const auto& meter_id = req.req.meter_id;
      tank_id_type meter_tank_id = tank.id;
      if (meter_id.tank_id.valid())
         meter_tank_id = *meter_id.tank_id;
      const tank_object& meter_tank = meter_tank_id(data.db);
      const auto* meter_state =
            meter_tank.get_state(ptnt::tank_accessory_address<ptnt::asset_flow_meter>{meter_id.attachment_id});
      if (meter_state == nullptr)
         return share_type(0);

      return req.req.max_release_amount((state == nullptr? share_type(0) : state->amount_released), *meter_state);
   }

public:
   static ptnt::asset_flow_limit inspect(tap_requirement_utility_impl& data, const tank_object& tank,
                                         ptnt::index_type requirement_index) {
      max_release_inspector inspector(data, tank);
      const auto& requirement = tank.schematic.taps.at(data.tap_ID.tap_id).requirements[requirement_index];
      return ptnt::TL::runtime::dispatch(ptnt::tap_requirement::list(), requirement.which(),
                                         [&inspector, &requirement, &data, requirement_index](auto t) {
         using Requirement = typename decltype(t)::type;
         ptnt::tank_accessory_address<Requirement> address{data.tap_ID.tap_id, requirement_index};
         Req<Requirement> req{requirement.get<Requirement>(), address};
         return inspector(req);
      });
   }
};

share_type tap_requirement_utility::max_tap_release() {
   const tank_object& tank = my->db.get(*my->tap_ID.tank_id);
   ptnt::asset_flow_limit tap_limit = tank.balance;
   const auto& tap = tank.schematic.taps.at(my->tap_ID.tap_id);

   for (ptnt::index_type i = 0; i < tap.requirements.size(); ++i) {
      auto req_limit = max_release_inspector::inspect(*my, tank, i);

      if (req_limit < tap_limit) {
         tap_limit = req_limit;
         my->most_restrictive_requirement = i;
      }
      if (tap_limit.is_type<share_type>() && tap_limit.get<share_type>() == 0)
         break;
   }

   my->max_release_run = true;
   // tap_limit was initially assigned an amount, and then only assigned a value less than its old one, so it can't
   // be unlimited
   return tap_limit.get<share_type>();
}

optional<protocol::tnt::index_type> tap_requirement_utility::most_restrictive_requirement_index() const {
   FC_ASSERT(my->max_release_run, "INTERNAL ERROR: Queried most restrictive requirement before running "
                                  "max_tap_release. Please report this error.");
   return my->most_restrictive_requirement;
}

class prepare_release_inspector {
   tap_requirement_utility_impl& data;
   tank_object& tank;
   share_type amount;
   prepare_release_inspector(tap_requirement_utility_impl& data, share_type amount)
      : data(data), tank(data.db.get(*data.tap_ID.tank_id)), amount(amount) {}

   template<typename Requirement>
   struct Req {
      const Requirement& req;
      ptnt::tank_accessory_address<Requirement> address;
   };

   // The following requirement types can be ignored; they require no modification based on the amount released
   using NoModificationRequirements = ptnt::TL::list<ptnt::immediate_flow_limit, ptnt::time_lock,
                                                     ptnt::minimum_tank_level, ptnt::documentation_requirement,
                                                     ptnt::hash_preimage_requirement>;
   template<typename R, std::enable_if_t<ptnt::TL::contains<NoModificationRequirements, R>(), bool> = true>
   void operator()(const Req<R>&) {}

   void operator()(const Req<ptnt::cumulative_flow_limit>& req) {
      auto& state = tank.get_or_create_state(req.address);
      state.amount_released += amount;
   }
   void operator()(const Req<ptnt::periodic_flow_limit>& req) {
      auto& state = tank.get_or_create_state(req.address);
      auto period_num = req.req.period_num_at_time(tank.creation_date, data.db.get_db().head_block_time());
      if (state.period_num != period_num) {
         state.period_num = period_num;
         state.amount_released = 0;
      }
      state.amount_released += amount;
   }
   // Delay requirement and review requirement (collectively, the "Request Requirements") have exactly identical
   // implementations, just different types, so unify them into a single function
   using request_requirements = ptnt::TL::list<ptnt::review_requirement, ptnt::delay_requirement>;
   using request_consume_queries = ptnt::TL::list<ptnt::queries::consume_approved_request_to_open,
                                                  ptnt::queries::consume_matured_request_to_open>;
   template<typename RR, std::enable_if_t<ptnt::TL::contains<request_requirements, RR>(), bool> = true>
   void operator()(const Req<RR>& req) {
      using consume_query = ptnt::targeted_query<ptnt::TL::at<request_consume_queries,
                                                              ptnt::TL::index_of<request_requirements, RR>()>>;

      auto index = req.address.requirement_index;
      data.adjust_limit(index, amount);

      // Delete the consumed requests, if it hasn't been done already
      if (data.adjusted_states[index])
         return;
      auto& state = tank.get_or_create_state(req.address);
      auto my_queries = data.queries.get_target_queries(req.address);
      for (const ptnt::tank_query_type& query : my_queries)
         if (query.is_type<consume_query>()) {
            const auto& q = query.get<consume_query>();
            state.pending_requests.erase(q.query_content.request_ID);
         }
      data.adjusted_states[index] = true;
   }
   void operator()(const Req<ptnt::ticket_requirement>& req) {
      using query_type = ptnt::targeted_query<ptnt::queries::redeem_ticket_to_open>;
      data.adjust_limit(req.address.requirement_index, amount);

      // Update the consumed ticket number, if it hasn't been done already
      if (data.adjusted_states[req.address.requirement_index])
         return;
      auto& state = tank.get_or_create_state(req.address);
      auto my_queries = data.queries.get_target_queries(req.address);
      for (const ptnt::tank_query_type& query : my_queries)
         if (query.is_type<query_type>())
            state.tickets_consumed = query.get<query_type>().query_content.ticket.ticket_number + 1;
   }
   void operator()(const Req<ptnt::exchange_requirement>& req) {
      auto& state = tank.get_or_create_state(req.address);
      state.amount_released += amount;
   }

public:
   static void inspect(tap_requirement_utility_impl& data, share_type amount, ptnt::index_type requirement_index) {
      prepare_release_inspector inspector(data, amount);
      const auto& requirement = inspector.tank.schematic.taps.at(data.tap_ID.tap_id).requirements[requirement_index];
      return ptnt::TL::runtime::dispatch(ptnt::tap_requirement::list(), requirement.which(),
                                         [&inspector, &requirement, &data, requirement_index](auto t) {
         using Requirement = typename decltype(t)::type;
         ptnt::tank_accessory_address<Requirement> address{data.tap_ID.tap_id, requirement_index};
         Req<Requirement> req{requirement.get<Requirement>(), address};
         return inspector(req);
      });
   }
};

void tap_requirement_utility::prepare_tap_release(share_type release_amount) {
   const tank_object& tank = my->db.get(*my->tap_ID.tank_id);
   ptnt::asset_flow_limit tap_limit = tank.balance;
   const auto& tap = tank.schematic.taps.at(my->tap_ID.tap_id);

   for (ptnt::index_type i = 0; i < tap.requirements.size(); ++i)
      prepare_release_inspector::inspect(*my, release_amount, i);
}

} } } // namespace graphene::chain::tnt
