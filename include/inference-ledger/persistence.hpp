// persistence.hpp
// Strict versioned persistence for the ledger.
//
// On-disk format (all big-endian, deterministic):
//   [ u64 magic  = ILEDGER ]
//   [ u32 schema_version ]
//   [ 16 bytes ledger_id ]
//   [ entry frame ]...
// where each frame is [ u32 len ][ u32 crc32 ][ payload ] (see codec.hpp).
//
// The loader rejects malformed lengths, truncation, corruption, duplicate
// entry ids, invalid enums/units/provenance, unsupported versions, NaN/Inf,
// impossible negative physical quantities and trailing garbage.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/ledger_entry.hpp"

namespace iledger {

// A stable 128-bit digest of an ordered set of ledger entries. Two identical
// entry streams always produce the same fingerprint.
struct LedgerFingerprint {
  std::array<std::uint8_t, 16> bytes{};
  bool operator==(const LedgerFingerprint& o) const noexcept {
    return bytes == o.bytes;
  }
  std::string to_string() const;
};

// Compute the canonical fingerprint over the ordered entries.
LedgerFingerprint ledger_fingerprint(const std::vector<LedgerEntry>& entries);

struct LoadResult {
  bool ok = false;
  std::string reason;
  LedgerId ledger_id{};
  std::vector<LedgerEntry> entries;
  LedgerFingerprint fingerprint{};
  std::size_t frame_count = 0;
};

class LedgerStore {
 public:
  // Snapshot the whole set of entries to a new file (replaces any existing).
  static bool save_snapshot(const std::vector<LedgerEntry>& entries,
                            const LedgerId& ledger_id, const std::string& path,
                            std::string& err);

  // Append entries to an existing stream. If the file does not exist it is
  // created with a valid header. Existing content is left untouched.
  static bool append_entries(const std::vector<LedgerEntry>& entries,
                             const std::string& path, std::string& err);

  // Load the whole file. Returns entries in original order. Rejects any file
  // with a malformed header, duplicate id, corrupt/truncated frame, invalid
  // enum or trailing garbage.
  static LoadResult load(const std::string& path);

  // Replay a file into `out`. Accepted/Duplicate entries are fine; any other
  // outcome means the file is not a valid append-only ledger for `out`.
  static bool replay_into(Ledger& out, const std::string& path,
                          std::string& err);

  // Recover: attempt to load, and if it fails, report the first rejecting
  // condition precisely. Never resurrects stale authority (it only reads).
  static LoadResult recover(const std::string& path);
};

}  // namespace iledger
