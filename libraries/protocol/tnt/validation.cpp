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

#include <graphene/protocol/tnt/validation.hpp>

#include "type_checks.hxx"

namespace graphene { namespace protocol { namespace tnt {

void check_authority(const authority& auth, const string& name_for_errors) {
   FC_ASSERT(!auth.is_impossible(), + name_for_errors + " must not be impossible authority");
   FC_ASSERT(auth.weight_threshold > 0, + name_for_errors + " must not be trivial authority");
   FC_ASSERT(!(auth == authority::null_authority()), + name_for_errors + " must not be null authority");
}

void tank_validator::validate_attachment(index_type attachment_id) {
   // Define a visitor that examines each attachment type
   struct {
      using result_type = void;
      tank_validator& validator;

      void check_sink_asset(const sink& s, asset_id_type a) {
         CHECK_SINK_ASSET_RESULT(); auto asset_result = validator.get_sink_asset(s);
         if (asset_result.is_type<no_asset>())
            FC_THROW_EXCEPTION(fc::assert_exception, "Flow meter destination sink cannot receive asset: ${S}",
                               ("S", s));
         if (asset_result.is_type<nonexistent_object>())
            FC_THROW_EXCEPTION(fc::assert_exception, "Flow meter destination sink does not exist: ${E}",
                               ("E", asset_result.get<nonexistent_object>()));
         if (asset_result.is_type<asset_id_type>())
            FC_ASSERT(asset_result.get<asset_id_type>() == a, "Flow meter destination sink accepts wrong asset type");
      }

      void operator()(const asset_flow_meter& att) {
         check_sink_asset(att.destination_sink, att.asset_type);
         ++validator.attachment_counters[tank_attachment::tag<asset_flow_meter>::value];
      }
      void operator()(const deposit_source_restrictor& att) {
         check_sink_asset(att.destination_sink, att.asset_type);
         FC_ASSERT(att.asset_type == validator.current_tank.asset_type,
                   "Deposit source restrictor must receive same asset as tank");
         FC_ASSERT(att.legal_deposit_paths.size() > 0,
                   "Deposit source restrictor must accept at least one deposit path");

         CHECK_DESTINATION_SINK_RESULT();
         auto output = validator.get_destination_sink(att.destination_sink,
                                                      validator.parameters.max_sink_chain_length, att.asset_type);
         if (output.is_type<bad_sink>()) {
            auto badsink = output.get<bad_sink>();
            switch (badsink.reason) {
            case bad_sink::receives_no_asset:
               FC_THROW_EXCEPTION(fc::assert_exception,
                                  "Sink in chain after deposit source restrictor does not receive asset: ${S}",
                                  ("S", badsink.s));
            case bad_sink::receives_wrong_asset:
               FC_THROW_EXCEPTION(fc::assert_exception,
                                  "Sink in chain after deposit source restrictor receives wrong asset: ${S}",
                                  ("S", badsink.s));
            case bad_sink::remote_sink:
               FC_THROW_EXCEPTION(fc::assert_exception,
                                  "Sinks in chain after deposit source restrictor must all be on same tank: ${S}",
                                  ("S", badsink.s));
            }
         }
         FC_ASSERT(!output.is_type<exceeded_max_chain_length>(),
                   "Sink chain after deposit source restrictor is too long");
         FC_ASSERT(!output.is_type<nonexistent_object>(),
                   "A sink in the output chain from deposit source restrictor references a nonexistent object: ${O}",
                   ("O", output.get<nonexistent_object>().object));
         if (output.is_type<const_ref<sink>>())
            FC_ASSERT(output.get<const_ref<sink>>().get().which() == sink::tag<same_tank>::value,
                      "Deposit source restrictor must eventually release to the tank it is attached to");
         ++validator.attachment_counters[tank_attachment::tag<deposit_source_restrictor>::value];
      }
      void operator()(const tap_opener& att) {
         FC_ASSERT(validator.current_tank.taps.contains(att.tap_index), "Tap opener references nonexistent tap");
         if (att.release_amount.which() == asset_flow_limit::tag<share_type>::value)
            FC_ASSERT(att.release_amount.get<share_type>() > 0, "Tap opener release amount must be positive");
         check_sink_asset(att.destination_sink, att.asset_type);
         ++validator.attachment_counters[tank_attachment::tag<tap_opener>::value];
      }
      void operator()(const attachment_connect_authority& att) {
         check_authority(att.connect_authority, "Attachment connect authority");
         FC_ASSERT(validator.current_tank.attachments.contains(att.attachment_id),
                   "Attachment connect authority references nonexistent attachment");
         const tank_attachment& attachment = validator.current_tank.attachments.at(att.attachment_id);
         fc::typelist::runtime::dispatch(tank_attachment::list(), attachment.which(), [&attachment](auto t) {
            FC_ASSERT(attachment.get<typename decltype(t)::type>().receives_asset().valid(),
                      "Attachment connect authority references attachment which does not receive asset");
         });
         ++validator.attachment_counters[tank_attachment::tag<attachment_connect_authority>::value];
      }
   } visitor{*this};

   // Fetch attachment and check for errors while fetching
   FC_ASSERT(current_tank.attachments.contains(attachment_id), "Specified tank attachment does not exist; ID: ${ID}",
             ("ID", attachment_id));
   CHECK_ATTACHMENT_RESULT();
   const auto& attachment = lookup_attachment(attachment_id_type{optional<tank_id_type>(), attachment_id});
   if (attachment.is_type<nonexistent_object>())
      FC_THROW_EXCEPTION(fc::assert_exception,
                         "Nonexistent object referenced while looking up tank attachment: ${E}",
                         ("E", attachment.get<nonexistent_object>()));
   if (attachment.is_type<need_lookup_function>()) return;

   // Visit the attachment to validate it
   attachment.get<const_ref<tank_attachment>>().get().visit(visitor);
}

void tank_validator::validate_tap_requirement(index_type tap_id, index_type requirement_index) {
   // Define a visitor that examines each attachment type
   struct {
      using result_type = void;
      tank_validator& validator;

      void check_meter(const attachment_id_type& id, const string& name_for_errors,
                       optional<asset_id_type> asset_type = {}) {
         CHECK_ATTACHMENT_RESULT();
         const auto& attachment_result = validator.lookup_attachment(id);
         FC_ASSERT(!attachment_result.is_type<nonexistent_object>(),
                   "Nonexistent object (${O}) referenced while looking up meter for " + name_for_errors,
                   ("O", attachment_result.get<nonexistent_object>().object));
         if (attachment_result.is_type<const_ref<tank_attachment>>()) {
            const auto& attachment = attachment_result.get<const_ref<tank_attachment>>().get();
            FC_ASSERT(attachment.is_type<asset_flow_meter>(),
                      + name_for_errors + " references attachment which is not a meter");
            if (asset_type.valid())
               FC_ASSERT(attachment.get<asset_flow_meter>().asset_type == *asset_type,
                         + name_for_errors + " references meter which accepts incorrect asset type");
         }
      }

      void operator()(const immediate_flow_limit& req) {
         FC_ASSERT(req.limit > 0, "Immediate flow limit must be positive");
         ++validator.requirement_counters[tap_requirement::tag<immediate_flow_limit>::value];
      }
      void operator()(const cumulative_flow_limit& req) {
         FC_ASSERT(req.limit > 0, "Cumulative flow limit must be positive");
         check_meter(req.meter_id, "Cumulative flow limit", validator.current_tank.asset_type);
         ++validator.requirement_counters[tap_requirement::tag<cumulative_flow_limit>::value];
      }
      void operator()(const periodic_flow_limit& req) {
         FC_ASSERT(req.limit > 0, "Periodic flow limit must be positive");
         check_meter(req.meter_id, "Periodic flow limit", validator.current_tank.asset_type);
         ++validator.requirement_counters[tap_requirement::tag<periodic_flow_limit>::value];
      }
      void operator()(const time_lock& req) {
         FC_ASSERT(!req.lock_unlock_times.empty(), "Time lock must specify at least one lock/unlock time");
         ++validator.requirement_counters[tap_requirement::tag<time_lock>::value];
      }
      void operator()(const minimum_tank_level& req) {
         FC_ASSERT(req.minimum_level > 0, "Minimum tank level must be positive");
         ++validator.requirement_counters[tap_requirement::tag<minimum_tank_level>::value];
      }
      void operator()(const review_requirement& req) {
         check_authority(req.reviewer, "Reviewer");
         ++validator.requirement_counters[tap_requirement::tag<review_requirement>::value];
      }
      void operator()(const documentation_requirement&) {
         /* no checks */
         ++validator.requirement_counters[tap_requirement::tag<documentation_requirement>::value];
      }
      void operator()(const delay_requirement& req) {
         if (req.veto_authority.valid())
            check_authority(*req.veto_authority, "Veto authority");
         FC_ASSERT(req.delay_period_sec > 0, "Delay period must be positive");
         ++validator.requirement_counters[tap_requirement::tag<delay_requirement>::value];
      }
      void operator()(const hash_lock& req) {
         fc::typelist::runtime::dispatch(hash_lock::hash_type::list(), req.hash.which(), [&req](auto t) {
            using hash_type = typename decltype(t)::type;
            FC_ASSERT(req.hash.get<hash_type>() != hash_type(), "Hash lock must not be null hash");
         });
         if (req.preimage_size)
            FC_ASSERT(*req.preimage_size > 0, "Hash lock preimage size must be positive");
         ++validator.requirement_counters[tap_requirement::tag<hash_lock>::value];
      }
      void operator()(const ticket_requirement& req) {
         FC_ASSERT(req.ticket_signer != public_key_type(), "Ticket signer must not be null public key");
         ++validator.requirement_counters[tap_requirement::tag<ticket_requirement>::value];
      }
      void operator()(const exchange_requirement& req) {
         check_meter(req.meter_id, "Exchange requirement");
         FC_ASSERT(req.tick_amount > 0, "Exchange requirement tick amount must be positive");
         FC_ASSERT(req.release_per_tick > 0, "Exchange requirement release amount must be positive");
         ++validator.requirement_counters[tap_requirement::tag<exchange_requirement>::value];
      }
   } visitor{*this};

   // Fetch attachment and check for errors while fetching
   FC_ASSERT(current_tank.taps.contains(tap_id), "Specified tap does not exist; ID: ${ID}",
             ("ID", tap_id));
   FC_ASSERT(current_tank.taps.at(tap_id).requirements.size() > requirement_index,
             "Specified tap requirement does not exist; Tap: ${T}, Requirement: ${R}",
             ("T", tap_id)("R", requirement_index));
   const auto& requirement = current_tank.taps.at(tap_id).requirements.at(requirement_index);

   // Visit the requirement to validate it
   requirement.visit(visitor);
}

void tank_validator::validate_tap(index_type tap_id) {
   FC_ASSERT(current_tank.taps.contains(tap_id), "Requested tap does not exist");
   const auto& tap = current_tank.taps.at(tap_id);
   FC_ASSERT(tap.connected_sink.valid() || tap.connect_authority.valid(),
             "Tap must be connected, or specify a connect authority");
   for (index_type i = 0; i < tap.requirements.size(); ++i)
       validate_tap_requirement(tap_id, i);
}

void tank_validator::validate_emergency_tap() {
   FC_ASSERT(current_tank.taps.contains(0), "Emergency tap does not exist");
   const auto& tap = current_tank.taps.at(0);
   FC_ASSERT(tap.requirements.empty(), "Emergency tap must have no tap requirements");
   FC_ASSERT(tap.open_authority.valid(), "Emergency tap must specify an open authority");
   FC_ASSERT(tap.connect_authority.valid(), "Emergency tap must specify a connect authority");
   FC_ASSERT(tap.destructor_tap == true, "Emergency tap must be a destructor tap");
}

void tank_validator::validate_tank() {
   validate_emergency_tap();
   for (auto tap_pair : current_tank.taps)
      validate_tap(tap_pair.first);
}

} } } // namespace graphene::protocol::tnt
