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
#pragma once

#include <graphene/protocol/tnt/types.hpp>

namespace graphene { namespace protocol { namespace tnt {

/// A callback function type to lookup a tank schematic by ID; returns null if tank does not exist
using tank_lookup_function = std::function<const tank_schematic*(tank_id_type)>;

template<class T>
using const_ref = std::reference_wrapper<const T>;

/// A result type indicating that the requested lookup referenced an item that did not exist
struct nonexistent_object {
   static_variant<tank_id_type, attachment_id_type, tap_id_type> object;
};
/// A result type indicating that the requested lookup could not be performed without a tank_lookup_function
struct need_lookup_function{};
/// A result type for a lookup
template<typename Expected>
using lookup_result = static_variant<const_ref<Expected>, need_lookup_function, nonexistent_object>;

/// A result type indicating that a sink can receive all asset types (i.e., sink is an account_id_type)
struct any_asset{};
/// A result type indicating that a referenced tank attachment cannot receive any asset
struct no_asset {
   /// The attachment that receives no asset
   attachment_id_type attachment_id;
};
/// A result type for what asset a sink receives
using sink_asset = static_variant<asset_id_type, any_asset, no_asset, need_lookup_function, nonexistent_object>;
/// A result type for what asset a tank attachment receives
using attachment_asset = static_variant<asset_id_type, no_asset, need_lookup_function, nonexistent_object>;

/// A chain of sinks where each sink deposits to the one following it until the final sink releases to a destination
struct sink_chain {
   /// Sinks in the chain
   vector<const_ref<sink>> sinks;
   /// "Current tank" for the final sink in the chain. This is null if and only if the chain never connects to a
   /// remote tank.
   optional<tank_id_type> final_sink_tank;

   sink_chain() = default;
   sink_chain(const_ref<sink> first_sink) : sinks{{first_sink}} {}
};
/// A result type indicating that a sink is incapable of receiving the provided asset
struct bad_sink {
   enum reason_enum { receives_wrong_asset, receives_no_asset };
   reason_enum reason;
   const sink& s;
};
/// A result type for the sink a tank attachment deposits to
using attachment_sink_result = static_variant<const_ref<sink>, bad_sink, need_lookup_function, nonexistent_object>;
/// A result type indicating that a sink chain is longer than the maximum length
struct exceeded_max_chain_length {};
/// A result type for the destination a sink chain deposits to
using sink_chain_result = static_variant<sink_chain, exceeded_max_chain_length, bad_sink,
                                         need_lookup_function, nonexistent_object>;

/// A class providing information retrieval utilities for tanks, tank accessories, and sinks
class lookup_utilities {
protected:
   const tank_schematic& current_tank;
   const tank_lookup_function& get_tank;

public:
   /// Create a utilities object using the provided tank_lookup_function. If function is not provided, all checks of
   /// references to external tanks or accessories thereof will be skipped.
   lookup_utilities(const tank_schematic& current_tank, const tank_lookup_function& tank_lookup = {})
       : current_tank(current_tank), get_tank(tank_lookup) {}

   /// @brief Lookup tank by ID, returning the current tank if ID is null
   lookup_result<tank_schematic> lookup_tank(optional<tank_id_type> id) const;
   /// @brief Lookup attachment by ID
   lookup_result<tank_attachment> lookup_attachment(attachment_id_type id) const;

   /// @brief Lookup what asset type a tank_attachment can receive
   attachment_asset get_attachment_asset(const attachment_id_type& id) const;
   /// @brief Lookup what sink a tank_attachment releases received asset to
   attachment_sink_result get_attachment_sink(const attachment_id_type& id) const;
   /// @brief Lookup what asset type(s) a sink can receive
   sink_asset get_sink_asset(const sink& s) const;

   /// @brief Get the chain of sinks starting at the provided sink
   /// @param s The sink to begin traversal at
   /// @param max_chain_length The maximum number of sinks to follow
   /// @param asset_type [Optional] If provided, checks that all sinks in chain accept supplied asset type
   ///
   /// Sinks receive asset when it is released and specify where it should go next. The location specified by a sink
   /// is not necessarily a depository that stores asset over time; rather, sinks can point to tank attachments,
   /// which cannot store asset and must immediately release it to another sink. Thus tank attachments (and perhaps
   /// other sink targets in the future) can form chains of sinks which must eventually terminate in a depository.
   ///
   /// This function follows a chain of sinks to find the asset depository that the provided sink eventually deposits
   /// to, and returns the full chain. It will detect if the chain references any nonexistent objects, and it can
   /// optionally check that all sinks in the chain accept the provided asset type.
   sink_chain_result get_sink_chain(const_ref<sink> s, size_t max_chain_length,
                                    optional<asset_id_type> asset_type = {}) const;
};

} } } // namespace graphene::protocol::tnt

FC_REFLECT(graphene::protocol::tnt::nonexistent_object, (object))
FC_REFLECT(graphene::protocol::tnt::need_lookup_function, )
FC_REFLECT(graphene::protocol::tnt::any_asset, )
FC_REFLECT(graphene::protocol::tnt::no_asset, (attachment_id))
FC_REFLECT_TYPENAME(graphene::protocol::tnt::sink_asset)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::attachment_asset)
FC_REFLECT_ENUM(graphene::protocol::tnt::bad_sink::reason_enum,
                (receives_wrong_asset)(receives_no_asset))
FC_REFLECT(graphene::protocol::tnt::sink_chain, (sinks)(final_sink_tank))
FC_REFLECT(graphene::protocol::tnt::bad_sink, (reason))
FC_REFLECT(graphene::protocol::tnt::exceeded_max_chain_length, )
FC_REFLECT_TYPENAME(graphene::protocol::tnt::sink_chain_result)

namespace fc {
template<class T>
struct get_typename<std::reference_wrapper<T>> {
   static const char* name() {
      static auto n = string("reference_wrapper<") + get_typename<T>::name() + ">";
      return n.c_str();
   }
};
template<class T>
struct get_typename<std::reference_wrapper<const T>> {
   static const char* name() {
      static auto n = string("reference_wrapper<const ") + get_typename<T>::name() + ">";
      return n.c_str();
   }
};
}
