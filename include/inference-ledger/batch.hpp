// batch.hpp
// Batch accounting and deterministic shared-cost attribution.
//
// Shared infrastructure (batched execution, shared residency, shared kernel/
// graph artifacts, shared KV/prefix reuse, transfer coalescing, persistent
// cache) is attributed by an explicit, persisted policy. The policy is never
// silently chosen. The residual of an exact allocation is reported as
// explicitly-unallocated overhead so that component shares plus overhead
// always equal the source total with no hidden rounding drift.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/request_account.hpp"

namespace iledger {

enum class AttributionPolicy {
  EqualShare,
  ProportionalBytes,
  ProportionalTokens,
  ProportionalExecution,
  ProportionalReserved,
  DirectOwnership,
  Weighted
};

inline const char* attribution_policy_name(AttributionPolicy p) noexcept {
  switch (p) {
    case AttributionPolicy::EqualShare: return "equal_share";
    case AttributionPolicy::ProportionalBytes: return "proportional_bytes";
    case AttributionPolicy::ProportionalTokens: return "proportional_tokens";
    case AttributionPolicy::ProportionalExecution: return "proportional_execution";
    case AttributionPolicy::ProportionalReserved: return "proportional_reserved";
    case AttributionPolicy::DirectOwnership: return "direct_ownership";
    case AttributionPolicy::Weighted: return "weighted";
  }
  return "unknown";
}

struct MemberShare {
  RequestId request{};
  double share = 0.0;
  double weight = 0.0;
};

struct SharedAllocation {
  AttributionPolicy policy = AttributionPolicy::EqualShare;
  std::vector<MemberShare> members;
  double unallocated_overhead = 0.0;
  double total = 0.0;

  double member_sum() const {
    double s = 0.0;
    for (const auto& m : members) s += m.share;
    return s;
  }
  bool reconciles() const {
    const double diff = (member_sum() + unallocated_overhead) - total;
    const double scale = (total > 0.0) ? total : 1.0;
    return diff < 1e-9 * scale && diff > -1e-9 * scale;
  }
};

SharedAllocation allocate_shared(double total,
                                 const std::vector<RequestId>& members,
                                 const std::vector<double>& weights,
                                 AttributionPolicy policy,
                                 RequestId direct_owner = RequestId{});

std::vector<double> member_weights_for_policy(
    const std::vector<RequestAccount>& members, AttributionPolicy policy);

struct BatchAccount {
  BatchId batch{};
  std::vector<RequestId> members;
  std::string phase;

  double total_execution_s = 0.0;
  double total_generated_tokens = 0.0;
  double total_gpu_s = 0.0;
  std::uint64_t cancelled_members = 0;
  bool partial_completion = false;

  std::vector<RequestAccount> member_accounts;
};

BatchAccount reconcile_batch(const std::vector<LedgerEntry>& entries,
                             const BatchId& batch);

}  // namespace iledger
