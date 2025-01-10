#include "pch.h"
#include <windows.h>
#include "MPQDumper.h"
#include <fstream>

// DLL 入口点
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        // 创建目录
        CreateDirectoryA("C:\\MPQDump", NULL);

        // 写入日志
        std::ofstream logFile("C:\\MPQDump\\dll_load.txt");
        if (logFile.is_open()) {
            logFile << "DLL已加载" << std::endl;
            logFile.close();
        }

        // 延迟创建线程，避免DLL加载时的线程安全问题
        Sleep(5000); // 延迟5秒，确保游戏完全加载
        CreateThread(NULL, 0, DumpMPQPaths, NULL, 0, NULL);
    }
    return TRUE;
}