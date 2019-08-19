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

#include <fc/exception/exception.hpp>

#include <graphene/protocol/tnt/operations.hpp>

namespace graphene { namespace protocol { namespace tnt {

// Perform stateless consistency checks on tap_requirements
struct tap_requirement_checker {
   using result_type = void;
   const tank_schematic& tank_schema;
   const tank_lookup_function& lookup_tank;
   tap_requirement_checker(const tank_schematic& schema, const tank_lookup_function& lookup_tank)
       : tank_schema(schema), lookup_tank(lookup_tank) {}

   const tank_schematic* get_tank(const optional<tank_id_type>& opt_id) const {
      if (!opt_id.valid())
         return &tank_schema;
      if (lookup_tank)
         return lookup_tank(*opt_id);
      return nullptr;
   }

   void check_meter(const attachment_id_type& id, const string& name_for_errors) const {
      constexpr auto meter_tag = tank_attachment::tag<asset_flow_meter>::value;
      auto schema = get_tank(id.tank_id);

      if (schema != nullptr) {
         FC_ASSERT(schema->attachments.contains(id.attachment_id),
                   + name_for_errors + " references nonexistent meter attachment");
         FC_ASSERT(schema->attachments.at(id.attachment_id).which() == meter_tag,
                   + name_for_errors + " references attachment which is not a meter");
      }
   }
   void check_authority(const authority& auth, const string& name_for_errors) const {
      FC_ASSERT(!auth.is_impossible(), + name_for_errors + " must not be impossible authority");
      FC_ASSERT(auth.weight_threshold > 0, + name_for_errors + " must not be trivial authority");
      FC_ASSERT(!(auth == authority::null_authority()), + name_for_errors + " must not be null authority");
   }

   void operator()(const immediate_flow_limit& req) const {
      FC_ASSERT(req.limit > 0, "Immediate flow limit must be positive");
   }
   void operator()(const cumulative_flow_limit& req) const {
      FC_ASSERT(req.limit > 0, "Cumulative flow limit must be positive");
      check_meter(req.meter_id, "Cumulative flow limit");
   }
   void operator()(const periodic_flow_limit& req) const {
      FC_ASSERT(req.limit > 0, "Periodic flow limit must be positive");
      check_meter(req.meter_id, "Periodic flow limit");
   }
   void operator()(const time_lock& req) const {
      FC_ASSERT(!req.lock_unlock_times.empty(), "Time lock must specify at least one lock/unlock time");
   }
   void operator()(const minimum_tank_level& req) const {
      FC_ASSERT(req.minimum_level > 0, "Minimum tank level must be positive");
   }
   void operator()(const review_requirement& req) const {
      check_authority(req.reviewer, "Reviewer");
   }
   void operator()(const documentation_requirement&) const { /* no checks */ }
   void operator()(const delay_requirement& req) const {
      if (req.veto_authority.valid())
         check_authority(*req.veto_authority, "Veto authority");
      FC_ASSERT(req.delay_period_sec > 0, "Delay period must be positive");
   }
   void operator()(const hash_lock& req) const {
      fc::typelist::runtime::dispatch(hash_lock::hash_type::list(), req.hash.which(), [&req](auto t) {
         using hash_type = typename decltype(t)::type;
         FC_ASSERT(req.hash.get<hash_type>() != hash_type(), "Hash lock must not be null hash");
      });
      if (req.preimage_size)
         FC_ASSERT(*req.preimage_size > 0, "Hash lock preimage size must be positive");
   }
   void operator()(const ticket_requirement& req) const {
      FC_ASSERT(req.ticket_signer != public_key_type(), "Ticket signer must not be null public key");
   }
   void operator()(const exchange_requirement& req) const {
      check_meter(req.meter_id, "Exchange requirement");
      FC_ASSERT(req.tick_amount > 0, "Exchange requirement tick amount must be positive");
      FC_ASSERT(req.release_per_tick > 0, "Exchange requirement release amount must be positive");
   }
};

void tap::validate(const tank_schematic &current_tank, const tank_lookup_function& tank_lookup) const {
   FC_ASSERT(connected_sink.valid() || connect_authority.valid(),
             "Tap must be connected, or specify a connect authority");
   tap_requirement_checker checker(current_tank, tank_lookup);
   for (const auto& req : requirements) req.visit(checker);
}

void tap::validate_emergency() const {
   FC_ASSERT(requirements.empty(), "Emergency tap must have no tap requirements");
   FC_ASSERT(open_authority.valid(), "Emergency tap must specify an open authority");
   FC_ASSERT(connect_authority.valid(), "Emergency tap must specify a connect authority");
   FC_ASSERT(destructor_tap == true, "Emergency tap must be a destructor tap");
}

tank_schematic tank_schematic::from_create_operation(const tank_create_operation &create_op) {
   tank_schematic schema;
   for (const auto& attachment : create_op.attachments)
      schema.attachments[schema.attachment_counter++] = attachment;
   for (const auto& tap : create_op.taps)
      schema.taps[schema.tap_counter++] = tap;
   return schema;
}

void tank_schematic::validate(graphene::protocol::tnt::tank_lookup_function tank_lookup) {
   FC_ASSERT(taps.size() > 0, "Must have at least one tap");
   taps[0].validate_emergency();
   std::for_each(taps.begin(), taps.end(), [this, &tank_lookup](const auto& pair) {
      pair.second.validate(*this, tank_lookup);
   });
}

} } } // namespace graphene::protocol::tnt
