// A real, disposable c-toxcore peer which knows nothing about WinPopup's
// file-sharing capability protocol. Only loopback bootstrap is configured.
#include "core.h"
#include <tox/tox.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace popup;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
namespace {
int checks = 0;
void Check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
    ++checks;
    std::cout << "PASS " << description << std::endl;
}
std::string Hex(const uint8_t* bytes, size_t length) {
    static const char alphabet[] = "0123456789ABCDEF";
    std::string text(length * 2, '0');
    for (size_t i = 0; i < length; ++i) {
        text[2 * i] = alphabet[bytes[i] >> 4];
        text[2 * i + 1] = alphabet[bytes[i] & 15];
    }
    return text;
}
std::vector<uint8_t> Unhex(const std::string& text) {
    if (text.size() % 2) throw std::runtime_error("Invalid test hex length");
    auto digit = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        throw std::runtime_error("Invalid test hex digit");
    };
    std::vector<uint8_t> bytes(text.size() / 2);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<uint8_t>((digit(text[2 * i]) << 4) | digit(text[2 * i + 1]));
    return bytes;
}
std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void Write(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("Cannot create disposable test file");
}
bool NoDownloadArtifacts(const fs::path& root, const fs::path& remoteName) {
    for (const auto& item : fs::recursive_directory_iterator(root)) {
        const auto name = item.path().filename();
        if (name == remoteName || name.wstring().find(L".winpopup-part-") != std::wstring::npos) return false;
    }
    return true;
}
struct CurrentDirectory {
    fs::path previous = fs::current_path();
    explicit CurrentDirectory(const fs::path& path) { fs::current_path(path); }
    ~CurrentDirectory() { std::error_code ignored; fs::current_path(previous, ignored); }
};
struct RawOffer { uint32_t contact, number, kind; uint64_t size; };
struct RawControl { uint32_t contact, number; Tox_File_Control control; };
struct RawState {
    std::vector<std::string> messages;
    std::vector<RawOffer> offers;
    std::vector<RawControl> controls;
    size_t chunkRequests = 0;
};
struct Peers {
    Core modern;
    std::unique_ptr<Tox, decltype(&tox_kill)> legacy{nullptr, &tox_kill};
    RawState raw;
    std::vector<Event> events;
    uint32_t modernFriend = 0, legacyFriend = 0;
    std::string legacyKey;

