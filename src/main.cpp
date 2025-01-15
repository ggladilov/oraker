#include <ApplicationServices/ApplicationServices.h>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <ranges>
#include <libproc.h>
#include <span>
#include <filesystem>
#include <regex>
#include <fstream>

#include "openvino/openvino.hpp"
#include "oraker/input_stream.hpp"
#include "oraker/utilities.hpp"

struct Object {
    cv::Rect box;
    float score;
    std::size_t label;
};

std::ostream& operator<<(std::ostream& stream, Object const& target) {
    stream << '{';
    stream << '[' << target.box.x << ", " << target.box.y << ", " << target.box.width << ", " << target.box.height << ']';
    stream << ", " << target.score;
    stream << ", " << target.label;
    stream << '}';
    return stream;
}

void inference(cv::Mat const& input) {
    cv::imshow("Network Input", input);
    std::cerr << input.channels() << 'x' << input.rows << 'x' << input.cols << '\n';
    cv::imwrite("./network_input.png", input);

    ov::Core core;
    auto model = core.read_model("./best_openvino_model/best.xml");

    auto const preprocessed = preprocess(input);
    std::cerr << preprocessed.channels() << 'x' << preprocessed.rows << 'x' << preprocessed.cols << '\n';

    auto inputTensor = ov::Tensor{
        ov::element::f32,
        ov::Shape{{
            1UL,
            static_cast<unsigned long>(preprocessed.rows),
            static_cast<unsigned long>(preprocessed.cols),
            static_cast<unsigned long>(preprocessed.channels())
        }},
        preprocessed.data
    };

    ov::preprocess::PrePostProcessor ppp(model);
    ppp.input().tensor().set_element_type(inputTensor.get_element_type()).set_shape(inputTensor.get_shape()).set_layout("NHWC");
    ppp.input().preprocess().convert_layout({0, 3, 1, 2});
    ppp.input().model().set_layout("NCHW    ");
    ppp.output().tensor().set_element_type(ov::element::f32).set_layout("CHW");

    model = ppp.build();

    auto compiledModel = core.compile_model(model, "CPU");
    auto request = compiledModel.create_infer_request();
    request.set_input_tensor(inputTensor);
    request.infer();

    const auto output = request.get_output_tensor();
    std::cout << output.get_element_type() << '\n';
    std::cout << output.get_shape() << '\n';

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<std::size_t> labels;
    std::vector<int> indices;

    // {
    //     std::vector<float> t(57*8400);
    //     for (std::size_t i = 0; i < 57; ++i)
    //     for (std::size_t j = 0; j < 8400; ++j)
    //         t[j * 57 + i] = *(output.data<float>() + i * 8400 + j);
    //     std::ofstream ofs("./ov_out_raw.txt");
    //     for (std::size_t i = 0; i < 8400; ++i) {
    //         for (std::size_t j = 0; j < 57; ++j)
    //             ofs << t[i * 57 + j] << ' ';
    //         ofs << '\n';
    //     }
    // }

    auto const objectAttributes = output.get_shape()[1];
    auto const objectsCount = output.get_shape()[2];

    auto transposedResult = cv::Mat{static_cast<int>(objectAttributes), static_cast<int>(objectsCount), CV_32F, output.data<float>()};
    transposedResult = transposedResult.t();
    std::cout << transposedResult.channels() << 'x' << transposedResult.rows << 'x' << transposedResult.cols << '\n';

    constexpr auto boxRank = static_cast<std::size_t>(4);
    constexpr auto scoreThreshold = static_cast<double>(0.25);

    // auto const factorX = input.cols / static_cast<double>(inputTensor.get_shape()[2]);
    // auto const factorY = input.rows / static_cast<double>(inputTensor.get_shape()[1]);

    for (std::size_t objectIndex = 0; objectIndex < objectsCount; ++objectIndex) {
        auto const objectData = transposedResult.row(objectIndex).ptr<float>();
        auto const objectBox = std::span(objectData, boxRank);
        auto const objectScores = std::span(objectData + boxRank, objectAttributes - boxRank);
        auto const maxScoreIter = std::ranges::max_element(objectScores);
        if (static_cast<double>(*maxScoreIter) <= scoreThreshold) {
            continue;
        }

        // auto const centerX = objectBox[0];
        // auto const centerY = objectBox[1];
        // auto const boxCols = objectBox[2];
        // auto const boxRows = objectBox[3];

        // auto const x = std::lround((centerX - 0.5 * boxCols) * factorX);
        // auto const y = std::lround((centerY - 0.5 * boxRows) * factorY);
        // auto const w = std::lround(boxCols * factorX);
        // auto const h = std::lround(boxRows * factorY);

        // img1 = network input = inputTensor = preprocessed
        // img0 = original image = input

        auto const gain = std::min(preprocessed.cols / static_cast<double>(input.cols), preprocessed.rows / static_cast<double>(input.rows));
        auto const pad = std::make_pair(
            std::llround((preprocessed.rows - input.rows * gain) / 2. - 0.1),
            std::llround((preprocessed.cols - input.cols * gain) / 2. - 0.1)
        );

        auto xc = static_cast<double>(objectBox[0]);
        auto yc = static_cast<double>(objectBox[1]);
        auto w = static_cast<double>(objectBox[2]);
        auto h = static_cast<double>(objectBox[3]);

        auto x0 = xc - w / 2.;
        auto y0 = yc - h / 2.;
        auto x1 = xc + w / 2.;
        auto y1 = yc + h / 2.;

        x0 -= pad.second;
        y0 -= pad.first;
        x1 -= pad.second;
        y1 -= pad.first;

        x0 /= gain;
        y0 /= gain;
        x1 /= gain;
        y1 /= gain;

        auto const x0i = std::llround(std::clamp(x0, 0., static_cast<double>(input.cols)));
        auto const y0i = std::llround(std::clamp(y0, 0., static_cast<double>(input.rows)));
        auto const x1i = std::llround(std::clamp(x1, 0., static_cast<double>(input.cols)));
        auto const y1i = std::llround(std::clamp(y1, 0., static_cast<double>(input.rows)));

        boxes.emplace_back(x0i, y0i, x1i - x0i, y1i - y0i);
        scores.push_back(*maxScoreIter);
        labels.push_back(std::ranges::distance(std::begin(objectScores), maxScoreIter));
    }

    {
        std::vector<Object> objects;
        auto const toObject = [&boxes, &scores, &labels](auto index) -> Object {
            return {boxes[index], scores[index], labels[index]};
        };
        std::vector<std::size_t> objectsIndices(boxes.size());
        std::iota(std::begin(objectsIndices), std::end(objectsIndices), 0);
        std::ranges::copy(std::views::transform(objectsIndices, toObject), std::back_inserter(objects));
        auto const compare = [](auto const& lhs, auto const& rhs) { return lhs.score >= rhs.score; };
        std::sort(std::begin(objects), std::end(objects), compare);
        std::ofstream ofs("./ov_out.txt");
        std::ranges::copy(objects, std::ostream_iterator<typename decltype(objects)::value_type>(ofs, "\n"));
    }

    cv::dnn::NMSBoxes(boxes, scores, 0.25f, 0.7f, indices);

    auto const objectsIndices = std::span(indices.data(), indices.size());

    std::vector<Object> objects;
    auto const toObject = [&boxes, &scores, &labels](auto index) -> Object {
        return {boxes[index], scores[index], labels[index]};
    };
    std::ranges::copy(std::views::transform(objectsIndices, toObject), std::back_inserter(objects));
    std::ranges::copy(objects, std::ostream_iterator<typename decltype(objects)::value_type>(std::cout, "\n"));

    auto const& front = objects.front();
    auto const& box = front.box;
    cv::rectangle(input, box, cv::Scalar{114., 114., 114.});
    // cv::rectangle(input, cv::Point{input.cols / 2 - 50, input.rows / 2 - 50}, cv::Point{input.cols / 2 + 50, input.rows / 2 + 50}, cv::Scalar{255., 255., 255.}, 10);
    // auto const text = std::string("♠︎");
    // float xsize = cv::getTextSize("@", 1, 1, 1, 0).width;

    // auto const size = cv::getTextSize(text, cv::FONT_HERSHEY_COMPLEX, 1., 2., nullptr);
    // auto const textBox = cv::Rect{
        // box.x,
        // box.y,
        // size.width + 10,
        // size.height + 20
    // };

    // cv::rectangle(input, textBox, cv::Scalar{114., 114., 114.});
    // cv::putText(input, text, cv::Point(box.x + 10, box.y - 10), cv::FONT_HERSHEY_DUPLEX, 1., cv::Scalar{0., 0., 0.}, 2., 0);

    // std::ofstream ofs("./inference.txt");
    // float maxConfidence = 0.;
    // for (std::size_t i = 0; i < 57; ++i) {
    //     for (std::size_t j = 0; j < 8400; ++j){
    //         ofs << output.data<float>()[i * 8400 + j] << " ";
    //         if (i > 3) {
    //             maxConfidence = std::max(maxConfidence, output.data<float>()[i * 8400 + j]);
    //         }
    //     }
    //     ofs << '\n';
    // }

    // std::cout << "Max conf = " << maxConfidence << '\n';

    // auto net = cv::dnn::readNetFromONNX("./best.onnx");
    cv::imshow("Network Input", input);
}



