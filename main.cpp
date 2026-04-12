#include <windows.h>
#include <iostream>
#include <fstream>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;
#pragma comment(lib, "user32.lib")

// --- TÜRLER VE YAPILAR ---
using f_LoadLibraryA = HINSTANCE(WINAPI*)(const char* lpLibFilename);
using f_GetProcAddress = FARPROC(WINAPI*)(HMODULE hModule, LPCSTR lpProcName);
using f_DLL_ENTRY_POINT = BOOL(WINAPI*)(void* hDll, DWORD dwReason, void* pReserved);

struct MANUAL_MAPPING_DATA {
    f_LoadLibraryA pLoadLibraryA;
    f_GetProcAddress pGetProcAddress;
    BYTE* pBase;
    DWORD64 pOriginalRet;
};

// --- GİZLİLİK: XOR ŞİFRELEME ---
std::string X(std::string data, char key) {
    std::string output = data;
    for (int i = 0; i < data.size(); i++) output[i] ^= key;
    return output;
}

// --- GİZLİLİK: PE HEADER SİLME ---
void ErasePEHeader(HANDLE hProc, LPVOID pBase) {
    DWORD oldProtect;
    BYTE zero[4096] = { 0 };
    if (VirtualProtectEx(hProc, pBase, 4096, PAGE_READWRITE, &oldProtect)) {
        WriteProcessMemory(hProc, pBase, zero, 4096, NULL);
        VirtualProtectEx(hProc, pBase, 4096, oldProtect, &oldProtect);
    }
}

// --- SHELLCODE (HEDEFTE ÇALIŞIR) ---
void __stdcall Shellcode(MANUAL_MAPPING_DATA* pData) {
    if (!pData) return;

    BYTE* pBase = pData->pBase;
    auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + pDos->e_lfanew);
    auto _LoadLibraryA = pData->pLoadLibraryA;
    auto _GetProcAddress = pData->pGetProcAddress;

    // 1. Relocation
    auto* pRelocDir = &pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (pRelocDir->Size) {
        auto* pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + pRelocDir->VirtualAddress);
        DWORD64 delta = reinterpret_cast<DWORD64>(pBase) - pNt->OptionalHeader.ImageBase;
        while (pRelocData->VirtualAddress) {
            WORD* pInfo = reinterpret_cast<WORD*>(pRelocData + 1);
            for (DWORD i = 0; i < (pRelocData->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD); ++i) {
                if ((pInfo[i] >> 12) == IMAGE_REL_BASED_DIR64) {
                    DWORD64* pPatch = reinterpret_cast<DWORD64*>(pBase + pRelocData->VirtualAddress + (pInfo[i] & 0xFFF));
                    *pPatch += delta;
                }
            }
            pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(pRelocData) + pRelocData->SizeOfBlock);
        }
    }

    // 2. Import
    auto* pImportDir = &pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size) {
        auto* pImportDescr = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pImportDir->VirtualAddress);
        while (pImportDescr->Name) {
            HINSTANCE hLib = _LoadLibraryA(reinterpret_cast<char*>(pBase + pImportDescr->Name));
            auto* pThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDescr->FirstThunk);
            auto* pOrig = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDescr->OriginalFirstThunk);
            while (pThunk->u1.AddressOfData) {
                if (IMAGE_SNAP_BY_ORDINAL(pOrig->u1.Ordinal)) {
                    pThunk->u1.Function = reinterpret_cast<DWORD64>(_GetProcAddress(hLib, reinterpret_cast<char*>(pOrig->u1.Ordinal & 0xFFFF)));
                } else {
                    auto* pImportData = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + pOrig->u1.AddressOfData);
                    pThunk->u1.Function = reinterpret_cast<DWORD64>(_GetProcAddress(hLib, pImportData->Name));
                }
                pThunk++; pOrig++;
            }
            pImportDescr++;
        }
    }

    // 3. Entry Point
    if (pNt->OptionalHeader.AddressOfEntryPoint) {
        auto _DllMain = reinterpret_cast<f_DLL_ENTRY_POINT>(pBase + pNt->OptionalHeader.AddressOfEntryPoint);
        _DllMain(pBase, DLL_PROCESS_ATTACH, nullptr);
    }
}