    explicit Peers(const fs::path& root) {
        CoreOptions options;
        options.profilePath = (root / L"profile" / L"modern.tox").wstring();
        options.password = "Disposable legacy peer test password";
        options.name = "WinPopup test peer";
        options.publicNetwork = false;
        options.startPort = 37000;
        options.endPort = 37099;
        fs::create_directories(root / L"profile");
        std::string error;
        Check(modern.Start(options, error), ("Modern peer starts: " + error).c_str());

        std::unique_ptr<Tox_Options, decltype(&tox_options_free)> settings(tox_options_new(nullptr), &tox_options_free);
        Check(settings != nullptr, "Raw Tox options allocated");
        tox_options_set_ipv6_enabled(settings.get(), false);
        tox_options_set_udp_enabled(settings.get(), true);
        tox_options_set_local_discovery_enabled(settings.get(), false);
        // Explicit disjoint ranges avoid IPv4/IPv6 sockets choosing the same
        // numeric port on Windows while the peers use loopback bootstrap.
        tox_options_set_start_port(settings.get(), 37100);
        tox_options_set_end_port(settings.get(), 37199);
        Tox_Err_New createError = TOX_ERR_NEW_OK;
        legacy.reset(tox_new(settings.get(), &createError));
        Check(legacy != nullptr && createError == TOX_ERR_NEW_OK, "Raw legacy peer starts without public bootstrap or custom-packet callbacks");
        const std::string name = "Legacy raw Tox client";
        Check(tox_self_set_name(legacy.get(), reinterpret_cast<const uint8_t*>(name.data()), name.size(), nullptr), "Raw peer has its own display name");
        tox_callback_friend_message(legacy.get(), [](Tox*, uint32_t, Tox_Message_Type, const uint8_t* data, size_t size, void* context) {
            static_cast<RawState*>(context)->messages.emplace_back(reinterpret_cast<const char*>(data), size);
        });
        tox_callback_file_recv(legacy.get(), [](Tox*, uint32_t contact, uint32_t number, uint32_t kind, uint64_t size,
            const uint8_t*, size_t, void* context) {
            static_cast<RawState*>(context)->offers.push_back({contact, number, kind, size});
        });
        tox_callback_file_recv_control(legacy.get(), [](Tox*, uint32_t contact, uint32_t number, Tox_File_Control control, void* context) {
            static_cast<RawState*>(context)->controls.push_back({contact, number, control});
        });
        tox_callback_file_chunk_request(legacy.get(), [](Tox*, uint32_t, uint32_t, uint64_t, size_t, void* context) {
            ++static_cast<RawState*>(context)->chunkRequests;
        });
        // Deliberately do not register a lossless custom-packet callback, send
        // any capability packet, accept file offers, or write received data.

        Tox_Public_Key publicKey;
        Tox_Dht_Id rawDht;
        tox_self_get_public_key(legacy.get(), publicKey);
        tox_self_get_dht_id(legacy.get(), rawDht);
        legacyKey = Hex(publicKey, sizeof(publicKey));
        auto modernKey = Unhex(modern.Address().substr(0, 64));
        Tox_Err_Friend_Add addError = TOX_ERR_FRIEND_ADD_OK;
        legacyFriend = tox_friend_add_norequest(legacy.get(), modernKey.data(), &addError);
        Check(addError == TOX_ERR_FRIEND_ADD_OK, "Raw peer explicitly trusts only the disposable modern identity");
        modern.AcceptFriend(legacyKey);
        auto modernDht = Unhex(modern.DhtKey());
        Tox_Err_Bootstrap bootstrapError = TOX_ERR_BOOTSTRAP_OK;
        Check(tox_bootstrap(legacy.get(), "127.0.0.1", modern.UdpPort(), modernDht.data(), &bootstrapError)
            && bootstrapError == TOX_ERR_BOOTSTRAP_OK, "Raw peer bootstraps exclusively to the loopback modern peer");
        uint16_t rawPort = tox_self_get_udp_port(legacy.get(), nullptr);
        Check(rawPort != 0 && rawPort != modern.UdpPort(), "Disposable peers use separate UDP sockets");
        modern.Bootstrap("127.0.0.1", rawPort, Hex(rawDht, sizeof(rawDht)));
        Check(Wait([&] {
            auto contacts = modern.Contacts();
            return contacts.size() == 1 && contacts[0].publicKey == legacyKey && contacts[0].connection == Connection::Direct &&
                tox_friend_get_connection_status(legacy.get(), legacyFriend, nullptr) == TOX_CONNECTION_UDP;
        }, 65s), "Raw legacy and WinPopup peers connect directly over loopback");
        modernFriend = modern.Contacts()[0].number;
    }
    void Pump() {
        tox_iterate(legacy.get(), &raw);
        auto fresh = modern.Poll();
        events.insert(events.end(), std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()));
    }
    template<class Predicate> bool Wait(Predicate predicate, std::chrono::seconds timeout = 15s) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        do {
            Pump();
            if (predicate()) return true;
            std::this_thread::sleep_for(10ms);
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    void Settle(std::chrono::milliseconds duration) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        do { Pump(); std::this_thread::sleep_for(10ms); } while (std::chrono::steady_clock::now() < deadline);
    }
    FileTransfer Transfer(uint64_t token) {
        for (const auto& transfer : modern.Transfers()) if (transfer.token == token) return transfer;
        return {};
    }
    void RawSend(const std::string& message) {
        Tox_Err_Friend_Send_Message sendError = TOX_ERR_FRIEND_SEND_MESSAGE_OK;
        tox_friend_send_message(legacy.get(), legacyFriend, TOX_MESSAGE_TYPE_NORMAL,
            reinterpret_cast<const uint8_t*>(message.data()), message.size(), &sendError);
        Check(sendError == TOX_ERR_FRIEND_SEND_MESSAGE_OK, "Raw peer accepts the outgoing ordinary text message");
    }
    bool HasMessage(const std::string& message) const {
        return std::any_of(events.begin(), events.end(), [&](const Event& event) {
            return event.type == EventType::Message && event.contact == modernFriend && event.text == message;
        });
    }
    bool RawHasMessage(const std::string& message) const {
        return std::find(raw.messages.begin(), raw.messages.end(), message) != raw.messages.end();
    }
};
}

