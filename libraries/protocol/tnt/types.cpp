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

#include <fc/exception/exception.hpp>

#include <graphene/protocol/tnt/types.hpp>

namespace graphene { namespace protocol { namespace tnt {

void tap::validate() const {
   FC_ASSERT(connected_sink.valid() || connect_authority.valid(),
             "Tap must be connected, or specify a connect authority");
}

void tap::validate_emergency() const
{
   validate();
   FC_ASSERT(requirements.empty(), "Emergency tap must have no tap requirements");
   FC_ASSERT(open_authority.valid(), "Emergency tap must specify an open authority");
   FC_ASSERT(connect_authority.valid(), "Emergency tap must specify a connect authority");
   FC_ASSERT(destructor_tap == true, "Emergency tap must be a destructor tap");
}

} } } // namespace graphene::protocol::tnt
