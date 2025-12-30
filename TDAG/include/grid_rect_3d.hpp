#ifndef GRID_RECT_3D_HPP
#define GRID_RECT_3D_HPP

#include "grid_point_3d.hpp"
#include <stdexcept>
 #include <sstream>
 
class Rect3D {
public:
    GridPoint3D start, end;
    
    Rect3D() = default;
    Rect3D(const GridPoint3D& start, const GridPoint3D& end) : start(start), end(end) {
        if (start.x > end.x || start.y > end.y || start.z > end.z) {
            throw std::invalid_argument("Invalid rectangle bounds");
        }
    }
    
    // 比较操作符
    bool operator==(const Rect3D& other) const {
        return start == other.start && end == other.end;
    }
    
    bool operator<(const Rect3D& other) const {
        if (start != other.start) return start < other.start;
        return end < other.end;
    }
    
    // 包含检查
    bool containsPoint(const GridPoint3D& point) const {
        return point.x >= start.x && point.x < end.x &&
               point.y >= start.y && point.y < end.y &&
               point.z >= start.z && point.z < end.z;
    }
    
    bool containsRect(const Rect3D& rect) const {
        return rect.start.x >= start.x && rect.end.x <= end.x &&
               rect.start.y >= start.y && rect.end.y <= end.y &&
               rect.start.z >= start.z && rect.end.z <= end.z;
    }
    
    bool containsRectBRC(const Rect3D& rect) const {
        return start.x <= rect.start.x && (end.x + 1) >= rect.end.x &&
               start.y <= rect.start.y && (end.y + 1) >= rect.end.y &&
               start.z <= rect.start.z && (end.z + 1) >= rect.end.z;
    }
    
    // 相交检查
    bool intersects(const Rect3D& rect) const {
        if (start.x == end.x || start.y == end.y || start.z == end.z ||
            rect.start.x == rect.end.x || rect.start.y == rect.end.y || rect.start.z == rect.end.z) {
            return false;
        }
        
        return !(start.x >= rect.end.x || rect.start.x >= end.x ||
                 end.y <= rect.start.y || rect.end.y <= start.y ||
                 start.z >= rect.end.z || rect.start.z >= end.z);
    }
    
    // 字符串表示
    std::string toString() const {
        return "Rect3D[" + start.toString() + ", " + end.toString() + "]";
    }

   
    //qdag label序列化与tdag一样：TdagSRC3D::serialize_cover
    std::string toLabelString() const {
        std::ostringstream oss;
        oss << "(" << start.x << "," << start.y << "," << start.z << ")-("
            << end.x   << "," << end.y   << "," << end.z   << ")";
        return oss.str();
    }
    
    // 序列化
    std::vector<unsigned char> toBytes() const {
        auto startBytes = start.toBytes();
        auto endBytes = end.toBytes();
        
        std::vector<unsigned char> bytes;
        bytes.reserve(24); // 2 * 12 bytes
        bytes.insert(bytes.end(), startBytes.begin(), startBytes.end());
        bytes.insert(bytes.end(), endBytes.begin(), endBytes.end());
        
        return bytes;
    }
    
    static Rect3D fromBytes(const std::vector<unsigned char>& bytes) {
        if (bytes.size() < 24) {
            throw std::invalid_argument("Insufficient bytes for Rect3D");
        }
        
        std::vector<unsigned char> startBytes(bytes.begin(), bytes.begin() + 12);
        std::vector<unsigned char> endBytes(bytes.begin() + 12, bytes.begin() + 24);

        return Rect3D(GridPoint3D::fromBytes(startBytes), GridPoint3D::fromBytes(endBytes));
    }

};


namespace std {
    template <>
    struct hash<Rect3D> {
        size_t operator()(const Rect3D& rect) const noexcept {
            size_t h1 = GridPoint3D::Hash{}(rect.start);
            size_t h2 = GridPoint3D::Hash{}(rect.end);
            return h1 ^ (h2 << 1); // 简单组合，可用 boost::hash_combine 优化
        }
    };

    // 可选：equal_to 专化（使用 operator==）
    template <>
    struct equal_to<Rect3D> {
        bool operator()(const Rect3D& lhs, const Rect3D& rhs) const noexcept {
            return lhs == rhs;
        }
    };
}

#endif // GRID_RECT_3D_HPP
