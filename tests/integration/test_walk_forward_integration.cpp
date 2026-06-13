// ─── Tests d'intégration : walk-forward sur QQQ.csv (Sprint 7.1, D24) ────────
// Rejoue la config de production en fenêtres glissantes IS/OOS sur les données
// réelles. Objectif : produire le verdict honnête « l'edge tient-il hors
// échantillon ? ». Le nombre de fenêtres est figé (golden) : déterministe pour
// des paramètres et un CSV donnés ; toute dérive (taille du CSV, paramètres)
// le casse.
#include <gtest/gtest.h>
#include "backtest/WalkForward.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

// QQQ.csv = 1858 barres. IS = 378 (~1,5 an), OOS = 126 (~0,5 an), pas = 126 :
// start ∈ {0,126,…,1260} (start+504 ≤ 1858) → 11 fenêtres.
WalkForwardReport runProdWalkForward() {
    CsvDataFeed feed(SWINGBOT_QQQ_CSV);
    WalkForwardValidator wf(prodSwingConfig(), 10'000.0, 0.001);
    return wf.run(feed.allBars(), /*isBars=*/378, /*oosBars=*/126, /*stepBars=*/126);
}

} // namespace

TEST(WalkForwardIntegration, ProdConfigProducesExpectedWindows) {
    const auto r = runProdWalkForward();

    EXPECT_EQ(r.windows.size(), 11u);

    // Chaque fenêtre est bien ordonnée dans le temps et auto-contenue :
    // IS précède OOS, et OOS commence juste après la fin de l'IS.
    for (const auto& w : r.windows) {
        EXPECT_LT(w.isStartDate, w.isEndDate);
        EXPECT_LT(w.oosStartDate, w.oosEndDate);
        EXPECT_LT(w.isEndDate, w.oosStartDate);
        // Période OOS = 126 barres → courbe d'équité de 126 points.
        EXPECT_EQ(w.oos.equityCurve.size(), 126u);
    }

    // La première fenêtre démarre à la première barre du CSV (2019-01-02).
    EXPECT_EQ(r.windows.front().isStartDate, "2019-01-02");
}

// Le harnais doit RENDRE UN VERDICT, pas seulement des chiffres : on vérifie
// que l'agrégat de sur-ajustement est cohérent et que le verdict en découle.
// (On ne fige pas le signe de l'alpha OOS : la stratégie change au Sprint 8 ;
// ce qui doit tenir, c'est la LOGIQUE de jugement.)
TEST(WalkForwardIntegration, VerdictFollowsFromOverfitCount) {
    const auto r = runProdWalkForward();

    int counted = 0;
    for (const auto& w : r.windows)
        if (WalkForwardValidator::isOverfit(w.is, w.oos)) counted++;
    EXPECT_EQ(counted, r.overfitWindows);

    // « généralise » ⇒ alpha OOS moyen positif ET aucune fenêtre sur-ajustée.
    if (r.generalizes) {
        EXPECT_GT(r.avgOosAlpha, 0.0);
        EXPECT_EQ(r.overfitWindows, 0);
    }
}
