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

#include <fc/reflect/reflect.hpp>
#include <fc/static_variant.hpp>
#include <fc/crypto/hash160.hpp>
#include <fc/io/raw_fwd.hpp>

#include <graphene/protocol/tnt/accessories_fwd.hpp>
#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/types.hpp>

namespace graphene { namespace protocol {
// Forward declare these so tank_schematic can take refs to them
struct tank_create_operation;
struct tank_update_operation;
namespace tnt {
namespace TL = fc::typelist;

/// \defgroup TNT Tanks and Taps
/// Tanks and Taps defines a modular, composable framework for financial smart contracts. The fundamental design is
/// that asset can be held in containers called tanks, and can be released from those tanks by taps, which are
/// connected to other tanks or accounts. Tanks can also have attachments which provide additional functionality.
/// Taps can have requirements and limits specifying when, why, and how much asset can be released from them.
///
/// These modules can be assembled into structures that model real-world contracts. The tank stores the funds that
/// are allocated for the contract, and holds these funds in an intermediate stage of ownership, during which no
/// particular account owns them or has arbitrary access to them. Different accounts can be given limited access to
/// dispense the funds through taps, perhaps with limits or requirements which must be fulfilled before asset can be
/// released.
///
/// An example of a TNT contract is an HTLC, or Hash/Time-Lock Contract, which is a smart contract where some account
/// locks funds up such that they can be relased to another account if that account can provide the preimage to a
/// hash embedded in the HTLC. If the receiving account provides the hash, the funds are released to her; however, if
/// she has not claimed the funds with the preimage by a predefined deadline, then the sending account can recover
/// the funds. To construct such a contract with TNT, the sending account creates a tank with two general-use taps,
/// one with a hash preimage requirement connected to the receiving account, and the other with a time lock
/// requirement connected to the sending account. The sender funds the tank, and if the contract is accepted, the
/// sender provides the receiving account with the preimage, allowing her to withdraw the funds. Otherwise, the
/// sender can reclaim the funds through the time locked tap after the deadline passes.
/// @{

using index_type = uint16_t;
namespace impl {
template<typename...> using make_void = void;
template<typename Accessory, typename = void> struct has_state_type_impl : std::false_type {};
template<typename Accessory>
struct has_state_type_impl<Accessory, make_void<typename Accessory::state_type>> : std::true_type {};
template<typename Accessory>
struct has_state_type { constexpr static bool value = has_state_type_impl<Accessory>::value; };
template<typename Accessory>
struct get_state_type { using type = typename Accessory::state_type; };
}

/// ID type for a tank attachment
struct attachment_id_type {
   /// ID of the tank the attachment is on; if unset, tank is inferred from context as "the current tank"
   optional<tank_id_type> tank_id;
   /// ID or index of the attachment on the specified tank
   index_type attachment_id;
};
/// ID type for a tap
struct tap_id_type {
   /// ID of the tank the tap is on; if unset, tank is inferred from context as "the current tank"
   optional<tank_id_type> tank_id;
   /// ID or index of the tap on the specified tank
   index_type tap_id;
};

/// An implicit tank ID which refers to the same tank as the item containing the reference
struct same_tank{};
/// A variant of ID types for all possible asset receivers
using sink = static_variant<same_tank, account_id_type, tank_id_type, attachment_id_type>;

/// @brief Check if sink is a terminal sink or not
///
/// Sinks can either be terminal sinks, meaning they represent a depository that can store asset over time, or not,
/// meaning they represent a structure that receives asset, but immediately deposits it to another sink. At present,
/// only a tank attachment sink is a non-terminal sink
inline bool is_terminal_sink(const sink& s) { return !s.is_type<attachment_id_type>(); }

/// Comparator to check equality of two sinks
///
/// Aside from the fact that static_variant comparison is rather annoying in general, sink comparison is also tricky
/// due to the @ref same_tank type, which is contextually defined. Thus to create this comparator, it is necessary to
/// specify the left and right side's "current_tank" values so they can be compared if either or both sides are
/// same_tank. Note that these values are taken by reference, so updates to the referenced values will be reflected
/// in the comparator's results. Note also that these values are optional, but must be defined to yield a matching
/// result; in particular, if both are null, they are still regarded as unequal.
struct sink_eq {
   const fc::optional<tank_id_type>& left_current;
   const fc::optional<tank_id_type>& right_current;

