#include <ApplicationServices/ApplicationServices.h>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <ranges>
#include <libproc.h>
#include <span>
#include <filesystem>
#include <regex>

auto findSafariPID() {
    static constexpr auto SAFARI_PROCESS_NAME = std::string_view{"Safari"};
    static constexpr auto MAX_PROCESS_CHUNK = static_cast<size_t>(2048);
    auto pids = std::array<pid_t, MAX_PROCESS_CHUNK>{};

    auto const actualProcessesCount = proc_listpids(PROC_ALL_PIDS, 0, reinterpret_cast<void*>(pids.data()), sizeof(pids)) / sizeof(pid_t);
    for (auto pid : std::span(pids).subspan(0, actualProcessesCount)) {
        proc_bsdinfo bsdInfo;
        auto const bytesWritten = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsdInfo, PROC_PIDTBSDINFO_SIZE);
        if (bytesWritten != PROC_PIDTBSDINFO_SIZE) {
            continue;
        }
        if (SAFARI_PROCESS_NAME == bsdInfo.pbi_name) {
            return pid;
        }
    }

    throw std::runtime_error("Failed to find Safari PID, it's either not running or too many processes present");
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

auto SaveCGImageToPNG(CGImageRef image, const std::string &filePath) {
    if (image == NULL) {
        std::cerr << "Invalid image reference." << std::endl;
        return false;
    }

    CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault, filePath.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path, kCFURLPOSIXPathStyle, false);
    CFRelease(path); // Release the CFStringRef object

    CGImageDestinationRef destination = CGImageDestinationCreateWithURL(url, kUTTypePNG, 1, NULL);
    CFRelease(url); // url can be released after creating the image destination

    if (!destination) {
        std::cerr << "Failed to create image destination." << std::endl;
        return false;
    }

    CGImageDestinationAddImage(destination, image, NULL);
    bool success = CGImageDestinationFinalize(destination);
    CFRelease(destination);

    return success;
}

auto CGImageToCVMat(CGImageRef image) {
    // Get image size
    size_t width = CGImageGetWidth(image);
    size_t height = CGImageGetHeight(image);
    size_t bitsPerComponent = CGImageGetBitsPerComponent(image);
    size_t bitsPerPixel = CGImageGetBitsPerPixel(image);
    size_t bytesPerRow = CGImageGetBytesPerRow(image);

    // Get pixel data
    CGDataProviderRef provider = CGImageGetDataProvider(image);
    CFDataRef dataRef = CGDataProviderCopyData(provider);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(CFDataGetBytePtr(dataRef));

    // Create a cv::Mat from the raw pixel data
    cv::Mat mat(height, width, bitsPerPixel == 32 ? CV_8UC4 : CV_8UC3, const_cast<uint8_t*>(data), bytesPerRow);

    // Convert to BGR format for OpenCV
    cv::Mat bgrMat;
    if (bitsPerPixel == 32) {
        cv::cvtColor(mat, bgrMat, cv::COLOR_RGBA2BGR);
    } else {
        bgrMat = mat.clone();
    }

    // Release CFDataRef
    CFRelease(dataRef);

    return bgrMat;
}

int main() {
    constexpr auto versionDirectoryName = std::string_view{"ver"};
    constexpr auto assetsDirectory = std::string_view{"./assets"};

    auto versionIndex = findLastVersionIndex(assetsDirectory, versionDirectoryName);
    auto imageIndex = findLastImageIndex(assetsDirectory, versionDirectoryName);
    auto const newVersionDirectoryName = std::string{versionDirectoryName} + std::to_string(++versionIndex);
    auto const newVersionPath = std::filesystem::path{assetsDirectory} / newVersionDirectoryName;
    assert(std::filesystem::create_directory(newVersionPath));

    auto const safariPID = findSafariPID();
    int keyCode = -1;

    do {
        auto const windowInfos = CGWindowListCopyWindowInfo(kCGWindowListExcludeDesktopElements, kCGNullWindowID);
        auto const windowInfosCount = CFArrayGetCount(windowInfos);
        for (CFIndex windowIndex = 0; windowIndex < windowInfosCount; ++windowIndex) {
            auto const windowInfo = reinterpret_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowInfos, windowIndex));
            auto const windowPIDRef = reinterpret_cast<CFNumberRef>(CFDictionaryGetValue(windowInfo, kCGWindowOwnerPID));
            int windowPID = -1;
            CFNumberGetValue(windowPIDRef, kCFNumberIntType, &windowPID);
            if (windowPID != safariPID) {
                continue;
            }

            CFStringRef windowNameRef;
            if (CFDictionaryGetValueIfPresent(windowInfo, kCGWindowName, reinterpret_cast<void const**>(&windowNameRef))) {
                auto const length = CFStringGetLength(windowNameRef);
                auto const maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
                char* windowName = new char[maxSize];
                if (!CFStringGetCString(windowNameRef, windowName, maxSize, kCFStringEncodingUTF8)) {
                    std::cerr << "Weren't able to find a window name\n";
                    continue;
                }

                if (strcmp(windowName, "Poker Now - Poker with Friends") != 0) {
                    delete[] windowName;
                    continue;
                }

                auto const windowIDRef = reinterpret_cast<CFNumberRef>(CFDictionaryGetValue(windowInfo, kCGWindowNumber));
                CGWindowID windowID = 0;
                CFNumberGetValue(windowIDRef, kCFNumberIntType, &windowID);
                auto const windowScreenShotRef = CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow, windowID, kCGWindowImageBestResolution);

                SaveCGImageToPNG(windowScreenShotRef, newVersionPath.native() + "/" + std::to_string(++imageIndex) + ".png");

                auto mat = CGImageToCVMat(windowScreenShotRef);
                cv::imshow("Test Image", mat);
                keyCode = cv::waitKey(0);

                delete[] windowName;
                break;
            }
        }

        if (keyCode == 113) {
            break;
        }

    } while (true);

    return 0;
}
