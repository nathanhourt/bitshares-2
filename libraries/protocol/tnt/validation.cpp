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

struct internal_attachment_checker {
   using result_type = void;

   void operator()(const asset_flow_meter&) const {}
   void operator()(const deposit_source_restrictor& att) const {
      FC_ASSERT(att.legal_deposit_paths.size() > 0,
                "Deposit source restrictor must accept at least one deposit path");
      using path_pattern = deposit_source_restrictor::deposit_path_pattern;
      using wildcard_element = deposit_source_restrictor::wildcard_sink;
      std::for_each(att.legal_deposit_paths.begin(), att.legal_deposit_paths.end(),
                    [](const path_pattern& path) { try {
         FC_ASSERT(path.size() > 1,
                   "Deposit path patterns must contain at least two elements for a source, and a destination");
         if (!path.front().is_type<wildcard_element>())
            FC_ASSERT(is_terminal_sink(path.front().get<sink>()),
                      "Deposit path patterns must begin with a terminal sink or a wildcard");
         if (!path.back().is_type<wildcard_element>()) {
            const sink& final_sink = path.back().get<sink>();
            FC_ASSERT(is_terminal_sink(final_sink),
                      "Deposit path patterns must end with a terminal sink or a wildcard");
            FC_ASSERT(final_sink.is_type<same_tank>() || final_sink.is_type<tank_id_type>(),
                      "Deposit path patterns must end with the current tank or a wildcard");
         }
         if (path.size() < 3)
            FC_ASSERT(!path.front().is_type<wildcard_element>(),
                      "A single wildcard is not a valid deposit source restrictor pattern");
         for (size_t i = 0; i < path.size(); ++i) {
            using wildcard = deposit_source_restrictor::wildcard_sink;
            if (i > 0 && path[i].is_type<wildcard>() && path[i-1].is_type<wildcard>())
               FC_ASSERT(!path[i].get<wildcard>().repeatable && !path[i-1].get<wildcard>().repeatable,
                         "A repeatable wildcard in a deposit path pattern cannot be adjacent to another wildcard");
         }
      } FC_CAPTURE_AND_RETHROW((path)) });
   }
   void operator()(const tap_opener& att) const {
      if (att.release_amount.which() == asset_flow_limit::tag<share_type>::value)
         FC_ASSERT(att.release_amount.get<share_type>() > 0, "Tap opener release amount must be positive");
   }
   void operator()(const attachment_connect_authority& att) const {
      check_authority(att.connect_authority, "Attachment connect authority");
   }
};

struct internal_requirement_checker {
   using result_type = void;

   void operator()(const immediate_flow_limit& req) const {
      FC_ASSERT(req.limit > 0, "Immediate flow limit must be positive");
   }
   void operator()(const cumulative_flow_limit& req) const {
      FC_ASSERT(req.limit > 0, "Cumulative flow limit must be positive");
   }
   void operator()(const periodic_flow_limit& req) const {
      FC_ASSERT(req.limit > 0, "Periodic flow limit must be positive");
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
   void operator()(const documentation_requirement&) const {}
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
      FC_ASSERT(req.tick_amount > 0, "Exchange requirement tick amount must be positive");
      FC_ASSERT(req.release_per_tick > 0, "Exchange requirement release amount must be positive");
   }
};

struct impacted_accounts_visitor {
   flat_set<account_id_type>& accounts;

   // Sink
   void operator()(const sink& s) const {
      if (s.is_type<account_id_type>()) accounts.insert(s.get<account_id_type>());
   }

   // Tank attachments
   void operator()(const asset_flow_meter& afm) const { (*this)(afm.destination_sink); }
   void operator()(const deposit_source_restrictor& dsr) const {
      for (const auto& pattern : dsr.legal_deposit_paths)
         for (const auto& element : pattern)
            if (element.is_type<sink>()) (*this)(element.get<sink>());
   }
   void operator()(const tap_opener& top) const { (*this)(top.destination_sink); }
   void operator()(const attachment_connect_authority& aca) const {
      add_authority_accounts(accounts, aca.connect_authority);
   }

   // Tap requirements
   void operator()(const immediate_flow_limit&) const {}
   void operator()(const cumulative_flow_limit&) const {}
   void operator()(const periodic_flow_limit&) const {}
   void operator()(const time_lock&) const {}
   void operator()(const minimum_tank_level&) const {}
   void operator()(const review_requirement& rreq) const { add_authority_accounts(accounts, rreq.reviewer); }
   void operator()(const documentation_requirement&) const {}
   void operator()(const delay_requirement& dreq) const {
      if (dreq.veto_authority.valid())
         add_authority_accounts(accounts, *dreq.veto_authority);
   }
   void operator()(const hash_lock&) const {}
   void operator()(const ticket_requirement&) const {}
   void operator()(const exchange_requirement&) const {}