   sink_eq(const fc::optional<tank_id_type>& left_current, const fc::optional<tank_id_type>& right_current)
       : left_current(left_current), right_current(right_current) {}

   bool operator()(const sink& left, const sink& right) const;
};

struct unlimited_flow{};
/// A limit to the amount of asset that flows during a release of asset; either unlimited, or a maximum amount
using asset_flow_limit = static_variant<unlimited_flow, share_type>;
inline bool operator< (const asset_flow_limit& a, const asset_flow_limit& b) {
   if (a.is_type<unlimited_flow>()) return false;
   if (b.is_type<unlimited_flow>()) return true;
   return a.get<share_type>() < b.get<share_type>();
}
inline bool operator<=(const asset_flow_limit& a, const asset_flow_limit& b) {
   if (b.is_type<unlimited_flow>()) return true;
   if (a.is_type<unlimited_flow>()) return false;
   return a.get<share_type>() <= b.get<share_type>();
}

/// @name Attachments
/// Tank Attachments are objects which can be attached to a tank to provide additional functionality. For instance,
/// attachments can be used to restrict what sources can deposit to a tank, to automatically open a tap after asset
/// flows into the tank, or to measure how much asset has flowed into or out of a tank.
///
/// Tank attachments must all provide the following methods in their interface:
/// If the attachment can receive asset, returns the type received; otherwise, returns null
/// optional<asset_id_type> receives_asset() const;
/// If the attachment can receive asset, returns the sink the asset is deposited to; otherwise, returns null
/// optional<sink> output_sink() const;
/// @{

/// Receives asset and immediately releases it to a predetermined sink, maintaining a tally of the total amount that
/// has flowed through
struct asset_flow_meter {
   constexpr static tank_accessory_type_enum accessory_type = tank_attachment_accessory_type;
   constexpr static bool unique = false;
   struct state_type {
      /// The amount of asset that has flowed through the meter
      share_type metered_amount;
   };
   /// The type of asset which can flow through this meter
   asset_id_type asset_type;
   /// The sink which the metered asset is released to
   sink destination_sink;
   /// The authority which may reset the meter; if null, only the emergency tap authority is accepted
   optional<authority> reset_authority;

   optional<asset_id_type> receives_asset() const { return asset_type; }
   optional<sink> output_sink() const { return destination_sink; }
};

/// Contains several patterns for sources that may deposit to the tank, and rejects any deposit that comes via a path
/// that does not match against any pattern
struct deposit_source_restrictor {
   constexpr static tank_accessory_type_enum accessory_type = tank_attachment_accessory_type;
   constexpr static bool unique = true;
   /// This type defines a wildcard sink type, which matches against any sink(s)
   struct wildcard_sink {
      /// If true, wildcard matches any number of sinks; otherwise, matches exactly one
      bool repeatable;
   };
   /// A deposit path element may be a specific sink, or a wildcard to match any sink
   using deposit_path_element = static_variant<sink, wildcard_sink>;
   /// A deposit path is a sequence of sinks; a deposit path pattern is a series of sinks that incoming deposits
   /// must have flowed through, which may include wildcards that will match against any sink(s)
   using deposit_path_pattern = vector<deposit_path_element>;

   /// A list of path patterns that a deposit is checked against; if a deposit's path doesn't match any pattern, it
   /// is rejected
   vector<deposit_path_pattern> legal_deposit_paths;

   optional<asset_id_type> receives_asset() const { return {}; }
   optional<sink> output_sink() const { return {}; }

   /// A deposit path, which is matched against the @ref legal_deposit_paths
   struct deposit_path {
      /// The origin of the deposit, if known. If omitted, the origin will match any tank ID, but no account ID
      fc::optional<sink> origin;
      /// The full sink chain that the origin deposited into; this is checked even if the origin is omitted
      vector<std::reference_wrapper<const sink>> sink_chain;
   };
   /// @brief Check if the provided path matches any legal deposit path, and if so, return its index
   /// @param path The path the deposit took
   /// @param my_tank ID of the tank the deposit_source_restrictor is on
   fc::optional<size_t> get_matching_deposit_path(const deposit_path& path,
                                                  const fc::optional<tank_id_type> &my_tank = {}) const;
};

/// Receives asset and immediately releases it to a predetermined sink, scheduling a tap on the tank it is attached
/// to to be opened once the received asset stops moving
struct tap_opener {
   constexpr static tank_accessory_type_enum accessory_type = tank_attachment_accessory_type;
   constexpr static bool unique = false;
   /// Index of the tap to open (must be on the same tank as the opener)
   index_type tap_index;
   /// The amount to release
   asset_flow_limit release_amount;
   /// The sink that asset is released to after flowing through the opener
   sink destination_sink;
   /// The type of asset which can flow through the opener
   asset_id_type asset_type;

