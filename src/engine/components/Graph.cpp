#include "engine/components/Graph.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "engine/FileDialog.h"

namespace eng {
// ---- GraphComponent --------------------------------------------------------

// Whoever hosts the engine registers the graph code generator here. It stays
// null in a host that has no node editor, which GraphComponent reports plainly
// rather than failing in some confusing way.
static GraphCompiler s_graphCompiler = nullptr;

void SetGraphCompiler(GraphCompiler fn) { s_graphCompiler = fn; }
bool HasGraphCompiler() { return s_graphCompiler != nullptr; }

bool GraphComponent::CompileSource() {
    source.clear();
    if (graphPath.empty()) {
        m_error = "No graph chosen. Use New Graph or Open Graph.";
        return false;
    }
    if (!s_graphCompiler) {
        m_error = "No graph compiler available in this program.";
        return false;
    }
    std::string lua, err;
    if (!s_graphCompiler(graphPath, lua, err)) {
        m_error = err.empty() ? ("Could not compile " + graphPath) : err;
        return false;
    }
    source = lua;
    return true;
}

void GraphComponent::OnStart(Entity& owner) {
    // Compile fresh every time play begins, so the graph on disk is always what
    // runs. On failure the base class reports it through the same error field an
    // ordinary script would use.
    CompileSource();
    ScriptComponent::OnStart(owner);
}

bool GraphComponent::Recompile() {
    if (!CompileSource()) return false;
    Load();
    return m_loaded;
}

void GraphComponent::OnInspector() {
    // Which graph this entity runs. Editable as text so a path can be pasted,
    // like the Script component's path field.
    //
    // The label must NOT be "Graph". ImGui identifies a widget by its label
    // within the current scope, and the Inspector draws each component inside
    // one scope whose collapsing header is already labelled with the component's
    // name - "Graph" here. Reusing that string makes two widgets with the same
    // identity, which ImGui reports as a conflicting ID.
    char buf[256];
    strncpy(buf, graphPath.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Graph file", buf, sizeof(buf))) graphPath = buf;

    if (ImGui::Button("Open Graph...")) {
        std::string picked = OpenFileDialog(
            "Node graphs (*.json)\0*.json\0All files\0*.*\0", "json");
        if (!picked.empty()) graphPath = picked;
    }
    ImGui::SameLine();
    // Creating a graph and opening one for editing both need the node editor,
    // which this component cannot reach. It records the request; the editor
    // notices it and acts.
    if (ImGui::Button("New Graph")) newRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Edit in Node Editor")) editRequested = true;

    ImGui::SameLine();
    if (ImGui::Button("Recompile")) Recompile();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Compile the graph and reload it now, without leaving\n"
                          "play mode. Play always compiles fresh anyway.");

    // Status, in the same words the Script component uses.
    if (m_loaded)              ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "compiled");
    else if (!m_error.empty()) ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "error");
    else                       ImGui::TextDisabled("not compiled yet");
    if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());

    ImGui::TextDisabled("Compiled to Lua in memory on Play; no file is written.");

    // The graph's Param nodes become properties, shown exactly as a script's are.
    DrawPropertiesInspector();
}

} // namespace eng
