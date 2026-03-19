#pragma once
// ============================================================
//  IBKRDataFeed.hpp  —  Données temps réel via IBKR CP Gateway
//
//  PRÉREQUIS : CP Gateway doit tourner localement sur port 5000
//  Télécharge : https://www.interactivebrokers.com/en/trading/ibgateway-latest.php
//
//  Architecture :
//    Bot C++ → http://localhost:5000/v1/api/... → IBKR servers
//
//  Dépendances : libcurl + nlohmann/json (déjà dans le projet)
// ============================================================
#include "core/Interfaces.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>
#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>
#include <map>

using json = nlohmann::json;

namespace trading {

// ── Conids IBKR connus ─────────────────────────────────────────────────────────
// Chaque titre a un identifiant numérique unique chez IBKR (conid)
// Pour trouver d'autres conids : GET /v1/api/iserver/secdef/search?symbol=SPY
static const std::map<std::string, std::string> KNOWN_CONIDS = {
    {"QQQ",  "320227571"},
    {"SPY",  "756733"},
    {"NVDA", "4815747"},
    {"AAPL", "265598"},
    {"MSFT", "272093"},
    {"TSLA", "76792991"},
    {"AMZN", "3691937"},
};

class IBKRDataFeed final : public IDataFeed {
public:
    // gatewayUrl : adresse du CP Gateway (défaut localhost:5000)
    // paper=true : utilise le compte paper trading d'IBKR
    explicit IBKRDataFeed(std::string gatewayUrl = "https://localhost:5000",
                          bool verifySsl = false)
        : gatewayUrl_(std::move(gatewayUrl))
        , verifySsl_ (verifySsl)  // false requis avec le cert auto-signé du gateway
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~IBKRDataFeed() { curl_global_cleanup(); }

    // ── IDataFeed ─────────────────────────────────────────────

    // Récupère les N dernières barres journalières
    // Utilise l'endpoint HMDS (Historical Market Data Service)
    std::vector<Bar> getBars(const std::string& symbol, int days) override {
        std::string conid = resolveConid(symbol);

        // period ex: "60d" pour 60 jours
        std::string period = std::to_string(days) + "d";

        std::string url = gatewayUrl_
            + "/v1/api/hmds/history"
            + "?conid=" + conid
            + "&period=" + period
            + "&bar=1d"         // barres journalières
            + "&outsideRth=false"; // heures régulières seulement

        auto resp = get(url);
        auto j    = json::parse(resp);

        std::vector<Bar> bars;
        if (!j.contains("data")) return bars;

        for (const auto& d : j["data"]) {
            Bar bar;
            // timestamp IBKR en ms → date ISO
            long long ms = d.value("t", 0LL);
            bar.date   = msToDate(ms);
            bar.open   = d.value("o", 0.0);
            bar.high   = d.value("h", 0.0);
            bar.low    = d.value("l", 0.0);
            bar.close  = d.value("c", 0.0);
            bar.volume = static_cast<long>(d.value("v", 0.0));
            if (bar.close > 0) bars.push_back(bar);
        }
        return bars;
    }

    // Prix en temps réel via market data snapshot
    std::optional<double> getLatestPrice(const std::string& symbol) override {
        std::string conid = resolveConid(symbol);

        // Souscrit d'abord aux données (IBKR requiert une souscription)
        subscribeMarketData(conid);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // field 31 = last price
        std::string url = gatewayUrl_
            + "/v1/api/iserver/marketdata/snapshot"
            + "?conids=" + conid
            + "&fields=31,84,86"; // 31=last, 84=bid, 86=ask

        try {
            auto resp = get(url);
            auto j    = json::parse(resp);
            if (j.is_array() && !j.empty()) {
                double price = j[0].value("31", 0.0);
                if (price > 0) return price;
            }
        } catch (...) {}
        return std::nullopt;
    }

    // Vérifie si le marché US est ouvert via l'horloge IBKR
    bool isMarketOpen() override {
        try {
            auto resp = get(gatewayUrl_ + "/v1/api/iserver/marketdata/availability");
            auto j    = json::parse(resp);
            // Cherche si NASDAQ/NYSE est dans les marchés ouverts
            if (j.is_array()) {
                for (const auto& m : j) {
                    std::string desc = m.value("description", "");
                    if (desc.find("NASDAQ") != std::string::npos ||
                        desc.find("NYSE")   != std::string::npos) {
                        return m.value("isOpen", false);
                    }
                }
            }
        } catch (...) {}

        // Fallback : vérifie l'heure EST (9h30-16h00, lun-ven)
        return isUsMarketHours();
    }

    // ── Utilitaires publics ───────────────────────────────────

    // Trouve le conid d'un symbole (pour les symboles non listés dans KNOWN_CONIDS)
    std::string searchConid(const std::string& symbol) {
        std::string url = gatewayUrl_
            + "/v1/api/iserver/secdef/search?symbol=" + symbol;
        auto resp = get(url);
        auto j    = json::parse(resp);
        if (j.is_array() && !j.empty())
            return std::to_string(j[0].value("conid", 0));
        throw std::runtime_error("Symbole introuvable : " + symbol);
    }

    // Vérifie l'authentification avec le gateway
    bool isAuthenticated() {
        try {
            auto resp = get(gatewayUrl_ + "/v1/api/iserver/auth/status");
            auto j    = json::parse(resp);
            return j.value("authenticated", false);
        } catch (...) {
            return false;
        }
    }

private:
    std::string gatewayUrl_;
    bool        verifySsl_;

    // ── Résolution de conid ───────────────────────────────────
    std::string resolveConid(const std::string& symbol) {
        auto it = KNOWN_CONIDS.find(symbol);
        if (it != KNOWN_CONIDS.end()) return it->second;
        // Recherche dynamique pour les symboles inconnus
        return searchConid(symbol);
    }

    // ── Souscription données temps réel ──────────────────────
    // IBKR requiert une souscription avant de pouvoir lire les données
    void subscribeMarketData(const std::string& conid) {
        try {
            std::string url = gatewayUrl_
                + "/v1/api/iserver/marketdata/snapshot"
                + "?conids=" + conid;
            get(url); // premier appel = souscription
        } catch (...) {}
    }

    // ── Conversion timestamp → date ───────────────────────────
    static std::string msToDate(long long ms) {
        std::time_t t = ms / 1000;
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

    // ── Vérification horaire marché US (fallback) ────────────
    static bool isUsMarketHours() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        // EST = UTC-5 (ou UTC-4 en été)
        int hour_est = (tm.tm_hour - 5 + 24) % 24;
        int wday     = tm.tm_wday; // 0=dim, 6=sam
        if (wday == 0 || wday == 6) return false; // weekend
        return (hour_est >= 9 && (hour_est < 16 || (hour_est == 9 && tm.tm_min >= 30)));
    }

    // ── HTTP GET (avec SSL désactivé pour cert auto-signé) ────
    std::string get(const std::string& url) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl_easy_init failed");

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);
        // Le CP Gateway utilise un certificat auto-signé → on désactive la vérif
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verifySsl_ ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verifySsl_ ? 2L : 0L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
            throw std::runtime_error(std::string("HTTP GET: ")
                                     + curl_easy_strerror(res));
        return response;
    }

    static size_t writeCallback(void* data, size_t size,
                                size_t nmemb, std::string* out) {
        out->append(static_cast<char*>(data), size * nmemb);
        return size * nmemb;
    }
};

} // namespace trading
