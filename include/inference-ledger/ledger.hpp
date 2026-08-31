// ledger.hpp
// Append-oriented ledger of immutable accounting records.
//
// Properties guaranteed by this class:
//   * entries are immutable (append-only; completed entries never change);
//   * the same LedgerEntryId is never stored twice (idempotent duplicates are
//     ignored, conflicting duplicates are rejected);
//   * when an authority state is attached, a stale-authority event is
//     rejected before it can mutate current totals;
//   * reads are isolated from concurrent appends via snapshots, which also
//     removes any iterator-invalidation hazard.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "inference-ledger/authority_state.hpp"
#include "inference-ledger/config.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger_entry.hpp"

namespace iledger {

enum class AppendStatus {
  Accepted,           // stored
  Duplicate,          // identical id -> idempotent, ignored, already counted
  DuplicateConflict,  // same id, different content -> rejected
  Stale,              // stale authority -> rejected
  Invalid             // malformed content (non-finite / negative physical)
};

inline const char* append_status_name(AppendStatus s) noexcept {
  switch (s) {
    case AppendStatus::Accepted: return "accepted";
    case AppendStatus::Duplicate: return "duplicate";
    case AppendStatus::DuplicateConflict: return "duplicate_conflict";
    case AppendStatus::Stale: return "stale";
    case AppendStatus::Invalid: return "invalid";
  }
  return "unknown";
}

struct AppendResult {
  AppendStatus status = AppendStatus::Invalid;
  const char* reason = "";
  bool stored() const noexcept { return status == AppendStatus::Accepted; }
};

class Ledger {
 public:
  Ledger() = default;
  explicit Ledger(LedgerId id) : id_(std::move(id)) {}
  Ledger(const Ledger&) = delete;
  Ledger& operator=(const Ledger&) = delete;

  const LedgerId& id() const noexcept { return id_; }

  // Attach/detach the coordinator's authority snapshot used for fencing.
  void attach_authority(const AuthorityState* state) noexcept { authority_ = state; }
  void detach_authority() noexcept { authority_ = nullptr; }

  // Append an entry. Returns a status describing what happened.
  AppendResult append(const LedgerEntry& e) {
    std::lock_guard<std::mutex> lk(m_);
    if (!content_is_valid(e)) return {AppendStatus::Invalid, "invalid content"};
    if (authority_ != nullptr) {
      const AuthorityVerdict v = validate_authority(e, *authority_);
      if (v != AuthorityVerdict::Accept) {
        return {AppendStatus::Stale, authority_verdict_name(v)};
      }
    }
    const auto it = index_.find(e.id);
    if (it != index_.end()) {
      if (entries_[it->second] == e) {
        return {AppendStatus::Duplicate, "idempotent duplicate ignored"};
      }
      return {AppendStatus::DuplicateConflict,
              "same id with differing content rejected"};
    }
    index_.emplace(e.id, entries_.size());
    entries_.push_back(e);
    return {AppendStatus::Accepted, "accepted"};
  }

  // A thread-safe copy of the current entries (snapshot). Queries never hold
  // the mutation lock, so snapshots can be safely taken during appends.
  std::vector<LedgerEntry> snapshot() const {
    std::lock_guard<std::mutex> lk(m_);
    return std::vector<LedgerEntry>(entries_.begin(), entries_.end());
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lk(m_);
    return entries_.size();
  }

  std::optional<LedgerEntry> find(const LedgerEntryId& id) const {
    std::lock_guard<std::mutex> lk(m_);
    const auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;
    return entries_[it->second];
  }

  const AuthorityState* authority() const noexcept { return authority_; }

 private:
  static bool content_is_valid(const LedgerEntry& e) {
    if (!e.quantity.is_finite()) return false;
    // Physical resource quantities must be non-negative; only explicit cost
    // adjustments may carry a signed value.
    bool physical = (e.resource_kind == ResourceKind::Compute ||
                     e.resource_kind == ResourceKind::Memory ||
                     e.resource_kind == ResourceKind::Kv ||
                     e.resource_kind == ResourceKind::Tensor ||
                     e.resource_kind == ResourceKind::Transfer ||
                     e.resource_kind == ResourceKind::Residency ||
                     e.resource_kind == ResourceKind::Cache ||
                     e.resource_kind == ResourceKind::Energy);
    if (physical && e.quantity.value < 0.0) return false;
    if (e.quantity.unit == Unit::Count && !e.quantity.is_integral()) return false;
    return true;
  }

  LedgerId id_{};
  mutable std::mutex m_;
  std::deque<LedgerEntry> entries_;  // stable references; append-only
  std::unordered_map<LedgerEntryId, std::size_t, Id128Hash<LedgerEntryIdTag>> index_;
  const AuthorityState* authority_ = nullptr;
};

}  // namespace iledger
