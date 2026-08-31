#include "inference-ledger/protocol.hpp"

#include <cstring>

#include "inference-ledger/config.hpp"

namespace iledger {

namespace {

std::uint8_t msg_type_val(MessageType t) { return static_cast<std::uint8_t>(t); }
MessageType msg_type_from(std::uint8_t v) {
  switch (v) {
    case 1: return MessageType::Register;
    case 2: return MessageType::RegisterAck;
    case 3: return MessageType::EventSubmit;
    case 4: return MessageType::EventAck;
    case 5: return MessageType::Roll;
    case 6: return MessageType::Shutdown;
    case 7: return MessageType::Ping;
  }
  return MessageType::Ping;
}

bool encode_message(const Message& msg, std::vector<std::uint8_t>& out) {
  ByteWriter w;
  switch (msg.type) {
    case MessageType::Register:
      w.id(msg.reg.worker);
      w.id(msg.reg.boot);
      break;
    case MessageType::RegisterAck:
      w.bool8(msg.reg_ack.ok);
      w.u64(msg.reg_ack.epoch.value());
      w.u64(msg.reg_ack.accounting_generation.value());
      w.string(msg.reg_ack.reason);
      break;
    case MessageType::EventSubmit: {
      std::vector<std::uint8_t> frame;
      if (!encode_entry(msg.event.entry, frame)) return false;
      w.u32(static_cast<std::uint32_t>(frame.size()));
      w.bytes(frame.data(), frame.size());
      break;
    }
    case MessageType::EventAck:
      w.u8(static_cast<std::uint8_t>(msg.event_ack.status));
      w.string(msg.event_ack.reason);
      break;
    case MessageType::Roll:
      w.u64(msg.roll.epoch.value());
      w.u64(msg.roll.accounting_generation.value());
      break;
    case MessageType::Shutdown:
    case MessageType::Ping:
      break;
  }

  const std::vector<std::uint8_t>& payload = w.data();
  std::vector<std::uint8_t> content;
  content.reserve(1u + payload.size());
  content.push_back(msg_type_val(msg.type));
  content.insert(content.end(), payload.begin(), payload.end());

  const std::uint32_t crc = crc32(content.data(), content.size());
  out.clear();
  out.reserve(content.size() + 8u);
  const std::uint32_t len = static_cast<std::uint32_t>(content.size());
  out.push_back(static_cast<std::uint8_t>(len >> 24));
  out.push_back(static_cast<std::uint8_t>(len >> 16));
  out.push_back(static_cast<std::uint8_t>(len >> 8));
  out.push_back(static_cast<std::uint8_t>(len));
  out.push_back(static_cast<std::uint8_t>(crc >> 24));
  out.push_back(static_cast<std::uint8_t>(crc >> 16));
  out.push_back(static_cast<std::uint8_t>(crc >> 8));
  out.push_back(static_cast<std::uint8_t>(crc));
  out.insert(out.end(), content.begin(), content.end());
  return true;
}

bool decode_message(const std::uint8_t* data, std::size_t n, Message& msg) {
  if (n < 1u) return false;
  msg.type = msg_type_from(data[0]);
  ByteReader r(data + 1, n - 1);
  switch (msg.type) {
    case MessageType::Register:
      return r.id(msg.reg.worker) && r.id(msg.reg.boot) && r.remaining() == 0;
    case MessageType::RegisterAck: {
      bool ok = false;
      std::uint64_t e = 0, g = 0;
      if (!r.bool8(ok)) return false;
      if (!r.u64(e)) return false;
      if (!r.u64(g)) return false;
      msg.reg_ack.ok = ok;
      msg.reg_ack.epoch = CoordinatorEpoch(e);
      msg.reg_ack.accounting_generation = AccountingGeneration(g);
      return r.string(msg.reg_ack.reason) && r.remaining() == 0;
    }
    case MessageType::EventSubmit: {
      std::uint32_t flen = 0;
      if (!r.u32(flen)) return false;
      if (r.remaining() != flen) return false;
      std::size_t consumed = 0;
      if (!decode_entry(data + 1 + 4, n - 1 - 4, msg.event.entry, consumed)) return false;
      return consumed == flen && consumed == r.remaining();
    }
    case MessageType::EventAck: {
      std::uint8_t st = 0;
      if (!r.u8(st)) return false;
      msg.event_ack.status = static_cast<AppendStatus>(st);
      return r.string(msg.event_ack.reason) && r.remaining() == 0;
    }
    case MessageType::Roll: {
      std::uint64_t e = 0, g = 0;
      if (!r.u64(e) || !r.u64(g)) return false;
      msg.roll.epoch = CoordinatorEpoch(e);
      msg.roll.accounting_generation = AccountingGeneration(g);
      return r.remaining() == 0;
    }
    case MessageType::Shutdown:
    case MessageType::Ping:
      return r.remaining() == 0;
  }
  return false;
}

}  // namespace

FramedSocket::~FramedSocket() { close(); }

void FramedSocket::close() {
  if (ok()) {
#ifdef _WIN32
    closesocket(sock_);
#else
    ::close(sock_);
#endif
    sock_ = static_cast<SOCKET>(~0);
  }
}

bool FramedSocket::write_all(const std::uint8_t* p, std::size_t n,
                             std::string& err) {
  std::size_t sent = 0;
  while (sent < n) {
#ifdef _WIN32
    const int r = ::send(sock_,
                         reinterpret_cast<const char*>(p + sent),
                         static_cast<int>(n - sent), 0);
#else
    const long r = ::send(sock_, reinterpret_cast<const char*>(p + sent),
                          static_cast<size_t>(n - sent), 0);
#endif
    if (r == 0) { err = "connection closed during send"; return false; }
    if (r < 0) {
#ifdef _WIN32
      const int w = WSAGetLastError();
      (void)w;
#endif
      err = "send error";
      return false;
    }
    sent += static_cast<std::size_t>(r);
  }
  return true;
}

bool FramedSocket::read_all(std::uint8_t* p, std::size_t n, std::string& err) {
  std::size_t got = 0;
  while (got < n) {
#ifdef _WIN32
    const int r = ::recv(sock_, reinterpret_cast<char*>(p + got),
                         static_cast<int>(n - got), 0);
#else
    const long r = ::recv(sock_, reinterpret_cast<char*>(p + got),
                          static_cast<size_t>(n - got), 0);
#endif
    if (r == 0) { err = "connection closed during recv"; return false; }
    if (r < 0) { err = "recv error"; return false; }
    got += static_cast<std::size_t>(r);
  }
  return true;
}

bool FramedSocket::send_message(const Message& msg, std::string& err) {
  std::lock_guard<std::mutex> lk(write_mu_);
  std::vector<std::uint8_t> frame;
  if (!encode_message(msg, frame)) { err = "message encode failed"; return false; }
  return write_all(frame.data(), frame.size(), err);
}

bool FramedSocket::recv_message(Message& msg, std::string& err) {
  std::uint8_t hdr[8];
  if (!read_all(hdr, 8, err)) return false;
  std::uint32_t len = (static_cast<std::uint32_t>(hdr[0]) << 24) |
                      (static_cast<std::uint32_t>(hdr[1]) << 16) |
                      (static_cast<std::uint32_t>(hdr[2]) << 8) |
                      static_cast<std::uint32_t>(hdr[3]);
  const std::uint32_t crc_want = (static_cast<std::uint32_t>(hdr[4]) << 24) |
                                 (static_cast<std::uint32_t>(hdr[5]) << 16) |
                                 (static_cast<std::uint32_t>(hdr[6]) << 8) |
                                 static_cast<std::uint32_t>(hdr[7]);
  if (len == 0) { err = "zero-length frame"; return false; }
  if (len > (16u << 20)) { err = "frame too large"; return false; }
  std::vector<std::uint8_t> content(len);
  if (!read_all(content.data(), content.size(), err)) return false;
  const std::uint32_t crc_got = crc32(content.data(), content.size());
  if (crc_got != crc_want) { err = "frame checksum mismatch"; return false; }
  return decode_message(content.data(), content.size(), msg);
}

bool network_init(std::string& err) {
#ifdef _WIN32
  WSADATA d;
  if (WSAStartup(MAKEWORD(2, 2), &d) != 0) {
    err = "WSAStartup failed";
    return false;
  }
#else
  (void)err;
#endif
  return true;
}

void network_cleanup() {
#ifdef _WIN32
  WSACleanup();
#endif
}

bool TcpServer::bind_and_listen(const std::string& host, std::uint16_t port,
                                std::string& err) {
#ifdef _WIN32
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
  if (s == static_cast<SOCKET>(~0)) { err = "socket failed"; return false; }
  int one = 1;
  (void)::setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&one), sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (host.empty()) {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
#ifdef _WIN32
      closesocket(s);
#else
      ::close(s);
#endif
      err = "bad host";
      return false;
    }
  }
  if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    err = "bind failed";
    return false;
  }
  if (::listen(s, 16) != 0) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    err = "listen failed";
    return false;
  }
  listen_sock = s;
  return true;
}

bool TcpServer::accept(SOCKET& out, std::string& err) {
#ifdef _WIN32
  sockaddr_in addr{};
  int len = static_cast<int>(sizeof(addr));
  SOCKET s = ::accept(listen_sock, reinterpret_cast<sockaddr*>(&addr), &len);
#else
  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  SOCKET s = ::accept(listen_sock, reinterpret_cast<sockaddr*>(&addr), &len);
#endif
  if (s == static_cast<SOCKET>(~0)) { err = "accept failed"; return false; }
  out = s;
  return true;
}

void TcpServer::close() {
  if (listen_sock != static_cast<SOCKET>(~0)) {
#ifdef _WIN32
    closesocket(listen_sock);
#else
    ::close(listen_sock);
#endif
    listen_sock = static_cast<SOCKET>(~0);
  }
}

bool connect_tcp(const std::string& host, std::uint16_t port, FramedSocket& out,
                 std::string& err) {
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == static_cast<SOCKET>(~0)) { err = "socket failed"; return false; }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
  if (::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    err = "connect failed";
    return false;
  }
  out.adopt(s);
  return true;
}

}  // namespace iledger
