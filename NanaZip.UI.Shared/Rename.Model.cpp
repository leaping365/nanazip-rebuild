/*
 * PROJECT:    NanaZip
 * PURPOSE:    Rename planning, validation, rollback and user settings.
 * LICENSE:    The MIT License
 */
#include "Rename.h"
#include <ShlObj.h>
#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <limits>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace NanaZip::Renaming
{
    static wchar_t const* SettingsKey = L"Software\\NanaZip\\Renaming";

    struct PathLess
    {
        bool operator()(std::wstring const& Left,
            std::wstring const& Right) const
        {
            return ::CompareStringOrdinal(Left.c_str(), -1,
                Right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        }
    };

    static bool Equal(std::wstring const& Left, std::wstring const& Right)
    {
        return ::CompareStringOrdinal(Left.c_str(), -1,
            Right.c_str(), -1, TRUE) == CSTR_EQUAL;
    }

    std::wstring NativePath(std::wstring const& Path)
    {
        if (Path.compare(0, 4, L"\\\\?\\") == 0)
            return Path;
        if (Path.compare(0, 2, L"\\\\") == 0)
            return L"\\\\?\\UNC\\" + Path.substr(2);
        return L"\\\\?\\" + Path;
    }

    std::wstring Parent(std::wstring const& Path)
    {
        size_t Position = Path.find_last_of(L"\\/");
        return Position == std::wstring::npos ? L"" :
            Path.substr(0, Position + 1);
    }

    std::wstring Name(std::wstring const& Path)
    {
        return Path.substr(Path.find_last_of(L"\\/") + 1);
    }

    static bool Identify(std::wstring const& Path,
        BY_HANDLE_FILE_INFORMATION& Information)
    {
        HANDLE File = ::CreateFileW(NativePath(Path).c_str(), 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (File == INVALID_HANDLE_VALUE)
            return false;
        bool Success = !!::GetFileInformationByHandle(File, &Information);
        ::CloseHandle(File);
        return Success;
    }

    static bool SameIdentity(BY_HANDLE_FILE_INFORMATION const& Left,
        BY_HANDLE_FILE_INFORMATION const& Right)
    {
        return Left.dwVolumeSerialNumber == Right.dwVolumeSerialNumber &&
            Left.nFileIndexHigh == Right.nFileIndexHigh &&
            Left.nFileIndexLow == Right.nFileIndexLow &&
            Left.ftCreationTime.dwHighDateTime ==
                Right.ftCreationTime.dwHighDateTime &&
            Left.ftCreationTime.dwLowDateTime ==
                Right.ftCreationTime.dwLowDateTime;
    }

    std::vector<Item> ReadItems(std::vector<std::wstring> const& Paths)
    {
        std::vector<Item> Items;
        for (std::wstring const& Path : Paths)
        {
            Item Current;
            Current.Path = Path;
            while (Current.Path.size() > 3 &&
                Current.Path.back() == L'\\')
                Current.Path.pop_back();
            Current.Name = Name(Current.Path);
            Current.Identified = Identify(Current.Path, Current.Identity);
            Current.Directory = Current.Identified &&
                (Current.Identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
            Items.push_back(Current);
        }
        return Items;
    }

    std::wstring SelectionNoun(std::vector<std::wstring> const& Paths)
    {
        bool Files = false;
        bool Directories = false;
        for (std::wstring const& Path : Paths)
        {
            DWORD Attributes = ::GetFileAttributesW(NativePath(Path).c_str());
            if (Attributes != INVALID_FILE_ATTRIBUTES &&
                (Attributes & FILE_ATTRIBUTE_DIRECTORY))
                Directories = true;
            else
                Files = true;
        }
        return Files && Directories ? L"项目" :
            Directories ? L"文件夹" : L"文件";
    }

    std::vector<std::wstring> SplitLines(std::wstring const& Text)
    {
        std::vector<std::wstring> Lines;
        size_t Start = !Text.empty() && Text[0] == 0xFEFF ? 1 : 0;
        for (size_t Index = Start; Index < Text.size(); ++Index)
        {
            if (Text[Index] == L'\r' || Text[Index] == L'\n')
            {
                Lines.push_back(Text.substr(Start, Index - Start));
                if (Text[Index] == L'\r' && Index + 1 < Text.size() &&
                    Text[Index + 1] == L'\n')
                    ++Index;
                Start = Index + 1;
            }
        }
        Lines.push_back(Text.substr(Start));
        while (!Lines.empty() && Lines.back().empty())
            Lines.pop_back();
        return Lines;
    }

    bool DecodeText(std::vector<BYTE> const& Bytes, std::wstring& Text)
    {
        Text.clear();
        if (Bytes.empty())
            return true;
        if (Bytes.size() > INT_MAX)
            return false;
        if (Bytes.size() >= 2 &&
            ((Bytes[0] == 0xFF && Bytes[1] == 0xFE) ||
                (Bytes[0] == 0xFE && Bytes[1] == 0xFF)))
        {
            if (Bytes.size() % 2)
                return false;
            bool Little = Bytes[0] == 0xFF;
            for (size_t Index = 2; Index < Bytes.size(); Index += 2)
                Text.push_back(static_cast<wchar_t>(Little ?
                    Bytes[Index] | (Bytes[Index + 1] << 8) :
                    (Bytes[Index] << 8) | Bytes[Index + 1]));
        }
        else
        {
            size_t Offset = Bytes.size() >= 3 && Bytes[0] == 0xEF &&
                Bytes[1] == 0xBB && Bytes[2] == 0xBF ? 3 : 0;
            if (Offset == Bytes.size())
                return true;
            char const* Data = reinterpret_cast<char const*>(Bytes.data() + Offset);
            int Length = static_cast<int>(Bytes.size() - Offset);
            UINT CodePage = CP_UTF8;
            int Count = ::MultiByteToWideChar(CodePage, MB_ERR_INVALID_CHARS,
                Data, Length, nullptr, 0);
            if (!Count && !Offset)
            {
                CodePage = 54936;
                Count = ::MultiByteToWideChar(CodePage, MB_ERR_INVALID_CHARS,
                    Data, Length, nullptr, 0);
            }
            if (!Count)
                return false;
            Text.resize(Count);
            ::MultiByteToWideChar(CodePage, MB_ERR_INVALID_CHARS,
                Data, Length, &Text[0], Count);
        }
        for (size_t Index = 0; Index < Text.size(); ++Index)
        {
            wchar_t Character = Text[Index];
            if (!Character)
                return false;
            if (Character >= 0xD800 && Character <= 0xDBFF)
            {
                if (++Index >= Text.size() || Text[Index] < 0xDC00 ||
                    Text[Index] > 0xDFFF)
                    return false;
            }
            else if (Character >= 0xDC00 && Character <= 0xDFFF)
                return false;
        }
        return true;
    }

    static std::wstring NameError(std::wstring const& Value)
    {
        if (Value.empty())
            return L"名称为空";
        if (Value.size() > 255)
            return L"名称超过 255 个字符";
        if (Value.back() == L'.' || Value.back() == L' ')
            return L"名称不能以空格或句点结尾";
        for (wchar_t Character : Value)
            if (Character < 32 || wcschr(L"<>:\"/\\|?*", Character))
                return L"名称含有非法字符";
        std::wstring Base = Value.substr(0, Value.find(L'.'));
        while (!Base.empty() && Base.back() == L' ')
            Base.pop_back();
        if (Equal(Base, L"CON") || Equal(Base, L"PRN") ||
            Equal(Base, L"AUX") || Equal(Base, L"NUL") ||
            Equal(Base, L"CONIN$") || Equal(Base, L"CONOUT$"))
            return L"系统保留名称";
        if (Base.size() == 4 &&
            (Equal(Base.substr(0, 3), L"COM") ||
                Equal(Base.substr(0, 3), L"LPT")) &&
            ((Base[3] >= L'1' && Base[3] <= L'9') ||
                Base[3] == 0xB9 || Base[3] == 0xB2 || Base[3] == 0xB3))
            return L"系统保留名称";
        return L"";
    }

    Plan Validate(std::vector<Entry> const& Entries)
    {
        Plan Output;
        Output.Entries = Entries;
        std::set<std::wstring, PathLess> Sources;
        std::set<std::wstring, PathLess> Directories;
        std::map<std::wstring, size_t, PathLess> Targets;
        for (Entry const& Current : Entries)
        {
            Sources.insert(Current.Source.Path);
            if (Current.Source.Directory)
                Directories.insert(Current.Source.Path);
        }
        for (Entry& Current : Output.Entries)
        {
            Current.Error = NameError(Current.Name);
            Current.Target = Parent(Current.Source.Path) + Current.Name;
            Current.Changed = Current.Source.Name != Current.Name;
            BY_HANDLE_FILE_INFORMATION Identity;
            if (!Current.Source.Identified ||
                !Identify(Current.Source.Path, Identity) ||
                !SameIdentity(Current.Source.Identity, Identity))
                Current.Error = L"源项目已变化或无法访问，请重新选择";
            if (Current.Source.Path.size() <= 3 ||
                Parent(Current.Source.Path).empty() ||
                (Current.Source.Path.compare(0, 2, L"\\\\") == 0 &&
                    Current.Source.Path.find(L'\\', 2) ==
                        Current.Source.Path.find_last_of(L'\\')))
                Current.Error = L"不能重命名驱动器或共享根目录";
            if (Current.Target.size() >= 32760)
                Current.Error = L"完整路径过长";
            ++Targets[Current.Target];
            std::wstring Ancestor = Parent(Current.Source.Path);
            while (Ancestor.size() > 3)
            {
                Ancestor.pop_back();
                if (Directories.count(Ancestor))
                    Current.Error = L"请勿同时选择父文件夹及其内部项目";
                Ancestor = Parent(Ancestor);
            }
            if (Current.Error.empty() && Current.Changed &&
                !Sources.count(Current.Target))
            {
                DWORD Attributes = ::GetFileAttributesW(
                    NativePath(Current.Target).c_str());
                DWORD Error = ::GetLastError();
                if (Attributes != INVALID_FILE_ATTRIBUTES)
                    Current.Error = L"目标已存在，不会覆盖";
                else if (Error != ERROR_FILE_NOT_FOUND &&
                    Error != ERROR_PATH_NOT_FOUND)
                    Current.Error = L"无法检查目标路径";
            }
        }
        for (Entry& Current : Output.Entries)
        {
            if (Targets[Current.Target] > 1 && Current.Error.empty())
                Current.Error = L"新名称重复";
            if (!Current.Error.empty())
                Output.Error = L"请先修正预览中标记的问题。";
            if (Current.Changed)
                ++Output.Changes;
        }
        Output.Valid = Output.Error.empty() && Output.Changes > 0;
        return Output;
    }

    static void ReplaceAll(std::wstring& Text, std::wstring const& Find,
        std::wstring const& Replacement)
    {
        if (Find.empty())
            return;
        size_t Position = 0;
        while ((Position = Text.find(Find, Position)) != std::wstring::npos)
        {
            Text.replace(Position, Find.size(), Replacement);
            Position += Replacement.size();
        }
    }

    Plan Preview(std::vector<Item> const& Items, Rule const& Settings,
        std::vector<std::wstring> const& Names)
    {
        std::vector<Entry> Entries;
        for (size_t Index = 0; Index < Items.size(); ++Index)
        {
            Entry Current;
            Current.Source = Items[Index];
            std::wstring Stem = Current.Source.Name;
            std::wstring Extension;
            size_t Dot = Stem.find_last_of(L'.');
            if (!Current.Source.Directory &&
                Dot != std::wstring::npos && Dot > 0)
            {
                Extension = Stem.substr(Dot);
                Stem.resize(Dot);
            }
            switch (Settings.Operation)
            {
            case Mode::Prefix:
                if (!Settings.SkipPrefix ||
                    Stem.compare(0, Settings.Text.size(), Settings.Text) != 0)
                    Stem = Settings.Text + Stem;
                break;
            case Mode::Suffix:
                Stem += Settings.Text;
                break;
            case Mode::ChangePrefix:
                if (!Settings.Find.empty() &&
                    Stem.compare(0, Settings.Find.size(), Settings.Find) == 0)
                    Stem = Settings.Text + Stem.substr(Settings.Find.size());
                break;
            case Mode::Replace:
            case Mode::Remove:
                ReplaceAll(Stem, Settings.Find,
                    Settings.Operation == Mode::Remove ? L"" : Settings.Text);
                break;
            case Mode::Number:
            {
                std::wstring Number = std::to_wstring(
                    static_cast<unsigned long long>(Settings.Start) + Index);
                if (Settings.Digits > Number.size() && Settings.Digits <= 12)
                    Number.insert(0, Settings.Digits - Number.size(), L'0');
                Stem = (Settings.Text.empty() ? Stem : Settings.Text) +
                    L"_" + Number;
                break;
            }
            case Mode::Trim:
            {
                size_t Count = 0;
                if (!Settings.FromEnd)
                {
                    for (unsigned Number = 0;
                        Number < Settings.Length && Count < Stem.size(); ++Number)
                    {
                        wchar_t Character = Stem[Count++];
                        if (Character >= 0xD800 && Character <= 0xDBFF &&
                            Count < Stem.size())
                            ++Count;
                    }
                    Stem.erase(0, Count);
                }
                else
                {
                    Count = Stem.size();
                    for (unsigned Number = 0;
                        Number < Settings.Length && Count > 0; ++Number)
                    {
                        wchar_t Character = Stem[--Count];
                        if (Character >= 0xDC00 && Character <= 0xDFFF && Count)
                            --Count;
                    }
                    Stem.resize(Count);
                }
                break;
            }
            case Mode::Date:
            {
                SYSTEMTIME Date;
                ::GetLocalTime(&Date);
                if (Settings.ModifiedDate)
                {
                    FILETIME Local;
                    ::FileTimeToLocalFileTime(
                        &Current.Source.Identity.ftLastWriteTime, &Local);
                    ::FileTimeToSystemTime(&Local, &Date);
                }
                wchar_t Buffer[32];
                swprintf_s(Buffer, L"%04u-%02u-%02u",
                    Date.wYear, Date.wMonth, Date.wDay);
                Stem = Settings.FromEnd ? Stem + L"_" + Buffer :
                    std::wstring(Buffer) + L"_" + Stem;
                break;
            }
            case Mode::Clean:
            {
                std::wstring Cleaned;
                bool Space = false;
                for (wchar_t Character : Stem)
                {
                    if (iswspace(Character))
                        Space = !Cleaned.empty();
                    else
                    {
                        if (Space)
                            Cleaned += L' ';
                        Cleaned += Character;
                        Space = false;
                    }
                }
                Stem = Cleaned;
                break;
            }
            case Mode::LetterCase:
                for (wchar_t& Character : Stem)
                {
                    if (Settings.UpperCase && Character >= L'a' && Character <= L'z')
                        Character -= L'a' - L'A';
                    else if (!Settings.UpperCase && Character >= L'A' && Character <= L'Z')
                        Character += L'a' - L'A';
                }
                break;
            case Mode::List:
                Stem = Index < Names.size() ? Names[Index] : L"";
                break;
            }
            Current.Name = Stem.empty() ? L"" : Stem + Extension;
            Entries.push_back(Current);
        }
        Plan Output = Validate(Entries);
        if (Settings.Operation == Mode::List && Names.size() != Items.size())
            Output.Error = L"名称行数（" + std::to_wstring(Names.size()) +
                L"）必须与所选数量（" + std::to_wstring(Items.size()) + L"）一致。";
        if ((Settings.Operation == Mode::ChangePrefix ||
            Settings.Operation == Mode::Replace ||
            Settings.Operation == Mode::Remove) && Settings.Find.empty())
            Output.Error = L"请输入需要查找或更换的文字。";
        if ((Settings.Operation == Mode::Number && Settings.Digits > 12) ||
            (Settings.Operation == Mode::Trim && !Settings.Length))
            Output.Error = L"请输入有效的数字参数。";
        Output.Valid = Output.Error.empty() && Output.Changes > 0;
        return Output;
    }

    static std::wstring ErrorMessage(DWORD Code)
    {
        wchar_t* Buffer = nullptr;
        ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, Code, 0, reinterpret_cast<LPWSTR>(&Buffer), 0, nullptr);
        std::wstring Text = Buffer ? Buffer : L"操作失败";
        ::LocalFree(Buffer);
        return Text;
    }

#ifdef NANAZIP_RENAME_TESTING
    int FailMoveAt = -1;
#endif

    static bool MovePath(std::wstring const& Source,
        std::wstring const& Target)
    {
#ifdef NANAZIP_RENAME_TESTING
        if (FailMoveAt >= 0 && FailMoveAt-- == 0)
        {
            ::SetLastError(ERROR_ACCESS_DENIED);
            return false;
        }
#endif
        return !!::MoveFileExW(NativePath(Source).c_str(),
            NativePath(Target).c_str(), 0);
    }

    Result Execute(Plan const& Previewed)
    {
        Result Output;
        Plan Checked = Validate(Previewed.Entries);
        if (!Previewed.Valid || !Checked.Valid)
        {
            Output.Message = L"文件状态已变化或预览无效，请重新检查预览。\r\n" +
                Checked.Error;
            return Output;
        }
        struct Move
        {
            Entry Value;
            std::wstring Temporary;
            std::wstring Current;
        };
        std::vector<Move> Moves;
        GUID Identifier;
        if (FAILED(::CoCreateGuid(&Identifier)))
        {
            Output.Message = L"无法生成安全的临时名称。";
            return Output;
        }
        wchar_t Token[40];
        ::StringFromGUID2(Identifier, Token, ARRAYSIZE(Token));
        for (Entry const& Current : Checked.Entries)
        {
            if (!Current.Changed)
                continue;
            Move Step;
            Step.Value = Current;
            Step.Current = Current.Source.Path;
            Step.Temporary = Parent(Current.Source.Path) +
                L".NanaZip-Rename-" + Token + L"-" +
                std::to_wstring(Moves.size());
            Moves.push_back(Step);
        }
        DWORD Failure = ERROR_SUCCESS;
        std::wstring FailedPath;
        for (Move& Step : Moves)
        {
            BY_HANDLE_FILE_INFORMATION Identity;
            if (!Identify(Step.Current, Identity) ||
                !SameIdentity(Step.Value.Source.Identity, Identity))
            {
                Failure = ERROR_FILE_INVALID;
                FailedPath = Step.Current;
                break;
            }
            if (!MovePath(Step.Current, Step.Temporary))
            {
                Failure = ::GetLastError();
                FailedPath = Step.Current;
                break;
            }
            Step.Current = Step.Temporary;
        }
        if (!Failure)
        {
            for (Move& Step : Moves)
            {
                if (!MovePath(Step.Current, Step.Value.Target))
                {
                    Failure = ::GetLastError();
                    FailedPath = Step.Value.Target;
                    break;
                }
                Step.Current = Step.Value.Target;
            }
        }
        if (Failure)
        {
            // Restore through the staging paths, including completed swaps.
            for (Move& Step : Moves)
            {
                if (Step.Current != Step.Temporary &&
                    Step.Current != Step.Value.Source.Path &&
                    MovePath(Step.Current, Step.Temporary))
                    Step.Current = Step.Temporary;
            }
            std::wstring Recovery;
            for (Move& Step : Moves)
            {
                if (Step.Current != Step.Value.Source.Path)
                {
                    if (MovePath(Step.Current, Step.Value.Source.Path))
                        Step.Current = Step.Value.Source.Path;
                    else
                        Recovery += L"\r\n当前：" + Step.Current +
                            L"\r\n原位置：" + Step.Value.Source.Path + L"\r\n";
                }
            }
            Output.Message = L"重命名失败：" + FailedPath + L"\r\n" +
                ErrorMessage(Failure) + (Recovery.empty() ?
                    L"\r\n已恢复本次已修改的名称。" :
                    L"\r\n以下项目无法自动恢复，请按当前位置检查：\r\n" + Recovery);
        }
        else
        {
            Output.Success = true;
            for (Move const& Step : Moves)
            {
                Entry Reverse;
                Reverse.Source = Step.Value.Source;
                Reverse.Source.Path = Step.Value.Target;
                Reverse.Source.Name = Step.Value.Name;
                Reverse.Name = Step.Value.Source.Name;
                Reverse.Target = Step.Value.Source.Path;
                Reverse.Changed = true;
                Output.Undo.push_back(Reverse);
            }
            Output.Message = L"已重命名 " + std::to_wstring(Moves.size()) + L" 个";
        }
        for (Move const& Step : Moves)
        {
            ::SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW,
                Parent(Step.Value.Source.Path).c_str(), nullptr);
        }
        return Output;
    }

    static bool WriteValue(HKEY Key, wchar_t const* Value,
        std::wstring const& Text)
    {
        return ::RegSetValueExW(Key, Value, 0, REG_SZ,
            reinterpret_cast<BYTE const*>(Text.c_str()),
            static_cast<DWORD>((Text.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }

    static std::wstring ReadValue(HKEY Key, wchar_t const* Value)
    {
        DWORD Bytes = 0;
        if (::RegGetValueW(Key, nullptr, Value, RRF_RT_REG_SZ,
            nullptr, nullptr, &Bytes) != ERROR_SUCCESS || Bytes > 1048576)
            return L"";
        std::vector<wchar_t> Buffer(Bytes / sizeof(wchar_t) + 1);
        if (::RegGetValueW(Key, nullptr, Value, RRF_RT_REG_SZ,
            nullptr, Buffer.data(), &Bytes) != ERROR_SUCCESS)
            return L"";
        return Buffer.data();
    }

    static bool WriteNumber(HKEY Key, wchar_t const* Value, DWORD Number)
    {
        return ::RegSetValueExW(Key, Value, 0, REG_DWORD,
            reinterpret_cast<BYTE const*>(&Number), sizeof(Number)) == ERROR_SUCCESS;
    }

    static DWORD ReadNumber(HKEY Key, wchar_t const* Value, DWORD Default)
    {
        DWORD Number = Default;
        DWORD Bytes = sizeof(Number);
        ::RegGetValueW(Key, nullptr, Value, RRF_RT_REG_DWORD,
            nullptr, &Number, &Bytes);
        return Number;
    }

    static bool SaveRule(HKEY Key, Rule const& Settings)
    {
        return WriteValue(Key, L"Text", Settings.Text) &&
            WriteValue(Key, L"Find", Settings.Find) &&
            WriteNumber(Key, L"Mode", static_cast<DWORD>(Settings.Operation)) &&
            WriteNumber(Key, L"Start", Settings.Start) &&
            WriteNumber(Key, L"Digits", Settings.Digits) &&
            WriteNumber(Key, L"Length", Settings.Length) &&
            WriteNumber(Key, L"Flags", (Settings.FromEnd ? 1 : 0) |
                (Settings.ModifiedDate ? 2 : 0) | (Settings.UpperCase ? 4 : 0) |
                8 | (Settings.SkipPrefix ? 16 : 0));
    }

    static Rule LoadRule(HKEY Key)
    {
        Rule Settings;
        Settings.Operation = static_cast<Mode>((std::min)(
            ReadNumber(Key, L"Mode", 0), static_cast<DWORD>(Mode::List)));
        Settings.Text = ReadValue(Key, L"Text");
        Settings.Find = ReadValue(Key, L"Find");
        Settings.Start = ReadNumber(Key, L"Start", 1);
        Settings.Digits = (std::min)(ReadNumber(Key, L"Digits", 3), 12UL);
        Settings.Length = ReadNumber(Key, L"Length", 1);
        DWORD Flags = ReadNumber(Key, L"Flags", 24);
        Settings.FromEnd = !!(Flags & 1);
        Settings.ModifiedDate = !!(Flags & 2);
        Settings.UpperCase = !!(Flags & 4);
        Settings.SkipPrefix = !!(Flags & 16);
        return Settings;
    }

    bool LoadPresets(std::vector<Preset>& Presets, unsigned& MenuCount)
    {
        Presets.clear();
        MenuCount = 8;
        HKEY Key;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, SettingsKey,
            0, KEY_READ, &Key) != ERROR_SUCCESS)
        {
            wchar_t const* Defaults[] = { L"【已完成】", L"【待审核】", L"项目A_" };
            for (wchar_t const* Text : Defaults)
            {
                Preset Current;
                Current.Label = Text;
                Current.Settings.Text = Text;
                Presets.push_back(Current);
            }
            return true;
        }
        MenuCount = (std::max)(1UL, (std::min)(
            ReadNumber(Key, L"MenuCount", 8), 20UL));
        std::wstring Active = ReadValue(Key, L"Active");
        HKEY Collection;
        if (Active.empty() || ::RegOpenKeyExW(Key, Active.c_str(),
            0, KEY_READ, &Collection) != ERROR_SUCCESS)
        {
            ::RegCloseKey(Key);
            return true;
        }
        DWORD Count = ReadNumber(Collection, L"Count", 0);
        for (DWORD Index = 0; Index < Count; ++Index)
        {
            HKEY Child;
            if (::RegOpenKeyExW(Collection, std::to_wstring(Index).c_str(),
                0, KEY_READ, &Child) != ERROR_SUCCESS)
                break;
            Preset Current;
            Current.Label = ReadValue(Child, L"Label");
            Current.ShowInMenu = !!ReadNumber(Child, L"Show", 1);
            Current.Settings = LoadRule(Child);
            Presets.push_back(Current);
            ::RegCloseKey(Child);
        }
        ::RegCloseKey(Collection);
        ::RegCloseKey(Key);
        return true;
    }

    bool SavePresets(std::vector<Preset> const& Presets, unsigned MenuCount)
    {
        HKEY Key;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, SettingsKey, 0, nullptr,
            0, KEY_ALL_ACCESS, nullptr, &Key, nullptr) != ERROR_SUCCESS)
            return false;
        GUID Identifier;
        if (FAILED(::CoCreateGuid(&Identifier)))
        {
            ::RegCloseKey(Key);
            return false;
        }
        wchar_t Token[40];
        ::StringFromGUID2(Identifier, Token, ARRAYSIZE(Token));
        std::wstring Generation = L"Presets-" + std::wstring(Token);
        HKEY Collection;
        bool Success = ::RegCreateKeyExW(Key, Generation.c_str(), 0, nullptr,
            0, KEY_ALL_ACCESS, nullptr, &Collection, nullptr) == ERROR_SUCCESS;
        if (Success)
        {
            for (size_t Index = 0; Success && Index < Presets.size(); ++Index)
            {
                HKEY Child;
                Success = ::RegCreateKeyExW(Collection,
                    std::to_wstring(Index).c_str(), 0, nullptr, 0,
                    KEY_ALL_ACCESS, nullptr, &Child, nullptr) == ERROR_SUCCESS;
                if (!Success)
                    break;
                Success = WriteValue(Child, L"Label", Presets[Index].Label) &&
                    WriteNumber(Child, L"Show", Presets[Index].ShowInMenu) &&
                    SaveRule(Child, Presets[Index].Settings);
                ::RegCloseKey(Child);
            }
            Success = Success && WriteNumber(Collection, L"Count",
                static_cast<DWORD>(Presets.size()));
            ::RegCloseKey(Collection);
        }
        std::wstring Previous = ReadValue(Key, L"Active");
        Success = Success && WriteNumber(Key, L"MenuCount", MenuCount) &&
            WriteValue(Key, L"Active", Generation);
        if (Success && Previous.compare(0, 8, L"Presets-") == 0)
            ::RegDeleteTreeW(Key, Previous.c_str());
        if (!Success)
            ::RegDeleteTreeW(Key, Generation.c_str());
        ::RegCloseKey(Key);
        return Success;
    }

    bool SaveUndo(std::vector<Entry> const& Entries)
    {
        HKEY Key;
        std::wstring Path = std::wstring(SettingsKey) + L"\\Undo";
        ::RegDeleteTreeW(HKEY_CURRENT_USER, Path.c_str());
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, Path.c_str(), 0, nullptr,
            0, KEY_ALL_ACCESS, nullptr, &Key, nullptr) != ERROR_SUCCESS)
            return false;
        bool Success = true;
        for (size_t Index = 0; Success && Index < Entries.size(); ++Index)
        {
            HKEY Child;
            Success = ::RegCreateKeyExW(Key, std::to_wstring(Index).c_str(),
                0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &Child, nullptr) == ERROR_SUCCESS;
            if (!Success)
                break;
            Entry const& Current = Entries[Index];
            Success = WriteValue(Child, L"Source", Current.Source.Path) &&
                WriteValue(Child, L"Name", Current.Name) &&
                ::RegSetValueExW(Child, L"Identity", 0, REG_BINARY,
                    reinterpret_cast<BYTE const*>(&Current.Source.Identity),
                    sizeof(Current.Source.Identity)) == ERROR_SUCCESS;
            ::RegCloseKey(Child);
        }
        Success = Success && WriteNumber(Key, L"Count",
            static_cast<DWORD>(Entries.size()));
        ::RegCloseKey(Key);
        return Success;
    }

    bool LoadUndo(std::vector<Entry>& Entries)
    {
        Entries.clear();
        HKEY Key;
        std::wstring Path = std::wstring(SettingsKey) + L"\\Undo";
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, Path.c_str(), 0,
            KEY_READ, &Key) != ERROR_SUCCESS)
            return false;
        DWORD Count = ReadNumber(Key, L"Count", 0);
        bool Success = Count > 0;
        for (DWORD Index = 0; Success && Index < Count; ++Index)
        {
            HKEY Child;
            Success = ::RegOpenKeyExW(Key, std::to_wstring(Index).c_str(),
                0, KEY_READ, &Child) == ERROR_SUCCESS;
            if (!Success)
                break;
            Entry Current;
            Current.Source.Path = ReadValue(Child, L"Source");
            Current.Source.Name = Name(Current.Source.Path);
            Current.Name = ReadValue(Child, L"Name");
            DWORD Bytes = sizeof(Current.Source.Identity);
            Current.Source.Identified = ::RegGetValueW(Child, nullptr,
                L"Identity", RRF_RT_REG_BINARY, nullptr,
                &Current.Source.Identity, &Bytes) == ERROR_SUCCESS &&
                Bytes == sizeof(Current.Source.Identity);
            Current.Source.Directory = !!(Current.Source.Identity.dwFileAttributes &
                FILE_ATTRIBUTE_DIRECTORY);
            Success = Current.Source.Identified && !Current.Source.Path.empty();
            Entries.push_back(Current);
            ::RegCloseKey(Child);
        }
        ::RegCloseKey(Key);
        if (!Success)
            Entries.clear();
        return Success;
    }
}
