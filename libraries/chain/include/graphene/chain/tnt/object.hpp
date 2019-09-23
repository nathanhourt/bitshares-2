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
#pragma once

#include <graphene/chain/types.hpp>

#include <graphene/db/generic_index.hpp>

#include <graphene/protocol/asset.hpp>
#include <graphene/protocol/tnt/types.hpp>

namespace graphene { namespace chain {

namespace impl {
/// Variant capable of containing the address of any stateful accessory
using stateful_accessory_address = tnt::TL::apply<tnt::TL::apply_each<tnt::stateful_tank_accessory_list,
                                                                      tnt::tank_accessory_address>, static_variant>;

/// Comparator for stateful_accessory_address types; uses the following semantics:
///  - Ordering is done according to address value, not type
///  - Tank attachment addresses are ordered by their attachment IDs
///  - Tap requirement addresses are ordered by their (tap ID, requirement index) pairs
///  - Tank attachment addresses are ordered before tap requirement addresses
/// The comparator also accepts tap_id_type operands, which match as equal to all requirement addresses on that tap
struct address_lt {
   using is_transparent = void;
   template<typename Address, typename = std::enable_if_t<stateful_accessory_address::can_store<Address>()>>
   constexpr static bool is_requirement_address = (Address::accessory_type::accessory_type ==
                                                   tnt::tap_requirement_accessory_type);

   template<typename Address_A, typename Address_B, std::enable_if_t<!is_requirement_address<Address_A> &&
                                                                     !is_requirement_address<Address_B>, bool> = true>
   bool compare(const Address_A& a, const Address_B& b) const {
      return a.attachment_ID < b.attachment_ID;
   }
   template<typename Address_A, typename Address_B, std::enable_if_t<is_requirement_address<Address_A> &&
                                                                     is_requirement_address<Address_B>, bool> = true>
   bool compare(const Address_A& a, const Address_B& b) const {
      return std::make_pair(a.tap_ID, a.requirement_index) < std::make_pair(b.tap_ID, b.requirement_index);
   }
   template<typename Address_A, typename Address_B, std::enable_if_t<is_requirement_address<Address_A> !=
                                                                     is_requirement_address<Address_B>, bool> = true>
   bool compare(const Address_A&, const Address_B&) const {
      return !is_requirement_address<Address_A>;
   }
   template<typename Address, std::enable_if_t<is_requirement_address<Address>, bool> = true>
   bool compare(const Address& a, const tnt::tap_id_type& tid) const { return a.tap_ID < tid.tap_id; }
   template<typename Address, std::enable_if_t<!is_requirement_address<Address>, bool> = true>
   bool compare(const Address&, const tnt::tap_id_type&) const { return true; }
   template<typename Address, std::enable_if_t<is_requirement_address<Address>, bool> = true>
   bool compare(const tnt::tap_id_type& tid, const Address& a) const { return tid.tap_id < a.tap_ID; }
   template<typename Address, std::enable_if_t<!is_requirement_address<Address>, bool> = true>
   bool compare(const tnt::tap_id_type&, const Address&) const { return false; }
   bool operator()(const stateful_accessory_address& a, const stateful_accessory_address& b) const {
      return tnt::TL::runtime::dispatch(stateful_accessory_address::list(), a.which(), [this, &a, &b] (auto A) {
         using A_type = typename decltype(A)::type;
         return tnt::TL::runtime::dispatch(stateful_accessory_address::list(), b.which(), [this, &a, &b](auto B) {
            using B_type = typename decltype(B)::type;
            return this->compare(a.get<A_type>(), b.get<B_type>());
         });
      });
   }
   bool operator()(const stateful_accessory_address& a, const tnt::tap_id_type& tid) const {
      return tnt::TL::runtime::dispatch(stateful_accessory_address::list(), a.which(), [this, &a, &tid](auto A) {
         return this->compare(a.get<typename decltype(A)::type>(), tid);
      });
   }
   bool operator()(const tnt::tap_id_type& tid, const stateful_accessory_address& a) const {
      return tnt::TL::runtime::dispatch(stateful_accessory_address::list(), a.which(), [this, &tid, &a](auto A) {
         return this->compare(tid, a.get<typename decltype(A)::type>());
      });
   }
};

/// A map of address to state value for stateful accessory types
using accessory_state_map = flat_map<stateful_accessory_address, tnt::tank_accessory_state, address_lt>;
}

/// @brief An asset storage container which is the core of Tanks and Taps, a framework for general smart contract
/// asset management
/// @ingroup object
/// @ingroup implementation
/// @ingroup TNT
///
/// This is the database object for the Tanks and Taps asset management framework. It represents a tank and tracks
/// the tank's schematic and balance.
class tank_object : public abstract_object<tank_object> {
public:
   static constexpr uint8_t space_id = protocol_ids;
   static constexpr uint8_t type_id = tank_object_type;

   /// The schematic of the tank
   tnt::tank_schematic schematic;
   /// The balance of the tank
   asset_store balance;
   /// The deposit being held for this tank
   asset_store deposit;

   /// Storage of tank accessories' states
   impl::accessory_state_map accessory_states;
   /// Cache of the ID of the tank's deposit_source_restrictor, if it has one
   optional<tnt::index_type> restrictor_ID;

   /// Delete state for any/all requirements on the specified tap
   void clear_tap_state(tnt::index_type tap_ID);
   /// Delete state for the supplied attachment ID
   void clear_attachment_state(tnt::index_type attachment_ID);
};

} } // namespace graphene::chain

MAP_OBJECT_ID_TO_TYPE(graphene::chain::tank_object)

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(graphene::chain::tank_object)

FC_REFLECT(graphene::chain::tank_object, (schematic)(balance)(accessory_states)(restrictor_ID))
