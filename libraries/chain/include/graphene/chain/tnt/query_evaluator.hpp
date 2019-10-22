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

#include <graphene/protocol/tnt/query_api.hpp>

#include <graphene/chain/tnt/object.hpp>

namespace graphene { namespace chain {
class database;
namespace ptnt = protocol::tnt;
namespace tnt {
template<typename T> using const_ref = std::reference_wrapper<const T>;
struct query_evaluator_impl;

/// @brief A class to evaluate and apply tank queries
/// @ingroup TNT
///
/// As with most graphene operations, running queries on a tank or tank accessory involves two main steps: the query
/// evaluation, which checks whether the query is valid, and application, which applies the requisite changes to the
/// database object.
///
/// This class encapsulates the query evaluation code and provides a simple interface for evaluating and applying
/// tank/accessory queries in bulk. Several queries may be evaluated, and exceptions are thrown if evaluation fails.
/// Subsequently, all evaluated queries can be applied. Once the queries have been applied, no new queries may be
/// evaluated.
///
/// @note This class takes queries by reference and stores those references internally. Clients are expected to keep
/// those references valid until after the query_evaluator has been destroyed.
///
/// The query_evaluator allows inspection of the queries which have been run. Use @ref get_tank_queries to get
/// queries whose target is the tank itself, or @ref get_target_queries to get all queries run on a given target.
/// These calls may be used during evaluation or after application.
class query_evaluator {
   std::unique_ptr<query_evaluator_impl> my;

public:
   query_evaluator();
   ~query_evaluator();

   /**
    * @brief Set the tank that this object will process queries for
    * @param tankd The tank to query
    *
    * This function must be called exactly once, before any queries are processed
    */
   void set_query_tank(const tank_object& tank);

   /**
    * @brief Evaluate a single query for validity
    * @param query The query to evaluate. This reference is saved, so be sure it's lifetime exceeds the lifetime of
    * the query_evaluator
    * @param tank The tank the query is being evaluated on
    * @param db The database containing the tank being queried
    * @return Any authorities required by the operation
    *
    * This function evaluates a single query for validity, but makes no changes to the tank's state. Exceptions will
    * be thrown for errors in the query evaluation
    *
    * This function may be called arbitrarily many times with different queries. To apply the queries, call @ref
    * apply_queries. Once @ref apply_queries has been called, this function must not be called again.
    */
   vector<authority> evaluate_query(const ptnt::tank_query_type& query, const database& db);
   /**
    * @brief Apply all of the queries evaluated with @ref evaluate_query
    * @param tank Mutable reference to the tank object the queries affect
    *
    * Apply the requisite changes from the evaluated queries to the supplied mutable tank object. After calling this
    * function, @ref evaluate_query must not be called again.
    */
   void apply_queries(tank_object& tank);

   /// @brief Return any evaluated queries which target the tank itself
   vector<const_ref<ptnt::tank_query_type>> get_tank_queries() const;
   /// @brief Return any evaluated queries which target the accessory at the specified address
   vector<const_ref<ptnt::tank_query_type>> get_target_queries(
         const ptnt::tank_accessory_address_type& address) const;
};

} } } // namespace graphene::chain::tnt
