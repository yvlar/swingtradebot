#pragma once
#include "brokers/PaperBroker.hpp"   // TradeRecord
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

namespace trading {

// ─── Monte-Carlo / bootstrap des trades (Sprint 7.3, D24) ────────────────────
// Un backtest ne produit qu'UN chemin d'équité : l'ordre historique des
// trades. Le bootstrap rééchantillonne ces trades AVEC REMISE pour générer
// des milliers de chemins alternatifs et estimer la DISTRIBUTION du retour,
// du CAGR et du max drawdown (p5/p50/p95) — pas un point unique. La graine
// est fixée : un même rapport est reproductible au bit près.
//
// Hypothèse (classique, à garder en tête) : les trades sont indépendants —
// le bootstrap casse l'autocorrélation des régimes de marché. Les P&L
// rééchantillonnés sont les pnlPct NETS de coûts du backtest (6.2).

struct MonteCarloPercentiles {
    double p5  = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
};

struct MonteCarloReport {
    int      paths = 0;        // chemins simulés (0 = pas de trades en entrée)
    unsigned seed  = 0;
    MonteCarloPercentiles totalReturnPct;
    MonteCarloPercentiles cagrPct;
    MonteCarloPercentiles maxDrawdownPct;
};

class MonteCarloBootstrap {
public:
    // years : durée calendaire du backtest d'origine (pour annualiser le
    // CAGR) ; numPaths/seed : taille et graine de la simulation
    MonteCarloBootstrap(
        std::vector<TradeRecord> trades,
        double                   initialCapital,
        double                   years,
        int                      numPaths = 1'000,
        unsigned                 seed     = 42
    )
        : trades_(std::move(trades))
        , initialCapital_(initialCapital)
        , years_(years)
        , numPaths_(numPaths)
        , seed_(seed)
    {}

    // ── Percentile « rang le plus proche » (pur, déterministe) ───────────────
    static double percentile(std::vector<double> values, double p) {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        const double rank = p / 100.0 * static_cast<double>(values.size());
        size_t idx = rank <= 1.0 ? 0
                                 : static_cast<size_t>(std::ceil(rank)) - 1;
        idx = std::min(idx, values.size() - 1);
        return values[idx];
    }

    // ── Simulation ────────────────────────────────────────────────────────────
    MonteCarloReport run() const {
        MonteCarloReport rep;
        rep.seed = seed_;
        if (trades_.empty() || numPaths_ <= 0 || initialCapital_ <= 0)
            return rep;

        // mt19937 est normalisé par le standard ; les DISTRIBUTIONS de la
        // bibliothèque ne le sont pas (résultats différents selon la stdlib).
        // L'indice est donc tiré par modulo : biais négligeable pour nos
        // tailles (n ≪ 2^32) et goldens portables entre environnements.
        std::mt19937 rng(seed_);
        const size_t n = trades_.size();

        std::vector<double> rets, cagrs, drawdowns;
        rets.reserve(numPaths_);
        cagrs.reserve(numPaths_);
        drawdowns.reserve(numPaths_);

        for (int p = 0; p < numPaths_; ++p) {
            double equity = initialCapital_;
            double peak   = initialCapital_;
            double maxDd  = 0.0;

            // Un chemin = n trades tirés avec remise, composés dans l'ordre
            for (size_t k = 0; k < n; ++k) {
                const TradeRecord& t = trades_[rng() % n];
                equity *= (1.0 + t.pnlPct / 100.0);
                peak = std::max(peak, equity);
                if (peak > 0)
                    maxDd = std::max(maxDd, (peak - equity) / peak * 100.0);
            }

            const double ret = (equity - initialCapital_) / initialCapital_
                               * 100.0;
            rets.push_back(ret);
            drawdowns.push_back(maxDd);
            cagrs.push_back(years_ > 0 && equity > 0
                ? (std::pow(equity / initialCapital_, 1.0 / years_) - 1.0)
                  * 100.0
                : 0.0);
        }

        rep.paths = numPaths_;
        rep.totalReturnPct = {percentile(rets, 5),      percentile(rets, 50),
                              percentile(rets, 95)};
        rep.cagrPct        = {percentile(cagrs, 5),     percentile(cagrs, 50),
                              percentile(cagrs, 95)};
        rep.maxDrawdownPct = {percentile(drawdowns, 5), percentile(drawdowns, 50),
                              percentile(drawdowns, 95)};
        return rep;
    }

    // ── Rapport console ───────────────────────────────────────────────────────
    void printReport(const MonteCarloReport& rep) const {
        auto ligne = [](const std::string& label,
                        const MonteCarloPercentiles& p) {
            std::cout << "  " << std::left << std::setw(20) << label
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(10) << p.p5 << std::setw(10) << p.p50
                      << std::setw(10) << p.p95 << "\n";
        };
        std::cout << "\n" << std::string(60, '=') << "\n"
                  << "  MONTE-CARLO — " << rep.paths << " chemins, "
                  << trades_.size() << " trades reechantillonnes (graine "
                  << rep.seed << ")\n" << std::string(60, '=') << "\n"
                  << "  " << std::left << std::setw(20) << ""
                  << std::right << std::setw(10) << "p5"
                  << std::setw(10) << "p50" << std::setw(10) << "p95\n"
                  << std::string(60, '-') << "\n";
        ligne("Retour total (%)", rep.totalReturnPct);
        ligne("CAGR (%)",         rep.cagrPct);
        ligne("Max drawdown (%)", rep.maxDrawdownPct);
        std::cout << std::string(60, '=') << "\n\n";
    }

private:
    std::vector<TradeRecord> trades_;
    double   initialCapital_;
    double   years_;
    int      numPaths_;
    unsigned seed_;
};

} // namespace trading
