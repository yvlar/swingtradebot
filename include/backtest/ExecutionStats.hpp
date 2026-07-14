#pragma once
#include "models/Models.hpp"
#include <algorithm>
#include <vector>

namespace trading {

// ─── ExecutionStats — instrumentation des signaux non exécutables ────────────
// (Sprint 23, item 23.1). Agrège les événements émis par les seams
// d'observation de TradingBot (setSignalObserver / setEntryObserver) et les
// mesures de déploiement relevées par le Backtester à chaque barre
// post-warmup. TOUT est compté à la source — aucun parsing de logs — et le
// backtest étant déterministe, les compteurs le sont aussi.
//
// Distinction clé pour le petit compte :
//   • entriesRejectedZeroQuantity  : le budget de risque vaut MOINS d'une
//     action entière → la contrainte d'action entière bloque, AUCUN ordre créé;
//   • entriesRejectedInsufficientCash : le risque autorisait ≥ 1 action mais
//     même une seule ne tient pas dans le cash utilisable;
//   • entriesRejectedRiskManager   : refus isTradeAllowed;
//   • entriesRejectedOrderFailed   : ordre soumis mais non exécuté (un ordre
//     PENDING n'est compté nulle part : la réconciliation tranchera).
struct ExecutionStats {
    long buySignalsGenerated  = 0;
    long sellSignalsGenerated = 0;

    long entriesAttempted                = 0;
    long entriesExecuted                 = 0;
    long entriesRejectedZeroQuantity     = 0;
    long entriesRejectedInsufficientCash = 0;
    long entriesRejectedRiskManager      = 0;
    long entriesRejectedOrderFailed      = 0;

    // Trace complète des tentatives : réutilisée par l'analyse fractionnaire
    // offline (item 23.5) — même signal, même prix, même capital.
    std::vector<EntryDecision> decisions;

    // Déploiement du capital, relevé PAR BARRE post-warmup (valeur de la
    // position / valeur du portefeuille, en %).
    double sumDeployedPct = 0.0;
    double maxDeployedPct = 0.0;
    long   barsObserved   = 0;

    void recordSignal(const Signal& s) {
        if (s.isBuy())       ++buySignalsGenerated;
        else if (s.isSell()) ++sellSignalsGenerated;
    }

    void recordEntry(const EntryDecision& d) {
        ++entriesAttempted;
        switch (d.outcome) {
            case EntryOutcome::Executee:             ++entriesExecuted; break;
            case EntryOutcome::RejetQuantiteZero:    ++entriesRejectedZeroQuantity; break;
            case EntryOutcome::RejetCashInsuffisant: ++entriesRejectedInsufficientCash; break;
            case EntryOutcome::RejetRiskManager:     ++entriesRejectedRiskManager; break;
            case EntryOutcome::RejetOrdreNonExecute: ++entriesRejectedOrderFailed; break;
        }
        decisions.push_back(d);
    }

    void recordBar(double positionValue, double portfolioValue) {
        const double pct = portfolioValue > 0.0
            ? positionValue / portfolioValue * 100.0 : 0.0;
        sumDeployedPct += pct;
        maxDeployedPct  = std::max(maxDeployedPct, pct);
        ++barsObserved;
    }

    // ── Métriques dérivées (item 23.1) ───────────────────────────────────────
    double averageCapitalDeployedPct() const {
        return barsObserved > 0 ? sumDeployedPct / barsObserved : 0.0;
    }
    double maximumCapitalDeployedPct() const { return maxDeployedPct; }
    double averageIdleCashPct() const {
        return 100.0 - averageCapitalDeployedPct();
    }
};

} // namespace trading
