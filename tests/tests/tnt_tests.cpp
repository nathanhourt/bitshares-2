/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#include <boost/test/unit_test.hpp>

#include <graphene/chain/tnt/object.hpp>
#include <graphene/chain/tnt/cow_db_wrapper.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_AUTO_TEST_SUITE(tnt_tests)

// This test is a basic exercise of the cow_db_wrapper, to check reading, writing, and committing changes to the db
BOOST_FIXTURE_TEST_CASE(cow_db_wrapper_test, database_fixture) { try {
   cow_db_wrapper wrapper(db);
   tank_id_type tank_id = db.create<tank_object>([](tank_object& t) {t.balance = 5;}).id;
   auto tank_wrapper = tank_id(wrapper);

   // Check read of wrapped values
   BOOST_CHECK_EQUAL(tank_wrapper.balance().value, 5);

   // Modify the wrapped object
   tank_wrapper.balance().value = 100;
   tank_wrapper.schematic().taps[0] = ptnt::tap();

   // Check the modifications stuck
   BOOST_CHECK_EQUAL(tank_wrapper.balance().value, 100);
   BOOST_CHECK_EQUAL(tank_wrapper.schematic().taps().size(), 1);
   BOOST_CHECK_EQUAL(tank_wrapper.schematic().taps().count(0), 1);

   // Check the modifications are held across other objects taken from the db wrapper
   BOOST_CHECK_EQUAL(tank_id(wrapper).balance().value, 100);
   BOOST_CHECK_EQUAL(tank_id(wrapper).schematic().taps().size(), 1);
   BOOST_CHECK_EQUAL(tank_id(wrapper).schematic().taps().count(0), 1);

   // Check the modifications have not applied to the database object
   BOOST_CHECK_EQUAL(tank_id(db).balance.value, 5);
   BOOST_CHECK_EQUAL(tank_id(db).schematic.taps.size(), 0);

   // Commit the changes, and check that they are reflected in the database
   wrapper.commit(db);
   BOOST_CHECK_EQUAL(tank_id(db).balance.value, 100);
   BOOST_CHECK_EQUAL(tank_id(db).schematic.taps.size(), 1);
   BOOST_CHECK_EQUAL(tank_id(db).schematic.taps.count(0), 1);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
