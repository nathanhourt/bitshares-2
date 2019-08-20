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

#include <graphene/protocol/tnt/lookups.hpp>
#include <graphene/protocol/tnt/parameters.hpp>

namespace graphene { namespace protocol { namespace tnt {

using attachment_counter_type = map<tank_attachment::tag_type, index_type>;
using requirement_counter_type = map<tap_requirement::tag_type, index_type>;

/// A class providing validation and summary information for tanks and tank accessories
class tank_validator : public lookup_utilities {
   const parameters_type parameters;

   // Counters of tank accessories
   attachment_counter_type attachment_counters;
   requirement_counter_type requirement_counters;

public:
   tank_validator(const tank_schematic& schema, parameters_type parameters,
                  const tank_lookup_function& lookup_tank = {})
       : lookup_utilities(schema, lookup_tank), parameters(parameters) {}

   /// @brief Validate the specified attachment
   void validate_attachment(index_type attachment_id);
   /// @brief Validate a particular requirement on the specified tap
   void validate_tap_requirement(index_type tap_id, index_type requirement_index);
   /// @brief Validate the specified tap
   void validate_tap(index_type tap_id);
   /// @brief Validate the emergency tap
   void validate_emergency_tap();
   /// @brief Validate the full tank schematic, including all taps, requirements, and tank attachments
   void validate_tank();

   const attachment_counter_type& get_attachment_counts() const { return attachment_counters; }
   const requirement_counter_type& get_requirement_counts() const { return requirement_counters; }
};

} } } // namespace graphene::protocol::tnt
