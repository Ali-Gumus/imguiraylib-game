#include "engine/ModelDefs.h"

#include "engine/Components.h"   // ModelComponent
#include "engine/Scene.h"        // Entity

#include "sol/sol.hpp"           // runs models.lua, where the definitions live

#include <algorithm>             // std::sort
#include <unordered_map>

namespace eng {

static std::unordered_map<std::string, ModelDef> s_defs;
static std::vector<std::string> s_names;    // sorted, for the node editor
static std::string s_error;

const char* ModelDefError() { return s_error.c_str(); }
const std::vector<std::string>& ModelDefNames() { return s_names; }

const ModelDef* FindModelDef(const std::string& name) {
    auto it = s_defs.find(name);
    return it == s_defs.end() ? nullptr : &it->second;
}

bool ReloadModelDefs() {
    s_error.clear();

    // A throwaway interpreter with exactly one function bound. The definitions
    // file is DATA, not gameplay: it gets no access to the scene, no entity,
    // and no way to do anything but describe models. Never attach it to an
    // entity - the same rule effects.lua and sounds.lua follow.
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                       sol::lib::string);

    // Collect into a fresh map, and only swap it in if the whole file ran. A
    // syntax error halfway through would otherwise leave half the models
    // defined and half missing, which is harder to diagnose than none.
    std::unordered_map<std::string, ModelDef> parsed;

    sol::table mdl = lua.create_named_table("model");
    // model.define(name, settings). Any field left out keeps a sensible
    // default, so the simplest definition is a name and a file.
    mdl["define"] = [&parsed](const std::string& name, sol::table t) {
        ModelDef d;
        d.name = name;

        sol::optional<std::string> file = t["file"];
        d.file = file.value_or(std::string());

        sol::optional<float> sc = t["scale"];
        d.scale = sc.value_or(1.0f);

        // `rot` and `pos` are three-element tables, which reads far better in
        // the data file than six loose numbers would.
        auto vec3 = [&t](const char* key, Vector3 fallback) -> Vector3 {
            sol::optional<sol::table> v = t[key];
            if (!v) return fallback;
            sol::table tv = *v;
            return { tv[1].get_or(fallback.x),
                     tv[2].get_or(fallback.y),
                     tv[3].get_or(fallback.z) };
        };
        d.rotationOffset = vec3("rot", {0, 0, 0});
        d.positionOffset = vec3("pos", {0, 0, 0});

        parsed[name] = d;
    };

    sol::protected_function_result r =
        lua.safe_script_file("assets/scripts/models.lua", sol::script_pass_on_error);
    if (!r.valid()) {
        s_error = r.get<sol::error>().what();
        // Keep whatever was loaded before. A broken edit should not delete the
        // models that were working a moment ago.
        return false;
    }

    s_defs = std::move(parsed);

    s_names.clear();
    s_names.reserve(s_defs.size());
    for (const auto& kv : s_defs) s_names.push_back(kv.first);
    std::sort(s_names.begin(), s_names.end());
    return true;
}

bool ApplyModelDef(Entity& e, const std::string& name) {
    const ModelDef* d = FindModelDef(name);
    if (d == nullptr || d->file.empty()) return false;

    // Add-only-if-missing: an entity authored with a model in the editor keeps
    // it. Only something built at runtime, which has none, gets one from here.
    if (e.GetComponent<ModelComponent>() != nullptr) return false;

    ModelComponent& mc = e.AddComponent<ModelComponent>();
    mc.SetPath(d->file);
    mc.rotationOffset = d->rotationOffset;
    mc.positionOffset = d->positionOffset;

    // Scale belongs to the entity rather than the model, because it is what
    // everything else measures against - the collider, the flight speeds, the
    // distance at which the model is worth drawing.
    e.transform.scale = {d->scale, d->scale, d->scale};
    return true;
}

} // namespace eng
