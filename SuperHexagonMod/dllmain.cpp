#include <windows.h>
#include <safetyhook.hpp>
using PrintFunc_t = void(__thiscall*)(void* /*this*/, int x, int y, std::basic_string<char> param_3, int r, int g, int b, bool centered);

PrintFunc_t graphics_printString = nullptr;

enum
{
    IMAGE_BASE = 0x00400000
};

HMODULE targetModule;
uintptr_t GetAddress(uintptr_t ghidraAddress)
{
    uintptr_t moduleBase = reinterpret_cast<uintptr_t>(targetModule);
    return moduleBase + (ghidraAddress - IMAGE_BASE);
}
void initialize_game_functions()
{
    graphics_printString = reinterpret_cast<PrintFunc_t>(GetAddress(0x00422c90));
}

void test_hook(SafetyHookContext& ctx)
{
    void* thisPtr = reinterpret_cast<void*>(ctx.edi);
    graphics_printString(thisPtr, 0, 5, "FPS Unlock Patch", 255, 0, 0, false);
}

SafetyHookMid hook{};

DWORD WINAPI MainThread(LPVOID lpParam)
{
    targetModule = GetModuleHandle(L"SuperHexagon.exe");
    initialize_game_functions();
    uintptr_t uintptr = GetAddress(0x00407c1d);
    hook = safetyhook::create_mid(uintptr, test_hook);
    //char buffer[256];
    //sprintf_s(buffer, "Test %08x %08x\n", targetModule);
    //MessageBoxA(nullptr, buffer, buffer, MB_OK);

    HANDLE current_process = GetCurrentProcess();
    constexpr byte data[] = {120};
    WriteProcessMemory(current_process, reinterpret_cast<LPVOID>(GetAddress(0x432640)), data, 1, nullptr); // NOP out a jump to skip our hook
    
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
        MessageBoxA(NULL, "Mod Loaded!", "Info", MB_OK | MB_ICONINFORMATION);
        HANDLE handle = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
        if (!handle)
        {
            MessageBoxA(NULL, "Failed to create thread!", "Error", MB_OK | MB_ICONERROR);
            return FALSE;
        }
    }
	
    return TRUE;
}

