#pragma once
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

#include <graphene/chain/tnt/object.hpp>
#include <graphene/chain/tnt/query_evaluator.hpp>
#include <graphene/chain/tnt/connection_flow_processor.hpp>
#include <graphene/chain/tnt/cow_db_wrapper.hpp>

namespace graphene { namespace chain { namespace tnt {

/// Details of a particular tap flow
struct tap_flow {
   /// The amount released from the tap
   asset amount_released;
   /// The ID of the tap that released asset
   ptnt::tap_id_type source_tap;
   /// The path of the tap flow, beginning with the source tank
   vector<ptnt::connection> connection_path;

   tap_flow(const asset& amount, const ptnt::tap_id_type& tap, vector<ptnt::connection> path)
      : amount_released(amount), source_tap(tap), connection_path(std::move(path)) {}
};

/**
 * @brief Evaluate a tap flow and all subsequently triggered tap flows
 * @param db A copy-on-write database to apply tap flow changes to
 * @param queries A query evaluator which has already applied any queries run prior to opening a tap
 * @param account The account responsible for the movement of asset. If this account is not authorized to transact
 * any of the asset types released, an exception will be thrown
 * @param tank_id ID of the tank to open a tap on
 * @param tap_id ID of the tap to open
 * @param flow_amount The amount requested to open the tap for
 * @param max_taps_to_open Maximum number of tap flows to process
 * @param fund_account_cb Callback to deposit released asset into an account's balance
 * @return List of the tap flows processed
 * @ingroup TNT
 */
vector<tap_flow> evaluate_tap_flow(cow_db_wrapper& db, const query_evaluator& queries, account_id_type account,
                                   ptnt::tap_id_type tap_to_open, ptnt::asset_flow_limit flow_amount,
                                   int max_taps_to_open, FundAccountCallback fund_account_cb);

} } } // namespace graphene::chain::tnt
