#include "oraker/detector.hpp"

#include "opencv2/opencv.hpp"

using namespace ork;

Detector::Detector(std::filesystem::path path) : modelPath(std::move(path)) {}

std::vector<Object> Detector::operator()(cv::Mat const& image) {
    return inference(image);
}

cv::Mat Detector::letterbox(cv::Mat const& image, Detector::Shape2D newShape) {
    auto const shape = Detector::Shape2D{image.cols, image.rows};
    auto const ratio = std::min(1.0, std::min(
        static_cast<double>(newShape.width) / shape.width,
        static_cast<double>(newShape.height) / shape.height
    ));
    auto const newShapeRatio = Detector::Shape2D{
        static_cast<std::size_t>(std::lround(shape.width * ratio)),
        static_cast<std::size_t>(std::lround(shape.height * ratio))
    };
    auto const shapePadding = Detector::Shape2D{
        std::abs(newShape.width - newShapeRatio.width) / 2,
        std::abs(newShape.height - newShapeRatio.height) / 2
    };

    cv::Mat resized;
    if (shape == newShapeRatio) {
        resized = image;
    } else {
        cv::resize(image, resized, newShapeRatio, 0.0, 0.0, cv::INTER_LINEAR);
    }

    auto const top = std::lround(shapePadding.height - 0.1);
    auto const bottom = std::lround(shapePadding.height + 0.1);
    auto const left = std::lround(shapePadding.width - 0.1);
    auto const right = std::lround(shapePadding.width + 0.1);
    auto const value = cv::Scalar_<int>{cv::Vec<int, 3>{114, 114, 114}};
    cv::copyMakeBorder(resized, resized, top, bottom, left, right, cv::BORDER_CONSTANT, value);
    return resized;
}

cv::Mat Detector::preprocess(cv::Mat const& image) {
    auto resized = letterbox(image);
    resized.convertTo(resized, CV_64FC3);
    resized /= 255.0;
    return resized;
}

std::vector<Object> inference(cv::Mat const& image) {

}
