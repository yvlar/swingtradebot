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
    bool failAll = false;   // simule une panne réseau sur tous les appels

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
};

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

// ════════════════════════════════════════════════════════════
//  isUsMarketOpenAtUtc — calendrier marché US, DST géré (item 20)
//  L'ancien code (UTC-5 fixe + ouverture à 9h00) échouait sur
//  plusieurs de ces instants — d'où le calcul figé par UTC.
// ════════════════════════════════════════════════════════════

namespace {
    // Construit un std::time_t à partir de composantes UTC (timegm = UTC pur,
    // indépendant du fuseau de la machine de test)
    std::time_t utc(int y, int mon, int d, int h, int mi) {
        std::tm tm{};
        tm.tm_year = y - 1900;
        tm.tm_mon  = mon - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min  = mi;
        return timegm(&tm);
    }
}

// La bascule EDT/EST est calculée, pas codée en dur
TEST(IbkrDataFeedUnit, EasternDstWindowDetected) {
    auto dstAt = [](int mon, int d) {
        std::tm tm{};
        std::time_t t = utc(2024, mon, d, 12, 0);
        gmtime_r(&t, &tm);
        return IBKRDataFeed::isUsEasternDst(tm);
    };
    EXPECT_FALSE(dstAt(1, 15));   // janvier → EST
    EXPECT_FALSE(dstAt(3, 9));    // 2024 : avant le 2e dimanche (10 mars) → EST
    EXPECT_TRUE (dstAt(3, 11));   // après la bascule → EDT
    EXPECT_TRUE (dstAt(7, 4));    // plein été → EDT
    EXPECT_TRUE (dstAt(11, 2));   // 2024 : avant le 1er dimanche (3 nov) → EDT
    EXPECT_FALSE(dstAt(11, 4));   // après la bascule → EST
    EXPECT_FALSE(dstAt(12, 25));  // décembre → EST
}

// Été (EDT, UTC-4) : 9h45 ET = 13h45 UTC. L'ancien calcul UTC-5 donnait
// 8h45 → marché « fermé » à tort.
TEST(IbkrDataFeedUnit, SummerMorningOpenWithDstOffset) {
    EXPECT_TRUE (IBKRDataFeed::isUsMarketOpenAtUtc(utc(2024, 7, 3, 13, 45)));
    // 16h30 ET = 20h30 UTC → fermé. L'ancien UTC-5 voyait 15h30 → « ouvert ».
    EXPECT_FALSE(IBKRDataFeed::isUsMarketOpenAtUtc(utc(2024, 7, 3, 20, 30)));
    // Bornes d'été : 13h30 UTC = 9h30 ET (ouverture), 20h00 UTC = 16h00 (clôture)
    EXPECT_TRUE (IBKRDataFeed::isUsMarketOpenAtUtc(utc(2024, 7, 3, 13, 30)));
    EXPECT_FALSE(IBKRDataFeed::isUsMarketOpenAtUtc(utc(2024, 7, 3, 20, 0)));
}

// Hiver (EST, UTC-5) : 9h00 ET = 14h00 UTC → fermé (avant 9h30). L'ancien
// code ouvrait dès 9h00 (la condition minute était morte).
TEST(IbkrDataFeedUnit, WinterOpenAt930NotAt900) {
    EXPECT_FALSE(IBKRDataFeed::isUsMarketOpenAtUtc(utc(2024, 1, 3, 14, 0)));   // 9h00 ET
    EXPECT_TRUE (IBKRDataFeed::isUsMarketOpenAtUtc(utc(2024, 1, 3, 14, 30)));  // 9h30 ET
    EXPECT_TRUE (IBKRDataFeed::isUsMarketOpenAtUtc(utc(2024, 1, 3, 20, 59)));  // 15h59 ET
    EXPECT_FALSE(IBKRDataFeed::isUsMarketOpenAtUtc(utc(2024, 1, 3, 21, 0)));   // 16h00 ET
}

// Week-end : fermé quelle que soit l'heure
TEST(IbkrDataFeedUnit, WeekendAlwaysClosed) {
    EXPECT_FALSE(IBKRDataFeed::isUsMarketOpenAtUtc(utc(2024, 7, 6, 14, 0)));   // samedi
    EXPECT_FALSE(IBKRDataFeed::isUsMarketOpenAtUtc(utc(2024, 7, 7, 18, 0)));   // dimanche
}
