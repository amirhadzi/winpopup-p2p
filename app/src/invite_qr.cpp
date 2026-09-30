#include "invite_qr.h"
#include "core.h"
#include "../third_party/qrcodegen/qrcodegen.hpp"
#include <exception>
#include <utility>

namespace popup {

bool QrCode::Module(int x, int y) const {
    if (x < 0 || y < 0 || x >= size || y >= size) return false;
    const auto index = static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x);
    return index < modules.size() && modules[index] != 0;
}

bool MakeInvitationQr(const std::string& invitation, QrCode& out, std::string& error) {
    out = {};
    std::string address;
    if (!Core::ValidateInvitation(invitation, address, error)) return false;
    try {
        const std::string payload = "tox:" + address;
        // Explicitly disable ECC boosting to keep the documented medium level.
        const auto qr = qrcodegen::QrCode::encodeSegments(
            qrcodegen::QrSegment::makeSegments(payload.c_str()),
            qrcodegen::QrCode::Ecc::MEDIUM, 1, 40, -1, false);
        QrCode result;
        result.size = qr.getSize();
        result.modules.resize(static_cast<size_t>(result.size) * static_cast<size_t>(result.size));
        for (int y = 0; y < result.size; ++y)
            for (int x = 0; x < result.size; ++x)
                result.modules[static_cast<size_t>(y) * static_cast<size_t>(result.size) + static_cast<size_t>(x)] =
                    qr.getModule(x, y) ? 1 : 0;
        out = std::move(result);
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "The invitation QR code could not be created. You can still copy your full Tox invitation.";
        return false;
    }
}

} // namespace popup