// --- ANA MOTOR ---
int main() {
    // XOR'lu Hedef: "cs2.exe" (Key: 0x5)
    std::string secretTarget = X("fv7/f{f", 0x5); 
    
    printf("[+] Meine Injector v4.2 - Stealth Mode\n");

    // 1. DLL Secimi
    std::vector<std::string> dlls;
    for (const auto& entry : fs::directory_iterator(".")) {
        if (entry.path().extension() == ".dll") dlls.push_back(entry.path().filename().string());
    }

    if (dlls.empty()) { printf("[-] Hata: DLL bulunamadi.\n"); Sleep(2000); return 1; }

    for (int i = 0; i < dlls.size(); i++) printf("[%d] %s\n", i + 1, dlls[i].c_str());
    int choice; std::cout << "Secim: "; std::cin >> choice;
    std::string selectedDll = dlls[choice - 1];

    // 2. Hedef Bekleme
    DWORD pid = 0;
    printf("[*] %s bekleniyor...\n", secretTarget.c_str());
    while (!pid) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe{ sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do { if (!_stricmp(pe.szExeFile, secretTarget.c_str())) pid = pe.th32ProcessID; } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap); Sleep(500);
    }

    // 3. Enjeksiyon
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) return 1;

    std::ifstream file(selectedDll, std::ios::binary | std::ios::ate);
    auto fileSize = file.tellg();
    BYTE* pSrcData = new BYTE[static_cast<size_t>(fileSize)];
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(pSrcData), fileSize);
    file.close();

    auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(pSrcData);
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(pSrcData + pDos->e_lfanew);
    BYTE* pTargetBase = static_cast<BYTE*>(VirtualAllocEx(hProc, nullptr, pNt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    WriteProcessMemory(hProc, pTargetBase, pSrcData, pNt->OptionalHeader.SizeOfHeaders, nullptr);
    auto* pSec = IMAGE_FIRST_SECTION(pNt);
    for (int i = 0; i < pNt->FileHeader.NumberOfSections; i++, pSec++) {
        WriteProcessMemory(hProc, pTargetBase + pSec->VirtualAddress, pSrcData + pSec->PointerToRawData, pSec->SizeOfRawData, nullptr);
    }

    MANUAL_MAPPING_DATA data{ LoadLibraryA, GetProcAddress, pTargetBase, 0 };
    void* pDataAddr = VirtualAllocEx(hProc, nullptr, sizeof(MANUAL_MAPPING_DATA), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(hProc, pDataAddr, &data, sizeof(MANUAL_MAPPING_DATA), nullptr);

    void* pShellAddr = VirtualAllocEx(hProc, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(hProc, pShellAddr, Shellcode, 0x1000, nullptr);

    // THREAD HIJACKING
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{ sizeof(te) };
    if (Thread32First(hThreadSnap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
                if (hThread) {
                    SuspendThread(hThread);
                    CONTEXT ctx{ CONTEXT_CONTROL };
                    GetThreadContext(hThread, &ctx);
                    ctx.Rip = reinterpret_cast<DWORD64>(pShellAddr);
                    ctx.Rcx = reinterpret_cast<DWORD64>(pDataAddr);
                    SetThreadContext(hThread, &ctx);
                    ResumeThread(hThread);
                    CloseHandle(hThread);
                    printf("[***] Enjeksiyon Basarili! Hijacking kullanildi.\n");
                    break;
                }
            }
        } while (Thread32Next(hThreadSnap, &te));
    }
    
    CloseHandle(hThreadSnap);
    ErasePEHeader(hProc, pTargetBase); // Izleri sil
    CloseHandle(hProc);
    delete[] pSrcData;

    printf("Tamamlandi. 3 saniye icinde kapaniyor...\n");
    Sleep(3000);
    return 0;
}