   // Accessory containers
   void operator()(const tap_requirement& treq) const {
      fc::typelist::runtime::dispatch(tap_requirement::list(), treq.which(), [this, &treq](auto t) {
         (*this)(treq.get<typename decltype(t)::type>());
      });
   }
   void operator()(const tank_attachment& tatt) const {
      fc::typelist::runtime::dispatch(tank_attachment::list(), tatt.which(), [this, &tatt](auto t) {
         (*this)(tatt.get<typename decltype(t)::type>());
      });
   }

   // Taps
   void operator()(const tap& tap) const {
      if (tap.open_authority.valid()) add_authority_accounts(accounts, *tap.open_authority);
      if (tap.connect_authority.valid()) add_authority_accounts(accounts, *tap.connect_authority);
      if (tap.connected_sink.valid()) (*this)(*tap.connected_sink);
      for (const auto& req : tap.requirements) (*this)(req);
   }
};

void tank_validator::validate_attachment(index_type attachment_id) {
   // Define a visitor that examines each attachment type
   struct {
      using result_type = void;
      tank_validator& validator;

      // Helper function: Verify that the provided sink accepts the provided asset
      void check_sink_asset(const sink& s, asset_id_type a) {
         CHECK_SINK_ASSET_RESULT();
         auto asset_result = validator.get_sink_asset(s);
         if (asset_result.is_type<no_asset>())
            FC_THROW_EXCEPTION(fc::assert_exception, "Flow meter destination sink cannot receive asset: ${S}",
                               ("S", s));
         if (asset_result.is_type<nonexistent_object>())
            FC_THROW_EXCEPTION(fc::assert_exception, "Flow meter destination sink does not exist: ${E}",
                               ("E", asset_result.get<nonexistent_object>()));
         if (asset_result.is_type<asset_id_type>())
            FC_ASSERT(asset_result.get<asset_id_type>() == a, "Flow meter destination sink accepts wrong asset type");
      }

      // vvvv THE ACTUAL ATTACHMENT VALIDATORS vvvv
      void operator()(const asset_flow_meter& att) {
         internal_attachment_checker()(att);
         check_sink_asset(att.destination_sink, att.asset_type);
         ++validator.attachment_counters[tank_attachment::tag<asset_flow_meter>::value];
      }
      void operator()(const deposit_source_restrictor& att) {
         internal_attachment_checker()(att);
         using path_pattern = deposit_source_restrictor::deposit_path_pattern;
         using wildcard_element = deposit_source_restrictor::wildcard_sink;
         std::for_each(att.legal_deposit_paths.begin(), att.legal_deposit_paths.end(),
                       [this](const path_pattern& path) { try {
            if (!path.back().is_type<wildcard_element>()) {
               const sink& final_sink = path.back().get<sink>();
               if (final_sink.is_type<tank_id_type>())
                  FC_ASSERT(validator.tank_id.valid() && final_sink.get<tank_id_type>() == *validator.tank_id,
                            "Deposit path patterns must end with the current tank or a wildcard");
            }
         } FC_CAPTURE_AND_RETHROW((path)) });
         ++validator.attachment_counters[tank_attachment::tag<deposit_source_restrictor>::value];
      }
      void operator()(const tap_opener& att) {
         internal_attachment_checker()(att);
         FC_ASSERT(validator.current_tank.taps.contains(att.tap_index), "Tap opener references nonexistent tap");
         check_sink_asset(att.destination_sink, att.asset_type);
         ++validator.attachment_counters[tank_attachment::tag<tap_opener>::value];
      }
      void operator()(const attachment_connect_authority& att) {
         internal_attachment_checker()(att);
         FC_ASSERT(validator.current_tank.attachments.contains(att.attachment_id),
                   "Attachment connect authority references nonexistent attachment");
         const tank_attachment& attachment = validator.current_tank.attachments.at(att.attachment_id);
         fc::typelist::runtime::dispatch(tank_attachment::list(), attachment.which(), [&attachment](auto t) {
            FC_ASSERT(attachment.get<typename decltype(t)::type>().receives_asset().valid(),
                      "Attachment connect authority references attachment which does not receive asset");
         });
         ++validator.attachment_counters[tank_attachment::tag<attachment_connect_authority>::value];
      }
      // ^^^^ THE ACTUAL ATTACHMENT VALIDATORS ^^^^
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

      // Helper function: Check that the provided attachment is a meter and, optionally, that it takes specified asset
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

      // vvvv THE ACTUAL TAP REQUIREMENT VALIDATORS vvvv
      void operator()(const immediate_flow_limit& req) {
         internal_requirement_checker()(req);
         ++validator.requirement_counters[tap_requirement::tag<immediate_flow_limit>::value];
      }
      void operator()(const cumulative_flow_limit& req) {
         internal_requirement_checker()(req);
         check_meter(req.meter_id, "Cumulative flow limit", validator.current_tank.asset_type);
         ++validator.requirement_counters[tap_requirement::tag<cumulative_flow_limit>::value];
      }
      void operator()(const periodic_flow_limit& req) {
         internal_requirement_checker()(req);
         check_meter(req.meter_id, "Periodic flow limit", validator.current_tank.asset_type);
         ++validator.requirement_counters[tap_requirement::tag<periodic_flow_limit>::value];
      }
      void operator()(const time_lock& req) {
         internal_requirement_checker()(req);
         ++validator.requirement_counters[tap_requirement::tag<time_lock>::value];
      }
      void operator()(const minimum_tank_level& req) {
         internal_requirement_checker()(req);
         ++validator.requirement_counters[tap_requirement::tag<minimum_tank_level>::value];
      }
      void operator()(const review_requirement& req) {
         internal_requirement_checker()(req);
         ++validator.requirement_counters[tap_requirement::tag<review_requirement>::value];
      }
      void operator()(const documentation_requirement& req) {
         internal_requirement_checker()(req);
         ++validator.requirement_counters[tap_requirement::tag<documentation_requirement>::value];
      }
      void operator()(const delay_requirement& req) {
         internal_requirement_checker()(req);
         ++validator.requirement_counters[tap_requirement::tag<delay_requirement>::value];
      }
      void operator()(const hash_lock& req) {
         internal_requirement_checker()(req);
         ++validator.requirement_counters[tap_requirement::tag<hash_lock>::value];
      }
      void operator()(const ticket_requirement& req) {
         internal_requirement_checker()(req);
         ++validator.requirement_counters[tap_requirement::tag<ticket_requirement>::value];
      }
      void operator()(const exchange_requirement& req) {
         check_meter(req.meter_id, "Exchange requirement");
         internal_requirement_checker()(req);
         ++validator.requirement_counters[tap_requirement::tag<exchange_requirement>::value];
      }
      // ^^^^ THE ACTUAL TAP REQUIREMENT VALIDATORS ^^^^
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

void tank_validator::check_tap_connection(index_type tap_id) const {
   FC_ASSERT(current_tank.taps.contains(tap_id), "Requested tap does not exist");
   const auto& tap = current_tank.taps.at(tap_id);
   // If tap is connected...
   if (tap.connected_sink.valid()) {
      // ...get the sink chain it connects to
      CHECK_SINK_CHAIN_RESULT();
      auto sink_chain = get_sink_chain(*tap.connected_sink, max_sink_chain_length, current_tank.asset_type);

      // Check error conditions
      FC_ASSERT(!sink_chain.is_type<exceeded_max_chain_length>(),
                "Tap connects to sink chain which exceeds maximum length limit");
      if (sink_chain.is_type<bad_sink>()) {
         bad_sink bs = sink_chain.get<bad_sink>();
         if (bs.reason == bad_sink::receives_no_asset)
            FC_THROW_EXCEPTION(fc::assert_exception,
                               "Tap connects to sink chain with a sink that cannot receive asset; sink: ${S}",
                               ("S", bs.s));
         if (bs.reason == bad_sink::receives_wrong_asset)
            FC_THROW_EXCEPTION(fc::assert_exception,
                               "Tap connects to sink chain with a sink that receives wrong asset; sink: ${S}",
                               ("S", bs.s));
         FC_THROW_EXCEPTION(fc::assert_exception,
                            "Tap connects to sink chain that failed validation for an unknown reason. "
                            "Please report this error. Bad sink: ${S}", ("S", bs));
      }
      FC_ASSERT(!sink_chain.is_type<nonexistent_object>(),
                "Tap connects to sink chain which references nonexistent object: ${O}",
                ("O", sink_chain.get<nonexistent_object>()));

      // No error, so it should be a real sink chahin
      if (sink_chain.is_type<tnt::sink_chain>()) {
         auto& real_sink_chain = sink_chain.get<tnt::sink_chain>();
         // Sanity check (even if the tap deposits directly to a tank, the sink chain has that tank in it)
         FC_ASSERT(!real_sink_chain.sinks.empty(),
                   "LOGIC ERROR: Tap is connected, but sink chain is empty. Please report this error.");

         // Find out if final sink is a tank (could be a tank ID or a same_tank)
         fc::optional<tank_id_type> dest_tank_id;
         const sink& final_sink = real_sink_chain.sinks.back().get();
         if (final_sink.is_type<same_tank>())
            dest_tank_id = real_sink_chain.final_sink_tank;
         else if (final_sink.is_type<tank_id_type>())
            dest_tank_id = final_sink.get<tank_id_type>();

         // If final sink *is* a tank...
         if (dest_tank_id.valid()) {
            // ...look it up, check error conditions...
            CHECK_TANK_RESULT();
            auto dest_tank = lookup_tank(dest_tank_id);
            FC_ASSERT(!dest_tank.is_type<nonexistent_object>(),
                      "Tap connects to sink chain that references a nonexistent object: ${O}",
                      ("O", dest_tank.get<nonexistent_object>().object));
            if (dest_tank.is_type<const_ref<tank_schematic>>()) {
               const auto& dest_schema = dest_tank.get<const_ref<tank_schematic>>().get();
               // ...and see if it has a deposit_source_restrictor. If it does, check the deposit path is legal
               if (const deposit_source_restrictor* restrictor = dest_schema.get_deposit_source_restrictor()) {
                  deposit_source_restrictor::deposit_path path;
                  // If we know the ID of the tank we're validating, that's the deposit origin. If not, oh well.
                  if (tank_id.valid())
                     path.origin = *tank_id;
                  path.sink_chain = std::move(real_sink_chain.sinks);
                  auto matching_path = restrictor->get_matching_deposit_path(path, dest_tank_id);
                  FC_ASSERT(matching_path.valid(), "Tap connects to destination tank, but is not accepted by "
                                                   "destination's deposit source restrictor");
               }
            }
         }
      }
      // Should never get here (sink_chain result was an unhandled type)
      FC_THROW_EXCEPTION(fc::assert_exception,
                         "LOGIC ERROR: Unhandled sink chain result type. Please report this error.");
   }
}

void tank_validator::get_referenced_accounts(flat_set<account_id_type>& accounts) const {
   for (const auto& tap_pair : current_tank.taps) get_referenced_accounts(accounts, tap_pair.second);
   for (const auto& att_pair : current_tank.attachments) get_referenced_accounts(accounts, att_pair.second);
}

void tank_validator::get_referenced_accounts(flat_set<account_id_type>& accounts, const tap& tap) {
   impacted_accounts_visitor check{accounts};
   check(tap);
}

void tank_validator::get_referenced_accounts(flat_set<account_id_type>& accounts, const tank_attachment& att) {
   impacted_accounts_visitor check{accounts};
   check(att);
}

void tank_validator::validate_tap(index_type tap_id) {
   FC_ASSERT(current_tank.taps.contains(tap_id), "Requested tap does not exist");
   const auto& tap = current_tank.taps.at(tap_id);
   FC_ASSERT(tap.connected_sink.valid() || tap.connect_authority.valid(),
             "Tap must be connected, or specify a connect authority");

   // Check tap requirements
   for (index_type i = 0; i < tap.requirements.size(); ++i) try {
      validate_tap_requirement(tap_id, i);
   } FC_CAPTURE_AND_RETHROW((tap_id)(i))

   // If connected, check sink validity
   try {
      check_tap_connection(tap_id);
   } FC_CAPTURE_AND_RETHROW((tap_id))
}

void tank_validator::validate_emergency_tap() {
   FC_ASSERT(current_tank.taps.contains(0), "Emergency tap does not exist");
   validate_emergency_tap(current_tank.taps.at(0));
}

void tank_validator::validate_tank() {
   // Validate attachments first because taps may connect to them, and we should be sure they're internally valid
   // by the time that happens.
   for (const auto& attachment_pair : current_tank.attachments) try {
      validate_attachment(attachment_pair.first);
   } FC_CAPTURE_AND_RETHROW((attachment_pair.first))
   validate_emergency_tap();
   for (const auto& tap_pair : current_tank.taps) try {
      validate_tap(tap_pair.first);
   } FC_CAPTURE_AND_RETHROW((tap_pair.first))
}

void tank_validator::validate_attachment(const tank_attachment &att) {
   internal_attachment_checker checker;
   att.visit(checker);
}

void tank_validator::validate_tap_requirement(const tap_requirement &req) {
   internal_requirement_checker checker;
   req.visit(checker);
}

void tank_validator::validate_tap(const tap& tap) {
   FC_ASSERT(tap.connected_sink.valid() || tap.connect_authority.valid(),
             "Tap must be connected, or specify a connect authority");
   for (const auto& req : tap.requirements) validate_tap_requirement(req);
}

void tank_validator::validate_emergency_tap(const tap& etap) {
   FC_ASSERT(etap.requirements.empty(), "Emergency tap must have no tap requirements");
   FC_ASSERT(etap.open_authority.valid(), "Emergency tap must specify an open authority");
   FC_ASSERT(etap.connect_authority.valid(), "Emergency tap must specify a connect authority");
   FC_ASSERT(etap.destructor_tap == true, "Emergency tap must be a destructor tap");
}

} } } // namespace graphene::protocol::tnt
