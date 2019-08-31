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
   const size_t max_sink_chain_length;
   const fc::optional<tank_id_type> tank_id;

   // Counters of tank accessories
   attachment_counter_type attachment_counters;
   requirement_counter_type requirement_counters;

public:
   /** @brief Create a tank_validator to validate a specified tank
     * @param schema Schematic of the tank to be validated
     * @param max_sink_chain_length Maximum length to walk sink chains before yielding an error
     * @param lookup_tank [Optional] A callback function to retrieve a tank_schematic corresponding to a tank ID. If
     * omitted, references to other tanks will be unchecked and presumed valid.
     * @param tank_id [Optional] ID of the tank being validated. Provide to enable more accurate validation of tap
     * connections to tanks using a @ref deposit_source_restrictor
     */
   tank_validator(const tank_schematic& schema, size_t max_sink_chain_length,
                  const tank_lookup_function& lookup_tank = {}, fc::optional<tank_id_type> tank_id = {})
       : lookup_utilities(schema, lookup_tank), max_sink_chain_length(max_sink_chain_length), tank_id(tank_id) {}

   /// @brief Validate the specified attachment
   void validate_attachment(index_type attachment_id);
   /// @brief Validate a particular requirement on the specified tap
   void validate_tap_requirement(index_type tap_id, index_type requirement_index);
   /// @brief Validate the specified tap, including its connection if connected
   void validate_tap(index_type tap_id);
   /// @brief Validate the emergency tap
   void validate_emergency_tap();
   /// @brief Validate the full tank schematic, including all taps, requirements, and tank attachments
   ///
   /// This will perform the following checks:
   ///  - Internal consistency checks of all tank attachments
   ///  - Emergency tap checks
   ///  - Internal consistency checks of all taps
   ///    - Internal consistency checks of all tap requirements
   ///    - Integrity check of full deposit path if tap is connected
   ///    - Check that deposit path is legal if it terminates on a tank with a @ref deposit_source_restrictor
   void validate_tank();

   /// @brief If the specified tap is connected, check that its connection is valid
   void check_tap_connection(index_type tap_id) const;

   /// Add every account referenced by this tank_schematic to the set
   void get_referenced_accounts(flat_set<account_id_type>& accounts) const;

   /// Get counts of each tank_attachment type on the schematic (these are tallied during validation)
   const attachment_counter_type& get_attachment_counts() const { return attachment_counters; }
   /// Get counts of each tap_requirement type on the schematic (these are tallied during validation)
   const requirement_counter_type& get_requirement_counts() const { return requirement_counters; }
};

} } } // namespace graphene::protocol::tnt
