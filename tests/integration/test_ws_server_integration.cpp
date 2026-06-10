// ============================================================
//  test_ws_server_integration.cpp  —  Tests INTÉGRATION
//
//  Indépendance garantie par :
//    - port=0  → l'OS assigne un port libre unique à chaque processus
//    - actual_port() → le client connaît le bon port
//    - SO_REUSEADDR dans ws_server.cpp → port libéré immédiatement
//    - gtest_discover_tests → chaque test = processus séparé
//
//  Résultat : zéro conflit de port, zéro état partagé entre tests
// ============================================================
#include <gtest/gtest.h>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
#include "core/ws_server.h"
#include "core/bot_state.h"

namespace beast = boost::beast;
namespace ws_ns = beast::websocket;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;
using json      = nlohmann::json;
using namespace std::chrono_literals;

// ── Client de test ────────────────────────────────────────
struct TestClient {
    static bool connect_and_read(unsigned short port, std::string& out,
                                 int retries = 10) {
        for (int i = 0; i < retries; ++i) {
            try {
                net::io_context ioc;
                tcp::resolver resolver(ioc);
                ws_ns::stream<beast::tcp_stream> ws(ioc);
                beast::get_lowest_layer(ws).expires_after(5s);
                beast::get_lowest_layer(ws).connect(
                        resolver.resolve("127.0.0.1", std::to_string(port)));
                beast::get_lowest_layer(ws).expires_never();
                ws.handshake("127.0.0.1", "/");
                beast::flat_buffer buf;
                ws.read(buf);
                out = beast::buffers_to_string(buf.data());
                ws.close(ws_ns::close_code::normal);
                return true;
            } catch (...) {
                std::this_thread::sleep_for(100ms);
            }
        }
        return false;
    }
};

// ── Fixture ───────────────────────────────────────────────
// port=0 → OS choisit un port libre → unique par processus → indépendant
class WsServerIntegration : public ::testing::Test {
protected:
    BotState       state_;
    WsServer*      server_ = nullptr;
    unsigned short port_   = 0;

    void SetUp() override {
        state_.equity  = 10000.0;
        state_.dry_run = true;
        state_.mode    = "paper";
        server_ = new WsServer(0, state_); // 0 = port libre choisi par l'OS
        server_->start();
        port_ = server_->actual_port();    // récupère le port assigné
        std::this_thread::sleep_for(100ms);
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
            delete server_;
            server_ = nullptr;
        }
        std::this_thread::sleep_for(100ms);
    }
};

// ─────────────────────────────────────────────────────────

TEST_F(WsServerIntegration, ClientCanConnect) {
std::string msg;
EXPECT_TRUE(TestClient::connect_and_read(port_, msg));
}

TEST_F(WsServerIntegration, ClientReceivesValidJson) {
std::string msg;
ASSERT_TRUE(TestClient::connect_and_read(port_, msg));
EXPECT_NO_THROW(json::parse(msg));
}

TEST_F(WsServerIntegration, InitialStateHasCorrectFields) {
std::string msg;
ASSERT_TRUE(TestClient::connect_and_read(port_, msg));
json j = json::parse(msg);
EXPECT_EQ(j["type"],  "state");
EXPECT_EQ(j["mode"],  "paper");
EXPECT_DOUBLE_EQ(j["equity"].get<double>(), 10000.0);
EXPECT_TRUE(j["dry_run"].get<bool>());
}

TEST_F(WsServerIntegration, BroadcastReceivedByClient) {
std::atomic<bool> received{false};
std::atomic<bool> client_ready{false};
std::string       received_msg;

std::thread client_thread([&](){
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        ws_ns::stream<beast::tcp_stream> ws(ioc);
        beast::get_lowest_layer(ws).expires_after(10s);
        beast::get_lowest_layer(ws).connect(
                resolver.resolve("127.0.0.1", std::to_string(port_)));
        beast::get_lowest_layer(ws).expires_never();
        ws.handshake("127.0.0.1", "/");
        beast::flat_buffer b1; ws.read(b1); // état initial
        client_ready = true;                // prêt pour le broadcast
        beast::flat_buffer b2; ws.read(b2); // attend le broadcast
        received_msg = beast::buffers_to_string(b2.data());
        received = true;
        ws.close(ws_ns::close_code::normal);
    } catch (...) { client_ready = true; }
});

// Broadcast dès que le client a reçu l'état initial
for (int i = 0; i < 100 && !client_ready; ++i)
std::this_thread::sleep_for(50ms);
server_->broadcast("{\"type\":\"state\",\"equity\":11500.0}");
client_thread.join();

EXPECT_TRUE(received.load());
json j = json::parse(received_msg);
EXPECT_DOUBLE_EQ(j["equity"].get<double>(), 11500.0);
}

TEST_F(WsServerIntegration, BroadcastUpdatedState) {
{
std::lock_guard<std::mutex> lk(state_.mtx);
state_.equity = 11500.0;
state_.cycle  = 3;
}
json j = json::parse(state_.to_json_str());
EXPECT_DOUBLE_EQ(j["equity"].get<double>(), 11500.0);
EXPECT_EQ(j["cycle"].get<int>(), 3);
EXPECT_NO_THROW(server_->broadcast(state_.to_json_str()));
}

TEST_F(WsServerIntegration, ThreeClientsConnectSuccessfully) {
std::atomic<int> success{0};
std::vector<std::thread> threads;
for (int i = 0; i < 3; ++i)
threads.emplace_back([&](){
std::string msg;
if (TestClient::connect_and_read(port_, msg)) success++;
});
for (auto& t : threads) t.join();
EXPECT_EQ(success.load(), 3);
}

