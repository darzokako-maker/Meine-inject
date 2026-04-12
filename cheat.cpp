#include <windows.h>

// Derleyiciye User32 kütüphanesini kullanmasını söyler (Hata çözümü)
#pragma comment(lib, "user32.lib")

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        // Enjekte olduğu an mesaj kutusu çıkarır
        MessageBoxA(NULL, "DLL Basariyla Enjekte Edildi!", "Meine-Inject", MB_OK | MB_ICONINFORMATION);
        break;
    }
    return TRUE;
}
