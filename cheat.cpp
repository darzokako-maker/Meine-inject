#include <windows.h>
#include <iostream>
#include <vector>
#include <math.h>
#include <thread>

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
}

struct Vector3 { float x, y, z; };

struct {
    bool menu_open = true;
    bool esp_chams = true;
    bool silent_aim = true;
    bool no_recoil = true;
    float max_fov = 8.0f; // 8.0 FOV: Güvenli ve etkili denge noktası
} cfg;

template <typename T> T Read(uintptr_t addr) { return *reinterpret_cast<T*>(addr); }
template <typename T> void Write(uintptr_t addr, T val) { *reinterpret_cast<T*>(addr) = val; }

Vector3 CalculateAngle(Vector3 src, Vector3 dst) {
    Vector3 angle;
    Vector3 delta = { dst.x - src.x, dst.y - src.y, dst.z - src.z };
    float hyp = sqrt(delta.x * delta.x + delta.y * delta.y);
    angle.x = -atan2(delta.z, hyp) * (180.0f / 3.1415926535f);
    angle.y = atan2(delta.y, delta.x) * (180.0f / 3.1415926535f);
    angle.z = 0.0f;
    return angle;
}

float GetFov(Vector3 current, Vector3 target) {
    return sqrt(pow(target.x - current.x, 2) + pow(target.y - current.y, 2));
}

Vector3 GetBonePos(uintptr_t player, int boneID) {
    uintptr_t sceneNode = Read<uintptr_t>(player + Offsets::m_pGameSceneNode);
    uintptr_t boneArray = Read<uintptr_t>(sceneNode + Offsets::m_modelState);
    return Read<Vector3>(boneArray + boneID * 32); 
}

void CheatMain(HMODULE hMod) {
    uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

    while (!GetAsyncKeyState(VK_END)) {
        if (GetAsyncKeyState(VK_INSERT) & 1) cfg.menu_open = !cfg.menu_open;

        if (cfg.menu_open) {
            if (GetAsyncKeyState(VK_F1) & 1) cfg.esp_chams = !cfg.esp_chams;
            if (GetAsyncKeyState(VK_F4) & 1) cfg.silent_aim = !cfg.silent_aim;
            
            system("cls");
            printf("=== MEINE GIZLI MOD ===\n");
            printf("[INSERT] Menuyu Gizle\n");
            printf("[F1] ESP (Chams) : %s\n", cfg.esp_chams ? "ACIK" : "KAPALI");
            printf("[F4] Silent Aim  : %s (FOV: 8.0)\n", cfg.silent_aim ? "ACIK" : "KAPALI");
            printf("------------------------\n");
            printf("Durum: Su an en guvenli ayarlardasiniz.\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        uintptr_t localPlayer = Read<uintptr_t>(client + Offsets::dwLocalPlayerPawn);
        if (!localPlayer) continue;

        if (cfg.no_recoil) Write<Vector3>(localPlayer + Offsets::m_aimPunchAngle, { 0, 0, 0 });

        uintptr_t entityList = Read<uintptr_t>(client + Offsets::dwEntityList);
        if (!entityList) continue;

        int localTeam = Read<int>(localPlayer + Offsets::m_iTeamNum);
        Vector3 localPos = Read<Vector3>(localPlayer + Offsets::m_vOldOrigin);
        Vector3 currentAngles = Read<Vector3>(client + Offsets::dwViewAngles);

        for (int i = 1; i < 32; i++) {
            uintptr_t listEntry = Read<uintptr_t>(entityList + ((8 * (i & 0x7FFF) >> 9) + 16));
            if (!listEntry) continue;
            uintptr_t player = Read<uintptr_t>(listEntry + 120 * (i & 0x1FF));
            if (!player || player == localPlayer) continue;

            int health = Read<int>(player + Offsets::m_iHealth);
            if (health <= 0 || Read<int>(player + Offsets::m_iTeamNum) == localTeam) continue;

            // ESP
            uintptr_t sceneNode = Read<uintptr_t>(player + Offsets::m_pGameSceneNode);
            if (sceneNode && cfg.esp_chams) Write<BYTE>(sceneNode + 0x1F0, 255);

            // GIZLI SILENT AIM
            if (cfg.silent_aim && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                Vector3 headPos = GetBonePos(player, 6);
                Vector3 aimAngle = CalculateAngle(localPos, headPos);
                
                if (GetFov(currentAngles, aimAngle) < cfg.max_fov) {
                    Write<Vector3>(client + Offsets::dwViewAngles, aimAngle);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (f) fclose(f);
    FreeConsole();
    FreeLibraryAndExitThread(hMod, 0);
}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID p) {
    if (r == DLL_PROCESS_ATTACH) CloseHandle(CreateThread(0, 0, (LPTHREAD_START_ROUTINE)CheatMain, h, 0, 0));
    return TRUE;
}
