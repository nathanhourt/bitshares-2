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

#include <graphene/protocol/tnt/operations.hpp>
#include <graphene/protocol/tnt/validation.hpp>

#include <fc/io/raw.hpp>

#include <boost/range/adaptor/transformed.hpp>

namespace graphene { namespace protocol {

share_type tank_create_operation::calculate_fee(const fee_parameters_type& params) const {
   return params.base_fee + (fc::raw::pack_size(*this) * params.price_per_byte);
}

void tank_create_operation::validate() const {
   FC_ASSERT(fee.amount > 0, "Must have positive fee");
   FC_ASSERT(deposit_amount > 0, "Must have positive deposit");

   // We don't have access to the real limits here, so check with max connection chain length of 100
   tnt::tank_validator(tnt::tank_schematic::from_create_operation(*this), 100).validate_tank();
}

void tank_create_operation::get_impacted_accounts(flat_set<account_id_type>& impacted) const {
   impacted.insert(payer);
   tnt::tank_validator(tnt::tank_schematic::from_create_operation(*this), 100).get_referenced_accounts(impacted);
}

share_type tank_update_operation::calculate_fee(const fee_parameters_type &params) const {
   return params.base_fee + (fc::raw::pack_size(*this) * params.price_per_byte);
}

void tank_update_operation::validate() const {
   FC_ASSERT(fee.amount > 0, "Must have positive fee");
   FC_ASSERT(taps_to_remove.count(0) == 0, "Emergency tap cannot be removed; it can only be replaced");
   FC_ASSERT(!update_authority.is_impossible(), "Update authority must not be impossible authority");
   FC_ASSERT(!(update_authority == authority::null_authority()), "Update authority must not be null");
   FC_ASSERT(update_authority.weight_threshold > 0, "Update authority must not be trivial");

   { // Check no tap IDs are both removed and replaced
      auto replaced_ids = boost::adaptors::transform(taps_to_replace, [](const auto& pair) { return pair.first; });
      std::set<tnt::index_type> remove_and_replace;
      std::set_intersection(replaced_ids.begin(), replaced_ids.end(), taps_to_remove.begin(), taps_to_remove.end(),
                            std::inserter(remove_and_replace, remove_and_replace.begin()));
      FC_ASSERT(remove_and_replace.empty(), "Cannot both remove and replace the same tap");
   }
   { // Check no attachment IDs are both removed and replaced
      auto replace_ids = boost::adaptors::transform(attachments_to_replace,
                                                    [](const auto& pair) { return pair.first; });
      std::set<tnt::index_type> remove_and_replace;
      std::set_intersection(replace_ids.begin(), replace_ids.end(),
                            attachments_to_remove.begin(), attachments_to_remove.end(),
                            std::inserter(remove_and_replace, remove_and_replace.begin()));
      FC_ASSERT(remove_and_replace.empty(), "Cannot both remove and replace the same attachment");
   }

   if (taps_to_replace.count(0) > 0) tnt::tank_validator::validate_emergency_tap(taps_to_replace.at(0));
   for (const auto& tap_pair : taps_to_replace) tnt::tank_validator::validate_tap(tap_pair.second);
   for (const auto& tap : taps_to_add) tnt::tank_validator::validate_tap(tap);
   tnt::uniqueness_checker<tnt::tank_attachment> is_unique;
   for (const auto& att_pair : attachments_to_replace) {
      FC_ASSERT(is_unique.check_tag(att_pair.second.which()),
                "Tank attachments of type [${T}] must be unique per tank", ("T", att_pair.second.content_typename()));
      tnt::tank_validator::validate_attachment(att_pair.second);
   }
   for (const auto& att : attachments_to_add) {
      FC_ASSERT(is_unique.check_tag(att.which()), "Tank attachments of type [${T}] must be unique per tank",
                ("T", att.content_typename()));
      tnt::tank_validator::validate_attachment(att);
   }
}

void tank_update_operation::get_impacted_accounts(flat_set<account_id_type>& impacted) const {
   impacted.insert(payer);
   add_authority_accounts(impacted, update_authority);

   using Val = tnt::tank_validator;
   for (const auto& tap_pair : taps_to_replace) Val::get_referenced_accounts(impacted, tap_pair.second);
   for (const auto& tap : taps_to_add) Val::get_referenced_accounts(impacted, tap);
   for (const auto& att_pair : attachments_to_replace) Val::get_referenced_accounts(impacted, att_pair.second);
   for (const auto& att : attachments_to_add) Val::get_referenced_accounts(impacted, att);
}

void tank_delete_operation::validate() const {
   FC_ASSERT(fee.amount > 0, "Must have positive fee");
   FC_ASSERT(!delete_authority.is_impossible(), "Delete authority must not be impossible authority");
   FC_ASSERT(!(delete_authority == authority::null_authority()), "Delete authority must not be null");
   FC_ASSERT(delete_authority.weight_threshold > 0, "Delete authority must not be trivial");
}

share_type tank_query_operation::calculate_fee(const fee_parameters_type& params) const {
   return params.base_fee + (fc::raw::pack_size(*this) * params.price_per_byte);
}

struct unique_query_checker {
   std::map<size_t, std::set<tnt::tank_accessory_address_type, tnt::accessory_address_lt<>>> unique_queries;

