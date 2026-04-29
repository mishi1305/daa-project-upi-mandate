#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

struct User {
    int id;
    std::string name;
    std::string email;
    std::string password;
    bool isAdmin;
};

struct Flight {
    std::string id;
    std::string fromCity;
    std::string toCity;
    std::string departureTime;
    double basePrice;
    double currentPrice;
    int seatsAvailable;
};

struct Mandate {
    int userId;
    std::string upiId;
    double limitAmount;
    double remainingAmount;
    long long expiryEpoch;
    bool active;
};

struct Booking {
    std::string id;
    int userId;
    std::string flightId;
    double pricePaid;
    std::string status;
    double refundAmount;
    long long bookedAt;
};

struct MandateHistoryEntry {
    std::string type;
    std::string bookingId;
    double amount;
    double balanceAfter;
    long long createdAt;
};

class TravelPlatform {
public:
    void loadData() {
        loadUsers();
        loadFlights();
        loadMandates();
        loadMandateHistory();
        loadBookings();
        seedDefaults();
    }

    std::string registerUser(const std::string &name, const std::string &email, const std::string &password) {
        for (const auto &u : users) {
            if (u.email == email) {
                return R"({"ok":false,"message":"Email already exists"})";
            }
        }

        User user{nextUserId++, name, email, password, false};
        users.push_back(user);
        saveUsers();
        return R"({"ok":true,"message":"User registered"})";
    }

    std::string login(const std::string &email, const std::string &password, bool adminOnly) {
        for (const auto &u : users) {
            if (u.email == email && u.password == password) {
                if (adminOnly && !u.isAdmin) {
                    return R"({"ok":false,"message":"Not an authorized admin"})";
                }
                std::ostringstream os;
                os << "{\"ok\":true,\"userId\":" << u.id << ",\"name\":\"" << escapeJson(u.name)
                   << "\",\"isAdmin\":" << (u.isAdmin ? "true" : "false") << "}";
                return os.str();
            }
        }
        return R"({"ok":false,"message":"Invalid credentials"})";
    }

    std::string addFlight(const std::string &fromCity, const std::string &toCity, const std::string &departureTime,
                          double basePrice, int seats) {
        Flight f{generateNumericId(usedFlightIds), fromCity, toCity, departureTime, basePrice, basePrice, seats};
        flights.push_back(f);
        saveFlights();
        return R"({"ok":true,"message":"Flight added"})";
    }

    std::string setPricingBounds(double minMultiplier, double maxMultiplier) {
        if (minMultiplier <= 0.0 || maxMultiplier <= 0.0 || minMultiplier > maxMultiplier) {
            return R"({"ok":false,"message":"Invalid pricing bounds"})";
        }
        priceMinMultiplier = minMultiplier;
        priceMaxMultiplier = maxMultiplier;
        return R"({"ok":true,"message":"Pricing mechanism updated"})";
    }

    std::string listFlights() {
        refreshDynamicPrices();
        std::ostringstream os;
        os << "{\"ok\":true,\"flights\":[";
        for (size_t i = 0; i < flights.size(); ++i) {
            if (i) os << ",";
            os << flightJson(flights[i]);
        }
        os << "]}";
        return os.str();
    }

    std::string createMandate(int userId, const std::string &upiId, double limitAmount, int validityMinutes) {
        if (!hasUser(userId)) {
            return R"({"ok":false,"message":"User not found"})";
        }
        if (upiId.empty() || limitAmount <= 0.0 || validityMinutes <= 0) {
            return R"({"ok":false,"message":"Invalid mandate input"})";
        }
        long long expiry = nowEpoch() + static_cast<long long>(validityMinutes) * 60;
        mandates[userId] = Mandate{userId, upiId, limitAmount, limitAmount, expiry, true};
        saveMandates();
        std::ostringstream os;
        os << "{\"ok\":true,\"message\":\"Mandate created\",\"mandate\":" << mandateJson(mandates[userId]) << "}";
        return os.str();
    }

