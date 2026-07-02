// ============================================================
//  test_ibkr_broker_unit.cpp  —  Tests UNITAIRES
//  Cible : IBKRBroker — flux de soumission/confirmation d'ordre
//  (HTTP substitué : aucun réseau, réponses du Gateway scriptées)
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include <utility>
#include "brokers/IBKRBroker.hpp"

using namespace trading;

namespace {

// ── Broker avec Gateway simulé ────────────────────────────
// Enregistre chaque requête HTTP et répond selon un motif d'URL.
class ScriptedIbkrBroker : public IBKRBroker {
public:
    using IBKRBroker::IBKRBroker;

    struct Call {
        std::string method;
        std::string url;
        std::string body;
    };

    std::vector<Call> calls;
    // paires (motif d'URL, réponse JSON) — premier motif trouvé gagne
    std::vector<std::pair<std::string, std::string>> responses;

    int countUrlContaining(const std::string& fragment) const {
        int n = 0;
        for (const auto& c : calls)
            if (c.url.find(fragment) != std::string::npos) n++;
        return n;
    }

protected:
    std::string request(const std::string& method,
                        const std::string& url,
                        const std::string& body) override {
        calls.push_back({method, url, body});
        for (const auto& [pattern, resp] : responses)
            if (url.find(pattern) != std::string::npos) return resp;
        return "[]";
    }
};

} // namespace

// ════════════════════════════════════════════════════════════
//  Sprint 1 item 4 — la confirmation ne doit PAS re-poster l'ordre
// ════════════════════════════════════════════════════════════

// BUG : après acquittement des messageIds, l'ordre complet était re-posté
// sur /orders → DEUX ordres réels possibles. La réponse de /iserver/reply
// contient déjà le résultat de l'ordre initial.
TEST(IbkrBrokerUnit, ConfirmationConsumesReplyWithoutReposting) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {
        {"/iserver/reply/q1",
         R"([{"order_id":"42","order_status":"Submitted"}])"},
        {"/account/DU123/orders",
         R"([{"messageIds":["q1"],"message":["Confirmez-vous cet ordre ?"]}])"},
    };

    auto order = b.submitBuy("QQQ", 9);

    EXPECT_EQ(b.countUrlContaining("/account/DU123/orders"), 1);  // UN SEUL post d'ordre
    EXPECT_EQ(b.countUrlContaining("/iserver/reply/q1"),     1);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->orderId, "42");
    EXPECT_EQ(order->status, OrderStatus::PENDING);   // "Submitted" → PENDING
}

// Variante moderne du CP Gateway : la question arrive sous {"id", "message"}
// et les questions peuvent s'enchaîner — chacune doit être consommée
TEST(IbkrBrokerUnit, ChainedConfirmationsAllConsumed) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {
        {"/iserver/reply/q1",
         R"([{"id":"q2","message":["Seconde confirmation ?"]}])"},
        {"/iserver/reply/q2",
         R"([{"order_id":"9","order_status":"PreSubmitted"}])"},
        {"/account/DU123/orders",
         R"([{"id":"q1","message":["Première confirmation ?"]}])"},
    };

    auto order = b.submitBuy("QQQ", 9);

    EXPECT_EQ(b.countUrlContaining("/account/DU123/orders"), 1);
    EXPECT_EQ(b.countUrlContaining("/iserver/reply/q1"),     1);
    EXPECT_EQ(b.countUrlContaining("/iserver/reply/q2"),     1);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->orderId, "9");
}

// BUG : aucun identifiant client (cOID) → un retry réseau pouvait créer un
// doublon. Le payload doit contenir un cOID idempotent.
TEST(IbkrBrokerUnit, OrderPayloadContainsIdempotentClientOrderId) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {
        {"/account/DU123/orders",
         R"([{"order_id":"7","order_status":"Filled","avg_price":421.5}])"},
    };

    auto order = b.submitBuy("QQQ", 9);
    ASSERT_TRUE(order.has_value());
    ASSERT_FALSE(b.calls.empty());

    auto body = json::parse(b.calls[0].body);
    ASSERT_TRUE(body.contains("orders"));
    ASSERT_FALSE(body["orders"].empty());
    ASSERT_TRUE(body["orders"][0].contains("cOID"));
    EXPECT_FALSE(body["orders"][0]["cOID"].get<std::string>().empty());
}

