#pragma once
// ============================================================
//  GatewayAuthMonitor.hpp — transitions d'auth du CP Gateway
//  (item 17.4)
//
//  Avant : la perte d'auth n'était visible que par l'alerte
//  watchdog « bot silencieux » (jusqu'à 65 min après, heartbeat
//  affamé) — sans diagnostic explicite ni alerte immédiate.
//  Ce moniteur détecte les TRANSITIONS : une alerte à la PERTE
//  (une seule, pas à chaque cycle sauté), un événement à la
//  RÉCUPÉRATION. Logique PURE et testable (patron LiveGate) —
//  le composition root câble les effets (alertNow, push_log).
// ============================================================

namespace trading {

class GatewayAuthMonitor {
public:
    enum class Event { Aucun, PerteAuth, Recuperation };

    // À appeler une fois par cycle avec l'état d'auth constaté.
    // Part « authentifié » : le composition root a déjà vérifié l'auth au
    // démarrage (le programme refuse de démarrer sinon) — une première
    // observation dé-authentifiée est donc bien une PERTE.
    Event observe(bool authenticated) {
        const bool avant = dernier_;
        dernier_ = authenticated;
        if (avant && !authenticated) return Event::PerteAuth;
        if (!avant && authenticated) return Event::Recuperation;
        return Event::Aucun;
    }

private:
    bool dernier_ = true;
};

} // namespace trading
