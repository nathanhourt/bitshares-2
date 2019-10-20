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

#include <queue>

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

template<typename EnqueueTapCallback>
class attachment_receive_inspector {
   tank_object& tank;
   const asset& amount;
   const EnqueueTapCallback& enqueueTap;
   attachment_receive_inspector(tank_object& tank, const asset& amount, const EnqueueTapCallback& enqueueTap)
      : tank(tank), amount(amount), enqueueTap(enqueueTap) {}

   using NonReceivingAttachments =
      ptnt::TL::list<ptnt::deposit_source_restrictor, ptnt::attachment_connect_authority>;
   template<typename Attachment,
            std::enable_if_t<ptnt::TL::contains<NonReceivingAttachments, Attachment>(), bool> = true>
   [[noreturn]] ptnt::sink operator()(const Attachment&, ptnt::tank_accessory_address<Attachment>) {
      FC_THROW_EXCEPTION(fc::assert_exception, "INTERNAL ERROR: Tried to flow asset to an attachment which cannot "
                                               "receive asset. Please report this error.");
   }

   ptnt::sink operator()(const ptnt::asset_flow_meter& meter,
                         ptnt::tank_accessory_address<ptnt::asset_flow_meter> address) {
      FC_ASSERT(meter.asset_type == amount.asset_id,
                "Flowed wrong type of asset to flow meter. Meter expects ${O} but received ${A}",
                ("O", meter.asset_type)("A", amount.asset_id));
      auto& state = tank.get_or_create_state(address);
      state.metered_amount += amount.amount;
      return meter.destination_sink;
   }
   ptnt::sink operator()(const ptnt::tap_opener& opener,
                         ptnt::tank_accessory_address<ptnt::tap_opener>) {
      FC_ASSERT(opener.asset_type == amount.asset_id,
                "Flowed wrong type of asset to tap opener. Opener expects ${O} but received ${A}",
                ("O", opener.asset_type)("A", amount.asset_id));
      enqueueTap(ptnt::tap_id_type{tank.id, opener.tap_index}, opener.release_amount);
      return opener.destination_sink;
   }

public:
   static ptnt::sink inspect(tank_object& tank, ptnt::index_type attachment_ID, const asset& amount,
                             const EnqueueTapCallback& enqueueTap) {
      attachment_receive_inspector inspector(tank, amount, enqueueTap);
      const auto& attachment = tank.schematic.attachments.at(attachment_ID);
      return ptnt::TL::runtime::dispatch(ptnt::tank_attachment::list(), attachment.which(),
                                  [&attachment, attachment_ID, &inspector](auto t) {
         return inspector(attachment.get<typename decltype(t)::type>(), {attachment_ID});
      });
   }
};

template<typename EnqueueTapCallback>
tap_flow_report::tap_flow process_tap_flow(cow_db_wrapper& db, ptnt::tap_id_type tap_ID, asset release_amount,
                                           const EnqueueTapCallback& enqueueTap) {
   auto tank = db.get(*tap_ID.tank_id);
   ptnt::sink sink = *tank.schematic().taps().at(tap_ID.tap_id).connected_sink;
   FC_ASSERT(tap_ID.tank_id.valid(),
             "INTERNAL ERROR: Attempted to process tap flow, but tap ID has null tank ID. Please report this error.");
   tank_id_type current_tank = *tap_ID.tank_id;
   
   tap_flow_report::tap_flow flow_report;
   flow_report.source_tap = tap_ID;
   flow_report.amount_released = release_amount;
   
   while (!ptnt::is_terminal_sink(sink)) {
      auto max_sinks = db.get_db().get_global_properties()
                       .parameters.extensions.value.updatable_tnt_options->max_sink_chain_length;
      FC_ASSERT(flow_report.flow_path.size() < max_sinks,
                "Tap flow has exceeded the maximm sink chain length. Chain: ${SC}",
                ("SC", flow_report.flow_path));

      // At present, the only non-terminal sink type is a tnak attachment
      ptnt::attachment_id_type att_id = sink.get<ptnt::attachment_id_type>();
      if (att_id.tank_id.valid())
         current_tank = *att_id.tank_id;
      else
         att_id.tank_id = current_tank;

      flow_report.flow_path.emplace_back(std::move(sink));
      sink = attachment_receive_inspector<EnqueueTapCallback>::inspect(current_tank(db), att_id.attachment_id,
                                                                       release_amount, enqueueTap);
   }

   if (sink.is_type<ptnt::same_tank>())
      sink = current_tank;
   if (sink.is_type<tank_id_type>()) {
      auto dest_tank = sink.get<tank_id_type>()(db);
      FC_ASSERT(dest_tank.schematic().asset_type() == release_amount.asset_id,
                "Destination tank of tap flow stores asset ID ${D}, but tap flow asset ID was ${F}",
                ("D", dest_tank.schematic().asset_type())("F", release_amount.asset_id));
      dest_tank.balance = dest_tank.balance() + release_amount.amount;
   } else if (sink.is_type<account_id_type>()) {
      // TODO: Check account can hold asset
      // TODO: Generate virtual operation
      // TODO: Figure out how to adjust account balance
   }
#warning TODO: deposit asset to rest

   flow_report.flow_path.emplace_back(std::move(sink));
   return flow_report;
}

tap_flow_report tap_flow_evaluator::evaluate_tap_flow(cow_db_wrapper& db, const query_evaluator& queries,
                                                      ptnt::tap_id_type tap_to_open,
                                                      ptnt::asset_flow_limit flow_amount, int max_taps_to_open) {
   std::queue<std::pair<ptnt::tap_id_type, ptnt::asset_flow_limit>> pending_taps;
   pending_taps.push(std::make_pair(tap_to_open, flow_amount));
   auto enqueueTap = [&pending_taps, &report=my->report, &max_taps_to_open](ptnt::tap_id_type id,
                                                                            ptnt::asset_flow_limit amount) {
      FC_ASSERT(pending_taps.size() + report.tap_flows.size() < (unsigned long)max_taps_to_open,
                "Tap flow has exceeded its maximum number of taps to open");
      pending_taps.emplace(std::move(id), std::move(amount));
   };

   while (!pending_taps.empty()) {
      ptnt::tap_id_type current_tap = pending_taps.front().first;
      ptnt::asset_flow_limit current_amount = pending_taps.front().second;

      // Get tank, check tap exists and fetch it; if it has an open authority, require it -- if not, anyone can open
      auto tank = db.get(*current_tap.tank_id);
      FC_ASSERT(tank.schematic().taps().count(current_tap.tap_id) != 0, "Tap to open does not exist!");
      const ptnt::tap& tap = tank.schematic().taps().at(current_tap.tap_id);
      if (tap.open_authority.valid())
         my->require_authority(*current_tap.tank_id, *tap.open_authority);
      tap_requirement_utility util(db, current_tap, queries);

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

      // By now, release_limit is the exact amount we will be releasing. Remove it from the tank balance
      tank.balance = tank.balance() - release_limit;
      // Flow the released asset until it stops
      my->report.tap_flows.emplace_back(process_tap_flow(db, current_tap, release_limit, std::move(enqueueTap)));
      pending_taps.pop();
   }

   return std::move(my->report);
}

} } } // namespace graphene::chain::tnt
