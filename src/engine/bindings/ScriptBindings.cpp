#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/components/Script.h"

#include <string>

namespace eng {

void RegisterScriptBindings(sol::state& lua) {
    // A script reaching another entity's Script component - to check it loaded,
    // or to point it at a different file.
    //
    // `source` is deliberately NOT exposed. That field holds code generated
    // from a node graph, and letting a script overwrite it would leave the
    // component running something the graph cannot reproduce, with no file to
    // look at when it misbehaves.
    lua.new_usertype<ScriptComponent>("Script",
        "path",     &ScriptComponent::path,
        "isLoaded", sol::property(&ScriptComponent::IsLoaded),
        "reload",   &ScriptComponent::Load
    );
    RegisterComponentAccess<ScriptComponent>(lua, "Script");
}

void DescribeScriptBindings(LuaApiRegistry& api) {
    api.Usertype("Entity", "entity")
        .Method("addComponent_Script() -> Script", "Add a Script and return it")
        .Method("getComponent_Script() -> Script", "Its Script, or nil");

    auto s = api.Usertype("Script", "script");
    s.Prop("path",     "The .lua file it runs. Assign, then call reload()");
    s.Prop("isLoaded", "Whether it loaded without error. Read-only");
    s.Method("reload()", "Re-read the file into a fresh interpreter");
}

} // namespace eng
