#ifndef POINT_3D_HPP
#define POINT_3D_HPP

#include <functional>
#include <string>
#include <sstream>

class Point3D {
public:
    int x, y, z;
    
    Point3D() : x(0), y(0), z(0) {}
    Point3D(int x, int y, int z) : x(x), y(y), z(z) {}
    
    // 比较操作符
    bool operator==(const Point3D& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    
    bool operator!=(const Point3D& other) const {
        return !(*this == other);
    }
    
    bool operator<(const Point3D& other) const {
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
    static Point3D fromBytes(const std::vector<unsigned char>& bytes) {
        if (bytes.size() < 12) {
            throw std::invalid_argument("Insufficient bytes for Point3D");
        }
        
        auto parseInt = [&](size_t offset) -> int {
            return (static_cast<int>(bytes[offset]) << 24) |
                   (static_cast<int>(bytes[offset + 1]) << 16) |
                   (static_cast<int>(bytes[offset + 2]) << 8) |
                   static_cast<int>(bytes[offset + 3]);
        };
        
        return Point3D(parseInt(0), parseInt(4), parseInt(8));
    }

     // Hash结构体，用于unordered_map
    struct Hash {
        std::size_t operator()(const Point3D& p) const {
            std::size_t h1 = std::hash<int>{}(p.x);
            std::size_t h2 = std::hash<int>{}(p.y);
            std::size_t h3 = std::hash<int>{}(p.z);
            
            // 组合哈希值
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
};



#endif // POINT_3D_HPP
