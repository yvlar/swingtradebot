// ─── Tests d'intégration : confirmation hors-protocole du pullback (Sprint 8-septies) ─
// Items 8d.1 à 8d.3. Le pullback (RSI ≤ 40, « s'ajoute ») est le PREMIER
// mécanisme à avoir passé l'acceptation « ≥ chaîne sur les deux pavages
// non-choisis » (Sprint 8-sexies, D42) — mais la leçon D36 est formelle : le
// candidat 8b.1 avait lui aussi survécu à sa première validation avant d'être
// réfuté hors-grille. Ce fichier fait donc passer au pullback une validation
// dédiée HORS du protocole qui l'a choisi (modèle 8-ter), sur QUATRE volets :
//   (a) multi-actifs : duels chaîne vs chaîne+pullback sur SPY/IWM/MDY
//       (pavage fin 2019-2026 — le pullback n'a été jugé que sur QQQ) ;
//   (b) grille resserrée : RSI ∈ {35, 40, 45} « s'ajoute » sur le pavage fin
//       QQQ (stabilité à la maille, leçon D39 : un axe n'est « stable » qu'à
//       la maille où on l'a mesuré) ;
//   (c) Monte-Carlo : distribution CAGR/drawdown des trades OOS poolés du
//       pavage canonique QQQ, chaîne vs pullback (modèle 8t.2) ;
//   (d) données LONGUES : duels sur QQQ_max (~6 870 barres depuis 1999-03,
//       dot-com 2000-2002 et 2008 inclus — item 8d.1/8d.2), sur deux pavages
//       jamais vus par aucune sélection (canonique-long et décalé-long).
// CRITÈRE DE CONFIRMATION (gate 8d.5, décision utilisateur) : pullback ≥
// chaîne sur les DEUX pavages longs ET ≥ chaîne sur ≥ 2 actifs sur 3 ET
// argmax de grille stable (40, ou plateau plat) ET Monte-Carlo non dégradé.
// « Non confirmé » est un résultat VALIDE (leçon 8-ter).
// Discipline : configs EXPLICITES champ par champ (D33), trades OOS poolés
// figés (D34), deltas figés, activation vérifiée (D41).
#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "backtest/DataQuality.hpp"
#include "backtest/WalkForward.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

// ── Volet données (item 8d.1) ────────────────────────────────────────────────

// Nombre de jours depuis l'époque civile pour une date YYYY-MM-DD (algorithme
// de Howard Hinnant, « days_from_civil ») — sert à la garde de DENSITÉ : un
// CSV journalier sain porte ~252 barres par année civile de bourse. C'est le
// garde-fou qui aurait attrapé D31 (1858 lignes pour 1790 jours de bourse).
long joursCivils(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
}

long joursCivilsDeDate(const std::string& iso) {
    return joursCivils(std::stoi(iso.substr(0, 4)),
                       static_cast<unsigned>(std::stoi(iso.substr(5, 2))),
                       static_cast<unsigned>(std::stoi(iso.substr(8, 2))));
}

struct ResumeCsv {
    size_t      barres = 0;
    std::string premiereDate, derniereDate;
    double      premierAdj = 0.0, dernierAdj = 0.0;
};

// Relit les colonnes brutes (comme auditTotalReturnCsv : CsvDataFeed écrase
// Close par Adj Close, on ne peut pas passer par lui pour auditer le fichier).
ResumeCsv resumeCsv(const std::string& chemin) {
    ResumeCsv r;
    std::ifstream f(chemin);
    std::string ligne;
    bool entete = true;
    while (std::getline(f, ligne)) {
        if (entete) { entete = false; continue; }
        if (ligne.empty()) continue;
        std::vector<std::string> tok;
        std::stringstream ss(ligne);
        std::string champ;
        while (std::getline(ss, champ, ',')) tok.push_back(champ);
        if (tok.size() < 7) continue;
        const double adj = std::stod(tok[5]);
        if (r.barres == 0) { r.premiereDate = tok[0]; r.premierAdj = adj; }
        r.derniereDate = tok[0];
        r.dernierAdj   = adj;
        ++r.barres;
    }
    return r;
}