    std::string mandateStatus(int userId) {
        if (!hasUser(userId)) {
            return R"({"ok":false,"message":"User not found"})";
        }
        auto it = mandates.find(userId);
        if (it == mandates.end()) {
            return R"({"ok":false,"message":"No mandate found"})";
        }
        if (it->second.expiryEpoch < nowEpoch()) {
            it->second.active = false;
            saveMandates();
        }
        std::ostringstream os;
        os << "{\"ok\":true,\"mandate\":" << mandateJson(it->second) << ",\"history\":[";
        bool first = true;
        for (const auto &entry : mandateHistory[userId]) {
            if (!first) os << ",";
            first = false;
            os << mandateHistoryJson(entry);
        }
        os << "]}";
        return os.str();
    }

    std::string manualBook(int userId, const std::string &flightId) {
        return bookInternal(userId, flightId, false, 0.0, "");
    }

    std::string autoBook(int userId, const std::string &fromCity, const std::string &toCity, double budget,
                         const std::string &latestTime) {
        refreshDynamicPrices();

        if (!hasUser(userId)) {
            return R"({"ok":false,"message":"User not found"})";
        }

        std::priority_queue<std::pair<double, std::string>, std::vector<std::pair<double, std::string>>,
                            std::greater<std::pair<double, std::string>>>
            minHeap;
        std::string fromNormalized = normalized(fromCity);
        std::string toNormalized = normalized(toCity);
        for (const auto &f : flights) {
            if (normalized(f.fromCity) == fromNormalized && normalized(f.toCity) == toNormalized && f.currentPrice <= budget &&
                f.seatsAvailable > 0 && isTimeLessOrEqual(f.departureTime, latestTime)) {
                minHeap.push({f.currentPrice, f.id});
            }
        }

        if (minHeap.empty()) {
            bool foundAny = false;
            double cheapestWithoutBudget = 0.0;
            for (const auto &f : flights) {
                if (normalized(f.fromCity) == fromNormalized && normalized(f.toCity) == toNormalized && f.seatsAvailable > 0 &&
                    isTimeLessOrEqual(f.departureTime, latestTime)) {
                    if (!foundAny || f.currentPrice < cheapestWithoutBudget) {
                        cheapestWithoutBudget = f.currentPrice;
                        foundAny = true;
                    }
                }
            }
            if (foundAny && budget < cheapestWithoutBudget) {
                return R"({"ok":false,"message":"Budget amount exceeded. Could not proceed. Please update your budget amount"})";
            }
            return R"({"ok":false,"message":"No flights match constraints"})";
        }

        std::string chosenFlightId = minHeap.top().second;
        return bookInternal(userId, chosenFlightId, true, budget, latestTime);
    }

    std::string cheapestForRoute(const std::string &fromCity, const std::string &toCity) {
        refreshDynamicPrices();
        std::priority_queue<std::pair<double, std::string>, std::vector<std::pair<double, std::string>>,
                            std::greater<std::pair<double, std::string>>>
            minHeap;
        std::string fromNormalized = normalized(fromCity);
        std::string toNormalized = normalized(toCity);
        for (const auto &f : flights) {
            if (normalized(f.fromCity) == fromNormalized && normalized(f.toCity) == toNormalized && f.seatsAvailable > 0) {
                minHeap.push({f.currentPrice, f.id});
            }
        }
        if (minHeap.empty()) {
            return R"({"ok":false,"message":"No available flights"})";
        }
        std::string id = minHeap.top().second;
        for (const auto &f : flights) {
            if (f.id == id) {
                std::ostringstream os;
                os << "{\"ok\":true,\"flight\":" << flightJson(f) << "}";
                return os.str();
            }
        }
        return R"({"ok":false,"message":"No available flights"})";
    }

