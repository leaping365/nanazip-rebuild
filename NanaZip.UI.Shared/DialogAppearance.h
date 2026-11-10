/*
 * PROJECT:    NanaZip
 * PURPOSE:    Consistent typography and icons for filesystem dialogs.
 * LICENSE:    The MIT License
 */
#pragma once
#include <Windows.h>
#include <Shellapi.h>

#pragma comment(lib, "Shell32.lib")

namespace NanaZip::DialogAppearance
{
    inline constexpr UINT ActivateMessage = WM_APP + 0x4E5;

    inline HWND ForegroundOwner()
    {
        HWND Window = ::GetForegroundWindow();
        return Window ? ::GetAncestor(Window, GA_ROOT) : nullptr;
    }

    inline void Activate(HWND Window, HWND Input = nullptr)
    {
        ::ShowWindow(Window, SW_SHOWNORMAL);
        ::SetForegroundWindow(Window);
        if (Input && ::GetForegroundWindow() == Window)
            ::SetFocus(Input);
    }

    // Custom modal windows must have an owner and restore its enabled state.
    class ModalWindowScope
    {
        HWND m_Owner = ForegroundOwner();
        bool m_Disabled = false;
    public:
        HWND Owner() const { return m_Owner; }
        void Show(HWND Window)
        {
            if (::IsWindow(m_Owner) && ::IsWindowEnabled(m_Owner))
            {
                ::EnableWindow(m_Owner, FALSE);
                m_Disabled = true;
            }
            Activate(Window, ::GetDlgItem(Window, 100));
            ::PostMessageW(Window, ActivateMessage, 0, 0);
        }
        ~ModalWindowScope()
        {
            if (m_Disabled && ::IsWindow(m_Owner))
                ::EnableWindow(m_Owner, TRUE);
        }
        ModalWindowScope() = default;
        ModalWindowScope(ModalWindowScope const&) = delete;
        ModalWindowScope& operator=(ModalWindowScope const&) = delete;
    };

