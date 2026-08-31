// protocol.hpp
// Framed TCP protocol between a coordinator and accounting sources (workers).
//
// Wire frame: [ u32 payload_len ][ u32 crc32 ][ u8 type ][ payload ]
// where crc32 covers [ type ][ payload ]. Payloads use the deterministic codec
// codec (codec.hpp). A per-connection write mutex serialises sends; network
// I/O is never performed under the ledger/state lock.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "inference-ledger/codec.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
using SOCKET = int;
#endif

namespace iledger {

enum class MessageType : std::uint8_t {
  Register = 1,
  RegisterAck = 2,
  EventSubmit = 3,
  EventAck = 4,
  Roll = 5,
  Shutdown = 6,
  Ping = 7
};

struct RegisterMsg {
  WorkerId worker{};
  WorkerBootId boot{};
};
struct RegisterAckMsg {
  bool ok = false;
  CoordinatorEpoch epoch{};
  AccountingGeneration accounting_generation{};
  std::string reason;
};
struct EventSubmitMsg {
  LedgerEntry entry{};
};
struct EventAckMsg {
  AppendStatus status = AppendStatus::Invalid;
  std::string reason;
};
struct RollMsg {
  CoordinatorEpoch epoch{};
  AccountingGeneration accounting_generation{};
};

// A decoded message; only the field matching `type` is populated.
struct Message {
  MessageType type = MessageType::Ping;
  RegisterMsg reg;
  RegisterAckMsg reg_ack;
  EventSubmitMsg event;
  EventAckMsg event_ack;
  RollMsg roll;
};

// Manage the two ends of a single TCP connection with strict framing and
// checksum. Writes are serialised per connection. The socket is owned here.
class FramedSocket {
 public:
  FramedSocket() = default;
  explicit FramedSocket(SOCKET s) : sock_(s) {}
  ~FramedSocket();
  FramedSocket(const FramedSocket&) = delete;
  FramedSocket& operator=(const FramedSocket&) = delete;

  bool ok() const noexcept { return sock_ != static_cast<SOCKET>(~0); }

  // Send one length+checksum framed message. Serialised per connection.
  bool send_message(const Message& msg, std::string& err);
  // Blocking receive of one framed message.
  bool recv_message(Message& msg, std::string& err);

  void close();
  void adopt(SOCKET s) {
    if (ok()) close();
    sock_ = s;
  }
  SOCKET native() const noexcept { return sock_; }

 private:
  bool write_all(const std::uint8_t* p, std::size_t n, std::string& err);
  bool read_all(std::uint8_t* p, std::size_t n, std::string& err);

  SOCKET sock_ = static_cast<SOCKET>(~0);
  std::mutex write_mu_;
};

// Winsock initialisation (call once per process).
bool network_init(std::string& err);
void network_cleanup();

// Server-side helpers.
struct TcpServer {
  SOCKET listen_sock = static_cast<SOCKET>(~0);
  bool bind_and_listen(const std::string& host, std::uint16_t port,
                       std::string& err);
  bool accept(SOCKET& out, std::string& err);
  void close();
};

// Client connect helper.
bool connect_tcp(const std::string& host, std::uint16_t port, FramedSocket& out,
                 std::string& err);

}  // namespace iledger
