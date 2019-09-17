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
/// @file Forward declares tank accessory types. Full declarations can be found in types.hpp
#pragma once

#include <fc/static_variant.hpp>

namespace graphene { namespace protocol { namespace tnt {

// Tank attachment types:
struct asset_flow_meter;
struct deposit_source_restrictor;
struct tap_opener;
struct attachment_connect_authority;

// Tap requirement types:
struct immediate_flow_limit;
struct cumulative_flow_limit;
struct periodic_flow_limit;
struct time_lock;
struct minimum_tank_level;
struct review_requirement;
struct documentation_requirement;
struct delay_requirement;
struct hash_preimage_requirement;
struct ticket_requirement;
struct exchange_requirement;

/// Tank accessories are objects which add functionality to a tank, including tap requirements and tank attachments
using tank_accessory_list = fc::typelist::list<
   /*00*/asset_flow_meter,
   /*01*/deposit_source_restrictor,
   /*02*/tap_opener,
   /*03*/attachment_connect_authority,
   /*04*/immediate_flow_limit,
   /*05*/cumulative_flow_limit,
   /*06*/periodic_flow_limit,
   /*07*/time_lock,
   /*08*/minimum_tank_level,
   /*19*/review_requirement,
   /*10*/documentation_requirement,
   /*11*/delay_requirement,
   /*12*/hash_preimage_requirement,
   /*13*/ticket_requirement,
   /*14*/exchange_requirement
>;

/// Enumeration of the different tank accessory types
enum tank_accessory_type_enum {
   tank_attachment_accessory_type,
   tap_requirement_accessory_type
};

/// Typelist filter to select tank accessories by type
template<tank_accessory_type_enum AccessoryType>
struct accessory_filter {
   template<typename Accessory>
   struct filter { constexpr static bool value = Accessory::accessory_type == AccessoryType; };
};
using tank_attachment_filter = accessory_filter<tank_attachment_accessory_type>;
using tap_requirement_filter = accessory_filter<tap_requirement_accessory_type>;

} } } // namespace graphene::protocol::tnt