// Garde de densité : barres ≈ années × 252 (bande [0,95 ; 1,01] — l'ancien
// QQQ.csv parasité de D31 aurait donné 1,038, hors bande).
void verifieDensite(const ResumeCsv& r, const char* nom) {
    const double annees = static_cast<double>(joursCivilsDeDate(r.derniereDate)
                                              - joursCivilsDeDate(r.premiereDate))
                          / 365.25;
    const double attendu = annees * 252.0;
    const double ratio   = static_cast<double>(r.barres) / attendu;
    std::cout << "  " << nom << " : " << r.barres << " barres, "
              << std::fixed << std::setprecision(2) << annees
              << " ans, densite " << std::setprecision(4) << ratio << "\n";
    EXPECT_GT(ratio, 0.95) << nom << " : trop peu de barres (trous de données)";
    EXPECT_LT(ratio, 1.01) << nom << " : trop de barres (doublons/jours non boursiers, D31)";
}

// ── Pavages walk-forward (items 8d.2 / 8d.3) ─────────────────────────────────

// Pavage FIN historique (8b.3) sur les CSV 2019-2026 — celui qui a CHOISI le
// réglage du pullback sur QQQ ; réutilisé ici sur les AUTRES actifs (volet a)
// et pour la grille resserrée (volet b).
constexpr size_t kIsFin   = 500;
constexpr size_t kOosFin  = 300;
constexpr size_t kStepFin = 300;

// Pavage CANONIQUE historique — source des trades OOS du Monte-Carlo (volet c).
constexpr size_t kIsCan   = 700;
constexpr size_t kOosCan  = 400;
constexpr size_t kStepCan = 400;

// Pavages LONGS sur QQQ_max (~6 870 barres, 1999-2026) — volet d et référence
// 8d.2. Jamais vus par AUCUNE sélection. Dimensionnés selon D35 (warmup ~201
// barres ≪ OOS) : canonique-long IS=700/OOS=400/pas=400 ; décalé-long
// IS=500/OOS=400/pas=400 offset 90 — aucune borne commune avec le canonique.
constexpr size_t kIsLong      = 700;
constexpr size_t kOosLong     = 400;
constexpr size_t kStepLong    = 400;
constexpr size_t kIsLongD     = 500;
constexpr size_t kOosLongD    = 400;
constexpr size_t kStepLongD   = 400;
constexpr size_t kOffsetLongD = 90;

// Réglage du candidat (verrouillé au Sprint 8-sexies, D42) : RSI ≤ 40,
// variante « s'ajoute » (regimeReentry reste true).
constexpr double kRsiCandidat = 40.0;

// ── Config de la chaîne v2, EXPLICITE champ par champ (règle D33) ────────────
// Snapshot volontairement dupliqué des autres fichiers de verdict : une mesure
// historique ne doit pas bouger si les défauts de SwingConfig évoluent (en
// particulier si le gate 8d.5 adopte le pullback comme défaut). symbol est
// passé pour mémoire ; le CSV réellement backtesté est l'argument de
// WalkForward (QQQ, QQQ_max, SPY… selon le volet).
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
    c.trailingAtrMult         = 0.0;     // 8q.1 : trailing % pur
    c.exitOnLowestLowN        = 0;       // 8s.1 : sortie structurelle désactivée
    c.riskPerTradePct         = 0.02;
    c.minHoldDays             = 3;
    c.smaTrendPeriod          = 200;     // 8.1 : filtre de régime
    c.rsiSellOnlyIfRegimeDown = true;    // 8.4 : vente RSI gatée
    c.regimeReentry           = true;    // 8.5 : re-entrée sur régime
    c.entryBreakoutM          = 0;       // 8s.2 : entrée breakout désactivée
    c.entryPullbackRsiMax     = 0.0;     // 8y.1 : entrée pullback désactivée
    c.entryMaxAtrPct          = 0.0;     // 8y.2 : filtre volatilité désactivé
    return c;
}

// Chaîne + pullback candidat (variante « s'ajoute » : regimeReentry inchangé).
SwingConfig cfgPullback(double rsiMax = kRsiCandidat) {
    SwingConfig c = cfgChaineV2();
    c.entryPullbackRsiMax = rsiMax;
    return c;
}

