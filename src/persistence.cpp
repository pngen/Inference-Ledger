#include "inference-ledger/persistence.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "inference-ledger/codec.hpp"
#include "inference-ledger/config.hpp"

namespace iledger {

namespace {

struct Mix {
  std::uint64_t a = 0xCBF29CE484222325ULL;
  std::uint64_t b = 0x9E3779B97F4A7C15ULL;
  void feed(std::uint64_t v) {
    a ^= v + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2);
    b = (b ^ (v * 0x100000001B3ULL)) * 0x9E3779B97F4A7C15ULL;
  }
  void feed_bytes(const std::uint8_t* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) feed(static_cast<std::uint64_t>(p[i]));
  }
};

}  // namespace

std::string LedgerFingerprint::to_string() const {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string s;
  s.reserve(32);
  for (std::uint8_t b : bytes) {
    s.push_back(kHex[(b >> 4) & 0x0F]);
    s.push_back(kHex[b & 0x0F]);
  }
  return s;
}

LedgerFingerprint ledger_fingerprint(const std::vector<LedgerEntry>& entries) {
  Mix m;
  for (const auto& e : entries) {
    std::vector<std::uint8_t> frame;
    if (!encode_entry(e, frame)) {
      // Should never happen for a valid entry; feed empty to stay deterministic.
      m.feed(0xFFFFFFFFFFFFFFFFULL);
      continue;
    }
    m.feed_bytes(frame.data(), frame.size());
  }
  LedgerFingerprint fp;
  for (std::size_t i = 0; i < 8; ++i) fp.bytes[i] = static_cast<std::uint8_t>(m.a >> (56 - i * 8));
  for (std::size_t i = 0; i < 8; ++i) fp.bytes[8 + i] = static_cast<std::uint8_t>(m.b >> (56 - i * 8));
  return fp;
}

namespace {

void write_u64(std::ofstream& f, std::uint64_t v) {
  std::uint8_t b[8];
  for (int i = 0; i < 8; ++i) b[i] = static_cast<std::uint8_t>(v >> (56 - i * 8));
  f.write(reinterpret_cast<const char*>(b), 8);
}
void write_u32(std::ofstream& f, std::uint32_t v) {
  std::uint8_t b[4];
  for (int i = 0; i < 4; ++i) b[i] = static_cast<std::uint8_t>(v >> (24 - i * 8));
  f.write(reinterpret_cast<const char*>(b), 4);
}

bool write_header(std::ofstream& f, const LedgerId& id) {
  write_u64(f, kLedgerMagic);
  write_u32(f, kSchemaVersion);
  std::uint8_t idb[kIdentityBytes];
  id.to_bytes(idb);
  f.write(reinterpret_cast<const char*>(idb), kIdentityBytes);
  return static_cast<bool>(f);
}

}  // namespace

bool LedgerStore::save_snapshot(const std::vector<LedgerEntry>& entries,
                                const LedgerId& ledger_id,
                                const std::string& path, std::string& err) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) { err = "cannot open " + path; return false; }
  if (!write_header(f, ledger_id)) { err = "cannot write header"; return false; }
  for (const auto& e : entries) {
    std::vector<std::uint8_t> frame;
    if (!encode_entry(e, frame)) { err = "entry failed to encode"; return false; }
    f.write(reinterpret_cast<const char*>(frame.data()),
            static_cast<std::streamsize>(frame.size()));
    if (!f) { err = "write failed"; return false; }
  }
  f.flush();
  if (!f) { err = "flush failed"; return false; }
  return true;
}

