#include "ScriptGraph.h"

#include "imgui.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>

namespace ed = ax::NodeEditor;
using nlohmann::json;

namespace edtr {

// ---- Node descriptions -----------------------------------------------------

const char* ScriptGraph::Title(NodeKind k) {
    switch (k) {
        case NodeKind::EventCreate:  return "On Create";
        case NodeKind::EventUpdate:  return "On Update";
        case NodeKind::EventDestroy: return "On Destroy";
        case NodeKind::Number:       return "Number";
        case NodeKind::Add:          return "Add";
        case NodeKind::Sub:          return "Subtract";
        case NodeKind::Mul:          return "Multiply";
        case NodeKind::Div:          return "Divide";
        case NodeKind::Yaw:          return "Yaw";
        case NodeKind::Pitch:        return "Pitch";
        case NodeKind::Roll:         return "Roll";
        case NodeKind::MoveForward:  return "Move Forward";
        case NodeKind::Print:        return "Print";
        case NodeKind::DestroySelf:  return "Destroy Self";
    }
    return "?";
}

// Value nodes (pure) have no exec pins; they only compute a number.
bool ScriptGraph::IsPure(NodeKind k) {
    switch (k) {
        case NodeKind::Number:
        case NodeKind::Add:
        case NodeKind::Sub:
        case NodeKind::Mul:
        case NodeKind::Div:
            return true;
        default:
            return false;
    }
}

// The list of pins a node kind has, with fixed slots.
std::vector<Pin> ScriptGraph::Signature(NodeKind k) {
    switch (k) {
        case NodeKind::EventCreate:
        case NodeKind::EventDestroy:
            return {{SlotExecOut, PinType::Exec, false, "then"}};
        case NodeKind::EventUpdate:
            return {{SlotExecOut,  PinType::Exec,  false, "then"},
                    {SlotDataOut,  PinType::Float, false, "dt"}};

        case NodeKind::Number:
            return {{SlotDataOut, PinType::Float, false, "value"}};
        case NodeKind::Add:
        case NodeKind::Sub:
        case NodeKind::Mul:
        case NodeKind::Div:
            return {{SlotDataIn,     PinType::Float, true,  "a"},
                    {SlotDataIn + 1, PinType::Float, true,  "b"},
                    {SlotDataOut,    PinType::Float, false, "result"}};

        case NodeKind::Yaw:
        case NodeKind::Pitch:
        case NodeKind::Roll:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "deg"}};
        case NodeKind::MoveForward:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "dist"}};
        case NodeKind::Print:
        case NodeKind::DestroySelf:
            return {{SlotExecIn,  PinType::Exec, true,  "in"},
                    {SlotExecOut, PinType::Exec, false, "out"}};
    }
    return {};
}

static bool IsEvent(NodeKind k) {
    return k == NodeKind::EventCreate || k == NodeKind::EventUpdate ||
           k == NodeKind::EventDestroy;
}

// ---- Construction ----------------------------------------------------------

ScriptGraph::ScriptGraph() { Reset(); }