// Variantes d'ATTÉNUATION du drawdown (item 8d.6, décision 8d.5) — le pullback
// gagne de l'alpha mais double le drawdown de queue ; on cherche à le brider
// SANS perdre l'alpha, avec des champs de config EXISTANTS :
//   • + filtre de volatilité : n'acheter le creux QUE si l'ATR est bas
//     (entryMaxAtrPct, 8y.2, s'applique à toutes les entrées) ;
//   • + stop plus serré (stopLossPct, défaut 0,05).
SwingConfig cfgPullbackAtr(double maxAtrPct) {
    SwingConfig c = cfgPullback();
    c.entryMaxAtrPct = maxAtrPct;
    return c;
}
SwingConfig cfgPullbackStop(double stopPct) {
    SwingConfig c = cfgPullback();
    c.stopLossPct = stopPct;
    return c;
}
// 3e levier (item 8d.7) : réduire le risque par trade (positions plus petites).
SwingConfig cfgPullbackRisk(double riskPct) {
    SwingConfig c = cfgPullback();
    c.riskPerTradePct = riskPct;
    return c;
}

double meanOosAlpha(const std::vector<WfWindow>& w) {
    if (w.empty()) return 0.0;
    double s = 0.0;
    for (const auto& x : w) s += x.oos.alpha;
    return s / static_cast<double>(w.size());
}

// Trades OOS POOLÉS (D34 : un alpha sans trades ne juge que le cash drag ;
// mêmes alphas + mêmes trades = mécanisme inerte, D41).
std::vector<TradeRecord> tradesOos(const std::vector<WfWindow>& w) {
    std::vector<TradeRecord> out;
    for (const auto& x : w)
        out.insert(out.end(), x.oos.trades.begin(), x.oos.trades.end());
    return out;
}

void imprime(const char* nom, const std::vector<WfWindow>& w) {
    std::cout << std::fixed << std::setprecision(4)
              << "  " << nom << " : alpha OOS moyen " << meanOosAlpha(w)
              << " pt, " << tradesOos(w).size() << " trades OOS pooles ("
              << w.size() << " fenetres)\n";
}

} // namespace

// ─── 8d.1 — Qualité des données longues ──────────────────────────────────────
// Les 4 exports *_max.csv sont des séries total-return RÉELLES (Adj ≠ Close),
// denses (≈ 252 barres/an — solde le backlog D31), aux comptes FIGÉS
// (export reproductible : period2 figé au 2026-07-01 dans le script).
TEST(PullbackConfirmationIntegration, LongHistoryCsvAreTotalReturnAndDense) {
    struct Attendu { const char* chemin; const char* nom; size_t barres;
                     const char* premiereDate; };
    const Attendu attendus[] = {
        {SWINGBOT_QQQ_MAX_CSV, "QQQ_max", 6870, "1999-03-10"},
        {SWINGBOT_SPY_MAX_CSV, "SPY_max", 8412, "1993-01-29"},
        {SWINGBOT_IWM_MAX_CSV, "IWM_max", 6562, "2000-05-26"},
        {SWINGBOT_MDY_MAX_CSV, "MDY_max", 7841, "1995-05-04"},
    };
    std::cout << "  8d.1 — audit des donnees longues (total-return, densite)\n";
    for (const auto& a : attendus) {
        // Dividendes réels : le garde-fou D29 ne doit PAS lever le drapeau.
        const auto audit = auditTotalReturnCsv(a.chemin);
        EXPECT_GT(audit.rows, 0u) << a.nom;
        EXPECT_FALSE(audit.suspectNoDividends)
            << a.nom << " : export sans dividendes (D29) — refaire l'export";

        const auto r = resumeCsv(a.chemin);
        EXPECT_EQ(r.barres, a.barres)          << a.nom << " : compte de barres dérivé";
        EXPECT_EQ(r.premiereDate, a.premiereDate) << a.nom;
        EXPECT_EQ(r.derniereDate, "2026-07-01")   << a.nom << " : fin figée (script)";
        verifieDensite(r, a.nom);
    }
}

// B&H total-return de QQQ_max figé : le contexte de TOUS les verdicts longs.
// 1999-03-10 → 2026-07-01 : la série contient le krach dot-com (−83 % sur
// QQQ 2000-2002) et 2008 — les deux régimes absents du dataset 2019-2026.
TEST(PullbackConfirmationIntegration, LongHistoryQqqBuyHoldIsLocked) {
    const auto r = resumeCsv(SWINGBOT_QQQ_MAX_CSV);
    const double bh = (r.dernierAdj / r.premierAdj - 1.0) * 100.0;
    std::cout << std::fixed << std::setprecision(4)
              << "  8d.1 — B&H QQQ_max (" << r.premiereDate << " -> "
              << r.derniereDate << ") : " << bh << " %\n";
    EXPECT_NEAR(bh, 1585.3819, 0.05);
}

