// ============================================================
//  test_block_bootstrap_unit.cpp  —  Tests UNITAIRES
//  Cible : BlockBootstrapMonteCarlo (Sprint 23, item 23.7)
//  Bootstrap par blocs de trades consécutifs — complément du
//  Monte-Carlo IID historique (qui reste INCHANGÉ).
// ============================================================
#include <gtest/gtest.h>
#include "backtest/BlockBootstrap.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

TradeRecord trade(double pnlPct, double deployed = 1.0) {
    TradeRecord t;
    t.pnlPct = pnlPct;
    t.deployedFraction = deployed;
    t.isWin = pnlPct > 0;
    return t;
}

// Séquence synthétique AUTOCORRÉLÉE : 5 gains puis 5 pertes en grappe —
// le cas que l'IID détruit et que les blocs préservent.
std::vector<TradeRecord> grappes() {
    std::vector<TradeRecord> ts;
    for (int i = 0; i < 5; ++i) ts.push_back(trade(+4.0));
    for (int i = 0; i < 5; ++i) ts.push_back(trade(-4.0));
    return ts;
}

} // namespace

// Reproductibilité : même graine → mêmes percentiles, exactement
TEST(BlockBootstrapUnit, SameSeedGivesIdenticalResults) {
    const auto ts = grappes();
    const auto a = BlockBootstrapMonteCarlo(10'000.0, 3, 42, 500).run(ts, 2.0);
    const auto b = BlockBootstrapMonteCarlo(10'000.0, 3, 42, 500).run(ts, 2.0);
    EXPECT_DOUBLE_EQ(a.cagrP50, b.cagrP50);
    EXPECT_DOUBLE_EQ(a.ddP95,   b.ddP95);
    EXPECT_DOUBLE_EQ(a.ddP5,    b.ddP5);
}

// Une autre graine change (presque sûrement) le tirage
TEST(BlockBootstrapUnit, DifferentSeedChangesDraw) {
    const auto ts = grappes();
    const auto a = BlockBootstrapMonteCarlo(10'000.0, 3, 42,  500).run(ts, 2.0);
    const auto b = BlockBootstrapMonteCarlo(10'000.0, 3, 123, 500).run(ts, 2.0);
    EXPECT_NE(a.ddP50, b.ddP50);
}

// blockSize ≥ n : chaque chemin EST la séquence observée → distribution
// dégénérée. Équité finale = 10 000 × 1,04⁵ × 0,96⁵ ; drawdown = celui de
// la grappe de 5 pertes : 1 − 0,96⁵ = 18,46 %.
TEST(BlockBootstrapUnit, BlockCoveringWholeSequenceReproducesObservedPath) {
    const auto ts = grappes();
    const auto r = BlockBootstrapMonteCarlo(10'000.0, 10, 42, 200).run(ts, 1.0);

    const double ddAttendu = (1.0 - std::pow(0.96, 5)) * 100.0;
    EXPECT_NEAR(r.ddP5,  ddAttendu, 1e-9);
    EXPECT_NEAR(r.ddP50, ddAttendu, 1e-9);
    EXPECT_NEAR(r.ddP95, ddAttendu, 1e-9);

    const double finale = 10'000.0 * std::pow(1.04, 5) * std::pow(0.96, 5);
    const double cagrAttendu = (finale / 10'000.0 - 1.0) * 100.0;  // 1 an
    EXPECT_NEAR(r.cagrP50, cagrAttendu, 1e-9);
}

// Les blocs CONSERVENT l'ordre interne : avec blockSize 1 le bootstrap par
// blocs EST l'IID — sur une séquence en grappes, la queue de drawdown IID
// (blocs de 1) doit être ≤ celle des blocs de 5 qui embarquent la grappe
// de pertes entière.
TEST(BlockBootstrapUnit, LargerBlocksPreserveLossClustersInTheTail) {
    const auto ts = grappes();
    const auto bloc1 = BlockBootstrapMonteCarlo(10'000.0, 1, 42, 2'000).run(ts, 2.0);
    const auto bloc5 = BlockBootstrapMonteCarlo(10'000.0, 5, 42, 2'000).run(ts, 2.0);
    // La grappe complète de 5 pertes (−18,46 %) est GARANTIE d'apparaître
    // dans les chemins de blocs 5 dès qu'un départ tombe sur elle ; en IID
    // il faut tirer 5 pertes d'affilée (rare). p95 blocs ≥ p95 IID.
    EXPECT_GE(bloc5.ddP95, bloc1.ddP95);
}

// Le Monte-Carlo IID historique reste ce qu'il était : mêmes conventions
// (deployedFraction × pnlPct) — un trade à moitié déployé pèse moitié moins.
// (Les goldens de MonteCarloIntegration verrouillent déjà l'IID ; ici on
// vérifie que la variante blocs applique la MÊME convention D45.)
TEST(BlockBootstrapUnit, DeployedFractionScalesReturnsLikeHistoricalMc) {
    std::vector<TradeRecord> plein  = { trade(-10.0, 1.0) };
    std::vector<TradeRecord> moitie = { trade(-10.0, 0.5) };
    const auto rPlein  = BlockBootstrapMonteCarlo(10'000.0, 1, 42, 100).run(plein, 1.0);
    const auto rMoitie = BlockBootstrapMonteCarlo(10'000.0, 1, 42, 100).run(moitie, 1.0);
    EXPECT_NEAR(rPlein.ddP50,  10.0, 1e-9);
    EXPECT_NEAR(rMoitie.ddP50,  5.0, 1e-9);
}

// Entrées dégénérées : pas de trades ou pas de chemins → résultat nul
TEST(BlockBootstrapUnit, DegenerateInputsGiveNullResult) {
    const auto r1 = BlockBootstrapMonteCarlo(10'000.0, 3, 42, 100).run({}, 1.0);
    EXPECT_EQ(r1.paths, 0u);
    const auto r2 = BlockBootstrapMonteCarlo(10'000.0, 3, 42, 0).run(grappes(), 1.0);
    EXPECT_EQ(r2.paths, 0u);
}

// Le nombre de trades par chemin est calé sur n : le CAGR d'une séquence
// tout-gagnante ne dépend pas de la taille de bloc (pas de sur-tirage).
TEST(BlockBootstrapUnit, PathLengthIsAlwaysNTrades) {
    std::vector<TradeRecord> gains;
    for (int i = 0; i < 7; ++i) gains.push_back(trade(+2.0));
    const auto b2 = BlockBootstrapMonteCarlo(10'000.0, 2, 42, 50).run(gains, 1.0);
    const auto b3 = BlockBootstrapMonteCarlo(10'000.0, 3, 42, 50).run(gains, 1.0);
    const double attendu = (std::pow(1.02, 7) - 1.0) * 100.0;
    EXPECT_NEAR(b2.cagrP50, attendu, 1e-9);
    EXPECT_NEAR(b3.cagrP50, attendu, 1e-9);
}
