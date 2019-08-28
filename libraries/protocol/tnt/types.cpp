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

#include <fc/exception/exception.hpp>

#include <graphene/protocol/tnt/operations.hpp>

namespace graphene { namespace protocol { namespace tnt {

tank_schematic tank_schematic::from_create_operation(const tank_create_operation &create_op) {
   tank_schematic schema;
   for (const auto& attachment : create_op.attachments)
      schema.attachments[schema.attachment_counter++] = attachment;
   for (const auto& tap : create_op.taps)
      schema.taps[schema.tap_counter++] = tap;
   schema.asset_type = create_op.contained_asset;
   return schema;
}

const deposit_source_restrictor* tank_schematic::get_deposit_source_restrictor() const {
   for (const auto& attachment_pair : attachments)
      if (attachment_pair.second.is_type<deposit_source_restrictor>())
         return &attachment_pair.second.get<deposit_source_restrictor>();
   return nullptr;
}

fc::optional<size_t> deposit_source_restrictor::get_matching_deposit_path(const deposit_path &path,
                                                                          const fc::optional<tank_id_type>& my_tank)
const {
   static_assert(std::is_same<deposit_path_element, static_variant<sink, wildcard_sink>>{},
                 "deposit_path_element is not the expected type; update this function to handle the new type");
   static_assert(std::is_same<sink, static_variant<same_tank, account_id_type, tank_id_type, attachment_id_type>>{},
                 "sink is not the expected type; update this function to handle the new type");
   for (size_t i = 0; i < legal_deposit_paths.size(); ++i) {
      const deposit_path_pattern& pattern = legal_deposit_paths[i];
      FC_ASSERT(!pattern.empty(), "LOGIC ERROR: Empty deposit path pattern; please report this error");
      fc::optional<tank_id_type> chain_current_tank;
      sink_eq eq(my_tank, chain_current_tank);
      auto pattern_element = pattern.begin();

      // Check origin; if origin is known...
      if (path.origin.valid()) {
         FC_ASSERT(!path.origin->is_type<same_tank>(),
                   "LOGIC ERROR: Deposit path origin is same_tank. Please report this error.");
         // If origin is a tank ID, set it as the initial chain current tank
         if (path.origin->is_type<tank_id_type>())
            chain_current_tank = path.origin->get<tank_id_type>();

         // Match against a wildcard
         if (pattern_element->is_type<wildcard_sink>()) {
            if (!pattern_element->get<wildcard_sink>().repeatable) {
               ++pattern_element;
            }
         // Match against a sink
         } else if (eq(pattern_element->get<sink>(), *path.origin)) {
            ++pattern_element;
         // No match; move to next pattern
         } else {
             continue;
         }
      // Origin unknown
      } else {
         // Match against a wildcard
         if (pattern_element->is_type<wildcard_sink>()) {
            if (!pattern_element->get<wildcard_sink>().repeatable) {
               ++pattern_element;
            }
         // Match against a sink
         } else {
            // Unknown origin never matches against an account ID; move to next pattern
            if (pattern_element->get<sink>().is_type<account_id_type>())
               continue;
            // Unknown origin matches against other sinks
            ++pattern_element;
         }
      }

      // Origin is matched; now match path
      FC_ASSERT(!path.sink_chain.empty(), "LOGIC ERROR: Empty deposit path; please report this error");
      auto chain_element = path.sink_chain.begin();

      while (pattern_element != pattern.end() && chain_element != path.sink_chain.end()) {
         if (chain_element->get().is_type<attachment_id_type>() &&
             chain_element->get().get<attachment_id_type>().tank_id.valid())
            chain_current_tank = chain_element->get().get<attachment_id_type>().tank_id;

         // Pattern is a wildcard, so it matches, but how many?
         if (pattern_element->is_type<wildcard_sink>()) {
            // Non-repeatable wildcard is easy, move both elements forward and go to next loop
            if (!pattern_element->get<wildcard_sink>().repeatable) {
               ++pattern_element;
               ++chain_element;
               continue;
            // Repeatable wildcard, here's the fun part...
            } else {
               // First, move to the next pattern element
               // If it's the end, then it's wildcards to the end, so it matches. Return the matching pattern.
               if (++pattern_element == pattern.end())
                  return i;
               // Get the sink we'll match against next
               const sink& next_pattern_element = pattern_element->get<sink>();
               // Now we iterate the chain elements looking for one that matches the next pattern element
               while (chain_element != path.sink_chain.end()) {
                  // If chain element matches next pattern element, we can leave the wildcard loop
                  if (eq(next_pattern_element, chain_element->get())) {
                     ++pattern_element;
                     ++chain_element;
                     continue;
                  }
                  // No match, stay in the wildcard loop
                  ++chain_element;
               }
               // We've run out of chain elements, never matching the one after the wildcard. Pattern does not match.
               break;
            }
         }
         // Pattern is not a wildcard, so compare the sinks. If they match, move both elements and loop again
         if (eq(pattern_element->get<sink>(), chain_element->get())) {
            ++pattern_element;
            ++chain_element;
            continue;
         }
         // Pattern is not a wildcard, nor did the sinks match, so this pattern doesn't match. Move to the next one.
         break;
      }

      // Matching is complete. If the entire chain matched the entire pattern, it's a successful match
      if (pattern_element == pattern.end() && chain_element == path.sink_chain.end())
         return i;
      // Something mismatched before one side or the other reached the end. Loop on to the next pattern...
   }

   // All patterns tested, but none matched. Return failure.
   return {};
}

template<typename L, typename R>
struct sink_eq_impl {
   const sink_eq& q;
   sink_eq_impl(const sink_eq& q) : q(q) {}
   bool operator()(const L&, const R&) const {
      return false;
   }
};
template<typename T>
struct sink_eq_impl<T, T> {
   const sink_eq& q;
   sink_eq_impl(const sink_eq& q) : q(q) {}
   bool operator()(const T& left, const T& right) const {
      return left == right;
   }
};
template<>
struct sink_eq_impl<attachment_id_type, attachment_id_type> {
   const sink_eq& q;
   sink_eq_impl(const sink_eq& q) : q(q) {}
   bool operator()(const attachment_id_type& left, const attachment_id_type& right) const {
      if (left.attachment_id != right.attachment_id)
         return false;
      if (left.tank_id.valid() && right.tank_id.valid())
         return *left.tank_id == *right.tank_id;
      if (left.tank_id.valid() && q.right_current.valid())
         return *left.tank_id == *q.right_current;
      if (q.left_current.valid() && right.tank_id.valid())
         return *q.left_current == *right.tank_id;
      return false;
   }
};
template<>
struct sink_eq_impl<same_tank, same_tank> {
   const sink_eq& q;
   sink_eq_impl(const sink_eq& q) : q(q) {}
   bool operator()(const same_tank&, const same_tank&) const {
      return q.left_current.valid() && q.right_current.valid() && *q.left_current == *q.right_current;
   }
};
template<>
struct sink_eq_impl<tank_id_type, same_tank> {
   const sink_eq& q;
   sink_eq_impl(const sink_eq& q) : q(q) {}
   bool operator()(const tank_id_type& left, const same_tank&) const {
      if (!q.right_current.valid())
         return false;
      return left == *q.right_current;
   }
};
template<>
struct sink_eq_impl<same_tank, tank_id_type> {
   const sink_eq& q;
   sink_eq_impl(const sink_eq& q) : q(q) {}
   bool operator()(const same_tank&, const tank_id_type& right) const {
      if (!q.left_current.valid())
         return false;
      return q.left_current == right;
   }
};

bool sink_eq::operator()(const sink &left, const sink &right) const {
   static_assert(std::is_same<sink, static_variant<same_tank, account_id_type, tank_id_type, attachment_id_type>>{},
                 "sink is not the expected type; update sink_eq_impl to handle the new type");
   return fc::typelist::runtime::dispatch(sink::list(), left.which(), [this, &left, &right](auto l) {
      using Left = typename decltype(l)::type;
      return fc::typelist::runtime::dispatch(sink::list(), right.which(), [this, &left, &right](auto r) {
         using Right = typename decltype(r)::type;
         return sink_eq_impl<Left, Right>(*this)(left.get<Left>(), right.get<Right>());
      });
   });
}

} } } // namespace graphene::protocol::tnt
