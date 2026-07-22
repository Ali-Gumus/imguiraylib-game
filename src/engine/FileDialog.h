#pragma once

#include <string>

namespace eng {

// Show the operating system's native "Open file" or "Save file" window and
// return the chosen path. Both return an empty string ("") if the user
// pressed Cancel.
//
// A chosen file inside the project folder comes back as a RELATIVE path (so
// saved scene files stay portable between computers); a file outside the
// project comes back as a full absolute path.
//
// `filter` restricts which file types are shown. It uses the Windows format:
// pairs of (description, pattern) separated by '\0' characters, ending with an
// extra '\0', e.g.  "Lua scripts (*.lua)\0*.lua\0All files\0*.*\0".
// `defExt` is the extension automatically added if the user types a name
// without one (e.g. "lua").
std::string OpenFileDialog(const char* filter, const char* defExt);

// Same, for saving. `suggestedName` pre-fills the file name box.
std::string SaveFileDialog(const char* filter, const char* defExt,
                           const char* suggestedName);

} // namespace eng
