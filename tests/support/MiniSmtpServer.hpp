#pragma once
// ============================================================
//  MiniSmtpServer.hpp  —  Serveur SMTP minimal avec STARTTLS
//  pour les tests d'intégration (canal email du Watchdog).
//
//  Même squelette que MiniHttpServer : mono-thread, sockets
//  POSIX, port éphémère (port 0), stop() TSan-propre via fd
//  atomique. Dialogue SMTP minimal : 220 → EHLO/250-STARTTLS →
//  STARTTLS/220 → poignée de main TLS (OpenSSL, SSL_accept sur
//  le fd brut) → EHLO/AUTH/MAIL FROM/RCPT TO/DATA capturés.
//
//  Certificat auto-signé GÉNÉRÉ À L'EXÉCUTION (APIs EVP/X509
//  d'OpenSSL 3) avec SAN IP:127.0.0.1 — exigé par la
//  vérification d'hôte de curl. Le PEM du certificat est écrit
//  dans un fichier temporaire (caPath(), à passer en
//  smtp_ca_path) ; la clé privée ne touche JAMAIS le disque.
//  Un certificat commité expirerait (rouge futur sans cause) et
//  sa clé ressemblerait à un secret pour les scanners — la
//  génération à l'exécution évite les deux.
// ============================================================
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class MiniSmtpServer {
public:
    explicit MiniSmtpServer(bool advertiseStartTls = true)
        : advertise_starttls_(advertiseStartTls) {
        // SSL_write peut lever SIGPIPE si le client a déjà fermé — pas de
        // MSG_NOSIGNAL possible via OpenSSL, on ignore le signal (test only).
        std::signal(SIGPIPE, SIG_IGN);

        makeCertificate_();

        ctx_ = SSL_CTX_new(TLS_server_method());
        if (!ctx_) throw std::runtime_error("MiniSmtpServer: SSL_CTX_new");
        if (SSL_CTX_use_certificate(ctx_, x509_) != 1
            || SSL_CTX_use_PrivateKey(ctx_, pkey_) != 1)
            throw std::runtime_error("MiniSmtpServer: cert/clé refusés");

        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) throw std::runtime_error("MiniSmtpServer: socket");

        int yes = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = 0;                     // port éphémère
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            throw std::runtime_error("MiniSmtpServer: bind");

        socklen_t len = sizeof(addr);
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        ::listen(fd_, 8);
        thread_ = std::thread([this] { loop_(); });
    }

    ~MiniSmtpServer() {
        stop();
        if (ctx_)  SSL_CTX_free(ctx_);
        if (x509_) X509_free(x509_);
        if (pkey_) EVP_PKEY_free(pkey_);
        if (!cert_path_.empty()) ::unlink(cert_path_.c_str());
    }

    int port() const { return port_; }

    std::string url() const {
        return "smtp://127.0.0.1:" + std::to_string(port_);
    }

    // Chemin PEM du certificat auto-signé — à passer en smtp_ca_path
    std::string caPath() const { return cert_path_; }

    void stop() {
        // fd_ atomique : stop() (thread du test) et loop_() (thread serveur)
        // y accèdent concurremment — même leçon TSan que MiniHttpServer.
        int fd = fd_.exchange(-1);
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
        if (thread_.joinable()) thread_.join();
    }

    // ── État capturé (sous mutex) ─────────────────────────
    bool tlsEstablished() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return tls_established_;
    }
    std::string authLine() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return auth_line_;
    }
    bool authInTls() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return auth_in_tls_;
    }
    std::string mailFrom() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return mail_from_;
    }
    std::vector<std::string> rcptTo() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return rcpt_to_;
    }
    std::string message() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return message_;
    }
    // Lignes reçues EN CLAIR (avant la poignée de main TLS)
    std::vector<std::string> plaintextLines() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return plain_lines_;
    }

    // Attend qu'un message complet (DATA) soit arrivé (timeout en ms)
    bool waitForMessage(int timeoutMs = 5'000) const {
        for (int waited = 0; waited < timeoutMs; waited += 20) {
            if (!message().empty()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return !message().empty();
    }

private:
    bool                     advertise_starttls_;
    std::atomic<int>         fd_{-1};
    int                      port_ = 0;
    std::thread              thread_;
    mutable std::mutex       mtx_;

    SSL_CTX*                 ctx_  = nullptr;
    EVP_PKEY*                pkey_ = nullptr;
    X509*                    x509_ = nullptr;
    std::string              cert_path_;

    bool                     tls_established_ = false;
    bool                     auth_in_tls_     = false;
    std::string              auth_line_;
    std::string              mail_from_;
    std::vector<std::string> rcpt_to_;
    std::string              message_;
    std::vector<std::string> plain_lines_;

    // ── Certificat auto-signé (généré à l'exécution) ──────
    void makeCertificate_() {
        pkey_ = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
        if (!pkey_) throw std::runtime_error("MiniSmtpServer: keygen");

        x509_ = X509_new();
        if (!x509_) throw std::runtime_error("MiniSmtpServer: X509_new");
        X509_set_version(x509_, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(x509_), 1);
        X509_gmtime_adj(X509_getm_notBefore(x509_), -60);
        X509_gmtime_adj(X509_getm_notAfter(x509_), 24L * 3600L);
        X509_set_pubkey(x509_, pkey_);

        X509_NAME* name = X509_get_subject_name(x509_);
        X509_NAME_add_entry_by_txt(
            name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>("127.0.0.1"), -1, -1, 0);
        X509_set_issuer_name(x509_, name);

        // SAN IP:127.0.0.1 — sans lui, curl refuse l'hôte même avec le CA
        X509V3_CTX v3ctx;
        X509V3_set_ctx_nodb(&v3ctx);
        X509V3_set_ctx(&v3ctx, x509_, x509_, nullptr, nullptr, 0);
        X509_EXTENSION* san = X509V3_EXT_conf_nid(
            nullptr, &v3ctx, NID_subject_alt_name, "IP:127.0.0.1");
        if (!san) throw std::runtime_error("MiniSmtpServer: SAN");
        X509_add_ext(x509_, san, -1);
        X509_EXTENSION_free(san);

        if (X509_sign(x509_, pkey_, EVP_sha256()) == 0)
            throw std::runtime_error("MiniSmtpServer: X509_sign");

        // Seul le CERTIFICAT est écrit sur disque (CURLOPT_CAINFO) — la
        // clé privée reste en mémoire.
        const char* tmpdir = std::getenv("TMPDIR");
        std::string tmpl = std::string(tmpdir ? tmpdir : "/tmp")
                         + "/minismtp-ca-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        int fd = ::mkstemp(buf.data());
        if (fd < 0) throw std::runtime_error("MiniSmtpServer: mkstemp");
        cert_path_ = buf.data();
        FILE* f = ::fdopen(fd, "w");
        if (!f) { ::close(fd); throw std::runtime_error("MiniSmtpServer: fdopen"); }
        PEM_write_X509(f, x509_);
        std::fclose(f);
    }

    // ── Connexion : lecture ligne à ligne, en clair ou TLS ─
    struct Conn {
        int         fd;
        SSL*        ssl = nullptr;
        std::string buf;

        bool readLine(std::string& line) {
            for (;;) {
                size_t pos = buf.find('\n');
                if (pos != std::string::npos) {
                    line = buf.substr(0, pos);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    buf.erase(0, pos + 1);
                    return true;
                }
                char tmp[1024];
                int n = ssl ? SSL_read(ssl, tmp, sizeof(tmp))
                            : static_cast<int>(::recv(fd, tmp, sizeof(tmp), 0));
                if (n <= 0) return false;
                buf.append(tmp, static_cast<size_t>(n));
            }
        }

        void write(const std::string& s) {
            if (ssl) {
                (void)SSL_write(ssl, s.data(), static_cast<int>(s.size()));
            } else {
                (void)::send(fd, s.data(), s.size(), MSG_NOSIGNAL);
            }
        }
    };

    void loop_() {
        while (true) {
            int fd = fd_.load();
            if (fd < 0) break;                        // stop() a fermé la socket
            int client = ::accept(fd, nullptr, nullptr);
            if (client < 0) break;                    // socket fermée → stop()
            handle_(client);
            ::close(client);
        }
    }

    static std::string upper_(const std::string& s) {
        std::string u = s;
        for (auto& c : u) c = static_cast<char>(std::toupper(
            static_cast<unsigned char>(c)));
        return u;
    }

    void handle_(int client) {
        Conn c{client, nullptr, {}};
        c.write("220 minismtp pret\r\n");

        std::string line;
        bool inTls = false;
        while (c.readLine(line)) {
            if (!inTls) {
                std::lock_guard<std::mutex> lk(mtx_);
                plain_lines_.push_back(line);
            }
            const std::string u = upper_(line);

            if (u.rfind("EHLO", 0) == 0 || u.rfind("HELO", 0) == 0) {
                if (inTls)
                    c.write("250-minismtp\r\n250-AUTH PLAIN\r\n250 8BITMIME\r\n");
                else if (advertise_starttls_)
                    c.write("250-minismtp\r\n250-STARTTLS\r\n250 8BITMIME\r\n");
                else
                    c.write("250-minismtp\r\n250 8BITMIME\r\n");
            } else if (u.rfind("STARTTLS", 0) == 0) {
                if (!advertise_starttls_) {
                    c.write("454 TLS non disponible\r\n");
                    continue;
                }
                c.write("220 pret pour TLS\r\n");
                SSL* ssl = SSL_new(ctx_);
                SSL_set_fd(ssl, client);
                if (SSL_accept(ssl) <= 0) {           // ex. client rejette le cert
                    SSL_free(ssl);
                    return;
                }
                c.ssl = ssl;
                inTls = true;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    tls_established_ = true;
                }
            } else if (u.rfind("AUTH", 0) == 0) {
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    auth_line_   = line;
                    auth_in_tls_ = inTls;
                }
                if (line.size() > std::strlen("AUTH PLAIN")) {
                    // Réponse initiale SASL en ligne (RFC 4954)
                    c.write("235 2.7.0 authentification ok\r\n");
                } else {
                    c.write("334 \r\n");
                    std::string b64;
                    if (c.readLine(b64)) {
                        std::lock_guard<std::mutex> lk(mtx_);
                        auth_line_ += " " + b64;
                    }
                    c.write("235 2.7.0 authentification ok\r\n");
                }
            } else if (u.rfind("MAIL FROM", 0) == 0) {
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    mail_from_ = line;
                }
                c.write("250 ok\r\n");
            } else if (u.rfind("RCPT TO", 0) == 0) {
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    rcpt_to_.push_back(line);
                }
                c.write("250 ok\r\n");
            } else if (u.rfind("DATA", 0) == 0) {
                c.write("354 fin par <CRLF>.<CRLF>\r\n");
                std::string msg, l;
                while (c.readLine(l)) {
                    if (l == ".") break;
                    msg += l + "\r\n";
                }
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    message_ = msg;
                }
                c.write("250 message accepte\r\n");
            } else if (u.rfind("QUIT", 0) == 0) {
                c.write("221 au revoir\r\n");
                break;
            } else {
                c.write("250 ok\r\n");                // RSET, NOOP, etc.
            }
        }

        if (c.ssl) {
            SSL_shutdown(c.ssl);
            SSL_free(c.ssl);
        }
    }
};
