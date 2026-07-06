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
#include "strategies/ProdConfig.hpp"
#include "core/bot_state.h"
#include "core/ws_server.h"
#include "core/db_logger.h"
#include "core/state_store.h"
#include "core/watchdog.h"
#include "core/curl_global.h"

#include "core/LiveGate.hpp"

#include <iostream>
#include <csignal>
#include <filesystem>
#include <thread>
#include <chrono>
#include <string>
#include <stdexcept>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static volatile std::sig_atomic_t g_running = 1; // type sûr en handler de signal (D8)
void on_signal(int) { g_running = 0; }

// ── Adaptateur trading::ILogger → DbLogger (item 21) ─────────
// Unifie les deux systèmes de journalisation : chaque log du bot atterrit
// AUSSI dans la table `logs` SQLite, en plus de la console. Branché via un
// CompositeLogger au composition root.
class DbLogSink final : public trading::ILogger {
public:
    explicit DbLogSink(DbLogger& db) : db_(db) {}
    void info (const std::string& m) override { write("INFO ", m); }
    void warn (const std::string& m) override { write("WARN ", m); }
    void error(const std::string& m) override { write("ERROR", m); }
    void debug(const std::string& m) override { write("DEBUG", m); }
private:
    DbLogger& db_;
    void write(const std::string& level, const std::string& msg) {
        db_.log({level, msg, trading::currentTimestamp()});
    }
};

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
    // Init libcurl pour tout le processus, avant tout thread et tout
    // objet réseau (feeds, brokers, watchdog) — voir core/curl_global.h
    CurlGlobal curlGlobal;

    bool liveMode  = hasFlag(argc, argv, "--live");
    std::string accountId = getArg(argc, argv, "--account");
    std::string gatewayUrl = "https://localhost:5000"; // toujours localhost

    std::cout << R"(