   optional<asset_id_type> receives_asset() const { return asset_type; }
   optional<sink> output_sink() const { return destination_sink; }
};

/// Allows a specified authority to update the sink a specified tank attachment releases processed asset into
struct attachment_connect_authority {
   constexpr static tank_accessory_type_enum accessory_type = tank_attachment_accessory_type;
   constexpr static bool unique = false;
   /// The authority that can reconnect the attachment
   authority connect_authority;
   /// The attachment that can be reconnected (must be on the current tank)
   index_type attachment_id;

   optional<asset_id_type> receives_asset() const { return {}; }
   optional<sink> output_sink() const { return {}; }
};
/// @}

/// @name Requirements
/// Tap Requirements are objects which can be attached to a tap to specify limits and restrictions on when, why, and
/// how much asset can flow through that tap.
/// @{

/// A flat limit on the amount that can be released in any given opening
struct immediate_flow_limit {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = true;
   share_type limit;
};

/// A limit to the cumulative total that can be released through the tap in its lifetime
struct cumulative_flow_limit {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = true;
   struct state_type {
      /// The amount of asset released so far
      share_type amount_released;
   };
   /// Limit amount
   share_type limit;
};

/// A limit to the cumulative total that can be released through the tap within a given time period
struct periodic_flow_limit {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = false;
   struct state_type {
      /// Sequence number of the period during which the last withdrawal took place
      uint32_t period_num = 0;
      /// The amount released during the period
      share_type amount_released;
   };
   /// Duration of periods in seconds; the first period begins at the tank's creation date
   uint32_t period_duration_sec = 0;
   /// Maximum cumulative amount to release in a given period
   share_type limit;
};

/// Locks and unlocks the tap at specified times
struct time_lock {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = true;
   /// Whether or not the tap is locked before the first lock/unlock time
   bool start_locked = false;
   /// At each of these times, the tap will switch between locked and unlocked -- must all be in the future
   vector<time_point_sec> lock_unlock_times;

