// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include "common/types.h"

#ifdef _WIN32
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <Windows.h>
#include <queue>
#include <winternl.h>
#endif

namespace FS {
namespace Utils {

#ifdef _WIN32
// --------------------
// NTSTATUS macro
// --------------------
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// ----------------------------------------------
// FILE_STANDARD_INFORMATION
// ----------------------------------------------
typedef struct _FILE_STANDARD_INFORMATION {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

// ----------------------------------------------
// FILE_INFORMATION_CLASS enum
// Add only what you need
// ----------------------------------------------
#ifndef FileStandardInformation
#define FileStandardInformation ((FILE_INFORMATION_CLASS)5)
#endif

//
// FILE_INFORMATION_CLASS additions
//
#ifndef FileIdBothDirectoryInformation
#define FileIdBothDirectoryInformation ((FILE_INFORMATION_CLASS)37)
#endif

//
// FILE_ID_BOTH_DIR_INFORMATION
//
// This struct is NOT provided in normal Windows SDK, only in WDK.
// We define the full stable version here.
//
typedef struct _FILE_ID_BOTH_DIR_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;

    ULONG EaSize;
    CCHAR ShortNameLength;
    WCHAR ShortName[12];

    // Added fields for FileIdBothDirectoryInformation
    LARGE_INTEGER FileId; // 64-bit file ID
    WCHAR FileName[1];    // variable length
} FILE_ID_BOTH_DIR_INFORMATION, *PFILE_ID_BOTH_DIR_INFORMATION;

extern "C" {
NTSYSCALLAPI NTSTATUS NTAPI NtQueryDirectoryFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID,
                                                 PIO_STATUS_BLOCK, PVOID, ULONG,
                                                 FILE_INFORMATION_CLASS, BOOLEAN, PUNICODE_STRING,
                                                 BOOLEAN);

NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG,
                                                   FILE_INFORMATION_CLASS);
}

// ========================================================
// Job queue: directories and raw paths
// ========================================================
struct WorkDir {
    std::wstring path;
};

struct FileEntry {
    std::wstring fullPath;
};

static std::wstring Utf8ToUtf16(const std::string& s) {
    if (s.empty())
        return std::wstring();

    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0)
        return std::wstring();

    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