auto findLastVersionIndex(std::filesystem::path const& assetsDirectory, std::string_view versionDirectoryName) {
    auto isVersion = [versionDirectoryName](auto filename) {
        std::regex matcher{"^" + std::string{versionDirectoryName} + "([0-9]+)$"};
        std::smatch match;
        return std::regex_match(filename, match, matcher);
    };

    auto extractIndex = [versionDirectoryName](auto filename) {
        return static_cast<std::size_t>(std::stoi(filename.substr(versionDirectoryName.size())));
    };

    return std::ranges::max(
        std::filesystem::directory_iterator{assetsDirectory} |
        std::views::filter([](auto entry) { return entry.is_directory(); }) |
        std::views::transform([](auto entry) { return entry.path().filename().string(); }) |
        std::views::filter(isVersion) |
        std::views::transform(extractIndex));
}

auto findLastVersionDirectory(std::filesystem::path const& assetsDirectory, std::string_view versionDirectoryName) {
    auto const lastVersionFolder = std::string{versionDirectoryName}.append(std::to_string(findLastVersionIndex(assetsDirectory, versionDirectoryName)));
    return assetsDirectory / lastVersionFolder;
}

auto findLastImageIndex(std::filesystem::path const& assetsDirectory, std::string_view versionDirectoryName) {
    auto isImage = [](auto filename) {
        std::regex matcher{"^([0-9]+).png$"};
        std::smatch match;
        return std::regex_match(filename, match, matcher);
    };

    auto extractIndex = [](auto filename) {
        return static_cast<std::size_t>(std::stoi(filename.substr(0, filename.find_first_of('.'))));
    };

    return std::ranges::max(
        std::filesystem::directory_iterator{findLastVersionDirectory(assetsDirectory, versionDirectoryName)} |
        std::views::filter([](auto entry) { return entry.is_regular_file(); }) |
        std::views::transform([](auto entry) { return entry.path().filename().string(); }) |
        std::views::filter(isImage) |
        std::views::transform(extractIndex));
}

