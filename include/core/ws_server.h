#pragma once
// ============================================================
//  ws_server.h  —  Serveur WebSocket broadcast (Boost.Beast)
// ============================================================
#include <string>
#include <memory>
#include "bot_state.h"

class WsServer {
public:
    // port=0 → l'OS choisit un port libre automatiquement
    // Garantit l'indépendance des tests même en processus séparés
    explicit WsServer(unsigned short port, BotState& state);
    ~WsServer();

    void   start();
    void   stop();
    void   broadcast(const std::string& json);
    size_t client_count() const;

    // Retourne le port réellement utilisé après start()
    // Indispensable quand port=0 pour que le client sache où se connecter
    unsigned short actual_port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};