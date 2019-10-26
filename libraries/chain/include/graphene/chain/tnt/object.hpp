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

#include <graphene/protocol/tnt/tank_accessory_address.hpp>
#include <graphene/protocol/asset.hpp>

#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {
namespace ptnt = protocol::tnt;

/// Variant capable of containing the address of any stateful accessory
using stateful_accessory_address =
   ptnt::TL::apply<ptnt::TL::apply_each<ptnt::stateful_accessory_list, ptnt::tank_accessory_address>, static_variant>;

/// A map of address to state value for stateful accessory types
using accessory_state_map = flat_map<stateful_accessory_address, ptnt::tank_accessory_state,
                                     ptnt::accessory_address_lt<stateful_accessory_address>>;

/// @brief An asset storage container which is the core of Tanks and Taps, a framework for general smart contract
/// asset management
/// @ingroup object
/// @ingroup TNT
///
/// This is the database object for the Tanks and Taps asset management framework. It represents a tank and tracks
/// the tank's schematic and balance.
class tank_object : public abstract_object<tank_object> {
public:
   static constexpr uint8_t space_id = protocol_ids;
   static constexpr uint8_t type_id = tank_object_type;

   /// The schematic of the tank
   ptnt::tank_schematic schematic;
   /// The balance of the tank (asset id is in the schematic)
   share_type balance;
   /// The deposit being held for this tank (deposit is always CORE asset)
   share_type deposit;
   /// Time of the tank's creation
   time_point_sec creation_date;

   /// Storage of tank accessories' states
   accessory_state_map accessory_states;

   /// Get state by address (const, generic types)
   const ptnt::tank_accessory_state* get_state(const stateful_accessory_address& address) const {
      auto itr = accessory_states.find(address);
      if (itr == accessory_states.end())
         return nullptr;
      return &itr->second;
   }
   /// Get state by address (const, specific types)
   template<typename Accessory, typename State = typename Accessory::state_type>
   const State* get_state(const ptnt::tank_accessory_address<Accessory>& address) const {
      auto itr = accessory_states.find(address);
      if (itr == accessory_states.end())
         return nullptr;
      FC_ASSERT(itr->second.template is_type<State>(), "Accessory state has unexpected type");
      return &itr->second.template get<State>();
   }
   /// Get state by address (mutable, generic types)
   ptnt::tank_accessory_state* get_state(const stateful_accessory_address& address) {
      return const_cast<ptnt::tank_accessory_state*>(const_cast<const tank_object*>(this)->get_state(address));
   }
   /// Get state by address (mutable, specific types)
   template<typename Accessory, typename State = typename Accessory::state_type>
   State* get_state(const ptnt::tank_accessory_address<Accessory>& address) {
      return const_cast<State*>(const_cast<const tank_object*>(this)->get_state(address));
   }
   /// Get state by address, creating a default one if none yet exists (generic types)
   ptnt::tank_accessory_state& get_or_create_state(const stateful_accessory_address& address) {
      auto itr = accessory_states.find(address);
      if (itr == accessory_states.end()) {
         itr = accessory_states.insert(std::make_pair(address, ptnt::tank_accessory_state())).first;
         itr->second.set_which(address.which());
      }
      return itr->second;
   }
   /// Get state by address, creating a default one if none yet exists (specific types)
   template<typename Accessory, typename State = typename Accessory::state_type>
   State& get_or_create_state(const ptnt::tank_accessory_address<Accessory>& address) {
      auto itr = accessory_states.find(address);
      if (itr == accessory_states.end()) {
         auto state = std::make_pair(stateful_accessory_address(address), ptnt::tank_accessory_state(State()));
         itr = accessory_states.insert(std::move(state)).first;
      }
      return itr->second.template get<State>();
   }

   /// Delete state for any/all requirements on the specified tap
   void clear_tap_state(ptnt::index_type tap_ID);
   /// Delete state for the supplied attachment ID
   void clear_attachment_state(ptnt::index_type attachment_ID);

   /// Get the specifically typed ID
   tank_id_type get_id() const { return id; }
};

using tank_object_index_type = multi_index_container<
   tank_object,
   indexed_by<
      ordered_unique<tag<by_id>, member<object, object_id_type, &object::id>>
   >
>;
using tank_index = generic_index<tank_object, tank_object_index_type>;

} } // namespace graphene::chain

MAP_OBJECT_ID_TO_TYPE(graphene::chain::tank_object)

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(graphene::chain::tank_object)

// This reflection information cannot be moved to the .cpp file as with the other objects, because it must be visible
// to the evaluation code.
FC_REFLECT(graphene::chain::tank_object,
           (schematic)(balance)(deposit)(creation_date)(accessory_states))
