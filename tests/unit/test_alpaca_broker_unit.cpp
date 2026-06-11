// ============================================================
//  test_alpaca_broker_unit.cpp  —  Tests UNITAIRES
//  Cible : AlpacaBroker — parsing des réponses API et canal Result
//  (HTTP substitué : aucun réseau, réponses Alpaca scriptées)
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include <utility>
#include "brokers/AlpacaBroker.hpp"

using namespace trading;

namespace {

// ── Broker avec API Alpaca simulée ────────────────────────
// Enregistre chaque requête et répond selon un motif d'URL ;
// peut lancer une HttpError pour simuler un statut 4xx/5xx.
class ScriptedAlpacaBroker : public AlpacaBroker {
public:
    ScriptedAlpacaBroker() : AlpacaBroker("cle", "secret", true) {}

    struct Call {
        std::string method;
        std::string url;
        std::string body;
        std::vector<std::string> headers;
    };

    std::vector<Call> calls;
    // paires (motif d'URL, réponse JSON) — premier motif trouvé gagne
    std::vector<std::pair<std::string, std::string>> responses;
    // si renseigné : lance HttpError(status) au lieu de répondre
    std::optional<std::pair<long, std::string>> httpFailure;

protected:
    std::string httpRequest(const std::string& method,
                            const std::string& url,
                            const std::string& body,
                            const std::vector<std::string>& headers) override {
        calls.push_back({method, url, body, headers});
        if (httpFailure)
            throw HttpError(httpFailure->first, httpFailure->second);
        for (const auto& [pattern, resp] : responses)
            if (url.find(pattern) != std::string::npos) return resp;
        return "{}";
    }
};

} // namespace

// ════════════════════════════════════════════════════════════
//  submitBuy / submitSell — payload et parsing d'ordre
// ════════════════════════════════════════════════════════════

TEST(AlpacaBrokerUnit, ZeroOrNegativeQtyShortCircuitsWithoutHttp) {
    ScriptedAlpacaBroker b;
    EXPECT_FALSE(b.submitBuy("QQQ", 0).has_value());
    EXPECT_FALSE(b.submitSell("QQQ", -3).has_value());
    EXPECT_TRUE(b.calls.empty());
}

TEST(AlpacaBrokerUnit, BuyPostsMarketOrderPayload) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/orders", R"({"id":"o-1","symbol":"QQQ","side":"buy",
        "status":"accepted","created_at":"2024-01-02T15:30:00Z"})"}};

    auto o = b.submitBuy("QQQ", 9);

    ASSERT_TRUE(o.has_value());
    ASSERT_EQ(b.calls.size(), 1u);
    EXPECT_EQ(b.calls[0].method, "POST");
    EXPECT_NE(b.calls[0].url.find("/v2/orders"), std::string::npos);
    // qty est transmis en chaîne, ordre market valide pour la journée
    EXPECT_NE(b.calls[0].body.find("\"qty\":\"9\""),          std::string::npos);
    EXPECT_NE(b.calls[0].body.find("\"side\":\"buy\""),       std::string::npos);
    EXPECT_NE(b.calls[0].body.find("\"type\":\"market\""),    std::string::npos);
    EXPECT_NE(b.calls[0].body.find("\"time_in_force\":\"day\""), std::string::npos);
}

// D15 : chaque ordre porte un client_order_id idempotent (le retry du
// HttpClient peut re-poster un POST /v2/orders → sans cet id, double-ordre).
TEST(AlpacaBrokerUnit, OrdersCarryIdempotentClientOrderId) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/orders", R"({"id":"o-1","status":"accepted","side":"buy"})"}};

    b.submitBuy("QQQ", 9);
    ASSERT_EQ(b.calls.size(), 1u);
    auto buyBody = json::parse(b.calls[0].body);
    ASSERT_TRUE(buyBody.contains("client_order_id"));
    std::string buyId = buyBody["client_order_id"];
    EXPECT_NE(buyId.find("swingbot-QQQ-buy-"), std::string::npos);

    b.submitSell("QQQ", 9);
    auto sellBody = json::parse(b.calls[1].body);
    ASSERT_TRUE(sellBody.contains("client_order_id"));
    EXPECT_NE(sellBody["client_order_id"].get<std::string>().find("swingbot-QQQ-sell-"),
              std::string::npos);

    // Deux achats dans la même heure → même id (idempotent face au retry)
    b.submitBuy("QQQ", 9);
    auto buyBody2 = json::parse(b.calls[2].body);
    EXPECT_EQ(buyBody2["client_order_id"].get<std::string>(), buyId);
}

