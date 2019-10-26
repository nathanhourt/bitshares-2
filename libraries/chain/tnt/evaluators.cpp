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

#include <graphene/chain/tnt/evaluators.hpp>
#include <graphene/chain/tnt/object.hpp>
#include <graphene/chain/tnt/tap_flow_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/protocol/tnt/validation.hpp>

namespace graphene { namespace chain {
namespace ptnt = graphene::protocol::tnt;

ptnt::tank_lookup_function make_lookup(const database& d) {
   return [&d](tank_id_type id) -> const ptnt::tank_schematic* {
      try {
         const auto& tank = id(d);
         return &tank.schematic;
      } catch (fc::exception&) {
         return nullptr;
      }
   };
}

void_result tank_create_evaluator::do_evaluate(const tank_create_operation& o) {
   const auto& d = db();
   FC_ASSERT(HARDFORK_BSIP_72_PASSED(d.head_block_time()), "Tanks and Taps is not yet enabled on this blockchain");
   const auto& tnt_parameters = d.get_global_properties().parameters.extensions.value.updatable_tnt_options;
   FC_ASSERT(tnt_parameters.valid(), "Tanks and Taps is not yet configured on this blockchain");

   FC_ASSERT(d.get_balance(o.payer, asset_id_type()).amount >= o.deposit_amount,
             "Insufficient balance to pay the deposit");

   new_tank = ptnt::tank_schematic::from_create_operation(o);
   ptnt::tank_validator validator(new_tank, tnt_parameters->max_connection_chain_length, make_lookup(d));
   validator.validate_tank();
   FC_ASSERT(validator.calculate_deposit(*tnt_parameters) == o.deposit_amount, "Incorrect deposit amount");

   return {};
}

object_id_type tank_create_evaluator::do_apply(const tank_create_operation& o) {
   auto& d = db();
   d.adjust_balance(o.payer, -o.deposit_amount);
   return d.create<tank_object>([&schema = new_tank, &o, now=d.head_block_time()](tank_object& tank) {
      tank.schematic = std::move(schema);
      tank.deposit = o.deposit_amount;
      tank.creation_date = now;
   }).id;
}

void_result tank_update_evaluator::do_evaluate(const tank_update_evaluator::operation_type& o) {
   const auto& d = db();
   const auto& tnt_parameters = d.get_global_properties().parameters.extensions.value.updatable_tnt_options;
   FC_ASSERT(tnt_parameters.valid(), "Tanks and Taps is not yet configured on this blockchain");

   old_tank = &o.tank_to_update(d);
   FC_ASSERT(o.update_authority == *old_tank->schematic.taps.at(0).open_authority,
             "Tank update authority is incorrect");
   updated_tank = old_tank->schematic;
   updated_tank.update_from_operation(o);
   ptnt::tank_validator validator(updated_tank, tnt_parameters->max_connection_chain_length,
                                  make_lookup(d), old_tank->id);
   validator.validate_tank();

   auto new_deposit = validator.calculate_deposit(*tnt_parameters);
   FC_ASSERT(old_tank->deposit - new_deposit == o.deposit_delta, "Incorrect deposit delta");
   if (o.deposit_delta > 0)
      FC_ASSERT(d.get_balance(o.payer, asset_id_type()).amount >= o.deposit_delta,
                "Insufficient balance to pay the deposit");

   return {};
}

void_result tank_update_evaluator::do_apply(const tank_update_evaluator::operation_type& o) {
   auto& d = db();
   if (o.deposit_delta != 0)
      d.adjust_balance(o.payer, o.deposit_delta);
   d.modify(*old_tank, [&schema = updated_tank, &o](tank_object& tank) {
      tank.schematic = std::move(schema);
      tank.deposit += o.deposit_delta;

      for (auto id : o.attachments_to_remove)
         tank.clear_attachment_state(id);
      for (auto id_att_pair : o.attachments_to_replace)
         tank.clear_attachment_state(id_att_pair.first);
      for (auto id : o.taps_to_remove)
         tank.clear_tap_state(id);
      for (auto id_tap_pair : o.taps_to_replace)
         tank.clear_tap_state(id_tap_pair.first);
   });

   return {};
}

void_result tank_delete_evaluator::do_evaluate(const tank_delete_evaluator::operation_type& o) {
   const auto& d = db();

   old_tank = &o.tank_to_delete(d);
   FC_ASSERT(o.delete_authority == *old_tank->schematic.taps.at(0).open_authority,
             "Tank update authority is incorrect");
   FC_ASSERT(old_tank->balance == 0, "Cannot delete a tank with an outstanding balance");
   FC_ASSERT(o.deposit_claimed == old_tank->deposit, "Incorrect deposit amount");

   return {};
}

void_result tank_delete_evaluator::do_apply(const tank_delete_evaluator::operation_type& o) {
   auto& d = db();
   d.adjust_balance(o.payer, o.deposit_claimed);
   d.remove(*old_tank);

   return {};
}

struct auth_usage_checker {
   const vector<authority>& declared_auths;
   std::set<vector<authority>::const_iterator> used_auths;

