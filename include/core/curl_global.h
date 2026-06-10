#pragma once
// ============================================================
//  curl_global.h  —  Garde RAII pour l'init globale de libcurl
//
//  curl_global_init/curl_global_cleanup ne sont PAS thread-safe
//  et doivent encadrer toute la durée de vie du processus, pas
//  celle d'un objet quelconque (Sprint 2, item 7 : 4 classes les
//  appelaient dans leurs ctors/dtors — un broker temporaire
//  détruit en début de programme nettoyait l'état global pendant
//  que d'autres handles pouvaient être en vol).
//
//  Usage : instancier UN SEUL CurlGlobal en tête de main(), avant
//  toute création de thread et tout objet utilisant libcurl.
//
//      int main() {
//          CurlGlobal curlGlobal;   // init libcurl pour tout le processus
//          ...
//      }                            // cleanup à la sortie
//
//  Un compteur statique rend les instances imbriquées inoffensives
//  (seules la première/dernière touchent l'état global), mais la
//  création doit rester mono-thread.
// ============================================================
#include <atomic>
#include <curl/curl.h>

class CurlGlobal {
public:
    CurlGlobal() {
        if (count_().fetch_add(1, std::memory_order_acq_rel) == 0)
            curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~CurlGlobal() {
        if (count_().fetch_sub(1, std::memory_order_acq_rel) == 1)
            curl_global_cleanup();
    }

    CurlGlobal(const CurlGlobal&)            = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;

    // Nombre d'instances vivantes (exposé pour les tests)
    static int instances() {
        return count_().load(std::memory_order_acquire);
    }

private:
    static std::atomic<int>& count_() {
        static std::atomic<int> c{0};
        return c;
    }
};
