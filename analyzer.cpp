#include "analyzer.h"
#include <fstream>
#include <algorithm>
#include <cstring>

inline bool TripAnalyzer::fastExtractHour(const char* datetime, size_t len, int& hour) const {
    // find space between date and time
    const char* space = nullptr;
    for (size_t i = 0; i < len && i < 20; ++i) {
        if (datetime[i] == ' ') {
            space = datetime + i;
            break;
        }
    }
    
    if (!space || space + 2 >= datetime + len) return false;
    
    char h1 = space[1];
    char h2 = space[2];
    
    if ((h1 - '0') > 9u || (h2 - '0') > 9u) return false;
    
    hour = (h1 - '0') * 10 + (h2 - '0');
    return hour <= 23;
}

// parse csv without creating unnecessary string copies
static inline bool fastParseLine(const char* line, size_t lineLen, 
                                  const char*& zoneStart, size_t& zoneLen,
                                  const char*& dateStart, size_t& dateLen) {
    const char* pos = line;
    const char* end = line + lineLen;
    int commaCount = 0;
    const char* comma1 = nullptr;
    const char* comma2 = nullptr;
    const char* comma3 = nullptr;
    
    while (pos < end && commaCount < 4) {
        if (*pos == ',') {
            ++commaCount;
            if (commaCount == 1) comma1 = pos;
            else if (commaCount == 2) comma2 = pos;
            else if (commaCount == 3) comma3 = pos;
        }
        ++pos;
    }
    
    if (commaCount < 4 || !comma1 || !comma2 || !comma3) return false;
    
    // zone is between 1st and 2nd comma
    zoneStart = comma1 + 1;
    zoneLen = comma2 - zoneStart;
    
    // datetime is between 3rd comma and 4th
    dateStart = comma3 + 1;
    const char* comma4 = pos;
    while (comma4 < end && *comma4 != ',') ++comma4;
    dateLen = comma4 - dateStart;
    
    return zoneLen > 0 && dateLen > 0;
}

void TripAnalyzer::ingestFile(const std::string& csvPath) {
    std::ifstream file(csvPath);
    
    if (!file.is_open()) return;
    
    // pre-allocate to avoid rehashing
    zoneCount_.reserve(1024);
    slotCount_.reserve(8192);
    zoneIndex_.reserve(1024);
    zoneToIdx_.reserve(1024);
    
    std::string line;
    line.reserve(256);
    
    // skip header
    if (!std::getline(file, line)) return;
    
    while (std::getline(file, line)) {
        const char* linePtr = line.c_str();
        size_t lineLen = line.size();
        
        const char* zoneStart;
        size_t zoneLen;
        const char* dateStart;
        size_t dateLen;
        
        if (!fastParseLine(linePtr, lineLen, zoneStart, zoneLen, dateStart, dateLen)) {
            continue;
        }
        
        int hour;
        if (!fastExtractHour(dateStart, dateLen, hour)) {
            continue;
        }
        
        std::string zone(zoneStart, zoneLen);
        
        ++zoneCount_[zone];
        
        // map zone to index for compact slot storage
        auto it = zoneToIdx_.find(zone);
        uint32_t zoneIdx;
        if (it == zoneToIdx_.end()) {
            zoneIdx = static_cast<uint32_t>(zoneIndex_.size());
            zoneToIdx_[zone] = zoneIdx;
            zoneIndex_.push_back(std::move(zone));
        } else {
            zoneIdx = it->second;
        }
        
        SlotKey key = makeSlotKey(zoneIdx, static_cast<uint8_t>(hour));
        ++slotCount_[key];
    }
}

std::vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    if (zoneCount_.empty()) return {};
    
    std::vector<ZoneCount> v;
    v.reserve(zoneCount_.size());
    
    for (const auto& kv : zoneCount_) {
        v.push_back(ZoneCount{kv.first, kv.second});
    }
    
    auto cmp = [](const ZoneCount& a, const ZoneCount& b) {
        return a.count != b.count ? a.count > b.count : a.zone < b.zone;
    };
    
    size_t n = v.size();
    size_t topK = static_cast<size_t>(k);
    
    // partial sort: faster than full sort when k << n
    if (n > topK) {
        std::nth_element(v.begin(), v.begin() + topK, v.end(), cmp);
        v.resize(topK);
    }
    
    std::sort(v.begin(), v.end(), cmp);
    return v;
}

std::vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    if (slotCount_.empty()) return {};
    
    std::vector<SlotCount> v;
    v.reserve(slotCount_.size());
    
    for (const auto& kv : slotCount_) {
        uint32_t zoneIdx = getZoneIdx(kv.first);
        uint8_t hour = getHour(kv.first);
        
        if (zoneIdx >= zoneIndex_.size()) continue;
        
        v.push_back(SlotCount{zoneIndex_[zoneIdx], static_cast<int>(hour), kv.second});
    }
    
    auto cmp = [](const SlotCount& a, const SlotCount& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.zone != b.zone) return a.zone < b.zone;
        return a.hour < b.hour;
    };
    
    size_t n = v.size();
    size_t topK = static_cast<size_t>(k);
    
    if (n > topK) {
        std::nth_element(v.begin(), v.begin() + topK, v.end(), cmp);
        v.resize(topK);
    }
    
    std::sort(v.begin(), v.end(), cmp);
    return v;
}