    inline HMODULE CurrentModule()
    {
        HMODULE Module = nullptr;
        ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&CurrentModule), &Module);
        return Module;
    }

    class DialogScope
    {
    private:
        DPI_AWARENESS_CONTEXT m_PreviousDpi = nullptr;
        DPI_HOSTING_BEHAVIOR m_PreviousHosting = DPI_HOSTING_BEHAVIOR_INVALID;
        HANDLE m_Activation = INVALID_HANDLE_VALUE;
        ULONG_PTR m_Cookie = 0;
        bool m_Active = false;

    public:
        DialogScope()
        {
            this->m_PreviousHosting = ::SetThreadDpiHostingBehavior(
                DPI_HOSTING_BEHAVIOR_MIXED);
            this->m_PreviousDpi = ::SetThreadDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            ACTCTXW Context = {};
            Context.cbSize = sizeof(Context);
            Context.dwFlags = ACTCTX_FLAG_HMODULE_VALID |
                ACTCTX_FLAG_RESOURCE_NAME_VALID;
            Context.hModule = CurrentModule();
            Context.lpResourceName = MAKEINTRESOURCEW(2);
            this->m_Activation = ::CreateActCtxW(&Context);
            if (this->m_Activation == INVALID_HANDLE_VALUE)
            {
                Context.lpResourceName = MAKEINTRESOURCEW(1);
                this->m_Activation = ::CreateActCtxW(&Context);
            }
            if (this->m_Activation != INVALID_HANDLE_VALUE)
                this->m_Active = !!::ActivateActCtx(
                    this->m_Activation, &this->m_Cookie);
        }

        ~DialogScope()
        {
            if (this->m_Active)
                ::DeactivateActCtx(0, this->m_Cookie);
            if (this->m_Activation != INVALID_HANDLE_VALUE)
                ::ReleaseActCtx(this->m_Activation);
            if (this->m_PreviousDpi)
                ::SetThreadDpiAwarenessContext(this->m_PreviousDpi);
            if (this->m_PreviousHosting != DPI_HOSTING_BEHAVIOR_INVALID)
                ::SetThreadDpiHostingBehavior(this->m_PreviousHosting);
        }

        DialogScope(DialogScope const&) = delete;
        DialogScope& operator=(DialogScope const&) = delete;
    };

    inline int PreferredClientHeight(size_t Count, bool Manager, bool Prefix)
    {
        size_t Maximum = Manager ? 20 : 30;
        int Rows = static_cast<int>(Count < 3 ? 3 :
            Count > Maximum ? Maximum : Count);
        return Manager ? 400 + Rows * 20 + 20 :
            (Prefix ? 450 : 411) + Rows * 20 + 30;
    }

    inline HFONT CreateFont(UINT Dpi, int Weight = FW_NORMAL)
    {
        LOGFONTW Font = {};
        Font.lfHeight = -::MulDiv(9, Dpi, 72);
        Font.lfWeight = Weight;
        Font.lfCharSet = DEFAULT_CHARSET;
        Font.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        wcscpy_s(Font.lfFaceName,
            PRIMARYLANGID(::GetUserDefaultUILanguage()) == LANG_CHINESE ?
                L"Microsoft YaHei UI" : L"Segoe UI");
        return ::CreateFontIndirectW(&Font);
    }

    inline void ApplyFont(HWND Window, HFONT Font)
    {
        ::SendMessageW(Window, WM_SETFONT, reinterpret_cast<WPARAM>(Font), TRUE);
        for (HWND Child = ::GetWindow(Window, GW_CHILD); Child;
            Child = ::GetWindow(Child, GW_HWNDNEXT))
            ::SendMessageW(Child, WM_SETFONT,
                reinterpret_cast<WPARAM>(Font), TRUE);
    }

    inline HICON LoadIcon(SHSTOCKICONID Identifier, UINT Dpi, bool Small)
    {
        int Metric = Small ? SM_CXSMICON : SM_CXICON;
        int Size = ::GetSystemMetricsForDpi(Metric, Dpi);
        if (Identifier == SIID_SETTINGS && Size > 0)
        {
            BITMAPINFO BitmapInfo = {};
            BitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            BitmapInfo.bmiHeader.biWidth = Size;
            BitmapInfo.bmiHeader.biHeight = -Size;
            BitmapInfo.bmiHeader.biPlanes = 1;
            BitmapInfo.bmiHeader.biBitCount = 32;
            BitmapInfo.bmiHeader.biCompression = BI_RGB;
            DWORD* Pixels = nullptr;
            HDC Device = ::CreateCompatibleDC(nullptr);
            HBITMAP Bitmap = ::CreateDIBSection(Device, &BitmapInfo,
                DIB_RGB_COLORS, reinterpret_cast<void**>(&Pixels), nullptr, 0);
            HFONT Font = ::CreateFontW(-(Size - 2), 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                DEFAULT_PITCH, L"Segoe MDL2 Assets");
            HICON Icon = nullptr;
            if (Device && Bitmap && Font && Pixels)
            {
                HGDIOBJ PreviousBitmap = ::SelectObject(Device, Bitmap);
                HGDIOBJ PreviousFont = ::SelectObject(Device, Font);
                RECT Rectangle = { 0, 0, Size, Size };
                ::FillRect(Device, &Rectangle,
                    static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
                ::SetTextColor(Device, RGB(255, 255, 255));
                ::SetBkMode(Device, TRANSPARENT);
                ::DrawTextW(Device, L"\xE713", 1, &Rectangle,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                ::GdiFlush();
                bool Visible = false;
                for (int Index = 0; Index < Size * Size; ++Index)
                {
                    DWORD Alpha = Pixels[Index] & 0xFF;
                    Visible = Visible || Alpha > 0;
                    Pixels[Index] = (Alpha << 24) |
                        ((103 * Alpha / 255) << 8) | (192 * Alpha / 255);
                }
                HBITMAP Mask = ::CreateBitmap(Size, Size, 1, 1, nullptr);
                ICONINFO Information = {};
                Information.fIcon = TRUE;
                Information.hbmColor = Bitmap;
                Information.hbmMask = Mask;
                if (Visible && Mask)
                    Icon = ::CreateIconIndirect(&Information);
                ::DeleteObject(Mask);
                ::SelectObject(Device, PreviousFont);
                ::SelectObject(Device, PreviousBitmap);
            }
            ::DeleteObject(Font);
            ::DeleteObject(Bitmap);
            ::DeleteDC(Device);
            if (Icon)
                return Icon;
        }
        SHSTOCKICONINFO Information = {};
        Information.cbSize = sizeof(Information);
        UINT Flags = SHGSI_ICON | (Small ? SHGSI_SMALLICON : SHGSI_LARGEICON);
        if (FAILED(::SHGetStockIconInfo(Identifier, Flags, &Information)) ||
            !Information.hIcon)
        {
            if (FAILED(::SHGetStockIconInfo(SIID_APPLICATION, Flags, &Information)))
                return nullptr;
        }
        HICON Icon = static_cast<HICON>(::CopyImage(Information.hIcon,
            IMAGE_ICON, Size, Size, 0));
        if (!Icon)
            return Information.hIcon;
        ::DestroyIcon(Information.hIcon);
        return Icon;
    }

    inline void ApplyIcons(HWND Window, SHSTOCKICONID Identifier,
        UINT Dpi, HICON& Small, HICON& Large)
    {
        HICON NewSmall = LoadIcon(Identifier, Dpi, true);
        HICON NewLarge = LoadIcon(Identifier, Dpi, false);
        ::SendMessageW(Window, WM_SETICON, ICON_SMALL,
            reinterpret_cast<LPARAM>(NewSmall));
        ::SendMessageW(Window, WM_SETICON, ICON_BIG,
            reinterpret_cast<LPARAM>(NewLarge));
        ::DestroyIcon(Small);
        ::DestroyIcon(Large);
        Small = NewSmall;
        Large = NewLarge;
        LONG_PTR Style = ::GetWindowLongPtrW(Window, GWL_EXSTYLE);
        if (Style & WS_EX_DLGMODALFRAME)
        {
            ::SetWindowLongPtrW(Window, GWL_EXSTYLE,
                Style & ~static_cast<LONG_PTR>(WS_EX_DLGMODALFRAME));
            ::SetWindowPos(Window, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                SWP_FRAMECHANGED);
        }
    }

    inline void ApplyFolderPrompt(HWND Window, UINT Dpi, HFONT& Font,
        HICON& Small, HICON& Large)
    {
        HFONT Previous = Font;
        Font = CreateFont(Dpi);
        ApplyFont(Window, Font);
        ::DeleteObject(Previous);
        ApplyIcons(Window, SIID_FOLDERFRONT, Dpi, Small, Large);
        auto Scale = [Dpi](int Value) { return ::MulDiv(Value, Dpi, 96); };
        ::MoveWindow(::GetDlgItem(Window, 101), Scale(16), Scale(16),
            Scale(328), Scale(22), TRUE);
        ::MoveWindow(::GetDlgItem(Window, 100), Scale(16), Scale(44),
            Scale(328), Scale(28), TRUE);
        ::MoveWindow(::GetDlgItem(Window, 1), Scale(176), Scale(88),
            Scale(80), Scale(28), TRUE);
        ::MoveWindow(::GetDlgItem(Window, 2), Scale(264), Scale(88),
            Scale(80), Scale(28), TRUE);
        RECT Rectangle = { 0, 0, Scale(360), Scale(132) };
        ::AdjustWindowRectExForDpi(&Rectangle,
            static_cast<DWORD>(::GetWindowLongPtrW(Window, GWL_STYLE)), FALSE,
            static_cast<DWORD>(::GetWindowLongPtrW(Window, GWL_EXSTYLE)), Dpi);
        ::SetWindowPos(Window, nullptr, 0, 0,
            Rectangle.right - Rectangle.left, Rectangle.bottom - Rectangle.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
