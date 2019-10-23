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

#include <graphene/protocol/tnt/types.hpp>

namespace graphene { namespace protocol { namespace tnt {

/// An address of a particular tank accessory; content varies depending on the accessory type
template<typename Accessory, typename = void>
struct tank_accessory_address;
template<typename Attachment>
struct tank_accessory_address<Attachment, std::enable_if_t<tank_attachment::can_store<Attachment>()>> {
   using accessory_type = Attachment;

   /// The ID of the attachment to query
   index_type attachment_ID;

   /// Get the tank attachment from the supplied tank schematic
   const Attachment& get(const tank_schematic& schematic) const {
      try {
      FC_ASSERT(schematic.attachments.count(attachment_ID) != 0,
                "Tank accessory address references nonexistent tap");
      FC_ASSERT(schematic.attachments.at(attachment_ID).template is_type<Attachment>(),
                "Tank accessory address references attachment of incorrect type");
      return schematic.attachments.at(attachment_ID).template get<Attachment>();
      } FC_CAPTURE_AND_RETHROW((*this))
   }

   bool operator==(const tank_accessory_address& other) const { return attachment_ID == other.attachment_ID; }

   FC_REFLECT_INTERNAL(tank_accessory_address, (attachment_ID))
};
template<typename Requirement>
struct tank_accessory_address<Requirement, std::enable_if_t<tap_requirement::can_store<Requirement>()>> {
   using accessory_type = Requirement;

   /// The ID of the tap with the requirement to query
   index_type tap_ID;
   /// The index of the requirement on the tap
   index_type requirement_index;

   /// Get the tap requirement from the supplied tank schematic
   const Requirement& get(const tank_schematic& schematic) const {
      FC_ASSERT(schematic.taps.count(tap_ID) != 0, "Tank accessory address references nonexistent tap");
      const tap& tp = schematic.taps.at(tap_ID);
      FC_ASSERT(tp.requirements.size() > requirement_index,
                "Tank accessory address references nonexistent tap requirement");
      FC_ASSERT(tp.requirements[requirement_index].template is_type<Requirement>(),
                "Tank accessory address references tap requirement of incorrect type");
      return tp.requirements[requirement_index].template get<Requirement>();
   }

   bool operator==(const tank_accessory_address& other) const {
      return tap_ID == other.tap_ID && requirement_index == other.requirement_index;
   }

