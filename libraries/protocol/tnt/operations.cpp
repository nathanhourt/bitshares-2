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

namespace graphene { namespace protocol {

share_type tank_create_operation::calculate_fee(const fee_parameters_type& params) const {
   return params.base_fee + (fc::raw::pack_size(*this) * params.price_per_byte);
}

void tank_create_operation::validate() const {
   FC_ASSERT(fee.amount > 0, "Must have positive fee");
   FC_ASSERT(deposit_amount > 0, "Must have positive deposit");

   // We don't have access to the real limits here, so check with max sink chain length of 100
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

   if (taps_to_replace.count(0) > 0) tnt::tank_validator::validate_emergency_tap(taps_to_replace.at(0));
   for (const auto& tap_pair : taps_to_replace) tnt::tank_validator::validate_tap(tap_pair.second);
   for (const auto& tap : taps_to_add) tnt::tank_validator::validate_tap(tap);
   for (const auto& att_pair : attachments_to_replace) tnt::tank_validator::validate_attachment(att_pair.second);
   for (const auto& att : attachments_to_add) tnt::tank_validator::validate_attachment(att);
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

} } // namespace graphene::protocol
