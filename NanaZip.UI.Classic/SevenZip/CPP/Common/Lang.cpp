// Common/Lang.cpp

#include "StdAfx.h"

#include "Lang.h"
#include "StringToInt.h"
#include "UTFConvert.h"

#include "../Windows/FileIO.h"

void CLang::Clear() throw()
{

}

bool CLang::OpenFromString(const AString &s2)
{
    UNREFERENCED_PARAMETER(s2);
    return true;
}

bool CLang::Open(CFSTR fileName, const char *id)
{
    UNREFERENCED_PARAMETER(fileName);
    UNREFERENCED_PARAMETER(id);
    return true;
}

const wchar_t *CLang::Get(UInt32 id) const throw()
{
    // NanaZip.Modern.dll already resolves Legacy.resw (Resource<id>, zh-Hans).
    // Load it on demand so this process does not have to link the DLL.
    using Proc = const wchar_t *(WINAPI *)(UINT32);
    static Proc proc = []() -> Proc
    {
        HMODULE module = ::GetModuleHandleW(L"NanaZip.Modern.dll");
        if (!module)
            module = ::LoadLibraryW(L"NanaZip.Modern.dll");
        if (!module)
            return nullptr;
        return reinterpret_cast<Proc>(::GetProcAddress(
            module,
            "K7ModernGetLegacyStringResource"));
    }();
    if (!proc)
        return nullptr;
    return proc(id);
}