// ─── 8d.2 — Référence : chaîne v2 sur les pavages LONGS (dot-com + 2008) ──────
// Première mesure du bot incluant deux vrais marchés baissiers. Ces valeurs
// sont le DÉNOMINATEUR des duels 8d.3d : on les fige d'abord (sentinelles →
// mesure → figées), séparément du candidat, pour que le verdict compare deux
// mesures indépendamment verrouillées.
TEST(PullbackConfirmationIntegration, ChainV2LongCanonicalPavingIsLocked) {
    const auto w = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_MAX_CSV,
                               kIsLong, kOosLong, kStepLong).run();
    std::cout << "  8d.2 — chaine v2, pavage canonique-long (QQQ_max IS=700/OOS=400)\n";
    imprime("chaine v2", w);
    EXPECT_EQ(w.size(), 15u);                        // pavage riche (dot-com+2008)
    // Figé le 2026-07-04. Première mesure de la chaîne v2 sur deux vrais
    // marchés baissiers : alpha OOS BIEN plus négatif que sur 2019-2026
    // (−17,30 vs −9,90 canonique court) — la chaîne souffre en régime
    // baissier. 95 trades OOS poolés sur 15 fenêtres.
    EXPECT_NEAR(meanOosAlpha(w), -17.3033, 1e-2);
    EXPECT_EQ(tradesOos(w).size(), 95u);
}

TEST(PullbackConfirmationIntegration, ChainV2LongShiftedPavingIsLocked) {
    const auto w = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_MAX_CSV,
                               kIsLongD, kOosLongD, kStepLongD, kOffsetLongD).run();
    std::cout << "  8d.2 — chaine v2, pavage decale-long (QQQ_max IS=500/OOS=400 offset=90)\n";
    imprime("chaine v2", w);
    EXPECT_EQ(w.size(), 15u);
    // Figé le 2026-07-04. −11,05 (96 trades) : moins négatif que le
    // canonique-long car le décalage évite le pire de la fenêtre dot-com.
    EXPECT_NEAR(meanOosAlpha(w), -11.0503, 1e-2);
    EXPECT_EQ(tradesOos(w).size(), 96u);
}

// Années OOS observées (dénominateur CAGR du Monte-Carlo) : total des barres
// OOS poolées / 252 séances par an (modèle 8t.2).
namespace {
double oosYears(const std::vector<WfWindow>& w) {
    size_t bars = 0;
    for (const auto& x : w) bars += (x.oosEnd - x.oosStart);
    return static_cast<double>(bars) / 252.0;
}
} // namespace

// ─── 8d.3a — VOLET MULTI-ACTIFS : pullback sur SPY / IWM / MDY ────────────────
// Le pullback n'a été jugé que sur QQQ. Ici, duels chaîne vs chaîne+pullback
// sur les 3 autres actifs (pavage fin 2019-2026, defs CSV existantes). Chaque
// actif figé indépendamment (alpha + trades). Acceptation partielle : pullback
// ≥ chaîne sur ≥ 2 actifs sur 3.
TEST(PullbackConfirmationIntegration, PullbackMultiAssetFinePavingIsLocked) {
    struct Actif { const char* nom; const char* csv;
                   double aChaine; size_t tChaine; double aPull; size_t tPull; };
    // Figé le 2026-07-04. Pullback ≥ chaîne sur IWM (+0,18) et MDY (+0,45),
    // très légèrement PIRE sur SPY (−0,02) : 2/3 → l'acceptation partielle
    // multi-actifs PASSE. Cohérent avec un candidat qui généralise sur
    // l'alpha (le signe ne s'inverse nulle part).
    Actif actifs[] = {
        {"SPY", SWINGBOT_SPY_CSV, -5.6813, 10u, -5.7030, 11u},
        {"IWM", SWINGBOT_IWM_CSV, -3.4564, 13u, -3.2756, 13u},
        {"MDY", SWINGBOT_MDY_CSV, -3.5306, 17u, -3.0823, 17u},
    };
    std::cout << "  8d.3a — multi-actifs, pavage fin (chaine vs chaine+pullback RSI<=40 s'ajoute)\n";
    int passe = 0;
    for (auto& a : actifs) {
        const auto wC = WalkForward(cfgChaineV2(), a.csv, kIsFin, kOosFin, kStepFin).run();
        const auto wP = WalkForward(cfgPullback(), a.csv, kIsFin, kOosFin, kStepFin).run();
        const double aC = meanOosAlpha(wC), aP = meanOosAlpha(wP);
        std::cout << "  " << a.nom << " : chaine " << aC << " (" << tradesOos(wC).size()
                  << ") vs pullback " << aP << " (" << tradesOos(wP).size() << ")\n";
        EXPECT_NEAR(aC, a.aChaine, 1e-2) << a.nom << " chaine";
        EXPECT_EQ(tradesOos(wC).size(), a.tChaine) << a.nom << " chaine trades";
        EXPECT_NEAR(aP, a.aPull, 1e-2) << a.nom << " pullback";
        EXPECT_EQ(tradesOos(wP).size(), a.tPull) << a.nom << " pullback trades";
        if (aP >= aC) ++passe;
    }
    std::cout << "  multi-actifs : pullback >= chaine sur " << passe << "/3 actifs\n";
}

