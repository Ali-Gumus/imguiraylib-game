#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"
#include "engine/components/Script.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {
// ============================================================================
// GraphComponent: an entity whose behaviour is a NODE GRAPH rather than a
// hand-written script.
// ----------------------------------------------------------------------------
// It derives from ScriptComponent because that is exactly what it is: a script
// whose source happens to be generated. Everything about running Lua - the
// private interpreter, the lifecycle hooks, the `properties` table and its
// Inspector fields with their override markers - is inherited unchanged. The
// only thing this adds is where the source comes from.
//
// The graph is compiled to Lua IN MEMORY when play begins. No .lua file is
// written, ever: a generated file is a copy that can drift from the graph that
// produced it, and there is no reason to keep one when the graph is right there.
//
// Compiling needs the node editor's code generator, which lives in the editor,
// while this component lives in the engine - and the engine must not depend on
// the editor. So the engine declares the seam (SetGraphCompiler below) and
// whoever hosts the engine fills it in.
// ============================================================================

// Turns a graph file into Lua source. Returns false and fills `outError` if the
// graph cannot be read.
using GraphCompiler = bool (*)(const std::string& graphPath,
                               std::string& outLua, std::string& outError);

// Register the function used to compile graphs. Called once by the host (the
// editor does it at startup). With none registered, a GraphComponent reports
// that plainly instead of failing in some obscure way.
void SetGraphCompiler(GraphCompiler fn);
bool HasGraphCompiler();

class GraphComponent : public ScriptComponent {
public:
    const char* Name() const override { return "Graph"; }
    bool AllowMultiple() const override { return true; }

    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<GraphComponent>();
        c->graphPath = graphPath;
        c->m_props   = m_props;   // tuned values survive Play/Stop, as for scripts
        return c;
    }

    void OnStart(Entity& owner) override;
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["graph"] = graphPath;
        nlohmann::json props = nlohmann::json::object();
        for (const auto& pr : m_props)
            if (pr.overridden) props[pr.name] = pr.value;
        out["props"] = props;
    }
    void Deserialize(const nlohmann::json& in) override {
        graphPath = in.value("graph", graphPath);
        m_props.clear();
        if (in.contains("props") && in["props"].is_object())
            for (auto it = in["props"].begin(); it != in["props"].end(); ++it)
                m_props.push_back({it.key(), it.value().get<float>(), true});
    }

    // Compile the graph and load the result. Returns false on failure, with the
    // reason in the inherited error field, which the Inspector already shows.
    bool Recompile();

    // The graph file this entity runs, e.g. "assets/graphs/enemy_graph.json".
    std::string graphPath;

    // Set by the Inspector's buttons and cleared by the editor once it has
    // acted. Opening a graph in the node editor, and creating a new one, both
    // need the editor - so rather than the engine reaching into it, these just
    // record that the user asked. Runtime only; never serialized.
    bool editRequested = false;
    bool newRequested  = false;

    GraphComponent() { path.clear(); }   // a graph has no .lua path of its own

protected:
    // Fill `source` by compiling the graph. Reports why in m_error if it cannot.
    bool CompileSource();
};
} // namespace eng
