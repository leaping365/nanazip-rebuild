// ContextMenu.cpp

#include "StdAfx.h"

// **************** NanaZip Modification Start ****************
#include "../../../../../../NanaZip.UI.Shared/Rename.h"
#include "../../../../../../NanaZip.UI.Shared/DialogAppearance.h"
// **************** NanaZip Modification End ****************

#include <string>
#include <vector>

#include "../../../Common/ComTry.h"
#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"

#include "../../../Windows/COM.h"
#include "../../../Windows/DLL.h"
#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileFind.h"
#include "../../../Windows/FileName.h"
#include "../../../Windows/MemoryGlobal.h"
#include "../../../Windows/Menu.h"
#include "../../../Windows/ProcessUtils.h"
#include "../../../Windows/Shell.h"

#include "../../PropID.h"

#include "../Common/ArchiveName.h"
#include "../Common/CompressCall.h"
#include "../Common/ExtractingFilePath.h"
#include "../Common/ZipRegistry.h"

#include "../FileManager/FormatUtils.h"
#include "../FileManager/PropertyName.h"

#ifdef LANG
#include "../FileManager/LangUtils.h"
#endif

#include "ContextMenu.h"
#include "ContextMenuFlags.h"
#include "MyMessages.h"

#include "resource.h"

// #define SHOW_DEBUG_CTX_MENU

#ifdef SHOW_DEBUG_CTX_MENU
#include <stdio.h>
#endif

using namespace NWindows;
using namespace NFile;
using namespace NDir;

#ifndef UNDER_CE
#define EMAIL_SUPPORT 1
#endif

extern LONG g_DllRefCount;

#ifdef _WIN32
extern HINSTANCE g_hInstance;
#endif
    
#ifdef SHOW_DEBUG_CTX_MENU

void Print_Ptr(void *p, char *s)
{
  char temp[64];
  ConvertUInt64ToHex((UInt64)(void *)p, temp);
  AString s2;
  s2 += temp;
  s2.Add_Space();
  s2 += s;
  OutputDebugStringA(s2);
}

void Print_Number(UInt32 number, char *s)
{
  char temp[64];
  ConvertUInt64ToString(number, temp);
  AString s2;
  s2 += temp;
  s2.Add_Space();
  s2 += s;
  OutputDebugStringA(s2);
}

#define ODS(sz) Print_Ptr(this, sz)
#define ODS_U(s) OutputDebugStringW(s);
// #define ODS(sz)
// #define ODS_U(s)

#define ODS2(sz) Print_Ptr(this, sz)

#else

#define Print_Number(number, s)
#define ODS(sz)
#define ODS_U(s)
#define ODS2(sz)

#endif



CZipContextMenu::CZipContextMenu():
   _isMenuForFM(false),
   _dropMode(false),
   _bitmap(NULL),
   _writeZone((UInt32)(Int32)-1),
   IsSeparator(false),
   IsRoot(true),
   CurrentSubCommand(0)
{
  ODS("-- CZipContextMenu()");

  InterlockedIncrement(&g_DllRefCount);
  _bitmap = ::LoadBitmap(g_hInstance, MAKEINTRESOURCE(IDB_MENU_LOGO));
}

CZipContextMenu::~CZipContextMenu()
{
  ODS("== ~CZipContextMenu");
  if (_bitmap != NULL)
    DeleteObject(_bitmap);
  InterlockedDecrement(&g_DllRefCount);
}

HRESULT CZipContextMenu::GetFileNames(LPDATAOBJECT dataObject, UStringVector &fileNames)
{
  fileNames.Clear();
  if (!dataObject)
    return E_INVALIDARG;

  #ifndef UNDER_CE
  
  FORMATETC fmte = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  NCOM::CStgMedium stgMedium;
  HRESULT result = dataObject->GetData(&fmte, &stgMedium);
  if (result != S_OK)
    return result;
  stgMedium._mustBeReleased = true;

  NShell::CDrop drop(false);
  NMemory::CGlobalLock globalLock(stgMedium->hGlobal);
  drop.Attach((HDROP)globalLock.GetPointer());
  drop.QueryFileNames(fileNames);
  
  #endif

  return S_OK;
}

// IShellExtInit

STDMETHODIMP CZipContextMenu::Initialize(LPCITEMIDLIST pidlFolder, LPDATAOBJECT dataObject, HKEY /* hkeyProgID */)
{
  // OutputDebugString(TEXT("::Initialize\r\n"));
  _dropMode = false;
  _dropPath.Empty();
  if (pidlFolder != 0)
  {
    #ifndef UNDER_CE
    if (NShell::GetPathFromIDList(pidlFolder, _dropPath))
    {
      // OutputDebugString(path);
      // OutputDebugString(TEXT("\r\n"));
      NName::NormalizeDirPathPrefix(_dropPath);
      _dropMode = !_dropPath.IsEmpty();
    }
    else
    #endif
      _dropPath.Empty();
  }

  /*
  m_IsFolder = false;
  if (pidlFolder == 0)
  */
  // pidlFolder is NULL :(
  return GetFileNames(dataObject, _fileNames);
}

HRESULT CZipContextMenu::InitContextMenu(const wchar_t *folder, const wchar_t * const *names, unsigned numFiles)
{
  _isMenuForFM = true;
  _fileNames.Clear();
  _dropMode = false;
  _dropPath.Empty();
  for (UInt32 i = 0; i < numFiles; i++)
  {
    if (names)
      _fileNames.Add(names[i]);
  }
  if (numFiles == 0 && folder && folder[0])
  {
    _dropPath = folder;
    NName::NormalizeDirPathPrefix(_dropPath);
    _dropMode = !_dropPath.IsEmpty();
  }
  return S_OK;
}


/////////////////////////////
// IContextMenu

static LPCSTR const kMainVerb = "SevenZip";
static LPCSTR const kOpenCascadedVerb = "SevenZip.OpenWithType.";
static LPCSTR const kCheckSumCascadedVerb = "SevenZip.Checksum";


struct CContextMenuCommand
{
  UInt32 flag;
  CZipContextMenu::ECommandInternalID CommandInternalID;
  LPCSTR Verb;
  UINT ResourceID;
};

#define CMD_REC(cns, verb, ids)  { NContextMenuFlags::cns, CZipContextMenu::cns, verb, ids }

static const CContextMenuCommand g_Commands[] =
{
  CMD_REC( kOpen,        "Open",        IDS_CONTEXT_OPEN),
  CMD_REC( kExtract,     "Extract",     IDS_CONTEXT_EXTRACT),
  CMD_REC( kExtractHere, "ExtractHere", IDS_CONTEXT_EXTRACT_HERE),
  // **************** NanaZip Modification Start ****************
  CMD_REC( kExtractHereSmart, "ExtractHereSmart", IDS_CONTEXT_EXTRACT_HERE_SMART),
  CMD_REC( kExtractHereDelete, "ExtractHereDelete", IDS_CONTEXT_EXTRACT_HERE_DELETE),
  CMD_REC( kExtractToDelete, "ExtractToDelete", IDS_CONTEXT_EXTRACT_TO_DELETE),
  // **************** NanaZip Modification End ****************
  CMD_REC( kExtractTo,   "ExtractTo",   IDS_CONTEXT_EXTRACT_TO),
  CMD_REC( kTest,        "Test",        IDS_CONTEXT_TEST),
  CMD_REC( kCompress,           "Compress",           IDS_CONTEXT_COMPRESS),
  CMD_REC( kCompressEmail,      "CompressEmail",      IDS_CONTEXT_COMPRESS_EMAIL),
  CMD_REC( kCompressTo7z,       "CompressTo7z",       IDS_CONTEXT_COMPRESS_TO),
  CMD_REC( kCompressTo7zEmail,  "CompressTo7zEmail",  IDS_CONTEXT_COMPRESS_TO_EMAIL),
  CMD_REC( kCompressToZip,      "CompressToZip",      IDS_CONTEXT_COMPRESS_TO),
  CMD_REC( kCompressToZipEmail, "CompressToZipEmail", IDS_CONTEXT_COMPRESS_TO_EMAIL),
  // **************** NanaZip Modification Start ****************
  CMD_REC( kCompressToZipDelete, "CompressToZipDelete", IDS_CONTEXT_COMPRESS_TO_DELETE)
  // **************** NanaZip Modification End ****************
};


struct CHashCommand
{
  CZipContextMenu::ECommandInternalID CommandInternalID;
  LPCSTR UserName;
  LPCSTR MethodName;
};

static const CHashCommand g_HashCommands[] =
{
  { CZipContextMenu::kHash_CRC32,  "CRC-32",  "CRC32" },
  { CZipContextMenu::kHash_CRC64,  "CRC-64",  "CRC64" },
  { CZipContextMenu::kHash_SHA1,   "SHA-1",   "SHA1" },
  { CZipContextMenu::kHash_SHA256, "SHA-256", "SHA256" },
  { CZipContextMenu::kHash_All,    "*",       "*" },
  { CZipContextMenu::kHash_Generate_SHA256, "SHA-256 -> file.sha256", "SHA256" },
  { CZipContextMenu::kHash_TestArc, "Checksum : Test", "Hash" }
};


static int FindCommand(CZipContextMenu::ECommandInternalID &id)
{
  for (unsigned i = 0; i < ARRAY_SIZE(g_Commands); i++)
    if (g_Commands[i].CommandInternalID == id)
      return i;
  return -1;
}


void CZipContextMenu::FillCommand(ECommandInternalID id, UString &mainString, CCommandMapItem &cmi)
{
  mainString.Empty();
  int i = FindCommand(id);
  if (i < 0)
    throw 201908;
  const CContextMenuCommand &command = g_Commands[(unsigned)i];
  cmi.CommandInternalID = command.CommandInternalID;
  cmi.Verb = kMainVerb;
  cmi.Verb += command.Verb;
  // cmi.HelpString = cmi.Verb;
  LangString(command.ResourceID, mainString);
  cmi.UserString = mainString;
  // return true;
}


static UString LangStringAlt(UInt32 id, const char *altString)
{
  UString s = LangString(id);
  if (s.IsEmpty())
    s = altString;
  return s;
}

static const wchar_t *CopyFileAddressTitle()
{
  const LANGID lang = ::GetUserDefaultUILanguage();
  if (PRIMARYLANGID(lang) == LANG_CHINESE)
    return L"复制文件地址";
  return L"Copy file address";
}

static const wchar_t *CopyFileNameTitle()
{
  const LANGID lang = ::GetUserDefaultUILanguage();
  if (PRIMARYLANGID(lang) == LANG_CHINESE)
    return L"复制文件名";
  return L"Copy file name";
}

static const wchar_t *CopyFileNameNoExtTitle()
{
  const LANGID lang = ::GetUserDefaultUILanguage();
  if (PRIMARYLANGID(lang) == LANG_CHINESE)
    return L"复制文件名（无扩展名）";
  return L"Copy file name (no extension)";
}

static const wchar_t *CopyFolderAddressTitle()
{
  const LANGID lang = ::GetUserDefaultUILanguage();
  if (PRIMARYLANGID(lang) == LANG_CHINESE)
    return L"复制文件夹地址";
  return L"Copy folder address";
}

static const wchar_t *CopyFolderNameTitle()
{
  const LANGID lang = ::GetUserDefaultUILanguage();
  if (PRIMARYLANGID(lang) == LANG_CHINESE)
    return L"复制文件夹名";
  return L"Copy folder name";
}

static bool SelectionIsOnlyFolders(const UStringVector &paths)
{
  if (paths.Size() == 0)
    return false;
  for (unsigned i = 0; i < paths.Size(); i++)
  {
    const DWORD attr = ::GetFileAttributesW(paths[i]);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
      return false;
  }
  return true;
}

static bool SelectionIsOnlyFiles(const UStringVector &paths)
{
  if (paths.Size() == 0)
    return false;
  for (unsigned i = 0; i < paths.Size(); i++)
  {
    const DWORD attr = ::GetFileAttributesW(paths[i]);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
      return false;
  }
  return true;
}

static std::wstring ParentDirOfWide(const std::wstring &path)
{
  std::wstring trimmed = path;
  while (!trimmed.empty() && (trimmed.back() == L'\\' || trimmed.back() == L'/'))
    trimmed.pop_back();
  const size_t slash = trimmed.find_last_of(L"\\/");
  if (slash == std::wstring::npos)
    return L"";
  return trimmed.substr(0, slash);
}