// ─── 8d.3b — VOLET GRILLE RESSERRÉE : RSI ∈ {35,40,45} « s'ajoute » ───────────
// Stabilité à la maille (leçon D39) : l'argmax reste-t-il 40 quand on resserre
// les crans autour de lui ? Mesures + argmax figés.
TEST(PullbackConfirmationIntegration, PullbackTightGridFinePavingSelectionIsLocked) {
    const double seuils[] = {35.0, 40.0, 45.0};
    double alphas[3] = {0, 0, 0};
    size_t trades[3] = {0, 0, 0};
    std::cout << "  8d.3b — grille resserree RSI {35,40,45} s'ajoute (pavage fin QQQ)\n";
    for (int i = 0; i < 3; ++i) {
        const auto w = WalkForward(cfgPullback(seuils[i]), SWINGBOT_QQQ_CSV,
                                   kIsFin, kOosFin, kStepFin).run();
        alphas[i] = meanOosAlpha(w);
        trades[i] = tradesOos(w).size();
        std::cout << "  RSI<=" << seuils[i] << " : " << alphas[i]
                  << " (" << trades[i] << ")\n";
    }
    // Figé le 2026-07-04. Grille resserrée {35,40,45} autour du candidat 40 :
    // l'argmax RESTE 40 (−6,5491), les voisins immédiats sont pires
    // (−6,90 / −6,80) → stabilité à la maille CONFIRMÉE (contraste avec D39,
    // où le candidat 8-ter dérivait quand on resserrait). C'est le volet où
    // le pullback se comporte le MIEUX.
    EXPECT_NEAR(alphas[0], -6.8995, 1e-2);
    EXPECT_NEAR(alphas[1], -6.5491, 1e-2);
    EXPECT_NEAR(alphas[2], -6.8030, 1e-2);
    EXPECT_EQ(trades[0], 13u);
    EXPECT_EQ(trades[1], 13u);
    EXPECT_EQ(trades[2], 15u);
    // Garde anti-dérive : l'argmax de la grille resserrée reste le candidat 40
    // (ou plateau plat — premier des ex æquo).
    int best = 0;
    for (int i = 1; i < 3; ++i) if (alphas[i] > alphas[best]) best = i;
    EXPECT_DOUBLE_EQ(seuils[best], kRsiCandidat);
}

