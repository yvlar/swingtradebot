// ============================================================
//  test_cointegration_unit.cpp  —  Tests UNITAIRES
//  Cible : Cointegration.hpp (olsFit, egResidual, dfTStat)
//  Réouverture Sprint 18 (piste §5.1) : briques Engle-Granger en math pur —
//  OLS roulante causale (hedge ratio) + test de Dickey-Fuller (lag 0, avec
//  constante) sur le résidu. Aucun I/O, scénarios DÉTERMINISTES à la main.
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include "backtest/Cointegration.hpp"

using namespace trading;

// ════════════════════════════════════════════════════════════
//  OLS : récupère EXACTEMENT α et β sur une relation sans bruit y = 1 + 2x.
// ════════════════════════════════════════════════════════════
TEST(CointegrationUnit, OlsRecoversExactAlphaBetaOnNoiselessLine) {
    const std::vector<double> x = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> y;
    for (double v : x) y.push_back(1.0 + 2.0 * v);

    const OlsFit f = olsFit(y, x, 4, 5);
    ASSERT_TRUE(f.ok);
    EXPECT_NEAR(f.beta,  2.0, 1e-12);
    EXPECT_NEAR(f.alpha, 1.0, 1e-12);
}

// ════════════════════════════════════════════════════════════
//  OLS : CAUSALITÉ — modifier des données APRÈS `end` ne change pas le fit
//  (le hedge ratio est estimé à l'instant de décision, anti look-ahead).
// ════════════════════════════════════════════════════════════
TEST(CointegrationUnit, OlsIsCausalFutureDataDoesNotChangeFit) {
    std::vector<double> x = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<double> y = {3, 5, 7, 9, 11, 13, 15, 17, 19, 21};   // y = 1 + 2x

    const OlsFit before = olsFit(y, x, 4, 5);   // fenêtre [0..4]
    x[7] = -1000.0; y[8] = 999.0;               // pollue le FUTUR uniquement
    const OlsFit after  = olsFit(y, x, 4, 5);

    ASSERT_TRUE(before.ok);
    ASSERT_TRUE(after.ok);
    EXPECT_DOUBLE_EQ(before.beta,  after.beta);
    EXPECT_DOUBLE_EQ(before.alpha, after.alpha);
}

// ════════════════════════════════════════════════════════════
//  OLS : gardes — x constant (Var = 0), fenêtre trop courte, indices hors borne.
// ════════════════════════════════════════════════════════════
TEST(CointegrationUnit, OlsGuardsDegenerateInputs) {
    const std::vector<double> xConst = {2, 2, 2, 2, 2};
    const std::vector<double> y      = {1, 2, 3, 4, 5};
    EXPECT_FALSE(olsFit(y, xConst, 4, 5).ok);   // Var(x) = 0 → β indéfini
    EXPECT_FALSE(olsFit(y, y, 4, 1).ok);        // fenêtre < 2
    EXPECT_FALSE(olsFit(y, y, 2, 5).ok);        // fenêtre déborde avant 0
    EXPECT_FALSE(olsFit(y, y, 7, 3).ok);        // end hors série
}

// ════════════════════════════════════════════════════════════
//  Résidu d'Engle-Granger : e = y − α − β·x avec les coefficients FOURNIS
//  (nul sur la relation exacte ; gardes sur fit invalide / bornes).
// ════════════════════════════════════════════════════════════
TEST(CointegrationUnit, ResidualIsZeroOnExactRelationAndGuarded) {
    const std::vector<double> x = {1, 2, 3, 4, 5};
    std::vector<double> y;
    for (double v : x) y.push_back(1.0 + 2.0 * v);
    const OlsFit f = olsFit(y, x, 4, 5);

    const auto e = egResidual(y, x, f, 0, 5);
    ASSERT_EQ(e.size(), 5u);
    for (double v : e) EXPECT_NEAR(v, 0.0, 1e-12);

    OlsFit bad; bad.ok = false;
    EXPECT_TRUE(egResidual(y, x, bad, 0, 5).empty());   // fit invalide
    EXPECT_TRUE(egResidual(y, x, f, 3, 3).empty());     // plage vide
    EXPECT_TRUE(egResidual(y, x, f, 0, 9).empty());     // hors borne
}

// ════════════════════════════════════════════════════════════
//  Dickey-Fuller : un AR(1) DÉTERMINISTE e_{t+1} = 0,2·e_t donne un ajustement
//  parfait (RSS = 0) avec ρ = −0,8 < 0 → sentinel −999, largement sous toute
//  valeur critique → le gate s'ouvrirait (forte réversion).
// ════════════════════════════════════════════════════════════
TEST(CointegrationUnit, DfStronglyNegativeOnPerfectAr1Reversion) {
    std::vector<double> e = {1.0};
    for (int i = 0; i < 9; ++i) e.push_back(e.back() * 0.2);
    const double t = dfTStat(e);
    EXPECT_LE(t, -3.34);
    EXPECT_DOUBLE_EQ(t, -999.0);   // ajustement parfait documenté (sentinel signé)
}

// ════════════════════════════════════════════════════════════
//  Dickey-Fuller : réversion OSCILLANTE amortie (pas un AR(1) parfait) → t-stat
//  FINI et fortement négatif (sous la critique Engle-Granger −3,34).
// ════════════════════════════════════════════════════════════
TEST(CointegrationUnit, DfFiniteNegativeOnDampedOscillation) {
    const std::vector<double> e = {1.0, -0.5, 0.3, -0.2, 0.15, -0.1,
                                   0.08, -0.05, 0.04, -0.02};
    const double t = dfTStat(e);
    EXPECT_LT(t, -3.34);
    EXPECT_GT(t, -999.0);          // fini (pas le sentinel)
}

// ════════════════════════════════════════════════════════════
//  Dickey-Fuller : une série TENDANCIELLE ne passe jamais la critique
//  (marche divergente ≠ résidu stationnaire → gate fermé).
// ════════════════════════════════════════════════════════════
TEST(CointegrationUnit, DfDoesNotPassOnTrendingSeries) {
    // Tendance linéaire pure : Δe constant → ρ̂ = 0 → non concluant (0.0).
    const std::vector<double> lin = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    EXPECT_GT(dfTStat(lin), -3.34);

    // Tendance convexe (incréments croissants) : ρ̂ > 0 → t ≥ 0.
    const std::vector<double> cvx = {1.0, 2.1, 3.3, 4.6, 6.0, 7.5,
                                     9.1, 10.8, 12.6, 14.5};
    EXPECT_GE(dfTStat(cvx), 0.0);
}

// ════════════════════════════════════════════════════════════
//  Dickey-Fuller : gardes — série trop courte (< 8) ou constante → 0,0
//  (« non concluant », jamais sous une critique négative → gate fermé).
// ════════════════════════════════════════════════════════════
TEST(CointegrationUnit, DfGuardsShortOrConstantSeries) {
    EXPECT_DOUBLE_EQ(dfTStat({1.0, 0.5, 0.25}), 0.0);            // < 8 points
    EXPECT_DOUBLE_EQ(dfTStat(std::vector<double>(10, 3.14)), 0.0);   // constante
}
