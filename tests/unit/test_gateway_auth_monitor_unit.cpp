// ============================================================
//  test_gateway_auth_monitor_unit.cpp  —  Tests UNITAIRES
//  Cible : GatewayAuthMonitor — transitions d'auth du Gateway
//  (item 17.4 : une alerte à la PERTE, un événement à la
//  RÉCUPÉRATION, jamais de répétition à chaque cycle)
// ============================================================
#include <gtest/gtest.h>
#include "core/GatewayAuthMonitor.hpp"

using trading::GatewayAuthMonitor;
using Event = trading::GatewayAuthMonitor::Event;

// Démarrage authentifié (cas nominal) : aucun événement
TEST(GatewayAuthMonitorUnit, AuthenticatedStartNoEvent) {
    GatewayAuthMonitor m;
    EXPECT_EQ(m.observe(true), Event::Aucun);
    EXPECT_EQ(m.observe(true), Event::Aucun);
}

// Le moniteur part « authentifié » (l'auth est vérifiée au démarrage) :
// une première observation dé-authentifiée est une PERTE
TEST(GatewayAuthMonitorUnit, FirstObservationUnauthenticatedFiresLoss) {
    GatewayAuthMonitor m;
    EXPECT_EQ(m.observe(false), Event::PerteAuth);
}

// La perte n'est signalée qu'UNE fois — pas d'alerte à chaque cycle sauté
TEST(GatewayAuthMonitorUnit, LossFiresOnceNotEveryCycle) {
    GatewayAuthMonitor m;
    EXPECT_EQ(m.observe(true),  Event::Aucun);
    EXPECT_EQ(m.observe(false), Event::PerteAuth);
    EXPECT_EQ(m.observe(false), Event::Aucun);
    EXPECT_EQ(m.observe(false), Event::Aucun);
}

// Le retour de l'auth est un événement distinct (reprise des cycles)
TEST(GatewayAuthMonitorUnit, RecoveryFiresRecuperation) {
    GatewayAuthMonitor m;
    EXPECT_EQ(m.observe(false), Event::PerteAuth);
    EXPECT_EQ(m.observe(true),  Event::Recuperation);
    EXPECT_EQ(m.observe(true),  Event::Aucun);
}

// Une nouvelle perte après récupération alerte de nouveau
TEST(GatewayAuthMonitorUnit, SecondLossAfterRecoveryFiresAgain) {
    GatewayAuthMonitor m;
    EXPECT_EQ(m.observe(false), Event::PerteAuth);
    EXPECT_EQ(m.observe(true),  Event::Recuperation);
    EXPECT_EQ(m.observe(false), Event::PerteAuth);
    EXPECT_EQ(m.observe(false), Event::Aucun);
}
