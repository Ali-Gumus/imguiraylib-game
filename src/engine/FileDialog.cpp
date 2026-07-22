// This is the ONLY file in the project that includes <windows.h>. Windows
// defines macros with names like CloseWindow, DrawText and Rectangle, which
// collide with functions of the same names in raylib. Keeping windows.h inside
// this single .cpp (and including no raylib header here) prevents that clash.
#define WIN32_LEAN_AND_MEAN   // pull in a smaller subset of windows.h (faster builds)
#include <windows.h>
#include <commdlg.h>          // GetOpenFileName / GetSaveFileName (the common dialogs)

#include "engine/FileDialog.h"

#include <filesystem>
#include <cstring>

namespace eng {

// Turn an absolute path into one relative to the current working directory
// (the project folder while the editor runs), when the file is inside it.
// Files outside the project are returned unchanged (absolute).
static std::string Relativize(const char* absolute) {
    std::error_code ec;   // receives an error instead of throwing
    auto rel = std::filesystem::relative(absolute,
                                         std::filesystem::current_path(), ec);
    // If the conversion failed, or the result starts with ".." (meaning the
    // file is outside the project), keep the absolute path.
    if (ec || rel.empty() || rel.native().starts_with(L".."))
        return absolute;
    return rel.generic_string();   // generic_string() uses forward slashes
}

// Shared implementation for both open and save. `save` picks which dialog.
static std::string Dialog(bool save, const char* filter, const char* defExt,
                          const char* suggestedName) {
    // The dialog writes the chosen path into this fixed-size buffer. MAX_PATH
    // is Windows' traditional maximum path length.
    char file[MAX_PATH] = "";
    if (suggestedName) {                         // pre-fill the name box, if given
        strncpy(file, suggestedName, sizeof(file) - 1);
        file[sizeof(file) - 1] = '\0';           // make sure it's null-terminated
    }

    // OPENFILENAMEA is the struct that configures the dialog. The trailing 'A'
    // is the "ANSI" (char-based) variant, which matches our char buffers.
    // `{}` zeroes every field first; then we fill in the ones we care about.
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);        // Windows checks this to know the version
    ofn.hwndOwner   = GetActiveWindow();  // makes the dialog modal over our window
    ofn.lpstrFilter = filter;             // the allowed file types
    ofn.lpstrFile   = file;               // buffer for (and initial value of) the path
    ofn.nMaxFile    = sizeof(file);       // buffer size
    ofn.lpstrDefExt = defExt;             // extension added if the user omits one
    // Flags:
    //  OFN_NOCHANGEDIR keeps the dialog from changing our working directory
    //    (which would break every relative asset path the engine uses).
    //  For save: warn before overwriting. For open: the file must exist.
    ofn.Flags = OFN_NOCHANGEDIR |
                (save ? OFN_OVERWRITEPROMPT
                      : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);

    // Show the dialog. Returns non-zero if the user picked a file, zero if
    // they cancelled or an error occurred.
    BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if (!ok) return "";                   // cancelled -> empty string
    return Relativize(file);
}

std::string OpenFileDialog(const char* filter, const char* defExt) {
    return Dialog(false, filter, defExt, nullptr);
}

std::string SaveFileDialog(const char* filter, const char* defExt,
                           const char* suggestedName) {
    return Dialog(true, filter, defExt, suggestedName);
}

} // namespace eng
