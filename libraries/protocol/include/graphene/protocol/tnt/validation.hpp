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

#include <graphene/protocol/tnt/lookups.hpp>
#include <graphene/protocol/tnt/parameters.hpp>

namespace graphene { namespace protocol { namespace tnt {

using attachment_counter_type = map<tank_attachment::tag_type, index_type>;
using requirement_counter_type = map<tap_requirement::tag_type, index_type>;

/// A class providing validation and summary information for tanks and tank accessories
class tank_validator : public lookup_utilities {
   const size_t max_connection_chain_length;
   const fc::optional<tank_id_type> tank_id;
   bool has_validated = false;

   // Counters of tank accessories
   attachment_counter_type attachment_counters;
   requirement_counter_type requirement_counters;

public:
   /** @brief Create a tank_validator to validate a specified tank
     * @param schema Schematic of the tank to be validated
     * @param max_connection_chain_length Maximum length to walk connection chains before yielding an error
     * @param lookup_tank [Optional] A callback function to retrieve a tank_schematic corresponding to a tank ID. If
     * omitted, references to other tanks will be unchecked and presumed valid.
     * @param tank_id [Optional] ID of the tank being validated. Provide to enable more accurate validation of tap
     * connections to tanks using a @ref deposit_source_restrictor
     */
   tank_validator(const tank_schematic& schema, size_t max_connection_chain_length,
                  const tank_lookup_function& lookup_tank = {}, fc::optional<tank_id_type> tank_id = {})
       : lookup_utilities(schema, lookup_tank), max_connection_chain_length(max_connection_chain_length),
         tank_id(tank_id) {}

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

   /// @brief Stateless/internal checks only on a particular tank attachment
   static void validate_attachment(const tank_attachment& att);
   /// @brief Stateless/internal checks only on a particular tap requirement
   static void validate_tap_requirement(const tap_requirement& req);
   /// @brief Stateless/internal checks only on a particular tap
   static void validate_tap(const tap& tap);
   /// @brief Stateless/internal emergency tap checks on a particular tap
   static void validate_emergency_tap(const tap& etap);

   /// @brief Add every account referenced by this tank_schematic to the set
   void get_referenced_accounts(flat_set<account_id_type>& accounts) const;
   /// @brief Add referenced accounts from a particular tap to the provided set
   static void get_referenced_accounts(flat_set<account_id_type>& accounts, const tap& tap);
   /// @brief Add referenced accounts from a particular tank attachment to the provided set
   static void get_referenced_accounts(flat_set<account_id_type>& accounts, const tank_attachment& att);

   /// Get counts of each tank_attachment type on the schematic (these are tallied during validation)
   const attachment_counter_type& get_attachment_counts() const {
      FC_ASSERT(has_validated,
                "Cannot get attachment counts until tank has been validated. Run validate_tank() first");
      return attachment_counters;
   }
   /// Get counts of each tap_requirement type on the schematic (these are tallied during validation)
   const requirement_counter_type& get_requirement_counts() const {
      FC_ASSERT(has_validated,
                "Cannot get requirement counts until tank has been validated. Run validate_tank() first");
      return requirement_counters;
   }
   /// Calculate the deposit required for this tank and all accessories
   share_type calculate_deposit(const parameters_type& parameters) const;
   /// Shorthand to calculate the deposit for a tank, without manually creating the tank_validator (tank must pass
   /// validation, or an exception will be thrown)
   static share_type calculate_deposit(const tank_schematic& schematic, const parameters_type& parameters);
   template<typename DB>
   static share_type calculate_deposit(const tank_schematic& schematic, const DB& database) {
      const auto& params = database.get_global_properties().parameters.extensions.value.updatable_tnt_options;
      FC_ASSERT(params.valid(), "Cannot calculate deposit: TNT disabled on target database");
      return calculate_deposit(schematic, *params);
   }
};

/// A class to check if tank accessories are unique; can be instantiated for tank_attachment or tap_requirement
template<typename AccessoryVariant>
class uniqueness_checker {
   using tag_type = typename AccessoryVariant::tag_type;
   bool is_unique_accessory(tag_type tag) {
      return fc::typelist::runtime::dispatch(typename AccessoryVariant::list(), tag, [](auto t) {
         return decltype(t)::type::unique;
      });
   }
   set<tag_type> tags_seen;

public:
   /// Checks if the provided tag should be unique and, if so, whether it has already been checked.
   /// Returns true if the uniqueness constraint is upheld; false if it is violated
   bool check_tag(tag_type tag) {
      if (is_unique_accessory(tag) && tags_seen.count(tag))
         return false;
      tags_seen.insert(tag);
      return true;
   }

   /// Reset the tags which have already been seen
   void reset() { tags_seen.clear(); }
};

} } } // namespace graphene::protocol::tnt
