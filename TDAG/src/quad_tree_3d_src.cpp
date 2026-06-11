#include "quad_tree_3d_src.hpp"
#include <cmath>
#include <queue>
#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>

QuadTree3DSRC::QuadTree3DSRC(int height, bool is_src) : is_src_(is_src) {
    max_domain_ = 1 << height;
    root_rect_ = Rect3D(GridPoint3D(0,0,0), GridPoint3D(max_domain_, max_domain_, max_domain_));
    buildQuadTreeIterative();
}

void QuadTree3DSRC::buildQuadTreeIterative() {
    std::queue<Rect3D> to_visit;
    std::unordered_set<Rect3D> visited;
    to_visit.push(root_rect_);
    visited.insert(root_rect_);
    qdag_dict_[root_rect_] = {};
    

    while (!to_visit.empty()) {
        Rect3D current = to_visit.front();
        to_visit.pop();

        int width = current.end.x - current.start.x;//叶子不再展开
        if (width <= 1) continue; 

        auto children = getChildren(current);
        qdag_dict_[current] = children;

        for (const auto& child : children) {
            // if (visited.insert(child).second) {
            //     to_visit.push(child);
            // }
            if (visited.find(child) == visited.end()) {//visited 用来去重避免重复入队；
                visited.insert(child);
                to_visit.push(child); 
            }
        }
    }
}

//生成孩子：SRC（3×3×3 平移）vs 普通八叉（2×2×2）
std::vector<Rect3D> QuadTree3DSRC::getChildren(const Rect3D& parent) const {
    // Returns the 27 direct and indirect QDAG nodes in 3-dimensions.
    int child_width = (parent.end.x - parent.start.x) / 2;
    if (child_width <= 0) return {};
    // if (is_src_ && child_width < 2) return {}; // 防止 step=0 产生 27 个重复孩子

    std::vector<Rect3D> children;
    // int step = is_src_ ? child_width / 2 : child_width; // SRC uses half-grid shifts
    // int num_div = is_src_ ? 3 : 2;

    // for (int dx = 0; dx < num_div; ++dx) {
    //     for (int dy = 0; dy < num_div; ++dy) {
    //         for (int dz = 0; dz < num_div; ++dz) {
    //             GridPoint3D start(
    //                 parent.start.x + dx * step, 
    //                 parent.start.y + dy * step, 
    //                 parent.start.z + dz * step);
    //             GridPoint3D end(start.x + child_width, start.y + child_width, start.z + child_width);
    //             children.emplace_back(start, end);
    //         }
    //     }
    // }
    // Logic to distinguish between staggered (SRC) and standard quadtree children
    if (is_src_ && child_width >= 2) { // Python's height >= 2 check equivalent
        int step = child_width / 2;
        for (int dx = 0; dx < 3; ++dx) {
            for (int dy = 0; dy < 3; ++dy) {
                for (int dz = 0; dz < 3; ++dz) {
                    GridPoint3D start(
                        parent.start.x + dx * step, 
                        parent.start.y + dy * step, 
                        parent.start.z + dz * step);
                    GridPoint3D end(start.x + child_width, start.y + child_width, start.z + child_width);
                    children.emplace_back(start, end);
                }
            }
        }
    } else { // Standard 8 children for non-SRC or low levels of SRC tree
        for (int dx = 0; dx < 2; ++dx) {
            for (int dy = 0; dy < 2; ++dy) {
                for (int dz = 0; dz < 2; ++dz) {
                     GridPoint3D start(
                        parent.start.x + dx * child_width, 
                        parent.start.y + dy * child_width, 
                        parent.start.z + dz * child_width);
                    GridPoint3D end(start.x + child_width, start.y + child_width, start.z + child_width);
                    children.emplace_back(start, end);
                }
            }
        }
    }
    return children;
}

