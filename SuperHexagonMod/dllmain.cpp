#include <windows.h>
#include <algorithm>
#include <safetyhook.hpp>
#include <vector>
using PrintFunc_t = void(__thiscall*)(void* /*this*/, int x, int y, std::basic_string<char> param_3, int r, int g, int b, bool centered);
using LogFunc_t = void(__cdecl*)(const char* format, ...);
using PrepareGame_t = void(__thiscall*)(void* /*this*/);
using Xml_SetIntValue_t = int(__thiscall*)(void* xml, std::basic_string<char>* name, int value, int which);
using Xml_GetIntValue_t = int(__thiscall*)(void* xml, std::basic_string<char>* name, int default_value, int which);

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
    uintptr_t window = 0;
    PrintFunc_t graphics_printString = nullptr;
    LogFunc_t Log = nullptr;
    PrepareGame_t superhex_prepareGame = nullptr;
    Xml_SetIntValue_t xml_setIntValue = nullptr;
    Xml_GetIntValue_t xml_getIntValue = nullptr;
    void initialize_game_functions()
    {
        graphics_printString = reinterpret_cast<PrintFunc_t>(get_address(0x00422c90));
        Log = reinterpret_cast<LogFunc_t>(get_address(0x0044d580));
        superhex_prepareGame = reinterpret_cast<PrepareGame_t>(get_address(0x00430ce0));
        xml_setIntValue = reinterpret_cast<Xml_SetIntValue_t>(get_address(0x0043fc50));
        xml_getIntValue = reinterpret_cast<Xml_GetIntValue_t>(get_address(0x0043f780));
    }
}

namespace
{
    constexpr int fps_options[] = { 60, 120, 144, 240 };
    constexpr int fps_options_count = std::size(fps_options);
    int current_fps_option = 0; // 120 FPS

    void set_fps_option(int option)
    {
        if (option < 0 || option >= fps_options_count)
            return;
        current_fps_option = option;
        if (window == 0)
        {
            Log("Wuh oh, window is null!");
            return;
        }
        unsigned long long frame_count = *reinterpret_cast<unsigned long long*>(window + 0x4c8);
        *reinterpret_cast<unsigned long long*>(window + 0x98) = frame_count / fps_options[current_fps_option];
    }
}

namespace
{
    void hook_patch_text(SafetyHookContext& ctx)
    {
        void* thisPtr = reinterpret_cast<void*>(ctx.edi);
        graphics_printString(thisPtr, 0, 5, "FPS Unlock Patch", 255, 0, 0, false);
    }

    void hook_modify_expected_frame_time(SafetyHookContext& ctx)
    {
        float time = 60.f / static_cast<float>(fps_options[current_fps_option]);
        // return is in ST(0)
        __asm {
            fld time
        }
    }

    void hook_do_not_round_frame_time(float f)
    {
        __asm {
            // directly return it to skip the rounding
            fld f
        }
    }

    void hook_change_target_frame_time(SafetyHookContext& ctx)
    {
        // get structure at EDI
        void* thisPtr = reinterpret_cast<void*>(ctx.edi);
        // set double at offset 0x40cd0 to 0.008333333 (120 FPS)
        double* targetFrameTime = reinterpret_cast<double*>(reinterpret_cast<uint8_t*>(thisPtr) + 0x40cd0);
        *targetFrameTime = 1.0 / static_cast<double>(fps_options[current_fps_option]);
    }

    double lost_movement = 0;
    void hook_restore_lost_movement(SafetyHookContext& ctx)
    {
        // get the double in Xmm1
        double desired_position = ctx.xmm1.f64[0];
        // interpret eax as a signed int
        int actual_position = static_cast<int>(ctx.eax);
        double position_diff = desired_position - actual_position;
        lost_movement += position_diff;
        if (int int_lost_movement = static_cast<int>(lost_movement); int_lost_movement != 0)
        {
            ctx.eax += int_lost_movement;
            lost_movement -= int_lost_movement;
        }
    }

    void hook_option_count_down(SafetyHookContext& ctx)
    {
        ctx.eax++; // increase the option count modulo
        ctx.esi++; // increase by 1 to do -1 instead of -2
    }

    void hook_option_count_up(SafetyHookContext& ctx)
    {
        ctx.ecx++; // increase the option count modulo
    }

    void hook_draw_option_text(SafetyHookContext& ctx)
    {
        void* graphics_ptr = reinterpret_cast<void*>(ctx.edi);
        int y = *reinterpret_cast<int*>(ctx.ebp - 0x470 + 4 /*EBP seems to be offset by 4...*/);
        char buffer[32];
        (void) sprintf_s(buffer, "FPS: %d", fps_options[current_fps_option]);
        graphics_printString(graphics_ptr, 0, y + 40, buffer, 255, 255, 255, true);
    }

