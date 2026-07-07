// ============================================================
//  ws_server.cpp  —  Boost.Beast async + SO_REUSEADDR + port 0
//
//  FIX : l'état initial est envoyé DANS on_accept(), après le
//  handshake WebSocket. Appeler async_write avant async_accept
//  sur le même stream cause un blocage silencieux côté client.
//
//  Sonde /healthz (item 17.3) : chaque session lit d'abord la
//  requête HTTP ; upgrade WebSocket → handshake inchangé, sinon
//  GET /healthz → 200 JSON (instantané de santé de BotState) et
//  tout autre chemin → 404. Un orchestrateur peut ainsi voir un
//  process « vivant mais malsain », au-delà du HEALTHCHECK TCP.
// ============================================================
#include "ws_server.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#include <chrono>
#include <deque>
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
namespace http  = beast::http;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

// ─── Session ──────────────────────────────────────────────
class Session : public std::enable_shared_from_this<Session> {
    ws::stream<beast::tcp_stream> ws_;
    beast::flat_buffer            buf_;
    // Requête HTTP lue AVANT le handshake (item 17.3) : upgrade WS ou sonde
    http::request<http::string_body> req_;
    BotState&                     state_;
    // File d'écriture (D4) : une seule async_write en vol à la fois —
    // manipulée exclusivement sur l'executor de la session (via net::post)
    std::deque<std::shared_ptr<std::string>> write_queue_;
    std::atomic<bool>             closing_{false};
    // Garde broadcast (item 17.3) : écrire sur un stream WS non handshaké
    // (session encore en phase HTTP) est un UB Beast — send() ne fait rien
    // tant que le handshake n'est pas terminé.
    std::atomic<bool>             upgraded_{false};
    std::function<void(Session*)> on_close_;
    std::string                   initial_msg_; // envoyé après handshake

public:
    explicit Session(tcp::socket&& sock,
                     BotState& state,
                     std::function<void(Session*)> on_close,
                     std::string initial_msg)
            : ws_(std::move(sock))
            , state_(state)
            , on_close_(std::move(on_close))
            , initial_msg_(std::move(initial_msg)) {}

    void start() {
        // Lecture HTTP d'abord : borne le temps d'une sonde (ou d'une
        // connexion muette) sans affecter les clients WS déjà upgradés
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));
        http::async_read(ws_.next_layer(), buf_, req_,
                         beast::bind_front_handler(&Session::on_http_read,
                                                   shared_from_this()));
    }

    // Thread-safe : poste sur l'executor de la session — la file y est
    // manipulée séquentiellement, jamais deux async_write concurrentes (D4)
    void send(const std::string& msg) {
        if (!upgraded_ || closing_) return;
        auto buf = std::make_shared<std::string>(msg);
        net::post(ws_.get_executor(),
                  [self = shared_from_this(), buf]() mutable {
                      self->enqueue_(std::move(buf));
                  });
    }

    void close() {
        bool was = closing_.exchange(true);
        if (!was && on_close_) on_close_(this);
    }

private:
    void on_http_read(beast::error_code ec, std::size_t) {
        if (ec) { close(); return; }

        if (ws::is_upgrade(req_)) {
            // Le WebSocket gère ses propres timeouts — celui du tcp_stream
            // doit être désarmé avant le handshake (règle Beast)
            beast::get_lowest_layer(ws_).expires_never();
            ws_.set_option(ws::stream_base::decorator([](ws::response_type& res){
                res.set(http::field::server, "SwingBot/2.0");
            }));
            ws_.async_accept(req_, beast::bind_front_handler(
                    &Session::on_accept, shared_from_this()));
            return;
        }

        // HTTP simple : /healthz = instantané de santé, sinon 404
        if (req_.method() == http::verb::get && req_.target() == "/healthz") {
            const long now = static_cast<long>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
            send_http_(http::status::ok, "application/json",
                       state_.health_json(now).dump());
        } else {
            send_http_(http::status::not_found, "text/plain", "introuvable");
        }
    }

    void send_http_(http::status st, const char* content_type,
                    std::string body) {
        auto res = std::make_shared<http::response<http::string_body>>(
            st, req_.version());
        res->set(http::field::server, "SwingBot/2.0");
        res->set(http::field::content_type, content_type);
        res->keep_alive(false);
        res->body() = std::move(body);
        res->prepare_payload();
        // res maintenu vivant par la capture le temps de l'écriture
        http::async_write(ws_.next_layer(), *res,
            [self = shared_from_this(), res](beast::error_code, std::size_t) {
                beast::error_code ig;
                beast::get_lowest_layer(self->ws_).socket().shutdown(
                    tcp::socket::shutdown_send, ig);
                self->close();
            });
    }

    void on_accept(beast::error_code ec) {
        if (ec) { close(); return; }
        upgraded_ = true;   // AVANT l'état initial : send() est gardé dessus
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

    // ── File d'écriture (D4) — tout s'exécute sur l'executor ──
    void enqueue_(std::shared_ptr<std::string> buf) {
        if (closing_) return;
        write_queue_.push_back(std::move(buf));
        if (write_queue_.size() > 1) return;  // une écriture est déjà en vol
        do_write_();
    }

    void do_write_() {
        ws_.async_write(net::buffer(*write_queue_.front()),
                        beast::bind_front_handler(&Session::on_write_,
                                                  shared_from_this()));
    }

    void on_write_(beast::error_code ec, std::size_t) {
        if (ec) { close(); return; }
        write_queue_.pop_front();
        if (!write_queue_.empty()) do_write_();  // message suivant de la file
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
                                               state_,  // sonde /healthz (17.3)
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