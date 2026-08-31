#include "inference-ledger/query.hpp"

#include <map>
#include <set>

namespace iledger {

std::vector<LedgerEntry> query_ledger(const std::vector<LedgerEntry>& entries,
                                      const LedgerQuery& q) {
  std::vector<LedgerEntry> out;
  for (const auto& e : entries) {
    if (q.request && !(e.request == *q.request)) continue;
    if (q.tenant && !(e.tenant == *q.tenant)) continue;
    if (q.model && !(e.model == *q.model)) continue;
    if (q.model_revision && !(e.model_revision == *q.model_revision)) continue;
    if (q.adapter && (!e.has_adapter || !(e.adapter == *q.adapter))) continue;
    if (q.attempt && !(e.attempt == *q.attempt)) continue;
    if (q.worker && !(e.worker == *q.worker)) continue;
    if (q.device && !(e.device == *q.device)) continue;
    if (q.event_kind && !(e.event_kind == *q.event_kind)) continue;
    if (q.resource_kind && !(e.resource_kind == *q.resource_kind)) continue;
    if (q.start_ns && (e.start_ts_ns < *q.start_ns)) continue;
    if (q.end_ns && (e.start_ts_ns > *q.end_ns)) continue;
    if (q.batch) {
      const auto it = e.metadata.find("batch");
      if (it == e.metadata.end() || it->second != q.batch->to_string()) continue;
    }
    if (!q.outcome.empty()) {
      const auto it = e.metadata.find("outcome");
      if (it == e.metadata.end() || it->second != q.outcome) continue;
    }
    out.push_back(e);
  }
  return out;
}

std::vector<RequestId> distinct_requests(const std::vector<LedgerEntry>& entries) {
  std::set<RequestId> seen;
  std::vector<RequestId> out;
  for (const auto& e : entries) {
    if (e.request.is_zero()) continue;
    if (seen.insert(e.request).second) out.push_back(e.request);
  }
  return out;
}

std::vector<std::pair<RequestId, std::vector<LedgerEntry>>>
group_by_request(const std::vector<LedgerEntry>& entries) {
  std::set<RequestId> order;
  std::map<RequestId, std::vector<LedgerEntry>> m;
  for (const auto& e : entries) {
    if (e.request.is_zero()) continue;
    order.insert(e.request);
    m[e.request].push_back(e);
  }
  std::vector<std::pair<RequestId, std::vector<LedgerEntry>>> out;
  for (const auto& req : order) out.push_back({req, m[req]});
  return out;
}

Aggregate aggregate_accounts(const std::vector<RequestAccount>& accounts) {
  Aggregate a;
  a.requests = accounts.size();
  double waste_sum = 0.0, reuse_sum = 0.0;
  for (const auto& acc : accounts) {
    a.total_gpu_s += acc.gpu_active_s.value;
    a.total_tokens += static_cast<double>(acc.generated_tokens);
    waste_sum += acc.waste_ratio();
    reuse_sum += acc.reuse_rate();
    a.transfer_bytes_per_request += acc.h2d_bytes.value + acc.d2h_bytes.value +
                                    acc.inter_node_bytes.value;
    a.kv_bytes_per_request += acc.kv_allocated.value;
    a.retry_overhead += static_cast<double>(acc.retries);
    a.avoided_work += acc.reuse_avoided_work.value + acc.reused_work.value;
  }
  const double n = accounts.empty() ? 1.0 : static_cast<double>(accounts.size());
  a.gpu_seconds_per_request = a.total_gpu_s / n;
  a.kv_bytes_per_request = a.kv_bytes_per_request / n;
  a.transfer_bytes_per_request = a.transfer_bytes_per_request / n;
  a.waste_ratio_avg = waste_sum / n;
  a.reuse_rate_avg = reuse_sum / n;
  a.retry_overhead = a.retry_overhead / n;
  a.cost_per_token = a.total_tokens > 0.0 ? a.total_cost / a.total_tokens : 0.0;
  a.gpu_seconds_per_token = a.total_tokens > 0.0 ? a.total_gpu_s / a.total_tokens : 0.0;
  return a;
}

}  // namespace iledger