// ════════════════════════════════════════════════════════════
//  Parsing des réponses (régression)
// ════════════════════════════════════════════════════════════

TEST(IbkrBrokerUnit, DirectFillParsedWithPrice) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {
        {"/account/DU123/orders",
         R"([{"order_id":"7","order_status":"Filled","avg_price":421.5}])"},
    };

    auto order = b.submitBuy("QQQ", 9);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->status, OrderStatus::FILLED);
    EXPECT_DOUBLE_EQ(order->price, 421.5);
    EXPECT_EQ(order->orderId, "7");
    EXPECT_EQ(order->quantity, 9);
}

TEST(IbkrBrokerUnit, RejectedOrderParsed) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {
        {"/account/DU123/orders",
         R"([{"order_id":"8","order_status":"Rejected"}])"},
    };

    auto order = b.submitSell("QQQ", 9);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->status, OrderStatus::REJECTED);
}

TEST(IbkrBrokerUnit, ZeroQuantityShortCircuitsWithoutHttp) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    EXPECT_FALSE(b.submitBuy("QQQ", 0).has_value());
    EXPECT_FALSE(b.submitSell("QQQ", -2).has_value());
    EXPECT_TRUE(b.calls.empty());
}

// ════════════════════════════════════════════════════════════
//  Sprint 5 item 19 — stop résident côté broker (STP GTC)
// ════════════════════════════════════════════════════════════

// submitStopLoss dépose un ordre STP de vente GTC au prix de déclenchement
TEST(IbkrBrokerUnit, StopLossPostsResidentStpOrder) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {
        {"/account/DU123/orders",
         R"([{"order_id":"55","order_status":"PreSubmitted"}])"},
    };

    auto o = b.submitStopLoss("QQQ", 9, 399.0);
    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->orderId, "55");

    ASSERT_FALSE(b.calls.empty());
    auto ord = json::parse(b.calls[0].body)["orders"][0];
    EXPECT_EQ(ord["orderType"], "STP");
    EXPECT_EQ(ord["side"],      "SELL");
    EXPECT_EQ(ord["tif"],       "GTC");
    EXPECT_DOUBLE_EQ(ord["price"].get<double>(), 399.0);
    // cOID distinct (tag STOP) → pas de collision avec un SELL marché de l'heure
    EXPECT_NE(ord["cOID"].get<std::string>().find("-STOP-"), std::string::npos);
}

// cancelStopLoss annule l'ordre stop mémorisé (DELETE /order/{id}) ;
// idempotent quand il n'y a rien à annuler
TEST(IbkrBrokerUnit, CancelStopLossDeletesTrackedOrder) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {
        {"/account/DU123/orders",
         R"([{"order_id":"55","order_status":"PreSubmitted"}])"},
    };

    EXPECT_FALSE(b.cancelStopLoss("QQQ"));        // rien à annuler avant dépôt

    b.submitStopLoss("QQQ", 9, 399.0);
    EXPECT_TRUE(b.cancelStopLoss("QQQ"));
    EXPECT_EQ(b.countUrlContaining("/order/55"), 1);
    EXPECT_FALSE(b.cancelStopLoss("QQQ"));        // déjà annulé → plus rien
}

// Arguments invalides (qty ≤ 0, prix ≤ 0) → nullopt sans appel HTTP
TEST(IbkrBrokerUnit, StopLossRejectsInvalidArgs) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    EXPECT_FALSE(b.submitStopLoss("QQQ", 0, 399.0).has_value());
    EXPECT_FALSE(b.submitStopLoss("QQQ", 9, 0.0).has_value());
    EXPECT_TRUE(b.calls.empty());
}

// ════════════════════════════════════════════════════════════
//  getPosition / getAccount — canal Result (item 10)
// ════════════════════════════════════════════════════════════

