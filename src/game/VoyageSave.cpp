#include "game/VoyageSave.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

namespace VoyageSave {

static constexpr char    kMagic[5] = "OVYG";
static constexpr uint8_t kSaveVer  = 2; // v1: gameTime + ship state. v2: + money + cargo

bool save(const std::string& path, const Data& data) {
    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary);
        if (!f) return false;

        f.write(kMagic, 5);
        f.write(reinterpret_cast<const char*>(&kSaveVer), 1);

        const float fields[9] = {
            data.gameTime,
            data.ship.position.x, data.ship.position.y,
            data.ship.velocity.x, data.ship.velocity.y,
            data.ship.heading, data.ship.yawRate,
            data.ship.throttle, data.ship.rudder,
        };
        f.write(reinterpret_cast<const char*>(fields), sizeof(fields));

        const int32_t money = data.money;
        f.write(reinterpret_cast<const char*>(&money), 4);

        const int32_t stackCount = (int32_t)data.cargo.stacks.size();
        f.write(reinterpret_cast<const char*>(&stackCount), 4);
        for (const CargoStack& s : data.cargo.stacks) {
            const uint8_t good  = (uint8_t)s.good;
            const int32_t count = s.count;
            f.write(reinterpret_cast<const char*>(&good), 1);
            f.write(reinterpret_cast<const char*>(&count), 4);
        }

        if (!f) { // a write failed — leave the live save untouched
            f.close();
            std::error_code rmEc;
            std::filesystem::remove(tmpPath, rmEc);
            return false;
        }
    } // ofstream flushed & closed here

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::error_code rmEc;
        std::filesystem::remove(tmpPath, rmEc);
        return false;
    }
    return true;
}

bool load(const std::string& path, Data& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[5];
    f.read(magic, 5);
    if (!f || std::memcmp(magic, kMagic, 5) != 0) return false; // legacy PFRM (or junk) → rejected

    uint8_t ver;
    f.read(reinterpret_cast<char*>(&ver), 1);
    if (!f || ver != kSaveVer) return false; // version mismatch → treated as new game (dev policy)

    float fields[9];
    f.read(reinterpret_cast<char*>(fields), sizeof(fields));
    if (!f) return false;

    // A corrupt float would poison the physics integration forever (NaN spreads
    // through position/velocity), so reject the whole file instead.
    for (float v : fields)
        if (!std::isfinite(v)) return false;

    int32_t money;
    f.read(reinterpret_cast<char*>(&money), 4);
    if (!f || money < 0) return false;

    // Cargo: at most one stack per good id; total must fit the hold.
    CargoHold cargo;
    int32_t stackCount;
    f.read(reinterpret_cast<char*>(&stackCount), 4);
    if (!f || stackCount < 0 || stackCount > (int32_t)CargoGoodId::COUNT) return false;
    cargo.stacks.reserve((size_t)stackCount);
    for (int32_t i = 0; i < stackCount; i++) {
        uint8_t good; int32_t count;
        f.read(reinterpret_cast<char*>(&good), 1);
        f.read(reinterpret_cast<char*>(&count), 4);
        if (!f || good >= (uint8_t)CargoGoodId::COUNT || count <= 0) return false;
        cargo.stacks.push_back({ (CargoGoodId)good, count });
    }
    if (cargo.used() > cargo.capacity) return false;

    // All reads succeeded — commit.
    out.gameTime        = fields[0];
    out.ship.position   = { fields[1], fields[2] };
    out.ship.velocity   = { fields[3], fields[4] };
    out.ship.heading    = fields[5];
    out.ship.yawRate    = fields[6];
    out.ship.throttle   = std::clamp(fields[7], -1.0f, 1.0f);
    out.ship.rudder     = std::clamp(fields[8], -1.0f, 1.0f);
    out.money           = money;
    out.cargo           = std::move(cargo);
    return true;
}

} // namespace VoyageSave
