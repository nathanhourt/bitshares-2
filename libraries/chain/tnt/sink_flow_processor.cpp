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

#include <graphene/chain/tnt/sink_flow_processor.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

namespace graphene { namespace chain { namespace tnt {

struct sink_flow_processor_impl {
   cow_db_wrapper& db;
   TapOpenCallback cbOpenTap;
   FundAccountCallback cbFundAccount;

   sink_flow_processor_impl(cow_db_wrapper& db, TapOpenCallback cbOpenTap, FundAccountCallback cbFundAccount)
      : db(db), cbOpenTap(cbOpenTap), cbFundAccount(cbFundAccount) {}
};

sink_flow_processor::sink_flow_processor(cow_db_wrapper& db, TapOpenCallback cbOpenTap,
                                         FundAccountCallback cbFundAccount) {
   my = std::make_unique<sink_flow_processor_impl>(db, cbOpenTap, cbFundAccount);
}

sink_flow_processor::~sink_flow_processor() = default;

class attachment_receive_inspector {
   tank_object& tank;
   const asset& amount;
   const sink_flow_processor_impl& data;
   attachment_receive_inspector(tank_object& tank, const asset& amount, const sink_flow_processor_impl& data)
      : tank(tank), amount(amount), data(data) {}

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
      data.cbOpenTap(ptnt::tap_id_type{tank.id, opener.tap_index}, opener.release_amount);
      return opener.destination_sink;
   }

public:
   static ptnt::sink inspect(tank_object& tank, ptnt::index_type attachment_ID, const asset& amount,
                             const sink_flow_processor_impl& data) {
      attachment_receive_inspector inspector(tank, amount, data);
      const auto& attachment = tank.schematic.attachments.at(attachment_ID);
      return ptnt::TL::runtime::dispatch(ptnt::tank_attachment::list(), attachment.which(),
                                  [&attachment, attachment_ID, &inspector](auto t) {
         return inspector(attachment.get<typename decltype(t)::type>(), {attachment_ID});
      });
   }
};

vector<ptnt::sink> sink_flow_processor::release_to_sink(ptnt::sink origin, ptnt::sink sink, asset amount) {
   FC_ASSERT(!origin.is_type<ptnt::same_tank>(), "Cannot process sink flow from origin of 'same_tank'");
   vector<ptnt::sink> sink_path;
   optional<tank_id_type> current_tank;
   if (origin.is_type<tank_id_type>())
      current_tank = origin.get<tank_id_type>();

   try {
   while (!ptnt::is_terminal_sink(sink)) {
      auto max_sinks = my->db.get_db().get_global_properties()
                       .parameters.extensions.value.updatable_tnt_options->max_sink_chain_length;
      FC_ASSERT(sink_path.size() < max_sinks, "Tap flow has exceeded the maximm sink chain length.");

      // At present, the only non-terminal sink type is a tank attachment
      ptnt::attachment_id_type att_id = sink.get<ptnt::attachment_id_type>();
      if (att_id.tank_id.valid())
         current_tank = *att_id.tank_id;
      else if (current_tank.valid())
         att_id.tank_id = *current_tank;
      else
         FC_THROW_EXCEPTION(fc::assert_exception,
                            "Could not process sink flow: sink specifies a tank attachment with implied tank ID "
                            "outside the context of any \"current tank\"");

      sink_path.emplace_back(std::move(sink));
      sink = attachment_receive_inspector::inspect(my->db.get(*current_tank), att_id.attachment_id, amount, *my);
   }

   if (sink.is_type<ptnt::same_tank>()) {
      FC_ASSERT(current_tank.valid(), "Could not process sink flow: sink specifies a tank attachment with implied "
                                      "tank ID outside the context of any \"current tank\"");
      sink = *current_tank;
   }
   // Complete the sink_path
   sink_path.emplace_back(sink);

   // Process deposit to the terminal sink
   if (sink.is_type<tank_id_type>()) {
      // Terminal sink is a tank
      auto dest_tank = sink.get<tank_id_type>()(my->db);
      // Check tank's asset type
      FC_ASSERT(dest_tank.schematic().asset_type() == amount.asset_id,
                "Destination tank of tap flow stores asset ID ${D}, but tap flow asset ID was ${F}",
                ("D", dest_tank.schematic().asset_type())("F", amount.asset_id));
      // Check the tank's deposit source restrictions
      if (dest_tank.restrictor_ID().valid()) {
         const auto& restrictor = dest_tank.schematic().attachments().at(*dest_tank.restrictor_ID())
                                  .get<ptnt::deposit_source_restrictor>();
         ptnt::deposit_source_restrictor::deposit_path path;
         path.origin = origin;
         path.sink_chain.assign(sink_path.begin(), sink_path.end());
         FC_ASSERT(restrictor.get_matching_deposit_path(path, ((const tank_object&)dest_tank).id));
      }
      // Update tank's balance
      dest_tank.balance = dest_tank.balance() + amount.amount;
   } else if (sink.is_type<account_id_type>()) {
      // Terminal sink is an account
      auto account = sink.get<account_id_type>();
      // Check account is authorized to hold the asset
      FC_ASSERT(is_authorized_asset(my->db.get_db(), account(my->db.get_db()), amount.asset_id(my->db.get_db())),
                "Could not process sink flow: terminal sink is an account which is unauthorized to hold the asset");
      // Use callback to pay the account
      vector<ptnt::sink> fullPath(sink_path.size() + 1);
      fullPath.emplace_back(origin);
      fullPath.insert(fullPath.end(), sink_path.begin(), sink_path.end());
      my->cbFundAccount(account, amount, std::move(fullPath));
   }
   } FC_CAPTURE_AND_RETHROW( (sink_path) )

   return sink_path;
}

} } } // graphene::chain::tnt
