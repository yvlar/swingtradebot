// ─── main_validate.cpp ───────────────────────────────────────────────────────
// Exécutable du HARNAIS DE VALIDATION (Sprint 7). Imprime, pour la stratégie
// telle qu'elle est câblée en production (ProdConfig.hpp) :
//   1. le backtest de référence sur QQQ (total-return),
//   2. le rapport walk-forward IS/OOS (item 7.1),
//   3. la carte de sensibilité de grille jugée en OOS (item 7.2),
//   4. la distribution Monte-Carlo CAGR/drawdown (item 7.3),
//   5. l'audit qualité des données multi-actifs (item 7.4, D29).
//
// Sortie humaine, non testée par un golden (les tests d'intégration verrouillent
// les chiffres) ; c'est l'outil à lancer pour INSPECTER l'edge — ou son absence.
//
// Chemins CSV injectés par CMake (SWINGBOT_*_CSV). Build : cible `validate`.
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "backtest/BackTester.hpp"
#include "backtest/WalkForward.hpp"
#include "backtest/GridOptimizer.hpp"
#include "backtest/MonteCarlo.hpp"
#include "backtest/DataQuality.hpp"
#include "backtest/RotationBacktester.hpp"
#include "backtest/PairsBacktester.hpp"
#include "backtest/VolRegimeBacktester.hpp"
#include "backtest/VixRegimeBacktester.hpp"
#include "backtest/VolScaledBacktester.hpp"
#include "backtest/VixScaledBacktester.hpp"
#include "backtest/CointPairsBacktester.hpp"
#include "backtest/VixTermRegimeBacktester.hpp"
#include "backtest/VixTermScaledBacktester.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

// Durée calendaire (années) entre deux dates « YYYY-MM-DD ».
long daysFromCivil(const std::string& date) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return 0;
    y -= m <= 2;
    const long     era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
}

void titre(const std::string& t) {
    std::cout << "\n" << std::string(60, '#') << "\n## " << t << "\n"
              << std::string(60, '#') << "\n";
}

} // namespace

