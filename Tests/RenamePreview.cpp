/*
 * PROJECT:    NanaZip
 * PURPOSE:    Standalone host for manual rename dialog verification.
 * LICENSE:    The MIT License
 */
#include "../NanaZip.UI.Shared/Rename.h"
#include <ShlObj.h>

int WINAPI wWinMain(HINSTANCE Instance, HINSTANCE Previous,
    LPWSTR CommandLine, int ShowCommand)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Previous);
    UNREFERENCED_PARAMETER(CommandLine);
    UNREFERENCED_PARAMETER(ShowCommand);
    int Count = 0;
    wchar_t** Arguments = ::CommandLineToArgvW(::GetCommandLineW(), &Count);
    std::vector<std::wstring> Paths;
    bool List = false;
    bool Manage = false;
    for (int Index = 1; Arguments && Index < Count; ++Index)
    {
        if (wcscmp(Arguments[Index], L"--list") == 0)
            List = true;
        else if (wcscmp(Arguments[Index], L"--manage") == 0)
            Manage = true;
        else
            Paths.push_back(Arguments[Index]);
    }
    ::LocalFree(Arguments);
    HKEY Isolated = nullptr;
    std::wstring IsolationPath;
    if (Count <= 1)
    {
        wchar_t Module[32768];
        ::GetModuleFileNameW(nullptr, Module, ARRAYSIZE(Module));
        std::wstring Root = NanaZip::Renaming::Parent(Module) + L"UiSamples\\";
        Paths.push_back(Root + L"文档1.docx");
        Paths.push_back(Root + L"表格1.xlsx");
        Paths.push_back(Root + L"新建文件夹");
#ifdef NANAZIP_PREVIEW_SINGLE
        Paths.clear();
        Paths.push_back(NanaZip::Renaming::Parent(Module) + L"KeyboardSamples\\Before.txt");
#endif
        List = true;
        GUID Identifier;
        ::CoCreateGuid(&Identifier);
        wchar_t Token[40];
        ::StringFromGUID2(Identifier, Token, ARRAYSIZE(Token));
        IsolationPath = L"Software\\NanaZip\\RenameUiVerification-" +
            std::wstring(Token);
        if (::RegCreateKeyExW(HKEY_CURRENT_USER,
            IsolationPath.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &Isolated,
            nullptr) == ERROR_SUCCESS)
            ::RegOverridePredefKey(HKEY_CURRENT_USER, Isolated);
    }
    NanaZip::Renaming::Preset Initial;
    Initial.Settings.Operation = NanaZip::Renaming::Mode::List;
    NanaZip::Renaming::Show(Paths, nullptr, List ? &Initial : nullptr, Manage);
    if (Isolated)
    {
        ::RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        ::RegCloseKey(Isolated);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, IsolationPath.c_str());
    }
    return 0;
}
