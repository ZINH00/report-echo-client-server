#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

static std::atomic<bool> g_running{true};
static int g_listen_fd = -1;

static std::vector<int> g_clients;
static std::mutex g_clients_mtx;

struct ServerOptions {
    bool echo = false;       // -e
    bool broadcast = false;  // -b
    uint16_t port = 0;
};

void usage_server(const char*) {
    std::cerr
        << "syntax : echo-server <port> [-e[-b]]\n"
        << "sample : echo-server 1234 -e -b\n";
}

void on_sigint(int) {
    g_running = false;
    if (g_listen_fd >= 0) close(g_listen_fd);
}

ssize_t send_all(int fd, const char* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        sent += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(sent);
}

void remove_client_fd(int fd) {
    std::lock_guard<std::mutex> lock(g_clients_mtx);
    auto it = std::find(g_clients.begin(), g_clients.end(), fd);
    if (it != g_clients.end()) g_clients.erase(it);
}

void broadcast_to_all(const char* buf, size_t len) {
    std::lock_guard<std::mutex> lock(g_clients_mtx);
    for (int cfd : g_clients) {
        (void)send_all(cfd, buf, len);
    }
}

void echo_to_one(int fd, const char* buf, size_t len) {
    (void)send_all(fd, buf, len);
}

void client_thread(int cfd, sockaddr_in peer, const ServerOptions opts) {
    char addrstr[64];
    inet_ntop(AF_INET, &peer.sin_addr, addrstr, sizeof(addrstr));
    uint16_t peer_port = ntohs(peer.sin_port);

    char buf[4096];
    while (g_running) {
        ssize_t n = recv(cfd, buf, sizeof(buf), 0);
        if (n == 0) {
            std::cerr << "[INFO] disconnected " << addrstr << ":" << peer_port << "\n";
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[WARN] recv error from " << addrstr << ":" << peer_port
                      << " errno=" << errno << " (" << strerror(errno) << ")\n";
            break;
        }

        std::cout << "[" << addrstr << ":" << peer_port << "] ";
        std::cout.write(buf, n);
        std::cout.flush();

        if (opts.broadcast) {
            broadcast_to_all(buf, static_cast<size_t>(n));
        } else if (opts.echo) {
            echo_to_one(cfd, buf, static_cast<size_t>(n));
        }
    }

    shutdown(cfd, SHUT_RDWR);
    close(cfd);
    remove_client_fd(cfd);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage_server(argv[0]);
        return 1;
    }

    ServerOptions opts{};
    try {
        int p = std::stoi(argv[1]);
        if (p <= 0 || p > 65535) throw std::out_of_range("port");
        opts.port = static_cast<uint16_t>(p);
    } catch (...) {
        usage_server(argv[0]);
        return 1;
    }

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-e") opts.echo = true;
        else if (a == "-b") opts.broadcast = true;
        else {
            usage_server(argv[0]);
            return 1;
        }
    }

    std::signal(SIGINT, on_sigint);

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        std::perror("socket");
        return 1;
    }

    int yes = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(opts.port);

    if (bind(g_listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("bind");
        return 1;
    }

    if (listen(g_listen_fd, 16) < 0) {
        std::perror("listen");
        return 1;
    }

    std::cerr << "[INFO] echo-server listen on port " << opts.port
              << " (echo=" << (opts.echo ? "on" : "off")
              << ", broadcast=" << (opts.broadcast ? "on" : "off") << ")\n";

    std::vector<std::thread> threads;

    while (g_running) {
        sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        int cfd = accept(g_listen_fd, (sockaddr*)&peer, &plen);
        if (cfd < 0) {
            if (!g_running) break;
            if (errno == EINTR) continue;
            std::perror("accept");
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(g_clients_mtx);
            g_clients.push_back(cfd);
        }

        std::thread(client_thread, cfd, peer, opts).detach();
    }

    {
        std::lock_guard<std::mutex> lock(g_clients_mtx);
        for (int fd : g_clients) {
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
        g_clients.clear();
    }

    if (g_listen_fd >= 0) close(g_listen_fd);
    std::cerr << "[INFO] server terminated\n";
    return 0;
}

