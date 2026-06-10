// ============================================================
//  test_http_client_unit.cpp  —  Tests UNITAIRES
//  Cible : core/HttpClient.hpp — codes HTTP, retry, backoff, 429
//  (Sprint 2, item 8 — aucun réseau : performOnce scripté)
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include "core/HttpClient.hpp"

using namespace trading;

namespace {

// ── Client scripté : réponses pré-programmées, sommeil enregistré ──
class ScriptedHttpClient : public HttpClient {
public:
    using HttpClient::HttpClient;

    // File de réponses : statut < 0 simule une erreur transport
    std::vector<HttpResponse> script;
    int attempts = 0;
    std::vector<long> sleeps_ms;

protected:
    HttpResponse performOnce(const std::string&, const std::string&,
                             const std::string&,
                             const std::vector<std::string>&) override {
        size_t i = static_cast<size_t>(attempts++);
        HttpResponse r = i < script.size() ? script[i] : script.back();
        if (r.status < 0)
            throw std::runtime_error("erreur transport simulée");
        return r;
    }

    void sleepFor(std::chrono::milliseconds ms) override {
        sleeps_ms.push_back(ms.count());   // pas de vrai sommeil
    }
};

HttpClientConfig fastCfg() {
    HttpClientConfig cfg;
    cfg.max_retries        = 3;
    cfg.initial_backoff_ms = 100;
    return cfg;
}

} // namespace

// ── Succès ────────────────────────────────────────────────

TEST(HttpClientUnit, SuccessReturnsBodyWithoutRetry) {
    ScriptedHttpClient c(fastCfg());
    c.script = {{200, R"({"ok":true})"}};

    EXPECT_EQ(c.request("GET", "https://x/api"), R"({"ok":true})");
    EXPECT_EQ(c.attempts, 1);
    EXPECT_TRUE(c.sleeps_ms.empty());
}

// ── Erreurs transitoires : retry + backoff exponentiel ────

TEST(HttpClientUnit, ServerErrorRetriedWithExponentialBackoff) {
    ScriptedHttpClient c(fastCfg());
    c.script = {{500, ""}, {503, ""}, {200, "ok"}};

    EXPECT_EQ(c.request("GET", "https://x/api"), "ok");
    EXPECT_EQ(c.attempts, 3);
    // Backoff exponentiel : 100 ms puis 200 ms
    ASSERT_EQ(c.sleeps_ms.size(), 2u);
    EXPECT_EQ(c.sleeps_ms[0], 100);
    EXPECT_EQ(c.sleeps_ms[1], 200);
}

TEST(HttpClientUnit, RateLimit429IsRetried) {
    ScriptedHttpClient c(fastCfg());
    c.script = {{429, "Too Many Requests"}, {200, "ok"}};

    EXPECT_EQ(c.request("POST", "https://x/api", "{}"), "ok");
    EXPECT_EQ(c.attempts, 2);
    ASSERT_EQ(c.sleeps_ms.size(), 1u);
    EXPECT_EQ(c.sleeps_ms[0], 100);
}

TEST(HttpClientUnit, TransportErrorIsRetried) {
    ScriptedHttpClient c(fastCfg());
    c.script = {{-1, ""}, {200, "ok"}};

    EXPECT_EQ(c.request("GET", "https://x/api"), "ok");
    EXPECT_EQ(c.attempts, 2);
}

// ── Erreurs définitives : pas de retry ────────────────────

TEST(HttpClientUnit, ClientError404ThrowsWithoutRetry) {
    ScriptedHttpClient c(fastCfg());
    c.script = {{404, "Not Found"}};

    EXPECT_THROW(c.request("GET", "https://x/api"), std::runtime_error);
    EXPECT_EQ(c.attempts, 1);
    EXPECT_TRUE(c.sleeps_ms.empty());
}

TEST(HttpClientUnit, ClientError401ThrowsWithoutRetry) {
    ScriptedHttpClient c(fastCfg());
    c.script = {{401, "Unauthorized"}};

    EXPECT_THROW(c.request("GET", "https://x/api"), std::runtime_error);
    EXPECT_EQ(c.attempts, 1);
}

// ── Épuisement des essais ─────────────────────────────────

TEST(HttpClientUnit, PersistentServerErrorExhaustsRetriesThenThrows) {
    ScriptedHttpClient c(fastCfg());
    c.script = {{500, ""}};   // toujours 500

    EXPECT_THROW(c.request("GET", "https://x/api"), std::runtime_error);
    // 1 essai initial + max_retries re-tentatives
    EXPECT_EQ(c.attempts, 4);
    EXPECT_EQ(c.sleeps_ms.size(), 3u);
}

TEST(HttpClientUnit, PersistentTransportErrorExhaustsRetriesThenThrows) {
    ScriptedHttpClient c(fastCfg());
    c.script = {{-1, ""}};   // transport KO en continu

    EXPECT_THROW(c.request("GET", "https://x/api"), std::runtime_error);
    EXPECT_EQ(c.attempts, 4);
}
