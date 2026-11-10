/*
 * PROJECT:    NanaZip
 * PURPOSE:    Shared preview-first filesystem renaming.
 * LICENSE:    The MIT License
 */
#pragma once

#include <Windows.h>
#include <string>
#include <vector>

namespace NanaZip::Renaming
{
    enum class Mode
    {
        Prefix, Suffix, ChangePrefix, Replace, Remove, Number,
        Trim, Date, Clean, LetterCase, List
    };

    struct Rule
    {
        Mode Operation = Mode::Prefix;
        std::wstring Text;
        std::wstring Find;
        unsigned Start = 1;
        unsigned Digits = 3;
        unsigned Length = 1;
        bool FromEnd = false;
        bool ModifiedDate = false;
        bool UpperCase = false;
        bool SkipPrefix = true;
    };

    struct Preset
    {
        std::wstring Label;
        Rule Settings;
        bool ShowInMenu = true;
    };

    struct Item
    {
        std::wstring Path;
        std::wstring Name;
        bool Directory = false;
        BY_HANDLE_FILE_INFORMATION Identity = {};
        bool Identified = false;
    };

    struct Entry
    {
        Item Source;
        std::wstring Name;
        std::wstring Target;
        std::wstring Error;
        bool Changed = false;
    };

    struct Plan
    {
        std::vector<Entry> Entries;
        std::wstring Error;
        size_t Changes = 0;
        bool Valid = false;
    };

    struct Result
    {
        bool Success = false;
        std::wstring Message;
        std::vector<Entry> Undo;
    };

    std::wstring NativePath(std::wstring const& Path);
    std::wstring Parent(std::wstring const& Path);
    std::wstring Name(std::wstring const& Path);
    std::wstring SelectionNoun(std::vector<std::wstring> const& Paths);
    std::vector<Item> ReadItems(std::vector<std::wstring> const& Paths);
    std::vector<std::wstring> SplitLines(std::wstring const& Text);
    bool DecodeText(std::vector<BYTE> const& Bytes, std::wstring& Text);
    Plan Preview(std::vector<Item> const& Items, Rule const& Settings,
        std::vector<std::wstring> const& Names);
    Plan Validate(std::vector<Entry> const& Entries);
    Result Execute(Plan const& Previewed);
#ifdef NANAZIP_RENAME_TESTING
    extern int FailMoveAt;
#endif
    bool LoadPresets(std::vector<Preset>& Presets, unsigned& MenuCount);
    bool SavePresets(std::vector<Preset> const& Presets, unsigned MenuCount);
    bool LoadUndo(std::vector<Entry>& Entries);
    bool SaveUndo(std::vector<Entry> const& Entries);
    void Show(std::vector<std::wstring> const& Paths,
        HWND Owner = nullptr, Preset const* Initial = nullptr,
        bool Manage = false, bool Undo = false);
}
