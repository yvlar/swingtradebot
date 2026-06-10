#pragma once
#include "core/Interfaces.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <stdexcept>

namespace trading {

// ─── ATR — Average True Range (approximation close-to-close) ─────────────────
// L'interface IIndicator<double> ne fournit que la série des clôtures : le
// true range dégénère en |close_i − close_{i−1}| (pas de high/low disponibles).
// Lissage de Wilder, seed = SMA des `period` premiers TR — même convention de
// warmup que RSI (zéros avant l'indice `period`, vide si série trop courte).
// Suffisant pour un stop dynamique proportionnel à la volatilité.
    class ATR final : public IIndicator<double> {
    public:
        explicit ATR(int period = 14) : period_(period) {
            if (period <= 0)
                throw std::invalid_argument("ATR period must be > 0");
        }

        std::vector<double> compute(const std::vector<double>& prices) const override {
            if (prices.size() < static_cast<size_t>(period_ + 1))
                return {};

            std::vector<double> result(prices.size(), 0.0);

            // True range dégradé : variation absolue clôture à clôture
            std::vector<double> tr;
            tr.reserve(prices.size() - 1);
            for (size_t i = 1; i < prices.size(); ++i)
                tr.push_back(std::abs(prices[i] - prices[i - 1]));

            // Seed : SMA des `period` premiers TR
            double atr = 0.0;
            for (int i = 0; i < period_; ++i)
                atr += tr[i];
            atr /= period_;
            result[period_] = atr;

            // Lissage de Wilder
            for (size_t i = period_ + 1; i < prices.size(); ++i) {
                atr = (atr * (period_ - 1) + tr[i - 1]) / period_;
                result[i] = atr;
            }

            return result;
        }

        std::string name() const override {
            return "ATR(" + std::to_string(period_) + ")";
        }

        int period() const { return period_; }

    private:
        int period_;
    };

// ─── VWAP — Volume Weighted Average Price ────────────────────────────────────
// computeWithVolume : VWAP cumulatif par session — réinitialisé quand la date
// calendaire (AAAA-MM-JJ) change — sur le prix typique (H+L+C)/3 pondéré par
// le volume. compute (contrat IIndicator) : sans volume disponible, dégénère
// en moyenne cumulative des prix (pondération uniforme).
    class VWAP final : public IIndicator<double> {
    public:
        std::vector<double> compute(const std::vector<double>& prices) const override {
            std::vector<double> result(prices.size(), 0.0);
            double sum = 0.0;
            for (size_t i = 0; i < prices.size(); ++i) {
                sum += prices[i];
                result[i] = sum / static_cast<double>(i + 1);
            }
            return result;
        }

        std::vector<double> computeWithVolume(const std::vector<Bar>& bars) const {
            std::vector<double> result(bars.size(), 0.0);
            double cumPV  = 0.0;
            double cumVol = 0.0;
            std::string session;

            for (size_t i = 0; i < bars.size(); ++i) {
                // Nouvelle session quand la date calendaire change
                std::string day = bars[i].date.substr(0, 10);
                if (day != session) {
                    session = day;
                    cumPV   = 0.0;
                    cumVol  = 0.0;
                }

                double typical = (bars[i].high + bars[i].low + bars[i].close) / 3.0;
                double volume  = static_cast<double>(bars[i].volume);
                cumPV  += typical * volume;
                cumVol += volume;

                // Volume absent (donnée manquante) : repli sur le prix typique
                result[i] = cumVol > 0 ? cumPV / cumVol : typical;
            }
            return result;
        }

        std::string name() const override { return "VWAP"; }
    };

// ─── VolumeOscillator — ratio volume courant / moyenne mobile ────────────────
// result[i] = volume[i] / SMA des `period` volumes PRÉCÉDENTS (barre courante
// exclue, pour ne pas diluer le pic qu'on cherche à détecter). 1.0 (neutre)
// pendant le warmup ou si la moyenne est nulle. Sert de filtre de
// confirmation : volume ≥ k × moyenne.
    class VolumeOscillator final : public IIndicator<double> {
    public:
        explicit VolumeOscillator(int period = 20) : period_(period) {
            if (period <= 0)
                throw std::invalid_argument("VolumeOscillator period must be > 0");
        }

        std::vector<double> compute(const std::vector<double>& volumes) const override {
            std::vector<double> result(volumes.size(), 1.0);
            if (volumes.size() <= static_cast<size_t>(period_))
                return result;

            // Somme glissante des `period` volumes précédant l'indice courant
            double window = 0.0;
            for (int i = 0; i < period_; ++i)
                window += volumes[i];

            for (size_t i = period_; i < volumes.size(); ++i) {
                double avg = window / period_;
                result[i]  = avg > 0 ? volumes[i] / avg : 1.0;
                window    += volumes[i] - volumes[i - period_];
            }
            return result;
        }

        std::string name() const override {
            return "VolumeOscillator(" + std::to_string(period_) + ")";
        }

        int period() const { return period_; }

    private:
        int period_;
    };

} // namespace trading
