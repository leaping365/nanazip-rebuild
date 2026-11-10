/*
 * PROJECT:    NanaZip
 * PURPOSE:    Native preview-first rename and preset dialogs.
 * LICENSE:    The MIT License
 */
#include "Rename.h"
#include "DialogAppearance.h"
#include <CommCtrl.h>
#include <commdlg.h>
#include <Shellapi.h>
#include <algorithm>
#include <memory>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")

namespace NanaZip::Renaming
{
    enum Control
    {
        Summary = 100, Operation, InputLabel, Input, FindLabel, Find,
        Paste, Import, Clear, Skip, PreviewLabel, PreviewList,
        Up, Down, Status, Presets, ManagePresets, SavePreset, UndoButton,
        StartLabel, Start, DigitsLabel, Digits, Direction, DateSource,
        LetterCase, CountLabel, PresetList, Search, PresetLabel,
        PresetName, PresetValueLabel, PresetValue, PresetNew, PresetDelete,
        PresetUp, PresetDown, PresetVisible, MenuCount, MenuCountLabel,
        PresetFindLabel, PresetFind, PresetMode, PresetUse
    };

    static wchar_t const* ModeNames[] =
    {
        L"添加前缀", L"添加后缀", L"更换前缀", L"替换文字",
        L"删除指定文字", L"自动编号", L"删除开头／结尾字符",
        L"添加日期", L"清理空格", L"调整英文大小写", L"按名单重命名"
    };

    struct State
    {
        HWND Window = nullptr;
        HFONT Font = nullptr;
        HICON SmallIcon = nullptr;
        HICON LargeIcon = nullptr;
        UINT Dpi = 96;
        bool Updating = false;
        bool Manager = false;
        bool UndoMode = false;
        bool Dirty = false;
        bool Initialized = false;
        bool UserSized = false;
        bool Completion = false;
        bool Executing = false;
        std::wstring CompletionText;
        std::wstring Noun;
        std::vector<Item> Items;
        std::vector<Entry> UndoEntries;
        std::vector<Preset> Saved;
        unsigned MenuLimit = 8;
        int SelectedPreset = -1;
        Plan Current;
        Rule Settings;
        std::wstring ListText;
        State* Caller = nullptr;
    };

    static LRESULT CALLBACK InputProcedure(HWND Window, UINT MessageValue,
        WPARAM Word, LPARAM Long, UINT_PTR Identifier, DWORD_PTR Data);

    static HWND Child(State& Context, int Identifier)
    {
        return ::GetDlgItem(Context.Window, Identifier);
    }

    static std::wstring Text(State& Context, int Identifier)
    {
        HWND Window = Child(Context, Identifier);
        int Length = ::GetWindowTextLengthW(Window);
        std::vector<wchar_t> Buffer(static_cast<size_t>(Length) + 1);
        ::GetWindowTextW(Window, Buffer.data(), Length + 1);
        return Buffer.data();
    }

    static void SetText(State& Context, int Identifier,
        std::wstring const& Value)
    {
        ::SetWindowTextW(Child(Context, Identifier), Value.c_str());
    }

