#pragma once

#include <string>

// Where the game EXE lives - the log and the INI sit beside it. Both character
// widths exist because the log path is wide and core's IniReader is ANSI
// (GetPrivateProfile*A).

namespace finch_ht
{
    // Everything before the last path separator, with no trailing separator.
    // "." when the path has none.
    std::wstring DirectoryOf(const std::wstring& path);
    std::string  DirectoryOf(const std::string& path);

    std::wstring ExeDirectory();
    std::string  ExeDirectoryNarrow();
}
