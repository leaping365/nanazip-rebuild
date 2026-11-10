/*
 * PROJECT:    NanaZip
 * FILE:       NanaZip.ShellExtension.cpp
 * PURPOSE:    Implementation for NanaZip Shell Extension
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: MouriNaruto (Kenji.Mouri@outlook.com)
 */

#include <Windows.h>

#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#include <shobjidl_core.h>
#include <shlobj.h>
#include <shlguid.h>

#include <winrt/Windows.Foundation.h>
#include "../NanaZip.UI.Shared/Rename.h"
#include "../NanaZip.UI.Shared/DialogAppearance.h"

#include "../SevenZip/CPP/Common/Common.h"
#include "../SevenZip/CPP/Windows/DLL.h"
#include "../SevenZip/CPP/Windows/FileDir.h"
#include "../SevenZip/CPP/Windows/FileFind.h"
#include "../SevenZip/CPP/Windows/FileName.h"
#include "../SevenZip/CPP/Windows/ProcessUtils.h"
#include "../SevenZip/CPP/7zip/UI/Common/ArchiveName.h"
#include "../SevenZip/CPP/7zip/UI/Common/CompressCall.h"
#include "../SevenZip/CPP/7zip/UI/Common/ExtractingFilePath.h"
#include "../SevenZip/CPP/7zip/UI/Common/ZipRegistry.h"
#include "../SevenZip/CPP/7zip/UI/FileManager/FormatUtils.h"
#include "../SevenZip/CPP/7zip/UI/FileManager/LangUtils.h"
#include "../SevenZip/CPP/7zip/UI/Explorer/ContextMenuFlags.h"
#include "../SevenZip/CPP/7zip/UI/Explorer/resource.h"

namespace
{
    static const char* const kExtractExcludeExtensions =
        " 3gp"
        " aac ans ape asc asm asp aspx avi awk"
        " bas bat bmp"
        " c cs cls clw cmd cpp csproj css ctl cxx"
        " def dep dlg dsp dsw"
        " eps"
        " f f77 f90 f95 fla flac frm"
        " gif"
        " h hpp hta htm html hxx"
        " ico idl inc ini inl"
        " java jpeg jpg js"
        " la lnk log"
        " mak manifest wmv mov mp3 mp4 mpe mpeg mpg m4a"
        " ofr ogg"
        " pac pas pdf php php3 php4 php5 phptml pl pm png ps py pyo"
        " ra rb rc reg rka rm rtf"
        " sed sh shn shtml sln sql srt swa"
        " tcl tex tiff tta txt"
        " vb vcproj vbs"
        " wav webp wma wv"
        " xml xsd xsl xslt"
        " ";

    static bool FindExt(const char* p, const FString& name)
    {
        int dotPos = name.ReverseFind_Dot();
        if (dotPos < 0 || dotPos == (int)name.Len() - 1)
            return false;

        AString s;

        for (unsigned pos = dotPos + 1;; pos++)
        {
            wchar_t c = name[pos];
            if (c == 0)
                break;
            if (c >= 0x80)
                return false;
            s += (char)MyCharLower_Ascii((char)c);
        }

        for (unsigned i = 0; p[i] != 0;)
        {
            unsigned j;
            for (j = i; p[j] != ' '; j++);
            if (s.Len() == j - i && memcmp(p + i, (const char*)s, s.Len()) == 0)
                return true;
            i = j + 1;
        }

        return false;
    }

    static bool DoNeedExtract(const FString& name)
    {
        return !FindExt(kExtractExcludeExtensions, name);
    }

    // Real archive types. Installer executables (.exe/.msi) stay compressible
    // even when 7-Zip can open them as self-extracting archives.
    static const char* const kRecognizedArchiveExtensions =
        " 7z zip zipx rar r00 cab iso tar tgz tbz tbz2 txz"
        " gz bz2 xz zst lz liz lz4 lz5 lzma lzh lha arj z cpio wim swm xar"
        " ";

    static bool IsRecognizedArchiveName(const FString& name)
    {
        if (FindExt(kRecognizedArchiveExtensions, name))
            return true;

        int dotPos = name.ReverseFind_Dot();
        if (dotPos <= 0)
            return false;

        AString ext;
        for (unsigned pos = (unsigned)dotPos + 1; name[pos] != 0; pos++)
        {
            wchar_t c = name[pos];
            if (c >= 0x80)
                return false;
            ext += (char)MyCharLower_Ascii((char)c);
        }
        if (ext.IsEqualTo_Ascii_NoCase("001"))
            return IsRecognizedArchiveName(name.Left(dotPos));
        return false;
    }

    static const char* const kArcExts[] =
    {
        "7z"
      , "bz2"
      , "gz"
      , "lz"
      , "liz"
      , "lz4"
      , "lz5"
      , "rar"
      , "zip"
      , "zst"
    };

    static bool IsItArcExt(const UString& ext)
    {
        for (unsigned i = 0; i < ARRAY_SIZE(kArcExts); i++)
            if (ext.IsEqualTo_Ascii_NoCase(kArcExts[i]))
                return true;
        return false;
    }

    UString GetSubFolderNameForExtract(const UString& arcName)
    {
        int dotPos = arcName.ReverseFind_Dot();
        if (dotPos < 0)
            return Get_Correct_FsFile_Name(arcName) + L'~';

        const UString ext = arcName.Ptr(dotPos + 1);
        UString res = arcName.Left(dotPos);
        res.TrimRight();
        dotPos = res.ReverseFind_Dot();
        if (dotPos > 0)
        {
            const UString ext2 = res.Ptr(dotPos + 1);
            if ((ext.IsEqualTo_Ascii_NoCase("001") && IsItArcExt(ext2))
                || (ext.IsEqualTo_Ascii_NoCase("rar") &&
                    (ext2.IsEqualTo_Ascii_NoCase("part001")
                        || ext2.IsEqualTo_Ascii_NoCase("part01")
                        || ext2.IsEqualTo_Ascii_NoCase("part1"))))
                res.DeleteFrom(dotPos);
            res.TrimRight();
        }
        return Get_Correct_FsFile_Name(res);
    }

    static void ReduceString(UString& s)
    {
        const unsigned kMaxSize = 60;
        if (s.Len() <= kMaxSize)
            return;
        s.Delete(kMaxSize / 2, s.Len() - kMaxSize);
        s.Insert(kMaxSize / 2, L" ... ");
    }

    // Backport GetQuotedReducedString from 7-Zip ZS since GetQuotedString
    // now does string escaping as well.
    static UString GetQuotedReducedString(const UString& s)
    {
        UString s2 = s;
        ReduceString(s2);
        s2.Replace(L"&", L"&&");
        s2.InsertAtFront(L'"');
        s2 += L'"'; // quote without GetQuotedString (because it escapes now)
        return s2;
    }

    static void MyFormatNew_ReducedName(UString& s, const UString& name)
    {
        s = MyFormatNew(s, GetQuotedReducedString(name));
    }

    static UString GetNanaZipPath()
    {
        return fs2us(NWindows::NDLL::GetModuleDirPrefix()) + L"NanaZip.Modern.FileManager.exe";
    }

    static const wchar_t* CopyFileAddressTitle()
    {
        const LANGID lang = ::GetUserDefaultUILanguage();
        if (PRIMARYLANGID(lang) == LANG_CHINESE)
            return L"复制文件地址";
        return L"Copy file address";
    }

    static const wchar_t* CopyFileNameTitle()
    {
        const LANGID lang = ::GetUserDefaultUILanguage();
        if (PRIMARYLANGID(lang) == LANG_CHINESE)
            return L"复制文件名";
        return L"Copy file name";
    }

    static const wchar_t* CopyFileNameNoExtTitle()
    {
        const LANGID lang = ::GetUserDefaultUILanguage();
        if (PRIMARYLANGID(lang) == LANG_CHINESE)
            return L"复制文件名（无扩展名）";
        return L"Copy file name (no extension)";
    }

    static const wchar_t* CopyFolderAddressTitle()
    {
        const LANGID lang = ::GetUserDefaultUILanguage();
        if (PRIMARYLANGID(lang) == LANG_CHINESE)
            return L"复制文件夹地址";
        return L"Copy folder address";
    }

    static const wchar_t* CopyFolderNameTitle()
    {
        const LANGID lang = ::GetUserDefaultUILanguage();
        if (PRIMARYLANGID(lang) == LANG_CHINESE)
            return L"复制文件夹名";
        return L"Copy folder name";
    }

    static bool SelectionIsOnlyFolders(const std::vector<std::wstring>& paths)
    {
        if (paths.empty())
        {
            return false;
        }
        for (const std::wstring& path : paths)
        {
            const DWORD attr = ::GetFileAttributesW(path.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES ||
                (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                return false;
            }
        }
        return true;
    }

    static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);
    static bool PathExists(const std::wstring& path);
    static bool IsExistingDirectory(const std::wstring& path);
    static std::wstring BaseNameOf(const std::wstring& path);
    static std::wstring NameWithoutLastExt(const std::wstring& name);

    static bool SelectionIsOnlyFiles(const std::vector<std::wstring>& paths)
    {
        if (paths.empty())
            return false;
        for (const std::wstring& path : paths)
        {
            const DWORD attr = ::GetFileAttributesW(path.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES ||
                (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                return false;
            }
        }
        return true;
    }

    static std::wstring ParentDirOf(const std::wstring& path)
    {
        std::wstring trimmed = path;
        while (!trimmed.empty() &&
            (trimmed.back() == L'\\' || trimmed.back() == L'/'))
        {
            trimmed.pop_back();
        }
        const size_t slash = trimmed.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return L"";
        return trimmed.substr(0, slash);
    }

    static bool PathsShareParent(const std::vector<std::wstring>& paths)
    {
        if (paths.empty())
            return false;
        const std::wstring parent = ParentDirOf(paths[0]);
        for (const std::wstring& path : paths)
        {
            if (_wcsicmp(ParentDirOf(path).c_str(), parent.c_str()) != 0)
                return false;
        }
        return !parent.empty();
    }

    static bool IsDriveRootPath(const std::wstring& path)
    {
        wchar_t root[MAX_PATH] = {};
        if (!::GetVolumePathNameW(path.c_str(), root, MAX_PATH))
            return false;
        std::wstring left = path;
        std::wstring right = root;
        while (!left.empty() && (left.back() == L'\\' || left.back() == L'/'))
            left.pop_back();
        while (!right.empty() && (right.back() == L'\\' || right.back() == L'/'))
            right.pop_back();
        return _wcsicmp(left.c_str(), right.c_str()) == 0;
    }

    static bool SelectionHasDriveRoot(const std::vector<std::wstring>& paths)
    {
        for (const std::wstring& path : paths)
        {
            if (IsDriveRootPath(path))
                return true;
        }
        return false;
    }

    static bool SelectionIsNested(const std::vector<std::wstring>& paths)
    {
        for (size_t i = 0; i < paths.size(); ++i)
        {
            std::wstring prefix = paths[i];
            while (!prefix.empty() &&
                (prefix.back() == L'\\' || prefix.back() == L'/'))
            {
                prefix.pop_back();
            }
            prefix += L'\\';
            for (size_t j = 0; j < paths.size(); ++j)
            {
                if (i != j &&
                    _wcsnicmp(paths[j].c_str(), prefix.c_str(), prefix.size()) == 0)
                {
                    return true;
                }
            }
        }
        return false;
    }

    static std::wstring TrimText(std::wstring text)
    {
        while (!text.empty() && iswspace(text.front()))
            text.erase(text.begin());
        while (!text.empty() && iswspace(text.back()))
            text.pop_back();
        return text;
    }

    static bool IsLegalFolderName(const std::wstring& name)
    {
        if (name.empty() || name == L"." || name == L"..")
            return false;
        if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos)
            return false;
        if (name.back() == L'.' || name.back() == L' ')
            return false;
        return true;
    }

