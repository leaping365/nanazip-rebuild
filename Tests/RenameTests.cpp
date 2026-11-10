/*
 * PROJECT:    NanaZip
 * PURPOSE:    Filesystem integration tests for preview-first renaming.
 * LICENSE:    The MIT License
 */
#include "../NanaZip.UI.Shared/Rename.h"
#include <cstdio>
#include <stdexcept>

namespace Rename = NanaZip::Renaming;

static unsigned Checks = 0;

static void Require(bool Condition, char const* Label)
{
    ++Checks;
    if (!Condition)
        throw std::runtime_error(Label);
}

static void Write(std::wstring const& Path, char Value)
{
    HANDLE File = ::CreateFileW(Rename::NativePath(Path).c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(File != INVALID_HANDLE_VALUE, "create fixture");
    DWORD Written = 0;
    bool Success = !!::WriteFile(File, &Value, 1, &Written, nullptr);
    ::CloseHandle(File);
    Require(Success && Written == 1, "write fixture");
}

static char Read(std::wstring const& Path)
{
    HANDLE File = ::CreateFileW(Rename::NativePath(Path).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(File != INVALID_HANDLE_VALUE, "open result");
    char Value = 0;
    DWORD ReadCount = 0;
    ::ReadFile(File, &Value, 1, &ReadCount, nullptr);
    ::CloseHandle(File);
    return Value;
}

static void SettingsTests(std::vector<Rename::Entry> const& Undo)
{
    GUID Identifier;
    Require(SUCCEEDED(::CoCreateGuid(&Identifier)), "settings test identity");
    wchar_t Token[40];
    ::StringFromGUID2(Identifier, Token, ARRAYSIZE(Token));
    std::wstring Path = L"Software\\NanaZip\\RenameVerification-" + std::wstring(Token);
    HKEY Isolated;
    Require(::RegCreateKeyExW(HKEY_CURRENT_USER, Path.c_str(), 0, nullptr,
        0, KEY_ALL_ACCESS, nullptr, &Isolated, nullptr) == ERROR_SUCCESS,
        "isolated registry fixture");
    Require(::RegOverridePredefKey(HKEY_CURRENT_USER, Isolated) == ERROR_SUCCESS,
        "isolate settings from user data");
    bool Passed = false;
    try
    {
        std::vector<Rename::Preset> Presets;
        unsigned Limit;
        Require(Rename::LoadPresets(Presets, Limit) && Presets.size() == 3,
            "initial default presets");
        for (unsigned Index = 0; Index < 1000; ++Index)
        {
            Rename::Preset Current;
            Current.Label = L"常用项" + std::to_wstring(Index);
            Current.Settings.Operation = Rename::Mode::ChangePrefix;
            Current.Settings.Text = L"【已完成】";
            Current.Settings.Find = L"【待审核】";
            Current.Settings.Start = Index;
            Current.Settings.SkipPrefix = Index % 2 == 0;
            Current.ShowInMenu = Index % 3 == 0;
            Presets.push_back(Current);
        }
        Require(Rename::SavePresets(Presets, 12), "save over a thousand presets");
        Presets.clear();
        Require(Rename::LoadPresets(Presets, Limit) &&
            Presets.size() == 1003 && Limit == 12, "all presets persist in order");
        Require(Presets.back().Settings.Find == L"【待审核】" &&
            Presets.back().Settings.Start == 999 &&
            !Presets.back().Settings.SkipPrefix && Presets.back().ShowInMenu,
            "complete rule settings persist");
        std::swap(Presets.front(), Presets.back());
        Presets.erase(Presets.begin() + 1);
        Require(Rename::SavePresets(Presets, 5), "replace preset generation");
        Presets.clear();
        Require(Rename::LoadPresets(Presets, Limit) && Presets.size() == 1002 &&
            Presets.front().Label == L"常用项999" && Limit == 5,
            "reordering and deletion persist");
        Require(Rename::SaveUndo(Undo), "save undo history");
        std::vector<Rename::Entry> Loaded;
        Require(Rename::LoadUndo(Loaded) && Loaded.size() == Undo.size() &&
            Loaded[0].Source.Path == Undo[0].Source.Path &&
            Rename::Validate(Loaded).Valid, "persistent undo revalidates identities");
        Require(Rename::SaveUndo({}) && !Rename::LoadUndo(Loaded), "clear undo");
        Passed = true;
    }
    catch (...)
    {
        ::RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        ::RegCloseKey(Isolated);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, Path.c_str());
        throw;
    }
    ::RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    ::RegCloseKey(Isolated);
    Require(::RegDeleteTreeW(HKEY_CURRENT_USER, Path.c_str()) == ERROR_SUCCESS,
        "remove isolated registry fixture");
    Require(Passed, "settings tests completed");
}

static int Run(std::wstring const& Root)
{
    Require(!!::CreateDirectoryW(Root.c_str(), nullptr), "isolated test directory");
    std::wstring A = Root + L"\\A.txt";
    std::wstring B = Root + L"\\B.txt";
    std::wstring Folder = Root + L"\\Folder.with.dot";
    Write(A, 'A');
    Write(B, 'B');
    Require(!!::CreateDirectoryW(Folder.c_str(), nullptr), "folder fixture");
    auto Items = Rename::ReadItems({ A, B, Folder });
    Require(Rename::SelectionNoun({ A }) == L"文件", "file noun");
    Require(Rename::SelectionNoun({ Folder }) == L"文件夹", "folder noun");
    Require(Rename::SelectionNoun({ A, Folder }) == L"项目", "mixed noun");
    Rename::Rule Rule;
    Rule.Text = L"【完成】";
    auto Plan = Rename::Preview(Items, Rule, {});
    Require(Plan.Valid && Plan.Entries[0].Name == L"【完成】A.txt" &&
        Plan.Entries[2].Name == L"【完成】Folder.with.dot", "prefix and folder dots");
    Require(Read(A) == 'A', "preview never mutates");
    Rule.Operation = Rename::Mode::Suffix;
    Require(Rename::Preview(Items, Rule, {}).Entries[0].Name ==
        L"A【完成】.txt", "suffix preserves extension");
    Rule.Operation = Rename::Mode::Replace;
    Rule.Find = L"A";
    Rule.Text = L"New";
    Require(Rename::Preview(Items, Rule, {}).Entries[0].Name == L"New.txt",
        "replace");
    Rule.Operation = Rename::Mode::ChangePrefix;
    Rule.Find = L"A";
    Require(Rename::Preview(Items, Rule, {}).Entries[1].Name == L"B.txt",
        "replace prefix only when matched");
    Rule.Operation = Rename::Mode::Remove;
    Rule.Find = L"Folder.";
    Require(Rename::Preview(Items, Rule, {}).Entries[2].Name == L"with.dot",
        "remove text");
    Rule.Operation = Rename::Mode::Number;
    Rule.Text = L"资料";
    Require(Rename::Preview(Items, Rule, {}).Entries[1].Name == L"资料_002.txt",
        "numbering follows preview order");
    Rule.Operation = Rename::Mode::Trim;
    Rule.Length = 7;
    Require(Rename::Preview(Items, Rule, {}).Entries[2].Name == L"with.dot",
        "trim characters");
    Rule.Operation = Rename::Mode::Date;
    Require(Rename::Preview(Items, Rule, {}).Entries[0].Name.size() == 16,
        "date prefix");
    Rule.Operation = Rename::Mode::LetterCase;
    Rule.UpperCase = false;
    Require(Rename::Preview(Items, Rule, {}).Entries[0].Name == L"a.txt",
        "case only preview");
    auto Lines = Rename::SplitLines(L"\uFEFF甲\r\n\r\n乙\n\n");
    Require(Lines.size() == 3 && Lines[1].empty(), "line endings and blank rows");
    std::wstring Decoded;
    Require(Rename::DecodeText({ 0xEF, 0xBB, 0xBF, 0xE5, 0x90, 0x8D }, Decoded) &&
        Decoded == L"名", "UTF-8 BOM");
    Require(Rename::DecodeText({ 0xFF, 0xFE, 0x0D, 0x54 }, Decoded) &&
        Decoded == L"名", "UTF-16 LE");
    Require(Rename::DecodeText({ 0xFE, 0xFF, 0x54, 0x0D }, Decoded) &&
        Decoded == L"名", "UTF-16 BE");
    Require(Rename::DecodeText({ 0xC3, 0xFB }, Decoded) && Decoded == L"名",
        "GB18030 input");
    Require(!Rename::DecodeText({ 0xFF, 0xFE, 0x01 }, Decoded), "reject odd UTF-16");
    Require(!Rename::DecodeText({ 0xFF, 0xFE, 0x00, 0xD8 }, Decoded),
        "reject unpaired surrogate");
    Rule.Operation = Rename::Mode::List;
    Require(!Rename::Preview(Items, Rule, { L"one" }).Valid, "list count mismatch");
    Require(!Rename::Preview(Items, Rule, { L"one", L"", L"three" }).Valid,
        "middle empty name");
    Require(Rename::Preview(Items, Rule, { L"one", L"", L"three" }).Entries[1].Error ==
        L"名称为空", "empty name reports the specific input error");
    Require(!Rename::Preview(Items, Rule, { L"same", L"SAME", L"three" }).Valid,
        "case insensitive duplicate");
    Require(!Rename::Preview(Items, Rule, { L"bad/name", L"two", L"three" }).Valid,
        "illegal path characters");
    Require(!Rename::Preview(Items, Rule, { L"CON", L"two", L"three" }).Valid,
        "reserved name");
    Require(!Rename::Preview(Items, Rule, { L"COM¹", L"two", L"three" }).Valid,
        "superscript reserved device");
    Require(!Rename::Preview(Items, Rule, { L"one", L"two", L"trailing." }).Valid,
        "trailing period");
    Write(Root + L"\\occupied.txt", 'X');
    Require(!Rename::Preview(Items, Rule,
        { L"occupied", L"two", L"three" }).Valid, "existing target blocked");
    auto Swap = Rename::Preview(Rename::ReadItems({ A, B }), Rule, { L"B", L"A" });
    Require(Swap.Valid, "swap preview accepted");
    auto Result = Rename::Execute(Swap);
    Require(Result.Success && Read(A) == 'B' && Read(B) == 'A', "swap preserves contents");
    SettingsTests(Result.Undo);
    auto Undo = Rename::Validate(Result.Undo);
    Require(Undo.Valid && Rename::Execute(Undo).Success && Read(A) == 'A',
        "undo swap through preview");
    for (int Failure : { 0, 1, 2, 3 })
    {
        Rename::FailMoveAt = Failure;
        Result = Rename::Execute(Rename::Preview(
            Rename::ReadItems({ A, B }), Rule, { L"B", L"A" }));
        Require(!Result.Success && Read(A) == 'A' && Read(B) == 'B',
            "rollback for staging and final move failures");
    }
    Rename::FailMoveAt = -1;
    auto Case = Rename::Preview(Rename::ReadItems({ A }), Rule, { L"a" });
    Result = Rename::Execute(Case);
    Require(Result.Success && Rename::Name(Result.Undo[0].Source.Path) == L"a.txt",
        "case only execute");
    Require(Rename::Execute(Rename::Validate(Result.Undo)).Success, "case undo");
    Plan = Rename::Preview(Rename::ReadItems({ A }), Rule, { L"New" });
    Write(Root + L"\\New.txt", 'N');
    Require(!Rename::Execute(Plan).Success && Read(A) == 'A' &&
        Read(Root + L"\\New.txt") == 'N', "revalidate after preview without overwrite");
    Plan = Rename::Preview(Rename::ReadItems({ A }), Rule, { L"Newer" });
    Require(!!::MoveFileW(A.c_str(), (Root + L"\\saved.txt").c_str()), "replace fixture source");
    Write(A, 'Z');
    Require(!Rename::Execute(Plan).Success && Read(A) == 'Z', "source identity change");
    Require(!!::DeleteFileW(A.c_str()), "cleanup replacement");
    Require(!!::MoveFileW((Root + L"\\saved.txt").c_str(), A.c_str()), "restore fixture");
    Write(Folder + L"\\inside.txt", 'I');
    auto Nested = Rename::Preview(Rename::ReadItems({ Folder, Folder + L"\\inside.txt" }),
        Rule, { L"Renamed", L"inside2" });
    Require(!Nested.Valid, "parent and child selected together");
    Plan = Rename::Preview(Rename::ReadItems({ Folder }), Rule, { L"新文件夹" });
    Result = Rename::Execute(Plan);
    Require(Result.Success && Read(Root + L"\\新文件夹\\inside.txt") == 'I',
        "folder rename preserves contents");
    Require(Rename::Execute(Rename::Validate(Result.Undo)).Success, "folder undo");
    Require(!!::DeleteFileW((Folder + L"\\inside.txt").c_str()), "cleanup child");
    Require(!!::RemoveDirectoryW(Folder.c_str()), "cleanup folder");
    for (std::wstring const& Path : { A, B, Root + L"\\occupied.txt", Root + L"\\New.txt" })
        Require(!!::DeleteFileW(Path.c_str()), "cleanup test files");
    Require(!!::RemoveDirectoryW(Root.c_str()), "no staging files remain");
    return 0;
}

int wmain(int Count, wchar_t** Arguments)
{
    if (Count != 2)
        return 2;
    try
    {
        int Result = Run(Arguments[1]);
        std::printf("PASS: %u checks\n", Checks);
        return Result;
    }
    catch (std::exception const& Error)
    {
        std::printf("FAIL after %u checks: %s\n", Checks, Error.what());
        return 1;
    }
}