    std::string cancelBooking(const std::string &bookingId) {
        for (auto &b : bookings) {
            if (b.id == bookingId) {
                if (b.status == "CANCELLED") {
                    return R"({"ok":false,"message":"Booking already cancelled"})";
                }
                b.status = "CANCELLED";
                b.refundAmount = b.pricePaid * 0.8;
                for (auto &f : flights) {
                    if (f.id == b.flightId) {
                        f.seatsAvailable += 1;
                    }
                }
                auto mandateIt = mandates.find(b.userId);
                if (mandateIt != mandates.end() && mandateIt->second.active) {
                    mandateIt->second.remainingAmount =
                        std::min(mandateIt->second.limitAmount, mandateIt->second.remainingAmount + b.refundAmount);
                    mandateHistory[b.userId].push_back(
                        MandateHistoryEntry{"REFUND", b.id, b.refundAmount, mandateIt->second.remainingAmount, nowEpoch()});
                    saveMandates();
                    saveMandateHistory();
                }
                saveBookings();
                saveFlights();
                std::ostringstream os;
                os << "{\"ok\":true,\"message\":\"Booking cancelled\",\"refund\":" << std::fixed << std::setprecision(2)
                   << b.refundAmount << "}";
                return os.str();
            }
        }
        return R"({"ok":false,"message":"Booking not found"})";
    }

    std::string listBookings(int userId) {
        std::ostringstream os;
        os << "{\"ok\":true,\"bookings\":[";
        std::vector<Booking> userBookings;
        for (const auto &b : bookings) {
            if (b.userId != userId) continue;
            userBookings.push_back(b);
        }
        std::sort(userBookings.begin(), userBookings.end(),
                  [](const Booking &a, const Booking &b) { return a.bookedAt > b.bookedAt; });
        bool first = true;
        for (const auto &b : userBookings) {
            if (!first) os << ",";
            first = false;
            os << bookingJson(b);
        }
        os << "]}";
        return os.str();
    }

private:
    std::vector<User> users;
    std::vector<Flight> flights;
    std::unordered_map<int, Mandate> mandates;
    std::unordered_map<int, std::vector<MandateHistoryEntry>> mandateHistory;
    std::vector<Booking> bookings;
    std::unordered_map<std::string, bool> flightLocks;
    std::mt19937 rng{std::random_device{}()};
    int nextUserId = 1;
    std::unordered_set<std::string> usedFlightIds;
    std::unordered_set<std::string> usedBookingIds;
    double priceMinMultiplier = 0.9;
    double priceMaxMultiplier = 1.25;

    static std::string escapeJson(const std::string &s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }

    static long long nowEpoch() {
        return static_cast<long long>(std::time(nullptr));
    }

    bool hasUser(int id) const {
        for (const auto &u : users) if (u.id == id) return true;
        return false;
    }

    static std::string normalized(const std::string &value) {
        std::string out;
        for (char c : value) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        return out;
    }

    static bool isTimeLessOrEqual(const std::string &a, const std::string &b) {
        if (b.empty()) return true;
        // format HH:MM
        if (a.size() != 5 || b.size() != 5) return false;
        return a <= b;
    }

    std::string generateNumericId(std::unordered_set<std::string> &usedIds, int length = 8) {
        std::uniform_int_distribution<int> digitDist(0, 9);
        for (int attempt = 0; attempt < 10000; ++attempt) {
            std::string id;
            id.reserve(length);
            for (int i = 0; i < length; ++i) {
                int digit = digitDist(rng);
                if (i == 0 && digit == 0) digit = 1;
                id.push_back(static_cast<char>('0' + digit));
            }
            if (!usedIds.count(id)) {
                usedIds.insert(id);
                return id;
            }
        }
        return std::to_string(nowEpoch());
    }

    void refreshDynamicPrices() {
        std::uniform_real_distribution<double> dist(priceMinMultiplier, priceMaxMultiplier);
        for (auto &f : flights) {
            double multiplier = dist(rng);
            f.currentPrice = std::max(1000.0, f.basePrice * multiplier);
        }
        saveFlights();
    }

