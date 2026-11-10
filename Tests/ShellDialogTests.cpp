// Verify real shell commands open foreground dialogs, then cancel without edits.
#include <Windows.h>
#include <ShlObj.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#include <string>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;

static ComPtr<IExplorerCommand> Find(IExplorerCommand* Root,
    IShellItemArray* Items, const wchar_t* Label)
{
    LPWSTR Title = nullptr;
    Root->GetTitle(Items, &Title);
    bool Match = Title && std::wstring(Title) == Label;
    ::CoTaskMemFree(Title);
    if (Match) return Root;
    ComPtr<IEnumExplorerCommand> Children;
    if (SUCCEEDED(Root->EnumSubCommands(&Children)))
    {
        ComPtr<IExplorerCommand> Child;
        while (Children->Next(1, &Child, nullptr) == S_OK)
        {
            auto Found = Find(Child.Get(), Items, Label);
            if (Found) return Found;
            Child.Reset();
        }
    }
    return {};
}

int wmain(int Count, wchar_t** Args)
{
    if (Count < 6) return 2; // DLL, CLSID, label, expected window class, paths...
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE Module = ::LoadLibraryExW(Args[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!Module) return 1;
    using FactoryFn = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto Create = reinterpret_cast<FactoryFn>(::GetProcAddress(Module, "DllGetClassObject"));
    CLSID Id;
    ::CLSIDFromString(Args[2], &Id);
    ComPtr<IClassFactory> Factory;
    if (!Create || FAILED(Create(Id, IID_PPV_ARGS(&Factory)))) return 1;
    ComPtr<IExplorerCommand> Root;
    if (FAILED(Factory->CreateInstance(nullptr, IID_PPV_ARGS(&Root)))) return 1;
    std::vector<PIDLIST_ABSOLUTE> Pids;
    for (int Index = 5; Index < Count; ++Index)
    {
        PIDLIST_ABSOLUTE Pid = nullptr;
        if (FAILED(::SHParseDisplayName(Args[Index], nullptr, &Pid, 0, nullptr))) return 1;
        Pids.push_back(Pid);
    }
    std::vector<PCIDLIST_ABSOLUTE> Fixed(Pids.begin(), Pids.end());
    ComPtr<IShellItemArray> Items;
    HRESULT Result = ::SHCreateShellItemArrayFromIDLists(static_cast<UINT>(Fixed.size()),
        Fixed.data(), &Items);
    for (auto Pid : Pids) ::CoTaskMemFree(Pid);
    if (FAILED(Result)) return 1;
    LPWSTR Title = nullptr;
    Root->GetTitle(Items.Get(), &Title);
    ::CoTaskMemFree(Title);
    auto Command = Find(Root.Get(), Items.Get(), Args[3]);
    if (!Command) { std::puts("FAIL command missing"); return 1; }
    bool Passed = true;
    // Repeat on the same thread: closing a dialog must not leave WM_QUIT behind.
    for (int Repeat = 0; Repeat < 3; ++Repeat)
    {
        HWND Owner = ::CreateWindowExW(0, L"STATIC", L"Shell dialog test owner",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 500, 300,
            nullptr, nullptr, nullptr, nullptr);
        DWORD ForegroundThread = ::GetWindowThreadProcessId(::GetForegroundWindow(), nullptr);
        DWORD CurrentThread = ::GetCurrentThreadId();
        bool Attached = ForegroundThread != CurrentThread &&
            !!::AttachThreadInput(CurrentThread, ForegroundThread, TRUE);
        ::SetForegroundWindow(Owner);
        if (Attached) ::AttachThreadInput(CurrentThread, ForegroundThread, FALSE);
        // Foreground transfer can be asynchronous across input queues.
        for (int Attempt = 0; Attempt < 40; ++Attempt)
        {
            MSG Pending;
            while (::PeekMessageW(&Pending, nullptr, 0, 0, PM_REMOVE))
            {
                ::TranslateMessage(&Pending);
                ::DispatchMessageW(&Pending);
            }
            if (::GetForegroundWindow() == Owner) break;
            ::Sleep(25);
        }
        if (::GetForegroundWindow() != Owner)
        {
            ::DestroyWindow(Owner);
            std::puts("FAIL unable to establish foreground test owner");
            return 1;
        }
        std::atomic<bool> Finished{false};
        std::atomic<bool> Activated{false};
        std::thread Observer([&]()
        {
            HWND Dialog = nullptr;
            for (int Attempt = 0; Attempt < 100 && !Finished; ++Attempt)
            {
                HWND Candidate = ::FindWindowW(Args[4], nullptr);
                DWORD Process = 0;
                if (Candidate) ::GetWindowThreadProcessId(Candidate, &Process);
                if (Candidate && Process == ::GetCurrentProcessId())
                {
                    Dialog = Candidate;
                    if (::GetForegroundWindow() == Dialog)
                    {
                        Activated = ::GetWindow(Dialog, GW_OWNER) == Owner &&
                            !::IsWindowEnabled(Owner);
                        break;
                    }
                }
                ::Sleep(50);
            }
            if (Dialog) ::PostMessageW(Dialog, WM_CLOSE, 0, 0);
        });
        HRESULT Invoked = Command->Invoke(Items.Get(), nullptr);
        Finished = true;
        Observer.join();
        MSG Message;
        bool Quit = !!::PeekMessageW(&Message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
        std::printf("repeat=%d invoke=%lx activated=%d quit=%d ownerEnabled=%d\n",
            Repeat, static_cast<unsigned long>(Invoked), static_cast<int>(Activated.load()),
            Quit, !!::IsWindowEnabled(Owner));
        Passed = Passed && SUCCEEDED(Invoked) && Activated && !Quit &&
            !!::IsWindowEnabled(Owner);
        ::DestroyWindow(Owner);
    }
    std::puts(Passed ? "PASS foreground, owner, owner restored, repeated cancel" :
        "FAIL dialog activation or lifecycle");
    return Passed ? 0 : 1;
}
