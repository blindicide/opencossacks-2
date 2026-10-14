#include "net/session.hpp"

#include <algorithm>
#include <charconv>

#include "core/version.hpp"

namespace oc2::net {

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> r;
    std::size_t from = 0;
    for (;;) {
        const std::size_t at = s.find(sep, from);
        if (at == std::string::npos) {
            r.push_back(s.substr(from));
            return r;
        }
        r.push_back(s.substr(from, at - from));
        from = at + 1;
    }
}

int toInt(const std::string& s, int fallback = 0) {
    int v = fallback;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

std::string clean(std::string s) { // в сообщениях табуляция и перевод строки — разделители
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

// Заголовок (до первой строки) и тело (после).
void head(const std::string& msg, std::vector<std::string>& fields, std::string& body) {
    const std::size_t nl = msg.find('\n');
    fields = split(msg.substr(0, nl), '\t');
    body = nl == std::string::npos ? std::string() : msg.substr(nl + 1);
}

} // namespace

std::string Session::encode(const world::World::Command& c) {
    std::string r = c.kind;
    for (const int v : c.a) r += '\t' + std::to_string(v);
    return r + '\t' + clean(c.s);
}

std::optional<world::World::Command> Session::decode(const std::string& line) {
    const std::vector<std::string> f = split(line, '\t');
    if (f.size() != 6 || f[0].empty()) return std::nullopt;
    world::World::Command c;
    c.kind = f[0];
    for (std::size_t i = 0; i < 4; ++i) c.a[i] = toInt(f[1 + i]);
    c.s = f[5];
    return c;
}

bool Session::host(const std::string& name, std::uint16_t port, std::string& error) {
    leave();
    listener_ = Listener::open(port);
    if (!listener_) {
        error = "Порт " + std::to_string(port) + " занят";
        return false;
    }
    role_ = Role::Host;
    phase_ = Phase::Lobby;
    name_ = clean(name);
    mySlot = 0;
    room = {};
    room.slots[0] = {2, name_, 0, 0, 0};
    return true;
}

bool Session::join(const std::string& name, const std::string& address, std::uint16_t port, std::string& error) {
    leave();
    conn_ = connectTo(address, port);
    if (!conn_) {
        error = "Не удалось подключиться к " + address;
        return false;
    }
    name_ = clean(name);
    conn_->send("JOIN\t" + std::string(kVersion) + '\t' + name_);
    role_ = Role::Guest; // хозяин ответит WELCOME, FULL или BADVER — разбирает pump()
    phase_ = Phase::Lobby;
    joined_ = false;
    return true;
}

void Session::leave() {
    if (conn_) {
        conn_->send("BYE");
        conn_->pump();
    }
    if (role_ == Role::Host) {
        for (auto& p : peers_)
            if (p) {
                p->send("CLOSE");
                p->pump();
            }
    }
    conn_.reset();
    listener_.reset();
    for (auto& p : peers_) p.reset();
    fresh_.clear();
    role_ = Role::None;
    phase_ = Phase::Lobby;
    started_ = roomChanged_ = lost_ = joined_ = false;
    lostReason_.clear();
    chat_.clear();
    turn_ = nextBroadcast_ = 0;
    pending_.clear();
    inbox_.clear();
    ready_.clear();
    gone_ = {};
    ownSums_.clear();
    sums_.clear();
    desync_ = -1;
}

unsigned Session::humanMask() const {
    unsigned m = 0;
    for (int i = 0; i < kSlots; ++i)
        if (room.slots[std::size_t(i)].kind == 2) m |= 1u << i;
    return m;
}

int Session::goneCount() const {
    return int(std::count(gone_.begin(), gone_.end(), true));
}

std::string Session::roomText() const {
    std::string t = "ROOM\t" + room.map + '\t' + std::to_string(room.difficulty);
    for (int i = 0; i < kSlots; ++i) {
        const Slot& s = room.slots[std::size_t(i)];
        t += "\nslot\t" + std::to_string(i) + '\t' + std::to_string(s.kind) + '\t' + std::to_string(s.nation) + '\t' +
             std::to_string(s.team) + '\t' + std::to_string(s.color) + '\t' + clean(s.name);
    }
    return t;
}

void Session::applyRoomText(const std::string& text) {
    std::vector<std::string> lines = split(text, '\n');
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::vector<std::string> f = split(lines[i], '\t');
        if (f[0] == "ROOM" && f.size() >= 3) {
            room.map = f[1];
            room.difficulty = toInt(f[2], 1);
        } else if (f[0] == "slot" && f.size() >= 7) {
            const int n = toInt(f[1], -1);
            if (n < 0 || n >= kSlots) continue;
            room.slots[std::size_t(n)] = {toInt(f[2], 1), f[6], toInt(f[3]), toInt(f[4]), toInt(f[5])};
        }
    }
    roomChanged_ = true;
}

