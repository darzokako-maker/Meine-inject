#include <windows.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        // DLL enjekte edildigi an bu mesaj kutusu cikar
        MessageBoxA(NULL, "DLL Basariyla Enjekte Edildi!", "Meine Injector", MB_OK | MB_ICONINFORMATION);
        break;
    }
    return TRUE;
}

