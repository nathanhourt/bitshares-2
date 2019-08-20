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

#include <graphene/protocol/base.hpp>

namespace graphene { namespace protocol { namespace tnt {

/// Chain-defined parameters and limits for TNT structures
struct parameters_type {
   /// The maximum length of a sink chain (such as a sequence of tank attachments)
   uint16_t max_sink_chain_length = GRAPHENE_DEFAULT_MAX_SINK_CHAIN_LENGTH;
   /// The maximum number of taps a single transaction may open
   uint16_t max_taps_to_open = GRAPHENE_DEFAULT_MAX_TAPS_TO_OPEN;

   extensions_type extensions;
};

} } } // namespace graphene::protocol::tnt

FC_REFLECT(graphene::protocol::tnt::parameters_type, (max_sink_chain_length)(max_taps_to_open)(extensions))