void Session::broadcast(const std::string& msg) {
    for (auto& p : peers_)
        if (p) p->send(msg);
}

void Session::publishRoom() {
    if (role_ == Role::Host) broadcast(roomText());
}

void Session::setMine(int nation, int team, int color) {
    Slot& s = room.slots[std::size_t(mySlot)];
    if (role_ == Role::Guest) {
        conn_->send("SLOT\t" + std::to_string(nation) + '\t' + std::to_string(team) + '\t' + std::to_string(color));
    } else {
        s.nation = nation, s.team = team, s.color = color;
        publishRoom();
    }
}

void Session::chat(const std::string& text) {
    const std::string t = clean(text);
    if (t.empty()) return;
    if (role_ == Role::Guest) {
        conn_->send("CHAT\t" + t);
    } else if (role_ == Role::Host) {
        const std::string line = name_ + ": " + t;
        chat_.push_back(line);
        broadcast("SAY\t" + line);
    }
}

std::vector<std::string> Session::takeChat() { return std::exchange(chat_, {}); }

bool Session::takeStarted() { return std::exchange(started_, false); }

bool Session::takeRoomChanged() { return std::exchange(roomChanged_, false); }

void Session::start() {
    if (role_ != Role::Host || phase_ != Phase::Lobby) return;
    broadcast(roomText());
    broadcast("START");
    beginGame();
}

void Session::beginGame() {
    phase_ = Phase::Playing;
    turn_ = nextBroadcast_ = 0;
    // Первые kDelay ходов пусты у всех: приказы игроков начинают приходить с хода kDelay.
    for (int t = 0; t < kDelay; ++t) {
        if (role_ == Role::Host) inbox_[t][0] = std::string();
        else conn_->send("TURN\t" + std::to_string(t) + "\n");
    }
    if (role_ == Role::Host) drainTurns();
}

void Session::submit(const world::World::Command& c) { pending_.push_back(encode(c)); }

bool Session::complete(int turn) const {
    const auto it = inbox_.find(turn);
    for (int s = 0; s < kSlots; ++s) {
        if (room.slots[std::size_t(s)].kind != 2 || gone_[std::size_t(s)]) continue;
        if (it == inbox_.end() || !it->second[std::size_t(s)]) return false;
    }
    return true;
}

void Session::drainTurns() {
    while (complete(nextBroadcast_)) {
        std::string body;
        const auto it = inbox_.find(nextBroadcast_);
        if (it != inbox_.end())
            for (int s = 0; s < kSlots; ++s) {
                const auto& p = it->second[std::size_t(s)];
                if (!p || p->empty()) continue;
                for (const std::string& line : split(*p, '\n'))
                    if (!line.empty()) body += std::to_string(s) + '\t' + line + '\n';
            }
        if (it != inbox_.end()) inbox_.erase(it);
        broadcast("TURN\t" + std::to_string(nextBroadcast_) + '\n' + body);
        ready_[nextBroadcast_] = std::move(body);
        ++nextBroadcast_;
    }
}

