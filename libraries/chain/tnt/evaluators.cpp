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
#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/protocol/tnt/validation.hpp>

namespace graphene { namespace chain {
namespace tnt = graphene::protocol::tnt;

tnt::tank_lookup_function make_lookup(const database& d) {
   return [&d](tank_id_type id) -> const tnt::tank_schematic* {
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

   new_tank = tnt::tank_schematic::from_create_operation(o);
   tnt::tank_validator validator(new_tank, tnt_parameters->max_sink_chain_length, make_lookup(d));
   validator.validate_tank();
   FC_ASSERT(validator.calculate_deposit(*tnt_parameters) == o.deposit_amount, "Incorrect deposit amount");

   return {};
}

object_id_type tank_create_evaluator::do_apply(const tank_create_operation& o) {
   auto& d = db();
   asset_store deposit_paid = asset_store::unchecked_create(o.deposit_amount);
   d.adjust_balance(o.payer, -deposit_paid.stored_asset());
   return d.create<tank_object>([&schema = new_tank, &deposit_paid](tank_object& tank) {
      tank.schematic = std::move(schema);
      tank.balance = asset_store(schema.asset_type);
      deposit_paid.to(tank.deposit);
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
   tnt::tank_validator validator(updated_tank, tnt_parameters->max_sink_chain_length, make_lookup(d), old_tank->id);
   validator.validate_tank();

   auto new_deposit = validator.calculate_deposit(*tnt_parameters);
   FC_ASSERT(old_tank->deposit.amount() - new_deposit == o.deposit_delta, "Incorrect deposit delta");
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

      if (o.deposit_delta > 0)
         asset_store::unchecked_create(o.deposit_delta).to(tank.deposit);
      else if (o.deposit_delta < 0)
         tank.deposit.move(-o.deposit_delta).unchecked_destroy();

      for (auto id : o.attachments_to_remove)
         tank.attachment_states.erase(id);
      for (auto id_att_pair : o.attachments_to_replace)
         tank.attachment_states.erase(id_att_pair.first);
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
   FC_ASSERT(old_tank->balance.empty(), "Cannot delete a tank with an outstanding balance");
   FC_ASSERT(o.deposit_claimed == old_tank->deposit.amount(), "Incorrect deposit amount");

   return {};
}

void_result tank_delete_evaluator::do_apply(const tank_delete_evaluator::operation_type& o) {
   auto& d = db();
   d.adjust_balance(o.payer, o.deposit_claimed);
   d.remove(*old_tank);

   return {};
}

} } // namespace graphene::chain
