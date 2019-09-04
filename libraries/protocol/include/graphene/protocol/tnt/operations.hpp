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

#pragma once

#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>
#include <graphene/protocol/tnt/types.hpp>

namespace graphene { namespace protocol {

struct tank_create_operation : public base_operation {
   struct fee_parameters_type {
      uint64_t base_fee = 5 * GRAPHENE_BLOCKCHAIN_PRECISION;
      uint64_t price_per_byte = GRAPHENE_BLOCKCHAIN_PRECISION / 10;
   };

   /// Fee to pay for the create operation
   asset fee;
   /// Account that pays for the fee and deposit
   account_id_type payer;
   /// Amount to pay for deposit (CORE asset)
   share_type deposit_amount;
   /// Type of asset the tank will hold
   asset_id_type contained_asset;
   /// Taps that will be attached to the tank
   vector<tnt::tap> taps;
   /// Attachments that will be attached to the tank
   vector<tnt::tank_attachment> attachments;

   extensions_type extensions;

   account_id_type fee_payer() const { return payer; }
   share_type calculate_fee(const fee_parameters_type& params) const;
   void validate() const;
   void get_impacted_accounts(flat_set<account_id_type>& impacted) const;
};

struct tank_update_operation : public base_operation {
   struct fee_parameters_type {
      uint64_t base_fee = GRAPHENE_BLOCKCHAIN_PRECISION;
      uint64_t price_per_byte = GRAPHENE_BLOCKCHAIN_PRECISION / 10;
   };

   /// Fee to pay for the update operation
   asset fee;
   /// Account that pays for the fee and deposit
   account_id_type payer;
   /// Authority required to update the tank (same as emergency tap open authority)
   authority update_authority;
   /// ID of the tank to update
   tank_id_type tank_to_update;
   /// Change in deposit amount on tank; credited or debited to payer
   share_type deposit_delta;

   /// IDs of taps to remove
   flat_set<tnt::index_type> taps_to_remove;
   /// Map of ID-to-new-value for taps to replace
   flat_map<tnt::index_type, tnt::tap> taps_to_replace;
   /// List of new taps to add; these will be assigned new IDs consecutively
   vector<tnt::tap> taps_to_add;

   /// IDs of attachments to remove
   vector<tnt::index_type> attachments_to_remove;
   /// Map of ID-to-new-value for attachments to replace
   flat_map<tnt::index_type, tnt::tank_attachment> attachments_to_replace;
   /// List of new attachments to add; these will be assigned new IDs consecutively
   vector<tnt::tank_attachment> attachments_to_add;

   extensions_type extensions;

   account_id_type fee_payer() const { return payer; }
   share_type calculate_fee(const fee_parameters_type& params) const;
   void validate() const;
   void get_impacted_accounts(flat_set<account_id_type>& impacted) const;
};

} } // namespace graphene::protocol

FC_REFLECT(graphene::protocol::tank_create_operation::fee_parameters_type, (base_fee)(price_per_byte))
FC_REFLECT(graphene::protocol::tank_create_operation,
           (fee)(payer)(deposit_amount)(contained_asset)(taps)(attachments)(extensions))
FC_REFLECT(graphene::protocol::tank_update_operation::fee_parameters_type, (base_fee)(price_per_byte))
FC_REFLECT(graphene::protocol::tank_update_operation,
           (fee)(payer)(update_authority)(tank_to_update)(deposit_delta)
           (taps_to_remove)(taps_to_replace)(taps_to_add)
           (attachments_to_remove)(attachments_to_replace)(attachments_to_add)(extensions))
