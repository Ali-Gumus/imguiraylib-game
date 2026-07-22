#include "ScriptGraph.h"

#include "imgui.h"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <functional>   // std::function, used for a recursive lambda
#include <cstring>

namespace ed = ax::NodeEditor;   // short alias for the node-editor library
using nlohmann::json;

namespace edtr {

// The human-readable title drawn at the top of each node.
static const char* TypeTitle(NodeType t) {
    switch (t) {
        case NodeType::EventCreate:  return "On Create";
        case NodeType::EventUpdate:  return "On Update";
        case NodeType::EventDestroy: return "On Destroy";
        case NodeType::ActionSpin:   return "Spin";
        case NodeType::ActionMove:   return "Move";
        case NodeType::ActionPrint:  return "Print";
        case NodeType::CondKey:      return "If Key";
        case NodeType::CondEvery:    return "Every X s";
        case NodeType::ActionDestroySelf: return "Destroy Self";
        case NodeType::ActionSpawnCube:   return "Spawn Cube";
        case NodeType::ActionMoveForward: return "Move Fwd";
    }
    return "?";
}

// True for the three entry-point node types.
static bool IsEvent(NodeType t) {
    return t == NodeType::EventCreate || t == NodeType::EventUpdate ||
           t == NodeType::EventDestroy;
}

ScriptGraph::ScriptGraph() { Reset(); }

void ScriptGraph::Reset() {
    m_nodes.clear();
    m_links.clear();
    m_nextID = 100;   // ids 1..99 are reserved for the fixed event nodes
    // The three event nodes always exist and can't be added or deleted; they
    // are the fixed entry points a graph is built from. They use ids 1, 2, 3.
    m_nodes.push_back({1, NodeType::EventCreate});
    m_nodes.push_back({2, NodeType::EventUpdate});
    m_nodes.push_back({3, NodeType::EventDestroy});
    m_nodes[0].y = 0;  m_nodes[1].y = 100;  m_nodes[2].y = 200;   // stack them vertically
    m_restorePositions = true;   // ask Draw to place them on the canvas
}

// Look a node up by id. Two versions: one that can modify it, one read-only.
GraphNode* ScriptGraph::FindNode(int id) {
    for (auto& n : m_nodes) if (n.id == id) return &n;
    return nullptr;
}
const GraphNode* ScriptGraph::FindNode(int id) const {
    for (auto& n : m_nodes) if (n.id == id) return &n;
    return nullptr;
}

// Return every node connected downstream of `fromNodeId`. A node's output can
// link to several inputs, so this returns a list (the flow can branch).
std::vector<const GraphNode*> ScriptGraph::NextInChain(int fromNodeId) const {
    std::vector<const GraphNode*> out;
    for (const auto& l : m_links)
        if (PinToNode(l.fromPin) == fromNodeId)
            if (const GraphNode* n = FindNode(PinToNode(l.toPin)))
                out.push_back(n);
    return out;
}

// Draw a single node: its title, its pins, and its editable parameter fields.
void ScriptGraph::DrawNode(GraphNode& n) {
    // After loading (or adding a node), push the saved position onto the canvas.
    if (m_restorePositions)
        ed::SetNodePosition(n.id, ImVec2(n.x, n.y));

    ed::BeginNode(n.id);
    // Widgets inside a node share ImGui's single pool of widget ids. Two Spin
    // nodes would each draw a "deg/s" field with the same id and clash, so we
    // scope every node's widgets under its unique node id.
    ImGui::PushID(n.id);
    ImGui::TextUnformatted(TypeTitle(n.type));

    // Pins are the connection points. Event nodes have only an OUTPUT ("then
    // do this..."). Action/condition nodes also have an INPUT so they can be
    // chained after something.
    if (!IsEvent(n.type)) {
        ed::BeginPin(InPin(n.id), ed::PinKind::Input);
        ImGui::TextUnformatted("-> in");
        ed::EndPin();
        ImGui::SameLine(0, 40);
    }
    ed::BeginPin(OutPin(n.id), ed::PinKind::Output);
    ImGui::TextUnformatted("out ->");
    ed::EndPin();

    // The editable fields, which differ per node type. PushItemWidth keeps the
    // fields a sensible width inside the node.
    ImGui::PushItemWidth(140);
    switch (n.type) {
        case NodeType::ActionSpin:
            ImGui::DragFloat("deg/s", &n.speed, 1.0f);
            break;
        case NodeType::ActionMoveForward:
            ImGui::DragFloat("units/s", &n.speed, 0.1f);
            break;
        case NodeType::ActionMove:
            ImGui::DragFloat3("units/s", n.offset, 0.1f);
            break;
        case NodeType::ActionPrint:
            ImGui::InputText("msg", n.text, sizeof(n.text));
            break;
        case NodeType::CondKey:
            ImGui::InputText("key", n.key, sizeof(n.key));
            break;
        case NodeType::CondEvery:
            ImGui::DragFloat("seconds", &n.interval, 0.1f, 0.05f, 3600.0f);
            break;
        case NodeType::ActionSpawnCube:
            ImGui::InputText("name", n.text, sizeof(n.text));
            ImGui::DragFloat3("offset", n.offset, 0.1f);
            break;
        default: break;   // some nodes (e.g. Destroy Self) have no parameters
    }
    ImGui::PopItemWidth();
    ImGui::PopID();
    ed::EndNode();

    // Read back where the node currently sits so we can save it later.
    ImVec2 pos = ed::GetNodePosition(n.id);
    n.x = pos.x;  n.y = pos.y;
}

// Handle the user creating and deleting links and nodes for this frame.
void ScriptGraph::HandleEdits() {
    // --- Link creation --------------------------------------------------
    // While the user drags from one pin toward another, the library asks us
    // whether that link is allowed. We validate it; the library draws the
    // rubber-band line.
    if (ed::BeginCreate()) {
        ed::PinId a, b;
        if (ed::QueryNewLink(&a, &b)) {
            int pa = (int)a.Get(), pb = (int)b.Get();
            // Make sure pa is the OUTPUT side and pb the INPUT side. Recall
            // input pins are id*4+1 (so pin % 4 == 1) and outputs are id*4+2.
            if (pa % 4 == 1) std::swap(pa, pb);

            bool valid = pa % 4 == 2 && pb % 4 == 1           // must be output -> input
                      && PinToNode(pa) != PinToNode(pb);      // not a node to itself
            // One output may feed several inputs (so On Update can branch into
            // several parallel chains). AcceptNewItem confirms the drop.
            if (valid && ed::AcceptNewItem())
                m_links.push_back({m_nextID++, pa, pb});
            if (!valid) ed::RejectNewItem();
        }
    }
    ed::EndCreate();

    // --- Deletion (the user selected something and pressed Delete) ------
    if (ed::BeginDelete()) {
        // Deleted links.
        ed::LinkId lid;
        while (ed::QueryDeletedLink(&lid)) {
            if (ed::AcceptDeletedItem())
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                    [&](const GraphLink& l) { return l.id == (int)lid.Get(); }),
                    m_links.end());
        }
        // Deleted nodes.
        ed::NodeId nid;
        while (ed::QueryDeletedNode(&nid)) {
            GraphNode* n = FindNode((int)nid.Get());
            // Refuse to delete the fixed event nodes.
            if (n && IsEvent(n->type)) { ed::RejectDeletedItem(); continue; }
            if (ed::AcceptDeletedItem()) {
                int id = (int)nid.Get();
                // Remove any links attached to the node, then the node itself.
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                    [id](const GraphLink& l) {
                        return PinToNode(l.fromPin) == id || PinToNode(l.toPin) == id;
                    }), m_links.end());
                m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(),
                    [id](const GraphNode& n) { return n.id == id; }),
                    m_nodes.end());
            }
        }
    }
    ed::EndDelete();
}

