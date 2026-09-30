// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "core.h"
#include <tox/tox.h>
#include <tox/toxencryptsave.h>
#include <sodium.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace popup {
namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t MaxProfileBytes = 64 * 1024 * 1024;
constexpr uint64_t FileSizeLimit = uint64_t{2} * 1024 * 1024 * 1024;
constexpr size_t ActiveFileLimit = 8;
constexpr size_t TransferHistoryLimit = 128;
constexpr std::array<uint8_t, 5> FileProtocol = {160, 'W', 'P', 'F', 1};
constexpr auto FileHandshakeTimeout = std::chrono::seconds(10);
constexpr auto FileControlTimeout = std::chrono::seconds(30);

bool Finished(FileState state) {
    return state == FileState::Completed || state == FileState::Cancelled || state == FileState::Failed;
}

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { Reset(); }
    void Reset(HANDLE next = INVALID_HANDLE_VALUE) {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = next;
    }
};

struct SecretBytes {
    std::vector<uint8_t> bytes;
    ~SecretBytes() { if (!bytes.empty()) sodium_memzero(bytes.data(), bytes.size()); }
};

struct PasswordWiper {
    std::string& password;
    ~PasswordWiper() {
        if (!password.empty()) sodium_memzero(password.data(), password.size());
        password.clear();
    }
};

