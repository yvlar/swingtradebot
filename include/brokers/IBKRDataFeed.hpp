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
#include "core/HttpClient.hpp"
#include "core/market_calendar.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>
#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>
#include <map>

namespace trading {

// Alias local au namespace (pas de pollution du scope global — D9)
using json = nlohmann::json;

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

class IBKRDataFeed : public IDataFeed {   // non-final : les tests substituent request()
public:
    // gatewayUrl : adresse du CP Gateway (défaut localhost:5000)
    // paper=true : utilise le compte paper trading d'IBKR
    explicit IBKRDataFeed(std::string gatewayUrl = "https://localhost:5000",
                          bool verifySsl = false)
        : gatewayUrl_(std::move(gatewayUrl))
        , verifySsl_ (verifySsl)  // false requis avec le cert auto-signé du gateway
        , http_      (makeClientConfig(verifySsl))
    {
        // L'init globale de libcurl est faite une seule fois au main
        // via CurlGlobal (core/curl_global.h) — jamais ici (item 7)
    }

    // ── IDataFeed ─────────────────────────────────────────────

    // Récupère les N dernières barres journalières
    // Utilise l'endpoint HMDS (Historical Market Data Service)
    // Ok(vide) = le gateway a répondu sans donnée ; Err = panne (item 10)
    Result<std::vector<Bar>> getBars(const std::string& symbol, int days) override {
        using R = Result<std::vector<Bar>>;
        try {
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
            if (!j.contains("data")) return R::Ok(std::move(bars));

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
            // 9.3 (E6/D25) : pendant la séance, le HMDS inclut la barre du
            // jour EN FORMATION — son close bouge à chaque requête et le
            // croisement EMA peut osciller intra-journée. On ne livre que
            // des barres CLÔTURÉES (AlpacaDataFeed borne déjà à end=hier).
            // Comportement verrouillé par test (le feed n'a pas de logger).
            if (!bars.empty() && bars.back().date == usEasternDateOfUtc(now_()))
                bars.pop_back();
            return R::Ok(std::move(bars));
        } catch (const std::exception& e) {
            return R::Err(std::string("IBKR getBars: ") + e.what());
        }
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

    // Tentative UNIQUE de ré-authentification de la session brokerage
    // (POST /v1/api/iserver/reauthenticate, item 17.4) — ne peut réussir
    // que si la session SSO du Gateway est encore valide (micro-coupure) ;
    // sinon la ré-auth reste MANUELLE (navigateur, RUNBOOK). Ne lève
    // jamais (même contrat qu'isAuthenticated). Le retour n'est qu'un
    // indice (« triggered ») : la vérité se relit via isAuthenticated().
    bool tryReauthenticate() {
        try {
            auto resp = request("POST",
                gatewayUrl_ + "/v1/api/iserver/reauthenticate", "");
            auto j = json::parse(resp);
            return !j.value("message", std::string()).empty();
        } catch (...) {
            return false;
        }
    }

private:
    std::string gatewayUrl_;
    bool        verifySsl_;
    HttpClient  http_;

    static HttpClientConfig makeClientConfig(bool verifySsl) {
        HttpClientConfig cfg;
        cfg.verify_ssl  = verifySsl;
        cfg.timeout_sec = 15;
        return cfg;
    }

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
    // Délègue au calendrier UTC/DST commun (item 20) : l'ancien calcul
    // codait UTC-5 en dur (faux 8 mois/an en EDT) et ouvrait dès 9h00.
    static bool isUsMarketHours() {
        return isUsEquityMarketOpenUtc(std::time(nullptr));
    }

    // ── HTTP GET via le client commun (code HTTP vérifié, retry) ──
    std::string get(const std::string& url) {
        return request("GET", url, "");
    }

protected:
    // HTTP bas niveau — virtuel pour la substitution dans les tests
    // unitaires (aucun réseau), même pattern qu'IBKRBroker
    virtual std::string request(const std::string& method,
                                const std::string& url,
                                const std::string& body) {
        return http_.request(method, url, body);
    }

    // Horloge — virtuelle pour la substitution dans les tests (filtre 9.3
    // de la barre du jour en formation)
    virtual std::time_t now_() const { return std::time(nullptr); }
};

} // namespace trading
