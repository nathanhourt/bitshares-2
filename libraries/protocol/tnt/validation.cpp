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

#include <fc/io/raw.hpp>

#include "type_checks.hxx"

namespace graphene { namespace protocol { namespace tnt {

void check_authority(const authority& auth, const string& name_for_errors) {
   FC_ASSERT(!auth.is_impossible(), + name_for_errors + " must not be impossible authority");
   FC_ASSERT(!(auth == authority::null_authority()), + name_for_errors + " must not be null authority");
}

struct internal_attachment_checker {
   optional<tank_id_type> my_id;

   // Check an authorized_connections_type for authorizations of sources on the same tank
   void operator()(const authorized_connections_type& sources) const {
      if (!my_id.valid())
         return;
      if (sources.is_type<flat_set<remote_connection>>()) {
         const auto& cons = sources.get<flat_set<remote_connection>>();
         for (const auto& con : cons) {
            if (con.is_type<tank_id_type>())
               FC_ASSERT(con.get<tank_id_type>() != *my_id,
                         "Cannot authorize connections from the same tank -- these are allowed implicitly");
            else if (con.is_type<attachment_id_type>())
               FC_ASSERT(con.get<attachment_id_type>().tank_id != my_id,
                         "Cannot authorize connections from the same tank -- these are allowed implicitly");
         }
      }
   }

