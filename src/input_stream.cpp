#include "oraker/input_stream.hpp"
#include "oraker/utilities.hpp"

#include <ApplicationServices/ApplicationServices.h>

#include <cstddef>
#include <vector>
#include <string_view>
#include <string>
#include <span>
#include <ranges>
#include <optional>
#include <expected>
#include <format>
#include <print>

#include <libproc.h>

using namespace ork;
using namespace std::literals;

namespace {

template <class Type>
class CFOwner {
public:
    CFOwner(Type resource) : owned(std::move(resource)) {}
    operator Type() const { return owned; }

    CFOwner(CFOwner&& rhs) {
        *this = std::move(rhs);
    }

    CFOwner& operator=(CFOwner&& rhs) noexcept {
        if (this == &rhs)
            return *this;

        owned = std::move(rhs.owned);
        rhs.owned = nullptr;
        return *this;
    }

    operator bool() const noexcept {
        return owned != nullptr;
    }

    ~CFOwner() {
        if (owned) {
            CFRelease(owned);
        }
    }

protected:
    Type owned;

private:
    CFOwner(CFOwner const&) = delete;
    CFOwner& operator=(CFOwner const&) = delete;
};

template <class Resource>
auto make_owned(Resource&& resource) -> CFOwner<std::decay_t<Resource>> {
    return reinterpret_cast<std::decay_t<Resource>>(const_cast<void*>(CFRetain(std::forward<Resource>(resource))));
}

template <class ValueType>
class CFArrayRefIterator {
public:
    using iterator_category = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = ValueType const;
    using pointer = value_type;
    using const_pointer = pointer;
    using reference = value_type;
    using const_reference = reference;

    CFArrayRefIterator(CFArrayRef array, std::size_t size, std::size_t position)
        : arrayRef(array), size(size), index(position) {}

    bool operator==(CFArrayRefIterator<ValueType> const& rhs) const noexcept {
        return arrayRef == rhs.arrayRef && index == rhs.index;
    }

    auto& operator++() noexcept {
        index = std::min(size, index + 1);
        return *this;
    }

    const_reference operator*() const noexcept {
        assert(index < size);
        return reinterpret_cast<value_type>(CFArrayGetValueAtIndex(arrayRef, index));
    }

private:
    CFArrayRef arrayRef;
    std::size_t index = 0z;
    std::size_t size = 0z;
};

template <class ValueType>
class CFArrayRefOwner : public CFOwner<CFArrayRef> {
public:
    using Base = CFOwner<CFArrayRef>;
    using iterator = CFArrayRefIterator<ValueType>;

    CFArrayRefOwner(CFArrayRef array) : Base(array), _size(0) {
        if (owned) {
            _size = static_cast<std::size_t>(CFArrayGetCount(owned));
        }
    }

    auto begin() const {
        return iterator{owned, size(), 0};
    }
    auto end() const {
        return iterator{owned, size(), size()};
    }
    auto size() const {
        return _size;
    }
    auto empty() const {
        return size() != 0;
    }

private:
    std::size_t _size;
};

template <class CFType>
auto getIfPresent(CFDictionaryRef const& dictionary, CFStringRef const& key) -> std::optional<CFOwner<CFType>> {
    CFType result;
    auto const isPresent = CFDictionaryGetValueIfPresent(dictionary, key, reinterpret_cast<void const**>(&result));
    return isPresent ? std::make_optional(make_owned(std::move(result))) : std::nullopt;
}

auto listPIDs(std::span<pid_t> buffer) {
    constexpr auto USE_PROC_LISTALLPIDS = int{0};
    auto const pidsCountInBytes = proc_listpids(
        PROC_ALL_PIDS,
        USE_PROC_LISTALLPIDS,
        reinterpret_cast<void*>(buffer.data()),
        buffer.size_bytes()
    );
    return pidsCountInBytes / sizeof(decltype(buffer)::value_type);
}

auto getPIDsCount() {
    return listPIDs({});
}

auto getPIDs() {
    auto pids = std::vector<pid_t>(getPIDsCount());
    auto const actualPIDsCount = listPIDs(pids);

    assert(actualPIDsCount <= pids.size());
    pids.resize(actualPIDsCount);
    return pids;
}

auto getPIDName(pid_t pid) -> std::optional<std::string> {
    constexpr auto NO_FALLBACK_TO_ZOMBIE = int{0};

    proc_bsdinfo bsdInfo;
    static_assert(sizeof(decltype(bsdInfo)) == PROC_PIDTBSDINFO_SIZE, "wrong buffer size for proc_pidinfo");

    auto const bytesWritten = proc_pidinfo(
        pid,
        PROC_PIDTBSDINFO,
        NO_FALLBACK_TO_ZOMBIE,
        &bsdInfo,
        PROC_PIDTBSDINFO_SIZE
    );

    if (bytesWritten != PROC_PIDTBSDINFO_SIZE) {
        return std::nullopt;
    }

    return std::string{bsdInfo.pbi_name};
}

auto findPID(std::string_view name) -> std::expected<pid_t, std::string> {
    auto const pids = getPIDs();
    for (auto pid : pids) {
        auto const maybePIDName = getPIDName(pid);
        if (!maybePIDName || maybePIDName != name)
            continue;
        return pid;
    }

    return std::unexpected{std::format("failed to find PID for '{}'", name)};
}

auto CGImage2Mat(CGImageRef image) -> std::expected<cv::Mat, std::string> {
    auto const provider = make_owned(CGImageGetDataProvider(image));
    auto const dataRefOwner = CFOwner(CGDataProviderCopyData(provider));
    if (!dataRefOwner)
        return std::unexpected{std::format("failed to convert CGImage to cv::Mat")};

    auto matView = cv::Mat(
        CGImageGetHeight(image),
        CGImageGetWidth(image),
        CGImageGetBitsPerPixel(image) == 32 ? CV_8UC4 : CV_8UC3,
        const_cast<uint8_t*>(CFDataGetBytePtr(dataRefOwner)),
        CGImageGetBytesPerRow(image)
    );

    cv::Mat result;
    if (matView.channels() == 4)
        cv::cvtColor(matView, result, cv::COLOR_RGBA2RGB);
    else
        result = matView;

    return result;
}

auto& getWindowIDCache() {
    static std::optional<CGWindowID> cache;
    return cache;
}

constexpr auto EXPECTED_WINDOW_NAME = "Poker Now - Poker with Friends"sv;

}  // namespace

