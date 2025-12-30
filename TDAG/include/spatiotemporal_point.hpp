#pragma once

#include <string>

// 代表原始、连续的时空数据点
struct SpatiotemporalPoint {
    std::string time_str;
    long long utc_timestamp;
    double latitude;
    double longitude;
    int record_id; // 假设每行数据有一个唯一ID

    SpatiotemporalPoint(std::string time_str,long long ts, double lat, double lon, int id)
        : time_str(time_str), utc_timestamp(ts), latitude(lat), longitude(lon), record_id(id) {}
};