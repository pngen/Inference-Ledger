// query.hpp
// Ledger queries and deterministic aggregates.
//
// Queries are expressed as a filter and evaluated over a snapshot. Aggregates
// are computed over request accounts and always reconcile to the underlying
// entries (they sum the same quantities the account derives).
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "inference-ledger/event_kind.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/request_account.hpp"

namespace iledger {

struct LedgerQuery {
  std::optional<RequestId> request;
  std::optional<TenantId> tenant;
  std::optional<ModelId> model;
  std::optional<ModelRevisionId> model_revision;
  std::optional<AdapterId> adapter;
  std::optional<BatchId> batch;
  std::optional<AttemptId> attempt;
  std::optional<WorkerId> worker;
  std::optional<DeviceId> device;
  std::optional<EventKind> event_kind;
  std::optional<ResourceKind> resource_kind;
  std::optional<std::uint64_t> start_ns;
  std::optional<std::uint64_t> end_ns;
  std::optional<PricingPolicyId> pricing_policy;
  std::string outcome;  // "completed" | "cancelled" | "failed"
};

// Drop-in: evaluate a query against a ledger snapshot.
std::vector<LedgerEntry> query_ledger(const std::vector<LedgerEntry>& entries,
                                      const LedgerQuery& q);

// Distinct requests present in a snapshot.
std::vector<RequestId> distinct_requests(const std::vector<LedgerEntry>& entries);

// Group entries by request, preserving first-seen order.
std::vector<std::pair<RequestId, std::vector<LedgerEntry>>>
group_by_request(const std::vector<LedgerEntry>& entries);

struct Aggregate {
  std::size_t requests = 0;
  double cost_per_request = 0.0;
  double cost_per_token = 0.0;
  double gpu_seconds_per_request = 0.0;
  double gpu_seconds_per_token = 0.0;
  double transfer_bytes_per_request = 0.0;
  double kv_bytes_per_request = 0.0;
  double waste_ratio_avg = 0.0;
  double retry_overhead = 0.0;
  double reuse_rate_avg = 0.0;
  double avoided_work = 0.0;
  double total_cost = 0.0;
  double total_gpu_s = 0.0;
  double total_tokens = 0.0;
};

// Compute aggregates over a set of request accounts. The costs are provided
// per request (as a parallel vector) so the same physical aggregate can be
// re-priced without touching the ledger.
Aggregate aggregate_accounts(const std::vector<RequestAccount>& accounts);

}  // namespace iledger
