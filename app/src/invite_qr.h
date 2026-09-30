#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace popup {

// The snapshot excludes the quiet zone. Render black modules on white, with
// at least this many white modules on every side and an integer pixel scale.
inline constexpr int InvitationQrQuietZone = 4;

struct QrCode {
    int size = 0;
    std::vector<uint8_t> modules; // Row-major, 0 = white, 1 = black.
    bool Module(int x, int y) const;
};

// Validates a Tox invitation and encodes the complete canonical "tox:" URI.
// Uses medium error correction. No networking, files, or external service.
// On failure, clears out and provides an error suitable for display.
bool MakeInvitationQr(const std::string& invitation, QrCode& out, std::string& error);

} // namespace popup
