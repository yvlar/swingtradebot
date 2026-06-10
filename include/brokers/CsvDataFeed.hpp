#pragma once
#include "core/Interfaces.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace trading {

// ─── CsvDataFeed ──────────────────────────────────────────────────────────────
// Implémente IDataFeed depuis un fichier CSV Yahoo Finance.
//
// FORMAT ATTENDU (Yahoo Finance — télécharge sur finance.yahoo.com):
//   Date,Open,High,Low,Close,Adj Close,Volume
//   2024-01-02,403.25,404.10,401.50,402.80,402.80,45123400
//
// USAGE:
//   auto feed = CsvDataFeed("QQQ.csv");
//   auto bars = feed.getBars("QQQ", 60);

class CsvDataFeed final : public IDataFeed {
public:
    explicit CsvDataFeed(const std::string& csvPath) : csvPath_(csvPath) {
        loadAll();
    }

    // Retourne les N dernières barres (ou moins si pas assez de données)
    // Données locales : jamais de panne → toujours Ok (item 10)
    Result<std::vector<Bar>> getBars(const std::string& /*symbol*/, int days) override {
        if (bars_.empty()) return Result<std::vector<Bar>>::Ok({});
        int start = std::max(0, static_cast<int>(bars_.size()) - days);
        return Result<std::vector<Bar>>::Ok(
            std::vector<Bar>(bars_.begin() + start, bars_.end()));
    }

    // En backtesting, le marché est toujours "ouvert"
    bool isMarketOpen() override { return true; }

    // ── Accès avancé pour le Backtester ──────────────────────────────────────

    // Retourne toutes les barres chargées
    const std::vector<Bar>& allBars() const { return bars_; }

    // Nombre de barres disponibles
    size_t size() const { return bars_.size(); }

    // Retourne les barres jusqu'à l'index i (fenêtre glissante pour le backtest)
    std::vector<Bar> getBarsUpTo(size_t index, int lookback) const {
        if (index >= bars_.size()) return {};
        int end   = static_cast<int>(index) + 1;
        int start = std::max(0, end - lookback);
        return std::vector<Bar>(bars_.begin() + start, bars_.begin() + end);
    }

    // Prix de clôture à un index donné
    double priceAt(size_t index) const {
        if (index >= bars_.size()) return 0.0;
        return bars_[index].close;
    }

private:
    std::string      csvPath_;
    std::vector<Bar> bars_;

    // ── Parsing CSV ───────────────────────────────────────────────────────────
    void loadAll() {
        std::ifstream file(csvPath_);
        if (!file.is_open())
            throw std::runtime_error("CsvDataFeed: impossible d'ouvrir " + csvPath_);

        std::string line;
        bool firstLine = true;

        while (std::getline(file, line)) {
            // Ignore la ligne d'en-tête
            if (firstLine) { firstLine = false; continue; }
            if (line.empty() || line[0] == '#') continue;

            auto bar = parseLine(line);
            if (bar.has_value())
                bars_.push_back(bar.value());
        }

        // Trie par date croissante (au cas où le CSV ne serait pas trié)
        std::sort(bars_.begin(), bars_.end(), [](const Bar& a, const Bar& b) {
            return a.date < b.date;
        });

        if (bars_.empty())
            throw std::runtime_error("CsvDataFeed: aucune donnée dans " + csvPath_);
    }

    // Format Yahoo Finance: Date,Open,High,Low,Close,Adj Close,Volume
    std::optional<Bar> parseLine(const std::string& line) {
        std::vector<std::string> tokens;
        std::stringstream ss(line);
        std::string token;

        while (std::getline(ss, token, ','))
            tokens.push_back(trim(token));

        // Yahoo Finance a 7 colonnes
        if (tokens.size() < 6) return std::nullopt;

        // Ignore les lignes avec valeurs manquantes ("null")
        for (size_t i = 1; i < std::min(tokens.size(), size_t(6)); ++i)
            if (tokens[i] == "null" || tokens[i].empty()) return std::nullopt;

        try {
            Bar b;
            b.date   = tokens[0];
            b.open   = std::stod(tokens[1]);
            b.high   = std::stod(tokens[2]);
            b.low    = std::stod(tokens[3]);
            b.close  = std::stod(tokens[4]);  // utilise Close (pas Adj Close)
            b.volume = tokens.size() > 6 ? std::stol(tokens[6]) : 0L;
            return b;
        } catch (...) {
            return std::nullopt;  // ligne malformée — on ignore
        }
    }

    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end   = s.find_last_not_of(" \t\r\n");
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }
};

} // namespace trading