    std::string bookInternal(int userId, const std::string &flightId, bool viaAutoBook, double budget, const std::string &latestTime) {
        if (!hasUser(userId)) {
            return R"({"ok":false,"message":"User not found"})";
        }

        Flight *selected = nullptr;
        for (auto &f : flights) {
            if (f.id == flightId) {
                selected = &f;
                break;
            }
        }
        if (!selected) {
            return R"({"ok":false,"message":"Flight not found"})";
        }

        if (viaAutoBook) {
            if (selected->currentPrice > budget || !isTimeLessOrEqual(selected->departureTime, latestTime)) {
                return R"({"ok":false,"message":"Flight no longer matches constraints"})";
            }
        }

        if (selected->seatsAvailable <= 0) {
            return R"({"ok":false,"message":"No seats available"})";
        }
        if (flightLocks[selected->id]) {
            return R"({"ok":false,"message":"Seat temporarily locked, retry"})";
        }
        flightLocks[selected->id] = true;

        auto lockGuard = std::unique_ptr<void, std::function<void(void *)>>(
            nullptr, [&](void *) { flightLocks[selected->id] = false; });

        auto it = mandates.find(userId);
        if (it == mandates.end() || !it->second.active) {
            return R"({"ok":false,"message":"No active mandate found"})";
        }
        Mandate &m = it->second;
        if (m.expiryEpoch < nowEpoch()) {
            m.active = false;
            saveMandates();
            return R"({"ok":false,"message":"Mandate expired"})";
        }
        if (selected->currentPrice > m.remainingAmount) {
            return R"({"ok":false,"message":"Balance account exceeded. Could not proceed"})";
        }

        selected->seatsAvailable -= 1;
        m.remainingAmount -= selected->currentPrice;
        Booking b{generateNumericId(usedBookingIds), userId, selected->id, selected->currentPrice, "CONFIRMED", 0.0, nowEpoch()};
        bookings.push_back(b);
        mandateHistory[userId].push_back(MandateHistoryEntry{"TRANSACTION", b.id, b.pricePaid, m.remainingAmount, nowEpoch()});
        saveFlights();
        saveBookings();
        saveMandates();
        saveMandateHistory();

        std::ostringstream os;
        os << "{\"ok\":true,\"message\":\"Booking confirmed\",\"booking\":" << bookingJson(b)
           << ",\"flight\":" << flightJson(*selected) << "}";
        return os.str();
    }

    std::string flightJson(const Flight &f) const {
        std::ostringstream os;
        os << "{\"id\":\"" << escapeJson(f.id) << "\",\"from\":\"" << escapeJson(f.fromCity) << "\",\"to\":\""
           << escapeJson(f.toCity)
           << "\",\"departure\":\"" << escapeJson(f.departureTime) << "\",\"basePrice\":" << std::fixed
           << std::setprecision(2) << f.basePrice << ",\"currentPrice\":" << std::fixed << std::setprecision(2)
           << f.currentPrice << ",\"seats\":" << f.seatsAvailable << "}";
        return os.str();
    }

    std::string bookingJson(const Booking &b) const {
        std::ostringstream os;
        os << "{\"id\":\"" << escapeJson(b.id) << "\",\"userId\":" << b.userId << ",\"flightId\":\""
           << escapeJson(b.flightId) << "\""
           << ",\"pricePaid\":" << std::fixed << std::setprecision(2) << b.pricePaid << ",\"status\":\"" << b.status
           << "\",\"refund\":" << std::fixed << std::setprecision(2) << b.refundAmount << ",\"bookedAt\":"
           << b.bookedAt << "}";
        return os.str();
    }

    std::string mandateJson(const Mandate &m) const {
        std::ostringstream os;
        os << "{\"userId\":" << m.userId << ",\"upiId\":\"" << escapeJson(m.upiId) << "\",\"limitAmount\":" << std::fixed
           << std::setprecision(2) << m.limitAmount << ",\"remainingAmount\":" << std::fixed << std::setprecision(2)
           << m.remainingAmount << ",\"expiryEpoch\":" << m.expiryEpoch << ",\"active\":"
           << (m.active ? "true" : "false") << "}";
        return os.str();
    }

    std::string mandateHistoryJson(const MandateHistoryEntry &entry) const {
        std::ostringstream os;
        os << "{\"type\":\"" << escapeJson(entry.type) << "\",\"bookingId\":\"" << escapeJson(entry.bookingId)
           << "\",\"amount\":" << std::fixed << std::setprecision(2) << entry.amount << ",\"balanceAfter\":" << std::fixed
           << std::setprecision(2) << entry.balanceAfter << ",\"createdAt\":" << entry.createdAt << "}";
        return os.str();
    }

