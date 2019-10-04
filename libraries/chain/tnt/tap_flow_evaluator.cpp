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
   auto tank = db.get(*tap_to_open.tank_id);
   FC_ASSERT(tank.schematic().taps().count(tap_to_open.tap_id) != 0, "Tap to open does not exist!");
   const ptnt::tap& tap = tank.schematic().taps().at(tap_to_open.tap_id);
   if (tap.open_authority.valid())
      my->require_authority(*tap_to_open.tank_id, *tap.open_authority);

   // Calculate the max amount the tap's requirements will allow to be released
   auto release_limit = max_tap_release(db.get_db(), tank._ref_, tap_to_open.tap_id, queries);
   const auto& max_release = release_limit.second;
   // Check that the tap is not locked
   if (max_release.is_type<share_type>() && max_release.get<share_type>() == 0) {
      if (release_limit.first.valid())
         FC_THROW_EXCEPTION(fc::assert_exception, "Cannot open tap: a tap requirement has locked the tap.\n${R}",
                            ("R", tank.schematic().taps().at((unsigned short)(*release_limit.first))));
      FC_THROW_EXCEPTION(fc::assert_exception, "Cannot open tap: tank is empty");
   }
   // Check that the requested release does not exceed the tap requirements' limit
   if (flow_amount.is_type<share_type>())
      FC_ASSERT(flow_amount <= max_release,
                "Cannot release requested amount of ${A} from tap: a tap requirement has limited flow to ${L}.\n${R}",
                ("A", flow_amount.get<share_type>())("L", max_release.get<share_type>())
                ("R", tank.schematic().taps().at((unsigned short)(*release_limit.first))));

#warning TODO: Finish tap flow logic

   return std::move(my->report);
}

// Get the maximum amount a particular tap_requirement will allow to be released
class max_release_inspector {
   const database& db;
   const tank_object& tank;
   const query_evaluator& queries;
   max_release_inspector(const database& d, const tank_object& t, const query_evaluator& q)
      : db(d), tank(t), queries(q) {}

   template<typename Requirement>
   struct Req {
      const Requirement& req;
      ptnt::tank_accessory_address<Requirement> address;
   };

