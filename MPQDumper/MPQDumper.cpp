#include "pch.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include "MPQDumper.h" 

#pragma pack(push, 1)
struct MPQHeader {
    DWORD magic; // 'MPQ\x1A'
    DWORD headerSize;
    DWORD archiveSize;
    DWORD formatVersion;
    DWORD sectorSize;
    DWORD hashTableOffset;
    DWORD blockTableOffset;
    DWORD hashTableEntries;
    DWORD blockTableEntries;
};

struct MPQHashTableEntry {
    DWORD hashA;
    DWORD hashB;
    WORD locale;
    WORD platform;
    DWORD blockIndex;
};

struct MPQBlockTableEntry {
    DWORD fileOffset;
    DWORD fileSize;
    DWORD compressedSize;
    DWORD flags;
};
#pragma pack(pop)

unsigned long cryptTable[0x500];

void PrepareCryptTable() {
    unsigned long seed = 0x00100001, index1 = 0, index2 = 0, i;

    for (index1 = 0; index1 < 0x100; index1++) {
        for (index2 = index1, i = 0; i < 5; i++, index2 += 0x100) {
            unsigned long temp1, temp2;

            seed = (seed * 125 + 3) % 0x2AAAAB;
            temp1 = (seed & 0xFFFF) << 0x10;

            seed = (seed * 125 + 3) % 0x2AAAAB;
            temp2 = (seed & 0xFFFF);

            cryptTable[index2] = (temp1 | temp2);
        }
    }
}

// 哈希算法
unsigned long HashString(const char* lpszFileName, unsigned long dwHashType) {
    unsigned char* key = (unsigned char*)lpszFileName;
    unsigned long seed1 = 0x7FED7FED, seed2 = 0xEEEEEEEE;
    int ch;

    while (*key != 0) {
        ch = toupper(*key++);
        seed1 = cryptTable[(dwHashType << 8) + ch] ^ (seed1 + seed2);
        seed2 = ch + seed1 + seed2 + (seed2 << 5) + 3;
    }
    return seed1;
}

// 解析 MPQ 文件
void ParseMPQ(const std::string& mpqPath) {
    // 打开 MPQ 文件
    HANDLE hFile = CreateFileA(mpqPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "无法打开 MPQ 文件！错误代码: " << GetLastError() << std::endl;
            logFile.close();
        }
        return;
    }

    // 读取文件头
    MPQHeader header;
    DWORD bytesRead;
    if (!ReadFile(hFile, &header, sizeof(header), &bytesRead, nullptr) || bytesRead != sizeof(header)) {
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "无法读取 MPQ 文件头！错误代码: " << GetLastError() << std::endl;
            logFile.close();
        }
        CloseHandle(hFile);
        return;
    }

    // 检查魔数
    if (header.magic != 0x1A51504D) { // 'MPQ\x1A'
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "无效的 MPQ 文件！魔数: " << std::hex << header.magic << std::endl;
            logFile.close();
        }
        CloseHandle(hFile);
        return;
    }

    // 读取哈希表
    std::vector<MPQHashTableEntry> hashTable(header.hashTableEntries);
    SetFilePointer(hFile, header.hashTableOffset, nullptr, FILE_BEGIN);
    if (!ReadFile(hFile, hashTable.data(), header.hashTableEntries * sizeof(MPQHashTableEntry), &bytesRead, nullptr)) {
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "无法读取哈希表！错误代码: " << GetLastError() << std::endl;
            logFile.close();
        }
        CloseHandle(hFile);
        return;
    }

    // 读取块表
    std::vector<MPQBlockTableEntry> blockTable(header.blockTableEntries);
    SetFilePointer(hFile, header.blockTableOffset, nullptr, FILE_BEGIN);
    if (!ReadFile(hFile, blockTable.data(), header.blockTableEntries * sizeof(MPQBlockTableEntry), &bytesRead, nullptr)) {
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "无法读取块表！错误代码: " << GetLastError() << std::endl;
            logFile.close();
        }
        CloseHandle(hFile);
        return;
    }

    // 创建输出文件
    std::ofstream mpqFile("C:\\MPQDump\\mpq_paths.txt");
    if (!mpqFile.is_open()) {
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "无法创建输出文件！" << std::endl;
            logFile.close();
        }
        CloseHandle(hFile);
        return;
    }

    // 遍历哈希表
    for (DWORD i = 0; i < header.hashTableEntries; i++) {
        if (hashTable[i].blockIndex != 0xFFFFFFFF) { // 有效的文件条目
            mpqFile << "Entry " << i << ": HashA=" << std::hex << hashTable[i].hashA
                << ", HashB=" << hashTable[i].hashB
                << ", BlockIndex=" << hashTable[i].blockIndex << std::endl;
        }
    }

    // 关闭文件
    CloseHandle(hFile);
    mpqFile.close();

    std::ofstream logFile("C:\\MPQDump\\dll_success.txt", std::ios::app);
    if (logFile.is_open()) {
        logFile << "MPQ 文件解析完成，结果已保存到 C:\\MPQDump\\mpq_paths.txt" << std::endl;
        logFile.close();
    }
}

// 线程函数：解析 MPQ 文件
DWORD WINAPI DumpMPQPaths(LPVOID lpParam) {
    // 写入日志
    std::ofstream logFile("C:\\MPQDump\\dll_thread.txt", std::ios::app);
    if (logFile.is_open()) {
        logFile << "DumpMPQPaths 线程已启动" << std::endl;
        logFile.close();
    }

    PrepareCryptTable();

    ParseMPQ("D:\\wow_cn_3.3.5\\Data\\patch-3.mpq");//MPQ路径
    return 0;
}