void ScriptGraph::Reset() {
    m_nodes.clear();
    m_links.clear();
    m_nextID = 100;
    // The three event nodes always exist (ids 1,2,3), like fixed entry points.
    m_nodes.push_back({1, NodeKind::EventCreate});
    m_nodes.push_back({2, NodeKind::EventUpdate});
    m_nodes.push_back({3, NodeKind::EventDestroy});
    m_nodes[0].y = 0;  m_nodes[1].y = 120;  m_nodes[2].y = 240;
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

// Which node feeds the given input pin (following the one wire into it)?
const GraphNode* ScriptGraph::SourceOf(int inputPin) const {
    for (const auto& l : m_links)
        if (l.toPin == inputPin)
            return FindNode(PinToNode(l.fromPin));
    return nullptr;
}

// ---- Drawing ---------------------------------------------------------------

void ScriptGraph::DrawNode(GraphNode& n) {
    if (m_restorePositions)
        ed::SetNodePosition(n.id, ImVec2(n.x, n.y));

    ed::BeginNode(n.id);
    ImGui::PushID(n.id);
    ImGui::TextUnformatted(Title(n.kind));

    // Draw all the node's pins. Inputs are labelled ">- name", outputs
    // "name -<". Exec pins and data pins use the same drawing here; the wire
    // color the library gives them differs by which we accept when linking.
    for (const Pin& p : Signature(n.kind)) {
        int pinId = PinId(n.id, p.slot);
        if (p.input) {
            ed::BeginPin(pinId, ed::PinKind::Input);
            ImGui::Text(">- %s", p.name);
            ed::EndPin();
        } else {
            ed::BeginPin(pinId, ed::PinKind::Output);
            ImGui::Text("%s -<", p.name);
            ed::EndPin();
        }
    }

    // Editable fields for the nodes that carry a constant.
    ImGui::PushItemWidth(90);
    if (n.kind == NodeKind::Number)
        ImGui::DragFloat("##val", &n.value, 0.1f);
    else if (n.kind == NodeKind::Print)
        ImGui::InputText("##txt", n.text, sizeof(n.text));
    ImGui::PopItemWidth();

    ImGui::PopID();
    ed::EndNode();

    ImVec2 pos = ed::GetNodePosition(n.id);
    n.x = pos.x;  n.y = pos.y;
}

void ScriptGraph::HandleEdits() {
    if (ed::BeginCreate()) {
        ed::PinId a, b;
        if (ed::QueryNewLink(&a, &b)) {
            int pa = (int)a.Get(), pb = (int)b.Get();
            // Identify the two pins from their slots.
            auto slotOf = [](int pin) { return pin % 16; };
            int sa = slotOf(pa), sb = slotOf(pb);
            auto isOutput = [](int slot) { return slot == 1 || slot >= 10; };
            // Make pa the output side, pb the input side.
            if (!isOutput(sa)) { std::swap(pa, pb); std::swap(sa, sb); }

            bool outOk = isOutput(sa);
            bool inOk  = (sb == 0) || (sb >= 4 && sb < 10);   // exec-in or data-in
            // Exec connects to exec; data connects to data.
            bool execA = (sa == 1), execB = (sb == 0);
            bool typesMatch = (execA == execB);
            bool differentNodes = PinToNode(pa) != PinToNode(pb);

            if (outOk && inOk && typesMatch && differentNodes && ed::AcceptNewItem()) {
                // An input pin accepts only ONE wire, so drop any existing wire
                // into pb before adding the new one.
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                    [pb](const GraphLink& l) { return l.toPin == pb; }), m_links.end());
                m_links.push_back({m_nextID++, pa, pb});
            } else if (!(outOk && inOk && typesMatch && differentNodes)) {
                ed::RejectNewItem();
            }
        }
    }
    ed::EndCreate();

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
            if (n && IsEvent(n->kind)) { ed::RejectDeletedItem(); continue; }
            if (ed::AcceptDeletedItem()) {
                int id = (int)nid.Get();
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                    [id](const GraphLink& l) {
                        return PinToNode(l.fromPin) == id || PinToNode(l.toPin) == id;
                    }), m_links.end());
                m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(),
                    [id](const GraphNode& n) { return n.id == id; }), m_nodes.end());
            }
        }
    }
    ed::EndDelete();
}

