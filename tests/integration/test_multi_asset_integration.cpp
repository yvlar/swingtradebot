// ─── Tests d'intégration : multi-actifs total-return (item 7.4, D29) ─────────
// La stratégie ne doit pas être validée sur un seul actif (QQQ) ni sur un seul
// régime. Ce test la fait tourner sur ≥ 3 actifs réels (SPY, IWM, MDY) servis
// en série total-return (Adj Close ≠ Close, dividendes réinvestis), et vérifie
// le garde-fou de qualité de données (auditTotalReturnCsv) qui détecte un export
// sans dividendes (symptôme D29). Chemins CSV injectés par CMake.
#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "backtest/BackTester.hpp"
#include "backtest/DataQuality.hpp"
#include "backtest/WalkForward.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;
namespace fs = std::filesystem;

namespace {
struct Asset { std::string nom; std::string csv; };

std::vector<Asset> actifs() {
    return {
        {"SPY", SWINGBOT_SPY_CSV},
        {"IWM", SWINGBOT_IWM_CSV},
        {"MDY", SWINGBOT_MDY_CSV},
    };
}

// ── Sprint 8-bis, item 8b.2 : chaîne v2 EXPLICITE champ par champ (D33) ──────
// Un verdict est une mesure HISTORIQUE : jamais `SwingConfig{}` dans un verrou
// (duplication volontaire avec les autres fichiers de verdict — chaque fichier
// porte son propre snapshot). Le symbole est ajusté par actif à l'appel.
SwingConfig cfgChaineV2() {
    SwingConfig c;
    c.symbol                  = "QQQ";
    c.emaFast                 = 9;
    c.emaSlow                 = 21;
    c.rsiPeriod               = 14;
    c.rsiBuyMax               = 100.0;   // 8.3 : plafond d'achat désactivé
    c.rsiSellMin              = 70.0;
    c.stopLossPct             = 0.05;
    c.takeProfitPct           = 0.0;     // 8.2 : take-profit désactivé
    c.trailingStopPct         = 0.03;
    c.riskPerTradePct         = 0.02;
    c.minHoldDays             = 3;
    c.smaTrendPeriod          = 200;     // 8.1 : filtre de régime
    c.rsiSellOnlyIfRegimeDown = true;    // 8.4 : vente RSI gatée
    c.regimeReentry           = true;    // 8.5 : re-entrée sur régime
    return c;
}

// Pavage FIN (item 8b.3) : mêmes fenêtres que le verdict QQQ pour une
// comparaison actif-à-actif honnête (4 fenêtres OOS contiguës).
constexpr size_t IS_FIN   = 500;
constexpr size_t OOS_FIN  = 300;
constexpr size_t STEP_FIN = 300;

// Agrégats du verdict par actif (mêmes définitions que les verrous QQQ).
struct VerdictActif {
    size_t fenetres = 0;
    double alphaOos = 0.0;   // alpha OOS moyen (pts vs B&H de l'actif)
    double expoOos  = 0.0;   // % temps investi OOS moyen
    size_t trades   = 0;     // trades OOS poolés (garde-fou D34)
};

VerdictActif verdictChaineSur(const Asset& a) {
    SwingConfig c = cfgChaineV2();
    c.symbol = a.nom;
    const auto w = WalkForward(c, a.csv, IS_FIN, OOS_FIN, STEP_FIN).run();
    VerdictActif v;
    v.fenetres = w.size();
    for (const auto& x : w) {
        v.alphaOos += x.oos.alpha;
        v.expoOos  += x.oos.pctTimeInvested;
        v.trades   += x.oos.trades.size();
        EXPECT_TRUE(std::isfinite(x.oos.alpha)) << a.nom;
    }
    if (!w.empty()) {
        v.alphaOos /= static_cast<double>(w.size());
        v.expoOos  /= static_cast<double>(w.size());
    }
    std::cout << std::fixed << std::setprecision(4)
              << "  ITEM 8b.2 — CHAINE v2 sur " << a.nom
              << " (" << v.fenetres << " fenetres OOS, IS=" << IS_FIN
              << "/OOS=" << OOS_FIN << ")\n"
              << "    alpha OOS moyen : " << v.alphaOos << "\n"
              << "    % temps investi : " << v.expoOos << "\n"
              << "    trades OOS      : " << v.trades << "\n";
    return v;
}
} // namespace

