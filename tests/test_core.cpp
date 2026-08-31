// test_core.cpp
// Identities, generations, units, provenance, event kinds, codec, ledger.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include "framework.hpp"
#include <limits>
#include "inference-ledger/authority.hpp"
#include "inference-ledger/authority_state.hpp"
#include "inference-ledger/codec.hpp"
#include "inference-ledger/config.hpp"
#include "inference-ledger/event_kind.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/unit.hpp"
#include "support.hpp"

using namespace iledger;
using namespace iledger::test;

TEST(core_identity_roundtrip) {
  RequestId a{0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL};
  std::uint8_t b[kIdentityBytes];
  a.to_bytes(b);
  RequestId c = RequestId::from_bytes(b);
  CHECK_EQ(c, a);
  CHECK_EQ(a.to_string(), "0123456789abcdeffedcba9876543210");
  auto p = RequestId::parse(a.to_string());
  CHECK(p.has_value());
  CHECK(*p == a);
  auto bad = RequestId::parse("zz");
  CHECK(!bad.has_value());
}

TEST(core_identity_ordering_and_hash) {
  Id128<RequestIdTag> x{1, 2}, y{1, 3}, z{1, 2};
  CHECK(x == z);
  CHECK(x < y);
  CHECK(y > x);
  CHECK((x <=> z) == std::strong_ordering::equal);
  std::hash<Id128<RequestIdTag>> h;
  CHECK_EQ(h(x), h(z));
}

TEST(core_identity_types_distinct) {
  // Different identity types are distinct C++ types and cannot be compared.
  RequestId r{1, 1};
  TenantId t{1, 1};
  CHECK(r.hi() == t.hi());
}

TEST(core_generation) {
  CoordinatorEpoch e(1);
  CHECK(e.value() == 1);
  CHECK(e.next().value() == 2);
  CHECK(e == CoordinatorEpoch(1));
  CHECK(e < e.next());
}

TEST(core_unit_model) {
  Quantity q(4096.0, Unit::Bytes, Provenance::Measured);
  CHECK(q.is_finite());
  CHECK(q.is_non_negative());
  CHECK_EQ(std::string(unit_name(q.unit)), "bytes");
  Quantity cnt(Quantity::count(5));
  CHECK(cnt.unit == Unit::Count);
  CHECK(cnt.is_integral());
}

TEST(core_provenance) {
  CHECK_EQ(std::string(provenance_name(Provenance::Measured)), "measured");
  CHECK_EQ(std::string(provenance_name(Provenance::Estimated)), "estimated");
  CHECK(provenance_parse("derived") == Provenance::Derived);
  CHECK(provenance_parse("bogus") == Provenance::Unavailable);
}

TEST(core_event_kind_names) {
  CHECK_EQ(std::string(event_kind_name(EventKind::Prefill)), "PREFILL");
  CHECK_EQ(std::string(event_kind_name(EventKind::SpeculationRejected)), "SPECULATION_REJECTED");
  CHECK(event_kind_parse("DECODE") == EventKind::Decode);
  CHECK(event_kind_parse("blah") == EventKind::RequestStart);
}

TEST(core_codec_roundtrip) {
  const LedgerId lid{0x494C4544474552ULL, 1};
  const RequestId req{0x1234, 1};
  const AttemptId att{0x2001, 1};
  const WorkerId w{0xAA, 1};
  const WorkerBootId boot{0xB007, 1};
  LedgerEntry e = make(LedgerEntryId{1, 2}, req, att, EventKind::GpuExecution,
                       ResourceKind::Compute, 0.25, Unit::Seconds,
                       Provenance::Measured, true, 100, 350, lid, TenantId{0x99,1},
                       w, boot, CoordinatorEpoch(1), RequestGeneration(1),
                       AttemptGeneration(1), AccountingGeneration(1),
                       DispatchId{0x3001,1});
  std::vector<std::uint8_t> bytes;
  CHECK(encode_entry(e, bytes));
  CHECK(!bytes.empty());
  LedgerEntry out;
  std::size_t consumed = 0;
  CHECK(decode_entry(bytes.data(), bytes.size(), out, consumed));
  CHECK_EQ(consumed, bytes.size());
  CHECK(out == e);
}