std::vector<Rect3D> QuadTree3DSRC::findContainingRangeCovers(const GridPoint3D& point) const {
    if (!root_rect_.containsPoint(point)) return {};

    std::vector<Rect3D> result;
    std::unordered_set<Rect3D> visited;
    std::function<void(const Rect3D&)> collect = [&](const Rect3D& rect) {
        if (!visited.insert(rect).second) {
            return;
        }
        result.push_back(rect);

        auto it = qdag_dict_.find(rect);
        if (it == qdag_dict_.end()) {
            return;
        }
        for (const auto& child : it->second) {
            if (child.containsPoint(point)) {
                collect(child);
            }
        }
    };

    collect(root_rect_);
    return result;
}

// Rect3D QuadTree3DSRC::getSingleRangeCover(const Rect3D& query) const {
//     // Python SRC 逻辑：under-approximation with offset
//     Rect3D adjusted(query.start, GridPoint3D(query.end.x + 1, query.end.y + 1, query.end.z + 1));
//     int max_side = std::max({adjusted.end.x - adjusted.start.x, adjusted.end.y - adjusted.start.y, adjusted.end.z - adjusted.start.z});
//     int power = 1 << static_cast<int>(std::ceil(std::log2(max_side ))); // +1 防 0
//     int offset = power / 2;

//     int left_x = (static_cast<int>(adjusted.start.x / static_cast<float>(offset)) * offset);
//     int left_y = (static_cast<int>(adjusted.start.y / static_cast<float>(offset)) * offset);
//     int left_z = (static_cast<int>(adjusted.start.z / static_cast<float>(offset)) * offset);
//     Rect3D left(GridPoint3D(left_x, left_y, left_z), GridPoint3D(left_x + power, left_y + power, left_z + power));

//     int right_x = static_cast<int>(std::ceil(adjusted.end.x / static_cast<float>(offset))) * offset - power;
//     int right_y = static_cast<int>(std::ceil(adjusted.end.y / static_cast<float>(offset))) * offset - power;
//     int right_z = static_cast<int>(std::ceil(adjusted.end.z / static_cast<float>(offset))) * offset - power;
//     Rect3D right(GridPoint3D(right_x, right_y, right_z), GridPoint3D(right_x + power, right_y + power, right_z + power));

//     // Check both left and right covers
//     if (left.containsRect(adjusted)) return left;
//     if (right.containsRect(adjusted)) return right;

//     // If no cover found, recurse with doubled power
//     if (power < max_domain_) {
//         return getSingleRangeCover(Rect3D(query.start, GridPoint3D(query.end.x + 1, query.end.y + 1, query.end.z + 1))); // Recurse
//     }

//     // Fallback to the smallest possible cover if within domain
//     return Rect3D(adjusted.start, GridPoint3D(adjusted.start.x + power, adjusted.start.y + power, adjusted.start.z + power));
// }


// Rect3D QuadTree3DSRC::getSingleRangeCover(const Rect3D& query) const {
    
//     auto clamp = [](int value, int low, int high) {
//         if (value < low) return low;
//         if (value > high) return high;
//         return value;
//     };

//     GridPoint3D clamped_start(
//         clamp(query.start.x, root_rect_.start.x, root_rect_.end.x),
//         clamp(query.start.y, root_rect_.start.y, root_rect_.end.y),
//         clamp(query.start.z, root_rect_.start.z, root_rect_.end.z));

//     GridPoint3D clamped_end(
//         clamp(query.end.x, root_rect_.start.x, root_rect_.end.x),
//         clamp(query.end.y, root_rect_.start.y, root_rect_.end.y),
//         clamp(query.end.z, root_rect_.start.z, root_rect_.end.z));

//     Rect3D adjusted(clamped_start, clamped_end);

//     int len_x = std::max(0, adjusted.end.x - adjusted.start.x);
//     int len_y = std::max(0, adjusted.end.y - adjusted.start.y);
//     int len_z = std::max(0, adjusted.end.z - adjusted.start.z);
//     int max_side = std::max({len_x, len_y, len_z, 1});