int main() {
    try {
        const fs::path root = fs::temp_directory_path() / L"WinPopupLegacyPeerTests" /
            std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(root / L"downloads");
        CurrentDirectory currentDirectory(root);
        Peers peers(root);
        const std::string incoming = u8"Legacy to WinPopup: ordinary text, café, 你好.";
        const std::string outgoing = u8"WinPopup to legacy: ordinary text still works — hello.";
        peers.RawSend(incoming);
        Check(peers.Wait([&] { return peers.HasMessage(incoming); }), "Legacy-to-WinPopup Unicode text works without file capability negotiation");
        peers.modern.Send(peers.modernFriend, outgoing);
        Check(peers.Wait([&] { return peers.RawHasMessage(outgoing); }), "WinPopup-to-legacy Unicode text still works");
        uint32_t receipt = 0;
        Check(peers.Wait([&] {
            bool sent = false;
            for (const auto& event : peers.events) if (event.type == EventType::Sent && event.contact == peers.modernFriend && event.text == outgoing) {
                receipt = event.receipt; sent = true; break;
            }
            return sent && std::any_of(peers.events.begin(), peers.events.end(), [&](const Event& event) {
                return event.type == EventType::Receipt && event.contact == peers.modernFriend && event.receipt == receipt;
            });
        }), "Legacy text delivery receipt matches the accepted outgoing text");

        const fs::path source = root / L"source-only.bin";
        const std::string sourceBytes = "This fixture must never be offered to the unsupported peer.";
        Write(source, sourceBytes);
        const auto started = std::chrono::steady_clock::now();
        uint64_t token = peers.modern.SendFile(peers.modernFriend, source.wstring(), peers.legacyKey);
        Check(token != 0, "Unsupported-peer request receives a stable local transfer token");
        Check(peers.Wait([&] {
            auto transfer = peers.Transfer(token);
            return transfer.token == token && transfer.state == FileState::Failed;
        }, 30s), "Unsupported peer file request fails within the bounded 30-second test deadline");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        std::cout << "Unsupported-peer negotiation elapsed: " << elapsed.count() << " ms\n";
        auto failed = peers.Transfer(token);
        Check(failed.transferred == 0 && failed.direction == FileDirection::Outgoing && failed.publicKey == peers.legacyKey,
            "Unsupported outgoing transfer preserves peer identity and transfers no bytes");
        Check(failed.detail.find("WinPopup") != std::string::npos && failed.detail.find("0.3") != std::string::npos,
            "Unsupported-peer failure explains the WinPopup 0.3 requirement");
        peers.Settle(1s);
        Check(peers.raw.offers.empty() && peers.raw.chunkRequests == 0, "Legacy peer receives no native file offer or file data request");
        Check(Read(source) == sourceBytes, "Unsupported outgoing negotiation preserves source bytes");

        const std::string legacyFilename = "ordinary-legacy-offer.bin";
        Tox_File_Id fileId{};
        for (size_t i = 0; i < sizeof(fileId); ++i) fileId[i] = static_cast<uint8_t>(i + 1);
        Tox_Err_File_Send fileError = TOX_ERR_FILE_SEND_OK;
        uint32_t rawFile = tox_file_send(peers.legacy.get(), peers.legacyFriend, TOX_FILE_KIND_DATA, 4096,
            fileId, reinterpret_cast<const uint8_t*>(legacyFilename.data()), legacyFilename.size(), &fileError);
        Check(fileError == TOX_ERR_FILE_SEND_OK, "Raw legacy peer emits an ordinary Tox file offer without custom capability packets");
        Check(peers.Wait([&] {
            return std::any_of(peers.raw.controls.begin(), peers.raw.controls.end(), [&](const RawControl& control) {
                return control.contact == peers.legacyFriend && control.number == rawFile && control.control == TOX_FILE_CONTROL_CANCEL;
            });
        }, 10s), "WinPopup rejects the unsupported incoming offer with native Tox cancellation");
        Check(peers.Wait([&] {
            return std::any_of(peers.events.begin(), peers.events.end(), [&](const Event& event) {
                return event.type == EventType::FileFinished && event.transfer.direction == FileDirection::Incoming &&
                    event.transfer.name == legacyFilename && event.transfer.state == FileState::Failed;
            });
        }, 5s), "Unsupported incoming offer reports a failed terminal transfer");
        peers.Settle(500ms);
        Check(std::none_of(peers.events.begin(), peers.events.end(), [](const Event& event) { return event.type == EventType::FileOffer; }),
            "Unsupported incoming file is never presented as an acceptable UI offer");
        Check(std::none_of(peers.raw.controls.begin(), peers.raw.controls.end(), [&](const RawControl& control) {
            return control.contact == peers.legacyFriend && control.number == rawFile && control.control == TOX_FILE_CONTROL_RESUME;
        }) && peers.raw.chunkRequests == 0, "Unsupported incoming offer is never resumed or asked for payload chunks");
        bool rejectedIncoming = false;
        for (const auto& transfer : peers.modern.Transfers()) if (transfer.direction == FileDirection::Incoming && transfer.name == legacyFilename) {
            rejectedIncoming = transfer.state == FileState::Failed && transfer.path.empty() && transfer.transferred == 0;
        }
        Check(rejectedIncoming && NoDownloadArtifacts(root, fs::path(legacyFilename)) && fs::is_empty(root / L"downloads"),
            "Unsupported incoming offer creates no download path, output file, or private partial file");

        peers.RawSend("Legacy text after file rejection");
        peers.modern.Send(peers.modernFriend, "WinPopup text after file rejection");
        Check(peers.Wait([&] { return peers.HasMessage("Legacy text after file rejection") && peers.RawHasMessage("WinPopup text after file rejection"); }),
            "Bidirectional ordinary text survives both unsupported file attempts");
        Check(peers.raw.messages.size() == 2, "Capability negotiation never appears as extra chat messages to a legacy client");
        peers.modern.Stop();
        Check(NoDownloadArtifacts(root, fs::path(legacyFilename)), "Shutdown leaves no rejected-download artifacts");
        std::cout << "ALL " << checks << " LEGACY PEER CHECKS PASSED\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
