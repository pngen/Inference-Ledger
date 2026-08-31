// iledger_cli.cpp
// Command-line interface for Inference Ledger.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "inference-ledger/batch.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/persistence.hpp"
#include "inference-ledger/pricing.hpp"
#include "inference-ledger/query.hpp"
#include "inference-ledger/request_account.hpp"

using namespace iledger;

namespace {

std::string f2(double v) {
  std::ostringstream os;
  os.precision(6);
  os << v;
  return os.str();
}

void print_account_text(const RequestAccount& a) {
  std::cout << "request " << a.request.to_string() << "\n";
  std::cout << "  outcome      " << a.terminal_outcome << "\n";
  std::cout << "  wall_latency_s " << f2(a.wall_latency_s.value) << " [" << provenance_name(a.wall_latency_s.provenance) << "]\n";
  std::cout << "  queue_s      " << f2(a.queue_time_s.value) << "\n";
  std::cout << "  execution_s  " << f2(a.execution_s.value) << "\n";
  std::cout << "  prefill_s    " << f2(a.prefill_s.value) << "\n";
  std::cout << "  decode_s     " << f2(a.decode_s.value) << "\n";
  std::cout << "  gpu_active_s " << f2(a.gpu_active_s.value) << "\n";
  std::cout << "  cpu_active_s " << f2(a.cpu_active_s.value) << "\n";
  std::cout << "  h2d_bytes    " << f2(a.h2d_bytes.value) << "\n";
  std::cout << "  d2h_bytes    " << f2(a.d2h_bytes.value) << "\n";
  std::cout << "  inter_node_bytes " << f2(a.inter_node_bytes.value) << "\n";
  std::cout << "  kv_allocated " << f2(a.kv_allocated.value) << "\n";
  std::cout << "  kv_peak      " << f2(a.kv_peak.value) << "\n";
  std::cout << "  kv_reuse     " << f2(a.kv_reuse.value) << "\n";
  std::cout << "  tensor_allocated " << f2(a.tensor_allocated.value) << "\n";
  std::cout << "  model_byte_seconds " << f2(a.model_byte_seconds.value) << "\n";
  std::cout << "  adapter_byte_seconds " << f2(a.adapter_byte_seconds.value) << "\n";
  std::cout << "  reservations acquired/released " << a.reservations_acquired << "/" << a.reservations_released << "\n";
  std::cout << "  allocation_peak " << f2(a.allocation_peak.value) << "\n";
  std::cout << "  spec proposed/accepted/rejected " << a.spec_proposed << "/" << a.spec_accepted << "/" << a.spec_rejected << "\n";
  std::cout << "  generated_tokens " << a.generated_tokens << "\n";
  std::cout << "  retries      " << a.retries << "\n";
  std::cout << "  attempt_count " << a.attempt_count << "\n";
  std::cout << "  failed_work_s " << f2(a.failed_attempt_work.value) << "\n";
  std::cout << "  cancelled_work_s " << f2(a.cancelled_work.value) << "\n";
  std::cout << "  recompute_s  " << f2(a.recomputed_work.value) << "\n";
  std::cout << "  reuse_avoided_s " << f2(a.reuse_avoided_work.value) << "\n";
  std::cout << "  reused_s     " << f2(a.reused_work.value) << "\n";
  std::cout << "  energy_j     " << f2(a.energy_joules.value) << "\n";
  std::cout << "  kernel_hits/misses " << a.kernel_hits << "/" << a.kernel_misses << "\n";
  std::cout << "  graph_hits/misses " << a.graph_hits << "/" << a.graph_misses << "\n";
  std::cout << "  waste_ratio  " << f2(a.waste_ratio()) << "\n";
  std::cout << "  reuse_credit " << f2(a.reuse_credit()) << "\n";
}

std::string json_escape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\', o += c;
    else o += c;
  }
  return o;
}

