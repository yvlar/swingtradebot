#pragma once
// ============================================================
//  watchdog.h  —  Détection de freeze + alertes email/SMS
//
//  Alertes supportées :
//    - Email via SMTP (libcurl)  → vcpkg install curl
//    - SMS via Twilio REST API   → vcpkg install curl
//    - Webhook (Discord/Slack)   → vcpkg install curl
//
//  CMake : find_package(CURL REQUIRED)
//          target_link_libraries(SwingBot CURL::libcurl)
// ============================================================
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <mutex>
#include <sstream>
#include <iostream>
#include <curl/curl.h>
#include "bot_state.h"

// ─── Config alertes ───────────────────────────────────────

struct AlertConfig {
    // Watchdog timing
    int  heartbeat_interval_sec = 60;   // bot doit signaler vie toutes les N sec
    int  max_silence_sec        = 180;  // alerte si silence > N sec (3× heartbeat)
    int  alert_timeout_sec      = 10;   // timeout curl des envois d'alerte (le
                                        // watchdog ne doit jamais se geler lui-même)

    // Email (SMTP via libcurl)
    bool        email_enabled  = false;
    std::string smtp_url       = "smtps://smtp.gmail.com:465";
    std::string smtp_user      = "ton_bot@gmail.com";
    std::string smtp_pass      = "";   // mot de passe app Google
    std::string email_to       = "toi@gmail.com";
    std::string email_from     = "ton_bot@gmail.com";

    // Twilio SMS
    bool        sms_enabled    = false;
    std::string twilio_sid     = "";
    std::string twilio_token   = "";
    std::string twilio_from    = "+1XXXXXXXXXX";
    std::string twilio_to      = "+1XXXXXXXXXX";

    // Discord / Slack webhook
    bool        webhook_enabled = false;
    std::string webhook_url     = "";   // Discord ou Slack webhook URL
};

// ─── Watchdog ─────────────────────────────────────────────

class Watchdog {
public:
    explicit Watchdog(AlertConfig cfg, BotState& state)
        : cfg_(std::move(cfg)), state_(state)
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~Watchdog() {
        stop();
        curl_global_cleanup();
    }

    // Appeler dans le thread principal du bot — indique que le bot est vivant
    // Thread-safe : last_heartbeat_ est atomique (lu par le thread watchdog)
    void heartbeat() {
        last_heartbeat_.store(std::chrono::steady_clock::now(),
                              std::memory_order_relaxed);
        consecutive_failures_ = 0;
    }

