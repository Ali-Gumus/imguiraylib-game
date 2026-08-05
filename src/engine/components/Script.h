#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"
#include "sol/sol.hpp"

#include <memory>
#include <string>
#include <vector>

namespace eng {

// Write a starter script to `path`: a working file with every lifecycle hook
// already present and correctly spelled, plus a `properties` table.
//
// Returns false if the file already exists or could not be written. Refusing to
// overwrite is deliberate - this is a template, and no template is worth losing
// a script someone has written.
bool WriteScriptTemplate(const std::string& path);

// ============================================================================
// ScriptComponent: makes an entity run a Lua script file every frame.
// ----------------------------------------------------------------------------
// Each ScriptComponent owns its OWN Lua interpreter ("state"). That isolation
// means one script's variables can't clash with another's, and reloading one
// script never disturbs the others.
// ============================================================================
class ScriptComponent : public Component {   // ": public Component" = inherits it
public:
    // The label shown on this component's header in the Inspector.
    const char* Name() const override { return "Script"; }

    // Allow several scripts on one entity (for example one for movement and
    // one for shooting). `override` tells the compiler this replaces a virtual
    // function from the base class, and errors out if the signature is wrong.
    bool AllowMultiple() const override { return true; }

    // Copying carries over the file PATH and the tuned property values, but
    // not the live Lua state (an interpreter mid-run is runtime-only, and a
    // fresh copy loads its own when play begins).
    //
    // The property values MUST be copied. Pressing Play deep-copies the scene
    // and keeps that copy as the backup to restore on Stop, so anything a
    // Clone leaves behind is not merely missing from the copy - it is erased
    // from the project the moment playing stops.
    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<ScriptComponent>();
        c->path    = path;
        c->m_props = m_props;
        return c;
    }

    // Lifecycle hooks (bodies are in the .cpp). OnStart loads the file and
    // calls the script's onStart; OnUpdate calls its onUpdate each frame;
    // OnDestroy calls its onDestroy.
    void OnStart(Entity& owner) override;
    void OnDestroy(Entity& owner) override;
    void OnUpdate(float dt, Entity& owner) override;
    void OnCollision(Entity& owner, Entity& other, float speed,
                     Vector3 point) override;
    // Calls the script's optional onDrawHud(entity, w, h), where it may use
    // the `draw.*` API to put things on screen over the finished 3D view. This
    // is what lets a HUD element be authored in a script - and, because
    // GraphComponent derives from this class, in a node graph - instead of
    // needing C++ drawing code written for it.
    void OnDrawHud(const Entity& owner, int width, int height) override;

    // Save the script path, plus ONLY the properties this entity has actually
    // overridden in the Inspector.
    //
    // Saving all of them would be a trap: the moment a scene was saved, every
    // value in the script's `properties` table would be frozen into the scene
    // file, and from then on editing the .lua would appear to do nothing -
    // the stored copy always wins. Writing only real overrides means an
    // untouched property keeps following whatever the script says.
    void Serialize(nlohmann::json& out) const override {
        out["path"] = path;
        nlohmann::json props = nlohmann::json::object();
        for (const auto& pr : m_props)
            if (pr.overridden) props[pr.name] = pr.value;
        out["props"] = props;
    }
    void Deserialize(const nlohmann::json& in) override {
        path = in.value("path", path);
        m_props.clear();
        // Anything stored in the file was written because it differed from the
        // script, so it comes back as an override.
        if (in.contains("props") && in["props"].is_object())
            for (auto it = in["props"].begin(); it != in["props"].end(); ++it)
                m_props.push_back({it.key(), it.value().get<float>(), true});
    }

    // Draws the path field + a Load/Reload button + any error text.
    void OnInspector() override;

    // (Re)read the .lua file into a fresh Lua state. Errors do NOT crash the
    // program: they are captured into m_error and shown in the Inspector.
    void Load();

    // The script file to run. Public so the editor and spawn code can set it.
    std::string path = "assets/scripts/spin.lua";

    // The last load or compile error, or "" if the script is fine. Read-only to
    // callers; the Inspector shows it, and tools can report it.
    const char* ErrorText() const { return m_error.c_str(); }
    bool        IsLoaded()  const { return m_loaded; }

    // ---- Tunable properties -------------------------------------------------
    // The Inspector's fields go through these rather than reaching into m_props
    // itself, so that what a widget does and what the value MEANS are not the
    // same piece of code. It also makes the behaviour testable without a window,
    // which is how the revert bug below was pinned down.
    //
    // All of these work on the LIVE script: they write into the running
    // `properties` table and never reload, so changing a value cannot disturb
    // anything the script is remembering between frames.

    // Give this entity its own value, replacing the script's. Marks it as an
    // override, which is what gets saved to the scene.
    void SetProperty(const std::string& name, float value);

    // Drop the override and put back the value the script file gave, as of the
    // last load. NOT a reload - see the note in the .cpp.
    void RevertProperty(const std::string& name);
    void RevertAllProperties();

    // The current effective value, or 0 if there is no such property.
    float GetProperty(const std::string& name) const;

    // Whether this entity has overridden it.
    bool IsPropertyOverridden(const std::string& name) const;

    // Run this Lua SOURCE instead of reading `path`, when it is not empty.
    //
    // This is how a node graph runs without a .lua file existing: something
    // compiles the graph into source, hands it here, and the script loads from
    // memory. It is deliberately NOT serialized - generated code is not
    // something to store in a scene file, and leaving it out of Serialize is
    // what makes "compiled fresh on Play, never written to disk" the natural
    // behaviour rather than a special case.
    std::string source;

protected:
    // (Re)read the script's `properties` table into m_props and draw them as
    // Inspector fields. Split out so that a component whose source comes from
    // somewhere else - a graph, say - presents its tunables identically,
    // including the override markers and revert buttons.
    void DrawPropertiesInspector();

    sol::state m_lua;                       // this script's private Lua interpreter
    // Handles to the script's optional functions. A sol::protected_function
    // can be called safely: if the Lua code errors, we get an error result
    // instead of a crash.
    sol::protected_function m_onStart;
    sol::protected_function m_onUpdate;
    sol::protected_function m_onDestroy;
    sol::protected_function m_onCollision;
    sol::protected_function m_onDrawHud;
    bool        m_loaded = false;           // did the file load without error?
    std::string m_error;                    // last error message, "" if none

    // One tunable value the script exposed via its global `properties` table.
    struct ScriptProp {
        std::string name;
        float       value = 0.0f;
        // Has this entity been given its own value for this property, replacing
        // the script's? Only overrides are saved to the scene, and only
        // overrides survive a reload - everything else re-reads the script, so
        // editing a default in the .lua takes effect the next time it loads.
        bool        overridden = false;
        // What the script file said this value was, as of the last load. Kept so
        // that reverting an override can put the default back WITHOUT reloading
        // the script, which would throw away everything the script is currently
        // remembering. Refreshed by Load, so it tracks the file.
        float       scriptDefault = 0.0f;
    };

    // The script's tunables, sorted by name. Shown as editable fields in the
    // Inspector.
    std::vector<ScriptProp> m_props;
};
} // namespace eng