void print_account_json(const RequestAccount& a) {
  std::cout << "{\n";
  std::cout << "  \"request\":\"" << a.request.to_string() << "\",\n";
  std::cout << "  \"outcome\":\"" << a.terminal_outcome << "\",\n";
  std::cout << "  \"wall_latency_s\":" << f2(a.wall_latency_s.value) << ",\n";
  std::cout << "  \"execution_s\":" << f2(a.execution_s.value) << ",\n";
  std::cout << "  \"prefill_s\":" << f2(a.prefill_s.value) << ",\n";
  std::cout << "  \"decode_s\":" << f2(a.decode_s.value) << ",\n";
  std::cout << "  \"gpu_active_s\":" << f2(a.gpu_active_s.value) << ",\n";
  std::cout << "  \"kv_allocated\":" << f2(a.kv_allocated.value) << ",\n";
  std::cout << "  \"kv_reuse\":" << f2(a.kv_reuse.value) << ",\n";
  std::cout << "  \"generated_tokens\":" << a.generated_tokens << ",\n";
  std::cout << "  \"retries\":" << a.retries << ",\n";
  std::cout << "  \"attempts\":" << a.attempt_count << ",\n";
  std::cout << "  \"waste_ratio\":" << f2(a.waste_ratio()) << ",\n";
  std::cout << "  \"reuse_credit\":" << f2(a.reuse_credit()) << "\n";
  std::cout << "}\n";
}

PricingPolicy load_policy(const std::string& path) {
  PricingPolicy p;
  p.currency = "usd";
  p.generation = 1;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto pos = line.find('=');
    if (pos == std::string::npos) continue;
    const std::string k = line.substr(0, pos);
    const double v = std::stod(line.substr(pos + 1));
    if (k == "gpu_per_second") p.rates.gpu_per_second = v;
    else if (k == "cpu_per_second") p.rates.cpu_per_second = v;
    else if (k == "memory_per_gib_second") p.rates.memory_per_gib_second = v;
    else if (k == "transfer_per_gib") p.rates.transfer_per_gib = v;
    else if (k == "energy_per_kwh") p.rates.energy_per_kwh = v;
    else if (k == "persistent_cache_cost") p.rates.persistent_cache_cost = v;
    else if (k == "currency") p.currency = line.substr(pos + 1);
  }
  p.id = PricingPolicyId{0xCAFE, 1};
  return p;
}