╔══════════════════════════════════════════════════════════╗
║     QQQ SWING BOT — IBKR )" << (liveMode ? "LIVE TRADING ⚠️  " : "Paper Trading    ") << R"(  ║
╚══════════════════════════════════════════════════════════╝
)" << '\n';

    // ── Configuration de la stratégie + réglages opérationnels ─
    // Source unique : config/prod.json chargé et VALIDÉ par ProdConfig.hpp
    // (item 9.1) — le même fichier est backtesté par le golden « config
    // prod » (D21). Ne pas redéfinir les paramètres ici.
    trading::SwingConfig cfg;
    trading::ProdSettings prodSettings;
    try {
        cfg          = trading::prodSwingConfig();
        prodSettings = trading::loadProdSettingsJson(SWINGBOT_PROD_CONFIG_JSON);
    } catch (const std::exception& e) {
        std::cerr << "❌ Config de production invalide : " << e.what() << "\n";
        return 1;
    }

    // Canaux d'alerte depuis l'environnement (A4, variables SWINGBOT_*) —
    // aucun credential en dur, exigé par le gate live ci-dessous.
    AlertConfig alertCfg = alertConfigFromEnv();
    alertCfg.heartbeat_interval_sec = 60;
    // Le heartbeat n'est émis qu'une fois par cycle (60 min) : le seuil de
    // silence doit couvrir un cycle complet + marge, sinon fausse alerte
    // « bot silencieux » à chaque cycle (découverte D1)
    alertCfg.max_silence_sec        = 3900;  // 65 min

    // ── Gate live (A1) : quatre couches, toutes obligatoires ──
    // (1) liveTradingApproved dans prod.json (verrouillé par test tant que
    // la DoD d'edge n'est pas atteinte), (2) un canal d'alerte configuré,
    // (3) stdin est un terminal (restart auto → refus), (4) « OUI » tapé.
    {
        trading::LiveGateInput gate;
        gate.liveFlag          = liveMode;
        gate.approvedInConfig  = prodSettings.liveTradingApproved;
        gate.alertChannelReady = anyAlertChannelEnabled(alertCfg);
#ifdef _WIN32
        gate.stdinIsTty        = _isatty(_fileno(stdin)) != 0;
#else
        gate.stdinIsTty        = isatty(STDIN_FILENO) != 0;
#endif
        if (liveMode) {
            std::cout << "⚠️  MODE LIVE — ORDRES RÉELS AVEC VOTRE ARGENT\n";
            std::cout << "   Checklist pré-live : voir RUNBOOK.md\n";
            // L'invite n'a de sens que si les couches non interactives passent
            if (gate.approvedInConfig && gate.alertChannelReady && gate.stdinIsTty)
                std::cout << "   Tapez OUI (exactement) pour confirmer : " << std::flush;
        }
        if (auto refus = trading::liveGateRefusal(gate, std::cin)) {
            std::cerr << "\n❌ Démarrage LIVE refusé : " << *refus << "\n";
            return 1;
        }
        if (!liveMode)
            std::cout << "📄 Mode PAPER TRADING — argent fictif, vrais prix\n\n";
        else
            std::cout << "\n✅ Gate live franchi — ordres réels ACTIFS\n\n";
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

    // ── Infrastructure ────────────────────────────────────────
    BotState botState;
    botState.mode    = liveMode ? "live" : "paper";
    botState.dry_run = false;

    WsServer wsServer(9001, botState);
    wsServer.start();
    std::cout << "\n[WsServer] Dashboard sur ws://localhost:9001\n";

    // Les DB vivent dans data/ : c'est le répertoire monté en volume par
    // docker-compose (./data:/app/data) — écrites dans /app, elles n'étaient
    // PAS persistées sur l'hôte.
    std::filesystem::create_directories("data");
    DbLogger db("data/swingbot_ibkr.db");
    std::cout << "[DB]       data/swingbot_ibkr.db ouvert\n";

    // alertCfg construit plus haut (env SWINGBOT_*, gate live)
    Watchdog watchdog(alertCfg, botState);
    watchdog.start();

    // ── Création des dépendances IBKR ─────────────────────────
    auto dataFeed = std::make_shared<trading::IBKRDataFeed>(gatewayUrl);
    auto broker   = std::make_shared<trading::IBKRBroker>(accountId, gatewayUrl);
    auto strategy = trading::SwingStrategy::create(cfg);
    auto riskMgr  = std::make_shared<trading::RiskManager>();
    // Journalisation unifiée (item 21) : console + persistance SQLite (table logs)
    auto logger   = std::make_shared<trading::CompositeLogger>();
    logger->addLogger(std::make_shared<trading::ConsoleLogger>());
    logger->addLogger(std::make_shared<DbLogSink>(db));
    // Persistance de l'état de position : survit aux redémarrages
    // (réconciliée avec la position broker à chaque cycle)
    auto stateStore = std::make_shared<trading::SqliteStateStore>("data/swingbot_ibkr_state.db");

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
    trading::TradingBot bot(dataFeed, broker, std::move(strategy), riskMgr, logger, stateStore);
    bot.setConfig(cfg);

    // ── Persistance des trades + dashboard (item 21) ──────────
    // Chaque fill confirmé alimente la table `trades` (record_trade/close_trade,
    // jamais appelées auparavant) et la liste des positions du dashboard.
    bot.setTradeObserver([&](const trading::TradeFill& f) {
        if (f.side == trading::OrderSide::BUY) {
            double sl = f.price * (1.0 - cfg.stopLossPct);
            double tp = f.price * (1.0 + cfg.takeProfitPct);
            db.record_trade(cfg.symbol, "buy", f.quantity, f.price, sl, tp, "open");

            std::lock_guard<std::mutex> lk(botState.mtx);
            botState.positions.clear();
            PositionData p;
            p.symbol        = f.symbol;
            p.side          = "long";
            p.qty           = f.quantity;
            p.avg_entry     = f.price;
            p.stop_loss     = sl;
            p.take_profit   = tp;
            p.current_price = f.price;
            botState.positions.push_back(p);
        } else {  // SELL : clôture du trade ouvert
            db.close_trade(cfg.symbol, f.price, f.pnl);

            std::lock_guard<std::mutex> lk(botState.mtx);
            botState.positions.clear();
        }
    });

    // ── Alerte kill-switch (A6) : l'opérateur DOIT savoir qu'un garde-fou
    // de risque s'est armé — dédupliqué par raison côté bot.
    bot.setHaltObserver([&](const std::string& raison) {
        botState.push_log("ERROR", "KILL-SWITCH : " + raison);
        watchdog.alertNow("🛑 KILL-SWITCH déclenché : " + raison
                          + " — entrées bloquées jusqu'à la prochaine séance");
    });

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    double initialEquity = account.equity;
    std::cout << "🟢 Bot démarré. Vérification toutes les 60 minutes.\n";
    std::cout << "   Ctrl+C pour arrêter proprement.\n\n";

    // Instantané de santé (item 15.2) : horodatage du dernier cycle SAIN, pour
    // exposer l'âge de la dernière santé (détecte un process qui tourne mais
    // silencieusement malsain, au-delà du HEALTHCHECK TCP).
    long lastHealthyEpoch = 0;
    auto nowEpoch = []() {
        return static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    };

    while (g_running) {
        try {
            // Supervision de l'auth Gateway (A3) : la session CP expire
            // (~24 h). Sans ré-auth, chaque appel échouerait en silence —
            // ici : cycle sauté, heartbeat RETENU → l'alerte watchdog part
            // après max_silence_sec, avec le diagnostic explicite.
            const bool gwAuth = dataFeed->isAuthenticated();
            bool cycleHealthy = false;
            if (!gwAuth) {
                botState.push_log("ERROR",
                    "CP Gateway non authentifié (session expirée ?) — "
                    "cycle sauté, ré-authentifier via https://localhost:5000");
                std::cerr << "[ERROR] Gateway non authentifié — cycle sauté\n";
            } else {
                bot.runOnce();
                // Heartbeat SEULEMENT sur cycle sain (A2) : une panne
                // feed/broker laisse le watchdog s'affamer → alerte.
                cycleHealthy = bot.lastCycleHealthy();
                if (cycleHealthy) {
                    watchdog.heartbeat();
                } else {
                    botState.push_log("ERROR",
                        "Cycle en échec — heartbeat retenu (le watchdog "
                        "alertera si la panne persiste)");
                }
            }
            // Instantané de santé (item 15.2) : le composition root a désormais
            // tous les faits du cycle (auth, cycle sain, kill-switch de NIVEAU).
            if (cycleHealthy) lastHealthyEpoch = nowEpoch();
            const std::string haltReason = bot.currentHaltReason();
            botState.set_health(cycleHealthy, lastHealthyEpoch, gwAuth,
                                !haltReason.empty(), haltReason);

            // Synchronise BotState avec le compte IBKR — SAUTÉE sur panne
            // (A5) : {0,0,INACTIVE} écrasait equity=0 et diffusait une
            // perte fictive massive au dashboard et à la courbe d'équité.
            auto acc = broker->getAccount();
            if (acc.status == "ACTIVE") {
                {
                    std::lock_guard<std::mutex> lk(botState.mtx);
                    botState.equity  = acc.equity;
                    botState.pnl     = acc.equity - initialEquity;
                    botState.pnl_pct = initialEquity > 0
                        ? botState.pnl / initialEquity * 100.0
                        : 0.0;
                }
                db.snapshot_equity(acc.equity, botState.pnl, botState.cycle + 1);
            } else {
                botState.push_log("WARN",
                    "Compte non ACTIF (" + acc.status + ") — sync equity sautée");
            }
            {
                std::lock_guard<std::mutex> lk(botState.mtx);
                botState.cycle++;
            }
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
