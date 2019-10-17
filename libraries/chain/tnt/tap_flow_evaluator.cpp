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

namespace graphene { namespace chain { namespace tnt {

struct tap_flow_evaluator_impl {
   tap_flow_report report;

   void require_authority(const tank_id_type& tank_id, const authority& auth) {
      auto& auths = report.authorities_required[tank_id];
      if (std::find(auths.begin(), auths.end(), auth) == auths.end())
         auths.push_back(auth);
   }
};

tap_flow_evaluator::tap_flow_evaluator() {
   my = std::make_unique<tap_flow_evaluator_impl>();
}

tap_flow_report tap_flow_evaluator::evaluate_tap_flow(cow_db_wrapper& db, const query_evaluator& queries,
                                                      ptnt::tap_id_type tap_to_open,
                                                      ptnt::asset_flow_limit flow_amount, int max_taps_to_open) {
   // Get tank, check tap exists and fetch it; if it has an open authority, require it -- if not, anyone can open
   auto tank = db.get(*tap_to_open.tank_id);
   FC_ASSERT(tank.schematic().taps().count(tap_to_open.tap_id) != 0, "Tap to open does not exist!");
   const ptnt::tap& tap = tank.schematic().taps().at(tap_to_open.tap_id);
   if (tap.open_authority.valid())
      my->require_authority(*tap_to_open.tank_id, *tap.open_authority);
   tap_requirement_utility util(db, tap_to_open, queries);

   // Calculate the max amount the tap's requirements will allow to be released
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
   if (flow_amount.is_type<share_type>()) {
      if (req_index.valid())
         FC_ASSERT(flow_amount.get<share_type>() <= release_limit,
                   "Cannot release requested amount of ${A} from tap: a requirement has limited flow to ${L}.\n${R}",
                   ("A", flow_amount.get<share_type>())("L", release_limit)("R", tap.requirements.at(*req_index)));
      FC_ASSERT(flow_amount.get<share_type>() <= release_limit,
                "Cannot release requested amount of ${A} from tap: tank balance is only ${L}",
                ("A", flow_amount.get<share_type>())("L", release_limit));
   }

#warning TODO: Finish tap flow logic

   return std::move(my->report);
}

} } } // namespace graphene::chain::tnt
