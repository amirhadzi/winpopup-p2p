#include "core.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: network_probe temporary-profile-folder\n"; return 2; }
    auto dir = std::filesystem::path(argv[1]);
    dir /= std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(dir);
    popup::Core core; popup::CoreOptions opts;
    opts.profilePath = (dir / "probe.tox").wstring();
    opts.password = "Temporary public connectivity probe";
    opts.name = "WinPopup connectivity test";
    std::string error;
    if (!core.Start(opts, error)) { std::cerr << error << '\n'; return 1; }
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (std::chrono::steady_clock::now() < until) {
        for (const auto& e : core.Poll()) {
            if (e.type == popup::EventType::Error) std::cerr << e.text << '\n';
            if (e.type == popup::EventType::Network && e.connection != popup::Connection::Offline) {
                std::cout << "PASS connected to public Tox network (" << (e.connection == popup::Connection::Direct ? "UDP" : "TCP") << ")\n";
                core.Stop(); return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cerr << "Public Tox network did not connect within 90 seconds on this network\n";
    core.Stop(); return 1;
}
