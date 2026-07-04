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