std::vector<std::pair<int, world::World::Command>> Session::beginTurn() {
    std::vector<std::pair<int, world::World::Command>> out;
    const auto it = ready_.find(turn_);
    if (it == ready_.end()) return out;
    for (const std::string& line : split(it->second, '\n')) {
        const std::size_t tab = line.find('\t');
        if (line.empty() || tab == std::string::npos) continue;
        if (auto c = decode(line.substr(tab + 1))) out.emplace_back(toInt(line.substr(0, tab)), std::move(*c));
    }
    ready_.erase(it);
    // Свои приказы, отданные за этот ход, выполнятся на turn + kDelay.
    std::string packet;
    for (const std::string& p : pending_) packet += p + '\n';
    pending_.clear();
    const int target = turn_ + kDelay;
    if (role_ == Role::Host) {
        inbox_[target][0] = std::move(packet);
        drainTurns();
    } else if (conn_) {
        conn_->send("TURN\t" + std::to_string(target) + '\n' + packet);
    }
    ++turn_;
    return out;
}

void Session::reportHash(int turn, std::uint64_t hash) {
    if (role_ == Role::Host) {
        ownSums_[turn] = hash;
        compareSums(turn);
    } else if (conn_) {
        conn_->send("SUM\t" + std::to_string(turn) + '\t' + std::to_string(hash));
    }
}

void Session::compareSums(int turn) {
    const auto own = ownSums_.find(turn);
    const auto in = sums_.find(turn);
    if (own == ownSums_.end() || in == sums_.end()) return;
    bool all = true;
    for (int s = 1; s < kSlots; ++s) {
        if (room.slots[std::size_t(s)].kind != 2 || gone_[std::size_t(s)]) continue;
        const auto& v = in->second[std::size_t(s)];
        if (!v) {
            all = false;
            continue;
        }
        if (*v != own->second && desync_ < 0) {
            desync_ = turn;
            broadcast("DESYNC\t" + std::to_string(turn));
        }
    }
    if (all) {
        ownSums_.erase(own);
        sums_.erase(in);
    }
}

void Session::dropPeer(int slot) {
    if (!peers_[std::size_t(slot)]) return;
    peers_[std::size_t(slot)].reset();
    if (phase_ == Phase::Lobby) {
        room.slots[std::size_t(slot)] = {};
        publishRoom();
        roomChanged_ = true;
    } else {
        gone_[std::size_t(slot)] = true;
        chat_.push_back(room.slots[std::size_t(slot)].name + " отключился");
        drainTurns();
    }
}

void Session::handleFresh(std::size_t index, const std::string& msg) {
    std::vector<std::string> f;
    std::string body;
    head(msg, f, body);
    Conn& c = *fresh_[index];
    if (f[0] != "JOIN" || f.size() < 3) {
        c.send("BADVER\t?");
        return;
    }
    if (f[1] != kVersion) {
        c.send("BADVER\t" + std::string(kVersion));
        return;
    }
    int slot = -1;
    if (phase_ == Phase::Lobby)
        for (int i = 1; i < kSlots && slot < 0; ++i)
            if (room.slots[std::size_t(i)].kind == 1 && !peers_[std::size_t(i)]) slot = i;
    if (slot < 0) {
        c.send("FULL");
        return;
    }
    peers_[std::size_t(slot)] = std::move(fresh_[index]);
    room.slots[std::size_t(slot)] = {2, clean(f[2]), 0, 0, slot};
    peers_[std::size_t(slot)]->send("WELCOME\t" + std::to_string(slot));
    chat_.push_back(room.slots[std::size_t(slot)].name + " вошёл в комнату");
    publishRoom();
    roomChanged_ = true;
}

