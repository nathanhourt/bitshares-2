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

#include <fc/static_variant.hpp>

namespace graphene { namespace protocol { namespace tnt {

struct asset_flow_meter;
struct deposit_source_restrictor;
struct tap_opener;
struct attachment_connect_authority;
using tank_attachment = fc::static_variant<asset_flow_meter, deposit_source_restrictor, tap_opener,
                                           attachment_connect_authority>;

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
using tap_requirement = fc::static_variant<immediate_flow_limit, cumulative_flow_limit, periodic_flow_limit,
                                           time_lock, minimum_tank_level, review_requirement,
                                           documentation_requirement, delay_requirement, hash_preimage_requirement,
                                           ticket_requirement, exchange_requirement>;

} } } // namespace graphene::protocol::tnt
