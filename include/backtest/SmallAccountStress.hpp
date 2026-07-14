#pragma once
#include "backtest/ExecutionCostModel.hpp"
#include <algorithm>
#include <cmath>

namespace trading {

// ─── Stress du petit compte (Sprint 23, item 23.6) ───────────────────────────
// Analyses de SURVIE indépendantes de la recherche d'une nouvelle stratégie :
// séquences de pertes consécutives au stop (en COMPOSITION réelle : chaque
// perte est dimensionnée sur le capital RESTANT, pas « n × risque initial »),
// gaps défavorables (fill au-delà du stop prévu) et niveaux de slippage.
// Ces chiffres sont des SCÉNARIOS DE STRESS déterministes, pas des
// prédictions ; aucun d'eux ne modifie la stratégie ni ne déclenche le live.

struct LossSequenceResult {
    double capitalRemaining = 0.0;  // capital après la séquence
    double capitalLossPct   = 0.0;  // perte en % du capital initial
    double drawdownPct      = 0.0;  // = capitalLossPct (pertes monotones)
    int    tradesExecuted   = 0;    // trades réellement exécutables (qty ≥ 1)
    int    tradesBlocked    = 0;    // trades sautés faute d'une action entière
};

namespace detail {

// Sizing du moteur (RiskManager::positionSize, actions entières, plafond de
// capital utilisable 95 %) — reproduit la formule de production.
inline int sizePosition(double cash, double price, double stopPct,
                        double riskPct, double maxCapitalUsagePct = 0.95) {
    if (cash <= 0 || price <= 0 || stopPct <= 0 || riskPct <= 0) return 0;
    const int parRisque  = static_cast<int>(cash * riskPct / (price * stopPct));
    const int parCapital = static_cast<int>(cash * maxCapitalUsagePct / price);
    return std::max(0, std::min(parRisque, parCapital));
}

// Un aller-retour PERDANT complet, coûts inclus. `exitDropPct` est la chute
// entre le prix d'ENTRÉE (fill) et le prix de sortie : stop nominal (0,05) ou
// gap au-delà (0,06/0,08/0,10). Renvoie le cash après le trade, ou `cash`
// inchangé si aucune action entière n'est finançable (trade bloqué).
inline double losingRoundTrip(double cash, double price, double stopPct,
                              double riskPct, double exitDropPct,
                              const ExecutionCostConfig& couts,
                              bool* executed = nullptr) {
    const double pen      = couts.fillPenaltyFraction();
    const double buyPrice = price * (1.0 + pen);
    int qty = sizePosition(cash, price, stopPct, riskPct);
    // Le coût total (notionnel + commission) doit tenir dans le cash — même
    // décrémentation que le PaperBroker (la commission n'est pas linéaire).
    while (qty > 0 && buyPrice * qty + couts.commissionForOrder(buyPrice, qty)
                          > cash)
        --qty;
    if (qty <= 0) { if (executed) *executed = false; return cash; }
    if (executed) *executed = true;

    const double buyCommission = couts.commissionForOrder(buyPrice, qty);
    // Sortie stressée : fill à exitDropPct SOUS le prix d'entrée, encore
    // dégradé du slippage/demi-spread de sortie.
    const double sellPrice      = buyPrice * (1.0 - exitDropPct) * (1.0 - pen);
    const double sellCommission = couts.commissionForOrder(sellPrice, qty);

    return cash - (buyPrice * qty + buyCommission)
                + (sellPrice * qty - sellCommission);
}

} // namespace detail

// Séquence de `nLosses` pertes CONSÉCUTIVES au stop, en composition réelle :
// chaque trade est re-dimensionné sur le capital restant (formule de
// production, actions entières), coûts payés des deux côtés. `price` est le
// prix de référence constant de l'actif (hypothèse de stress : le titre
// re-testé au même niveau — le paramètre isole l'effet du SIZING).
inline LossSequenceResult simulateLossSequence(double initialCapital,
                                               double riskPct,
                                               double stopPct,
                                               int nLosses,
                                               double price,
                                               const ExecutionCostConfig& couts) {
    LossSequenceResult r;
    double cash = initialCapital;
    for (int i = 0; i < nLosses; ++i) {
        bool executed = false;
        cash = detail::losingRoundTrip(cash, price, stopPct, riskPct,
                                       stopPct, couts, &executed);
        if (executed) ++r.tradesExecuted; else ++r.tradesBlocked;
    }
    r.capitalRemaining = cash;
    r.capitalLossPct   = initialCapital > 0
        ? (initialCapital - cash) / initialCapital * 100.0 : 0.0;
    r.drawdownPct      = r.capitalLossPct;   // pertes monotones : DD = perte
    return r;
}

// Gap défavorable : UN trade dont la sortie se remplit à `gapDropPct` sous le
// prix d'entrée (au-delà du stop prévu `stopPct`), coûts inclus. Compare la
// perte réelle à la perte PLANIFIÉE (capital × risque) : le dépassement est
// exactement ce qu'un stop ne garantit PAS sur un gap d'ouverture.
struct GapStressResult {
    double lossDollars       = 0.0;  // perte réelle du trade (coûts inclus)
    double plannedLossDollars = 0.0; // perte théorique planifiée (capital × risque)
    double lossPctOfCapital  = 0.0;
    bool   executed          = false; // false = aucune action entière finançable
};

inline GapStressResult simulateGapExit(double initialCapital,
                                       double riskPct,
                                       double stopPct,
                                       double gapDropPct,
                                       double price,
                                       const ExecutionCostConfig& couts) {
    GapStressResult r;
    r.plannedLossDollars = initialCapital * riskPct;
    bool executed = false;
    const double apres = detail::losingRoundTrip(initialCapital, price, stopPct,
                                                 riskPct, gapDropPct, couts,
                                                 &executed);
    r.executed = executed;
    if (executed) {
        r.lossDollars      = initialCapital - apres;
        r.lossPctOfCapital = initialCapital > 0
            ? r.lossDollars / initialCapital * 100.0 : 0.0;
    }
    return r;
}

} // namespace trading