static std::wstring BaseNameWide(const std::wstring &path)
{
  const size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos)
    return path;
  return path.substr(slash + 1);
}

static std::wstring NameWithoutLastExtWide(const std::wstring &name)
{
  const size_t dot = name.find_last_of(L'.');
  if (dot == std::wstring::npos || dot == 0)
    return name;
  return name.substr(0, dot);
}

static std::wstring JoinWide(const std::wstring &dir, const std::wstring &name)
{
  std::wstring result = dir;
  if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
    result += L'\\';
  result += name;
  return result;
}

static bool WidePathExists(const std::wstring &path)
{
  return !path.empty() && ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool WideIsDirectory(const std::wstring &path)
{
  const DWORD attr = ::GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static std::vector<std::wstring> ToWidePaths(const UStringVector &paths)
{
  std::vector<std::wstring> result;
  for (unsigned i = 0; i < paths.Size(); i++)
    result.push_back(std::wstring(paths[i].Ptr(), paths[i].Len()));
  return result;
}

static bool WidePathsShareParent(const std::vector<std::wstring> &paths)
{
  if (paths.empty())
    return false;
  const std::wstring parent = ParentDirOfWide(paths[0]);
  for (size_t i = 0; i < paths.size(); i++)
  {
    if (_wcsicmp(ParentDirOfWide(paths[i]).c_str(), parent.c_str()) != 0)
      return false;
  }
  return !parent.empty();
}

static bool IsDriveRootPath(const wchar_t *path)
{
  if (!path || !path[0])
    return false;
  wchar_t root[MAX_PATH] = {};
  if (!::GetVolumePathNameW(path, root, MAX_PATH))
    return false;
  std::wstring left = path;
  std::wstring right = root;
  while (!left.empty() && (left.back() == L'\\' || left.back() == L'/'))
    left.pop_back();
  while (!right.empty() && (right.back() == L'\\' || right.back() == L'/'))
    right.pop_back();
  return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

static bool SelectionHasDriveRoot(const UStringVector &paths)
{
  for (unsigned i = 0; i < paths.Size(); i++)
  {
    if (IsDriveRootPath(paths[i]))
      return true;
  }
  return false;
}

static bool WideSelectionIsNested(const std::vector<std::wstring> &paths)
{
  for (size_t i = 0; i < paths.size(); i++)
  {
    std::wstring prefix = paths[i];
    while (!prefix.empty() && (prefix.back() == L'\\' || prefix.back() == L'/'))
      prefix.pop_back();
    prefix += L'\\';
    for (size_t j = 0; j < paths.size(); j++)
    {
      if (i != j && _wcsnicmp(paths[j].c_str(), prefix.c_str(), prefix.size()) == 0)
        return true;
    }
  }
  return false;
}

static std::wstring TrimWide(std::wstring text)
{
  while (!text.empty() && iswspace(text.front()))
    text.erase(text.begin());
  while (!text.empty() && iswspace(text.back()))
    text.pop_back();
  return text;
}

static bool IsLegalFolderNameWide(const std::wstring &name)
{
  if (name.empty() || name == L"." || name == L"..")
    return false;
  if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos)
    return false;
  if (name.back() == L'.' || name.back() == L' ')
    return false;
  return true;
}

static const wchar_t *FilesToFolderTitle()
{
  const LANGID lang = ::GetUserDefaultUILanguage();
  if (PRIMARYLANGID(lang) == LANG_CHINESE)
    return L"文件归入文件夹";
  return L"Move files into a folder";
}

static const wchar_t *UnwrapFolderTitle()
{
  const LANGID lang = ::GetUserDefaultUILanguage();
  if (PRIMARYLANGID(lang) == LANG_CHINESE)
    return L"去掉外层文件夹";
  return L"Remove enclosing folder";
}

struct NzFolderNameState
{
  std::wstring Text;
  bool Accepted;
  bool Done;
  HWND Edit;
  HFONT Font;
  HICON IconSmall;
  HICON IconBig;
};

static LRESULT CALLBACK NzFolderNameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  NzFolderNameState *state = (NzFolderNameState *)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (msg == NanaZip::DialogAppearance::ActivateMessage)
  {
    NanaZip::DialogAppearance::Activate(hwnd, ::GetDlgItem(hwnd, 100));
    return 0;
  }
  switch (msg)
  {
  case WM_NCCREATE:
  {
    const CREATESTRUCTW *cs = (const CREATESTRUCTW *)lp;
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    return TRUE;
  }
  case WM_CREATE:
    state = (NzFolderNameState *)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    ::CreateWindowExW(0, L"STATIC", L"输入文件夹名",
      WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
      16, 16, 320, 20, hwnd, (HMENU)101, NULL, NULL);
    state->Edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
      16, 42, 320, 24, hwnd, (HMENU)100, NULL, NULL);
    ::CreateWindowExW(0, L"BUTTON", L"确定",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
      176, 80, 76, 28, hwnd, (HMENU)1, NULL, NULL);
    ::CreateWindowExW(0, L"BUTTON", L"取消",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      260, 80, 76, 28, hwnd, (HMENU)2, NULL, NULL);
    NanaZip::DialogAppearance::ApplyFolderPrompt(hwnd, ::GetDpiForWindow(hwnd),
      state->Font, state->IconSmall, state->IconBig);
    return 0;
  case WM_DPICHANGED:
  {
    NanaZip::DialogAppearance::ApplyFolderPrompt(hwnd, HIWORD(wp),
      state->Font, state->IconSmall, state->IconBig);
    const RECT *rectangle = (const RECT *)lp;
    ::SetWindowPos(hwnd, NULL, rectangle->left, rectangle->top, 0, 0,
      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  case WM_COMMAND:
    if (!state)
      break;
    if (LOWORD(wp) == 1)
    {
      wchar_t buffer[260] = {};
      ::GetWindowTextW(state->Edit, buffer, 260);
      state->Text = TrimWide(buffer);
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

static bool AskFolderNameWide(std::wstring &name)
{
  NanaZip::DialogAppearance::ModalWindowScope Modal;
  NanaZip::DialogAppearance::DialogScope Appearance;
  static bool registered = false;
  const wchar_t *className = L"NanaZipClassicFolderNameWnd";
  if (!registered)
  {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = NzFolderNameProc;
    wc.hInstance = ::GetModuleHandleW(NULL);
    wc.hCursor = ::LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    registered = ::RegisterClassW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  }
  if (!registered)
    return false;
  NzFolderNameState state = {};
  RECT rect = { 0, 0, 360, 128 };
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
  ::AdjustWindowRectEx(&rect, style, FALSE, WS_EX_DLGMODALFRAME);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  HWND hwnd = ::CreateWindowExW(
    WS_EX_DLGMODALFRAME, className, L"文件归入文件夹", style,
    (::GetSystemMetrics(SM_CXSCREEN) - width) / 2,
    (::GetSystemMetrics(SM_CYSCREEN) - height) / 2,
    width, height, Modal.Owner(), NULL, ::GetModuleHandleW(NULL), &state);
  if (!hwnd)
    return false;
  Modal.Show(hwnd);
  MSG msg;
  while (!state.Done && ::GetMessageW(&msg, NULL, 0, 0) > 0)
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

static void ShowMoveNotice(const wchar_t *title, const std::wstring &message)
{
  if (!message.empty())
    ::MessageBoxW(NanaZip::DialogAppearance::ForegroundOwner(), message.c_str(), title, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

static std::wstring SkippedMessageWide(const wchar_t *lead, const std::vector<std::wstring> &skipped)
{
  std::wstring message = lead;
  const size_t limit = skipped.size() < 12 ? skipped.size() : 12;
  for (size_t i = 0; i < limit; i++)
  {
    message += L"\r\n";
    message += skipped[i];
  }
  if (skipped.size() > limit)
    message += L"\r\n...";
  return message;
}

static void MoveFilesIntoFolder(const UStringVector &fileNames)
{
  const std::vector<std::wstring> files = ToWidePaths(fileNames);
  if (!SelectionIsOnlyFiles(fileNames) || !WidePathsShareParent(files))
  {
    ShowMoveNotice(FilesToFolderTitle(), L"这些文件不在同一个文件夹里，没有移动。");
    return;
  }
  std::wstring folderName;
  if (files.size() == 1)
    folderName = NameWithoutLastExtWide(BaseNameWide(files[0]));
  else if (!AskFolderNameWide(folderName))
    return;
  folderName = TrimWide(folderName);
  if (!IsLegalFolderNameWide(folderName))
  {
    ShowMoveNotice(FilesToFolderTitle(), L"文件夹名不能包含 \\ / : * ? \" < > | 。");
    return;
  }
  const std::wstring parent = ParentDirOfWide(files[0]);
  const std::wstring destDir = JoinWide(parent, folderName);
  if (WidePathExists(destDir) && !WideIsDirectory(destDir))
  {
    ShowMoveNotice(FilesToFolderTitle(), L"已有同名文件，没有创建文件夹。");
    return;
  }
  const bool created = !WidePathExists(destDir);
  if (created && !::CreateDirectoryW(destDir.c_str(), NULL))
  {
    ShowMoveNotice(FilesToFolderTitle(), L"文件夹没有创建成功。");
    return;
  }
  std::vector<std::wstring> skipped;
  for (size_t i = 0; i < files.size(); i++)
  {
    const std::wstring dest = JoinWide(destDir, BaseNameWide(files[i]));
    if (WidePathExists(dest) || !::MoveFileW(files[i].c_str(), dest.c_str()))
      skipped.push_back(BaseNameWide(files[i]));
  }
  if (created && skipped.size() == files.size())
    ::RemoveDirectoryW(destDir.c_str());
  if (!skipped.empty())
    ShowMoveNotice(FilesToFolderTitle(), SkippedMessageWide(L"以下文件没有移动，避免覆盖：", skipped));
}

static void RemoveEnclosingFolders(const UStringVector &folderNames)
{
  const std::vector<std::wstring> folders = ToWidePaths(folderNames);
  if (!SelectionIsOnlyFolders(folderNames) || SelectionHasDriveRoot(folderNames))
    return;
  if (WideSelectionIsNested(folders))
  {
    ShowMoveNotice(UnwrapFolderTitle(), L"选中的文件夹套在一起，没有移动任何文件。");
    return;
  }
  std::vector<std::wstring> skipped;
  for (size_t i = 0; i < folders.size(); i++)
  {
    const std::wstring parent = ParentDirOfWide(folders[i]);
    if (parent.empty())
      continue;
    std::vector<std::wstring> children;
    WIN32_FIND_DATAW data;
    memset(&data, 0, sizeof(data));
    const HANDLE find = ::FindFirstFileW(JoinWide(folders[i], L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE)
    {
      do
      {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
          continue;
        children.push_back(JoinWide(folders[i], data.cFileName));
      } while (::FindNextFileW(find, &data));
      ::FindClose(find);
    }
    bool allMoved = true;
    for (size_t c = 0; c < children.size(); c++)
    {
      const std::wstring dest = JoinWide(parent, BaseNameWide(children[c]));
      if (WidePathExists(dest) || !::MoveFileW(children[c].c_str(), dest.c_str()))
      {
        allMoved = false;
        skipped.push_back(BaseNameWide(children[c]));
      }
    }
    if (allMoved)
      ::RemoveDirectoryW(folders[i].c_str());
  }
  if (!skipped.empty())
    ShowMoveNotice(UnwrapFolderTitle(), SkippedMessageWide(L"以下内容没有移动，避免覆盖：", skipped));
}

static bool TextHasSpace(const UString &s)
{
  for (unsigned i = 0; i < s.Len(); i++)
    if (s[i] == L' ')
      return true;
  return false;
}

static UString QuoteIfHasSpace(const UString &s)
{
  if (!TextHasSpace(s))
    return s;
  UString q;
  q += L'"';
  q += s;
  q += L'"';
  return q;
}

static UString BaseNameOf(const UString &path)
{
  const int slash = path.ReverseFind_PathSepar();
  if (slash < 0)
    return path;
  return path.Ptr((unsigned)slash + 1);
}

static UString NameWithoutLastExt(const UString &name)
{
  const int dot = name.ReverseFind_Dot();
  if (dot <= 0)
    return name;
  return name.Left((unsigned)dot);
}

static bool CopyUnicodeTextToClipboard(const UString &text)
{
  if (text.IsEmpty())
    return false;
  const SIZE_T bytes = ((SIZE_T)text.Len() + 1) * sizeof(wchar_t);
  HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!mem)
    return false;
  void *dest = ::GlobalLock(mem);
  if (!dest)
  {
    ::GlobalFree(mem);
    return false;
  }
  ::CopyMemory(dest, text.Ptr(), bytes);
  ::GlobalUnlock(mem);
  bool opened = false;
  for (int attempt = 0; attempt < 8; attempt++)
  {
    if (::OpenClipboard(NULL))
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

static void CopySelectedFileText(const UStringVector &paths, int kind)
{
  UString text;
  for (unsigned i = 0; i < paths.Size(); i++)
  {
    UString line = paths[i];
    if (kind != 0)
    {
      line = BaseNameOf(line);
      if (kind == 2)
        line = NameWithoutLastExt(line);
    }
    if (!text.IsEmpty())
      text += L"\r\n";
    text += QuoteIfHasSpace(line);
  }
  CopyUnicodeTextToClipboard(text);
}

static const wchar_t *NewFolderDateTitle()
{
  const LANGID lang = ::GetUserDefaultUILanguage();
  if (PRIMARYLANGID(lang) == LANG_CHINESE)
    return L"以日期新建文件夹";
  return L"New folder by date";
}

static const wchar_t *NewFolderBatchTitle()
{
  const LANGID lang = ::GetUserDefaultUILanguage();
  if (PRIMARYLANGID(lang) == LANG_CHINESE)
    return L"批量新建文件夹";
  return L"Batch new folders";
}

static bool NzPathExists(const wchar_t *path)
{
  return path && path[0] && ::GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static bool NzIsDirectory(const wchar_t *path)
{
  if (!path || !path[0])
    return false;
  const DWORD attr = ::GetFileAttributesW(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static std::wstring NzJoin(const wchar_t *dir, const std::wstring &name)
{
  std::wstring result = dir ? dir : L"";
  if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
    result += L'\\';
  result += name;
  return result;
}

static void CreateDateFolder(const wchar_t *dir)
{
  if (!NzIsDirectory(dir))
    return;
  SYSTEMTIME st;
  memset(&st, 0, sizeof(st));
  ::GetLocalTime(&st);
  wchar_t base[32];
  swprintf_s(base, L"%04u_%02u%02u", st.wYear, st.wMonth, st.wDay);
  std::wstring path = NzJoin(dir, base);
  if (!NzPathExists(path.c_str()))
  {
    ::CreateDirectoryW(path.c_str(), NULL);
    return;
  }
  for (int i = 2; i < 10000; i++)
  {
    path = NzJoin(dir, std::wstring(base) + L"_" + std::to_wstring(i));
    if (!NzPathExists(path.c_str()))
    {
      ::CreateDirectoryW(path.c_str(), NULL);
      return;
    }
  }
}

struct NzBatchDialogState
{
  HWND Edit;
  HWND IconWnd;
  HWND TitleWnd;
  HWND HintWnd;
  HWND HintWnd2;
  HWND OkWnd;
  HWND CancelWnd;
  HFONT Font;
  HFONT HintFont;
  HFONT TitleFont;
  HICON IconHeader;
  HICON IconBig;
  HICON IconSmall;
  COLORREF Accent;
  int Dpi;
  bool Accepted;
  bool Done;
  std::wstring Text;
  NzBatchDialogState()
    : Edit(NULL), IconWnd(NULL), TitleWnd(NULL), HintWnd(NULL), HintWnd2(NULL),
      OkWnd(NULL), CancelWnd(NULL), Font(NULL), HintFont(NULL), TitleFont(NULL),
      IconHeader(NULL), IconBig(NULL), IconSmall(NULL),
      Accent(RGB(0, 103, 192)), Dpi(96), Accepted(false), Done(false) {}
};

static int NzScale(int value, int dpi)
{
  return MulDiv(value, dpi, 96);
}

static HBRUSH NzLightBrush()
{
  static HBRUSH brush = CreateSolidBrush(RGB(243, 243, 243));
  return brush;
}

static HBRUSH NzWhiteBrush()
{
  static HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
  return brush;
}

static COLORREF NzAccent()
{
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM",
      L"AccentColor", RRF_RT_REG_DWORD, NULL, &value, &size) == ERROR_SUCCESS)
    return RGB(value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF);
  return RGB(0, 103, 192);
}

static COLORREF NzShade(COLORREF color, int delta)
{
  int r = GetRValue(color) + delta;
  int g = GetGValue(color) + delta;
  int b = GetBValue(color) + delta;
  if (r < 0) r = 0;
  if (g < 0) g = 0;
  if (b < 0) b = 0;
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return RGB(r, g, b);
}

static HFONT NzUiFont(int dpi, int pixel, int weight)
{
  UNREFERENCED_PARAMETER(pixel);
  return NanaZip::DialogAppearance::CreateFont(dpi, weight);
}

static HICON NzLoadIcon(int cx, int cy)
{
  wchar_t path[MAX_PATH];
  HMODULE module = NULL;
  if (GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          (LPCWSTR)&NzLoadIcon, &module) &&
      GetModuleFileNameW(module, path, MAX_PATH))
  {
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash)
    {
      wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"NanaZip.ico");
      HICON icon = (HICON)LoadImageW(NULL, path, IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
      if (icon)
        return icon;
    }
  }
  return (HICON)LoadImageW(NULL, L"C:\\Program Files\\NanaZip\\NanaZip.ico",
      IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
}

static void NzDrawButton(const DRAWITEMSTRUCT *draw, bool primary, COLORREF accent, HFONT font, int dpi)
{
  RECT rc = draw->rcItem;
  bool pressed = (draw->itemState & ODS_SELECTED) != 0;
  COLORREF fill = primary ? (pressed ? NzShade(accent, -28) : accent) : (pressed ? RGB(232, 232, 232) : RGB(255, 255, 255));
  COLORREF text = primary ? RGB(255, 255, 255) : RGB(26, 26, 26);
  COLORREF border = primary ? fill : RGB(208, 208, 208);
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ oldBrush = SelectObject(draw->hDC, brush);
  HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
  int radius = NzScale(6, dpi);
  RoundRect(draw->hDC, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
  SelectObject(draw->hDC, oldPen);
  SelectObject(draw->hDC, oldBrush);
  DeleteObject(pen);
  DeleteObject(brush);
  wchar_t label[32];
  label[0] = 0;
  GetWindowTextW(draw->hwndItem, label, 32);
  SetBkMode(draw->hDC, TRANSPARENT);
  SetTextColor(draw->hDC, text);
  HGDIOBJ oldFont = SelectObject(draw->hDC, font);
  DrawTextW(draw->hDC, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(draw->hDC, oldFont);
}

static void NzLayoutBatch(HWND hwnd, NzBatchDialogState *state)
{
  RECT rc;
  GetClientRect(hwnd, &rc);
  int dpi = state->Dpi > 0 ? state->Dpi : 96;
  int margin = NzScale(24, dpi);
  int icon = NzScale(32, dpi);
  MoveWindow(state->IconWnd, margin, margin, icon, icon, TRUE);
  MoveWindow(state->TitleWnd, margin + icon + NzScale(12, dpi), margin,
      rc.right - margin * 2 - icon - NzScale(12, dpi), icon, TRUE);
  int hintY = margin + icon + NzScale(8, dpi);
  int lineH = NzScale(18, dpi);
  int hintH = lineH * 2;
  MoveWindow(state->HintWnd, margin, hintY, rc.right - margin * 2, lineH, TRUE);
  MoveWindow(state->HintWnd2, margin, hintY + lineH, rc.right - margin * 2, lineH, TRUE);
  int buttonW = NzScale(112, dpi);
  int buttonH = NzScale(36, dpi);
  int buttonY = rc.bottom - margin - buttonH;
  MoveWindow(state->CancelWnd, rc.right - margin - buttonW, buttonY, buttonW, buttonH, TRUE);
  MoveWindow(state->OkWnd, rc.right - margin - buttonW * 2 - NzScale(12, dpi), buttonY, buttonW, buttonH, TRUE);
  int editY = hintY + hintH + NzScale(12, dpi);
  int editH = buttonY - NzScale(16, dpi) - editY;
  if (editH < NzScale(160, dpi))
    editH = NzScale(160, dpi);
  int border = NzScale(1, dpi);
  MoveWindow(state->Edit, margin + border, editY + border,
      rc.right - margin * 2 - border * 2, editH - border * 2, TRUE);
  int pad = NzScale(10, dpi);
  SendMessageW(state->Edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(pad, pad));
}

static void NzApplyChrome(HWND hwnd, NzBatchDialogState *state, int dpi)
{
  state->Dpi = dpi;
  state->Accent = NzAccent();
  HFONT oldFont = state->Font;
  HFONT oldTitle = state->TitleFont;
  state->Font = NzUiFont(dpi, 14, FW_NORMAL);
  HFONT oldHint = state->HintFont;
  state->HintFont = NzUiFont(dpi, 12, FW_NORMAL);
  state->TitleFont = NzUiFont(dpi, 18, FW_SEMIBOLD);
  SendMessageW(state->TitleWnd, WM_SETFONT, (WPARAM)state->TitleFont, TRUE);
  SendMessageW(state->HintWnd, WM_SETFONT, (WPARAM)state->HintFont, TRUE);
  if (state->HintWnd2)
    SendMessageW(state->HintWnd2, WM_SETFONT, (WPARAM)state->HintFont, TRUE);
  SendMessageW(state->Edit, WM_SETFONT, (WPARAM)state->Font, TRUE);
  SendMessageW(state->OkWnd, WM_SETFONT, (WPARAM)state->Font, TRUE);
  SendMessageW(state->CancelWnd, WM_SETFONT, (WPARAM)state->Font, TRUE);
  if (oldFont)
    DeleteObject(oldFont);
  if (oldHint)
    DeleteObject(oldHint);
  if (oldTitle)
    DeleteObject(oldTitle);
  HICON oldHeader = state->IconHeader;
  HICON oldBig = state->IconBig;
  HICON oldSmallIcon = state->IconSmall;
  state->IconHeader = NzLoadIcon(NzScale(32, dpi), NzScale(32, dpi));
  state->IconBig = NzLoadIcon(NzScale(32, dpi), NzScale(32, dpi));
  state->IconSmall = NzLoadIcon(NzScale(16, dpi), NzScale(16, dpi));
  SetWindowTextW(hwnd, L"批量新建文件夹");
  if (state->IconBig)
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)state->IconBig);
  if (state->IconSmall)
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)state->IconSmall);
  if (state->IconWnd)
    InvalidateRect(state->IconWnd, NULL, TRUE);
  if (oldHeader)
    DestroyIcon(oldHeader);
  if (oldBig)
    DestroyIcon(oldBig);
  if (oldSmallIcon)
    DestroyIcon(oldSmallIcon);
}

static LRESULT CALLBACK NzBatchDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  NzBatchDialogState *state = (NzBatchDialogState *)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (msg == NanaZip::DialogAppearance::ActivateMessage)
  {
    NanaZip::DialogAppearance::Activate(hwnd, ::GetDlgItem(hwnd, 100));
    return 0;
  }
  switch (msg)
  {
  case WM_NCCREATE:
  {
    CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    return TRUE;
  }
  case WM_CREATE:
  {
    state = (NzBatchDialogState *)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    state->IconWnd = ::CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 10, 10, hwnd, (HMENU)101, ::GetModuleHandleW(NULL), NULL);
    state->TitleWnd = ::CreateWindowExW(0, L"STATIC", L"批量新建文件夹",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0, 0, 10, 10, hwnd, (HMENU)102, ::GetModuleHandleW(NULL), NULL);
    state->HintWnd = ::CreateWindowExW(0, L"STATIC", L"一行一个文件夹名，可以直接输入或粘贴。",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, hwnd, (HMENU)103, ::GetModuleHandleW(NULL), NULL);
    state->HintWnd2 = ::CreateWindowExW(0, L"STATIC", L"Ctrl+回车创建。",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, hwnd, (HMENU)104, ::GetModuleHandleW(NULL), NULL);
    state->Edit = ::CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0, 0, 10, 10, hwnd, (HMENU)100, ::GetModuleHandleW(NULL), NULL);
    state->OkWnd = ::CreateWindowExW(0, L"BUTTON", L"确定",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hwnd, (HMENU)1, ::GetModuleHandleW(NULL), NULL);
    state->CancelWnd = ::CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hwnd, (HMENU)2, ::GetModuleHandleW(NULL), NULL);
    int dpi = state->Dpi > 0 ? state->Dpi : 96;
    typedef UINT (WINAPI *GetDpiFn)(HWND);
    GetDpiFn getDpi = (GetDpiFn)::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (getDpi)
    {
      UINT windowDpi = getDpi(hwnd);
      if (windowDpi > 0)
        dpi = (int)windowDpi;
    }
    NzApplyChrome(hwnd, state, dpi);
    NzLayoutBatch(hwnd, state);
    return 0;
  }
  case WM_SIZE:
    if (state)
      NzLayoutBatch(hwnd, state);
    return 0;
  case WM_ERASEBKGND:
  {
    RECT rc;
    ::GetClientRect(hwnd, &rc);
    ::FillRect((HDC)wp, &rc, NzLightBrush());
    return 1;
  }
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC dc = ::BeginPaint(hwnd, &ps);
    if (state && state->Edit)
    {
      RECT rc;
      ::GetWindowRect(state->Edit, &rc);
      ::MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&rc, 2);
      int border = NzScale(1, state->Dpi > 0 ? state->Dpi : 96);
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
    const DRAWITEMSTRUCT *draw = (const DRAWITEMSTRUCT *)lp;
    if (!state || !draw)
      break;
    if (draw->hwndItem == state->IconWnd && state->IconHeader)
    {
      int width = draw->rcItem.right - draw->rcItem.left;
      int height = draw->rcItem.bottom - draw->rcItem.top;
      ::FillRect(draw->hDC, &draw->rcItem, NzLightBrush());
      ::DrawIconEx(draw->hDC, 0, 0, state->IconHeader, width, height, 0, NULL, DI_NORMAL);
      return TRUE;
    }
    if (draw->hwndItem == state->OkWnd || draw->hwndItem == state->CancelWnd)
    {
      NzDrawButton(draw, draw->hwndItem == state->OkWnd, state->Accent, state->Font, state->Dpi);
      return TRUE;
    }
    break;
  }
  case WM_CTLCOLORSTATIC:
  {
    HDC dc = (HDC)wp;
    ::SetBkMode(dc, TRANSPARENT);
    if (state && ((HWND)lp == state->HintWnd || (HWND)lp == state->HintWnd2))
      ::SetTextColor(dc, RGB(96, 96, 96));
    else
      ::SetTextColor(dc, RGB(26, 26, 26));
    return (LRESULT)NzLightBrush();
  }
  case WM_CTLCOLOREDIT:
  {
    HDC dc = (HDC)wp;
    ::SetBkColor(dc, RGB(255, 255, 255));
    ::SetTextColor(dc, RGB(26, 26, 26));
    return (LRESULT)NzWhiteBrush();
  }
  case WM_COMMAND:
    if (!state)
      return 0;
    if (LOWORD(wp) == 1)
    {
      int len = ::GetWindowTextLengthW(state->Edit);
      state->Text.assign((size_t)len, L'\0');
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
        ::DeleteObject(state->Font);
      if (state->HintFont)
        ::DeleteObject(state->HintFont);
      if (state->TitleFont)
        ::DeleteObject(state->TitleFont);
      if (state->IconHeader)
        ::DestroyIcon(state->IconHeader);
      if (state->IconBig)
        ::DestroyIcon(state->IconBig);
      if (state->IconSmall)
        ::DestroyIcon(state->IconSmall);
      state->Font = NULL;
      state->HintFont = NULL;
      state->TitleFont = NULL;
      state->IconHeader = NULL;
      state->IconBig = NULL;
      state->IconSmall = NULL;
    }
    return 0;
  }
  return ::DefWindowProcW(hwnd, msg, wp, lp);
}

static bool ShowBatchFolderDialog(std::wstring &text)
{
  NanaZip::DialogAppearance::ModalWindowScope Modal;
  NanaZip::DialogAppearance::DialogScope Appearance;
  static bool registered = false;
  const wchar_t *className = L"NanaZipBatchNewFolderWnd";
  if (!registered)
  {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = NzBatchDialogProc;
    wc.hInstance = ::GetModuleHandleW(NULL);
    wc.hCursor = ::LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NzLightBrush();
    wc.lpszClassName = className;
    registered = ::RegisterClassW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  }
  if (!registered)
    return false;
  typedef HANDLE (WINAPI *SetDpiFn)(HANDLE);
  SetDpiFn setDpi = (SetDpiFn)::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext");
  HANDLE oldContext = NULL;
  if (setDpi)
    oldContext = setDpi((HANDLE)(INT_PTR)-4);
  NzBatchDialogState state;
  HDC screen = ::GetDC(NULL);
  int dpi = screen ? ::GetDeviceCaps(screen, LOGPIXELSX) : 96;
  if (screen)
    ::ReleaseDC(NULL, screen);
  if (dpi <= 0)
    dpi = 96;
  state.Dpi = dpi;
  RECT rect = { 0, 0, NzScale(680, dpi), NzScale(460, dpi) };
  ::AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  const int x = (::GetSystemMetrics(SM_CXSCREEN) - width) / 2;
  const int y = (::GetSystemMetrics(SM_CYSCREEN) - height) / 2;
  HWND hwnd = ::CreateWindowExW(
      WS_EX_DLGMODALFRAME, className, L"批量新建文件夹",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_CLIPCHILDREN,
      x, y, width, height, Modal.Owner(), NULL, ::GetModuleHandleW(NULL), &state);
  if (setDpi && oldContext)
    setDpi(oldContext);
  if (!hwnd)
    return false;
  Modal.Show(hwnd);
  MSG msg;
  while (!state.Done && ::GetMessageW(&msg, NULL, 0, 0) > 0)
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

static std::wstring NzTrim(const std::wstring &text)
{
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && (text[begin] == L' ' || text[begin] == L'\t' || text[begin] == L'\r'))
    begin++;
  while (end > begin && (text[end - 1] == L' ' || text[end - 1] == L'\t' || text[end - 1] == L'\r'))
    end--;
  return text.substr(begin, end - begin);
}

static bool NzIllegalName(const std::wstring &name)
{
  if (name.empty() || name == L"." || name == L"..")
    return true;
  return name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos;
}

static void CreateBatchFolders(const wchar_t *dir)
{
  if (!NzIsDirectory(dir))
    return;
  std::wstring text;
  if (!ShowBatchFolderDialog(text))
    return;
  std::wstring skipped;
  int skippedCount = 0;
  size_t pos = 0;
  while (pos <= text.size())
  {
    size_t next = text.find(L'\n', pos);
    std::wstring raw = text.substr(pos, next == std::wstring::npos ? std::wstring::npos : next - pos);
    pos = next == std::wstring::npos ? text.size() + 1 : next + 1;
    std::wstring name = NzTrim(raw);
    if (name.empty())
      continue;
    const wchar_t *reason = NULL;
    std::wstring full = NzJoin(dir, name);
    if (NzIllegalName(name))
      reason = L"非法字符";
    else if (NzPathExists(full.c_str()))
      reason = L"已存在";
    else if (!::CreateDirectoryW(full.c_str(), NULL))
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
      skippedCount++;
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

  
void CZipContextMenu::AddCommand(ECommandInternalID id, UString &mainString, CCommandMapItem &cmi)
{
  FillCommand(id, mainString, cmi);
  _commandMap.Add(cmi);
}


static void MyInsertMenu(CMenu &menu, int pos, UINT id, const UString &s, HBITMAP bitmap)
{
  if (!menu)
    return;
  CMenuItem mi;
  mi.fType = MFT_STRING;
  mi.fMask = MIIM_TYPE | MIIM_ID;
  if (bitmap)
    mi.fMask |= MIIM_CHECKMARKS;
  mi.wID = id;
  mi.StringValue = s;
  mi.hbmpUnchecked = bitmap;
  // mi.hbmpChecked = bitmap; // do we need hbmpChecked ???
  if (!menu.InsertItem(pos, true, mi))
    throw 20190816;

  // SetMenuItemBitmaps also works
  // ::SetMenuItemBitmaps(menu, pos, MF_BYPOSITION, bitmap, NULL);
}


static void MyAddSubMenu(
    CObjectVector<CZipContextMenu::CCommandMapItem> &_commandMap,
    const char *verb,
    CMenu &menu, int pos, UINT id, const UString &s, HMENU hSubMenu, HBITMAP bitmap)
{
  CZipContextMenu::CCommandMapItem cmi;
  cmi.CommandInternalID = CZipContextMenu::kCommandNULL;
  cmi.Verb = verb;
  cmi.IsPopup = true;
  // cmi.HelpString = verb;
  cmi.UserString = s;
  _commandMap.Add(cmi);

  if (!menu)
    return;

  CMenuItem mi;
  mi.fType = MFT_STRING;
  mi.fMask = MIIM_SUBMENU | MIIM_TYPE | MIIM_ID;
  if (bitmap)
    mi.fMask |= MIIM_CHECKMARKS;
  mi.wID = id;
  mi.hSubMenu = hSubMenu;
  mi.hbmpUnchecked = bitmap;
  
  mi.StringValue = s;
  if (!menu.InsertItem(pos, true, mi))
    throw 20190817;
}


static const char * const kArcExts[] =
{
    "7z"
  , "bz2"
  , "gz"
  , "rar"
  , "zip"
};

static bool IsItArcExt(const UString &ext)
{
  for (unsigned i = 0; i < ARRAY_SIZE(kArcExts); i++)
    if (ext.IsEqualTo_Ascii_NoCase(kArcExts[i]))
      return true;
  return false;
}

UString GetSubFolderNameForExtract(const UString &arcName);
UString GetSubFolderNameForExtract(const UString &arcName)
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
          (  ext2.IsEqualTo_Ascii_NoCase("part001")
          || ext2.IsEqualTo_Ascii_NoCase("part01")
          || ext2.IsEqualTo_Ascii_NoCase("part1"))))
      res.DeleteFrom(dotPos);
    res.TrimRight();
  }
  return Get_Correct_FsFile_Name(res);
}

static void ReduceString(UString &s)
{
  // **************** NanaZip Modification Start ****************
  // const unsigned kMaxSize = 64;
  const unsigned kMaxSize = 60;
  // **************** NanaZip Modification End ****************
  if (s.Len() <= kMaxSize)
    return;
  s.Delete(kMaxSize / 2, s.Len() - kMaxSize);
  s.Insert(kMaxSize / 2, L" ... ");
}

static UString GetQuotedReducedString(const UString &s)
{
  UString s2 = s;
  ReduceString(s2);
  s2.Replace(L"&", L"&&");
  return GetQuotedString(s2);
}

static void MyFormatNew_ReducedName(UString &s, const UString &name)
{
  s = MyFormatNew(s, GetQuotedReducedString(name));
}

static const char * const kExtractExcludeExtensions =
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
  " wav wma wv"
  " xml xsd xsl xslt"
  " ";

/*
static const char * const kNoOpenAsExtensions =
  " 7z arj bz2 cab chm cpio flv gz lha lzh lzma rar swm tar tbz2 tgz wim xar xz z zip ";
*/

static const char * const kOpenTypes[] =
{
    ""
  , "*"
  , "#"
  , "#:e"
  // , "#:a"
  , "7z"
  , "zip"
  , "cab"
  , "rar"
};

static bool FindExt(const char *p, const FString &name)
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
    if (s.Len() == j - i && memcmp(p + i, (const char *)s, s.Len()) == 0)
      return true;
    i = j + 1;
  }
  
  return false;
}

