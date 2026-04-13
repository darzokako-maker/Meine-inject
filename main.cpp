#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
#include <vector>
#include <fstream>

// --- AYARLAR VE YAPILAR ---
#define TARGET_PROCESS "cs2.exe"
#define DLL_NAME "hile.dll" // DLL adı hile.dll olarak güncellendi

struct MAPPING_DATA {
    void* pImageBase;
    HMODULE(WINAPI* pLoadLibraryA)(LPCSTR);
    FARPROC(WINAPI* pGetProcAddress)(HMODULE, LPCSTR);
    UINT_PTR pOriginalRip; 
};

// --- HEDEF SÜREÇTE ÇALIŞACAK SHELLCODE ---
void __stdcall Shellcode(MAPPING_DATA* pData) {
    BYTE* pBase = (BYTE*)pData->pImageBase;
    auto* pOpt = &((PIMAGE_NT_HEADERS)(pBase + ((PIMAGE_DOS_HEADER)pBase)->e_lfanew))->OptionalHeader;

    auto f_LoadLibraryA = pData->pLoadLibraryA;
    auto f_GetProcAddress = pData->pGetProcAddress;

    // 1. Relocation (Adres Düzeltme)
    auto* pRelocDir = &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (pRelocDir->Size) {
        auto* pRelocData = (PIMAGE_BASE_RELOCATION)(pBase + pRelocDir->VirtualAddress);
        while (pRelocData->VirtualAddress) {
            UINT entries = (pRelocData->SizeOfBlock - sizeof(PIMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD* pInfo = (WORD*)(pRelocData + 1);
            for (UINT i = 0; i != entries; ++i, ++pInfo) {
                if ((*pInfo >> 12) == IMAGE_REL_BASED_DIR64) {
                    UINT_PTR* pPatch = (UINT_PTR*)(pBase + pRelocData->VirtualAddress + (*pInfo & 0xFFF));
                    *pPatch += (UINT_PTR)(pBase)-pOpt->ImageBase;
                }
            }
            pRelocData = (PIMAGE_BASE_RELOCATION)((BYTE*)pRelocData + pRelocData->SizeOfBlock);
        }
    }

    // 2. IAT Rebuilding (API Bağlama)
    auto* pImportDir = &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size) {
        auto* pImportDescr = (PIMAGE_IMPORT_DESCRIPTOR)(pBase + pImportDir->VirtualAddress);
        while (pImportDescr->Name) {
            HINSTANCE hMod = f_LoadLibraryA((char*)(pBase + pImportDescr->Name));
            auto* pThunk = (PIMAGE_THUNK_DATA)(pBase + pImportDescr->OriginalFirstThunk);
            auto* pIAT = (PIMAGE_THUNK_DATA)(pBase + pImportDescr->FirstThunk);
            if (!pThunk) pThunk = pIAT;
            for (; pThunk->u1.AddressOfData; ++pThunk, ++pIAT) {
                if (IMAGE_ORDINAL_FLAG & pThunk->u1.Ordinal)
                    pIAT->u1.Function = (UINT_PTR)f_GetProcAddress(hMod, (char*)(pThunk->u1.Ordinal & 0xFFFF));
                else
                    pIAT->u1.Function = (UINT_PTR)f_GetProcAddress(hMod, (char*)((PIMAGE_IMPORT_BY_NAME)(pBase + pThunk->u1.AddressOfData))->Name);
            }
            pImportDescr++;
        }
    }

    // 3. Entry Point Çağrısı
    using f_DllMain = BOOL(WINAPI*)(void*, DWORD, void*);
    ((f_DllMain)(pBase + pOpt->AddressOfEntryPoint))(pBase, DLL_PROCESS_ATTACH, nullptr);
}

// --- YARDIMCI FONKSİYONLAR ---
DWORD GetPID(const char* procName) {
    DWORD pid = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32; pe32.dwSize = sizeof(pe32);
        if (Process32First(hSnap, &pe32)) {
            do { if (!_stricmp(pe32.szExeFile, procName)) { pid = pe32.th32ProcessID; break; } } while (Process32Next(hSnap, &pe32));
        }
        CloseHandle(hSnap);
    }
    return pid;
}

DWORD GetThreadID(DWORD pid) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te; te.dwSize = sizeof(te);
    if (Thread32First(hSnap, &te)) {
        do { if (te.th32OwnerProcessID == pid) { CloseHandle(hSnap); return te.th32ThreadID; } } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);
    return 0;
}

// --- ANA ENJEKTÖR MANTIĞI ---
int main() {
    printf("--- STEALTH INJECTOR V5 (hile.dll Modu) ---\n");
    
    DWORD pid = 0;
    printf("[*] %s bekleniyor...\n", TARGET_PROCESS);
    while (!pid) { pid = GetPID(TARGET_PROCESS); Sleep(500); }
    printf("[!] Oyun bulundu! PID: %d\n", pid);

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) {
        printf("[X] Surece erisim saglanamadi!\n");
        return 1;
    }

    // 1. DLL Dosyasını Oku
    std::ifstream file(DLL_NAME, std::ios::binary | std::ios::ate);
    if (file.fail()) {
        printf("[X] %s dosyasi bulunamadi!\n", DLL_NAME);
        CloseHandle(hProc);
        return 1;
    }

    auto size = file.tellg();
    BYTE* pSrcData = new BYTE[(UINT_PTR)size];
    file.seekg(0, std::ios::beg);
    file.read((char*)pSrcData, size);
    file.close();

    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(pSrcData + ((PIMAGE_DOS_HEADER)pSrcData)->e_lfanew);

    // 2. Bellekte Yer Aç ve Kopyala
    void* pTargetBase = VirtualAllocEx(hProc, nullptr, ntHeaders->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(hProc, pTargetBase, pSrcData, ntHeaders->OptionalHeader.SizeOfHeaders, nullptr);

    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(ntHeaders);
    for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        WriteProcessMemory(hProc, (LPVOID)((uintptr_t)pTargetBase + pSection[i].VirtualAddress), pSrcData + pSection[i].PointerToRawData, pSection[i].SizeOfRawData, nullptr);
    }

    // 3. Mapping Verilerini Hazırla
    MAPPING_DATA mData;
    mData.pImageBase = pTargetBase;
    mData.pLoadLibraryA = LoadLibraryA;
    mData.pGetProcAddress = GetProcAddress;

    // 4. Thread Hijacking
    DWORD tid = GetThreadID(pid);
    HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, tid);
    SuspendThread(hThread);

    CONTEXT ctx; ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(hThread, &ctx);
    mData.pOriginalRip = ctx.Rip;

    void* pRemoteData = VirtualAllocEx(hProc, nullptr, sizeof(mData), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(hProc, pRemoteData, &mData, sizeof(mData), nullptr);

    void* pRemoteShell = VirtualAllocEx(hProc, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(hProc, pRemoteShell, Shellcode, 0x1000, nullptr);

    ctx.Rip = (DWORD64)pRemoteShell;
    ctx.Rcx = (DWORD64)pRemoteData; 
    SetThreadContext(hThread, &ctx);
    
    // 5. Baslatma ve Gizlilik
    ResumeThread(hThread);
    
    BYTE nullBuffer[4096] = { 0 };
    WriteProcessMemory(hProc, pTargetBase, nullBuffer, 4096, nullptr);

    printf("[SUCCESS] %s basariyla enjekte edildi.\n", DLL_NAME);
    
    CloseHandle(hThread);
    CloseHandle(hProc);
    delete[] pSrcData;
    Sleep(3000);
    return 0;
}
