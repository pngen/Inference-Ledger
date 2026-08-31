#include "inference-ledger/coordinator.hpp"

#include <utility>

namespace iledger {

Coordinator::Coordinator(LedgerId ledger_id)
    : ledger_id_(ledger_id), ledger_(ledger_id) {
  authority_.epoch = CoordinatorEpoch(1);
  authority_.accounting_generation = AccountingGeneration(1);
  ledger_.attach_authority(&authority_);
}

Coordinator::~Coordinator() { stop(); }

bool Coordinator::start(std::uint16_t port, std::string& err) {
  if (!network_init(err)) return false;
  if (!server_.bind_and_listen("127.0.0.1", port, err)) return false;
  // On Windows the bound port is what we asked for.
  port_ = port;
  running_.store(true);
  accept_thread_ = std::thread([this] { accept_loop(); });
  accounting_thread_ = std::thread([this] { accounting_loop(); });
  return true;
}

void Coordinator::stop() {
  stop_requested_.store(true);
  running_.store(false);
  server_.close();
  // Close every open connection so blocking reads return.
  {
    std::lock_guard<std::mutex> lk(conns_mu_);
    for (const auto& c : all_conns_) {
      if (c->sock) c->sock->close();
    }
  }
  q_cv_.notify_all();
  if (accept_thread_.joinable()) accept_thread_.join();
  if (accounting_thread_.joinable()) accounting_thread_.join();
}

void Coordinator::advance_epoch() {
  std::lock_guard<std::mutex> lk(state_mu_);
  authority_.epoch = authority_.epoch.next();
  authority_.accounting_generation = authority_.accounting_generation.next();
}

void Coordinator::set_request_authority(const LedgerEntry& e) {
  std::lock_guard<std::mutex> lk(state_mu_);
  authority_.request_generation[e.request] = e.authority.request_generation;
  authority_.current_attempt[e.request] = e.authority.attempt;
  authority_.attempt_generation[e.request] = e.authority.attempt_generation;
  authority_.request_rolled[e.request] = false;
}

AuthorityState Coordinator::authority() const {
  std::lock_guard<std::mutex> lk(state_mu_);
  return authority_;
}

std::size_t Coordinator::connected_workers() const {
  std::lock_guard<std::mutex> lk(conns_mu_);
  std::size_t n = 0;
  for (const auto& c : all_conns_) {
    if (c->registered) ++n;
  }
  return n;
}

void Coordinator::enqueue_event(const LedgerEntry& e) {
  WorkItem w;
  w.kind = WorkItem::Kind::Event;
  w.entry = e;
  enqueue(std::move(w));
}

void Coordinator::enqueue(WorkItem w) {
  {
    std::lock_guard<std::mutex> lk(q_mu_);
    queue_.push_back(std::move(w));
  }
  q_cv_.notify_one();
}

void Coordinator::accept_loop() {
  while (running_.load()) {
    SOCKET raw = static_cast<SOCKET>(~0);
    std::string err;
    if (!server_.accept(raw, err)) {
      if (stop_requested_.load()) break;
      continue;
    }
    auto conn = std::make_shared<Conn>();
    conn->sock = std::make_shared<FramedSocket>();
    conn->sock->adopt(raw);
    {
      std::lock_guard<std::mutex> lk(conns_mu_);
      all_conns_.push_back(conn);
    }
    std::thread([this, conn] { read_loop(conn); }).detach();
  }
}

void Coordinator::read_loop(std::shared_ptr<Conn> conn) {
  while (running_.load()) {
    Message msg;
    std::string err;
    if (!conn->sock->recv_message(msg, err)) {
      // Worker loss (disconnect) or error.
      if (conn->registered) {
        WorkItem w;
        w.kind = WorkItem::Kind::WorkerLost;
        w.conn = conn;
        w.reg.worker = conn->worker;
        enqueue(std::move(w));
      }
      return;  // signal and consume; this thread is done with this connection
    }
    switch (msg.type) {
      case MessageType::Register: {
        WorkItem w;
        w.kind = WorkItem::Kind::Register;
        w.conn = conn;
        w.reg = msg.reg;
        enqueue(std::move(w));
        break;
      }
      case MessageType::EventSubmit: {
        WorkItem w;
        w.kind = WorkItem::Kind::Event;
        w.conn = conn;
        w.entry = msg.event.entry;
        enqueue(std::move(w));
        break;
      }
      case MessageType::Ping: {
        Message pong;
        pong.type = MessageType::Ping;
        std::string perr;
        conn->sock->send_message(pong, perr);
        break;
      }
      default:
        break;
    }
  }
}

void Coordinator::accounting_loop() {
  while (true) {
    WorkItem w;
    {
      std::unique_lock<std::mutex> lk(q_mu_);
      q_cv_.wait(lk, [this] { return !queue_.empty() || stop_requested_.load(); });
      if (stop_requested_.load() && queue_.empty()) return;
      w = std::move(queue_.front());
      queue_.pop_front();
    }
    if (w.kind == WorkItem::Kind::Register) {
      handle_registration(*w.conn, w.reg);
    } else if (w.kind == WorkItem::Kind::Event) {
      handle_event(*w.conn, w.entry);
    } else if (w.kind == WorkItem::Kind::WorkerLost) {
      handle_worker_lost(w.reg.worker);
    }
  }
}

void Coordinator::handle_registration(Conn& conn, const RegisterMsg& reg) {
  CoordinatorEpoch epoch;
  AccountingGeneration ag;
  {
    std::lock_guard<std::mutex> lk(state_mu_);
    authority_.worker_boot[reg.worker] = reg.boot;
    epoch = authority_.epoch;
    ag = authority_.accounting_generation;
  }
  conn.worker = reg.worker;
  conn.registered = true;
  Message ack;
  ack.type = MessageType::RegisterAck;
  ack.reg_ack.ok = true;
  ack.reg_ack.epoch = epoch;
  ack.reg_ack.accounting_generation = ag;
  ack.reg_ack.reason = "registered";
  std::string err;
  conn.sock->send_message(ack, err);
}

void Coordinator::handle_event(Conn& conn, const LedgerEntry& e) {
  AppendResult ar;
  {
    std::lock_guard<std::mutex> lk(state_mu_);
    if (e.event_kind == EventKind::RequestStart) {
      authority_.request_generation[e.request] = e.authority.request_generation;
      authority_.current_attempt[e.request] = e.authority.attempt;
      authority_.attempt_generation[e.request] = e.authority.attempt_generation;
      authority_.request_rolled[e.request] = false;
    }
    ar = ledger_.append(e);
    if (ar.status == AppendStatus::Accepted) {
      if (e.event_kind == EventKind::RequestStart) in_flight_.insert(e.request);
      else if (e.event_kind == EventKind::RequestEnd) in_flight_.erase(e.request);
    }
  }
  Message ack;
  ack.type = MessageType::EventAck;
  ack.event_ack.status = ar.status;
  ack.event_ack.reason = ar.reason;
  std::string err;
  conn.sock->send_message(ack, err);
}

void Coordinator::handle_worker_lost(const WorkerId& worker) {
  // Roll authority so any in-flight event from the lost worker's old boot is
  // rejected as stale. Mark all in-flight requests as rolled until a fresh
  // RequestStart adopts new authority.
  {
    std::lock_guard<std::mutex> lk(state_mu_);
    for (const auto& req : in_flight_) authority_.request_rolled[req] = true;
  }
  advance_epoch();
  // Gather surviving sockets under the connection lock, release, then send.
  std::vector<std::shared_ptr<FramedSocket>> targets;
  {
    std::lock_guard<std::mutex> lk(conns_mu_);
    for (const auto& c : all_conns_) {
      if (!c->registered) continue;
      if (c->worker == worker) continue;
      targets.push_back(c->sock);
    }
  }
  CoordinatorEpoch epoch;
  AccountingGeneration ag;
  {
    std::lock_guard<std::mutex> slk(state_mu_);
    epoch = authority_.epoch;
    ag = authority_.accounting_generation;
  }
  Message roll;
  roll.type = MessageType::Roll;
  roll.roll.epoch = epoch;
  roll.roll.accounting_generation = ag;
  for (const auto& t : targets) {
    std::string err;
    t->send_message(roll, err);
  }
}

}  // namespace iledger