void Session::handleHostMessage(int slot, const std::string& msg) {
    std::vector<std::string> f;
    std::string body;
    head(msg, f, body);
    if (f[0] == "SLOT" && f.size() >= 4 && phase_ == Phase::Lobby) {
        Slot& s = room.slots[std::size_t(slot)];
        s.nation = std::clamp(toInt(f[1]), 0, 9);
        s.team = std::clamp(toInt(f[2]), 0, 7);
        s.color = std::clamp(toInt(f[3]), 0, 7);
        publishRoom();
        roomChanged_ = true;
    } else if (f[0] == "CHAT" && f.size() >= 2) {
        const std::string line = room.slots[std::size_t(slot)].name + ": " + clean(f[1]);
        chat_.push_back(line);
        broadcast("SAY\t" + line);
    } else if (f[0] == "TURN" && f.size() >= 2 && phase_ == Phase::Playing) {
        const int t = toInt(f[1], -1);
        if (t >= nextBroadcast_ && t < nextBroadcast_ + 64) {
            inbox_[t][std::size_t(slot)] = body;
            drainTurns();
        }
    } else if (f[0] == "SUM" && f.size() >= 3) {
        const int t = toInt(f[1], -1);
        std::uint64_t h = 0;
        std::from_chars(f[2].data(), f[2].data() + f[2].size(), h);
        sums_[t][std::size_t(slot)] = h;
        compareSums(t);
    } else if (f[0] == "BYE") {
        dropPeer(slot);
    }
}

void Session::pumpHost() {
    while (auto c = listener_ ? listener_->accept() : nullptr) fresh_.push_back(std::move(c));
    for (std::size_t i = 0; i < fresh_.size();) {
        fresh_[i]->pump();
        std::string msg;
        bool drop = fresh_[i]->closed();
        if (!drop && fresh_[i]->poll(msg)) {
            handleFresh(i, msg);
            if (fresh_[i]) fresh_[i]->pump(); // отказ (FULL / BADVER): отправить и закрыть
            drop = true;
        }
        if (drop) fresh_.erase(fresh_.begin() + std::ptrdiff_t(i));
        else ++i;
    }
    for (int s = 1; s < kSlots; ++s) {
        Conn* p = peers_[std::size_t(s)].get();
        if (!p) continue;
        p->pump();
        std::string msg;
        while (peers_[std::size_t(s)] && p->poll(msg)) handleHostMessage(s, msg);
        if (peers_[std::size_t(s)] && p->closed()) dropPeer(s);
    }
}

void Session::pumpGuest() {
    if (!conn_) return;
    conn_->pump();
    std::string msg;
    while (conn_ && conn_->poll(msg)) {
        std::vector<std::string> f;
        std::string body;
        head(msg, f, body);
        if (f[0] == "WELCOME" && f.size() > 1) {
            mySlot = toInt(f[1]);
            joined_ = true;
        } else if (f[0] == "FULL" || f[0] == "BADVER") {
            lost_ = true;
            lostReason_ = f[0] == "FULL" ? "В комнате нет свободных мест"
                                         : "Другая версия игры у хозяина: " + (f.size() > 1 ? f[1] : std::string("?"));
            phase_ = Phase::Closed;
            conn_.reset();
        } else if (f[0] == "ROOM") applyRoomText(msg);
        else if (f[0] == "SAY" && f.size() >= 2) chat_.push_back(f[1]);
        else if (f[0] == "START") {
            started_ = true;
            beginGame();
        } else if (f[0] == "TURN" && f.size() >= 2) ready_[toInt(f[1], -1)] = body;
        else if (f[0] == "DESYNC" && f.size() >= 2) desync_ = toInt(f[1]);
        else if (f[0] == "CLOSE") {
            lost_ = true;
            lostReason_ = "Хозяин закрыл комнату";
            phase_ = Phase::Closed;
            conn_.reset();
        }
    }
    if (conn_ && conn_->closed() && !lost_) {
        lost_ = true;
        lostReason_ = "Связь с хозяином потеряна";
        phase_ = Phase::Closed;
    }
}

void Session::pump() {
    if (role_ == Role::Host) pumpHost();
    else if (role_ == Role::Guest) pumpGuest();
}

} // namespace oc2::net