//     int power = 1;
//     while (power < max_side) {
//         power <<= 1;
//     }
//     if (power > max_domain_) {
//         power = max_domain_;
//     }

//     auto findAxisStart = [&](int query_start, int query_end, int axis_min, int axis_max, int offset, int cube_size) -> std::optional<int> {
//         int min_candidate = query_end - cube_size;
//         int max_candidate = query_start;

//         min_candidate = std::max(min_candidate, axis_min);
//         max_candidate = std::min(max_candidate, axis_max - cube_size);

//         if (min_candidate > max_candidate) return std::nullopt;

//         if (offset <= 0) {
//             return max_candidate;
//         }

//         int k_min = (min_candidate + offset - 1) / offset;
//         int k_max = max_candidate / offset;
//         if (k_min > k_max) return std::nullopt;

//         int candidate = k_max * offset;
//         if (candidate < min_candidate) {
//             candidate += offset;
//             if (candidate > max_candidate) return std::nullopt;
//         }

//         return candidate;
//     };

//     for (int cube_size = power; cube_size <= max_domain_;) {
//         int offset = is_src_ ? std::max(1, cube_size / 2) : cube_size;

//         auto start_x = findAxisStart(adjusted.start.x, adjusted.end.x, root_rect_.start.x, root_rect_.end.x, offset, cube_size);
//         auto start_y = findAxisStart(adjusted.start.y, adjusted.end.y, root_rect_.start.y, root_rect_.end.y, offset, cube_size);
//         auto start_z = findAxisStart(adjusted.start.z, adjusted.end.z, root_rect_.start.z, root_rect_.end.z, offset, cube_size);

//         if (start_x && start_y && start_z) {
//             GridPoint3D start(*start_x, *start_y, *start_z);
//             GridPoint3D end(start.x + cube_size, start.y + cube_size, start.z + cube_size);
//             Rect3D candidate(start, end);
//             if (candidate.containsRect(adjusted)) {
//                 return candidate;
//             }
//         }

//         if (cube_size >= max_domain_) {
//             break;
//         }

//         cube_size <<= 1;
//         if (cube_size > max_domain_) {
//             cube_size = max_domain_;
//         }
//     }

//     return root_rect_;
// }

