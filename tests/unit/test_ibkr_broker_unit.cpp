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