   void operator()(const asset_flow_meter& att) const { (*this)(att.remote_sources); }
   void operator()(const tap_opener& att) const {
      if (att.release_amount.which() == asset_flow_limit::tag<share_type>::value)
         FC_ASSERT(att.release_amount.get<share_type>() > 0, "Tap opener release amount must be positive");
      (*this)(att.remote_sources);
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
      FC_ASSERT(req.period_duration_sec > 0, "Periodic flow limit period must be positive");
   }
   void operator()(const time_lock& req) const {
      FC_ASSERT(!req.lock_unlock_times.empty(), "Time lock must specify at least one lock/unlock time");
      for (size_t i = 1; i < req.lock_unlock_times.size(); ++i)
         FC_ASSERT(req.lock_unlock_times[i-1] < req.lock_unlock_times[i],
                   "Time lock times must be unique and strictly increasing");
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
   void operator()(const hash_preimage_requirement& req) const {
      fc::typelist::runtime::dispatch(hash_preimage_requirement::hash_type::list(), req.hash.which(), [&req](auto t) {
         using hash_type = typename decltype(t)::type;
         FC_ASSERT(req.hash.get<hash_type>() != hash_type(), "Hash lock must not be null hash");
         FC_ASSERT(req.hash.get<hash_type>() != hash_type::hash(vector<char>()),
                   "Hash lock must not be hash of empty value");
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

   // Connection
   void operator()(const connection& c) const {
      if (c.is_type<account_id_type>()) accounts.insert(c.get<account_id_type>());
   }
   void operator()(const authorized_connections_type& sources) const {
      if (sources.is_type<flat_set<remote_connection>>()) {
         const auto& cons = sources.get<flat_set<remote_connection>>();
         for (const auto& con : cons)
            if (con.is_type<account_id_type>())
               accounts.insert(con.get<account_id_type>());
      }
   }

   // Tank attachments
   void operator()(const asset_flow_meter& afm) const { (*this)(afm.destination); }
   void operator()(const tap_opener& top) const { (*this)(top.destination); }
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
   void operator()(const hash_preimage_requirement&) const {}
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
      if (tap.connected_connection.valid()) (*this)(*tap.connected_connection);
      for (const auto& req : tap.requirements) (*this)(req);
   }
};

void tank_validator::validate_attachment(index_type attachment_id) {
   // Define a visitor that examines each attachment type
   struct {
      tank_validator& validator;

      // Helper function: Verify that the provided connection accepts the provided asset
      void check_connection_asset(const connection& s, asset_id_type a) {
         CHECK_CONNECTION_ASSET_RESULT();
         auto asset_result = validator.get_connection_asset(s);
         if (asset_result.is_type<no_asset>())
            FC_THROW_EXCEPTION(fc::assert_exception, "Flow meter destination connection cannot receive asset: ${S}",
                               ("S", s));
         if (asset_result.is_type<nonexistent_object>())
            FC_THROW_EXCEPTION(fc::assert_exception, "Flow meter destination connection does not exist: ${E}",
                               ("E", asset_result.get<nonexistent_object>()));
         if (asset_result.is_type<asset_id_type>())
            FC_ASSERT(asset_result.get<asset_id_type>() == a,
                      "Flow meter destination connection accepts wrong asset type");
      }

      // vvvv THE ACTUAL ATTACHMENT VALIDATORS vvvv
      void operator()(const asset_flow_meter& att) {
         internal_attachment_checker()(att);
         check_connection_asset(att.destination, att.asset_type);
         ++validator.attachment_counters[tank_attachment::tag<asset_flow_meter>::value];
      }
      void operator()(const tap_opener& att) {
         internal_attachment_checker()(att);
         FC_ASSERT(validator.current_tank.taps.count(att.tap_index) > 0, "Tap opener references nonexistent tap");
         check_connection_asset(att.destination, att.asset_type);
         ++validator.attachment_counters[tank_attachment::tag<tap_opener>::value];
      }
      void operator()(const attachment_connect_authority& att) {
         internal_attachment_checker()(att);
         FC_ASSERT(validator.current_tank.attachments.count(att.attachment_id) > 0,
                   "Attachment connect authority references nonexistent attachment");
         const tank_attachment& attachment = validator.current_tank.attachments.at(att.attachment_id);
         fc::typelist::runtime::dispatch(tank_attachment::list(), attachment.which(), [](auto t) {
            FC_ASSERT(decltype(t)::type::can_receive_asset,
                      "Attachment connect authority references attachment which does not receive asset");
         });
         ++validator.attachment_counters[tank_attachment::tag<attachment_connect_authority>::value];
      }
      // ^^^^ THE ACTUAL ATTACHMENT VALIDATORS ^^^^
   } visitor{*this};

   // Fetch attachment and check for errors while fetching
   FC_ASSERT(current_tank.attachments.count(attachment_id) > 0, "Specified tank attachment does not exist; ID: ${ID}",
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
         ++validator.requirement_counters[tap_requirement::tag<cumulative_flow_limit>::value];
      }
      void operator()(const periodic_flow_limit& req) {
         internal_requirement_checker()(req);
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
      void operator()(const hash_preimage_requirement& req) {
         internal_requirement_checker()(req);
         ++validator.requirement_counters[tap_requirement::tag<hash_preimage_requirement>::value];
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
   FC_ASSERT(current_tank.taps.count(tap_id) > 0, "Specified tap does not exist; ID: ${ID}",
             ("ID", tap_id));
   FC_ASSERT(current_tank.taps.at(tap_id).requirements.size() > requirement_index,
             "Specified tap requirement does not exist; Tap: ${T}, Requirement: ${R}",
             ("T", tap_id)("R", requirement_index));
   const auto& requirement = current_tank.taps.at(tap_id).requirements.at(requirement_index);

   // Visit the requirement to validate it
   requirement.visit(visitor);
}

template<typename Attachment>
struct receives_asset { constexpr static bool value = Attachment::can_receive_asset; };
// A filtered container of attachments, only the ones that can receive asset
using asset_attachments = TL::apply<TL::filter<tank_attachment::list, receives_asset>, static_variant>;

void tank_validator::check_tap_connection(index_type tap_id) const {
   FC_ASSERT(current_tank.taps.count(tap_id) > 0, "Requested tap does not exist");
   const auto& tap = current_tank.taps.at(tap_id);
   // If tap is connected...
   if (tap.connected_connection.valid()) {
      // ...get the connection chain it connects to
      CHECK_CONNECTION_CHAIN_RESULT();
      auto connection_chain = get_connection_chain(*tap.connected_connection, max_connection_chain_length,
                                                   current_tank.asset_type);

      // Check error conditions
      FC_ASSERT(!connection_chain.is_type<exceeded_max_chain_length>(),
                "Tap connects to connection chain which exceeds maximum length limit");
      if (connection_chain.is_type<bad_connection>()) {
         bad_connection bs = connection_chain.get<bad_connection>();
         if (bs.reason == bad_connection::receives_no_asset)
            FC_THROW_EXCEPTION(fc::assert_exception,
                               "Tap connects to connection chain with a connection that cannot receive asset; "
                               "connection: ${S}",
                               ("S", bs.s));
         if (bs.reason == bad_connection::receives_wrong_asset)
            FC_THROW_EXCEPTION(fc::assert_exception,
                               "Tap connects to connection chain with a connection that receives wrong asset; "
                               "connection: ${S}",
                               ("S", bs.s));
         FC_THROW_EXCEPTION(fc::assert_exception,
                            "Tap connects to connection chain that failed validation for an unknown reason. "
                            "Please report this error. Bad connection: ${S}", ("S", bs));
      }
      FC_ASSERT(!connection_chain.is_type<nonexistent_object>(),
                "Tap connects to connection chain which references nonexistent object: ${O}",
                ("O", connection_chain.get<nonexistent_object>()));

      // Check that connection_chain is a real connection chain (this should never fail unless the type changes)
      FC_ASSERT(connection_chain.is_type<tnt::connection_chain>(),
                "LOGIC ERROR: Unhandled connection chain result type. Please report this error.",
                ("chain type", connection_chain.which()));
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
   FC_ASSERT(current_tank.taps.count(tap_id) > 0, "Requested tap does not exist");
   const auto& tap = current_tank.taps.at(tap_id);
   FC_ASSERT(tap.connected_connection.valid() || tap.connect_authority.valid(),
             "Tap must be connected, or specify a connect authority");

   // Check tap requirements
   uniqueness_checker<tap_requirement> is_unique;
   for (index_type i = 0; i < tap.requirements.size(); ++i) try {
      FC_ASSERT(is_unique.check_tag(tap.requirements[i].which()),
                "Tap requirements of type [${T}] must be unique per tap",
                ("T", tap.requirements[i].content_typename()));
      validate_tap_requirement(tap_id, i);
   } FC_CAPTURE_AND_RETHROW((tap_id)(i))

   // If connected, check connection validity
   try {
      check_tap_connection(tap_id);
   } FC_CAPTURE_AND_RETHROW((tap_id))
}

void tank_validator::validate_emergency_tap() {
   FC_ASSERT(current_tank.taps.count(0) > 0, "Emergency tap does not exist");
   validate_emergency_tap(current_tank.taps.at(0));
}

void tank_validator::validate_tank() {
   // Validate attachments first because taps may connect to them, and we should be sure they're internally valid
   // by the time that happens.
   uniqueness_checker<tank_attachment> is_unique;
   for (const auto& attachment_pair : current_tank.attachments) try {
      FC_ASSERT(is_unique.check_tag(attachment_pair.second.which()),
                "Tank attachments of type [${T}] must be unique per tank",
                ("T", attachment_pair.second.content_typename()));
      validate_attachment(attachment_pair.first);
   } FC_CAPTURE_AND_RETHROW((attachment_pair.first))
   validate_emergency_tap();
   for (const auto& tap_pair : current_tank.taps) try {
      validate_tap(tap_pair.first);
   } FC_CAPTURE_AND_RETHROW((tap_pair.first))
   has_validated = true;
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
   FC_ASSERT(tap.connected_connection.valid() || tap.connect_authority.valid(),
             "Tap must be connected, or specify a connect authority");
   uniqueness_checker<tap_requirement> is_unique;
   for (const auto& req : tap.requirements) {
      FC_ASSERT(is_unique.check_tag(req.which()), "Tap requirements of type [${T}] must be unique per tap",
                ("T", req.content_typename()));
      validate_tap_requirement(req);
   }
}

void tank_validator::validate_emergency_tap(const tap& etap) {
   FC_ASSERT(etap.requirements.empty(), "Emergency tap must have no tap requirements");
   FC_ASSERT(etap.open_authority.valid(), "Emergency tap must specify an open authority");
   FC_ASSERT(etap.open_authority->weight_threshold > 0, "Emergency tap open authority must not be trivial");
   check_authority(*etap.open_authority, "Emergency tap open authority");
   FC_ASSERT(etap.connect_authority.valid(), "Emergency tap must specify a connect authority");
   check_authority(*etap.connect_authority, "Emergency tap connect authority");
   FC_ASSERT(etap.destructor_tap == true, "Emergency tap must be a destructor tap");
}

share_type tank_validator::calculate_deposit(const parameters_type& parameters) const {
   FC_ASSERT(has_validated, "Cannot calculate deposit before tank has been validated. Run validate_tank() first");
   share_type total_deposit = parameters.tank_deposit;
   auto attachment_to_accessory_map = TL::runtime::make_array<size_t>(
               TL::filter_index_map<tank_accessory_list, tank_attachment_filter::filter>());
   auto requirement_to_accessory_map = TL::runtime::make_array<size_t>(
               TL::filter_index_map<tank_accessory_list, tap_requirement_filter::filter>());

   // Helper to get the deposit for the supplied accessory type
   // If the accessory type has a deposit override, the overridden value is returned. Otherwise, returns the supplied
   // default deposit plus the stateful deposit premium if the accessory is stateful.
   auto get_accessory_deposit = [&parameters](size_t type, share_type default_deposit) -> share_type {
      static auto accessory_has_state_map =
              TL::runtime::make_array<bool>(TL::transform<tank_accessory_list,
                                                          TL::transformer_from_filter<impl::has_state_type>
                                                            ::transformer>());
      auto override_itr = parameters.override_deposits.find(type);
      if (override_itr != parameters.override_deposits.end())
         return override_itr->second;
      return default_deposit + (parameters.stateful_accessory_deposit_premium * accessory_has_state_map[type]);
   };

   // Add in deposits on tank attachments
   for (auto tag_count_pair : attachment_counters)
      total_deposit += get_accessory_deposit(attachment_to_accessory_map[size_t(tag_count_pair.first)],
                                             parameters.default_tank_attachment_deposit);
   // Add in deposits on tap requirements
   for (auto tag_count_pair : requirement_counters)
      total_deposit += get_accessory_deposit(requirement_to_accessory_map[size_t(tag_count_pair.first)],
                                             parameters.default_tap_requirement_deposit);

   return total_deposit;
}

share_type tank_validator::calculate_deposit(const tank_schematic& schematic, const parameters_type& parameters) {
   tank_validator val(schematic, parameters.max_connection_chain_length);
   val.validate_tank();
   return val.calculate_deposit(parameters);
}

} } } // namespace graphene::protocol::tnt
