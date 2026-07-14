// ============================================================
//  test_small_account_feasibility_unit.cpp  —  Tests UNITAIRES
//  Cible : verdict de faisabilité (Sprint 23, item 23.2)
//  Règles DÉTERMINISTES et documentées — faisabilité
//  opérationnelle ≠ rentabilité.
// ============================================================
#include <gtest/gtest.h>
#include "backtest/SmallAccountFeasibility.hpp"

using namespace trading;

namespace {

// Stats synthétiques : a tentatives, e exécutées, z quantité-zéro, c cash
ExecutionStats stats(long a, long e, long z, long c = 0) {
    ExecutionStats s;
    s.entriesAttempted                = a;
    s.entriesExecuted                 = e;
    s.entriesRejectedZeroQuantity     = z;
    s.entriesRejectedInsufficientCash = c;
    return s;
}

FractionalSizingAnalysis fracPerdue(double pointsPerdus) {
    FractionalSizingAnalysis f;
    f.lostDeploymentPctDueToIntegerConstraint = pointsPerdus;
    return f;
}

} // namespace

// Aucune entrée exécutée → INEXPLOITABLE, quel que soit le reste
TEST(SmallAccountFeasibilityUnit, NoExecutionMeansInexploitable) {
    EXPECT_EQ(feasibilityVerdict(stats(10, 0, 10), fracPerdue(0.0)),
              FeasibilityVerdict::InexploitableActionsEntieres);
    EXPECT_EQ(feasibilityVerdict(stats(0, 0, 0), fracPerdue(0.0)),
              FeasibilityVerdict::InexploitableActionsEntieres);
}

// ≥ 50 % des tentatives bloquées (quantité zéro + cash) → FRACTIONS
// POTENTIELLEMENT NÉCESSAIRES (seuil kSeuilBlocageFractions documenté)
TEST(SmallAccountFeasibilityUnit, MajorityBlockedSuggestsFractions) {
    EXPECT_EQ(feasibilityVerdict(stats(10, 5, 5), fracPerdue(0.0)),
              FeasibilityVerdict::FractionsPotentiellementNecessaires);
    EXPECT_EQ(feasibilityVerdict(stats(10, 4, 4, 2), fracPerdue(0.0)),
              FeasibilityVerdict::FractionsPotentiellementNecessaires);
}

// Blocage minoritaire mais non nul → EXPLOITABLE MAIS CONTRAINT
TEST(SmallAccountFeasibilityUnit, MinorityBlockedIsConstrained) {
    EXPECT_EQ(feasibilityVerdict(stats(10, 9, 1), fracPerdue(0.0)),
              FeasibilityVerdict::ExploitableMaisContraint);
}

// Aucun blocage mais ≥ 15 points de déploiement perdus par la troncature
// entière → CONTRAINT aussi (seuil kSeuilDeploiementPerdu documenté)
TEST(SmallAccountFeasibilityUnit, HeavyDeploymentLossIsConstrained) {
    EXPECT_EQ(feasibilityVerdict(stats(10, 10, 0), fracPerdue(15.0)),
              FeasibilityVerdict::ExploitableMaisContraint);
    EXPECT_EQ(feasibilityVerdict(stats(10, 10, 0), fracPerdue(14.9)),
              FeasibilityVerdict::ExploitableActionsEntieres);
}

// Cas nominal : tout s'exécute, perte de déploiement faible → EXPLOITABLE
TEST(SmallAccountFeasibilityUnit, CleanExecutionIsExploitable) {
    EXPECT_EQ(feasibilityVerdict(stats(10, 10, 0), fracPerdue(2.0)),
              FeasibilityVerdict::ExploitableActionsEntieres);
}

// FAISABILITÉ ≠ RENTABILITÉ : un rapport peut être opérationnellement
// exploitable ET sans edge — les deux verdicts sont indépendants.
TEST(SmallAccountFeasibilityUnit, ExploitableDoesNotImplyEdge) {
    SmallAccountFeasibilityReport r;
    r.stats      = stats(10, 10, 0);
    r.fractional = fracPerdue(0.0);
    r.verdict    = feasibilityVerdict(r.stats, r.fractional);
    r.totalReturnPct       = 5.0;    // le compte a exécuté et gagné un peu…
    r.buyHoldNetReturnPct  = 50.0;   // …mais le B&H net fait 10× mieux
    r.alphaNetPct          = r.totalReturnPct - r.buyHoldNetReturnPct;
    r.edgeNetDemontre      = r.alphaNetPct > 0.0;

    EXPECT_EQ(r.verdict, FeasibilityVerdict::ExploitableActionsEntieres);
    EXPECT_FALSE(r.edgeNetDemontre);
}

// Étiquettes stables (affichées par validate et verrouillées en intégration)
TEST(SmallAccountFeasibilityUnit, VerdictLabelsAreStable) {
    EXPECT_STREQ(feasibilityVerdictLabel(
                     FeasibilityVerdict::InexploitableActionsEntieres),
                 "INEXPLOITABLE_ACTIONS_ENTIERES");
    EXPECT_STREQ(feasibilityVerdictLabel(
                     FeasibilityVerdict::ExploitableMaisContraint),
                 "EXPLOITABLE_MAIS_CONTRAINT");
    EXPECT_STREQ(feasibilityVerdictLabel(
                     FeasibilityVerdict::ExploitableActionsEntieres),
                 "EXPLOITABLE_ACTIONS_ENTIERES");
    EXPECT_STREQ(feasibilityVerdictLabel(
                     FeasibilityVerdict::FractionsPotentiellementNecessaires),
                 "FRACTIONS_POTENTIELLEMENT_NECESSAIRES");
}