    // Lance le thread de surveillance (non-bloquant)
    void start() {
        running_ = true;
        thread_ = std::thread(&Watchdog::loop_, this);
        std::cout << "[Watchdog] Démarré — timeout=" << cfg_.max_silence_sec << "s\n";
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    // Callback optionnel — appelé avant l'envoi d'alerte
    void on_alert(std::function<void(const std::string&)> cb) {
        alert_cb_ = std::move(cb);
    }

private:
    AlertConfig   cfg_;
    BotState&     state_;
    std::atomic<bool> running_{false};
    std::thread   thread_;
    std::atomic<int>  consecutive_failures_{0};
    std::function<void(const std::string&)> alert_cb_;

    // Atomique : écrit par le thread principal (heartbeat), lu par loop_()
    std::atomic<std::chrono::steady_clock::time_point> last_heartbeat_{
        std::chrono::steady_clock::now()};

    void loop_() {
        while (running_) {
            std::this_thread::sleep_for(
                std::chrono::seconds(cfg_.heartbeat_interval_sec));

            auto now     = std::chrono::steady_clock::now();
            auto silence = std::chrono::duration_cast<std::chrono::seconds>(
                               now - last_heartbeat_.load(
                                   std::memory_order_relaxed)).count();

            if (silence > cfg_.max_silence_sec) {
                consecutive_failures_++;
                std::string msg = build_alert_msg_(silence);
                std::cerr << "[Watchdog] ALERTE: " << msg << "\n";

                state_.push_log("ERROR", "WATCHDOG: bot silencieux depuis "
                    + std::to_string(silence) + "s — alerte envoyée");

                if (alert_cb_) alert_cb_(msg);
                dispatch_alerts_(msg);
            } else {
                // Bot vivant — log discret
                if (consecutive_failures_ > 0) {
                    state_.push_log("OK", "WATCHDOG: bot de retour après "
                        + std::to_string(silence) + "s de silence");
                    consecutive_failures_ = 0;
                }
            }
        }
    }

    std::string build_alert_msg_(long silence_sec) const {
        std::ostringstream ss;
        ss << "🚨 SWING BOT ALERTE\n"
           << "Silence: " << silence_sec << "s (max=" << cfg_.max_silence_sec << "s)\n";
        {
            // BotState est partagé avec le thread principal → lecture sous lock
            std::lock_guard<std::mutex> lk(state_.mtx);
            ss << "Mode: "    << state_.mode << "\n"
               << "Cycle: #"  << state_.cycle << "\n"
               << "Equity: $" << state_.equity << "\n"
               << "Positions: " << state_.positions.size() << " ouvertes\n";
        }
        ss << "Tentative #" << consecutive_failures_.load();
        return ss.str();
    }

    void dispatch_alerts_(const std::string& msg) {
        if (cfg_.email_enabled)   send_email_(msg);
        if (cfg_.sms_enabled)     send_sms_(msg);
        if (cfg_.webhook_enabled) send_webhook_(msg);
    }

    // ── Email SMTP (libcurl) ─────────────────────────────
    void send_email_(const std::string& body) {
        CURL* curl = curl_easy_init();
        if (!curl) return;

        struct curl_slist* recipients = nullptr;
        recipients = curl_slist_append(recipients, cfg_.email_to.c_str());

        std::string payload =
            "To: "      + cfg_.email_to   + "\r\n"
            "From: "    + cfg_.email_from + "\r\n"
            "Subject: [SwingBot] Alerte — Bot silencieux\r\n"
            "\r\n"      + body            + "\r\n";

        // Utilise un pointeur partageable pour la callback
        auto payload_ptr = std::make_shared<std::string>(payload);
        size_t offset = 0;

        curl_easy_setopt(curl, CURLOPT_URL,           cfg_.smtp_url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERNAME,      cfg_.smtp_user.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD,      cfg_.smtp_pass.c_str());
        curl_easy_setopt(curl, CURLOPT_USE_SSL,       CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM,     cfg_.email_from.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT,     recipients);
        // Callback extrait avant curl_easy_setopt (le macro ne supporte pas les lambdas multi-lignes)
        using ReadData = std::pair<std::string*, size_t*>;
        static const auto read_cb = +[](char* buf, size_t sz, size_t n, void* ud) -> size_t {
            auto* p = static_cast<ReadData*>(ud);
            size_t avail   = p->first->size() - *p->second;
            size_t to_copy = std::min(avail, sz * n);
            if (!to_copy) return 0;
            memcpy(buf, p->first->c_str() + *p->second, to_copy);
            *p->second += to_copy;
            return to_copy;
        };
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_cb);
        auto ud = std::make_pair(payload_ptr.get(), &offset);
        curl_easy_setopt(curl, CURLOPT_READDATA,   &ud);
        curl_easy_setopt(curl, CURLOPT_UPLOAD,     1L);
        curl_easy_setopt(curl, CURLOPT_VERBOSE,    0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,    static_cast<long>(cfg_.alert_timeout_sec));
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(cfg_.alert_timeout_sec));

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "[Watchdog] Email error: " << curl_easy_strerror(res) << "\n";
        else
            std::cout << "[Watchdog] Email envoyé à " << cfg_.email_to << "\n";

        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
    }

    // ── SMS Twilio ───────────────────────────────────────
    void send_sms_(const std::string& body) {
        CURL* curl = curl_easy_init();
        if (!curl) return;

        std::string url = "https://api.twilio.com/2010-04-01/Accounts/"
                         + cfg_.twilio_sid + "/Messages.json";

        // Encode le body pour x-www-form-urlencoded
        char* enc_body  = curl_easy_escape(curl, body.c_str(), 0);
        char* enc_to    = curl_easy_escape(curl, cfg_.twilio_to.c_str(), 0);
        char* enc_from  = curl_easy_escape(curl, cfg_.twilio_from.c_str(), 0);
        std::string post = std::string("To=") + enc_to
                         + "&From=" + enc_from
                         + "&Body=" + enc_body;
        curl_free(enc_body); curl_free(enc_to); curl_free(enc_from);

        curl_easy_setopt(curl, CURLOPT_URL,         url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERNAME,    cfg_.twilio_sid.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD,    cfg_.twilio_token.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS,  post.c_str());
        curl_easy_setopt(curl, CURLOPT_VERBOSE,     0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,     static_cast<long>(cfg_.alert_timeout_sec));
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(cfg_.alert_timeout_sec));

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "[Watchdog] SMS error: " << curl_easy_strerror(res) << "\n";
        else
            std::cout << "[Watchdog] SMS envoyé à " << cfg_.twilio_to << "\n";

        curl_easy_cleanup(curl);
    }

    // ── Discord / Slack Webhook ──────────────────────────
    void send_webhook_(const std::string& body) {
        CURL* curl = curl_easy_init();
        if (!curl) return;

        // Format compatible Discord et Slack
        json payload = { {"content", body}, {"text", body} };
        std::string post = payload.dump();

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL,        cfg_.webhook_url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_VERBOSE,    0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,    static_cast<long>(cfg_.alert_timeout_sec));
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(cfg_.alert_timeout_sec));

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "[Watchdog] Webhook error: " << curl_easy_strerror(res) << "\n";
        else
            std::cout << "[Watchdog] Webhook envoyé\n";

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
};