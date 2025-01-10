#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

// 检查管理员权限
bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;

    // 创建管理员组的 SID
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(
        &NtAuthority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &adminGroup)) {
        // 检查当前进程是否属于管理员组
        if (!CheckTokenMembership(NULL, adminGroup, &isAdmin)) {
            isAdmin = FALSE;
        }
        FreeSid(adminGroup);
    }

    return isAdmin == TRUE;
}

// 自动请求以管理员权限重新运行
void RelaunchAsAdmin() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH); // 获取当前程序的路径

    SHELLEXECUTEINFOA sei = { sizeof(SHELLEXECUTEINFOA) };
    sei.lpVerb = "runas";           // 请求管理员权限
    sei.lpFile = exePath;           // 当前程序路径
    sei.hwnd = NULL;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExA(&sei)) {
        DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            std::cout << "用户取消了管理员权限请求" << std::endl;
        }
        else {
            std::cout << "请求管理员权限失败, 错误码: " << error << std::endl;
        }
    }
    else {
        std::cout << "以管理员权限重新启动程序..." << std::endl;
    }
    exit(0); // 退出当前程序，等待以管理员权限运行的进程启动
}

DWORD GetProcessIdByName(const wchar_t* processName) {
    DWORD processId = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::cout << "无法创建进程快照, 错误码: " << GetLastError() << std::endl;
        return 0;
    }

    PROCESSENTRY32W processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &processEntry)) {
        do {
            if (_wcsicmp(processEntry.szExeFile, processName) == 0) {
                processId = processEntry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &processEntry));
    }
    else {
        std::cout << "Process32FirstW 失败, 错误码: " << GetLastError() << std::endl;
    }

    CloseHandle(snapshot);

    if (processId == 0) {
        std::cout << "未找到目标进程: " << processName << std::endl;
    }
    return processId;
}


DWORD WINAPI LoadDllAndGetError(LPVOID lpParam) {
    HMODULE hModule = LoadLibraryA((LPCSTR)lpParam);
    if (!hModule) {
        return GetLastError();
    }
    return 1;
}

bool InjectDLL(DWORD processId, const char* dllPath) {
    std::cout << "开始注入过程..." << std::endl;

    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, processId);

    if (!hProcess) {
        std::cout << "打开进程失败, 错误码: " << GetLastError() << std::endl;
        return false;
    }

    SIZE_T pathLen = (strlen(dllPath) + 1);
    LPVOID pszLibFileRemote = VirtualAllocEx(hProcess, NULL, pathLen,
        MEM_COMMIT, PAGE_READWRITE);

    if (!pszLibFileRemote) {
        std::cout << "分配内存失败, 错误码: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, pszLibFileRemote, dllPath, pathLen, NULL)) {
        std::cout << "写入内存失败, 错误码: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pszLibFileRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel32 = GetModuleHandleA("Kernel32");
    FARPROC pfnLoadLibrary = GetProcAddress(hKernel32, "LoadLibraryA");

    std::cout << "创建远程线程..." << std::endl;
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)pfnLoadLibrary, pszLibFileRemote, 0, NULL);

    if (!hThread) {
        std::cout << "创建线程失败, 错误码: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pszLibFileRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    DWORD waitResult = WaitForSingleObject(hThread, INFINITE);
    std::cout << "等待结果: " << waitResult << std::endl;

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    std::cout << "LoadLibrary返回值: 0x" << std::hex << exitCode << std::endl;

    if (exitCode == 0) {
        // 获取目标进程中的最后错误码
        HMODULE hKernel32 = GetModuleHandleA("Kernel32");
        FARPROC pfnGetLastError = GetProcAddress(hKernel32, "GetLastError");

        HANDLE hErrorThread = CreateRemoteThread(hProcess, NULL, 0,
            (LPTHREAD_START_ROUTINE)pfnGetLastError, NULL, 0, NULL);

        if (hErrorThread) {
            WaitForSingleObject(hErrorThread, INFINITE);
            DWORD errorCode;
            GetExitCodeThread(hErrorThread, &errorCode);
            std::cout << "目标进程错误码: " << std::dec << errorCode << std::endl;
            CloseHandle(hErrorThread);
        }
    }

    VirtualFreeEx(hProcess, pszLibFileRemote, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    return exitCode != 0;
}


int main() {

    if (!IsRunningAsAdmin()) {
        std::cout << "程序未以管理员权限运行，尝试重新启动..." << std::endl;
        RelaunchAsAdmin();
        return 0;
    }
    std::cout << "程序以管理员权限运行" << std::endl;

    // 获取exe所在目录
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    // 获取目录部分
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) {
        *lastSlash = 0;
    }

    std::cout << "EXE目录: " << exePath << std::endl;

    char fullDllPath[MAX_PATH];
    sprintf_s(fullDllPath, "%s\\MPQDumper.dll", exePath);
    std::cout << "尝试加载: " << fullDllPath << std::endl;

    HMODULE hDll = LoadLibraryA(fullDllPath);
    if (!hDll) {
        DWORD error = GetLastError();
        char sysMsg[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            sysMsg, sizeof(sysMsg), NULL);
        std::cout << "加载失败详细信息: " << sysMsg << std::endl;
        system("pause");
        return 1;
    }

    FreeLibrary(hDll);

    DWORD processId = GetProcessIdByName(L"Wow.exe");

    if (!processId) {
        std::cout << "未找到WoW进程" << std::endl;
        system("pause");
        return 1;
    }

    std::cout << "完整DLL路径: " << fullDllPath << std::endl;
    std::cout << "进程ID: " << processId << std::endl;

    if (InjectDLL(processId, fullDllPath)) {
        std::cout << "DLL注入成功" << std::endl;
    }

    system("pause");
    return 0;
}

