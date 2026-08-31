// identity.hpp
// Strongly typed 128-bit identities for Inference Ledger.
//
// Each named identity is a distinct type (via the Tag template parameter),
// so RequestId, TenantId, ModelId, ... can never be silently interchanged.
// Identifiers serialize deterministically (big-endian, 16 bytes) and
// round-trip exactly. Comparison, hashing and hex printing are provided.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <ostream>
#include <random>
#include <string>
#include <string_view>

#include "inference-ledger/config.hpp"

namespace iledger {

// A template tag produces a distinct Id128 type. The tag itself is never
// instantiated as a value; it only carries type identity.
template <typename Tag>
class Id128 {
 public:
  using TagType = Tag;

  // Convenience "concrete tag" namespace helpers.
  Id128() noexcept = default;
  Id128(std::uint64_t hi, std::uint64_t lo) noexcept : hi_(hi), lo_(lo) {}
  explicit Id128(std::uint64_t seed) noexcept : hi_(0), lo_(seed) {}

  // Decode a 16-byte big-endian identity (deterministic, exact round-trip).
  static Id128 from_bytes(const std::uint8_t* in) noexcept {
    Id128 out;
    out.hi_ = (static_cast<std::uint64_t>(in[0]) << 56) |
              (static_cast<std::uint64_t>(in[1]) << 48) |
              (static_cast<std::uint64_t>(in[2]) << 40) |
              (static_cast<std::uint64_t>(in[3]) << 32) |
              (static_cast<std::uint64_t>(in[4]) << 24) |
              (static_cast<std::uint64_t>(in[5]) << 16) |
              (static_cast<std::uint64_t>(in[6]) << 8) |
              static_cast<std::uint64_t>(in[7]);
    out.lo_ = (static_cast<std::uint64_t>(in[8]) << 56) |
              (static_cast<std::uint64_t>(in[9]) << 48) |
              (static_cast<std::uint64_t>(in[10]) << 40) |
              (static_cast<std::uint64_t>(in[11]) << 32) |
              (static_cast<std::uint64_t>(in[12]) << 24) |
              (static_cast<std::uint64_t>(in[13]) << 16) |
              (static_cast<std::uint64_t>(in[14]) << 8) |
              static_cast<std::uint64_t>(in[15]);
    return out;
  }

  // Encode as 16-byte big-endian identity (deterministic).
  void to_bytes(std::uint8_t* out) const noexcept {
    out[0] = static_cast<std::uint8_t>(hi_ >> 56);
    out[1] = static_cast<std::uint8_t>(hi_ >> 48);
    out[2] = static_cast<std::uint8_t>(hi_ >> 40);
    out[3] = static_cast<std::uint8_t>(hi_ >> 32);
    out[4] = static_cast<std::uint8_t>(hi_ >> 24);
    out[5] = static_cast<std::uint8_t>(hi_ >> 16);
    out[6] = static_cast<std::uint8_t>(hi_ >> 8);
    out[7] = static_cast<std::uint8_t>(hi_);
    out[8] = static_cast<std::uint8_t>(lo_ >> 56);
    out[9] = static_cast<std::uint8_t>(lo_ >> 48);
    out[10] = static_cast<std::uint8_t>(lo_ >> 40);
    out[11] = static_cast<std::uint8_t>(lo_ >> 32);
    out[12] = static_cast<std::uint8_t>(lo_ >> 24);
    out[13] = static_cast<std::uint8_t>(lo_ >> 16);
    out[14] = static_cast<std::uint8_t>(lo_ >> 8);
    out[15] = static_cast<std::uint8_t>(lo_);
  }

  // Parse a 32-hex-char (lowercase or uppercase) identity.
  // Returns std::nullopt on malformed input.
  static std::optional<Id128> parse(std::string_view hex) {
    if (hex.size() != 32u) return std::nullopt;
    Id128 out;
    for (std::size_t i = 0; i < 32u; ++i) {
      const int nib = hex_to_nibble(hex[i]);
      if (nib < 0) return std::nullopt;
      const std::size_t byte_index = i / 2u;
      const std::uint8_t shift = (i % 2u == 0u) ? 4u : 0u;
      if (byte_index < 8u) {
        out.hi_ |= (static_cast<std::uint64_t>(nib) & 0x0Fu) << (56u - byte_index * 8u + shift);
      } else {
        const std::size_t lo_idx = byte_index - 8u;
        out.lo_ |= (static_cast<std::uint64_t>(nib) & 0x0Fu) << (56u - lo_idx * 8u + shift);
      }
    }
    return out;
  }

  // Lowercase 32-hex-character string.
  std::string to_string() const {
    std::string s;
    s.reserve(32u);
    std::uint8_t buf[kIdentityBytes];
    to_bytes(buf);
    constexpr char kHex[] = "0123456789abcdef";
    for (std::uint8_t b : buf) {
      s.push_back(kHex[(b >> 4) & 0x0F]);
      s.push_back(kHex[b & 0x0F]);
    }
    return s;
  }

  bool is_zero() const noexcept { return hi_ == 0 && lo_ == 0; }

  std::uint64_t hi() const noexcept { return hi_; }
  std::uint64_t lo() const noexcept { return lo_; }

