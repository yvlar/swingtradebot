#pragma once
// ============================================================
//  MiniHttpServer.hpp  —  Serveur HTTP minimal pour les tests
//  d'intégration (HttpClient réel, webhook du Watchdog).
//
//  Mono-thread, sockets POSIX, port éphémère (port 0) comme
//  WsServer : aucun conflit entre processus de test parallèles.
//  Répond à chaque requête avec un statut et un corps fixes et
//  enregistre les requêtes brutes reçues.
// ============================================================
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MiniHttpServer {
public:
    explicit MiniHttpServer(int status = 200, std::string body = "ok")
        : status_(status), body_(std::move(body)) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) throw std::runtime_error("MiniHttpServer: socket");

        int yes = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = 0;                     // port éphémère
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            throw std::runtime_error("MiniHttpServer: bind");

        socklen_t len = sizeof(addr);
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        ::listen(fd_, 8);
        thread_ = std::thread([this] { loop_(); });
    }

    ~MiniHttpServer() { stop(); }

    int port() const { return port_; }

    std::string url(const std::string& path = "/") const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }

    void stop() {
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            fd_ = -1;
        }
        if (thread_.joinable()) thread_.join();
    }

    // Requêtes brutes reçues (ligne de requête + en-têtes + corps)
    std::vector<std::string> requests() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return requests_;
    }

    size_t requestCount() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return requests_.size();
    }

    // Attend qu'au moins n requêtes soient arrivées (timeout en ms)
    bool waitForRequests(size_t n, int timeoutMs = 5'000) const {
        for (int waited = 0; waited < timeoutMs; waited += 20) {
            if (requestCount() >= n) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return requestCount() >= n;
    }

private:
    int                      fd_   = -1;
    int                      port_ = 0;
    int                      status_;
    std::string              body_;
    std::thread              thread_;
    mutable std::mutex       mtx_;
    std::vector<std::string> requests_;

    void loop_() {
        while (true) {
            int client = ::accept(fd_, nullptr, nullptr);
            if (client < 0) break;                    // socket fermée → stop()

            std::string raw = readRequest_(client);
            {
                std::lock_guard<std::mutex> lk(mtx_);
                requests_.push_back(raw);
            }

            std::string resp =
                "HTTP/1.1 " + std::to_string(status_) + " X\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: " + std::to_string(body_.size()) + "\r\n"
                "Connection: close\r\n\r\n" + body_;
            ::send(client, resp.data(), resp.size(), MSG_NOSIGNAL);
            ::close(client);
        }
    }

    // Lit la requête : en-têtes complets puis Content-Length octets de corps
    static std::string readRequest_(int client) {
        std::string raw;
        char buf[4096];

        while (raw.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = ::recv(client, buf, sizeof(buf), 0);
            if (n <= 0) return raw;
            raw.append(buf, static_cast<size_t>(n));
        }

        size_t bodyStart = raw.find("\r\n\r\n") + 4;
        size_t expected  = contentLength_(raw);
        while (raw.size() - bodyStart < expected) {
            ssize_t n = ::recv(client, buf, sizeof(buf), 0);
            if (n <= 0) break;
            raw.append(buf, static_cast<size_t>(n));
        }
        return raw;
    }

    static size_t contentLength_(const std::string& raw) {
        const std::string key = "Content-Length:";
        size_t pos = raw.find(key);
        if (pos == std::string::npos) {
            // libcurl peut écrire l'en-tête en casse standard ou non
            pos = raw.find("content-length:");
            if (pos == std::string::npos) return 0;
        }
        return std::stoul(raw.substr(pos + key.size()));
    }
};