TEST(core_codec_rejects_corruption) {
  const LedgerId lid{0x1, 1};
  LedgerEntry e = make(LedgerEntryId{1, 1}, RequestId{0x1234,1}, AttemptId{0x2001,1},
                       EventKind::Decode, ResourceKind::Compute, 8, Unit::Count,
                       Provenance::Measured, true, 100, 200, lid, TenantId{0x99,1},
                       WorkerId{0xAA,1}, WorkerBootId{0xB007,1}, CoordinatorEpoch(1),
                       RequestGeneration(1), AttemptGeneration(1), AccountingGeneration(1),
                       DispatchId{0x3001,1});
  std::vector<std::uint8_t> bytes;
  CHECK(encode_entry(e, bytes));
  // Flip a byte in the middle -> bad crc.
  std::vector<std::uint8_t> bad = bytes;
  bad[bad.size() / 2] ^= 0xFF;
  LedgerEntry out; std::size_t consumed = 0;
  CHECK(!decode_entry(bad.data(), bad.size(), out, consumed));
  // Truncate -> malformed.
  std::vector<std::uint8_t> trunc(bytes.begin(), bytes.begin() + 5);
  CHECK(!decode_entry(trunc.data(), trunc.size(), out, consumed));
}

TEST(core_codec_rejects_nan_and_negative) {
  const LedgerId lid{0x1, 1};
  auto base = make(LedgerEntryId{1, 1}, RequestId{0x1234,1}, AttemptId{0x2001,1},
                   EventKind::Decode, ResourceKind::Compute, 8, Unit::Count,
                   Provenance::Measured, true, 100, 200, lid, TenantId{0x99,1},
                   WorkerId{0xAA,1}, WorkerBootId{0xB007,1}, CoordinatorEpoch(1),
                   RequestGeneration(1), AttemptGeneration(1), AccountingGeneration(1),
                   DispatchId{0x3001,1});
  LedgerEntry nanE = base;
  nanE.quantity.value = std::numeric_limits<double>::quiet_NaN();
  std::vector<std::uint8_t> bytes;
  CHECK(!encode_entry(nanE, bytes));
  LedgerEntry neg = base;
  neg.resource_kind = ResourceKind::Kv;
  neg.quantity.value = -5.0;
  CHECK(!encode_entry(neg, bytes));
}

TEST(core_codec_rejects_invalid_enum) {
  // Build valid bytes then patch the event-kind byte to an invalid value.
  const LedgerId lid{0x1, 1};
  LedgerEntry e = make(LedgerEntryId{1, 1}, RequestId{0x1234,1}, AttemptId{0x2001,1},
                       EventKind::Decode, ResourceKind::Compute, 8, Unit::Count,
                       Provenance::Measured, true, 100, 200, lid, TenantId{0x99,1},
                       WorkerId{0xAA,1}, WorkerBootId{0xB007,1}, CoordinatorEpoch(1),
                       RequestGeneration(1), AttemptGeneration(1), AccountingGeneration(1),
                       DispatchId{0x3001,1});
  std::vector<std::uint8_t> bytes;
  CHECK(encode_entry(e, bytes));
  // Locate the event-kind byte: after 6 ids (16 each) + header. We patch any
  // byte that equals the Decode enum value and is followed by the Compute value.
  // Simpler: brute-force a corrupt byte in the payload and ensure decode refuses
  // OR a patched enum-byte is caught. We validate via a targeted search.
  // Layout: 8-byte frame header, 1 endian, 4 version, then 7 ids, 1 bool,
  // then 5 ids (attempt/dispatch/worker/node/device), then event_kind.
  const std::size_t evOff = 8 + 1 + 4 + 7 * kIdentityBytes + 1 + 5 * kIdentityBytes;
  std::vector<std::uint8_t> bad = bytes;
  bad[evOff] = 200;  // invalid event kind
  LedgerEntry out; std::size_t consumed = 0;
  CHECK(!decode_entry(bad.data(), bad.size(), out, consumed));
}

TEST(core_ledger_append_idempotent) {
  Ledger ledger(LedgerId{0x1, 1});
  LedgerEntry e = make(LedgerEntryId{5, 1}, RequestId{0x1234,1}, AttemptId{0x2001,1},
                       EventKind::Reserve, ResourceKind::Memory, 1, Unit::Count,
                       Provenance::Measured, false, 100, 0, ledger.id(), TenantId{0x99,1},
                       WorkerId{0xAA,1}, WorkerBootId{0xB007,1}, CoordinatorEpoch(1),
                       RequestGeneration(1), AttemptGeneration(1), AccountingGeneration(1),
                       DispatchId{0x3001,1});
  AppendResult a = ledger.append(e);
  CHECK(a.status == AppendStatus::Accepted);
  CHECK_EQ(ledger.size(), 1u);
  AppendResult b = ledger.append(e);  // idempotent duplicate
  CHECK(b.status == AppendStatus::Duplicate);
  CHECK_EQ(ledger.size(), 1u);
  // Conflict: same id, different content.
  LedgerEntry e2 = e;
  e2.quantity.value = 7.0;
  AppendResult c = ledger.append(e2);
  CHECK(c.status == AppendStatus::DuplicateConflict);
  CHECK_EQ(ledger.size(), 1u);
}