    static const wchar_t* FilesToFolderTitle()
    {
        const LANGID lang = ::GetUserDefaultUILanguage();
        if (PRIMARYLANGID(lang) == LANG_CHINESE)
            return L"文件归入文件夹";
        return L"Move files into a folder";
    }

    static const wchar_t* UnwrapFolderTitle()
    {
        const LANGID lang = ::GetUserDefaultUILanguage();
        if (PRIMARYLANGID(lang) == LANG_CHINESE)
            return L"去掉外层文件夹";
        return L"Remove enclosing folder";
    }

    struct FolderNameState
    {
        std::wstring Text;
        bool Accepted = false;
        bool Done = false;
        HWND Edit = nullptr;
        HFONT Font = nullptr;
        HICON IconSmall = nullptr;
        HICON IconBig = nullptr;
    };

    static LRESULT CALLBACK FolderNameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* state = reinterpret_cast<FolderNameState*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == NanaZip::DialogAppearance::ActivateMessage)
        {
            NanaZip::DialogAppearance::Activate(hwnd, ::GetDlgItem(hwnd, 100));
            return 0;
        }
        switch (msg)
        {
        case WM_NCCREATE:
        {
            const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<FolderNameState*>(
                ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            ::CreateWindowExW(
                0, L"STATIC", L"输入文件夹名",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                16, 16, 320, 20, hwnd, (HMENU)101, nullptr, nullptr);
            state->Edit = ::CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                16, 42, 320, 24, hwnd, (HMENU)100, nullptr, nullptr);
            ::CreateWindowExW(
                0, L"BUTTON", L"确定",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                176, 80, 76, 28, hwnd, (HMENU)1, nullptr, nullptr);
            ::CreateWindowExW(
                0, L"BUTTON", L"取消",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                260, 80, 76, 28, hwnd, (HMENU)2, nullptr, nullptr);
            NanaZip::DialogAppearance::ApplyFolderPrompt(hwnd,
                ::GetDpiForWindow(hwnd), state->Font,
                state->IconSmall, state->IconBig);
            return 0;
        }
        case WM_DPICHANGED:
        {
            NanaZip::DialogAppearance::ApplyFolderPrompt(hwnd, HIWORD(wp),
                state->Font, state->IconSmall, state->IconBig);
            const RECT* rectangle = reinterpret_cast<const RECT*>(lp);
            ::SetWindowPos(hwnd, nullptr, rectangle->left, rectangle->top,
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_COMMAND:
            if (!state)
                break;
            if (LOWORD(wp) == 1)
            {
                wchar_t buffer[260] = {};
                ::GetWindowTextW(state->Edit, buffer, 260);
                state->Text = TrimText(buffer);
                state->Accepted = true;
                state->Done = true;
                ::DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == 2)
            {
                state->Accepted = false;
                state->Done = true;
                ::DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (state)
            {
                state->Accepted = false;
                state->Done = true;
            }
            ::DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            ::DeleteObject(state->Font);
            ::DestroyIcon(state->IconSmall);
            ::DestroyIcon(state->IconBig);
            return 0;
        default:
            break;
        }
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }

    static bool AskFolderName(std::wstring& name)
    {
        NanaZip::DialogAppearance::ModalWindowScope Modal;
        NanaZip::DialogAppearance::DialogScope Appearance;
        static bool registered = false;
        const wchar_t* className = L"NanaZipFolderNameWnd";
        if (!registered)
        {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = FolderNameProc;
            wc.hInstance = ::GetModuleHandleW(nullptr);
            wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = className;
            registered = ::RegisterClassW(&wc) != 0 ||
                ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }
        if (!registered)
            return false;
        FolderNameState state;
        RECT rect = { 0, 0, 360, 128 };
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
        ::AdjustWindowRectEx(&rect, style, FALSE, WS_EX_DLGMODALFRAME);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        HWND hwnd = ::CreateWindowExW(
            WS_EX_DLGMODALFRAME, className, L"文件归入文件夹",
            style,
            (::GetSystemMetrics(SM_CXSCREEN) - width) / 2,
            (::GetSystemMetrics(SM_CYSCREEN) - height) / 2,
            width, height, Modal.Owner(), nullptr, ::GetModuleHandleW(nullptr), &state);
        if (!hwnd)
            return false;
        Modal.Show(hwnd);
        MSG msg;
        while (!state.Done && ::GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
            {
                state.Accepted = false;
                state.Done = true;
                ::DestroyWindow(hwnd);
                continue;
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN)
            {
                ::SendMessageW(hwnd, WM_COMMAND, 1, 0);
                continue;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (!state.Accepted)
            return false;
        name = state.Text;
        return true;
    }

    static void ShowMoveNotice(const wchar_t* title, const std::wstring& message)
    {
        if (!message.empty())
            ::MessageBoxW(NanaZip::DialogAppearance::ForegroundOwner(), message.c_str(), title, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    }

    static std::wstring SkippedMessage(
        const wchar_t* lead, const std::vector<std::wstring>& skipped)
    {
        std::wstring message = lead;
        const size_t limit = skipped.size() < 12 ? skipped.size() : 12;
        for (size_t i = 0; i < limit; ++i)
        {
            message += L"\r\n";
            message += skipped[i];
        }
        if (skipped.size() > limit)
            message += L"\r\n...";
        return message;
    }

    static void MoveFilesIntoFolder(const std::vector<std::wstring>& files)
    {
        if (!SelectionIsOnlyFiles(files) || !PathsShareParent(files))
        {
            ShowMoveNotice(FilesToFolderTitle(),
                L"这些文件不在同一个文件夹里，没有移动。");
            return;
        }
        std::wstring folderName;
        if (files.size() == 1)
            folderName = NameWithoutLastExt(BaseNameOf(files[0]));
        else if (!AskFolderName(folderName))
            return;
        folderName = TrimText(folderName);
        if (!IsLegalFolderName(folderName))
        {
            ShowMoveNotice(FilesToFolderTitle(),
                L"文件夹名不能包含 \\ / : * ? \" < > | 。");
            return;
        }
        const std::wstring parent = ParentDirOf(files[0]);
        const std::wstring destDir = JoinPath(parent, folderName);
        if (PathExists(destDir) && !IsExistingDirectory(destDir))
        {
            ShowMoveNotice(FilesToFolderTitle(),
                L"已有同名文件，没有创建文件夹。");
            return;
        }
        const bool created = !PathExists(destDir);
        if (created && !::CreateDirectoryW(destDir.c_str(), nullptr))
        {
            ShowMoveNotice(FilesToFolderTitle(), L"文件夹没有创建成功。");
            return;
        }
        std::vector<std::wstring> skipped;
        for (const std::wstring& file : files)
        {
            const std::wstring dest = JoinPath(destDir, BaseNameOf(file));
            if (PathExists(dest) || !::MoveFileW(file.c_str(), dest.c_str()))
                skipped.push_back(BaseNameOf(file));
        }
        if (created && skipped.size() == files.size())
            ::RemoveDirectoryW(destDir.c_str());
        if (!skipped.empty())
        {
            ShowMoveNotice(FilesToFolderTitle(), SkippedMessage(
                L"以下文件没有移动，避免覆盖：", skipped));
        }
    }

    static void RemoveEnclosingFolders(const std::vector<std::wstring>& folders)
    {
        if (!SelectionIsOnlyFolders(folders) || SelectionHasDriveRoot(folders))
            return;
        if (SelectionIsNested(folders))
        {
            ShowMoveNotice(UnwrapFolderTitle(),
                L"选中的文件夹套在一起，没有移动任何文件。");
            return;
        }
        std::vector<std::wstring> skipped;
        for (const std::wstring& folder : folders)
        {
            const std::wstring parent = ParentDirOf(folder);
            if (parent.empty())
                continue;
            std::vector<std::wstring> children;
            WIN32_FIND_DATAW data = {};
            const HANDLE find = ::FindFirstFileW(
                JoinPath(folder, L"*").c_str(), &data);
            if (find != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (wcscmp(data.cFileName, L".") == 0 ||
                        wcscmp(data.cFileName, L"..") == 0)
                    {
                        continue;
                    }
                    children.push_back(JoinPath(folder, data.cFileName));
                } while (::FindNextFileW(find, &data));
                ::FindClose(find);
            }
            bool allMoved = true;
            for (const std::wstring& child : children)
            {
                const std::wstring dest = JoinPath(parent, BaseNameOf(child));
                if (PathExists(dest) || !::MoveFileW(child.c_str(), dest.c_str()))
                {
                    allMoved = false;
                    skipped.push_back(BaseNameOf(child));
                }
            }
            if (allMoved)
                ::RemoveDirectoryW(folder.c_str());
        }
        if (!skipped.empty())
        {
            ShowMoveNotice(UnwrapFolderTitle(), SkippedMessage(
                L"以下内容没有移动，避免覆盖：", skipped));
        }
    }

    static std::wstring QuoteIfHasSpace(const std::wstring& text)
    {
        if (text.find(L' ') == std::wstring::npos)
        {
            return text;
        }
        return L"\"" + text + L"\"";
    }

    static std::wstring BaseNameOf(const std::wstring& path)
    {
        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
        {
            return path;
        }
        return path.substr(slash + 1);
    }

    static std::wstring NameWithoutLastExt(const std::wstring& name)
    {
        const size_t dot = name.find_last_of(L'.');
        if (dot == std::wstring::npos || dot == 0)
        {
            return name;
        }
        return name.substr(0, dot);
    }

    static bool CopyUnicodeTextToClipboard(const std::wstring& text)
    {
        if (text.empty())
        {
            return false;
        }

        const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!mem)
        {
            return false;
        }

        void* dest = ::GlobalLock(mem);
        if (!dest)
        {
            ::GlobalFree(mem);
            return false;
        }
        ::CopyMemory(dest, text.c_str(), bytes);
        ::GlobalUnlock(mem);

        bool opened = false;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            if (::OpenClipboard(nullptr))
            {
                opened = true;
                break;
            }
            ::Sleep(15);
        }
        if (!opened)
        {
            ::GlobalFree(mem);
            return false;
        }
        if (!::EmptyClipboard())
        {
            ::CloseClipboard();
            ::GlobalFree(mem);
            return false;
        }
        if (!::SetClipboardData(CF_UNICODETEXT, mem))
        {
            ::CloseClipboard();
            ::GlobalFree(mem);
            return false;
        }
        ::CloseClipboard();
        return true;
    }

    static void CopySelectedFileText(
        const std::vector<std::wstring>& paths,
        int kind)
    {
        std::wstring text;
        for (const std::wstring& path : paths)
        {
            std::wstring line = path;
            if (kind != 0)
            {
                line = BaseNameOf(path);
                if (kind == 2)
                {
                    line = NameWithoutLastExt(line);
                }
            }
            if (!text.empty())
            {
                text += L"\r\n";
            }
            text += QuoteIfHasSpace(line);
        }
        CopyUnicodeTextToClipboard(text);
    }

    static const wchar_t* NewFolderDateTitle()
    {
        const LANGID lang = ::GetUserDefaultUILanguage();
        if (PRIMARYLANGID(lang) == LANG_CHINESE)
            return L"以日期新建文件夹";
        return L"New folder by date";
    }

    static const wchar_t* NewFolderBatchTitle()
    {
        const LANGID lang = ::GetUserDefaultUILanguage();
        if (PRIMARYLANGID(lang) == LANG_CHINESE)
            return L"批量新建文件夹";
        return L"Batch new folders";
    }

    static bool IsExistingDirectory(const std::wstring& path)
    {
        const DWORD attr = ::GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES &&
            (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    static bool PathExists(const std::wstring& path)
    {
        return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name)
    {
        if (dir.empty())
            return name;
        if (dir.back() == L'\\' || dir.back() == L'/')
            return dir + name;
        return dir + L'\\' + name;
    }

    static void CreateDateFolder(const std::wstring& dir)
    {
        if (!IsExistingDirectory(dir))
            return;
        SYSTEMTIME st = {};
        ::GetLocalTime(&st);
        wchar_t base[32] = {};
        ::swprintf_s(base, L"%04u_%02u%02u", st.wYear, st.wMonth, st.wDay);
        std::wstring path = JoinPath(dir, base);
        if (!PathExists(path))
        {
            ::CreateDirectoryW(path.c_str(), nullptr);
            return;
        }
        for (int i = 2; i < 10000; ++i)
        {
            path = JoinPath(dir, std::wstring(base) + L"_" + std::to_wstring(i));
            if (!PathExists(path))
            {
                ::CreateDirectoryW(path.c_str(), nullptr);
                return;
            }
        }
    }

    static HICON LoadNanaZipIcon(int cx, int cy)
    {
        wchar_t modulePath[MAX_PATH] = {};
        HMODULE module = nullptr;
        if (::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&LoadNanaZipIcon),
                &module) &&
            ::GetModuleFileNameW(module, modulePath, MAX_PATH))
        {
            wchar_t* slash = wcsrchr(modulePath, L'\\');
            if (slash)
            {
                const size_t remain = MAX_PATH - static_cast<size_t>(slash + 1 - modulePath);
                wcscpy_s(slash + 1, remain, L"NanaZip.ico");
                HICON icon = static_cast<HICON>(::LoadImageW(
                    nullptr, modulePath, IMAGE_ICON, cx, cy, LR_LOADFROMFILE));
                if (icon)
                    return icon;
                wcscpy_s(slash + 1, remain, L"NanaZip.Modern.FileManager.exe");
                HICON bigIcon = nullptr;
                HICON smallIcon = nullptr;
                if (::ExtractIconExW(modulePath, 0, &bigIcon, &smallIcon, 1) > 0)
                {
                    if (cx <= 20)
                    {
                        if (bigIcon)
                            ::DestroyIcon(bigIcon);
                        return smallIcon;
                    }
                    if (smallIcon)
                        ::DestroyIcon(smallIcon);
                    return bigIcon;
                }
            }
        }
        return static_cast<HICON>(::LoadImageW(
            nullptr, L"C:\\Program Files\\NanaZip\\NanaZip.ico",
            IMAGE_ICON, cx, cy, LR_LOADFROMFILE));
    }

    struct BatchDialogState
    {
        HWND Edit = nullptr;
        HWND IconWnd = nullptr;
        HWND TitleWnd = nullptr;
        HWND HintWnd = nullptr;
        HWND HintWnd2 = nullptr;
        HWND OkWnd = nullptr;
        HWND CancelWnd = nullptr;
        HFONT Font = nullptr;
        HFONT HintFont = nullptr;
        HFONT TitleFont = nullptr;
        HICON IconBig = nullptr;
        HICON IconSmall = nullptr;
        HICON IconHeader = nullptr;
        COLORREF Accent = RGB(0, 103, 192);
        int Dpi = 96;
        bool Accepted = false;
        bool Done = false;
        std::wstring Text;
    };

    struct BatchDpiApi
    {
        using SetContextFn = HANDLE(WINAPI*)(HANDLE);
        using SetHostingFn = int(WINAPI*)(int);
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        using AdjustFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        using SpiForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
        using MetricsFn = int(WINAPI*)(int, UINT);

        SetContextFn SetContext = nullptr;
        SetHostingFn SetHosting = nullptr;
        GetDpiForMonitorFn GetForMonitor = nullptr;
        GetDpiForWindowFn GetForWindow = nullptr;
        AdjustFn Adjust = nullptr;
        SpiForDpiFn Spi = nullptr;
        MetricsFn Metrics = nullptr;

        BatchDpiApi()
        {
            HMODULE user = ::GetModuleHandleW(L"user32.dll");
            if (!user)
                user = ::LoadLibraryW(L"user32.dll");
            HMODULE shcore = ::LoadLibraryW(L"Shcore.dll");
            if (user)
            {
                SetContext = reinterpret_cast<SetContextFn>(
                    ::GetProcAddress(user, "SetThreadDpiAwarenessContext"));
                SetHosting = reinterpret_cast<SetHostingFn>(
                    ::GetProcAddress(user, "SetThreadDpiHostingBehavior"));
                GetForWindow = reinterpret_cast<GetDpiForWindowFn>(
                    ::GetProcAddress(user, "GetDpiForWindow"));
                Adjust = reinterpret_cast<AdjustFn>(
                    ::GetProcAddress(user, "AdjustWindowRectExForDpi"));
                Spi = reinterpret_cast<SpiForDpiFn>(
                    ::GetProcAddress(user, "SystemParametersInfoForDpi"));
                Metrics = reinterpret_cast<MetricsFn>(
                    ::GetProcAddress(user, "GetSystemMetricsForDpi"));
            }
            if (shcore)
            {
                GetForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
                    ::GetProcAddress(shcore, "GetDpiForMonitor"));
            }
        }
    };

    static BatchDpiApi& BatchDpi()
    {
        static BatchDpiApi api;
        return api;
    }

    static HANDLE PerMonitorV2Context()
    {
        return reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4));
    }

    struct BatchDpiScope
    {
        HANDLE PreviousContext = nullptr;
        int PreviousHosting = -1;
        bool ContextChanged = false;
        bool HostingChanged = false;

        BatchDpiScope()
        {
            BatchDpiApi& api = BatchDpi();
            if (api.SetHosting)
            {
                PreviousHosting = api.SetHosting(1);
                HostingChanged = PreviousHosting != -1;
            }
            if (api.SetContext)
            {
                PreviousContext = api.SetContext(PerMonitorV2Context());
                ContextChanged = PreviousContext != nullptr;
            }
        }

        ~BatchDpiScope()
        {
            BatchDpiApi& api = BatchDpi();
            if (ContextChanged && api.SetContext)
                api.SetContext(PreviousContext);
            if (HostingChanged && api.SetHosting)
                api.SetHosting(PreviousHosting);
        }
    };

    static int MonitorDpi()
    {
        POINT pt = {};
        ::GetCursorPos(&pt);
        const HMONITOR monitor = ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        UINT x = 96;
        UINT y = 96;
        BatchDpiApi& api = BatchDpi();
        if (api.GetForMonitor && monitor &&
            SUCCEEDED(api.GetForMonitor(monitor, 0, &x, &y)) &&
            x > 0)
            return static_cast<int>(x);
        const HDC dc = ::GetDC(nullptr);
        const int dpi = dc ? ::GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc)
            ::ReleaseDC(nullptr, dc);
        return dpi > 0 ? dpi : 96;
    }

    static int ScalePx(int value, int dpi)
    {
        return ::MulDiv(value, dpi, 96);
    }

    static HBRUSH BatchLightBrush()
    {
        static HBRUSH brush = ::CreateSolidBrush(RGB(243, 243, 243));
        return brush;
    }

    static HBRUSH BatchWhiteBrush()
    {
        static HBRUSH brush = ::CreateSolidBrush(RGB(255, 255, 255));
        return brush;
    }

    static COLORREF BatchAccentColor()
    {
        DWORD value = 0;
        DWORD size = sizeof(value);
        if (::RegGetValueW(
                HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\DWM",
                L"AccentColor",
                RRF_RT_REG_DWORD,
                nullptr,
                &value,
                &size) == ERROR_SUCCESS)
        {
            return RGB(value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF);
        }
        return RGB(0, 103, 192);
    }

    static COLORREF ShadeColor(COLORREF color, int delta)
    {
        auto clamp = [delta](int channel)
        {
            int value = channel + delta;
            if (value < 0)
                value = 0;
            if (value > 255)
                value = 255;
            return value;
        };
        return RGB(
            clamp(GetRValue(color)),
            clamp(GetGValue(color)),
            clamp(GetBValue(color)));
    }

    static HFONT CreateBatchFont(int dpi, int pixel, int weight)
    {
        UNREFERENCED_PARAMETER(pixel);
        return NanaZip::DialogAppearance::CreateFont(dpi, weight);
    }

    static void DrawModernButton(
        const DRAWITEMSTRUCT* draw, bool primary, COLORREF accent, HFONT font, int dpi)
    {
        RECT rc = draw->rcItem;
        const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
        COLORREF fill = primary ? (pressed ? ShadeColor(accent, -28) : accent) : (pressed ? RGB(232, 232, 232) : RGB(255, 255, 255));
        COLORREF text = primary ? RGB(255, 255, 255) : RGB(26, 26, 26);
        COLORREF border = primary ? fill : RGB(208, 208, 208);
        HBRUSH brush = ::CreateSolidBrush(fill);
        HPEN pen = ::CreatePen(PS_SOLID, 1, border);
        const HGDIOBJ oldBrush = ::SelectObject(draw->hDC, brush);
        const HGDIOBJ oldPen = ::SelectObject(draw->hDC, pen);
        const int radius = ScalePx(6, dpi);
        ::RoundRect(draw->hDC, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
        ::SelectObject(draw->hDC, oldPen);
        ::SelectObject(draw->hDC, oldBrush);
        ::DeleteObject(pen);
        ::DeleteObject(brush);
        wchar_t label[32] = {};
        ::GetWindowTextW(draw->hwndItem, label, 32);
        ::SetBkMode(draw->hDC, TRANSPARENT);
        ::SetTextColor(draw->hDC, text);
        const HGDIOBJ oldFont = ::SelectObject(draw->hDC, font);
        ::DrawTextW(draw->hDC, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ::SelectObject(draw->hDC, oldFont);
        if (draw->itemState & ODS_FOCUS)
        {
            RECT focus = rc;
            const int inset = ScalePx(3, dpi);
            ::InflateRect(&focus, -inset, -inset);
            ::DrawFocusRect(draw->hDC, &focus);
        }
    }

    static void LayoutBatchDialog(HWND hwnd, BatchDialogState* state)
    {
        if (!state)
            return;
        RECT rc = {};
        ::GetClientRect(hwnd, &rc);
        const int dpi = state->Dpi > 0 ? state->Dpi : 96;
        const auto scale = [dpi](int value)
        {
            return ScalePx(value, dpi);
        };
        const int margin = scale(16);
        const int hintY = margin;
        const int lineH = scale(18);
        const int hintH = lineH * 2;
        ::MoveWindow(state->HintWnd, margin, hintY, rc.right - margin * 2, lineH, TRUE);
        ::MoveWindow(state->HintWnd2, margin, hintY + lineH, rc.right - margin * 2, lineH, TRUE);
        const int buttonW = scale(96);
        const int buttonH = scale(32);
        const int buttonY = rc.bottom - margin - buttonH;
        ::MoveWindow(
            state->CancelWnd,
            rc.right - margin - buttonW,
            buttonY,
            buttonW,
            buttonH,
            TRUE);
        ::MoveWindow(
            state->OkWnd,
            rc.right - margin - buttonW * 2 - scale(12),
            buttonY,
            buttonW,
            buttonH,
            TRUE);
        const int editY = hintY + hintH + scale(8);
        const int border = scale(1);
        const int editBottom = buttonY - scale(12);
        int editH = editBottom - (editY + border);
        if (editH < scale(88))
            editH = scale(88);
        ::MoveWindow(
            state->Edit,
            margin + border,
            editY + border,
            rc.right - margin * 2 - border * 2,
            editH - border * 2,
            TRUE);
        const int pad = scale(10);
        ::SendMessageW(
            state->Edit, EM_SETMARGINS,
            EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(pad, pad));
    }

    static void ApplyBatchDialogChrome(HWND hwnd, BatchDialogState* state, int dpi)
    {
        if (!state || dpi <= 0)
            return;
        state->Dpi = dpi;
        state->Accent = BatchAccentColor();
        HFONT oldFont = state->Font;
        HFONT oldTitle = state->TitleFont;
        state->Font = CreateBatchFont(dpi, 14, FW_NORMAL);
        HFONT oldHint = state->HintFont;
        state->HintFont = CreateBatchFont(dpi, 12, FW_NORMAL);
        if (state->HintWnd)
            ::SendMessageW(state->HintWnd, WM_SETFONT, (WPARAM)state->HintFont, TRUE);
        if (state->HintWnd2)
            ::SendMessageW(state->HintWnd2, WM_SETFONT, (WPARAM)state->HintFont, TRUE);
        if (state->Edit)
            ::SendMessageW(state->Edit, WM_SETFONT, (WPARAM)state->Font, TRUE);
        if (state->OkWnd)
            ::SendMessageW(state->OkWnd, WM_SETFONT, (WPARAM)state->Font, TRUE);
        if (state->CancelWnd)
            ::SendMessageW(state->CancelWnd, WM_SETFONT, (WPARAM)state->Font, TRUE);
        if (oldFont)
            ::DeleteObject(oldFont);
        if (oldHint)
            ::DeleteObject(oldHint);
        if (oldTitle)
            ::DeleteObject(oldTitle);

        int bigSize = ScalePx(32, dpi);
        int smallSize = ScalePx(16, dpi);
        if (BatchDpi().Metrics)
        {
            const int metricBig = BatchDpi().Metrics(SM_CXICON, static_cast<UINT>(dpi));
            const int metricSmall = BatchDpi().Metrics(SM_CXSMICON, static_cast<UINT>(dpi));
            if (metricBig > 0)
                bigSize = metricBig;
            if (metricSmall > 0)
                smallSize = metricSmall;
        }
        HICON oldBig = state->IconBig;
        HICON oldSmall = state->IconSmall;
        state->IconBig = LoadNanaZipIcon(bigSize, bigSize);
        state->IconSmall = LoadNanaZipIcon(smallSize, smallSize);
        ::SetWindowTextW(hwnd, L"批量新建文件夹");
        if (state->IconBig)
            ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)state->IconBig);
        if (state->IconSmall)
            ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)state->IconSmall);
        if (oldBig)
            ::DestroyIcon(oldBig);
        if (oldSmall)
            ::DestroyIcon(oldSmall);
    }

    static LRESULT CALLBACK BatchDialogProc(
        HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* state = reinterpret_cast<BatchDialogState*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == NanaZip::DialogAppearance::ActivateMessage)
        {
            NanaZip::DialogAppearance::Activate(hwnd, ::GetDlgItem(hwnd, 100));
            return 0;
        }
        switch (msg)
        {
        case WM_NCCREATE:
        {
            const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return ::DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<BatchDialogState*>(
                ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            state->HintWnd = ::CreateWindowExW(
                0, L"STATIC", L"一行一个文件夹名，可以直接输入或粘贴。",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_NOPREFIX,
                0, 0, 10, 10, hwnd, (HMENU)103,
                ::GetModuleHandleW(nullptr), nullptr);
            state->HintWnd2 = ::CreateWindowExW(
                0, L"STATIC", L"Ctrl+回车创建。",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_NOPREFIX,
                0, 0, 10, 10, hwnd, (HMENU)104,
                ::GetModuleHandleW(nullptr), nullptr);
            state->Edit = ::CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                0, 0, 10, 10, hwnd, (HMENU)100,
                ::GetModuleHandleW(nullptr), nullptr);
            state->OkWnd = ::CreateWindowExW(
                0, L"BUTTON", L"确定",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 10, 10, hwnd, (HMENU)1,
                ::GetModuleHandleW(nullptr), nullptr);
            state->CancelWnd = ::CreateWindowExW(
                0, L"BUTTON", L"取消",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 10, 10, hwnd, (HMENU)2,
                ::GetModuleHandleW(nullptr), nullptr);
            int dpi = state->Dpi > 0 ? state->Dpi : 96;
            if (BatchDpi().GetForWindow)
            {
                const UINT windowDpi = BatchDpi().GetForWindow(hwnd);
                if (windowDpi > 0)
                    dpi = static_cast<int>(windowDpi);
            }
            ApplyBatchDialogChrome(hwnd, state, dpi);
            LayoutBatchDialog(hwnd, state);
            return 0;
        }
        case WM_SIZE:
            LayoutBatchDialog(hwnd, state);
            return 0;
        case WM_DPICHANGED:
        {
            if (!state)
                break;
            const int dpi = HIWORD(wp);
            ApplyBatchDialogChrome(hwnd, state, dpi > 0 ? dpi : state->Dpi);
            const auto* suggested = reinterpret_cast<const RECT*>(lp);
            if (suggested)
            {
                ::SetWindowPos(
                    hwnd,
                    nullptr,
                    suggested->left,
                    suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            LayoutBatchDialog(hwnd, state);
            return 0;
        }
        case WM_ERASEBKGND:
        {
            RECT rc = {};
            ::GetClientRect(hwnd, &rc);
            ::FillRect(reinterpret_cast<HDC>(wp), &rc, BatchLightBrush());
            return 1;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            const HDC dc = ::BeginPaint(hwnd, &ps);
            if (state && state->Edit)
            {
                RECT rc = {};
                ::GetWindowRect(state->Edit, &rc);
                ::MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&rc), 2);
                const int border = state->Dpi > 0 ? ScalePx(1, state->Dpi) : 1;
                ::InflateRect(&rc, border, border);
                HBRUSH frame = ::CreateSolidBrush(RGB(208, 208, 208));
                ::FrameRect(dc, &rc, frame);
                ::DeleteObject(frame);
            }
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM:
        {
            const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (!state || !draw)
                break;
            if (draw->hwndItem == state->OkWnd || draw->hwndItem == state->CancelWnd)
            {
                DrawModernButton(
                    draw,
                    draw->hwndItem == state->OkWnd,
                    state->Accent,
                    state->Font,
                    state->Dpi > 0 ? state->Dpi : 96);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        {
            const HDC dc = reinterpret_cast<HDC>(wp);
            ::SetBkMode(dc, TRANSPARENT);
            if (state && (reinterpret_cast<HWND>(lp) == state->HintWnd ||
                reinterpret_cast<HWND>(lp) == state->HintWnd2))
                ::SetTextColor(dc, RGB(96, 96, 96));
            else
                ::SetTextColor(dc, RGB(26, 26, 26));
            return reinterpret_cast<LRESULT>(BatchLightBrush());
        }
        case WM_CTLCOLOREDIT:
        {
            const HDC dc = reinterpret_cast<HDC>(wp);
            ::SetBkColor(dc, RGB(255, 255, 255));
            ::SetTextColor(dc, RGB(26, 26, 26));
            return reinterpret_cast<LRESULT>(BatchWhiteBrush());
        }
        case WM_COMMAND:
            if (!state)
                return 0;
            if (LOWORD(wp) == 1)
            {
                const int len = ::GetWindowTextLengthW(state->Edit);
                state->Text.assign(static_cast<size_t>(len), L'\0');
                if (len > 0)
                    ::GetWindowTextW(state->Edit, &state->Text[0], len + 1);
                state->Accepted = true;
                state->Done = true;
                ::DestroyWindow(hwnd);
            }
            else if (LOWORD(wp) == 2)
            {
                state->Accepted = false;
                state->Done = true;
                ::DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            if (state)
            {
                state->Accepted = false;
                state->Done = true;
            }
            ::DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                if (state->Font)
                {
                    ::DeleteObject(state->Font);
                    state->Font = nullptr;
                }
                if (state->HintFont)
                {
                    ::DeleteObject(state->HintFont);
                    state->HintFont = nullptr;
                }
                if (state->TitleFont)
                {
                    ::DeleteObject(state->TitleFont);
                    state->TitleFont = nullptr;
                }
                if (state->IconBig)
                {
                    ::DestroyIcon(state->IconBig);
                    state->IconBig = nullptr;
                }
                if (state->IconSmall)
                {
                    ::DestroyIcon(state->IconSmall);
                    state->IconSmall = nullptr;
                }
                if (state->IconHeader)
                {
                    ::DestroyIcon(state->IconHeader);
                    state->IconHeader = nullptr;
                }
            }
            return 0;
        default:
            break;
        }
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }

    static bool ShowBatchFolderDialogCore(std::wstring& text)
    {
        NanaZip::DialogAppearance::ModalWindowScope Modal;
        BatchDpiScope dpiScope;
        NanaZip::DialogAppearance::DialogScope Appearance;
        static bool registered = false;
        const wchar_t* className = L"NanaZipBatchNewFolderWnd";
        if (!registered)
        {
            WNDCLASSW wc = {};
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = BatchDialogProc;
            wc.hInstance = ::GetModuleHandleW(nullptr);
            wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = BatchLightBrush();
            wc.lpszClassName = className;
            registered = ::RegisterClassW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }
        if (!registered)
            return false;

        BatchDialogState state;
        state.Dpi = MonitorDpi();
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_CLIPCHILDREN;
        const DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_DLGMODALFRAME;
        RECT rect = { 0, 0, ScalePx(380, state.Dpi), ScalePx(262, state.Dpi) };
        if (BatchDpi().Adjust)
            BatchDpi().Adjust(&rect, style, FALSE, exStyle, static_cast<UINT>(state.Dpi));
        else
            ::AdjustWindowRectEx(&rect, style, FALSE, exStyle);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        POINT cursor = {};
        ::GetCursorPos(&cursor);
        const HMONITOR monitor = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info = {};
        info.cbSize = sizeof(info);
        int x = 0;
        int y = 0;
        if (monitor && ::GetMonitorInfoW(monitor, &info))
        {
            x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
            y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
        }
        else
        {
            x = (::GetSystemMetrics(SM_CXSCREEN) - width) / 2;
            y = (::GetSystemMetrics(SM_CYSCREEN) - height) / 2;
        }
        HWND hwnd = ::CreateWindowExW(
            WS_EX_DLGMODALFRAME, className, L"批量新建文件夹",
            style,
            x, y, width, height, Modal.Owner(), nullptr,
            ::GetModuleHandleW(nullptr), &state);
        if (!hwnd)
            return false;
        ::SetWindowTextW(hwnd, L"批量新建文件夹");
        if (HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll"))
        {
            using DwmSetFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
            auto setAttr = reinterpret_cast<DwmSetFn>(
                ::GetProcAddress(dwm, "DwmSetWindowAttribute"));
            const int corner = 2;
            if (setAttr)
                setAttr(hwnd, 33, &corner, sizeof(corner));
        }
        Modal.Show(hwnd);

        MSG msg;
        while (!state.Done && ::GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
            {
                state.Accepted = false;
                state.Done = true;
                ::DestroyWindow(hwnd);
                continue;
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN &&
                (::GetKeyState(VK_CONTROL) & 0x8000))
            {
                ::PostMessageW(hwnd, WM_COMMAND, 1, 0);
                continue;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (!state.Accepted)
            return false;
        text = state.Text;
        return true;
    }

    static bool ShowBatchFolderDialog(std::wstring& text)
    {
        return ShowBatchFolderDialogCore(text);
    }

    static std::wstring TrimSpaces(const std::wstring& text)
    {
        size_t begin = 0;
        size_t end = text.size();
        while (begin < end && (text[begin] == L' ' || text[begin] == L'\t' || text[begin] == L'\r'))
            ++begin;
        while (end > begin && (text[end - 1] == L' ' || text[end - 1] == L'\t' || text[end - 1] == L'\r'))
            --end;
        return text.substr(begin, end - begin);
    }

    static bool NameHasIllegalChars(const std::wstring& name)
    {
        if (name.empty() || name == L"." || name == L"..")
            return true;
        return name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos;
    }

    static void CreateBatchFolders(const std::wstring& dir)
    {
        if (!IsExistingDirectory(dir))
            return;
        std::wstring text;
        if (!ShowBatchFolderDialog(text))
            return;

        std::wstring skipped;
        int skippedCount = 0;
        size_t pos = 0;
        while (pos <= text.size())
        {
            const size_t next = text.find(L'\n', pos);
            const std::wstring raw = text.substr(
                pos, next == std::wstring::npos ? std::wstring::npos : next - pos);
            pos = next == std::wstring::npos ? text.size() + 1 : next + 1;
            const std::wstring name = TrimSpaces(raw);
            if (name.empty())
                continue;
            const wchar_t* reason = nullptr;
            if (NameHasIllegalChars(name))
                reason = L"非法字符";
            else if (PathExists(JoinPath(dir, name)))
                reason = L"已存在";
            else if (!::CreateDirectoryW(JoinPath(dir, name).c_str(), nullptr))
                reason = L"创建失败";
            if (reason)
            {
                if (skippedCount < 30)
                {
                    skipped += name;
                    skipped += L"（";
                    skipped += reason;
                    skipped += L"）\r\n";
                }
                ++skippedCount;
            }
        }
        if (skippedCount == 0)
            return;
        std::wstring message = L"已跳过：\r\n";
        message += skipped;
        if (skippedCount > 30)
            message += L"……\r\n";
        ::MessageBoxW(NanaZip::DialogAppearance::ForegroundOwner(), message.c_str(), L"批量新建文件夹", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    }

    static std::wstring FolderPathFromSite(IUnknown* site)
    {
        if (!site)
            return std::wstring();
        IServiceProvider* provider = nullptr;
        if (FAILED(site->QueryInterface(IID_IServiceProvider, reinterpret_cast<void**>(&provider))) ||
            !provider)
            return std::wstring();
        IShellBrowser* browser = nullptr;
        HRESULT hr = provider->QueryService(
            SID_STopLevelBrowser, IID_IShellBrowser, reinterpret_cast<void**>(&browser));
        if (FAILED(hr) || !browser)
        {
            hr = provider->QueryService(
                SID_SShellBrowser, IID_IShellBrowser, reinterpret_cast<void**>(&browser));
        }
        provider->Release();
        if (FAILED(hr) || !browser)
            return std::wstring();
        IShellView* view = nullptr;
        hr = browser->QueryActiveShellView(&view);
        browser->Release();
        if (FAILED(hr) || !view)
            return std::wstring();
        IFolderView* folderView = nullptr;
        hr = view->QueryInterface(IID_IFolderView, reinterpret_cast<void**>(&folderView));
        view->Release();
        if (FAILED(hr) || !folderView)
            return std::wstring();
        IPersistFolder2* persist = nullptr;
        hr = folderView->GetFolder(IID_IPersistFolder2, reinterpret_cast<void**>(&persist));
        folderView->Release();
        if (FAILED(hr) || !persist)
            return std::wstring();
        PIDLIST_ABSOLUTE pidl = nullptr;
        hr = persist->GetCurFolder(&pidl);
        persist->Release();
        std::wstring result;
        if (SUCCEEDED(hr) && pidl)
        {
            wchar_t path[MAX_PATH] = {};
            if (::SHGetPathFromIDListW(pidl, path))
                result = path;
            ::CoTaskMemFree(pidl);
        }
        return result;
    }
}

namespace NanaZip::ShellExtension
{
    namespace CommandID
    {
        enum
        {
            None,

            Open,
            Test,

            Extract,
            ExtractHere,
            ExtractHereSmart,
            ExtractHereDelete,
            ExtractTo,
            ExtractToDelete,

            Compress,
            CompressTo7z,
            CompressToZip,
            CompressToZipDelete,

            CompressEmail,
            CompressTo7zEmail,
            CompressToZipEmail,

            HashCRC32,
            HashCRC64,
            HashSHA1,
            HashSHA256,
            HashAll,

            CopyFileAddress,
            CopyFileName,
            CopyFileNameNoExt,

            NewFolder,
            NewFolderDate,
            NewFolderBatch,

            FilesToFolder,
            UnwrapFolder,

            RenameMenu,
            Rename,
            RenameList,
            RenameManage,
            RenameUndo,

            Maximum
        };
    }

    using SubCommandList = std::vector<winrt::com_ptr<IExplorerCommand>>;
    using SubCommandListIterator = SubCommandList::const_iterator;

    struct RenameEnumerator : winrt::implements<
        RenameEnumerator, IEnumExplorerCommand>
    {
        SubCommandList Commands;
        size_t Position = 0;

        HRESULT STDMETHODCALLTYPE Next(ULONG Count, IExplorerCommand** Output,
            ULONG* Fetched) noexcept override
        {
            if (!Output || (!Fetched && Count != 1))
                return E_POINTER;
            ULONG Actual = 0;
            while (Actual < Count && this->Position < this->Commands.size())
                this->Commands[this->Position++].copy_to(&Output[Actual++]);
            if (Fetched)
                *Fetched = Actual;
            return Actual == Count ? S_OK : S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE Skip(ULONG Count) noexcept override
        {
            size_t Remaining = this->Commands.size() - this->Position;
            this->Position += (std::min)(static_cast<size_t>(Count), Remaining);
            return Count <= Remaining ? S_OK : S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE Reset() noexcept override
        {
            this->Position = 0;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Clone(IEnumExplorerCommand** Output) noexcept override
        {
            if (!Output)
                return E_POINTER;
            *Output = nullptr;
            try
            {
                auto Copy = winrt::make_self<RenameEnumerator>();
                Copy->Commands = this->Commands;
                Copy->Position = this->Position;
                return Copy->QueryInterface(IID_PPV_ARGS(Output));
            }
            catch (...)
            {
                return winrt::to_hresult();
            }
        }
    };

    struct ExplorerCommandBase : public winrt::implements<
        ExplorerCommandBase,
        IExplorerCommand>
    {
    private:

        std::wstring m_Title;

        DWORD m_CommandID;
        bool m_IsSeparator;
        CBoolPair m_ElimDup;
        UInt32 m_WriteZone;
        std::wstring m_TargetDir;
        std::vector<std::wstring> m_RenamePaths;
        NanaZip::Renaming::Preset m_RenamePreset;
        bool m_HasRenamePreset = false;
        SubCommandList m_RenameCommands;

    public:

        ExplorerCommandBase(
            std::wstring const& Title = std::wstring(),
            DWORD CommandID = CommandID::None,
            CBoolPair const& ElimDup = CBoolPair(),
            UInt32 const& WriteZone = static_cast<UInt32>(-1),
            std::wstring const& TargetDir = std::wstring(),
            std::vector<std::wstring> const& RenamePaths = {},
            NanaZip::Renaming::Preset const* RenamePreset = nullptr) :
            m_Title(Title),
            m_CommandID(CommandID),
            m_ElimDup(ElimDup),
            m_WriteZone(WriteZone),
            m_TargetDir(TargetDir)
        {
            this->m_IsSeparator = (this->m_CommandID == CommandID::None);
            this->m_RenamePaths = RenamePaths;
            if (RenamePreset)
            {
                this->m_RenamePreset = *RenamePreset;
                this->m_HasRenamePreset = true;
            }
            if (this->m_CommandID == CommandID::RenameMenu)
            {
                auto Add = [&](std::wstring const& Label, DWORD Identifier,
                    NanaZip::Renaming::Preset const* Preset = nullptr)
                {
                    this->m_RenameCommands.push_back(
                        winrt::make<ExplorerCommandBase>(Label, Identifier,
                            CBoolPair(), static_cast<UInt32>(-1),
                            std::wstring(), RenamePaths, Preset));
                };
                Add(L"按规则重命名…", CommandID::Rename);
                Add(L"", CommandID::None);
                std::vector<NanaZip::Renaming::Preset> Saved;
                unsigned Limit = 8;
                NanaZip::Renaming::LoadPresets(Saved, Limit);
                unsigned Count = 0;
                for (auto const& Current : Saved)
                {
                    if (Current.ShowInMenu && Count++ < Limit)
                        Add(Current.Label, CommandID::Rename, &Current);
                }
                Add(L"更多常用名称…", CommandID::RenameManage);
                Add(L"", CommandID::None);
                Add(L"管理常用名称…", CommandID::RenameManage);
                std::vector<NanaZip::Renaming::Entry> Undo;
                if (NanaZip::Renaming::LoadUndo(Undo))
                    Add(L"撤销上次重命名…", CommandID::RenameUndo);
            }
        }

#pragma region IExplorerCommand

        HRESULT STDMETHODCALLTYPE GetTitle(
            _In_opt_ IShellItemArray* psiItemArray,
            _Outptr_ LPWSTR* ppszName)
        {
            UNREFERENCED_PARAMETER(psiItemArray);

            if (this->m_IsSeparator)
            {
                *ppszName = nullptr;
                return S_FALSE;
            }

            return ::SHStrDupW(this->m_Title.c_str(), ppszName);
        }

        HRESULT STDMETHODCALLTYPE GetIcon(
            _In_opt_ IShellItemArray* psiItemArray,
            _Outptr_ LPWSTR* ppszIcon)
        {
            UNREFERENCED_PARAMETER(psiItemArray);

            *ppszIcon = nullptr;
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE GetToolTip(
            _In_opt_ IShellItemArray* psiItemArray,
            _Outptr_ LPWSTR* ppszInfotip)
        {
            UNREFERENCED_PARAMETER(psiItemArray);
            *ppszInfotip = nullptr;
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE GetCanonicalName(
            _Out_ GUID* pguidCommandName)
        {
            *pguidCommandName = GUID_NULL;
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE GetState(
            _In_opt_ IShellItemArray* psiItemArray,
            _In_ BOOL fOkToBeSlow,
            _Out_ EXPCMDSTATE* pCmdState)
        {
            UNREFERENCED_PARAMETER(psiItemArray);
            UNREFERENCED_PARAMETER(fOkToBeSlow);
            *pCmdState = ECS_ENABLED;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Invoke(
            _In_opt_ IShellItemArray* psiItemArray,
            _In_opt_ IBindCtx* pbc)
        {
            UNREFERENCED_PARAMETER(pbc);

            NanaZip::DialogAppearance::DialogScope Appearance;

            if (this->m_IsSeparator)
            {
                return E_NOTIMPL;
            }

            if (this->m_CommandID >= CommandID::Rename &&
                this->m_CommandID <= CommandID::RenameUndo)
            {
                // Capture the invoking window before reading presets or files.
                HWND Owner = ::GetForegroundWindow();
                if (Owner)
                    Owner = ::GetAncestor(Owner, GA_ROOT);
                NanaZip::Renaming::Preset Initial = this->m_RenamePreset;
                bool HasInitial = this->m_HasRenamePreset;
                if (this->m_CommandID == CommandID::RenameList)
                {
                    Initial.Settings.Operation = NanaZip::Renaming::Mode::List;
                    HasInitial = true;
                }
                NanaZip::Renaming::Show(this->m_RenamePaths, Owner,
                    HasInitial ? &Initial : nullptr,
                    this->m_CommandID == CommandID::RenameManage,
                    this->m_CommandID == CommandID::RenameUndo);
                return S_OK;
            }

            if (this->m_CommandID == CommandID::NewFolderDate ||
                this->m_CommandID == CommandID::NewFolderBatch)
            {
                if (this->m_CommandID == CommandID::NewFolderDate)
                    CreateDateFolder(this->m_TargetDir);
                else
                    CreateBatchFolders(this->m_TargetDir);
                return S_OK;
            }

            if (this->m_CommandID == CommandID::FilesToFolder ||
                this->m_CommandID == CommandID::UnwrapFolder)
            {
                std::vector<std::wstring> Selected;
                if (psiItemArray)
                {
                    DWORD Count = 0;
                    if (SUCCEEDED(psiItemArray->GetCount(&Count)))
                    {
                        for (DWORD i = 0; i < Count; ++i)
                        {
                            winrt::com_ptr<IShellItem> Item;
                            if (FAILED(psiItemArray->GetItemAt(i, Item.put())))
                                continue;
                            LPWSTR DisplayName = nullptr;
                            if (SUCCEEDED(Item->GetDisplayName(
                                SIGDN_FILESYSPATH, &DisplayName)))
                            {
                                Selected.push_back(DisplayName);
                                ::CoTaskMemFree(DisplayName);
                            }
                        }
                    }
                }
                if (this->m_CommandID == CommandID::FilesToFolder)
                    MoveFilesIntoFolder(Selected);
                else
                    RemoveEnclosingFolders(Selected);
                return S_OK;
            }

            std::vector<std::wstring> FilePaths;
            if (psiItemArray)
            {
                DWORD Count = 0;
                if (SUCCEEDED(psiItemArray->GetCount(&Count)))
                {
                    for (DWORD i = 0; i < Count; ++i)
                    {
                        winrt::com_ptr<IShellItem> Item;
                        if (SUCCEEDED(psiItemArray->GetItemAt(
                            i,
                            Item.put())))
                        {
                            LPWSTR DisplayName = nullptr;
                            if (SUCCEEDED(Item->GetDisplayName(
                                SIGDN_FILESYSPATH,
                                &DisplayName)))
                            {
                                FilePaths.push_back(std::wstring(DisplayName));
                                ::CoTaskMemFree(DisplayName);
                            }
                        }
                    }
                }
            }

            bool NeedExtract = false;
            if (FilePaths.size() > 0)
            {
                for (std::wstring const FilePath : FilePaths)
                {
                    DWORD FileAttributes = ::GetFileAttributesW(
                        FilePath.c_str());
                    if (FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    {
                        continue;
                    }

                    if (DoNeedExtract(::PathFindFileNameW(FilePath.c_str())))
                    {
                        NeedExtract = true;
                        break;
                    }
                }
            }

            std::wstring SpecFolder = L"*";
            if (NeedExtract)
            {
                if (FilePaths.size() == 1)
                {
                    SpecFolder = GetSubFolderNameForExtract(
                        ::PathFindFileNameW(FilePaths[0].c_str()));
                }
                SpecFolder += L'\\';
            }

            UStringVector FileNames;
            for (std::wstring const FilePath : FilePaths)
            {
                FileNames.Add(FilePath.c_str());
            }

            FString FolderPrefix;

            std::wstring ArchiveName;
            if (FilePaths.size() > 0)
            {
                NWindows::NFile::NFind::CFileInfo FileInfo0;

                const UString& FileName = FileNames.Front();

                if (NWindows::NFile::NName::IsDevicePath(us2fs(FileName)))
                {
                    // CFileInfo::Find can be slow for device files. So we
                    // don't call it.
                    // we need only name here.
                    // change it 4 - must be constant
                    FileInfo0.Name = us2fs(FileName.Ptr(
                        NWindows::NFile::NName::kDevicePathPrefixSize));
                    FolderPrefix = "C:\\";
                }
                else
                {
                    if (!FileInfo0.Find(us2fs(FileName)))
                    {
                        return ::HRESULT_FROM_WIN32(::GetLastError());
                    }
                    NWindows::NFile::NDir::GetOnlyDirPrefix(
                        us2fs(FileName),
                        FolderPrefix);
                }

                const UString Name = CreateArchiveName(
                    FileNames,
                    FileNames.Size() == 1 ? &FileInfo0 : nullptr);
                ArchiveName = std::wstring(Name.Ptr(), Name.Len());

            }

            std::wstring BaseFolder = std::wstring(
                FolderPrefix.Ptr(),
                FolderPrefix.Len());

            std::wstring ArchiveName7z = ArchiveName + L".7z";
            std::wstring ArchiveNameZip = ArchiveName + L".zip";

            switch (this->m_CommandID)
            {
            case CommandID::Open:
            {
                if (FilePaths.size() != 1)
                {
                    break;
                }

                DWORD FileAttributes = ::GetFileAttributesW(
                    FilePaths[0].c_str());
                if (FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    break;
                }

                if (!DoNeedExtract(FilePaths[0].c_str()))
                {
                    break;
                }

                ::OpenArchiveInFileManager(FilePaths[0].c_str());
                break;
            }
            case CommandID::Test:
            {
                if (!NeedExtract)
                {
                    break;
                }

                TestArchives(FileNames);
                break;
            }
            case CommandID::Extract:
            case CommandID::ExtractHere:
            case CommandID::ExtractHereSmart:
            case CommandID::ExtractHereDelete:
            case CommandID::ExtractTo:
            case CommandID::ExtractToDelete:
            {
                if (!NeedExtract)
                {
                    break;
                }

                std::wstring Folder = BaseFolder;
                if (this->m_CommandID != CommandID::ExtractHere &&
                    this->m_CommandID != CommandID::ExtractHereSmart &&
                    this->m_CommandID != CommandID::ExtractHereDelete)
                {
                    Folder += SpecFolder;
                }

                const bool DeleteArchive = (
                    this->m_CommandID == CommandID::ExtractHereDelete ||
                    this->m_CommandID == CommandID::ExtractToDelete);

                ExtractArchives(
                    FileNames,
                    Folder.c_str(),
                    (this->m_CommandID == CommandID::Extract),
                    ((this->m_CommandID == CommandID::ExtractTo ||
                        this->m_CommandID == CommandID::ExtractToDelete)
                    && this->m_ElimDup.Val),
                    this->m_WriteZone,
                    (this->m_CommandID == CommandID::ExtractHereSmart),
                    false,
                    DeleteArchive);

                break;
            }
            case CommandID::Compress:
            case CommandID::CompressTo7z:
            case CommandID::CompressToZip:
            case CommandID::CompressToZipDelete:
            case CommandID::CompressEmail:
            case CommandID::CompressTo7zEmail:
            case CommandID::CompressToZipEmail:
            {
                bool Email =(
                    (this->m_CommandID == CommandID::CompressEmail) ||
                    (this->m_CommandID == CommandID::CompressTo7zEmail) ||
                    (this->m_CommandID == CommandID::CompressToZipEmail));
                bool ShowDialog = (
                    (this->m_CommandID == CommandID::Compress) ||
                    (this->m_CommandID == CommandID::CompressEmail));
                bool AddExtension = (
                    (this->m_CommandID == CommandID::Compress) ||
                    (this->m_CommandID == CommandID::CompressEmail));
                bool Is7z = (
                    (this->m_CommandID == CommandID::CompressTo7z) ||
                    (this->m_CommandID == CommandID::CompressTo7zEmail));
                bool DeleteSources = (
                    this->m_CommandID == CommandID::CompressToZipDelete);

                std::wstring Name = (
                    AddExtension
                    ? ArchiveName
                    : (Is7z ? ArchiveName7z : ArchiveNameZip));

                CompressFiles(
                    BaseFolder.c_str(),
                    Name.c_str(),
                    Is7z ? L"7z" : L"zip",
                    AddExtension,
                    FileNames,
                    Email,
                    ShowDialog,
                    false,
                    DeleteSources);

                break;
            }
            case CommandID::HashCRC32:
            case CommandID::HashCRC64:
            case CommandID::HashSHA1:
            case CommandID::HashSHA256:
            case CommandID::HashAll:
            {
                std::wstring MethodName;
                switch (this->m_CommandID)
                {
                case CommandID::HashCRC32:
                    MethodName = L"CRC32";
                    break;
                case CommandID::HashCRC64:
                    MethodName = L"CRC64";
                    break;
                case CommandID::HashSHA1:
                    MethodName = L"SHA1";
                    break;
                case CommandID::HashSHA256:
                    MethodName = L"SHA256";
                    break;
                case CommandID::HashAll:
                    MethodName = L"*";
                    break;
                default:
                    break;
                }

                CalcChecksum(FileNames, MethodName.c_str(), L"", L"");
                break;
            }
            case CommandID::CopyFileAddress:
            {
                CopySelectedFileText(FilePaths, 0);
                break;
            }
            case CommandID::CopyFileName:
            {
                CopySelectedFileText(FilePaths, 1);
                break;
            }
            case CommandID::CopyFileNameNoExt:
            {
                CopySelectedFileText(FilePaths, 2);
                break;
            }
            default:
                break;
            }

            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetFlags(
            _Out_ EXPCMDFLAGS* pFlags)
        {
            *pFlags =
                this->m_IsSeparator
                ? ECF_ISSEPARATOR
                : this->m_CommandID == CommandID::RenameMenu
                    ? ECF_HASSUBCOMMANDS : ECF_DEFAULT;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE EnumSubCommands(
            _Outptr_ IEnumExplorerCommand** ppEnum)
        {
            *ppEnum = nullptr;
            if (this->m_CommandID == CommandID::RenameMenu)
            {
                auto Enumerator = winrt::make_self<RenameEnumerator>();
                Enumerator->Commands = this->m_RenameCommands;
                return Enumerator->QueryInterface(IID_PPV_ARGS(ppEnum));
            }
            return E_NOTIMPL;
        }

#pragma endregion
    };


    struct ExplorerCommandRoot : public winrt::implements<
        ExplorerCommandRoot,
        IExplorerCommand,
        IEnumExplorerCommand,
        IObjectWithSite>
    {
    private:

        DWORD m_ContextMenuFlags;
        CBoolPair m_ContextMenuElimDup;
        bool m_Background = false;
        winrt::com_ptr<IUnknown> m_Site;

        bool m_Initialized = false;
        SubCommandList m_SubCommands;
        SubCommandListIterator m_CurrentSubCommand;

        void Initialize(
            _In_opt_ IShellItemArray* psiItemArray)
        {
            if (m_Initialized)
            {
                return;
            }

            m_Initialized = true;

            std::vector<std::wstring> FilePaths;
            if (psiItemArray)
            {
                DWORD Count = 0;
                if (SUCCEEDED(psiItemArray->GetCount(&Count)))
                {
                    for (DWORD i = 0; i < Count; ++i)
                    {
                        winrt::com_ptr<IShellItem> Item;
                        if (SUCCEEDED(psiItemArray->GetItemAt(
                            i,
                            Item.put())))
                        {
                            LPWSTR DisplayName = nullptr;
                            if (SUCCEEDED(Item->GetDisplayName(
                                SIGDN_FILESYSPATH,
                                &DisplayName)))
                            {
                                FilePaths.push_back(std::wstring(DisplayName));
                                ::CoTaskMemFree(DisplayName);
                            }
                        }
                    }
                }
            }

            if (this->m_Background)
            {
                std::wstring target;
                if (FilePaths.size() == 1 && IsExistingDirectory(FilePaths[0]))
                    target = FilePaths[0];
                if (target.empty())
                    target = FolderPathFromSite(this->m_Site.get());
                if (!target.empty())
                {
                    this->m_SubCommands.push_back(winrt::make<ExplorerCommandBase>(
                        NewFolderBatchTitle(), CommandID::NewFolderBatch,
                        CBoolPair(), static_cast<UInt32>(-1), target));
                    this->m_SubCommands.push_back(winrt::make<ExplorerCommandBase>(
                        NewFolderDateTitle(), CommandID::NewFolderDate,
                        CBoolPair(), static_cast<UInt32>(-1), target));
                }
                return;
            }

            if (FilePaths.empty())
            {
                return;
            }

            UStringVector FileNames;
            for (std::wstring const FilePath : FilePaths)
            {
                FileNames.Add(FilePath.c_str());
            }

            bool NeedExtract = false;
            // Compress again only when the selection is not entirely archives.
            bool OnlyArchives = !FilePaths.empty();
            for (std::wstring const FilePath : FilePaths)
            {
                DWORD FileAttributes = ::GetFileAttributesW(
                    FilePath.c_str());
                const bool IsDirectory = (
                    FileAttributes != INVALID_FILE_ATTRIBUTES &&
                    (FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
                const bool IsArchive = (
                    !IsDirectory &&
                    FileAttributes != INVALID_FILE_ATTRIBUTES &&
                    IsRecognizedArchiveName(::PathFindFileNameW(FilePath.c_str())));

                if (IsArchive)
                {
                    NeedExtract = true;
                }
                else
                {
                    OnlyArchives = false;
                }
            }

            std::wstring SpecFolder = L"*";
            if (NeedExtract)
            {
                if (FilePaths.size() == 1)
                {
                    SpecFolder = GetSubFolderNameForExtract(
                        ::PathFindFileNameW(FilePaths[0].c_str()));
                }
                SpecFolder += L'\\';
            }

            FString FolderPrefix;

            std::wstring ArchiveName;
            {
                NWindows::NFile::NFind::CFileInfo FileInfo0;

                const UString& FileName = FileNames.Front();

                if (NWindows::NFile::NName::IsDevicePath(us2fs(FileName)))
                {
                    // CFileInfo::Find can be slow for device files. So we
                    // don't call it.
                    // we need only name here.
                    // change it 4 - must be constant
                    FileInfo0.Name = us2fs(FileName.Ptr(
                        NWindows::NFile::NName::kDevicePathPrefixSize));
                    FolderPrefix = "C:\\";
                }
                else
                {
                    if (!FileInfo0.Find(us2fs(FileName)))
                    {
                        return;
                    }
                    NWindows::NFile::NDir::GetOnlyDirPrefix(
                        us2fs(FileName),
                        FolderPrefix);
                }

                const UString Name = CreateArchiveName(
                    FileNames,
                    FileNames.Size() == 1 ? &FileInfo0 : nullptr);
                ArchiveName = std::wstring(Name.Ptr(), Name.Len());

            }

            std::wstring BaseFolder = std::wstring(
                FolderPrefix.Ptr(),
                FolderPrefix.Len());

            std::wstring ArchiveName7z = ArchiveName + L".7z";
            std::wstring ArchiveNameZip = ArchiveName + L".zip";

            using NanaZip::ShellExtension::ExplorerCommandBase;

            CContextMenuInfo ContextMenuInfo;
            ContextMenuInfo.Load();
            DWORD ContextMenuFlags = ContextMenuInfo.Flags;
            CBoolPair ContextMenuElimDup = ContextMenuInfo.ElimDup;
            UInt32 ContextMenuWriteZone = ContextMenuInfo.WriteZone;

            LoadLangOneTime();

            if (FilePaths.size() == 1 && IsExistingDirectory(FilePaths[0]))
            {
                const std::wstring& target = FilePaths[0];
                this->m_SubCommands.push_back(winrt::make<ExplorerCommandBase>(
                    NewFolderBatchTitle(), CommandID::NewFolderBatch,
                    CBoolPair(), static_cast<UInt32>(-1), target));
                this->m_SubCommands.push_back(winrt::make<ExplorerCommandBase>(
                    NewFolderDateTitle(), CommandID::NewFolderDate,
                    CBoolPair(), static_cast<UInt32>(-1), target));
                if (!IsDriveRootPath(target))
                {
                    this->m_SubCommands.push_back(winrt::make<ExplorerCommandBase>(
                        UnwrapFolderTitle(), CommandID::UnwrapFolder,
                        CBoolPair(), static_cast<UInt32>(-1), target));
                }
                this->m_SubCommands.push_back(winrt::make<ExplorerCommandBase>());
            }
            else if (FilePaths.size() > 1 &&
                SelectionIsOnlyFolders(FilePaths) &&
                !SelectionHasDriveRoot(FilePaths))
            {
                this->m_SubCommands.push_back(winrt::make<ExplorerCommandBase>(
                    UnwrapFolderTitle(), CommandID::UnwrapFolder,
                    CBoolPair(), static_cast<UInt32>(-1), FilePaths[0]));
                this->m_SubCommands.push_back(winrt::make<ExplorerCommandBase>());
            }

            if (NeedExtract)
            {
                if (ContextMenuFlags & NContextMenuFlags::kTest)
                {
                    UString TranslatedString;
                    LangString(IDS_CONTEXT_TEST, TranslatedString);
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>(
                            std::wstring(
                                TranslatedString.Ptr(),
                                TranslatedString.Len()),
                            CommandID::Test));
                }

                if (ContextMenuFlags & NContextMenuFlags::kExtractHere)
                {
                    UString TranslatedString;
                    LangString(IDS_CONTEXT_EXTRACT_HERE, TranslatedString);
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>(
                            std::wstring(
                                TranslatedString.Ptr(),
                                TranslatedString.Len()),
                            CommandID::ExtractHere,
                            ContextMenuElimDup,
                            ContextMenuWriteZone));
                }

                if (ContextMenuFlags & NContextMenuFlags::kExtractHereDelete)
                {
                    UString TranslatedString;
                    LangString(IDS_CONTEXT_EXTRACT_HERE_DELETE, TranslatedString);
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>(
                            std::wstring(
                                TranslatedString.Ptr(),
                                TranslatedString.Len()),
                            CommandID::ExtractHereDelete,
                            ContextMenuElimDup,
                            ContextMenuWriteZone));
                }

                if (ContextMenuFlags & NContextMenuFlags::kExtractHereSmart)
                {
                    UString TranslatedString;
                    LangString(IDS_CONTEXT_EXTRACT_HERE_SMART, TranslatedString);
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>(
                            std::wstring(
                                TranslatedString.Ptr(),
                                TranslatedString.Len()),
                            CommandID::ExtractHereSmart,
                            ContextMenuElimDup,
                            ContextMenuWriteZone));
                }

                if (ContextMenuFlags & NContextMenuFlags::kExtractTo)
                {
                    UString TranslatedString;
                    LangString(IDS_CONTEXT_EXTRACT_TO, TranslatedString);
                    MyFormatNew_ReducedName(TranslatedString, SpecFolder.c_str());
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>(
                            std::wstring(
                                TranslatedString.Ptr(),
                                TranslatedString.Len()),
                            CommandID::ExtractTo,
                            ContextMenuElimDup,
                            ContextMenuWriteZone));
                }

                if (ContextMenuFlags & NContextMenuFlags::kExtractToDelete)
                {
                    UString TranslatedString;
                    LangString(IDS_CONTEXT_EXTRACT_TO_DELETE, TranslatedString);
                    MyFormatNew_ReducedName(TranslatedString, SpecFolder.c_str());
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>(
                            std::wstring(
                                TranslatedString.Ptr(),
                                TranslatedString.Len()),
                            CommandID::ExtractToDelete,
                            ContextMenuElimDup,
                            ContextMenuWriteZone));
                }

                if (ContextMenuFlags & NContextMenuFlags::kExtract)
                {
                    UString TranslatedString;
                    LangString(IDS_CONTEXT_EXTRACT, TranslatedString);
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>(
                            std::wstring(
                                TranslatedString.Ptr(),
                                TranslatedString.Len()),
                            CommandID::Extract,
                            ContextMenuElimDup,
                            ContextMenuWriteZone));
                }
            }

            if (ContextMenuFlags & NContextMenuFlags::kCompressTo7z)
            {
                UString TranslatedString;
                LangString(IDS_CONTEXT_COMPRESS_TO, TranslatedString);
                MyFormatNew_ReducedName(TranslatedString, ArchiveName7z.c_str());
                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        std::wstring(
                            TranslatedString.Ptr(),
                            TranslatedString.Len()),
                        CommandID::CompressTo7z));
            }

            if (!OnlyArchives &&
                (ContextMenuFlags & NContextMenuFlags::kCompressToZip))
            {
                UString TranslatedString;
                LangString(IDS_CONTEXT_COMPRESS_TO, TranslatedString);
                MyFormatNew_ReducedName(TranslatedString, ArchiveNameZip.c_str());
                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        std::wstring(
                            TranslatedString.Ptr(),
                            TranslatedString.Len()),
                        CommandID::CompressToZip));
            }

            if (!OnlyArchives &&
                (ContextMenuFlags & NContextMenuFlags::kCompressToZipDelete))
            {
                UString TranslatedString;
                LangString(IDS_CONTEXT_COMPRESS_TO_DELETE, TranslatedString);
                MyFormatNew_ReducedName(TranslatedString, ArchiveNameZip.c_str());
                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        std::wstring(
                            TranslatedString.Ptr(),
                            TranslatedString.Len()),
                        CommandID::CompressToZipDelete));
            }

            if (SelectionIsOnlyFiles(FilePaths) && PathsShareParent(FilePaths))
            {
                if (!this->m_SubCommands.empty())
                {
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>());
                }
                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        FilesToFolderTitle(),
                        CommandID::FilesToFolder));
            }

            if (ContextMenuFlags & NContextMenuFlags::kCompressEmail)
            {
                UString TranslatedString;
                LangString(IDS_CONTEXT_COMPRESS_EMAIL, TranslatedString);
                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        std::wstring(
                            TranslatedString.Ptr(),
                            TranslatedString.Len()),
                        CommandID::CompressEmail));
            }

            if (ContextMenuFlags & NContextMenuFlags::kCompressTo7zEmail)
            {
                UString TranslatedString;
                LangString(IDS_CONTEXT_COMPRESS_TO_EMAIL, TranslatedString);
                MyFormatNew_ReducedName(TranslatedString, ArchiveName7z.c_str());
                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        std::wstring(
                            TranslatedString.Ptr(),
                            TranslatedString.Len()),
                        CommandID::CompressTo7zEmail));
            }

            if (ContextMenuFlags & NContextMenuFlags::kCompressToZipEmail)
            {
                UString TranslatedString;
                LangString(IDS_CONTEXT_COMPRESS_TO_EMAIL, TranslatedString);
                MyFormatNew_ReducedName(TranslatedString, ArchiveNameZip.c_str());
                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        std::wstring(
                            TranslatedString.Ptr(),
                            TranslatedString.Len()),
                        CommandID::CompressToZipEmail));
            }

            if (ContextMenuFlags & NContextMenuFlags::kCRC)
            {
                if (!this->m_SubCommands.empty())
                {
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>());
                }

                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        L"CRC-32",
                        CommandID::HashCRC32));

                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        L"CRC-64",
                        CommandID::HashCRC64));

                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        L"SHA-1",
                        CommandID::HashSHA1));

                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        L"SHA-256",
                        CommandID::HashSHA256));

                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        L"*",
                        CommandID::HashAll));
            }

            // Folders only: address and name. Any file keeps the three file items.
            const bool onlyFolders = SelectionIsOnlyFolders(FilePaths);
            if (!FilePaths.empty() &&
                !this->m_SubCommands.empty())
            {
                if (!this->m_SubCommands.empty())
                {
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>());
                }
                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        onlyFolders ? CopyFolderAddressTitle() : CopyFileAddressTitle(),
                        CommandID::CopyFileAddress));
                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        onlyFolders ? CopyFolderNameTitle() : CopyFileNameTitle(),
                        CommandID::CopyFileName));
                if (!onlyFolders)
                {
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>(
                            CopyFileNameNoExtTitle(),
                            CommandID::CopyFileNameNoExt));
                }
            }

            // 「新建压缩包...」is the last item, with a separator above it.
            if (!OnlyArchives &&
                (ContextMenuFlags & NContextMenuFlags::kCompress))
            {
                if (!this->m_SubCommands.empty())
                {
                    this->m_SubCommands.push_back(
                        winrt::make<ExplorerCommandBase>());
                }
                UString TranslatedString;
                LangString(IDS_CONTEXT_COMPRESS, TranslatedString);
                this->m_SubCommands.push_back(
                    winrt::make<ExplorerCommandBase>(
                        std::wstring(
                            TranslatedString.Ptr(),
                            TranslatedString.Len()),
                        CommandID::Compress));
            }

            if (!SelectionHasDriveRoot(FilePaths))
            {
                auto Rename = winrt::make<ExplorerCommandBase>(
                    L"重命名" + NanaZip::Renaming::SelectionNoun(FilePaths),
                    CommandID::RenameMenu, CBoolPair(),
                    static_cast<UInt32>(-1), std::wstring(), FilePaths);
                auto Position = this->m_SubCommands.begin() +
                    (std::min)(static_cast<size_t>(2), this->m_SubCommands.size());
                if (Position != this->m_SubCommands.begin())
                {
                    EXPCMDFLAGS PreviousFlags = ECF_DEFAULT;
                    (*(Position - 1))->GetFlags(&PreviousFlags);
                    if (!(PreviousFlags & ECF_ISSEPARATOR))
                        Position = this->m_SubCommands.insert(Position,
                            winrt::make<ExplorerCommandBase>()) + 1;
                }
                Position = this->m_SubCommands.insert(Position, Rename);
                Position = this->m_SubCommands.insert(Position + 1,
                    winrt::make<ExplorerCommandBase>(
                        L"按名单重命名…", CommandID::RenameList, CBoolPair(),
                        static_cast<UInt32>(-1), std::wstring(), FilePaths));
                auto Next = Position + 1;
                EXPCMDFLAGS NextFlags = ECF_DEFAULT;
                if (Next != this->m_SubCommands.end())
                    (*Next)->GetFlags(&NextFlags);
                if (!(NextFlags & ECF_ISSEPARATOR))
                    this->m_SubCommands.insert(Next,
                        winrt::make<ExplorerCommandBase>());
            }
        }

    public:

        explicit ExplorerCommandRoot(bool background = false)
            : m_Background(background)
        {
            CContextMenuInfo ContextMenuInfo;
            ContextMenuInfo.Load();
            this->m_ContextMenuFlags = ContextMenuInfo.Flags;
            this->m_ContextMenuElimDup = ContextMenuInfo.ElimDup;
        }

        HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) noexcept
        {
            this->m_Site = nullptr;
            if (site)
                this->m_Site.copy_from(site);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppvSite) noexcept
        {
            if (!ppvSite)
                return E_POINTER;
            *ppvSite = nullptr;
            if (!this->m_Site)
                return E_FAIL;
            return this->m_Site->QueryInterface(riid, ppvSite);
        }

