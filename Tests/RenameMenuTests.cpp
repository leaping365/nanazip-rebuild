/*
 * PROJECT:    NanaZip
 * PURPOSE:    Verify the built Explorer command tree without registration.
 * LICENSE:    The MIT License
 */
#include <Windows.h>
#include <ShlObj.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdio>

using Microsoft::WRL::ComPtr;

int wmain(int Count, wchar_t** Arguments)
{
    if (Count < 3)
        return 2;
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE Module = ::LoadLibraryExW(Arguments[1], nullptr,
        LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!Module)
    {
        std::printf("FAIL: load extension %lu\n", ::GetLastError());
        return 1;
    }
    using GetFactory = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto FactoryFunction = reinterpret_cast<GetFactory>(
        ::GetProcAddress(Module, "DllGetClassObject"));
    CLSID Identifier;
    ::CLSIDFromString(L"{469D94E9-6AF4-4395-B396-99B1308F8CE5}", &Identifier);
    ComPtr<IClassFactory> Factory;
    if (!FactoryFunction || FAILED(FactoryFunction(Identifier,
        IID_PPV_ARGS(Factory.GetAddressOf()))))
        return 1;
    ComPtr<IExplorerCommand> Root;
    if (FAILED(Factory->CreateInstance(nullptr, IID_PPV_ARGS(Root.GetAddressOf()))))
        return 1;
    std::vector<PIDLIST_ABSOLUTE> Ids;
    for (int Index = 2; Index < Count; ++Index)
    {
        PIDLIST_ABSOLUTE Value = nullptr;
        if (FAILED(::SHParseDisplayName(Arguments[Index], nullptr, &Value, 0, nullptr)))
            return 1;
        Ids.push_back(Value);
    }
    ComPtr<IShellItemArray> Items;
    std::vector<PCIDLIST_ABSOLUTE> Fixed(Ids.begin(), Ids.end());
    HRESULT Status = ::SHCreateShellItemArrayFromIDLists(
        static_cast<UINT>(Ids.size()),
        Fixed.data(), Items.GetAddressOf());
    for (auto Id : Ids)
        ::CoTaskMemFree(Id);
    if (FAILED(Status))
        return 1;
    LPWSTR RootTitle = nullptr;
    if (FAILED(Root->GetTitle(Items.Get(), &RootTitle)))
        return 1;
    ::CoTaskMemFree(RootTitle);
    ComPtr<IEnumExplorerCommand> Commands;
    if (FAILED(Root->EnumSubCommands(Commands.GetAddressOf())))
        return 1;
    unsigned Position = 0;
    bool FoundRename = false;
    bool FoundList = false;
    bool FoundSeparator = false;
    bool FoundTopSeparator = false;
    bool FoundCopy = false;
    bool FoundCompress = false;
    bool Files = false;
    bool Folders = false;
    for (int Index = 2; Index < Count; ++Index)
    {
        DWORD Attributes = ::GetFileAttributesW(Arguments[Index]);
        if (Attributes != INVALID_FILE_ATTRIBUTES &&
            (Attributes & FILE_ATTRIBUTE_DIRECTORY))
            Folders = true;
        else
            Files = true;
    }
    std::wstring Expected = L"重命名" + std::wstring(Files && Folders ?
        L"项目" : Folders ? L"文件夹" : L"文件");
    for (;;)
    {
        ComPtr<IExplorerCommand> Current;
        ULONG Fetched = 0;
        if (Commands->Next(1, Current.GetAddressOf(), &Fetched) != S_OK)
            break;
        LPWSTR Title = nullptr;
        Current->GetTitle(Items.Get(), &Title);
        std::wstring Label = Title ? Title : L"";
        ::CoTaskMemFree(Title);
        if (Label == L"用 NanaZip 打开" || Label == L"Open with NanaZip")
            return 1;
        std::wprintf(L"%u: %s\n", Position + 1, Label.c_str());
        if (Position == 2)
        {
            EXPCMDFLAGS Flags = ECF_DEFAULT;
            if (FAILED(Current->GetFlags(&Flags)) || !(Flags & ECF_ISSEPARATOR))
                return 1;
            FoundTopSeparator = true;
        }
        if (Position == 4)
        {
            if (Label != L"按名单重命名…")
                return 1;
            FoundList = true;
        }
        if (Position == 5)
        {
            EXPCMDFLAGS Flags = ECF_DEFAULT;
            if (FAILED(Current->GetFlags(&Flags)) || !(Flags & ECF_ISSEPARATOR))
                return 1;
            FoundSeparator = true;
        }
        if (Label.find(L"复制") != std::wstring::npos)
            FoundCopy = true;
        if (Label.find(L"压缩") != std::wstring::npos)
            FoundCompress = true;
        if (Label.find(L"重命名") == 0)
        {
            if (Position != 3 || Label != Expected)
                return 1;
            FoundRename = true;
            ComPtr<IEnumExplorerCommand> Children;
            if (FAILED(Current->EnumSubCommands(Children.GetAddressOf())))
                return 1;
            unsigned ChildCount = 0;
            for (;;)
            {
                ComPtr<IExplorerCommand> Child;
                if (Children->Next(1, Child.GetAddressOf(), &Fetched) != S_OK)
                    break;
                LPWSTR ChildTitle = nullptr;
                Child->GetTitle(Items.Get(), &ChildTitle);
                std::wstring ChildLabel = ChildTitle ? ChildTitle : L"";
                ::CoTaskMemFree(ChildTitle);
                if ((ChildCount == 0 && ChildLabel != L"按规则重命名…") ||
                    ChildLabel == L"按名单重命名…")
                    return 1;
                ++ChildCount;
            }
            if (ChildCount < 5)
                return 1;
            ComPtr<IEnumExplorerCommand> Cloned;
            if (FAILED(Children->Reset()) || FAILED(Children->Clone(Cloned.GetAddressOf())))
                return 1;
            if (Cloned->Skip(1) != S_OK)
                return 1;
        }
        ++Position;
    }
    bool Passed = FoundRename && FoundList && FoundTopSeparator && FoundSeparator && FoundCopy && FoundCompress;
    std::printf(Passed ?
        "PASS: rename group separators, submenu enumerable, existing commands retained\n" :
        "FAIL: command tree\n");
    return Passed ? 0 : 1;
}