   ptnt::asset_flow_limit operator()(const Req<ptnt::immediate_flow_limit>& req) const { return req.req.limit; }
   ptnt::asset_flow_limit operator()(const Req<ptnt::cumulative_flow_limit>& req) const {
      const auto* state = tank.get_state(req.address);
      if (state == nullptr)
         return req.req.limit;
      return req.req.limit - state->amount_released;
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::periodic_flow_limit>& req) const {
      const auto* state = tank.get_state(req.address);
      if (state == nullptr)
         return req.req.limit;
      auto period_num = (db.head_block_time() - tank.creation_date).to_seconds() / req.req.period_duration_sec;
      if (state->period_num == period_num)
         return req.req.limit - state->amount_released;
      return req.req.limit;
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::time_lock>& req) const {
      auto unlocked_now = req.req.unlocked_at_time(db.head_block_time());
      if (unlocked_now)
         return ptnt::unlimited_flow();
      return share_type(0);
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::minimum_tank_level>& req) const {
      if (tank.balance <= req.req.minimum_level)
         return share_type(0);
      return tank.balance - req.req.minimum_level;
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::documentation_requirement>&) const {
      auto tank_queries = queries.get_tank_queries();
      for (const ptnt::tank_query_type& q : tank_queries)
         if (q.is_type<ptnt::targeted_query<ptnt::queries::documentation_string>>())
            return ptnt::unlimited_flow();
      return share_type(0);
   }
   // Delay requirement and review requirement (collectively, the "Request Requirements") have exactly identical
   // implementations, just different types, so unify them into a single function
   using request_requirements = ptnt::TL::list<ptnt::review_requirement, ptnt::delay_requirement>;
   using request_consume_queries = ptnt::TL::list<ptnt::queries::consume_approved_request_to_open,
                                                  ptnt::queries::consume_matured_request_to_open>;
   template<typename RR, std::enable_if_t<ptnt::TL::contains<request_requirements, RR>(), bool> = true>
   ptnt::asset_flow_limit operator()(const Req<RR>& req) const {
      using consume_query = ptnt::targeted_query<ptnt::TL::at<request_consume_queries,
                                                              ptnt::TL::index_of<request_requirements, RR>()>>;
      const auto* state = tank.get_state(req.address);
      if (state == nullptr)
         return share_type(0);
      auto my_queries = queries.get_target_queries(req.address);
      share_type limit = 0;

      for (const ptnt::tank_query_type& query : my_queries)
         if (query.is_type<consume_query>()) {
            const auto& request = state->pending_requests.at(query.get<consume_query>().query_content.request_ID);
            if (request.request_amount.template is_type<ptnt::unlimited_flow>())
               return ptnt::unlimited_flow();
            limit += request.request_amount.template get<share_type>();
         }

      return std::move(limit);
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::hash_preimage_requirement>& req) const {
      auto my_queries = queries.get_target_queries(req.address);
      for (const ptnt::tank_query_type& query : my_queries)
         if (query.is_type<ptnt::targeted_query<ptnt::queries::reveal_hash_preimage>>())
            return ptnt::unlimited_flow();
      return share_type(0);
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::ticket_requirement>& req) const {
      auto my_queries = queries.get_target_queries(req.address);
      share_type limit = 0;

      for (const ptnt::tank_query_type& query : my_queries) {
         if (query.is_type<ptnt::targeted_query<ptnt::queries::redeem_ticket_to_open>>()) {
            const auto& q = query.get<ptnt::targeted_query<ptnt::queries::redeem_ticket_to_open>>().query_content;
            if (q.ticket.max_withdrawal.is_type<ptnt::unlimited_flow>())
               return ptnt::unlimited_flow();
            limit += q.ticket.max_withdrawal.get<share_type>();
         }
      }

      return std::move(limit);
   }
   ptnt::asset_flow_limit operator()(const Req<ptnt::exchange_requirement>& req) const {
      const auto* state = tank.get_state(req.address);
      const auto& meter_id = req.req.meter_id;
      tank_id_type meter_tank = tank.id;
      if (meter_id.tank_id.valid())
         meter_tank = *meter_id.tank_id;
      const auto* meter_state =
            meter_tank(db).get_state(ptnt::tank_accessory_address<ptnt::asset_flow_meter>{meter_id.attachment_id});
      if (meter_state == nullptr)
         return share_type(0);

      return req.req.max_release_amount((state == nullptr? share_type(0) : state->amount_released), *meter_state);
   }

public:
   static ptnt::asset_flow_limit inspect(const database& db, const tank_object& tank, const query_evaluator& queries,
                                         ptnt::index_type tap_ID, ptnt::index_type requirement_index) {
      max_release_inspector inspector(db, tank, queries);
      const auto& requirement = tank.schematic.taps.at(tap_ID).requirements[requirement_index];
      return ptnt::TL::runtime::dispatch(ptnt::tap_requirement::list(), requirement.which(),
                                         [&inspector, &requirement, tap_ID, requirement_index](auto t) {
         using Requirement = typename decltype(t)::type;
         ptnt::tank_accessory_address<Requirement> address{tap_ID, requirement_index};
         Req<Requirement> req{requirement.get<Requirement>(), address};
         return inspector(req);
      });
   }
};

pair<optional<size_t>, ptnt::asset_flow_limit> tap_flow_evaluator::max_tap_release(const database& db,
                                                                                   const tank_object& tank,
                                                                                   const ptnt::index_type& tap_ID,
                                                                                   const query_evaluator& queries) {
   ptnt::asset_flow_limit tap_limit = tank.balance;
   optional<size_t> lowest_limit_requirement;
   const auto& tap = tank.schematic.taps.at(tap_ID);

   for (ptnt::index_type i = 0; i < tap.requirements.size(); ++i) {
      auto req_limit = max_release_inspector::inspect(db, tank, queries, tap_ID, i);

      if (req_limit < tap_limit) {
         tap_limit = req_limit;
         lowest_limit_requirement = i;
      }
      if (tap_limit.is_type<share_type>() && tap_limit.get<share_type>() == 0)
         break;
   }

   return {lowest_limit_requirement, tap_limit};
}

} } } // namespace graphene::chain::tnt
