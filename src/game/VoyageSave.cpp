#include "game/VoyageSave.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace VoyageSave {

static constexpr char    kMagic[5] = "OVYG";
static constexpr uint8_t kSaveVer  = 1; // v1: gameTime + ship state

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

    out.gameTime        = fields[0];
    out.ship.position   = { fields[1], fields[2] };
    out.ship.velocity   = { fields[3], fields[4] };
    out.ship.heading    = fields[5];
    out.ship.yawRate    = fields[6];
    out.ship.throttle   = std::clamp(fields[7], -1.0f, 1.0f);
    out.ship.rudder     = std::clamp(fields[8], -1.0f, 1.0f);
    return true;
}

} // namespace VoyageSave
