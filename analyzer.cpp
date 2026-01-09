#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct ZoneCount {
    std::string zone;
    long long count;
};

struct SlotCount {
    std::string zone;
    int hour;
    long long count;
};

class TripAnalyzer {
public:
    void ingestFile(const std::string& csvPath);
    std::vector<ZoneCount> topZones(int k = 10) const;
    std::vector<SlotCount> topBusySlots(int k = 10) const;

private:
    // pack zone index + hour into single uint32_t for better performance
    using SlotKey = uint32_t;
    
    inline SlotKey makeSlotKey(uint32_t zoneIdx, uint8_t hour) const {
        return (zoneIdx << 8) | hour;
    }
    
    inline uint32_t getZoneIdx(SlotKey key) const {
        return key >> 8;
    }
    
    inline uint8_t getHour(SlotKey key) const {
        return key & 0xFF;
    }

    // extract hour from datetime string
    inline bool fastExtractHour(const char* datetime, size_t len, int& hour) const;

    // main data structures
    std::unordered_map<std::string, long long> zoneCount_;
    std::unordered_map<SlotKey, long long> slotCount_;
    
    // zone string <-> index mapping
    std::vector<std::string> zoneIndex_;
    std::unordered_map<std::string, uint32_t> zoneToIdx_;
};
