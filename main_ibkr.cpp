// ============================================================
//  main_ibkr.cpp  —  Paper Trading IBKR en temps réel
//
//  PRÉREQUIS :
//  1. Compte IBKR Pro ouvert sur interactivebrokers.ca
//  2. CP Gateway téléchargé et authentifié sur localhost:5000
//  3. Paper trading activé dans Account Management
//
//  USAGE :
//    ./build/swing_bot_ibkr               # paper (défaut)
//    ./build/swing_bot_ibkr --live        # LIVE (argent réel !)
//    ./build/swing_bot_ibkr --account DU123456  # forcer l'accountId
// ============================================================
#include "brokers/IBKRDataFeed.hpp"
#include "brokers/IBKRBroker.hpp"
#include "bot/TradingBot.hpp"
#include "bot/Logger.hpp"
#include "bot/RiskManager.hpp"
#include "strategies/SwingStrategy.hpp"
#include "core/bot_state.h"
#include "core/ws_server.h"
#include "core/db_logger.h"
#include "core/watchdog.h"

#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <string>
#include <stdexcept>

static volatile bool g_running = true;
void on_signal(int) { g_running = false; }

// ── Parse argument ligne de commande ─────────────────────────
static std::string getArg(int argc, char* argv[],
                           const std::string& flag,
                           const std::string& def = "") {
    for (int i = 1; i < argc - 1; ++i)
        if (std::string(argv[i]) == flag) return argv[i + 1];
    return def;
}
static bool hasFlag(int argc, char* argv[], const std::string& flag) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == flag) return true;
    return false;
}

int main(int argc, char* argv[]) {
    bool liveMode  = hasFlag(argc, argv, "--live");
    std::string accountId = getArg(argc, argv, "--account");
    std::string gatewayUrl = "https://localhost:5000"; // toujours localhost

    std::cout << R"(
╔══════════════════════════════════════════════════════════╗
║     QQQ SWING BOT — IBKR )" << (liveMode ? "LIVE TRADING ⚠️  " : "Paper Trading    ") << R"(  ║
╚══════════════════════════════════════════════════════════╝
)" << '\n';

    if (liveMode) {
        std::cout << "⚠️  MODE LIVE — ORDRES RÉELS AVEC VOTRE ARGENT\n";
        std::cout << "   Appuyez sur Ctrl+C dans les 5 secondes pour annuler...\n\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
    } else {
        std::cout << "📄 Mode PAPER TRADING — argent fictif, vrais prix\n\n";
    }

    // ── Vérification du CP Gateway ────────────────────────────
    std::cout << "🔌 Connexion au CP Gateway sur " << gatewayUrl << "...\n";
    {
        trading::IBKRDataFeed testFeed(gatewayUrl);
        if (!testFeed.isAuthenticated()) {
            std::cerr << "\n❌ CP Gateway non authentifié.\n\n";
            std::cerr << "   Pour démarrer le gateway :\n";
            std::cerr << "   1. Lance clientportal.gw/bin/run.sh conf.yaml (Linux)\n";
            std::cerr << "   2. Ouvre https://localhost:5000 dans ton navigateur\n";
            std::cerr << "   3. Connecte-toi avec ton compte IBKR\n";
            std::cerr << "   4. Relance ce programme\n\n";
            return 1;
        }
        std::cout << "✅ Gateway authentifié\n";
    }

    // ── Récupération de l'accountId ───────────────────────────
    if (accountId.empty()) {
        std::cout << "🔍 Recherche du compte...\n";
        try {
            accountId = trading::IBKRBroker::fetchFirstAccountId(gatewayUrl);
            std::cout << "✅ Compte trouvé : " << accountId << "\n";
        } catch (const std::exception& e) {
            std::cerr << "❌ " << e.what() << "\n";
            return 1;
        }
    }

    // ── Configuration de la stratégie ─────────────────────────
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

    // ── Infrastructure ────────────────────────────────────────
    BotState botState;
    botState.mode    = liveMode ? "live" : "paper";
    botState.dry_run = false;

    WsServer wsServer(9001, botState);
    wsServer.start();
    std::cout << "\n[WsServer] Dashboard sur ws://localhost:9001\n";

    DbLogger db("swingbot_ibkr.db");
    std::cout << "[DB]       swingbot_ibkr.db ouvert\n";

    AlertConfig alertCfg;
    alertCfg.heartbeat_interval_sec = 60;
    alertCfg.max_silence_sec        = 300;
    alertCfg.webhook_enabled        = false;
    // alertCfg.webhook_url = "https://discord.com/api/webhooks/XXX/YYY";
    Watchdog watchdog(alertCfg, botState);
    watchdog.start();

    // ── Création des dépendances IBKR ─────────────────────────
    auto dataFeed = std::make_shared<trading::IBKRDataFeed>(gatewayUrl);
    auto broker   = std::make_shared<trading::IBKRBroker>(accountId, gatewayUrl);
    auto strategy = trading::SwingStrategy::create(cfg);
    auto riskMgr  = std::make_shared<trading::RiskManager>();
    auto logger   = std::make_shared<trading::ConsoleLogger>();

    // ── Vérification du solde ─────────────────────────────────
    auto account = broker->getAccount();
    if (account.status != "ACTIVE") {
        std::cerr << "❌ Compte non actif (status=" << account.status << ")\n";
        return 1;
    }
    std::cout << "\n💰 Compte IBKR actif : " << accountId << "\n";
    std::cout << "   Cash   : $" << static_cast<int>(account.cash)   << "\n";
    std::cout << "   Equity : $" << static_cast<int>(account.equity) << "\n\n";

    // ── Bot principal ─────────────────────────────────────────
    trading::TradingBot bot(dataFeed, broker, std::move(strategy), riskMgr, logger);
    bot.setConfig(cfg);

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    double initialEquity = account.equity;
    std::cout << "🟢 Bot démarré. Vérification toutes les 60 minutes.\n";
    std::cout << "   Ctrl+C pour arrêter proprement.\n\n";

    while (g_running) {
        try {
            bot.runOnce();
            watchdog.heartbeat();

            // Synchronise BotState avec le compte IBKR
            auto acc = broker->getAccount();
            {
                std::lock_guard<std::mutex> lk(botState.mtx);
                botState.equity  = acc.equity;
                botState.pnl     = acc.equity - initialEquity;
                botState.pnl_pct = initialEquity > 0
                    ? botState.pnl / initialEquity * 100.0
                    : 0.0;
                botState.cycle++;
            }

            db.snapshot_equity(acc.equity, botState.pnl, botState.cycle);
            wsServer.broadcast(botState.to_json_str());

        } catch (const std::exception& e) {
            botState.push_log("ERROR", std::string("Exception: ") + e.what());
            std::cerr << "[ERROR] " << e.what() << "\n";
        }

        // Attend 60 minutes, vérifie Ctrl+C chaque seconde
        for (int s = 0; s < 3600 && g_running; ++s)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n[INFO] Arrêt en cours...\n";
    watchdog.stop();
    wsServer.stop();
    std::cout << "[INFO] Bot arrêté proprement.\n";
    return 0;
}
