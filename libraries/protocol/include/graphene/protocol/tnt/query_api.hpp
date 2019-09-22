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

#include <graphene/protocol/tnt/types.hpp>

namespace graphene { namespace protocol { namespace tnt {

/// @name Tank Queries
///
/// These are queries that can be run on tanks or tank accessories, such as attachments or tap requirements. These
/// queries can be used to perform actions or record information relevant to particular accessories, or the tank as a
/// whole. Each query type corresponds to a particular action to take, and queries frequently contain arguments which
/// control details about the action to take.
///
/// All queries specify a target type, which is the particular accessory type they pertain to. If a query pertains to
/// the tank in general, its target type should be @ref tank_query.
///
/// All queries must be declared in this file. Query implementation logic is defined in the chain library.
/// @{

/// Queries can be targeted at this type to pertain to the tank as a whole rather than any particular accessory
struct tank_query { using target_type = tank_schematic; };

namespace queries {

/// Reset a meter to zero
struct reset_meter {
   using target_type = asset_flow_meter;
   // No arguments

   void validate() const {}
};

/// Reconnect a tank attachment that receives asset so it deposits asset to a new sink
struct reconnect_attachment {
   using target_type = attachment_connect_authority;

   /// The new sink to connect the attachment to
   sink new_sink;

   void validate() const {}
};

/// Create a new request to open a tap which has a @ref review_requirement
struct create_request_for_review {
   using target_type = review_requirement;

   /// The amount being requested to release through the tap
   asset_flow_limit request_amount;
   /// Optional comment from the requester about why they are requesting a release of asset
   optional<string> comment;

   void validate() const {
      if (request_amount.is_type<share_type>())
         FC_ASSERT(request_amount.get<share_type>() != 0, "Request amount must not be zero");
      if (comment.valid()) {
         FC_ASSERT(!comment->empty(), "If provided, comment must not be empty");
         FC_ASSERT(comment->size() <= 150, "Comment is max 150 characters");
      }
   }
};

/// Review a request to open a tap which has a @ref review_requirement
struct review_request_to_open {
   using target_type = review_requirement;

   /// ID of the request being reviewed
   index_type request_ID;
   /// Whether the request passed review or not
   bool approved;
   /// Optional comment from the reviewer about the request; max 150 chars
   optional<string> comment;

   void validate() const {
      if (comment.valid()) {
         FC_ASSERT(!comment->empty(), "If provided, comment must not be empty");
         FC_ASSERT(comment->size() <= 150, "Comment is max 150 characters");
      }
   }
};

/// Cancel a request to open a tap which has a @ref review_requirement
struct cancel_request_for_review {
   using target_type = review_requirement;

   /// ID of the request to cancel
   index_type request_ID;
   /// Optional comment about why the request was canceled; max 150 chars
   optional<string> comment;

   void validate() const {
      if (comment.valid()) {
         FC_ASSERT(!comment->empty(), "If provided, comment must not be empty");
         FC_ASSERT(comment->size() <= 150, "Comment is max 150 characters");
      }
   }
};

/// Open a tap which has a @ref review_requirement by consuming an approved request
struct consume_approved_request_to_open {
   using target_type = review_requirement;

   /// ID of the request to consume
   index_type request_ID;

   void validate() const {}
};

/// Document the reason for the action being taken
struct documentation_string {
   // Documentation is always allowed, even if there is no documentation_requirement, so target the tank itself
   using target_type = tank_query;

   /// The documented reason for action; max 150 chars
   string reason;

   void validate() const {
      FC_ASSERT(!reason.empty(), "Reason must not be empty");
      FC_ASSERT(reason.size() <= 150, "Reason is max 150 characters");
   }
};

/// Create a new request to open a tap which has a @ref delay_requirement
struct create_request_for_delay {
   using target_type = delay_requirement;

   /// The amount being requested to release through the tap
   asset_flow_limit request_amount;
   /// Optional comment from the requester about why they are requesting a release of asset
   optional<string> comment;

   void validate() const {
      if (request_amount.is_type<share_type>())
         FC_ASSERT(request_amount.get<share_type>() != 0, "Request amount must not be zero");
      if (comment.valid()) {
         FC_ASSERT(!comment->empty(), "If provided, comment must not be empty");
         FC_ASSERT(comment->size() <= 150, "Comment is max 150 characters");
      }
   }
};

/// Veto a request to open a tap which has a @ref delay_requirement
struct veto_request_in_delay {
   using target_type = delay_requirement;

   /// ID of the request to veto
   index_type request_ID;
   /// Optional comment about why the request was vetoed; max 150 chars
   optional<string> comment;

   void validate() const {
      if (comment.valid()) {
         FC_ASSERT(!comment->empty(), "If provided, comment must not be empty");
         FC_ASSERT(comment->size() <= 150, "Comment is max 150 characters");
      }
   }
};

/// Cancel a request to open a tap which has a @ref delay_requirement
struct cancel_request_in_delay {
   using target_type = delay_requirement;

   /// ID of the request to cancel
   index_type request_ID;
   /// Optional comment about why the request was canceled; max 150 chars
   optional<string> comment;

   void validate() const {
      if (comment.valid()) {
         FC_ASSERT(!comment->empty(), "If provided, comment must not be empty");
         FC_ASSERT(comment->size() <= 150, "Comment is max 150 characters");
      }
   }
};

/// Open a tap which has a @ref delay_requirement by consuming a matured request
struct consume_matured_request_to_open {
   using target_type = delay_requirement;

   /// ID of the request to consume
   index_type request_ID;

