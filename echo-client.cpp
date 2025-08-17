#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

static std::atomic<bool> g_running{true};

void usage_client(const char*) {
    std::cerr
        << "syntax : echo-client <ip> <port>\n"
        << "sample : echo-client 192.168.10.2 1234\n";
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

void rx_thread(int fd) {
    char buf[4096];
    while (g_running) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            std::cerr << "[INFO] server closed connection\n";
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[WARN] recv error errno=" << errno
                      << " (" << strerror(errno) << ")\n";
            break;
        }
        std::cout.write(buf, n);
        std::cout.flush();
    }
    g_running = false;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        usage_client(argv[0]);
        return 1;
    }

    const char* ip = argv[1];
    uint16_t port = 0;
    try {
        int p = std::stoi(argv[2]);
        if (p <= 0 || p > 65535) throw std::out_of_range("port");
        port = static_cast<uint16_t>(p);
    } catch (...) {
        usage_client(argv[0]);
        return 1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return 1;
    }

    sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &serv.sin_addr) != 1) {
        std::cerr << "invalid ip: " << ip << "\n";
        return 1;
    }

    if (connect(fd, (sockaddr*)&serv, sizeof(serv)) < 0) {
        std::perror("connect");
        return 1;
    }

    std::cerr << "[INFO] connected to " << ip << ":" << port << "\n";

    std::thread t(rx_thread, fd);

    std::string line;
    while (g_running && std::getline(std::cin, line)) {
        line.push_back('\n');
        if (send_all(fd, line.data(), line.size()) < 0) {
            std::perror("send");
            break;
        }
    }

    shutdown(fd, SHUT_WR);
    if (t.joinable()) t.join();
    close(fd);
    std::cerr << "[INFO] client terminated\n";
    return 0;
}

