#pragma once

#include <functional>
#include <string>
#include <sstream>

// 代表离散化后的整数网格坐标
class GridPoint3D {
public:
    int x, y, z;
    GridPoint3D(int x = 0, int y = 0, int z = 0) noexcept : x(x), y(y), z(z) {}

    // 比较操作符
    bool operator==(const GridPoint3D& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    
    bool operator!=(const GridPoint3D& other) const {
        return !(*this == other);
    }
    
    bool operator<(const GridPoint3D& other) const {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }
    
    // 字符串表示
    std::string toString() const {
        std::stringstream ss;
        ss << "(" << x << "," << y << "," << z << ")";
        return ss.str();
    }
    
    // 序列化为字节
    std::vector<unsigned char> toBytes() const {
        std::vector<unsigned char> bytes;
        bytes.reserve(12); // 3 * 4 bytes for 3 ints
        
        // 转换为大端字节序
        for (int val : {x, y, z}) {
            bytes.push_back((val >> 24) & 0xFF);
            bytes.push_back((val >> 16) & 0xFF);
            bytes.push_back((val >> 8) & 0xFF);
            bytes.push_back(val & 0xFF);
        }
        
        return bytes;
    }
    
    // 从字节反序列化
    static GridPoint3D fromBytes(const std::vector<unsigned char>& bytes) {
        if (bytes.size() < 12) {
            throw std::invalid_argument("Insufficient bytes for GridPoint3D");
        }
        
        auto parseInt = [&](size_t offset) -> int {
            return (static_cast<int>(bytes[offset]) << 24) |
                   (static_cast<int>(bytes[offset + 1]) << 16) |
                   (static_cast<int>(bytes[offset + 2]) << 8) |
                   static_cast<int>(bytes[offset + 3]);
        };
        
        return GridPoint3D(parseInt(0), parseInt(4), parseInt(8));
    }

     // Hash结构体，用于unordered_map
    struct Hash {
        std::size_t operator()(const GridPoint3D& p) const {
            std::size_t h1 = std::hash<int>{}(p.x);
            std::size_t h2 = std::hash<int>{}(p.y);
            std::size_t h3 = std::hash<int>{}(p.z);
            
            // 组合哈希值
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };  
};