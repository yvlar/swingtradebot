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

    std::cout << "\n";
    return 0;
}
