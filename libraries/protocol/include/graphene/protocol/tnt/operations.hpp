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
#include <graphene/protocol/tnt/query_api.hpp>

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
   /// Note that state data for all requirements of replaced taps will be deleted
   flat_map<tnt::index_type, tnt::tap> taps_to_replace;
   /// List of new taps to add; these will be assigned new IDs consecutively
   vector<tnt::tap> taps_to_add;

   /// IDs of attachments to remove
   vector<tnt::index_type> attachments_to_remove;
   /// Map of ID-to-new-value for attachments to replace
   /// Note that state data for replaced attachments will be deleted
   flat_map<tnt::index_type, tnt::tank_attachment> attachments_to_replace;
   /// List of new attachments to add; these will be assigned new IDs consecutively
   vector<tnt::tank_attachment> attachments_to_add;

   extensions_type extensions;

   account_id_type fee_payer() const { return payer; }
   share_type calculate_fee(const fee_parameters_type& params) const;
   void validate() const;
   void get_impacted_accounts(flat_set<account_id_type>& impacted) const;
   void get_required_authorities(vector<authority>& auths) const { auths.push_back(update_authority); }
};

struct tank_delete_operation : public base_operation {
   struct fee_parameters_type {
      uint64_t base_fee = GRAPHENE_BLOCKCHAIN_PRECISION;
   };

   /// Fee to pay for the delete operation
   asset fee;
   /// Account that pays for the fee and receives the deposit
   account_id_type payer;
   /// Authority required to destroy the tank (same as emergency tap open authority)
   authority delete_authority;
   /// ID of the tank to delete
   tank_id_type tank_to_delete;
   /// Amount of the deposit on the tank, to be refunded to payer
   share_type deposit_claimed;

   extensions_type extensions;

   account_id_type fee_payer() const { return payer; }
   share_type calculate_fee(const fee_parameters_type& params) const { return params.base_fee; }
   void validate() const;
   void get_required_authorities(vector<authority>& auths) const { auths.push_back(delete_authority); }
};

struct tank_query_operation : public base_operation {
   struct fee_parameters_type {
      uint64_t base_fee = GRAPHENE_BLOCKCHAIN_PRECISION;
      uint64_t price_per_byte = GRAPHENE_BLOCKCHAIN_PRECISION / 10;
   };

   /// Fee to pay for the query operation
   asset fee;
   /// Account that pays for the fee
   account_id_type payer;
   /// Authorities required to authenticate the queries
   vector<authority> required_authorities;
   /// ID of the tank to query
   tank_id_type tank_to_query;
   /// The queries to run
   vector<tnt::tank_query_type> queries;

   extensions_type extensions;

   account_id_type fee_payer() const { return payer; }
   share_type calculate_fee(const fee_parameters_type& params) const;
   void validate() const;
   void get_required_authorities(vector<authority>& auths) const {
      auths.insert(auths.end(), required_authorities.begin(), required_authorities.end());
   }
};

struct tap_open_operation : public base_operation {
   struct fee_parameters_type {
      uint64_t base_fee = GRAPHENE_BLOCKCHAIN_PRECISION;
      uint64_t price_per_byte = GRAPHENE_BLOCKCHAIN_PRECISION / 10;
   };

   /// Fee to pay for the query operation
   asset fee;
   /// Account that pays for the fee, and receives any deposit claimed
   account_id_type payer;
   /// Authorities required to authenticate the queries and open the tap
   vector<authority> required_authorities;
   /// Any queries which should be run prior to opening the tap
   vector<tnt::tank_query_type> queries;
   /// ID of the tap to open
   tnt::tap_id_type tap_to_open;
   /// Amount of asset to release through the tap
   tnt::asset_flow_limit release_amount;
   /// If specified, destroy the tank and claim the deposit. Requires tap be a destructor tap, flow_limit be
   /// unlimited, and that the tank be empty when the operation finishes executing
   optional<share_type> deposit_claimed;
   /// Total number of taps opened by this operation (always at least 1, but maybe more due to tap_openers)
   uint16_t tap_open_count = 0;

   extensions_type extensions;

