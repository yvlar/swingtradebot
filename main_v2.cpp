// ============================================================
//  main_v2.cpp  —  Bot complet avec nlohmann/json, SQLite, Watchdog
// ============================================================
#include "core/bot_state.h"
#include "core/ws_server.h"
#include "core/db_logger.h"
#include "core/watchdog.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

// ─── Globals ──────────────────────────────────────────────
BotState  g_state;
WsServer* g_ws  = nullptr;
DbLogger* g_db  = nullptr;
Watchdog* g_wd  = nullptr;

static volatile bool g_running = true;
void on_signal(int) { g_running = false; }

// ─── Helper : log partout + broadcast ─────────────────────
void bot_log(const std::string& level, const std::string& msg) {
    g_state.push_log(level, msg);
    std::cout << "[" << level << "] " << msg << "\n";

    // Persistance SQLite
    if (g_db && !g_state.logs.empty())
        g_db->log(g_state.logs.back());

    // Broadcast WS
    if (g_ws) g_ws->broadcast(g_state.to_json_str());
}

// ─── Callback push état ───────────────────────────────────
void push_state() {
    if (g_ws) g_ws->broadcast(g_state.to_json_str());
}

// ─── Cycle du bot ─────────────────────────────────────────
void run_cycle() {
    g_state.running = true;
    g_state.cycle++;
    bot_log("INFO", "=== Cycle #" + std::to_string(g_state.cycle) + " ===");

    // ── DATA FEED ─────────────────────────────────────────
    g_state.set_stage(PipelineStage::DATA);
    push_state();
    bot_log("INFO", "DataFeed: chargement OHLCV …");
    // → ton DataFeed::fetch() ici
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    g_wd->heartbeat();  // bot encore vivant

    // ── SIGNAL ───────────────────────────────────────────
    g_state.set_stage(PipelineStage::SIGNAL);
    {
        std::lock_guard<std::mutex> lk(g_state.mtx);
        g_state.signals = {
            {"QQQ","LONG",3, 28.5, 472.30, 2.85, 466.60, 483.70},
        };
    }
    push_state();
    // Persiste tous les signaux
    for (const auto& s : g_state.signals)
        if (g_db) g_db->record_signal(s, g_state.cycle);

    bot_log("OK", "Signal QQQ LONG score=3/3 RSI=28.5");
    g_wd->heartbeat();

    // ── RISK ─────────────────────────────────────────────
    g_state.set_stage(PipelineStage::RISK);
    push_state();
    double dollar_risk = g_state.equity * 0.01;
    int    qty         = static_cast<int>(dollar_risk / (2.85 * 2.0));
    bot_log("INFO", "RiskManager: qty=" + std::to_string(qty)
            + " risque=$" + std::to_string((int)dollar_risk));
    g_wd->heartbeat();

    // ── BROKER ───────────────────────────────────────────
    g_state.set_stage(PipelineStage::BROKER);
    push_state();

    if (g_state.dry_run) {
        bot_log("WARN", "DRY-RUN — ordre non envoyé");
    } else {
        bot_log("OK", "FILL QQQ LONG " + std::to_string(qty) + "@472.30");

        // Persiste le trade en SQLite
        if (g_db) g_db->record_trade("QQQ","long",qty,472.30,466.60,483.70);

        // Met à jour les positions dans l'état global
        PositionData pos;
        pos.symbol = "QQQ"; pos.side = "long"; pos.qty = qty;
        pos.avg_entry = 472.30; pos.stop_loss = 466.60;
        pos.take_profit = 483.70; pos.current_price = 472.30;
        pos.pnl = 0; pos.pnl_pct = 0;
        {
            std::lock_guard<std::mutex> lk(g_state.mtx);
            g_state.positions.push_back(pos);
            g_state.equity -= qty * 472.30;
        }
    }

    // ── FIN DE CYCLE ─────────────────────────────────────
    g_state.set_stage(PipelineStage::IDLE);
    g_state.pnl     = g_state.equity - 10000.0;
    g_state.pnl_pct = (g_state.pnl / 10000.0) * 100.0;
    g_state.running = false;

    // Snapshot equity dans SQLite
    if (g_db) g_db->snapshot_equity(g_state.equity, g_state.pnl, g_state.cycle);

    bot_log("OK", "=== Cycle terminé. Equity=$"
            + std::to_string((int)g_state.equity) + " ===");
    g_wd->heartbeat();   // heartbeat final du cycle
    push_state();
}

// ─── main ─────────────────────────────────────────────────
int main() {
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    // ── Init état ────────────────────────────────────────
    g_state.equity  = 10000.0;
    g_state.dry_run = true;
    g_state.mode    = "paper";

    // ── SQLite ───────────────────────────────────────────
    DbLogger db("swingbot.db");
    g_db = &db;
    std::cout << "[DB] swingbot.db ouvert\n";

    // ── WebSocket ────────────────────────────────────────
    WsServer ws_server(9001, g_state);
    g_ws = &ws_server;
    ws_server.start();

    // ── Watchdog ─────────────────────────────────────────
    AlertConfig alert_cfg;
    alert_cfg.heartbeat_interval_sec = 60;
    alert_cfg.max_silence_sec        = 180;

    // Email (optionnel — remplis tes infos)
    alert_cfg.email_enabled = false;
    // alert_cfg.smtp_user  = "ton_bot@gmail.com";
    // alert_cfg.smtp_pass  = "mot_de_passe_app";
    // alert_cfg.email_to   = "toi@gmail.com";

    // Discord webhook (optionnel)
    alert_cfg.webhook_enabled = false;
    // alert_cfg.webhook_url = "https://discord.com/api/webhooks/XXX/YYY";

    // SMS Twilio (optionnel)
    alert_cfg.sms_enabled = false;
    // alert_cfg.twilio_sid   = "ACxxxxxxxx";
    // alert_cfg.twilio_token = "xxxxxxxx";
    // alert_cfg.twilio_from  = "+1XXXXXXXXXX";
    // alert_cfg.twilio_to    = "+1XXXXXXXXXX";

    Watchdog wd(alert_cfg, g_state);
    g_wd = &wd;
    wd.on_alert([](const std::string& msg){
        std::cerr << "[WATCHDOG ALERT]\n" << msg << "\n";
    });
    wd.start();

    std::cout << "=== SwingBot v2 démarré ===\n"
              << "WS  : ws://localhost:9001\n"
              << "DB  : swingbot.db\n"
              << "Mode: " << g_state.mode << "\n\n";

    // ── Boucle principale ─────────────────────────────────
    while (g_running) {
        run_cycle();
        std::cout << "Prochain cycle dans 60 min…\n";
        for (int i = 0; i < 3600 && g_running; i++)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\nArrêt propre…\n";
    wd.stop();
    ws_server.stop();
    return 0;
}
