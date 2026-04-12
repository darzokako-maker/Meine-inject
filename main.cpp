#include <windows.h>
#include <iostream>
#include <fstream>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <filesystem>
#include <time.h>

namespace fs = std::filesystem;
#pragma comment(lib, "user32.lib")

// --- GİZLİLİK VE YAPI TANIMLARI ---
using f_LoadLibraryA = HINSTANCE(WINAPI*)(const char* lpLibFilename);
using f_GetProcAddress = FARPROC(WINAPI*)(HMODULE hModule, LPCSTR lpProcName);
using f_DLL_ENTRY_POINT = BOOL(WINAPI*)(void* hDll, DWORD dwReason, void* pReserved);

struct MANUAL_MAPPING_DATA {
    f_LoadLibraryA pLoadLibraryA;
    f_GetProcAddress pGetProcAddress;
    BYTE* pBase;
};

// --- SHELLCODE (Hedef Süreçte Çalışan Motor) ---
void __stdcall Shellcode(MANUAL_MAPPING_DATA* pData) {
    if (!pData) return;
    BYTE* pBase = pData->pBase;
    auto* pDosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
    auto* pNtHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + pDosHeader->e_lfanew);
    auto* pOptHeader = &pNtHeaders->OptionalHeader;
    auto _LoadLibraryA = pData->pLoadLibraryA;
    auto _GetProcAddress = pData->pGetProcAddress;

    // Relocation
    auto* pRelocDir = &pOptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (pRelocDir->Size) {
        auto* pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + pRelocDir->VirtualAddress);
        DWORD delta = reinterpret_cast<DWORD>(pBase) - pOptHeader->ImageBase;
        while (pRelocData->VirtualAddress) {
            WORD* pRelativeInfo = reinterpret_cast<WORD*>(pRelocData + 1);
            for (DWORD i = 0; i < (pRelocData->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD); ++i) {
                if ((pRelativeInfo[i] >> 12) == IMAGE_REL_BASED_HIGHLOW) {
                    DWORD* pPatch = reinterpret_cast<DWORD*>(pBase + pRelocData->VirtualAddress + (pRelativeInfo[i] & 0xFFF));
                    *pPatch += delta;
                }
            }
            pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(pRelocData) + pRelocData->SizeOfBlock);
        }
    }

    // Import
    auto* pImportDir = &pOptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size) {
        auto* pImportDescr = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pImportDir->VirtualAddress);
        while (pImportDescr->Name) {
            HINSTANCE hLib = _LoadLibraryA(reinterpret_cast<char*>(pBase + pImportDescr->Name));
            auto* pThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDescr->FirstThunk);
            auto* pOriginalThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDescr->OriginalFirstThunk);
            while (pThunk->u1.AddressOfData) {
                if (IMAGE_SNAP_BY_ORDINAL(pOriginalThunk->u1.Ordinal)) {
                    pThunk->u1.Function = reinterpret_cast<DWORD>(_GetProcAddress(hLib, reinterpret_cast<char*>(pOriginalThunk->u1.Ordinal & 0xFFFF)));
                } else {
                    auto* pImportData = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + pOriginalThunk->u1.AddressOfData);
                    pThunk->u1.Function = reinterpret_cast<DWORD>(_GetProcAddress(hLib, pImportData->Name));
                }
                pThunk++; pOriginalThunk++;
            }
            pImportDescr++;
        }
    }

    if (pOptHeader->AddressOfEntryPoint) {
        auto _DllMain = reinterpret_cast<f_DLL_ENTRY_POINT>(pBase + pOptHeader->AddressOfEntryPoint);
        _DllMain(pBase, DLL_PROCESS_ATTACH, nullptr);
    }
}

