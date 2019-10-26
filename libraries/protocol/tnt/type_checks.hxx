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

// These result types are variants with lots of error conditions, and we don't use visitors on these, so we don't get
// the compiler check that we're handling all the possibilities. Thus we use these macros to insert a compile time
// check when inspecting these types that they are still what we're expecting to handle. If the types change, these
// will fail. When that happens, be sure to update all of the call sites to handle the new type before adjusting the
// expected type here.
#define CHECK_TANK_RESULT() \
static_assert(std::is_same<lookup_result<tank_schematic>, \
                           static_variant<const_ref<tank_schematic>, need_lookup_function, nonexistent_object>>{}, \
              "Tank lookup result has changed; update this function to handle the new type")
#define CHECK_ATTACHMENT_RESULT() \
static_assert(std::is_same<lookup_result<tank_attachment>, \
                           static_variant<const_ref<tank_attachment>, need_lookup_function, nonexistent_object>>{}, \
              "Attachment lookup result has changed; update this function to handle the new type")
#define CHECK_CONNECTION_ASSET_RESULT() \
static_assert(std::is_same<connection_asset, static_variant<asset_id_type, any_asset, no_asset, \
                           need_lookup_function, nonexistent_object>>{}, \
              "Connection asset result has changed; update this function to handle the new type")
#define CHECK_ATTACHMENT_CONNECTION_RESULT() \
static_assert(std::is_same<attachment_connection_result, \
                           static_variant<const_ref<connection>, bad_connection, need_lookup_function, \
                           nonexistent_object>>{}, \
              "Destination connection result has changed; update this function to handle the new type")
#define CHECK_CONNECTION_CHAIN_RESULT() \
static_assert(std::is_same<connection_chain_result, \
                           static_variant<connection_chain, exceeded_max_chain_length, bad_connection, \
                                          need_lookup_function, nonexistent_object>>{}, \
              "Destination connection result has changed; update this function to handle the new type")
