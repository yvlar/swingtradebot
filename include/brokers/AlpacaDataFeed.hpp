#pragma once
// ============================================================
//  AlpacaDataFeed.hpp  —  Données temps réel via Alpaca Markets
//
//  Implémente IDataFeed avec l'API Alpaca v2.
//  Compatible paper trading ET live trading (même code, URL différente).
//
//  Dépendances : libcurl (déjà dans le projet)
//
//  Docs API : https://docs.alpaca.markets/reference/stockbars
// ============================================================
#pragma once
#include "core/Interfaces.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <chrono>

using json = nlohmann::json;

namespace trading {

class AlpacaDataFeed final : public IDataFeed {
public:
    // ── Construction ─────────────────────────────────────────
    // paper=true  → paper-api.alpaca.markets  (argent fictif)
    // paper=false → api.alpaca.markets        (argent réel — attention !)
    AlpacaDataFeed(std::string apiKey,
                   std::string apiSecret,
                   bool        paper = true)
        : apiKey_   (std::move(apiKey))
        , apiSecret_(std::move(apiSecret))
        , baseDataUrl_("https://data.alpaca.markets")
        , baseTradingUrl_(paper
            ? "https://paper-api.alpaca.markets"
            : "https://api.alpaca.markets")
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~AlpacaDataFeed() { curl_global_cleanup(); }

    // ── IDataFeed ─────────────────────────────────────────────

    // Récupère les N dernières barres journalières
    std::vector<Bar> getBars(const std::string& symbol, int days) override {
        // Calcule la date de début (aujourd'hui - N jours ouvrables ≈ days × 1.5)
        auto start = isoDate(daysAgo(static_cast<int>(days * 1.5)));
        auto end   = isoDate(daysAgo(1)); // hier (barre du jour non fermée)

        std::string url = baseDataUrl_
            + "/v2/stocks/" + symbol + "/bars"
            + "?timeframe=1Day"
            + "&start=" + start
            + "&end="   + end
            + "&limit="  + std::to_string(days)
            + "&feed=iex"
            + "&sort=asc";

        auto resp = get(url);
        auto j    = json::parse(resp);

        std::vector<Bar> bars;
        for (const auto& b : j.value("bars", json::array())) {
            Bar bar;
            bar.date   = b.value("t", "").substr(0, 10); // "2024-01-02T..."→"2024-01-02"
            bar.open   = b.value("o", 0.0);
            bar.high   = b.value("h", 0.0);
            bar.low    = b.value("l", 0.0);
            bar.close  = b.value("c", 0.0);
            bar.volume = static_cast<long>(b.value("v", 0.0));
            if (bar.close > 0) bars.push_back(bar);
        }
        return bars;
    }

    // Prix en temps quasi-réel (dernière transaction)
    std::optional<double> getLatestPrice(const std::string& symbol) override {
        std::string url = baseDataUrl_
            + "/v2/stocks/" + symbol + "/trades/latest"
            + "?feed=iex";
        try {
            auto resp = get(url);
            auto j    = json::parse(resp);
            double p  = j["trade"].value("p", 0.0);
            return p > 0 ? std::optional<double>(p) : std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
    }

    // Vérifie si le marché américain est ouvert via l'API Alpaca
    bool isMarketOpen() override {
        try {
            auto resp = get(baseTradingUrl_ + "/v2/clock");
            auto j    = json::parse(resp);
            return j.value("is_open", false);
        } catch (...) {
            return false;
        }
    }

private:
    std::string apiKey_;
    std::string apiSecret_;
    std::string baseDataUrl_;
    std::string baseTradingUrl_;

    // ── HTTP GET avec libcurl ─────────────────────────────────
    std::string get(const std::string& url) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl_easy_init failed");

        std::string response;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("APCA-API-KEY-ID: "    + apiKey_).c_str());
        headers = curl_slist_append(headers, ("APCA-API-SECRET-KEY: " + apiSecret_).c_str());
        headers = curl_slist_append(headers, "Accept: application/json");

        curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,        10L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
            throw std::runtime_error(std::string("HTTP GET failed: ")
                                     + curl_easy_strerror(res));

        // Vérifie les erreurs API Alpaca
        auto j = json::parse(response, nullptr, false);
        if (!j.is_discarded() && j.contains("code"))
            throw std::runtime_error("Alpaca API error: " + response);

        return response;
    }

    static size_t writeCallback(void* data, size_t size, size_t nmemb, std::string* out) {
        out->append(static_cast<char*>(data), size * nmemb);
        return size * nmemb;
    }

    // ── Utilitaires de date ───────────────────────────────────
    static std::time_t daysAgo(int n) {
        auto now = std::chrono::system_clock::now();
        auto tp  = now - std::chrono::hours(24 * n);
        return std::chrono::system_clock::to_time_t(tp);
    }

    static std::string isoDate(std::time_t t) {
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char buf[11];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        return std::string(buf);
    }
};

} // namespace trading