   void validate() const {}
};

/// Provide a preimage to a hash value to fulfill a @ref hash_preimage_requirement
struct reveal_hash_preimage {
   using target_type = hash_preimage_requirement;

   /// Preimage of the hash
   vector<char> preimage;

   void validate() const {
      FC_ASSERT(!preimage.empty(), "Preimage must not be empty");
   }
};

/// Provide a signed ticket authorizing the opening of a tap with a @ref ticket_requirement
struct redeem_ticket_to_open {
   using target_type = ticket_requirement;

   /// The ticket being redeemed
   ticket_requirement::ticket_type ticket;
   /// The signature for the ticket
   signature_type ticket_signature;

   void validate() const {
      if (ticket.max_withdrawal.is_type<share_type>())
         FC_ASSERT(ticket.max_withdrawal.get<share_type>() > 0, "Maximum withdrawal must not be zero");
   }
};

/// Reset both an exchange requirement's amount released and the meter it monitors to zero
struct reset_exchange_and_meter {
   using target_type = exchange_requirement;
   // No arguments

   void validate() const {}
};

} // namespace queries

/// An address of a particular tank accessory; content varies depending on the accessory type
template<typename Accessory, typename = void>
struct tank_accessory_address;
template<typename Attachment>
struct tank_accessory_address<Attachment, std::enable_if_t<tank_attachment::can_store<Attachment>()>> {
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
   /// The ID of the tap with the requirement to query
   index_type tap_ID;
   /// The index of the requirement on the tap
   index_type requirement_index;

   /// Get the tap requirement from the supplied tank schematic
   const Requirement& get_target(const tank_schematic& schematic) {
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

/// A query with data specifying which accessory is being queried
template<typename Query, typename = void>
struct targeted_query {
   using query_type = Query;
   using target_type = typename Query::target_type;
   static_assert(std::is_same<target_type, tank_query>{}, "Unrecognized query target type; known targets are "
                                                          "tank attachments, tap requirements, and tank_query");

   /// The content of the query
   Query query_content;

   /// Get the query target from the supplied tank schematic
   const target_type& get_target(const tank_schematic& schematic) {
      return schematic;
   }

   FC_REFLECT_INTERNAL(targeted_query, (query_content))
};
template<typename Query>
struct targeted_query<Query, std::enable_if_t<TL::contains<tank_accessory_list, typename Query::target_type>()>> {
   using query_type = Query;
   using target_type = typename Query::target_type;

   /// The content of the query
   Query query_content;
   /// The address of the accessory
   tank_accessory_address<target_type> accessory_address;

   /// Get the query target from the supplied tank schematic
   const target_type& get_target(const tank_schematic& schematic) { return accessory_address.get(schematic); }

   FC_REFLECT_INTERNAL(targeted_query, (query_content)(accessory_address))
};

/// List of all query types. New types always go to the end.
using query_type_list = fc::typelist::list<queries::reset_meter, queries::reconnect_attachment,
                                           queries::create_request_for_review, queries::review_request_to_open,
                                           queries::cancel_request_for_review,
                                           queries::consume_approved_request_to_open, queries::documentation_string,
                                           queries::create_request_for_delay, queries::veto_request_in_delay,
                                           queries::cancel_request_in_delay, queries::consume_matured_request_to_open,
                                           queries::reveal_hash_preimage, queries::redeem_ticket_to_open,
                                           queries::reset_exchange_and_meter>;

/// Typelist transformer to convert a query type to a targeted query
template<typename Q> struct to_targeted_query { using type = targeted_query<Q>; };

/// static_variant of all tank query types, with target information
using tank_query_type = static_variant<fc::typelist::transform<query_type_list, to_targeted_query>>;

/// @}

} } } // namespace graphene::protocol::tnt

FC_REFLECT(graphene::protocol::tnt::tank_query, )
FC_REFLECT(graphene::protocol::tnt::queries::reset_meter, )
FC_REFLECT(graphene::protocol::tnt::queries::reconnect_attachment, (new_sink))
FC_REFLECT(graphene::protocol::tnt::queries::create_request_for_review, (request_amount)(comment))
FC_REFLECT(graphene::protocol::tnt::queries::review_request_to_open, (request_ID)(approved)(comment))
FC_REFLECT(graphene::protocol::tnt::queries::cancel_request_for_review, (request_ID)(comment))
FC_REFLECT(graphene::protocol::tnt::queries::consume_approved_request_to_open, (request_ID))
FC_REFLECT(graphene::protocol::tnt::queries::documentation_string, )
FC_REFLECT(graphene::protocol::tnt::queries::create_request_for_delay, (request_amount)(comment))
FC_REFLECT(graphene::protocol::tnt::queries::veto_request_in_delay, (request_ID)(comment))
FC_REFLECT(graphene::protocol::tnt::queries::cancel_request_in_delay, (request_ID)(comment))
FC_REFLECT(graphene::protocol::tnt::queries::consume_matured_request_to_open, (request_ID))
FC_REFLECT(graphene::protocol::tnt::queries::reveal_hash_preimage, (preimage))
FC_REFLECT(graphene::protocol::tnt::queries::redeem_ticket_to_open, (ticket)(ticket_signature))
FC_REFLECT(graphene::protocol::tnt::queries::reset_exchange_and_meter, )

FC_COMPLETE_INTERNAL_REFLECTION_TEMPLATE((typename Accessory),
                                         graphene::protocol::tnt::tank_accessory_address<Accessory>)
FC_COMPLETE_INTERNAL_REFLECTION_TEMPLATE((typename Query), graphene::protocol::tnt::targeted_query<Query>)
