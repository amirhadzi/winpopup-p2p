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
enum class FileDirection { Incoming, Outgoing };
enum class FileState { Offered, Transferring, Paused, Completed, Cancelled, Failed };
struct FileTransfer {
    uint64_t token = 0;
    uint32_t contact = 0;
    std::string publicKey;
    std::string name;
    std::wstring path;
    uint64_t size = 0;
    uint64_t transferred = 0;
    FileDirection direction = FileDirection::Incoming;
    FileState state = FileState::Offered;
    std::string detail;
};
enum class EventType { Network, Contacts, Request, Message, Sent, Receipt, Error, Info, FileOffer, FileProgress, FileFinished };
struct Event {
    EventType type = EventType::Info;
    uint32_t contact = 0;
    uint32_t receipt = 0;
    std::string text;
    std::string key;
    Connection connection = Connection::Offline;
    FileTransfer transfer;
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
    // Local tokens remain unique for this Core object's lifetime. Transfer data
    // is streamed; incoming files stay paused until AcceptFile is called.
    // Files require the WinPopup 0.3 capability handshake on both peers. Text
    // messaging remains compatible with ordinary Tox clients.
    uint64_t SendFile(uint32_t number, const std::wstring& sourcePath, const std::string& expectedPublicKey = {});
    void AcceptFile(uint64_t token, const std::wstring& destination);
    void CancelFile(uint64_t token);
    std::vector<FileTransfer> Transfers() const;
    static uint64_t MaxFileBytes();
    static size_t MaxActiveFiles();
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
