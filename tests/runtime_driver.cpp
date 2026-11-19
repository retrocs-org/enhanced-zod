#include <cstdio>
#include <cerrno>
#include <climits>
#include <fstream>
#include <iostream>
#include <chrono>
#include <future>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#endif
#include "zhttp.h"
#include "zlocalserver.h"
#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#endif
#include "zmp_async.h"

// Inject an OS replacement failure after the new save has been written.
// The original save must survive this exact failure window.
static bool fail_replace = false;
#ifdef _WIN32
static BOOL replace_save(LPCSTR from, LPCSTR to, DWORD flags) {
    if(fail_replace) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return MoveFileExA(from, to, flags);
}
#define MoveFileExA replace_save
#else
static int replace_save(const char *from, const char *to) {
    if(fail_replace) { errno = EACCES; return -1; }
    return rename(from, to);
}
#define rename replace_save
#endif
#include "../src/zsavegame.cpp"
#ifdef _WIN32
#undef MoveFileExA
#else
#undef rename
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    if(argc >= 5 && std::string(argv[1]) == "local") {
        std::vector<std::string> args{argv[2], argv[3]};
        for(int i = 5; i < argc; ++i) args.push_back(argv[i]);
        std::string original_port = getenv("ZOD_PORT") ? getenv("ZOD_PORT") : "";
        ZLocalServerProcess process; std::string error;
        bool ok = ZLocalServer_Start(args, atoi(argv[4]), process, error);
        if(ok && !process.Running()) return 10;
        std::cout << ok << " " << error << "\n";
        if((getenv("ZOD_PORT") ? getenv("ZOD_PORT") : "") != original_port) return 12;
        ZLocalServerProcess moved(std::move(process));
        if(process.Running() || (ok && !moved.Running())) return 13;
        // Destruction, rather than an explicit Stop(), owns match cleanup.
        // Python checks that the listener is gone after this function returns.
        return 0;
    }
    if(argc >= 4 && std::string(argv[1]) == "http") {
        auto result = ZHttp_Get(argv[2], atoi(argv[3]), argc > 4 ? argv[4] : "/matches");
        std::cout << result.ok << " " << result.status << "\n" << result.body;
        return 0;
    }
    if(argc == 3 && std::string(argv[1]) == "save-serialization") {
        std::string path = argv[2];
        ZSaveInfo info;
        info.map_name = "maps/example.map"; info.map_title = "A saved battle";
        info.campaign_index = 7; info.one_off_map = true; info.elapsed = 123.5;
        info.kills = 19; info.lost = 7; info.difficulty = 2; info.when = 1794864000;
        ZSaveWriter writer; ZSave_WriteInfo(writer, info);
        writer.Int(INT_MIN); writer.Int(INT_MAX); writer.Double(-12.25);
        const std::string binary("a\0b", 3);
        writer.Str(binary); writer.Blob(nullptr, 0); writer.Blob(binary.data(), 3);
        if(!writer.WriteFile(path)) return 1;
        ZSaveReader reader; ZSaveInfo restored;
        if(!reader.ReadFile(path) || !ZSave_ReadInfo(reader, restored)) return 2;
        if(restored.map_name != info.map_name || restored.map_title != info.map_title ||
           restored.campaign_index != 7 || !restored.one_off_map || restored.elapsed != 123.5 ||
           restored.kills != 19 || restored.lost != 7 || restored.difficulty != 2 ||
           restored.when != info.when) return 3;
        if(reader.Int() != INT_MIN || reader.Int() != INT_MAX || reader.Double() != -12.25 ||
           reader.Str() != binary || !reader.Blob().empty()) return 4;
        auto blob = reader.Blob();
        if(std::string(blob.begin(), blob.end()) != binary || !reader.Ok()) return 5;
        reader.Int(); if(reader.Ok()) return 6; // no read beyond EOF
        std::ifstream input(path, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(input)), {});
        input.close();
        // Every truncated header must fail, even if it contains the correct magic.
        size_t header_size = 12 + 4 + info.map_name.size() + 4 + info.map_title.size() + 36;
        for(size_t length = 0; length < header_size; ++length) {
            std::ofstream truncated(path, std::ios::binary | std::ios::trunc);
            truncated.write(data.data(), length); truncated.close();
            if(!reader.ReadFile(path) || ZSave_ReadInfo(reader, restored)) return 7;
        }
        data[4] = 'X';
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt.write(data.data(), data.size()); corrupt.close();
        if(!reader.ReadFile(path) || ZSave_ReadInfo(reader, restored)) return 8;
        return 0;
    }
    if(argc == 3 && std::string(argv[1]) == "save") {
        std::string path = argv[2];
        ZSaveWriter old; old.Str("old save");
        if(!old.WriteFile(path)) return 1;
        ZSaveWriter replacement; replacement.Str("new save");
        fail_replace = true;
        if(replacement.WriteFile(path)) return 2;
        ZSaveReader reader;
        if(!reader.ReadFile(path) || reader.Str() != "old save") return 3;
        fail_replace = false;
        if(!replacement.WriteFile(path)) return 4;
        if(!reader.ReadFile(path) || reader.Str() != "new save") return 5;
        std::ifstream temporary(path + ".tmp");
        return temporary.good() ? 6 : 0;
    }
    if(argc == 2 && std::string(argv[1]) == "async") {
        std::promise<void> release, finished;
        auto gate = release.get_future().share();
        auto complete = finished.get_future();
        auto start = std::chrono::steady_clock::now();
        {
            ZMPAsync<int> task;
            task.Start([gate, &finished]() { gate.wait(); finished.set_value(); return 42; });
            int result;
            if(task.Poll(result)) return 1;
        } // destroying a menu must not wait for its request
        if(std::chrono::steady_clock::now() - start > std::chrono::milliseconds(500)) return 2;
        release.set_value();
        if(complete.wait_for(std::chrono::seconds(2)) != std::future_status::ready) return 3;
        ZMPAsync<int> task;
        task.Start([]() { return 42; });
        int value = 0;
        for(int i = 0; i < 1000; ++i) {
            if(task.Poll(value)) return value == 42 && !task.Pending() ? 0 : 4;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 5;
    }
    return 99;
}
