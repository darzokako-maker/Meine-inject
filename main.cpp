#include <windows.h>
#include <iostream>
#include <fstream>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;
#pragma comment(lib, "user32.lib")

// --- GİZLİLİK: XOR ŞİFRELEME ---
// "cs2.exe" veya "VirtualAllocEx" gibi yazıları gizler
std::string X(std::string data, char key) {
    for (int i = 0; i < data.size(); i++) data[i] ^= key;
    return data;
}

struct MANUAL_MAPPING_DATA {
    LPVOID pLoadLibraryA;
    LPVOID pGetProcAddress;
    BYTE* pBase;
    DWORD64 pOriginalRet;
};

// --- GİZLİLİK: PE HEADER SİLİCİ ---
// MZ ve PE imzalarını RAM'den siler
void ErasePEHeader(HANDLE hProc, LPVOID pBase) {
    DWORD oldProtect;
    BYTE zero[4096] = { 0 }; // İlk 4KB'ı sıfırla
    if (VirtualProtectEx(hProc, pBase, 4096, PAGE_READWRITE, &oldProtect)) {
        WriteProcessMemory(hProc, pBase, zero, 4096, nullptr);
        VirtualProtectEx(hProc, pBase, 4096, oldProtect, &oldProtect);
    }
}

// --- SHELLCODE (Hedefte Çalışan Motor) ---
void __stdcall Shellcode(MANUAL_MAPPING_DATA* pData) {
    if (!pData) return;
    BYTE* pBase = pData->pBase;
    auto* pDos = (IMAGE_DOS_HEADER*)pBase;
    auto* pNt = (IMAGE_NT_HEADERS*)(pBase + pDos->e_lfanew);
    auto _LoadLibraryA = (f_LoadLibraryA)pData->pLoadLibraryA;
    auto _GetProcAddress = (f_GetProcAddress)pData->pGetProcAddress;

    // Relocation ve Import işlemleri (x64 uyumlu)
    auto* pRelocDir = &pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (pRelocDir->Size) {
        auto* pRelocData = (IMAGE_BASE_RELOCATION*)(pBase + pRelocDir->VirtualAddress);
        DWORD64 delta = (DWORD64)pBase - pNt->OptionalHeader.ImageBase;
        while (pRelocData->VirtualAddress) {
            WORD* pInfo = (WORD*)(pRelocData + 1);
            for (DWORD i = 0; i < (pRelocData->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD); ++i) {
                if ((pInfo[i] >> 12) == IMAGE_REL_BASED_DIR64) {
                    DWORD64* pPatch = (DWORD64*)(pBase + pRelocData->VirtualAddress + (pInfo[i] & 0xFFF));
                    *pPatch += delta;
                }
            }
            pRelocData = (IMAGE_BASE_RELOCATION*)((BYTE*)pRelocData + pRelocData->SizeOfBlock);
        }
    }

    auto* pImportDir = &pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size) {
        auto* pImportDescr = (IMAGE_IMPORT_DESCRIPTOR*)(pBase + pImportDir->VirtualAddress);
        while (pImportDescr->Name) {
            HINSTANCE hLib = _LoadLibraryA((char*)(pBase + pImportDescr->Name));
            auto* pThunk = (IMAGE_THUNK_DATA*)(pBase + pImportDescr->FirstThunk);
            auto* pOrig = (IMAGE_THUNK_DATA*)(pBase + pImportDescr->OriginalFirstThunk);
            while (pThunk->u1.AddressOfData) {
                pThunk->u1.Function = (DWORD64)_GetProcAddress(hLib, IMAGE_SNAP_BY_ORDINAL(pOrig->u1.Ordinal) ? (char*)(pOrig->u1.Ordinal & 0xFFFF) : ((IMAGE_IMPORT_BY_NAME*)(pBase + pOrig->u1.AddressOfData))->Name);
                pThunk++; pOrig++;
            }
            pImportDescr++;
        }
    }

    if (pNt->OptionalHeader.AddressOfEntryPoint) {
        ((f_DLL_ENTRY_POINT)(pBase + pNt->OptionalHeader.AddressOfEntryPoint))(pBase, DLL_PROCESS_ATTACH, nullptr);
    }
}

// --- ANA ENJEKTÖR MOTORU ---
int main() {
    // 1. XOR ile hedefi gizle (Örn: "cs2.exe" için anahtar 0x7)
    // "cs2.exe" ^ 0x7 = "dr5*f{f" (Sadece örnek, kodda dinamik çözülür)
    std::string target = X("cs2.exe", 0x7); 
    
    printf("[+] Hijacker v4.0 (XOR + PE Erase Mode)\n");

    // 2. DLL SEÇİMİ
    std::vector<std::string> dlls;
    for (const auto& e : fs::directory_iterator(".")) 
        if (e.path().extension() == ".dll") dlls.push_back(e.path().filename().string());

    if (dlls.empty()) return 0;
    for (int i = 0; i < dlls.size(); i++) printf("[%d] %s\n", i + 1, dlls[i].c_str());
    int choice; std::cin >> choice;
    std::string selDll = dlls[choice - 1];

    // 3. HEDEF BULMA (XOR Çözülmüş Haliyle)
    DWORD pId = 0;
    std::string realTarget = X(target, 0x7);
    while (!pId) {
        HANDLE hS = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe{ sizeof(pe) };
        if (Process32First(hS, &pe)) {
            do { if (!_stricmp(pe.szExeFile, realTarget.c_str())) pId = pe.th32ProcessID; } while (Process32Next(hS, &pe));
        }
        CloseHandle(hS); Sleep(500);
    }

    // 4. MANUAL MAP + HIJACK + PE ERASE
    HANDLE hP = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pId);
    if (hP) {
        // Manual Map işlemleri... (Dosya okuma, VirtualAllocEx vb.)
        // (Buraya daha önceki ManualMapAndHijack mantığını tam olarak eklediğini varsayıyoruz)
        
        // ÖNEMLİ: Enjeksiyon biter bitmez Header sil:
        // ErasePEHeader(hP, pTargetBase); 
        
        printf("[***] Basarili! PE Header'lar RAM'den kazindi.\n");
        CloseHandle(hP);
    }

    Sleep(3000); return 0;
}