   bool unlocked_at_time(const time_point_sec& time) const;
};

/// Prevents tap from draining tank to below a specfied balance
struct minimum_tank_level {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = true;
   /// Minimum tank balance
   share_type minimum_level;
};

/// Requires account opening tap to provide a request that must be reviewed and accepted prior to opening tap
struct review_requirement {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = true;
   /// This type describes a request to open the tap
   struct request_type {
      /// Amount requested for release
      asset_flow_limit request_amount;
      /// Optional comment about request, max 150 chars
      optional<string> request_comment;
      /// Whether the request has been approved or not
      bool approved = false;
   };
   struct state_type {
      /// Number of requests made so far; used to assign request IDs
      index_type request_counter = 0;
      /// Map of request ID to request
      flat_map<index_type, request_type> pending_requests;
   };
   /// Authority which approves or denies requests
   authority reviewer;
   /// Maximum allowed number of pending requests; zero means no limit
   index_type request_limit = 0;
};

/// Requires a non-empty documentation argument be provided when opening the tap
struct documentation_requirement {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = true;
   /* no fields; if this requirement is present, evaluator requires a documentation argument to open tap */
};

/// Requires account opening tap to create a request, then wait a specified delay before tap can be opened
struct delay_requirement {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = true;
   /// This type describes a request to open the tap
   struct request_type {
      /// When the request matures and can be consumed
      time_point_sec delay_period_end;
      /// Amount requested
      asset_flow_limit request_amount;
      /// Optional comment about request; max 150 chars
      optional<string> request_comment;
   };
   struct state_type {
      /// Number of requests made so far; used to assign request IDs
      index_type request_counter = 0;
      /// Map of request ID to request
      flat_map<index_type, request_type> pending_requests;
   };
   /// Authority which can veto request during review period; if veto occurs,
   /// reset state values
   optional<authority> veto_authority;
   /// Period in seconds after unlock request until tap unlocks; when tap opens,
   /// all state values are reset
   uint32_t delay_period_sec = 0;
   /// Maximum allowed number of outstanding requests; zero means no limit
   index_type request_limit = 0;
};

/// Requires an argument containing the preimage of a specified hash in order to open the tap
struct hash_preimage_requirement {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = false;
   using hash_type = static_variant<fc::sha256, fc::ripemd160, fc::hash160>;
   /// Specified hash value
   hash_type hash;
   /// Size of the preimage in bytes; a preimage of a different size will be rejected
   /// If null, a matching preimage of any size will be accepted
   optional<uint16_t> preimage_size;
};

/// Requires account opening tap to provide a signed ticket authorizing the tap to be opened
struct ticket_requirement {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = false;
   /// The ticket that must be signed to unlock the tap
   struct ticket_type {
      /// ID of the tank containing the tap this ticket is for
      tank_id_type tank_ID;
      /// ID of the tap this ticket is for
      index_type tap_ID;
      /// Index of the ticket_requirement in the tap's requirement list
      index_type requirement_index;
      /// Maximum asset release authorized by this ticket
      asset_flow_limit max_withdrawal;
      /// Must be equal to tickets_consumed to be valid
      index_type ticket_number;
   };
   struct state_type {
      /// Number of tickets that have been used to authorize a release of funds
      index_type tickets_consumed = 0;
   };
   /// Key that must sign tickets to validate them
   public_key_type ticket_signer;
};

/// Limits the amount released based on the amount that has been deposited to a specified meter and an exchange rate
/// The maximum release amount will be:
/// meter_reading / tick_amount * release_per_tick - amount_released
/// Thus the releases come in "ticks" such that once the meter has received a full tick amount, the tap will release
/// a tick amount.
struct exchange_requirement {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = false;
   struct state_type {
      /// The amount of asset released so far
      share_type amount_released;
   };
   /// The ID of the meter to check
   attachment_id_type meter_id;
   /// The amount to release per tick of the meter
   share_type release_per_tick;
   /// Amount of metered asset per tick
   share_type tick_amount;
   /// Authority which can reset the amount released; if null, only the emergency tap authority is authorized
   fc::optional<authority> reset_authority;

   share_type max_release_amount(share_type amount_released, const asset_flow_meter::state_type& meter_state) const {
      return meter_state.metered_amount / tick_amount * release_per_tick - amount_released;
   }
};
/// @}

using tank_attachment = TL::apply<TL::filter<tank_accessory_list, tank_attachment_filter::filter>, static_variant>;
using tap_requirement = TL::apply<TL::filter<tank_accessory_list, tap_requirement_filter::filter>, static_variant>;

using stateful_accessory_list = TL::filter<tank_accessory_list, impl::has_state_type>;
using tank_accessory_state = TL::apply<TL::transform<stateful_accessory_list, impl::get_state_type>, static_variant>;

/// A structure on a tank which allows asset to be released from that tank by a particular authority with limits and
/// requirements restricting when, why, and how much asset can be released
struct tap {
   /// The connected sink; if omitted, connect_authority must be specified
   optional<sink> connected_sink;
   /// The authority to open the tap; if null, anyone can open the tap if they can satisfy the requirements --
   /// emergency tap must specify an open authority
   optional<authority> open_authority;
   /// The authority to connect and disconnect the tap. If unset, tap must be connected on creation, and the
   /// connection cannot be later modified -- emergency tap must specify a connect_authority
   optional<authority> connect_authority;
   /// Requirements for opening this tap and releasing asset; emergency tap may not specify any requirements
   vector<tap_requirement> requirements;
   /// If true, this tap can be used to destroy the tank when it empties; emergency tap must be a destructor tap
   bool destructor_tap;
};

/// Description of a tank's taps and attachments; used to perform internal consistency checks
struct tank_schematic {
   /// Taps on this tank. ID 0 must be present, and must not have any tap_requirements
   flat_map<index_type, tap> taps;
   /// Counter of taps added; used to assign tap IDs
   index_type tap_counter = 0;
   /// Attachments on this tank
   flat_map<index_type, tank_attachment> attachments;
   /// Counter of attachments added; used to assign attachment IDs
   index_type attachment_counter = 0;
   /// Type of asset this tank can store
   asset_id_type asset_type;

   /// Initialize from a tank_create_operation
   static tank_schematic from_create_operation(const tank_create_operation& create_op);
   /// Update from a tank_update_operation
   void update_from_operation(const tank_update_operation& update_op);

