// ============================================================
//  test_alpaca_data_feed_unit.cpp  —  Tests UNITAIRES
//  Cible : AlpacaDataFeed — parsing des barres et canal Result
//  (HTTP substitué : aucun réseau, réponses Alpaca scriptées)
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include <utility>
#include "brokers/AlpacaDataFeed.hpp"

using namespace trading;

namespace {

class ScriptedAlpacaDataFeed : public AlpacaDataFeed {
public:
    ScriptedAlpacaDataFeed() : AlpacaDataFeed("cle", "secret", true) {}

    struct Call {
        std::string url;
        std::vector<std::string> headers;
    };

    std::vector<Call> calls;
    std::vector<std::pair<std::string, std::string>> responses;
    std::optional<std::pair<long, std::string>> httpFailure;

protected:
    std::string httpRequest(const std::string& /*method*/,
                            const std::string& url,
                            const std::string& /*body*/,
                            const std::vector<std::string>& headers) override {
        calls.push_back({url, headers});
        if (httpFailure)
            throw HttpError(httpFailure->first, httpFailure->second);
        for (const auto& [pattern, resp] : responses)
            if (url.find(pattern) != std::string::npos) return resp;
        return "{}";
    }
};

} // namespace

// ════════════════════════════════════════════════════════════
//  getBars — parsing
// ════════════════════════════════════════════════════════════

TEST(AlpacaDataFeedUnit, BarsParsedWithIsoDateTruncated) {
    ScriptedAlpacaDataFeed f;
    f.responses = {{"/v2/stocks/QQQ/bars", R"({"bars":[
        {"t":"2024-01-02T05:00:00Z","o":403.2,"h":404.1,"l":401.5,"c":402.8,"v":45123400},
        {"t":"2024-01-03T05:00:00Z","o":402.9,"h":405.0,"l":402.0,"c":404.6,"v":39000000}
    ]})"}};

    auto r = f.getBars("QQQ", 60);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 2u);

    const Bar& b = r.value().front();
    EXPECT_EQ(b.date, "2024-01-02");          // timestamp ISO tronqué à la date
    EXPECT_DOUBLE_EQ(b.open,  403.2);
    EXPECT_DOUBLE_EQ(b.high,  404.1);
    EXPECT_DOUBLE_EQ(b.low,   401.5);
    EXPECT_DOUBLE_EQ(b.close, 402.8);
    EXPECT_EQ(b.volume, 45'123'400L);
}

// Barres sans close exploitable (c <= 0) → filtrées
TEST(AlpacaDataFeedUnit, BarsWithoutValidCloseSkipped) {
    ScriptedAlpacaDataFeed f;
    f.responses = {{"/bars", R"({"bars":[
        {"t":"2024-01-02T05:00:00Z","c":0.0},
        {"t":"2024-01-03T05:00:00Z","c":404.6}
    ]})"}};

    auto r = f.getBars("QQQ", 60);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value().front().date, "2024-01-03");
}

// Réponse sans clé "bars" : Ok(vide) — pas de donnée n'est PAS une panne
TEST(AlpacaDataFeedUnit, MissingBarsKeyIsOkEmpty) {
    ScriptedAlpacaDataFeed f;
    f.responses = {{"/bars", R"({"next_page_token":null})"}};

    auto r = f.getBars("QQQ", 60);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().empty());
}

// JSON invalide = panne → Err (item 10), jamais Ok(vide)
TEST(AlpacaDataFeedUnit, MalformedJsonIsErr) {
    ScriptedAlpacaDataFeed f;
    f.responses = {{"/bars", "<html>503</html>"}};

    auto r = f.getBars("QQQ", 60);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().find("Alpaca getBars"), std::string::npos);
}

TEST(AlpacaDataFeedUnit, HttpFailureIsErr) {
    ScriptedAlpacaDataFeed f;
    f.httpFailure = {{500, "statut 500"}};

    auto r = f.getBars("QQQ", 60);
    EXPECT_FALSE(r.ok());
}

// Erreur applicative Alpaca dans un corps 2xx (clé "code") → Err
TEST(AlpacaDataFeedUnit, ApplicativeErrorBodyIsErr) {
    ScriptedAlpacaDataFeed f;
    f.responses = {{"/bars", R"({"code":40110000,"message":"access key verification failed"})"}};

    auto r = f.getBars("QQQ", 60);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().find("Alpaca API error"), std::string::npos);
}

// L'URL porte les paramètres du contrat API (timeframe, limit, feed IEX)
TEST(AlpacaDataFeedUnit, RequestUrlCarriesApiParameters) {
    ScriptedAlpacaDataFeed f;
    f.responses = {{"/bars", R"({"bars":[]})"}};
    f.getBars("QQQ", 60);

    ASSERT_EQ(f.calls.size(), 1u);
    const std::string& url = f.calls[0].url;
    EXPECT_NE(url.find("/v2/stocks/QQQ/bars"), std::string::npos);
    EXPECT_NE(url.find("timeframe=1Day"),      std::string::npos);
    EXPECT_NE(url.find("limit=60"),            std::string::npos);
    EXPECT_NE(url.find("feed=iex"),            std::string::npos);
    EXPECT_NE(url.find("sort=asc"),            std::string::npos);
}

TEST(AlpacaDataFeedUnit, AuthHeadersOnEveryCall) {
    ScriptedAlpacaDataFeed f;
    f.responses = {{"/bars", R"({"bars":[]})"}};
    f.getBars("QQQ", 60);

    ASSERT_EQ(f.calls.size(), 1u);
    ASSERT_GE(f.calls[0].headers.size(), 2u);
    EXPECT_NE(f.calls[0].headers[0].find("APCA-API-KEY-ID: cle"),        std::string::npos);
    EXPECT_NE(f.calls[0].headers[1].find("APCA-API-SECRET-KEY: secret"), std::string::npos);
}

// ════════════════════════════════════════════════════════════
//  isMarketOpen — horloge Alpaca
// ════════════════════════════════════════════════════════════

TEST(AlpacaDataFeedUnit, MarketOpenFollowsClock) {
    ScriptedAlpacaDataFeed f;
    f.responses = {{"/v2/clock", R"({"is_open":true})"}};
    EXPECT_TRUE(f.isMarketOpen());

    f.responses = {{"/v2/clock", R"({"is_open":false})"}};
    EXPECT_FALSE(f.isMarketOpen());
}

// Panne de l'horloge → fermé par prudence (pas de trade à l'aveugle)
TEST(AlpacaDataFeedUnit, ClockFailureMeansClosed) {
    ScriptedAlpacaDataFeed f;
    f.httpFailure = {{500, "statut 500"}};
    EXPECT_FALSE(f.isMarketOpen());
}
