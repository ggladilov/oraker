#pragma once

#include <filesystem>
#include <vector>
#include "object.hpp"

namespace ork {

class Detector {
public:
    explicit Detector(std::filesystem::path modelPath = "./best_openvino_model/best.xml");
    std::vector<Object> operator()(cv::Mat const& image);

private:
    using Shape2D = cv::Size_<std::size_t>;
    cv::Mat letterbox(cv::Mat const& image, Shape2D newShape = {640, 640});
    cv::Mat preprocess(cv::Mat const& image);
    std::vector<Object> inference(cv::Mat const& image);

    std::filesystem::path modelPath;
};

}  // namespace ork