TEST(AlpacaBrokerUnit, AuthHeadersOnEveryCall) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/account", R"({"cash":"1","equity":"1","status":"ACTIVE"})"}};
    b.getAccount();

    ASSERT_EQ(b.calls.size(), 1u);
    ASSERT_GE(b.calls[0].headers.size(), 2u);
    EXPECT_NE(b.calls[0].headers[0].find("APCA-API-KEY-ID: cle"),        std::string::npos);
    EXPECT_NE(b.calls[0].headers[1].find("APCA-API-SECRET-KEY: secret"), std::string::npos);
}

// Mapping des statuts Alpaca → OrderStatus
TEST(AlpacaBrokerUnit, FilledOrderParsedWithPriceAndQty) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/orders", R"({"id":"o-2","symbol":"QQQ","side":"buy",
        "status":"filled","filled_avg_price":"402.50","filled_qty":"9"})"}};

    auto o = b.submitBuy("QQQ", 9);
    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->status, OrderStatus::FILLED);
    EXPECT_EQ(o->side,   OrderSide::BUY);
    EXPECT_DOUBLE_EQ(o->price, 402.50);
    EXPECT_EQ(o->quantity, 9);
    EXPECT_EQ(o->orderId, "o-2");
}

TEST(AlpacaBrokerUnit, CanceledAndRejectedStatusesMapped) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/orders", R"({"status":"canceled","side":"sell"})"}};
    auto c = b.submitSell("QQQ", 1);
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->status, OrderStatus::CANCELLED);
    EXPECT_EQ(c->side,   OrderSide::SELL);

    b.responses = {{"/v2/orders", R"({"status":"rejected","side":"buy"})"}};
    auto r = b.submitBuy("QQQ", 1);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, OrderStatus::REJECTED);
}

// Statut inconnu / prix non encore rempli → PENDING avec prix 0
TEST(AlpacaBrokerUnit, UnknownStatusIsPendingWithZeroPrice) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/orders", R"({"status":"new","side":"buy",
        "filled_avg_price":"","filled_qty":""})"}};

    auto o = b.submitBuy("QQQ", 5);
    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->status, OrderStatus::PENDING);
    EXPECT_DOUBLE_EQ(o->price, 0.0);
    EXPECT_EQ(o->quantity, 0);
}

// Erreur applicative Alpaca dans un corps 2xx → nullopt + lastError
TEST(AlpacaBrokerUnit, ApplicativeErrorBodyFailsOrderWithLastError) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/orders",
        R"({"code":40310000,"message":"insufficient buying power"})"}};

    EXPECT_FALSE(b.submitBuy("QQQ", 9).has_value());
    EXPECT_NE(b.lastError().find("insufficient buying power"), std::string::npos);
}

TEST(AlpacaBrokerUnit, HttpFailureOnSubmitReturnsNullopt) {
    ScriptedAlpacaBroker b;
    b.httpFailure = {{500, "statut 500"}};
    EXPECT_FALSE(b.submitBuy("QQQ", 9).has_value());
    EXPECT_NE(b.lastError().find("500"), std::string::npos);
}

// ════════════════════════════════════════════════════════════
//  getPosition — canal Result (item 10) : 404 ≠ panne
// ════════════════════════════════════════════════════════════

