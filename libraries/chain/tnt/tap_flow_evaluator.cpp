/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/tnt/tap_flow_evaluator.hpp>
#include <graphene/chain/tnt/tap_requirement_utility.hpp>
#include <graphene/chain/tnt/connection_flow_processor.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

#include <queue>

namespace graphene { namespace chain { namespace tnt {

vector<tap_flow> evaluate_tap_flow(cow_db_wrapper& db, const query_evaluator& queries, account_id_type account,
                                   ptnt::tap_id_type tap_to_open, ptnt::asset_flow_limit flow_amount,
                                   int max_taps_to_open, FundAccountCallback fund_account_cb) {
   const account_object& responsible_account = account(db.get_db());
   std::queue<std::pair<ptnt::tap_id_type, ptnt::asset_flow_limit>> pending_taps;
   vector<tap_flow> tap_flows;
   std::map<ptnt::tap_id_type, tap_requirement_utility> tap_utilities;

   auto getTapUtil = [&tap_utilities, &db, &queries](ptnt::tap_id_type id) -> tap_requirement_utility& {
     auto itr = tap_utilities.lower_bound(id);
     if (itr->first != id)
        itr = tap_utilities.emplace_hint(itr, std::make_pair(id, tap_requirement_utility(db, id, queries)));
     return itr->second;
   };
   auto enqueueTap = [&pending_taps, &tap_flows, &max_taps_to_open](ptnt::tap_id_type id,
                                                                    ptnt::asset_flow_limit amount) {
      FC_ASSERT(pending_taps.size() + tap_flows.size() < (unsigned long)max_taps_to_open,
                "Tap flow has exceeded its maximum number of taps to open");
      pending_taps.emplace(std::move(id), std::move(amount));
   };
   connection_flow_processor connection_processor(db, std::move(enqueueTap), std::move(fund_account_cb));

   pending_taps.push(std::make_pair(tap_to_open, flow_amount));

   while (!pending_taps.empty()) {
      ptnt::tap_id_type current_tap = pending_taps.front().first;
      ptnt::asset_flow_limit current_amount = pending_taps.front().second;

      // Get tank, check tap exists and fetch it; if it has an open authority, require it -- if not, anyone can open
      FC_ASSERT(current_tap.tank_id.valid(), "Cannot open tap: tank ID not specified");
      auto tank = db.get(*current_tap.tank_id);
      FC_ASSERT(tank.schematic().taps().count(current_tap.tap_id) != 0, "Tap to open does not exist!");
      const ptnt::tap& tap = tank.schematic().taps().at(current_tap.tap_id);
      FC_ASSERT(tap.connected_connection.valid(), "Cannot open tap ${ID}: tap is not connected to a connection",
                ("ID", current_tap));
      // Check the responsible account is authorized to transact the tank's asset
      const asset_object& tank_asset = tank.schematic().asset_type()(db);
      FC_ASSERT(is_authorized_asset(db.get_db(), responsible_account, tank_asset),
                "Cannot open tap: responsible account ${R} is not authorized to transact the tank's asset ${A}",
                ("R", account)("A", tank_asset.symbol));
      // Check tank balance (it's checked later too, but we can skip a lot of work if it's obviously wrong)
      if (current_amount.is_type<share_type>())
         FC_ASSERT(tank.balance() >= current_amount.get<share_type>(),
                   "Cannot release requested amount through tap: tank has insufficient balance");

      // Calculate the max amount the tap's requirements will allow to be released
      tap_requirement_utility& util = getTapUtil(current_tap);
      auto release_limit = util.max_tap_release();
      auto req_index = util.most_restrictive_requirement_index();
      // Check that the tap is not locked
      if (release_limit == 0) {
         if (req_index.valid())
            FC_THROW_EXCEPTION(fc::assert_exception, "Cannot open tap: a tap requirement has locked the tap.\n${R}",
                               ("R", tap.requirements.at(*req_index)));
         FC_THROW_EXCEPTION(fc::assert_exception, "Cannot open tap: tank is empty");
      }
      // Check that the requested release does not exceed the tap requirements' limit
      if (current_amount.is_type<share_type>()) {
         if (!req_index.valid())
            FC_ASSERT(current_amount.get<share_type>() <= release_limit,
                      "Cannot release requested amount of ${A} from tap: tank balance is only ${L}",
                      ("A", current_amount.get<share_type>())("L", release_limit));
         FC_ASSERT(current_amount.get<share_type>() <= release_limit,
                   "Cannot release requested amount of ${A} from tap: a requirement has limited flow to ${L}.\n${R}",
                   ("A", current_amount.get<share_type>())("L", release_limit)("R", tap.requirements.at(*req_index)));
         release_limit = current_amount.get<share_type>();
      }

      // Notify the tap requirements of the amount being released
      util.prepare_tap_release(release_limit);
      // By now, release_limit is the exact amount we will be releasing. Remove it from the tank balance
      tank.balance = tank.balance() - release_limit;
      // Flow the released asset until it stops
      auto connection_path = connection_processor.release_to_connection(*current_tap.tank_id,
                                                                        *tap.connected_connection, release_limit);
      // Add flow to report
      tap_flows.emplace_back(release_limit, tap_to_open, std::move(connection_path));
      // Remove the tap from the queue to open
      pending_taps.pop();
   }

   return tap_flows;
}

} } } // namespace graphene::chain::tnt