// ════════════════════════════════════════════════════════════
//  La stratégie tourne sur ≥ 3 actifs sans casser
// ════════════════════════════════════════════════════════════
TEST(MultiAssetIntegration, StrategyEvaluatedOnAtLeastThreeAssets) {
    const auto liste = actifs();
    ASSERT_GE(liste.size(), 3u);

    std::cout << "  Multi-actifs (config prod, total-return)\n"
              << "  " << std::left << std::setw(6) << "Actif"
              << std::right << std::setw(12) << "Retour%"
              << std::setw(12) << "B&H%" << std::setw(12) << "Alpha"
              << std::setw(10) << "Trades" << std::setw(10) << "CAGR%" << "\n";

    for (const auto& a : liste) {
        Backtester bt(prodSwingConfig(), a.csv, 10'000.0, 0.001);
        const auto r = bt.run();

        // Le backtest produit des métriques finies et une courbe d'équité.
        EXPECT_GT(r.equityCurve.size(), 0u) << a.nom;
        EXPECT_TRUE(std::isfinite(r.totalReturnPct))   << a.nom;
        EXPECT_TRUE(std::isfinite(r.buyHoldReturnPct))  << a.nom;
        EXPECT_TRUE(std::isfinite(r.cagrPct))           << a.nom;
        EXPECT_GE(r.totalTrades, 0)                     << a.nom;

        std::cout << "  " << std::left << std::setw(6) << a.nom
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(12) << r.totalReturnPct
                  << std::setw(12) << r.buyHoldReturnPct
                  << std::setw(12) << r.alpha
                  << std::setw(10) << r.totalTrades
                  << std::setw(10) << r.cagrPct << "\n";
    }
}

// ════════════════════════════════════════════════════════════
//  Les dividendes sont bel et bien comptés (vs le QQQ.csv d'avant, D29)
// ════════════════════════════════════════════════════════════
TEST(MultiAssetIntegration, DividendsCountedOnTotalReturnAssets) {
    // Chaque actif réel doit porter de l'information de dividende :
    // Adj Close ≠ Close sur au moins une partie des lignes.
    for (const auto& a : actifs()) {
        const auto q = auditTotalReturnCsv(a.csv);
        EXPECT_GT(q.rows, 0u) << a.nom;
        EXPECT_FALSE(q.suspectNoDividends)
            << a.nom << " : Adj Close == Close partout (export sans dividende ?)";
        EXPECT_LT(q.adjEqualsCloseRows, q.rows)
            << a.nom << " : aucune ligne ajustée";
    }

    // QQQ ré-exporté en total-return (item 7.4) n'est plus suspect non plus.
    const auto qqq = auditTotalReturnCsv(SWINGBOT_QQQ_CSV);
    EXPECT_GT(qqq.rows, 0u);
    EXPECT_FALSE(qqq.suspectNoDividends)
        << "QQQ.csv devrait être total-return après le ré-export (D29)";
}

// ════════════════════════════════════════════════════════════
//  Le garde-fou lève bien le drapeau sur un CSV sans dividende
// ════════════════════════════════════════════════════════════
TEST(MultiAssetIntegration, GuardFlagsSyntheticNoDividendCsv) {
    // Reconstitue le symptôme D29 dans un fichier temporaire : Adj == Close.
    const std::string path = "integ_dq_nodiv_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv";
    {
        std::ofstream f(path);
        f << "Date,Open,High,Low,Close,Adj Close,Volume\n"
          << "2024-01-02,100,101,99,100.50,100.50,1000\n"
          << "2024-01-03,100.50,102,100,101.20,101.20,1100\n"
          << "2024-01-04,101.20,103,101,102.00,102.00,1200\n";
    }
    const auto r = auditTotalReturnCsv(path);
    EXPECT_TRUE(r.suspectNoDividends);
    EXPECT_EQ(r.rows, r.adjEqualsCloseRows);
    fs::remove(path);
}

// ════════════════════════════════════════════════════════════
//  Sprint 8-bis, item 8b.2 : verdict OOS de la chaîne v2 PAR ACTIF
//  (mêmes fenêtres que le verdict QQQ — un edge qui ne tient que
//  sur QQQ est un artefact. Verdict QQQ de référence :
//  SprintChainFinePavingOosVerdictIsLocked, alpha −7,15, 13 trades.)
//
//  VERDICT MESURÉ (figé) : AUCUN actif ne rend un alpha OOS > 0 —
//  pas d'edge multi-actifs pour la chaîne v2. MAIS le comportement
//  est COHÉRENT d'un actif à l'autre (la chaîne trade partout en
//  OOS : 10-17 trades, exposition 53-81 %) et la sous-performance
//  est PLUS FAIBLE que sur QQQ : SPY −5,52, IWM −3,01, MDY −3,60
//  (vs −7,15 sur QQQ, chacun vs le B&H de SON actif). La chaîne
//  n'est donc pas un artefact QQQ : elle se comporte pareil partout,
//  et perd d'autant moins que le B&H de l'actif est moins violent.
// ════════════════════════════════════════════════════════════

TEST(MultiAssetIntegration, V2ChainOosVerdictLockedOnSpy) {
    const auto v = verdictChaineSur({"SPY", SWINGBOT_SPY_CSV});
    ASSERT_GE(v.fenetres, 4u);
    EXPECT_NEAR(v.alphaOos, -5.5162, 1e-2);   // golden de mesure
    EXPECT_NEAR(v.expoOos,  80.8081, 1e-2);
    EXPECT_EQ(v.trades, 10u);                 // garde-fou D34 : non vide
    EXPECT_LT(v.alphaOos, 0.0);               // pas d'edge sur SPY
}

TEST(MultiAssetIntegration, V2ChainOosVerdictLockedOnIwm) {
    const auto v = verdictChaineSur({"IWM", SWINGBOT_IWM_CSV});
    ASSERT_GE(v.fenetres, 4u);
    EXPECT_NEAR(v.alphaOos, -3.0086, 1e-2);   // golden de mesure
    EXPECT_NEAR(v.expoOos,  52.7778, 1e-2);
    EXPECT_EQ(v.trades, 13u);                 // garde-fou D34 : non vide
    EXPECT_LT(v.alphaOos, 0.0);               // pas d'edge sur IWM
}

TEST(MultiAssetIntegration, V2ChainOosVerdictLockedOnMdy) {
    const auto v = verdictChaineSur({"MDY", SWINGBOT_MDY_CSV});
    ASSERT_GE(v.fenetres, 4u);
    EXPECT_NEAR(v.alphaOos, -3.6029, 1e-2);   // golden de mesure
    EXPECT_NEAR(v.expoOos,  68.4343, 1e-2);
    EXPECT_EQ(v.trades, 17u);                 // garde-fou D34 : non vide
    EXPECT_LT(v.alphaOos, 0.0);               // pas d'edge sur MDY
}
