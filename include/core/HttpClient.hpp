#pragma once
// ============================================================
//  HttpClient.hpp  —  Client HTTP commun (libcurl)
//
//  Sprint 2, item 8 : le couple request()/writeCallback était
//  dupliqué dans IBKRBroker, IBKRDataFeed, AlpacaBroker et
//  AlpacaDataFeed, sans vérification du code HTTP ni retry.
//
//  Ce client centralise :
//    - la vérification du code de statut HTTP ;
//    - le retry avec backoff exponentiel sur les erreurs
//      transitoires : erreur transport, 429 (rate limit), 5xx ;
//    - l'échec immédiat (sans retry) sur les 4xx définitifs.
//
//  NOTE retry & ordres : un retry de POST peut re-soumettre un
//  ordre déjà reçu par le serveur — l'idempotence est garantie en
//  amont par le cOID d'IBKRBroker (un ordre par symbole/side/heure).
//
//  L'init globale de libcurl est faite au main via CurlGlobal
//  (core/curl_global.h) — jamais ici.
// ============================================================
#include <curl/curl.h>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace trading {

struct HttpResponse {
    long        status = 0;
    std::string body;
};

// Erreur HTTP porteuse du code de statut (0 = erreur transport) :
// permet aux appelants de distinguer p. ex. un 404 « pas de ressource »
// d'une vraie panne (item 10)
class HttpError : public std::runtime_error {
public:
    HttpError(long status, const std::string& msg)
        : std::runtime_error(msg), status_(status) {}
    long status() const { return status_; }
private:
    long status_;
};

struct HttpClientConfig {
    long timeout_sec        = 15;    // timeout curl par essai
    bool verify_ssl         = true;  // false pour le cert auto-signé du CP Gateway
    int  max_retries        = 3;     // re-tentatives APRÈS le premier essai
    int  initial_backoff_ms = 250;   // doublé à chaque re-tentative
};

class HttpClient {
public:
    explicit HttpClient(HttpClientConfig cfg = {}) : cfg_(cfg) {}
    virtual ~HttpClient() = default;

    // Exécute la requête avec retry + backoff exponentiel.
    // Retourne le corps de la réponse si statut 2xx.
    // Lance std::runtime_error : statut 4xx définitif (sans retry),
    // ou erreur transitoire persistante après épuisement des essais.
    std::string request(const std::string& method,
                        const std::string& url,
                        const std::string& body = "",
                        const std::vector<std::string>& headers = {}) {
        int backoff_ms = cfg_.initial_backoff_ms;
        std::string lastError;

        for (int attempt = 0; attempt <= cfg_.max_retries; ++attempt) {
            if (attempt > 0) {
                sleepFor(std::chrono::milliseconds(backoff_ms));
                backoff_ms *= 2;
            }

            HttpResponse resp;
            try {
                resp = performOnce(method, url, body, headers);
            } catch (const std::exception& e) {
                lastError = e.what();   // erreur transport → transitoire
                continue;
            }

            if (resp.status >= 200 && resp.status < 300)
                return resp.body;

            // 429 (rate limit) et 5xx : transitoires → retry avec backoff
            if (resp.status == 429 || resp.status >= 500) {
                lastError = "statut HTTP " + std::to_string(resp.status);
                continue;
            }

            // 3xx/4xx définitif : réessayer ne changera rien
            throw HttpError(resp.status, "HTTP " + method + " " + url
                + " → statut " + std::to_string(resp.status)
                + (resp.body.empty() ? "" : " : " + resp.body));
        }

        throw HttpError(0, "HTTP " + method + " " + url
            + " : échec après " + std::to_string(cfg_.max_retries + 1)
            + " essais (" + lastError + ")");
    }

    std::string get(const std::string& url,
                    const std::vector<std::string>& headers = {}) {
        return request("GET", url, "", headers);
    }

    std::string post(const std::string& url, const std::string& body,
                     const std::vector<std::string>& headers = {}) {
        return request("POST", url, body, headers);
    }

    std::string del(const std::string& url,
                    const std::vector<std::string>& headers = {}) {
        return request("DELETE", url, "", headers);
    }

protected:
    // Seam de test : UN essai HTTP, sans retry. Les tests substituent
    // cette méthode pour scripter les réponses sans réseau.
    virtual HttpResponse performOnce(const std::string& method,
                                     const std::string& url,
                                     const std::string& body,
                                     const std::vector<std::string>& headers) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl_easy_init failed");

        HttpResponse resp;
        struct curl_slist* hdrs = nullptr;
        for (const auto& h : headers)
            hdrs = curl_slist_append(hdrs, h.c_str());

        curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
        if (hdrs)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeCallback_);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &resp.body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,        cfg_.timeout_sec);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cfg_.verify_ssl ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, cfg_.verify_ssl ? 2L : 0L);

        if (method == "POST")
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        else if (method != "GET")
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK)
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);

        curl_slist_free_all(hdrs);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
            throw std::runtime_error(std::string("HTTP ") + method + " " + url
                                     + " : " + curl_easy_strerror(res));
        return resp;
    }

    // Seam de test : permet de vérifier le backoff sans dormir réellement
    virtual void sleepFor(std::chrono::milliseconds ms) {
        std::this_thread::sleep_for(ms);
    }

private:
    HttpClientConfig cfg_;

    static size_t writeCallback_(void* data, size_t size,
                                 size_t nmemb, std::string* out) {
        out->append(static_cast<char*>(data), size * nmemb);
        return size * nmemb;
    }
};

} // namespace trading