std::string Hex(const uint8_t* bytes, size_t length) {
    static constexpr char alphabet[] = "0123456789ABCDEF";
    std::string result(length * 2, '0');
    for (size_t i = 0; i < length; ++i) {
        result[2 * i] = alphabet[bytes[i] >> 4];
        result[2 * i + 1] = alphabet[bytes[i] & 15];
    }
    return result;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool Decode(const std::string& input, uint8_t* output, size_t bytes) {
    if (input.size() != bytes * 2) return false;
    for (size_t i = 0; i < bytes; ++i) {
        int hi = HexDigit(input[2 * i]), lo = HexDigit(input[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        output[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::string Trim(const std::string& input) {
    size_t first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    size_t last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}

Connection Convert(Tox_Connection connection) {
    switch (connection) {
    case TOX_CONNECTION_UDP: return Connection::Direct;
    case TOX_CONNECTION_TCP: return Connection::Relay;
    default: return Connection::Offline;
    }
}

// Network-provided strings must not embed a NUL that hides their suffix in Win32.
std::string DisplayText(const uint8_t* bytes, size_t length) {
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length;) {
        uint8_t c = bytes[i];
        if (c < 128) {
            if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127)
                result.append("\xEF\xBF\xBD");
            else result.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        size_t count = c >= 0xC2 && c <= 0xDF ? 2 : c >= 0xE0 && c <= 0xEF ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
        bool valid = count && i + count <= length;
        if (valid) {
            for (size_t j = 1; j < count; ++j) if ((bytes[i + j] & 0xC0) != 0x80) valid = false;
            if (c == 0xE0 && bytes[i + 1] < 0xA0) valid = false;
            if (c == 0xED && bytes[i + 1] >= 0xA0) valid = false;
            if (c == 0xF0 && bytes[i + 1] < 0x90) valid = false;
            if (c == 0xF4 && bytes[i + 1] >= 0x90) valid = false;
        }
        if (!valid) {
            result.append("\xEF\xBF\xBD");
            ++i;
        } else {
            result.append(reinterpret_cast<const char*>(bytes + i), count);
            i += count;
        }
    }
    return result;
}

std::string SystemError(const std::string& operation) {
    return operation + " (Windows error " + std::to_string(GetLastError()) + ").";
}

bool ReservedFileName(const std::string& name) {
    auto stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    for (char& c : stem) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CLOCK$" ||
        stem == "CONIN$" || stem == "CONOUT$" ||
        (stem.size() == 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT") && stem[3] >= '1' && stem[3] <= '9') ||
        (stem.size() == 5 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT") &&
            static_cast<uint8_t>(stem[3]) == 0xC2 && (static_cast<uint8_t>(stem[4]) == 0xB9 ||
            static_cast<uint8_t>(stem[4]) == 0xB2 || static_cast<uint8_t>(stem[4]) == 0xB3));
}

std::string SafeFileName(const uint8_t* bytes, size_t length) {
    std::string name = DisplayText(bytes, std::min(length, size_t{TOX_MAX_FILENAME_LENGTH}));
    auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    for (char& c : name) if (static_cast<unsigned char>(c) < 32 || std::string("<>:\"|?*").find(c) != std::string::npos) c = '_';
    // Remove Unicode directional overrides from a suggested untrusted filename.
    for (size_t i = 0; i + 2 < name.size();) {
        auto a = static_cast<uint8_t>(name[i]), b = static_cast<uint8_t>(name[i + 1]), c = static_cast<uint8_t>(name[i + 2]);
        if (a == 0xE2 && ((b == 0x80 && c >= 0xAA && c <= 0xAE) || (b == 0x81 && c >= 0xA6 && c <= 0xA9))) name.replace(i, 3, "_");
        else ++i;
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (name.empty()) name = "received-file";
    if (name.size() > 200) {
        size_t count = 200;
        while (count && (static_cast<uint8_t>(name[count]) & 0xC0) == 0x80) --count;
        name.resize(count);
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (ReservedFileName(name)) name.insert(0, "_");
    return name;
}

std::string FriendAddError(Tox_Err_Friend_Add error) {
    switch (error) {
    case TOX_ERR_FRIEND_ADD_TOO_LONG: return "The invitation note is too long.";
    case TOX_ERR_FRIEND_ADD_NO_MESSAGE: return "An invitation needs a short note.";
    case TOX_ERR_FRIEND_ADD_OWN_KEY: return "That is your own Tox ID.";
    case TOX_ERR_FRIEND_ADD_ALREADY_SENT: return "This person is already in your contacts or has a pending invitation.";
    case TOX_ERR_FRIEND_ADD_BAD_CHECKSUM: return "The Tox ID checksum is invalid. Copy the complete ID again.";
    case TOX_ERR_FRIEND_ADD_MALLOC: return "There is not enough memory to add this contact.";
    default: return "Could not add the contact (Tox error " + std::to_string(error) + ").";
    }
}

struct BootstrapNode {
    const char* host;
    uint16_t udpPort;
    const char* key;
    std::array<uint16_t, 3> tcpPorts;
};

// Pinned public node keys from https://nodes.tox.chat/, checked 2026-09-30.
// These nodes discover peers and relay encrypted packets; they have no chat keys.
constexpr BootstrapNode PublicNodes[] = {
    {"tox1.mf-net.eu", 33445, "B3E5FA80DC8EBD1149AD2AB35ED8B85BD546DEDE261CA593234C619249419506", {3389, 33445, 0}},
    {"tox2.mf-net.eu", 33445, "70EA214FDE161E7432530605213F18F7427DC773E276B3E317A07531F548545F", {3389, 33445, 0}},
    {"tox.initramfs.io", 33445, "3F0A45A268367C1BEA652F258C85F4A66DA76BCAA667A49E770BCC4917AB6A25", {33445, 3389, 0}},
    {"144.172.88.203", 33445, "2016A0F2797EE3A8B004BA623F11AAFC8146F1B8F45107232A1A1AECCE856674", {443, 33445, 0}},
    {"172.104.215.182", 33445, "DA2BD927E01CD05EBCC2574EBE5BEBB10FF59AE0B2105A7D1E2B40E49BB20239", {33445, 443, 3389}},
};
} // namespace

struct Core::Impl {
    enum class Operation { Add, Accept, Remove, Send, Rename, Retry, Bootstrap, SendFile, AcceptFile, CancelFile };
    struct Command {
        Operation op;
        uint32_t contact = 0;
        std::string text;
        std::string other;
        uint16_t port = 0;
        uint64_t token = 0;
        std::wstring path;
    };

    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::condition_variable wake;
    std::thread worker;
    std::deque<Command> commands;
    std::vector<Event> events;
    std::vector<Contact> contacts;
    std::vector<FileTransfer> transfers;
    uint64_t nextTransferToken = 1;
    std::string address;
    std::string selfName;
    std::string dhtKey;
    uint16_t udpPort = 0;
    bool running = false;
    bool stopping = false;

    // Worker-owned state: no caller thread reads any of these fields.
    Tox* tox = nullptr;
    Tox_Pass_Key* saveKey = nullptr;
    Handle profileLock;
    std::filesystem::path profilePath;
    bool publicNetwork = true;
    bool dirty = false;
    bool callbackFailed = false;
    bool contactsChanged = false;
    Connection network = Connection::Offline;
    Clock::time_point lastBootstrap{};
    Clock::time_point lastSave{};
    std::string lastSaveError;
    struct ActiveFile {
        FileTransfer info;
        uint32_t fileNumber = 0;
        bool bound = false;
        Handle handle;
        std::filesystem::path partial;
        bool ownsPartial = false;
        Clock::time_point reported{};
        std::array<uint8_t, TOX_FILE_ID_LENGTH> id{};
        Clock::time_point deadline{};
        bool cancelRequested = false;
        bool controlSent = false;
        bool wireFinished = false;
        bool receiverDone = false;
        ~ActiveFile() {
            handle.Reset();
            if (ownsPartial) DeleteFileW(partial.c_str());
        }
    };
    std::map<uint64_t, std::unique_ptr<ActiveFile>> activeFiles;
    std::map<std::pair<uint32_t, uint32_t>, uint64_t> fileNumbers;
    struct FilePeer {
        bool capable = false;
        bool blocked = false;
        bool helloReply = false;
        Clock::time_point lastHello{};
    };
    std::map<uint32_t, FilePeer> filePeers;

    void PushLocked(Event event) {
        if (event.type == EventType::FileProgress || event.type == EventType::FileFinished) {
            events.erase(std::remove_if(events.begin(), events.end(), [&](const Event& old) {
                return old.type == EventType::FileProgress && old.transfer.token == event.transfer.token;
            }), events.end());
        }
        // Coalesce snapshots and retransmitted invitations while the UI is busy.
        if (event.type == EventType::Contacts || event.type == EventType::Network || event.type == EventType::Request) {
            auto found = std::find_if(events.begin(), events.end(), [&](const Event& old) {
                return old.type == event.type && (event.type != EventType::Request || old.key == event.key);
            });
            if (found != events.end()) { *found = std::move(event); return; }
        }
        if (events.size() >= 4095) {
            if (events.size() == 4095) {
                Event warning;
                warning.type = EventType::Error;
                warning.text = "The event queue is full. The network has stopped to protect memory; some incoming events could not be displayed. Restart WinPopup to reconnect.";
                events.push_back(std::move(warning));
            }
            stopping = true;
            wake.notify_all();
            return;
        }
        events.push_back(std::move(event));
    }

    void Push(Event event) {
        std::lock_guard<std::mutex> lock(mutex);
        PushLocked(std::move(event));
    }

    void Error(const std::string& text, uint32_t contact = 0, bool sendFailure = false) {
        Event event;
        event.type = EventType::Error;
        event.contact = contact;
        event.text = text;
        if (sendFailure) event.key = "send";
        Push(std::move(event));
    }

    void SafeError(const char* text) noexcept {
        try { Error(text); } catch (...) {}
    }

    uint64_t NewTransferToken() {
        std::lock_guard<std::mutex> lock(mutex);
        if (nextTransferToken == 0) throw std::runtime_error("The transfer token space is exhausted.");
        return nextTransferToken++;
    }

    void PublishTransferLocked(const FileTransfer& transfer, EventType type) {
        auto found = std::find_if(transfers.begin(), transfers.end(), [&](const FileTransfer& old) { return old.token == transfer.token; });
        if (found == transfers.end()) transfers.push_back(transfer); else *found = transfer;
        while (transfers.size() > TransferHistoryLimit) {
            auto oldest = std::find_if(transfers.begin(), transfers.end(), [](const FileTransfer& old) { return Finished(old.state); });
            if (oldest == transfers.end()) break;
            transfers.erase(oldest);
        }
        Event event;
        event.type = type;
        event.contact = transfer.contact;
        event.text = transfer.detail;
        event.transfer = transfer;
        PushLocked(std::move(event));
    }

    void PublishTransfer(ActiveFile& file, EventType type, bool force = true) {
        const auto now = Clock::now();
        if (!force && now - file.reported < std::chrono::milliseconds(150)) return;
        file.reported = now;
        std::lock_guard<std::mutex> lock(mutex);
        PublishTransferLocked(file.info, type);
    }

    void TransferCommandStatus(uint64_t token) {
        std::lock_guard<std::mutex> lock(mutex);
        auto found = std::find_if(transfers.begin(), transfers.end(), [=](const FileTransfer& old) { return old.token == token; });
        if (found != transfers.end()) {
            auto snapshot = *found;
            PublishTransferLocked(snapshot, Finished(snapshot.state) ? EventType::FileFinished : EventType::FileProgress);
        } else {
            Event event;
            event.type = EventType::Error;
            event.key = "file";
            event.text = "This file transfer is no longer available.";
            PushLocked(std::move(event));
        }
    }

    void Enqueue(Command command) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!running || stopping || commands.size() >= 1024) {
            if (command.op == Operation::SendFile) {
                FileTransfer transfer;
                transfer.token = command.token;
                transfer.contact = command.contact;
                transfer.path = command.path;
                transfer.direction = FileDirection::Outgoing;
                transfer.state = FileState::Failed;
                transfer.detail = "The messenger is not ready or is busy. Please try again.";
                PublishTransferLocked(transfer, EventType::FileFinished);
                return;
            }
            Event event;
            event.type = EventType::Error;
            event.contact = command.contact;
            event.text = "The messenger is not ready or is busy. Please try again.";
            if (command.op == Operation::Send) event.key = "send";
            if (command.op == Operation::AcceptFile || command.op == Operation::CancelFile) event.key = "file";
            PushLocked(std::move(event));
            return;
        }
        commands.push_back(std::move(command));
        wake.notify_one();
    }

    void RefreshContacts() {
        std::vector<Tox_Friend_Number> numbers(tox_self_get_friend_list_size(tox));
        if (!numbers.empty()) tox_self_get_friend_list(tox, numbers.data());
        std::vector<Contact> next;
        next.reserve(numbers.size());
        for (auto number : numbers) {
            Contact contact;
            contact.number = number;
            Tox_Public_Key key;
            if (!tox_friend_get_public_key(tox, number, key, nullptr)) continue;
            contact.publicKey = Hex(key, sizeof key);
            Tox_Err_Friend_Query error;
            size_t length = tox_friend_get_name_size(tox, number, &error);
            if (error == TOX_ERR_FRIEND_QUERY_OK && length > 0 && length <= TOX_MAX_NAME_LENGTH) {
                std::vector<uint8_t> name(length);
                if (tox_friend_get_name(tox, number, name.data(), &error))
                    contact.name = DisplayText(name.data(), name.size());
            }
            if (contact.name.empty()) contact.name = "Contact " + contact.publicKey.substr(0, 8);
            contact.connection = Convert(tox_friend_get_connection_status(tox, number, &error));
            next.push_back(std::move(contact));
        }
        std::lock_guard<std::mutex> lock(mutex);
        contacts = std::move(next);
        Event event;
        event.type = EventType::Contacts;
        PushLocked(std::move(event));
    }

    ActiveFile* FindFile(uint32_t contact, uint32_t number, FileDirection* direction = nullptr) {
        auto mapped = fileNumbers.find({contact, number});
        if (mapped == fileNumbers.end()) return nullptr;
        auto file = activeFiles.find(mapped->second);
        if (file == activeFiles.end() || (direction && file->second->info.direction != *direction)) return nullptr;
        return file->second.get();
    }

    void EraseFile(uint64_t token) {
        auto found = activeFiles.find(token);
        if (found == activeFiles.end()) return;
        if (found->second->bound) {
            auto mapped = fileNumbers.find({found->second->info.contact, found->second->fileNumber});
            if (mapped != fileNumbers.end() && mapped->second == token) fileNumbers.erase(mapped);
        }
        activeFiles.erase(found);
    }

    bool SendFileProtocol(uint32_t contact, uint8_t operation, const uint8_t* id = nullptr) {
        std::array<uint8_t, 6 + TOX_FILE_ID_LENGTH> packet{};
        std::copy(FileProtocol.begin(), FileProtocol.end(), packet.begin());
        packet[5] = operation;
        if (id) std::copy_n(id, TOX_FILE_ID_LENGTH, packet.begin() + 6);
        return tox_friend_send_lossless_packet(tox, contact, packet.data(), id ? packet.size() : size_t{6}, nullptr);
    }

    void FinishFile(uint64_t token, FileState state, const std::string& detail, bool notifyPeer, bool force = false) {
        auto found = activeFiles.find(token);
        if (found == activeFiles.end()) return;
        auto& file = *found->second;
        if (Finished(file.info.state)) {
            if (force) { EraseFile(token); return; }
            if (!notifyPeer) file.wireFinished = true;
            return;
        }
        file.handle.Reset();
        file.info.state = state;
        file.info.detail = detail;
        if (file.ownsPartial) {
            if (DeleteFileW(file.partial.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) file.ownsPartial = false;
            else file.info.detail += " The partial file could not be removed: close other applications and delete the .winpopup-part file manually.";
        }
        PublishTransfer(file, EventType::FileFinished);
        if (!file.bound || force) { EraseFile(token); return; }
        file.deadline = Clock::now() + FileControlTimeout;
        file.cancelRequested = notifyPeer;
        file.wireFinished = !notifyPeer;
        // Only the receiver emits native CANCEL. Outgoing slots cannot be reused
        // until its ordered Done(file-ID) fence drains all old receiver controls.
        // Retaining an app record alone is insufficient: Tox can free a sender
        // slot automatically after the last data packet is transport-acknowledged.
    }

    void StopFilesForContact(uint32_t contact, const std::string& reason, bool notifyPeer) {
        std::vector<uint64_t> tokens;
        for (const auto& item : activeFiles) if (item.second->info.contact == contact) tokens.push_back(item.first);
        for (auto token : tokens) FinishFile(token, FileState::Failed, reason, notifyPeer, true);
        filePeers.erase(contact);
    }

    void StopAllFiles(const std::string& reason) {
        while (!activeFiles.empty()) FinishFile(activeFiles.begin()->first, FileState::Cancelled, reason, false, true);
        filePeers.clear();
    }

    void FileProtocolPacket(uint32_t contact, const uint8_t* data, size_t length) {
        if (length < 6 || !std::equal(FileProtocol.begin(), FileProtocol.end(), data)) return;
        auto& peer = filePeers[contact];
        if ((data[5] == 1 || data[5] == 2) && length == 6) {
            peer.capable = true;
            if (data[5] == 1) peer.helloReply = true;
            return;
        }
        if (!peer.capable || length != 6 + TOX_FILE_ID_LENGTH) return;
        if (data[5] == 3) {
            // An old ID can never cancel a newly recycled numeric file slot.
            for (const auto& item : activeFiles) {
                auto& file = *item.second;
                if (file.info.contact == contact && file.info.direction == FileDirection::Incoming &&
                    std::equal(file.id.begin(), file.id.end(), data + 6)) {
                    if (!Finished(file.info.state)) FinishFile(item.first, FileState::Cancelled, "The other person cancelled the transfer.", true);
                    return;
                }
            }
            // Its original Done was already sent, or this is an irrelevant ID.
            // A best-effort duplicate Done is harmless; never send native CANCEL.
            SendFileProtocol(contact, 4, data + 6);
        } else if (data[5] == 4) {
            for (const auto& item : activeFiles) {
                auto& file = *item.second;
                if (file.info.contact == contact && file.info.direction == FileDirection::Outgoing &&
                    std::equal(file.id.begin(), file.id.end(), data + 6)) {
                    file.receiverDone = true;
                    return;
                }
            }
        }
    }

    void PumpFiles() {
        const auto now = Clock::now();
        for (auto& item : filePeers) {
            auto& peer = item.second;
            if (peer.helloReply && SendFileProtocol(item.first, 2)) peer.helloReply = false;
        }
        std::vector<uint64_t> erase;
        for (auto& item : activeFiles) {
            auto& file = *item.second;
            if (!Finished(file.info.state) || !file.bound) continue;
            auto& peer = filePeers[file.info.contact];
            if (file.info.direction == FileDirection::Incoming) {
                if (!file.wireFinished) {
                    Tox_Err_File_Control error;
                    if (tox_file_control(tox, file.info.contact, file.fileNumber, TOX_FILE_CONTROL_CANCEL, &error) ||
                        error == TOX_ERR_FILE_CONTROL_NOT_FOUND) file.wireFinished = true;
                }
                // Called only after tox_iterate has returned. Natural completion
                // has now freed its receiver slot; CANCEL was queued before Done.
                if (file.wireFinished && (!peer.capable || SendFileProtocol(file.info.contact, 4, file.id.data()))) {
                    erase.push_back(item.first);
                    continue;
                }
            } else {
                if (file.cancelRequested && !file.controlSent)
                    file.controlSent = SendFileProtocol(file.info.contact, 3, file.id.data());
                if (file.wireFinished && file.receiverDone) {
                    erase.push_back(item.first);
                    continue;
                }
            }
            if (!peer.blocked && now >= file.deadline) {
                peer.blocked = true;
                file.info.detail += " Safe transfer cleanup was not acknowledged. Reconnect this peer before sending more files; text chat remains available.";
                PublishTransfer(file, EventType::FileFinished);
            }
        }
        for (auto token : erase) EraseFile(token);

        // Starting native offers only here (outside Tox callbacks) keeps slot
        // reuse ordered after both Tox termination and the peer's Done fence.
        std::vector<uint64_t> pending;
        for (const auto& item : activeFiles) if (!item.second->bound && !Finished(item.second->info.state)) pending.push_back(item.first);
        for (auto token : pending) {
            auto found = activeFiles.find(token);
            if (found == activeFiles.end()) continue;
            auto& file = *found->second;
            auto& peer = filePeers[file.info.contact];
            if (peer.blocked) {
                FinishFile(token, FileState::Failed, "File transfers are blocked until this peer reconnects because safe cleanup was not acknowledged. Text chat remains available.", false);
                continue;
            }
            if (!peer.capable) {
                if (now - peer.lastHello >= std::chrono::seconds(1)) {
                    if (SendFileProtocol(file.info.contact, 1)) peer.lastHello = now;
                }
                if (now >= file.deadline)
                    FinishFile(token, FileState::Failed, "File sharing requires WinPopup 0.3 or later at both ends. The peer did not confirm support; text chat is still available.", false);
                continue;
            }
            bool fenced = false;
            for (const auto& other : activeFiles) {
                if (other.second->bound && other.second->info.contact == file.info.contact &&
                    other.second->info.direction == FileDirection::Outgoing && Finished(other.second->info.state)) { fenced = true; break; }
            }
            if (fenced || peer.helloReply) continue;
            Tox_Err_File_Send error;
            const auto& name = file.info.name;
            uint32_t number = tox_file_send(tox, file.info.contact, TOX_FILE_KIND_DATA, file.info.size, file.id.data(),
                reinterpret_cast<const uint8_t*>(name.data()), name.size(), &error);
            if (error != TOX_ERR_FILE_SEND_OK) {
                FinishFile(token, FileState::Failed, "Could not offer the file (Tox error " + std::to_string(error) + ").", false);
                continue;
            }
            file.fileNumber = number;
            file.bound = true;
            if (fileNumbers.count({file.info.contact, number})) {
                peer.blocked = true;
                FinishFile(token, FileState::Failed, "The network reused an active identifier. Reconnect this peer before sending more files.", true);
                continue;
            }
            fileNumbers[{file.info.contact, number}] = token;
            file.info.detail = "Waiting for the other person to accept the file.";
            PublishTransfer(file, EventType::FileProgress);
        }
    }

    void BeginSendFile(const Command& command) {
        auto file = std::make_unique<ActiveFile>();
        file->info.token = command.token;
        file->info.contact = command.contact;
        file->info.direction = FileDirection::Outgoing;
        file->info.path = command.path;
        file->info.publicKey = command.other;
        auto* current = file.get();
        activeFiles.emplace(command.token, std::move(file));
        auto fail = [&](const std::string& message) { FinishFile(command.token, FileState::Failed, message, true); };
        if (activeFiles.size() > ActiveFileLimit) { fail("At most 8 file transfers can be active at once."); return; }
        Tox_Public_Key key;
        if (!tox_friend_get_public_key(tox, command.contact, key, nullptr) || command.other.empty() || Hex(key, sizeof key) != command.other) {
            fail("File not sent: the selected contact changed or no longer exists. Select the contact again."); return;
        }
        if (tox_friend_get_connection_status(tox, command.contact, nullptr) == TOX_CONNECTION_NONE) {
            fail("File not sent: this contact is offline or no longer exists."); return;
        }
        current->info.publicKey = Hex(key, sizeof key);
        if (command.path.empty() || command.path.find(L'\0') != std::wstring::npos) { fail("Choose a valid source file."); return; }
        try {
            auto path = std::filesystem::absolute(std::filesystem::path(command.path));
            auto basename = path.filename().u8string();
            current->info.name = SafeFileName(reinterpret_cast<const uint8_t*>(basename.data()), basename.size());
            current->info.path = path.wstring();
            current->handle.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (current->handle.value == INVALID_HANDLE_VALUE) { fail(SystemError("Could not open the source file for reading")); return; }
            BY_HANDLE_FILE_INFORMATION information{};
            LARGE_INTEGER size{};
            if (GetFileType(current->handle.value) != FILE_TYPE_DISK ||
                !GetFileInformationByHandle(current->handle.value, &information) ||
                (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !GetFileSizeEx(current->handle.value, &size) || size.QuadPart < 0) {
                fail("Choose a regular file, not a directory or device."); return;
            }
            current->info.size = static_cast<uint64_t>(size.QuadPart);
            if (current->info.size > FileSizeLimit) { fail("The maximum file size is 2 GiB."); return; }
            randombytes_buf(current->id.data(), current->id.size());
            current->deadline = Clock::now() + FileHandshakeTimeout;
            current->info.detail = "Checking that the other app supports safe WinPopup file transfers...";
            PublishTransfer(*current, EventType::FileProgress);
        } catch (const std::exception&) { fail("Could not open or prepare the source file. Check the path and available memory."); }
    }

    void ReceiveFileOffer(uint32_t contact, uint32_t number, uint32_t kind, uint64_t size, const uint8_t* name, size_t length) {
        // Never guess direction from Tox's recyclable numeric identifiers.
        if (fileNumbers.count({contact, number})) {
            StopFilesForContact(contact, "The peer reused an active transfer identifier.", true);
            tox_file_control(tox, contact, number, TOX_FILE_CONTROL_CANCEL, nullptr);
            return;
        }
        auto file = std::make_unique<ActiveFile>();
        file->info.token = NewTransferToken();
        file->info.contact = contact;
        file->info.name = SafeFileName(name, length);
        file->info.size = size;
        file->info.direction = FileDirection::Incoming;
        file->info.detail = "Waiting for your permission to save this file.";
        Tox_Public_Key key;
        if (tox_friend_get_public_key(tox, contact, key, nullptr)) file->info.publicKey = Hex(key, sizeof key);
        file->fileNumber = number;
        file->bound = true;
        tox_file_get_file_id(tox, contact, number, file->id.data(), nullptr);
        uint64_t token = file->info.token;
        auto* current = file.get();
        activeFiles.emplace(token, std::move(file));
        fileNumbers[{contact, number}] = token;
        if (!filePeers[contact].capable || filePeers[contact].blocked || kind != TOX_FILE_KIND_DATA || size > FileSizeLimit || activeFiles.size() > ActiveFileLimit) {
            FinishFile(token, FileState::Failed, !filePeers[contact].capable || filePeers[contact].blocked ?
                "File sharing requires WinPopup 0.3 or later at both ends. Text chat is still available." :
                kind != TOX_FILE_KIND_DATA ? "Unsupported file type; only ordinary file transfers are accepted." :
                size > FileSizeLimit ? "File rejected: the maximum supported size is 2 GiB." : "File rejected: 8 transfers are already active.", true);
            return;
        }
        PublishTransfer(*current, EventType::FileOffer);
    }

    void AcceptIncomingFile(const Command& command) {
        auto found = activeFiles.find(command.token);
        if (found == activeFiles.end() || found->second->info.direction != FileDirection::Incoming ||
            found->second->info.state != FileState::Offered || found->second->ownsPartial) {
            TransferCommandStatus(command.token); return;
        }
        auto& file = *found->second;
        auto fail = [&](const std::string& message) { FinishFile(command.token, FileState::Failed, message, true); };
        try {
            if (command.path.empty() || command.path.find(L'\0') != std::wstring::npos) { fail("Choose a valid destination filename."); return; }
            auto destination = std::filesystem::absolute(std::filesystem::path(command.path));
            auto basename = destination.filename().u8string();
            if (basename.empty() || basename == "." || basename == ".." || basename.find_first_of("<>:\"|?*") != std::string::npos ||
                basename.back() == '.' || basename.back() == ' ' || ReservedFileName(basename) ||
                destination.wstring().rfind(L"\\\\.\\", 0) == 0) {
                fail("Choose a normal destination filename, not a device name or alternate data stream."); return;
            }
            DWORD attributes = GetFileAttributesW(destination.c_str());
            DWORD pathError = GetLastError();
            if (attributes != INVALID_FILE_ATTRIBUTES) { fail("That destination already exists. Choose a new filename; nothing was overwritten."); return; }
            if (pathError != ERROR_FILE_NOT_FOUND && pathError != ERROR_PATH_NOT_FOUND) { fail("The destination cannot be checked safely. Choose another location."); return; }
            std::array<uint8_t, 8> random{};
            randombytes_buf(random.data(), random.size());
            auto suffix = Hex(random.data(), random.size());
            file.partial = destination;
            file.partial += L".winpopup-part-" + std::wstring(suffix.begin(), suffix.end());
            file.handle.value = CreateFileW(file.partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (file.handle.value == INVALID_HANDLE_VALUE) { fail(SystemError("Could not create the temporary download file")); return; }
            file.ownsPartial = true;
            file.info.path = destination.wstring();
            Tox_Err_File_Control error;
            if (!tox_file_control(tox, file.info.contact, file.fileNumber, TOX_FILE_CONTROL_RESUME, &error)) {
                fail("Could not accept this file (Tox error " + std::to_string(error) + ")."); return;
            }
            file.info.state = FileState::Transferring;
            file.info.detail = "Receiving into a temporary file; the final filename appears only after completion.";
            PublishTransfer(file, EventType::FileProgress);
        } catch (const std::exception&) { fail("Could not prepare the download path. Choose a writable destination."); }
    }

    void FileControl(uint32_t contact, uint32_t number, Tox_File_Control control) {
        auto* file = FindFile(contact, number);
        if (!file) return;
        if (control == TOX_FILE_CONTROL_CANCEL) {
            FinishFile(file->info.token, FileState::Cancelled, "The other person cancelled or declined the transfer.", false); return;
        }
        if (Finished(file->info.state)) return;
        // A sender's control cannot authorize an unaccepted incoming download.
        if (file->info.direction == FileDirection::Incoming && !file->ownsPartial) return;
        file->info.state = control == TOX_FILE_CONTROL_PAUSE ? FileState::Paused : FileState::Transferring;
        file->info.detail = control == TOX_FILE_CONTROL_PAUSE ? "Paused by the other person." : "Transferring encrypted file data.";
        PublishTransfer(*file, EventType::FileProgress);
    }

    void SendFileChunk(uint32_t contact, uint32_t number, uint64_t position, size_t length) {
        FileDirection direction = FileDirection::Outgoing;
        auto* file = FindFile(contact, number, &direction);
        if (!file) return;
        const uint64_t token = file->info.token;
        if (length == 0) {
            if (Finished(file->info.state)) { file->wireFinished = true; return; }
            if (position != file->info.size || file->info.transferred != file->info.size)
                FinishFile(token, FileState::Failed, "The file ended before all expected bytes were transferred.", false);
            else FinishFile(token, FileState::Completed, "The peer received the final packet. Its disk-save result is not separately acknowledged.", false);
            return;
        }
        if (Finished(file->info.state)) return;
        if (length > 65536 || position > file->info.size || length > file->info.size - position) {
            FinishFile(token, FileState::Failed, "The network requested an invalid file range.", true); return;
        }
        std::array<uint8_t, 65536> buffer{};
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(position);
        DWORD read = 0;
        if (!SetFilePointerEx(file->handle.value, offset, nullptr, FILE_BEGIN) ||
            !ReadFile(file->handle.value, buffer.data(), static_cast<DWORD>(length), &read, nullptr) || read != length) {
            FinishFile(token, FileState::Failed, "The source file could not be read completely.", true); return;
        }
        Tox_Err_File_Send_Chunk error;
        if (!tox_file_send_chunk(tox, contact, number, position, buffer.data(), length, &error)) {
            // Tox re-requests a chunk if its send queue was full. Seeking above
            // supports repeated requests without buffering the whole file.
            if (error != TOX_ERR_FILE_SEND_CHUNK_SENDQ)
                FinishFile(token, FileState::Failed, "Could not send a file chunk (Tox error " + std::to_string(error) + ").", true);
            return;
        }
        file->info.transferred = std::max(file->info.transferred, position + length);
        file->info.state = FileState::Transferring;
        PublishTransfer(*file, EventType::FileProgress, false);
    }

    void ReceiveFileChunk(uint32_t contact, uint32_t number, uint64_t position, const uint8_t* data, size_t length) {
        FileDirection direction = FileDirection::Incoming;
        auto* file = FindFile(contact, number, &direction);
        if (!file) return;
        if (Finished(file->info.state)) return;
        const uint64_t token = file->info.token;
        if (!file->ownsPartial || file->handle.value == INVALID_HANDLE_VALUE) {
            FinishFile(token, FileState::Failed, "Data arrived before this download was accepted.", true); return;
        }
        if (position != file->info.transferred || position > file->info.size || length > file->info.size - position || length > 65536) {
            FinishFile(token, FileState::Failed, "The incoming file data did not match its expected position or size.", true); return;
        }
        if (length == 0) {
            if (position != file->info.size) { FinishFile(token, FileState::Failed, "The file ended before all expected bytes arrived.", true); return; }
            if (!FlushFileBuffers(file->handle.value)) { FinishFile(token, FileState::Failed, "Could not flush the downloaded file to disk.", true); return; }
            file->handle.Reset();
            if (!MoveFileExW(file->partial.c_str(), file->info.path.c_str(), MOVEFILE_WRITE_THROUGH)) {
                FinishFile(token, FileState::Failed, "Could not publish the completed download. The destination may already exist; nothing was overwritten.", true); return;
            }
            file->ownsPartial = false;
            FinishFile(token, FileState::Completed, "File received completely and saved. It has not been opened.", false);
            return;
        }
        DWORD written = 0;
        if (!WriteFile(file->handle.value, data, static_cast<DWORD>(length), &written, nullptr) || written != length) {
            FinishFile(token, FileState::Failed, "Could not write the downloaded file. Check disk space and folder access.", true); return;
        }
        file->info.transferred += length;
        file->info.state = FileState::Transferring;
        PublishTransfer(*file, EventType::FileProgress, false);
    }

    template<typename Function> static void Callback(void* context, Function&& function) noexcept {
        auto* self = static_cast<Impl*>(context);
        try { function(*self); }
        catch (...) { self->callbackFailed = true; }
    }

    void RegisterCallbacks() {
        tox_callback_self_connection_status(tox, [](Tox*, Tox_Connection status, void* context) {
            Callback(context, [=](Impl& self) {
                self.network = Convert(status);
                Event event;
                event.type = EventType::Network;
                event.connection = self.network;
                event.text = status == TOX_CONNECTION_NONE ? "Connecting to peers..." :
                    status == TOX_CONNECTION_UDP ? "Connected to the peer network" : "Connected through an encrypted relay";
                self.Push(std::move(event));
            });
        });
        tox_callback_friend_connection_status(tox, [](Tox*, uint32_t number, Tox_Connection status, void* context) {
            Callback(context, [=](Impl& self) {
                self.contactsChanged = true;
                self.dirty = true;
                if (status == TOX_CONNECTION_NONE) self.StopFilesForContact(number, "The peer disconnected. The incomplete transfer was cancelled.", false);
                else {
                    auto& peer = self.filePeers[number];
                    if (self.SendFileProtocol(number, 1)) peer.lastHello = Clock::now();
                }
            });
        });
        tox_callback_friend_name(tox, [](Tox*, uint32_t, const uint8_t*, size_t, void* context) {
            // Tox emits this callback before it overwrites the old stored name.
            // Query snapshots after tox_iterate returns, never from this callback.
            Callback(context, [](Impl& self) { self.contactsChanged = true; self.dirty = true; });
        });
        tox_callback_friend_request(tox, [](Tox*, const uint8_t* key, const uint8_t* message, size_t length, void* context) {
            Callback(context, [=](Impl& self) {
                Event event;
                event.type = EventType::Request;
                event.key = Hex(key, TOX_PUBLIC_KEY_SIZE);
                event.text = DisplayText(message, length);
                self.Push(std::move(event));
            });
        });
        tox_callback_friend_message(tox, [](Tox*, uint32_t number, Tox_Message_Type, const uint8_t* message, size_t length, void* context) {
            Callback(context, [=](Impl& self) {
                Event event;
                event.type = EventType::Message;
                event.contact = number;
                event.text = DisplayText(message, length);
                self.Push(std::move(event));
            });
        });
        tox_callback_friend_read_receipt(tox, [](Tox*, uint32_t number, uint32_t receipt, void* context) {
            Callback(context, [=](Impl& self) {
                Event event;
                event.type = EventType::Receipt;
                event.contact = number;
                event.receipt = receipt;
                event.text = "Delivered to their app";
                self.Push(std::move(event));
            });
        });
        tox_callback_friend_lossless_packet(tox, [](Tox*, uint32_t contact, const uint8_t* data, size_t length, void* context) {
            Callback(context, [=](Impl& self) { self.FileProtocolPacket(contact, data, length); });
        });
        tox_callback_file_recv(tox, [](Tox*, uint32_t contact, uint32_t number, uint32_t kind, uint64_t size,
            const uint8_t* name, size_t length, void* context) {
            Callback(context, [=](Impl& self) { self.ReceiveFileOffer(contact, number, kind, size, name, length); });
        });
        tox_callback_file_recv_control(tox, [](Tox*, uint32_t contact, uint32_t number, Tox_File_Control control, void* context) {
            Callback(context, [=](Impl& self) { self.FileControl(contact, number, control); });
        });
        tox_callback_file_chunk_request(tox, [](Tox*, uint32_t contact, uint32_t number, uint64_t position, size_t length, void* context) {
            Callback(context, [=](Impl& self) { self.SendFileChunk(contact, number, position, length); });
        });
        tox_callback_file_recv_chunk(tox, [](Tox*, uint32_t contact, uint32_t number, uint64_t position,
            const uint8_t* data, size_t length, void* context) {
            Callback(context, [=](Impl& self) { self.ReceiveFileChunk(contact, number, position, data, length); });
        });
    }

    bool Save(std::string& error) {
        std::filesystem::path temporary;
        try {
            SecretBytes plain;
            plain.bytes.resize(tox_get_savedata_size(tox));
            if (plain.bytes.empty() || plain.bytes.size() > MaxProfileBytes - TOX_PASS_ENCRYPTION_EXTRA_LENGTH) {
                error = "The profile is too large to save safely.";
                return false;
            }
            tox_get_savedata(tox, plain.bytes.data());
            std::vector<uint8_t> encrypted(plain.bytes.size() + TOX_PASS_ENCRYPTION_EXTRA_LENGTH);
            Tox_Err_Encryption encryptionError;
            if (!tox_pass_key_encrypt(saveKey, plain.bytes.data(), plain.bytes.size(), encrypted.data(), &encryptionError)) {
                error = "Could not encrypt the profile. The previous profile is unchanged.";
                return false;
            }
            std::array<uint8_t, 12> suffix{};
            randombytes_buf(suffix.data(), suffix.size());
            std::string hex = Hex(suffix.data(), suffix.size());
            temporary = profilePath;
            temporary += L".tmp-" + std::wstring(hex.begin(), hex.end());
            Handle output;
            output.value = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (output.value == INVALID_HANDLE_VALUE) {
                error = SystemError("Could not create an encrypted profile save");
                return false;
            }
            DWORD written = 0;
            if (!WriteFile(output.value, encrypted.data(), static_cast<DWORD>(encrypted.size()), &written, nullptr) ||
                written != encrypted.size() || !FlushFileBuffers(output.value)) {
                error = SystemError("Could not finish the encrypted profile save");
                output.Reset();
                DeleteFileW(temporary.c_str());
                return false;
            }
            output.Reset();
            if (!MoveFileExW(temporary.c_str(), profilePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                error = SystemError("Could not replace the profile; the previous file is unchanged");
                DeleteFileW(temporary.c_str());
                return false;
            }
            dirty = false;
            lastSave = Clock::now();
            lastSaveError.clear();
            return true;
        } catch (const std::exception& exception) {
            if (!temporary.empty()) DeleteFileW(temporary.c_str());
            error = std::string("Could not save the encrypted profile: ") + exception.what();
            return false;
        }
    }

    void SaveOrNotify(bool finalAttempt = false) {
        std::string error;
        if (!Save(error)) {
            dirty = true;
            lastSave = Clock::now();
            if (finalAttempt || error != lastSaveError) Error(error);
            lastSaveError = error;
        }
    }

    bool Initialize(CoreOptions& options, std::string& error) {
        PasswordWiper passwordWiper{options.password};
        if (sodium_init() < 0) {
            error = "Could not initialize cryptography.";
            return false;
        }
        if (options.password.empty()) {
            error = "Enter a password to encrypt your portable identity.";
            return false;
        }
        if (options.profilePath.empty()) {
            error = "Choose a path for the portable profile.";
            return false;
        }
        profilePath = std::filesystem::absolute(std::filesystem::path(options.profilePath));
        std::filesystem::create_directories(profilePath.parent_path());
        std::filesystem::path lockPath = profilePath;
        lockPath += L".lock";
        profileLock.value = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (profileLock.value == INVALID_HANDLE_VALUE) {
            error = "This profile is already open, or its folder cannot be written. Close the other copy or choose a writable folder.";
            return false;
        }
        SecretBytes plain;
        bool existing = std::filesystem::exists(profilePath);
        Tox_Err_Key_Derivation keyError;
        if (existing) {
            uintmax_t size = std::filesystem::file_size(profilePath);
            if (size <= TOX_PASS_ENCRYPTION_EXTRA_LENGTH || size > MaxProfileBytes) {
                error = "This profile is empty, damaged, or too large. It has not been changed.";
                return false;
            }
            std::vector<uint8_t> encrypted(static_cast<size_t>(size));
            std::ifstream input(profilePath, std::ios::binary);
            if (!input.read(reinterpret_cast<char*>(encrypted.data()), static_cast<std::streamsize>(size))) {
                error = "Could not read the profile. It has not been changed.";
                return false;
            }
            if (!tox_is_data_encrypted(encrypted.data())) {
                error = "This is not an encrypted WinPopup profile. It has not been changed.";
                return false;
            }
            Tox_Pass_Salt salt;
            if (!tox_get_salt(encrypted.data(), salt, nullptr)) {
                error = "The profile header is damaged. It has not been changed.";
                return false;
            }
            saveKey = tox_pass_key_derive_with_salt(reinterpret_cast<const uint8_t*>(options.password.data()), options.password.size(), salt, &keyError);
            if (!saveKey) {
                error = "Could not derive the profile encryption key. Try closing other applications.";
                return false;
            }
            plain.bytes.resize(encrypted.size() - TOX_PASS_ENCRYPTION_EXTRA_LENGTH);
            Tox_Err_Decryption decryptError;
            if (!tox_pass_key_decrypt(saveKey, encrypted.data(), encrypted.size(), plain.bytes.data(), &decryptError)) {
                error = "The password is incorrect or the profile is damaged. Your profile has not been changed.";
                return false;
            }
        } else {
            saveKey = tox_pass_key_derive(reinterpret_cast<const uint8_t*>(options.password.data()), options.password.size(), &keyError);
            if (!saveKey) {
                error = "Could not derive the profile encryption key. Try closing other applications.";
                return false;
            }
        }
        std::unique_ptr<Tox_Options, decltype(&tox_options_free)> settings(tox_options_new(nullptr), &tox_options_free);
        if (!settings) { error = "Could not allocate network settings."; return false; }
        tox_options_set_ipv6_enabled(settings.get(), true);
        tox_options_set_udp_enabled(settings.get(), true);
        tox_options_set_local_discovery_enabled(settings.get(), true);
        tox_options_set_hole_punching_enabled(settings.get(), true);
        if (options.startPort) tox_options_set_start_port(settings.get(), options.startPort);
        if (options.endPort) tox_options_set_end_port(settings.get(), options.endPort);
        if (existing) {
            tox_options_set_savedata_type(settings.get(), TOX_SAVEDATA_TYPE_TOX_SAVE);
            if (!tox_options_set_savedata_data(settings.get(), plain.bytes.data(), plain.bytes.size())) {
                error = "Could not allocate the saved profile.";
                return false;
            }
        }
        Tox_Err_New toxError;
        tox = tox_new(settings.get(), &toxError);
        // A partially loaded Tox object can be non-null with LOAD_BAD_FORMAT.
        // Reject it: saving that object would destroy the original profile.
        if (!tox || toxError != TOX_ERR_NEW_OK) {
            error = toxError == TOX_ERR_NEW_PORT_ALLOC ? "Could not open a network port. Check your firewall or another running copy." :
                toxError == TOX_ERR_NEW_LOAD_BAD_FORMAT ? "The decrypted profile is damaged. It has not been changed." :
                "Could not start the peer network (Tox error " + std::to_string(toxError) + ").";
            return false;
        }
        if (!existing) {
            const std::string name = Trim(options.name);
            if (name.empty() || name.size() > TOX_MAX_NAME_LENGTH ||
                !tox_self_set_name(tox, reinterpret_cast<const uint8_t*>(name.data()), name.size(), nullptr)) {
                error = "Choose a display name between 1 and " + std::to_string(TOX_MAX_NAME_LENGTH) + " UTF-8 bytes.";
                return false;
            }
        }
        publicNetwork = options.publicNetwork;
        network = Connection::Offline;
        RegisterCallbacks();
        Tox_Address ownAddress;
        Tox_Dht_Id ownDht;
        tox_self_get_address(tox, ownAddress);
        tox_self_get_dht_id(tox, ownDht);
        std::vector<uint8_t> ownName(tox_self_get_name_size(tox));
        if (!ownName.empty()) tox_self_get_name(tox, ownName.data());
        {
            std::lock_guard<std::mutex> lock(mutex);
            address = Hex(ownAddress, sizeof ownAddress);
            selfName = DisplayText(ownName.data(), ownName.size());
            dhtKey = Hex(ownDht, sizeof ownDht);
            udpPort = tox_self_get_udp_port(tox, nullptr);
        }
        // Persist a new identity before giving its ID to the user.
        if (!existing && !Save(error)) return false;
        lastSave = Clock::now();
        RefreshContacts();
        return true;
    }

    void PublicBootstrap() {
        lastBootstrap = Clock::now();
        if (!publicNetwork) return;
        unsigned accepted = 0;
        for (const auto& node : PublicNodes) {
            Tox_Dht_Id key;
            if (!Decode(node.key, key, sizeof key)) continue;
            if (tox_bootstrap(tox, node.host, node.udpPort, key, nullptr)) ++accepted;
            for (auto port : node.tcpPorts) {
                if (port && tox_add_tcp_relay(tox, node.host, port, key, nullptr)) ++accepted;
            }
        }
        if (!accepted) Error("Could not reach the public bootstrap addresses. Check your internet connection; WinPopup will retry.");
    }

    void Execute(const Command& command) {
        switch (command.op) {
        case Operation::Add: {
            std::string id, error;
            if (!Core::ValidateInvitation(command.text, id, error)) { Error(error); break; }
            Tox_Address friendAddress;
            Decode(id, friendAddress, sizeof friendAddress);
            std::string hello = command.other.empty() ? "Hello! Let's chat on WinPopup." : command.other;
            if (hello.size() > TOX_MAX_FRIEND_REQUEST_LENGTH) { Error("The invitation note is too long."); break; }
            Tox_Err_Friend_Add addError;
            tox_friend_add(tox, friendAddress, reinterpret_cast<const uint8_t*>(hello.data()), hello.size(), &addError);
            if (addError != TOX_ERR_FRIEND_ADD_OK && addError != TOX_ERR_FRIEND_ADD_SET_NEW_NOSPAM) { Error(FriendAddError(addError)); break; }
            dirty = true;
            RefreshContacts();
            SaveOrNotify();
            Event event;
            event.type = EventType::Info;
            event.text = "Invitation added. They must accept it before you can chat. Both apps need to be online.";
            Push(std::move(event));
            break;
        }
        case Operation::Accept: {
            Tox_Public_Key key;
            if (!Decode(Trim(command.text), key, sizeof key)) { Error("The invitation public key is invalid."); break; }
            Tox_Err_Friend_Add addError;
            tox_friend_add_norequest(tox, key, &addError);
            if (addError != TOX_ERR_FRIEND_ADD_OK) { Error(FriendAddError(addError)); break; }
            dirty = true;
            RefreshContacts();
            SaveOrNotify();
            break;
        }
        case Operation::Remove: {
            StopFilesForContact(command.contact, "The contact was removed. The incomplete transfer was cancelled.", true);
            if (!tox_friend_delete(tox, command.contact, nullptr)) { Error("This contact could not be removed.", command.contact); break; }
            dirty = true;
            RefreshContacts();
            SaveOrNotify();
            break;
        }
        case Operation::Send: {
            if (command.text.empty()) { Error("Type a message first.", command.contact, true); break; }
            if (command.text.size() > TOX_MAX_MESSAGE_LENGTH) {
                Error("Message not sent: the limit is " + std::to_string(TOX_MAX_MESSAGE_LENGTH) + " UTF-8 bytes. Shorten the message and try again.", command.contact, true);
                break;
            }
            Tox_Err_Friend_Send_Message sendError;
            uint32_t receipt = tox_friend_send_message(tox, command.contact, TOX_MESSAGE_TYPE_NORMAL,
                reinterpret_cast<const uint8_t*>(command.text.data()), command.text.size(), &sendError);
            if (sendError != TOX_ERR_FRIEND_SEND_MESSAGE_OK) {
                std::string message = sendError == TOX_ERR_FRIEND_SEND_MESSAGE_FRIEND_NOT_CONNECTED ?
                    "Message not sent: this person is offline. Both apps must be online; try again when they connect." :
                    sendError == TOX_ERR_FRIEND_SEND_MESSAGE_FRIEND_NOT_FOUND ? "Message not sent: this contact no longer exists." :
                    "Message not sent: the network queue is busy. Try again in a moment.";
                Error(message, command.contact, true);
                break;
            }
            Event event;
            event.type = EventType::Sent;
            event.contact = command.contact;
            event.receipt = receipt;
            event.text = command.text;
            Push(std::move(event));
            break;
        }
        case Operation::Rename: {
            std::string name = Trim(command.text);
            if (name.empty() || name.size() > TOX_MAX_NAME_LENGTH ||
                !tox_self_set_name(tox, reinterpret_cast<const uint8_t*>(name.data()), name.size(), nullptr)) {
                Error("Choose a display name between 1 and " + std::to_string(TOX_MAX_NAME_LENGTH) + " UTF-8 bytes.");
                break;
            }
            dirty = true;
            {
                std::lock_guard<std::mutex> lock(mutex);
                selfName = name;
            }
            SaveOrNotify();
            Event event;
            event.type = EventType::Info;
            event.text = "Your display name has been updated.";
            Push(std::move(event));
            break;
        }
        case Operation::Retry: PublicBootstrap(); break;
        case Operation::Bootstrap: {
            Tox_Dht_Id key;
            if (command.text.empty() || command.text.size() >= TOX_MAX_HOSTNAME_LENGTH || !command.port ||
                !Decode(Trim(command.other), key, sizeof key)) { Error("The peer bootstrap address is invalid."); break; }
            Tox_Err_Bootstrap error;
            if (!tox_bootstrap(tox, command.text.c_str(), command.port, key, &error))
                Error("Could not bootstrap from the specified peer (Tox error " + std::to_string(error) + ").");
            break;
        }
        case Operation::SendFile: BeginSendFile(command); break;
        case Operation::AcceptFile: AcceptIncomingFile(command); break;
        case Operation::CancelFile:
            if (activeFiles.count(command.token)) FinishFile(command.token, FileState::Cancelled, "The transfer was cancelled locally.", true);
            else TransferCommandStatus(command.token);
            break;
        }
    }

    void Cleanup() noexcept {
        try { StopAllFiles("WinPopup stopped. Incomplete transfers must be sent again."); } catch (...) {}
        activeFiles.clear();
        fileNumbers.clear();
        if (tox) { tox_kill(tox); tox = nullptr; }
        if (saveKey) { tox_pass_key_free(saveKey); saveKey = nullptr; }
        profileLock.Reset();
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
        commands.clear();
        for (auto& contact : contacts) contact.connection = Connection::Offline;
    }

    void Run(CoreOptions options, std::promise<std::string> ready) noexcept {
        bool signalled = false;
        bool initialized = false;
        try {
            std::string error;
            if (!Initialize(options, error)) {
                Cleanup();
                ready.set_value(error);
                return;
            }
            initialized = true;
            {
                std::lock_guard<std::mutex> lock(mutex);
                running = true;
            }
            ready.set_value({});
            signalled = true;
            Event event;
            event.type = EventType::Network;
            event.connection = Connection::Offline;
            event.text = publicNetwork ? "Connecting to the peer network..." : "Looking for local peers...";
            Push(std::move(event));
            PublicBootstrap();
            for (;;) {
                std::deque<Command> pending;
                bool finish;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    pending.swap(commands);
                    finish = stopping;
                }
                // Honor every command accepted before Stop(), including profile changes.
                for (const auto& command : pending) Execute(command);
                if (finish) break;
                tox_iterate(tox, this);
                PumpFiles();
                if (contactsChanged) {
                    contactsChanged = false;
                    RefreshContacts();
                }
                if (callbackFailed) {
                    callbackFailed = false;
                    Error("A network event could not be processed because memory is low.");
                }
                auto now = Clock::now();
                if (publicNetwork && network == Connection::Offline && now - lastBootstrap >= std::chrono::seconds(30)) PublicBootstrap();
                if ((dirty && now - lastSave >= std::chrono::seconds(5)) || now - lastSave >= std::chrono::minutes(2)) SaveOrNotify();
                auto interval = std::chrono::milliseconds(std::clamp(tox_iteration_interval(tox), uint32_t(5), uint32_t(50)));
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, interval, [this] { return stopping || !commands.empty(); });
            }
            StopAllFiles("WinPopup stopped. Incomplete transfers must be sent again.");
            SaveOrNotify(true);
        } catch (const std::exception& exception) {
            if (!signalled) {
                try { ready.set_value(std::string("Could not start WinPopup: ") + exception.what()); } catch (...) {}
            } else {
                SafeError("The peer network stopped unexpectedly. Restart WinPopup to reconnect.");
            }
            if (initialized) { try { SaveOrNotify(true); } catch (...) {} }
        } catch (...) {
            if (!signalled) { try { ready.set_value("Could not start WinPopup due to an unexpected error."); } catch (...) {} }
            else SafeError("The peer network stopped unexpectedly. Restart WinPopup to reconnect.");
            if (initialized) { try { SaveOrNotify(true); } catch (...) {} }
        }
        Cleanup();
    }
};

Core::Core() : impl_(std::make_unique<Impl>()) {}
Core::~Core() { Stop(); }

bool Core::Start(const CoreOptions& options, std::string& error) {
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycle);
    error.clear();
    if (impl_->worker.joinable()) { error = "This messenger has already been started."; return false; }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stopping = false;
        impl_->running = false;
        impl_->commands.clear();
        impl_->events.clear();
        impl_->contacts.clear();
        impl_->transfers.clear();
        impl_->address.clear();
        impl_->selfName.clear();
        impl_->dhtKey.clear();
        impl_->udpPort = 0;
    }
    try {
        std::promise<std::string> ready;
        auto result = ready.get_future();
        impl_->worker = std::thread(&Impl::Run, impl_.get(), options, std::move(ready));
        error = result.get();
        if (!error.empty()) { impl_->worker.join(); return false; }
        return true;
    } catch (const std::exception& exception) {
        error = std::string("Could not start the network worker: ") + exception.what();
        if (impl_->worker.joinable()) {
            { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->stopping = true; }
            impl_->wake.notify_all();
            impl_->worker.join();
        }
        return false;
    }
}

void Core::Stop() {
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycle);
    if (!impl_->worker.joinable()) return;
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->stopping = true; }
    impl_->wake.notify_all();
    impl_->worker.join();
}

std::string Core::Address() const { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->address; }
std::string Core::SelfName() const { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->selfName; }
std::vector<Contact> Core::Contacts() const { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->contacts; }
std::vector<Event> Core::Poll() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<Event> result;
    result.swap(impl_->events);
    return result;
}
void Core::AddFriend(const std::string& invitation, const std::string& hello) { impl_->Enqueue({Impl::Operation::Add, 0, invitation, hello}); }
void Core::AcceptFriend(const std::string& publicKey) { impl_->Enqueue({Impl::Operation::Accept, 0, publicKey, {}}); }
void Core::RemoveFriend(uint32_t number) { impl_->Enqueue({Impl::Operation::Remove, number, {}, {}}); }
void Core::Send(uint32_t number, const std::string& text) { impl_->Enqueue({Impl::Operation::Send, number, text, {}}); }
uint64_t Core::SendFile(uint32_t number, const std::wstring& sourcePath, const std::string& expectedPublicKey) {
    const auto token = impl_->NewTransferToken();
    std::string expected = expectedPublicKey;
    if (expected.empty()) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto contact = std::find_if(impl_->contacts.begin(), impl_->contacts.end(), [=](const Contact& value) { return value.number == number; });
        if (contact != impl_->contacts.end()) expected = contact->publicKey;
    }
    Tox_Public_Key decoded;
    if (Decode(expected, decoded, sizeof decoded)) expected = Hex(decoded, sizeof decoded);
    impl_->Enqueue({Impl::Operation::SendFile, number, {}, expected, 0, token, sourcePath});
    return token;
}
void Core::AcceptFile(uint64_t token, const std::wstring& destination) {
    impl_->Enqueue({Impl::Operation::AcceptFile, 0, {}, {}, 0, token, destination});
}
void Core::CancelFile(uint64_t token) { impl_->Enqueue({Impl::Operation::CancelFile, 0, {}, {}, 0, token, {}}); }
std::vector<FileTransfer> Core::Transfers() const { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->transfers; }
uint64_t Core::MaxFileBytes() { return FileSizeLimit; }
size_t Core::MaxActiveFiles() { return ActiveFileLimit; }
void Core::Rename(const std::string& name) { impl_->Enqueue({Impl::Operation::Rename, 0, name, {}}); }
void Core::RetryBootstrap() { impl_->Enqueue({Impl::Operation::Retry, 0, {}, {}}); }
void Core::Bootstrap(const std::string& host, uint16_t port, const std::string& key) { impl_->Enqueue({Impl::Operation::Bootstrap, 0, host, key, port}); }
uint16_t Core::UdpPort() const { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->udpPort; }
std::string Core::DhtKey() const { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->dhtKey; }

bool Core::ValidateInvitation(const std::string& input, std::string& normalized, std::string& error) {
    normalized.clear();
    error.clear();
    std::string value = Trim(input);
    if (value.size() >= 4 && (value[0] == 't' || value[0] == 'T') &&
        (value[1] == 'o' || value[1] == 'O') && (value[2] == 'x' || value[2] == 'X') && value[3] == ':') {
        value.erase(0, 4);
        if (value.rfind("//", 0) == 0) value.erase(0, 2);
    }
    Tox_Address bytes;
    if (!Decode(value, bytes, sizeof bytes)) {
        error = "Paste a complete 76-character Tox ID, or a tox: invitation containing one.";
        return false;
    }
    uint8_t checksum[2] = {0, 0};
    for (size_t i = 0; i < sizeof(bytes) - 2; ++i) checksum[i % 2] ^= bytes[i];
    if (checksum[0] != bytes[sizeof(bytes) - 2] || checksum[1] != bytes[sizeof(bytes) - 1]) {
        error = "The Tox ID checksum does not match. Copy the complete ID again.";
        return false;
    }
    normalized = Hex(bytes, sizeof bytes);
    return true;
}

size_t Core::MaxMessageBytes() { return TOX_MAX_MESSAGE_LENGTH; }
size_t Core::MaxRequestBytes() { return TOX_MAX_FRIEND_REQUEST_LENGTH; }
} // namespace popup
