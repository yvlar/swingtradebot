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

    std::cout << "\n";
    return 0;
}