   account_id_type fee_payer() const { return payer; }
   share_type calculate_fee(const fee_parameters_type& params) const;
   void validate() const;
   void get_required_authorities(vector<authority>& auths) const {
      auths.insert(auths.end(), required_authorities.begin(), required_authorities.end());
   }
};

struct tap_connect_operation : public base_operation {
   struct fee_parameters_type {
      uint64_t base_fee = GRAPHENE_BLOCKCHAIN_PRECISION;
   };

   /// Fee to pay for the tap open operation
   asset fee;
   /// Account that pays for the fee
   account_id_type payer;
   /// Authority required to reconnect the tap (must match tap->connect_authority)
   authority connect_authority;
   /// ID of the tap to reconnect
   tnt::tap_id_type tap_to_connect;
   /// New destination for the tap; if null, tap will be disconnected
   optional<tnt::sink> new_sink;
   /// If true, new_sink must not be null, and the tap connect authority will be cleared after this operation and the
   /// tap will no longer be able to be reconnected
   /// WARNING: Set this to false unless you really know what you're doing
   bool clear_connect_authority;

   extensions_type extensions;

   account_id_type fee_payer() const { return payer; }
   share_type calculate_fee(const fee_parameters_type& params) const { return params.base_fee; }
   void validate() const;
   void get_required_authorities(vector<authority>& auths) const {
      auths.push_back(connect_authority);
   }
};

struct account_fund_sink_operation : public base_operation {
   struct fee_parameters_type {
      uint64_t base_fee = GRAPHENE_BLOCKCHAIN_PRECISION;
   };

   /// Fee to pay for the fund operation
   asset fee;
   /// Account providing the funds and paying the fee
   account_id_type funding_account;
   /// Destination for the funds
   tnt::sink funding_destination;
   /// Amount of asset to deposit into the sink
   asset funding_amount;

   extensions_type extensions;

   account_id_type fee_payer() const { return funding_account; }
   share_type calculate_fee(const fee_parameters_type& params) const { return params.base_fee; }
   void validate() const;
};

struct sink_fund_account_operation : public base_operation {
   // Virtual operation -- does not charge a fee
   struct fee_parameters_type {};

   /// The account receiving the funds
   account_id_type receiving_account;
   /// The amount received
   asset amount_received;
   /// The path of sinks the asset took to arrive at the account
   vector<tnt::sink> asset_path;

   extensions_type extensions;

   account_id_type fee_payer() const { return receiving_account; }
   share_type calculate_fee(const fee_parameters_type&) const { return 0; }
   void validate() const {}
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
FC_REFLECT(graphene::protocol::tank_delete_operation::fee_parameters_type, (base_fee))
FC_REFLECT(graphene::protocol::tank_delete_operation,
           (fee)(payer)(delete_authority)(tank_to_delete)(deposit_claimed)(extensions))
FC_REFLECT(graphene::protocol::tank_query_operation::fee_parameters_type, (base_fee)(price_per_byte))
FC_REFLECT(graphene::protocol::tank_query_operation,
           (fee)(payer)(required_authorities)(tank_to_query)(queries)(extensions))
FC_REFLECT(graphene::protocol::tap_open_operation::fee_parameters_type, (base_fee)(price_per_byte))
FC_REFLECT(graphene::protocol::tap_open_operation,
           (fee)(payer)(required_authorities)(queries)(tap_to_open)
           (release_amount)(deposit_claimed)(tap_open_count)(extensions))
FC_REFLECT(graphene::protocol::tap_connect_operation::fee_parameters_type, (base_fee))
FC_REFLECT(graphene::protocol::tap_connect_operation,
           (fee)(payer)(connect_authority)(tap_to_connect)(new_sink)(clear_connect_authority)(extensions))
FC_REFLECT(graphene::protocol::account_fund_sink_operation::fee_parameters_type, (base_fee))
FC_REFLECT(graphene::protocol::account_fund_sink_operation,
           (fee)(funding_account)(funding_destination)(funding_amount)(extensions))
FC_REFLECT(graphene::protocol::sink_fund_account_operation::fee_parameters_type,)
FC_REFLECT(graphene::protocol::sink_fund_account_operation,
           (receiving_account)(amount_received)(asset_path)(extensions))