#pragma region IExplorerCommand

        HRESULT STDMETHODCALLTYPE GetTitle(
            _In_opt_ IShellItemArray* psiItemArray,
            _Outptr_ LPWSTR* ppszName)
        {
            this->Initialize(psiItemArray);

            if (this->m_SubCommands.empty())
            {
                *ppszName = nullptr;
                return E_NOTIMPL;
            }

            return ::SHStrDupW(L"NanaZip", ppszName);
        }

        HRESULT STDMETHODCALLTYPE GetIcon(
            _In_opt_ IShellItemArray* psiItemArray,
            _Outptr_ LPWSTR* ppszIcon)
        {
            UNREFERENCED_PARAMETER(psiItemArray);
            UString Path = ::GetNanaZipPath();
            std::wstring Icon = std::wstring(Path.Ptr(), Path.Len());
            Icon += L",-1";
            return ::SHStrDupW(Icon.c_str(), ppszIcon);
        }

        HRESULT STDMETHODCALLTYPE GetToolTip(
            _In_opt_ IShellItemArray* psiItemArray,
            _Outptr_ LPWSTR* ppszInfotip)
        {
            UNREFERENCED_PARAMETER(psiItemArray);
            *ppszInfotip = nullptr;
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE GetCanonicalName(
            _Out_ GUID* pguidCommandName)
        {
            *pguidCommandName = GUID_NULL;
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE GetState(
            _In_opt_ IShellItemArray* psiItemArray,
            _In_ BOOL fOkToBeSlow,
            _Out_ EXPCMDSTATE* pCmdState)
        {
            UNREFERENCED_PARAMETER(psiItemArray);
            UNREFERENCED_PARAMETER(fOkToBeSlow);
            *pCmdState = ECS_ENABLED;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Invoke(
            _In_opt_ IShellItemArray* psiItemArray,
            _In_opt_ IBindCtx* pbc)
        {
            UNREFERENCED_PARAMETER(psiItemArray);
            UNREFERENCED_PARAMETER(pbc);
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE GetFlags(
            _Out_ EXPCMDFLAGS* pFlags)
        {
            *pFlags = ECF_HASSUBCOMMANDS;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE EnumSubCommands(
            _Outptr_ IEnumExplorerCommand** ppEnum)
        {
            if (this->m_SubCommands.empty())
            {
                *ppEnum = nullptr;
                return E_NOTIMPL;
            }
            else
            {
                this->m_CurrentSubCommand = this->m_SubCommands.cbegin();
                return this->QueryInterface(IID_PPV_ARGS(ppEnum));
            }
        }

#pragma endregion

#pragma region IEnumExplorerCommand

        HRESULT STDMETHODCALLTYPE Next(
            _In_ ULONG celt,
            _Out_ IExplorerCommand** pUICommand,
            _Out_opt_ ULONG* pceltFetched)
        {
            ULONG Fetched = 0;

            for (
                ULONG i = 0;
                (i < celt) &&
                (this->m_CurrentSubCommand != this->m_SubCommands.cend());
                ++i)
            {
                this->m_CurrentSubCommand->copy_to(&pUICommand[i]);
                ++Fetched;
                ++this->m_CurrentSubCommand;
            }

            if (pceltFetched)
            {
                *pceltFetched = Fetched;
            }

            return (Fetched == celt) ? S_OK : S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE Skip(
            _In_ ULONG celt)
        {
            UNREFERENCED_PARAMETER(celt);
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE Reset()
        {
            this->m_CurrentSubCommand = this->m_SubCommands.cbegin();
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Clone(
            _Out_ IEnumExplorerCommand** ppenum)
        {
            *ppenum = nullptr;
            return E_NOTIMPL;
        }

#pragma endregion
    };

    // {8F4C2B71-6A15-4E3D-9C88-1D7A0E5B2F64}
    static const GUID CLSID_BackgroundMenu = {
        0x8f4c2b71, 0x6a15, 0x4e3d, { 0x9c, 0x88, 0x1d, 0x7a, 0x0e, 0x5b, 0x2f, 0x64 }
    };

    struct DECLSPEC_UUID("469D94E9-6AF4-4395-B396-99B1308F8CE5")
        ClassFactory : public winrt::implements<
        ClassFactory, IClassFactory>
    {
    private:
        bool m_Background = false;

    public:

        explicit ClassFactory(bool background = false) noexcept
            : m_Background(background)
        {
        }

        HRESULT STDMETHODCALLTYPE CreateInstance(
            _In_opt_ IUnknown* pUnkOuter,
            _In_ REFIID riid,
            _COM_Outptr_ void** ppvObject) noexcept override
        {
            UNREFERENCED_PARAMETER(pUnkOuter);

            try
            {
                return winrt::make<ExplorerCommandRoot>(this->m_Background)->QueryInterface(
                    riid, ppvObject);
            }
            catch (...)
            {
                return winrt::to_hresult();
            }
        }

        HRESULT STDMETHODCALLTYPE LockServer(
            _In_ BOOL fLock) noexcept override
        {
            if (fLock)
            {
                ++winrt::get_module_lock();
            }
            else
            {
                --winrt::get_module_lock();
            }

            return S_OK;
        }
    };
}

EXTERN_C HRESULT STDAPICALLTYPE DllCanUnloadNow()
{
    if (winrt::get_module_lock())
    {
        return S_FALSE;
    }

    winrt::clear_factory_cache();
    return S_OK;
}

EXTERN_C HRESULT STDAPICALLTYPE DllGetClassObject(
    _In_ REFCLSID rclsid,
    _In_ REFIID riid,
    _Outptr_ LPVOID* ppv)
{
    if (!ppv)
    {
        return E_POINTER;
    }

    if (riid != IID_IClassFactory && riid != IID_IUnknown)
    {
        return E_NOINTERFACE;
    }

    const bool background =
        rclsid == NanaZip::ShellExtension::CLSID_BackgroundMenu;
    if (rclsid != __uuidof(NanaZip::ShellExtension::ClassFactory) &&
        !background)
    {
        return E_INVALIDARG;
    }

    try
    {
        return winrt::make<NanaZip::ShellExtension::ClassFactory>(
            background)->QueryInterface(riid, ppv);
    }
    catch (...)
    {
        return winrt::to_hresult();
    }
}

long g_DllRefCount = 0;
HWND g_HWND = nullptr;
HINSTANCE g_hInstance = nullptr;

// Only used in Shell Extension to check whether NanaZip Modern is loaded.
EXTERN_C HMODULE K7ModernCurrentModule;

BOOL WINAPI DllMain(
    _In_ HINSTANCE hinstDLL,
    _In_ DWORD fdwReason,
    _In_ LPVOID lpvReserved)
{
    UNREFERENCED_PARAMETER(lpvReserved);

    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        g_hInstance = hinstDLL;
        ::K7ModernCurrentModule = hinstDLL;
        break;
    }
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
