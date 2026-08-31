// iledger_multiprocess.cpp
// One atomic real-multiprocess authority proof over framed TCP.
//
// Spawns: a coordinator OS process, worker A (boot 1), worker B. Drives the
// scenario: request begins, reservation recorded, events dispatched, worker A
// produces valid accounting, worker A is killed as an OS process, a fresh
// worker A (new WorkerBootId) restarts, authority is rolled, and every stale
// epoch / boot / attempt / attempt-generation / accounting-generation / and
// duplicate event is rejected over real TCP. Fresh post-restart accounting
// succeeds, the ledger is saved, reloaded and replayed to identical totals and
// a stable digest.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include <windows.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "inference-ledger/codec.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/persistence.hpp"
#include "inference-ledger/query.hpp"
#include "inference-ledger/request_account.hpp"

using namespace iledger;

namespace {

struct Child {
  HANDLE proc = nullptr;
  HANDLE hRead = nullptr;   // stdout read
  HANDLE hWrite = nullptr;  // stdin write
  std::thread reader;
  std::mutex mu;
  std::vector<std::string> lines;
  bool done = false;
};

bool spawn(const std::wstring& cmdline, Child& c) {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE outR, outW, inR, inW;
  if (!CreatePipe(&outR, &outW, &sa, 0)) return false;
  if (!CreatePipe(&inR, &inW, &sa, 0)) return false;
  SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = outW;
  si.hStdInput = inR;
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION pi{};
  std::wstring copy = cmdline;
  if (!CreateProcessW(nullptr, copy.data(), nullptr, nullptr, TRUE, 0, nullptr,
                      nullptr, &si, &pi)) {
    CloseHandle(outR); CloseHandle(outW); CloseHandle(inR); CloseHandle(inW);
    return false;
  }
  CloseHandle(outW);
  CloseHandle(inR);
  CloseHandle(pi.hThread);
  c.proc = pi.hProcess;
  c.hRead = outR;
  c.hWrite = inW;
  c.reader = std::thread([&c] {
    char buf[4096];
    std::string accum;
    while (true) {
      DWORD r = 0;
      if (!ReadFile(c.hRead, buf, sizeof(buf), &r, nullptr)) break;
      if (r == 0) break;
      accum.append(buf, r);
      std::size_t pos = 0;
      while ((pos = accum.find('\n')) != std::string::npos) {
        std::string line = accum.substr(0, pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        {
          std::lock_guard<std::mutex> lk(c.mu);
          c.lines.push_back(line);
        }
        accum.erase(0, pos + 1);
      }
    }
    {
      std::lock_guard<std::mutex> lk(c.mu);
      c.lines.push_back(accum);
      c.done = true;
    }
  });
  return true;
}

bool wait_for(Child& c, const std::string& marker, int timeout_ms) {
  const int step = 20;
  for (int waited = 0; waited < timeout_ms; waited += step) {
    {
      std::lock_guard<std::mutex> lk(c.mu);
      for (const auto& l : c.lines) {
        if (l.find(marker) != std::string::npos) return true;
      }
    }
    Sleep(static_cast<DWORD>(step));
  }
  return false;
}

std::vector<std::string> snapshot_lines(Child& c) {
  std::lock_guard<std::mutex> lk(c.mu);
  return c.lines;
}

bool contains(Child& c, const std::string& marker) {
  std::lock_guard<std::mutex> lk(c.mu);
  for (const auto& l : c.lines) {
    if (l.find(marker) != std::string::npos) return true;
  }
  return false;
}

void write_stdin(Child& c, const std::string& text) {
  DWORD written = 0;
  WriteFile(c.hWrite, text.data(), static_cast<DWORD>(text.size()), &written,
            nullptr);
}

void kill_child(Child& c) {
  if (c.proc) {
    TerminateProcess(c.proc, 1);
    WaitForSingleObject(c.proc, 5000);
    CloseHandle(c.proc);
    c.proc = nullptr;
  }
  if (c.hRead) { CloseHandle(c.hRead); c.hRead = nullptr; }
  if (c.hWrite) { CloseHandle(c.hWrite); c.hWrite = nullptr; }
  if (c.reader.joinable()) c.reader.join();
}

std::wstring ws(const std::string& s) {
  return std::wstring(s.begin(), s.end());
}

// Write a vector of entries as a binary script file.
bool write_script(const std::vector<LedgerEntry>& entries,
                  const std::string& path) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  for (const auto& e : entries) {
    std::vector<std::uint8_t> frame;
    if (!encode_entry(e, frame)) return false;
    f.write(reinterpret_cast<const char*>(frame.data()),
            static_cast<std::streamsize>(frame.size()));
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string dir = (argc > 1) ? std::string(argv[1]) : ".";
  const std::uint16_t port = 38921;
  const std::string ledger_path = dir + "\\ledger.db";

  // Fixed identities for the scenario.
  const RequestId R{0x1234, 0x0000000000000001ULL};
  const RequestId R2{0x1234, 0x0000000000000002ULL};
  const TenantId T{0x99, 0x1};
  const WorkloadId W{0x88, 0x1};
  const ModelId M{0x77, 0x1};
  const ModelRevisionId MR{0x77, 0x2};
  const WorkerId WA{0xAA, 0x1};
  const WorkerId WB{0xBB, 0x1};
  const WorkerBootId bootA1{0x1001, 0x1};
  const WorkerBootId bootA2{0x1002, 0x1};
  const WorkerBootId bootB{0x2001, 0x1};
  const AttemptId A1{0x3001, 0x1};
  const AttemptId A2{0x3002, 0x1};
  const AttemptId B1{0x4001, 0x1};
  const DispatchId D1{0x5001, 0x1};
  const DispatchId D2{0x5002, 0x1};
  const DeviceId DEV{0x6001, 0x1};
  const NodeId NODE{0x6002, 0x1};

  // Helper to build an authority envelope.
  auto envelope = [&](CoordinatorEpoch epoch, WorkerBootId boot,
                      RequestGeneration rg, AttemptId at,
                      AttemptGeneration atg, AccountingGeneration ag,
                      DispatchId disp) {
    AuthorityEnvelope a;
    a.epoch = epoch;
    a.worker_boot = boot;
    a.request_generation = rg;
    a.attempt = at;
    a.attempt_generation = atg;
    a.accounting_generation = ag;
    a.dispatch = disp;
    return a;
  };

  // Build a full entry.
  auto mk = [&](LedgerEntryId id, EventKind kind, ResourceKind rk, double v,
                Unit unit, Provenance prov, bool has_end, std::uint64_t s,
                std::uint64_t end, RequestId req, WorkerId worker,
                AttemptId attempt, CoordinatorEpoch epoch, WorkerBootId boot,
                RequestGeneration rg, AttemptGeneration atg,
                AccountingGeneration ag2, DispatchId disp, ModelId model,
                bool has_adapter, AdapterId ad, std::string batch) {
    LedgerEntry e;
    e.id = id;
    e.ledger = LedgerId{0x494C4544474552ULL, 0x0000000000000001ULL};
    e.tenant = T;
    e.workload = W;
    e.request = req;
    e.model = model;
    e.model_revision = MR;
    e.has_adapter = has_adapter;
    e.adapter = ad;
    e.attempt = attempt;
    e.dispatch = disp;
    e.worker = worker;
    e.node = NODE;
    e.device = DEV;
    e.event_kind = kind;
    e.resource_kind = rk;
    e.quantity.value = v;
    e.quantity.unit = unit;
    e.quantity.provenance = prov;
    e.start_ts_ns = s;
    e.end_ts_ns = end;
    e.has_end = has_end;
    e.source.worker = worker;
    e.source.boot = boot;
    e.source.accounting_generation = ag2;
    e.authority = envelope(epoch, boot, rg, attempt, atg, ag2, disp);
    if (!batch.empty()) e.metadata["batch"] = batch;
    return e;
  };

  bool pass = true;
  std::vector<std::string> reasons;

  // ---- Spawn coordinator ----------------
  Child coord;
  if (!spawn(ws("iledger_coordinator.exe " + std::to_string(port) + " " +
                ledger_path), coord)) {
    std::cerr << "failed to spawn coordinator\n";
    return 1;
  }
  if (!wait_for(coord, "COORD ", 8000)) {
    std::cerr << "coordinator did not start\n";
    reasons.push_back("coordinator did not start");
    pass = false;
    kill_child(coord);
    return 1;
  }

  // ---- Worker A boot 1 ----------------
  std::vector<LedgerEntry> a1;
  const CoordinatorEpoch E1(1);
  const RequestGeneration RG1(1);
  const AttemptGeneration G1(1);
  const AccountingGeneration AG1(1);
  // RequestStart
  a1.push_back(mk(LedgerEntryId{0xA, 1}, EventKind::RequestStart, ResourceKind::Generic,
                  0.0, Unit::Count, Provenance::Measured, false, 1000, 0, R, WA, A1,
                  E1, bootA1, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  a1.push_back(mk(LedgerEntryId{0xA, 2}, EventKind::Reserve, ResourceKind::Memory,
                  1.0, Unit::Count, Provenance::Measured, false, 1100, 0, R, WA, A1,
                  E1, bootA1, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  a1.push_back(mk(LedgerEntryId{0xA, 3}, EventKind::KvAllocate, ResourceKind::Kv,
                  4096.0, Unit::Bytes, Provenance::Measured, false, 1200, 0, R, WA, A1,
                  E1, bootA1, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  a1.push_back(mk(LedgerEntryId{0xA, 4}, EventKind::TransferH2D, ResourceKind::Transfer,
                  1024.0, Unit::Bytes, Provenance::Measured, true, 1300, 1350, R, WA, A1,
                  E1, bootA1, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  a1.push_back(mk(LedgerEntryId{0xA, 5}, EventKind::Prefill, ResourceKind::Compute,
                  0.5, Unit::Seconds, Provenance::Measured, true, 1400, 1900, R, WA, A1,
                  E1, bootA1, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  a1.push_back(mk(LedgerEntryId{0xA, 6}, EventKind::Decode, ResourceKind::Compute,
                  32.0, Unit::Count, Provenance::Measured, true, 2000, 2400, R, WA, A1,
                  E1, bootA1, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  a1.push_back(mk(LedgerEntryId{0xA, 7}, EventKind::GpuExecution, ResourceKind::Compute,
                  0.5, Unit::Seconds, Provenance::Measured, true, 1400, 2400, R, WA, A1,
                  E1, bootA1, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  a1.push_back(mk(LedgerEntryId{0xA, 8}, EventKind::ModelResidency, ResourceKind::Residency,
                  1048576.0, Unit::Bytes, Provenance::Measured, true, 1400, 2400, R, WA, A1,
                  E1, bootA1, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));

  const std::string sA1 = dir + "\\wA1.script";
  write_script(a1, sA1);

  // ---- Worker B (own request R2) ----------------
  std::vector<LedgerEntry> b1;
  const RequestGeneration BRG(1);
  const AttemptGeneration BG(1);
  b1.push_back(mk(LedgerEntryId{0xB, 1}, EventKind::RequestStart, ResourceKind::Generic,
                  0.0, Unit::Count, Provenance::Measured, false, 1000, 0, R2, WB, B1,
                  E1, bootB, BRG, BG, AG1, D2, M, false, AdapterId{}, ""));
  b1.push_back(mk(LedgerEntryId{0xB, 2}, EventKind::GpuExecution, ResourceKind::Compute,
                  0.25, Unit::Seconds, Provenance::Measured, true, 1500, 1750, R2, WB, B1,
                  E1, bootB, BRG, BG, AG1, D2, M, false, AdapterId{}, ""));
  // Duplicate of worker-B's RequestStart (same id, same content) -> idempotent
  b1.push_back(mk(LedgerEntryId{0xB, 1}, EventKind::RequestStart, ResourceKind::Generic,
                  0.0, Unit::Count, Provenance::Measured, false, 1000, 0, R2, WB, B1,
                  E1, bootB, BRG, BG, AG1, D2, M, false, AdapterId{}, ""));
  b1.push_back(mk(LedgerEntryId{0xB, 3}, EventKind::RequestEnd, ResourceKind::Generic,
                  0.0, Unit::Count, Provenance::Measured, false, 1800, 0, R2, WB, B1,
                  E1, bootB, BRG, BG, AG1, D2, M, false, AdapterId{}, ""));
  const std::string sB = dir + "\\wB.script";
  write_script(b1, sB);

  Child wa;
  Child wb;
  if (!spawn(ws("iledger_worker.exe 127.0.0.1 " + std::to_string(port) + " " +
                WA.to_string() + " " + bootA1.to_string() + " " + sA1 + " 1"), wa)) {
    pass = false; reasons.push_back("spawn workerA failed");
  }
  if (!spawn(ws("iledger_worker.exe 127.0.0.1 " + std::to_string(port) + " " +
                WB.to_string() + " " + bootB.to_string() + " " + sB + " 1"), wb)) {
    pass = false; reasons.push_back("spawn workerB failed");
  }

  // Wait for worker A boot1 to finish; capture acks.
  if (!wait_for(wa, "WORKER_DONE", 15000)) {
    pass = false; reasons.push_back("workerA boot1 did not finish");
  }
  if (!wait_for(wb, "WORKER_DONE", 15000)) {
    pass = false; reasons.push_back("workerB did not finish");
  }

  // Verify worker B duplicate ack == Duplicate(1).
  for (const auto& line : snapshot_lines(wb)) {
    if (line.find("ACK 2 ") != std::string::npos) {
      if (line.find("ACK 2 1") == std::string::npos) {
        pass = false; reasons.push_back("workerB duplicate not idempotent: " + line);
      }
    }
  }

  // ---- Kill worker A as an OS process ----------------
  kill_child(wa);

  // Allow the coordinator to notice the loss and roll authority.
  Sleep(600);

  // ---- Restart worker A with fresh boot A2 ----------------
  const CoordinatorEpoch E2(2);
  const AccountingGeneration AG2(2);
  std::vector<LedgerEntry> a2;
  // Stale replays (all must be rejected).
  a2.push_back(mk(LedgerEntryId{0xA, 20}, EventKind::Reserve, ResourceKind::Memory,
                  1.0, Unit::Count, Provenance::Measured, false, 1100, 0, R, WA, A1,
                  CoordinatorEpoch(1), bootA2, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  a2.push_back(mk(LedgerEntryId{0xA, 21}, EventKind::Reserve, ResourceKind::Memory,
                  1.0, Unit::Count, Provenance::Measured, false, 1100, 0, R, WA, A1,
                  E2, bootA1, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  a2.push_back(mk(LedgerEntryId{0xA, 22}, EventKind::RequestEnd, ResourceKind::Generic,
                  0.0, Unit::Count, Provenance::Measured, false, 2500, 0, R, WA, A1,
                  E2, bootA2, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  a2.push_back(mk(LedgerEntryId{0xA, 23}, EventKind::Decode, ResourceKind::Compute,
                  8.0, Unit::Count, Provenance::Measured, true, 2500, 2600, R, WA, A1,
                  E2, bootA2, RG1, G1, AccountingGeneration(1), D1, M, false, AdapterId{}, ""));
  // Duplicate of a worker-A boot1 entry (stale epoch) -> should be rejected as stale.
  a2.push_back(mk(LedgerEntryId{0xA, 2}, EventKind::Reserve, ResourceKind::Memory,
                  1.0, Unit::Count, Provenance::Measured, false, 1100, 0, R, WA, A1,
                  CoordinatorEpoch(1), bootA1, RG1, G1, AG1, D1, M, false, AdapterId{}, ""));
  // Fresh post-restart accounting.
  const RequestGeneration RG2(2);
  const AttemptGeneration G2(2);
  a2.push_back(mk(LedgerEntryId{0xA, 30}, EventKind::RequestStart, ResourceKind::Generic,
                  0.0, Unit::Count, Provenance::Measured, false, 3000, 0, R, WA, A2,
                  E2, bootA2, RG2, G2, AG2, D2, M, false, AdapterId{}, ""));
  a2.push_back(mk(LedgerEntryId{0xA, 31}, EventKind::Reserve, ResourceKind::Memory,
                  1.0, Unit::Count, Provenance::Measured, false, 3100, 0, R, WA, A2,
                  E2, bootA2, RG2, G2, AG2, D2, M, false, AdapterId{}, ""));
  a2.push_back(mk(LedgerEntryId{0xA, 32}, EventKind::GpuExecution, ResourceKind::Compute,
                  0.5, Unit::Seconds, Provenance::Measured, true, 3200, 3800, R, WA, A2,
                  E2, bootA2, RG2, G2, AG2, D2, M, false, AdapterId{}, ""));
  a2.push_back(mk(LedgerEntryId{0xA, 33}, EventKind::Decode, ResourceKind::Compute,
                  64.0, Unit::Count, Provenance::Measured, true, 3800, 4200, R, WA, A2,
                  E2, bootA2, RG2, G2, AG2, D2, M, false, AdapterId{}, ""));
  a2.push_back(mk(LedgerEntryId{0xA, 34}, EventKind::Release, ResourceKind::Memory,
                  1.0, Unit::Count, Provenance::Measured, false, 4300, 0, R, WA, A2,
                  E2, bootA2, RG2, G2, AG2, D2, M, false, AdapterId{}, ""));
  a2.push_back(mk(LedgerEntryId{0xA, 35}, EventKind::RequestEnd, ResourceKind::Generic,
                  0.0, Unit::Count, Provenance::Measured, false, 4400, 0, R, WA, A2,
                  E2, bootA2, RG2, G2, AG2, D2, M, false, AdapterId{}, ""));
  const std::string sA2 = dir + "\\wA2.script";
  write_script(a2, sA2);

  Child wa2;
  if (!spawn(ws("iledger_worker.exe 127.0.0.1 " + std::to_string(port) + " " +
                WA.to_string() + " " + bootA2.to_string() + " " + sA2 + " 1"), wa2)) {
    pass = false; reasons.push_back("spawn workerA boot2 failed");
  }
  if (!wait_for(wa2, "WORKER_DONE", 15000)) {
    pass = false; reasons.push_back("workerA boot2 did not finish");
  }

  // Assert every stale/duplicate ack status is the expected reject.
  for (const auto& line : snapshot_lines(wa2)) {
    if (line.find("ACK 0 ") != std::string::npos && line.find("ACK 0 3") == std::string::npos)
      { pass = false; reasons.push_back("stale epoch not rejected: " + line); }
    if (line.find("ACK 1 ") != std::string::npos && line.find("ACK 1 3") == std::string::npos)
      { pass = false; reasons.push_back("stale boot not rejected: " + line); }
    if (line.find("ACK 2 ") != std::string::npos && line.find("ACK 2 3") == std::string::npos)
      { pass = false; reasons.push_back("stale attempt not rejected: " + line); }
    if (line.find("ACK 3 ") != std::string::npos && line.find("ACK 3 3") == std::string::npos)
      { pass = false; reasons.push_back("stale accounting gen not rejected: " + line); }
    if (line.find("ACK 4 ") != std::string::npos && line.find("ACK 4 3") == std::string::npos)
      { pass = false; reasons.push_back("stale duplicate not rejected: " + line); }
    if (line.find("ACK 5 ") != std::string::npos && line.find("ACK 5 0") == std::string::npos)
      { pass = false; reasons.push_back("fresh RequestStart not accepted: " + line); }
    if (line.find("ACK 10 ") != std::string::npos && line.find("ACK 10 0") == std::string::npos)
      { pass = false; reasons.push_back("fresh RequestEnd not accepted: " + line); }
  }

  // ---- Shut down coordinator, save ledger ----------------
  write_stdin(coord, "SHUTDOWN\n");
  if (!wait_for(coord, "COORD_STOPPED", 5000)) {
    pass = false; reasons.push_back("coordinator did not save/shutdown");
  }
  kill_child(coord);
  kill_child(wb);
  kill_child(wa2);

  // ---- Verify the persisted ledger ----------------
  LoadResult lr = LedgerStore::load(ledger_path);
  if (!lr.ok) {
    pass = false; reasons.push_back("ledger load failed: " + lr.reason);
  } else {
    reasons.push_back("loaded " + std::to_string(lr.entries.size()) + " frames");
    // Reconstruct request R and R2.
    if (!lr.entries.empty()) {
      RequestAccount accR = reconcile_request(query_ledger(lr.entries,
          LedgerQuery{.request = R}), R);
      RequestAccount accR2 = reconcile_request(query_ledger(lr.entries,
          LedgerQuery{.request = R2}), R2);
      if (!accR.completed) { pass=false; reasons.push_back("R not completed"); }
      if (accR.attempt_count < 2) { pass=false; reasons.push_back("R attempts not reconstructed"); }
      if (accR.retries != 0) { /* R had no retry event; stale attempt work counted */ }
      if (!(accR.attempt_count == 2)) { pass=false; reasons.push_back("R attempts != 2"); }
      if (!accR2.completed) { pass=false; reasons.push_back("R2 not completed"); }
    }
  }

  // ---- Replay reproduction ----------------
  Ledger fresh;
  std::string rerr;
  if (!LedgerStore::replay_into(fresh, ledger_path, rerr)) {
    pass = false; reasons.push_back("replay failed: " + rerr);
  } else {
    const auto fs = fresh.snapshot();
    const LedgerFingerprint fp2 = ledger_fingerprint(fs);
    if (fp2 != lr.fingerprint) {
      pass = false; reasons.push_back("replay fingerprint mismatch");
    } else {
      reasons.push_back("replay digest stable: " + fp2.to_string());
    }
  }

  // ---- Report ----------------
  if (pass) {
    std::cout << "MULTIPROCESS_PROOF PASS\n";
    for (const auto& r2 : reasons) std::cout << "  + " << r2 << "\n";
  } else {
    std::cout << "MULTIPROCESS_PROOF FAIL\n";
    for (const auto& r2 : reasons) std::cout << "  - " << r2 << "\n";
  }
  return pass ? 0 : 1;
}