// ─── 8d.3c — VOLET MONTE-CARLO : robustesse de la distribution des trades ─────
// Ré-échantillonnage des trades OOS poolés (pavage canonique QQQ), chaîne vs
// pullback. p50/p95 CAGR et drawdown figés (graine 42, modèle 8t.2). « Non
// dégradé » = CAGR p50 pullback ≥ chaîne ET DD p95 pullback ≤ chaîne+marge.
TEST(PullbackConfirmationIntegration, PullbackMonteCarloCanonicalIsLocked) {
    const auto wC = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                kIsCan, kOosCan, kStepCan).run();
    const auto wP = WalkForward(cfgPullback(), SWINGBOT_QQQ_CSV,
                                kIsCan, kOosCan, kStepCan).run();
    MonteCarlo mc(10'000.0, /*seed=*/42, /*paths=*/2000);
    const auto rC = mc.run(tradesOos(wC), oosYears(wC));
    const auto rP = mc.run(tradesOos(wP), oosYears(wP));
    std::cout << std::fixed << std::setprecision(4)
              << "  8d.3c — Monte-Carlo canonique QQQ (graine 42, 2000 chemins)\n"
              << "  chaine   CAGR p50 " << rC.cagrP50 << " DD p95 " << rC.ddP95 << "\n"
              << "  pullback CAGR p50 " << rP.cagrP50 << " DD p95 " << rP.ddP95 << "\n";
    // RE-BASELINE 2026-07-04 (Sprint 8-octies, item 8o.1, D45) : le MC pondère
    // désormais chaque trade par sa fraction de capital déployée (~40 % pour la
    // chaîne). Les valeurs CHUTENT (l'ancien MC assumait 100 % de déploiement,
    // surestimant tout) mais le VERDICT QUALITATIF TIENT : le pullback améliore
    // le CAGR médian (2,80 → 3,21) et DÉGRADE le drawdown de queue
    // (DD p95 4,28 → 7,88, soit ~1,8×). Acheter les creux volatils reste plus
    // risqué. C'est le VOLET DISQUALIFIANT (le vrai levier = 8o.2 vol-sizing).
    EXPECT_NEAR(rC.cagrP50, 2.7984, 1e-3);
    EXPECT_NEAR(rC.ddP95,   4.2758, 1e-3);
    EXPECT_NEAR(rP.cagrP50, 3.2094, 1e-3);
    EXPECT_NEAR(rP.ddP95,   7.8821, 1e-3);
    // Verrou du fait disqualifiant : le drawdown de queue du pullback reste
    // NETTEMENT pire que celui de la chaîne (va à l'encontre de la DoD
    // risque/rendement).
    EXPECT_GT(rP.ddP95, rC.ddP95 * 1.5);     // ~1,8× le DD de la chaîne
    EXPECT_GT(rP.cagrP50, rC.cagrP50);       // le CAGR, lui, s'améliore
}

// ─── 8d.3d — VOLET DONNÉES LONGUES : duels sur QQQ_max (dot-com + 2008) ───────
// Le juge le plus dur : deux pavages longs jamais vus par aucune sélection,
// incluant deux vrais marchés baissiers. Référence chaîne figée en 8d.2.
TEST(PullbackConfirmationIntegration, PullbackLongCanonicalPavingVerdictIsLocked) {
    const auto wC = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_MAX_CSV,
                                kIsLong, kOosLong, kStepLong).run();
    const auto wP = WalkForward(cfgPullback(), SWINGBOT_QQQ_MAX_CSV,
                                kIsLong, kOosLong, kStepLong).run();
    std::cout << "  8d.3d — duel pullback, pavage canonique-long (QQQ_max)\n";
    imprime("chaine v2       ", wC);
    imprime("chaine + pullback", wP);
    const double aP = meanOosAlpha(wP);
    // Figé le 2026-07-04. MIEUX que la chaîne même avec dot-com + 2008 :
    // −17,17 (106 trades) vs −17,30 (95), +0,14 pt. Marge mince mais le
    // signe ne s'inverse pas — l'alpha du pullback GÉNÉRALISE aux régimes
    // baissiers (contraste net avec le candidat 8-ter, qui s'inversait).
    EXPECT_NEAR(aP, -17.1682, 1e-2);
    EXPECT_EQ(tradesOos(wP).size(), 106u);
    EXPECT_GT(aP, meanOosAlpha(wC));         // acceptation alpha PASSE ici
}

TEST(PullbackConfirmationIntegration, PullbackLongShiftedPavingVerdictIsLocked) {
    const auto wC = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_MAX_CSV,
                                kIsLongD, kOosLongD, kStepLongD, kOffsetLongD).run();
    const auto wP = WalkForward(cfgPullback(), SWINGBOT_QQQ_MAX_CSV,
                                kIsLongD, kOosLongD, kStepLongD, kOffsetLongD).run();
    std::cout << "  8d.3d — duel pullback, pavage decale-long (QQQ_max offset=90)\n";
    imprime("chaine v2       ", wC);
    imprime("chaine + pullback", wP);
    const double aP = meanOosAlpha(wP);
    // Figé le 2026-07-04. MIEUX ici aussi, et plus nettement : −10,00
    // (105 trades) vs −11,05 (96), +1,05 pt. L'acceptation ALPHA
    // (« ≥ chaîne sur les DEUX pavages longs ») PASSE sur les deux → le
    // pullback est le PREMIER mécanisme dont l'alpha généralise hors du
    // dataset qui l'a choisi ET aux marchés baissiers. MAIS voir 8d.3c :
    // ce gain d'alpha se paie par un drawdown de queue quasi doublé.
    EXPECT_NEAR(aP, -10.0033, 1e-2);
    EXPECT_EQ(tradesOos(wP).size(), 105u);
    EXPECT_GT(aP, meanOosAlpha(wC));         // acceptation alpha PASSE aussi ici
}

