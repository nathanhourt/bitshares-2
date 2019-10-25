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
struct connection_flow_processor_impl;

/// A callback the @ref connection_flow_processor can use to notify the caller that connection flow processing has
/// requested a tap be opened. Callback is provided with the ID of the tap to open and the requested flow limit
using TapOpenCallback = std::function<void(ptnt::tap_id_type, ptnt::asset_flow_limit)>;
/// A callback the @ref connection_flow_processor can use to deposit flowed asset to an account's balance. Callback
/// is provided with the ID of the account, the amount received, and the path of the asset flow including the origin.
/// Account will have already been checked for authorization to hold the asset when the callback is invoked.
using FundAccountCallback = std::function<void(account_id_type, asset, vector<ptnt::connection>)>;

/**
 * @brief Processes release of asset into a connection, including the movement of asset along the connection chain
 * and deposit into a terminal connection
 *
 * When asset is released into a connection, that connection may be a terminal connection, or it may be an
 * intermediate step that processes the asset flow, then releases it to another connection. All connection flows
 * eventually end in a terminal connection. This class processes the release of asset from its first connection
 * through to deposit in the terminal connection. This includes all of the accounting and state updates called for by
 * intermediate connections along the way.
 */
class connection_flow_processor {
   std::unique_ptr<connection_flow_processor_impl> my;

public:
   connection_flow_processor(cow_db_wrapper& db, TapOpenCallback cbOpenTap, FundAccountCallback cbFundAccount);
   ~connection_flow_processor();

   /**
    * @brief Release asset into provided connection, processing asset flow to the terminal connection
    * @param origin Terminal connection describing the source the asset is flowing from
    * @param connection The connection to release asset into
    * @param amount The amount to release into the connection
    * @return The full path of connections the asset flowed through, beginning with the connection argument
    *
    * Release asset into the provided connection and process its flow through any intermediate connections to the
    * terminal connection, performing any processing and state updates required by intermediate connections or the
    * terminal connection.
    *
    * This includes handling all asset flows through tank attachments, as well as deposit into tanks and accounts,
    * with relevant deposit source and asset ownership checks applied.
    */
   vector<ptnt::connection> release_to_connection(ptnt::connection origin, ptnt::connection connection, asset amount);
};

} } } // namespace graphene::chain::tnt