    static std::string fileLine(const std::vector<std::string> &parts) {
        std::ostringstream os;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) os << "|";
            os << parts[i];
        }
        return os.str();
    }

    static std::vector<std::string> split(const std::string &s, char delim) {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) out.push_back(item);
        return out;
    }

    void loadUsers() {
        users.clear();
        std::ifstream in("data/users.db");
        std::string line;
        while (std::getline(in, line)) {
            auto p = split(line, '|');
            if (p.size() != 5) continue;
            users.push_back(User{std::stoi(p[0]), p[1], p[2], p[3], p[4] == "1"});
            nextUserId = std::max(nextUserId, users.back().id + 1);
        }
    }

    void saveUsers() {
        std::ofstream out("data/users.db", std::ios::trunc);
        for (const auto &u : users) {
            out << fileLine({std::to_string(u.id), u.name, u.email, u.password, u.isAdmin ? "1" : "0"}) << "\n";
        }
    }

    void loadFlights() {
        flights.clear();
        usedFlightIds.clear();
        std::ifstream in("data/flights.db");
        std::string line;
        while (std::getline(in, line)) {
            auto p = split(line, '|');
            if (p.size() != 7) continue;
            flights.push_back(Flight{p[0], p[1], p[2], p[3], std::stod(p[4]), std::stod(p[5]), std::stoi(p[6])});
            usedFlightIds.insert(p[0]);
        }
    }

    void saveFlights() {
        std::ofstream out("data/flights.db", std::ios::trunc);
        for (const auto &f : flights) {
            out << fileLine({f.id, f.fromCity, f.toCity, f.departureTime, toFixed(f.basePrice),
                             toFixed(f.currentPrice), std::to_string(f.seatsAvailable)})
                << "\n";
        }
    }

    void loadMandates() {
        mandates.clear();
        std::ifstream in("data/mandates.db");
        std::string line;
        while (std::getline(in, line)) {
            auto p = split(line, '|');
            if (p.size() == 4) {
                int uid = std::stoi(p[0]);
                double limit = std::stod(p[1]);
                mandates[uid] = Mandate{uid, "", limit, limit, std::stoll(p[2]), p[3] == "1"};
                continue;
            }
            if (p.size() != 6) continue;
            int uid = std::stoi(p[0]);
            mandates[uid] = Mandate{uid, p[1], std::stod(p[2]), std::stod(p[3]), std::stoll(p[4]), p[5] == "1"};
        }
    }

    void saveMandates() {
        std::ofstream out("data/mandates.db", std::ios::trunc);
        for (const auto &kv : mandates) {
            const auto &m = kv.second;
            out << fileLine({std::to_string(m.userId), m.upiId, toFixed(m.limitAmount), toFixed(m.remainingAmount),
                             std::to_string(m.expiryEpoch), m.active ? "1" : "0"})
                << "\n";
        }
    }

    void loadBookings() {
        bookings.clear();
        usedBookingIds.clear();
        std::ifstream in("data/bookings.db");
        std::string line;
        while (std::getline(in, line)) {
            auto p = split(line, '|');
            if (p.size() != 7) continue;
            bookings.push_back(Booking{p[0], std::stoi(p[1]), p[2], std::stod(p[3]), p[4], std::stod(p[5]), std::stoll(p[6])});
            usedBookingIds.insert(p[0]);
        }
    }

    void saveBookings() {
        std::ofstream out("data/bookings.db", std::ios::trunc);
        for (const auto &b : bookings) {
            out << fileLine({b.id, std::to_string(b.userId), b.flightId, toFixed(b.pricePaid), b.status, toFixed(b.refundAmount),
                             std::to_string(b.bookedAt)})
                << "\n";
        }
    }

    void loadMandateHistory() {
        mandateHistory.clear();
        std::ifstream in("data/mandate_history.db");
        std::string line;
        while (std::getline(in, line)) {
            auto p = split(line, '|');
            if (p.size() != 6) continue;
            int userId = std::stoi(p[0]);
            mandateHistory[userId].push_back(
                MandateHistoryEntry{p[1], p[2], std::stod(p[3]), std::stod(p[4]), std::stoll(p[5])});
        }
    }

    void saveMandateHistory() {
        std::ofstream out("data/mandate_history.db", std::ios::trunc);
        for (const auto &entrySet : mandateHistory) {
            int userId = entrySet.first;
            for (const auto &entry : entrySet.second) {
                out << fileLine({std::to_string(userId), entry.type, entry.bookingId, toFixed(entry.amount),
                                 toFixed(entry.balanceAfter), std::to_string(entry.createdAt)})
                    << "\n";
            }
        }
    }

    static std::string toFixed(double v) {
        std::ostringstream os;
        os << std::fixed << std::setprecision(2) << v;
        return os.str();
    }

    void seedDefaults() {
        bool hasAdmin = false;
        for (const auto &u : users) if (u.isAdmin) hasAdmin = true;
        if (!hasAdmin) {
            users.push_back(User{nextUserId++, "Admin", "admin@flexiprice.com", "admin123", true});
            saveUsers();
        }
        if (flights.empty()) {
            flights.push_back(Flight{generateNumericId(usedFlightIds), "Delhi", "Mumbai", "09:30", 5200, 5200, 25});
            flights.push_back(Flight{generateNumericId(usedFlightIds), "Delhi", "Bangalore", "13:45", 6900, 6900, 18});
            flights.push_back(Flight{generateNumericId(usedFlightIds), "Mumbai", "Kolkata", "17:10", 6100, 6100, 20});
            flights.push_back(Flight{generateNumericId(usedFlightIds), "Delhi", "Mumbai", "20:00", 5600, 5600, 15});
            saveFlights();
        }
    }
};