   auth_usage_checker(const vector<authority>& declared_auths) : declared_auths(declared_auths) {}

   void require_auth(const authority& auth) {
      auto itr = std::find(declared_auths.begin(), declared_auths.end(), auth);
      FC_ASSERT(itr != declared_auths.end(), "Required authority for query was not declared: ${A}", ("A", auth));
      used_auths.insert(itr);
   }
   void require_auths(const vector<authority>& auths) {
      for (const auto& auth : auths) require_auth(auth);
   }

   void check_all_used() const {
      if (used_auths.size() < declared_auths.size()) {
         vector<authority> unused_auths(declared_auths.size() - used_auths.size());
         for (auto itr = declared_auths.begin(); itr != declared_auths.end(); ++itr)
            if (used_auths.count(itr) == 0)
               unused_auths.push_back(*itr);
         FC_THROW_EXCEPTION(fc::assert_exception, "Authorities were declared as required, but not used: ${Auths}",
                            ("Auths", unused_auths));
      }
   }
};

void_result tank_query_evaluator::do_evaluate(const tank_query_evaluator::operation_type& o) {
   const auto& d = db();
   query_tank = &o.tank_to_query(d);
   evaluator.set_query_tank(*query_tank);
   auth_usage_checker auth_checker(o.required_authorities);
   const auto& tnt_parameters = d.get_global_properties().parameters.extensions.value.updatable_tnt_options;
   FC_ASSERT(tnt_parameters.valid(), "Tanks and Taps is not yet configured on this blockchain");

   for(const auto& query : o.queries) { try {
      auto required_auths = evaluator.evaluate_query(query, d);
      auth_checker.require_auths(std::move(required_auths));
   } FC_CAPTURE_AND_RETHROW((query)) }
   auth_checker.check_all_used();

   return {};
}

void_result tank_query_evaluator::do_apply(const tank_query_evaluator::operation_type&) {
   db().modify(*query_tank, [&evaluator=evaluator](tank_object& tank) {
      evaluator.apply_queries(tank);
   });
   return {};
}

void_result tap_open_evaluator::do_evaluate(const tap_open_evaluator::operation_type& o) {
   const auto& d = db();
   tank = &d.get(*o.tap_to_open.tank_id);
   db_wrapper = std::make_unique<cow_db_wrapper>(d);
   const auto& tnt_parameters = d.get_global_properties().parameters.extensions.value.updatable_tnt_options;
   FC_ASSERT(tnt_parameters.valid(), "Tanks and Taps is not yet configured on this blockchain");

   // Check the tap exists
   auto tap_itr = tank->schematic.taps.find(o.tap_to_open.tap_id);
   FC_ASSERT(tap_itr != tank->schematic.taps.end(), "Cannot open tap: tap does not exist");
   const ptnt::tap& tap = tap_itr->second;

   // Perform requisite checks for tank destruction via destructor tap
   if (o.deposit_claimed.valid()) {
      FC_ASSERT(*o.deposit_claimed == tank->deposit, "Deposit claim does not match tank deposit amount");
      FC_ASSERT(tap.destructor_tap, "Cannot destroy tank: tap is not a destructor tap");
      delete_tank = true;

      // Fast track: if we're deleting an empty tank, we can skip everything, just check auths and validity
      if (tank->balance == 0) {
         FC_ASSERT(o.queries.empty(), "When destroying an empty tank via destructor tap, queries are not run");
         FC_ASSERT(o.tap_open_count == 1,
                   "When destroying an empty tank via destructor tap, tap open count must be 1");
         if (tap.open_authority.valid())
            FC_ASSERT(o.required_authorities == vector<authority>{*tap.open_authority},
                      "When destroying an empty tank via destructor tap, declare only the tap open authority");
         else
            FC_ASSERT(o.required_authorities.empty(), "Declare no authorities when destroying an empty tank via "
                                                      "destructor tap with no open authority");
         if (o.release_amount.is_type<share_type>())
            FC_ASSERT(o.release_amount.get<share_type>() == 0,
                      "When destroying an empty tank via destructor tap, release amount must be 0 or unlimited");
         return {};
      }
   }

   auth_usage_checker auth_checker(o.required_authorities);
   tnt::query_evaluator query_evaluator;
   query_evaluator.set_query_tank(*tank);

   // Check tap is connected and open is authorized
   FC_ASSERT(tap.connected_connection.valid(), "Cannot open tap: tap is not connected");
   if (tap.open_authority.valid())
      auth_checker.require_auth(*tap.open_authority);

   // Evaluate the queries
   for (const auto& query : o.queries) { try {
      auto required_auths = query_evaluator.evaluate_query(query, d);
      auth_checker.require_auths(std::move(required_auths));
   } FC_CAPTURE_AND_RETHROW((query)) }
   query_evaluator.apply_queries(db_wrapper->get<tank_object>(tank->id));

   // Create the callback for tap flow evaluation
   tnt::FundAccountCallback cb_pay = [this](account_id_type account, asset amount, vector<ptnt::connection> path) {
      accounts_to_pay.emplace_back(account, amount, std::move(path));
   };

   // Perform the tap flows
   auto flows =
      tnt::evaluate_tap_flow(*db_wrapper, query_evaluator, o.payer, o.tap_to_open, o.release_amount, o.tap_open_count,
                             std::move(cb_pay));
   // Check that the declarations matched the requirements
   FC_ASSERT(flows.size() == o.tap_open_count, "Declared count of taps to open does not match count of taps opened");
   auth_checker.check_all_used();
   // If destroying the tank, make sure it got emptied during tap flow
   if (delete_tank)
      FC_ASSERT(db_wrapper->get(*o.tap_to_open.tank_id).balance() == 0,
                "Cannot destroy nonempty tank if tank is not being emptied in the current operation");

   return {};
}

void_result tap_open_evaluator::do_apply(const tap_open_evaluator::operation_type& o) {
   database& d = db();
   db_wrapper->commit(d);

   for (auto& payable : accounts_to_pay) {
      d.adjust_balance(payable.receiving_account, payable.amount_received);
      d.push_applied_operation(std::move(payable));
   }

   if (delete_tank) {
      d.remove(*tank);
      d.adjust_balance(o.payer, asset(*o.deposit_claimed));
   }

   return {};
}

void_result tap_connect_evaluator::do_evaluate(const tap_connect_evaluator::operation_type& o) {
   const database& d = db();
   tank = &d.get(*o.tap_to_connect.tank_id);

   // Check tap exists
   auto tap_itr = tank->schematic.taps.find(o.tap_to_connect.tap_id);
   FC_ASSERT(tap_itr != tank->schematic.taps.end(), "Cannot connect tap: tap does not exist");
   const ptnt::tap& tap = tap_itr->second;

   // Check authority
   FC_ASSERT(tap.connect_authority.valid(), "Cannot connect tap: tap connect authority is unset");
   FC_ASSERT(o.connect_authority == *tap.connect_authority);

   return {};
}

void_result tap_connect_evaluator::do_apply(const tap_connect_evaluator::operation_type& o) {
   database& d = db();
   d.modify(*tank, [&o](tank_object& tank) {
      ptnt::tap& tap = tank.schematic.taps[o.tap_to_connect.tap_id];
      tap.connected_connection = o.new_connection;
      if (o.clear_connect_authority)
         tap.connect_authority.reset();
   });

   return {};
}

void_result account_fund_connection_evaluator::do_evaluate(const account_fund_connection_evaluator::operation_type& o)
{
   const database& d = db();
   FC_ASSERT(HARDFORK_BSIP_72_PASSED(d.head_block_time()), "Tanks and Taps is not yet enabled on this blockchain");
   db_wrapper = std::make_unique<cow_db_wrapper>(d);
   const auto& tnt_parameters = d.get_global_properties().parameters.extensions.value.updatable_tnt_options;
   FC_ASSERT(tnt_parameters.valid(), "Tanks and Taps is not yet configured on this blockchain");

   // Check account balance
   FC_ASSERT(d.get_balance(o.funding_account, o.funding_amount.asset_id) >= o.funding_amount,
             "Cannot fund connection: account has insufficient balance");

   // Create the callbacks for connection flow processing
   tnt::TapOpenCallback cb_open = [](ptnt::tap_id_type, ptnt::asset_flow_limit) {
      FC_THROW_EXCEPTION(fc::assert_exception,
                         "Opening taps from within account_fund_connection_operation is not currently supported");
   };
   tnt::FundAccountCallback cb_pay = [this](account_id_type account, asset amount, vector<ptnt::connection> path) {
      accounts_to_pay.emplace_back(account, amount, std::move(path));
   };

   // Process the flow
   tnt::connection_flow_processor flow_processor(*db_wrapper, std::move(cb_open), std::move(cb_pay));
   flow_processor.release_to_connection(o.funding_account, o.funding_destination, o.funding_amount);

   return {};
}

void_result account_fund_connection_evaluator::do_apply(const account_fund_connection_evaluator::operation_type& o) {
   database& d = db();

   db_wrapper->commit(d);
   d.adjust_balance(o.funding_account, -o.funding_amount);

   for (const auto& payable : accounts_to_pay) {
      d.adjust_balance(payable.receiving_account, payable.amount_received);
      d.push_applied_operation(std::move(payable));
   }

   return {};
}

} } // namespace graphene::chain
