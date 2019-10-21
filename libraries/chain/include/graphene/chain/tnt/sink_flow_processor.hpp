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

#include <graphene/chain/tnt/cow_db_wrapper.hpp>
#include <graphene/chain/tnt/object.hpp>

namespace graphene { namespace chain { namespace tnt {
struct sink_flow_processor_impl;

/// A callback the @ref sink_flow_processor can use to notify the caller that sink flow processing has requested a
/// tap be opened. Callback is provided with the ID of the tap to open and the requested flow limit
using TapOpenCallback = std::function<void(ptnt::tap_id_type, ptnt::asset_flow_limit)>;
/// A callback the @ref sink_flow_processor can use to deposit flowed asset to an account's balance. Callback is
/// provided with the ID of the account, the amount received, and the path of the asset flow including the origin.
/// Account will have already been checked for authorization to hold the asset when the callback is invoked.
using FundAccountCallback = std::function<void(account_id_type, asset, vector<ptnt::sink>)>;

/**
 * @brief Processes release of asset into a sink, including the movement of asset along the sink chain and deposit
 * into a terminal sink
 *
 * When asset is released into a sink, that sink may be a terminal sink, or it may be an intermediate step that
 * processes the asset flow, then releases it to another sink. All sink flows eventually end in a terminal sink. This
 * class processes the release of asset from its first sink through to deposit in the terminal sink. This includes
 * all of the accounting and state updates called for by intermediate sinks along the way.
 */
class sink_flow_processor {
   std::unique_ptr<sink_flow_processor_impl> my;

public:
   sink_flow_processor(cow_db_wrapper& db, TapOpenCallback cbOpenTap, FundAccountCallback cbFundAccount);
   ~sink_flow_processor();

   /**
    * @brief Release asset into provided sink, processing asset flow to the terminal sink
    * @param origin Terminal sink describing the source the asset is flowing from
    * @param sink The sink to release asset into
    * @param amount The amount to release into the sink
    * @return The full path of sinks the asset flowed through, beginning with the sink argument
    *
    * Release asset into the provided sink and process its flow through any intermediate sinks to the terminal sink,
    * performing any processing and state updates required by intermediate sinks or the terminal sink.
    *
    * This includes handling all asset flows through tank attachments, as well as deposit into tanks and accounts,
    * with relevant deposit source and asset ownership checks applied.
    */
   vector<ptnt::sink> release_to_sink(ptnt::sink origin, ptnt::sink sink, asset amount);
};

} } } // namespace graphene::chain::tnt
