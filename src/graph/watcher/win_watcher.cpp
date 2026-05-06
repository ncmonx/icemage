#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "win_watcher.hpp"
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace icmg::graph {

WinWatcher::WinWatcher() = default;

WinWatcher::~WinWatcher() {
    stop();
}

bool WinWatcher::start(const std::string& dir, Callback cb) {
    if (running_.load()) return false;

    HANDLE h = CreateFileA(
        dir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (h == INVALID_HANDLE_VALUE) return false;

    dir_handle_ = h;
    running_.store(true);
    thread_ = std::thread([this, dir, cb]() { watchLoop(dir, cb); });
    return true;
}

void WinWatcher::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (dir_handle_ && dir_handle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx((HANDLE)dir_handle_, nullptr);
        CloseHandle((HANDLE)dir_handle_);
        dir_handle_ = nullptr;
    }
    if (thread_.joinable()) thread_.join();
}

void WinWatcher::watchLoop(const std::string& dir, Callback cb) {
    constexpr DWORD BUF_SIZE = 65536;
    alignas(DWORD) char buf[BUF_SIZE];

    DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                   FILE_NOTIFY_CHANGE_LAST_WRITE |
                   FILE_NOTIFY_CHANGE_DIR_NAME;

    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    while (running_.load()) {
        DWORD bytes = 0;
        BOOL ok = ReadDirectoryChangesW(
            (HANDLE)dir_handle_,
            buf, BUF_SIZE,
            TRUE,   // watch subtree
            filter,
            &bytes, &ov, nullptr);

        if (!ok) break;

        DWORD wait = WaitForSingleObject(ov.hEvent, 500);
        if (wait == WAIT_TIMEOUT) continue;
        if (wait != WAIT_OBJECT_0) break;

        if (!GetOverlappedResult((HANDLE)dir_handle_, &ov, &bytes, FALSE)) break;
        if (bytes == 0) continue;

        auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf);
        while (true) {
            // Convert wide filename to UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0,
                info->FileName, info->FileNameLength / sizeof(wchar_t),
                nullptr, 0, nullptr, nullptr);
            std::string fname(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0,
                info->FileName, info->FileNameLength / sizeof(wchar_t),
                fname.data(), len, nullptr, nullptr);

            std::string fullpath = dir + "\\" + fname;

            WatchEvent ev;
            switch (info->Action) {
                case FILE_ACTION_ADDED:
                case FILE_ACTION_RENAMED_NEW_NAME: ev = WatchEvent::Created;  break;
                case FILE_ACTION_REMOVED:
                case FILE_ACTION_RENAMED_OLD_NAME: ev = WatchEvent::Deleted;  break;
                default:                           ev = WatchEvent::Modified; break;
            }

            cb(fullpath, ev);

            if (info->NextEntryOffset == 0) break;
            info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<char*>(info) + info->NextEntryOffset);
        }

        ResetEvent(ov.hEvent);
    }

    if (ov.hEvent) CloseHandle(ov.hEvent);
}

} // namespace icmg::graph
#endif // _WIN32
