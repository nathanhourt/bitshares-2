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

   friend bool operator==(const attachment_id_type& a, const attachment_id_type& b) {
      return std::tie(a.tank_id, a.attachment_id) == std::tie(b.tank_id, b.attachment_id);
   }
   friend bool operator!=(const attachment_id_type& a, const attachment_id_type& b) { return !(a==b); }
   friend bool operator<(const attachment_id_type& a, const attachment_id_type& b) {
      return std::tie(a.tank_id, a.attachment_id) < std::tie(b.tank_id, b.attachment_id);
   }
};
/// ID type for a tap
struct tap_id_type {
   /// ID of the tank the tap is on; if unset, tank is inferred from context as "the current tank"
   optional<tank_id_type> tank_id;
   /// ID or index of the tap on the specified tank
   index_type tap_id;

   friend bool operator==(const tap_id_type& a, const tap_id_type& b) {
      return std::tie(a.tank_id, a.tap_id) == std::tie(b.tank_id, b.tap_id);
   }
   friend bool operator!=(const tap_id_type& a, const tap_id_type& b) { return !(a==b); }
   friend bool operator<(const tap_id_type& a, const tap_id_type& b) {
      return std::tie(a.tank_id, a.tap_id) < std::tie(b.tank_id, b.tap_id);
   }
};

/// An implicit tank ID which refers to the same tank as the item containing the reference
struct same_tank{ friend bool operator<(const same_tank&, const same_tank&) { return false; } };
/// A pipeline over which asset can flow. A connection specifies a location that asset can send or receive from.
using connection = static_variant<same_tank, account_id_type, tank_id_type, attachment_id_type>;
/// A connection to/from somewhere not on the current tank
using remote_connection = static_variant<account_id_type, tank_id_type, attachment_id_type>;

/// @brief Check if connection is a terminal connection or not
///
/// Connections can either be terminal connections, meaning they represent a depository that can store asset over
/// time, or not, meaning they represent a structure that receives asset, but immediately deposits it to another
/// connection. At present, only a tank attachment connection is a non-terminal connection
inline bool is_terminal_connection(const connection& s) { return !s.is_type<attachment_id_type>(); }

inline bool operator<(const remote_connection& left, const remote_connection& right) {
   if (left.which() != right.which())
      return left.which() < right.which();
   return TL::runtime::dispatch(remote_connection::list(), left.which(), [&left, &right](auto t) {
      using Connection = typename decltype(t)::type;
      return left.get<Connection>() < right.get<Connection>();
   });
}

/// Empty type to indicate that a structure will receive asset from all sources
struct all_sources{};
/// A restriction on what sources a structure (presently, either a tank or a tank attachment) will receive asset
/// from. Deposits coming immediately from a remote source (not the same tank) not specified in the set will be
/// rejected with an error. If @ref all_sources is specified, deposits will not be restricted based on source.
using authorized_connections_type = static_variant<flat_set<remote_connection>, all_sources>;

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
/// If the attachment can receive asset, returns the connection the asset is deposited to; otherwise, returns null
/// optional<connection> output_connection() const;
/// @{

/// Receives asset and immediately releases it to a predetermined connection, maintaining a tally of the total amount
/// that has flowed through
struct asset_flow_meter {
   constexpr static tank_accessory_type_enum accessory_type = tank_attachment_accessory_type;
   constexpr static bool unique = false;
   constexpr static bool can_receive_asset = true;
   struct state_type {
      /// The amount of asset that has flowed through the meter
      share_type metered_amount;
   };
   /// The type of asset which can flow through this meter
   asset_id_type asset_type;
   /// The connection which the metered asset is released to
   connection destination;
   /// What remote sources, if any, can deposit to this meter
   authorized_connections_type remote_sources;
   /// The authority which may reset the meter; if null, only the emergency tap authority is accepted
   optional<authority> reset_authority;

   asset_id_type receives_asset() const { return asset_type; }
   const authorized_connections_type& authorized_sources() const { return remote_sources; }
   const connection& output_connection() const { return destination; }

   asset_flow_meter(asset_id_type asset_type = {}, connection destination = {}, optional<authority> reset_auth = {})
      : asset_type(asset_type), destination(std::move(destination)), reset_authority(std::move(reset_auth)) {}
};

/// Receives asset and immediately releases it to a predetermined connection, scheduling a tap on the tank it is
/// attached to to be opened once the received asset stops moving
struct tap_opener {
   constexpr static tank_accessory_type_enum accessory_type = tank_attachment_accessory_type;
   constexpr static bool unique = false;
   constexpr static bool can_receive_asset = true;
   /// Index of the tap to open (must be on the same tank as the opener)
   index_type tap_index;
   /// The amount to release
   asset_flow_limit release_amount;
   /// The connection that asset is released to after flowing through the opener
   connection destination;
   /// What remote sources, if any, can deposit to this opener
   authorized_connections_type remote_sources;
   /// The type of asset which can flow through the opener
   asset_id_type asset_type;

   asset_id_type receives_asset() const { return asset_type; }
   const authorized_connections_type& authorized_sources() const { return remote_sources; }
   const connection& output_connection() const { return destination; }

   tap_opener(index_type tap_index = 0, asset_flow_limit release_amount = {},
              connection dest = {}, asset_id_type asset = {})
      : tap_index(tap_index), release_amount(release_amount), destination(std::move(dest)), asset_type(asset) {}
};

/// Allows a specified authority to update the connection a specified tank attachment releases processed asset into
struct attachment_connect_authority {
   constexpr static tank_accessory_type_enum accessory_type = tank_attachment_accessory_type;
   constexpr static bool unique = false;
   constexpr static bool can_receive_asset = false;
   /// The authority that can reconnect the attachment
   authority connect_authority;
   /// The attachment that can be reconnected (must be on the current tank)
   index_type attachment_id;

