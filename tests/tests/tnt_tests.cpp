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
#include <graphene/chain/tnt/tap_open_helper.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/protocol/tnt/validation.hpp>

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
   BOOST_CHECK_EQUAL(((const tank_object&)(tank_wrapper)).balance.value, 100);
   BOOST_CHECK_EQUAL(((const tank_object&)(tank_wrapper)).schematic.taps.size(), 1);
   BOOST_CHECK_EQUAL(((const tank_object&)(tank_wrapper)).schematic.taps.count(0), 1);

   // Check the modifications are held across other objects taken from the db wrapper
   BOOST_CHECK_EQUAL(tank_id(wrapper).balance().value, 100);
   BOOST_CHECK_EQUAL(tank_id(wrapper).schematic().taps().size(), 1);
   BOOST_CHECK_EQUAL(tank_id(wrapper).schematic().taps().count(0), 1);
   BOOST_CHECK_EQUAL(((const tank_object&)(tank_id(wrapper))).balance.value, 100);
   BOOST_CHECK_EQUAL(((const tank_object&)(tank_id(wrapper))).schematic.taps.size(), 1);
   BOOST_CHECK_EQUAL(((const tank_object&)(tank_id(wrapper))).schematic.taps.count(0), 1);

   // Check the modifications have not applied to the database object
   BOOST_CHECK_EQUAL(tank_id(db).balance.value, 5);
   BOOST_CHECK_EQUAL(tank_id(db).schematic.taps.size(), 0);

   // Commit the changes, and check that they are reflected in the database
   wrapper.commit(db);
   BOOST_CHECK_EQUAL(tank_id(db).balance.value, 100);
   BOOST_CHECK_EQUAL(tank_id(db).schematic.taps.size(), 1);
   BOOST_CHECK_EQUAL(tank_id(db).schematic.taps.count(0), 1);

   // Set tank balance to 0 again, since we printed the CORE for that test, and database_fixture will error over it
   tank_wrapper.balance().value = 0;
   wrapper.commit(db);
} FC_LOG_AND_RETHROW() }

