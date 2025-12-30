#ifndef TDAG_SRC_3D_HPP
#define TDAG_SRC_3D_HPP

#include "emm_interface.hpp"
#include "index_interface.hpp"
#include "Dpoint_3d.hpp"
#include "Drect_3d.hpp"
#include "tdag.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

/**
 * @class TdagSRC3D
 * @brief Implements the Index_Interface using the Tdag-based Single Range Cover (SRC) strategy.
 *
 * This class is a concrete "plugin" that translates 3D spatial points and queries
 * into keyword labels suitable for a generic EMM engine.
 */
// TdagSRC3D class as modular plugin
class TdagSRC3D : public Index_Interface {
public:
    TdagSRC3D(int max_x, int max_y, int max_z);

    KeywordMap mapPointsToLabels(const PointMap3D& points) const override;
    std::vector<Label> getQueryLabels(const Rect3D& query_rect) const override;

private:
    int max_x_,max_y_,max_z_;
    std::shared_ptr<Tdag> x_tree_;
    std::shared_ptr<Tdag> y_tree_;
    std::shared_ptr<Tdag> z_tree_;

    std::string serialize_cover(
        const std::pair<int, int>& x_r, 
        const std::pair<int, int>& y_r, 
        const std::pair<int, int>& z_r) const;

};

#endif  // TDAG_SRC_3D_HPP
