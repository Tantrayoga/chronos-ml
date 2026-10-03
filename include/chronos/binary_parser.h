#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chronos {

// Wire layout: fixed-width, big-endian (network byte order), no padding —
// this mirrors how a typical exchange binary protocol (e.g. ITCH/OUCH-style
// fixed-field encodings) actually lays bytes out on the socket.
//   [0]      message_type : uint8_t
//   [1..8]   order_id     : uint64_t, big-endian
//   [9..16]  price        : uint64_t, big-endian (fixed-point ticks)
//   [17..20] quantity     : uint32_t, big-endian
//   [21]     side         : uint8_t
inline constexpr size_t kWireOrderSize = 1 + 8 + 8 + 4 + 1;  // 22 bytes

enum class WireMessageType : uint8_t {
    NewOrder = 1,
    CancelOrder = 2,
};

enum class WireSide : uint8_t {
    Buy = 0,
    Sell = 1,
};

// Host-side, host-endian, naturally-aligned representation of a parsed
// message — this is what the rest of the engine consumes. It is never cast
// directly over network bytes (only ever populated field-by-field by
// parse_wire_order below), so there's no reason to give up natural
// alignment for it the way the wire layout above must.
struct WireOrder {
    WireMessageType message_type = WireMessageType::NewOrder;
    uint64_t order_id = 0;
    uint64_t price = 0;
    uint32_t quantity = 0;
    WireSide side = WireSide::Buy;
};

// Parses a raw byte buffer (as it arrives off a socket — no alignment
// guarantee, big-endian multi-byte fields) into a host-endian WireOrder.
// Returns false if `buffer` is null or `length` is too short to hold a full
// message; the caller is expected to buffer partial reads and retry once
// more bytes have arrived, rather than treat a short read as an error.
//
// Every multi-byte field is extracted with std::memcpy into a local
// variable rather than by reinterpret_cast-ing `buffer` to a struct
// pointer and dereferencing through it. Two separate problems with the
// cast approach: (1) `buffer` has no alignment guarantee — it's whatever
// offset a socket read happened to land it at — and loading a uint64_t
// through a misaligned pointer is undefined behavior on strict-alignment
// ISAs; (2) reading through a pointer of a type that doesn't match the
// bytes' effective type (raw socket bytes are not a `WireOrder` object) is
// a strict-aliasing violation, which gives the optimizer license to
// miscompile the read. std::memcpy sidesteps both: the compiler recognizes
// the fixed-size, fixed-offset copy pattern at -O2/-O3 and lowers it
// directly to the same load instructions a raw cast would have issued —
// the "copy" has zero runtime cost once optimized, only the undefined
// behavior is removed.
inline bool parse_wire_order(const uint8_t* buffer, size_t length, WireOrder& out) noexcept {
    if (buffer == nullptr || length < kWireOrderSize) {
        return false;
    }

    size_t offset = 0;

    uint8_t message_type_raw = 0;
    std::memcpy(&message_type_raw, buffer + offset, sizeof(message_type_raw));
    offset += sizeof(message_type_raw);
    out.message_type = static_cast<WireMessageType>(message_type_raw);

    // __builtin_bswap{64,32} reverses byte order unconditionally; this is
    // correct specifically because every platform this engine targets
    // (ARM64 Apple Silicon, x86_64) is little-endian, so "network
    // (big-endian) -> host" always means "swap." On a big-endian host this
    // would need to become a no-op instead — not a concern for this
    // engine's deployment targets, but worth stating as the assumption it
    // is rather than leaving it implicit.
    uint64_t order_id_be = 0;
    std::memcpy(&order_id_be, buffer + offset, sizeof(order_id_be));
    offset += sizeof(order_id_be);
    out.order_id = __builtin_bswap64(order_id_be);

    uint64_t price_be = 0;
    std::memcpy(&price_be, buffer + offset, sizeof(price_be));
    offset += sizeof(price_be);
    out.price = __builtin_bswap64(price_be);

    uint32_t quantity_be = 0;
    std::memcpy(&quantity_be, buffer + offset, sizeof(quantity_be));
    offset += sizeof(quantity_be);
    out.quantity = __builtin_bswap32(quantity_be);

    uint8_t side_raw = 0;
    std::memcpy(&side_raw, buffer + offset, sizeof(side_raw));
    offset += sizeof(side_raw);
    out.side = static_cast<WireSide>(side_raw);

    return true;
}

}  // namespace chronos
