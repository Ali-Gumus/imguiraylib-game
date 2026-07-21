#include "ScriptGraph.h"

#include "imgui.h"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <cstring>

namespace ed = ax::NodeEditor;
using nlohmann::json;

namespace edtr {

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
    }
    return "?";
}

static bool IsEvent(NodeType t) {
    return t == NodeType::EventCreate || t == NodeType::EventUpdate ||
           t == NodeType::EventDestroy;
}

ScriptGraph::ScriptGraph() { Reset(); }

void ScriptGraph::Reset() {
    m_nodes.clear();
    m_links.clear();
    m_nextID = 100;                  // 1..99 stay reserved for event nodes
    // The three entry points always exist — you don't create or delete
    // them, exactly like you don't create "Update" in a Unity script.
    m_nodes.push_back({1, NodeType::EventCreate});
    m_nodes.push_back({2, NodeType::EventUpdate});
    m_nodes.push_back({3, NodeType::EventDestroy});
    m_nodes[0].y = 0;  m_nodes[1].y = 100;  m_nodes[2].y = 200;
    m_restorePositions = true;
}

GraphNode* ScriptGraph::FindNode(int id) {
    for (auto& n : m_nodes) if (n.id == id) return &n;
    return nullptr;
}
const GraphNode* ScriptGraph::FindNode(int id) const {
    for (auto& n : m_nodes) if (n.id == id) return &n;
    return nullptr;
}

// Follow the flow: every node linked after this one (fan-out = branches).
std::vector<const GraphNode*> ScriptGraph::NextInChain(int fromNodeId) const {
    std::vector<const GraphNode*> out;
    for (const auto& l : m_links)
        if (PinToNode(l.fromPin) == fromNodeId)
            if (const GraphNode* n = FindNode(PinToNode(l.toPin)))
                out.push_back(n);
    return out;
}

void ScriptGraph::DrawNode(GraphNode& n) {
    if (m_restorePositions)
        ed::SetNodePosition(n.id, ImVec2(n.x, n.y));

    ed::BeginNode(n.id);
    // Widgets inside nodes share ImGui's global ID space — two Spin
    // nodes both drawing "deg/s" would collide. Scope IDs per node.
    ImGui::PushID(n.id);
    ImGui::TextUnformatted(TypeTitle(n.type));

    // Pins: events only have an output ("then do..."); actions have an
    // input AND an output so they can be chained in sequence.
    if (!IsEvent(n.type)) {
        ed::BeginPin(InPin(n.id), ed::PinKind::Input);
        ImGui::TextUnformatted("-> in");
        ed::EndPin();
        ImGui::SameLine(0, 40);
    }
    ed::BeginPin(OutPin(n.id), ed::PinKind::Output);
    ImGui::TextUnformatted("out ->");
    ed::EndPin();

    // Parameters, edited directly inside the node.
    ImGui::PushItemWidth(140);
    switch (n.type) {
        case NodeType::ActionSpin:
            ImGui::DragFloat("deg/s", &n.speed, 1.0f);
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
        default: break;   // Destroy Self has no parameters
    }
    ImGui::PopItemWidth();
    ImGui::PopID();
    ed::EndNode();

    // Remember where the user dragged it (for saving).
    ImVec2 pos = ed::GetNodePosition(n.id);
    n.x = pos.x;  n.y = pos.y;
}

