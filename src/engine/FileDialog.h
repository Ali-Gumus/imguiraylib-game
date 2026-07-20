#pragma once

#include <string>

namespace eng {

// Native Windows open/save dialogs. Returns "" if the user cancelled.
// Paths inside the project are returned RELATIVE (portable scene files);
// outside, absolute.
//
// `filter` uses the Win32 double-null format, pairs of (label, pattern):
//   "Lua scripts (*.lua)\0*.lua\0All files\0*.*\0"
std::string OpenFileDialog(const char* filter, const char* defExt);
std::string SaveFileDialog(const char* filter, const char* defExt,
                           const char* suggestedName);

} // namespace eng
