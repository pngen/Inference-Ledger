#include "inference-ledger/request_account.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace iledger {

namespace {

constexpr double kNsPerSecond = 1000000000.0;

double sec(std::uint64_t ns) {
  return static_cast<double>(ns) / kNsPerSecond;
}

double dur_sec(const LedgerEntry& e) {
  if (!e.has_end) return 0.0;
  return sec(e.duration_ns());
}

struct AttemptAttr {
  bool completed = false, failed = false, cancelled = false;
  std::uint64_t terminal_ns = 0;
  double exec_s = 0.0, prefill_s = 0.0, decode_s = 0.0, gpu_s = 0.0;
  double cpu_s = 0.0;
  double spec_rej_s = 0.0;
  std::uint64_t gen_tokens = 0;
};

}  // namespace

void RequestAccount::set_units() {
  wall_latency_s.unit = Unit::Seconds;
  queue_time_s.unit = Unit::Seconds;
  execution_s.unit = Unit::Seconds;
  prefill_s.unit = Unit::Seconds;
  decode_s.unit = Unit::Seconds;
  gpu_active_s.unit = Unit::Seconds;
  cpu_active_s.unit = Unit::Seconds;
  batch_wait_s.unit = Unit::Seconds;
  h2d_bytes.unit = Unit::Bytes;
  d2h_bytes.unit = Unit::Bytes;
  inter_node_bytes.unit = Unit::Bytes;
  transfer_duration_s.unit = Unit::Seconds;
  kv_allocated.unit = Unit::Bytes;
  kv_peak.unit = Unit::Bytes;
  kv_reuse.unit = Unit::Bytes;
  tensor_allocated.unit = Unit::Bytes;
  tensor_peak.unit = Unit::Bytes;
  tensor_reuse.unit = Unit::Bytes;
  model_byte_seconds.unit = Unit::ByteSeconds;
  adapter_byte_seconds.unit = Unit::ByteSeconds;
  tensor_byte_seconds.unit = Unit::ByteSeconds;
  allocation_peak.unit = Unit::Bytes;
  failed_attempt_work.unit = Unit::Seconds;
  cancelled_work.unit = Unit::Seconds;
  recomputed_work.unit = Unit::Seconds;
  reuse_avoided_work.unit = Unit::Seconds;
  reused_work.unit = Unit::Seconds;
  spec_wasted_work_s.unit = Unit::Seconds;
  energy_joules.unit = Unit::EnergyJoule;
}

double RequestAccount::cost_per_token() const {
  // The physical account carries no monetary cost; cost is produced by the
  // pricing layer. This returns 0.0 to keep the physical account honest.
  return 0.0;
}

