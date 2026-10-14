#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "net/socket.hpp"

namespace oc2::net {

// Поиск комнат в локальной сети: хозяин раз в секунду рассылает объявление, остальные собирают список.
class Discovery {
public:
    static constexpr std::uint16_t kPort = 34568;

    struct Game {
        std::string host;    // IPv4 отправителя
        std::uint16_t port = 0;
        std::string name;    // имя хозяина
        std::string map;     // файл карты
        int humans = 1;
        std::chrono::steady_clock::time_point seen;
    };

    // Слушать объявления (меню «Сетевая игра»).
    void listen(bool on);
    // Объявлять свою комнату (пустой name — не объявлять).
    void announce(const std::string& name, std::uint16_t tcpPort, const std::string& map, int humans);
    void stop();
    // Принять пришедшее, отправить своё, убрать устаревшие записи (4 с без объявления).
    void pump();
    const std::vector<Game>& games() const { return games_; }
    bool listening() const { return listening_; }

    // Разбор и сборка датаграммы «OC2D\tверсия\tпорт\tчеловек\tимя\tкарта».
    static std::string encode(const std::string& version, std::uint16_t port, int humans, const std::string& name,
                              const std::string& map);
    static bool decode(const std::string& text, const std::string& version, Game& out);

private:
    std::unique_ptr<Beacon> beacon_;
    bool listening_ = false;
    std::string line_;
    std::chrono::steady_clock::time_point lastSend_{};
    std::vector<Game> games_;
};

} // namespace oc2::net
