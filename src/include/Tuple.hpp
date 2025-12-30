#pragma once
#include <vector>

namespace utils {


struct Tuple { // the TSet entry for JXT
    std::vector<unsigned char> ct;                     // size:1
    std::vector<std::vector<unsigned char>> h_x;       // y, size:T
    std::vector<std::vector<unsigned char>> h_y;       // y', size:T
};

} // namespace utils