static bool DoNeedExtract(const FString &name)
{
  return !FindExt(kExtractExcludeExtensions, name);
}

// Real archive types. .exe/.msi installers stay compressible.
static const char * const kRecognizedArchiveExtensions =
  " 7z zip zipx rar r00 cab iso tar tgz tbz tbz2 txz"
  " gz bz2 xz zst lz liz lz4 lz5 lzma lzh lha arj z cpio wim swm xar"
  " ";

static bool IsRecognizedArchiveName(const FString &name)
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

// we must use diferent Verbs for Popup subMenu.
void CZipContextMenu::AddMapItem_ForSubMenu(const char *verb)
{
  CCommandMapItem cmi;
  cmi.CommandInternalID = kCommandNULL;
  cmi.Verb = verb;
  // cmi.HelpString = verb;
  _commandMap.Add(cmi);
}


static HRESULT RETURN_WIN32_LastError_AS_HRESULT()
{
  DWORD lastError = ::GetLastError();
  if (lastError == 0)
    return E_FAIL;
  return HRESULT_FROM_WIN32(lastError);
}


/*
  we add CCommandMapItem to _commandMap for each new Menu ID.
  so then we use _commandMap[offset].
  That way we can execute commands that have menu item.
  Another non-implemented way:
    We can return the number off all possible commands in QueryContextMenu().
    so the caller could call InvokeCommand() via string verb even
    without using menu items.
*/
    


