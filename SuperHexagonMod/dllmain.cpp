#include <windows.h>
#include <safetyhook.hpp>
#include <vector>
using PrintFunc_t = void(__thiscall*)(void* /*this*/, int x, int y, std::basic_string<char> param_3, int r, int g, int b, bool centered);
using LogFunc_t = void(__cdecl*)(const char* format, ...);

uintptr_t window;

enum
{
    IMAGE_BASE = 0x00400000
};

namespace
{
    HMODULE targetModule;

    uintptr_t get_address(uintptr_t ghidraAddress)
    {
        uintptr_t moduleBase = reinterpret_cast<uintptr_t>(targetModule);
        return moduleBase + (ghidraAddress - IMAGE_BASE);
    }
}

namespace
{
    PrintFunc_t graphics_printString = nullptr;
    LogFunc_t Log = nullptr;
    void initialize_game_functions()
    {
        graphics_printString = reinterpret_cast<PrintFunc_t>(get_address(0x00422c90));
        Log = reinterpret_cast<LogFunc_t>(get_address(0x0044d580));
    }
}




void hook_patch_text(SafetyHookContext& ctx)
{
    void* thisPtr = reinterpret_cast<void*>(ctx.edi);
    graphics_printString(thisPtr, 0, 5, "FPS Unlock Patch", 255, 0, 0, false);
}

void hook_modify_expected_frame_time(SafetyHookContext& ctx)
{
    float time = 0.5f; // 120 FPS
    // load into ST0
    __asm {
        fld time
    }
}

void hook_change_target_frame_time(SafetyHookContext& ctx)
{
    // get structure at EDI
    void* thisPtr = reinterpret_cast<void*>(ctx.edi);
    // set double at offset 0x40cd0 to 0.008333333 (120 FPS)
    double* targetFrameTime = reinterpret_cast<double*>(reinterpret_cast<uint8_t*>(thisPtr) + 0x40cd0);
    *targetFrameTime = 0.008333333;
}

std::vector<SafetyHookMid> hooks;

DWORD WINAPI MainThread(LPVOID lpParam)
{
    targetModule = GetModuleHandle(L"SuperHexagon.exe");
    initialize_game_functions();
    hooks.push_back(safetyhook::create_mid(get_address(0x00407c1d), hook_patch_text));
    hooks.push_back(safetyhook::create_mid(get_address(0x00430deb), hook_change_target_frame_time));
    for (SafetyHookMid& hook : hooks)
    {
        Log("Mid Hook at %p -> %p. Enable: %d\n", hook.target(), reinterpret_cast<void*>(hook.destination()), hook.enabled());
    }
    SafetyHookInline h = safetyhook::create_inline(get_address(0x00431120), hook_modify_expected_frame_time);
    Log("Inline Hook at %p -> %p. Valid: %d\n", h.target(), reinterpret_cast<void*>(h.destination()), h.enabled());
    //char buffer[256];
    //sprintf_s(buffer, "Test %08x %08x\n", targetModule);
    //MessageBoxA(nullptr, buffer, buffer, MB_OK);

    window = *reinterpret_cast<uintptr_t*>(get_address(0x0055ed28));
    Log("Ptr to window: %p\n", window);
    // interpret offset 0x4c8 as a long long and store it in a variable
    unsigned long long frame_count = *reinterpret_cast<unsigned long long*>(window + 0x4c8);
    // intepret offset 0X98 as a long long and set it to 5
    *reinterpret_cast<unsigned long long*>(window + 0x98) = frame_count / 120;

    HANDLE current_process = GetCurrentProcess();
    constexpr byte data[] = {120};
    WriteProcessMemory(current_process, reinterpret_cast<LPVOID>(get_address(0x432640)), data, 1, nullptr); // NOP out a jump to skip our hook
    
    while (true)
    {
        if (GetAsyncKeyState(VK_F6) & 0x80000)
        {
            MessageBoxA(nullptr, "Hello World!", "Hello World!", MB_OK);
        }
        Sleep(100);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        //MessageBoxA(NULL, "Mod Loaded!", "Info", MB_OK | MB_ICONINFORMATION);
        HANDLE handle = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
        if (!handle)
        {
            MessageBoxA(NULL, "Failed to create thread!", "Error", MB_OK | MB_ICONERROR);
            return FALSE;
        }
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        for (SafetyHookMid& hook : hooks)
        {
            hook.reset();
        }
        hooks.clear();
    }
	
    return TRUE;
}

