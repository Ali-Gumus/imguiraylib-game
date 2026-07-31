#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Components.h"  // SetScriptInputEnabled is declared there

#include "raylib.h"

#include <cctype>               // toupper, for single-letter key names
#include <string>

namespace eng {

// A single on/off flag shared by this file. `static` at file scope means it is
// private to this .cpp (other files can't see the variable directly). The two
// functions below are the controlled way to set and read it.
static bool s_scriptInputEnabled = true;
void SetScriptInputEnabled(bool enabled) { s_scriptInputEnabled = enabled; }
static bool ScriptInputEnabled() { return s_scriptInputEnabled; }

// Translate a friendly key name that a script uses ("W", "SPACE", "UP") into
// the numeric key code raylib expects. Unknown names return KEY_NULL, so a
// typo simply reads as "not pressed" instead of causing an error.
static int KeyFromName(const std::string& name) {
    if (name.size() == 1) {                          // single character like "W" or "5"
        char c = (char)toupper((unsigned char)name[0]);   // make it upper-case
        // raylib's letter key codes are consecutive, so KEY_A + offset works.
        if (c >= 'A' && c <= 'Z') return KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return KEY_ZERO + (c - '0');
    }
    if (name == "SPACE")  return KEY_SPACE;
    if (name == "ENTER")  return KEY_ENTER;
    if (name == "SHIFT")  return KEY_LEFT_SHIFT;
    if (name == "CTRL")   return KEY_LEFT_CONTROL;
    if (name == "UP")     return KEY_UP;
    if (name == "DOWN")   return KEY_DOWN;
    if (name == "LEFT")   return KEY_LEFT;
    if (name == "RIGHT")  return KEY_RIGHT;
    return KEY_NULL;
}

void RegisterInputBindings(sol::state& lua) {
    sol::table input = lua.create_named_table("input");
    // Each key query is ANDed with ScriptInputEnabled(), so when the editor
    // has closed the gate (you're typing, etc.) every key reads as not pressed.
    input["key_down"]    = [](const std::string& k) { return ScriptInputEnabled() && IsKeyDown(KeyFromName(k)); };
    input["key_pressed"] = [](const std::string& k) { return ScriptInputEnabled() && IsKeyPressed(KeyFromName(k)); };
}

void DescribeInputBindings(LuaApiRegistry& api) {
    auto in = api.Table("input");
    in.Fn("key_down(key) -> bool",
          "Whether a key is held right now. \"W\", \"SPACE\", \"SHIFT\", \"CTRL\", \"UP\"...");
    in.Fn("key_pressed(key) -> bool",
          "Whether a key went down THIS frame. For one-shot actions, not for movement");
}

} // namespace eng
