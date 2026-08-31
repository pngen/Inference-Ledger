// coordinator.hpp
// A real coordinator for distributed inference accounting.
//
// The coordinator owns the authoritative ledger and the authority snapshot
// (epoch, WorkerBootId, request/attempt/accounting generations). Workers
// register over framed TCP and submit accounting events; the coordinator
// validates each event against the current authority, appends valid events to
// the ledger, rejects stale/duplicate events, and rolls authority when a
// worker is lost.
//
// Network I/O (accept, recv, send) is never performed while the ledger/state
// lock is held: reader threads only read frames and push work to a single
// accounting thread, which validates and appends under the lock and then
// releases before sending acknowledgements.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "inference-ledger/authority_state.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/protocol.hpp"

namespace iledger {

class Coordinator {
 public:
  explicit Coordinator(LedgerId ledger_id);
  ~Coordinator();

  bool start(std::uint16_t port, std::string& err);
  void stop();

  // Roll authority (epoch + accounting generation). Used on worker loss or an
  // explicit authority adoption.
  void advance_epoch();

  // Register a request authority (used internally on RequestStart and exposed
  // for deterministic tests).
  void set_request_authority(const LedgerEntry& e);

  AuthorityState authority() const;
  const Ledger& ledger() const { return ledger_; }
  std::uint16_t port() const noexcept { return port_; }
  std::size_t connected_workers() const;

  void enqueue_event(const LedgerEntry& e);

 private:
  struct Conn {
    std::shared_ptr<FramedSocket> sock;
    WorkerId worker{};
    bool registered = false;
  };

  void accept_loop();
  void read_loop(std::shared_ptr<Conn> conn);
  void accounting_loop();

  void handle_registration(Conn& conn, const RegisterMsg& reg);
  void handle_event(Conn& conn, const LedgerEntry& e);
  void handle_worker_lost(const WorkerId& worker);

  struct WorkItem {
    enum class Kind { Event, Register, WorkerLost } kind = Kind::Event;
    std::shared_ptr<Conn> conn;
    LedgerEntry entry{};
    RegisterMsg reg{};
  };
  void enqueue(WorkItem w);

  LedgerId ledger_id_;
  Ledger ledger_;
  mutable std::mutex state_mu_;
  AuthorityState authority_;
  std::set<RequestId> in_flight_;  // requests with an open RequestStart

  TcpServer server_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::uint16_t port_ = 0;

  mutable std::mutex conns_mu_;
  std::vector<std::shared_ptr<Conn>> all_conns_;

  std::mutex q_mu_;
  std::condition_variable q_cv_;
  std::deque<WorkItem> queue_;

  std::thread accept_thread_;
  std::thread accounting_thread_;
};

}  // namespace iledger
