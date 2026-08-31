// iledger_benchmark.cpp
// Measures actual completed work for the core accounting operations.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include <chrono>
#include <cstdio>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "inference-ledger/batch.hpp"
#include "inference-ledger/codec.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/persistence.hpp"
#include "inference-ledger/pricing.hpp"
#include "inference-ledger/query.hpp"
#include "inference-ledger/request_account.hpp"
#include "support.hpp"

using namespace iledger;
using namespace iledger::test;

namespace {
double ms_since(std::chrono::steady_clock::time_point& start) {
  const auto now = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(now - start).count();
}
}  // namespace

int main(int argc, char** argv) {
  const std::string path = (argc > 1) ? argv[1] : "";
  std::vector<LedgerEntry> entries;
  const LedgerId LID{0x494C4544474552ULL, 1};
  const TenantId TEN{0x99, 1};
  const WorkerId W{0xAA, 1};
  const WorkerBootId BOOT{0xB007, 1};
  const CoordinatorEpoch EP(1);
  const AccountingGeneration AG(1);
  const RequestGeneration RG(1);
  const AttemptGeneration ATG(1);
  const DispatchId DISP{0x3001, 1};

  if (!path.empty()) {
    LoadResult lr = LedgerStore::load(path);
    if (!lr.ok) { std::cerr << "load: " << lr.reason << "\n"; return 1; }
    entries = lr.entries;
    std::cout << "using loaded ledger '" << path << "' with " << entries.size() << " entries\n";
  } else {
    std::mt19937_64 rng(0xFEED);
    const int kRequests = 2000;
    const int perReq = 50;
    std::uint64_t seq = 0;
    for (int r = 0; r < kRequests; ++r) {
      const RequestId req{static_cast<std::uint64_t>(r + 1), 1};
      const AttemptId att{static_cast<std::uint64_t>(r + 1), 1};
      for (int i = 0; i < perReq; ++i) {
        LedgerEntry e = make(LedgerEntryId{++seq, 1}, req, att,
                             EventKind(i % 2 ? EventKind::GpuExecution : EventKind::Decode),
                             ResourceKind::Compute,
                             (i % 2) ? 0.01 : 8.0,
                             (i % 2) ? Unit::Seconds : Unit::Count,
                             Provenance::Measured, true, 1000 + i * 10, 1200 + i * 10,
                             LID, TEN, W, BOOT, EP, RG, ATG, AG, DISP);
        e.metadata["batch"] = (r % 10 == 0)
            ? (BatchId{0xB, 1}).to_string()
            : (BatchId{0xA, 1}).to_string();
        entries.push_back(e);
      }
    }
    std::cout << "synthetic dataset: " << entries.size() << " entries, "
              << kRequests << " requests (" << perReq << " each)\n";
  }

  // ---- append throughput ----
  Ledger ledger(LID);
  auto t = std::chrono::steady_clock::now();
  for (const auto& e : entries) ledger.append(e);
  const double append_ms = ms_since(t);
  std::cout << "append: " << entries.size() << " in " << append_ms << " ms ("
            << (entries.size() / (append_ms / 1000.0)) << " entries/s)\n";

  // ---- indexed lookup ----
  const auto snap = ledger.snapshot();
  t = std::chrono::steady_clock::now();
  std::uint64_t found = 0;
  for (const auto& e : entries) {
    const auto got = ledger.find(e.id);
    if (got) ++found;
  }
  const double lookup_ms = ms_since(t);
  std::cout << "indexed lookup: " << found << "/" << entries.size() << " in "
            << lookup_ms << " ms\n";

  // ---- request-account reconstruction ----
  t = std::chrono::steady_clock::now();
  std::size_t recon = 0;
  for (const auto& req : distinct_requests(snap)) {
    RequestAccount acc = reconcile_request(query_ledger(snap, {.request = req}), req);
    recon += acc.reconcile.entries;
  }
  const double recon_ms = ms_since(t);
  std::cout << "request reconstruction: " << recon << " entries accounted in "
            << recon_ms << " ms\n";

  // ---- aggregate ----
  t = std::chrono::steady_clock::now();
  std::vector<RequestAccount> accounts;
  for (const auto& req : distinct_requests(snap))
    accounts.push_back(reconcile_request(query_ledger(snap, {.request = req}), req));
  Aggregate agg = aggregate_accounts(accounts);
  const double agg_ms = ms_since(t);
  std::cout << "aggregate: " << accounts.size() << " accounts in " << agg_ms
            << " ms; cost/token=" << agg.cost_per_token << " gpu_s/token="
            << agg.gpu_seconds_per_token << "\n";

  // ---- batch reconciliation ----
  t = std::chrono::steady_clock::now();
  BatchAccount ba = reconcile_batch(snap, BatchId{0xB, 1});
  const double batch_ms = ms_since(t);
  std::cout << "batch reconcile (batch_id b1): " << ba.members.size() << " members in "
            << batch_ms << " ms\n";

  // ---- binary serialization ----
  t = std::chrono::steady_clock::now();
  std::size_t bytes = 0;
  for (const auto& e : entries) {
    std::vector<std::uint8_t> fr;
    if (encode_entry(e, fr)) bytes += fr.size();
  }
  const double enc_ms = ms_since(t);
  std::cout << "binary encode: " << entries.size() << " entries -> " << (bytes >> 10)
            << " KiB in " << enc_ms << " ms\n";
  t = std::chrono::steady_clock::now();
  // decode the first 100k
  std::size_t decoded = 0;
  for (const auto& e : entries) {
    std::vector<std::uint8_t> fr;
    if (encode_entry(e, fr)) {
      LedgerEntry out; std::size_t c = 0;
      if (decode_entry(fr.data(), fr.size(), out, c)) ++decoded;
    }
  }
  const double dec_ms = ms_since(t);
  std::cout << "binary encode+decode: " << decoded << " round-trips in " << dec_ms << " ms\n";

  // ---- persistence save/reload ----
  const std::string tmp = "bench_ledger.db";
  std::string err;
  t = std::chrono::steady_clock::now();
  LedgerStore::save_snapshot(snap, LID, tmp, err);
  const double save_ms = ms_since(t);
  t = std::chrono::steady_clock::now();
  LoadResult rl = LedgerStore::load(tmp);
  const double load_ms = ms_since(t);
  std::cout << "persistence save: " << save_ms << " ms; reload: " << load_ms
            << " ms; ok=" << rl.ok << "\n";
  std::remove(tmp.c_str());

  // ---- pricing recomputation ----
  PricingPolicy p;
  p.id = PricingPolicyId{0xCAFE, 1}; p.generation = 1; p.currency = "usd";
  p.rates.gpu_per_second = 1.0; p.rates.transfer_per_gib = 0.1;
  t = std::chrono::steady_clock::now();
  double cost = 0.0;
  for (const auto& a : accounts) { CostResult c = apply_pricing(a, p); cost += c.total(); }
  const double price_ms = ms_since(t);
  std::cout << "pricing recompute: " << accounts.size() << " in " << price_ms
            << " ms; total=" << cost << "\n";

  // ---- replay ----
  t = std::chrono::steady_clock::now();
  // re-run append as a replay measure
  Ledger fresh(LID);
  for (const auto& e : snap) fresh.append(e);
  const double replay_ms = ms_since(t);
  std::cout << "replay (fresh append): " << fresh.size() << " in " << replay_ms << " ms\n";

  // ---- concurrent ingestion (8 threads over the same set) ----
  t = std::chrono::steady_clock::now();
  std::vector<std::thread> threads;
  Ledger conc(LID);
  for (int th = 0; th < 8; ++th) {
    threads.emplace_back([&conc, &entries, th] {
      for (std::size_t i = th; i < entries.size(); i += 8) conc.append(entries[i]);
    });
  }
  for (auto& th : threads) th.join();
  const double conc_ms = ms_since(t);
  std::cout << "concurrent ingestion (8 threads): " << conc.size() << " in "
            << conc_ms << " ms\n";

  std::cout << "BENCHMARK_DONE\n";
  return 0;
}
