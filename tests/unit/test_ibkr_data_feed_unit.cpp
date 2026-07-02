// ============================================================
//  test_ibkr_data_feed_unit.cpp  —  Tests UNITAIRES
//  Cible : IBKRDataFeed — parsing HMDS, conids, horloge marché
//  (HTTP substitué : aucun réseau, réponses du Gateway scriptées)
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include <utility>
#include "brokers/IBKRDataFeed.hpp"

using namespace trading;

namespace {

class ScriptedIbkrDataFeed : public IBKRDataFeed {
public:
    ScriptedIbkrDataFeed() : IBKRDataFeed("https://gw", false) {}

    struct Call {
        std::string method;
        std::string url;
    };

    std::vector<Call> calls;
    std::vector<std::pair<std::string, std::string>> responses;
    bool failAll = false;    // simule une panne réseau sur tous les appels
    std::time_t fakeNow = 0; // horloge substituée (0 = horloge réelle)

protected:
    std::string request(const std::string& method,
                        const std::string& url,
                        const std::string& /*body*/) override {
        calls.push_back({method, url});
        if (failAll) throw HttpError(0, "panne reseau simulee");
        for (const auto& [pattern, resp] : responses)
            if (url.find(pattern) != std::string::npos) return resp;
        return "{}";
    }

    std::time_t now_() const override {
        return fakeNow != 0 ? fakeNow : IBKRDataFeed::now_();
    }
};

// Instant UTC → time_t (même technique que test_market_calendar_unit.cpp)
std::time_t utcInstant(int y, int mon, int d, int h, int mi) {
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mon - 1; tm.tm_mday = d;
    tm.tm_hour = h; tm.tm_min = mi;
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

} // namespace

// ════════════════════════════════════════════════════════════
//  getBars — endpoint HMDS et parsing
// ════════════════════════════════════════════════════════════

// Timestamps IBKR en millisecondes → dates ISO ; 2024-01-02 00:00 UTC
TEST(IbkrDataFeedUnit, BarsParsedWithMsTimestamps) {
    ScriptedIbkrDataFeed f;
    f.responses = {{"/v1/api/hmds/history", R"({"data":[
        {"t":1704153600000,"o":403.2,"h":404.1,"l":401.5,"c":402.8,"v":45123400.0},
        {"t":1704240000000,"o":402.9,"h":405.0,"l":402.0,"c":404.6,"v":39000000.0}
    ]})"}};

    auto r = f.getBars("QQQ", 60);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 2u);

    const Bar& b = r.value().front();
    EXPECT_EQ(b.date, "2024-01-02");
    EXPECT_DOUBLE_EQ(b.open,  403.2);
    EXPECT_DOUBLE_EQ(b.close, 402.8);
    EXPECT_EQ(b.volume, 45'123'400L);
    EXPECT_EQ(r.value().back().date, "2024-01-03");
}

// ════════════════════════════════════════════════════════════
//  9.3 (E6/D25) — le HMDS inclut la barre du jour EN FORMATION
//  pendant la séance : le croisement EMA pouvait osciller
//  intra-journée. Le feed ne doit livrer que des barres CLÔTURÉES.
// ════════════════════════════════════════════════════════════

// La barre datée d'aujourd'hui (heure de l'Est) est exclue
TEST(IbkrDataFeedUnit, TodayFormingBarExcluded) {
    ScriptedIbkrDataFeed f;
    f.fakeNow = utcInstant(2024, 1, 3, 18, 0);   // 13h ET, en séance (hiver)
    f.responses = {{"/v1/api/hmds/history", R"({"data":[
        {"t":1704153600000,"o":403.2,"h":404.1,"l":401.5,"c":402.8,"v":1000.0},
        {"t":1704240000000,"o":402.9,"h":405.0,"l":402.0,"c":404.6,"v":900.0}
    ]})"}};                                       // 2024-01-02 et 2024-01-03

    auto r = f.getBars("QQQ", 60);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 1u);              // la barre du 03 (en formation) est retirée
    EXPECT_EQ(r.value().back().date, "2024-01-02");
}

// Les barres historiques (aucune datée d'aujourd'hui) sont intactes
TEST(IbkrDataFeedUnit, HistoricalBarsUntouched) {
    ScriptedIbkrDataFeed f;
    f.fakeNow = utcInstant(2024, 1, 5, 18, 0);
    f.responses = {{"/v1/api/hmds/history", R"({"data":[
        {"t":1704153600000,"o":403.2,"h":404.1,"l":401.5,"c":402.8,"v":1000.0},
        {"t":1704240000000,"o":402.9,"h":405.0,"l":402.0,"c":404.6,"v":900.0}
    ]})"}};

    auto r = f.getBars("QQQ", 60);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 2u);
}

// Réponse ne contenant QUE la barre du jour → Ok(vide), cycle sauté proprement
TEST(IbkrDataFeedUnit, OnlyTodayBarYieldsEmptyOk) {
    ScriptedIbkrDataFeed f;
    f.fakeNow = utcInstant(2024, 1, 3, 18, 0);
    f.responses = {{"/v1/api/hmds/history", R"({"data":[
        {"t":1704240000000,"o":402.9,"h":405.0,"l":402.0,"c":404.6,"v":900.0}
    ]})"}};

    auto r = f.getBars("QQQ", 60);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().empty());
}

// Le conid connu de QQQ est résolu localement, sans requête de recherche
TEST(IbkrDataFeedUnit, KnownConidResolvedWithoutSearch) {
    ScriptedIbkrDataFeed f;
    f.responses = {{"/hmds/history", R"({"data":[]})"}};
    f.getBars("QQQ", 60);

    ASSERT_EQ(f.calls.size(), 1u);
    EXPECT_NE(f.calls[0].url.find("conid=320227571"), std::string::npos);
    EXPECT_NE(f.calls[0].url.find("period=60d"),      std::string::npos);
    EXPECT_NE(f.calls[0].url.find("bar=1d"),          std::string::npos);
}