static std::string readBody(const std::string &request) {
    size_t pos = request.find("\r\n\r\n");
    if (pos != std::string::npos) return request.substr(pos + 4);
    pos = request.find("\n\n");
    if (pos != std::string::npos) return request.substr(pos + 2);
    return "";
}

static std::string firstLine(const std::string &request) {
    size_t pos = request.find("\r\n");
    if (pos == std::string::npos) return request;
    return request.substr(0, pos);
}

static std::string jsonString(const std::string &body, const std::string &key) {
    std::regex rx("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (std::regex_search(body, m, rx)) return m[1].str();
    return "";
}

static int jsonInt(const std::string &body, const std::string &key, int fallback = 0) {
    std::regex rx("\"" + key + "\"\\s*:\\s*\"?(-?\\d+)\"?");
    std::smatch m;
    if (std::regex_search(body, m, rx)) return std::stoi(m[1].str());
    return fallback;
}

static double jsonDouble(const std::string &body, const std::string &key, double fallback = 0.0) {
    std::regex rx("\"" + key + "\"\\s*:\\s*\"?(-?\\d+(?:\\.\\d+)?)\"?");
    std::smatch m;
    if (std::regex_search(body, m, rx)) return std::stod(m[1].str());
    return fallback;
}

static std::string jsonNumericString(const std::string &body, const std::string &key) {
    std::regex quotedRx("\"" + key + "\"\\s*:\\s*\"(\\d+)\"");
    std::smatch m;
    if (std::regex_search(body, m, quotedRx)) return m[1].str();
    std::regex rawRx("\"" + key + "\"\\s*:\\s*(\\d+)");
    if (std::regex_search(body, m, rawRx)) return m[1].str();
    return "";
}

static std::string urlDecode(const std::string &value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            std::string hex = value.substr(i + 1, 2);
            char ch = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            decoded.push_back(ch);
            i += 2;
        } else if (value[i] == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(value[i]);
        }
    }
    return decoded;
}

static std::string httpResponse(const std::string &body, int status = 200, const std::string &statusText = "OK") {
    std::ostringstream os;
    os << "HTTP/1.1 " << status << " " << statusText << "\r\n";
    os << "Content-Type: application/json\r\n";
    os << "Access-Control-Allow-Origin: *\r\n";
    os << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    os << "Access-Control-Allow-Headers: Content-Type\r\n";
    os << "Content-Length: " << body.size() << "\r\n";
    os << "Connection: close\r\n\r\n";
    os << body;
    return os.str();
}