TEST(IbkrBrokerUnit, OpenPositionParsed) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {{"/position/320227571", R"([{"position":9.0,
        "avgCost":402.5,"mktValue":3700.0,"unrealizedPnl":77.5}])"}};

    auto r = b.getPosition("QQQ");
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value().has_value());
    EXPECT_EQ(r.value()->shares, 9);
    EXPECT_DOUBLE_EQ(r.value()->avgPrice,      402.5);
    EXPECT_DOUBLE_EQ(r.value()->marketValue,   3700.0);
    EXPECT_DOUBLE_EQ(r.value()->unrealizedPnl,   77.5);
}

// Réponse vide ou position nulle = certitude « pas de position » → Ok(nullopt)
TEST(IbkrBrokerUnit, EmptyOrZeroPositionIsOkNullopt) {
    ScriptedIbkrBroker vide("DU123", "https://gw");
    vide.responses = {{"/position/", "[]"}};
    auto r1 = vide.getPosition("QQQ");
    ASSERT_TRUE(r1.ok());
    EXPECT_FALSE(r1.value().has_value());

    ScriptedIbkrBroker zero("DU123", "https://gw");
    zero.responses = {{"/position/", R"([{"position":0.0}])"}};
    auto r2 = zero.getPosition("QQQ");
    ASSERT_TRUE(r2.ok());
    EXPECT_FALSE(r2.value().has_value());
}

// Réponse illisible = panne : la position est INCONNUE → Err, jamais nullopt
TEST(IbkrBrokerUnit, MalformedPositionResponseIsErr) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {{"/position/", "<html>502</html>"}};

    auto r = b.getPosition("QQQ");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().find("IBKR getPosition"), std::string::npos);
    EXPECT_FALSE(b.lastError().empty());
}

// Symbole hors KNOWN_CONIDS → Err immédiat, sans appel HTTP
TEST(IbkrBrokerUnit, UnknownSymbolIsErrWithoutHttp) {
    ScriptedIbkrBroker b("DU123", "https://gw");

    auto r = b.getPosition("ZZZ");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().find("Conid inconnu"), std::string::npos);
    EXPECT_TRUE(b.calls.empty());
}

// Les montants du compte sont dans des objets imbriqués IBKR
TEST(IbkrBrokerUnit, AccountSummaryParsedFromNestedAmounts) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {{"/summary", R"({
        "availablefunds":  {"amount": 25000.50},
        "netliquidation":  {"amount": 31000.75}
    })"}};

    auto a = b.getAccount();
    EXPECT_DOUBLE_EQ(a.cash,   25'000.50);
    EXPECT_DOUBLE_EQ(a.equity, 31'000.75);
    EXPECT_EQ(a.status, "ACTIVE");
}

// Panne sur le résumé de compte → INACTIVE (le RiskManager bloquera tout trade)
TEST(IbkrBrokerUnit, AccountFailureReturnsInactive) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {{"/summary", "pas du json"}};

    auto a = b.getAccount();
    EXPECT_DOUBLE_EQ(a.cash, 0.0);
    EXPECT_EQ(a.status, "INACTIVE");
}

// BUG : un résumé sans champs de solde (schéma Gateway différent) était marqué
// ACTIVE avec cash=0 → positionSize=0 → le bot ne tradait plus jamais, sans
// aucune erreur visible. Un schéma incomplet doit être un échec BRUYANT.
TEST(IbkrBrokerUnit, AccountSummaryWithoutBalancesIsInactive) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {{"/summary", R"({"autrechose": 42})"}};

    auto a = b.getAccount();
    EXPECT_EQ(a.status, "INACTIVE");
    EXPECT_FALSE(b.lastError().empty());
}

// Un seul des deux montants présent → toujours INACTIVE (schéma incomplet)
TEST(IbkrBrokerUnit, AccountSummaryWithSingleBalanceIsInactive) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {{"/summary", R"({"availablefunds": {"amount": 25000.0}})"}};

    auto a = b.getAccount();
    EXPECT_EQ(a.status, "INACTIVE");
    EXPECT_FALSE(b.lastError().empty());
}