void cmd_cost(const std::vector<LedgerEntry>& snap, const std::string& reqhex,
              const PricingPolicy& policy) {
  const auto req = RequestId::parse(reqhex);
  if (!req) { std::cerr << "bad request id\n"; return; }
  RequestAccount acc = reconcile_request(query_ledger(snap, {.request = *req}), *req);
  CostResult c = apply_pricing(acc, policy);
  std::cout << "cost for request " << reqhex << " (" << c.currency << ")\n";
  std::cout << "  useful_work    " << f2(c.useful_work_cost) << "\n";
  std::cout << "  wasted_work    " << f2(c.wasted_work_cost) << "\n";
  std::cout << "  retry_cost     " << f2(c.retry_cost) << "\n";
  std::cout << "  cancelled_cost " << f2(c.cancelled_cost) << "\n";
  std::cout << "  rejected_spec  " << f2(c.rejected_spec_cost) << "\n";
  std::cout << "  residency_cost " << f2(c.residency_cost) << "\n";
  std::cout << "  transfer_cost  " << f2(c.transfer_cost) << "\n";
  std::cout << "  energy_cost    " << f2(c.energy_cost) << "\n";
  std::cout << "  reuse_credit   " << f2(c.reuse_credit) << "\n";
  std::cout << "  total          " << f2(c.total()) << "\n";
  std::cout << "  reconciles     " << (c.reconciles() ? "yes" : "no") << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: iledger <subcommand> ...\n"
              << "  list <ledger> [--csv]\n"
              << "  request <ledger> <request_id_hex> [--json]\n"
              << "  explain <ledger> <request_id_hex>\n"
              << "  attempt <ledger> <attempt_id_hex>\n"
              << "  tenant <ledger> <tenant_id_hex>\n"
              << "  model <ledger> <model_id_hex>\n"
              << "  batch <ledger> <batch_id_hex>\n"
              << "  device <ledger> <device_id_hex>\n"
              << "  resources <ledger> [--resource <kind>]\n"
              << "  cost <ledger> <request_id_hex> --policy <file>\n"
              << "  pricing <ledger> <request_id_hex> --policy <file>\n"
              << "  waste <ledger> <request_id_hex>\n"
              << "  reuse <ledger> <request_id_hex>\n"
              << "  compare <ledger> <reqA> <reqB> [--policy <file>]\n"
              << "  snapshot <ledger> <out>\n"
              << "  replay <ledger>\n"
              << "  recover <ledger>\n"
              << "  multiprocess <dir>\n"
              << "  cuda\n"
              << "  benchmark <ledger>\n";
    return 2;
  }
  const std::string cmd = argv[1];
  if (cmd == "multiprocess") {
    const std::string dir = (argc > 2) ? argv[2] : ".";
    const int rc = std::system(("iledger_multiprocess.exe " + dir).c_str());
    return rc;
  }
  if (cmd == "cuda") {
    return std::system("iledger_cuda_proof.exe");
  }
  if (cmd == "serve" || cmd == "worker") {
    std::cerr << "use the iledger_coordinator / iledger_worker executables\n";
    return 2;
  }

  if (argc < 3) { std::cerr << "missing ledger path\n"; return 2; }
  const std::string path = argv[2];
  const LoadResult lr = LedgerStore::load(path);
  if (!lr.ok) { std::cerr << "load: " << lr.reason << "\n"; return 1; }
  const auto snap = lr.entries;

  if (cmd == "list") {
    const bool csv = (argc > 3 && std::string(argv[3]) == "--csv");
    if (csv) {
      std::cout << "id,event,resource,value,unit,provenance,request,attempt,epoch,accounting_gen\n";
      for (const auto& e : snap) {
        std::cout << e.id.to_string() << "," << event_kind_name(e.event_kind)
                  << "," << resource_kind_name(e.resource_kind) << ","
                  << f2(e.quantity.value) << "," << unit_name(e.quantity.unit)
                  << "," << provenance_name(e.quantity.provenance) << ","
                  << e.request.to_string() << "," << e.attempt.to_string() << ","
                  << e.authority.epoch.value() << ","
                  << e.authority.accounting_generation.value() << "\n";
      }
      return 0;
    }
    for (const auto& e : snap) {
      std::cout << e.id.to_string() << " " << event_kind_name(e.event_kind)
                << " " << resource_kind_name(e.resource_kind) << " "
                << f2(e.quantity.value) << " " << unit_name(e.quantity.unit)
                << " req=" << e.request.to_string() << "\n";
    }
    return 0;
  }
  if (cmd == "explain") {
    if (argc < 4) { std::cerr << "need request id\n"; return 2; }
    const auto req = RequestId::parse(argv[3]);
    RequestAccount acc = reconcile_request(query_ledger(snap, {.request = *req}), *req);
    std::cout << "EXPLAIN request " << req->to_string() << "\n";
    std::cout << "  consumed:  gpu_active_s=" << f2(acc.gpu_active_s.value)
              << " kv_alloc=" << f2(acc.kv_allocated.value)
              << " transfer=" << f2(acc.h2d_bytes.value + acc.d2h_bytes.value) << "B\n";
    std::cout << "  time:      wall=" << f2(acc.wall_latency_s.value) << "s prefill="
              << f2(acc.prefill_s.value) << "s decode=" << f2(acc.decode_s.value) << "s\n";
    std::cout << "  useful:    generated_tokens=" << acc.generated_tokens
              << " accepted_spec=" << acc.spec_accepted << "\n";
    std::cout << "  wasted:    rejected_spec=" << acc.spec_rejected
              << " failed_work_s=" << f2(acc.failed_attempt_work.value)
              << " cancelled_s=" << f2(acc.cancelled_work.value) << "\n";
    std::cout << "  reused:    kv_reuse=" << f2(acc.kv_reuse.value) << "B reuse_credit="
              << f2(acc.reuse_credit()) << "\n";
    std::cout << "  avoided:   reuse_avoided_s=" << f2(acc.reuse_avoided_work.value)
              << " (derived/estimated)\n";
    std::cout << "  provenance: aggregate=derived from " << acc.reconcile.entries
              << " entries across " << acc.attempt_count << " attempt(s) via worker "
              << (snap.empty() ? std::string("?") : snap.front().worker.to_string()) << "\n";
    std::cout << "  shared-cost policy: not applied at the request level (see batch)\n";
    return 0;
  }
  if (cmd == "request") {
    if (argc < 4) { std::cerr << "need request id\n"; return 2; }
    const auto req = RequestId::parse(argv[3]);
    if (!req) { std::cerr << "bad request id\n"; return 2; }
    RequestAccount acc = reconcile_request(query_ledger(snap, {.request = *req}), *req);
    const bool json = (argc > 4 && std::string(argv[4]) == "--json");
    if (json) print_account_json(acc); else print_account_text(acc);
    return 0;
  }
  if (cmd == "resources") {
    std::map<ResourceKind, double> bykind;
    for (const auto& e : snap) {
      if (e.quantity.unit == Unit::Bytes || e.quantity.unit == Unit::Count)
        bykind[e.resource_kind] += e.quantity.value;
    }
    for (const auto& kv : bykind) {
      std::cout << resource_kind_name(kv.first) << " " << f2(kv.second) << "\n";
    }
    return 0;
  }
  if (cmd == "tenant" || cmd == "model" || cmd == "device") {
    if (argc < 4) { std::cerr << "need id\n"; return 2; }
    std::vector<LedgerEntry> es;
    if (cmd == "tenant") {
      const auto id = TenantId::parse(argv[3]);
      for (const auto& e : snap) if (e.tenant == *id) es.push_back(e);
    } else if (cmd == "model") {
      const auto id = ModelId::parse(argv[3]);
      for (const auto& e : snap) if (e.model == *id) es.push_back(e);
    } else {
      const auto id = DeviceId::parse(argv[3]);
      for (const auto& e : snap) if (e.device == *id) es.push_back(e);
    }
    std::cout << cmd << " " << argv[3] << ": " << es.size() << " entries\n";
    for (const auto& req : distinct_requests(es)) {
      RequestAccount acc = reconcile_request(query_ledger(es, {.request = req}), req);
      std::cout << "  req " << req.to_string() << " gpu_s=" << f2(acc.gpu_active_s.value)
                << " tokens=" << acc.generated_tokens << "\n";
    }
    return 0;
  }
  if (cmd == "batch") {
    if (argc < 4) { std::cerr << "need batch id\n"; return 2; }
    const auto b = BatchId::parse(argv[3]);
    BatchAccount ba = reconcile_batch(snap, *b);
    std::cout << "batch " << argv[3] << ": " << ba.members.size() << " members\n";
    std::cout << "  exec_s " << f2(ba.total_execution_s) << " tokens "
              << f2(ba.total_generated_tokens) << " partial " << ba.partial_completion << "\n";
    return 0;
  }
  if (cmd == "cost" || cmd == "pricing") {
    if (argc < 6 || std::string(argv[4]) != "--policy") { std::cerr << "need --policy <file>\n"; return 2; }
    PricingPolicy p = load_policy(argv[5]);
    cmd_cost(snap, argv[3], p);
    return 0;
  }
  if (cmd == "waste" || cmd == "reuse") {
    if (argc < 4) { std::cerr << "need request id\n"; return 2; }
    const auto req = RequestId::parse(argv[3]);
    RequestAccount acc = reconcile_request(query_ledger(snap, {.request = *req}), *req);
    if (cmd == "waste") {
      std::cout << "waste for " << argv[3] << "\n";
      std::cout << "  failed_work_s     " << f2(acc.failed_attempt_work.value) << "\n";
      std::cout << "  cancelled_work_s  " << f2(acc.cancelled_work.value) << "\n";
      std::cout << "  spec_rejected     " << acc.spec_rejected << "\n";
      std::cout << "  spec_wasted_s     " << f2(acc.spec_wasted_work_s.value) << "\n";
      std::cout << "  waste_ratio       " << f2(acc.waste_ratio()) << "\n";
    } else {
      std::cout << "reuse for " << argv[3] << "\n";
      std::cout << "  kv_reuse_bytes    " << f2(acc.kv_reuse.value) << "\n";
      std::cout << "  tensor_reuse      " << f2(acc.tensor_reuse.value) << "\n";
      std::cout << "  reuse_avoided_s   " << f2(acc.reuse_avoided_work.value) << "\n";
      std::cout << "  reused_s          " << f2(acc.reused_work.value) << "\n";
      std::cout << "  reuse_credit      " << f2(acc.reuse_credit()) << "\n";
      std::cout << "  reuse_rate        " << f2(acc.reuse_rate()) << "\n";
    }
    return 0;
  }
  if (cmd == "compare") {
    if (argc < 5) { std::cerr << "need reqA reqB\n"; return 2; }
    const auto ra = RequestId::parse(argv[3]);
    const auto rb = RequestId::parse(argv[4]);
    RequestAccount a = reconcile_request(query_ledger(snap, {.request = *ra}), *ra);
    RequestAccount b = reconcile_request(query_ledger(snap, {.request = *rb}), *rb);
    std::cout << "compare " << argv[3] << " vs " << argv[4] << "\n";
    std::cout << "  gpu_s        " << f2(a.gpu_active_s.value) << " vs " << f2(b.gpu_active_s.value) << "\n";
    std::cout << "  tokens       " << a.generated_tokens << " vs " << b.generated_tokens << "\n";
    std::cout << "  kv_allocated " << f2(a.kv_allocated.value) << " vs " << f2(b.kv_allocated.value) << "\n";
    std::cout << "  kv_reuse     " << f2(a.kv_reuse.value) << " vs " << f2(b.kv_reuse.value) << "\n";
    std::cout << "  retries      " << a.retries << " vs " << b.retries << "\n";
    std::cout << "  waste_ratio  " << f2(a.waste_ratio()) << " vs " << f2(b.waste_ratio()) << "\n";
    std::cout << "  reuse_credit " << f2(a.reuse_credit()) << " vs " << f2(b.reuse_credit()) << "\n";
    return 0;
  }
  if (cmd == "snapshot" || cmd == "save") {
    if (argc < 4) { std::cerr << "need output path\n"; return 2; }
    std::string err;
    if (LedgerStore::save_snapshot(snap, lr.ledger_id, argv[3], err))
      std::cout << "saved " << snap.size() << " entries to " << argv[3] << "\n";
    else { std::cerr << "save: " << err << "\n"; return 1; }
    return 0;
  }
  if (cmd == "replay") {
    Ledger fresh(LedgerId{0x494C4544474552ULL, 0x1});
    std::string err;
    if (LedgerStore::replay_into(fresh, path, err))
      std::cout << "replayed " << fresh.size() << " entries; fingerprint "
                << ledger_fingerprint(fresh.snapshot()).to_string() << "\n";
    else { std::cerr << "replay: " << err << "\n"; return 1; }
    return 0;
  }
  if (cmd == "recover") {
    LoadResult rec = LedgerStore::recover(path);
    std::cout << "recover: " << (rec.ok ? "ok" : rec.reason) << "\n";
    return rec.ok ? 0 : 1;
  }
  if (cmd == "benchmark") {
    return std::system(("iledger_benchmark.exe " + path).c_str());
  }
  std::cerr << "unknown command " << cmd << "\n";
  return 2;
}