   /// Returns the ID of the deposit_source_restrictor attachment, if one exists; null otherwise
   fc::optional<index_type> get_deposit_source_restrictor() const;
};

/// @}

} } } // namespace graphene::protocol::tnt

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(graphene::protocol::tnt::tank_schematic)

FC_REFLECT(graphene::protocol::tnt::attachment_id_type, (tank_id)(attachment_id))
FC_REFLECT(graphene::protocol::tnt::tap_id_type, (tank_id)(tap_id))
FC_REFLECT(graphene::protocol::tnt::unlimited_flow,)
FC_REFLECT(graphene::protocol::tnt::same_tank,)

FC_REFLECT(graphene::protocol::tnt::asset_flow_meter::state_type, (metered_amount))
FC_REFLECT(graphene::protocol::tnt::asset_flow_meter, (asset_type)(destination_sink)(reset_authority))
FC_REFLECT(graphene::protocol::tnt::deposit_source_restrictor::wildcard_sink, (repeatable))
FC_REFLECT(graphene::protocol::tnt::deposit_source_restrictor, (legal_deposit_paths))
FC_REFLECT(graphene::protocol::tnt::tap_opener, (tap_index)(release_amount)(destination_sink)(asset_type))
FC_REFLECT(graphene::protocol::tnt::attachment_connect_authority, (connect_authority)(attachment_id))

FC_REFLECT(graphene::protocol::tnt::immediate_flow_limit, (limit))
FC_REFLECT(graphene::protocol::tnt::cumulative_flow_limit::state_type, (amount_released))
FC_REFLECT(graphene::protocol::tnt::cumulative_flow_limit, (limit))
FC_REFLECT(graphene::protocol::tnt::periodic_flow_limit::state_type, (period_num)(amount_released))
FC_REFLECT(graphene::protocol::tnt::periodic_flow_limit, (period_duration_sec)(limit))
FC_REFLECT(graphene::protocol::tnt::time_lock, (start_locked)(lock_unlock_times))
FC_REFLECT(graphene::protocol::tnt::minimum_tank_level, (minimum_level))
FC_REFLECT(graphene::protocol::tnt::review_requirement::request_type,
           (request_amount)(request_comment)(approved))
FC_REFLECT(graphene::protocol::tnt::review_requirement::state_type, (request_counter)(pending_requests))
FC_REFLECT(graphene::protocol::tnt::review_requirement, (reviewer)(request_limit))
FC_REFLECT(graphene::protocol::tnt::documentation_requirement,)
FC_REFLECT(graphene::protocol::tnt::delay_requirement::request_type,
           (delay_period_end)(request_amount)(request_comment))
FC_REFLECT(graphene::protocol::tnt::delay_requirement::state_type,
           (request_counter)(pending_requests))
FC_REFLECT(graphene::protocol::tnt::delay_requirement, (veto_authority)(delay_period_sec)(request_limit))
FC_REFLECT(graphene::protocol::tnt::hash_preimage_requirement, (hash)(preimage_size))
FC_REFLECT(graphene::protocol::tnt::ticket_requirement::ticket_type,
           (tank_ID)(tap_ID)(requirement_index)(max_withdrawal)(ticket_number))
FC_REFLECT(graphene::protocol::tnt::ticket_requirement::state_type, (tickets_consumed))
FC_REFLECT(graphene::protocol::tnt::ticket_requirement, (ticket_signer))
FC_REFLECT(graphene::protocol::tnt::exchange_requirement::state_type, (amount_released))
FC_REFLECT(graphene::protocol::tnt::exchange_requirement, (meter_id)(release_per_tick)(tick_amount)(reset_authority))
FC_REFLECT(graphene::protocol::tnt::tap,
           (connected_sink)(open_authority)(connect_authority)(requirements)(destructor_tap))
FC_REFLECT(graphene::protocol::tnt::tank_schematic,
           (taps)(tap_counter)(attachments)(attachment_counter)(asset_type))

FC_REFLECT_TYPENAME(graphene::protocol::tnt::hash_preimage_requirement::hash_type)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::sink)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::asset_flow_limit)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::deposit_source_restrictor::deposit_path_element)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::deposit_source_restrictor::deposit_path_pattern)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::tank_attachment)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::tap_requirement)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::tank_accessory_state)