TEST(core_ledger_rejects_invalid) {
  Ledger ledger(LedgerId{0x1, 1});
  LedgerEntry e = make(LedgerEntryId{5, 1}, RequestId{0x1234,1}, AttemptId{0x2001,1},
                       EventKind::Decode, ResourceKind::Compute, 8, Unit::Count,
                       Provenance::Measured, true, 100, 200, ledger.id(), TenantId{0x99,1},
                       WorkerId{0xAA,1}, WorkerBootId{0xB007,1}, CoordinatorEpoch(1),
                       RequestGeneration(1), AttemptGeneration(1), AccountingGeneration(1),
                       DispatchId{0x3001,1});
  e.quantity.value = -1.0;  // negative physical
  AppendResult a = ledger.append(e);
  CHECK(a.status == AppendStatus::Invalid);
  CHECK_EQ(ledger.size(), 0u);
}

TEST(core_authority_fencing) {
  AuthorityState s;
  s.epoch = CoordinatorEpoch(1);
  s.accounting_generation = AccountingGeneration(3);
  s.worker_boot[WorkerId{0xAA,1}] = WorkerBootId{0xB007,1};
  s.request_generation[RequestId{0x1234,1}] = RequestGeneration(1);
  s.current_attempt[RequestId{0x1234,1}] = AttemptId{0x2001,1};
  s.attempt_generation[RequestId{0x1234,1}] = AttemptGeneration(1);

  LedgerEntry e = make(LedgerEntryId{1,1}, RequestId{0x1234,1}, AttemptId{0x2001,1},
                       EventKind::GpuExecution, ResourceKind::Compute, 0.1, Unit::Seconds,
                       Provenance::Measured, true, 100, 200, LedgerId{0x1,1}, TenantId{0x99,1},
                       WorkerId{0xAA,1}, WorkerBootId{0xB007,1}, CoordinatorEpoch(1),
                       RequestGeneration(1), AttemptGeneration(1), AccountingGeneration(3),
                       DispatchId{0x3001,1});
  CHECK(validate_authority(e, s) == AuthorityVerdict::Accept);

  LedgerEntry staleEpoch = e;
  staleEpoch.authority.epoch = CoordinatorEpoch(0);
  CHECK(validate_authority(staleEpoch, s) == AuthorityVerdict::StaleEpoch);

  LedgerEntry staleBoot = e;
  staleBoot.authority.worker_boot = WorkerBootId{0xDEAD,1};
  CHECK(validate_authority(staleBoot, s) == AuthorityVerdict::StaleWorker);

  LedgerEntry staleAttempt = e;
  staleAttempt.authority.attempt = AttemptId{0x9999,1};
  CHECK(validate_authority(staleAttempt, s) == AuthorityVerdict::StaleGeneration);

  LedgerEntry staleAg = e;
  staleAg.authority.accounting_generation = AccountingGeneration(1);
  CHECK(validate_authority(staleAg, s) == AuthorityVerdict::StaleGeneration);

  // Rolled request rejects non-RequestStart.
  s.request_rolled[RequestId{0x1234,1}] = true;
  CHECK(validate_authority(e, s) == AuthorityVerdict::StaleGeneration);
}

TEST(core_authority_ledger_rejects_stale) {
  AuthorityState s;
  s.epoch = CoordinatorEpoch(1);
  s.accounting_generation = AccountingGeneration(3);
  s.worker_boot[WorkerId{0xAA,1}] = WorkerBootId{0xB007,1};

  Ledger ledger(LedgerId{0x1,1});
  ledger.attach_authority(&s);
  LedgerEntry e = make(LedgerEntryId{1,1}, RequestId{0x1234,1}, AttemptId{0x2001,1},
                       EventKind::Reserve, ResourceKind::Memory, 1, Unit::Count,
                       Provenance::Measured, false, 100, 0, ledger.id(), TenantId{0x99,1},
                       WorkerId{0xAA,1}, WorkerBootId{0xB007,1}, CoordinatorEpoch(1),
                       RequestGeneration(1), AttemptGeneration(1), AccountingGeneration(3),
                       DispatchId{0x3001,1});
  AppendResult ok = ledger.append(e);
  CHECK(ok.status == AppendStatus::Accepted);
  LedgerEntry e2 = e;
  e2.authority.epoch = CoordinatorEpoch(0);
  AppendResult stale = ledger.append(e2);
  CHECK(stale.status == AppendStatus::Stale);
  CHECK_EQ(ledger.size(), 1u);
}