bool LedgerStore::append_entries(const std::vector<LedgerEntry>& entries,
                                 const std::string& path, std::string& err) {
  // Determine whether the file already has a valid header.
  bool has_header = false;
  {
    std::ifstream in(path, std::ios::binary);
    if (in) {
      std::uint8_t magic[8];
      in.read(reinterpret_cast<char*>(magic), 8);
      if (in.gcount() == 8) {
        std::uint64_t m = 0;
        for (int i = 0; i < 8; ++i) m = (m << 8) | magic[i];
        has_header = (m == kLedgerMagic);
      }
    }
  }
  if (!has_header) {
    // Fresh file: write header using the first entry's ledger id (or zero).
    LedgerId id{};
    if (!entries.empty()) id = entries.front().ledger;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f || !write_header(f, id)) { err = "cannot create file"; return false; }
  }
  std::ofstream f(path, std::ios::binary | std::ios::app);
  if (!f) { err = "cannot open for append"; return false; }
  for (const auto& e : entries) {
    std::vector<std::uint8_t> frame;
    if (!encode_entry(e, frame)) { err = "entry failed to encode"; return false; }
    f.write(reinterpret_cast<const char*>(frame.data()),
            static_cast<std::streamsize>(frame.size()));
    if (!f) { err = "append write failed"; return false; }
  }
  f.flush();
  if (!f) { err = "append flush failed"; return false; }
  return true;
}

LoadResult LedgerStore::load(const std::string& path) {
  LoadResult res;
  std::ifstream in(path, std::ios::binary);
  if (!in) { res.reason = "cannot open " + path; return res; }
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
  if (data.size() < 8u + 4u + kIdentityBytes) {
    res.reason = "file too small for header";
    return res;
  }

  std::uint64_t magic = 0;
  for (int i = 0; i < 8; ++i) magic = (magic << 8) | data[static_cast<std::size_t>(i)];
  if (magic != kLedgerMagic) { res.reason = "bad magic"; return res; }
  std::uint32_t ver = (static_cast<std::uint32_t>(data[8]) << 24) |
                      (static_cast<std::uint32_t>(data[9]) << 16) |
                      (static_cast<std::uint32_t>(data[10]) << 8) |
                      static_cast<std::uint32_t>(data[11]);
  if (ver != kSchemaVersion) { res.reason = "unsupported version"; return res; }
  res.ledger_id = LedgerId::from_bytes(data.data() + 12);

  std::size_t pos = 8u + 4u + kIdentityBytes;
  std::vector<LedgerEntry> seen;
  std::vector<LedgerEntryId> ids;

  while (pos < data.size()) {
    if (data.size() - pos < 8u) {
      res.reason = "truncated frame header";
      return res;
    }
    const std::uint32_t len = (static_cast<std::uint32_t>(data[pos]) << 24) |
                              (static_cast<std::uint32_t>(data[pos + 1]) << 16) |
                              (static_cast<std::uint32_t>(data[pos + 2]) << 8) |
                              static_cast<std::uint32_t>(data[pos + 3]);
    if (static_cast<std::size_t>(len) + 8u > data.size() - pos) {
      res.reason = "truncated frame payload";
      return res;
    }
    LedgerEntry e;
    std::size_t consumed = 0;
    if (!decode_entry(data.data() + pos, data.size() - pos, e, consumed)) {
      res.reason = "corrupt frame or invalid record";
      return res;
    }
    if (consumed != static_cast<std::size_t>(len) + 8u) {
      res.reason = "frame length mismatch";
      return res;
    }
    for (const auto& existing : ids) {
      if (existing == e.id) {
        res.reason = "duplicate entry id";
        return res;
      }
    }
    ids.push_back(e.id);
    seen.push_back(e);
    pos += consumed;
    ++res.frame_count;
  }

  // After the loop, pos == data.size(); no trailing garbage remains.
  res.entries = std::move(seen);
  res.fingerprint = ledger_fingerprint(res.entries);
  res.ok = true;
  res.reason = "loaded " + std::to_string(res.frame_count) + " frames";
  return res;
}

bool LedgerStore::replay_into(Ledger& out, const std::string& path,
                              std::string& err) {
  LoadResult res = load(path);
  if (!res.ok) { err = res.reason; return false; }
  std::size_t accepted = 0;
  for (const auto& e : res.entries) {
    const AppendResult ar = out.append(e);
    if (ar.status != AppendStatus::Accepted &&
        ar.status != AppendStatus::Duplicate) {
      err = std::string("replay rejected entry: ") + ar.reason;
      return false;
    }
    if (ar.status == AppendStatus::Accepted) ++accepted;
  }
  return true;
}

LoadResult LedgerStore::recover(const std::string& path) {
  return load(path);
}

}  // namespace iledger
