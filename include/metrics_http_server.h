#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace wxz::core {

// 极简 HTTP server：只提供 GET /metrics（或自定义 path）
// - 单线程 accept loop + 每连接同步处理，响应后 close
// - 不依赖第三方库
class MetricsHttpServer final {
public:
    struct Options {
        std::string bind_addr{"0.0.0.0"};
        int port{9100};
        std::string path{"/metrics"};
        int backlog{64};
    };

    using RenderFn = std::function<std::string()>;

    MetricsHttpServer(Options opts, RenderFn render);
    ~MetricsHttpServer();

    MetricsHttpServer(const MetricsHttpServer&) = delete;
    MetricsHttpServer& operator=(const MetricsHttpServer&) = delete;

    bool start();
    void stop();

private:
    void run_();
    static void write_all_(int fd, const char* data, std::size_t size);

private:
    Options opts_;
    RenderFn render_;

    std::atomic<bool> running_{false};
    int listen_fd_{-1};
    std::thread worker_;
};

} // namespace wxz::core
