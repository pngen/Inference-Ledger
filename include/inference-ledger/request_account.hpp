// request_account.hpp
// Canonical per-request accounting: exact, replayable, provenance-tagged.
//
// RequestAccount is DERIVED from ledger entries. Every value carries a unit
// and a provenance; nothing is invented. Physical and logical consumption,
// useful work, wasted work, reused work and avoided work are kept distinct.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/unit.hpp"

namespace iledger {

// Work classification used to separate useful output from everything else.
enum class WorkKind {
  Useful,     // accepted output work (decode + accepted speculation)
  Wasted,     // rejected speculation / failed-attempt resource use
  Cancelled,  // work discarded by cancellation
  Recomputed, // work repeated because a prior result was discarded
  Avoided,    // work that reuse prevented (counterfactual, derived/estimated)
  Reused,     // work satisfied by reuse rather than recomputation
  Idle        // reserved-but-idle capacity
};

inline const char* work_kind_name(WorkKind w) noexcept {
  switch (w) {
    case WorkKind::Useful: return "useful";
    case WorkKind::Wasted: return "wasted";
    case WorkKind::Cancelled: return "cancelled";
    case WorkKind::Recomputed: return "recomputed";
    case WorkKind::Avoided: return "avoided";
    case WorkKind::Reused: return "reused";
    case WorkKind::Idle: return "idle";
  }
  return "unknown";
}

// Results of reconciling a request against its ledger entries. The boolean
// field is true only when every invariant checked during reconciliation holds.
struct ReconcileResult {
  bool ok = false;
  std::string reason;
  std::size_t entries = 0;
  std::size_t attempts = 0;
  std::size_t duplicate_ignored = 0;
  double declared_useful_work = 0.0;   // seconds of accepted execution
  double declared_wasted_work = 0.0;   // seconds of rejected/failed execution
};

struct RequestAccount {
  RequestId request{};
  TenantId tenant{};
  WorkloadId workload{};
  ModelId model{};
  ModelRevisionId model_revision{};

  // --- timing (seconds, derived) ---
  Quantity wall_latency_s{};     // Unit::Seconds
  Quantity queue_time_s{};
  Quantity execution_s{};        // GPU + CPU active
  Quantity prefill_s{};
  Quantity decode_s{};
  Quantity gpu_active_s{};
  Quantity cpu_active_s{};       // derived/unavailable if not measured
  Quantity batch_wait_s{};

  // --- transfers (bytes, derived) ---
  Quantity h2d_bytes{};
  Quantity d2h_bytes{};
  Quantity inter_node_bytes{};
  Quantity transfer_duration_s{};

  // --- KV ---
  Quantity kv_allocated{};
  Quantity kv_peak{};
  Quantity kv_reuse{};

  // --- tensors ---
  Quantity tensor_allocated{};
  Quantity tensor_peak{};
  Quantity tensor_reuse{};

  // --- residency (byte-seconds, derived) ---
  Quantity model_byte_seconds{};
  Quantity adapter_byte_seconds{};
  Quantity tensor_byte_seconds{};

  // --- reservations / allocation ---
  std::uint64_t reservations_acquired = 0;
  std::uint64_t reservations_released = 0;
  Quantity allocation_peak{};

  // --- speculation (tokens, derived) ---
  std::uint64_t spec_proposed = 0;
  std::uint64_t spec_accepted = 0;
  std::uint64_t spec_rejected = 0;
  std::uint64_t generated_tokens = 0;
  Quantity spec_wasted_work_s{};  // seconds of rejected speculative execution

  // --- retries / waste ---
  std::uint64_t retries = 0;
  Quantity failed_attempt_work{};   // seconds of failed-attempt execution
  Quantity cancelled_work{};        // seconds of cancelled execution
  Quantity recomputed_work{};       // seconds recomputed
  Quantity reuse_avoided_work{};    // seconds avoided (derived/estimated)
  Quantity reused_work{};           // seconds satisfied by reuse

  // --- cache / artifact reuse (counts, derived) ---
  std::uint64_t kernel_hits = 0;
  std::uint64_t kernel_misses = 0;
  std::uint64_t graph_hits = 0;
  std::uint64_t graph_misses = 0;
  std::uint64_t cache_reads = 0;
  std::uint64_t cache_writes = 0;

  // --- energy (joules, derived/reported/unavailable) ---
  Quantity energy_joules{};

  // --- outcome ---
  bool completed = false;
  bool cancelled = false;
  bool failed = false;
  std::string terminal_outcome;  // "completed" | "cancelled" | "failed"

  // --- provenance / metadata ---
  std::size_t attempt_count = 0;
  ReconcileResult reconcile{};
  std::string aggregate_provenance_name = "derived";  // values derived from ledger

  // Derived ratios (dimensionless).
  double waste_ratio() const {
    const double useful = execution_s.value;
    const double waste = (failed_attempt_work.value + cancelled_work.value +
                          reject_spec_work());
    const double total = useful + waste;
    return total > 0.0 ? waste / total : 0.0;
  }
  double reuse_credit() const {
    return reuse_avoided_work.value + reused_work.value;
  }
  double reuse_rate() const {
    const double useful = execution_s.value;
    const double reused = reused_work.value;
    return (useful + reused) > 0.0 ? reused / (useful + reused) : 0.0;
  }
  double spec_acceptance_efficiency() const {
    return spec_proposed > 0 ? static_cast<double>(spec_accepted) /
                                   static_cast<double>(spec_proposed)
                             : 0.0;
  }
  double reject_spec_work() const {
    return spec_wasted_work_s.value;
  }
  double cost_per_token() const;

  void set_units();
};

// Reconstruct a request's exact account from its full set of ledger entries.
// The entries must all belong to the same request (the caller filters by
// request id via a ledger query). Attempts are folded independently and their
// totals are reconciled to the request total.
RequestAccount reconcile_request(const std::vector<LedgerEntry>& entries,
                                 const RequestId& request);

}  // namespace iledger