    void hook_option_select(SafetyHookContext& ctx)
    {
        if (ctx.eax == 6) // selected option
        {
            set_fps_option((current_fps_option + 1) % fps_options_count);
            void* superhex_ptr = reinterpret_cast<void*>(ctx.edi);
            superhex_prepareGame(superhex_ptr);
            *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(superhex_ptr) + 0x48) = true;
        }
    }

    void hook_stupid_stuff(SafetyHookContext& ctx)
    {
        ctx.xmm2.f64[0] = 1.0 / 60.0;
    }

    void hook_clamp_value_to_0(SafetyHookContext& ctx)
    {
        // get structure at EDI
        float* value = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(ctx.edi) + 0x5500);
        *value = max(*value, 0.0f);
        // jump to 0x00429388
    }

    bool said_game_over = true;
    void hook_game_over(SafetyHookContext& ctx)
    {
        if (ctx.xmm1.f32[0] >= ctx.xmm3.f32[0] && !said_game_over)
        {
            ctx.eip = get_address(0x004293dc);
            said_game_over = true;
        }
        else if (ctx.xmm1.f32[0] < ctx.xmm3.f32[0])
        {
            ctx.eip = get_address(0x00429574);
            said_game_over = false;
        }
    }

    void hook_add_fps_option_to_options_file(SafetyHookContext& ctx)
    {
        std::basic_string option_name = "SETTINGS:fps";
        xml_setIntValue(reinterpret_cast<void*>(ctx.esi), &option_name, 0, 0);
    }

    void hook_read_fps_option(SafetyHookContext& ctx)
    {
        std::basic_string option_name = "SETTINGS:fps";
        set_fps_option(xml_getIntValue(reinterpret_cast<void*>(ctx.edi + 0xd960), &option_name, 0, 0));
    }

    void hook_save_fps_option(SafetyHookContext& ctx)
    {
        std::basic_string option_name = "SETTINGS:fps";
        xml_setIntValue(reinterpret_cast<void*>(ctx.edi), &option_name, current_fps_option, 0);
    }
}


std::vector<SafetyHookMid> mid_hooks;
std::vector<SafetyHookInline> inline_hooks;

DWORD WINAPI MainThread(LPVOID lpParam)
{
    targetModule = GetModuleHandle(L"SuperHexagon.exe");
    initialize_game_functions();
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00407c1d), hook_patch_text));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00430deb), hook_change_target_frame_time));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00424dc5), hook_restore_lost_movement));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00424d2d), hook_restore_lost_movement));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x004250ef), hook_restore_lost_movement));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00425145), hook_restore_lost_movement));
    // options
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x0042600a), hook_option_count_down));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x0042604e), hook_option_count_up));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00409e65), hook_draw_option_text));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x004261d1), hook_option_select));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00429114), hook_stupid_stuff));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00429383), hook_clamp_value_to_0));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x004293cf), hook_game_over));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00403bf3), hook_add_fps_option_to_options_file));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00403e30), hook_read_fps_option));
    mid_hooks.push_back(safetyhook::create_mid(get_address(0x00419ccc), hook_save_fps_option));

    
    inline_hooks.push_back(safetyhook::create_inline(get_address(0x00431120), hook_modify_expected_frame_time));
    inline_hooks.push_back(safetyhook::create_inline(get_address(0x00431140), hook_do_not_round_frame_time));
    
    for (SafetyHookMid& hook : mid_hooks)
    {
        Log("Mid Hook at %p -> %p. Enable: %d\n", hook.target(), reinterpret_cast<void*>(hook.destination()), hook.enabled());
    }
    
    for (SafetyHookInline& hook : inline_hooks)
    {
        Log("Inline Hook at %p -> %p. Enable: %d\n", hook.target(), reinterpret_cast<void*>(hook.destination()), hook.enabled());
    }
    //char buffer[256];
    //sprintf_s(buffer, "Test %08x %08x\n", targetModule);
    //MessageBoxA(nullptr, buffer, buffer, MB_OK);

    // TODO move this to a safer spot probably to avoid race conditions
    window = *reinterpret_cast<uintptr_t*>(get_address(0x0055ed28));
    Log("Ptr to window: %p\n", window);
    /*// interpret offset 0x4c8 as a long long and store it in a variable
    unsigned long long frame_count = *reinterpret_cast<unsigned long long*>(window + 0x4c8);
    // intepret offset 0X98 as a long long and set it to 5
    *reinterpret_cast<unsigned long long*>(window + 0x98) = frame_count / 120;*/
    
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
        for (SafetyHookMid& hook : mid_hooks)
        {
            hook.reset();
        }
        mid_hooks.clear();
        for (SafetyHookInline& h : inline_hooks)
        {
            h.reset();
        }
        inline_hooks.clear();
    }
	
    return TRUE;
}

