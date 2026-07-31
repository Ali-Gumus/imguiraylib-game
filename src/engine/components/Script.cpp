#include "engine/components/Script.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "engine/Components.h"
#include "engine/LuaBindings.h"
#include "engine/FileDialog.h"

namespace eng {
// ---- ScriptComponent -------------------------------------------------------

// (Re)load this component's Lua file, building a brand-new interpreter and
// re-exposing the C++ API to it. Safe to call repeatedly (that's "reload").
void ScriptComponent::Load() {
    // The m_onStart/Update/Destroy handles point INTO the current m_lua. We
    // must clear those handles before replacing m_lua, otherwise destroying
    // the old interpreter would try to clean up references that still think
    // they live in it. Assigning {} makes each handle empty.
    m_onStart   = {};
    m_onUpdate  = {};
    m_onDestroy = {};
    m_onCollision = {};
    m_onDrawHud   = {};
    m_lua       = sol::state{};   // a fresh, empty Lua interpreter
    m_loaded   = false;
    // Only clear a previous error when there is actually something to run. A
    // component with neither source nor path has usually just failed to produce
    // any (a graph that would not compile), and wiping the reason would leave
    // the Inspector saying nothing at all.
    if (!source.empty() || !path.empty()) m_error.clear();
    if (source.empty() && path.empty()) {
        if (m_error.empty()) m_error = "Nothing to run: no script file and no source.";
        return;
    }

    // Open only a safe subset of Lua's standard library. We deliberately do
    // NOT open `io` or `os`, so a script cannot read or delete files. This is
    // a basic sandbox.
    m_lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                         sol::lib::string);

    // --- Furnish the state with the engine's scripting API ------------------
    // Each of these lives in its own file under src/engine/bindings/, next to
    // the description of what it exposes. Nothing here knows what any of them
    // contain, which is the point: adding a subject to the API touches its own
    // file and this list, and nothing else.
    //
    // ORDER MATTERS in one direction only - a usertype must exist before
    // anything hands one to Lua - so the types go first and the tables follow.
    // GetLuaApiEntries in LuaApiRegistry.cpp lists the descriptions in this same
    // order, so the catalogue reads the way a script meets the API.
    RegisterTransformBindings(m_lua);
    RegisterEntityBindings(m_lua);
    RegisterInputBindings(m_lua);
    RegisterHudBindings(m_lua);
    RegisterDrawBindings(m_lua);
    RegisterFxBindings(m_lua);
    RegisterAudioBindings(m_lua);
    RegisterPhysicsBindings(m_lua);
    RegisterLightBindings(m_lua);
    RegisterSceneBindings(m_lua);
    // Camera last: it hangs createCamera off the Scene table, which must exist.
    RegisterHealthBindings(m_lua);
    RegisterRigidBodyBindings(m_lua);
    RegisterColliderBindings(m_lua);
    RegisterModelBindings(m_lua);
    RegisterShapeBindings(m_lua);
    RegisterTerrainBindings(m_lua);
    RegisterMinimapBindings(m_lua);
    RegisterScriptBindings(m_lua);
    RegisterCameraBindings(m_lua);

    // --- Actually run the script -------------------------------------------
    // Either from a string in memory or from a file on disk. The "pass on
    // error" form means a mistake in the script comes back as an invalid result
    // instead of throwing, so a bad script shows an error rather than crashing.
    //
    // The source path exists for generated code - a node graph compiled at Play
    // - which has no file and should not have one.
    sol::protected_function_result r =
        source.empty() ? m_lua.safe_script_file(path, sol::script_pass_on_error)
                       : m_lua.safe_script(source, sol::script_pass_on_error);
    if (!r.valid()) {
        m_error = r.get<sol::error>().what();   // human-readable message + line
        return;                                  // stay unloaded
    }

    // Fetch the optional lifecycle functions the script may have defined.
    // Any that the script didn't define come back empty and are simply never
    // called.
    m_onStart   = m_lua["onStart"];
    m_onUpdate  = m_lua["onUpdate"];
    m_onDestroy = m_lua["onDestroy"];
    m_onCollision = m_lua["onCollision"];
    m_onDrawHud   = m_lua["onDrawHud"];

    // Read the optional global `properties` table: each numeric entry becomes
    // an editable field in the Inspector. We keep any value the user already
    // tuned (stored in m_props) instead of resetting it to the script default,
    // and write the effective value back into the table so the script uses it.
    std::vector<ScriptProp> merged;
    sol::object propObj = m_lua["properties"];
    if (propObj.is<sol::table>()) {
        sol::table pt = propObj.as<sol::table>();
        for (auto& kv : pt) {
            if (kv.first.is<std::string>() && kv.second.is<double>()) {
                std::string name  = kv.first.as<std::string>();
                float       value = kv.second.as<float>();   // the script's default
                bool        over  = false;

                // An OVERRIDDEN property keeps the value this entity was given.
                // Anything else takes the script's current default, so editing
                // the .lua actually shows up the next time the script loads.
                for (const auto& pr : m_props) {
                    if (pr.name == name && pr.overridden) {
                        value = pr.value;
                        over  = true;
                        break;
                    }
                }

                pt[name] = value;                    // the script reads this back
                merged.push_back({name, value, over});
            }
        }
        // Sort by name so the fields keep a stable order in the Inspector
        // (a Lua table has no defined iteration order).
        std::sort(merged.begin(), merged.end(),
                  [](const ScriptProp& a, const ScriptProp& b) { return a.name < b.name; });
    }
    m_props = std::move(merged);

    m_loaded = true;
}

