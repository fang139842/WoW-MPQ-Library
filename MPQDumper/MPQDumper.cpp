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

// ��ϣ�㷨
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

// ���� MPQ �ļ�
void ParseMPQ(const std::string& mpqPath) {
    // �� MPQ �ļ�
    HANDLE hFile = CreateFileA(mpqPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "�޷��� MPQ �ļ����������: " << GetLastError() << std::endl;
            logFile.close();
        }
        return;
    }

    // ��ȡ�ļ�ͷ
    MPQHeader header;
    DWORD bytesRead;
    if (!ReadFile(hFile, &header, sizeof(header), &bytesRead, nullptr) || bytesRead != sizeof(header)) {
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "�޷���ȡ MPQ �ļ�ͷ���������: " << GetLastError() << std::endl;
            logFile.close();
        }
        CloseHandle(hFile);
        return;
    }

    // ���ħ��
    if (header.magic != 0x1A51504D) { // 'MPQ\x1A'
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "��Ч�� MPQ �ļ���ħ��: " << std::hex << header.magic << std::endl;
            logFile.close();
        }
        CloseHandle(hFile);
        return;
    }

    // ��ȡ��ϣ��
    std::vector<MPQHashTableEntry> hashTable(header.hashTableEntries);
    SetFilePointer(hFile, header.hashTableOffset, nullptr, FILE_BEGIN);
    if (!ReadFile(hFile, hashTable.data(), header.hashTableEntries * sizeof(MPQHashTableEntry), &bytesRead, nullptr)) {
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "�޷���ȡ��ϣ���������: " << GetLastError() << std::endl;
            logFile.close();
        }
        CloseHandle(hFile);
        return;
    }

    // ��ȡ���
    std::vector<MPQBlockTableEntry> blockTable(header.blockTableEntries);
    SetFilePointer(hFile, header.blockTableOffset, nullptr, FILE_BEGIN);
    if (!ReadFile(hFile, blockTable.data(), header.blockTableEntries * sizeof(MPQBlockTableEntry), &bytesRead, nullptr)) {
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "�޷���ȡ����������: " << GetLastError() << std::endl;
            logFile.close();
        }
        CloseHandle(hFile);
        return;
    }

    // ��������ļ�
    std::ofstream mpqFile("C:\\MPQDump\\mpq_paths.txt");
    if (!mpqFile.is_open()) {
        std::ofstream logFile("C:\\MPQDump\\dll_error.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "�޷���������ļ���" << std::endl;
            logFile.close();
        }
        CloseHandle(hFile);
        return;
    }

    // ������ϣ��
    for (DWORD i = 0; i < header.hashTableEntries; i++) {
        if (hashTable[i].blockIndex != 0xFFFFFFFF) { // ��Ч���ļ���Ŀ
            mpqFile << "Entry " << i << ": HashA=" << std::hex << hashTable[i].hashA
                << ", HashB=" << hashTable[i].hashB
                << ", BlockIndex=" << hashTable[i].blockIndex << std::endl;
        }
    }

    // �ر��ļ�
    CloseHandle(hFile);
    mpqFile.close();

    std::ofstream logFile("C:\\MPQDump\\dll_success.txt", std::ios::app);
    if (logFile.is_open()) {
        logFile << "MPQ �ļ�������ɣ�����ѱ��浽 C:\\MPQDump\\mpq_paths.txt" << std::endl;
        logFile.close();
    }
}

// �̺߳��������� MPQ �ļ�
DWORD WINAPI DumpMPQPaths(LPVOID lpParam) {
    // д����־
    std::ofstream logFile("C:\\MPQDump\\dll_thread.txt", std::ios::app);
    if (logFile.is_open()) {
        logFile << "DumpMPQPaths �߳�������" << std::endl;
        logFile.close();
    }

    PrepareCryptTable();

    ParseMPQ("D:\\wow_cn_3.3.5\\Data\\patch-3.mpq");//MPQ·��
    return 0;
}