STDMETHODIMP CZipContextMenu::QueryContextMenu(HMENU hMenu, UINT indexMenu,
      UINT commandIDFirst, UINT commandIDLast, UINT flags)
{
  ODS("+ QueryContextMenu()");
  COM_TRY_BEGIN
  try {

  #ifdef SHOW_DEBUG_CTX_MENU
  { char s[256]; sprintf(s, "QueryContextMenu: index=%d first=%d last=%d flags=%x _files=%d",
    indexMenu, commandIDFirst, commandIDLast, flags, _fileNames.Size()); OutputDebugStringA(s); }

  /*
  for (UInt32 i = 0; i < _fileNames.Size(); i++)
  {
    OutputDebugStringW(_fileNames[i]);
  }
  */
  #endif

  LoadLangOneTime();
  
  if (_fileNames.Size() == 0 && _dropPath.IsEmpty())
  {
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    // return E_INVALIDARG;
  }

  if (commandIDFirst > commandIDLast)
    return E_INVALIDARG;


  UINT currentCommandID = commandIDFirst;
  
  if ((flags & 0x000F) != CMF_NORMAL
      && (flags & CMF_VERBSONLY) == 0
      && (flags & CMF_EXPLORE) == 0)
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, currentCommandID - commandIDFirst);
  // return MAKE_HRESULT(SEVERITY_SUCCESS, 0, currentCommandID);
  // 19.01 : we changed from (currentCommandID) to (currentCommandID - commandIDFirst)
  // why it was so before?

  _commandMap.Clear();

  CMenu popupMenu;
  CMenuDestroyer menuDestroyer;

  CContextMenuInfo ci;
  ci.Load();

  _elimDup = ci.ElimDup;
  _writeZone = ci.WriteZone;

  HBITMAP bitmap = NULL;
  if (ci.MenuIcons.Val)
    bitmap = _bitmap;

  UINT subIndex = indexMenu;

  ODS("### 50");
  
  if (ci.Cascaded.Val)
  {
    if (hMenu)
    if (!popupMenu.CreatePopup())
      return RETURN_WIN32_LastError_AS_HRESULT();
    menuDestroyer.Attach(popupMenu);

    /* 9.31: we commented the following code. Probably we don't need.
    Check more systems. Maybe it was for old Windows? */
    /*
    AddMapItem_ForSubMenu();
    currentCommandID++;
    */
    subIndex = 0;
  }
  else
  {
    popupMenu.Attach(hMenu);
    CMenuItem mi;
    mi.fType = MFT_SEPARATOR;
    mi.fMask = MIIM_TYPE;
    if (hMenu)
    popupMenu.InsertItem(subIndex++, true, mi);
  }

  UString newFolderTarget;
  bool showNewFolder = false;
  bool backgroundOnly = false;
  if (_fileNames.Size() == 0 && !_dropPath.IsEmpty())
  {
    newFolderTarget = _dropPath;
    showNewFolder = true;
    backgroundOnly = true;
  }
  else if (_fileNames.Size() == 1 && NzIsDirectory(_fileNames[0]))
  {
    newFolderTarget = _fileNames[0];
    showNewFolder = true;
  }

  if (showNewFolder && currentCommandID + 1 < commandIDLast)
  {
    const CZipContextMenu::ECommandInternalID ids[2] = {
      kNewFolderBatch, kNewFolderDate
    };
    const wchar_t *titles[2] = {
      NewFolderBatchTitle(), NewFolderDateTitle()
    };
    const char *verbs[2] = {
      ".NewFolderBatch", ".NewFolderDate"
    };
    for (unsigned i = 0; i < 2; i++)
    {
      CCommandMapItem cmi;
      cmi.CommandInternalID = ids[i];
      cmi.Verb = kMainVerb;
      cmi.Verb += verbs[i];
      cmi.UserString = titles[i];
      cmi.Folder = newFolderTarget;
      _commandMap.Add(cmi);
      MyInsertMenu(popupMenu, subIndex++, currentCommandID++, cmi.UserString, bitmap);
    }
    if (!backgroundOnly && !IsDriveRootPath(newFolderTarget) &&
        currentCommandID < commandIDLast)
    {
      CCommandMapItem cmi;
      cmi.CommandInternalID = kUnwrapFolder;
      cmi.Verb = kMainVerb;
      cmi.Verb += ".UnwrapFolder";
      cmi.UserString = UnwrapFolderTitle();
      cmi.Folder = newFolderTarget;
      _commandMap.Add(cmi);
      MyInsertMenu(popupMenu, subIndex++, currentCommandID++, cmi.UserString, bitmap);
    }
    if (!backgroundOnly && popupMenu)
    {
      CMenuItem sep;
      sep.fType = MFT_SEPARATOR;
      sep.fMask = MIIM_TYPE;
      popupMenu.InsertItem(subIndex++, true, sep);
    }
  }
  else if (!backgroundOnly && SelectionIsOnlyFolders(_fileNames) &&
      !SelectionHasDriveRoot(_fileNames) && currentCommandID < commandIDLast)
  {
    CCommandMapItem cmi;
    cmi.CommandInternalID = kUnwrapFolder;
    cmi.Verb = kMainVerb;
    cmi.Verb += ".UnwrapFolder";
    cmi.UserString = UnwrapFolderTitle();
    _commandMap.Add(cmi);
    MyInsertMenu(popupMenu, subIndex++, currentCommandID++, cmi.UserString, bitmap);
    if (popupMenu)
    {
      CMenuItem sep;
      sep.fType = MFT_SEPARATOR;
      sep.fMask = MIIM_TYPE;
      popupMenu.InsertItem(subIndex++, true, sep);
    }
  }

  if (backgroundOnly)
  {
    if (ci.Cascaded.Val)
    {
      CMenu menu;
      menu.Attach(hMenu);
      menuDestroyer.Disable();
      MyAddSubMenu(_commandMap, kMainVerb, menu, indexMenu++, currentCommandID++, (UString)"NanaZip",
          popupMenu, bitmap);
    }
    else
      indexMenu = subIndex;
    popupMenu.Detach();
    if (_commandMap.Size() != currentCommandID - commandIDFirst)
      throw 20190818;
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, currentCommandID - commandIDFirst);
  }

  UInt32 contextMenuFlags = ci.Flags;

  NFind::CFileInfo fi0;
  FString folderPrefix;
  
  if (_fileNames.Size() > 0)
  {
    const UString &fileName = _fileNames.Front();

    #if defined(_WIN32) && !defined(UNDER_CE)
    if (NName::IsDevicePath(us2fs(fileName)))
    {
      // CFileInfo::Find can be slow for device files. So we don't call it.
      // we need only name here.
      fi0.Name = us2fs(fileName.Ptr(NName::kDevicePathPrefixSize)); // change it 4 - must be constant
      folderPrefix =
        #ifdef UNDER_CE
          "\\";
        #else
          "C:\\";
        #endif
    }
    else
    #endif
    {
      if (!fi0.Find(us2fs(fileName)))
      {
        throw 20190820;
        // return RETURN_WIN32_LastError_AS_HRESULT();
      }
      GetOnlyDirPrefix(us2fs(fileName), folderPrefix);
    }
  }

  ODS("### 100");

  UString mainString;
  
  if (_fileNames.Size() == 1 && currentCommandID + 14 <= commandIDLast)
  {
    if (!fi0.IsDir() && DoNeedExtract(fi0.Name))
    {
      // kOpen stays cleared, so the old "Open archive" item is not shown here.
      bool thereIsMainOpenItem = ((contextMenuFlags & NContextMenuFlags::kOpen) != 0);
      if ((contextMenuFlags & NContextMenuFlags::kOpenAs) != 0
          // && (!thereIsMainOpenItem || !FindExt(kNoOpenAsExtensions, fi0.Name))
          && hMenu // we want to reduce number of menu items below 16
          )
      {
        CMenu subMenu;
        if (!hMenu || subMenu.CreatePopup())
        {
          MyAddSubMenu(_commandMap, kOpenCascadedVerb, popupMenu, subIndex++, currentCommandID++, LangString(IDS_CONTEXT_OPEN), subMenu, bitmap);
          _commandMap.Back().CtxCommandType = CtxCommandType_OpenRoot;

          UINT subIndex2 = 0;
          for (unsigned i = (thereIsMainOpenItem ? 1 : 0); i < ARRAY_SIZE(kOpenTypes); i++)
          {
            CCommandMapItem cmi;
            if (i == 0)
              FillCommand(kOpen, mainString, cmi);
            else
            {
              mainString = kOpenTypes[i];
              cmi.CommandInternalID = kOpen;
              cmi.Verb = kMainVerb;
              cmi.Verb += ".Open.";
              cmi.Verb += mainString;
              // cmi.HelpString = cmi.Verb;
              cmi.ArcType = mainString;
              cmi.CtxCommandType = CtxCommandType_OpenChild;
            }
            _commandMap.Add(cmi);
            Set_UserString_in_LastCommand(mainString);
            MyInsertMenu(subMenu, subIndex2++, currentCommandID++, mainString, bitmap);
          }

          subMenu.Detach();
        }
      }
    }
  }

  if (_fileNames.Size() > 0 && currentCommandID + 10 <= commandIDLast)
  {
    bool needExtract = (!fi0.IsDir() && DoNeedExtract(fi0.Name));
    
    if (!needExtract)
    {
      for (unsigned i = 1; i < _fileNames.Size(); i++)
      {
        NFind::CFileInfo fi;
        if (!fi.Find(us2fs(_fileNames[i])))
        {
          throw 20190821;
          // return RETURN_WIN32_LastError_AS_HRESULT();
        }
        if (!fi.IsDir() && DoNeedExtract(fi.Name))
        {
          needExtract = true;
          break;
        }
      }
    }
    
    // const UString &fileName = _fileNames.Front();
    
    if (needExtract)
    {
      {
        UString baseFolder = fs2us(folderPrefix);
        if (_dropMode)
          baseFolder = _dropPath;
    
        UString specFolder ('*');
        if (_fileNames.Size() == 1)
          specFolder = GetSubFolderNameForExtract(fs2us(fi0.Name));
        specFolder.Add_PathSepar();

        if ((contextMenuFlags & NContextMenuFlags::kTest) != 0)
        {
          CCommandMapItem cmi;
          AddCommand(kTest, mainString, cmi);
          MyInsertMenu(popupMenu, subIndex++, currentCommandID++, mainString, bitmap);
        }

        if ((contextMenuFlags & NContextMenuFlags::kExtractHere) != 0)
        {
          // Extract Here
          CCommandMapItem cmi;
          cmi.Folder = baseFolder;
          AddCommand(kExtractHere, mainString, cmi);
          MyInsertMenu(popupMenu, subIndex++, currentCommandID++, mainString, bitmap);
        }

        // **************** NanaZip Modification Start ****************
        if ((contextMenuFlags & NContextMenuFlags::kExtractHereDelete) != 0)
        {
          CCommandMapItem cmi;
          cmi.Folder = baseFolder;
          AddCommand(kExtractHereDelete, mainString, cmi);
          MyInsertMenu(popupMenu, subIndex++, currentCommandID++, mainString, bitmap);
        }
        // **************** NanaZip Modification End ****************

        // **************** NanaZip Modification Start ****************
        if ((contextMenuFlags & NContextMenuFlags::kExtractHereSmart) != 0)
        {
          // Extract Here
          CCommandMapItem cmi;
          cmi.Folder = baseFolder;
          AddCommand(kExtractHereSmart, mainString, cmi);
          MyInsertMenu(popupMenu, subIndex++, currentCommandID++, mainString, bitmap);
        }
        // **************** NanaZip Modification End ****************

        if ((contextMenuFlags & NContextMenuFlags::kExtractTo) != 0)
        {
          // Extract To
          CCommandMapItem cmi;
          UString s;
          cmi.Folder = baseFolder + specFolder;
          AddCommand(kExtractTo, s, cmi);
          MyFormatNew_ReducedName(s, specFolder);
          Set_UserString_in_LastCommand(s);
          MyInsertMenu(popupMenu, subIndex++, currentCommandID++, s, bitmap);
        }

        // **************** NanaZip Modification Start ****************
        if ((contextMenuFlags & NContextMenuFlags::kExtractToDelete) != 0)
        {
          CCommandMapItem cmi;
          UString s;
          cmi.Folder = baseFolder + specFolder;
          AddCommand(kExtractToDelete, s, cmi);
          MyFormatNew_ReducedName(s, specFolder);
          Set_UserString_in_LastCommand(s);
          MyInsertMenu(popupMenu, subIndex++, currentCommandID++, s, bitmap);
        }

        if ((contextMenuFlags & NContextMenuFlags::kExtract) != 0)
        {
          CCommandMapItem cmi;
          cmi.Folder = baseFolder + specFolder;
          AddCommand(kExtract, mainString, cmi);
          MyInsertMenu(popupMenu, subIndex++, currentCommandID++, mainString, bitmap);
        }
        // **************** NanaZip Modification End ****************
      }
    }
    
    bool onlyArchives = (_fileNames.Size() > 0 && !fi0.IsDir() && IsRecognizedArchiveName(fi0.Name));
    if (onlyArchives)
    {
      for (unsigned i = 1; i < _fileNames.Size(); i++)
      {
        NFind::CFileInfo fi;
        if (!fi.Find(us2fs(_fileNames[i])) || fi.IsDir() || !IsRecognizedArchiveName(fi.Name))
        {
          onlyArchives = false;
          break;
        }
      }
    }

    const UString arcName = CreateArchiveName(_fileNames, _fileNames.Size() == 1 ? &fi0 : NULL);

    UString arcName7z = arcName;
    arcName7z += ".7z";
    UString arcNameZip = arcName;
    arcNameZip += ".zip";

    #ifdef EMAIL_SUPPORT
    // CompressEmail
    if ((contextMenuFlags & NContextMenuFlags::kCompressEmail) != 0 && !_dropMode)
    {
      CCommandMapItem cmi;
      cmi.ArcName = arcName;
      AddCommand(kCompressEmail, mainString, cmi);
      MyInsertMenu(popupMenu, subIndex++, currentCommandID++, mainString, bitmap);
    }
    #endif

    // CompressTo7z
    if (contextMenuFlags & NContextMenuFlags::kCompressTo7z &&
        !arcName7z.IsEqualTo_NoCase(fs2us(fi0.Name)))
    {
      CCommandMapItem cmi;
      UString s;
      if (_dropMode)
        cmi.Folder = _dropPath;
      else
        cmi.Folder = fs2us(folderPrefix);
      cmi.ArcName = arcName7z;
      cmi.ArcType = "7z";
      AddCommand(kCompressTo7z, s, cmi);
      MyFormatNew_ReducedName(s, arcName7z);
      Set_UserString_in_LastCommand(s);
      MyInsertMenu(popupMenu, subIndex++, currentCommandID++, s, bitmap);
    }

    #ifdef EMAIL_SUPPORT
    // CompressTo7zEmail
    if ((contextMenuFlags & NContextMenuFlags::kCompressTo7zEmail) != 0  && !_dropMode)
    {
      CCommandMapItem cmi;
      UString s;
      cmi.ArcName = arcName7z;
      cmi.ArcType = "7z";
      AddCommand(kCompressTo7zEmail, s, cmi);
      MyFormatNew_ReducedName(s, arcName7z);
      Set_UserString_in_LastCommand(s);
      MyInsertMenu(popupMenu, subIndex++, currentCommandID++, s, bitmap);
    }
    #endif

    // CompressToZip
    if (!onlyArchives &&
        contextMenuFlags & NContextMenuFlags::kCompressToZip &&
        !arcNameZip.IsEqualTo_NoCase(fs2us(fi0.Name)))
    {
      CCommandMapItem cmi;
      UString s;
      if (_dropMode)
        cmi.Folder = _dropPath;
      else
        cmi.Folder = fs2us(folderPrefix);
      cmi.ArcName = arcNameZip;
      cmi.ArcType = "zip";
      AddCommand(kCompressToZip, s, cmi);
      MyFormatNew_ReducedName(s, arcNameZip);
      Set_UserString_in_LastCommand(s);
      MyInsertMenu(popupMenu, subIndex++, currentCommandID++, s, bitmap);
    }

    // **************** NanaZip Modification Start ****************
    if (!onlyArchives &&
        contextMenuFlags & NContextMenuFlags::kCompressToZipDelete &&
        !arcNameZip.IsEqualTo_NoCase(fs2us(fi0.Name)))
    {
      CCommandMapItem cmi;
      UString s;
      if (_dropMode)
        cmi.Folder = _dropPath;
      else
        cmi.Folder = fs2us(folderPrefix);
      cmi.ArcName = arcNameZip;
      cmi.ArcType = "zip";
      AddCommand(kCompressToZipDelete, s, cmi);
      MyFormatNew_ReducedName(s, arcNameZip);
      Set_UserString_in_LastCommand(s);
      MyInsertMenu(popupMenu, subIndex++, currentCommandID++, s, bitmap);
    }
    // **************** NanaZip Modification End ****************

    #ifdef EMAIL_SUPPORT
    // CompressToZipEmail
    if ((contextMenuFlags & NContextMenuFlags::kCompressToZipEmail) != 0  && !_dropMode)
    {
      CCommandMapItem cmi;
      UString s;
      cmi.ArcName = arcNameZip;
      cmi.ArcType = "zip";
      AddCommand(kCompressToZipEmail, s, cmi);
      MyFormatNew_ReducedName(s, arcNameZip);
      Set_UserString_in_LastCommand(s);
      MyInsertMenu(popupMenu, subIndex++, currentCommandID++, s, bitmap);
    }
    #endif
  }


  // don't use InsertMenu:  See MSDN:
  // PRB: Duplicate Menu Items In the File Menu For a Shell Context Menu Extension
  // ID: Q214477
  
  if (ci.Cascaded.Val)
  {
    CMenu menu;
    menu.Attach(hMenu);
    menuDestroyer.Disable();
    MyAddSubMenu(_commandMap, kMainVerb, menu, indexMenu++, currentCommandID++, (UString)"NanaZip",
        popupMenu, // popupMenu.Detach(),
        bitmap);
  }
  else
  {
    // popupMenu.Detach();
    indexMenu = subIndex;
  }

  const bool needCrc = ((contextMenuFlags &
      (NContextMenuFlags::kCRC |
       NContextMenuFlags::kCRC_Cascaded)) != 0);
  
  if (
      // !_isMenuForFM && // 21.04: we don't hide CRC SHA menu in 7-Zip FM
      needCrc
      && currentCommandID + 1 < commandIDLast)
  {
    CMenu subMenu;
    // CMenuDestroyer menuDestroyer_CRC;

    UINT subIndex_CRC = 0;
    
    if (!hMenu || subMenu.CreatePopup())
    {
      // menuDestroyer_CRC.Attach(subMenu);
      const bool insertHashMenuTo7zipMenu = (ci.Cascaded.Val
          && (contextMenuFlags & NContextMenuFlags::kCRC_Cascaded) != 0);
      
      CMenu menu;
      {
        int indexInParent;
        if (insertHashMenuTo7zipMenu)
        {
          indexInParent = subIndex;
          menu.Attach(popupMenu);
        }
        else
        {
          indexInParent = indexMenu;
          menu.Attach(hMenu);
          // menuDestroyer_CRC.Disable();
        }
        MyAddSubMenu(_commandMap, kCheckSumCascadedVerb, menu, indexInParent++, currentCommandID++, (UString)"CRC SHA", subMenu,
          /* insertHashMenuTo7zipMenu ? NULL : */ bitmap);
        _commandMap.Back().CtxCommandType = CtxCommandType_CrcRoot;
        if (!insertHashMenuTo7zipMenu)
          indexMenu = indexInParent;
      }

      for (unsigned i = 0; i < ARRAY_SIZE(g_HashCommands); i++)
      {
        if (currentCommandID >= commandIDLast)
          break;
        const CHashCommand &hc = g_HashCommands[i];
        CCommandMapItem cmi;
        cmi.CommandInternalID = hc.CommandInternalID;
        cmi.Verb = kCheckSumCascadedVerb;
        cmi.Verb += '.';
        cmi.Verb += hc.MethodName;
        UString s;
        s += hc.UserName;
        
        if (hc.CommandInternalID == kHash_Generate_SHA256)
        {
          {
            popupMenu.Attach(hMenu);
            CMenuItem mi;
            mi.fType = MFT_SEPARATOR;
            mi.fMask = MIIM_TYPE;
            subMenu.InsertItem(subIndex_CRC++, true, mi);
          }

          UString name;
          if (_fileNames.Size() > 1)
            name = CreateArchiveName(_fileNames, _fileNames.Size() == 1 ? &fi0 : NULL);
          else
            name = fs2us(fi0.Name);
          name += ".sha256";
          cmi.Folder = fs2us(folderPrefix);
          cmi.ArcName = name;
          s = "SHA-256 -> ";
          s += name;
        }
        else if (hc.CommandInternalID == kHash_TestArc)
        {
          s = LangStringAlt(IDS_CONTEXT_TEST, "Test archive");
          s += " : ";
          s += GetNameOfProperty(kpidChecksum, UString("Checksum"));
        }

        // cmi.HelpString = cmi.Verb;
        cmi.UserString = s;
        cmi.CtxCommandType = CtxCommandType_CrcChild;
        _commandMap.Add(cmi);
        MyInsertMenu(subMenu, subIndex_CRC++, currentCommandID++, s, bitmap);
      }
      
      subMenu.Detach();
    }
  }

  if (SelectionIsOnlyFiles(_fileNames) &&
      WidePathsShareParent(ToWidePaths(_fileNames)) &&
      currentCommandID < commandIDLast)
  {
    if (popupMenu && popupMenu.GetItemCount() > 0)
    {
      CMenuItem mi;
      mi.fType = MFT_SEPARATOR;
      mi.fMask = MIIM_TYPE;
      popupMenu.InsertItem((int)popupMenu.GetItemCount(), true, mi);
    }
    CCommandMapItem cmi;
    cmi.CommandInternalID = kFilesToFolder;
    cmi.Verb = kMainVerb;
    cmi.Verb += ".FilesToFolder";
    cmi.UserString = FilesToFolderTitle();
    _commandMap.Add(cmi);
    MyInsertMenu(popupMenu, (UINT)popupMenu.GetItemCount(), currentCommandID++, cmi.UserString, bitmap);
  }

  // Separator, then copy items.
  {
    const bool menuHasItems = popupMenu && popupMenu.GetItemCount() > 0;
    const bool onlyFolders = SelectionIsOnlyFolders(_fileNames);
    const unsigned copyCount = onlyFolders ? 2u : 3u;
    const UINT room = copyCount - 1;
    if (_fileNames.Size() > 0 && menuHasItems
        && currentCommandID + room < commandIDLast)
    {
      if (menuHasItems && popupMenu)
      {
        CMenuItem mi;
        mi.fType = MFT_SEPARATOR;
        mi.fMask = MIIM_TYPE;
        popupMenu.InsertItem((int)popupMenu.GetItemCount(), true, mi);
      }

      const CZipContextMenu::ECommandInternalID copyIds[3] = {
        kCopyFileAddress, kCopyFileName, kCopyFileNameNoExt
      };
      const wchar_t *copyTitles[3] = {
        onlyFolders ? CopyFolderAddressTitle() : CopyFileAddressTitle(),
        onlyFolders ? CopyFolderNameTitle() : CopyFileNameTitle(),
        CopyFileNameNoExtTitle()
      };
      const char *copyVerbs[3] = {
        ".CopyFileAddress", ".CopyFileName", ".CopyFileNameNoExt"
      };
      for (unsigned i = 0; i < copyCount; i++)
      {
        CCommandMapItem cmi;
        cmi.CommandInternalID = copyIds[i];
        cmi.Verb = kMainVerb;
        cmi.Verb += copyVerbs[i];
        cmi.UserString = copyTitles[i];
        _commandMap.Add(cmi);
        MyInsertMenu(popupMenu, (UINT)popupMenu.GetItemCount(), currentCommandID++, cmi.UserString, bitmap);
      }
    }

  }

  // 「新建压缩包...」is the last item, with a separator above it.
  {
    bool compressOnlyArchives = (_fileNames.Size() > 0 && !fi0.IsDir() && IsRecognizedArchiveName(fi0.Name));
    if (compressOnlyArchives)
    {
      for (unsigned i = 1; i < _fileNames.Size(); i++)
      {
        NFind::CFileInfo fi;
        if (!fi.Find(us2fs(_fileNames[i])) || fi.IsDir() || !IsRecognizedArchiveName(fi.Name))
        {
          compressOnlyArchives = false;
          break;
        }
      }
    }
    if (!compressOnlyArchives && (contextMenuFlags & NContextMenuFlags::kCompress) != 0
        && currentCommandID < commandIDLast)
    {
      if (popupMenu && popupMenu.GetItemCount() > 0)
      {
        CMenuItem mi;
        mi.fType = MFT_SEPARATOR;
        mi.fMask = MIIM_TYPE;
        popupMenu.InsertItem((int)popupMenu.GetItemCount(), true, mi);
      }
      CCommandMapItem cmi;
      if (_dropMode)
        cmi.Folder = _dropPath;
      else
        cmi.Folder = fs2us(folderPrefix);
      cmi.ArcName = CreateArchiveName(_fileNames, _fileNames.Size() == 1 ? &fi0 : NULL);
      AddCommand(kCompress, mainString, cmi);
      MyInsertMenu(popupMenu, (UINT)popupMenu.GetItemCount(), currentCommandID++, mainString, bitmap);
    }
  }

  // **************** NanaZip Modification Start ****************
  if (!_dropMode && !_isMenuForFM && _fileNames.Size() &&
      !SelectionHasDriveRoot(_fileNames) && currentCommandID < commandIDLast)
  {
    CCommandMapItem cmi;
    cmi.CommandInternalID = kRename;
    cmi.Verb = "SevenZip.Rename";
    cmi.UserString = (L"重命名" +
        NanaZip::Renaming::SelectionNoun(ToWidePaths(_fileNames)) + L"…").c_str();
    _commandMap.Add(cmi);
    UINT position = (UINT)popupMenu.GetItemCount();
    if (position > 2)
      position = 2;
    MyInsertMenu(popupMenu, position, currentCommandID++, cmi.UserString, bitmap);
  }
  // **************** NanaZip Modification End ****************
  popupMenu.Detach();

  /*
  if (!ci.Cascaded.Val)
    indexMenu = subIndex;
  */

  ODS("### 400");

  #ifdef SHOW_DEBUG_CTX_MENU
  { char s[256]; sprintf(s, "Commands=%d currentCommandID - commandIDFirst = %d",
    _commandMap.Size(), currentCommandID - commandIDFirst); OutputDebugStringA(s); }
  #endif

  if (_commandMap.Size() != currentCommandID - commandIDFirst)
    throw 20190818;
  return MAKE_HRESULT(SEVERITY_SUCCESS, 0, currentCommandID - commandIDFirst);

  }
  catch(...)
  {
    /* we added some menu items already : num_added_menu_items,
       So we MUST return (number_of_defined_ids), where (number_of_defined_ids >= num_added_menu_items)
       This will prevent incorrect menu working, when same IDs can be
       assigned in multiple menu items from different subhandlers.
       And we must add items to _commandMap before adding to menu.
     */
    #ifdef SHOW_DEBUG_CTX_MENU
    { char s[256]; sprintf(s, "catch() exception: Commands=%d",
      _commandMap.Size()); OutputDebugStringA(s); }
    #endif
    // if (_commandMap.Size() != 0)
      return MAKE_HRESULT(SEVERITY_SUCCESS, 0, _commandMap.Size());
    // throw;
  }
  COM_TRY_END
}