   bool operator()(const tnt::targeted_query<tnt::queries::documentation_string>&) { return true; }
   template<typename Q>
   bool operator()(const tnt::targeted_query<Q>& q) {
      size_t query_tag = tnt::TL::index_of<tnt::query_type_list, Q>();
      auto& address_set = unique_queries[query_tag];
      auto hint = address_set.lower_bound(q.accessory_address);
      if (hint != address_set.end() && *hint == q.accessory_address)
         return false;
      address_set.insert(hint, q.accessory_address);
      return true;
   }
};

void validate_queries(const vector<tnt::tank_query_type>& queries, const tank_id_type& queried_tank, bool tap_open) {
   unique_query_checker is_unique;
   for (const auto& query : queries) {
      query.visit([&is_unique, tap_open](const auto& q) {
         q.query_content.validate();
         using query_type = typename std::decay_t<decltype(q)>::query_type;
         if (query_type::tap_open_only)
            FC_ASSERT(tap_open, "${Q} may only be used in tap_open_operation, not tank_query_operations",
                      ("Q", fc::get_typename<query_type>::name()));
         if (query_type::unique)
            FC_ASSERT(is_unique(q), "Cannot run multiple ${T} queries against the same target in the same operation",
                      ("T", fc::get_typename<query_type>::name()));
      });

      // Dirty, but I couldn't think of a simpler way to check this
      if (query.is_type<tnt::targeted_query<tnt::queries::redeem_ticket_to_open>>()) {
         const auto& targeted_query = query.get<tnt::targeted_query<tnt::queries::redeem_ticket_to_open>>();
         const auto& ticket = targeted_query.query_content.ticket;
         FC_ASSERT(ticket.tank_ID == queried_tank, "Ticket tank does not match target");
         FC_ASSERT(ticket.tap_ID == targeted_query.accessory_address.tap_ID, "Ticket tap does not match target");
         FC_ASSERT(ticket.requirement_index == targeted_query.accessory_address.requirement_index,
                   "Ticket requirement index does not match target");
      }
   }
}

void tank_query_operation::validate() const {
   FC_ASSERT(fee.amount > 0, "Must have positive fee");
   for (auto itr = required_authorities.begin(); itr != required_authorities.end(); ++itr)
      FC_ASSERT(std::find(itr+1, required_authorities.end(), *itr) == required_authorities.end(),
                "required_authorities must not contain duplicates");
   FC_ASSERT(!queries.empty(), "Query list must not be empty");
   validate_queries(queries, tank_to_query, false);
}

share_type tap_open_operation::calculate_fee(const fee_parameters_type& params) const {
   return params.base_fee + (fc::raw::pack_size(*this) * params.price_per_byte)
          + (tap_open_count * params.price_per_tap);
}

void tap_open_operation::validate() const {
   FC_ASSERT(fee.amount > 0, "Must have positive fee");
   for (auto itr = required_authorities.begin(); itr != required_authorities.end(); ++itr)
      FC_ASSERT(std::find(itr+1, required_authorities.end(), *itr) == required_authorities.end(),
                "required_authorities must not contain duplicates");
   FC_ASSERT(tap_to_open.tank_id.valid(), "Tank ID must be specified");
   validate_queries(queries, *tap_to_open.tank_id, true);

   if (release_amount.is_type<share_type>()) {
      FC_ASSERT(release_amount.get<share_type>() >= 0, "Release amount must not be negative");
      FC_ASSERT(release_amount.get<share_type>() > 0 || deposit_claimed.valid(),
                "Release amount can only be zero if destroying the tank");
   }

   FC_ASSERT(tap_open_count >= 1, "Number of taps to open must be at least one");
}

void tap_connect_operation::validate() const {
   FC_ASSERT(fee.amount > 0, "Must have positive fee");
   FC_ASSERT(tap_to_connect.tank_id.valid(), "Tank ID must be specified");
   if (clear_connect_authority)
      FC_ASSERT(new_connection.valid(), "If clearing the connect authority, new connection must be specified");
}

void account_fund_connection_operation::validate() const {
   FC_ASSERT(fee.amount > 0, "Must have positive fee");
   FC_ASSERT(funding_amount.amount > 0, "Must have positive funding amount");
}

} } // namespace graphene::protocol