   FC_REFLECT_INTERNAL(tank_accessory_address, (tap_ID)(requirement_index))
};

template<typename AccA, typename AccB, typename = std::enable_if_t<!std::is_same<AccA, AccB>::value>>
bool operator==(const tank_accessory_address<AccA>&, const tank_accessory_address<AccB>&) { return false; }

using tank_accessory_address_type = TL::apply<TL::apply_each<tank_accessory_list, tank_accessory_address>,
                                              static_variant>;

namespace impl {
// This namespace block contains the implementation of accessory_address_lt below
// This is a shorthand to check if a specific Address type is for a tap requirement vs a tank attachment
template<typename Address,
         std::enable_if_t<TL::contains<tank_accessory_list, typename Address::accessory_type>(), bool> = true>
constexpr static bool is_requirement_address = (Address::accessory_type::accessory_type ==
                                                tnt::tap_requirement_accessory_type);
// Base template... just says it's invalid
template<typename Variant, typename AccessoryA, typename AccessoryB, typename=void>
struct address_lt_impl {
   constexpr static bool valid = false;
};
// Compare specific address types, both of which address tap requirements
template<typename V, typename A, typename B>
struct address_lt_impl<V, A, B, std::enable_if_t<is_requirement_address<A> && is_requirement_address<B>>> {
   constexpr static bool valid = true;
   bool operator()(const A& a, const B& b) const {
      return std::make_pair(a.tap_ID, a.requirement_index) < std::make_pair(b.tap_ID, b.requirement_index);
   }
};
// Compare specific address types, both of which address tank attachments
template<typename V, typename A, typename B>
struct address_lt_impl<V, A, B, std::enable_if_t<!is_requirement_address<A> && !is_requirement_address<B>>> {
   constexpr static bool valid = true;
   bool operator()(const A& a, const B& b) const {
      return a.attachment_ID < b.attachment_ID;
   }
};
// Compare specific address types, one of which is a tank attachment and the other is a tap requirement
template<typename V, typename A, typename B>
struct address_lt_impl<V, A, B, std::enable_if_t<is_requirement_address<A> != is_requirement_address<B>>> {
   constexpr static bool valid = true;
   bool operator()(const A&, const B&) const {
      // If A is the tank attachment, it's less than B; otherwise, it's greater
      return !is_requirement_address<A>;
   }
};
// Compare a tap requirement address against a tap ID
template<typename V, typename A>
struct address_lt_impl<V, A, tap_id_type, std::enable_if_t<is_requirement_address<A>>> {
   constexpr static bool valid = true;
   bool operator()(const A& a, const tnt::tap_id_type& tid) const { return a.tap_ID < tid.tap_id; }
};
// Compare a tap ID against a tap requirement address
template<typename V, typename A>
struct address_lt_impl<V, tap_id_type, A, std::enable_if_t<is_requirement_address<A>>> {
   constexpr static bool valid = true;
   bool operator()(const tnt::tap_id_type& tid, const A& a) const { return tid.tap_id < a.tap_ID; }
};
// Compare a tank attachment address against a tap ID (attachments are unconditionally less than tap IDs)
template<typename V, typename A>
struct address_lt_impl<V, A, tap_id_type, std::enable_if_t<!is_requirement_address<A>>>{
   constexpr static bool valid = true;
   bool operator()(const A&, const tnt::tap_id_type&) const { return true; }
};
// Compare a tap ID against a tank attachment address (tap IDs are unconditionally greater than attachments)
template<typename V, typename A>
struct address_lt_impl<V, tap_id_type, A, std::enable_if_t<!is_requirement_address<A>>>{
   constexpr static bool valid = true;
   bool operator()(const tnt::tap_id_type&, const A&) const { return false; }
};
// Compare two address variants
template<typename V>
struct address_lt_impl<V, V, V, void> {
   constexpr static bool valid = true;
   bool operator()(const V& a, const V& b) const {
      return tnt::TL::runtime::dispatch(typename V::list(), a.which(), [&a, &b] (auto A) -> bool {
         using A_type = typename decltype(A)::type;
         return tnt::TL::runtime::dispatch(typename V::list(), b.which(), [&a, &b](auto B) -> bool {
            using B_type = typename decltype(B)::type;
            address_lt_impl<V, A_type, B_type> inner;
            return inner(a.template get<A_type>(), b.template get<B_type>());
         });
      });
   }
};
// Compare an address variant with a specific address type or tap ID
template<typename V, typename A>
struct address_lt_impl<V, V, A, std::enable_if_t<V::template can_store<A>() || std::is_same<A, tap_id_type>::value>> {
   constexpr static bool valid = true;
   bool operator()(const V& v, const A& a) const {
      return tnt::TL::runtime::dispatch(typename V::list(), v.which(), [&v, &a](auto t) {
         using V_type = typename decltype(t)::type;
         address_lt_impl<V, V_type, A> inner;
         return inner(v.template get<V_type>(), a);
      });
   }
};
// Compare a specific address type or tap ID with an address variant
template<typename V, typename A>
struct address_lt_impl<V, A, V, std::enable_if_t<V::template can_store<A>() || std::is_same<A, tap_id_type>::value>> {
   constexpr static bool valid = true;
   bool operator()(const A& a, const V& v) const {
      return tnt::TL::runtime::dispatch(typename V::list(), v.which(), [&a, &v](auto t) {
         using V_type = typename decltype(t)::type;
         address_lt_impl<V, A, V_type> inner;
         return inner(a, v.template get<V_type>());
      });
   }
};
}

/// Comparator for accessory_address types; uses the following semantics:
///  - Ordering is defined according to value, not type, except all tank attachments are less than tap requirements
///  - Tank attachment addresses are ordered by their attachment IDs
///  - Tap requirement addresses are ordered by their (tap ID, requirement index) pairs
/// The comparator also accepts tap_id_type operands, which match as equal to all requirement addresses on that tap
template<typename AddressVariant = tank_accessory_address_type>
struct accessory_address_lt {
   // Having an is_transparent type tells associative containers that the comparator supports comparing keys against
   // other types than the key. The actual type is ignored, only its existence is checked.
   using is_transparent = void;
   // Implementation is proxied out to implementation SFINAE template
   template<typename A, typename B, typename=std::enable_if_t<impl::address_lt_impl<AddressVariant, A, B>::valid>>
   bool operator()(const A& a, const B& b) const {
      impl::address_lt_impl<AddressVariant, A, B> inner;
      return inner(a, b);
   }
};

} } } // namespace graphene::protocol::tnt

FC_COMPLETE_INTERNAL_REFLECTION_TEMPLATE((typename Accessory),
                                         graphene::protocol::tnt::tank_accessory_address<Accessory>)