int CZipContextMenu::FindVerb(const UString &verb)
{
  FOR_VECTOR (i, _commandMap)
    if (_commandMap[i].Verb == verb)
      return i;
  return -1;
}

#ifdef UNDER_CE
  #define MY__IS_INTRESOURCE(_r) ((((ULONG_PTR)(_r)) >> 16) == 0)
#else
  #define MY__IS_INTRESOURCE(_r) IS_INTRESOURCE(_r)
#endif


#ifdef SHOW_DEBUG_CTX_MENU
static void PrintStringA(const char *name, LPCSTR ptr)
{
  AString m;
  m += name;
  m += ": ";
  char s[32];
  sprintf(s, "%p", ptr);
  m += s;
  if (!MY__IS_INTRESOURCE(ptr))
  {
    m += ": \"";
    m += ptr;
    m += "\"";
  }
  OutputDebugStringA(m);
}

#if !defined(UNDER_CE)
static void PrintStringW(const char *name, LPCWSTR ptr)
{
  UString m;
  m += name;
  m += ": ";
  char s[32];
  sprintf(s, "%p", ptr);
  m += s;
  if (!MY__IS_INTRESOURCE(ptr))
  {
    m += ": \"";
    m += ptr;
    m += "\"";
  }
  OutputDebugStringW(m);
}
#endif
#endif


