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

   FC_REFLECT_INTERNAL(tank_accessory_address, (tap_ID)(requirement_index))
};

using tank_accessory_address_type = TL::apply<TL::apply_each<tank_accessory_list, tank_accessory_address>,
                                              static_variant>;

/// Comparator for accessory_address types; uses the following semantics:
///  - Ordering is done according to address value, not type
///  - Tank attachment addresses are ordered by their attachment IDs
///  - Tap requirement addresses are ordered by their (tap ID, requirement index) pairs
///  - Tank attachment addresses are ordered before tap requirement addresses
/// The comparator also accepts tap_id_type operands, which match as equal to all requirement addresses on that tap
template<typename AddressVariant = tank_accessory_address_type>
struct accessory_address_lt {
   using is_transparent = void;
   template<typename Address, typename = std::enable_if_t<AddressVariant::template can_store<Address>()>>
   constexpr static bool is_requirement_address = (Address::accessory_type::accessory_type ==
                                                   tnt::tap_requirement_accessory_type);

   template<typename Address_A, typename Address_B, std::enable_if_t<!is_requirement_address<Address_A> &&
                                                                     !is_requirement_address<Address_B>, bool> = true>
   bool operator()(const Address_A& a, const Address_B& b) const {
      return a.attachment_ID < b.attachment_ID;
   }
   template<typename Address_A, typename Address_B, std::enable_if_t<is_requirement_address<Address_A> &&
                                                                     is_requirement_address<Address_B>, bool> = true>
   bool operator()(const Address_A& a, const Address_B& b) const {
      return std::make_pair(a.tap_ID, a.requirement_index) < std::make_pair(b.tap_ID, b.requirement_index);
   }
   template<typename Address_A, typename Address_B, std::enable_if_t<is_requirement_address<Address_A> !=
                                                                     is_requirement_address<Address_B>, bool> = true>
   bool operator()(const Address_A&, const Address_B&) const {
      return !is_requirement_address<Address_A>;
   }
   template<typename Address, std::enable_if_t<is_requirement_address<Address>, bool> = true>
   bool operator()(const Address& a, const tnt::tap_id_type& tid) const { return a.tap_ID < tid.tap_id; }
   template<typename Address, std::enable_if_t<!is_requirement_address<Address>, bool> = true>
   bool operator()(const Address&, const tnt::tap_id_type&) const { return true; }
   template<typename Address, std::enable_if_t<is_requirement_address<Address>, bool> = true>
   bool operator()(const tnt::tap_id_type& tid, const Address& a) const { return tid.tap_id < a.tap_ID; }
   template<typename Address, std::enable_if_t<!is_requirement_address<Address>, bool> = true>
   bool operator()(const tnt::tap_id_type&, const Address&) const { return false; }
   bool operator()(const AddressVariant& a, const AddressVariant& b) const {
      return tnt::TL::runtime::dispatch(typename AddressVariant::list(), a.which(), [this, &a, &b] (auto A) {
         using A_type = typename decltype(A)::type;
         return tnt::TL::runtime::dispatch(typename AddressVariant::list(), b.which(), [this, &a, &b](auto B) {
            using B_type = typename decltype(B)::type;
            return (*this)(a.template get<A_type>(), b.template get<B_type>());
         });
      });
   }
   bool operator()(const AddressVariant& a, const tnt::tap_id_type& tid) const {
      return tnt::TL::runtime::dispatch(typename AddressVariant::list(), a.which(), [this, &a, &tid](auto A) {
         return (*this)(a.template get<typename decltype(A)::type>(), tid);
      });
   }
   bool operator()(const tnt::tap_id_type& tid, const AddressVariant& a) const {
      return tnt::TL::runtime::dispatch(typename AddressVariant::list(), a.which(), [this, &tid, &a](auto A) {
         return (*this)(tid, a.template get<typename decltype(A)::type>());
      });
   }
};

} } } // namespace graphene::protocol::tnt

FC_COMPLETE_INTERNAL_REFLECTION_TEMPLATE((typename Accessory),
                                         graphene::protocol::tnt::tank_accessory_address<Accessory>)
