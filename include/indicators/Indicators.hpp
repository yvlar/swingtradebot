#pragma once
#include "core/Interfaces.hpp"
#include <vector>
#include <string>
#include <stdexcept>

namespace trading {

// ─── EMA — Exponential Moving Average ─────────────────────────────────────────
    class EMA final : public IIndicator<double> {
    public:
        explicit EMA(int period) : period_(period) {
            if (period <= 0)
                throw std::invalid_argument("EMA period must be > 0");
        }

        std::vector<double> compute(const std::vector<double>& prices) const override {
            if (prices.size() < static_cast<size_t>(period_))
                return {};

            std::vector<double> result(prices.size(), 0.0);
            const double k = 2.0 / (period_ + 1.0);

            // Seed: SMA des 'period' premières valeurs
            double sma = 0.0;
            for (int i = 0; i < period_; ++i)
                sma += prices[i];
            sma /= period_;

            result[period_ - 1] = sma;

            for (size_t i = period_; i < prices.size(); ++i)
                result[i] = prices[i] * k + result[i - 1] * (1.0 - k);

            return result;
        }

        std::string name() const override {
            return "EMA(" + std::to_string(period_) + ")";
        }

        int period() const { return period_; }

    private:
        int period_;
    };

// ─── RSI — Relative Strength Index ────────────────────────────────────────────
    class RSI final : public IIndicator<double> {
    public:
        explicit RSI(int period = 14) : period_(period) {
            if (period <= 1)
                throw std::invalid_argument("RSI period must be > 1");
        }

        std::vector<double> compute(const std::vector<double>& prices) const override {
            if (prices.size() < static_cast<size_t>(period_ + 1))
                return {};

            std::vector<double> result(prices.size(), 0.0);

            // Calcul des variations journalières
            std::vector<double> gains, losses;
            for (size_t i = 1; i < prices.size(); ++i) {
                double delta = prices[i] - prices[i - 1];
                gains.push_back(delta > 0 ? delta : 0.0);
                losses.push_back(delta < 0 ? -delta : 0.0);
            }

            // Moyenne initiale (SMA)
            double avgGain = 0.0, avgLoss = 0.0;
            for (int i = 0; i < period_; ++i) {
                avgGain += gains[i];
                avgLoss += losses[i];
            }
            avgGain /= period_;
            avgLoss /= period_;

            auto toRSI = [](double ag, double al) -> double {
                if (ag < 1e-10 && al < 1e-10) return 50.0; // marché plat → RSI neutre
                if (al < 1e-10) return 100.0;               // que des hausses
                double rs = ag / al;
                return 100.0 - (100.0 / (1.0 + rs));
            };

            result[period_] = toRSI(avgGain, avgLoss);

            // Smoothed moving average de Wilder
            for (size_t i = period_ + 1; i < prices.size(); ++i) {
                avgGain = (avgGain * (period_ - 1) + gains[i - 1]) / period_;
                avgLoss = (avgLoss * (period_ - 1) + losses[i - 1]) / period_;
                result[i] = toRSI(avgGain, avgLoss);
            }

            return result;
        }

        std::string name() const override {
            return "RSI(" + std::to_string(period_) + ")";
        }

        int period() const { return period_; }

    private:
        int period_;
    };

// ─── CrossoverDetector ─────────────────────────────────────────────────────────
// Utilitaire: détecte les croisements entre deux séries
    class CrossoverDetector {
    public:
        enum class Cross { NONE, BULLISH, BEARISH };

        // Retourne le type de croisement au dernier index
        // warmup : nombre de barres à ignorer depuis le début (pour éviter les faux signaux EMA)
        static Cross detect(
                const std::vector<double>& fast,
                const std::vector<double>& slow,
                size_t warmup = 0
        ) {
            if (fast.size() < 2 || slow.size() < 2)
                return Cross::NONE;

            size_t n = fast.size();

            // Ignorer les croisements dans la période de warmup
            if (n - 1 <= warmup)
                return Cross::NONE;

            bool prevFastAbove = fast[n-2] > slow[n-2];
            bool currFastAbove = fast[n-1] > slow[n-1];

            if (!prevFastAbove && currFastAbove) return Cross::BULLISH;
            if ( prevFastAbove && !currFastAbove) return Cross::BEARISH;
            return Cross::NONE;
        }
    };

} // namespace trading