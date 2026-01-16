#include "metrics_http_server.h"

#include <cerrno>
#include <cstring>
#include <string_view>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace wxz::core {

MetricsHttpServer::MetricsHttpServer(Options opts, RenderFn render)
    : opts_(std::move(opts)), render_(std::move(render)) {}

MetricsHttpServer::~MetricsHttpServer() {
    stop();
}

bool MetricsHttpServer::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return true;

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        running_.store(false);
        return false;
    }

    int yes = 1;
    (void)::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(opts_.port));
    if (opts_.bind_addr.empty() || opts_.bind_addr == "0" || opts_.bind_addr == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (::inet_pton(AF_INET, opts_.bind_addr.c_str(), &addr.sin_addr) != 1) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            running_.store(false);
            return false;
        }
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }

    if (::listen(listen_fd_, opts_.backlog) != 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }

    worker_ = std::thread([this]() { run_(); });
    return true;
}

void MetricsHttpServer::stop() {
    if (!running_.exchange(false)) return;

    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (worker_.joinable()) worker_.join();
}

void MetricsHttpServer::write_all_(int fd, const char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t n = ::send(fd, data, size, MSG_NOSIGNAL);
        if (n <= 0) return;
        data += static_cast<std::size_t>(n);
        size -= static_cast<std::size_t>(n);
    }
}

void MetricsHttpServer::run_() {
    while (running_.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd_, &rfds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000; // 200ms

        const int rc = ::select(listen_fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (!running_.load()) break;
        if (rc <= 0) continue;
        if (!FD_ISSET(listen_fd_, &rfds)) continue;

        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        const int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (fd < 0) continue;

        char buf[4096];
        const ssize_t nread = ::recv(fd, buf, sizeof(buf) - 1, 0);
        if (nread <= 0) {
            ::close(fd);
            continue;
        }
        buf[nread] = '\0';

        std::string_view req(buf, static_cast<std::size_t>(nread));
        // 解析：METHOD SP PATH SP HTTP/...
        const std::size_t sp1 = req.find(' ');
        const std::size_t sp2 = (sp1 == std::string_view::npos) ? std::string_view::npos : req.find(' ', sp1 + 1);
        const std::string_view method = (sp1 == std::string_view::npos) ? std::string_view{} : req.substr(0, sp1);
        const std::string_view path = (sp2 == std::string_view::npos) ? std::string_view{} : req.substr(sp1 + 1, sp2 - (sp1 + 1));

        const bool ok = (method == "GET" && path == opts_.path);

        if (!ok) {
            static constexpr const char kHdr[] =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                "Content-Length: 9\r\n"
                "Connection: close\r\n"
                "\r\n"
                "not_found";
            write_all_(fd, kHdr, sizeof(kHdr) - 1);
            ::close(fd);
            continue;
        }

        std::string body;
        try {
            body = render_ ? render_() : std::string{};
        } catch (...) {
            body.clear();
        }

        const std::string hdr =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n";

        write_all_(fd, hdr.data(), hdr.size());
        if (!body.empty()) write_all_(fd, body.data(), body.size());

        ::close(fd);
    }
}

} // namespace wxz::core