STDMETHODIMP CZipContextMenu::InvokeCommand(LPCMINVOKECOMMANDINFO commandInfo)
{
  COM_TRY_BEGIN

  #ifdef SHOW_DEBUG_CTX_MENU

  { char s[1280]; sprintf(s,
      #ifdef _WIN64
      "64"
      #else
      "32"
      #endif
      ": InvokeCommand: cbSize=%d flags=%x "
    , commandInfo->cbSize, commandInfo->fMask); OutputDebugStringA(s); }

    PrintStringA("Verb", commandInfo->lpVerb);
    PrintStringA("Parameters", commandInfo->lpParameters);
    PrintStringA("Directory", commandInfo->lpDirectory);
  #endif

  int commandOffset = -1;

  // xp64 / Win10 : explorer.exe sends 0 in lpVerbW
  // MSDN: if (IS_INTRESOURCE(lpVerbW)), we must use LOWORD(lpVerb) as sommand offset

  // FIXME: MINGW doesn't define CMINVOKECOMMANDINFOEX
  #if !defined(UNDER_CE) /* && defined(_MSC_VER) */
  bool unicodeVerb = false;
  if (commandInfo->cbSize == sizeof(CMINVOKECOMMANDINFOEX) &&
      (commandInfo->fMask & CMIC_MASK_UNICODE) != 0)
  {
    LPCMINVOKECOMMANDINFOEX commandInfoEx = (LPCMINVOKECOMMANDINFOEX)commandInfo;
    if (!MY__IS_INTRESOURCE(commandInfoEx->lpVerbW))
    {
      unicodeVerb = true;
      commandOffset = FindVerb(commandInfoEx->lpVerbW);
    }
    
    #ifdef SHOW_DEBUG_CTX_MENU
    PrintStringW("VerbW", commandInfoEx->lpVerbW);
    PrintStringW("ParametersW", commandInfoEx->lpParametersW);
    PrintStringW("DirectoryW", commandInfoEx->lpDirectoryW);
    PrintStringW("TitleW", commandInfoEx->lpTitleW);
    PrintStringA("Title", commandInfoEx->lpTitle);
    #endif
  }
  if (!unicodeVerb)
  #endif
  {
    #ifdef SHOW_DEBUG_CTX_MENU
    OutputDebugStringA("use non-UNICODE verb");
    #endif
    // if (HIWORD(commandInfo->lpVerb) == 0)
    if (MY__IS_INTRESOURCE(commandInfo->lpVerb))
      commandOffset = LOWORD(commandInfo->lpVerb);
    else
      commandOffset = FindVerb(GetUnicodeString(commandInfo->lpVerb));
  }

  #ifdef SHOW_DEBUG_CTX_MENU
  { char s[128]; sprintf(s, "commandOffset=%d",
      commandOffset); OutputDebugStringA(s); }
  #endif

  if (commandOffset < 0 || (unsigned)commandOffset >= _commandMap.Size())
    return E_INVALIDARG;
  const CCommandMapItem &cmi = _commandMap[(unsigned)commandOffset];
  return InvokeCommandCommon(cmi);
  COM_TRY_END
}