// ════════════════════════════════════════════════════════════
//  D38 — re-découverte de l'orderId du stop résident au restart.
//  BUG : residentStopOrderId_ vivait en mémoire seulement — après
//  un redémarrage, cancelStopLoss était un no-op et le stop IBKR
//  restait orphelin chez le broker (déclenchement après coup).
// ════════════════════════════════════════════════════════════

// Broker NEUF (restart simulé) : cancelStopLoss re-découvre l'ordre stop via
// GET /iserver/account/orders (order_ref porte le tag swingbot-SYM-STOP-)
// puis envoie le DELETE sur l'id retrouvé.
TEST(IbkrBrokerUnit, CancelStopLossRediscoversOrderAfterRestart) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {{"/iserver/account/orders", R"({"orders":[
        {"orderId": 77, "order_ref": "swingbot-QQQ-STOP-2026070214",
         "status": "PreSubmitted", "side": "SELL", "ticker": "QQQ"}
    ]})"}};

    EXPECT_TRUE(b.cancelStopLoss("QQQ"));
    EXPECT_EQ(b.countUrlContaining("/iserver/account/orders"), 1);
    EXPECT_EQ(b.countUrlContaining("/order/77"), 1);
    ASSERT_EQ(b.calls.size(), 2u);
    EXPECT_EQ(b.calls[1].method, "DELETE");
}

// Aucun ordre stop ouvert → false, et surtout AUCUN DELETE
TEST(IbkrBrokerUnit, CancelStopLossNoOpenStopReturnsFalseWithoutDelete) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {{"/iserver/account/orders", R"({"orders":[]})"}};

    EXPECT_FALSE(b.cancelStopLoss("QQQ"));
    EXPECT_EQ(b.countUrlContaining("/order/"), 0);
}

// Les stops d'un AUTRE symbole et les stops en statut terminal sont ignorés
TEST(IbkrBrokerUnit, CancelStopLossIgnoresOtherSymbolsAndTerminalStops) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {{"/iserver/account/orders", R"({"orders":[
        {"orderId": 11, "order_ref": "swingbot-SPY-STOP-2026070214",
         "status": "PreSubmitted"},
        {"orderId": 22, "order_ref": "swingbot-QQQ-STOP-2026070113",
         "status": "Cancelled"}
    ]})"}};

    EXPECT_FALSE(b.cancelStopLoss("QQQ"));
    EXPECT_EQ(b.countUrlContaining("/order/"), 0);
}

// ════════════════════════════════════════════════════════════
//  Compléments de couverture — confirmations épuisées, exceptions
// ════════════════════════════════════════════════════════════

// Le Gateway pose une question à CHAQUE acquittement : après 5 tours,
// on abandonne (nullopt + lastError) plutôt que de boucler à l'infini
TEST(IbkrBrokerUnit, EndlessConfirmationsAbortAfterMaxRounds) {
    ScriptedIbkrBroker b("DU123", "https://gw");
    b.responses = {
        {"/iserver/reply/",          R"([{"id":"q-suivante","message":["encore ?"]}])"},
        {"/iserver/account/DU123",   R"([{"id":"q-1","message":["confirmer ?"]}])"},
    };

    EXPECT_FALSE(b.submitBuy("QQQ", 1).has_value());
    EXPECT_NE(b.lastError().find("non résolues"), std::string::npos);
    EXPECT_EQ(b.countUrlContaining("/iserver/reply/"), 5);   // kMaxConfirmRounds
}

// Symbole sans conid connu : l'exception est absorbée → nullopt + lastError
TEST(IbkrBrokerUnit, SubmitUnknownSymbolFailsSoftly) {
    ScriptedIbkrBroker b("DU123", "https://gw");

    EXPECT_FALSE(b.submitBuy("ZZZ", 1).has_value());
    EXPECT_NE(b.lastError().find("Conid inconnu"), std::string::npos);
    EXPECT_TRUE(b.calls.empty());
}
