#pragma once
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

#include <graphene/chain/tnt/object.hpp>
#include <graphene/chain/tnt/query_evaluator.hpp>
#include <graphene/chain/tnt/cow_db_wrapper.hpp>

namespace graphene { namespace chain { namespace tnt {
struct tap_requirement_utility_impl;

/// This class examines the requirements of a tap to determine the tap's flow limit, and updates the requirements to
/// record the amount released from the tap
///
/// Because a single tap may be opened multiple times in a single operation, this object should be created once for
/// each tap being opened and saved and reused for each reopening of the tap in the same operation. This means that
/// @ref max_tap_release and @ref prepare_tap_release may be called multiple times.
class tap_requirement_utility {
   std::unique_ptr<tap_requirement_utility_impl> my;

public:
   /**
    * @brief Initialize the tap_requirement_utility
    * @param db The database containing the tank and tap being inspected
    * @param tap_ID ID of the tap being inspected
    * @param queries The queries which have been run in the current operation
    */
   tap_requirement_utility(cow_db_wrapper& db, protocol::tnt::tap_id_type tap_ID, const query_evaluator& queries);
   tap_requirement_utility(tap_requirement_utility&&);
   tap_requirement_utility& operator=(tap_requirement_utility&&);
   ~tap_requirement_utility();

   /**
    * @brief Evaluate the tap's requirements to determine the maximum amount that can be released from the tap
    * @return The max amount which can be released, which is the limit of the most restrictive tap requirement or the
    * tank's balance, whichever is lower
    */
   share_type max_tap_release();

   /// @return The index of the most restrictive requirement, or null if the tank's balance is lower than any
   /// requirement's limit
   /// @throw fc::assert_exception If @ref max_tap_release has not yet been run
   optional<ptnt::index_type> most_restrictive_requirement_index() const;

   /**
    * @brief Update tap requirements' states with the amount being released from the tap
    * @param release_amount The amount being released from the tap
    */
   void prepare_tap_release(share_type release_amount);
};

} } } // namespace graphene::chain::tnt
