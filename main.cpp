#include <windows.h>
#include <iostream>
#include <tlhelp32.h>

#pragma comment(lib, "user32.lib")

int main() {
    const char* dllPath = "cheat.dll";
    const char* procName = "notepad.exe";

    printf("Hedef bekleniyor: %s\n", procName);

    DWORD procId = 0;
    while (!procId) {
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
            CloseHandle(hSnap);
        }
        Sleep(500);
    }

    printf("Surec bulundu! PID: %d\n", procId);

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, procId);
    if (hProc) {
        void* loc = VirtualAllocEx(hProc, 0, MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (loc) {
            WriteProcessMemory(hProc, loc, dllPath, strlen(dllPath) + 1, 0);
            HANDLE hThread = CreateRemoteThread(hProc, 0, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, loc, 0, 0);
            if (hThread) {
                printf("Enjeksiyon basarili!\n");
                CloseHandle(hThread);
            }
        }
        CloseHandle(hProc);
    }
    return 0;
}