TEST(AlpacaBrokerUnit, OpenPositionParsed) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/positions/QQQ", R"({"symbol":"QQQ","qty":"9",
        "avg_entry_price":"402.5","market_value":"3700.0","unrealized_pl":"77.5"})"}};

    auto r = b.getPosition("QQQ");
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value().has_value());
    EXPECT_EQ(r.value()->shares, 9);
    EXPECT_DOUBLE_EQ(r.value()->avgPrice,      402.5);
    EXPECT_DOUBLE_EQ(r.value()->marketValue,   3700.0);
    EXPECT_DOUBLE_EQ(r.value()->unrealizedPnl,   77.5);
}

TEST(AlpacaBrokerUnit, ZeroSharesIsOkNullopt) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/positions/QQQ", R"({"symbol":"QQQ","qty":"0"})"}};

    auto r = b.getPosition("QQQ");
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value().has_value());
}

// 404 = Alpaca confirme l'ABSENCE de position : certitude, pas une panne
TEST(AlpacaBrokerUnit, Http404IsOkNulloptNotError) {
    ScriptedAlpacaBroker b;
    b.httpFailure = {{404, "HTTP GET … → statut 404"}};

    auto r = b.getPosition("QQQ");
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value().has_value());
}

// 5xx = panne : la position est INCONNUE → Err, jamais Ok(nullopt)
TEST(AlpacaBrokerUnit, Http500IsErrNotNullopt) {
    ScriptedAlpacaBroker b;
    b.httpFailure = {{503, "statut 503"}};

    auto r = b.getPosition("QQQ");
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.error().find("Alpaca getPosition"), std::string::npos);
}

TEST(AlpacaBrokerUnit, MalformedPositionJsonIsErr) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/positions/QQQ", "pas du json"}};

    auto r = b.getPosition("QQQ");
    EXPECT_FALSE(r.ok());
}

// ════════════════════════════════════════════════════════════
//  getAccount / utilitaires
// ════════════════════════════════════════════════════════════

TEST(AlpacaBrokerUnit, AccountParsedFromStrings) {
    ScriptedAlpacaBroker b;
    b.responses = {{"/v2/account",
        R"({"cash":"25000.50","equity":"31000.75","status":"ACTIVE"})"}};

    auto a = b.getAccount();
    EXPECT_DOUBLE_EQ(a.cash,   25'000.50);
    EXPECT_DOUBLE_EQ(a.equity, 31'000.75);
    EXPECT_EQ(a.status, "ACTIVE");
}

// Panne sur le compte → compte INACTIVE (le RiskManager bloquera tout trade)
TEST(AlpacaBrokerUnit, AccountFailureReturnsInactive) {
    ScriptedAlpacaBroker b;
    b.httpFailure = {{500, "statut 500"}};

    auto a = b.getAccount();
    EXPECT_DOUBLE_EQ(a.cash,   0.0);
    EXPECT_DOUBLE_EQ(a.equity, 0.0);
    EXPECT_EQ(a.status, "INACTIVE");
}

TEST(AlpacaBrokerUnit, CancelAllOrdersSendsDeleteAndSwallowsErrors) {
    ScriptedAlpacaBroker b;
    b.cancelAllOrders();
    ASSERT_EQ(b.calls.size(), 1u);
    EXPECT_EQ(b.calls[0].method, "DELETE");
    EXPECT_NE(b.calls[0].url.find("/v2/orders"), std::string::npos);

    // En cas de panne, l'appel ne doit pas lancer
    b.httpFailure = {{500, "statut 500"}};
    EXPECT_NO_THROW(b.cancelAllOrders());
    EXPECT_NO_THROW(b.closeAllPositions());
}

// La panne HTTP sur une VENTE est absorbée de la même façon qu'à l'achat
TEST(AlpacaBrokerUnit, HttpFailureOnSellReturnsNullopt) {
    ScriptedAlpacaBroker b;
    b.httpFailure = {{500, "statut 500"}};
    EXPECT_FALSE(b.submitSell("QQQ", 9).has_value());
    EXPECT_NE(b.lastError().find("500"), std::string::npos);
}
