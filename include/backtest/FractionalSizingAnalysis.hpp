#pragma once
#include "models/Models.hpp"
#include <algorithm>
#include <vector>

namespace trading {

// ─── Analyse des fractions d'action — STRICTEMENT OFFLINE (Sprint 23, 23.5) ──
// Contre-factuel ANALYTIQUE : « quelle aurait été la quantité si les fractions
// d'action étaient possibles ? ». Réutilise le signal, le prix, le capital et
// la formule de risque EXACTE du RiskManager (via la trace EntryDecision de
// l'instrumentation 23.1) — AUCUN moteur dupliqué, AUCUN type de quantité des
// brokers live modifié : submitBuy/submitSell restent en int, IBKR et Alpaca
// sont intacts. Les quantités fractionnaires ci-dessous sont THÉORIQUES et ne
// sortent jamais du harnais de validation.
struct FractionalSizingAnalysis {
    long   signalsAnalyzed                      = 0;
    // Signaux dont la quantité entière vaut 0 ALORS QUE la quantité
    // fractionnaire est > 0 : bloqués UNIQUEMENT par la contrainte entière.
    long   signalsBlockedOnlyByIntegerConstraint = 0;
    double avgTheoreticalFractionalQuantity     = 0.0;
    double avgIntegerQuantity                   = 0.0;
    // Déploiement (valeur de position / cash disponible) en % — moyenne sur
    // les tentatives analysées.
    double integerDeploymentPct                 = 0.0;
    double fractionalDeploymentPct              = 0.0;
    // Points de % de déploiement PERDUS à cause de la troncature entière.
    double lostDeploymentPctDueToIntegerConstraint = 0.0;
};

// Analyse la trace des tentatives d'entrée (23.1). La quantité fractionnaire
// théorique reprend la formule du RiskManager SANS la troncature :
//   min( cash × risque / (prix × stop),  cash × maxCapitalUsage / prix )
// `maxCapitalUsagePct` = 0,95 par défaut (valeur du RiskManager de prod).
inline FractionalSizingAnalysis analyzeFractionalSizing(
    const std::vector<EntryDecision>& decisions,
    double riskPerTradePct,
    double stopLossPct,
    double maxCapitalUsagePct = 0.95) {

    FractionalSizingAnalysis a;
    if (decisions.empty() || stopLossPct <= 0.0 || riskPerTradePct <= 0.0)
        return a;

    double sumFrac = 0.0, sumInt = 0.0, sumFracDep = 0.0, sumIntDep = 0.0;
    for (const auto& d : decisions) {
        if (d.price <= 0.0 || d.cash <= 0.0) continue;
        ++a.signalsAnalyzed;

        const double parRisque  = d.cash * riskPerTradePct
                                  / (d.price * stopLossPct);
        const double parCapital = d.cash * maxCapitalUsagePct / d.price;
        const double frac = std::min(parRisque, parCapital);
        const double entiere = static_cast<double>(std::max(d.shares, 0));

        if (entiere <= 0.0 && frac > 0.0)
            ++a.signalsBlockedOnlyByIntegerConstraint;

        sumFrac    += frac;
        sumInt     += entiere;
        sumFracDep += frac    * d.price / d.cash * 100.0;
        sumIntDep  += entiere * d.price / d.cash * 100.0;
    }
    if (a.signalsAnalyzed == 0) return a;

    const double n = static_cast<double>(a.signalsAnalyzed);
    a.avgTheoreticalFractionalQuantity = sumFrac / n;
    a.avgIntegerQuantity               = sumInt / n;
    a.fractionalDeploymentPct          = sumFracDep / n;
    a.integerDeploymentPct             = sumIntDep / n;
    a.lostDeploymentPctDueToIntegerConstraint =
        a.fractionalDeploymentPct - a.integerDeploymentPct;
    return a;
}

} // namespace trading
