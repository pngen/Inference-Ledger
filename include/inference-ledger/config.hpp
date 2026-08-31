// config.hpp
// Central compile-time configuration for Inference Ledger.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
// No telemetry transmission.
#pragma once

#include <cstdint>

namespace iledger {

// Global schema version for the on-disk / wire binary encoding.
// Independent of the semantic product version in the package metadata.
inline constexpr std::uint32_t kSchemaVersion = 1u;

// Product version reported by the CLI and the CMake package.
inline constexpr std::int32_t kProductVersionMajor = 1;
inline constexpr std::int32_t kProductVersionMinor = 0;
inline constexpr std::int32_t kProductVersionPatch = 0;

// Canonical endian marker used by the deterministic binary codec.
// Every multi-byte integer is serialized big-endian regardless of host.
inline constexpr std::uint8_t kCodecEndianMarker = 0xBE;

// Fixed magic that begins every persisted ledger stream / snapshot.
inline constexpr std::uint64_t kLedgerMagic = 0x494C45444745524BuLL; // "ILEDGER"

// Number of bytes in a serialized 128-bit identity.
inline constexpr std::size_t kIdentityBytes = 16u;

// Soft cap on the number of in-memory ledger entries a Ledger holds before
// it starts to spill to the append stream. Chosen to bound memory in the
// long-running coordinator while remaining larger than any single batch.
inline constexpr std::size_t kDefaultMemoryEntryCap = 1u << 20;

// Maximum number of distinct attribution policies that may be referenced by
// a single ledger. Used to bound table sizes in derived batches.
inline constexpr std::size_t kMaxAttributionPolicies = 1024u;

}  // namespace iledger
