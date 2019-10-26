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

#include <graphene/chain/tnt/connection_flow_processor.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

namespace graphene { namespace chain { namespace tnt {

struct connection_flow_processor_impl {
   cow_db_wrapper& db;
   TapOpenCallback cbOpenTap;
   FundAccountCallback cbFundAccount;

   connection_flow_processor_impl(cow_db_wrapper& db, TapOpenCallback cbOpenTap, FundAccountCallback cbFundAccount)
      : db(db), cbOpenTap(cbOpenTap), cbFundAccount(cbFundAccount) {}
};

connection_flow_processor::connection_flow_processor(cow_db_wrapper& db, TapOpenCallback cbOpenTap,
                                         FundAccountCallback cbFundAccount) {
   my = std::make_unique<connection_flow_processor_impl>(db, cbOpenTap, cbFundAccount);
}

connection_flow_processor::~connection_flow_processor() = default;

optional<tank_id_type> get_connection_tank(const ptnt::remote_connection& c) {
   if (c.is_type<tank_id_type>())
      return c.get<tank_id_type>();
   if (c.is_type<ptnt::attachment_id_type>()) {
      return c.get<ptnt::attachment_id_type>().tank_id;
   }
   return {};
}

class attachment_receive_inspector {
   tank_object& tank;
   const asset& amount;
   const ptnt::connection& source;
   const connection_flow_processor_impl& data;
   attachment_receive_inspector(tank_object& tank, const asset& amount, const ptnt::connection& source,
                                const connection_flow_processor_impl& data)
      : tank(tank), amount(amount), source(source), data(data) {}

   using NonReceivingAttachments = ptnt::TL::list<ptnt::attachment_connect_authority>;
   template<typename Attachment,
            std::enable_if_t<ptnt::TL::contains<NonReceivingAttachments, Attachment>(), bool> = true>
   [[noreturn]] ptnt::connection operator()(const Attachment&, ptnt::tank_accessory_address<Attachment>) {
      FC_THROW_EXCEPTION(fc::assert_exception, "INTERNAL ERROR: Tried to flow asset to an attachment which cannot "
                                               "receive asset. Please report this error.");
   }

   void check_source_restriction(const ptnt::authorized_connections_type& allowed, ptnt::connection dest) {
      // If all sources are authorized, there's nothing to check
      if (allowed.is_type<ptnt::all_sources>())
         return;
      const auto& authorized = allowed.get<flat_set<ptnt::remote_connection>>();
      auto remote_source = ptnt::remote_connection::import_from(source);
      auto remote_tank = get_connection_tank(remote_source);
      // If the destination is on the same tank as the source, there's nothing to check
      if (remote_tank.valid() && *remote_tank == tank.get_id())
         return;
      FC_ASSERT(authorized.count(remote_source) > 0,
                "Cannot process connection flow ${S} -> ${D}: destination does not allow deposits from source",
                ("S", remote_source)("D", dest));
   }

