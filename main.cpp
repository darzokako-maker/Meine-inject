#include <windows.h>
#include <iostream>
#include <fstream>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <time.h>

#pragma comment(lib, "user32.lib")

// --- GİZLİLİK: XOR ŞİFRELEME ---
// Metinlerin (string) binary içinde açıkça görünmesini engeller
std::string Crypt(std::string data, char key) {
    std::string output = data;
    for (int i = 0; i < data.size(); i++)
        output[i] = data[i] ^ key;
    return output;
}

// --- GİZLİLİK: MANUEL MAP VERİ YAPISI ---
using f_LoadLibraryA = HINSTANCE(WINAPI*)(const char* lpLibFilename);
using f_GetProcAddress = FARPROC(WINAPI*)(HMODULE hModule, LPCSTR lpProcName);
using f_DLL_ENTRY_POINT = BOOL(WINAPI*)(void* hDll, DWORD dwReason, void* pReserved);

struct MANUAL_MAPPING_DATA {
    f_LoadLibraryA pLoadLibraryA;
    f_GetProcAddress pGetProcAddress;
    BYTE* pBase;
};

// --- GİZLİLİK: SHELLCODE (Hedef Süreçte Çalışır) ---
void __stdcall Shellcode(MANUAL_MAPPING_DATA* pData) {
    if (!pData) return;

    BYTE* pBase = pData->pBase;
    auto* pDosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
    auto* pNtHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + pDosHeader->e_lfanew);
    auto* pOptHeader = &pNtHeaders->OptionalHeader;

    auto _LoadLibraryA = pData->pLoadLibraryA;
    auto _GetProcAddress = pData->pGetProcAddress;

    // 1. RELOCATION
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

    // 2. IMPORT
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

    // 3. DLL_MAIN
    if (pOptHeader->AddressOfEntryPoint) {
        auto _DllMain = reinterpret_cast<f_DLL_ENTRY_POINT>(pBase + pOptHeader->AddressOfEntryPoint);
        _DllMain(pBase, DLL_PROCESS_ATTACH, nullptr);
    }
}

// --- ANA ENJEKTÖR MOTORU ---
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

    // Sections Kopyala
    WriteProcessMemory(hProc, pTargetBase, pSrcData, pNtHeaders->OptionalHeader.SizeOfHeaders, nullptr);
    auto* pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; ++i, ++pSectionHeader) {
        WriteProcessMemory(hProc, pTargetBase + pSectionHeader->VirtualAddress, pSrcData + pSectionHeader->PointerToRawData, pSectionHeader->SizeOfRawData, nullptr);
    }

    // Mapping Verileri
    MANUAL_MAPPING_DATA data{ 0 };
    data.pLoadLibraryA = LoadLibraryA;
    data.pGetProcAddress = GetProcAddress;
    data.pBase = pTargetBase;

    void* pDataAddr = VirtualAllocEx(hProc, nullptr, sizeof(MANUAL_MAPPING_DATA), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(hProc, pDataAddr, &data, sizeof(MANUAL_MAPPING_DATA), nullptr);

    // Shellcode
    void* pShellcodeAddr = VirtualAllocEx(hProc, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(hProc, pShellcodeAddr, Shellcode, 0x1000, nullptr);

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(pShellcodeAddr), pDataAddr, 0, nullptr);
    
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE); // Shellcode bitene kadar bekle
        CloseHandle(hThread);

        // --- GİZLİLİK: PE HEADERS SİLME ---
        // Bellekteki MZ/PE imzalarını temizleyerek tarayıcıları atlatır
        BYTE* emptyBuffer = new BYTE[4096];
        memset(emptyBuffer, 0, 4096);
        DWORD oldProtect;
        VirtualProtectEx(hProc, pTargetBase, 4096, PAGE_READWRITE, &oldProtect);
        WriteProcessMemory(hProc, pTargetBase, emptyBuffer, 4096, nullptr);
        VirtualProtectEx(hProc, pTargetBase, 4096, oldProtect, &oldProtect);
        delete[] emptyBuffer;

        return true;
    }
    return false;
}

int main() {
    srand(static_cast<unsigned int>(time(NULL)));
    
    // Rastgele Konsol Başlığı (Statik Analiz Engelleyici)
    std::string title = "";
    for(int i=0; i<15; i++) title += (char)(rand() % 26 + 97);
    SetConsoleTitleA(title.c_str());

    // XOR Şifreli Hedef: "notepad.exe" (Anahtar: 0x55)
    // Şifreli metin: \x3B\x3A\x21\x30\x25\x34\x31\x75\x30\x2D\x30
    std::string encTarget = "\x3B\x3A\x21\x30\x25\x34\x31\x75\x30\x2D\x30";
    std::string target = Crypt(encTarget, 0x55);

    printf("[+] Gizli Motor Aktif. Hedef bekleniyor...\n");

    DWORD procId = 0;
    while (!procId) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe{ sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                if (!_stricmp(pe.szExeFile, target.c_str())) {
                    procId = pe.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
        Sleep(500);
    }

    printf("[!] Hedef Bulundu (PID: %d). Enjekte ediliyor...\n", procId);

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, procId);
    if (hProc) {
        if (ManualMapStealth(hProc, "cheat.dll")) {
            printf("[***] BASARILI! Izler silindi.\n");
        } else {
            printf("[-] Hata: Enjeksiyon basarisiz.\n");
        }
        CloseHandle(hProc);
    }

    Sleep(3000);
    return 0;
}
