#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace oc2::net {

// Неблокирующее TCP-соединение; сообщения — кадры «длина (4 байта, big-endian) + данные».
class Conn {
public:
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    // Ставит сообщение в очередь отправки (до 1 МиБ).
    void send(const std::string& message);
    // Отправляет накопленное и читает пришедшее; после обрыва closed() == true.
    void pump();
    // Следующее принятое целое сообщение.
    bool poll(std::string& message);
    bool closed() const { return closed_; }
    // Все ли отправленное ушло в сокет.
    bool flushed() const { return out_.empty(); }
    // Адрес собеседника «a.b.c.d».
    const std::string& peer() const { return peer_; }

private:
    friend class Listener;
    friend std::unique_ptr<Conn> connectTo(const std::string&, std::uint16_t, int);
    Conn(std::intptr_t handle, std::string peer);
    std::intptr_t handle_;
    std::string peer_;
    std::string in_, out_;
    bool closed_ = false;
};

// Слушающий сокет хозяина комнаты.
class Listener {
public:
    // nullptr, если порт занят.
    static std::unique_ptr<Listener> open(std::uint16_t port);
    ~Listener();
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    // Новое входящее соединение или nullptr.
    std::unique_ptr<Conn> accept();
    std::uint16_t port() const { return port_; }

private:
    explicit Listener(std::intptr_t handle, std::uint16_t port) : handle_(handle), port_(port) {}
    std::intptr_t handle_;
    std::uint16_t port_;
};

// Подключение к host:port (IPv4-адрес или имя); ждёт не дольше timeoutMs. nullptr — не удалось.
std::unique_ptr<Conn> connectTo(const std::string& host, std::uint16_t port, int timeoutMs = 3000);

// Первый не-loopback IPv4-адрес машины (для показа хозяину), пусто если нет.
std::string localAddress();

} // namespace oc2::net
