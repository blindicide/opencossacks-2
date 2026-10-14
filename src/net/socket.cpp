#include "net/socket.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace oc2::net {

namespace {

#ifdef _WIN32
using Sock = SOCKET;
constexpr Sock kBad = INVALID_SOCKET;
void closeSock(Sock s) { closesocket(s); }
bool wouldBlock() { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
void setNonBlocking(Sock s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
void startup() {
    static const bool once = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    (void)once;
}
#else
using Sock = int;
constexpr Sock kBad = -1;
void closeSock(Sock s) { ::close(s); }
bool wouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EINTR; }
void setNonBlocking(Sock s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
void startup() {
    static const bool once = [] {
        signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)once;
}
#endif

constexpr std::size_t kMaxFrame = 1u << 20;

std::string addressText(const sockaddr_in& a) {
    char buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a.sin_addr, buf, sizeof buf);
    return buf;
}

void tune(Sock s) {
    setNonBlocking(s);
    const int one = 1; // без задержки Нейгла: приказы мелкие
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
}

} // namespace

Conn::Conn(std::intptr_t handle, std::string peer) : handle_(handle), peer_(std::move(peer)) {}

Conn::~Conn() {
    if (handle_ != std::intptr_t(kBad)) closeSock(Sock(handle_));
}

void Conn::send(const std::string& message) {
    if (closed_ || message.size() > kMaxFrame) return;
    const std::uint32_t n = std::uint32_t(message.size());
    const char head[4] = {char(n >> 24), char(n >> 16), char(n >> 8), char(n)};
    out_.append(head, 4);
    out_ += message;
}

void Conn::pump() {
    if (closed_) return;
    while (!out_.empty()) {
        const auto sent = ::send(Sock(handle_), out_.data(), int(out_.size()), 0);
        if (sent > 0) {
            out_.erase(0, std::size_t(sent));
        } else {
            if (sent < 0 && wouldBlock()) break;
            closed_ = true;
            return;
        }
    }
    char buf[4096];
    for (;;) {
        const auto got = ::recv(Sock(handle_), buf, sizeof buf, 0);
        if (got > 0) {
            in_.append(buf, std::size_t(got));
        } else {
            if (got < 0 && wouldBlock()) break;
            closed_ = true; // 0 — собеседник закрыл
            break;
        }
    }
}

bool Conn::poll(std::string& message) {
    if (in_.size() < 4) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(in_.data());
    const std::size_t n = std::size_t(p[0]) << 24 | std::size_t(p[1]) << 16 | std::size_t(p[2]) << 8 | p[3];
    if (n > kMaxFrame) {
        closed_ = true;
        in_.clear();
        return false;
    }
    if (in_.size() < 4 + n) return false;
    message.assign(in_, 4, n);
    in_.erase(0, 4 + n);
    return true;
}

std::unique_ptr<Listener> Listener::open(std::uint16_t port) {
    startup();
    const Sock s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBad) return nullptr;
    const int one = 1;
#ifndef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(s, 8) != 0) {
        closeSock(s);
        return nullptr;
    }
    socklen_t len = sizeof a;
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &len); // port == 0: система выбрала сама
    setNonBlocking(s);
    return std::unique_ptr<Listener>(new Listener(std::intptr_t(s), ntohs(a.sin_port)));
}

Listener::~Listener() { closeSock(Sock(handle_)); }

std::unique_ptr<Conn> Listener::accept() {
    sockaddr_in a{};
    socklen_t len = sizeof a;
    const Sock c = ::accept(Sock(handle_), reinterpret_cast<sockaddr*>(&a), &len);
    if (c == kBad) return nullptr;
    tune(c);
    return std::unique_ptr<Conn>(new Conn(std::intptr_t(c), addressText(a)));
}

std::unique_ptr<Conn> connectTo(const std::string& host, std::uint16_t port, int timeoutMs) {
    startup();
    addrinfo hints{}, *list = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list) != 0 || !list) return nullptr;
    sockaddr_in addr{};
    std::memcpy(&addr, list->ai_addr, sizeof addr);
    freeaddrinfo(list);
    const Sock s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBad) return nullptr;
    setNonBlocking(s);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 && !wouldBlock()) {
        closeSock(s);
        return nullptr;
    }
    fd_set wr, ex;
    FD_ZERO(&wr);
    FD_ZERO(&ex);
    FD_SET(s, &wr);
    FD_SET(s, &ex);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (::select(int(s) + 1, nullptr, &wr, &ex, &tv) <= 0 || FD_ISSET(s, &ex)) {
        closeSock(s);
        return nullptr;
    }
    int err = 0;
    socklen_t len = sizeof err;
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
    if (err != 0) {
        closeSock(s);
        return nullptr;
    }
    tune(s);
    return std::unique_ptr<Conn>(new Conn(std::intptr_t(s), addressText(addr)));
}

std::unique_ptr<Beacon> Beacon::open(std::uint16_t port) {
    startup();
    const Sock s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kBad) return nullptr;
    const int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
#if defined(SO_REUSEPORT) && !defined(_WIN32)
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&one), sizeof one);
#endif
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        closeSock(s);
        return nullptr;
    }
    setNonBlocking(s);
    return std::unique_ptr<Beacon>(new Beacon(std::intptr_t(s), port));
}

Beacon::~Beacon() {
    if (handle_ != std::intptr_t(kBad)) closeSock(Sock(handle_));
}

void Beacon::announce(const std::string& message) {
    for (const char* ip : {"255.255.255.255", "127.0.0.1"}) {
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(port_);
        inet_pton(AF_INET, ip, &to.sin_addr);
        ::sendto(Sock(handle_), message.data(), int(message.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
    }
}

bool Beacon::poll(std::string& message, std::string& from) {
    char buf[1024];
    sockaddr_in a{};
#ifdef _WIN32
    int len = sizeof a;
#else
    socklen_t len = sizeof a;
#endif
    const auto n = ::recvfrom(Sock(handle_), buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&a), &len);
    if (n <= 0) return false;
    message.assign(buf, std::size_t(n));
    from = addressText(a);
    return true;
}

std::string localAddress() {
#ifdef _WIN32
    startup();
    char name[256] = {};
    if (gethostname(name, sizeof name) != 0) return {};
    addrinfo hints{}, *list = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(name, nullptr, &hints, &list) != 0) return {};
    std::string r;
    for (addrinfo* i = list; i && r.empty(); i = i->ai_next) {
        const std::string t = addressText(*reinterpret_cast<sockaddr_in*>(i->ai_addr));
        if (t.rfind("127.", 0) != 0) r = t;
    }
    freeaddrinfo(list);
    return r;
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return {};
    std::string r;
    for (ifaddrs* i = list; i && r.empty(); i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        const std::string t = addressText(*reinterpret_cast<sockaddr_in*>(i->ifa_addr));
        if (t.rfind("127.", 0) != 0) r = t;
    }
    freeifaddrs(list);
    return r;
#endif
}

} // namespace oc2::net