// ========================================================
// Parallel NT-native directory size
// ========================================================
u64 GetDirSize(const std::string& path, u64 rounding_alignment, std::atomic<bool>* cancelFlag) {
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0)
        numThreads = 1;

    std::wstring rootPath = Utf8ToUtf16(path);
    std::mutex mtx;
    std::condition_variable cv;

    std::queue<WorkDir> dirQueue;
    std::queue<FileEntry> fileQueue;
    bool producerDone = false;

    // ====================================================
    // Worker threads: read FileStandardInformation
    // ====================================================
    auto worker = [&](u64& localSum) {
        std::unique_lock<std::mutex> lock(mtx);

        while (true) {
            cv.wait(lock, [&] {
                return !fileQueue.empty() || producerDone || (cancelFlag && cancelFlag->load());
            });

            if (cancelFlag && cancelFlag->load())
                return;

            if (fileQueue.empty()) {
                if (producerDone)
                    return;
                continue;
            }

            FileEntry entry = std::move(fileQueue.front());
            fileQueue.pop();
            lock.unlock();

            // ---------------------------------------------------------
            // Query size using NtQueryInformationFile
            // ---------------------------------------------------------
            HANDLE h = CreateFileW(entry.fullPath.c_str(), FILE_READ_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

            if (h != INVALID_HANDLE_VALUE) {
                IO_STATUS_BLOCK ios;
                FILE_STANDARD_INFORMATION info = {};

                NTSTATUS st =
                    NtQueryInformationFile(h, &ios, &info, sizeof(info), FileStandardInformation);

                if (NT_SUCCESS(st)) {
                    u64 size = info.EndOfFile.QuadPart;

                    // Alignment
                    if (rounding_alignment > 1) {
                        u64 rem = size % rounding_alignment;
                        if (rem)
                            size += rounding_alignment - rem;
                    }

                    localSum += size;
                }

                CloseHandle(h);
            }

            lock.lock();
        }
    };

    // ====================================================
    // Producer: NT-native recursive directory enumeration
    // ====================================================
    auto producer = [&]() {
        std::vector<std::wstring> stack;
        stack.push_back(rootPath);

        while (!stack.empty() && !(cancelFlag && cancelFlag->load())) {
            std::wstring current = std::move(stack.back());
            stack.pop_back();

            // Open directory handle
            HANDLE hDir = CreateFileW(current.c_str(), FILE_LIST_DIRECTORY,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

            if (hDir == INVALID_HANDLE_VALUE)
                continue;

            BYTE buffer[64 * 1024]; // large for fewer syscalls
            IO_STATUS_BLOCK ios;

            while (true) {
                NTSTATUS st = NtQueryDirectoryFile(hDir, nullptr, nullptr, nullptr, &ios, buffer,
                                                   sizeof(buffer), FileIdBothDirectoryInformation,
                                                   FALSE, nullptr, FALSE);

                if (!NT_SUCCESS(st))
                    break;

                PFILE_ID_BOTH_DIR_INFORMATION info = (PFILE_ID_BOTH_DIR_INFORMATION)buffer;

                while (true) {
                    // Skip "." and ".."
                    if (info->FileNameLength > 0) {
                        std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));

                        if (name != L"." && name != L"..") {
                            std::wstring fullpath = current + L"\\" + name;

                            if (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                                // Push subdirectory to stack (DFS)
                                stack.push_back(fullpath);
                            } else {
                                // Push file to fileQueue
                                {
                                    std::lock_guard<std::mutex> lock(mtx);
                                    fileQueue.push({fullpath});
                                }
                                cv.notify_one();
                            }
                        }
                    }

                    if (info->NextEntryOffset == 0)
                        break;

                    info = (PFILE_ID_BOTH_DIR_INFORMATION)(((BYTE*)info) + info->NextEntryOffset);
                }
            }

            CloseHandle(hDir);
        }

        // Signal workers that we’re done
        {
            std::lock_guard<std::mutex> lock(mtx);
            producerDone = true;
        }
        cv.notify_all();
    };

    // ====================================================
    // Launch worker threads
    // ====================================================
    std::vector<u64> partial(numThreads, 0);
    std::vector<std::thread> threads;

    for (unsigned i = 0; i < numThreads; i++)
        threads.emplace_back(worker, std::ref(partial[i]));

    // Start producer in current thread (or separate thread)
    producer();

    // Join workers
    for (auto& t : threads)
        t.join();

    // Sum results
    u64 total = 0;
    for (u64 v : partial)
        total += v;

    return total;
}
#else
u64 GetDirSize(const std::string& path, u64 rounding_alignment, std::atomic<bool>* cancel_flag) {
    namespace stdfs = std::filesystem;

    const stdfs::path dir_path(path);
    if (!stdfs::exists(dir_path) || !stdfs::is_directory(dir_path))
        return 0;

    u64 total_size = 0;
    std::error_code ec;

    stdfs::directory_options opts = stdfs::directory_options::skip_permission_denied;

    for (stdfs::recursive_directory_iterator it(dir_path, opts, ec), end; it != end && !ec;
         it.increment(ec)) {
        if (cancel_flag && cancel_flag->load())
            return total_size; // early exit on cancel

        if (!it->is_regular_file(ec))
            continue;

        u64 size = it->file_size(ec);
        if (ec)
            continue;

        // Apply rounding alignment if requested
        if (rounding_alignment > 1) {
            const u64 remainder = size % rounding_alignment;
            if (remainder)
                size += (rounding_alignment - remainder);
        }

        total_size += size;
    }

    return total_size;
}
#endif

} // namespace Utils
} // namespace FS