    static bool Checked(State& Context, int Identifier)
    {
        return ::SendDlgItemMessageW(Context.Window, Identifier,
            BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    static void Check(State& Context, int Identifier, bool Value)
    {
        ::SendDlgItemMessageW(Context.Window, Identifier,
            BM_SETCHECK, Value ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    static void Enable(State& Context, int Identifier, bool Value)
    {
        ::EnableWindow(Child(Context, Identifier), Value);
    }

    static void Visible(State& Context, int Identifier, bool Value)
    {
        ::ShowWindow(Child(Context, Identifier), Value ? SW_SHOW : SW_HIDE);
    }

    static int Scale(State& Context, int Value)
    {
        return ::MulDiv(Value, Context.Dpi, 96);
    }

    static void SetWindowIcons(State& Context)
    {
        SHSTOCKICONID Identifier = Context.Completion ? SIID_INFO :
            Context.Manager ? SIID_SETTINGS : SIID_RENAME;
        DialogAppearance::ApplyIcons(Context.Window, Identifier, Context.Dpi,
            Context.SmallIcon, Context.LargeIcon);
    }

    static void Place(State& Context, int Identifier,
        int Left, int Top, int Width, int Height)
    {
        ::MoveWindow(Child(Context, Identifier), Scale(Context, Left),
            Scale(Context, Top), Scale(Context, Width), Scale(Context, Height), TRUE);
    }

    static HWND Add(State& Context, int Identifier, wchar_t const* Class,
        wchar_t const* Label, DWORD Style = 0)
    {
        if (wcscmp(Class, L"STATIC") == 0)
        {
            if ((Style & SS_TYPEMASK) == SS_LEFT)
                Style |= SS_LEFTNOWORDWRAP;
            Style |= SS_NOPREFIX;
        }
        HWND Window = ::CreateWindowExW(0, Class, Label,
            WS_CHILD | WS_VISIBLE | Style, 0, 0, 0, 0,
            Context.Window, reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(Identifier)), nullptr, nullptr);
        ::SendMessageW(Window, WM_SETFONT,
            reinterpret_cast<WPARAM>(Context.Font), TRUE);
        if (wcscmp(Class, L"EDIT") == 0)
        {
            ::SendMessageW(Window, EM_SETLIMITTEXT, 16000000, 0);
        }
        if (!Context.Manager && (Style & WS_TABSTOP))
            ::SetWindowSubclass(Window, InputProcedure, 1,
                reinterpret_cast<DWORD_PTR>(&Context));
        return Window;
    }

    static void AddButton(State& Context, int Identifier,
        wchar_t const* Label, DWORD Style = BS_PUSHBUTTON)
    {
        Add(Context, Identifier, L"BUTTON", Label, WS_TABSTOP | Style);
    }

    static void AddCombo(State& Context, int Identifier,
        std::vector<std::wstring> const& Values)
    {
        HWND Combo = Add(Context, Identifier, L"COMBOBOX", L"",
            WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST);
        for (std::wstring const& Value : Values)
            ::SendMessageW(Combo, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(Value.c_str()));
        ::SendMessageW(Combo, CB_SETCURSEL, 0, 0);
    }

    static int ComboIndex(State& Context, int Identifier)
    {
        return static_cast<int>(::SendDlgItemMessageW(Context.Window,
            Identifier, CB_GETCURSEL, 0, 0));
    }

    static void ComboSelect(State& Context, int Identifier, int Index)
    {
        ::SendDlgItemMessageW(Context.Window, Identifier,
            CB_SETCURSEL, Index, 0);
    }

    static void Message(State& Context, std::wstring const& Value,
        UINT Flags = MB_OK | MB_ICONINFORMATION)
    {
        ::MessageBoxW(Context.Window, Value.c_str(), L"NanaZip · 重命名", Flags);
    }

    static bool Number(State& Context, int Identifier, unsigned& Value)
    {
        std::wstring String = Text(Context, Identifier);
        if (String.empty() || String.size() > 9)
            return false;
        Value = 0;
        for (wchar_t Character : String)
        {
            if (Character < L'0' || Character > L'9')
                return false;
            Value = Value * 10 + Character - L'0';
        }
        return true;
    }

    static void Layout(State& Context)
    {
        if (Context.Completion)
            return;
        RECT Rectangle;
        ::GetClientRect(Context.Window, &Rectangle);
        int Width = ::MulDiv(Rectangle.right, 96, Context.Dpi);
        int Height = ::MulDiv(Rectangle.bottom, 96, Context.Dpi);
        if (Context.Manager)
        {
            Place(Context, Summary, 20, 16, Width - 40, 22);
            Place(Context, Search, 20, 45, Width - 40, 28);
            int ListHeight = (std::max)(80, Height - 400);
            Place(Context, PresetList, 20, 85, Width - 40, ListHeight);
            int Top = 95 + ListHeight;
            Place(Context, PresetNew, 20, Top, 85, 28);
            Place(Context, PresetUp, 113, Top, 70, 28);
            Place(Context, PresetDown, 191, Top, 70, 28);
            Place(Context, PresetDelete, 269, Top, 70, 28);
            int ColumnWidth = Width / 2 - 28;
            Place(Context, PresetLabel, 20, Top + 40, ColumnWidth, 22);
            Place(Context, PresetName, 20, Top + 64, Width / 2 - 28, 28);
            Place(Context, PresetMode, Width / 2 + 4, Top + 64,
                Width / 2 - 24, 330);
            Place(Context, PresetValueLabel, 20, Top + 101, ColumnWidth, 22);
            Place(Context, PresetValue, 20, Top + 125, Width / 2 - 28, 28);
            Place(Context, PresetFindLabel, Width / 2 + 4, Top + 101,
                ColumnWidth, 22);
            Place(Context, PresetFind, Width / 2 + 4, Top + 125,
                Width / 2 - 24, 28);
            Place(Context, PresetVisible, 20, Top + 163, 310, 24);
            Place(Context, MenuCountLabel, 20, Height - 89, 155, 22);
            Place(Context, MenuCount, 183, Height - 92, 85, 150);
            Place(Context, CountLabel, 282, Height - 89, Width - 302, 22);
            Place(Context, Status, 20, Height - 49, Width - 385, 36);
            Place(Context, PresetUse, Width - 350, Height - 49, 125, 30);
            Place(Context, IDCANCEL, Width - 215, Height - 49, 90, 30);
            Place(Context, IDOK, Width - 115, Height - 49, 95, 30);
            return;
        }
        Place(Context, Summary, 20, 16, Width - 40, 22);
        Place(Context, Operation, 20, 46, 208, 360);
        Place(Context, Presets, 240, 46, Width - 370, 300);
        Place(Context, ManagePresets, Width - 120, 46, 100, 28);
        bool List = Context.Settings.Operation == Mode::List;
        Place(Context, InputLabel, 20, 88, Width - 40, 22);
        Place(Context, Input, 20, 113, Width - 40, List ? 120 : 29);
        Place(Context, FindLabel, 20, 154, Width - 40, 20);
        Place(Context, Find, 20, 179, Width - 40, 29);
        Place(Context, Paste, 20, 243, 78, 28);
        Place(Context, Import, 107, 243, 110, 28);
        Place(Context, Clear, 226, 243, 70, 28);
        Place(Context, SavePreset, Width - 160, 243, 140, 28);
        Place(Context, CountLabel, 309, 246, Width - 329, 25);
        Place(Context, StartLabel, 20, 216, 145, 20);
        Place(Context, Start, 20, 242, 145, 28);
        Place(Context, DigitsLabel, 177, 216, 110, 20);
        Place(Context, Digits, 177, 242, 105, 28);
        Place(Context, Direction, 20, 179, 208, 180);
        Place(Context, DateSource, 240, 179, 208, 180);
        Place(Context, LetterCase, 20, 179, 208, 160);
        if (Context.Settings.Operation == Mode::Trim)
        {
            Place(Context, StartLabel, 240, 152, 145, 20);
            Place(Context, Start, 240, 179, 145, 28);
        }
        Place(Context, Skip, 20, 284, Width - 40, 24);
        int Shift = Context.Settings.Operation == Mode::Prefix ? 0 : 39;
        Place(Context, PreviewLabel, 20, 323 - Shift, Width - 240, 24);
        Place(Context, Up, Width - 205, 319 - Shift, 88, 28);
        Place(Context, Down, Width - 108, 319 - Shift, 88, 28);
        Place(Context, PreviewList, 20, 357 - Shift, Width - 40,
            (std::max)(50, Height - 450 + Shift));
        int Available = Scale(Context, Width - 55);
        int Small = Scale(Context, 38);
        int CheckWidth = Scale(Context, 155);
        HWND ListWindow = Child(Context, PreviewList);
        ListView_SetColumnWidth(ListWindow, 0, Small);
        ListView_SetColumnWidth(ListWindow, 1, (Available - Small - CheckWidth) / 2);
        ListView_SetColumnWidth(ListWindow, 2, (Available - Small - CheckWidth) / 2);
        ListView_SetColumnWidth(ListWindow, 3, CheckWidth);
        Place(Context, Status, 20, Height - 83, Width - 40, 30);
        Place(Context, UndoButton, 20, Height - 43, 155, 30);
        Place(Context, IDCANCEL, Width - 233, Height - 43, 88, 30);
        Place(Context, IDOK, Width - 135, Height - 43, 115, 30);
    }

    static int MinimumClientHeight(State const& Context)
    {
        return Context.Manager ? 480 :
            Context.Settings.Operation == Mode::Prefix ? 500 : 461;
    }

    static void FitToItems(State& Context, bool Center = false)
    {
        if (Context.UserSized)
            return;
        MONITORINFO Monitor = {};
        Monitor.cbSize = sizeof(Monitor);
        if (!::GetMonitorInfoW(::MonitorFromWindow(Context.Window,
            MONITOR_DEFAULTTONEAREST), &Monitor))
            return;
        RECT Current;
        ::GetWindowRect(Context.Window, &Current);
        RECT Frame = {};
        ::AdjustWindowRectExForDpi(&Frame,
            static_cast<DWORD>(::GetWindowLongPtrW(Context.Window, GWL_STYLE)), FALSE,
            static_cast<DWORD>(::GetWindowLongPtrW(Context.Window, GWL_EXSTYLE)), Context.Dpi);
        int WorkWidth = Monitor.rcWork.right - Monitor.rcWork.left;
        int WorkHeight = Monitor.rcWork.bottom - Monitor.rcWork.top;
        int Minimum = Scale(Context, MinimumClientHeight(Context)) +
            Frame.bottom - Frame.top;
        int Preferred = Scale(Context, DialogAppearance::PreferredClientHeight(
            Context.Manager ? Context.Saved.size() : Context.Items.size(),
            Context.Manager, Context.Settings.Operation == Mode::Prefix)) +
            Frame.bottom - Frame.top;
        int Limit = (std::min)(WorkHeight,
            (std::max)(Minimum, WorkHeight * 85 / 100));
        int Height = (std::min)(Preferred, Limit);
        int CandidateWidth = Center ?
            Scale(Context, 760) + Frame.right - Frame.left :
            Current.right - Current.left;
        int Width = (std::min)(WorkWidth, CandidateWidth);
        int Left = Center ? Monitor.rcWork.left + (WorkWidth - Width) / 2 :
            (std::max)(Monitor.rcWork.left,
                (std::min)(Current.left, Monitor.rcWork.right - Width));
        int Top = Center ? Monitor.rcWork.top + (WorkHeight - Height) / 2 :
            (std::max)(Monitor.rcWork.top,
                (std::min)(Current.top, Monitor.rcWork.bottom - Height));
        if (Center || Current.right - Current.left != Width ||
            Current.bottom - Current.top != Height ||
            Current.left != Left || Current.top != Top)
            ::SetWindowPos(Context.Window, nullptr, Left, Top, Width, Height,
                SWP_NOZORDER | SWP_NOACTIVATE);
    }

    static void UpdateRule(State& Context)
    {
        Context.Settings.Operation = static_cast<Mode>(
            (std::max)(0, ComboIndex(Context, Operation)));
        Context.Settings.SkipPrefix = Checked(Context, Skip);
        Context.Settings.Find = Text(Context, Find);
        if (Context.Settings.Operation == Mode::List)
            Context.ListText = Text(Context, Input);
        else
            Context.Settings.Text = Text(Context, Input);
        Context.Settings.FromEnd = ComboIndex(Context, Direction) == 1;
        Context.Settings.ModifiedDate = ComboIndex(Context, DateSource) == 1;
        Context.Settings.UpperCase = ComboIndex(Context, LetterCase) == 1;
    }

    static void Refresh(State& Context)
    {
        Context.Updating = true;
        UpdateRule(Context);
        Mode OperationValue = Context.Settings.Operation;
        bool List = OperationValue == Mode::List;
        bool HasFind = OperationValue == Mode::Replace ||
            OperationValue == Mode::Remove || OperationValue == Mode::ChangePrefix;
        bool HasInput = List || OperationValue == Mode::Prefix ||
            OperationValue == Mode::Suffix || OperationValue == Mode::Number ||
            OperationValue == Mode::ChangePrefix || OperationValue == Mode::Replace;
        Visible(Context, Input, HasInput);
        Visible(Context, InputLabel, HasInput);
        Visible(Context, Find, HasFind);
        Visible(Context, FindLabel, HasFind);
        Visible(Context, Paste, List);
        Visible(Context, Import, List);
        Visible(Context, Clear, List);
        Visible(Context, CountLabel, List);
        Visible(Context, SavePreset, !List && !Context.UndoMode);
        Visible(Context, Skip, OperationValue == Mode::Prefix);
        Visible(Context, Start, OperationValue == Mode::Number ||
            OperationValue == Mode::Trim);
        Visible(Context, StartLabel, OperationValue == Mode::Number ||
            OperationValue == Mode::Trim);
        Visible(Context, Digits, OperationValue == Mode::Number);
        Visible(Context, DigitsLabel, OperationValue == Mode::Number);
        Visible(Context, Direction, OperationValue == Mode::Trim ||
            OperationValue == Mode::Date);
        Visible(Context, DateSource, OperationValue == Mode::Date);
        Visible(Context, LetterCase, OperationValue == Mode::LetterCase);
        SetText(Context, StartLabel, OperationValue == Mode::Trim ?
            L"删除字符数" : L"起始数字");
        SetText(Context, FindLabel, OperationValue == Mode::ChangePrefix ?
            L"要更换的旧前缀（仅匹配名称开头）" : L"查找文字");
        wchar_t const* Labels[] = { L"在原名称前添加", L"在主名称后添加",
            L"新前缀", L"替换为（留空表示删除）", L"", L"统一主名称（留空保留原名）" };
        SetText(Context, InputLabel, List ?
            L"新名称（一行一个；回车换行，Ctrl+Enter 确认）" :
            static_cast<int>(OperationValue) < 6 ?
                Labels[static_cast<int>(OperationValue)] : L"");
        SetText(Context, CountLabel, L"已输入 " +
            std::to_wstring(SplitLines(Context.ListText).size()) + L" 行");
        bool Numeric = true;
        if (OperationValue == Mode::Number)
        {
            Numeric = Number(Context, Start, Context.Settings.Start);
            unsigned DigitsValue = 0;
            Numeric = Number(Context, Digits, DigitsValue) &&
                DigitsValue > 0 && DigitsValue <= 12 && Numeric;
            Context.Settings.Digits = DigitsValue;
        }
        else if (OperationValue == Mode::Trim)
            Numeric = Number(Context, Start, Context.Settings.Length) &&
                Context.Settings.Length > 0;
        Context.Current = Context.UndoMode ? Validate(Context.UndoEntries) :
            Preview(Context.Items, Context.Settings, SplitLines(Context.ListText));
        if (!Numeric)
        {
            Context.Current.Valid = false;
            Context.Current.Error = L"请输入有效数字；编号位数为 1–12。";
        }
        HWND ListWindow = Child(Context, PreviewList);
        int Selected = ListView_GetNextItem(ListWindow, -1, LVNI_SELECTED);
        ::SendMessageW(ListWindow, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(ListWindow);
        for (size_t Index = 0; Index < Context.Current.Entries.size(); ++Index)
        {
            Entry const& Current = Context.Current.Entries[Index];
            std::wstring Order = std::to_wstring(Index + 1);
            LVITEMW Row = {};
            Row.mask = LVIF_TEXT;
            Row.iItem = static_cast<int>(Index);
            Row.pszText = const_cast<wchar_t*>(Order.c_str());
            ListView_InsertItem(ListWindow, &Row);
            ListView_SetItemText(ListWindow, Row.iItem, 1,
                const_cast<wchar_t*>(Current.Source.Name.c_str()));
            ListView_SetItemText(ListWindow, Row.iItem, 2,
                const_cast<wchar_t*>(Current.Name.c_str()));
            std::wstring StatusText = !Current.Error.empty() ? Current.Error :
                Current.Changed ? L"可重命名" : L"无需修改";
            ListView_SetItemText(ListWindow, Row.iItem, 3,
                const_cast<wchar_t*>(StatusText.c_str()));
        }
        if (Selected >= 0)
            ListView_SetItemState(ListWindow, Selected, LVIS_SELECTED, LVIS_SELECTED);
        ::SendMessageW(ListWindow, WM_SETREDRAW, TRUE, 0);
        ::InvalidateRect(ListWindow, nullptr, TRUE);
        std::wstring StatusText = Context.Current.Error;
        if (StatusText.empty())
            StatusText = std::to_wstring(Context.Items.size()) + L" 个" +
                Context.Noun + L" · " + std::to_wstring(Context.Current.Changes) +
                L" 个将更名 · " + (Context.Current.Valid ?
                    L"预览检查通过，不会覆盖已有项目" : L"无需修改");
        SetText(Context, Status, StatusText);
        Enable(Context, IDOK, Context.Current.Valid);
        Enable(Context, Up, Selected > 0 && !Context.UndoMode);
        Enable(Context, Down, Selected >= 0 &&
            Selected + 1 < static_cast<int>(Context.Items.size()) && !Context.UndoMode);
        Context.Dirty = false;
        Layout(Context);
        if (Context.Initialized)
            FitToItems(Context);
        Context.Updating = false;
    }

    static void PopulatePresets(State& Context)
    {
        HWND Combo = Child(Context, Presets);
        ::SendMessageW(Combo, CB_RESETCONTENT, 0, 0);
        ::SendMessageW(Combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(L"选择常用名称或规则…"));
        for (Preset const& Current : Context.Saved)
            ::SendMessageW(Combo, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(Current.Label.c_str()));
        ::SendMessageW(Combo, CB_SETCURSEL, 0, 0);
    }

    static void ApplyRule(State& Context, Rule const& Settings)
    {
        Context.Updating = true;
        Context.Settings = Settings;
        ComboSelect(Context, Operation, static_cast<int>(Settings.Operation));
        Check(Context, Skip, Settings.SkipPrefix);
        ComboSelect(Context, Direction, Settings.FromEnd ? 1 : 0);
        ComboSelect(Context, DateSource, Settings.ModifiedDate ? 1 : 0);
        ComboSelect(Context, LetterCase, Settings.UpperCase ? 1 : 0);
        SetText(Context, Input, Settings.Operation == Mode::List ?
            Context.ListText : Settings.Text);
        SetText(Context, Find, Settings.Find);
        SetText(Context, Start, std::to_wstring(Settings.Operation == Mode::Trim ?
            Settings.Length : Settings.Start));
        SetText(Context, Digits, std::to_wstring(Settings.Digits));
        Context.Updating = false;
        Refresh(Context);
    }

    static void RecreateInput(State& Context, bool Multiline)
    {
        ::DestroyWindow(Child(Context, Input));
        Add(Context, Input, L"EDIT", L"", WS_TABSTOP | WS_BORDER |
            (Multiline ? ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL |
                WS_VSCROLL : ES_AUTOHSCROLL));
    }

    static void PasteText(State& Context)
    {
        if (!::OpenClipboard(Context.Window))
        {
            Message(Context, L"剪贴板暂时不可用，请稍后重试。", MB_OK | MB_ICONWARNING);
            return;
        }
        HANDLE Data = ::GetClipboardData(CF_UNICODETEXT);
        wchar_t const* Value = Data ?
            static_cast<wchar_t const*>(::GlobalLock(Data)) : nullptr;
        if (Value)
        {
            size_t Limit = ::GlobalSize(Data) / sizeof(wchar_t);
            size_t Length = wcsnlen_s(Value, Limit);
            if (Length < Limit && Length <= 16000000)
                SetText(Context, Input, std::wstring(Value, Length));
            ::GlobalUnlock(Data);
        }
        ::CloseClipboard();
        if (!Value)
            Message(Context, L"剪贴板中没有可用的文字。", MB_OK | MB_ICONWARNING);
        Refresh(Context);
    }

    static void ImportText(State& Context)
    {
        std::vector<wchar_t> Path(32768);
        OPENFILENAMEW Options = {};
        Options.lStructSize = sizeof(Options);
        Options.hwndOwner = Context.Window;
        Options.lpstrFilter = L"文本文件 (*.txt)\0*.txt\0所有文件\0*.*\0\0";
        Options.lpstrFile = Path.data();
        Options.nMaxFile = static_cast<DWORD>(Path.size());
        Options.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        Options.lpstrTitle = L"导入重命名名单（一行一个名称）";
        if (!::GetOpenFileNameW(&Options))
            return;
        HANDLE File = ::CreateFileW(NativePath(Path.data()).c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER Size = {};
        bool Success = File != INVALID_HANDLE_VALUE &&
            ::GetFileSizeEx(File, &Size) && Size.QuadPart <= 32000000;
        std::vector<BYTE> Bytes;
        if (Success)
        {
            Bytes.resize(static_cast<size_t>(Size.QuadPart));
            DWORD Read = 0;
            Success = Bytes.empty() || (::ReadFile(File, Bytes.data(),
                static_cast<DWORD>(Bytes.size()), &Read, nullptr) && Read == Bytes.size());
        }
        if (File != INVALID_HANDLE_VALUE)
            ::CloseHandle(File);
        std::wstring Value;
        if (!Success || !DecodeText(Bytes, Value))
        {
            Message(Context, L"无法导入。请选择不超过 32 MB 的 UTF-8、UTF-16 或 GB18030 文本。",
                MB_OK | MB_ICONWARNING);
            return;
        }
        SetText(Context, Input, Value);
        Refresh(Context);
        SetText(Context, Status, L"已导入 " + Name(Path.data()) + L" · " +
            Text(Context, Status));
    }

    static void ManagerList(State& Context)
    {
        Context.Updating = true;
        HWND List = Child(Context, PresetList);
        ::SendMessageW(List, LB_RESETCONTENT, 0, 0);
        std::wstring SearchText = Text(Context, Search);
        for (size_t Index = 0; Index < Context.Saved.size(); ++Index)
        {
            Preset const& Current = Context.Saved[Index];
            std::wstring Label = Current.Label + L"  ·  " +
                ModeNames[static_cast<int>(Current.Settings.Operation)];
            if (!SearchText.empty() && ::FindNLSStringEx(LOCALE_NAME_INVARIANT,
                FIND_FROMSTART | NORM_IGNORECASE, Label.c_str(), -1,
                SearchText.c_str(), -1, nullptr, nullptr, nullptr, 0) < 0)
                continue;
            LRESULT Position = ::SendMessageW(List, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(Label.c_str()));
            ::SendMessageW(List, LB_SETITEMDATA, Position, Index);
            if (static_cast<int>(Index) == Context.SelectedPreset)
                ::SendMessageW(List, LB_SETCURSEL, Position, 0);
        }
        SetText(Context, Summary, L"常用名称与规则 · " +
            std::to_wstring(Context.Saved.size()) + L" 项 · 保存数量不限");
        Context.Updating = false;
    }

    static void ManagerFields(State& Context)
    {
        Context.Updating = true;
        bool Selected = Context.SelectedPreset >= 0 &&
            Context.SelectedPreset < static_cast<int>(Context.Saved.size());
        int Fields[] = { PresetName, PresetValue, PresetFind, PresetMode,
            PresetVisible, PresetDelete, PresetUse };
        for (int Identifier : Fields)
            Enable(Context, Identifier, Selected);
        Enable(Context, PresetUp, Selected && Context.SelectedPreset > 0);
        Enable(Context, PresetDown, Selected &&
            Context.SelectedPreset + 1 < static_cast<int>(Context.Saved.size()));
        if (Selected)
        {
            Preset const& Current = Context.Saved[Context.SelectedPreset];
            SetText(Context, PresetName, Current.Label);
            SetText(Context, PresetValue, Current.Settings.Text);
            SetText(Context, PresetFind, Current.Settings.Find);
            ComboSelect(Context, PresetMode, static_cast<int>(Current.Settings.Operation));
            Check(Context, PresetVisible, Current.ShowInMenu);
        }
        else
        {
            SetText(Context, PresetName, L"");
            SetText(Context, PresetValue, L"");
            SetText(Context, PresetFind, L"");
        }
        Context.Updating = false;
    }

    static bool Persist(State& Context)
    {
        for (Preset& Current : Context.Saved)
        {
            if (Current.Label.empty() && !Current.Settings.Text.empty())
                Current.Label = Current.Settings.Text;
            if (Current.Label.empty())
            {
                Message(Context, L"请填写名称内容，或为规则填写菜单显示名称。",
                    MB_OK | MB_ICONWARNING);
                return false;
            }
        }
        if (!SavePresets(Context.Saved, Context.MenuLimit))
        {
            Message(Context, L"无法保存常用名称，请检查当前用户设置的写入权限。",
                MB_OK | MB_ICONWARNING);
            return false;
        }
        return true;
    }

    static INT_PTR CALLBACK Procedure(HWND Window, UINT MessageValue,
        WPARAM Word, LPARAM Long);

    static INT_PTR Open(State& Context, HWND Owner)
    {
        HMODULE Module = nullptr;
        ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Procedure), &Module);
        struct Template
        {
            DLGTEMPLATE Dialog;
            WORD Menu;
            WORD Class;
            WORD Title;
        };
        Template Value = {};
        Value.Dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU |
            (Context.Completion ? 0 : WS_THICKFRAME) | DS_MODALFRAME | DS_CENTER;
        Value.Dialog.dwExtendedStyle = WS_EX_CONTROLPARENT;
        Value.Dialog.cx = 455;
        Value.Dialog.cy = 390;
        return ::DialogBoxIndirectParamW(Module, &Value.Dialog, Owner,
            Procedure, reinterpret_cast<LPARAM>(&Context));
    }

    static LRESULT CALLBACK InputProcedure(HWND Window, UINT MessageValue,
        WPARAM Word, LPARAM Long, UINT_PTR Identifier, DWORD_PTR Data)
    {
        UNREFERENCED_PARAMETER(Identifier);
        State* Context = reinterpret_cast<State*>(Data);
        bool Confirm = !!(::GetKeyState(VK_CONTROL) & 0x8000);
        if (MessageValue == WM_GETDLGCODE && Long)
        {
            MSG const* MessageValuePointer = reinterpret_cast<MSG const*>(Long);
            wchar_t Class[32];
            ::GetClassNameW(Window, Class, ARRAYSIZE(Class));
            if (MessageValuePointer->wParam == VK_RETURN &&
                (Confirm || wcscmp(Class, L"Edit") == 0 || wcscmp(Class, L"EDIT") == 0))
                return DLGC_WANTALLKEYS;
        }
        if (MessageValue == WM_KEYDOWN && Word == VK_RETURN && Confirm)
        {
            ::SendMessageW(Context->Window, WM_COMMAND, IDOK, 0);
            return 0;
        }
        if (MessageValue == WM_CHAR && Word == L'\r' && Confirm)
            return 0;
        if (MessageValue == WM_NCDESTROY)
            ::RemoveWindowSubclass(Window, InputProcedure, 1);
        return ::DefSubclassProc(Window, MessageValue, Word, Long);
    }

    static void Manage(State& Context, bool NewPreset)
    {
        State Manager;
        Manager.Manager = true;
        Manager.Caller = &Context;
        Manager.Items = Context.Items;
        Manager.Noun = Context.Noun;
        Manager.Saved = Context.Saved;
        Manager.MenuLimit = Context.MenuLimit;
        Manager.SelectedPreset = Manager.Saved.empty() ? -1 : 0;
        if (NewPreset)
        {
            UpdateRule(Context);
            Preset Current;
            Current.Settings = Context.Settings;
            Current.Label = std::wstring(ModeNames[static_cast<int>(
                Context.Settings.Operation)]) + L" · " + Context.Settings.Text;
            Manager.Saved.push_back(Current);
            Manager.SelectedPreset = static_cast<int>(Manager.Saved.size() - 1);
        }
        Open(Manager, Context.Window);
        LoadPresets(Context.Saved, Context.MenuLimit);
        PopulatePresets(Context);
    }

    static void CreateControls(State& Context)
    {
        Context.Font = DialogAppearance::CreateFont(Context.Dpi);
        Add(Context, Summary, L"STATIC", L"", SS_NOPREFIX);
        std::vector<std::wstring> Modes;
        for (wchar_t const* Value : ModeNames)
            Modes.push_back(Value);
        if (Context.Manager)
        {
            ::SetWindowTextW(Context.Window, L"NanaZip · 常用名称与规则");
            Add(Context, Search, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
            ::SendDlgItemMessageW(Context.Window, Search, EM_SETCUEBANNER,
                FALSE, reinterpret_cast<LPARAM>(L"输入常用名称后点“新增”，也可搜索已有名称"));
            Add(Context, PresetList, L"LISTBOX", L"", WS_TABSTOP | WS_BORDER |
                WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT);
            AddButton(Context, PresetNew, L"＋ 新增");
            AddButton(Context, PresetUp, L"上移");
            AddButton(Context, PresetDown, L"下移");
            AddButton(Context, PresetDelete, L"删除");
            Add(Context, PresetLabel, L"STATIC", L"菜单显示名称（默认同名称内容）");
            Add(Context, PresetName, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
            ::SendDlgItemMessageW(Context.Window, PresetName, EM_SETCUEBANNER,
                FALSE, reinterpret_cast<LPARAM>(L"默认使用下方名称内容"));
            Modes.pop_back();
            AddCombo(Context, PresetMode, Modes);
            Add(Context, PresetValueLabel, L"STATIC", L"名称内容（括号、空格原样保留）");
            Add(Context, PresetValue, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
            ::SendDlgItemMessageW(Context.Window, PresetValue, EM_SETCUEBANNER,
                FALSE, reinterpret_cast<LPARAM>(L"输入常用名称，例如【教学资料】"));
            Add(Context, PresetFindLabel, L"STATIC", L"旧前缀／查找文字（按需填写）");
            Add(Context, PresetFind, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
            AddButton(Context, PresetVisible, L"显示在右键快捷菜单中", BS_AUTOCHECKBOX);
            Add(Context, MenuCountLabel, L"STATIC", L"右键菜单显示数量");
            Add(Context, CountLabel, L"STATIC", L"其余常用项在“更多常用名称”中选择");
            AddCombo(Context, MenuCount, { L"5 项", L"8 项", L"12 项", L"20 项" });
            ComboSelect(Context, MenuCount, Context.MenuLimit == 5 ? 0 :
                Context.MenuLimit == 12 ? 2 : Context.MenuLimit == 20 ? 3 : 1);
            AddButton(Context, PresetUse, L"使用并预览");
            AddButton(Context, IDCANCEL, L"取消");
            AddButton(Context, IDOK, L"保存并完成", BS_DEFPUSHBUTTON);
            Add(Context, Status, L"STATIC", L"", SS_NOPREFIX);
            ManagerList(Context);
            ManagerFields(Context);
            SetText(Context, Status, L"编号等参数请在重命名窗口设置后保存。");
            return;
        }
        ::SetWindowTextW(Context.Window, (L"NanaZip · " +
            (Context.UndoMode ? std::wstring(L"撤销重命名预览") :
                Context.Noun + L"重命名")).c_str());
        SetText(Context, Summary, L"已选择 " + std::to_wstring(Context.Items.size()) +
            L" 个" + Context.Noun + L" · 所有操作确认前先预览");
        AddCombo(Context, Operation, Modes);
        AddCombo(Context, Presets, {});
        AddButton(Context, ManagePresets, L"管理常用项…");
        Add(Context, InputLabel, L"STATIC", L"");
        Add(Context, Input, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL);
        Add(Context, FindLabel, L"STATIC", L"");
        Add(Context, Find, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL);
        AddButton(Context, Paste, L"粘贴");
        AddButton(Context, Import, L"导入 TXT…");
        AddButton(Context, Clear, L"清空");
        AddButton(Context, SavePreset, L"保存为常用规则…");
        Add(Context, CountLabel, L"STATIC", L"");
        AddButton(Context, Skip, L"跳过已有相同前缀的项目", BS_AUTOCHECKBOX);
        Add(Context, StartLabel, L"STATIC", L"起始数字");
        Add(Context, Start, L"EDIT", L"1", WS_TABSTOP | WS_BORDER | ES_NUMBER);
        Add(Context, DigitsLabel, L"STATIC", L"编号位数（1–12）");
        Add(Context, Digits, L"EDIT", L"3", WS_TABSTOP | WS_BORDER | ES_NUMBER);
        AddCombo(Context, Direction, { L"名称开头", L"名称结尾" });
        AddCombo(Context, DateSource, { L"当天日期", L"文件／文件夹修改日期" });
        AddCombo(Context, LetterCase, { L"全部小写（英文字母）", L"全部大写（英文字母）" });
        Add(Context, PreviewLabel, L"STATIC", L"更名前预览 · 点击行可调整对应顺序");
        AddButton(Context, Up, L"↑ 上移");
        AddButton(Context, Down, L"↓ 下移");
        HWND List = Add(Context, PreviewList, WC_LISTVIEWW, L"",
            WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
        ListView_SetExtendedListViewStyle(List, LVS_EX_FULLROWSELECT |
            LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        wchar_t const* Columns[] = { L"序", L"原名称", L"新名称", L"检查结果" };
        for (int Index = 0; Index < 4; ++Index)
        {
            LVCOLUMNW Column = {};
            Column.mask = LVCF_TEXT | LVCF_WIDTH;
            Column.pszText = const_cast<wchar_t*>(Columns[Index]);
            Column.cx = 120;
            ListView_InsertColumn(List, Index, &Column);
        }
        AddButton(Context, UndoButton, L"撤销上次重命名…");
        AddButton(Context, IDCANCEL, L"取消");
        AddButton(Context, IDOK, L"确认重命名", BS_DEFPUSHBUTTON);
        Add(Context, Status, L"STATIC", L"", SS_NOPREFIX);
        PopulatePresets(Context);
        RecreateInput(Context, Context.Settings.Operation == Mode::List);
        ApplyRule(Context, Context.Settings);
        if (Context.UndoMode)
        {
            int Disabled[] = { Operation, Presets, ManagePresets, Input,
                Find, Skip, UndoButton };
            for (int Identifier : Disabled)
                Enable(Context, Identifier, false);
            SetText(Context, IDOK, L"确认撤销");
        }
    }

    static void ExecutePreview(State& Context)
    {
        if (Context.Dirty || !Context.Current.Valid)
        {
            Refresh(Context);
            return;
        }
        Plan Displayed = Context.Current;
        Refresh(Context);
        bool Same = Context.Current.Valid &&
            Context.Current.Entries.size() == Displayed.Entries.size();
        for (size_t Index = 0; Same && Index < Displayed.Entries.size(); ++Index)
            Same = Displayed.Entries[Index].Name ==
                Context.Current.Entries[Index].Name;
        if (!Same)
        {
            SetText(Context, Status, L"预览已更新，请检查后再次确认。");
            return;
        }
        Context.Executing = true;
        Enable(Context, IDOK, false);
        HCURSOR Previous = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        Result Output = Execute(Context.Current);
        ::SetCursor(Previous);
        if (!Output.Success)
        {
            Message(Context, Output.Message, MB_OK | MB_ICONERROR);
            Context.Executing = false;
            Refresh(Context);
            return;
        }
        bool Saved = SaveUndo(Context.UndoMode ? std::vector<Entry>() : Output.Undo);
        if (Saved)
        {
            State Completion;
            Completion.Completion = true;
            Completion.CompletionText = Output.Message + Context.Noun + L"。";
            Open(Completion, Context.Window);
        }
        else
            Message(Context, Output.Message + Context.Noun +
                L"。\r\n撤销记录未能保存。", MB_OK | MB_ICONWARNING);
        ::EndDialog(Context.Window, IDOK);
    }

    static void HandleManager(State& Context, int Identifier, int Notification)
    {
        if (Identifier == Search && Notification == EN_CHANGE)
        {
            ManagerList(Context);
            if (::SendDlgItemMessageW(Context.Window, PresetList,
                LB_GETCURSEL, 0, 0) == LB_ERR)
            {
                LRESULT Count = ::SendDlgItemMessageW(Context.Window,
                    PresetList, LB_GETCOUNT, 0, 0);
                Context.SelectedPreset = Count > 0 ? static_cast<int>(
                    ::SendDlgItemMessageW(Context.Window, PresetList,
                        LB_GETITEMDATA, 0, 0)) : -1;
                if (Count > 0)
                    ::SendDlgItemMessageW(Context.Window, PresetList,
                        LB_SETCURSEL, 0, 0);
            }
            ManagerFields(Context);
            return;
        }
        if (Identifier == PresetList && Notification == LBN_SELCHANGE)
        {
            LRESULT Position = ::SendDlgItemMessageW(Context.Window,
                PresetList, LB_GETCURSEL, 0, 0);
            Context.SelectedPreset = static_cast<int>(::SendDlgItemMessageW(
                Context.Window, PresetList, LB_GETITEMDATA, Position, 0));
            ManagerFields(Context);
            return;
        }
        if (Identifier == IDOK || Identifier == PresetUse)
        {
            if (!Persist(Context))
                return;
            if (Identifier == PresetUse && Context.Caller &&
                Context.SelectedPreset >= 0)
            {
                Rule Settings = Context.Saved[Context.SelectedPreset].Settings;
                RecreateInput(*Context.Caller, false);
                ApplyRule(*Context.Caller, Settings);
            }
            else if (Identifier == PresetUse && Context.SelectedPreset >= 0)
            {
                std::vector<std::wstring> Paths;
                for (Item const& Current : Context.Items)
                    Paths.push_back(Current.Path);
                Preset Current = Context.Saved[Context.SelectedPreset];
                Show(Paths, Context.Window, &Current);
            }
            ::EndDialog(Context.Window, IDOK);
            return;
        }
        if (Identifier == PresetNew && Notification == BN_CLICKED)
        {
            std::wstring NameValue = Text(Context, Search);
            Preset Current;
            Current.Label = NameValue;
            Current.Settings.Text = NameValue;
            int Draft = -1;
            for (size_t Index = 0; Index < Context.Saved.size(); ++Index)
            {
                Preset const& Candidate = Context.Saved[Index];
                if ((Candidate.Label.empty() || Candidate.Label == L"新常用名称") &&
                    Candidate.Settings.Text.empty() &&
                    Candidate.Settings.Operation == Mode::Prefix)
                {
                    Draft = static_cast<int>(Index);
                    break;
                }
            }
            if (Draft >= 0)
                Context.Saved[Draft] = Current;
            else
            {
                Context.Saved.push_back(Current);
                Draft = static_cast<int>(Context.Saved.size() - 1);
            }
            Context.SelectedPreset = Draft;
            Context.Updating = true;
            SetText(Context, Search, L"");
            Context.Updating = false;
            ManagerList(Context);
            ManagerFields(Context);
            if (Context.Initialized)
                FitToItems(Context);
            ::SetFocus(Child(Context, PresetValue));
            ::SendDlgItemMessageW(Context.Window, PresetValue, EM_SETSEL, 0, -1);
            return;
        }
        else if (Identifier == PresetDelete && Notification == BN_CLICKED && Context.SelectedPreset >= 0)
        {
            Context.Saved.erase(Context.Saved.begin() + Context.SelectedPreset);
            Context.SelectedPreset = Context.Saved.empty() ? -1 :
                (std::min)(Context.SelectedPreset, static_cast<int>(Context.Saved.size() - 1));
        }
        else if ((Identifier == PresetUp || Identifier == PresetDown) &&
            Notification == BN_CLICKED && Context.SelectedPreset >= 0)
        {
            int Other = Context.SelectedPreset + (Identifier == PresetUp ? -1 : 1);
            if (Other >= 0 && Other < static_cast<int>(Context.Saved.size()))
            {
                std::swap(Context.Saved[Other], Context.Saved[Context.SelectedPreset]);
                Context.SelectedPreset = Other;
            }
        }
        else if (Identifier == MenuCount && Notification == CBN_SELCHANGE)
        {
            unsigned Limits[] = { 5, 8, 12, 20 };
            Context.MenuLimit = Limits[(std::max)(0, ComboIndex(Context, MenuCount))];
        }
        else if (Context.SelectedPreset >= 0)
        {
            Preset& Current = Context.Saved[Context.SelectedPreset];
            if ((Identifier == PresetName || Identifier == PresetValue ||
                Identifier == PresetFind) && Notification == EN_CHANGE)
            {
                std::wstring NewText = Text(Context, PresetValue);
                bool Follow = Identifier == PresetValue &&
                    (Current.Label.empty() || Current.Label == Current.Settings.Text ||
                        Current.Label == L"新常用名称");
                Current.Label = Follow ? NewText : Text(Context, PresetName);
                Current.Settings.Text = NewText;
                Current.Settings.Find = Text(Context, PresetFind);
                if (Follow)
                {
                    Context.Updating = true;
                    SetText(Context, PresetName, Current.Label);
                    Context.Updating = false;
                }
                ManagerList(Context);
                return;
            }
            if (Identifier == PresetMode && Notification == CBN_SELCHANGE)
                Current.Settings.Operation = static_cast<Mode>(
                    ComboIndex(Context, PresetMode));
            else if (Identifier == PresetVisible && Notification == BN_CLICKED)
                Current.ShowInMenu = Checked(Context, PresetVisible);
            else
                return;
        }
        else
            return;
        ManagerList(Context);
        ManagerFields(Context);
        if (Context.Initialized)
            FitToItems(Context);
    }

    static INT_PTR ProcedureCore(HWND Window, UINT MessageValue,
        WPARAM Word, LPARAM Long)
    {
        State* Context = reinterpret_cast<State*>(
            ::GetWindowLongPtrW(Window, DWLP_USER));
        if (MessageValue == WM_INITDIALOG)
        {
            Context = reinterpret_cast<State*>(Long);
            Context->Window = Window;
            Context->Dpi = ::GetDpiForWindow(Window);
            ::SetWindowLongPtrW(Window, DWLP_USER, Long);
            ::SetDialogDpiChangeBehavior(Window, DDC_DISABLE_ALL, DDC_DISABLE_ALL);
            if (Context->Completion)
            {
                SetWindowIcons(*Context);
                Context->Font = DialogAppearance::CreateFont(Context->Dpi);
                ::SetWindowTextW(Window, L"NanaZip · 重命名");
                HWND Icon = Add(*Context, Summary, L"STATIC", L"", SS_ICON);
                ::SendMessageW(Icon, STM_SETICON,
                    reinterpret_cast<WPARAM>(Context->LargeIcon), 0);
                Add(*Context, Status, L"STATIC", Context->CompletionText.c_str());
                Place(*Context, Summary, 18, 22, 32, 32);
                Place(*Context, Status, 64, 28, 290, 28);
                RECT Rectangle = { 0, 0, Scale(*Context, 380), Scale(*Context, 86) };
                ::AdjustWindowRectExForDpi(&Rectangle,
                    static_cast<DWORD>(::GetWindowLongPtrW(Window, GWL_STYLE)), FALSE,
                    static_cast<DWORD>(::GetWindowLongPtrW(Window, GWL_EXSTYLE)), Context->Dpi);
                RECT OwnerRectangle;
                ::GetWindowRect(::GetWindow(Window, GW_OWNER), &OwnerRectangle);
                int Width = Rectangle.right - Rectangle.left;
                int Height = Rectangle.bottom - Rectangle.top;
                ::SetWindowPos(Window, nullptr,
                    OwnerRectangle.left + (OwnerRectangle.right - OwnerRectangle.left - Width) / 2,
                    OwnerRectangle.top + (OwnerRectangle.bottom - OwnerRectangle.top - Height) / 2,
                    Width, Height, SWP_NOZORDER);
                ::SetTimer(Window, 2, 1000, nullptr);
                return TRUE;
            }
            SetWindowIcons(*Context);
            Context->Updating = true;
            CreateControls(*Context);
            Context->Updating = false;
            Context->Initialized = true;
            FitToItems(*Context, true);
            Layout(*Context);
            // Activate after DialogBox has shown the window and initialized focus.
            // Doing this during WM_INITDIALOG can race the shell's menu teardown.
            ::PostMessageW(Window, DialogAppearance::ActivateMessage, 0, 0);
            return TRUE;
        }
        if (!Context)
            return FALSE;
        if (MessageValue == DialogAppearance::ActivateMessage)
        {
            DialogAppearance::Activate(Window);
            return TRUE;
        }
        if (Context->Completion)
        {
            if ((MessageValue == WM_TIMER && Word == 2) ||
                MessageValue == WM_CLOSE ||
                (MessageValue == WM_COMMAND && LOWORD(Word) == IDCANCEL))
            {
                ::EndDialog(Window, IDOK);
                return TRUE;
            }
            if (MessageValue == WM_DESTROY)
            {
                ::KillTimer(Window, 2);
                ::DeleteObject(Context->Font);
                ::DestroyIcon(Context->SmallIcon);
                ::DestroyIcon(Context->LargeIcon);
                return TRUE;
            }
            return FALSE;
        }
        switch (MessageValue)
        {
        case WM_SIZE:
            Layout(*Context);
            return TRUE;
        case WM_SIZING:
            Context->UserSized = true;
            return FALSE;
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO* Information = reinterpret_cast<MINMAXINFO*>(Long);
            Information->ptMinTrackSize.x = Scale(*Context, 640);
            RECT Minimum = { 0, 0, 0,
                Scale(*Context, MinimumClientHeight(*Context)) };
            ::AdjustWindowRectExForDpi(&Minimum,
                static_cast<DWORD>(::GetWindowLongPtrW(Window, GWL_STYLE)), FALSE,
                static_cast<DWORD>(::GetWindowLongPtrW(Window, GWL_EXSTYLE)), Context->Dpi);
            MONITORINFO Monitor = {};
            Monitor.cbSize = sizeof(Monitor);
            ::GetMonitorInfoW(::MonitorFromWindow(Window, MONITOR_DEFAULTTONEAREST), &Monitor);
            Information->ptMinTrackSize.y = (std::min)(
                Minimum.bottom - Minimum.top, Monitor.rcWork.bottom - Monitor.rcWork.top);
            return TRUE;
        }
        case WM_DPICHANGED:
        {
            Context->Dpi = HIWORD(Word);
            HFONT Previous = Context->Font;
            Context->Font = DialogAppearance::CreateFont(Context->Dpi);
            DialogAppearance::ApplyFont(Window, Context->Font);
            ::DeleteObject(Previous);
            SetWindowIcons(*Context);
            RECT* Rectangle = reinterpret_cast<RECT*>(Long);
            ::SetWindowPos(Window, nullptr, Rectangle->left, Rectangle->top,
                Rectangle->right - Rectangle->left, Rectangle->bottom - Rectangle->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            Layout(*Context);
            FitToItems(*Context);
            return TRUE;
        }
        case WM_TIMER:
            ::KillTimer(Window, 1);
            if (!Context->Manager && !Context->Executing)
                Refresh(*Context);
            return TRUE;
        case WM_CLOSE:
            ::EndDialog(Window, IDCANCEL);
            return TRUE;
        case WM_DESTROY:
            ::KillTimer(Window, 1);
            ::DeleteObject(Context->Font);
            ::DestroyIcon(Context->SmallIcon);
            ::DestroyIcon(Context->LargeIcon);
            return TRUE;
        case WM_NOTIFY:
        {
            NMHDR* Header = reinterpret_cast<NMHDR*>(Long);
            if (Header->idFrom == PreviewList && Header->code == LVN_ITEMCHANGED &&
                !Context->Updating)
            {
                int Selected = ListView_GetNextItem(Child(*Context, PreviewList), -1, LVNI_SELECTED);
                Enable(*Context, Up, Selected > 0 && !Context->UndoMode);
                Enable(*Context, Down, Selected >= 0 && Selected + 1 <
                    static_cast<int>(Context->Items.size()) && !Context->UndoMode);
            }
            if (Header->idFrom == PreviewList && Header->code == NM_CUSTOMDRAW)
            {
                NMLVCUSTOMDRAW* Draw = reinterpret_cast<NMLVCUSTOMDRAW*>(Long);
                LRESULT Value = CDRF_DODEFAULT;
                if (Draw->nmcd.dwDrawStage == CDDS_PREPAINT)
                    Value = CDRF_NOTIFYITEMDRAW;
                else if (Draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT &&
                    Draw->nmcd.dwItemSpec < Context->Current.Entries.size() &&
                    !Context->Current.Entries[Draw->nmcd.dwItemSpec].Error.empty())
                    Draw->clrText = RGB(180, 35, 24);
                ::SetWindowLongPtrW(Window, DWLP_MSGRESULT, Value);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
        {
            if (Context->Updating || Context->Executing)
                return TRUE;
            int Identifier = LOWORD(Word);
            int Notification = HIWORD(Word);
            if (Identifier == IDCANCEL)
            {
                ::EndDialog(Window, IDCANCEL);
                return TRUE;
            }
            if (Context->Manager)
            {
                HandleManager(*Context, Identifier, Notification);
                return TRUE;
            }
            if (Identifier == IDOK)
                ExecutePreview(*Context);
            else if (Identifier == Paste)
                PasteText(*Context);
            else if (Identifier == Import)
                ImportText(*Context);
            else if (Identifier == Clear)
            {
                SetText(*Context, Input, L"");
                Refresh(*Context);
            }
            else if (Identifier == ManagePresets || Identifier == SavePreset)
                Manage(*Context, Identifier == SavePreset);
            else if (Identifier == Presets && Notification == CBN_SELCHANGE)
            {
                int Index = ComboIndex(*Context, Presets) - 1;
                if (Index >= 0 && Index < static_cast<int>(Context->Saved.size()))
                {
                    Rule Settings = Context->Saved[Index].Settings;
                    RecreateInput(*Context, Settings.Operation == Mode::List);
                    ApplyRule(*Context, Settings);
                }
            }
            else if (Identifier == Operation && Notification == CBN_SELCHANGE)
            {
                Mode Previous = Context->Settings.Operation;
                Mode Next = static_cast<Mode>(ComboIndex(*Context, Operation));
                if (Previous == Mode::List)
                    Context->ListText = Text(*Context, Input);
                else
                    Context->Settings.Text = Text(*Context, Input);
                Context->Settings.Operation = Next;
                RecreateInput(*Context, Next == Mode::List);
                ApplyRule(*Context, Context->Settings);
            }
            else if (Identifier == Up || Identifier == Down)
            {
                int Selected = ListView_GetNextItem(Child(*Context, PreviewList), -1, LVNI_SELECTED);
                int Other = Selected + (Identifier == Up ? -1 : 1);
                if (!Context->UndoMode && Selected >= 0 && Other >= 0 &&
                    Other < static_cast<int>(Context->Items.size()))
                {
                    std::swap(Context->Items[Selected], Context->Items[Other]);
                    Refresh(*Context);
                    ListView_SetItemState(Child(*Context, PreviewList), Other,
                        LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
            else if (Identifier == UndoButton)
            {
                std::vector<Entry> Entries;
                if (!LoadUndo(Entries))
                    Message(*Context, L"没有可撤销的重命名记录。");
                else
                    Show({}, Window, nullptr, false, true);
            }
            else if (Notification == EN_CHANGE || Notification == CBN_SELCHANGE ||
                Identifier == Skip)
            {
                Context->Dirty = true;
                Enable(*Context, IDOK, false);
                if (Context->Items.size() == 1)
                    Refresh(*Context);
                else
                    ::SetTimer(Window, 1, 180, nullptr);
            }
            return TRUE;
        }
        }
        return FALSE;
    }

    static INT_PTR CALLBACK Procedure(HWND Window, UINT MessageValue,
        WPARAM Word, LPARAM Long)
    {
        try
        {
            return ProcedureCore(Window, MessageValue, Word, Long);
        }
        catch (...)
        {
            ::MessageBoxW(Window, L"操作异常中断，请检查所选目录中的实际名称。",
                L"NanaZip · 重命名", MB_OK | MB_ICONERROR);
            ::EndDialog(Window, IDCANCEL);
            return TRUE;
        }
    }

    void Show(std::vector<std::wstring> const& Paths, HWND Owner,
        Preset const* Initial, bool Manager, bool Undo)
    {
        DialogAppearance::DialogScope Appearance;
        if (!Owner)
            Owner = ::GetForegroundWindow();
        if (Owner)
            Owner = ::GetAncestor(Owner, GA_ROOT);
        INITCOMMONCONTROLSEX Controls = {};
        Controls.dwSize = sizeof(Controls);
        Controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
        ::InitCommonControlsEx(&Controls);
        State Context;
        Context.Manager = Manager;
        Context.UndoMode = Undo;
        Context.Items = ReadItems(Paths);
        Context.Noun = SelectionNoun(Paths);
        LoadPresets(Context.Saved, Context.MenuLimit);
        Context.SelectedPreset = Context.Saved.empty() ? -1 : 0;
        if (Initial)
            Context.Settings = Initial->Settings;
        if (Undo)
        {
            if (!LoadUndo(Context.UndoEntries))
            {
                ::MessageBoxW(Owner, L"没有可撤销的重命名记录。",
                    L"NanaZip", MB_OK | MB_ICONINFORMATION);
                return;
            }
            std::vector<std::wstring> UndoPaths;
            Context.Items.clear();
            for (Entry const& Current : Context.UndoEntries)
            {
                Context.Items.push_back(Current.Source);
                UndoPaths.push_back(Current.Source.Path);
            }
            Context.Noun = SelectionNoun(UndoPaths);
        }
        if (!Manager && !Undo && Paths.empty())
            return;
        if (Context.Items.size() == 1 && !Initial)
        {
            Context.Settings.Operation = Mode::List;
            std::wstring NameValue = Context.Items[0].Name;
            size_t Dot = NameValue.find_last_of(L'.');
            if (!Context.Items[0].Directory && Dot != std::wstring::npos && Dot > 0)
                NameValue.resize(Dot);
            Context.ListText = NameValue;
        }
        HRESULT Initialized = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (Open(Context, Owner) == -1)
            ::MessageBoxW(Owner, L"无法打开重命名窗口。", L"NanaZip", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(Initialized))
            ::CoUninitialize();
    }
}