  // Canonical total order (hi then lo).
  friend constexpr bool operator==(const Id128& a, const Id128& b) noexcept {
    return a.hi_ == b.hi_ && a.lo_ == b.lo_;
  }
  friend constexpr bool operator!=(const Id128& a, const Id128& b) noexcept {
    return !(a == b);
  }
  friend constexpr std::strong_ordering operator<=>(const Id128& a,
                                                     const Id128& b) noexcept {
    if (a.hi_ < b.hi_) return std::strong_ordering::less;
    if (a.hi_ > b.hi_) return std::strong_ordering::greater;
    if (a.lo_ < b.lo_) return std::strong_ordering::less;
    if (a.lo_ > b.lo_) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
  }

  static uint64_t combine(uint64_t hi, uint64_t lo) noexcept { return hi ^ (lo * 0x9E3779B97F4A7C15ULL); }

 private:
  static int hex_to_nibble(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  std::uint64_t hi_ = 0;
  std::uint64_t lo_ = 0;
};

template <typename Tag>
std::ostream& operator<<(std::ostream& os, const Id128<Tag>& id) {
  return os << id.to_string();
}

template <typename Tag>
struct Id128Hash {
  std::size_t operator()(const Id128<Tag>& id) const noexcept {
    const std::uint64_t h = Id128<Tag>::combine(id.hi(), id.lo());
    return static_cast<std::size_t>(h);
  }
};

// ---------------------------------------------------------------------------
// Concrete identity types. Each is a distinct type.
// ---------------------------------------------------------------------------
struct RequestIdTag {};
struct TenantIdTag {};
struct WorkloadIdTag {};
struct ModelIdTag {};
struct ModelRevisionIdTag {};
struct AdapterIdTag {};
struct SequenceIdTag {};
struct BatchIdTag {};
struct AttemptIdTag {};
struct DispatchIdTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct NodeIdTag {};
struct DeviceIdTag {};
struct ReservationIdTag {};
struct AllocationIdTag {};
struct TransferIdTag {};
struct KernelArtifactIdTag {};
struct GraphArtifactIdTag {};
struct KVStateIdTag {};
struct TensorStateIdTag {};
struct LedgerEntryIdTag {};
struct LedgerIdTag {};
struct PricingPolicyIdTag {};
struct AccountingPolicyIdTag {};
struct CoordinatorEpochTag {};
struct RequestGenerationTag {};
struct AttemptGenerationTag {};
struct AccountingGenerationTag {};

using RequestId = Id128<RequestIdTag>;
using TenantId = Id128<TenantIdTag>;
using WorkloadId = Id128<WorkloadIdTag>;
using ModelId = Id128<ModelIdTag>;
using ModelRevisionId = Id128<ModelRevisionIdTag>;
using AdapterId = Id128<AdapterIdTag>;
using SequenceId = Id128<SequenceIdTag>;
using BatchId = Id128<BatchIdTag>;
using AttemptId = Id128<AttemptIdTag>;
using DispatchId = Id128<DispatchIdTag>;
using WorkerId = Id128<WorkerIdTag>;
using WorkerBootId = Id128<WorkerBootIdTag>;
using NodeId = Id128<NodeIdTag>;
using DeviceId = Id128<DeviceIdTag>;
using ReservationId = Id128<ReservationIdTag>;
using AllocationId = Id128<AllocationIdTag>;
using TransferId = Id128<TransferIdTag>;
using KernelArtifactId = Id128<KernelArtifactIdTag>;
using GraphArtifactId = Id128<GraphArtifactIdTag>;
using KVStateId = Id128<KVStateIdTag>;
using TensorStateId = Id128<TensorStateIdTag>;
using LedgerEntryId = Id128<LedgerEntryIdTag>;
using LedgerId = Id128<LedgerIdTag>;
using PricingPolicyId = Id128<PricingPolicyIdTag>;
using AccountingPolicyId = Id128<AccountingPolicyIdTag>;

// Generation values are monotonic counters wrapped in a strong type so that
// a stale generation is rejected at compile time where possible.
template <typename Tag>
struct Generation {
 public:
  using TagType = Tag;
  Generation() noexcept = default;
  explicit Generation(std::uint64_t v) noexcept : value_(v) {}
  std::uint64_t value() const noexcept { return value_; }
  bool is_zero() const noexcept { return value_ == 0; }
  constexpr bool operator==(const Generation&) const noexcept = default;
  friend constexpr std::strong_ordering operator<=>(const Generation& a,
                                                     const Generation& b) noexcept {
    return a.value_ <=> b.value_;
  }
  Generation next() const noexcept { return Generation{value_ + 1u}; }

 private:
  std::uint64_t value_ = 0;
};

using CoordinatorEpoch = Generation<CoordinatorEpochTag>;
using RequestGeneration = Generation<RequestGenerationTag>;
using AttemptGeneration = Generation<AttemptGenerationTag>;
using AccountingGeneration = Generation<AccountingGenerationTag>;

}  // namespace iledger

// std::hash specializations (defined in iledger namespace scope above via
// the template; the concrete types reuse it through the primary template).
namespace std {
template <typename Tag>
struct hash<iledger::Id128<Tag>> : iledger::Id128Hash<Tag> {};
}  // namespace std