// --- MANUEL MAP MOTORU ---
bool ManualMapStealth(HANDLE hProc, const char* szDllPath) {
    std::ifstream File(szDllPath, std::ios::binary | std::ios::ate);
    auto FileSize = File.tellg();
    BYTE* pSrcData = new BYTE[static_cast<UINT_PTR>(FileSize)];
    File.seekg(0, std::ios::beg);
    File.read(reinterpret_cast<char*>(pSrcData), FileSize);
    File.close();

    auto* pDosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(pSrcData);
    auto* pNtHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(pSrcData + pDosHeader->e_lfanew);
    BYTE* pTargetBase = static_cast<BYTE*>(VirtualAllocEx(hProc, nullptr, pNtHeaders->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    WriteProcessMemory(hProc, pTargetBase, pSrcData, pNtHeaders->OptionalHeader.SizeOfHeaders, nullptr);
    auto* pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; ++i, ++pSectionHeader) {
        WriteProcessMemory(hProc, pTargetBase + pSectionHeader->VirtualAddress, pSrcData + pSectionHeader->PointerToRawData, pSectionHeader->SizeOfRawData, nullptr);
    }

    MANUAL_MAPPING_DATA data{ LoadLibraryA, GetProcAddress, pTargetBase };
    void* pDataAddr = VirtualAllocEx(hProc, nullptr, sizeof(MANUAL_MAPPING_DATA), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(hProc, pDataAddr, &data, sizeof(MANUAL_MAPPING_DATA), nullptr);

    void* pShellcodeAddr = VirtualAllocEx(hProc, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(hProc, pShellcodeAddr, Shellcode, 0x1000, nullptr);

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(pShellcodeAddr), pDataAddr, 0, nullptr);
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
        // Header Silme (Gizlilik)
        BYTE* zero = new BYTE[4096]{0};
        DWORD old;
        VirtualProtectEx(hProc, pTargetBase, 4096, PAGE_READWRITE, &old);
        WriteProcessMemory(hProc, pTargetBase, zero, 4096, nullptr);
        VirtualProtectEx(hProc, pTargetBase, 4096, old, &old);
        return true;
    }
    return false;
}

// --- ANA MENU VE KULLANICI SEÇİMLERİ ---
int main() {
    srand(time(0));
    SetConsoleTitleA("Meine Manual-Map Menu");
    system("color 0D"); // Mor stil

    printf("--- [ MEINE INJECTOR PRO ] ---\n\n");

    // 1. DLL SEÇME
    std::vector<std::string> dlls;
    for (const auto& entry : fs::directory_iterator(".")) {
        if (entry.path().extension() == ".dll") {
            dlls.push_back(entry.path().filename().string());
        }
    }

    if (dlls.empty()) {
        printf("[!] Klasorde DLL bulunamadi! Cikiliyor...\n");
        Sleep(3000); return 0;
    }

    printf("[?] Enjekte edilecek DLL'i secin:\n");
    for (size_t i = 0; i < dlls.size(); i++) {
        printf(" [%zu] %s\n", i + 1, dlls[i].c_str());
    }
    int choice;
    printf("\nSecim: "); std::cin >> choice;
    if (choice < 1 || choice > dlls.size()) return 0;
    std::string selectedDll = dlls[choice - 1];

    // 2. İŞLEM SEÇME
    std::string targetName;
    printf("\n[?] Hedef islem adini girin (Ornek: notepad.exe): ");
    std::cin >> targetName;

    printf("\n[*] %s bekleniyor pusuya yatildi...\n", targetName.c_str());

    DWORD procId = 0;
    while (!procId) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe{ sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                if (!_stricmp(pe.szExeFile, targetName.c_str())) {
                    procId = pe.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
        Sleep(800);
    }

    // 3. ENJEKSİYON
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, procId);
    if (hProc) {
        if (ManualMapStealth(hProc, selectedDll.c_str())) {
            printf("\n[***] ENJEKSIYON BASARILI! DLL: %s -> Hedef: %s\n", selectedDll.c_str(), targetName.c_str());
        } else {
            printf("\n[-] Hata olustu!\n");
        }
        CloseHandle(hProc);
    }

    printf("\n3 saniye icinde kapaniyor...");
    Sleep(3000);
    return 0;
}