HRESULT CZipContextMenu::InvokeCommandCommon(const CCommandMapItem &cmi)
{
  NanaZip::DialogAppearance::DialogScope Appearance;
  const ECommandInternalID cmdID = cmi.CommandInternalID;

  try
  {
    switch (cmdID)
    {
      // **************** NanaZip Modification Start ****************
      case kRename:
      {
        NanaZip::Renaming::Show(ToWidePaths(_fileNames));
        break;
      }
      // **************** NanaZip Modification End ****************
      case kNewFolderDate:
      {
        CreateDateFolder(cmi.Folder);
        break;
      }
      case kNewFolderBatch:
      {
        CreateBatchFolders(cmi.Folder);
        break;
      }
      case kFilesToFolder:
      {
        MoveFilesIntoFolder(_fileNames);
        break;
      }
      case kUnwrapFolder:
      {
        RemoveEnclosingFolders(_fileNames);
        break;
      }
      case kOpen:
      {
        OpenArchiveInFileManager(_fileNames[0]);
        break;
      }
      case kCopyFileAddress:
      {
        CopySelectedFileText(_fileNames, 0);
        break;
      }
      case kCopyFileName:
      {
        CopySelectedFileText(_fileNames, 1);
        break;
      }
      case kCopyFileNameNoExt:
      {
        CopySelectedFileText(_fileNames, 2);
        break;
      }
      case kExtract:
      case kExtractHere:
      // **************** NanaZip Modification Start ****************
#if 0 // ******** Annotated 7-Zip Mainline Source Code snippet Start ********
      case kExtractTo:
      {
        ExtractArchives(_fileNames, cmi.Folder,
            (cmdID == kExtract), // showDialog
            (cmdID == kExtractTo) && _elimDup.Val, // elimDup
            _writeZone
            );
        break;
      }
#endif // ******** Annotated 7-Zip Mainline Source Code snippet End ********
      case kExtractHereSmart:
      case kExtractHereDelete:
      case kExtractTo:
      case kExtractToDelete:
      {
        ExtractArchives(_fileNames, cmi.Folder,
            (cmdID == kExtract), // showDialog
            (cmdID == kExtractTo || cmdID == kExtractToDelete) && _elimDup.Val, // elimDup
            _writeZone,
            (cmdID == kExtractHereSmart),
            false, // openFolder
            (cmdID == kExtractHereDelete || cmdID == kExtractToDelete) // deleteArchive
            );
        break;
      }
      // **************** NanaZip Modification End ****************
      case kTest:
      {
        TestArchives(_fileNames);
        break;
      }
      case kCompress:
      case kCompressEmail:
      case kCompressTo7z:
      case kCompressTo7zEmail:
      case kCompressToZip:
      case kCompressToZipEmail:
      // **************** NanaZip Modification Start ****************
      case kCompressToZipDelete:
      // **************** NanaZip Modification End ****************
      {
        const bool email =
            (cmdID == kCompressEmail) ||
            (cmdID == kCompressTo7zEmail) ||
            (cmdID == kCompressToZipEmail);
        const bool showDialog =
            (cmdID == kCompress) ||
            (cmdID == kCompressEmail);
        const bool addExtension = (cmdID == kCompress || cmdID == kCompressEmail);
        // **************** NanaZip Modification Start ****************
        const bool deleteSources =
            (cmdID == kCompressToZipDelete);
        // **************** NanaZip Modification End ****************
        CompressFiles(cmi.Folder,
            cmi.ArcName, cmi.ArcType,
            addExtension,
            _fileNames, email, showDialog,
            false, // waitFinish
            deleteSources
            );
        break;
      }
      
      case kHash_CRC32:
      case kHash_CRC64:
      case kHash_SHA1:
      case kHash_SHA256:
      case kHash_All:
      case kHash_Generate_SHA256:
      case kHash_TestArc:
      {
        for (unsigned i = 0; i < ARRAY_SIZE(g_HashCommands); i++)
        {
          const CHashCommand &hc = g_HashCommands[i];
          if (hc.CommandInternalID == cmdID)
          {
            if (cmdID == kHash_TestArc)
            {
              TestArchives(_fileNames, true); // hashMode
              break;
            }
            UString generateName;
            if (cmdID == kHash_Generate_SHA256)
              generateName = cmi.ArcName;
            CalcChecksum(_fileNames, (UString)hc.MethodName,
                cmi.Folder, generateName);
            break;
          }
        }
        break;
      }
      case kCommandNULL:
        break;
    }
  }
  catch(...)
  {
    ::MessageBoxW(0, L"Error", L"NanaZip", MB_ICONERROR);
  }
  return S_OK;
}



static void MyCopyString(void *dest, const UString &src, bool writeInUnicode, UINT size)
{
  if (size != 0)
    size--;
  if (writeInUnicode)
  {
    UString s = src;
    s.DeleteFrom(size);
    MyStringCopy((wchar_t *)dest, s);
  }
  else
  {
    AString s = GetAnsiString(src);
    s.DeleteFrom(size);
    MyStringCopy((char *)dest, s);
  }
}


