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

   /// Storage of tank attachments' states
   map<tnt::index_type, tnt::tank_attachment_state> attachment_states;
   /// Storage of tap requirements' states (index is [tap_ID, requirement_index])
   map<pair<tnt::index_type, tnt::index_type>, tnt::tap_requirement_state> requirement_states;
   /// Cache of the ID of the tank's deposit_source_restrictor, if it has one
   optional<tnt::index_type> restrictor_ID;
};

} } // namespace graphene::chain

MAP_OBJECT_ID_TO_TYPE(graphene::chain::tank_object)

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(graphene::chain::tank_object)

FC_REFLECT(graphene::chain::tank_object, (schematic)(balance)(attachment_states)(requirement_states)(restrictor_ID))
