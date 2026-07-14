#pragma once
#include "backtest/ExecutionCostModel.hpp"
#include "models/Models.hpp"
#include <cmath>
#include <vector>

namespace trading {

// ─── Benchmark buy-and-hold BRUT et NET (Sprint 23, item 23.4) ────────────────
// Le benchmark historique du Backtester (buyHoldReturnPct) est BRUT : variation
// pure du prix entre la fin du warmup et la dernière barre, sans frais, sans
// contrainte d'action entière, sans cash résiduel. Il RESTE la métrique
// historique — ce module ajoute la mesure NETTE, comparable à ce qu'un petit
// compte obtiendrait réellement :
//   • achat initial au close post-warmup, dégradé du slippage/demi-spread,
//     commission du modèle de coûts payée ;
//   • quantité ENTIÈRE lorsque le scénario est en actions entières (le cash
//     résiduel non investi reste dans la valeur finale) ;
//   • vente finale au dernier close, dégradée et commissionnée pareil ;
//   • même capital initial que la stratégie comparée.
struct NetBuyHoldResult {
    double grossReturnPct = 0.0;  // métrique historique (prix pur)
    double netReturnPct   = 0.0;  // net de coûts + contrainte entière + cash résiduel
    double sharesBought   = 0.0;  // quantité achetée (entière si wholeShares)
    double residualCash   = 0.0;  // cash non investi après l'achat initial
    double finalValue     = 0.0;  // cash résiduel + produit net de la vente finale
    double totalFees      = 0.0;  // commissions + frais réglementaires (2 côtés)
};

// Calcule le B&H brut et net sur bars[warmup..fin]. `wholeShares` = true →
// quantité entière (contrainte du compte réel sans fractions) ; false →
// quantité fractionnaire théorique (contre-factuel 23.5, STRICTEMENT offline).
// Renvoie un résultat nul si la fenêtre est trop courte ou le capital ≤ 0.
inline NetBuyHoldResult computeNetBuyHold(const std::vector<Bar>& bars,
                                          int warmup,
                                          double initialCapital,
                                          const ExecutionCostConfig& couts,
                                          bool wholeShares) {
    NetBuyHoldResult r;
    if (initialCapital <= 0.0 || warmup < 0
        || bars.size() <= static_cast<size_t>(warmup) + 1)
        return r;

    const double entryClose = bars[static_cast<size_t>(warmup)].close;
    const double exitClose  = bars.back().close;
    if (entryClose <= 0.0 || exitClose <= 0.0) return r;

    // Métrique historique (brute) : même formule que computeMetrics.
    r.grossReturnPct = (exitClose - entryClose) / entryClose * 100.0;

    const double pen      = couts.fillPenaltyFraction();
    const double buyPrice = entryClose * (1.0 + pen);   // achat dégradé

    // Quantité maximale finançable : prix × q + commission(q) ≤ capital.
    double qty;
    if (wholeShares) {
        // La commission n'est pas linéaire (minimum par ordre) : borne haute
        // par le cash puis décrémentation — même logique que le PaperBroker.
        long q = static_cast<long>(initialCapital / buyPrice);
        while (q > 0 && buyPrice * q + couts.commissionForOrder(buyPrice, q)
                            > initialCapital)
            --q;
        qty = static_cast<double>(q);
    } else {
        // Fractionnaire (théorique/offline) : point fixe q ← (capital −
        // commission(q)) / prix — converge en quelques itérations, la
        // commission étant petite et croissante en q.
        qty = initialCapital / buyPrice;
        for (int i = 0; i < 8; ++i) {
            const double next = (initialCapital
                                 - couts.commissionForOrder(buyPrice, qty))
                                / buyPrice;
            if (next <= 0.0) { qty = 0.0; break; }
            qty = next;
        }
    }
    if (qty <= 0.0) {
        // Même une action (ou fraction) est infinançable : tout reste en cash.
        r.residualCash = initialCapital;
        r.finalValue   = initialCapital;
        r.netReturnPct = 0.0;
        return r;
    }

    const double buyCommission = couts.commissionForOrder(buyPrice, qty);
    r.sharesBought = qty;
    r.residualCash = initialCapital - buyPrice * qty - buyCommission;

    const double sellPrice      = exitClose * (1.0 - pen);   // vente dégradée
    const double sellCommission = couts.commissionForOrder(sellPrice, qty);
    const double proceeds       = sellPrice * qty - sellCommission;

    r.totalFees    = buyCommission + sellCommission;
    r.finalValue   = r.residualCash + proceeds;
    r.netReturnPct = (r.finalValue - initialCapital) / initialCapital * 100.0;
    return r;
}

} // namespace trading
