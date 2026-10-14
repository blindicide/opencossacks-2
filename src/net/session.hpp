#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "net/socket.hpp"
#include "world/world.hpp"

namespace oc2::net {

// Сетевая игра по локальной сети: хозяин создаёт комнату, гости входят по адресу, потом все узлы
// идут в ногу (lockstep): каждый шаг мира делается из одних и тех же приказов у всех. Приказы игрока
// не выполняются на месте, а уходят хозяину и возвращаются общим ходом kDelay ходов спустя.
class Session {
public:
    static constexpr int kSlots = 7;
    static constexpr int kTurnSteps = 3; // шагов мира в одном ходу (120 мс)
    static constexpr int kDelay = 2;     // через сколько ходов выполняется приказ
    static constexpr std::uint16_t kPort = 34567;

    enum class Role { None, Host, Guest };
    enum class Phase { Lobby, Playing, Closed };

    struct Slot {
        int kind = 1; // 0 — нет игрока, 1 — компьютер, 2 — человек
        std::string name; // имя человека (UTF-8)
        int nation = 0, team = 0, color = 0;
    };
    struct Room {
        std::string map; // карта относительно данных (как у списка миссий)
        int difficulty = 1;
        std::array<Slot, kSlots> slots;
    };

    ~Session() { leave(); }

    // Открыть комнату: хозяин занимает слот 0. false и текст ошибки — порт занят.
    bool host(const std::string& name, std::uint16_t port, std::string& error);
    // Войти в комнату по адресу. false и текст ошибки — не подключились; отказ хозяина придёт позже (lost()).
    bool join(const std::string& name, const std::string& address, std::uint16_t port, std::string& error);
    // Выйти (из комнаты или из игры).
    void leave();

    Role role() const { return role_; }
    Phase phase() const { return phase_; }
    bool active() const { return role_ != Role::None && phase_ != Phase::Closed; }
    // Раз в кадр: приём входящих, чтение сообщений, передача ходов.
    void pump();

    // --- Комната ---
    Room room;      // у хозяина правится на месте, у гостя приходит от хозяина
    int mySlot = 0;
    // Хозяин изменил комнату: разослать.
    void publishRoom();
    // Свои нация/команда/цвет: хозяин применяет сразу, гость шлёт хозяину.
    void setMine(int nation, int team, int color);
    void chat(const std::string& text);
    std::vector<std::string> takeChat();
    // Хозяин: начать игру. Слоты с людьми — kind == 2.
    void start();
    // Гость: игру начал хозяин (возвращается один раз).
    bool takeStarted();
    // Хозяин принял гостя (WELCOME получен).
    bool joined() const { return joined_; }
    std::uint16_t port() const { return listener_ ? listener_->port() : 0; }
    // Когда комната изменилась (получена от хозяина) — один раз.
    bool takeRoomChanged();
    // Биты слотов-людей.
    unsigned humanMask() const;
    // Гость потерял хозяина или хозяин закрыл комнату.
    bool lost() const { return lost_; }
    // Причина закрытия для показа (UTF-8).
    const std::string& lostReason() const { return lostReason_; }

    // --- Игра ---
    // Свой приказ: уйдёт в ближайший ход, выполнится через kDelay ходов.
    void submit(const world::World::Command& c);
    // Готов ли общий ход для выполнения.
    bool turnReady() const { return ready_.count(turn_) > 0; }
    // Начать ход: вернуть приказы всех игроков (слот, приказ), отправить свои на ход turn+kDelay.
    std::vector<std::pair<int, world::World::Command>> beginTurn();
    int turn() const { return turn_; }
    // Контрольная сумма мира после хода turn (каждый kSumEvery-й).
    static constexpr int kSumEvery = 32;
    void reportHash(int turn, std::uint64_t hash);
    // Ход, на котором узлы разошлись (−1 — расхождения не было).
    int desyncTurn() const { return desync_; }
    // Слот отключился в игре.
    bool gone(int slot) const { return slot >= 0 && slot < kSlots && gone_[std::size_t(slot)]; }
    int goneCount() const;

    static std::string encode(const world::World::Command& c);
    static std::optional<world::World::Command> decode(const std::string& line);

private:
    void pumpHost();
    void pumpGuest();
    void handleHostMessage(int slot, const std::string& msg);
    void handleFresh(std::size_t index, const std::string& msg);
    void dropPeer(int slot);
    void broadcast(const std::string& msg);
    void drainTurns();
    bool complete(int turn) const;
    void beginGame();
    std::string roomText() const;
    void applyRoomText(const std::string& text);
    void compareSums(int turn);

    Role role_ = Role::None;
    Phase phase_ = Phase::Lobby;
    std::string name_;
    std::unique_ptr<Listener> listener_;
    std::unique_ptr<Conn> conn_;                   // гость: связь с хозяином
    std::array<std::unique_ptr<Conn>, kSlots> peers_; // хозяин: гости по слотам
    std::vector<std::unique_ptr<Conn>> fresh_;     // подключились, но ещё не представились
    std::vector<std::string> chat_;
    bool started_ = false, roomChanged_ = false, lost_ = false, joined_ = false;
    std::string lostReason_;

    int turn_ = 0;          // следующий выполняемый ход
    int nextBroadcast_ = 0; // хозяин: следующий ход, который соберётся и уйдёт всем
    std::vector<std::string> pending_;
    std::map<int, std::array<std::optional<std::string>, kSlots>> inbox_; // хозяин: приказы по ходам и слотам
    std::map<int, std::string> ready_;                                    // готовые ходы: «слот\tприказ» построчно
    std::array<bool, kSlots> gone_{};
    std::map<int, std::uint64_t> ownSums_;
    std::map<int, std::array<std::optional<std::uint64_t>, kSlots>> sums_;
    int desync_ = -1;
};

} // namespace oc2::net
