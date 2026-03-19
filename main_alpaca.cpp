// ============================================================
//  main_alpaca.cpp  —  Paper Trading Alpaca en temps réel
//
//  Ce fichier remplace main.cpp pour le trading avec les vrais
//  prix du marché, mais avec de l'argent fictif (paper trading).
//
//  AVANT DE LANCER :
//    export ALPACA_KEY=PKxxxxxxxxxxxxxxxx
//    export ALPACA_SECRET=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
//
//  USAGE :
//    ./build/swing_bot_alpaca           # paper trading (défaut)
//    ./build/swing_bot_alpaca --live    # LIVE TRADING (argent réel !)
// ============================================================
#include "brokers/AlpacaDataFeed.hpp"
#include "brokers/AlpacaBroker.hpp"
#include "bot/TradingBot.hpp"
#include "bot/Logger.hpp"
#include "bot/RiskManager.hpp"
#include "strategies/SwingStrategy.hpp"
#include "core/bot_state.h"
#include "core/ws_server.h"
#include "core/db_logger.h"
#include "core/watchdog.h"

#include <iostream>
#include <cstdlib>
#include <csignal>
#include <thread>
#include <chrono>
#include <string>

// ─── Globals ──────────────────────────────────────────────
static volatile bool g_running = true;
void on_signal(int) { g_running = false; }

// ─── Lecture des variables d'environnement ────────────────
static std::string getEnv(const std::string& key, const std::string& def = "") {
    const char* v = std::getenv(key.c_str());
    return v ? std::string(v) : def;
}

int main(int argc, char* argv[]) {

    // ── Mode paper ou live ────────────────────────────────
    bool liveMode = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--live") liveMode = true;

    std::cout << R"(
╔══════════════════════════════════════════════════════════╗
║       QQQ SWING BOT — Alpaca )" << (liveMode ? "LIVE TRADING ⚠️  " : "Paper Trading    ") << R"(  ║
╚══════════════════════════════════════════════════════════╝
)" << '\n';

    if (liveMode) {
        std::cout << "⚠️  MODE LIVE — ORDRES RÉELS AVEC VOTRE ARGENT\n";
        std::cout << "   Appuyez sur Ctrl+C dans les 5 secondes pour annuler...\n\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
    } else {
        std::cout << "📄 Mode PAPER TRADING — argent fictif, vrais prix\n\n";
    }

    // ── Lecture des clés API ──────────────────────────────
    std::string apiKey    = getEnv("ALPACA_KEY");
    std::string apiSecret = getEnv("ALPACA_SECRET");

    if (apiKey.empty() || apiSecret.empty()) {
        std::cerr << "❌ Clés API manquantes.\n\n";
        std::cerr << "   Définis les variables d'environnement :\n";
        std::cerr << "   export ALPACA_KEY=PKxxxxxxxxxxxxxxxx\n";
        std::cerr << "   export ALPACA_SECRET=xxxxxxxxxxxxxxxx\n\n";
        std::cerr << "   Crée un compte gratuit sur https://alpaca.markets\n";
        return 1;
    }

    std::cout << "🔑 Clé API : " << apiKey.substr(0, 6) << "...\n";
    std::cout << "🌐 Mode    : " << (liveMode ? "LIVE" : "paper") << "\n\n";

    // ── Configuration de la stratégie ─────────────────────
    trading::SwingConfig cfg;
    cfg.symbol          = "QQQ";
    cfg.emaFast         = 13;
    cfg.emaSlow         = 21;
    cfg.rsiPeriod       = 14;
    cfg.rsiBuyMax       = 65.0;
    cfg.rsiSellMin      = 80.0;
    cfg.stopLossPct     = 0.07;
    cfg.takeProfitPct   = 0.15;
    cfg.trailingStopPct = 0.03;
    cfg.riskPerTradePct = 0.02;
    cfg.minHoldDays     = 2;

    // ── Infrastructure WebSocket + SQLite + Watchdog ──────
    BotState  botState;
    botState.mode    = liveMode ? "live" : "paper";
    botState.dry_run = false;  // Alpaca exécute de vrais ordres paper

    WsServer  wsServer(9001, botState);
    wsServer.start();
    std::cout << "[WsServer] Dashboard sur ws://localhost:9001\n";

    DbLogger  db("swingbot_alpaca.db");
    std::cout << "[DB] swingbot_alpaca.db ouvert\n";

    AlertConfig alertCfg;
    alertCfg.heartbeat_interval_sec = 60;
    alertCfg.max_silence_sec        = 300;
    alertCfg.webhook_enabled        = false; // Active ici ton Discord webhook
    // alertCfg.webhook_url = "https://discord.com/api/webhooks/XXX/YYY";
    Watchdog watchdog(alertCfg, botState);
    watchdog.start();

    // ── Création des dépendances Alpaca ───────────────────
    auto dataFeed   = std::make_shared<trading::AlpacaDataFeed>(apiKey, apiSecret, !liveMode);
    auto broker     = std::make_shared<trading::AlpacaBroker>  (apiKey, apiSecret, !liveMode);
    auto strategy   = trading::SwingStrategy::create(cfg);
    auto riskMgr    = std::make_shared<trading::RiskManager>();
    auto logger     = std::make_shared<trading::ConsoleLogger>();

    // ── Vérification du compte ────────────────────────────
    auto account = broker->getAccount();
    if (account.status != "ACTIVE") {
        std::cerr << "❌ Compte Alpaca non actif (status=" << account.status << ")\n";
        return 1;
    }
    std::cout << "💰 Compte Alpaca actif\n";
    std::cout << "   Cash   : $" << static_cast<int>(account.cash)   << "\n";
    std::cout << "   Equity : $" << static_cast<int>(account.equity) << "\n\n";

    // Annule les ordres en attente du run précédent
    broker->cancelAllOrders();

    // ── Bot principal ─────────────────────────────────────
    trading::TradingBot bot(dataFeed, broker, std::move(strategy), riskMgr, logger);
    bot.setConfig(cfg);

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    std::cout << "🟢 Bot démarré. Vérification toutes les 60 minutes.\n";
    std::cout << "   Ctrl+C pour arrêter proprement.\n\n";

    while (g_running) {
        try {
            bot.runOnce();
            watchdog.heartbeat();

            // Synchronise BotState avec le compte réel
            auto acc = broker->getAccount();
            {
                std::lock_guard<std::mutex> lk(botState.mtx);
                botState.equity  = acc.equity;
                botState.pnl     = acc.equity - 100000.0; // capital initial paper = 100k
                botState.pnl_pct = botState.pnl / 100000.0 * 100.0;
                botState.cycle++;
            }

            // Persiste le snapshot
            db.snapshot_equity(acc.equity, botState.pnl, botState.cycle);

            // Broadcast dashboard
            wsServer.broadcast(botState.to_json_str());

        } catch (const std::exception& e) {
            botState.push_log("ERROR", std::string("Exception: ") + e.what());
            std::cerr << "[ERROR] " << e.what() << "\n";
        }

        // Attend 60 minutes (vérifie Ctrl+C toutes les secondes)
        for (int s = 0; s < 3600 && g_running; ++s)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // ── Arrêt propre ──────────────────────────────────────
    std::cout << "\n[INFO] Arrêt en cours...\n";
    watchdog.stop();
    wsServer.stop();
    std::cout << "[INFO] Bot arrêté proprement.\n";
    return 0;
}
