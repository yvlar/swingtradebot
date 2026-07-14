#pragma once
#include <algorithm>

namespace trading {

// ─── ExecutionCostConfig — modèle de coûts configurable (Sprint 23, item 23.3) ─
// Abstraction des frais d'exécution pour le backtest OFFLINE. Le modèle
// historique (commission en % + slippage/demi-spread en bps, Sprint 6.2/D22)
// reste le comportement par défaut du PaperBroker et du Backtester : cette
// structure est un AJOUT opt-in, jamais un remplacement.
//
// AVERTISSEMENT : les scénarios ci-dessous sont des HYPOTHÈSES de simulation.
// Ils ne prétendent PAS reproduire les tarifs actuels d'un courtier précis —
// le scénario C en particulier est un stress volontairement pessimiste pour un
// petit compte, pas une prédiction.
//
// Composition de la commission d'un ordre (achat OU vente — les deux côtés
// paient) :
//   commission = clamp(commissionPct × valeur + commissionPerShare × qty,
//                      minimumCommissionPerOrder, maximumCommissionPerOrder)
//                + regulatoryFeeFixed + regulatoryFeePct × valeur
// avec valeur = prix d'exécution × quantité. Un seuil ≤ 0 désactive le
// minimum/maximum correspondant (même convention que le kill-switch).
// Le slippage + demi-spread dégradent le PRIX de fill (achat au-dessus,
// vente en dessous), exactement comme le modèle historique.
struct ExecutionCostConfig {
    double commissionPct            = 0.0;  // fraction de la valeur (0.001 = 0,1 %)
    double commissionPerShare       = 0.0;  // $ par action
    double minimumCommissionPerOrder = 0.0; // $ ; ≤ 0 = pas de minimum
    double maximumCommissionPerOrder = 0.0; // $ ; ≤ 0 = pas de plafond
    double slippageBps              = 0.0;  // points de base, par côté
    double halfSpreadBps            = 0.0;  // points de base, par côté
    double regulatoryFeeFixed       = 0.0;  // $ par ordre (hors clamp min/max)
    double regulatoryFeePct         = 0.0;  // fraction de la valeur (hors clamp)

    // Pénalité de fill (slippage + demi-spread) en fraction du prix — même
    // définition que fillPenaltyPct_ du PaperBroker historique.
    double fillPenaltyFraction() const {
        return (slippageBps + halfSpreadBps) / 10'000.0;
    }

    // Commission + frais réglementaires d'UN ordre. `qty` est un double pour
    // servir aussi le contre-factuel fractionnaire STRICTEMENT offline (23.5)
    // — les brokers réels, eux, restent en quantités entières.
    double commissionForOrder(double prix, double qty) const {
        if (prix <= 0.0 || qty <= 0.0) return 0.0;
        const double valeur = prix * qty;
        double comm = commissionPct * valeur + commissionPerShare * qty;
        if (minimumCommissionPerOrder > 0.0)
            comm = std::max(comm, minimumCommissionPerOrder);
        if (maximumCommissionPerOrder > 0.0)
            comm = std::min(comm, maximumCommissionPerOrder);
        return comm + regulatoryFeeFixed + regulatoryFeePct * valeur;
    }

    // ── Scénarios centralisés (item 23.3) ────────────────────────────────────
    // Scénario A — HISTORIQUE CONSERVATEUR : exactement les valeurs du modèle
    // historique (Sprint 6.2/D22) : 0,1 % par côté + 2 bps de slippage +
    // 0,5 bp de demi-spread. Passé au chemin « modèle de coûts », il doit
    // reproduire les goldens du chemin historique (verrouillé par test).
    static ExecutionCostConfig historiqueConservateur() {
        ExecutionCostConfig c;
        c.commissionPct = 0.001;
        c.slippageBps   = 2.0;
        c.halfSpreadBps = 0.5;
        return c;
    }

    // Scénario B — FAIBLE COMMISSION : commission par action SANS minimum
    // fixe. Sert à mesurer la SENSIBILITÉ du petit compte au niveau de
    // commission, pas à modéliser un courtier réel.
    static ExecutionCostConfig faibleCommission() {
        ExecutionCostConfig c;
        c.commissionPerShare = 0.005;   // 0,5 cent / action, sans minimum
        c.slippageBps        = 2.0;
        c.halfSpreadBps      = 0.5;
        return c;
    }

    // Scénario C — STRESS PETIT COMPTE : minimum PAR ORDRE qui peut dominer
    // une petite transaction (1 action × 200 $ → 1 $ de minimum = 0,5 % du
    // notionnel PAR CÔTÉ), slippage doublé. HYPOTHÈSE DE STRESS assumée,
    // PAS un tarif réel.
    static ExecutionCostConfig stressPetitCompte() {
        ExecutionCostConfig c;
        c.commissionPerShare        = 0.005;
        c.minimumCommissionPerOrder = 1.0;
        c.slippageBps               = 4.0;
        c.halfSpreadBps             = 1.0;
        return c;
    }
};

} // namespace trading