   ptnt::connection operator()(const ptnt::asset_flow_meter& meter,
                               ptnt::tank_accessory_address<ptnt::asset_flow_meter> address) {
      check_source_restriction(meter.authorized_sources(),
                               ptnt::attachment_id_type{tank.get_id(), address.attachment_ID});
      FC_ASSERT(meter.asset_type == amount.asset_id,
                "Flowed wrong type of asset to flow meter. Meter expects ${O} but received ${A}",
                ("O", meter.asset_type)("A", amount.asset_id));
      auto& state = tank.get_or_create_state(address);
      state.metered_amount += amount.amount;
      return meter.destination;
   }
   ptnt::connection operator()(const ptnt::tap_opener& opener,
                               ptnt::tank_accessory_address<ptnt::tap_opener> address) {
      check_source_restriction(opener.authorized_sources(),
                               ptnt::attachment_id_type{tank.get_id(), address.attachment_ID});
      FC_ASSERT(opener.asset_type == amount.asset_id,
                "Flowed wrong type of asset to tap opener. Opener expects ${O} but received ${A}",
                ("O", opener.asset_type)("A", amount.asset_id));
      data.cbOpenTap(ptnt::tap_id_type{tank.id, opener.tap_index}, opener.release_amount);
      return opener.destination;
   }

public:
   static ptnt::connection inspect(tank_object& tank, ptnt::index_type attachment_ID, const asset& amount,
                             const ptnt::connection& source, const connection_flow_processor_impl& data) {
      attachment_receive_inspector inspector(tank, amount, source, data);
      const auto& attachment = tank.schematic.attachments.at(attachment_ID);
      return ptnt::TL::runtime::dispatch(ptnt::tank_attachment::list(), attachment.which(),
                                  [&attachment, attachment_ID, &inspector](auto t) {
         return inspector(attachment.get<typename decltype(t)::type>(), {attachment_ID});
      });
   }
};

vector<ptnt::connection> connection_flow_processor::release_to_connection(ptnt::connection origin,
                                                                          ptnt::connection connection, asset amount) {
   FC_ASSERT(!origin.is_type<ptnt::same_tank>(), "Cannot process connection flow from origin of 'same_tank'");
   vector<ptnt::connection> connection_path;
   optional<tank_id_type> current_tank;
   if (origin.is_type<tank_id_type>())
      current_tank = origin.get<tank_id_type>();

   try {
   while (!ptnt::is_terminal_connection(connection)) {
      auto max_connections = my->db.get_db().get_global_properties()
                       .parameters.extensions.value.updatable_tnt_options->max_connection_chain_length;
      FC_ASSERT(connection_path.size() < max_connections,
                "Tap flow has exceeded the maximm connection chain length.");

      // At present, the only non-terminal connection type is a tank attachment
      ptnt::attachment_id_type& att_id = connection.get<ptnt::attachment_id_type>();
      if (att_id.tank_id.valid())
         current_tank = *att_id.tank_id;
      else if (current_tank.valid())
         att_id.tank_id = *current_tank;
      else
         FC_THROW_EXCEPTION(fc::assert_exception,
                            "Could not process connection flow: connection specifies a tank attachment with implied "
                            "tank ID outside the context of any \"current tank\"");

      connection =
            attachment_receive_inspector::inspect(my->db.get(*current_tank), att_id.attachment_id, amount,
                                                  (connection_path.empty()? origin : connection_path.back()), *my);
      connection_path.emplace_back(std::move(connection));
   }

   if (connection.is_type<ptnt::same_tank>()) {
      FC_ASSERT(current_tank.valid(), "Could not process connection flow: connection specifies a tank attachment "
                                      "with implied tank ID outside the context of any \"current tank\"");
      connection = *current_tank;
   }
   // Save the next to last connection so we can check the deposit source
   auto penultimate_connection =
         ptnt::remote_connection::import_from(connection_path.empty()? origin : connection_path.back());
   // Complete the connection_path
   connection_path.emplace_back(connection);

   // Process deposit to the terminal connection
   if (connection.is_type<tank_id_type>()) {
      // Terminal connection is a tank
      auto dest_tank = connection.get<tank_id_type>()(my->db);
      const tank_object& tank_ref = dest_tank;
      // Check tank's asset type
      FC_ASSERT(tank_ref.schematic.asset_type == amount.asset_id,
                "Destination tank of tap flow stores asset ID ${D}, but tap flow asset ID was ${F}",
                ("D", tank_ref.schematic.asset_type)("F", amount.asset_id));
      // Check the tank's deposit source restrictions
      auto source_tank = get_connection_tank(penultimate_connection);
      // If the source tank is the same as the dest tank, or the dest allows all sources, we can skip the check
      if (!(source_tank.valid() && *source_tank == tank_ref.get_id()) &&
          tank_ref.schematic.remote_sources.is_type<flat_set<ptnt::remote_connection>>()) {
         const auto& authorized = tank_ref.schematic.remote_sources.get<flat_set<ptnt::remote_connection>>();
         FC_ASSERT(authorized.count(penultimate_connection),
                   "Cannot process connection flow: terminal tank does not allow deposits from source: ${S} -> ${D}",
                   ("S", penultimate_connection)("D", tank_ref.get_id()));
      }
      // Update tank's balance
      dest_tank.balance = dest_tank.balance() + amount.amount;
   } else if (connection.is_type<account_id_type>()) {
      // Terminal connection is an account
      auto account = connection.get<account_id_type>();
      // Check account is authorized to hold the asset
      FC_ASSERT(is_authorized_asset(my->db.get_db(), account(my->db.get_db()), amount.asset_id(my->db.get_db())),
                "Could not process connection flow: terminal connection is an account which is unauthorized to hold "
                "the asset");
      // Use callback to pay the account
      vector<ptnt::connection> fullPath(connection_path.size() + 1);
      fullPath.emplace_back(origin);
      fullPath.insert(fullPath.end(), connection_path.begin(), connection_path.end());
      my->cbFundAccount(account, amount, std::move(fullPath));
   }
   } FC_CAPTURE_AND_RETHROW( (connection_path) )

   return connection_path;
}

} } } // graphene::chain::tnt