STDMETHODIMP CZipContextMenu::GetCommandString(UINT_PTR commandOffset, UINT uType,
    UINT * /* pwReserved */ , LPSTR pszName, UINT cchMax)
{
  COM_TRY_BEGIN

  const int cmdOffset = (int)commandOffset;
  
  #ifdef SHOW_DEBUG_CTX_MENU
  { char s[256]; sprintf(s, "GetCommandString: cmdOffset=%d uType=%d cchMax = %d",
      cmdOffset, uType, cchMax); OutputDebugStringA(s); }
  #endif

  if (uType == GCS_VALIDATEA || uType == GCS_VALIDATEW)
  {
    if (cmdOffset < 0 || (unsigned)cmdOffset >= _commandMap.Size())
      return S_FALSE;
    else
      return S_OK;
  }

  if (cmdOffset < 0 || (unsigned)cmdOffset >= _commandMap.Size())
  {
    #ifdef SHOW_DEBUG_CTX_MENU
    OutputDebugStringA("---------------- cmdOffset: E_INVALIDARG");
    #endif
    return E_INVALIDARG;
  }

  const CCommandMapItem &cmi = _commandMap[(unsigned)cmdOffset];

  if (uType == GCS_HELPTEXTA || uType == GCS_HELPTEXTW)
  {
    // we can return "Verb" here for debug purposes.
    // HelpString;
    MyCopyString(pszName, cmi.Verb, uType == GCS_HELPTEXTW, cchMax);
    return S_OK;
  }

  if (uType == GCS_VERBA || uType == GCS_VERBW)
  {
    MyCopyString(pszName, cmi.Verb, uType == GCS_VERBW, cchMax);
    return S_OK;
  }
  
  return E_INVALIDARG;
  
  COM_TRY_END
}



// ---------- IExplorerCommand ----------

static HRESULT WINAPI My_SHStrDupW(LPCWSTR src, LPWSTR *dest)
{
  if (src)
  {
    const SIZE_T size = (wcslen(src) + 1) * sizeof(WCHAR);
    WCHAR *p = (WCHAR *)CoTaskMemAlloc(size);
    if (p)
    {
      memcpy(p, src, size);
      *dest = p;
      return S_OK;
    }
  }
  *dest = NULL;
  return E_OUTOFMEMORY;
}


#define CZipExplorerCommand CZipContextMenu

class CCoTaskWSTR
{
  LPWSTR m_str;
  CLASS_NO_COPY(CCoTaskWSTR)
public:
  CCoTaskWSTR(): m_str(NULL) {}
  ~CCoTaskWSTR() { ::CoTaskMemFree(m_str); }
  LPWSTR* operator&() { return &m_str; }
  operator LPCWSTR () const { return m_str; }
  // operator LPCOLESTR() const { return m_str; }
  operator bool() const { return m_str != NULL; }
  // bool operator!() const { return m_str == NULL; }

  /*
  void Wipe_and_Free()
  {
    if (m_str)
    {
      memset(m_str, 0, ::SysStringLen(m_str) * sizeof(*m_str));
      Empty();
    }
  }
  */

private:
  /*
  CCoTaskWSTR(LPCOLESTR src) { m_str = ::CoTaskMemAlloc(src); }
  
  CCoTaskWSTR& operator=(LPCOLESTR src)
  {
    ::CoTaskMemFree(m_str);
    m_str = ::SysAllocString(src);
    return *this;
  }
 

  void Empty()
  {
    ::CoTaskMemFree(m_str);
    m_str = NULL;
  }
  */
};

static void LoadPaths(IShellItemArray *psiItemArray, UStringVector &paths)
{
  if (psiItemArray)
  {
    DWORD numItems = 0;
    if (psiItemArray->GetCount(&numItems) == S_OK)
    {
      for (DWORD i = 0; i < numItems; i++)
      {
        CMyComPtr<IShellItem> item;
        if (psiItemArray->GetItemAt(i, &item) == S_OK && item)
        {
          CCoTaskWSTR displayName;
          if (item->GetDisplayName(SIGDN_FILESYSPATH, &displayName) == S_OK
              && (bool)displayName)
          {
            OutputDebugStringW(displayName);
            paths.Add((LPCWSTR)displayName);
          }
        }
      }
    }
  }
}


void CZipExplorerCommand::LoadItems(IShellItemArray *psiItemArray)
{
  SubCommands.Clear();

  UStringVector paths;
  LoadPaths(psiItemArray, paths);
  _fileNames = paths;

  HRESULT res = QueryContextMenu(
      NULL, // hMenu,
      0, // indexMenu,
      0, // commandIDFirst,
      0 + 999, // commandIDLast,
      CMF_NORMAL);

  if (FAILED(res))
    return /* res */;


  CZipExplorerCommand *crcHandler = NULL;
  CZipExplorerCommand *openHandler = NULL;

  bool useCascadedCrc = true; // false;
  bool useCascadedOpen = true; // false;

  for (unsigned i = 0; i < _commandMap.Size(); i++)
  {
    const CCommandMapItem &cmi = _commandMap[i];


    if (cmi.IsPopup)
      if (!cmi.IsSubMenu())
        continue;

    // if (cmi.IsSubMenu()) continue // for debug
      
    CZipContextMenu *shellExt = new CZipContextMenu();
    shellExt->IsRoot = false;

    if (cmi.CtxCommandType == CtxCommandType_CrcRoot && !useCascadedCrc)
      shellExt->IsSeparator = true;

    {
      CZipExplorerCommand *handler = this;
      if (cmi.CtxCommandType == CtxCommandType_CrcChild && crcHandler)
        handler = crcHandler;
      else if (cmi.CtxCommandType == CtxCommandType_OpenChild && openHandler)
        handler = openHandler;
      handler->SubCommands.AddNew() = shellExt;
    }

    shellExt->_commandMap_Cur.Add(cmi);

    ODS_U(cmi.UserString);

    if (cmi.CtxCommandType == CtxCommandType_CrcRoot && useCascadedCrc)
      crcHandler = shellExt;
    if (cmi.CtxCommandType == CtxCommandType_OpenRoot && useCascadedOpen)
    {
      // ODS2("cmi.CtxCommandType == CtxCommandType_OpenRoot");
      openHandler = shellExt;
    }
  }
}


STDMETHODIMP CZipExplorerCommand::GetTitle(IShellItemArray *psiItemArray, LPWSTR *ppszName)
{
  ODS("- GetTitle()");
 // COM_TRY_BEGIN
  if (IsSeparator)
  {
    *ppszName = NULL;
    return S_FALSE;
  }

  UString name;
  if (IsRoot)
  {
    LoadItems(psiItemArray);
    name = "NanaZip"; //  "New"
  }
  else
    name = "NanaZip item";
  
  if (!_commandMap_Cur.IsEmpty())
  {
    const CCommandMapItem &mi = _commandMap_Cur[0];
    // s += mi.Verb;
    // s += " : ";
    name = mi.UserString;
  }

  return My_SHStrDupW(name, ppszName);
  // return S_OK;
  // COM_TRY_END
}


STDMETHODIMP CZipExplorerCommand::GetIcon(IShellItemArray * /* psiItemArray */, LPWSTR *ppszIcon)
{
  ODS("- GetIcon()");
  // COM_TRY_BEGIN
  *ppszIcon = NULL;
  // return E_NOTIMPL;
  UString imageName = fs2us(NWindows::NDLL::GetModuleDirPrefix());
  // imageName += "7zG.exe";
  // **************** NanaZip Modification Start ****************
  //imageName += "7-zip.dll";
  imageName += "NanaZip.Universal.Windows.exe";
  // **************** NanaZip Modification End ****************
  // imageName += ",190";
  return My_SHStrDupW(imageName, ppszIcon);
  // COM_TRY_END
}


STDMETHODIMP CZipExplorerCommand::GetToolTip (IShellItemArray * /* psiItemArray */, LPWSTR *ppszInfotip)
{
  // COM_TRY_BEGIN
  ODS("- GetToolTip()");
  *ppszInfotip = NULL;
  return E_NOTIMPL;
  // COM_TRY_END
}


STDMETHODIMP CZipExplorerCommand::GetCanonicalName(GUID *pguidCommandName)
{
  // COM_TRY_BEGIN
  ODS("- GetCanonicalName()");
  *pguidCommandName = GUID_NULL;
  return E_NOTIMPL;
  // COM_TRY_END
}


STDMETHODIMP CZipExplorerCommand::GetState(IShellItemArray * /* psiItemArray */, BOOL /* fOkToBeSlow */, EXPCMDSTATE *pCmdState)
{
  // COM_TRY_BEGIN
  ODS("- GetState()");
  *pCmdState = ECS_ENABLED;
  return S_OK;
  // COM_TRY_END
}




STDMETHODIMP CZipExplorerCommand::Invoke(IShellItemArray *psiItemArray, IBindCtx * /* pbc */)
{
  COM_TRY_BEGIN

  if (_commandMap_Cur.IsEmpty())
    return E_INVALIDARG;

  ODS("- Invoke()");
  UStringVector paths;
  LoadPaths(psiItemArray, paths);
  _fileNames = paths;
  return InvokeCommandCommon(_commandMap_Cur[0]);

  COM_TRY_END
}


STDMETHODIMP CZipExplorerCommand::GetFlags(EXPCMDFLAGS *pFlags)
{
  ODS("- GetFlags()");
  // COM_TRY_BEGIN
  EXPCMDFLAGS f = ECF_DEFAULT;
  if (IsSeparator)
    f = ECF_ISSEPARATOR;
  else if (IsRoot)
    f = ECF_HASSUBCOMMANDS;
  else
  {
    if (!_commandMap_Cur.IsEmpty())
    {
      // const CCommandMapItem &cmi = ;
      if (_commandMap_Cur[0].IsSubMenu())
      {
        // ODS("ECF_HASSUBCOMMANDS");
        f = ECF_HASSUBCOMMANDS;
      }
    }
  }
  *pFlags = f;
  return S_OK;
  // COM_TRY_END
}


STDMETHODIMP CZipExplorerCommand::EnumSubCommands(IEnumExplorerCommand **ppEnum)
{
  ODS("- EnumSubCommands()");
  // COM_TRY_BEGIN
  *ppEnum = NULL;

  if (!_commandMap_Cur.IsEmpty() && _commandMap_Cur[0].IsSubMenu())
  {
  }
  else
  {
    if (!IsRoot)
      return E_NOTIMPL;
    if (SubCommands.IsEmpty())
    {
      return E_NOTIMPL;
    }
  }
 
  // shellExt->
  return QueryInterface(IID_IEnumExplorerCommand, (void **)ppEnum);

  // return S_OK;
  // COM_TRY_END
}


STDMETHODIMP CZipContextMenu::Next(ULONG celt, IExplorerCommand **pUICommand, ULONG *pceltFetched)
{
  ODS("CZipContextMenu::Next()");
  Print_Number(celt, "celt");
  Print_Number(CurrentSubCommand, "CurrentSubCommand");
  Print_Number(SubCommands.Size(), "SubCommands.Size()");
  
  COM_TRY_BEGIN
  ULONG fetched = 0;

  ULONG i;
  for (i = 0; i < celt; i++)
  {
    pUICommand[i] = NULL;
  }
  
  for (i = 0; i < celt && CurrentSubCommand < SubCommands.Size(); i++)
  {
    pUICommand[i] = SubCommands[CurrentSubCommand++];
    pUICommand[i]->AddRef();
    fetched++;
  }
  
  if (pceltFetched)
    *pceltFetched = fetched;

  ODS(fetched == celt ? " === OK === " : "=== ERROR ===");

  // we return S_FALSE for (fetched == 0)
  return (fetched == celt) ? S_OK : S_FALSE;
  COM_TRY_END
}


STDMETHODIMP CZipContextMenu::Skip(ULONG celt)
{
  ODS("CZipContextMenu::Skip()");
  celt = celt;
  return E_NOTIMPL;
}


STDMETHODIMP CZipContextMenu::Reset(void)
{
  ODS("CZipContextMenu::Reset()");
  CurrentSubCommand = 0;
  return S_OK;
}


STDMETHODIMP CZipContextMenu::Clone(IEnumExplorerCommand **ppenum)
{
  ODS("CZipContextMenu::Clone()");
  *ppenum = NULL;
  return E_NOTIMPL;
}
