#pragma once

#include <cstddef>
#include <opencv2/core/types.hpp>

namespace ork {

struct Object {
    cv::Rect box;
    std::size_t label;
    double score;
};

}  // namespace ork