// Symbole inconnu → recherche dynamique /secdef/search puis HMDS
TEST(IbkrDataFeedUnit, UnknownSymbolTriggersConidSearch) {
    ScriptedIbkrDataFeed f;
    f.responses = {
        {"/secdef/search", R"([{"conid":123456,"symbol":"XYZ"}])"},
        {"/hmds/history",  R"({"data":[]})"},
    };

    auto r = f.getBars("XYZ", 30);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(f.calls.size(), 2u);
    EXPECT_NE(f.calls[0].url.find("search?symbol=XYZ"), std::string::npos);
    EXPECT_NE(f.calls[1].url.find("conid=123456"),      std::string::npos);
}

// Recherche sans résultat → Err (le symbole est introuvable, pas « zéro barre »)
TEST(IbkrDataFeedUnit, SymbolNotFoundIsErr) {
    ScriptedIbkrDataFeed f;
    f.responses = {{"/secdef/search", "[]"}};

    auto r = f.getBars("XYZ", 30);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().find("introuvable"), std::string::npos);
}

// Barres sans close (c <= 0) filtrées ; réponse sans "data" → Ok(vide)
TEST(IbkrDataFeedUnit, InvalidBarsSkippedAndMissingDataIsOkEmpty) {
    ScriptedIbkrDataFeed f;
    f.responses = {{"/hmds/history", R"({"data":[
        {"t":1704153600000,"c":0.0},
        {"t":1704240000000,"c":404.6}
    ]})"}};
    auto r = f.getBars("QQQ", 60);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 1u);

    f.responses = {{"/hmds/history", R"({"error":"no data"})"}};
    auto vide = f.getBars("QQQ", 60);
    ASSERT_TRUE(vide.ok());
    EXPECT_TRUE(vide.value().empty());
}

// Panne réseau ou JSON invalide → Err (item 10), jamais Ok(vide)
TEST(IbkrDataFeedUnit, NetworkOrParsingFailureIsErr) {
    ScriptedIbkrDataFeed panne;
    panne.failAll = true;
    auto r1 = panne.getBars("QQQ", 60);
    ASSERT_FALSE(r1.ok());
    EXPECT_NE(r1.error().find("IBKR getBars"), std::string::npos);

    ScriptedIbkrDataFeed malforme;
    malforme.responses = {{"/hmds/history", "<html>502 Bad Gateway</html>"}};
    EXPECT_FALSE(malforme.getBars("QQQ", 60).ok());
}

// ════════════════════════════════════════════════════════════
//  isMarketOpen — disponibilité des marchés via le Gateway
// ════════════════════════════════════════════════════════════

TEST(IbkrDataFeedUnit, MarketOpenFollowsNasdaqAvailability) {
    ScriptedIbkrDataFeed f;
    f.responses = {{"/marketdata/availability", R"([
        {"description":"LSE","isOpen":true},
        {"description":"NASDAQ","isOpen":true}
    ])"}};
    EXPECT_TRUE(f.isMarketOpen());

    f.responses = {{"/marketdata/availability", R"([
        {"description":"NYSE","isOpen":false}
    ])"}};
    EXPECT_FALSE(f.isMarketOpen());
}

// ════════════════════════════════════════════════════════════
//  searchConid / isAuthenticated
// ════════════════════════════════════════════════════════════

TEST(IbkrDataFeedUnit, SearchConidReturnsFirstMatch) {
    ScriptedIbkrDataFeed f;
    f.responses = {{"/secdef/search", R"([{"conid":756733},{"conid":999}])"}};
    EXPECT_EQ(f.searchConid("SPY"), "756733");
}

TEST(IbkrDataFeedUnit, SearchConidThrowsWhenNotFound) {
    ScriptedIbkrDataFeed f;
    f.responses = {{"/secdef/search", "[]"}};
    EXPECT_THROW(f.searchConid("INCONNU"), std::runtime_error);
}

TEST(IbkrDataFeedUnit, AuthStatusParsed) {
    ScriptedIbkrDataFeed f;
    f.responses = {{"/auth/status", R"({"authenticated":true})"}};
    EXPECT_TRUE(f.isAuthenticated());

    f.responses = {{"/auth/status", R"({"authenticated":false})"}};
    EXPECT_FALSE(f.isAuthenticated());
}

// Gateway injoignable → non authentifié (pas d'exception qui fuit)
TEST(IbkrDataFeedUnit, AuthFailureMeansNotAuthenticated) {
    ScriptedIbkrDataFeed f;
    f.failAll = true;
    EXPECT_FALSE(f.isAuthenticated());
}

// Gateway sans info NASDAQ/NYSE : repli sur l'horloge EST locale —
// le résultat dépend de l'heure d'exécution, on fige seulement
// l'absence d'exception et la cohérence du repli
TEST(IbkrDataFeedUnit, MissingExchangeInfoFallsBackToLocalClock) {
    ScriptedIbkrDataFeed vide;
    vide.responses = {{"/marketdata/availability", "[]"}};

    ScriptedIbkrDataFeed panne;
    panne.failAll = true;

    bool viaGateway = false, viaPanne = false;
    EXPECT_NO_THROW(viaGateway = vide.isMarketOpen());
    EXPECT_NO_THROW(viaPanne   = panne.isMarketOpen());
    // Même repli dans les deux cas → même réponse au même instant
    EXPECT_EQ(viaGateway, viaPanne);
}
