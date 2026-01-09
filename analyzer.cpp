#include "analyzer.h"

#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cstdint>

// -------- Fast hour extraction --------
// Works for "YYYY-MM-DD HH:MM" and "YYYY-MM-DD HH:MM:SS"
static inline bool extractHourFast(const std::string& dt, int& hour) {
    size_t sp = dt.find(' ');
    if (sp == std::string::npos || sp + 2 >= dt.size()) return false;

    char a = dt[sp + 1];
    char b = dt[sp + 2];
    if (a < '0' || a > '9' || b < '0' || b > '9') return false;

    hour = (a - '0') * 10 + (b - '0');
    return (0 <= hour && hour <= 23);
}

static bool parseLineFast(const std::string& line, std::string& pickupZoneId, int& hour) {
    size_t c1 = line.find(',');
    if (c1 == std::string::npos) return false;

    size_t c2 = line.find(',', c1 + 1);
    if (c2 == std::string::npos) return false;

    size_t c3 = line.find(',', c2 + 1);
    if (c3 == std::string::npos) return false;

    size_t c4 = line.find(',', c3 + 1);
    if (c4 == std::string::npos) return false;

    size_t c5 = line.find(',', c4 + 1);
    if (c5 == std::string::npos) return false;

    // Exactly 6 columns => no more commas after the 5th comma
    if (line.find(',', c5 + 1) != std::string::npos) return false;

    // PickupZoneID = between c1 and c2
    if (c2 <= c1 + 1) return false;
    pickupZoneId.assign(line, c1 + 1, c2 - (c1 + 1));
    if (pickupZoneId.empty()) return false;

    // PickupDateTime = between c3 and c4
    if (c4 <= c3 + 1) return false;
    std::string datetime(line, c3 + 1, c4 - (c3 + 1));

    return extractHourFast(datetime, hour);
}

// -------- Internal state (kept in .cpp so analyzer.h stays unchanged) --------
namespace {

struct State {
    // Store each zone string once, map it to a compact integer id
    std::unordered_map<std::string, int> zoneToId;
    std::vector<std::string> idToZone;

    // Fast counters by zone id
    std::vector<long long> zoneCount;

    // key = (zoneId << 6) | hour   (hour fits in 6 bits)
    std::unordered_map<std::uint64_t, long long> slotCount;
};

// One State per TripAnalyzer instance
static std::unordered_map<const TripAnalyzer*, State> g_state;

static inline std::uint64_t makeSlotKey(int zoneId, int hour) {
    return ( (std::uint64_t)zoneId << 6 ) | (std::uint64_t)hour;
}

static int getZoneId(State& st, const std::string& zone) {
    auto it = st.zoneToId.find(zone);
    if (it != st.zoneToId.end()) return it->second;

    int id = (int)st.idToZone.size();
    st.idToZone.push_back(zone);
    st.zoneToId.emplace(zone, id);
    st.zoneCount.push_back(0);
    return id;
}

} // namespace

void TripAnalyzer::ingestFile(const std::string& csvPath) {
    State& st = g_state[this];

    // Reset counts every ingest (tests can call ingest once per instance)
    st.zoneToId.clear();
    st.idToZone.clear();
    st.zoneCount.clear();
    st.slotCount.clear();

    std::ifstream fin(csvPath);
    if (!fin.is_open()) {
        // A1 expects no crash and empty results
        return;
    }

    // Light reserves help performance on big inputs (safe defaults)
    st.zoneToId.reserve(65536);
    st.slotCount.reserve(65536);

    std::string line;

    // Skip header (tests always write header first)
    if (!std::getline(fin, line)) return;

    // Process remaining lines
    while (std::getline(fin, line)) {
        std::string zone;
        int hour;

        // Skip dirty lines safely
        if (!parseLineFast(line, zone, hour)) continue;

        int zoneId = getZoneId(st, zone);

        ++st.zoneCount[zoneId];
        ++st.slotCount[makeSlotKey(zoneId, hour)];
    }
}

std::vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    if (k <= 0) return {};

    auto it = g_state.find(this);
    if (it == g_state.end()) return {};

    const State& st = it->second;

    std::vector<ZoneCount> out;
    out.reserve(st.idToZone.size());

    for (int id = 0; id < (int)st.idToZone.size(); ++id) {
        long long cnt = st.zoneCount[id];
        if (cnt == 0) continue;
        out.push_back(ZoneCount{st.idToZone[id], cnt});
    }

    // count desc, zone asc
    auto cmp = [](const ZoneCount& a, const ZoneCount& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.zone < b.zone;
    };

    if ((int)out.size() > k) {
        std::nth_element(out.begin(), out.begin() + k, out.end(), cmp);
        out.resize(k);
    }
    std::sort(out.begin(), out.end(), cmp);

    return out;
}

std::vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    if (k <= 0) return {};

    auto it = g_state.find(this);
    if (it == g_state.end()) return {};

    const State& st = it->second;

    std::vector<SlotCount> out;
    out.reserve(st.slotCount.size());

    for (const auto& kv : st.slotCount) {
        std::uint64_t key = kv.first;
        long long cnt = kv.second;

        int hour = (int)(key & 63ULL);
        int zoneId = (int)(key >> 6);

        out.push_back(SlotCount{st.idToZone[zoneId], hour, cnt});
    }

    // count desc, zone asc, hour asc
    auto cmp = [](const SlotCount& a, const SlotCount& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.zone != b.zone)   return a.zone < b.zone;
        return a.hour < b.hour;
    };

    if ((int)out.size() > k) {
        std::nth_element(out.begin(), out.begin() + k, out.end(), cmp);
        out.resize(k);
    }
    std::sort(out.begin(), out.end(), cmp);

    return out;
}