TEST_F(WsServerIntegration, BroadcastReachesAllClients) {
constexpr int N = 3;
std::atomic<int> connected{0};
std::atomic<int> received{0};
std::vector<std::thread> threads;

for (int i = 0; i < N; ++i)
threads.emplace_back([&](){
try {
net::io_context ioc;
tcp::resolver resolver(ioc);
ws_ns::stream<beast::tcp_stream> ws(ioc);
beast::get_lowest_layer(ws).expires_after(10s);
beast::get_lowest_layer(ws).connect(
        resolver.resolve("127.0.0.1", std::to_string(port_)));
beast::get_lowest_layer(ws).expires_never();
ws.handshake("127.0.0.1", "/");
beast::flat_buffer b1; ws.read(b1);
connected++;
beast::flat_buffer b2; ws.read(b2);
received++;
ws.close(ws_ns::close_code::normal);
} catch (...) { connected++; }
});

// Broadcast dès que tous les clients sont prêts
for (int i = 0; i < 100 && connected.load() < N; ++i)
std::this_thread::sleep_for(50ms);
server_->broadcast(state_.to_json_str());
for (auto& t : threads) t.join();
EXPECT_EQ(received.load(), N);
}

// ── Sprint 2, D4 : broadcasts rapprochés → file d'écriture ──
// BUG : Session::send lançait async_write sans file d'attente — deux
// broadcasts rapprochés écrivaient en concurrence sur le même stream
// WebSocket (UB Beast). Avec la file, tous les messages arrivent, dans
// l'ordre d'émission. Les gros payloads forcent le chevauchement.
TEST_F(WsServerIntegration, RapidBroadcastsAllDeliveredInOrder) {
    constexpr int N = 20;
    const std::string padding(256 * 1024, 'x');   // écritures longues

    std::atomic<bool> ready{false};
    std::atomic<int>  received{0};
    std::vector<int>  order;

    std::thread client([&](){
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            ws_ns::stream<beast::tcp_stream> ws(ioc);
            beast::get_lowest_layer(ws).expires_after(10s);
            beast::get_lowest_layer(ws).connect(
                    resolver.resolve("127.0.0.1", std::to_string(port_)));
            beast::get_lowest_layer(ws).expires_never();
            ws.handshake("127.0.0.1", "/");
            beast::flat_buffer b1; ws.read(b1);   // état initial
            ready = true;
            for (int i = 0; i < N; ++i) {
                beast::flat_buffer b; ws.read(b);
                json j = json::parse(beast::buffers_to_string(b.data()));
                order.push_back(j["seq"].get<int>());
                received++;
            }
            ws.close(ws_ns::close_code::normal);
        } catch (...) { ready = true; }
    });

    for (int i = 0; i < 100 && !ready; ++i)
        std::this_thread::sleep_for(50ms);

    // Salve de broadcasts sans aucune pause : chevauchement garanti
    for (int i = 0; i < N; ++i)
        server_->broadcast("{\"seq\":" + std::to_string(i)
                           + ",\"pad\":\"" + padding + "\"}");
    client.join();

    ASSERT_EQ(received.load(), N);
    for (int i = 0; i < N; ++i)
        EXPECT_EQ(order[i], i) << "message " << i << " hors d'ordre";
}

TEST_F(WsServerIntegration, ClientBrutalDisconnectNoServerCrash) {
std::thread([&](){
try {
net::io_context ioc;
tcp::resolver resolver(ioc);
ws_ns::stream<beast::tcp_stream> ws(ioc);
beast::get_lowest_layer(ws).expires_after(5s);
beast::get_lowest_layer(ws).connect(
        resolver.resolve("127.0.0.1", std::to_string(port_)));
ws.handshake("127.0.0.1", "/");
// Déconnexion brutale sans close propre
} catch (...) {}
}).join();

std::this_thread::sleep_for(200ms);
EXPECT_NO_THROW(server_->broadcast("{\"test\":true}"));
std::string msg;
EXPECT_TRUE(TestClient::connect_and_read(port_, msg));
}
// ════════════════════════════════════════════════════════════
//  Messages entrants du client — le serveur lit, ignore et survit
// ════════════════════════════════════════════════════════════

// Un client qui PARLE au serveur (le dashboard pourrait envoyer un ping) :
// le serveur consomme le message sans broadcaster ni crasher, et continue
// de servir les broadcasts suivants
TEST_F(WsServerIntegration, ClientMessageConsumedWithoutCrash) {
    net::io_context ioc;
    tcp::resolver resolver(ioc);
    ws_ns::stream<beast::tcp_stream> ws(ioc);
    beast::get_lowest_layer(ws).expires_after(5s);
    beast::get_lowest_layer(ws).connect(
            resolver.resolve("127.0.0.1", std::to_string(port_)));
    beast::get_lowest_layer(ws).expires_never();
    ws.handshake("127.0.0.1", "/");

    // état initial poussé à la connexion
    beast::flat_buffer buf;
    ws.read(buf);

    // le client envoie deux messages que le serveur doit absorber
    ws.write(net::buffer(std::string(R"({"type":"ping"})")));
    ws.write(net::buffer(std::string("bonjour")));
    std::this_thread::sleep_for(200ms);

    // le serveur fonctionne toujours : un broadcast arrive entier
    server_->broadcast(R"({"type":"state","apres":"messages"})");
    buf.consume(buf.size());
    ws.read(buf);
    EXPECT_NE(beast::buffers_to_string(buf.data()).find("apres"),
              std::string::npos);

    ws.close(ws_ns::close_code::normal);
    std::this_thread::sleep_for(100ms);   // laisse on_read voir la fermeture
}
