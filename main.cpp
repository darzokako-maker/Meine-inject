#include <windows.h>
#include <iostream>
#include <tlhelp32.h>
#include <string>

// Calisan bir surecin ID'sini (PID) ismine bakarak bulur
DWORD GetProcId(const char* procName) {
    DWORD procId = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 procEntry;
        procEntry.dwSize = sizeof(procEntry);
        if (Process32First(hSnap, &procEntry)) {
            do {
                if (!_stricmp(procEntry.szExeFile, procName)) {
                    procId = procEntry.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnap, &procEntry));
        }
    }
    CloseHandle(hSnap);
    return procId;
}

int main() {
    const char* dllPath = "cheat.dll"; // Enjekte edilecek dosyanin adi
    const char* procName = "notepad.exe"; // Test icin Not Defteri'ni hedef aldik

    std::cout << "Hedef bekleniyor: " << procName << std::endl;
    DWORD procId = 0;
    while (!procId) {
        procId = GetProcId(procName);
        Sleep(100);
    }

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, 0, procId);
    if (hProc && hProc != INVALID_HANDLE_VALUE) {
        void* loc = VirtualAllocEx(hProc, 0, MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        WriteProcessMemory(hProc, loc, dllPath, strlen(dllPath) + 1, 0);
        HANDLE hThread = CreateRemoteThread(hProc, 0, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, loc, 0, 0);
        if (hThread) CloseHandle(hThread);
    }
    if (hProc) CloseHandle(hProc);
    std::cout << "Enjeksiyon denendi!" << std::endl;
    return 0;
}

