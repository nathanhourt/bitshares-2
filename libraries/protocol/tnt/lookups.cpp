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

#include <graphene/protocol/tnt/lookups.hpp>

#include "type_checks.hxx"

namespace graphene { namespace protocol { namespace tnt {

template<typename Result>
bool is_error(const lookup_result<Result>& r) { return !r.template is_type<const_ref<Result>>(); }
bool is_error(const connection_asset& sa) {
   return sa.is_type<need_lookup_function>() || sa.is_type<nonexistent_object>();
}
bool is_error(const attachment_asset& aa) { return !aa.is_type<asset_id_type>(); }
bool is_error(const connection_chain_result& scr) { return !scr.is_type<connection_chain>(); }
bool is_error(const attachment_connection_result& asr) { return !asr.is_type<const_ref<connection>>(); }

lookup_result<tank_schematic> lookup_utilities::lookup_tank(optional<tank_id_type> id) const {
   if (!id.valid()) return std::cref(current_tank);
   if (!get_tank)
      return need_lookup_function();
   auto tank = get_tank(*id);
   if (tank == nullptr)
      return nonexistent_object{*id};
   return std::cref(*tank);
}

lookup_result<tank_attachment> lookup_utilities::lookup_attachment(attachment_id_type id) const {
   CHECK_TANK_RESULT();
   auto tank_result = lookup_tank(id.tank_id);
   if (is_error(tank_result))
      return lookup_result<tank_attachment>::import_from(tank_result);
   const tank_schematic& tank = tank_result.get<const_ref<tank_schematic>>();

   auto itr = tank.attachments.find(id.attachment_id);
   if (itr != tank.attachments.end())
      return std::cref(itr->second);
   return nonexistent_object{id};
}

attachment_asset lookup_utilities::get_attachment_asset(const attachment_id_type &id) const {
   CHECK_ATTACHMENT_RESULT();
   auto attachment_result = lookup_attachment(id);
   if (is_error(attachment_result))
      return attachment_asset::import_from(attachment_result);
   const tank_attachment& attachment = attachment_result.get<const_ref<tank_attachment>>();

   return fc::typelist::runtime::dispatch(tank_attachment::list(), attachment.which(),
                                          [&id, &attachment](auto t) -> attachment_asset {
      auto result = attachment_get_asset_received(attachment.get<typename decltype(t)::type>());
      if (result.valid())
         return *result;
      return no_asset{id};
   });
}

template<typename Attachment, std::enable_if_t<Attachment::can_receive_asset, bool> = true>
optional<const_ref<connection>> get_attachment_connection_impl(const Attachment& a) { return a.output_connection(); }
template<typename Attachment, std::enable_if_t<!Attachment::can_receive_asset, bool> = true>
optional<const_ref<connection>> get_attachment_connection_impl(const Attachment&) { return {}; }

attachment_connection_result lookup_utilities::get_attachment_connection(const attachment_id_type &id) const {
   CHECK_ATTACHMENT_CONNECTION_RESULT();
   auto attachment_result = lookup_attachment(id);
   if (is_error(attachment_result)) return attachment_connection_result::import_from(attachment_result);
   const tank_attachment& attachment = attachment_result.get<const_ref<tank_attachment>>();

   return fc::typelist::runtime::dispatch(tank_attachment::list(), attachment.which(),
                                          [&id, &attachment](auto t) -> attachment_connection_result {
      auto result = get_attachment_connection_impl(attachment.get<typename decltype(t)::type>());
      if (result.valid())
         return *result;
      return bad_connection{bad_connection::receives_no_asset, id};
   });
}

connection_asset lookup_utilities::get_connection_asset(const connection &s) const {
   struct {
      const lookup_utilities& utils;
      connection_asset operator()(const same_tank&) const { return utils.current_tank.asset_type; }
      connection_asset operator()(const account_id_type&) const { return any_asset(); }
      connection_asset operator()(const tank_id_type& id) const {
         auto tank_result = utils.lookup_tank(id);
         if (is_error(tank_result))
            return connection_asset::import_from(tank_result);
         return tank_result.get<const_ref<tank_schematic>>().get().asset_type;
      }
      connection_asset operator()(const attachment_id_type& id) const {
         return utils.get_attachment_asset(id);
      }
   } connection_visitor{*this};
   return s.visit(connection_visitor);
}

connection_chain_result lookup_utilities::get_connection_chain(const_ref<connection> s, size_t max_chain_length,
                                                   optional<asset_id_type> asset_type) const {
   auto check_asset = [&asset_type, this](const connection& s) -> optional<connection_chain_result> {
      if (!asset_type.valid()) return {};
      CHECK_CONNECTION_ASSET_RESULT();
      auto connection_asset = get_connection_asset(s);
      if (connection_asset.is_type<any_asset>() || connection_asset.is_type<need_lookup_function>()) return {};
      if (connection_asset.is_type<asset_id_type>()) {
         if (connection_asset.get<asset_id_type>() == *asset_type)
            return {};
         return bad_connection{bad_connection::receives_wrong_asset, s};
      }
      if (connection_asset.is_type<no_asset>())
         return bad_connection{bad_connection::receives_no_asset, s};
      return connection_asset.get<nonexistent_object>();
   };
   auto check_result = check_asset(s);
   if (check_result.valid()) return *check_result;

   connection_chain chain(s);
   while (!is_terminal_connection(chain.connections.back())) {
      if (chain.connections.size() > max_chain_length)
         return exceeded_max_chain_length();

      s = chain.connections.back();
      auto attachment_id = s.get().get<attachment_id_type>();
      if (attachment_id.tank_id.valid())
         chain.final_connection_tank = attachment_id.tank_id;
      else
         attachment_id.tank_id = chain.final_connection_tank;

      CHECK_CONNECTION_CHAIN_RESULT();
      auto connection_result = get_attachment_connection(attachment_id);
      if (is_error(connection_result))
         return connection_chain_result::import_from(connection_result);
      if ((check_result = check_asset(s)).valid())
         return *check_result;
      chain.connections.push_back(connection_result.get<const_ref<connection>>());
   }

   if ((check_result = check_asset(s)).valid())
      return *check_result;
   return connection_chain_result(chain);
}

} } } // namespace graphene::protocol::tnt