int main() {
    TravelPlatform app;
    app.loadData();

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed on port 8080\n";
        closesocket(server);
        WSACleanup();
        return 1;
    }

    if (listen(server, 10) == SOCKET_ERROR) {
        std::cerr << "Listen failed\n";
        closesocket(server);
        WSACleanup();
        return 1;
    }

    std::cout << "Backend running on http://localhost:8080\n";

    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        char buffer[16384];
        int received = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            closesocket(client);
            continue;
        }
        buffer[received] = '\0';
        std::string request(buffer);
        std::string line = firstLine(request);
        std::string body = readBody(request);
        std::string out;

        if (line.find("OPTIONS ") == 0) {
            out = httpResponse(R"({"ok":true})");
        } else if (line.find("POST /api/register ") == 0) {
            out = httpResponse(app.registerUser(jsonString(body, "name"), jsonString(body, "email"), jsonString(body, "password")));
        } else if (line.find("POST /api/login ") == 0) {
            out = httpResponse(app.login(jsonString(body, "email"), jsonString(body, "password"), false));
        } else if (line.find("POST /api/admin/login ") == 0) {
            out = httpResponse(app.login(jsonString(body, "email"), jsonString(body, "password"), true));
        } else if (line.find("GET /api/flights ") == 0) {
            out = httpResponse(app.listFlights());
        } else if (line.find("POST /api/admin/flights ") == 0) {
            out = httpResponse(app.addFlight(jsonString(body, "from"), jsonString(body, "to"), jsonString(body, "departure"),
                                             jsonDouble(body, "basePrice"), jsonInt(body, "seats")));
        } else if (line.find("POST /api/admin/pricing ") == 0) {
            out = httpResponse(app.setPricingBounds(jsonDouble(body, "minMultiplier"), jsonDouble(body, "maxMultiplier")));
        } else if (line.find("POST /api/mandate ") == 0) {
            std::string uidStr = jsonNumericString(body, "userId");
            int uid = uidStr.empty() ? 0 : std::stoi(uidStr);
            out = httpResponse(app.createMandate(uid, jsonString(body, "upiId"), jsonDouble(body, "limitAmount"),
                                                 jsonInt(body, "validityMinutes")));
        } else if (line.find("GET /api/mandate?userId=") == 0) {
            size_t pos = line.find("userId=");
            int userId = 0;
            if (pos != std::string::npos) {
                std::string num;
                for (size_t i = pos + 7; i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])); ++i) num.push_back(line[i]);
                if (!num.empty()) userId = std::stoi(num);
            }
            out = httpResponse(app.mandateStatus(userId));
        } else if (line.find("POST /api/book ") == 0) {
            out = httpResponse(app.manualBook(jsonInt(body, "userId"), jsonNumericString(body, "flightId")));
        } else if (line.find("POST /api/autobook ") == 0) {
            out = httpResponse(app.autoBook(jsonInt(body, "userId"), jsonString(body, "from"), jsonString(body, "to"),
                                            jsonDouble(body, "budget"), jsonString(body, "latestTime")));
        } else if (line.find("POST /api/cancel ") == 0) {
            out = httpResponse(app.cancelBooking(jsonNumericString(body, "bookingId")));
        } else if (line.find("GET /api/bookings?userId=") == 0) {
            size_t pos = line.find("userId=");
            int userId = 0;
            if (pos != std::string::npos) {
                std::string num;
                for (size_t i = pos + 7; i < line.size() && std::isdigit(line[i]); ++i) num.push_back(line[i]);
                if (!num.empty()) userId = std::stoi(num);
            }
            out = httpResponse(app.listBookings(userId));
        } else if (line.find("GET /api/cheapest?") == 0) {
            std::regex fromRx("from=([^& ]+)");
            std::regex toRx("to=([^& ]+)");
            std::smatch m1, m2;
            std::string from, to;
            if (std::regex_search(line, m1, fromRx)) from = urlDecode(m1[1].str());
            if (std::regex_search(line, m2, toRx)) to = urlDecode(m2[1].str());
            out = httpResponse(app.cheapestForRoute(from, to));
        } else {
            out = httpResponse(R"({"ok":false,"message":"Route not found"})", 404, "Not Found");
        }

        send(client, out.c_str(), static_cast<int>(out.size()), 0);
        closesocket(client);
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