// Call one Lua hook safely. If it isn't loaded or doesn't exist, do nothing.
// If calling it errors, record the message and mark the script unloaded so the
// broken hook isn't called again every frame. This is a template so it accepts
// any set of arguments to forward to the Lua function.
template <typename... Args>
static void CallHook(sol::protected_function& fn, bool& loaded,
                     std::string& error, Args&&... args) {
    if (!loaded || !fn.valid()) return;
    sol::protected_function_result r = fn(std::forward<Args>(args)...);
    if (!r.valid()) {
        error  = r.get<sol::error>().what();
        loaded = false;
    }
}

void ScriptComponent::OnStart(Entity& owner) {
    Load();   // (re)load the file first, then run its onStart
    // `owner` is passed by reference, so the script edits the real entity.
    CallHook(m_onStart, m_loaded, m_error, owner);
}

void ScriptComponent::OnUpdate(float dt, Entity& owner) {
    CallHook(m_onUpdate, m_loaded, m_error, owner, dt);
}

void ScriptComponent::OnDestroy(Entity& owner) {
    CallHook(m_onDestroy, m_loaded, m_error, owner);
}

void ScriptComponent::OnDrawHud(const Entity& owner, int width, int height) {
    // The const is dropped to hand Lua the same Entity type every other hook
    // gets. A script COULD move the entity from here, which would be a poor
    // idea - drawing is not the place to change the world, and it would run
    // once per visible view rather than once per frame - but presenting a
    // different entity type from this one hook would be a worse surprise.
    CallHook(m_onDrawHud, m_loaded, m_error, const_cast<Entity&>(owner),
             (float)width, (float)height);
}

void ScriptComponent::OnCollision(Entity& owner, Entity& other, float speed,
                                  Vector3 point) {
    // The contact point is passed as three plain numbers rather than a Vector3
    // usertype, matching how the rest of this API talks to Lua (fx.burst and
    // scene.spawn both take loose x, y, z). A script signature therefore reads
    //     function onCollision(entity, other, speed, x, y, z)
    // and may ignore any trailing argument it does not need - Lua simply drops
    // extra arguments, so an onCollision(entity, other, speed) works too.
    CallHook(m_onCollision, m_loaded, m_error, owner, other, speed,
             point.x, point.y, point.z);
}

void ScriptComponent::OnInspector() {
    // Load the script the first time it's shown (unless it already failed), so
    // its properties appear without pressing a button. This only runs the
    // script's top level, not the per-frame hooks.
    if (!m_loaded && m_error.empty() && !path.empty())
        Load();

    // ImGui edits text through a fixed char buffer, not a std::string, so we
    // copy the path into a buffer, let the user edit it, then copy back.
    char buf[256];
    strncpy(buf, path.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';                 // ensure it ends with a 0 byte
    if (ImGui::InputText("Path", buf, sizeof(buf)))
        path = buf;

    // A button that opens the native file picker filtered to .lua files.
    if (ImGui::Button("Browse...")) {
        std::string picked = OpenFileDialog(
            "Lua scripts (*.lua)\0*.lua\0All files\0*.*\0", "lua");
        if (!picked.empty()) {        // empty string = the user cancelled
            path = picked;
            Load();                   // choosing a file also loads it
        }
    }
    ImGui::SameLine();               // keep the next widget on the same row
    if (ImGui::Button("Load / Reload")) Load();

    // A colored status word next to the buttons.
    ImGui::SameLine();
    if (m_loaded)              ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "loaded");
    else if (!m_error.empty()) ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "error");
    else                       ImGui::TextDisabled("not loaded");

    // If there was an error, print the full message, wrapped to the panel.
    if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());

    DrawPropertiesInspector();
}

// The script's exposed properties, as editable fields. Editing one writes the
// new value straight into the running script's `properties` table, so changes
// take effect immediately, including mid-play.
//
// This lives on its own so that any component backed by a Lua script presents
// its tunables the same way, whether the script came from a file or was
// generated from a node graph.
void ScriptComponent::DrawPropertiesInspector() {
    if (m_loaded && !m_props.empty()) {
        ImGui::SeparatorText("Properties");
        // Any property this entity has overridden shows a revert arrow and an
        // orange name. Without that mark there is no way to tell a value that
        // came from the script from one this entity is holding on to - which is
        // exactly the confusion of editing a .lua and seeing nothing change.
        bool anyOverride = false;

        for (int i = 0; i < (int)m_props.size(); i++) {
            ScriptProp& pr = m_props[i];
            // Two widgets share a row and would collide as ImGui identities,
            // since ImGui names widgets by their label. PushID makes this row's
            // widgets unique regardless of the property's name.
            ImGui::PushID(i);

            // The revert button, drawn first so the numbers still line up.
            if (pr.overridden) {
                anyOverride = true;
                if (ImGui::SmallButton("<")) {
                    // Drop the override and take the script's value again. The
                    // script is reloaded so the default is re-read from the
                    // file rather than remembered from before.
                    pr.overridden = false;
                    Load();
                    ImGui::PopID();
                    break;         // Load() rebuilt m_props; stop walking it
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Revert to the value in the script file");
                ImGui::SameLine();
            }

            if (ImGui::DragFloat(pr.name.c_str(), &pr.value, 0.05f)) {
                // Editing a field is what makes it an override: from now on
                // this entity keeps its own value and ignores the script's.
                pr.overridden = true;
                m_lua["properties"][pr.name] = pr.value;
            }
            // Colour the overridden ones so the difference is visible at a
            // glance, the way a changed setting is marked anywhere else.
            if (pr.overridden) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "*");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Overridden here; the script file is ignored\n"
                                      "for this value until you revert it.");
            }
            ImGui::PopID();
        }

        if (anyOverride && ImGui::SmallButton("Revert all to script")) {
            for (auto& pr : m_props) pr.overridden = false;
            Load();
        }
    }
}

} // namespace eng
