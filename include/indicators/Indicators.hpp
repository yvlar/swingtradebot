#pragma once
#include "core/Interfaces.hpp"
#include <vector>
#include <string>
#include <stdexcept>
#include <cmath>
#include <algorithm>

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

// ─── SMA — Simple Moving Average ──────────────────────────────────────────────
// Moyenne mobile simple sur `period` clôtures. Sert de filtre de régime de fond
// (ex. SMA200) : on ne reste long que tant que le prix est au-dessus (item 8.1).
// Même convention de warmup que l'EMA : zéros avant l'indice `period-1`, vide si
// la série est plus courte que `period`.
    class SMA final : public IIndicator<double> {
    public:
        explicit SMA(int period) : period_(period) {
            if (period <= 0)
                throw std::invalid_argument("SMA period must be > 0");
        }

        std::vector<double> compute(const std::vector<double>& prices) const override {
            if (prices.size() < static_cast<size_t>(period_))
                return {};

            std::vector<double> result(prices.size(), 0.0);

            // Somme glissante : seed = SMA des `period` premières valeurs
            double window = 0.0;
            for (int i = 0; i < period_; ++i)
                window += prices[i];
            result[period_ - 1] = window / period_;

            for (size_t i = period_; i < prices.size(); ++i) {
                window += prices[i] - prices[i - period_];
                result[i] = window / period_;
            }

            return result;
        }

        std::string name() const override {
            return "SMA(" + std::to_string(period_) + ")";
        }

        int period() const { return period_; }

    private:
        int period_;
    };

// ─── RollingStdDev — écart-type glissant (bande de Bollinger / z-score) ────────
// Écart-type de POPULATION sur une fenêtre glissante de `period` clôtures. Sert
// de brique au z-score de la variante mean-reversion (Sprint 11) :
//   z = (clôture − SMA(period)) / RollingStdDev(period).
// Calqué sur SMA (même convention de sortie : vecteur aligné sur `prices`,
// warmup = 0.0 jusqu'à `period-1`, vide si la série est plus courte que
// `period`). Somme glissante des valeurs ET des carrés → O(n).
    class RollingStdDev final : public IIndicator<double> {
    public:
        explicit RollingStdDev(int period) : period_(period) {
            if (period <= 0)
                throw std::invalid_argument("RollingStdDev period must be > 0");
        }

        std::vector<double> compute(const std::vector<double>& prices) const override {
            if (prices.size() < static_cast<size_t>(period_))
                return {};

            std::vector<double> result(prices.size(), 0.0);

            // Seed : somme et somme des carrés des `period` premières valeurs
            double sum = 0.0, sumSq = 0.0;
            for (int i = 0; i < period_; ++i) {
                sum   += prices[i];
                sumSq += prices[i] * prices[i];
            }
            result[period_ - 1] = stddev_(sum, sumSq);

            for (size_t i = period_; i < prices.size(); ++i) {
                const double in  = prices[i];
                const double out = prices[i - period_];
                sum   += in - out;
                sumSq += in * in - out * out;
                result[i] = stddev_(sum, sumSq);
            }

            return result;
        }

        std::string name() const override {
            return "StdDev(" + std::to_string(period_) + ")";
        }

        int period() const { return period_; }

    private:
        // σ = √(E[x²] − E[x]²) sur le fenêtrage courant. Variance bornée à 0 :
        // les arrondis de la somme glissante peuvent la rendre légèrement
        // négative sur une série quasi plate.
        double stddev_(double sum, double sumSq) const {
            const double mean = sum / period_;
            const double var  = std::max(0.0, sumSq / period_ - mean * mean);
            return std::sqrt(var);
        }

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