void ScriptGraph::HandleEdits() {
    // --- Creating links: the library asks "user is dragging pin A to B,
    // do you accept?" — WE decide validity, it draws the rubber band.
    if (ed::BeginCreate()) {
        ed::PinId a, b;
        if (ed::QueryNewLink(&a, &b)) {
            int pa = (int)a.Get(), pb = (int)b.Get();
            // Normalize: pa = output side, pb = input side.
            if (pa % 4 == 1) std::swap(pa, pb);

            bool valid = pa % 4 == 2 && pb % 4 == 1          // out -> in
                      && PinToNode(pa) != PinToNode(pb);     // not to itself
            // Fan-out is allowed: one output may feed several branches
            // (e.g. On Update -> four separate If-Key chains for WASD).
            if (valid && ed::AcceptNewItem())
                m_links.push_back({m_nextID++, pa, pb});
            if (!valid) ed::RejectNewItem();
        }
    }
    ed::EndCreate();

    // --- Deleting (user selects a link/node and presses Delete).
    if (ed::BeginDelete()) {
        ed::LinkId lid;
        while (ed::QueryDeletedLink(&lid)) {
            if (ed::AcceptDeletedItem())
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                    [&](const GraphLink& l) { return l.id == (int)lid.Get(); }),
                    m_links.end());
        }
        ed::NodeId nid;
        while (ed::QueryDeletedNode(&nid)) {
            GraphNode* n = FindNode((int)nid.Get());
            if (n && IsEvent(n->type)) { ed::RejectDeletedItem(); continue; }
            if (ed::AcceptDeletedItem()) {
                int id = (int)nid.Get();
                // Links touching the node die with it.
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

void ScriptGraph::HandleContextMenu() {
    // Convert mouse -> canvas coords NOW, while the canvas transform is
    // active. Inside Suspend() that transform doesn't exist and
    // ScreenToCanvas asserts (empty internal stack).
    ImVec2 canvasMouse = ed::ScreenToCanvas(ImGui::GetMousePos());

    // Popups don't live on the zoomable canvas — Suspend leaves canvas
    // space, Resume returns. Forgetting this = popup at wrong zoom/pos.
    ed::Suspend();
    // Capture ONLY at the moment the popup opens. ShowBackgroundContextMenu
    // stays true while the menu is up, so without the IsPopupOpen guard the
    // position kept following the mouse INTO the menu — nodes spawned at
    // the menu item, offset more the further out you were zoomed.
    if (ed::ShowBackgroundContextMenu() && !ImGui::IsPopupOpen("AddNode")) {
        m_popupX = canvasMouse.x;
        m_popupY = canvasMouse.y;
        ImGui::OpenPopup("AddNode");
    }

    if (ImGui::BeginPopup("AddNode")) {
        NodeType picked{};
        bool add = false;
        if (ImGui::MenuItem("Spin"))  { picked = NodeType::ActionSpin;  add = true; }
        if (ImGui::MenuItem("Move"))  { picked = NodeType::ActionMove;  add = true; }
        if (ImGui::MenuItem("Print")) { picked = NodeType::ActionPrint; add = true; }
        if (ImGui::MenuItem("Destroy Self")) { picked = NodeType::ActionDestroySelf; add = true; }
        if (ImGui::MenuItem("Spawn Cube"))   { picked = NodeType::ActionSpawnCube;   add = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("If Key"))    { picked = NodeType::CondKey;   add = true; }
        if (ImGui::MenuItem("Every X s")) { picked = NodeType::CondEvery; add = true; }
        if (add) {
            GraphNode n;
            n.id   = m_nextID++;
            n.type = picked;
            n.x = m_popupX;  n.y = m_popupY;   // where the user right-clicked
            m_nodes.push_back(n);
            // Don't SetNodePosition here — we're inside Suspend(), where
            // canvas coordinates don't apply (same trap as the crash we
            // had). The restore pass places it next frame, canvas active.
            m_restorePositions = true;
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

void ScriptGraph::Draw(ed::EditorContext* ctx) {
    ed::SetCurrentEditor(ctx);
    ed::Begin("ScriptGraph");

    for (auto& n : m_nodes) DrawNode(n);
    m_restorePositions = false;       // saved positions applied on first pass

    for (const auto& l : m_links)
        ed::Link(l.id, l.fromPin, l.toPin);

    HandleEdits();
    HandleContextMenu();

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

// ---- Persistence ------------------------------------------------------

bool ScriptGraph::Save(const std::string& path) const {
    json doc;
    doc["nextID"] = m_nextID;
    for (const auto& n : m_nodes)
        doc["nodes"].push_back({{"id", n.id}, {"type", (int)n.type},
                                {"x", n.x}, {"y", n.y},
                                {"speed", n.speed},
                                {"offset", {n.offset[0], n.offset[1], n.offset[2]}},
                                {"text", n.text}, {"key", n.key},
                                {"interval", n.interval}});
    for (const auto& l : m_links)
        doc["links"].push_back({{"id", l.id}, {"from", l.fromPin}, {"to", l.toPin}});

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return false;
    f << doc.dump(2);
    return true;
}

bool ScriptGraph::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    json doc = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) return false;

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
        std::strncpy(n.text, jn.value("text", "").c_str(), sizeof(n.text) - 1);
        std::strncpy(n.key,  jn.value("key", "W").c_str(), sizeof(n.key) - 1);
        n.interval = jn.value("interval", 1.0f);
        m_nodes.push_back(n);
    }
    for (const json& jl : doc.value("links", json::array()))
        m_links.push_back({jl.value("id", 0), jl.value("from", 0), jl.value("to", 0)});

    m_nextID = doc.value("nextID", 100);
    m_restorePositions = true;        // apply x/y on next Draw
    return true;
}

// ---- Code generation: graph -> Lua ------------------------------------

// Minimal escaping so a quote in a Print message can't break the code.
static std::string EscapeLua(const char* s) {
    std::string out;
    for (const char* p = s; *p; ++p) {
        if (*p == '"' || *p == '\\') out += '\\';
        out += *p;
    }
    return out;
}

static void EmitAction(std::string& lua, const GraphNode& n) {
    char buf[256];
    switch (n.type) {
        case NodeType::ActionSpin:
            snprintf(buf, sizeof(buf),
                "    entity.transform.rotation.y = entity.transform.rotation.y + %.3f * dt\n",
                n.speed);
            lua += buf;
            break;
        case NodeType::ActionMove:
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
            // Only enqueues — the engine applies it after the update
            // loop, so a script destroying its own entity is safe.
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

    // Timer blocks need state that SURVIVES between frames: one
    // file-scope accumulator per "Every X s" node, named by node id.
    for (const auto& n : m_nodes)
        if (n.type == NodeType::CondEvery) {
            char buf[64];
            snprintf(buf, sizeof(buf), "local acc_%d = 0\n", n.id);
            lua += buf;
        }
    lua += "\n";

    struct Event { int nodeId; const char* header; const char* dt; };
    // on_start/on_destroy run once: dt has no meaning, so actions use 1
    // (Move there = instant step, Spin = instant turn by `speed` degrees).
    const Event events[] = {
        {1, "function on_start(entity)\n",   "    local dt = 1\n"},
        {2, "function on_update(entity, dt)\n", ""},
        {3, "function on_destroy(entity)\n", "    local dt = 1\n"},
    };

    // Recursive walk: emit a node, then each branch hanging off it.
    // A condition wraps ONLY its own subtree in `if ... end`; sibling
    // branches (fan-out) are independent statements in sequence.
    // `depth` guards against link cycles (a cycle must not hang us).
    std::function<void(std::string&, const GraphNode&, int)> emit =
        [&](std::string& lua, const GraphNode& n, int depth) {
            if (depth > 32) return;
            if (n.type == NodeType::CondKey) {
                lua += "    if input.key_down(\"" + EscapeLua(n.key) + "\") then\n";
                for (const GraphNode* next : NextInChain(n.id))
                    emit(lua, *next, depth + 1);
                lua += "    end\n";
            } else if (n.type == NodeType::CondEvery) {
                // Accumulate dt; on crossing the interval, subtract it
                // (not reset to 0 — leftover time carries over, keeping
                // the average rate exact) and run the branch.
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

    for (const Event& ev : events) {
        // No branches off this event -> no function (the engine treats
        // missing hooks as "not interested").
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
