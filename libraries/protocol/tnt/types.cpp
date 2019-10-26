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

#include <graphene/protocol/tnt/operations.hpp>

#include <fc/exception/exception.hpp>
#include <fc/io/raw.hpp>

namespace graphene { namespace protocol { namespace tnt {

tank_schematic tank_schematic::from_create_operation(const tank_create_operation &create_op) {
   tank_schematic schema;
   for (const auto& attachment : create_op.attachments)
      schema.attachments[schema.attachment_counter++] = attachment;
   for (const auto& tap : create_op.taps)
      schema.taps[schema.tap_counter++] = tap;
   schema.asset_type = create_op.contained_asset;
   schema.remote_sources = create_op.authorized_sources;
   return schema;
}

void tank_schematic::update_from_operation(const tank_update_operation& update_op) {
   for (auto id : update_op.taps_to_remove)
      taps.erase(id);
   for (const auto& id_tap_pair : update_op.taps_to_replace)
      taps[id_tap_pair.first] = id_tap_pair.second;
   for (const tap& tap_to_add : update_op.taps_to_add)
      taps[tap_counter++] = tap_to_add;

   for (auto id : update_op.attachments_to_remove)
      attachments.erase(id);
   for (const auto& id_att_pair : update_op.attachments_to_replace)
      attachments[id_att_pair.first] = id_att_pair.second;
   for (const tank_attachment& att : update_op.attachments_to_add)
      attachments[attachment_counter++] = att;

   if (update_op.new_authorized_sources.valid())
      remote_sources = *update_op.new_authorized_sources;
}

bool time_lock::unlocked_at_time(const fc::time_point_sec& time) const {
   auto first_future_time = std::find_if_not(lock_unlock_times.begin(), lock_unlock_times.end(),
                                             [&time](const time_point_sec& t) { return t < time; });
   auto switch_count = first_future_time - lock_unlock_times.begin();
   // If the lock has switched an even number of times, it's the same now as it was when it started
   if (switch_count % 2)
      return !start_locked;
   // It's switched an odd number of times, so it's unlocked now if it started locked
   return start_locked;
}

} } } // namespace graphene::protocol::tnt

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION(graphene::protocol::tnt::tank_schematic)
