#include <windows.h>
#include <iostream>
#include <vector>
#include <math.h>
#include <thread>

// --- GÜNCEL OFFSETLER ---
namespace Offsets {
    constexpr uintptr_t dwLocalPlayerPawn = 0x1824A18;
    constexpr uintptr_t dwEntityList = 0x19BDD58;
    constexpr uintptr_t dwViewAngles = 0x231E9B8;
    constexpr uintptr_t m_iHealth = 0x334;
    constexpr uintptr_t m_iTeamNum = 0x3CB;
    constexpr uintptr_t m_vOldOrigin = 0x127C;
    constexpr uintptr_t m_pGameSceneNode = 0x318;
    constexpr uintptr_t m_modelState = 0x160;
    constexpr uintptr_t m_aimPunchAngle = 0x1584;
    constexpr uintptr_t m_flFlashMaxAlpha = 0x146C; 
    constexpr uintptr_t m_iIDEntIndex = 0x15A4;     
}

struct Vector3 { float x, y, z; };

// --- AYARLAR VE DURUM ---
struct Config {
    bool silent_aim = true;
    bool no_flash = true;
    bool triggerbot = false;
    bool esp_chams = true;
    bool show_menu = true;
    float max_fov = 8.0f;
} cfg;

template <typename T> T Read(uintptr_t addr) { return *reinterpret_cast<T*>(addr); }
template <typename T> void Write(uintptr_t addr, T val) { *reinterpret_cast<T*>(addr) = val; }

Vector3 CalculateAngle(Vector3 src, Vector3 dst) {
    Vector3 angle;
    Vector3 delta = { dst.x - src.x, dst.y - src.y, dst.z - src.z };
    float hyp = sqrt(delta.x * delta.x + delta.y * delta.y);
    angle.x = -atan2(delta.z, hyp) * (180.0f / 3.1415926535f);
    angle.y = atan2(delta.y, delta.x) * (180.0f / 3.1415926535f);
    return angle;
}

// Menü yazdırma fonksiyonu (Donmaları engellemek için sadece değişimde çağrılır)
void UpdateMenu() {
    if (!cfg.show_menu) { system("cls"); return; }
    system("cls");
    printf("=== MEINE STEALH V5 ===\n");
    printf("[INS] Menu Gizle/Goster\n");
    printf("[F1]  ESP (Chams)   : %s\n", cfg.esp_chams ? "ON" : "OFF");
    printf("[F2]  No Flash      : %s\n", cfg.no_flash ? "ON" : "OFF");
    printf("[F4]  Silent Aim    : %s\n", cfg.silent_aim ? "ON" : "OFF");
    printf("[ALT] Triggerbot    : HOLD\n");
    printf("[END] Cikis\n");
}

void CheatMain(HMODULE hMod) {
    uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
    
    UpdateMenu();

    while (!GetAsyncKeyState(VK_END)) {
        // --- TUŞ KONTROLLERİ (Donma yapmayan gecikmeli kontrol) ---
        if (GetAsyncKeyState(VK_INSERT) & 1) { cfg.show_menu = !cfg.show_menu; UpdateMenu(); }
        if (GetAsyncKeyState(VK_F1) & 1)     { cfg.esp_chams = !cfg.esp_chams; UpdateMenu(); }
        if (GetAsyncKeyState(VK_F2) & 1)     { cfg.no_flash = !cfg.no_flash; UpdateMenu(); }
        if (GetAsyncKeyState(VK_F4) & 1)     { cfg.silent_aim = !cfg.silent_aim; UpdateMenu(); }

        uintptr_t localPlayer = Read<uintptr_t>(client + Offsets::dwLocalPlayerPawn);
        if (!localPlayer) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

        // No Flash Uygula
        if (cfg.no_flash) {
            if (Read<float>(localPlayer + Offsets::m_flFlashMaxAlpha) > 0.0f)
                Write<float>(localPlayer + Offsets::m_flFlashMaxAlpha, 0.0f);
        }

        // Triggerbot (Alt tuşu basılıyken)
        if (GetAsyncKeyState(VK_MENU) & 0x8000) {
            int entIndex = Read<int>(localPlayer + Offsets::m_iIDEntIndex);
            if (entIndex > 0) {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            }
        }

        // Entity İşlemleri
        uintptr_t entityList = Read<uintptr_t>(client + Offsets::dwEntityList);
        if (entityList) {
            int localTeam = Read<int>(localPlayer + Offsets::m_iTeamNum);
            Vector3 localPos = Read<Vector3>(localPlayer + Offsets::m_vOldOrigin);
            Vector3 currentAngles = Read<Vector3>(client + Offsets::dwViewAngles);

            for (int i = 1; i < 32; i++) {
                uintptr_t listEntry = Read<uintptr_t>(entityList + ((8 * (i & 0x7FFF) >> 9) + 16));
                if (!listEntry) continue;
                uintptr_t player = Read<uintptr_t>(listEntry + 120 * (i & 0x1FF));
                if (!player || player == localPlayer) continue;

                if (Read<int>(player + Offsets::m_iHealth) <= 0) continue;
                if (Read<int>(player + Offsets::m_iTeamNum) == localTeam) continue;

                // ESP Chams
                if (cfg.esp_chams) {
                    uintptr_t sceneNode = Read<uintptr_t>(player + Offsets::m_pGameSceneNode);
                    if (sceneNode) Write<BYTE>(sceneNode + 0x1F0, 255);
                }

                // Silent Aim
                if (cfg.silent_aim && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    uintptr_t sceneNode = Read<uintptr_t>(player + Offsets::m_pGameSceneNode);
                    uintptr_t modelState = Read<uintptr_t>(sceneNode + Offsets::m_modelState);
                    Vector3 headPos = Read<Vector3>(modelState + (6 * 32));
                    
                    Vector3 aimAngle = CalculateAngle(localPos, headPos);
                    float fov = sqrt(pow(aimAngle.x - currentAngles.x, 2) + pow(aimAngle.y - currentAngles.y, 2));

                    if (fov < cfg.max_fov) {
                        Write<Vector3>(client + Offsets::dwViewAngles, aimAngle);
                    }
                }
            }
        }
        // İşlemciyi yormamak ve donmayı önlemek için 1ms bekleme
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    FreeLibraryAndExitThread(hMod, 0);
}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID p) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CloseHandle(CreateThread(0, 0, (LPTHREAD_START_ROUTINE)CheatMain, h, 0, 0));
    }
    return TRUE;
}