// The right-click "add a node" menu.
void ScriptGraph::HandleContextMenu() {
    // Convert the mouse position from screen pixels to canvas coordinates NOW,
    // while the canvas transform is active. (Between Suspend and Resume below
    // that transform is turned off, and asking for the conversion there is
    // invalid.)
    ImVec2 canvasMouse = ed::ScreenToCanvas(ImGui::GetMousePos());

    // ImGui popups must be drawn in normal screen space, not on the zoomable
    // canvas. Suspend() steps out of canvas space; Resume() steps back in.
    ed::Suspend();
    // ShowBackgroundContextMenu stays true the whole time the menu is open, so
    // we record the click position only at the instant it first opens (guarded
    // by IsPopupOpen). Otherwise the position would follow the mouse onto the
    // menu and the new node would appear in the wrong place.
    if (ed::ShowBackgroundContextMenu() && !ImGui::IsPopupOpen("AddNode")) {
        m_popupX = canvasMouse.x;
        m_popupY = canvasMouse.y;
        ImGui::OpenPopup("AddNode");
    }

    if (ImGui::BeginPopup("AddNode")) {
        NodeType picked{};
        bool add = false;
        // Each menu item selects a node type to create.
        if (ImGui::MenuItem("Spin"))  { picked = NodeType::ActionSpin;  add = true; }
        if (ImGui::MenuItem("Move"))  { picked = NodeType::ActionMove;  add = true; }
        if (ImGui::MenuItem("Print")) { picked = NodeType::ActionPrint; add = true; }
        if (ImGui::MenuItem("Move Forward")) { picked = NodeType::ActionMoveForward; add = true; }
        if (ImGui::MenuItem("Destroy Self")) { picked = NodeType::ActionDestroySelf; add = true; }
        if (ImGui::MenuItem("Spawn Cube"))   { picked = NodeType::ActionSpawnCube;   add = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("If Key"))    { picked = NodeType::CondKey;   add = true; }
        if (ImGui::MenuItem("Every X s")) { picked = NodeType::CondEvery; add = true; }
        if (add) {
            GraphNode n;
            n.id   = m_nextID++;
            n.type = picked;
            n.x = m_popupX;  n.y = m_popupY;   // place it where the user clicked
            m_nodes.push_back(n);
            // We can't set the on-canvas position here (we're between Suspend
            // and Resume). Ask Draw to apply the saved x/y on the next frame.
            m_restorePositions = true;
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

// Draw the whole graph for one frame and process interactions.
void ScriptGraph::Draw(ed::EditorContext* ctx) {
    ed::SetCurrentEditor(ctx);   // tell the library which canvas we're drawing
    ed::Begin("ScriptGraph");

    for (auto& n : m_nodes) DrawNode(n);
    m_restorePositions = false;  // positions have now been applied

    // Draw a line for each link.
    for (const auto& l : m_links)
        ed::Link(l.id, l.fromPin, l.toPin);

    HandleEdits();
    HandleContextMenu();

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

// ---- Saving and loading the graph as JSON ---------------------------------

bool ScriptGraph::Save(const std::string& path) const {
    json doc;
    doc["nextID"] = m_nextID;
    // Write every node with all of its fields.
    for (const auto& n : m_nodes)
        doc["nodes"].push_back({{"id", n.id}, {"type", (int)n.type},
                                {"x", n.x}, {"y", n.y},
                                {"speed", n.speed},
                                {"offset", {n.offset[0], n.offset[1], n.offset[2]}},
                                {"text", n.text}, {"key", n.key},
                                {"interval", n.interval}});
    // Write every link.
    for (const auto& l : m_links)
        doc["links"].push_back({{"id", l.id}, {"from", l.fromPin}, {"to", l.toPin}});

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return false;
    f << doc.dump(2);   // pretty-printed JSON
    return true;
}

bool ScriptGraph::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    json doc = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) return false;   // not valid JSON: give up

    m_nodes.clear();
    m_links.clear();
    for (const json& jn : doc.value("nodes", json::array())) {
        GraphNode n;
        n.id    = jn.value("id", 0);
        n.type  = (NodeType)jn.value("type", 0);
        n.x     = jn.value("x", 0.0f);
        n.y     = jn.value("y", 0.0f);
        n.speed = jn.value("speed", 90.0f);
        if (jn.contains("offset"))
            for (int i = 0; i < 3; ++i) n.offset[i] = jn["offset"][i];
        // Copy the strings into the node's fixed-size char arrays.
        std::strncpy(n.text, jn.value("text", "").c_str(), sizeof(n.text) - 1);
        std::strncpy(n.key,  jn.value("key", "W").c_str(), sizeof(n.key) - 1);
        n.interval = jn.value("interval", 1.0f);
        m_nodes.push_back(n);
    }
    for (const json& jl : doc.value("links", json::array()))
        m_links.push_back({jl.value("id", 0), jl.value("from", 0), jl.value("to", 0)});

    m_nextID = doc.value("nextID", 100);
    m_restorePositions = true;   // place the loaded nodes on the canvas next frame
    return true;
}

// ---- Turning the graph into a Lua script ----------------------------------

// Escape a string so it can sit safely inside a Lua "..." literal: a quote or
// backslash in the user's text gets a backslash in front of it.
static std::string EscapeLua(const char* s) {
    std::string out;
    for (const char* p = s; *p; ++p) {
        if (*p == '"' || *p == '\\') out += '\\';
        out += *p;
    }
    return out;
}

// Append the Lua for a single ACTION node to the growing script text.
static void EmitAction(std::string& lua, const GraphNode& n) {
    char buf[256];   // temporary buffer for formatted lines
    switch (n.type) {
        case NodeType::ActionSpin:
            // Rotate around the local Y axis (quaternion-based, so it's safe to
            // accumulate every frame).
            snprintf(buf, sizeof(buf),
                "    entity.transform:rotate(0, 1, 0, %.3f * dt)\n", n.speed);
            lua += buf;
            break;
        case NodeType::ActionMoveForward:
            // Move along the entity's own facing (local -Z is forward).
            snprintf(buf, sizeof(buf),
                "    entity.transform:translate_local(0, 0, -%.3f * dt)\n", n.speed);
            lua += buf;
            break;
        case NodeType::ActionMove:
            // Move by a fixed offset in world axes.
            snprintf(buf, sizeof(buf),
                "    entity.transform.position.x = entity.transform.position.x + %.3f * dt\n"
                "    entity.transform.position.y = entity.transform.position.y + %.3f * dt\n"
                "    entity.transform.position.z = entity.transform.position.z + %.3f * dt\n",
                n.offset[0], n.offset[1], n.offset[2]);
            lua += buf;
            break;
        case NodeType::ActionPrint:
            lua += "    print(\"" + EscapeLua(n.text) + "\")\n";
            break;
        case NodeType::ActionDestroySelf:
            lua += "    scene.destroy(entity)\n";
            break;
        case NodeType::ActionSpawnCube:
            snprintf(buf, sizeof(buf),
                "    scene.spawn_cube(\"%s\",\n"
                "        entity.transform.position.x + %.3f,\n"
                "        entity.transform.position.y + %.3f,\n"
                "        entity.transform.position.z + %.3f)\n",
                EscapeLua(n.text).c_str(), n.offset[0], n.offset[1], n.offset[2]);
            lua += buf;
            break;
        default: break;
    }
}

bool ScriptGraph::GenerateLua(const std::string& path) const {
    std::string lua =
        "-- GENERATED from a node graph. Edit the GRAPH, not this file --\n"
        "-- your changes here are overwritten on the next generate.\n\n";

    // "Every X s" nodes need a counter that survives between frames. Declare
    // one file-scope Lua variable per such node, named after its id.
    for (const auto& n : m_nodes)
        if (n.type == NodeType::CondEvery) {
            char buf[64];
            snprintf(buf, sizeof(buf), "local acc_%d = 0\n", n.id);
            lua += buf;
        }
    lua += "\n";

    // Describes each of the three event functions we might generate.
    struct Event { int nodeId; const char* header; const char* dt; };
    // on_start and on_destroy run only once, where a per-frame `dt` has no
    // meaning, so we define dt = 1 there so time-scaled actions still work.
    const Event events[] = {
        {1, "function on_start(entity)\n",   "    local dt = 1\n"},
        {2, "function on_update(entity, dt)\n", ""},
        {3, "function on_destroy(entity)\n", "    local dt = 1\n"},
    };

    // A recursive helper that writes a node and then everything chained after
    // it. Condition nodes wrap only their own downstream chain in an `if`;
    // separate branches off the same output become independent statements. The
    // `depth` limit stops a looped graph from recursing forever.
    // std::function lets a lambda call itself by name.
    std::function<void(std::string&, const GraphNode&, int)> emit =
        [&](std::string& lua, const GraphNode& n, int depth) {
            if (depth > 32) return;
            if (n.type == NodeType::CondKey) {
                lua += "    if input.key_down(\"" + EscapeLua(n.key) + "\") then\n";
                for (const GraphNode* next : NextInChain(n.id))
                    emit(lua, *next, depth + 1);
                lua += "    end\n";
            } else if (n.type == NodeType::CondEvery) {
                // Add the frame time to the counter; when it passes the
                // interval, subtract the interval (rather than resetting to 0,
                // so leftover time carries over and the average rate stays
                // exact) and run the branch.
                char buf[160];
                snprintf(buf, sizeof(buf),
                    "    acc_%d = acc_%d + dt\n"
                    "    if acc_%d >= %.3f then\n"
                    "    acc_%d = acc_%d - %.3f\n",
                    n.id, n.id, n.id, n.interval, n.id, n.id, n.interval);
                lua += buf;
                for (const GraphNode* next : NextInChain(n.id))
                    emit(lua, *next, depth + 1);
                lua += "    end\n";
            } else {
                EmitAction(lua, n);
                for (const GraphNode* next : NextInChain(n.id))
                    emit(lua, *next, depth + 1);
            }
        };

    // Build each event function, but only if something is actually chained to
    // it (an event with no chain produces no function at all).
    for (const Event& ev : events) {
        auto roots = NextInChain(ev.nodeId);
        if (roots.empty()) continue;

        lua += ev.header;
        lua += ev.dt;
        for (const GraphNode* n : roots)
            emit(lua, *n, 0);
        lua += "end\n\n";
    }

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return false;
    f << lua;
    return true;
}

} // namespace edtr