#define CORE(x) (x * GRAPHENE_BLOCKCHAIN_PRECISION)
BOOST_FIXTURE_TEST_CASE(basic_tank_test, database_fixture) { try {
   ACTORS((nathan)(joe)(sam))
   fund(nathan, asset(CORE(5000)));
   share_type nathan_bal = CORE(5000);

   set_tnt_committee_parameters();

   // Just one attachment: a tap_opener than opens tap 4, releases to Joe
   ptnt::tap_opener opener(4, ptnt::unlimited_flow(), joe_id);

   // Create the taps
   // E-tap: requires Nathan and Joe to connect or open
   ptnt::tap emergency_tap;
   emergency_tap.open_authority = authority(2, nathan_id, 1, joe_id, 1);
   emergency_tap.connect_authority = emergency_tap.open_authority;
   emergency_tap.destructor_tap = true;

   // Tap 1: Nathan can open for up to 100 CORE, goes to Joe
   ptnt::tap tap_1;
   tap_1.open_authority = authority(1, nathan_id, 1);
   tap_1.connected_sink = joe_id;
   tap_1.requirements = {ptnt::cumulative_flow_limit(CORE(100))};
   tap_1.destructor_tap = true;

   // Tap 2: Joe can open for up to 50 CORE per 10 blocks, goes to Nathan
   ptnt::tap tap_2;
   tap_2.open_authority = authority(1, joe_id, 1);
   tap_2.connected_sink = nathan_id;
   tap_2.requirements = {ptnt::periodic_flow_limit(CORE(50), GRAPHENE_DEFAULT_BLOCK_INTERVAL * 10)};

   // Tap 3: A dedicated key can open, unlimited release, goes to Joe via the tap opener causing tap 4 to open as well
   // In effect, this tap can send some asset to Joe, and the rest of the tank goes to Sam
   fc::ecc::private_key tap_3_private_key = fc::ecc::private_key::generate();
   public_key_type tap_3_public_key = tap_3_private_key.get_public_key();
   ptnt::tap tap_3;
   tap_3.open_authority = authority(1, tap_3_public_key, 1);
   tap_3.connected_sink = ptnt::attachment_id_type{{}, 0};

   // Tap 4: No-one can open (the tap opener opens it), goes to Sam
   ptnt::tap tap_4;
   tap_4.open_authority = authority(1);
   tap_4.connected_sink = sam_id;

   // Create the tank
   tank_create_operation create;
   create.payer = nathan_id;
   create.attachments = {opener};
   create.taps = {emergency_tap, tap_1, tap_2, tap_3, tap_4};
   create.set_fee_and_deposit(db);
   trx.clear();
   trx.set_expiration(db.head_block_time() + 1000);
   trx.operations = {create};
   sign(trx, nathan_private_key);
   tank_id_type tank_id = db.push_transaction(trx).operation_results.front().get<object_id_type>();
   nathan_bal = nathan_bal - create.deposit_amount.value - create.fee.amount.value;

   // Check that the money has moved as we expect, and the tank is created as directed
   {
      const tank_object& tank = tank_id(db);
      BOOST_CHECK_EQUAL(db.get_balance(nathan_id, {}).amount.value, nathan_bal.value);
      BOOST_CHECK_EQUAL(tank.deposit.value, create.deposit_amount.value);
      BOOST_CHECK_EQUAL(tank.balance.value, 0);
      BOOST_CHECK_EQUAL(tank.schematic.attachments.size(), 1);
      BOOST_CHECK_EQUAL(tank.schematic.attachment_counter, 1);
      BOOST_CHECK_EQUAL(tank.schematic.tap_counter, 5);
      BOOST_CHECK_EQUAL(tank.schematic.taps.size(), 5);
      BOOST_CHECK_EQUAL(fc::json::to_string(tank.schematic.attachments.at(0)),
                        fc::json::to_string(ptnt::tank_attachment(opener)));
      BOOST_CHECK_EQUAL(fc::json::to_string(tank.schematic.taps.at(0)), fc::json::to_string(emergency_tap));
      BOOST_CHECK_EQUAL(fc::json::to_string(tank.schematic.taps.at(1)), fc::json::to_string(tap_1));
      BOOST_CHECK_EQUAL(fc::json::to_string(tank.schematic.taps.at(2)), fc::json::to_string(tap_2));
      BOOST_CHECK_EQUAL(fc::json::to_string(tank.schematic.taps.at(3)), fc::json::to_string(tap_3));
      BOOST_CHECK_EQUAL(fc::json::to_string(tank.schematic.taps.at(4)), fc::json::to_string(tap_4));
   }

   // Add 1000 CORE to the tank
   account_fund_sink_operation fill;
   fill.funding_account = nathan_id;
   fill.funding_amount = asset(CORE(1000));
   fill.funding_destination = tank_id;
   fill.fee = fill.calculate_fee(account_fund_sink_operation::fee_parameters_type());
   trx.clear();
   trx.operations = {fill};
   sign(trx, nathan_private_key);
   db.push_transaction(trx);
   nathan_bal = nathan_bal - fill.funding_amount.amount.value - fill.fee.amount.value;

   // Check that the money moved as expected
   {
      const tank_object& tank = tank_id(db);
      BOOST_CHECK_EQUAL(db.get_balance(nathan_id, {}).amount.value, nathan_bal.value);
      BOOST_CHECK_EQUAL(tank.balance.value, CORE(1000));
   }

   // Release 10 CORE through tap 1
   tap_open_operation open;
   open.payer = nathan_id;
   open.tap_to_open = ptnt::tap_id_type{tank_id, 1};
   open.release_amount = share_type(CORE(10));
   graphene::chain::tnt::set_tap_open_count_and_authorities(db, open);
   open.fee = open.calculate_fee(tap_open_operation::fee_parameters_type());
   BOOST_CHECK_EQUAL(open.tap_open_count, 1);
   BOOST_CHECK_EQUAL(fc::json::to_string(open.required_authorities),
                     fc::json::to_string(vector<authority>{authority(1, nathan_id, 1)}));
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   db.push_transaction(trx);
   nathan_bal -= open.fee.amount.value;

   // Check that the money moved as expected and that the cumulative_flow_limit's state is correct
   {
      const tank_object& tank = tank_id(db);
      BOOST_CHECK_EQUAL(db.get_balance(nathan_id, {}).amount.value, nathan_bal.value);
      BOOST_CHECK_EQUAL(db.get_balance(joe_id, {}).amount.value, CORE(10));
      BOOST_CHECK_EQUAL(tank.balance.value, CORE(990));
      ptnt::tank_accessory_address<ptnt::cumulative_flow_limit> limit_address{1, 0};
      BOOST_CHECK(tank.get_state(limit_address) != nullptr);
      BOOST_CHECK_EQUAL(tank.get_state(limit_address)->amount_released.value, CORE(10));
   }

   // Attempt to release 91 CORE through tap 1; should fail because tap 1 can only release 90 more
   open.release_amount = share_type(CORE(91));
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   EXPECT_EXCEPTION_STRING("a requirement has limited flow to 9000000.", [&] {db.push_transaction(trx); });

   // Attempt to destroy the tank through tap 1; should fail because the tank is not empty
   open.release_amount = share_type(CORE(90));
   open.deposit_claimed = tank_id(db).deposit;
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   EXPECT_EXCEPTION_STRING("Cannot destroy nonempty tank", [&] {db.push_transaction(trx); });

   // Do an unlimited release from tap 1, which should release 90 CORE
   open.release_amount = ptnt::unlimited_flow();
   open.deposit_claimed = {};
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   db.push_transaction(trx);
   nathan_bal -= open.fee.amount.value;

   // Check that the money moved as expected and that the cumulative_flow_limit's state is correct
   {
      const tank_object& tank = tank_id(db);
      BOOST_CHECK_EQUAL(db.get_balance(nathan_id, {}).amount.value, nathan_bal.value);
      BOOST_CHECK_EQUAL(db.get_balance(joe_id, {}).amount.value, CORE(100));
      BOOST_CHECK_EQUAL(tank.balance.value, CORE(900));
      ptnt::tank_accessory_address<ptnt::cumulative_flow_limit> limit_address{1, 0};
      BOOST_CHECK(tank.get_state(limit_address) != nullptr);
      BOOST_CHECK_EQUAL(tank.get_state(limit_address)->amount_released.value, CORE(100));
   }

   // Attempt to open tap 2. Should fail because Joe needs to authorize
   open.tap_to_open.tap_id = 2;
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   EXPECT_EXCEPTION_STRING("Required authority for query was not declared", [&] {db.push_transaction(trx); });

   // Do it with Joe's authorization, but with Sam's too (should fail for extra authorities)
   open.required_authorities = {authority(1, joe_id, 1), authority(1, sam_id, 1)};
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   sign(trx, joe_private_key);
   sign(trx, sam_private_key);
   EXPECT_EXCEPTION_STRING("Authorities were declared as required, but not used", [&] {db.push_transaction(trx); });

   // Try without Sam, but without Joe's signature to back the declared authorization
   open.required_authorities = {authority(1, joe_id, 1)};
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   EXPECT_EXCEPTION_STRING("missing required other authority", [&] {db.push_transaction(trx); });

   // And finally, do it right
   sign(trx, joe_private_key);
   db.push_transaction(trx);
   nathan_bal = nathan_bal - (unsigned)open.fee.amount.value + CORE(50);

   // Check that the money moved as expected and that the periodic_flow_limit's state is correct
   {
      const tank_object& tank = tank_id(db);
      BOOST_CHECK_EQUAL(db.get_balance(nathan_id, {}).amount.value, nathan_bal.value);
      BOOST_CHECK_EQUAL(db.get_balance(joe_id, {}).amount.value, CORE(100));
      BOOST_CHECK_EQUAL(tank.balance.value, CORE(850));
      ptnt::tank_accessory_address<ptnt::periodic_flow_limit> limit_address{2, 0};
      BOOST_CHECK(tank.get_state(limit_address) != nullptr);
      BOOST_CHECK_EQUAL(tank.get_state(limit_address)->amount_released.value, CORE(50));
   }

   // Now try it again; should fail because the tap is locked until the period rolls over
   generate_block();
   trx.set_reference_block(db.head_block_id());
   trx.clear_signatures();
   sign(trx, nathan_private_key);
   sign(trx, joe_private_key);
   EXPECT_EXCEPTION_STRING("a tap requirement has locked the tap", [&] {db.push_transaction(trx); });

   // Not one satoshi shall pass
   open.release_amount = share_type(1);
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   sign(trx, joe_private_key);
   EXPECT_EXCEPTION_STRING("a tap requirement has locked the tap", [&] {db.push_transaction(trx); });

   // Now advance time until the next period, and release 1 CORE
   generate_blocks(9);
   trx.clear();
   trx.set_reference_block(db.head_block_id());
   open.release_amount = share_type(CORE(1));
   trx.operations = {open};
   sign(trx, nathan_private_key);
   sign(trx, joe_private_key);
   db.push_transaction(trx);
   nathan_bal = nathan_bal - open.fee.amount.value + CORE(1);

   // Check that the money moved as expected and that the periodic_flow_limit's state is correct
   {
      const tank_object& tank = tank_id(db);
      BOOST_CHECK_EQUAL(db.get_balance(nathan_id, {}).amount.value, nathan_bal.value);
      BOOST_CHECK_EQUAL(db.get_balance(joe_id, {}).amount.value, CORE(100));
      BOOST_CHECK_EQUAL(tank.balance.value, CORE(849));
      ptnt::tank_accessory_address<ptnt::periodic_flow_limit> limit_address{2, 0};
      BOOST_CHECK(tank.get_state(limit_address) != nullptr);
      BOOST_CHECK_EQUAL(tank.get_state(limit_address)->amount_released.value, CORE(1));
      BOOST_CHECK_EQUAL(tank.get_state(limit_address)->period_num, 1);
   }

   // Try to open tap 4 (fails because it has an impossible authority)
   open.release_amount = ptnt::unlimited_flow();
   open.tap_to_open.tap_id = 4;
   open.required_authorities.clear();
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   EXPECT_EXCEPTION_STRING("Required authority for query was not declared", [&] {db.push_transaction(trx); });

   // Try again with the impossible authority declared
   open.required_authorities = {authority(1)};
   trx.clear_signatures();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   EXPECT_EXCEPTION_STRING("missing required other authority", [&] {db.push_transaction(trx); });

   // Try to open tap 3 (fails because only 1 tap declared to open, but transaction opens 2)
   open.required_authorities = {*tap_3.open_authority};
   open.tap_to_open.tap_id = 3;
   open.release_amount = share_type(CORE(700));
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   sign(trx, tap_3_private_key);
   EXPECT_EXCEPTION_STRING("exceeded its maximum number of taps to open", [&] {db.push_transaction(trx); });

   // Try again with 3 declared tap openings (too many)
   open.tap_open_count = 3;
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   sign(trx, tap_3_private_key);
   EXPECT_EXCEPTION_STRING("count of taps to open does not match", [&] {db.push_transaction(trx); });

   // Check automatic setter gets it right, but omit tap 3 key signature
   graphene::chain::tnt::set_tap_open_count_and_authorities(db, open);
   BOOST_CHECK_EQUAL(open.tap_open_count, 2);
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   EXPECT_EXCEPTION_STRING("missing required other authority", [&] {db.push_transaction(trx); });
   // Now get it right
   sign(trx, tap_3_private_key);
   db.push_transaction(trx);
   nathan_bal -= open.fee.amount.value;

   // Check that the money moved as expected
   {
      const tank_object& tank = tank_id(db);
      BOOST_CHECK_EQUAL(db.get_balance(nathan_id, {}).amount.value, nathan_bal.value);
      BOOST_CHECK_EQUAL(db.get_balance(joe_id, {}).amount.value, CORE(800));
      BOOST_CHECK_EQUAL(db.get_balance(sam_id, {}).amount.value, CORE(149));
      BOOST_CHECK_EQUAL(tank.balance.value, 0);
   }

   // Attempt to use tap 1 to delete the tank, but fail due to excessive deposit claim amount
   open.tap_to_open.tap_id = 1;
   open.release_amount = share_type(0);
   open.deposit_claimed = tank_id(db).deposit + 1;
   graphene::chain::tnt::set_tap_open_count_and_authorities(db, open);
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   EXPECT_EXCEPTION_STRING("Deposit claim does not match tank deposit amount", [&] {db.push_transaction(trx); });
   // Retry with insufficient claim amount
   open.deposit_claimed = tank_id(db).deposit - 1;
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   EXPECT_EXCEPTION_STRING("Deposit claim does not match tank deposit amount", [&] {db.push_transaction(trx); });
   // Retry with correct claim amount -- should work even though the tap is permanently locked
   open.deposit_claimed = tank_id(db).deposit;
   trx.clear();
   trx.operations = {open};
   sign(trx, nathan_private_key);
   db.push_transaction(trx);
   nathan_bal = nathan_bal - open.fee.amount.value + open.deposit_claimed->value;

   // Check that the deposit was claimed and the tank was destroyed
   BOOST_CHECK(db.find<tank_object>(tank_id) == nullptr);
   BOOST_CHECK_EQUAL(db.get_balance(nathan_id, {}).amount.value, nathan_bal.value);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
