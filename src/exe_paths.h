#pragma once

#include <string>

// Where the game EXE lives - the log sits beside it.

namespace finch_ht
{
    // Everything before the last path separator, with no trailing separator.
    // "." when the path has none.
    std::wstring DirectoryOf(const std::wstring& path);

    std::wstring ExeDirectory();
}