RequestAccount reconcile_request(const std::vector<LedgerEntry>& entries,
                                 const RequestId& request) {
  RequestAccount acc;
  acc.request = request;
  acc.set_units();

  std::map<AttemptId, AttemptAttr> attempts;

  bool has_start = false, has_end = false;
  std::uint64_t start_ns = 0, end_ns = 0;
  double queue_s = 0.0, batch_s = 0.0;
  double h2d = 0.0, d2h = 0.0, node = 0.0, trans_dur = 0.0;
  double kv_alloc = 0.0, kv_live = 0.0, kv_peak = 0.0, kv_reuse = 0.0;
  double t_alloc = 0.0, t_live = 0.0, t_peak = 0.0, t_reuse = 0.0;
  double model_bs = 0.0, adapter_bs = 0.0;
  double recompute_s = 0.0, reuse_avoid = 0.0, reused = 0.0;
  double energy_j = 0.0;
  std::uint64_t res_acq = 0, res_rel = 0;
  std::uint64_t spec_prop = 0, spec_acc = 0, spec_rej = 0;
  std::uint64_t retries = 0;
  std::uint64_t k_hits = 0, k_misses = 0, g_hits = 0, g_misses = 0;
  std::uint64_t c_reads = 0, c_writes = 0;

  acc.tenant = entries.empty() ? TenantId{} : entries.front().tenant;
  acc.workload = entries.empty() ? WorkloadId{} : entries.front().workload;
  acc.model = entries.empty() ? ModelId{} : entries.front().model;
  acc.model_revision = entries.empty() ? ModelRevisionId{} : entries.front().model_revision;

  for (const auto& e : entries) {
    const double d = dur_sec(e);
    const double val = e.quantity.value;
    const Unit u = e.quantity.unit;
    auto& att = attempts[e.attempt];
    switch (e.event_kind) {
      case EventKind::RequestStart:
        if (e.has_end) {
          start_ns = e.end_ts_ns;
        } else {
          start_ns = e.start_ts_ns;
        }
        has_start = true;
        break;
      case EventKind::RequestEnd:
        end_ns = e.start_ts_ns;
        has_end = true;
        att.completed = true;
        att.terminal_ns = std::max(att.terminal_ns, e.start_ts_ns);
        break;
      case EventKind::Queue:
        queue_s += d;
        break;
      case EventKind::BatchWait:
        batch_s += d;
        break;
      case EventKind::Prefill:
        att.prefill_s += d;
        att.exec_s += d;
        att.gpu_s += d;
        break;
      case EventKind::Decode:
        att.decode_s += d;
        att.exec_s += d;
        att.gpu_s += d;
        if (u == Unit::Count) att.gen_tokens += static_cast<std::uint64_t>(std::llround(val));
        break;
      case EventKind::GpuExecution:
        att.exec_s += d;
        att.gpu_s += d;
        break;
      case EventKind::CpuExecution:
        att.exec_s += d;
        att.cpu_s += d;
        break;
      case EventKind::SpeculationProposed:
        spec_prop += static_cast<std::uint64_t>(std::llround(val));
        break;
      case EventKind::SpeculationAccepted:
        spec_acc += static_cast<std::uint64_t>(std::llround(val));
        att.gen_tokens += static_cast<std::uint64_t>(std::llround(val));
        break;
      case EventKind::SpeculationRejected:
        spec_rej += static_cast<std::uint64_t>(std::llround(val));
        att.spec_rej_s += d;
        break;
      case EventKind::KvAllocate:
        kv_alloc += val;
        kv_live += val;
        kv_peak = std::max(kv_peak, kv_live);
        break;
      case EventKind::KvRelease:
        kv_live = std::max(0.0, kv_live - val);
        break;
      case EventKind::KvReuse:
        kv_reuse += val;
        break;
      case EventKind::TensorAllocate:
        t_alloc += val;
        t_live += val;
        t_peak = std::max(t_peak, t_live);
        break;
      case EventKind::TensorRelease:
        t_live = std::max(0.0, t_live - val);
        break;
      case EventKind::TensorReuse:
        t_reuse += val;
        break;
      case EventKind::ModelResidency:
        model_bs += val * d;
        break;
      case EventKind::AdapterResidency:
        adapter_bs += val * d;
        break;
      case EventKind::KernelHit:
        k_hits += static_cast<std::uint64_t>(std::llround(val));
        break;
      case EventKind::KernelMiss:
        k_misses += static_cast<std::uint64_t>(std::llround(val));
        break;
      case EventKind::GraphHit:
        g_hits += static_cast<std::uint64_t>(std::llround(val));
        break;
      case EventKind::GraphMiss:
        g_misses += static_cast<std::uint64_t>(std::llround(val));
        break;
      case EventKind::TransferH2D:
        h2d += val;
        trans_dur += d;
        break;
      case EventKind::TransferD2H:
        d2h += val;
        trans_dur += d;
        break;
      case EventKind::TransferInterNode:
        node += val;
        trans_dur += d;
        break;
      case EventKind::CacheRead:
        c_reads += static_cast<std::uint64_t>(std::llround(val));
        break;
      case EventKind::CacheWrite:
        c_writes += static_cast<std::uint64_t>(std::llround(val));
        break;
      case EventKind::Reserve:
        ++res_acq;
        break;
      case EventKind::Release:
        ++res_rel;
        break;
      case EventKind::Retry:
        retries += static_cast<std::uint64_t>(std::llround(val));
        break;
      case EventKind::Failure:
        att.failed = true;
        break;
      case EventKind::Cancellation:
        att.cancelled = true;
        break;
      case EventKind::Recompute:
        recompute_s += d;
        break;
      case EventKind::ReuseAvoided:
        reuse_avoid += val;
        break;
      case EventKind::Energy:
        // Preserve physical energy value; convert Wh to Joules when needed.
        if (u == Unit::EnergyWh) energy_j += val * 3600.0;
        else energy_j += val;
        break;
      case EventKind::CostAdjustment:
        // handled by the pricing layer, not the physical account.
        break;
    }
  }

  // Determine the winning (terminal, successful) attempt.
  AttemptId winning;
  bool has_winning = false;
  std::uint64_t best_ns = 0;
  for (const auto& kvp : attempts) {
    const auto& a = kvp.second;
    if (a.completed && !a.failed && !a.cancelled) {
      if (!has_winning || a.terminal_ns >= best_ns) {
        has_winning = true;
        winning = kvp.first;
        best_ns = a.terminal_ns;
      }
    }
  }

  if (has_winning) {
    const auto& a = attempts[winning];
    acc.execution_s.value = a.exec_s;
    acc.prefill_s.value = a.prefill_s;
    acc.decode_s.value = a.decode_s;
    acc.gpu_active_s.value = a.gpu_s;
    acc.cpu_active_s.value = a.cpu_s;
    acc.generated_tokens = a.gen_tokens;
    acc.completed = true;
    acc.terminal_outcome = "completed";
  } else {
    // No successful attempt: entire request is failed or cancelled.
    bool any_cancelled = false;
    for (const auto& kvp : attempts) {
      if (kvp.second.cancelled && !kvp.second.failed) any_cancelled = true;
    }
    acc.completed = false;
    acc.cancelled = any_cancelled;
    acc.failed = !any_cancelled;
    acc.terminal_outcome = any_cancelled ? "cancelled" : "failed";
  }

  // Wasted / cancelled execution from non-winning attempts.
  double failed_work = 0.0, cancelled_work = 0.0, spec_wasted = 0.0;
  for (const auto& kvp : attempts) {
    const auto& a = kvp.second;
    if (has_winning && kvp.first == winning) continue;
    if (a.failed) failed_work += a.exec_s;
    if (a.cancelled) cancelled_work += a.exec_s;
    spec_wasted += a.spec_rej_s;
  }
  // Even the winning attempt can carry rejected-speculation waste.
  if (has_winning) spec_wasted += attempts[winning].spec_rej_s;

  acc.wall_latency_s.value = (has_start && has_end && end_ns >= start_ns)
                                 ? sec(end_ns - start_ns)
                                 : 0.0;
  acc.queue_time_s.value = queue_s;
  acc.batch_wait_s.value = batch_s;
  acc.h2d_bytes.value = h2d;
  acc.d2h_bytes.value = d2h;
  acc.inter_node_bytes.value = node;
  acc.transfer_duration_s.value = trans_dur;
  acc.kv_allocated.value = kv_alloc;
  acc.kv_peak.value = kv_peak;
  acc.kv_reuse.value = kv_reuse;
  acc.tensor_allocated.value = t_alloc;
  acc.tensor_peak.value = t_peak;
  acc.tensor_reuse.value = t_reuse;
  acc.model_byte_seconds.value = model_bs;
  acc.adapter_byte_seconds.value = adapter_bs;
  acc.tensor_byte_seconds.value = 0.0;
  acc.tensor_byte_seconds.provenance = Provenance::Unavailable;
  acc.allocation_peak.value = std::max(kv_peak, t_peak);
  acc.reservations_acquired = res_acq;
  acc.reservations_released = res_rel;
  acc.spec_proposed = spec_prop;
  acc.spec_accepted = spec_acc;
  acc.spec_rejected = spec_rej;
  acc.spec_wasted_work_s.value = spec_wasted;
  acc.retries = retries;
  acc.failed_attempt_work.value = failed_work;
  acc.cancelled_work.value = cancelled_work;
  acc.recomputed_work.value = recompute_s;
  acc.reuse_avoided_work.value = reuse_avoid;
  acc.reused_work.value = reused;
  acc.kernel_hits = k_hits;
  acc.kernel_misses = k_misses;
  acc.graph_hits = g_hits;
  acc.graph_misses = g_misses;
  acc.cache_reads = c_reads;
  acc.cache_writes = c_writes;
  acc.energy_joules.value = energy_j;

  // Provenance: derived from ledger entries, except where a measurement was
  // simply not observed on this path (CPU active, peak, tensor residency).
  const Provenance derived = Provenance::Derived;
  acc.wall_latency_s.provenance = derived;
  acc.queue_time_s.provenance = derived;
  acc.execution_s.provenance = derived;
  acc.prefill_s.provenance = derived;
  acc.decode_s.provenance = derived;
  acc.gpu_active_s.provenance = derived;
  acc.cpu_active_s.provenance = attempts.empty() ? Provenance::Unavailable : derived;
  acc.batch_wait_s.provenance = derived;
  acc.h2d_bytes.provenance = derived;
  acc.d2h_bytes.provenance = derived;
  acc.inter_node_bytes.provenance = derived;
  acc.transfer_duration_s.provenance = derived;
  acc.kv_allocated.provenance = derived;
  acc.kv_peak.provenance = derived;
  acc.kv_reuse.provenance = derived;
  acc.tensor_allocated.provenance = derived;
  acc.tensor_peak.provenance = derived;
  acc.tensor_reuse.provenance = derived;
  acc.model_byte_seconds.provenance = derived;
  acc.adapter_byte_seconds.provenance = derived;
  acc.allocation_peak.provenance = derived;
  acc.failed_attempt_work.provenance = derived;
  acc.cancelled_work.provenance = derived;
  acc.recomputed_work.provenance = derived;
  acc.reuse_avoided_work.provenance = derived;
  acc.reused_work.provenance = derived;
  acc.spec_wasted_work_s.provenance = derived;
  acc.energy_joules.provenance = energy_j > 0.0 ? derived : Provenance::Unavailable;

  acc.attempt_count = attempts.size();
  acc.reconcile.ok = true;
  acc.reconcile.reason = "reconciled " + std::to_string(entries.size()) +
                         " entries across " + std::to_string(attempts.size()) +
                         " attempt(s)";
  acc.reconcile.entries = entries.size();
  acc.reconcile.attempts = attempts.size();
  return acc;
}

}  // namespace iledger
