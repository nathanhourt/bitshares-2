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

#include <graphene/chain/tnt/query_evaluator.hpp>
#include <graphene/chain/tnt/tap_flow_evaluator.hpp>

#include <graphene/protocol/tnt/operations.hpp>

namespace graphene { namespace chain { namespace tnt {

/**
 * @brief Set the tap_open_count and required_authorities fields on a @ref tap_open_operation
 * @param op The operation to configure
 * @ingroup TNT
 */
void set_tap_open_count_and_authorities(const database& db, tap_open_operation& op) {
   const auto& tank = db.get(*op.tap_to_open.tank_id);
   const auto& tap = tank.schematic.taps.at(op.tap_to_open.tap_id);
   op.required_authorities.clear();

   // Fast track: if destroying an empty tank, only the tap authority is required, and only one tap is opened
   if (op.deposit_claimed.valid() && tank.balance == 0) {
      FC_ASSERT(op.queries.empty(),
                "When destroying an empty tank, queries are not evaluated, so they must not be provided");
      op.tap_open_count = 1;
      if (tap.open_authority.valid())
         op.required_authorities = {*tap.open_authority};
      return;
   }

   query_evaluator eval;
   eval.set_query_tank(tank);
   cow_db_wrapper wdb(db);
   auto max_taps = db.get_global_properties().parameters.extensions.value.updatable_tnt_options->max_taps_to_open;

   auto add_auth = [&old_auths=op.required_authorities](authority auth) {
      if (std::find(old_auths.begin(), old_auths.end(), auth) == old_auths.end())
         old_auths.emplace_back(std::move(auth));
   };
   auto pay_account = [](account_id_type, asset, vector<ptnt::connection>) {};

   if (tap.open_authority.valid())
      add_auth(*tap.open_authority);

   for (const auto& query : op.queries) {
      auto new_auths = eval.evaluate_query(query, db);
      for (auto& auth : new_auths)
         add_auth(std::move(auth));
   }

   auto flows = evaluate_tap_flow(wdb, eval, op.payer, op.tap_to_open, op.release_amount, max_taps,
                                  std::move(pay_account));
   op.tap_open_count = flows.size();
}

} } } // namespace graphene::chain::tnt
