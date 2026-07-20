// The ONLY file that includes windows.h — it defines names that collide
// with raylib (CloseWindow, DrawText, Rectangle...), so it stays
// quarantined here and no raylib header enters this file.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include "engine/FileDialog.h"

#include <filesystem>
#include <cstring>

namespace eng {

// Store paths relative to the working directory (= project root in the
// editor) when possible, so scene/graph files don't bake in C:\Users\...
static std::string Relativize(const char* absolute) {
    std::error_code ec;
    auto rel = std::filesystem::relative(absolute,
                                         std::filesystem::current_path(), ec);
    if (ec || rel.empty() || rel.native().starts_with(L".."))
        return absolute;                       // outside the project: keep absolute
    return rel.generic_string();               // forward slashes, portable
}

static std::string Dialog(bool save, const char* filter, const char* defExt,
                          const char* suggestedName) {
    char file[MAX_PATH] = "";
    if (suggestedName) {
        strncpy(file, suggestedName, sizeof(file) - 1);
        file[sizeof(file) - 1] = '\0';
    }

    OPENFILENAMEA ofn{};                       // A = ANSI variant, matches our char*
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetActiveWindow();       // modal over the editor window
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = sizeof(file);
    ofn.lpstrDefExt = defExt;                  // appended if the user types none
    // NOCHANGEDIR is load-bearing: without it the dialog changes our
    // working directory and every relative asset path breaks.
    ofn.Flags = OFN_NOCHANGEDIR |
                (save ? OFN_OVERWRITEPROMPT
                      : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);

    BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if (!ok) return "";                        // cancelled (or error): empty
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
