// Throw-site capture impl. See throw_site.hpp.
#include "throw_site.hpp"

#include <deque>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace icmg::core {

namespace {

constexpr std::size_t kRingCap = 4;

std::mutex& ringMu() {
    static std::mutex m;
    return m;
}

std::deque<std::vector<ThrowFrame>>& ring() {
    static std::deque<std::vector<ThrowFrame>> r;
    return r;
}

}  // namespace

std::string formatThrowFrame(const ThrowFrame& f) {
    std::ostringstream os;
    os << (f.module.empty() ? "?" : f.module) << "+0x" << std::hex << f.offset;
    return os.str();
}

std::string formatThrowStack(const std::vector<ThrowFrame>& stack,
                             std::size_t maxFrames) {
    if (stack.empty()) return {};
    std::ostringstream os;
    std::size_t n = stack.size() < maxFrames ? stack.size() : maxFrames;
    for (std::size_t i = 0; i < n; ++i) {
        os << "        " << formatThrowFrame(stack[i]) << "\n";
    }
    return os.str();
}

void recordThrowStack(std::vector<ThrowFrame> stack) {
    std::lock_guard<std::mutex> g(ringMu());
    ring().push_back(std::move(stack));
    while (ring().size() > kRingCap) ring().pop_front();
}

std::string lastThrowStackFormatted() {
    std::lock_guard<std::mutex> g(ringMu());
    if (ring().empty()) return {};
    return formatThrowStack(ring().back());
}

void clearThrowStacksForTest() {
    std::lock_guard<std::mutex> g(ringMu());
    ring().clear();
}

#ifdef _WIN32

namespace {

// MSVC C++ exception SEH code (`throw` lowers to RaiseException with this).
constexpr DWORD kMsvcCppException = 0xE06D7363u;

std::string moduleBaseNameAt(void* pc, std::uint64_t& offsetOut) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(pc), &mod) ||
        !mod) {
        offsetOut = reinterpret_cast<std::uint64_t>(pc);
        return {};
    }
    offsetOut = reinterpret_cast<std::uint64_t>(pc) -
                reinterpret_cast<std::uint64_t>(mod);
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(mod, path, MAX_PATH);
    if (!len) return {};
    // base name, narrow best-effort (module names are ASCII in practice)
    std::wstring wp(path, len);
    std::size_t slash = wp.find_last_of(L"\\/");
    std::wstring base =
        (slash == std::wstring::npos) ? wp : wp.substr(slash + 1);
    std::string out;
    out.reserve(base.size());
    for (wchar_t c : base) out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
}

LONG CALLBACK firstChanceCppHandler(PEXCEPTION_POINTERS info) {
    if (info && info->ExceptionRecord &&
        info->ExceptionRecord->ExceptionCode == kMsvcCppException) {
        void* pcs[32] = {};
        USHORT n = RtlCaptureStackBackTrace(0, 32, pcs, nullptr);
        std::vector<ThrowFrame> frames;
        frames.reserve(n);
        for (USHORT i = 0; i < n; ++i) {
            ThrowFrame f;
            f.module = moduleBaseNameAt(pcs[i], f.offset);
            frames.push_back(std::move(f));
        }
        recordThrowStack(std::move(frames));
    }
    return EXCEPTION_CONTINUE_SEARCH;  // observe only, never handle
}

}  // namespace

void installThrowSiteCapture() {
    static bool installed = [] {
        AddVectoredExceptionHandler(/*first=*/0, firstChanceCppHandler);
        return true;
    }();
    (void)installed;
}

#else

void installThrowSiteCapture() {}

#endif

}  // namespace icmg::core