int main() {

    // while (true)
    //    Generate Prediction
    //    If Changes in prediction
    //        Display
    //    Else
    //        Continue
    //    If input is wrong prediction
    //        Store Prediction
    //        Request Correction
    //    Calculate Probabilities
    //    Display Probabilities
    //    If input is exit
    //        Exit

    // Modules:
    //     macOS-specific code (InputStream): find Safari, make screenshot, convert to cv::Mat?
    //     NN code (Detector): pre-processing, inference, post-processing, compare with previous?
    //     GUI (DebugVisualizer, Visualizer): display input image, display predictions
    //     Compute probabilities (Analyzer): calculate chance(s) to win or per combination
    //     Console output (View, IO?): print game state, print probabilities, ask for and take corrections
    //     Save input + predictions for post re-train (Serializer)
    //     Based on user input dispatch corresponding action (Dispatcher): continue, re-compute probabilities (after corrections input), quit
    //     Apply corrections (Corrector): get Predictions, apply last correction (if present), set last correction from View
    //     Parse prediction into state (Game)

    // Single-threaded
    // while (true)
    //    Get New Prediction
    //    Display Prediction
    //    Display Game State (For Corrections If Needed)
    //    Calculate Probabilities
    //    Display Probabilities
    //    Wait for Input
    //        Quit -> Exit
    //        Continue -> Continue
    //        Error -> Save Image & Prediction For Re-Train
    //                 Request Correction
    //                 Display New Probability

    // auto lastInput;
    // auto lastState;
    // View view;

    // auto processState = [](Input newInput, State newState){
    //     Visualizer visualizer;

    //     if (newInput == lastInput && newState == lastState)
    //         return;

    //     lastInput = newInput;

    //     visualizer.display(newInput);
    //     visualizer.display(newState);

    //     if (newState != lastState)
    //         lastState = newState;
    //         Analyzer analyzer;
    //         auto probability = analyzer(lastState);

    //     /* display(state, probability); */
    //     view.display(lastState);
    //     view.display(probability);
    // };

    // auto iteration = [](){
    //     InputStream stream;
    //     auto input = stream.read();

    //     Detector detector;
    //     lastPrediction = detector(input);

    //     Game game;
    //     auto state = game.parse(lastPrediction);
    //     processState(input, state);
    // };

    // auto correction = [&correction]{
    //     // includes option to re-use last correction and optionally build on top of it
    //     view.display(requestCorrectionMenu);
    //     auto userCorrection = view.getUserInput(); // to be tested
    //     // pass reference to last correction to read from in case of request to build on top of it
    //     correction.append(correction);
    // };

    // Dispatcher::Correction
    //     Correction correction;
    //     do {
    //         correction();
    //     } while (userInput != Correction::Complete);
    //     Corrector corrector;
    //     auto newState = corrector(correction, lastState);
    //     processState(lastInput, newState);


    // Dispatcher dispatcher;
    // dispatcher.onContinue(iteration);
    // dispatcher.onExit(exit);
    // dispatcher.onCorrection();

    // Dispatcher::getCommand()
    //     view.display(requestCommandMenu);
    //     auto userInput = view.getUserInput();
    //     return Command(userInput);

    // Dispatcher::dispatch(command)
    //     switch (command) {
    //         Command::Continue: continueCallback();
    //         Command::Correction: correctionCallback();
    //     }

    // while (true) {
    //     auto command = dispatcher.getCommand();
    //     if (dispatcher.isExit(command)) {
    //         break;
    //     }
    //     dispatcher.dispatch(command);
    // }


    // 1. Convert classID in prediction to visual card value representation
    // 2. Terminal menu-mode ♠ ♦ ♣ ♥

    constexpr auto versionDirectoryName = std::string_view{"ver"};
    constexpr auto assetsDirectory = std::string_view{"./assets/images"};

    // auto versionIndex = findLastVersionIndex(assetsDirectory, versionDirectoryName);
    // auto imageIndex = findLastImageIndex(assetsDirectory, versionDirectoryName);
    // auto const newVersionDirectoryName = std::string{versionDirectoryName} + std::to_string(++versionIndex);
    // auto const newVersionPath = std::filesystem::path{assetsDirectory} / newVersionDirectoryName;
    // assert(std::filesystem::create_directory(newVersionPath));

    ork::InputStream istream("Safari");
    cv::Mat lastInput;

    do {
        try {
            istream >> lastInput;
        } catch (std::runtime_error const& error) {
            std::cerr << std::format("Failed to capture game frame, error: '{}'", error.what()) << '\n';
            return 1;
        } catch (...) {
            std::cerr << "Failed to capture game frame, unknown error\n";
            return -1;
        }

        std::cerr << lastInput.channels() << 'x' << lastInput.rows << 'x' << lastInput.cols << '\n';
        inference(lastInput);

        auto keyCode = cv::waitKey(0);
        if (keyCode == 113) {
            break;
        }

    } while (true);

    return 0;
}
