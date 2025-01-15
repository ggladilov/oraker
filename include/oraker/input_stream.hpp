#pragma once

#include <string>
#include <expected>

#include <opencv2/opencv.hpp>

namespace ork {

class InputStream {
public:
    explicit InputStream(std::string name);
    std::expected<cv::Mat, std::string> read();

private:
    std::string name;
};

InputStream& operator>>(InputStream&, cv::Mat&);

}  // namespace ork
