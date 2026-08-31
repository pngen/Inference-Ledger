// test_persistence.cpp
// Save/load/snapshot/append/replay/recovery and corruption rejection.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include "framework.hpp"
#include "inference-ledger/codec.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/persistence.hpp"
#include "support.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace iledger;
using namespace iledger::test;

namespace {
const LedgerId LID{0x494C4544474552ULL, 1};
const TenantId TEN{0x99, 1};
const WorkerId W{0xAA, 1};
const WorkerBootId BOOT{0xB007, 1};
const CoordinatorEpoch EP(1);
const AccountingGeneration AG(1);
const RequestId REQ{0x1234, 1};
const AttemptId ATT{0x2001, 1};
const RequestGeneration RG(1);
const AttemptGeneration ATG(1);
const DispatchId DISP{0x3001, 1};

LedgerEntry ev(const LedgerEntryId& id, EventKind k, ResourceKind rk, double v,
               Unit u, bool has_end, std::uint64_t s, std::uint64_t e) {
  return make(id, REQ, ATT, k, rk, v, u, Provenance::Measured, has_end, s, e,
              LID, TEN, W, BOOT, EP, RG, ATG, AG, DISP);
}

const char* kPath = "test_persist_ledger.db";
}  // namespace

TEST(persistence_save_load_roundtrip) {
  std::remove(kPath);
  std::vector<LedgerEntry> es = {
    ev(LedgerEntryId{1,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000, 0),
    ev(LedgerEntryId{2,1}, EventKind::Decode, ResourceKind::Compute, 8, Unit::Count, true, 1400, 1600),
    ev(LedgerEntryId{3,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 1700, 0),
  };
  std::string err;
  CHECK(LedgerStore::save_snapshot(es, LID, kPath, err));
  LoadResult r = LedgerStore::load(kPath);
  CHECK(r.ok);
  CHECK_EQ(r.entries.size(), 3u);
  CHECK(r.ledger_id == LID);
  CHECK(r.entries[1] == es[1]);
  Ledger fresh;
  CHECK(LedgerStore::replay_into(fresh, kPath, err));
  CHECK_EQ(fresh.size(), 3u);
  CHECK(ledger_fingerprint(fresh.snapshot()) == r.fingerprint);
  std::remove(kPath);
}

TEST(persistence_append) {
  std::remove(kPath);
  std::string err;
  CHECK(LedgerStore::save_snapshot({ev(LedgerEntryId{1,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000, 0)}, LID, kPath, err));
  CHECK(LedgerStore::append_entries({ev(LedgerEntryId{2,1}, EventKind::Decode, ResourceKind::Compute, 8, Unit::Count, true, 1400, 1600)}, kPath, err));
  LoadResult r = LedgerStore::load(kPath);
  CHECK(r.ok);
  CHECK_EQ(r.entries.size(), 2u);
  std::remove(kPath);
}

TEST(persistence_reject_duplicate_id) {
  std::remove(kPath);
  const auto e1 = ev(LedgerEntryId{1,1}, EventKind::Decode, ResourceKind::Compute, 8, Unit::Count, true, 1400, 1600);
  std::string err;
  CHECK(LedgerStore::save_snapshot({e1}, LID, kPath, err));
  CHECK(LedgerStore::append_entries({e1}, kPath, err));
  LoadResult r = LedgerStore::load(kPath);
  CHECK(!r.ok);
  CHECK(r.reason.find("duplicate") != std::string::npos);
  std::remove(kPath);
}

TEST(persistence_reject_corruption) {
  std::remove(kPath);
  const auto e1 = ev(LedgerEntryId{1,1}, EventKind::Decode, ResourceKind::Compute, 8, Unit::Count, true, 1400, 1600);
  std::string err;
  CHECK(LedgerStore::save_snapshot({e1}, LID, kPath, err));
  {
    std::fstream f(kPath, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(40);  // inside the frame payload (CRC-protected)
    char c = 0;
    f.get(c);
    f.seekp(40);
    c = static_cast<char>(c ^ 0x55);
    f.put(c);
  }
  LoadResult r = LedgerStore::load(kPath);
  CHECK(!r.ok);
  LoadResult rec = LedgerStore::recover(kPath);
  CHECK(!rec.ok);
  std::remove(kPath);
}

TEST(persistence_reject_truncation) {
  std::remove(kPath);
  const auto e1 = ev(LedgerEntryId{1,1}, EventKind::Decode, ResourceKind::Compute, 8, Unit::Count, true, 1400, 1600);
  std::string err;
  CHECK(LedgerStore::save_snapshot({e1}, LID, kPath, err));
  {
    std::ofstream f(kPath, std::ios::binary | std::ios::app);
    char c[3] = {'z', 'y', 'x'};
    f.write(c, 3);
  }
  LoadResult r = LedgerStore::load(kPath);
  CHECK(!r.ok);
  std::remove(kPath);
}

TEST(persistence_reject_bad_magic) {
  std::remove(kPath);
  const auto e1 = ev(LedgerEntryId{1,1}, EventKind::Decode, ResourceKind::Compute, 8, Unit::Count, true, 1400, 1600);
  std::string err;
  CHECK(LedgerStore::save_snapshot({e1}, LID, kPath, err));
  {
    std::fstream f(kPath, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(0);
    f.put('X');
  }
  LoadResult r = LedgerStore::load(kPath);
  CHECK(!r.ok);
  CHECK(r.reason.find("magic") != std::string::npos);
  std::remove(kPath);
}

TEST(persistence_reject_negative_physical) {
  std::remove(kPath);
  auto e = ev(LedgerEntryId{1,1}, EventKind::KvAllocate, ResourceKind::Kv, -5, Unit::Bytes, false, 1000, 0);
  std::string err;
  CHECK(!LedgerStore::save_snapshot({e}, LID, kPath, err));
  std::remove(kPath);
}

TEST(persistence_replay_fingerprint_stable) {
  std::remove(kPath);
  std::vector<LedgerEntry> es;
  for (int i = 0; i < 100; ++i) {
    es.push_back(ev(LedgerEntryId{static_cast<std::uint64_t>(i+1),1}, EventKind::Decode,
                    ResourceKind::Compute, static_cast<double>(i % 50), Unit::Count,
                    true, 1000 + i * 100, 1200 + i * 100));
  }
  std::string err;
  CHECK(LedgerStore::save_snapshot(es, LID, kPath, err));
  LoadResult r1 = LedgerStore::load(kPath);
  LoadResult r2 = LedgerStore::load(kPath);
  CHECK(r1.ok && r2.ok);
  CHECK(r1.fingerprint == r2.fingerprint);
  Ledger fresh;
  CHECK(LedgerStore::replay_into(fresh, kPath, err));
  CHECK(ledger_fingerprint(fresh.snapshot()) == r1.fingerprint);
  std::remove(kPath);
}
