// ============================================================
//  test_http_client_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : HttpClient — transport curl RÉEL contre un serveur
//  HTTP local (MiniHttpServer, port éphémère). Complète les
//  tests unitaires qui scriptent performOnce.
// ============================================================
#include <gtest/gtest.h>
#include "core/HttpClient.hpp"
#include "core/curl_global.h"
#include "../support/MiniHttpServer.hpp"

using namespace trading;

namespace {

// libcurl initialisé une fois pour tout le binaire de test
CurlGlobal curlGlobal;

HttpClientConfig quickCfg(int retries = 0) {
    HttpClientConfig cfg;
    cfg.timeout_sec        = 5;
    cfg.max_retries        = retries;
    cfg.initial_backoff_ms = 1;     // backoff réel mais négligeable
    return cfg;
}

} // namespace

TEST(HttpClientIntegration, RealGetReturnsBody) {
    MiniHttpServer server(200, R"({"ok":true})");
    HttpClient client(quickCfg());

    EXPECT_EQ(client.get(server.url("/sante")), R"({"ok":true})");

    ASSERT_EQ(server.requestCount(), 1u);
    EXPECT_NE(server.requests()[0].find("GET /sante"), std::string::npos);
}

TEST(HttpClientIntegration, RealPostTransmitsBodyAndHeaders) {
    MiniHttpServer server(200, "recu");
    HttpClient client(quickCfg());

    auto resp = client.post(server.url("/ordres"), R"({"qty":9})",
                            {"X-Test: oui", "Content-Type: application/json"});
    EXPECT_EQ(resp, "recu");

    ASSERT_EQ(server.requestCount(), 1u);
    const std::string raw = server.requests()[0];
    EXPECT_NE(raw.find("POST /ordres"),  std::string::npos);
    EXPECT_NE(raw.find(R"({"qty":9})"),  std::string::npos);
    EXPECT_NE(raw.find("X-Test: oui"),   std::string::npos);
}

TEST(HttpClientIntegration, RealDeleteUsesCustomMethod) {
    MiniHttpServer server(200, "");
    HttpClient client(quickCfg());

    client.del(server.url("/ordres"));

    ASSERT_EQ(server.requestCount(), 1u);
    EXPECT_NE(server.requests()[0].find("DELETE /ordres"), std::string::npos);
}

// 4xx définitif : HttpError porteuse du statut, sans retry
TEST(HttpClientIntegration, Real404ThrowsHttpErrorWithoutRetry) {
    MiniHttpServer server(404, "pas la");
    HttpClient client(quickCfg(/*retries=*/2));

    try {
        client.get(server.url("/inconnu"));
        FAIL() << "HttpError attendue";
    } catch (const HttpError& e) {
        EXPECT_EQ(e.status(), 404);
    }
    EXPECT_EQ(server.requestCount(), 1u);   // aucun retry sur 4xx
}

// 5xx : retry réel (avec vrai backoff d'1 ms) jusqu'à épuisement
TEST(HttpClientIntegration, Real500RetriedThenThrows) {
    MiniHttpServer server(500, "boom");
    HttpClient client(quickCfg(/*retries=*/2));

    EXPECT_THROW(client.get(server.url("/")), HttpError);
    EXPECT_EQ(server.requestCount(), 3u);   // 1 essai + 2 retries
}

// Port fermé : erreur transport (CURLE_COULDNT_CONNECT) → HttpError statut 0
TEST(HttpClientIntegration, ConnectionRefusedThrowsTransportError) {
    // Réserve un port puis le libère : plus personne n'écoute dessus
    int port;
    {
        MiniHttpServer tmp(200, "");
        port = tmp.port();
    }
    HttpClient client(quickCfg());

    try {
        client.get("http://127.0.0.1:" + std::to_string(port) + "/");
        FAIL() << "HttpError attendue";
    } catch (const HttpError& e) {
        EXPECT_EQ(e.status(), 0);           // 0 = panne transport, pas un statut
    }
}