InputStream::InputStream(std::string userName) : name(std::move(userName)) {}

std::expected<cv::Mat, std::string> InputStream::read() {
    auto& windowIDCache = getWindowIDCache();
    if (!windowIDCache) {
        auto const pid = findPID(name);
        if (!pid) {
            return std::unexpected(pid.error());
        }

        auto const windowsOwnedRef = CFArrayRefOwner<CFDictionaryRef>(
            CGWindowListCopyWindowInfo(kCGWindowListExcludeDesktopElements, kCGNullWindowID)
        );

        auto const getPID = [](auto info) -> std::optional<pid_t> {
            auto const maybePIDRefOwner = getIfPresent<CFNumberRef>(info, kCGWindowOwnerPID);
            if (!maybePIDRefOwner) return std::nullopt;
            pid_t pid;
            return CFNumberGetValue(*maybePIDRefOwner, kCFNumberIntType, &pid) ? std::make_optional(pid) : std::nullopt;
        };

        auto const getName = [](auto info) -> std::optional<std::string> {
            auto const maybeNameRefOwner = getIfPresent<CFStringRef>(info, kCGWindowName);
            if (!maybeNameRefOwner) return std::nullopt;
            auto const size = CFStringGetMaximumSizeForEncoding(
                CFStringGetLength(*maybeNameRefOwner),
                kCFStringEncodingUTF8
            ) + 1;
            auto nameBuffer = std::vector<char>(size);
            if (!CFStringGetCString(*maybeNameRefOwner, nameBuffer.data(), nameBuffer.size(), kCFStringEncodingUTF8))
                return std::nullopt;
            return std::string{std::from_range, nameBuffer | std::views::filter([](auto x) { return x != '\0'; })};
        };

        auto const getID = [](auto info) -> std::optional<CGWindowID> {
            auto const maybeIDRefOwner = getIfPresent<CFNumberRef>(info, kCGWindowNumber);
            if (!maybeIDRefOwner) return std::nullopt;
            CGWindowID id;
            return CFNumberGetValue(*maybeIDRefOwner, kCFNumberIntType, &id) ? std::make_optional(id) : std::nullopt;
        };

        for (auto windowInfo : windowsOwnedRef) {
            auto const maybePID = getPID(windowInfo);
            if (!maybePID || maybePID != pid)
                continue;
            auto const maybeName = getName(windowInfo);
            if (!maybeName || *maybeName != EXPECTED_WINDOW_NAME) {
                continue;
            }
            if ((windowIDCache = getID(windowInfo)))
                break;
        }

        if (!windowIDCache)
            return std::unexpected{
                std::format(
                    "no window found with name '{}' and PID {} (retrieved by key '{}')",
                    EXPECTED_WINDOW_NAME, *pid, name
                )
            };
    }

    auto windowRef = CFOwner(
        ORAKER_IGNORE_DEPRECATION_PUSH
        CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow, *windowIDCache, kCGWindowImageDefault)
        ORAKER_DIAGNOSTIC_POP
    );
    return CGImage2Mat(windowRef);
}

InputStream& ork::operator>>(InputStream& stream, cv::Mat& image) {
    auto frame = stream.read();
    if (!frame) {
        throw std::runtime_error(frame.error());
    }
    image = *frame;
    return stream;
}
