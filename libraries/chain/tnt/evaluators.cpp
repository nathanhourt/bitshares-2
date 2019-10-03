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
#include <graphene/chain/tnt/cow_db_wrapper.hpp>
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
   FC_ASSERT(HARDFORK_BSIP_72_PASSED(d.head_block_time()), "Tanks and Taps is not yet configured on this blockchain");
   const auto& tnt_parameters = d.get_global_properties().parameters.extensions.value.updatable_tnt_options;
   FC_ASSERT(tnt_parameters.valid(), "Tanks and Taps is not yet enabled on this blockchain");

   FC_ASSERT(d.get_balance(o.payer, asset_id_type()).amount >= o.deposit_amount,
             "Insufficient balance to pay the deposit");

   new_tank = ptnt::tank_schematic::from_create_operation(o);
   ptnt::tank_validator validator(new_tank, tnt_parameters->max_sink_chain_length, make_lookup(d));
   validator.validate_tank();
   FC_ASSERT(validator.calculate_deposit(*tnt_parameters) == o.deposit_amount, "Incorrect deposit amount");

   return {};
}

object_id_type tank_create_evaluator::do_apply(const tank_create_operation& o) {
   auto& d = db();
   d.adjust_balance(o.payer, -o.deposit_amount);
   return d.create<tank_object>([&schema = new_tank, &o](tank_object& tank) {
      tank.schematic = std::move(schema);
      tank.balance.asset_id = schema.asset_type;
      tank.deposit = o.deposit_amount;
      tank.restrictor_ID = tank.schematic.get_deposit_source_restrictor();
   }).id;
}

void_result tank_update_evaluator::do_evaluate(const tank_update_evaluator::operation_type& o) {
   const auto& d = db();
   FC_ASSERT(HARDFORK_BSIP_72_PASSED(d.head_block_time()), "Tanks and Taps is not yet configured on this blockchain");
   const auto& tnt_parameters = d.get_global_properties().parameters.extensions.value.updatable_tnt_options;
   FC_ASSERT(tnt_parameters.valid(), "Tanks and Taps is not yet enabled on this blockchain");

   old_tank = &o.tank_to_update(d);
   FC_ASSERT(o.update_authority == *old_tank->schematic.taps.at(0).open_authority,
             "Tank update authority is incorrect");
   updated_tank = old_tank->schematic;
   updated_tank.update_from_operation(o);
   ptnt::tank_validator validator(updated_tank, tnt_parameters->max_sink_chain_length, make_lookup(d), old_tank->id);
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
   FC_ASSERT(HARDFORK_BSIP_72_PASSED(d.head_block_time()), "Tanks and Taps is not yet configured on this blockchain");

   old_tank = &o.tank_to_delete(d);
   FC_ASSERT(o.delete_authority == *old_tank->schematic.taps.at(0).open_authority,
             "Tank update authority is incorrect");
   FC_ASSERT(old_tank->balance.amount == 0, "Cannot delete a tank with an outstanding balance");
   FC_ASSERT(o.deposit_claimed == old_tank->deposit, "Incorrect deposit amount");

   return {};
}

void_result tank_delete_evaluator::do_apply(const tank_delete_evaluator::operation_type& o) {
   auto& d = db();
   d.adjust_balance(o.payer, o.deposit_claimed);
   d.remove(*old_tank);

   return {};
}

void_result tank_query_evaluator::do_evaluate(const tank_query_evaluator::operation_type& o) {
   const auto& d = db();
   query_tank = &o.tank_to_query(d);
   evaluator.set_query_tank(*query_tank);
   std::set<decltype(o.required_authorities)::const_iterator> used_auths;

   for(const auto& query : o.queries) { try {
      auto required_auths = evaluator.evaluate_query(query, d);
      for (const auto& auth : required_auths) {
         auto itr = std::find(o.required_authorities.begin(), o.required_authorities.end(), auth);
         FC_ASSERT(itr != o.required_authorities.end(), "Missing required authority for query: ${A}", ("A", auth));
         used_auths.insert(itr);
      }
   } FC_CAPTURE_AND_RETHROW((query)) }

   if (used_auths.size() != o.required_authorities.size()) {
      vector<authority> unused_auths;
      for (auto itr = o.required_authorities.begin(); itr != o.required_authorities.end(); ++itr)
         if (used_auths.count(itr) == 0)
            unused_auths.push_back(*itr);
      FC_THROW_EXCEPTION(fc::assert_exception, "Authorities were declared as required, but not used: ${Auths}",
                         ("Auths", unused_auths));
   }

   return {};
}

void_result tank_query_evaluator::do_apply(const tank_query_evaluator::operation_type&) {
   db().modify(*query_tank, [&evaluator=evaluator](tank_object& tank) {
      evaluator.apply_queries(tank);
   });
   return {};
}

} } // namespace graphene::chain
