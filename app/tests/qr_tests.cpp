#include "invite_qr.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    ++checks;
    std::cout << "PASS " << message << '\n';
}

// Synthetic public address with a valid Tox checksum; no private key or real
// profile is involved, and no network connection is opened by these tests.
std::string FixtureAddress(bool descending) {
    std::array<uint8_t, 38> bytes{};
    for (size_t i = 0; i < 36; ++i) {
        bytes[i] = static_cast<uint8_t>(descending ? 255 - i : i);
        bytes[36 + i % 2] ^= bytes[i];
    }
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (auto value : bytes) {
        result += hex[value >> 4];
        result += hex[value & 15];
    }
    return result;
}

void WriteFixture(const std::filesystem::path& directory, const char* name,
                  const popup::QrCode& qr, const std::string& payload) {
    constexpr int scale = 6;
    const int border = popup::InvitationQrQuietZone;
    const int pixels = (qr.size + border * 2) * scale;
    std::ofstream image(directory / (std::string(name) + ".pgm"), std::ios::binary);
    image << "P5\n" << pixels << ' ' << pixels << "\n255\n";
    for (int y = 0; y < pixels; ++y)
        for (int x = 0; x < pixels; ++x) {
            const unsigned char pixel = qr.Module(x / scale - border, y / scale - border) ? 0 : 255;
            image.write(reinterpret_cast<const char*>(&pixel), 1);
        }
    Check(static_cast<bool>(image), "PGM fixture written for independent QR decoding");
    std::ofstream text(directory / (std::string(name) + ".txt"), std::ios::binary);
    text << payload;
    Check(static_cast<bool>(text), "exact expected QR payload written");
}
} // namespace

int main(int argc, char** argv) {
    try {
        popup::QrCode first, second, normalized;
        std::string error;
        const std::string address = FixtureAddress(false);
        const std::string otherAddress = FixtureAddress(true);
        const std::string payload = "tox:" + address;
        Check(payload.size() == 80, "complete invitation fixture is 80 ASCII characters");
        Check(popup::MakeInvitationQr(payload, first, error) && error.empty(), "valid complete invitation encodes");
        Check(first.size == 37 && first.modules.size() == 37 * 37, "80-byte invitation uses QR version 5 at medium correction");
        Check(std::all_of(first.modules.begin(), first.modules.end(), [](uint8_t cell) { return cell <= 1; }), "snapshot contains binary modules");
        Check(first.Module(0, 0) && first.Module(6, 6) && !first.Module(1, 1) && first.Module(3, 3), "finder geometry is intact");
        Check(!first.Module(-1, 0) && !first.Module(0, -1) && !first.Module(first.size, 0) && !first.Module(0, first.size), "outside modules are white for quiet-zone rendering");
        Check(popup::InvitationQrQuietZone >= 4, "rendering contract requires a four-module quiet zone");
        std::string lower = address;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        Check(popup::MakeInvitationQr(" \tToX://" + lower + "\r\n", normalized, error) && normalized.modules == first.modules,
              "accepted invitation forms encode the same canonical complete URI");
        Check(popup::MakeInvitationQr(address, normalized, error) && normalized.modules == first.modules, "bare ID includes the tox URI prefix in its QR");
        Check(popup::MakeInvitationQr("tox:" + otherAddress, second, error) && second.modules != first.modules, "a different contact produces different QR data");
        auto corrupt = address;
        corrupt[0] = corrupt[0] == 'A' ? 'B' : 'A';
        normalized = first;
        Check(!popup::MakeInvitationQr(corrupt, normalized, error) && !error.empty(), "bad invitation checksum is rejected");
        Check(normalized.size == 0 && normalized.modules.empty(), "failed generation clears stale QR data");
        Check(!popup::MakeInvitationQr("https://example.com/" + address, normalized, error), "unrelated URL cannot be encoded as an invitation");
        Check(!popup::MakeInvitationQr("", normalized, error), "empty invitation is rejected");
        popup::QrCode incomplete{37, {1}};
        Check(!incomplete.Module(36, 36) && !popup::QrCode{}.Module(0, 0), "empty or truncated snapshots are safe to render");
        if (argc > 1) {
            const std::filesystem::path directory(argv[1]);
            std::filesystem::create_directories(directory);
            WriteFixture(directory, "invitation-ascending", first, payload);
            WriteFixture(directory, "invitation-descending", second, "tox:" + otherAddress);
            std::cout << "FIXTURE_PAYLOAD " << payload << '\n';
        }
        std::cout << checks << " QR checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