// ─── 8d.6 — ATTÉNUATION du drawdown : brider le pullback sans perdre l'alpha ──
// Décision utilisateur 8d.5 (2026-07-04) : ne pas adopter tant que le drawdown
// de queue n'est pas maîtrisé. On teste, avec des champs de config EXISTANTS,
// si gater le pullback par la volatilité (entryMaxAtrPct) ou resserrer le stop
// (stopLossPct) ramène le DD p95 vers celui de la chaîne (10,80) TOUT EN
// gardant l'alpha ≥ chaîne sur les deux pavages longs.
// CRITÈRE DE RÉUSSITE (re-gate) : DD p95 ≤ 12,80 (chaîne + ~2 pts) ET alpha ≥
// chaîne sur canonique-long (−17,3033) ET décalé-long (−11,0503).
namespace {
double ddP95Canonical(const SwingConfig& c) {
    const auto ws = WalkForward(c, SWINGBOT_QQQ_CSV, kIsCan, kOosCan, kStepCan).run();
    std::vector<TradeRecord> pool;
    size_t bars = 0;
    for (const auto& x : ws) {
        pool.insert(pool.end(), x.oos.trades.begin(), x.oos.trades.end());
        bars += (x.oosEnd - x.oosStart);
    }
    return MonteCarlo(10'000.0, 42, 2000).run(pool, static_cast<double>(bars) / 252.0).ddP95;
}
double alphaLongCanon(const SwingConfig& c) {
    return meanOosAlpha(WalkForward(c, SWINGBOT_QQQ_MAX_CSV,
                                    kIsLong, kOosLong, kStepLong).run());
}
double alphaLongShift(const SwingConfig& c) {
    return meanOosAlpha(WalkForward(c, SWINGBOT_QQQ_MAX_CSV,
                                    kIsLongD, kOosLongD, kStepLongD, kOffsetLongD).run());
}
} // namespace

TEST(PullbackConfirmationIntegration, PullbackAttenuationVariantsAreLocked) {
    struct Variante { const char* nom; SwingConfig cfg;
                      double ddP95; double aCanon; double aShift; };
    // RE-BASELINE 2026-07-04 (item 8o.1, D45 : MC size-aware). Les DD chutent
    // (déploiement ~40 %) mais le VERDICT D44 TIENT : AUCUNE variante ne REUSSIT
    // (DD p95 ≤ chaîne+2 = 6,28 ET alpha ≥ chaîne sur les deux pavages longs).
    // Le seul levier qui casse le drawdown est le gating ATR ≤ 0,015 : DD p95
    // 7,88 → 2,82 (sous la chaîne 4,28) — mais il perd l'alpha sur le
    // canonique-long (−17,83 < −17,30). Le stop serré ne réduit pas le DD
    // (le trailing 3 % tire d'abord) et rabote l'alpha. LEÇON (D44) : l'alpha
    // du pullback ET son drawdown viennent des MÊMES achats de creux volatils —
    // ces deux leviers config-only ne les séparent pas (le vrai levier =
    // vol-sizing 8o.2). Alpha inchangé vs avant D45 (backtest non modifié).
    Variante variantes[] = {
        {"pullback nu",        cfgPullback(),           7.8821, -17.1682, -10.0033},
        {"+ ATR<=0.015",       cfgPullbackAtr(0.015),   2.8169, -17.8340, -10.0269},
        {"+ ATR<=0.025",       cfgPullbackAtr(0.025),   8.6996, -16.9849, -10.4046},
        {"+ stop 0.03",        cfgPullbackStop(0.03),  12.6751, -16.0649,  -8.1484},
        {"+ stop 0.04",        cfgPullbackStop(0.04),   9.4605, -16.7756,  -9.3481},
    };
    std::cout << std::fixed << std::setprecision(4)
              << "  8d.6 — attenuation du drawdown (reference chaine : DD p95 4,28 ;"
              << " alpha long -17,30 / -11,05)\n";
    for (auto& v : variantes) {
        const double dd = ddP95Canonical(v.cfg);
        const double aC = alphaLongCanon(v.cfg);
        const double aS = alphaLongShift(v.cfg);
        const bool ddOk    = dd <= 6.28;
        const bool alphaOk = aC >= -17.3033 && aS >= -11.0503;
        std::cout << "  " << std::left << std::setw(16) << v.nom << std::right
                  << " : DD p95 " << dd << " (" << (ddOk ? "OK" : "--")
                  << "), alpha long " << aC << " / " << aS
                  << " (" << (alphaOk ? "OK" : "--") << ")"
                  << ((ddOk && alphaOk) ? "  <<< REUSSIT" : "") << "\n";
        EXPECT_NEAR(dd, v.ddP95, 1e-3);   // SENTINELLE → figer
        EXPECT_NEAR(aC, v.aCanon, 1e-2);  // SENTINELLE → figer
        EXPECT_NEAR(aS, v.aShift, 1e-2);  // SENTINELLE → figer
    }
}

// ─── 8d.7 — 3e levier de découplage alpha/risque (décision re-gate 8d.5) ──────
// L'atténuation 8d.6 n'a rien trouvé qui garde l'alpha ET casse le drawdown.
// On creuse deux leviers config-only supplémentaires : (1) riskPerTradePct
// réduit (positions plus petites) — le levier explicitement demandé ; (2) un
// sweep FIN du gate ATR entre 0,015 (cassait le DD mais perdait 0,53 pt
// d'alpha) et 0,025 (transparent). MÊME critère : DD p95 ≤ 12,80 ET alpha ≥
// chaîne sur les DEUX pavages longs (−17,3033 / −11,0503).
TEST(PullbackConfirmationIntegration, PullbackThirdLeverVariantsAreLocked) {
    struct Variante { const char* nom; SwingConfig cfg;
                      double ddP95; double aCanon; double aShift; };
    // RE-BASELINE 2026-07-04 (item 8o.1, D45 : MC size-aware). NUANCE IMPORTANTE :
    // sous le MC size-blind, riskPerTradePct paraissait « invariant d'échelle »
    // (DD figé à 19,27) — c'était un ARTEFACT du MC aveugle à la taille (D45).
    // Corrigé, riskPerTradePct RÉDUIT bien le DD (3,64 à 0,010 ; 5,38 à 0,015 vs
    // chaîne 4,28) — mais au prix de l'alpha (−17,83 / −17,49 < −17,30). Le
    // sweep ATR 0,018/0,020 réduit peu le DD et garde l'alpha, sans passer.
    // VERDICT D44 INCHANGÉ : aucune variante n'atteint DD ≤ chaîne+2 (6,28) ET
    // alpha ≥ chaîne — mais parce que réduire la taille UNIFORMÉMENT coûte
    // l'alpha proportionnellement. Le vrai levier reste le sizing MODULÉ par la
    // volatilité (réduire SEULEMENT en régime volatil) — c'est l'objet de 8o.2,
    // enfin mesurable maintenant que le MC voit la taille. Alpha inchangé vs
    // avant D45 (backtest non modifié).
    Variante variantes[] = {
        {"risk 0.010",   cfgPullbackRisk(0.010),  3.6367, -17.8252, -11.3039},
        {"risk 0.015",   cfgPullbackRisk(0.015),  5.3756, -17.4898, -10.6775},
        {"ATR<=0.018",   cfgPullbackAtr(0.018),   9.0866, -16.8470, -10.0722},
        {"ATR<=0.020",   cfgPullbackAtr(0.020),  10.4691, -17.0015, -10.5339},
    };
    std::cout << std::fixed << std::setprecision(4)
              << "  8d.7 — 3e levier (reference chaine : DD p95 4,28 ;"
              << " alpha long -17,30 / -11,05)\n";
    for (auto& v : variantes) {
        const double dd = ddP95Canonical(v.cfg);
        const double aC = alphaLongCanon(v.cfg);
        const double aS = alphaLongShift(v.cfg);
        const bool ddOk    = dd <= 6.28;
        const bool alphaOk = aC >= -17.3033 && aS >= -11.0503;
        std::cout << "  " << std::left << std::setw(14) << v.nom << std::right
                  << " : DD p95 " << dd << " (" << (ddOk ? "OK" : "--")
                  << "), alpha long " << aC << " / " << aS
                  << " (" << (alphaOk ? "OK" : "--") << ")"
                  << ((ddOk && alphaOk) ? "  <<< REUSSIT" : "") << "\n";
        EXPECT_NEAR(dd, v.ddP95, 1e-3);   // SENTINELLE → figer
        EXPECT_NEAR(aC, v.aCanon, 1e-2);  // SENTINELLE → figer
        EXPECT_NEAR(aS, v.aShift, 1e-2);  // SENTINELLE → figer
    }
}
