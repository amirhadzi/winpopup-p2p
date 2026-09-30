#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace popup {
enum class Connection { Offline, Relay, Direct };
struct Contact {
    uint32_t number = 0;
    std::string publicKey;
    std::string name;
    Connection connection = Connection::Offline;
};
enum class EventType { Network, Contacts, Request, Message, Sent, Receipt, Error, Info };
struct Event {
    EventType type = EventType::Info;
    uint32_t contact = 0;
    uint32_t receipt = 0;
    std::string text;
    std::string key;
    Connection connection = Connection::Offline;
};
struct CoreOptions {
    std::wstring profilePath;
    std::string password;
    std::string name = "WinPopup user";
    bool publicNetwork = true;
    uint16_t startPort = 0;
    uint16_t endPort = 0;
};
// All Tox calls belong to the worker. Methods below are thread safe.
class Core {
public:
    Core();
    ~Core();
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    bool Start(const CoreOptions& options, std::string& error);
    void Stop();
    std::string Address() const;
    std::string SelfName() const;
    std::vector<Contact> Contacts() const;
    std::vector<Event> Poll();
    void AddFriend(const std::string& invitation, const std::string& hello);
    void AcceptFriend(const std::string& publicKey);
    void RemoveFriend(uint32_t number);
    void Send(uint32_t number, const std::string& text);
    void Rename(const std::string& name);
    void RetryBootstrap();
    // Explicit peer bootstrap supports isolated LAN tests; does not disable encryption.
    void Bootstrap(const std::string& host, uint16_t port, const std::string& dhtKey);
    uint16_t UdpPort() const;
    std::string DhtKey() const;
    static bool ValidateInvitation(const std::string& input, std::string& normalized, std::string& error);
    static size_t MaxMessageBytes();
    static size_t MaxRequestBytes();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