void ScriptGraph::HandleContextMenu() {
    ImVec2 canvasMouse = ed::ScreenToCanvas(ImGui::GetMousePos());
    ed::Suspend();
    if (ed::ShowBackgroundContextMenu() && !ImGui::IsPopupOpen("AddNode")) {
        m_popupX = canvasMouse.x;
        m_popupY = canvasMouse.y;
        ImGui::OpenPopup("AddNode");
    }
    if (ImGui::BeginPopup("AddNode")) {
        NodeKind picked{};
        bool add = false;
        auto item = [&](const char* label, NodeKind k) {
            if (ImGui::MenuItem(label)) { picked = k; add = true; }
        };
        if (ImGui::BeginMenu("Values")) {
            item("Number", NodeKind::Number);
            item("Add", NodeKind::Add);
            item("Subtract", NodeKind::Sub);
            item("Multiply", NodeKind::Mul);
            item("Divide", NodeKind::Div);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Actions")) {
            item("Yaw", NodeKind::Yaw);
            item("Pitch", NodeKind::Pitch);
            item("Roll", NodeKind::Roll);
            item("Move Forward", NodeKind::MoveForward);
            item("Print", NodeKind::Print);
            item("Destroy Self", NodeKind::DestroySelf);
            ImGui::EndMenu();
        }
        if (add) {
            GraphNode n;
            n.id   = m_nextID++;
            n.kind = picked;
            n.x = m_popupX;  n.y = m_popupY;
            if (picked == NodeKind::Number) n.value = 1.0f;
            m_nodes.push_back(n);
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
    m_restorePositions = false;

    for (const auto& l : m_links)
        ed::Link(l.id, l.fromPin, l.toPin);

    HandleEdits();
    HandleContextMenu();

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

// ---- Persistence -----------------------------------------------------------

bool ScriptGraph::Save(const std::string& path) const {
    json doc;
    doc["nextID"] = m_nextID;
    for (const auto& n : m_nodes)
        doc["nodes"].push_back({{"id", n.id}, {"kind", (int)n.kind},
                                {"value", n.value}, {"text", n.text},
                                {"x", n.x}, {"y", n.y}});
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
        n.kind  = (NodeKind)jn.value("kind", 0);
        n.value = jn.value("value", 0.0f);
        n.x     = jn.value("x", 0.0f);
        n.y     = jn.value("y", 0.0f);
        std::strncpy(n.text, jn.value("text", "").c_str(), sizeof(n.text) - 1);
        m_nodes.push_back(n);
    }
    for (const json& jl : doc.value("links", json::array()))
        m_links.push_back({jl.value("id", 0), jl.value("from", 0), jl.value("to", 0)});
    m_nextID = doc.value("nextID", 100);
    m_restorePositions = true;
    return true;
}

// ---- Code generation -------------------------------------------------------

// The Lua EXPRESSION produced by a value node (Number -> its number, Add ->
// "(a + b)", etc.). Reads its own inputs recursively.
std::string ScriptGraph::ExprForNode(const GraphNode& n) const {
    char buf[64];
    switch (n.kind) {
        case NodeKind::Number:
            snprintf(buf, sizeof(buf), "%.4g", n.value);
            return buf;
        case NodeKind::EventUpdate:
            return "dt";   // the update event's dt output
        case NodeKind::Add: case NodeKind::Sub:
        case NodeKind::Mul: case NodeKind::Div: {
            const char* op = (n.kind == NodeKind::Add) ? " + " :
                             (n.kind == NodeKind::Sub) ? " - " :
                             (n.kind == NodeKind::Mul) ? " * " : " / ";
            std::string a = ExprForInput(PinId(n.id, SlotDataIn));
            std::string b = ExprForInput(PinId(n.id, SlotDataIn + 1));
            return "(" + a + op + b + ")";
        }
        default:
            return "0";
    }
}

// The expression wired into a data-input pin, or "0" if nothing is connected.
std::string ScriptGraph::ExprForInput(int inputPin) const {
    const GraphNode* src = SourceOf(inputPin);
    if (!src) return "0";
    return ExprForNode(*src);
}

// Walk the exec wire out of `fromExecPin` and emit each action's statement,
// following the chain until it ends.
void ScriptGraph::EmitExecChain(std::string& lua, int fromExecPin) const {
    int pin = fromExecPin;
    int guard = 0;
    while (guard++ < 256) {
        // Find the node whose exec-in this exec-out wires to.
        const GraphNode* next = nullptr;
        for (const auto& l : m_links)
            if (l.fromPin == pin) { next = FindNode(PinToNode(l.toPin)); break; }
        if (!next) return;

        switch (next->kind) {
            case NodeKind::Yaw:
                lua += "    entity.transform:rotate(0, 1, 0, " +
                       ExprForInput(PinId(next->id, SlotDataIn)) + ")\n";
                break;
            case NodeKind::Pitch:
                lua += "    entity.transform:rotate(1, 0, 0, " +
                       ExprForInput(PinId(next->id, SlotDataIn)) + ")\n";
                break;
            case NodeKind::Roll:
                lua += "    entity.transform:rotate(0, 0, 1, " +
                       ExprForInput(PinId(next->id, SlotDataIn)) + ")\n";
                break;
            case NodeKind::MoveForward:
                lua += "    entity.transform:translate_local(0, 0, -(" +
                       ExprForInput(PinId(next->id, SlotDataIn)) + "))\n";
                break;
            case NodeKind::Print: {
                std::string msg(next->text);
                std::string esc;
                for (char c : msg) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                lua += "    print(\"" + esc + "\")\n";
                break;
            }
            case NodeKind::DestroySelf:
                lua += "    scene.destroy(entity)\n";
                break;
            default: break;
        }
        pin = PinId(next->id, SlotExecOut);   // continue down the chain
    }
}

void ScriptGraph::EmitEvent(std::string& lua, NodeKind ev,
                            const char* header, bool provideDt) const {
    const GraphNode* evNode = nullptr;
    for (const auto& n : m_nodes) if (n.kind == ev) { evNode = &n; break; }
    if (!evNode) return;

    // Only emit the function if something is wired to the event's exec output.
    int execOut = PinId(evNode->id, SlotExecOut);
    bool hasChain = false;
    for (const auto& l : m_links) if (l.fromPin == execOut) { hasChain = true; break; }
    if (!hasChain) return;

    (void)provideDt;   // dt is already the on_update parameter; nothing to add
    lua += header;
    EmitExecChain(lua, execOut);
    lua += "end\n\n";
}

bool ScriptGraph::GenerateLua(const std::string& path) const {
    std::string lua =
        "-- GENERATED from a node graph. Edit the GRAPH, not this file --\n\n";

    EmitEvent(lua, NodeKind::EventCreate,  "function on_start(entity)\n",      false);
    EmitEvent(lua, NodeKind::EventUpdate,  "function on_update(entity, dt)\n", true);
    EmitEvent(lua, NodeKind::EventDestroy, "function on_destroy(entity)\n",    false);

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return false;
    f << lua;
    return true;
}

} // namespace edtr
