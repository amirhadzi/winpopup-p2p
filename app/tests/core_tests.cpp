#include "core.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>

using namespace popup;
using namespace std::chrono_literals;
static int checks = 0;
static void Check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
    ++checks; std::cout << "PASS " << description << std::endl;
}
template<class Fn> static bool WaitFor(Fn&& predicate, std::chrono::seconds timeout = 35s) {
    auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (predicate()) return true;
        std::this_thread::sleep_for(20ms);
    }
    return false;
}
static std::string Bytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}
int main(int argc, char** argv) {
  try {
    std::filesystem::path root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path() / "WinPopupCoreTests";
    root /= std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    CoreOptions oa, ob;
    oa.profilePath = (root / "alice.tox").wstring(); oa.name = "Alice";
    ob.profilePath = (root / "bob.tox").wstring(); ob.name = "Bob";
    oa.password = ob.password = "Test-only passphrase 123!";
    oa.publicNetwork = ob.publicNetwork = false;
    Core a, b;
    std::string error, normalized;
    Check(a.Start(oa, error), ("Alice profile starts: " + error).c_str());
    Check(b.Start(ob, error), ("Bob profile starts: " + error).c_str());
    Check(a.SelfName() == "Alice" && b.SelfName() == "Bob", "local display names match new profiles");
    {
        const auto beforeSecondOpen = Bytes(oa.profilePath);
        Core alreadyOpen;
        Check(!alreadyOpen.Start(oa, error), "simultaneous profile opening rejected");
        Check(Bytes(oa.profilePath) == beforeSecondOpen, "simultaneous open preserves existing identity");
    }
    const auto addressA = a.Address(), addressB = b.Address();
    Check(addressA.size() == 76 && addressB.size() == 76 && addressA != addressB, "unique complete Tox identities");
    Check(Core::ValidateInvitation("tox:" + addressB, normalized, error) && normalized == addressB, "Tox invitation URI accepted");
    auto bad = addressB; bad[0] = bad[0] == 'A' ? 'B' : 'A';
    Check(!Core::ValidateInvitation(bad, normalized, error), "corrupted invitation checksum rejected");
    Check(!Core::ValidateInvitation("https://example.com/" + addressB, normalized, error), "unrelated URL rejected");
    Check(!Core::ValidateInvitation("", normalized, error), "empty invitation rejected");
    Check(!Core::ValidateInvitation(std::string(10000, 'A'), normalized, error), "oversized invitation rejected");
    Check(a.UdpPort() != 0 && b.UdpPort() != 0 && a.UdpPort() != b.UdpPort(), "separate UDP sockets bound");
    a.Bootstrap("127.0.0.1", b.UdpPort(), b.DhtKey());
    b.Bootstrap("127.0.0.1", a.UdpPort(), a.DhtKey());
    a.AddFriend(addressB, "Hello from an isolated local test");
    std::string requestKey;
    Check(WaitFor([&] {
        for (auto& e : b.Poll()) if (e.type == EventType::Request) requestKey = e.key;
        return !requestKey.empty();
    }, 65s), "friend request arrives through real Tox transport");
    Check(b.Contacts().empty(), "incoming request is not automatically trusted");
    Check(requestKey == addressA.substr(0,64), "friend request authenticated to sender public key");
    b.AcceptFriend(requestKey);
    Check(WaitFor([&] {
        auto ac = a.Contacts(), bc = b.Contacts();
        return ac.size() == 1 && bc.size() == 1 && ac[0].connection != Connection::Offline && bc[0].connection != Connection::Offline;
    }, 65s), "accepted peers connect");
    const auto ai = a.Contacts()[0].number, bi = b.Contacts()[0].number;
    Check(a.Contacts()[0].connection == Connection::Direct, "localhost session uses direct peer connection");
    Check(WaitFor([&] { return a.Contacts()[0].name == "Bob" && b.Contacts()[0].name == "Alice"; }), "peer display names synchronize");
    a.Poll(); b.Poll();
    const std::string message = u8"WinPopup test: encrypted hello 👋 — café — 你好";
    a.Send(ai, message);
    bool incoming = false, sent = false, delivered = false;
    uint32_t sentId = 0, receiptId = 0;
    Check(WaitFor([&] {
        for (auto& e : b.Poll()) if (e.type == EventType::Message && e.text == message && e.contact == bi) incoming = true;
        for (auto& e : a.Poll()) {
            if (e.type == EventType::Sent && e.contact == ai) { sent = true; sentId = e.receipt; }
            if (e.type == EventType::Receipt && e.contact == ai) { delivered = true; receiptId = e.receipt; }
        }
        return incoming && sent && delivered;
    }), "Unicode message and delivery receipt complete end to end");
    Check(sentId == receiptId, "delivery receipt matches outgoing message");
    b.Send(bi, "Reply from Bob");
    Check(WaitFor([&] { for (auto& e : a.Poll()) if(e.type == EventType::Message && e.text == "Reply from Bob") return true; return false; }), "bidirectional messaging");
    b.Send(bi, std::string("before \xC0\xAF after"));
    Check(WaitFor([&] {
        for (auto& e : a.Poll()) if (e.type == EventType::Message && e.text == u8"before �� after") return true;
        return false;
    }), "malformed incoming UTF-8 safely replaced");
    a.Poll();
    a.Send(ai, std::string(Core::MaxMessageBytes()+1, 'x'));
    Check(WaitFor([&] { for (auto& e : a.Poll()) if(e.type == EventType::Error) return true; return false; }, 5s), "oversized message produces error");
    a.Rename("Alice renamed");
    Check(WaitFor([&] { return b.Contacts()[0].name == "Alice renamed"; }), "rename synchronizes");
    a.Stop(); b.Stop();
    const auto encrypted = Bytes(oa.profilePath);
    Check(encrypted.size() > 80 && encrypted.substr(0,8) == "toxEsave", "profile encrypted using Tox encrypted-save format");
    Check(encrypted.find("Alice renamed") == std::string::npos && encrypted.find(message) == std::string::npos, "profile stores no plaintext name or test message");
    Core wrong;
    auto wrongOptions = oa; wrongOptions.password = "Definitely wrong password";
    Check(!wrong.Start(wrongOptions, error), "wrong profile password rejected");
    Check(Bytes(oa.profilePath) == encrypted, "wrong password does not overwrite identity");
    Core reload;
    Check(reload.Start(oa, error), "encrypted profile reopens with correct password");
    Check(reload.Address() == addressA && reload.Contacts().size() == 1, "identity and contact persist across restart");
    Check(reload.SelfName() == "Alice renamed", "saved display name survives default startup options");
    reload.Poll();
    reload.Send(reload.Contacts()[0].number, "Must not claim offline delivery");
    bool offlineError = false, offlineSent = false;
    Check(WaitFor([&] {
        for (auto& e : reload.Poll()) {
            if (e.type == EventType::Error) offlineError = true;
            if (e.type == EventType::Sent || e.type == EventType::Receipt) offlineSent = true;
        }
        return offlineError;
    }, 5s) && !offlineSent, "offline message fails without a false sent or delivered event");
    reload.RemoveFriend(reload.Contacts()[0].number);
    Check(WaitFor([&] { return reload.Contacts().empty(); }, 5s), "contact removal takes effect");
    reload.Stop();
    Core finalReload;
    Check(finalReload.Start(oa, error) && finalReload.Contacts().empty(), "contact removal persists");
    finalReload.Stop();
    auto brokenPath = root / "corrupt.tox";
    { std::ofstream f(brokenPath, std::ios::binary); f << "not a valid encrypted profile"; }
    Core corrupt; auto brokenOptions = oa; brokenOptions.profilePath = brokenPath.wstring();
    Check(!corrupt.Start(brokenOptions, error), "corrupt profile rejected");
    Check(Bytes(brokenPath) == "not a valid encrypted profile", "corrupt profile preserved for recovery");
    std::cout << "ALL " << checks << " CHECKS PASSED\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