int main() {
    const std::string qqq = SWINGBOT_QQQ_CSV;
    // Config prod chargée depuis config/prod.json (item 9.1) — échec bruyant
    SwingConfig cfg;
    try {
        cfg = prodSwingConfig();
    } catch (const std::exception& e) {
        std::cerr << "❌ Config de production invalide : " << e.what() << "\n";
        return 1;
    }

    // 1. Backtest de référence ────────────────────────────────────────────────
    titre("1. BACKTEST DE REFERENCE — QQQ total-return (config prod)");
    Backtester bt(cfg, qqq, 10'000.0, 0.001);
    const auto bk = bt.run();
    bt.printReport(bk);

    // 2. Walk-forward IS/OOS ──────────────────────────────────────────────────
    titre("2. WALK-FORWARD (in-sample vs out-of-sample)");
    WalkForward wf(cfg, qqq, /*IS=*/700, /*OOS=*/300, /*pas=*/300);
    const auto windows = wf.run();
    wf.printReport(windows);

    // 2-bis. Walk-forward FIN (Sprint 8-bis, item 8b.3, D34) ──────────────────
    // 4 fenêtres OOS au lieu de 2 : OOS=300 (et non 250) pour laisser ~99
    // barres tradables après le warmup local de ~201 barres (SMA200).
    titre("2-bis. WALK-FORWARD FIN (IS=500/OOS=300, 4 fenetres — item 8b.3)");
    WalkForward wfFin(cfg, qqq, /*IS=*/500, /*OOS=*/300, /*pas=*/300);
    wfFin.printReport(wfFin.run());

    // 3. Optimiseur de grille jugé en OOS ─────────────────────────────────────
    // Grille PLEINE sur les axes qui pilotent la chaîne v2 (Sprint 8-bis,
    // item 8b.1) : emaFast × emaSlow × smaTrendPeriod × trailingStopPct =
    // 81 combos, jugés sur le pavage FIN (4 fenêtres OOS, item 8b.3). Les
    // axes morts de l'ancienne stratégie (rsiBuyMax, TP) restent en singleton
    // à leur valeur de chaîne. Hors timeout : le verrou CI est la grille
    // réduite de test_grid_optimizer_integration.cpp.
    titre("3. CARTE DE SENSIBILITE (grille chaine v2, Sharpe OOS, pavage fin)");
    auto objectifOos = [&](const SwingConfig& c) -> GridScore {
        WalkForward w(c, qqq, 500, 300, 300);
        const auto ws = w.run();
        GridScore s;
        if (ws.empty()) return s;
        double sh = 0, al = 0, dd = 0;
        for (const auto& x : ws) { sh += x.oos.sharpeRatio; al += x.oos.alpha; dd += x.oos.maxDrawdownPct; }
        const double k = static_cast<double>(ws.size());
        s.metric = sh / k; s.alpha = al / k; s.drawdown = dd / k;
        return s;
    };
    GridOptimizer opt({5, 9, 13}, {21, 34, 50}, {100}, {70}, {0.05}, {0.0},
                      objectifOos, cfg,
                      /*smaTrend*/ {150, 200, 250},
                      /*trailing*/ {0.03, 0.05, 0.08});
    opt.printSensitivityMap(opt.evaluate());

    // 4. Monte-Carlo des trades ───────────────────────────────────────────────
    titre("4. MONTE-CARLO (bootstrap des trades, graine 42)");
    double years = 0.0;
    if (bk.equityDates.size() >= 2)
        years = (daysFromCivil(bk.equityDates.back()) -
                 daysFromCivil(bk.equityDates.front())) / 365.25;
    const auto mc = MonteCarlo(10'000.0, 42, 2000).run(bk.trades, years);
    std::cout << std::fixed << std::setprecision(2)
              << "  " << bk.trades.size() << " trades, " << mc.paths
              << " chemins, " << years << " ans\n"
              << "  CAGR     p5=" << mc.cagrP5 << "%  p50=" << mc.cagrP50
              << "%  p95=" << mc.cagrP95 << "%\n"
              << "  Drawdown p5=" << mc.ddP5 << "%  p50=" << mc.ddP50
              << "%  p95=" << mc.ddP95 << "%\n";

    // 5. Qualité des données multi-actifs ─────────────────────────────────────
    titre("5. QUALITE DES DONNEES (total-return, D29)");
    const std::vector<std::pair<std::string, std::string>> actifs = {
        {"QQQ", qqq}, {"SPY", SWINGBOT_SPY_CSV},
        {"IWM", SWINGBOT_IWM_CSV}, {"MDY", SWINGBOT_MDY_CSV},
    };
    for (const auto& a : actifs) {
        const auto q = auditTotalReturnCsv(a.second);
        std::cout << "  " << std::left << std::setw(5) << a.first
                  << q.adjEqualsCloseRows << "/" << q.rows
                  << " lignes Adj==Close  "
                  << (q.suspectNoDividends ? "SUSPECT (sans dividende ?)"
                                           : "ok (total-return)") << "\n";
    }

    // 6. Walk-forward multi-actifs (Sprint 8-bis, item 8b.2) ──────────────────
    // La chaîne v2 ET le candidat de la grille 8b.1, jugés par actif sur le
    // pavage fin — un edge qui ne tient que sur QQQ est un artefact.
    // Candidat RE-CALÉ post-B2 (D37, Sprint 8-ter) : (emaFast=9, smaT=250,
    // trail=0,03) — emaFast et trail sont déjà les valeurs de la chaîne, seul
    // smaTrendPeriod diffère (l'ancien trail=0,05 était le plateau pré-B2).
    titre("6. WALK-FORWARD MULTI-ACTIFS (chaine v2 et candidat 8b.1, pavage fin)");
    SwingConfig candidat = cfg;
    candidat.smaTrendPeriod  = 250;
    candidat.trailingStopPct = 0.03;
    const std::vector<std::pair<std::string, SwingConfig>> variantes = {
        {"chaine v2       ", cfg}, {"candidat 8b.1   ", candidat},
    };
    std::cout << "  " << std::left << std::setw(20) << "Config"
              << std::setw(7) << "Actif"
              << std::right << std::setw(14) << "alpha OOS moy"
              << std::setw(12) << "trades OOS" << "\n";
    for (const auto& v : variantes) {
        for (const auto& a : actifs) {
            SwingConfig c = v.second;
            c.symbol = a.first;
            const auto ws = WalkForward(c, a.second, 500, 300, 300).run();
            double alpha = 0; size_t trades = 0;
            for (const auto& w : ws) { alpha += w.oos.alpha; trades += w.oos.trades.size(); }
            if (!ws.empty()) alpha /= static_cast<double>(ws.size());
            std::cout << "  " << std::left << std::setw(20) << v.first
                      << std::setw(7) << a.first
                      << std::right << std::fixed << std::setprecision(4)
                      << std::setw(14) << alpha
                      << std::setw(12) << trades << "\n";
        }
    }

    // 7. Validation HORS-GRILLE du candidat (Sprint 8-ter, item 8t.1) ─────────
    // Le candidat vs la chaîne v2 sur des fenêtres que la grille 8b.1 n'a PAS
    // choisies : pavage canonique (700/400) et pavage DÉCALÉ (500/400,
    // offset=90 — aucune borne commune avec les pavages historiques, les 90
    // dernières barres jugées en OOS pour la première fois). Les verrous CI
    // sont dans test_candidate_validation_integration.cpp.
    titre("7. VALIDATION HORS-GRILLE DU CANDIDAT (Sprint 8-ter, 8t.1)");
    const std::vector<std::pair<std::string, SwingConfig>> duel = {
        {"chaine v2", cfg}, {"candidat 8b.1", candidat},
    };
    const struct { const char* nom; size_t is, oos, pas, dec; } pavages[] = {
        {"canonique (IS=700/OOS=400)",        700, 400, 400,  0},
        {"decale (IS=500/OOS=400, offset=90)", 500, 400, 400, 90},
    };
    for (const auto& p : pavages) {
        std::cout << "\n  Pavage " << p.nom << " :\n";
        for (const auto& v : duel) {
            WalkForward w(v.second, qqq, p.is, p.oos, p.pas, p.dec);
            const auto ws = w.run();
            w.printReport(ws);
            double alpha = 0; size_t trades = 0;
            for (const auto& x : ws) { alpha += x.oos.alpha; trades += x.oos.trades.size(); }
            if (!ws.empty()) alpha /= static_cast<double>(ws.size());
            std::cout << "  -> " << v.first << " : alpha OOS moyen "
                      << std::fixed << std::setprecision(4) << alpha
                      << " pt, " << trades << " trades OOS pooles\n";
        }
    }

    // 7-bis. Monte-Carlo du candidat (Sprint 8-ter, item 8t.2) ────────────────
    // Bootstrap des trades OOS poolés du pavage DÉCALÉ (fenêtres disjointes et
    // non-choisies), même dénominateur d'années pour les deux configs.
    titre("7-bis. MONTE-CARLO DU CANDIDAT (trades OOS pooles, 8t.2)");
    for (const auto& v : duel) {
        const auto ws = WalkForward(v.second, qqq, 500, 400, 400, 90).run();
        std::vector<TradeRecord> pool;
        for (const auto& x : ws)
            pool.insert(pool.end(), x.oos.trades.begin(), x.oos.trades.end());
        double ans = 0.0;
        if (!ws.empty() && !ws.front().oos.equityDates.empty() &&
            !ws.back().oos.equityDates.empty())
            ans = (daysFromCivil(ws.back().oos.equityDates.back()) -
                   daysFromCivil(ws.front().oos.equityDates.front())) / 365.25;
        if (pool.empty() || ans <= 0.0) {
            std::cout << "  " << v.first
                      << " : aucun trade OOS (verdict cash drag, D34)\n";
            continue;
        }
        const auto r = MonteCarlo(10'000.0, 42, 2000).run(pool, ans);
        std::cout << std::fixed << std::setprecision(2)
                  << "  " << v.first << " (" << pool.size() << " trades, "
                  << ans << " ans)\n"
                  << "    CAGR     p5=" << r.cagrP5 << "%  p50=" << r.cagrP50
                  << "%  p95=" << r.cagrP95 << "%\n"
                  << "    Drawdown p5=" << r.ddP5 << "%  p50=" << r.ddP50
                  << "%  p95=" << r.ddP95 << "%\n";
    }

    // 7-ter. Grille de confirmation resserrée (Sprint 8-ter, item 8t.3) ───────
    // Voisinage fin CENTRÉ sur le candidat (27 combos, mêmes objectif/pavage
    // que la grille 8b.1) : un vrai plateau doit rester alpha > 0 ET retenir
    // smaT=250 quand on resserre les crans (D36).
    titre("7-ter. GRILLE DE CONFIRMATION RESSERREE AUTOUR DU CANDIDAT (8t.3)");
    GridOptimizer optSerre({7, 9, 11}, {21}, {100}, {70}, {0.05}, {0.0},
                           objectifOos, cfg,
                           /*smaTrend*/ {225, 250, 275},
                           /*trailing*/ {0.02, 0.03, 0.04});
    optSerre.printSensitivityMap(optSerre.evaluate());

    // 8. Trailing ATR (Sprint 8-quater, item 8q.2) ────────────────────────────
    // La chaîne v2 (trailing % fixe 0,03) contre chaîne + trailing ATR(14)
    // mult ∈ {2, 3, 4}, sur les TROIS pavages (leçon 8-ter : fenêtres variées
    // d'emblée, le mult est choisi sur le FIN, jugé sur les deux autres).
    // Les verrous CI sont dans test_trailing_atr_integration.cpp.
    titre("8. TRAILING ATR (chaine v2 vs mult 2/3/4, trois pavages — 8q.2)");
    std::vector<std::pair<std::string, SwingConfig>> duelAtr = {
        {"chaine v2 (3%)", cfg},
    };
    for (double mult : {2.0, 3.0, 4.0}) {
        SwingConfig c = cfg;
        c.trailingAtrMult = mult;
        duelAtr.push_back({"ATR mult=" + std::to_string(static_cast<int>(mult)), c});
    }
    const struct { const char* nom; size_t is, oos, pas, dec; } pavagesAtr[] = {
        {"fin (IS=500/OOS=300, CHOIX du mult)",  500, 300, 300,  0},
        {"canonique (IS=700/OOS=400)",           700, 400, 400,  0},
        {"decale (IS=500/OOS=400, offset=90)",   500, 400, 400, 90},
    };
    for (const auto& p : pavagesAtr) {
        std::cout << "\n  Pavage " << p.nom << " :\n";
        for (const auto& v : duelAtr) {
            const auto ws = WalkForward(v.second, qqq, p.is, p.oos, p.pas, p.dec).run();
            double alpha = 0; size_t trades = 0;
            for (const auto& x : ws) { alpha += x.oos.alpha; trades += x.oos.trades.size(); }
            if (!ws.empty()) alpha /= static_cast<double>(ws.size());
            std::cout << "  -> " << std::left << std::setw(16) << v.first
                      << std::right << " : alpha OOS moyen "
                      << std::fixed << std::setprecision(4) << alpha
                      << " pt, " << trades << " trades OOS pooles ("
                      << ws.size() << " fenetres)\n";
        }
    }

    // 9. Familles de signaux (Sprint 8-quinquies, item 8s.3) ──────────────────
    // La chaîne v2 contre : sortie STRUCTURELLE « plus bas de N jours »
    // (exitOnLowestLowN, 8s.1) et entrée BREAKOUT « plus haut de M jours »
    // (entryBreakoutM, 8s.2 — variantes « s'ajoute » et « remplace » vis-à-vis
    // de la re-entrée 8.5, décision utilisateur 2026-07-03), sur les TROIS
    // pavages (le réglage est choisi sur le FIN, jugé sur les deux autres).
    // Les verrous CI sont dans test_signal_families_integration.cpp.
    titre("9. FAMILLES DE SIGNAUX (chaine v2 vs structure/breakout, trois pavages — 8s.3)");
    std::vector<std::pair<std::string, SwingConfig>> duelSignaux = {
        {"chaine v2", cfg},
    };
    for (int nStruct : {10, 20, 55}) {
        SwingConfig c = cfg;
        c.exitOnLowestLowN = nStruct;
        duelSignaux.push_back({"structure N=" + std::to_string(nStruct), c});
    }
    for (int mBreak : {20, 55}) {
        SwingConfig cAjout = cfg;
        cAjout.entryBreakoutM = mBreak;
        duelSignaux.push_back({"breakout M=" + std::to_string(mBreak) + " +", cAjout});
        SwingConfig cRemplace = cAjout;
        cRemplace.regimeReentry = false;
        duelSignaux.push_back({"breakout M=" + std::to_string(mBreak) + " rempl.", cRemplace});
    }
    for (const auto& p : pavagesAtr) {
        std::cout << "\n  Pavage " << p.nom << " :\n";
        for (const auto& v : duelSignaux) {
            const auto ws = WalkForward(v.second, qqq, p.is, p.oos, p.pas, p.dec).run();
            double alpha = 0; size_t trades = 0;
            for (const auto& x : ws) { alpha += x.oos.alpha; trades += x.oos.trades.size(); }
            if (!ws.empty()) alpha /= static_cast<double>(ws.size());
            std::cout << "  -> " << std::left << std::setw(21) << v.first
                      << std::right << " : alpha OOS moyen "
                      << std::fixed << std::setprecision(4) << alpha
                      << " pt, " << trades << " trades OOS pooles ("
                      << ws.size() << " fenetres)\n";
        }
    }

    // 10. 3e famille de signaux (Sprint 8-sexies, item 8y.3) ──────────────────
    // La chaîne v2 contre : entrée PULLBACK « RSI ≤ seuil en régime up »
    // (entryPullbackRsiMax, 8y.1 — variantes « s'ajoute » et « remplace »
    // vis-à-vis de la re-entrée 8.5, comme le breakout 8s.3) et filtre de
    // VOLATILITÉ sur les entrées (entryMaxAtrPct, 8y.2 — ATR(14)/clôture,
    // crans {0,010, 0,015, 0,025}, cran bas ajouté par décision utilisateur
    // d'ouverture), sur les TROIS pavages (le réglage est choisi sur le FIN,
    // jugé sur les deux autres). Les verrous CI sont dans
    // test_pullback_volatility_integration.cpp.
    titre("10. 3e FAMILLE (chaine v2 vs pullback/filtre ATR, trois pavages — 8y.3)");
    std::vector<std::pair<std::string, SwingConfig>> duelPullback = {
        {"chaine v2", cfg},
    };
    for (double rsiMax : {30.0, 40.0, 50.0}) {
        SwingConfig cAjout = cfg;
        cAjout.entryPullbackRsiMax = rsiMax;
        duelPullback.push_back({"pullback RSI<=" + std::to_string(static_cast<int>(rsiMax)) + " +", cAjout});
        SwingConfig cRemplace = cAjout;
        cRemplace.regimeReentry = false;
        duelPullback.push_back({"pullback RSI<=" + std::to_string(static_cast<int>(rsiMax)) + " rempl.", cRemplace});
    }
    for (double seuilAtr : {0.010, 0.015, 0.025}) {
        SwingConfig c = cfg;
        c.entryMaxAtrPct = seuilAtr;
        std::ostringstream nomAtr;
        nomAtr << "filtre ATR " << std::fixed << std::setprecision(3) << seuilAtr;
        duelPullback.push_back({nomAtr.str(), c});
    }
    for (const auto& p : pavagesAtr) {
        std::cout << "\n  Pavage " << p.nom << " :\n";
        for (const auto& v : duelPullback) {
            const auto ws = WalkForward(v.second, qqq, p.is, p.oos, p.pas, p.dec).run();
            double alpha = 0; size_t trades = 0;
            for (const auto& x : ws) { alpha += x.oos.alpha; trades += x.oos.trades.size(); }
            if (!ws.empty()) alpha /= static_cast<double>(ws.size());
            std::cout << "  -> " << std::left << std::setw(22) << v.first
                      << std::right << " : alpha OOS moyen "
                      << std::fixed << std::setprecision(4) << alpha
                      << " pt, " << trades << " trades OOS pooles ("
                      << ws.size() << " fenetres)\n";
        }
    }

    // 11. Confirmation hors-protocole du pullback (Sprint 8-septies, 8d.3) ─────
    // Le pullback (RSI ≤ 40 « s'ajoute », candidat D42) est jugé HORS du
    // protocole qui l'a choisi : multi-actifs, grille resserrée, Monte-Carlo,
    // et surtout DONNÉES LONGUES (QQQ_max ~1999+, dot-com 2000-2002 et 2008
    // inclus). Verdict : l'ALPHA généralise (≥ chaîne sur les 2 pavages longs
    // et 2/3 actifs, argmax de grille stable à 40), MAIS le Monte-Carlo montre
    // un drawdown de queue quasi DOUBLÉ (DD p95 10,80 → 19,27 %). Verrous CI
    // dans test_pullback_confirmation_integration.cpp.
    titre("11. CONFIRMATION PULLBACK (donnees longues + multi-actifs + Monte-Carlo — 8d.3)");
    SwingConfig cfgPull = cfg;
    cfgPull.entryPullbackRsiMax = 40.0;   // candidat D42, variante « s'ajoute »

    std::cout << "\n  Donnees LONGUES QQQ_max (~1999+, dot-com + 2008) :\n";
    const struct { const char* nom; size_t is, oos, pas, dec; } pavagesLong[] = {
        {"canonique-long (IS=700/OOS=400)",         700, 400, 400,  0},
        {"decale-long (IS=500/OOS=400, offset=90)", 500, 400, 400, 90},
    };
    const std::string qqqMax = SWINGBOT_QQQ_MAX_CSV;
    for (const auto& p : pavagesLong) {
        std::cout << "  Pavage " << p.nom << " :\n";
        for (const auto& v : {std::make_pair("chaine v2", cfg),
                              std::make_pair("chaine + pullback", cfgPull)}) {
            const auto ws = WalkForward(v.second, qqqMax, p.is, p.oos, p.pas, p.dec).run();
            double alpha = 0; size_t trades = 0;
            for (const auto& x : ws) { alpha += x.oos.alpha; trades += x.oos.trades.size(); }
            if (!ws.empty()) alpha /= static_cast<double>(ws.size());
            std::cout << "  -> " << std::left << std::setw(18) << v.first
                      << std::right << " : alpha OOS moyen "
                      << std::fixed << std::setprecision(4) << alpha
                      << " pt, " << trades << " trades OOS pooles ("
                      << ws.size() << " fenetres)\n";
        }
    }

    std::cout << "\n  Multi-actifs (pavage fin 2019-2026, chaine vs chaine+pullback) :\n";
    const std::pair<const char*, std::string> autresActifs[] = {
        {"SPY", SWINGBOT_SPY_CSV}, {"IWM", SWINGBOT_IWM_CSV}, {"MDY", SWINGBOT_MDY_CSV},
    };
    for (const auto& a : autresActifs) {
        const auto wC = WalkForward(cfg,     a.second, 500, 300, 300).run();
        const auto wP = WalkForward(cfgPull, a.second, 500, 300, 300).run();
        double aC = 0, aP = 0;
        for (const auto& x : wC) aC += x.oos.alpha;
        for (const auto& x : wP) aP += x.oos.alpha;
        if (!wC.empty()) aC /= static_cast<double>(wC.size());
        if (!wP.empty()) aP /= static_cast<double>(wP.size());
        std::cout << "  -> " << std::left << std::setw(4) << a.first << std::right
                  << " : chaine " << std::fixed << std::setprecision(4) << aC
                  << " vs pullback " << aP << (aP >= aC ? "  (pullback >=)" : "  (chaine >)")
                  << "\n";
    }

    std::cout << "\n  Monte-Carlo canonique QQQ (graine 42, 2000 chemins) — le volet DISQUALIFIANT :\n";
    for (const auto& v : {std::make_pair("chaine v2", cfg),
                          std::make_pair("chaine + pullback", cfgPull)}) {
        const auto ws = WalkForward(v.second, qqq, 700, 400, 400).run();
        std::vector<TradeRecord> pool;
        size_t bars = 0;
        for (const auto& x : ws) {
            pool.insert(pool.end(), x.oos.trades.begin(), x.oos.trades.end());
            bars += (x.oosEnd - x.oosStart);
        }
        const auto r = MonteCarlo(10'000.0, 42, 2000).run(pool, static_cast<double>(bars) / 252.0);
        std::cout << "  -> " << std::left << std::setw(18) << v.first << std::right
                  << " : CAGR p50 " << std::fixed << std::setprecision(4) << r.cagrP50
                  << " %, DD p95 " << r.ddP95 << " %\n";
    }
    std::cout << "\n  VERDICT 8d.3 : alpha CONFIRME (generalise donnees longues + multi-actifs),\n"
              << "  mais drawdown de queue quasi double (DD p95 10,80 -> 19,27) → adoption gatee (8d.5).\n";

    std::cout << "\n  Attenuation du drawdown (8d.6, DD p95 canonique QQQ) :\n";
    for (const auto& v : {std::make_pair("pullback nu",  cfgPull),
                          std::make_pair("+ ATR<=0.015", [&]{ SwingConfig c = cfgPull; c.entryMaxAtrPct = 0.015; return c; }()),
                          std::make_pair("+ stop 0.03",  [&]{ SwingConfig c = cfgPull; c.stopLossPct = 0.03; return c; }())}) {
        const auto ws = WalkForward(v.second, qqq, 700, 400, 400).run();
        std::vector<TradeRecord> pool; size_t bars = 0;
        for (const auto& x : ws) { pool.insert(pool.end(), x.oos.trades.begin(), x.oos.trades.end()); bars += (x.oosEnd - x.oosStart); }
        const auto r = MonteCarlo(10'000.0, 42, 2000).run(pool, static_cast<double>(bars) / 252.0);
        std::cout << "  -> " << std::left << std::setw(14) << v.first << std::right
                  << " : DD p95 " << std::fixed << std::setprecision(4) << r.ddP95 << " %\n";
    }
    std::cout << "  VERDICT 8d.6 : le gating ATR casse le drawdown (7,12 < chaine) mais perd l'alpha\n"
              << "  long ; aucune variante ne reussit DD+alpha → alpha et risque du pullback sont couples (D44).\n";

    // 12. Sizing modulé par la volatilité (Sprint 8-octies, 8o.3) ─────────────
    // MC désormais SIZE-AWARE (D45, 8o.1) : le vol-sizing (8o.2) est enfin
    // mesurable. DD p95 canonique (MC size-aware). Verrous CI dans
    // test_pullback_confirmation_integration.cpp (VolSizingDecouplingIsLocked).
    titre("12. VOL-SIZING (pullback + volSizingAtrRef, MC size-aware — 8o.3)");
    std::cout << "\n  DD p95 canonique QQQ (MC size-aware, reference chaine 4,28) :\n";
    for (const auto& v : {std::make_pair("pullback nu",  40.0),
                          std::make_pair("+ vol 0.015",  15.0),
                          std::make_pair("+ vol 0.025",  25.0)}) {
        SwingConfig c = cfg;
        c.entryPullbackRsiMax = 40.0;
        if (v.second < 40.0) c.volSizingAtrRef = v.second / 1000.0;
        const auto ws = WalkForward(c, qqq, 700, 400, 400).run();
        std::vector<TradeRecord> pool; size_t bars = 0;
        for (const auto& x : ws) { pool.insert(pool.end(), x.oos.trades.begin(), x.oos.trades.end()); bars += (x.oosEnd - x.oosStart); }
        const auto r = MonteCarlo(10'000.0, 42, 2000).run(pool, static_cast<double>(bars) / 252.0);
        std::cout << "  -> " << std::left << std::setw(14) << v.first << std::right
                  << " : DD p95 " << std::fixed << std::setprecision(4) << r.ddP95 << " %\n";
    }
    std::cout << "  VERDICT 8o.3 : le vol-sizing REDUIT le DD du pullback (7,88 -> 6,51 a ref 0,015)\n"
              << "  au prix d'un cheveu d'alpha, mais ne le ramene PAS au niveau chaine (4,28) : premiere\n"
              << "  frontiere DD/alpha favorable, decouplage PARTIEL — decision d'adoption au gate 8o.4.\n";

    // ── 13. ROTATION MULTI-ACTIFS (Sprint 8-nonies) ─────────────────────────
    // Moteur SÉPARÉ de la chaîne : détenir le régime le plus fort parmi
    // QQQ/SPY/IWM/MDY (données longues), cash sinon. Verrous CI dans
    // test_rotation_oos_integration.cpp.
    titre("13. ROTATION MULTI-ACTIFS (QQQ/SPY/IWM/MDY, historique max — 8n)");
    {
        const std::vector<std::string> csvs = {
            SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SPY_MAX_CSV,
            SWINGBOT_IWM_MAX_CSV, SWINGBOT_MDY_MAX_CSV };
        RotationConfig rc;  // SMA200, coûts par défaut (comm 0,1 % + 2,5 bps/côté)
        RotationBacktester rot(rc, csvs);
        const auto& axe = rot.axis();
        std::cout << "\n  Axe commun aligne : " << axe.dates.front() << " -> "
                  << axe.dates.back() << " (" << axe.size() << " barres, "
                  << axe.assets() << " actifs)\n";

        const auto rr = rot.run();
        std::cout << std::fixed << std::setprecision(2)
                  << "  Run complet : rotation " << rr.totalReturnPct << " % vs meilleur B&H "
                  << rr.bestBuyHoldPct << " % (B&L) et panier equipondere "
                  << rr.basketReturnPct << " %\n"
                  << "    alpha vs meilleur = " << rr.alphaVsBest
                  << " | alpha vs panier = " << rr.alphaVsBasket
                  << " | DD max = " << rr.maxDrawdownPct << " %\n"
                  << "    CAGR = " << rr.cagrPct << " % | Calmar = " << rr.calmarRatio
                  << " | bascules = " << rr.switchCount
                  << " | temps investi = " << rr.pctTimeInvested << " %\n";

        std::cout << "\n  Verdict OUT-OF-SAMPLE (alpha moyen par pavage) :\n";
        struct Pav { const char* nom; size_t is, oos, step, off; };
        const Pav pavs[] = { {"canonique", 700, 400, 400, 0},
                             {"fin",       500, 300, 300, 0},
                             {"decale",    700, 400, 400, 90} };
        for (const auto& p : pavs) {
            const auto ws = RotationWalkForward(rc, csvs, p.is, p.oos, p.step, p.off).run();
            double aBest = 0.0, aBasket = 0.0;
            std::vector<TradeRecord> pool; double years = 0.0;
            for (const auto& w : ws) {
                aBest += w.oos.alphaVsBest; aBasket += w.oos.alphaVsBasket;
                pool.insert(pool.end(), w.oos.trades.begin(), w.oos.trades.end());
                if (w.oos.equityDates.size() >= 2) {
                    const long d0 = daysFromCivil(w.oos.equityDates.front());
                    const long d1 = daysFromCivil(w.oos.equityDates.back());
                    if (d1 > d0) years += static_cast<double>(d1 - d0) / 365.25;
                }
            }
            const double n = ws.empty() ? 1.0 : static_cast<double>(ws.size());
            std::cout << "  -> " << std::left << std::setw(10) << p.nom << std::right
                      << " : " << ws.size() << " fenetres, alpha vs meilleur "
                      << aBest / n << " | vs panier " << aBasket / n;
            if (!pool.empty()) {
                const auto mc = MonteCarlo(10'000.0, 42, 2000).run(pool, years);
                std::cout << " | MC DD p95 " << mc.ddP95 << " %";
            }
            std::cout << "\n";
        }
        std::cout << "  VERDICT 8n.2 : AUCUN EDGE — la rotation ne bat NI le meilleur B&H NI le\n"
                  << "  panier, net de couts, sur les 3 pavages ; le timing de regime whipsaw et les\n"
                  << "  couts detruisent de la valeur, drawdown de queue massif (~55 %). Prod paper.\n";
    }

    // ── 14. FAMILLE MEAN-REVERSION (Sprint 10) ──────────────────────────────
    // Première famille de signal RÉELLEMENT différente (contrarian : acheter la
    // survente, sortir au retour à la moyenne) — l'INVERSE du trend-following.
    // Jugée en OOS comme toutes les précédentes. Verrous CI dans
    // test_mean_reversion_oos_integration.cpp.
    titre("14. FAMILLE MEAN-REVERSION (contrarian RSI, QQQ + multi-actifs — 10.2)");
    {
        auto mrChain = []() {
            SwingConfig c;
            c.symbol = "QQQ";
            c.mode = StrategyMode::MeanReversion;
            c.mrRsiEntryMax = 30.0; c.mrRsiExitMin = 55.0;
            c.stopLossPct = 0.05; c.takeProfitPct = 0.0; c.trailingStopPct = 0.03;
            c.riskPerTradePct = 0.02; c.minHoldDays = 3; c.smaTrendPeriod = 200;
            return c;
        };
        auto meanOos = [](const std::vector<WfWindow>& w) {
            if (w.empty()) return 0.0;
            double s = 0.0; for (const auto& x : w) s += x.oos.alpha;
            return s / static_cast<double>(w.size());
        };
        auto nbTrades = [](const std::vector<WfWindow>& w) {
            size_t n = 0; for (const auto& x : w) n += x.oos.trades.size(); return n;
        };

        Backtester btmr(mrChain(), qqq, 10'000.0, 0.001);
        const auto bkmr = btmr.run();
        std::cout << std::fixed << std::setprecision(2)
                  << "\n  Backtest plein QQQ : rendement " << bkmr.totalReturnPct
                  << " % vs B&H " << bkmr.buyHoldReturnPct << " % | trades " << bkmr.trades.size()
                  << " | temps investi " << bkmr.pctTimeInvested << " %\n";

        const auto wc = WalkForward(mrChain(), qqq, 700, 400, 400).run();
        const auto wf = WalkForward(mrChain(), qqq, 500, 300, 300).run();
        std::cout << std::setprecision(4)
                  << "  Alpha OOS moyen : canonique " << meanOos(wc)
                  << " (trades " << nbTrades(wc) << ") | fin " << meanOos(wf)
                  << " (trades " << nbTrades(wf) << ")\n";

        std::cout << "  Multi-actifs (pavage fin) :";
        const std::pair<const char*, const char*> actifs[] = {
            {"SPY", SWINGBOT_SPY_CSV}, {"IWM", SWINGBOT_IWM_CSV}, {"MDY", SWINGBOT_MDY_CSV} };
        for (const auto& a : actifs) {
            SwingConfig c = mrChain(); c.symbol = a.first;
            const auto w = WalkForward(c, a.second, 500, 300, 300).run();
            std::cout << "  " << a.first << " " << meanOos(w);
        }
        std::cout << "\n";

        double best = -1e9, bE = 0, bX = 0;
        for (double e : {25.0, 30.0, 35.0}) for (double x : {50.0, 55.0, 60.0}) {
            SwingConfig c = mrChain(); c.mrRsiEntryMax = e; c.mrRsiExitMin = x;
            const double a = meanOos(WalkForward(c, qqq, 500, 300, 300).run());
            if (a > best) { best = a; bE = e; bX = x; }
        }
        std::cout << "  Balayage de seuils : meilleur entry<=" << bE << " / exit>=" << bX
                  << " -> alpha OOS " << best
                  << (best > 0.0 ? "  (CANDIDAT)" : "  (aucun candidat)") << "\n";
        std::cout << "  VERDICT 10.2 : AUCUN EDGE — alpha OOS negatif partout, la chaine MR en\n"
                  << "  regime ne declenche presque pas (cash drag, D34) ; aucun seuil ne bat le\n"
                  << "  B&H. Gate de confirmation 10.3 FERME. Prod paper.\n";
    }

    // ── 15. VARIANTE MEAN-REVERSION Z-SCORE / BOLLINGER (Sprint 11) ──────────
    // Décision 10.4 = (a) : le 1er jet MR (RSI ≤ 30 en régime) ne tradait qu'1
    // fois en OOS (cash drag, D47) — la FAMILLE n'était pas jugée. L'entrée
    // z-score achète la clôture sous la bande basse de Bollinger, moins couplée
    // au régime → elle TRADE réellement. Verrous CI dans
    // test_mean_reversion_oos_integration.cpp (ZScore*).
    titre("15. VARIANTE MEAN-REVERSION Z-SCORE (Bollinger, QQQ + multi-actifs — 11.2)");
    {
        auto mrZChain = []() {
            SwingConfig c;
            c.symbol = "QQQ";
            c.mode = StrategyMode::MeanReversion;
            c.mrBandPeriod = 20; c.mrBandEntryK = 2.0; c.mrBandExitZ = 0.0;
            c.stopLossPct = 0.05; c.takeProfitPct = 0.0; c.trailingStopPct = 0.03;
            c.riskPerTradePct = 0.02; c.minHoldDays = 3; c.smaTrendPeriod = 200;
            return c;
        };
        auto meanOos = [](const std::vector<WfWindow>& w) {
            if (w.empty()) return 0.0;
            double s = 0.0; for (const auto& x : w) s += x.oos.alpha;
            return s / static_cast<double>(w.size());
        };
        auto nbTrades = [](const std::vector<WfWindow>& w) {
            size_t n = 0; for (const auto& x : w) n += x.oos.trades.size(); return n;
        };

        const auto wc = WalkForward(mrZChain(), qqq, 700, 400, 400).run();
        const auto wf = WalkForward(mrZChain(), qqq, 500, 300, 300).run();
        std::cout << std::fixed << std::setprecision(4)
                  << "\n  Alpha OOS moyen : canonique " << meanOos(wc)
                  << " (trades " << nbTrades(wc) << ") | fin " << meanOos(wf)
                  << " (trades " << nbTrades(wf) << ")\n";

        SwingConfig off = mrZChain(); off.smaTrendPeriod = 1;
        const auto woff = WalkForward(off, qqq, 500, 300, 300).run();
        std::cout << "  Filtre de regime (fin) : ON " << meanOos(wf)
                  << " (trades " << nbTrades(wf) << ") | OFF " << meanOos(woff)
                  << " (trades " << nbTrades(woff) << ")\n";

        std::cout << "  Multi-actifs (pavage fin) :";
        const std::pair<const char*, const char*> actifs[] = {
            {"SPY", SWINGBOT_SPY_CSV}, {"IWM", SWINGBOT_IWM_CSV}, {"MDY", SWINGBOT_MDY_CSV} };
        for (const auto& a : actifs) {
            SwingConfig c = mrZChain(); c.symbol = a.first;
            const auto w = WalkForward(c, a.second, 500, 300, 300).run();
            std::cout << "  " << a.first << " " << meanOos(w);
        }
        std::cout << "\n";

        double best = -1e9; int bP = 0; double bK = 0.0;
        for (int p : {10, 20}) for (double k : {1.5, 2.0, 2.5}) {
            SwingConfig c = mrZChain(); c.mrBandPeriod = p; c.mrBandEntryK = k;
            const double a = meanOos(WalkForward(c, qqq, 500, 300, 300).run());
            if (a > best) { best = a; bP = p; bK = k; }
        }
        std::cout << "  Balayage periode x k : meilleur period=" << bP << " / k=" << bK
                  << " -> alpha OOS " << best
                  << (best > 0.0 ? "  (CANDIDAT)" : "  (aucun candidat)") << "\n";
        std::cout << "  VERDICT 11.2 : la variante z-score TRADE reellement (3/4 trades OOS vs 1\n"
                  << "  pour le 1er jet RSI, D47 leve) mais AUCUN EDGE — alpha OOS negatif sur les\n"
                  << "  deux pavages, les 3 actifs et tous les reglages ; retirer le filtre fait\n"
                  << "  trader plus (23) mais empire l'alpha. Gate 11.3 FERME. Prod paper.\n";
    }

    // ── 16. FAMILLE PAIRS-TRADING (Sprint 12) ───────────────────────────────
    // 4e famille d'alpha (décision utilisateur 11.4) : valeur relative /
    // MARKET-NEUTRAL — la seule famille ORTHOGONALE après les trois familles
    // directionnelles soldées sans edge. z-score du spread log(P0)-log(P1),
    // position dollar-neutral 0,5/0,5. Moteur SÉPARÉ (PairsBacktester), jugé sur
    // Sharpe OOS (un market-neutral ne bat pas un indice long). Verrous CI dans
    // test_pairs_oos_integration.cpp.
    titre("16. FAMILLE PAIRS-TRADING (spread market-neutral, multi-paires — 12.2)");
    {
        auto cfgPairs = []() {
            PairsConfig c;
            c.zWindow = 20; c.entryK = 2.0; c.exitZ = 0.0;
            c.initialCapital = 10'000.0;
            c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
            return c;
        };
        struct AggP { size_t fen = 0; double sharpe = 0.0, ret = 0.0, dd = 0.0, leg0dd = 0.0;
                      size_t trades = 0; };
        auto agrege = [](const std::vector<PairWindow>& ws) {
            AggP a; a.fen = ws.size();
            for (const auto& w : ws) {
                a.sharpe += w.oos.sharpeRatio; a.ret += w.oos.totalReturnPct;
                a.dd += w.oos.maxDrawdownPct; a.leg0dd += w.oos.leg0BuyHoldDrawdownPct;
                a.trades += w.oos.trades.size();
            }
            if (a.fen) { const double n = static_cast<double>(a.fen);
                a.sharpe /= n; a.ret /= n; a.dd /= n; a.leg0dd /= n; }
            return a;
        };
        const std::vector<std::string> qs = { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SPY_MAX_CSV };

        const auto ac = agrege(PairsWalkForward(cfgPairs(), qs, 700, 400, 400).run());
        const auto af = agrege(PairsWalkForward(cfgPairs(), qs, 500, 300, 300).run());
        const auto as = agrege(PairsWalkForward(cfgPairs(), qs, 700, 400, 400, 90).run());
        std::cout << std::fixed << std::setprecision(4)
                  << "\n  Sharpe OOS (QQQ/SPY) : canon " << ac.sharpe
                  << " (ret " << ac.ret << ", trades " << ac.trades << ")\n"
                  << "                         fin   " << af.sharpe
                  << " (ret " << af.ret << ", trades " << af.trades << ")\n"
                  << "                         decale " << as.sharpe
                  << " (ret " << as.ret << ", trades " << as.trades << ")\n";
        std::cout << "  Clause DoD DD reduit >= 50 % (canon) : DD strat " << ac.dd
                  << " % vs DD B&H jambe0 " << ac.leg0dd << " % (atteinte mais ret < 0)\n";

        std::cout << "  Multi-paires (fin) :";
        const std::pair<const char*, std::pair<const char*, const char*>> paires[] = {
            {"QQQ/SPY", {SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SPY_MAX_CSV}},
            {"QQQ/IWM", {SWINGBOT_QQQ_MAX_CSV, SWINGBOT_IWM_MAX_CSV}},
            {"QQQ/MDY", {SWINGBOT_QQQ_MAX_CSV, SWINGBOT_MDY_MAX_CSV}},
            {"SPY/IWM", {SWINGBOT_SPY_MAX_CSV, SWINGBOT_IWM_MAX_CSV}} };
        for (const auto& p : paires) {
            const auto agg = agrege(PairsWalkForward(cfgPairs(),
                { p.second.first, p.second.second }, 500, 300, 300).run());
            std::cout << "  " << p.first << " " << agg.sharpe;
        }
        std::cout << "\n";

        double best = -1e9; int bZ = 0; double bK = 0.0;
        for (int z : {10, 20}) for (double k : {1.5, 2.0, 2.5}) {
            PairsConfig c = cfgPairs(); c.zWindow = z; c.entryK = k;
            const double sh = agrege(PairsWalkForward(c, qs, 500, 300, 300).run()).sharpe;
            if (sh > best) { best = sh; bZ = z; bK = k; }
        }
        std::cout << "  Balayage zWindow x entryK : meilleur z=" << bZ << " / k=" << bK
                  << " -> Sharpe OOS " << best
                  << (best > 0.0 ? "  (CANDIDAT)" : "  (aucun candidat)") << "\n";
        std::cout << "  VERDICT 12.2 : AUCUN EDGE — la famille TRADE massivement (~210 A/R OOS,\n"
                  << "  D47 satisfait) mais Sharpe OOS negatif sur les 3 pavages, les 4 paires et\n"
                  << "  tous les reglages. Le spread sur fenetre courte est du bruit, pas un retour\n"
                  << "  a la moyenne exploitable net de couts. Gate FERME. Prod paper.\n";
    }

    // ── 17. FAMILLE VOL-REGIME (Sprint 13) ──────────────────────────────────
    // 5e famille d'alpha (décision utilisateur (c) : famille NEUVE au-delà des
    // signaux de prix simples). Filtre binaire long/cash modulé par le RÉGIME DE
    // VOLATILITÉ (hypothèse Moreira & Muir 2017) : long l'actif quand la vol
    // réalisée est CALME (≤ seuil × sa médiane glissante de référence), sinon
    // cash. Moteur SÉPARÉ (VolRegimeBacktester), jugé sur le delta de Sharpe OOS
    // vs Buy & Hold (le filtre est directionnel long/cash, donc comparable au
    // B&H). Verrous CI dans test_vol_regime_oos_integration.cpp.
    titre("17. FAMILLE VOL-REGIME (vol realisee, long/cash, 3 pavages + balayage — 13.2)");
    {
        auto cfgVol = []() {
            VolRegimeConfig c;
            c.volLookback = 20; c.refLookback = 126; c.thresholdMult = 1.0;
            c.initialCapital = 10'000.0;
            c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
            return c;
        };
        struct AggV { size_t fen = 0; double sharpe = 0.0, bhSharpe = 0.0, ret = 0.0,
                                            dd = 0.0, bhdd = 0.0; size_t trades = 0;
                      double dSharpe() const { return sharpe - bhSharpe; } };
        auto agrege = [](const std::vector<VolWindow>& ws) {
            AggV a; a.fen = ws.size();
            for (const auto& w : ws) {
                a.sharpe += w.oos.sharpeRatio; a.bhSharpe += w.oos.buyHoldSharpe;
                a.ret += w.oos.totalReturnPct; a.dd += w.oos.maxDrawdownPct;
                a.bhdd += w.oos.buyHoldMaxDrawdownPct; a.trades += w.oos.trades.size();
            }
            if (a.fen) { const double n = static_cast<double>(a.fen);
                a.sharpe /= n; a.bhSharpe /= n; a.ret /= n; a.dd /= n; a.bhdd /= n; }
            return a;
        };
        const std::string qqq = SWINGBOT_QQQ_MAX_CSV;

        const auto ac = agrege(VolRegimeWalkForward(cfgVol(), qqq, 750, 500, 500).run());
        const auto af = agrege(VolRegimeWalkForward(cfgVol(), qqq, 550, 400, 400).run());
        const auto as = agrege(VolRegimeWalkForward(cfgVol(), qqq, 750, 500, 500, 90).run());
        std::cout << std::fixed << std::setprecision(4)
                  << "\n  dSharpe OOS vs B&H (QQQ) : canon " << ac.dSharpe()
                  << " (strat " << ac.sharpe << " vs B&H " << ac.bhSharpe
                  << ", trades " << ac.trades << ")\n"
                  << "                             fin   " << af.dSharpe()
                  << " (strat " << af.sharpe << " vs B&H " << af.bhSharpe
                  << ", trades " << af.trades << ")\n"
                  << "                             decale " << as.dSharpe()
                  << " (strat " << as.sharpe << " vs B&H " << as.bhSharpe
                  << ", trades " << as.trades << ")\n";
        std::cout << "  DD reduit (canon) : DD strat " << ac.dd << " % vs DD B&H "
                  << ac.bhdd << " % (reduit mais < 50 %, clause DoD NON atteinte)\n";

        std::cout << "  Multi-univers (fin) :";
        const std::pair<const char*, const char*> univ[] = {
            {"QQQ", SWINGBOT_QQQ_MAX_CSV}, {"SPY", SWINGBOT_SPY_MAX_CSV},
            {"IWM", SWINGBOT_IWM_MAX_CSV}, {"MDY", SWINGBOT_MDY_MAX_CSV} };
        for (const auto& u : univ) {
            const auto agg = agrege(VolRegimeWalkForward(cfgVol(), u.second, 550, 400, 400).run());
            std::cout << "  " << u.first << " dS=" << agg.dSharpe();
        }
        std::cout << "\n";

        double best = -1e9; int bV = 0; double bM = 0.0;
        for (int vl : {10, 20, 42}) for (double mm : {0.8, 1.0, 1.2}) {
            VolRegimeConfig c = cfgVol(); c.volLookback = vl; c.thresholdMult = mm;
            const double ds = agrege(VolRegimeWalkForward(c, qqq, 550, 400, 400).run()).dSharpe();
            if (ds > best) { best = ds; bV = vl; bM = mm; }
        }
        std::cout << "  Balayage volLookback x seuil : meilleur vl=" << bV << " / m=" << bM
                  << " -> dSharpe OOS " << best
                  << (best > 0.0 ? "  (CANDIDAT)" : "  (aucun candidat)") << "\n";
        std::cout << "  VERDICT 13.2 : AUCUN EDGE — le filtre TRADE reellement (~110-135 stints\n"
                  << "  OOS, D47 satisfait) et gagne de l'argent (Sharpe > 0), mais SOUS le B&H\n"
                  << "  sur le Sharpe (dSharpe < 0) sur les 3 pavages, les 4 actifs et tous les\n"
                  << "  reglages. Le cash drag T4 coute plus que la reduction de DD (< 50 %) ne\n"
                  << "  rapporte. Gate FERME. Prod paper. Les CINQ familles sont soldees sans edge.\n";
    }

    // ── 18. FAMILLE VIX-REGIME (Sprint 14) ──────────────────────────────────
    // 5e famille, variante à données EXTERNES (décision utilisateur (c'), backlog
    // D50) : régime de volatilité IMPLICITE. On TRADE QQQ mais on gate l'exposition
    // sur le niveau du ^VIX (vol implicite, exogène, anticipatrice) vs sa médiane
    // glissante. Moteur SÉPARÉ (VixRegimeBacktester, 2 séries alignées QQQ+VIX),
    // jugé sur le delta de Sharpe OOS vs B&H. Verrous CI dans
    // test_vix_regime_oos_integration.cpp.
    titre("18. FAMILLE VIX-REGIME (vol implicite ^VIX, QQQ long/cash, 3 pavages + balayage — 14.2)");
    {
        auto cfgVix = []() {
            VixRegimeConfig c;
            c.refLookback = 126; c.thresholdMult = 1.0;
            c.initialCapital = 10'000.0;
            c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
            return c;
        };
        struct AggV { size_t fen = 0; double sharpe = 0.0, bhSharpe = 0.0, ret = 0.0,
                                            dd = 0.0, bhdd = 0.0; size_t trades = 0;
                      double dSharpe() const { return sharpe - bhSharpe; } };
        auto agrege = [](const std::vector<VixWindow>& ws) {
            AggV a; a.fen = ws.size();
            for (const auto& w : ws) {
                a.sharpe += w.oos.sharpeRatio; a.bhSharpe += w.oos.buyHoldSharpe;
                a.ret += w.oos.totalReturnPct; a.dd += w.oos.maxDrawdownPct;
                a.bhdd += w.oos.buyHoldMaxDrawdownPct; a.trades += w.oos.trades.size();
            }
            if (a.fen) { const double n = static_cast<double>(a.fen);
                a.sharpe /= n; a.bhSharpe /= n; a.ret /= n; a.dd /= n; a.bhdd /= n; }
            return a;
        };
        const std::vector<std::string> qv = { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_VIX_MAX_CSV };

        const auto ac = agrege(VixRegimeWalkForward(cfgVix(), qv, 750, 500, 500).run());
        const auto af = agrege(VixRegimeWalkForward(cfgVix(), qv, 550, 400, 400).run());
        const auto as = agrege(VixRegimeWalkForward(cfgVix(), qv, 750, 500, 500, 90).run());
        std::cout << std::fixed << std::setprecision(4)
                  << "\n  dSharpe OOS vs B&H (QQQ gate VIX) : canon " << ac.dSharpe()
                  << " (strat " << ac.sharpe << " vs B&H " << ac.bhSharpe
                  << ", trades " << ac.trades << ")\n"
                  << "                                     fin   " << af.dSharpe()
                  << " (strat " << af.sharpe << " vs B&H " << af.bhSharpe
                  << ", trades " << af.trades << ")\n"
                  << "                                     decale " << as.dSharpe()
                  << " (strat " << as.sharpe << " vs B&H " << as.bhSharpe
                  << ", trades " << as.trades << ")\n";
        std::cout << "  DD reduit (canon) : DD strat " << ac.dd << " % vs DD B&H "
                  << ac.bhdd << " % (reduit mais < 50 %, clause DoD NON atteinte)\n";

        std::cout << "  Multi-univers (fin, tous gates sur le MEME VIX) :";
        const std::pair<const char*, const char*> univ[] = {
            {"QQQ", SWINGBOT_QQQ_MAX_CSV}, {"SPY", SWINGBOT_SPY_MAX_CSV},
            {"IWM", SWINGBOT_IWM_MAX_CSV}, {"MDY", SWINGBOT_MDY_MAX_CSV} };
        for (const auto& u : univ) {
            const auto agg = agrege(VixRegimeWalkForward(cfgVix(),
                { u.second, SWINGBOT_VIX_MAX_CSV }, 550, 400, 400).run());
            std::cout << "  " << u.first << " dS=" << agg.dSharpe();
        }
        std::cout << "\n";

        double best = -1e9; int bR = 0; double bM = 0.0;
        for (int rl : {63, 126, 252}) for (double mm : {0.8, 1.0, 1.2}) {
            VixRegimeConfig c = cfgVix(); c.refLookback = rl; c.thresholdMult = mm;
            const double ds = agrege(VixRegimeWalkForward(c, qv, 550, 400, 400).run()).dSharpe();
            if (ds > best) { best = ds; bR = rl; bM = mm; }
        }
        std::cout << "  Balayage refLookback x seuil : meilleur rl=" << bR << " / m=" << bM
                  << " -> dSharpe OOS " << best
                  << (best > 0.0 ? "  (CANDIDAT)" : "  (aucun candidat)") << "\n";
        std::cout << "  VERDICT 14.2 : AUCUN EDGE — la vol IMPLICITE (VIX) ne fait pas mieux que\n"
                  << "  la vol REALISEE (Sprint 13). Le filtre TRADE massivement (195-227 stints\n"
                  << "  OOS) et gagne de l'argent (Sharpe > 0) mais SOUS le B&H sur le Sharpe\n"
                  << "  (dSharpe < 0) sur les 3 pavages, les 4 actifs et tous les reglages. Gate\n"
                  << "  FERME. Prod paper. La 5e famille (deux variantes) est soldee sans edge.\n";
    }

    // ── 19. SCALING CONTINU MOREIRA-MUIR (Sprint 18) ────────────────────────
    // Réouverture de la recherche d'edge (décision utilisateur (r), piste §5.2
    // de CONCLUSION_RECHERCHE_EDGE.md) : au lieu du filtre BINAIRE long/cash des
    // sections 17-18 (Sharpe OOS positif mais SOUS le B&H — cash drag D50/D51),
    // le poids investi est CONTINU : w* = min(1, cible / vol annualisée), bande
    // anti-churn, coût proportionnel au notionnel tradé |Δw|. Deux variantes :
    // vol RÉALISÉE (VolScaledBacktester) et vol IMPLICITE ^VIX
    // (VixScaledBacktester). Directionnel → jugé sur l'ALPHA vs B&H net de
    // coûts (critère primaire) avec repli Sprint 8 (Sharpe ≥ B&H ET DD −50 %).
    // Verrous CI dans test_vol_scaled_oos_integration.cpp /
    // test_vix_scaled_oos_integration.cpp.
    titre("19. SCALING CONTINU MOREIRA-MUIR (poids w = cible/vol, vol realisee + VIX — 18.3)");
    {
        auto cfgVs = []() {
            VolScaledConfig c;
            c.volLookback = 20; c.targetVolAnnPct = 15.0;
            c.maxWeight = 1.0; c.rebalanceBand = 0.05;
            c.initialCapital = 10'000.0;
            c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
            return c;
        };
        struct AggS { size_t fen = 0; double sharpe = 0.0, bhSharpe = 0.0, ret = 0.0,
                                             dd = 0.0, bhdd = 0.0, alpha = 0.0,
                                             poids = 0.0, churn = 0.0;
                      size_t trades = 0;
                      double dSharpe() const { return sharpe - bhSharpe; } };
        auto agrege = [](const std::vector<VolScaledWindow>& ws) {
            AggS a; a.fen = ws.size();
            for (const auto& w : ws) {
                a.sharpe += w.oos.sharpeRatio; a.bhSharpe += w.oos.buyHoldSharpe;
                a.ret += w.oos.totalReturnPct; a.dd += w.oos.maxDrawdownPct;
                a.bhdd += w.oos.buyHoldMaxDrawdownPct; a.alpha += w.oos.alphaVsBuyHold;
                a.poids += w.oos.avgWeight; a.churn += w.oos.turnover;
                a.trades += w.oos.trades.size();
            }
            if (a.fen) { const double n = static_cast<double>(a.fen);
                a.sharpe /= n; a.bhSharpe /= n; a.ret /= n; a.dd /= n; a.bhdd /= n;
                a.alpha /= n; a.poids /= n; a.churn /= n; }
            return a;
        };
        const std::string qqq = SWINGBOT_QQQ_MAX_CSV;

        const auto ac = agrege(VolScaledWalkForward(cfgVs(), qqq, 750, 500, 500).run());
        const auto af = agrege(VolScaledWalkForward(cfgVs(), qqq, 550, 400, 400).run());
        const auto as = agrege(VolScaledWalkForward(cfgVs(), qqq, 750, 500, 500, 90).run());
        std::cout << std::fixed << std::setprecision(4)
                  << "\n  Alpha OOS vs B&H (QQQ, vol realisee) : canon " << ac.alpha
                  << " (dSharpe " << ac.dSharpe() << ", poids moy " << ac.poids
                  << ", trades " << ac.trades << ")\n"
                  << "                                          fin   " << af.alpha
                  << " (dSharpe " << af.dSharpe() << ", poids moy " << af.poids
                  << ", trades " << af.trades << ")\n"
                  << "                                          decale " << as.alpha
                  << " (dSharpe " << as.dSharpe() << ", poids moy " << as.poids
                  << ", trades " << as.trades << ")\n";
        std::cout << "  DD (canon) : strat " << ac.dd << " % vs B&H " << ac.bhdd
                  << " % ; churn moyen (somme |dw| par fenetre) " << ac.churn << "\n";

        std::cout << "  Multi-univers (fin) :";
        const std::pair<const char*, const char*> univ[] = {
            {"QQQ", SWINGBOT_QQQ_MAX_CSV}, {"SPY", SWINGBOT_SPY_MAX_CSV},
            {"IWM", SWINGBOT_IWM_MAX_CSV}, {"MDY", SWINGBOT_MDY_MAX_CSV} };
        for (const auto& u : univ) {
            const auto agg = agrege(VolScaledWalkForward(cfgVs(), u.second, 550, 400, 400).run());
            std::cout << "  " << u.first << " a=" << agg.alpha;
        }
        std::cout << "\n";

        // Variante vol IMPLICITE : le poids est piloté par le NIVEAU du ^VIX.
        auto cfgVx = []() {
            VixScaledConfig c;
            c.targetVixPct = 15.0; c.maxWeight = 1.0; c.rebalanceBand = 0.05;
            c.initialCapital = 10'000.0;
            c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
            return c;
        };
        struct AggX { size_t fen = 0; double alpha = 0.0, sharpe = 0.0, bhSharpe = 0.0;
                      size_t trades = 0;
                      double dSharpe() const { return sharpe - bhSharpe; } };
        auto agregeX = [](const std::vector<VixScaledWindow>& ws) {
            AggX a; a.fen = ws.size();
            for (const auto& w : ws) {
                a.alpha += w.oos.alphaVsBuyHold; a.sharpe += w.oos.sharpeRatio;
                a.bhSharpe += w.oos.buyHoldSharpe; a.trades += w.oos.trades.size();
            }
            if (a.fen) { const double n = static_cast<double>(a.fen);
                a.alpha /= n; a.sharpe /= n; a.bhSharpe /= n; }
            return a;
        };
        const std::vector<std::string> qv = { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_VIX_MAX_CSV };
        const auto xc = agregeX(VixScaledWalkForward(cfgVx(), qv, 750, 500, 500).run());
        const auto xf = agregeX(VixScaledWalkForward(cfgVx(), qv, 550, 400, 400).run());
        const auto xs = agregeX(VixScaledWalkForward(cfgVx(), qv, 750, 500, 500, 90).run());
        std::cout << "  Variante VIX : alpha OOS canon " << xc.alpha
                  << " (dS " << xc.dSharpe() << ")  fin " << xf.alpha
                  << " (dS " << xf.dSharpe() << ")  decale " << xs.alpha
                  << " (dS " << xs.dSharpe() << ")\n";

        // Balayage cible × bande (critère : alpha OOS moyen, pavage fin).
        double best = -1e9; double bT = 0.0, bB = 0.0;
        for (double tg : {10.0, 15.0, 20.0}) for (double bd : {0.02, 0.05, 0.10}) {
            VolScaledConfig c = cfgVs(); c.targetVolAnnPct = tg; c.rebalanceBand = bd;
            const double al = agrege(VolScaledWalkForward(c, qqq, 550, 400, 400).run()).alpha;
            if (al > best) { best = al; bT = tg; bB = bd; }
        }
        std::cout << "  Balayage cible x bande : meilleur cible=" << bT << " / bande=" << bB
                  << " -> alpha OOS " << best
                  << (best > 0.0 ? "  (CANDIDAT)" : "  (aucun candidat)") << "\n";

        // Monte-Carlo size-aware (D45) sur les stints OOS poolés (canonique).
        std::vector<TradeRecord> pool;
        double annees = 0.0;
        for (const auto& w : VolScaledWalkForward(cfgVs(), qqq, 750, 500, 500).run()) {
            for (const auto& t : w.oos.trades) pool.push_back(t);
            if (w.oos.equityDates.size() >= 2) {
                const long d0 = daysFromCivil(w.oos.equityDates.front());
                const long d1 = daysFromCivil(w.oos.equityDates.back());
                if (d1 > d0) annees += static_cast<double>(d1 - d0) / 365.25;
            }
        }
        if (!pool.empty()) {
            MonteCarlo mc(10'000.0, /*graine=*/42, /*chemins=*/2000);
            const auto rmc = mc.run(pool, annees);
            std::cout << "  Monte-Carlo size-aware (canon, seed 42, 2000 chemins) : cagrP50 "
                      << rmc.cagrP50 << " %  ddP95 " << rmc.ddP95 << " %\n";
        }

        std::cout << "  VERDICT 18.3 : AUCUN EDGE — mais le RESULTAT NEGATIF LE PLUS SERRE du\n"
                  << "  projet : le scaling continu COMBLE quasiment l'ecart de Sharpe vs B&H\n"
                  << "  (dSharpe +0,06/-0,06/+0,03 contre -0,5 pour le binaire D50 : le cash drag\n"
                  << "  est bien la bonne cible) et reduit le DD (~1/3), MAIS l'alpha absolu reste\n"
                  << "  negatif sur les 3 pavages et les 4 actifs (meilleur reglage -0,52), le\n"
                  << "  signe du dSharpe s'inverse selon le pavage (lecon 8t.1) et la clause de\n"
                  << "  repli exige DD -50 %. La variante VIX fait MOINS bien que la vol realisee.\n"
                  << "  Gate FERME. Prod paper.\n";
    }

    // ── 20. PAIRS-TRADING COINTEGRE ENGLE-GRANGER (Sprint 18) ───────────────
    // Réouverture (décision (r), piste §5.1) : correction structurelle du
    // Sprint 12/D49 (« le spread naïf sans test de cointégration est du
    // bruit ») — hedge ratio OLS ROULANT (β figé à l'entrée) + GATE de
    // cointégration (Dickey-Fuller sur le résidu, critique Engle-Granger figée
    // −3,34) : quand la paire n'est PAS cointégrée, on ne trade PAS. Famille
    // market-neutral → jugée sur le SHARPE OOS (pas l'alpha vs B&H). Garde
    // d'activation D47 : pctBarsCointegrated (un gate toujours fermé ne serait
    // pas un verdict). Verrous CI dans test_coint_pairs_oos_integration.cpp.
    // AVERTISSEMENT WARMUP (D35) : max(betaWindow, adfWindow) − 1 = 125 barres
    // (et 251 quand betaWindow = 252) → fenêtres OOS de 500 barres.
    titre("20. PAIRS-TRADING COINTEGRE (Engle-Granger : hedge roulant + gate ADF — 18.6)");
    {
        auto cfgCp = []() {
            CointPairsConfig c;
            c.betaWindow = 126; c.adfWindow = 126; c.adfCritical = -3.34;
            c.zWindow = 20; c.entryK = 2.0; c.exitZ = 0.0;
            c.initialCapital = 10'000.0;
            c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
            return c;
        };
        struct AggC { size_t fen = 0; double sharpe = 0.0, ret = 0.0, dd = 0.0,
                                             leg0dd = 0.0, coint = 0.0;
                      size_t trades = 0; };
        auto agrege = [](const std::vector<CointPairWindow>& ws) {
            AggC a; a.fen = ws.size();
            for (const auto& w : ws) {
                a.sharpe += w.oos.sharpeRatio; a.ret += w.oos.totalReturnPct;
                a.dd += w.oos.maxDrawdownPct; a.leg0dd += w.oos.leg0BuyHoldDrawdownPct;
                a.coint += w.oos.pctBarsCointegrated;
                a.trades += w.oos.trades.size();
            }
            if (a.fen) { const double n = static_cast<double>(a.fen);
                a.sharpe /= n; a.ret /= n; a.dd /= n; a.leg0dd /= n; a.coint /= n; }
            return a;
        };
        const std::vector<std::string> qs = { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SPY_MAX_CSV };

        const auto ac = agrege(CointPairsWalkForward(cfgCp(), qs, 750, 500, 500).run());
        const auto af = agrege(CointPairsWalkForward(cfgCp(), qs, 700, 500, 500).run());
        const auto as = agrege(CointPairsWalkForward(cfgCp(), qs, 750, 500, 500, 90).run());
        std::cout << std::fixed << std::setprecision(4)
                  << "\n  Sharpe OOS (QQQ/SPY) : canon " << ac.sharpe
                  << " (ret " << ac.ret << ", A/R " << ac.trades
                  << ", coint " << ac.coint << " %)\n"
                  << "                         fin   " << af.sharpe
                  << " (ret " << af.ret << ", A/R " << af.trades
                  << ", coint " << af.coint << " %)\n"
                  << "                         decale " << as.sharpe
                  << " (ret " << as.ret << ", A/R " << as.trades
                  << ", coint " << as.coint << " %)\n";
        std::cout << "  DD (canon) : strat " << ac.dd << " % vs B&H jambe0 "
                  << ac.leg0dd << " %\n";

        std::cout << "  Multi-paires (fin) :";
        const std::pair<const char*, std::pair<const char*, const char*>> paires[] = {
            {"QQQ/SPY", {SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SPY_MAX_CSV}},
            {"QQQ/IWM", {SWINGBOT_QQQ_MAX_CSV, SWINGBOT_IWM_MAX_CSV}},
            {"QQQ/MDY", {SWINGBOT_QQQ_MAX_CSV, SWINGBOT_MDY_MAX_CSV}},
            {"SPY/IWM", {SWINGBOT_SPY_MAX_CSV, SWINGBOT_IWM_MAX_CSV}} };
        for (const auto& p : paires) {
            const auto agg = agrege(CointPairsWalkForward(cfgCp(),
                { p.second.first, p.second.second }, 700, 500, 500).run());
            std::cout << "  " << p.first << " S=" << agg.sharpe
                      << " (coint " << agg.coint << " %)";
        }
        std::cout << "\n";

        double best = -1e9; int bW = 0; double bK = 0.0;
        for (int bw : {126, 252}) for (double k : {1.5, 2.0, 2.5}) {
            CointPairsConfig c = cfgCp(); c.betaWindow = bw; c.adfWindow = bw; c.entryK = k;
            const double sh = agrege(CointPairsWalkForward(c, qs, 700, 500, 500).run()).sharpe;
            if (sh > best) { best = sh; bW = bw; bK = k; }
        }
        std::cout << "  Balayage betaWindow x entryK : meilleur w=" << bW << " / k=" << bK
                  << " -> Sharpe OOS " << best
                  << (best > 0.0 ? "  (CANDIDAT)" : "  (aucun candidat)") << "\n";

        // Monte-Carlo size-aware (D45) sur les A/R OOS poolés (canonique).
        std::vector<TradeRecord> pool;
        double annees = 0.0;
        for (const auto& w : CointPairsWalkForward(cfgCp(), qs, 750, 500, 500).run()) {
            for (const auto& t : w.oos.trades) pool.push_back(t);
            if (w.oos.equityDates.size() >= 2) {
                const long d0 = daysFromCivil(w.oos.equityDates.front());
                const long d1 = daysFromCivil(w.oos.equityDates.back());
                if (d1 > d0) annees += static_cast<double>(d1 - d0) / 365.25;
            }
        }
        if (!pool.empty()) {
            MonteCarlo mc(10'000.0, /*graine=*/42, /*chemins=*/2000);
            const auto rmc = mc.run(pool, annees);
            std::cout << "  Monte-Carlo size-aware (canon, seed 42, 2000 chemins) : cagrP50 "
                      << rmc.cagrP50 << " %  ddP95 " << rmc.ddP95 << " %\n";
        } else {
            std::cout << "  Monte-Carlo : AUCUN trade OOS poole (gate ferme en continu) —\n"
                      << "  la garde D47 s'applique : verdict d'ACTIVATION, pas de performance.\n";
        }

        std::cout << "  VERDICT 18.6 : AUCUN EDGE — le gate de cointegration FILTRE bien le bruit\n"
                  << "  (Sharpe OOS -0,17/-0,31 contre -1,16/-1,20 pour le naif D49 ; MC cagrP50\n"
                  << "  -0,33 % contre -3,62 % ; DD p95 17 % contre 68 %) : il degrade beaucoup\n"
                  << "  moins, mais ne CREE pas d'edge — Sharpe OOS negatif sur les 3 pavages, les\n"
                  << "  4 paires et tous les reglages (gate ouvert ~7-10 % des barres, 19-24 A/R :\n"
                  << "  D47 satisfait, le verdict est un verdict de PERFORMANCE, pas d'activation).\n"
                  << "  ETFs indiciels US : pas de paire durablement cointegree exploitable net de\n"
                  << "  couts. Gate FERME. Prod paper.\n";
    }

    // ── 21. TERM-STRUCTURE VIX/VIX3M (Sprint 19) ────────────────────────────
    // Réouverture (décision utilisateur (r'), piste §5.3 — DERNIÈRE piste
    // offline du §5) : la FORME de la courbe de vol implicite comme signal de
    // régime. Ratio VIX/VIX3M : contango (< 1) = régime normal, backwardation
    // (> 1) = stress (2008, 2011, 2015, 2018, 2020, 2022). Deux variantes :
    // filtre BINAIRE long/cash (ratio lissé ≤ seuil, VixTermRegimeBacktester)
    // et poids CONTINU w = min(1, (VIX3M/VIX)^k) (VixTermScaledBacktester —
    // leçon D55 : le cash drag est le vrai coût du binaire). Axe QQQ∩VIX∩VIX3M
    // borné par le VIX3M (~2006) : SANS l'épisode dot-com. Directionnel →
    // critère PRIMAIRE dSharpe vs B&H + sanity alpha (D23). Verrous CI dans
    // test_vix_term_regime_oos_integration.cpp / test_vix_term_scaled_*.cpp.
    titre("21. TERM-STRUCTURE VIX/VIX3M (contango/backwardation, binaire + continu — 19.5)");
    {
        // Variante BINAIRE : long si le ratio lissé (SMA 5) est ≤ 1, cash sinon.
        auto cfgTr = []() {
            VixTermRegimeConfig c;
            c.ratioThreshold = 1.0; c.smoothLookback = 5;
            c.initialCapital = 10'000.0;
            c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
            return c;
        };
        struct AggT { size_t fen = 0; double sharpe = 0.0, bhSharpe = 0.0, ret = 0.0,
                                             dd = 0.0, bhdd = 0.0, alpha = 0.0,
                                             invest = 0.0;
                      size_t trades = 0;
                      double dSharpe() const { return sharpe - bhSharpe; } };
        auto agrege = [](const std::vector<VixTermRegimeWindow>& ws) {
            AggT a; a.fen = ws.size();
            for (const auto& w : ws) {
                a.sharpe += w.oos.sharpeRatio; a.bhSharpe += w.oos.buyHoldSharpe;
                a.ret += w.oos.totalReturnPct; a.dd += w.oos.maxDrawdownPct;
                a.bhdd += w.oos.buyHoldMaxDrawdownPct; a.alpha += w.oos.alphaVsBuyHold;
                a.invest += w.oos.pctTimeInvested;
                a.trades += w.oos.trades.size();
            }
            if (a.fen) { const double n = static_cast<double>(a.fen);
                a.sharpe /= n; a.bhSharpe /= n; a.ret /= n; a.dd /= n; a.bhdd /= n;
                a.alpha /= n; a.invest /= n; }
            return a;
        };
        const std::vector<std::string> trio =
            { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_VIX_MAX_CSV, SWINGBOT_VIX3M_MAX_CSV };

        const auto tc = agrege(VixTermRegimeWalkForward(cfgTr(), trio, 750, 500, 500).run());
        const auto tf = agrege(VixTermRegimeWalkForward(cfgTr(), trio, 550, 400, 400).run());
        const auto ts = agrege(VixTermRegimeWalkForward(cfgTr(), trio, 750, 500, 500, 90).run());
        std::cout << std::fixed << std::setprecision(4)
                  << "\n  Binaire (seuil 1,0 / SMA 5) — dSharpe OOS vs B&H (QQQ) :\n"
                  << "    canon " << tc.dSharpe() << " (alpha " << tc.alpha
                  << ", investi " << tc.invest << " %, trades " << tc.trades << ")\n"
                  << "    fin   " << tf.dSharpe() << " (alpha " << tf.alpha
                  << ", investi " << tf.invest << " %, trades " << tf.trades << ")\n"
                  << "    decale " << ts.dSharpe() << " (alpha " << ts.alpha
                  << ", investi " << ts.invest << " %, trades " << ts.trades << ")\n";
        std::cout << "  DD (canon) : strat " << tc.dd << " % vs B&H " << tc.bhdd << " %\n";

        std::cout << "  Multi-univers (fin) :";
        const std::pair<const char*, const char*> univ[] = {
            {"QQQ", SWINGBOT_QQQ_MAX_CSV}, {"SPY", SWINGBOT_SPY_MAX_CSV},
            {"IWM", SWINGBOT_IWM_MAX_CSV}, {"MDY", SWINGBOT_MDY_MAX_CSV} };
        for (const auto& u : univ) {
            const auto agg = agrege(VixTermRegimeWalkForward(cfgTr(),
                { u.second, SWINGBOT_VIX_MAX_CSV, SWINGBOT_VIX3M_MAX_CSV },
                550, 400, 400).run());
            std::cout << "  " << u.first << " dS=" << agg.dSharpe();
        }
        std::cout << "\n";

        // Balayage seuil × lissage (critère : dSharpe OOS moyen, pavage fin).
        double best = -1e9; double bS = 0.0; int bL = 0;
        for (double se : {0.95, 1.0, 1.05}) for (int li : {1, 5, 10}) {
            VixTermRegimeConfig c = cfgTr(); c.ratioThreshold = se; c.smoothLookback = li;
            const double ds = agrege(VixTermRegimeWalkForward(c, trio,
                                                              550, 400, 400).run()).dSharpe();
            if (ds > best) { best = ds; bS = se; bL = li; }
        }
        std::cout << "  Balayage seuil x lissage : meilleur seuil=" << bS << " / lissage=" << bL
                  << " -> dSharpe OOS " << best
                  << (best > 0.0 ? "  (CANDIDAT)" : "  (aucun candidat)") << "\n";
        // Grille resserrée autour du candidat (leçon 8t.3), pavage CANONIQUE :
        // le signe du dSharpe doit être STABLE pour être un edge (8t.1).
        std::cout << "  Grille resserree (lissage 5, canon) :";
        for (double se : {1.04, 1.05, 1.06, 1.08}) {
            VixTermRegimeConfig c = cfgTr(); c.ratioThreshold = se; c.smoothLookback = 5;
            const auto ag = agrege(VixTermRegimeWalkForward(c, trio, 750, 500, 500).run());
            std::cout << "  s=" << se << " dS=" << ag.dSharpe();
        }
        std::cout << "\n";

        // Variante CONTINUE : poids = min(1, (VIX3M/VIX)^k), bande anti-churn.
        auto cfgTs = []() {
            VixTermScaledConfig c;
            c.steepness = 1.0; c.maxWeight = 1.0; c.rebalanceBand = 0.05;
            c.initialCapital = 10'000.0;
            c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
            return c;
        };
        struct AggC { size_t fen = 0; double alpha = 0.0, sharpe = 0.0, bhSharpe = 0.0,
                                             poids = 0.0;
                      size_t trades = 0;
                      double dSharpe() const { return sharpe - bhSharpe; } };
        auto agregeC = [](const std::vector<VixTermScaledWindow>& ws) {
            AggC a; a.fen = ws.size();
            for (const auto& w : ws) {
                a.alpha += w.oos.alphaVsBuyHold; a.sharpe += w.oos.sharpeRatio;
                a.bhSharpe += w.oos.buyHoldSharpe; a.poids += w.oos.avgWeight;
                a.trades += w.oos.trades.size();
            }
            if (a.fen) { const double n = static_cast<double>(a.fen);
                a.alpha /= n; a.sharpe /= n; a.bhSharpe /= n; a.poids /= n; }
            return a;
        };
        const auto cc = agregeC(VixTermScaledWalkForward(cfgTs(), trio, 750, 500, 500).run());
        const auto cf = agregeC(VixTermScaledWalkForward(cfgTs(), trio, 550, 400, 400).run());
        const auto cs = agregeC(VixTermScaledWalkForward(cfgTs(), trio, 750, 500, 500, 90).run());
        std::cout << "  Continu (pente 1 / bande 0,05) : dSharpe canon " << cc.dSharpe()
                  << " (alpha " << cc.alpha << ")  fin " << cf.dSharpe()
                  << " (alpha " << cf.alpha << ")  decale " << cs.dSharpe()
                  << " (alpha " << cs.alpha << ")  poids moy " << cc.poids << "\n";

        // Balayage pente × bande (critère : dSharpe OOS moyen, pavage fin).
        double bestC = -1e9; double bP = 0.0, bB = 0.0;
        for (double pe : {1.0, 2.0, 3.0}) for (double bd : {0.02, 0.05, 0.10}) {
            VixTermScaledConfig c = cfgTs(); c.steepness = pe; c.rebalanceBand = bd;
            const double ds = agregeC(VixTermScaledWalkForward(c, trio,
                                                               550, 400, 400).run()).dSharpe();
            if (ds > bestC) { bestC = ds; bP = pe; bB = bd; }
        }
        std::cout << "  Balayage pente x bande : meilleur pente=" << bP << " / bande=" << bB
                  << " -> dSharpe OOS " << bestC
                  << (bestC > 0.0 ? "  (CANDIDAT)" : "  (aucun candidat)") << "\n";

        // Monte-Carlo size-aware (D45) sur les trades OOS poolés (canonique, binaire).
        std::vector<TradeRecord> pool;
        double annees = 0.0;
        for (const auto& w : VixTermRegimeWalkForward(cfgTr(), trio, 750, 500, 500).run()) {
            for (const auto& t : w.oos.trades) pool.push_back(t);
            if (w.oos.equityDates.size() >= 2) {
                const long d0 = daysFromCivil(w.oos.equityDates.front());
                const long d1 = daysFromCivil(w.oos.equityDates.back());
                if (d1 > d0) annees += static_cast<double>(d1 - d0) / 365.25;
            }
        }
        if (!pool.empty()) {
            MonteCarlo mc(10'000.0, /*graine=*/42, /*chemins=*/2000);
            const auto rmc = mc.run(pool, annees);
            std::cout << "  Monte-Carlo size-aware (canon, seed 42, 2000 chemins, binaire) : cagrP50 "
                      << rmc.cagrP50 << " %  ddP95 " << rmc.ddP95 << " %\n";
        }

        std::cout << "  VERDICT 19.5 : AUCUN EDGE — la term-structure confirme son MECANISME\n"
                  << "  (l'inversion est rare : ~93 % investi en binaire, ~98 % de poids en continu\n"
                  << "  -> quasi plus de cash drag, la lecon D50/D55 est integree) mais n'en fait\n"
                  << "  PAS un edge : binaire dSharpe < 0 au seuil naturel sur les 3 pavages ;\n"
                  << "  le candidat du balayage (seuil 1,05 -> +0,07, une PREMIERE) est REFUTE par\n"
                  << "  la grille resserree (le signe s'inverse sur le canon a +/-0,01 de seuil,\n"
                  << "  lecon 8t.1/8t.3) ; continu dSharpe >= 0 partout mais <= 0,03 (bruit) et\n"
                  << "  alpha absolu negatif partout (DoD non atteinte). Axe 2006+ SANS dot-com.\n"
                  << "  Gate FERME. Prod paper. La DERNIERE piste offline du paragraphe 5 est soldee.\n";
    }

    // ── 22. INDICES DÉRIVÉS D'OPTIONS : SKEW / VVIX (Sprint 22) ─────────────
    // Chantier (r'') « données alternatives / surface d'options » (§5.4, décision
    // utilisateur 2026-07-11) : deux indices CBOE dérivés de la surface d'options
    // comme signal de régime, via le moteur GÉNÉRIQUE du Sprint 14 (aucun nouveau
    // moteur). Hypothèse (a) : ^SKEW — la queue chère (SKEW haut vs sa médiane
    // glissante) annonce-t-elle un régime à éviter ? Hypothèse (b) : ^VVIX — la
    // vol de la vol comme mesure d'incertitude. Verrous CI dans
    // test_skew_regime_oos_integration.cpp / test_vvix_regime_oos_integration.cpp.
    titre("22. INDICES DERIVES D'OPTIONS (^SKEW prix de queue, ^VVIX vol de la vol — 22.3)");
    {
        auto cfgIdx = []() {
            VixRegimeConfig c;
            c.refLookback = 126; c.thresholdMult = 1.0;
            c.initialCapital = 10'000.0;
            c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
            return c;
        };
        struct AggV { size_t fen = 0; double sharpe = 0.0, bhSharpe = 0.0, ret = 0.0,
                                            dd = 0.0, bhdd = 0.0; size_t trades = 0;
                      double dSharpe() const { return sharpe - bhSharpe; } };
        auto agrege = [](const std::vector<VixWindow>& ws) {
            AggV a; a.fen = ws.size();
            for (const auto& w : ws) {
                a.sharpe += w.oos.sharpeRatio; a.bhSharpe += w.oos.buyHoldSharpe;
                a.ret += w.oos.totalReturnPct; a.dd += w.oos.maxDrawdownPct;
                a.bhdd += w.oos.buyHoldMaxDrawdownPct; a.trades += w.oos.trades.size();
            }
            if (a.fen) { const double n = static_cast<double>(a.fen);
                a.sharpe /= n; a.bhSharpe /= n; a.ret /= n; a.dd /= n; a.bhdd /= n; }
            return a;
        };

        // Un indice = un bloc : pavages, multi-univers, balayage, MC — mêmes
        // chiffres que les verrous. La plage de seuils du SKEW est resserrée
        // (indice borné ~100-183) ; celle du VVIX reprend le Sprint 14 (vol-like).
        struct Idx { const char* nom; const char* csv; const char* axe;
                     std::vector<double> mults; };
        const Idx indices[] = {
            {"SKEW", SWINGBOT_SKEW_MAX_CSV, "1999+ (borne par QQQ — dot-com/2008 couverts)",
             {0.95, 1.0, 1.05, 1.1}},
            {"VVIX", SWINGBOT_VVIX_MAX_CSV, "2007+ (borne par VVIX — PAS de dot-com, caveat D57)",
             {0.8, 1.0, 1.2}},
        };
        for (const auto& ix : indices) {
            const std::vector<std::string> qi = { SWINGBOT_QQQ_MAX_CSV, ix.csv };
            const auto ac = agrege(VixRegimeWalkForward(cfgIdx(), qi, 750, 500, 500).run());
            const auto af = agrege(VixRegimeWalkForward(cfgIdx(), qi, 550, 400, 400).run());
            const auto as = agrege(VixRegimeWalkForward(cfgIdx(), qi, 750, 500, 500, 90).run());
            std::cout << std::fixed << std::setprecision(4)
                      << "\n  [" << ix.nom << "] axe " << ix.axe << "\n"
                      << "  dSharpe OOS vs B&H (QQQ gate " << ix.nom << ") : canon " << ac.dSharpe()
                      << " (strat " << ac.sharpe << " vs B&H " << ac.bhSharpe
                      << ", trades " << ac.trades << ")\n"
                      << "                                      fin   " << af.dSharpe()
                      << " (strat " << af.sharpe << " vs B&H " << af.bhSharpe
                      << ", trades " << af.trades << ")\n"
                      << "                                      decale " << as.dSharpe()
                      << " (strat " << as.sharpe << " vs B&H " << as.bhSharpe
                      << ", trades " << as.trades << ")\n";
            std::cout << "  DD reduit (canon) : DD strat " << ac.dd << " % vs DD B&H "
                      << ac.bhdd << " % (reduit mais < 50 %, clause DoD NON atteinte)\n";

            std::cout << "  Multi-univers (fin, tous gates sur le MEME " << ix.nom << ") :";
            const std::pair<const char*, const char*> univ[] = {
                {"QQQ", SWINGBOT_QQQ_MAX_CSV}, {"SPY", SWINGBOT_SPY_MAX_CSV},
                {"IWM", SWINGBOT_IWM_MAX_CSV}, {"MDY", SWINGBOT_MDY_MAX_CSV} };
            for (const auto& u : univ) {
                const auto agg = agrege(VixRegimeWalkForward(cfgIdx(),
                    { u.second, ix.csv }, 550, 400, 400).run());
                std::cout << "  " << u.first << " dS=" << agg.dSharpe();
            }
            std::cout << "\n";

            double best = -1e9; int bR = 0; double bM = 0.0;
            for (int rl : {63, 126, 252}) for (double mm : ix.mults) {
                VixRegimeConfig c = cfgIdx(); c.refLookback = rl; c.thresholdMult = mm;
                const double ds = agrege(VixRegimeWalkForward(c, qi, 550, 400, 400).run()).dSharpe();
                if (ds > best) { best = ds; bR = rl; bM = mm; }
            }
            std::cout << "  Balayage refLookback x seuil : meilleur rl=" << bR << " / m=" << bM
                      << " -> dSharpe OOS " << best
                      << (best > 0.0 ? "  (CANDIDAT)" : "  (aucun candidat)") << "\n";

            // Monte-Carlo size-aware (D45) sur les trades OOS poolés (canonique).
            std::vector<TradeRecord> pool;
            double annees = 0.0;
            for (const auto& w : VixRegimeWalkForward(cfgIdx(), qi, 750, 500, 500).run()) {
                for (const auto& t : w.oos.trades) pool.push_back(t);
                if (w.oos.equityDates.size() >= 2) {
                    const long d0 = daysFromCivil(w.oos.equityDates.front());
                    const long d1 = daysFromCivil(w.oos.equityDates.back());
                    if (d1 > d0) annees += static_cast<double>(d1 - d0) / 365.25;
                }
            }
            if (!pool.empty()) {
                MonteCarlo mc(10'000.0, /*graine=*/42, /*chemins=*/2000);
                const auto rmc = mc.run(pool, annees);
                std::cout << "  Monte-Carlo size-aware (canon, seed 42, 2000 chemins) : cagrP50 "
                          << rmc.cagrP50 << " %  ddP95 " << rmc.ddP95 << " %\n";
            }
        }

        std::cout << "\n  VERDICT 22.3 : AUCUN EDGE — ni le prix du risque de queue (SKEW) ni la\n"
                  << "  vol de la vol (VVIX) ne battent le B&H sur le Sharpe OOS : dSharpe < 0 sur\n"
                  << "  les 3 pavages, les 4 actifs et TOUS les reglages des deux balayages\n"
                  << "  (SKEW : meilleur -0,08 ; VVIX : meilleur -0,25 ; monotone — moins le filtre\n"
                  << "  intervient, moins il perd : la limite « toujours long » EST le B&H).\n"
                  << "  Aucun candidat > 0 -> grille resserree sans objet (8t.1/8t.3). Le SKEW\n"
                  << "  churne (300-343 stints OOS) et concentre les pertes (MC ddP95 62,5 %).\n"
                  << "  Gate FERME. Prod paper. Les deux hypotheses du chantier (r'') sont soldees.\n";
    }

    std::cout << "\n";
    return 0;
}
