/*
 * PROJECT:    NanaZip
 * PURPOSE:    Check dialog label sizing and icons across display scales.
 * LICENSE:    The MIT License
 */
#include "../NanaZip.UI.Shared/DialogAppearance.h"
#include <cstdio>

int wmain()
{
    unsigned Checks = 0;
    DPI_AWARENESS_CONTEXT Contexts[] = {
        DPI_AWARENESS_CONTEXT_UNAWARE,
        DPI_AWARENESS_CONTEXT_SYSTEM_AWARE,
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE,
        DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED
    };
    for (DPI_AWARENESS_CONTEXT Context : Contexts)
    {
        DPI_AWARENESS_CONTEXT Previous = ::SetThreadDpiAwarenessContext(Context);
        if (!Previous)
            return 1;
        {
            NanaZip::DialogAppearance::DialogScope Scope;
            if (!::AreDpiAwarenessContextsEqual(::GetThreadDpiAwarenessContext(),
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
                return 1;
            ++Checks;
            HWND Window = ::CreateWindowExW(0, L"STATIC", L"DPI test",
                WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, nullptr, nullptr);
            HWND Child = ::CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD, 0, 0, 5, 5, Window, nullptr, nullptr, nullptr);
            bool Aware = Window && Child &&
                ::AreDpiAwarenessContextsEqual(::GetWindowDpiAwarenessContext(Window),
                    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
                ::AreDpiAwarenessContextsEqual(::GetWindowDpiAwarenessContext(Child),
                    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            ::DestroyWindow(Window);
            if (!Aware)
                return 1;
            ++Checks;
        }
        if (!::AreDpiAwarenessContextsEqual(::GetThreadDpiAwarenessContext(), Context))
            return 1;
        ::SetThreadDpiAwarenessContext(Previous);
        ++Checks;
    }
    bool ManagerModes[] = { false, true };
    for (bool Manager : ManagerModes)
    {
        int Small = NanaZip::DialogAppearance::PreferredClientHeight(1, Manager, false);
        int Medium = NanaZip::DialogAppearance::PreferredClientHeight(10, Manager, false);
        int Large = NanaZip::DialogAppearance::PreferredClientHeight(1000, Manager, false);
        if (!(Small < Medium && Medium <= Large) ||
            Small != NanaZip::DialogAppearance::PreferredClientHeight(0, Manager, false) ||
            Large != NanaZip::DialogAppearance::PreferredClientHeight(
                static_cast<size_t>(-1), Manager, false))
            return 1;
        ++Checks;
    }
    UINT Scales[] = { 96U, 120U, 144U, 192U };
    for (UINT Dpi : Scales)
    {
        HFONT Font = NanaZip::DialogAppearance::CreateFont(Dpi);
        if (!Font)
            return 1;
        LOGFONTW Description;
        if (!::GetObjectW(Font, sizeof(Description), &Description) ||
            Description.lfHeight != -::MulDiv(9, Dpi, 72))
            return 1;
        ++Checks;
        HDC Device = ::GetDC(nullptr);
        HGDIOBJ Previous = ::SelectObject(Device, Font);
        struct Label
        {
            wchar_t const* Text;
            int Width;
        };
        Label Labels[] = {
            { L"名称内容（括号、空格原样保留）", 292 },
            { L"旧前缀／查找文字（按需填写）", 292 },
            { L"右键菜单显示数量", 155 },
            { L"其余常用项在“更多常用名称”中选择", 338 },
            { L"编号等参数请在重命名窗口设置后保存。", 255 },
            { L"保存并完成", 79 },
            { L"管理常用项…", 84 },
            { L"跳过已有相同前缀的项目", 600 },
            { L"一行一个文件夹名，可以直接输入或粘贴。", 320 },
            { L"输入文件夹名", 328 }
        };
        for (Label const& Current : Labels)
        {
            SIZE Size;
            if (!::GetTextExtentPoint32W(Device, Current.Text,
                static_cast<int>(wcslen(Current.Text)), &Size) ||
                Size.cx > ::MulDiv(Current.Width, Dpi, 96) ||
                Size.cy > ::MulDiv(20, Dpi, 96))
            {
                std::printf("FAIL: text bounds at %u DPI\n", Dpi);
                return 1;
            }
            ++Checks;
        }
        ::SelectObject(Device, Previous);
        ::ReleaseDC(nullptr, Device);
        ::DeleteObject(Font);
        SHSTOCKICONID Identifiers[] = { SIID_RENAME, SIID_SETTINGS, SIID_FOLDERFRONT };
        for (SHSTOCKICONID Identifier : Identifiers)
        {
            HICON Small = NanaZip::DialogAppearance::LoadIcon(Identifier, Dpi, true);
            HICON Large = NanaZip::DialogAppearance::LoadIcon(Identifier, Dpi, false);
            if (!Small || !Large)
                return 1;
            ::DestroyIcon(Small);
            ::DestroyIcon(Large);
            ++Checks;
        }
    }
    std::printf("PASS: %u typography and icon checks at 100/125/150/200%%\n", Checks);
    return 0;
}
