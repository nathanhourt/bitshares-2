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
#include <graphene/chain/tnt/cow_db_wrapper.hpp>

namespace graphene { namespace chain {
class database;
namespace ptnt = protocol::tnt;
namespace tnt {
struct tap_flow_evaluator_impl;

/// A report of the results of a tap flow evaluation
struct tap_flow_report {
   /// Details of a particular tap flow
   struct tap_flow {
      /// The amount released from the tap
      asset amount_released;
      /// The ID of the tap that released asset
      ptnt::index_type source_tap;
      /// The path of the tap flow, beginning with the source tank
      vector<ptnt::sink> flow_path;
   };

   /// All tap flows processed during this tap flow
   vector<tap_flow> tap_flows;
   /// All authorities required by the tap flow, associated with the ID of the tank which required the authority
   flat_map<tank_id_type, vector<authority>> authorities_required;
};

/**
 * @brief Evaluates the logic of opening taps and releasing asset
 * @ingroup TNT
 *
 * This class implements the logic involved in opening taps and releasing asset to sinks, adjusting balances of tanks
 * and asset destinations, and triggering tank attachments which receive asset and release it to another sink. It
 * processes all tap flows triggered by the first one as well (i.e. due to asset flowing through a tap_opener), up to
 * a maximum number of taps to open.
 *
 * The tap_flow_evaluator processes the tap_requirements of the associated taps, and processes the logic and state
 * updates requisite to asset flowing through tank attachments. It does not, however, process query logic. It accepts
 * a @ref query_evaluator which should have already processed any necessary queries before the tap_flow_evaluator
 * runs. The queries are expected to be already applied to the provided COW database.
 *
 * This class applies the results of tap flow evaluation to the provided COW database directly. After running the tap
 * flow evaluation, invoke @ref cow_db_wrapper::commit to store the changes to the database.
 */
class tap_flow_evaluator {
   std::unique_ptr<tap_flow_evaluator_impl> my;

public:
   tap_flow_evaluator();

   /**
    * @brief Evaluate a tap flow and all subsequently triggered tap flows
    * @param db A copy-on-write database to apply tap flow changes to
    * @param queries A query evaluator which has already applied any queries run prior to opening a tap
    * @param tank_id ID of the tank to open a tap on
    * @param tap_id ID of the tap to open
    * @param flow_amount The amount requested to open the tap for
    * @param max_taps_to_open Maximum number of tap flows to process
    * @return Report of the taps opened and flows processed
    */
   tap_flow_report evaluate_tap_flow(cow_db_wrapper& db, const query_evaluator& queries,
                                     ptnt::tap_id_type tap_to_open, ptnt::asset_flow_limit flow_amount,
                                     int max_taps_to_open);

   /**
    * @brief Evaluate a tap's requirements to determine the maximum amount that can be released from the tap
    * @param db The database
    * @param tank The tank with the tap to evaluate
    * @param tap_ID The ID of the tap to evaluate
    * @param queries The queries which have been evaluated (some tap requirements need certain queries to have been
    * run within the same operation that opens the tap)
    * @return The index of the requirement with the lowest release limit, and that requirement's release limit. If
    * the index is null, the limit is the tank's balance.
    */
   pair<optional<size_t>, ptnt::asset_flow_limit> max_tap_release(const database& db, const tank_object& tank,
                                                                  const protocol::tnt::index_type& tap_ID,
                                                                  const query_evaluator& queries);
};

} } } // namespace graphene::chain::tnt
