// ============================================================
//  ws_server.cpp  —  Boost.Beast async + SO_REUSEADDR + port 0
//
//  FIX : l'état initial est envoyé DANS on_accept(), après le
//  handshake WebSocket. Appeler async_write avant async_accept
//  sur le même stream cause un blocage silencieux côté client.
// ============================================================
#include "ws_server.h"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

namespace beast = boost::beast;
namespace ws    = beast::websocket;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

// ─── Session ──────────────────────────────────────────────
class Session : public std::enable_shared_from_this<Session> {
    ws::stream<beast::tcp_stream> ws_;
    beast::flat_buffer            buf_;
    std::mutex                    write_mtx_;
    std::atomic<bool>             closing_{false};
    std::function<void(Session*)> on_close_;
    std::string                   initial_msg_; // envoyé après handshake

public:
    explicit Session(tcp::socket&& sock,
                     std::function<void(Session*)> on_close,
                     std::string initial_msg)
            : ws_(std::move(sock))
            , on_close_(std::move(on_close))
            , initial_msg_(std::move(initial_msg)) {}

    void start() {
        ws_.set_option(ws::stream_base::decorator([](ws::response_type& res){
            res.set(boost::beast::http::field::server, "SwingBot/2.0");
        }));
        ws_.async_accept(beast::bind_front_handler(
                &Session::on_accept, shared_from_this()));
    }

    void send(const std::string& msg) {
        std::lock_guard<std::mutex> lk(write_mtx_);
        if (closing_) return;
        auto buf = std::make_shared<std::string>(msg);
        ws_.async_write(net::buffer(*buf),
                        [self = shared_from_this(), buf](beast::error_code ec, std::size_t) {
                            if (ec) self->close();
                        });
    }

    void close() {
        bool was = closing_.exchange(true);
        if (!was && on_close_) on_close_(this);
    }

private:
    void on_accept(beast::error_code ec) {
        if (ec) { close(); return; }
        // FIX : envoyer l'état initial APRÈS le handshake
        if (!initial_msg_.empty())
            send(initial_msg_);
        do_read();
    }

    void do_read() {
        ws_.async_read(buf_,
                       beast::bind_front_handler(&Session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t) {
        if (ec == ws::error::closed || ec) { close(); return; }
        buf_.consume(buf_.size());
        do_read();
    }
};

// ─── Impl ─────────────────────────────────────────────────
struct WsServer::Impl {
    net::io_context    ioc_;
    tcp::acceptor      acceptor_;
    BotState&          state_;
    std::thread        io_thread_;
    mutable std::mutex sessions_mtx_;
    std::set<std::shared_ptr<Session>> sessions_;

    Impl(unsigned short port, BotState& st)
            : ioc_(1), acceptor_(ioc_), state_(st)
    {
        tcp::endpoint endpoint(tcp::v4(), port);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(net::socket_base::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen(net::socket_base::max_listen_connections);
    }

    void start() {
        do_accept();
        io_thread_ = std::thread([this]{ ioc_.run(); });
        std::cout << "[WsServer] Port " << actual_port() << "\n";
    }

    void stop() {
        ioc_.stop();
        if (io_thread_.joinable()) io_thread_.join();
    }

    unsigned short actual_port() const {
        return acceptor_.local_endpoint().port();
    }

    void do_accept() {
        acceptor_.async_accept(net::make_strand(ioc_),
                               [this](beast::error_code ec, tcp::socket sock) {
                                   if (!ec) {
                                       // Capture l'état courant pour l'envoyer après le handshake
                                       auto sess = std::make_shared<Session>(
                                               std::move(sock),
                                               [this](Session* s){ remove_session(s); },
                                               state_.to_json_str()  // snapshot initial
                                       );
                                       {
                                           std::lock_guard<std::mutex> lk(sessions_mtx_);
                                           sessions_.insert(sess);
                                       }
                                       // start() fait le handshake, puis on_accept() envoie initial_msg_
                                       sess->start();
                                   }
                                   do_accept();
                               });
    }

    void broadcast(const std::string& msg) {
        std::lock_guard<std::mutex> lk(sessions_mtx_);
        for (auto& s : sessions_) s->send(msg);
    }

    void remove_session(Session* raw) {
        std::lock_guard<std::mutex> lk(sessions_mtx_);
        for (auto it = sessions_.begin(); it != sessions_.end(); ++it)
            if (it->get() == raw) { sessions_.erase(it); break; }
    }

    size_t client_count() const {
        std::lock_guard<std::mutex> lk(sessions_mtx_);
        return sessions_.size();
    }
};

// ─── API publique ──────────────────────────────────────────
WsServer::WsServer(unsigned short port, BotState& state)
        : pimpl_(std::make_unique<Impl>(port, state)) {}
WsServer::~WsServer() { stop(); }
void           WsServer::start()                         { pimpl_->start();               }
void           WsServer::stop()                          { pimpl_->stop();                }
void           WsServer::broadcast(const std::string& j) { pimpl_->broadcast(j);          }
size_t         WsServer::client_count() const            { return pimpl_->client_count(); }
unsigned short WsServer::actual_port()  const            { return pimpl_->actual_port();  }