   attachment_connect_authority(authority connect_authority = {}, index_type attachment_id = 0)
      : connect_authority(std::move(connect_authority)), attachment_id(attachment_id) {}
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

   immediate_flow_limit(share_type limit = 0) : limit(limit) {}
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

   cumulative_flow_limit(share_type limit = 0) : limit(limit) {}
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
   uint32_t period_duration_sec;
   /// Maximum cumulative amount to release in a given period
   share_type limit;

   uint32_t period_num_at_time(const time_point_sec& creation_date, const time_point_sec& time) const {
      return uint32_t((time - creation_date).to_seconds() / period_duration_sec);
   }

   periodic_flow_limit(share_type limit = 0, uint32_t period_duration_sec = 0)
      : period_duration_sec(period_duration_sec), limit(limit) {}
};

/// Locks and unlocks the tap at specified times
struct time_lock {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = true;
   /// Whether or not the tap is locked before the first lock/unlock time
   bool start_locked = false;
   /// At each of these times, the tap will switch between locked and unlocked -- must all be in the future
   vector<time_point_sec> lock_unlock_times;

   /// Check whether the time_lock is unlocked at the provided time
   bool unlocked_at_time(const time_point_sec& time) const;

   time_lock(vector<time_point_sec> lock_unlock_times = {}, bool start_locked = false)
      : start_locked(start_locked), lock_unlock_times(std::move(lock_unlock_times)) {}
};

/// Prevents tap from draining tank to below a specfied balance
struct minimum_tank_level {
   constexpr static tank_accessory_type_enum accessory_type = tap_requirement_accessory_type;
   constexpr static bool unique = true;
   /// Minimum tank balance
   share_type minimum_level;

   minimum_tank_level(share_type minimum_level = 0) : minimum_level(minimum_level) {}
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
   index_type request_limit;

   review_requirement(authority reviewer = {}, index_type request_limit = 0)
      : reviewer(std::move(reviewer)), request_limit(request_limit) {}
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

   delay_requirement(uint32_t delay_period_sec = 0, optional<authority> veto_auth = {}, index_type request_limit = 0)
      : veto_authority(std::move(veto_auth)), delay_period_sec(delay_period_sec), request_limit(request_limit) {}
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

   hash_preimage_requirement(hash_type hash = {}, optional<uint16_t> preimage_size = {})
      : hash(std::move(hash)), preimage_size(preimage_size) {}
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

   ticket_requirement(public_key_type signer = {}) : ticket_signer(std::move(signer)) {}
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

   exchange_requirement(attachment_id_type meter_id = {}, share_type release_per_tick = 0, share_type tick_amount = 0,
                        fc::optional<authority> reset_authority = {})
      : meter_id(meter_id), release_per_tick(release_per_tick), tick_amount(tick_amount),
        reset_authority(std::move(reset_authority)) {}
};
/// @}

using tank_attachment = TL::apply<TL::filter<tank_accessory_list, tank_attachment_filter::filter>, static_variant>;
using tap_requirement = TL::apply<TL::filter<tank_accessory_list, tap_requirement_filter::filter>, static_variant>;

using stateful_accessory_list = TL::filter<tank_accessory_list, impl::has_state_type>;
using tank_accessory_state = TL::apply<TL::transform<stateful_accessory_list, impl::get_state_type>, static_variant>;

/// A structure on a tank which allows asset to be released from that tank by a particular authority with limits and
/// requirements restricting when, why, and how much asset can be released
struct tap {
   /// The connected connection; if omitted, connect_authority must be specified
   optional<connection> connected_connection;
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
   /// What remote sources, if any, can deposit to this tank
   authorized_connections_type remote_sources;
   /// Type of asset this tank can store
   asset_id_type asset_type;

   /// Initialize from a tank_create_operation
   static tank_schematic from_create_operation(const tank_create_operation& create_op);
   /// Update from a tank_update_operation
   void update_from_operation(const tank_update_operation& update_op);
};

/// @}

} } } // namespace graphene::protocol::tnt

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(graphene::protocol::tnt::tank_schematic)

FC_REFLECT(graphene::protocol::tnt::attachment_id_type, (tank_id)(attachment_id))
FC_REFLECT(graphene::protocol::tnt::tap_id_type, (tank_id)(tap_id))
FC_REFLECT(graphene::protocol::tnt::unlimited_flow,)
FC_REFLECT(graphene::protocol::tnt::all_sources,)
FC_REFLECT(graphene::protocol::tnt::same_tank,)

FC_REFLECT(graphene::protocol::tnt::asset_flow_meter::state_type, (metered_amount))
FC_REFLECT(graphene::protocol::tnt::asset_flow_meter, (asset_type)(destination)(reset_authority))
FC_REFLECT(graphene::protocol::tnt::tap_opener, (tap_index)(release_amount)(destination)(asset_type))
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
           (connected_connection)(open_authority)(connect_authority)(requirements)(destructor_tap))
FC_REFLECT(graphene::protocol::tnt::tank_schematic,
           (taps)(tap_counter)(attachments)(attachment_counter)(remote_sources)(asset_type))

FC_REFLECT_TYPENAME(graphene::protocol::tnt::hash_preimage_requirement::hash_type)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::connection)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::authorized_connections_type)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::asset_flow_limit)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::tank_attachment)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::tap_requirement)
FC_REFLECT_TYPENAME(graphene::protocol::tnt::tank_accessory_state)
