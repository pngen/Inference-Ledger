// codec.hpp
// Deterministic, checksummed binary codec for Inference Ledger records.
//
// Every multi-byte integer is big-endian. Strings are length-prefixed (u32).
// Identities are 16 raw bytes. Entries are written sparsely. The codec
// rejects NaN/Inf, negative physical quantities, integer overflow, malformed
// lengths, truncation and trailing garbage.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "inference-ledger/config.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/unit.hpp"

namespace iledger {

class ByteWriter {
 public:
  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u16(std::uint16_t v) {
    buf_.push_back(static_cast<std::uint8_t>(v >> 8));
    buf_.push_back(static_cast<std::uint8_t>(v));
  }
  void u32(std::uint32_t v) {
    u8(static_cast<std::uint8_t>(v >> 24));
    u8(static_cast<std::uint8_t>(v >> 16));
    u8(static_cast<std::uint8_t>(v >> 8));
    u8(static_cast<std::uint8_t>(v));
  }
  void u64(std::uint64_t v) {
    u32(static_cast<std::uint32_t>(v >> 32));
    u32(static_cast<std::uint32_t>(v));
  }
  void bytes(const std::uint8_t* p, std::size_t n) {
    buf_.insert(buf_.end(), p, p + n);
  }
  void bool8(bool v) { u8(v ? 1u : 0u); }
  template <typename Tag>
  void id(const Id128<Tag>& x) {
    std::uint8_t b[kIdentityBytes];
    x.to_bytes(b);
    bytes(b, kIdentityBytes);
  }
  void string(const std::string& s) {
    if (s.size() > 0xFFFFFFFFu) return;
    u32(static_cast<std::uint32_t>(s.size()));
    bytes(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
  }
  const std::vector<std::uint8_t>& data() const noexcept { return buf_; }
  std::size_t size() const noexcept { return buf_.size(); }

 private:
  std::vector<std::uint8_t> buf_;
};

class ByteReader {
 public:
  ByteReader(const std::uint8_t* p, std::size_t n) : p_(p), n_(n), pos_(0) {}
  explicit ByteReader(const std::vector<std::uint8_t>& v)
      : ByteReader(v.data(), v.size()) {}

  bool u8(std::uint8_t& out) {
    if (remaining() < 1) { fail(); return false; }
    out = p_[pos_++];
    return true;
  }
  bool u16(std::uint16_t& out) {
    std::uint8_t a, b;
    if (!u8(a) || !u8(b)) { fail(); return false; }
    out = static_cast<std::uint16_t>((static_cast<std::uint16_t>(a) << 8) | b);
    return true;
  }
  bool u32(std::uint32_t& out) {
    std::uint8_t a, b, c, d;
    if (!u8(a) || !u8(b) || !u8(c) || !u8(d)) { fail(); return false; }
    out = (static_cast<std::uint32_t>(a) << 24) |
          (static_cast<std::uint32_t>(b) << 16) |
          (static_cast<std::uint32_t>(c) << 8) | static_cast<std::uint32_t>(d);
    return true;
  }
  bool u64(std::uint64_t& out) {
    std::uint32_t hi, lo;
    if (!u32(hi) || !u32(lo)) { fail(); return false; }
    out = (static_cast<std::uint64_t>(hi) << 32) | lo;
    return true;
  }
  bool bytes(std::uint8_t* out, std::size_t n) {
    if (remaining() < n) { fail(); return false; }
    std::memcpy(out, p_ + pos_, n);
    pos_ += n;
    return true;
  }
  bool bool8(bool& out) {
    std::uint8_t v;
    if (!u8(v)) { fail(); return false; }
    out = (v != 0);
    return true;
  }
  template <typename Tag>
  bool id(Id128<Tag>& out) {
    std::uint8_t b[kIdentityBytes];
    if (!bytes(b, kIdentityBytes)) { fail(); return false; }
    out = Id128<Tag>::from_bytes(b);
    return true;
  }
  bool string(std::string& out) {
    std::uint32_t len = 0;
    if (!u32(len)) { fail(); return false; }
    if (remaining() < len) { fail(); return false; }
    out.assign(reinterpret_cast<const char*>(p_ + pos_), len);
    pos_ += len;
    return true;
  }
  bool ok() const noexcept { return ok_; }
  std::size_t remaining() const noexcept { return pos_ <= n_ ? n_ - pos_ : 0; }
  void reset() noexcept { pos_ = 0; ok_ = true; }

 private:
  void fail() noexcept { ok_ = false; }
  const std::uint8_t* p_;
  std::size_t n_;
  std::size_t pos_;
  bool ok_ = true;
};

std::uint32_t crc32(const std::uint8_t* data, std::size_t len);

// Frame: [u32 payload_len][u32 crc32][payload].
bool encode_entry(const LedgerEntry& e, std::vector<std::uint8_t>& out);
bool decode_entry(const std::uint8_t* data, std::size_t n, LedgerEntry& entry,
                  std::size_t& consumed);

}  // namespace iledger