// Rewritten to match Python's logic
Rect3D QuadTree3DSRC::getSingleRangeCover(const Rect3D& query) const {
    // Clamp the query to be within the max domain first
    auto clamp = [&](int value, int low, int high) {
        return std::max(low, std::min(value, high));
    };
    
    Rect3D clamped_query(
        GridPoint3D(clamp(query.start.x, 0, max_domain_), clamp(query.start.y, 0, max_domain_), clamp(query.start.z, 0, max_domain_)),
        GridPoint3D(clamp(query.end.x, 0, max_domain_), clamp(query.end.y, 0, max_domain_), clamp(query.end.z, 0, max_domain_))
    );

    // 1. Adjust query for exclusive end point, as in Python
    // Rect3D adjusted_query(
    //     clamped_query.start,
    //     GridPoint3D(clamped_query.end.x, clamped_query.end.y, clamped_query.end.z)
    // );

    // 1) 明确半开区间：[start, end)；若原 end == start（空区间），保证 len>=1 的最小覆盖
    Rect3D adjusted_query = clamped_query;
    if (adjusted_query.end.x == adjusted_query.start.x && adjusted_query.end.x < max_domain_) adjusted_query.end.x += 1;
    if (adjusted_query.end.y == adjusted_query.start.y && adjusted_query.end.y < max_domain_) adjusted_query.end.y += 1;
    if (adjusted_query.end.z == adjusted_query.start.z && adjusted_query.end.z < max_domain_) adjusted_query.end.z += 1;

    // 2. Find longest side
    int len_x = adjusted_query.end.x - adjusted_query.start.x;
    int len_y = adjusted_query.end.y - adjusted_query.start.y;
    int len_z = adjusted_query.end.z - adjusted_query.start.z;
    int longest_side = std::max({1, len_x, len_y, len_z});

    // 3. Find next highest power of 2
    int power_of_2 = 1;
    while (power_of_2 < longest_side) {
        power_of_2 <<= 1; //乘以2
    }

    // Iteratively find the correct cover, doubling power_of_2 if needed
    while (power_of_2 <= max_domain_) {
        // 4. Calculate offset
        if (!is_src_) { // Non-SRC mode has no offset logic
            int start_x = static_cast<int>(std::floor(static_cast<float>(adjusted_query.start.x) / power_of_2)) * power_of_2;
            int start_y = static_cast<int>(std::floor(static_cast<float>(adjusted_query.start.y) / power_of_2)) * power_of_2;
            int start_z = static_cast<int>(std::floor(static_cast<float>(adjusted_query.start.z) / power_of_2)) * power_of_2;
            Rect3D candidate(GridPoint3D(start_x, start_y, start_z), GridPoint3D(start_x + power_of_2, start_y + power_of_2, start_z + power_of_2));
            if (candidate.containsRect(adjusted_query)) {
                return candidate;
            }
        } else { // SRC mode
            int offset = power_of_2 / 2;
            if (offset == 0) {
                GridPoint3D start(
                    std::min(std::max(adjusted_query.start.x, 0), max_domain_ - 1),
                    std::min(std::max(adjusted_query.start.y, 0), max_domain_ - 1),
                    std::min(std::max(adjusted_query.start.z, 0), max_domain_ - 1)
                );
                return Rect3D(
                    start,
                    GridPoint3D(start.x + 1, start.y + 1, start.z + 1)
                );
            }

            // 5. Generate candidate start coordinates for each axis
            int left_start_x = static_cast<int>(std::floor(static_cast<float>(adjusted_query.start.x) / offset)) * offset;
            // int right_start_x = static_cast<int>(std::ceil(static_cast<float>(adjusted_query.end.x) / offset)) * offset - power_of_2;
            int right_start_x = static_cast<int>(std::ceil (static_cast<float>(adjusted_query.end.x - 1) / (float)offset)) * offset - power_of_2;

            int left_start_y = static_cast<int>(std::floor(static_cast<float>(adjusted_query.start.y) / offset)) * offset;
            // int right_start_y = static_cast<int>(std::ceil(static_cast<float>(adjusted_query.end.y) / offset)) * offset - power_of_2;
            int right_start_y = static_cast<int>(std::ceil (static_cast<float>(adjusted_query.end.y - 1) /(float) offset)) * offset - power_of_2;

            int left_start_z = static_cast<int>(std::floor(static_cast<float>(adjusted_query.start.z) / offset)) * offset;
            // int right_start_z = static_cast<int>(std::ceil(static_cast<float>(adjusted_query.end.z) / offset)) * offset - power_of_2;
            int right_start_z = static_cast<int>(std::ceil (static_cast<float>(adjusted_query.end.z - 1) /(float) offset)) * offset - power_of_2;

            std::vector<int> x_starts = {left_start_x, right_start_x};
            std::vector<int> y_starts = {left_start_y, right_start_y};
            std::vector<int> z_starts = {left_start_z, right_start_z};
            
            // 6. Test all 8 combinations
            for (int sx : x_starts) {
                for (int sy : y_starts) {
                    for (int sz : z_starts) {
                        Rect3D candidate(
                            GridPoint3D(sx, sy, sz),
                            GridPoint3D(sx + power_of_2, sy + power_of_2, sz + power_of_2)
                        );
                        if (root_rect_.containsRect(candidate) && candidate.containsRect(adjusted_query)) {
                            return candidate;
                        }
                    }
                }
            }
        }

        // 7. If no cover found, double the size and try again
        power_of_2 <<= 1;
    }

    return root_rect_; // Fallback to the root if no smaller cover is found
}
