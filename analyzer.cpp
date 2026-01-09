#include "analyzer.h"
#include <fstream>
#include <algorithm>
#include <sstream>

inline bool TripAnalyzer::fastExtractHour(const char* datetime, size_t len, int& hour) const {
    const char* space = nullptr;
    for (size_t i = 0; i < len && i < 30; ++i) {
        if (datetime[i] == ' ') {
            space = datetime + i;
            break;
        }
    }
    
    if (!space || space + 2 >= datetime + len) return false;
    
    char h1 = space[1];
    char h2 = space[2];
    
    if (h1 < '0' || h1 > '9' || h2 < '0' || h2 > '9') return false;
    
    hour = (h1 - '0') * 10 + (h2 - '0');
    return hour >= 0 && hour <= 23;
}

void TripAnalyzer::ingestFile(const std::string& csvPath) {
    std::ifstream file(csvPath);
    
    if (!file.is_open()) {
        return;
    }
    
    zoneCount_.reserve(1024);
    slotCount_.reserve(8192);
    zoneIndex_.reserve(1024);
    zoneToIdx_.reserve(1024);
    
    std::string line;
    
    // skip header row
    if (!std::getline(file, line)) {
        return;
    }
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        
        while (std::getline(ss, field, ',')) {
            fields.push_back(field);
        }
        
        if (fields.size() < 6) continue;
        
        std::string zone = fields[1];
        std::string datetime = fields[3];
        
        if (zone.empty() || datetime.empty()) continue;
        
        int hour;
        if (!fastExtractHour(datetime.c_str(), datetime.size(), hour)) {
            continue;
        }
        
        ++zoneCount_[zone];
        
        auto it = zoneToIdx_.find(zone);
        uint32_t zoneIdx;
        if (it == zoneToIdx_.end()) {
            zoneIdx = static_cast<uint32_t>(zoneIndex_.size());
            zoneToIdx_[zone] = zoneIdx;
            zoneIndex_.push_back(zone);
        } else {
            zoneIdx = it->second;
        }
        
        SlotKey key = makeSlotKey(zoneIdx, static_cast<uint8_t>(hour));
        ++slotCount_[key];
    }
}

std::vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    std::vector<ZoneCount> result;
    
    if (zoneCount_.empty()) return result;
    
    result.reserve(zoneCount_.size());
    
    for (const auto& kv : zoneCount_) {
        result.push_back(ZoneCount{kv.first, kv.second});
    }
    
    auto cmp = [](const ZoneCount& a, const ZoneCount& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.zone < b.zone;
    };
    
    if (result.size() > static_cast<size_t>(k)) {
        std::nth_element(result.begin(), result.begin() + k, result.end(), cmp);
        result.resize(k);
    }
    
    std::sort(result.begin(), result.end(), cmp);
    
    return result;
}

std::vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    std::vector<SlotCount> result;
    
    if (slotCount_.empty()) return result;
    
    result.reserve(slotCount_.size());
    
    for (const auto& kv : slotCount_) {
        uint32_t zoneIdx = getZoneIdx(kv.first);
        uint8_t hour = getHour(kv.first);
        
        if (zoneIdx >= zoneIndex_.size()) continue;
        
        result.push_back(SlotCount{zoneIndex_[zoneIdx], static_cast<int>(hour), kv.second});
    }
    
    auto cmp = [](const SlotCount& a, const SlotCount& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.zone != b.zone) return a.zone < b.zone;
        return a.hour < b.hour;
    };
    
    if (result.size() > static_cast<size_t>(k)) {
        std::nth_element(result.begin(), result.begin() + k, result.end(), cmp);
        result.resize(k);
    }
    
    std::sort(result.begin(), result.end(), cmp);
    
    return result;
}
