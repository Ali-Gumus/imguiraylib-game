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
        case NodeKind::KeyDown:      return "Key Down";
        case NodeKind::Greater:      return "A > B";
        case NodeKind::Less:         return "A < B";
        case NodeKind::Equal:        return "A == B";
        case NodeKind::Branch:       return "Branch";
        case NodeKind::GetVar:       return "Get";
        case NodeKind::SetVar:       return "Set";
        case NodeKind::PosX:         return "Pos X";
        case NodeKind::PosY:         return "Pos Y";
        case NodeKind::PosZ:         return "Pos Z";
        case NodeKind::FwdX:         return "Forward X";
        case NodeKind::FwdY:         return "Forward Y";
        case NodeKind::FwdZ:         return "Forward Z";
        case NodeKind::UpX:          return "Up X";
        case NodeKind::UpY:          return "Up Y";
        case NodeKind::UpZ:          return "Up Z";
        case NodeKind::Sqrt:         return "Sqrt";
        case NodeKind::Exp:          return "Exp";
        case NodeKind::SetPosX:      return "Set Pos X";
        case NodeKind::SetPosY:      return "Set Pos Y";
        case NodeKind::SetPosZ:      return "Set Pos Z";
        case NodeKind::TurnToPlayer: return "Turn To Player";
        case NodeKind::Fire:         return "Fire";
        case NodeKind::IsPlayerNear: return "If Player Near";
        case NodeKind::SetScale:     return "Set Scale";
        case NodeKind::HitNearest:   return "Hit Nearest";
        case NodeKind::HudSet:       return "HUD Set";
        case NodeKind::AvoidCrowd:   return "Avoid Crowd";
        case NodeKind::AimedAtPlayer:return "Aimed At Player";
        case NodeKind::ChaseTarget:  return "Chase Target";
        case NodeKind::And:          return "And";
        case NodeKind::Or:           return "Or";
        case NodeKind::LessEqual:    return "A <= B";
        case NodeKind::GreaterEqual: return "A >= B";
        case NodeKind::Param:        return "Param";
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
        case NodeKind::KeyDown:
        case NodeKind::Greater:
        case NodeKind::Less:
        case NodeKind::Equal:
        case NodeKind::GetVar:
        case NodeKind::PosX: case NodeKind::PosY: case NodeKind::PosZ:
        case NodeKind::FwdX: case NodeKind::FwdY: case NodeKind::FwdZ:
        case NodeKind::UpX:  case NodeKind::UpY:  case NodeKind::UpZ:
        case NodeKind::Sqrt: case NodeKind::Exp:
        case NodeKind::IsPlayerNear:
        case NodeKind::AimedAtPlayer:
        case NodeKind::And: case NodeKind::Or:
        case NodeKind::LessEqual: case NodeKind::GreaterEqual:
        case NodeKind::Param:
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
        case NodeKind::Param:
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

        case NodeKind::KeyDown:
            return {{SlotDataOut, PinType::Bool, false, "held"}};
        case NodeKind::Greater:
        case NodeKind::Less:
        case NodeKind::Equal:
        case NodeKind::LessEqual:
        case NodeKind::GreaterEqual:
            return {{SlotDataIn,     PinType::Float, true,  "a"},
                    {SlotDataIn + 1, PinType::Float, true,  "b"},
                    {SlotDataOut,    PinType::Bool,  false, "result"}};
        // Boolean combiners: two Bool inputs, one Bool output.
        case NodeKind::And:
        case NodeKind::Or:
            return {{SlotDataIn,     PinType::Bool, true,  "a"},
                    {SlotDataIn + 1, PinType::Bool, true,  "b"},
                    {SlotDataOut,    PinType::Bool, false, "result"}};
        case NodeKind::Branch:
            return {{SlotExecIn,   PinType::Exec, true,  "in"},
                    {SlotDataIn,    PinType::Bool, true,  "cond"},
                    {SlotExecOut,   PinType::Exec, false, "true"},
                    {SlotExecOut2,  PinType::Exec, false, "false"}};

        case NodeKind::GetVar:
            return {{SlotDataOut, PinType::Float, false, "value"}};
        case NodeKind::SetVar:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "value"}};

        // Value nodes that read something about this entity (no inputs).
        case NodeKind::PosX: case NodeKind::PosY: case NodeKind::PosZ:
        case NodeKind::FwdX: case NodeKind::FwdY: case NodeKind::FwdZ:
        case NodeKind::UpX:  case NodeKind::UpY:  case NodeKind::UpZ:
            return {{SlotDataOut, PinType::Float, false, "value"}};
        // Unary math: one number in, one out.
        case NodeKind::Sqrt: case NodeKind::Exp:
            return {{SlotDataIn,  PinType::Float, true,  "x"},
                    {SlotDataOut, PinType::Float, false, "result"}};
        // Is a player within range? number in, bool out.
        case NodeKind::IsPlayerNear:
            return {{SlotDataIn,  PinType::Float, true,  "range"},
                    {SlotDataOut, PinType::Bool,  false, "near"}};
        // Write a position component.
        case NodeKind::SetPosX: case NodeKind::SetPosY: case NodeKind::SetPosZ:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "value"}};
        case NodeKind::TurnToPlayer:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "deg"}};
        case NodeKind::Fire:
            return {{SlotExecIn,  PinType::Exec, true,  "in"},
                    {SlotExecOut, PinType::Exec, false, "out"}};

        // Set every scale axis to one number wired into "value".
        case NodeKind::SetScale:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "value"}};
        // Impact test: the tag to hit and the hit points are node fields; the
        // reach is the "radius" data input.
        case NodeKind::HitNearest:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "radius"}};
        // Publish one number to the HUD; the display name is a node field.
        case NodeKind::HudSet:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "value"}};
        // Separation: the neighbour tag and push strength are node fields; the
        // reach is the "range" data input.
        case NodeKind::AvoidCrowd:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "range"}};
        // Firing test: within "range" of the player AND within the "angle" cone.
        case NodeKind::AimedAtPlayer:
            return {{SlotDataIn,     PinType::Float, true,  "range"},
                    {SlotDataIn + 1, PinType::Float, true,  "angle"},
                    {SlotDataOut,    PinType::Bool,  false, "aimed"}};
        // Chase camera: the target name is a node field; the three follow
        // numbers are data inputs.
        case NodeKind::ChaseTarget:
            return {{SlotExecIn,     PinType::Exec,  true,  "in"},
                    {SlotExecOut,    PinType::Exec,  false, "out"},
                    {SlotDataIn,     PinType::Float, true,  "distance"},
                    {SlotDataIn + 1, PinType::Float, true,  "height"},
                    {SlotDataIn + 2, PinType::Float, true,  "stiffness"}};
    }
    return {};
}

// Look up a pin's type from its node's signature.
PinType ScriptGraph::PinTypeOf(int pin) const {
    const GraphNode* n = FindNode(PinToNode(pin));
    if (n) {
        int slot = PinToSlot(pin);
        for (const Pin& p : Signature(n->kind))
            if (p.slot == slot) return p.type;
    }
    return PinType::Exec;
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
    else if (n.kind == NodeKind::Param) {
        ImGui::InputText("##pname", n.text, sizeof(n.text));  // the property name
        ImGui::DragFloat("##pdef", &n.value, 0.05f);          // its default value
    }
    else if (n.kind == NodeKind::Print || n.kind == NodeKind::KeyDown)
        ImGui::InputText("##txt", n.text, sizeof(n.text));   // message, or key name
    else if (n.kind == NodeKind::GetVar || n.kind == NodeKind::SetVar)
        ImGui::InputText("##var", n.text, sizeof(n.text));   // the variable name
    else if (n.kind == NodeKind::Fire)
        ImGui::InputText("##bullet", n.text, sizeof(n.text)); // the bullet script
    else if (n.kind == NodeKind::HitNearest) {
        ImGui::InputText("##tag", n.text, sizeof(n.text));    // the tag to damage
        ImGui::DragFloat("##dmg", &n.value, 0.1f);            // hit points removed
    }
    else if (n.kind == NodeKind::HudSet)
        ImGui::InputText("##hud", n.text, sizeof(n.text));    // the HUD value name
    else if (n.kind == NodeKind::AvoidCrowd) {
        ImGui::InputText("##ctag", n.text, sizeof(n.text));   // the neighbour tag
        ImGui::DragFloat("##force", &n.value, 0.1f);          // push strength
    }
    else if (n.kind == NodeKind::ChaseTarget)
        ImGui::InputText("##target", n.text, sizeof(n.text)); // the target entity name
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
            int sa = PinToSlot(pa), sb = PinToSlot(pb);
            auto isOut = [](int s) { return s == SlotExecOut || s == SlotExecOut2 || s >= SlotDataOut; };
            auto isIn  = [](int s) { return s == SlotExecIn || (s >= SlotDataIn && s < SlotDataOut); };
            // Make pa the output side, pb the input side.
            if (isOut(sb) && isIn(sa)) { std::swap(pa, pb); std::swap(sa, sb); }

            bool ok = isOut(sa) && isIn(sb)                 // output -> input
                   && PinToNode(pa) != PinToNode(pb)        // not to itself
                   && PinTypeOf(pa) == PinTypeOf(pb);       // same pin type

            if (ok && ed::AcceptNewItem()) {
                // An input pin holds only ONE wire; drop any existing one first.
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                    [pb](const GraphLink& l) { return l.toPin == pb; }), m_links.end());
                m_links.push_back({m_nextID++, pa, pb});
            } else if (!ok) {
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
            item("Param", NodeKind::Param);
            item("Add", NodeKind::Add);
            item("Subtract", NodeKind::Sub);
            item("Multiply", NodeKind::Mul);
            item("Divide", NodeKind::Div);
            ImGui::Separator();
            item("Key Down", NodeKind::KeyDown);
            item("A > B", NodeKind::Greater);
            item("A < B", NodeKind::Less);
            item("A == B", NodeKind::Equal);
            item("A >= B", NodeKind::GreaterEqual);
            item("A <= B", NodeKind::LessEqual);
            item("And", NodeKind::And);
            item("Or", NodeKind::Or);
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
        if (ImGui::BeginMenu("Flow")) {
            item("Branch (If)", NodeKind::Branch);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Variables")) {
            item("Get", NodeKind::GetVar);
            item("Set", NodeKind::SetVar);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Read")) {
            item("Pos X", NodeKind::PosX);   item("Pos Y", NodeKind::PosY);   item("Pos Z", NodeKind::PosZ);
            item("Forward X", NodeKind::FwdX); item("Forward Y", NodeKind::FwdY); item("Forward Z", NodeKind::FwdZ);
            item("Up X", NodeKind::UpX);     item("Up Y", NodeKind::UpY);     item("Up Z", NodeKind::UpZ);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Math")) {
            item("Sqrt", NodeKind::Sqrt);
            item("Exp", NodeKind::Exp);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Engine")) {
            item("Set Pos X", NodeKind::SetPosX); item("Set Pos Y", NodeKind::SetPosY); item("Set Pos Z", NodeKind::SetPosZ);
            item("Turn To Player", NodeKind::TurnToPlayer);
            item("Fire", NodeKind::Fire);
            item("If Player Near", NodeKind::IsPlayerNear);
            item("Set Scale", NodeKind::SetScale);
            item("Hit Nearest", NodeKind::HitNearest);
            item("HUD Set", NodeKind::HudSet);
            item("Avoid Crowd", NodeKind::AvoidCrowd);
            item("Aimed At Player", NodeKind::AimedAtPlayer);
            item("Chase Target", NodeKind::ChaseTarget);
            ImGui::EndMenu();
        }
        if (add) {
            GraphNode n;
            n.id   = m_nextID++;
            n.kind = picked;
            n.x = m_popupX;  n.y = m_popupY;
            if (picked == NodeKind::Number) n.value = 1.0f;
            if (picked == NodeKind::Param) {
                std::strncpy(n.text, "param", sizeof(n.text) - 1);  // default property name
                n.value = 1.0f;                                     // default value
            }
            if (picked == NodeKind::KeyDown)
                std::strncpy(n.text, "W", sizeof(n.text) - 1);   // default key
            if (picked == NodeKind::Fire)
                std::strncpy(n.text, "assets/scripts/bullet.lua", sizeof(n.text) - 1);
            if (picked == NodeKind::TurnToPlayer) n.value = 65.0f;   // (unused, but tidy)
            if (picked == NodeKind::HitNearest) {
                std::strncpy(n.text, "enemy", sizeof(n.text) - 1);   // default target tag
                n.value = 1.0f;                                      // default damage
            }
            if (picked == NodeKind::HudSet)
                std::strncpy(n.text, "value", sizeof(n.text) - 1);   // default HUD name
            if (picked == NodeKind::AvoidCrowd) {
                std::strncpy(n.text, "enemy", sizeof(n.text) - 1);   // default neighbour tag
                n.value = 12.0f;                                     // default push strength
            }
            if (picked == NodeKind::ChaseTarget)
                std::strncpy(n.text, "Jet", sizeof(n.text) - 1);     // default target name
            m_nodes.push_back(n);
            m_restorePositions = true;
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

// The little "Variables" list drawn at the top of the Node Editor panel,
// above the canvas. Each row is a variable's name and starting value.
void ScriptGraph::DrawVariablesUI() {
    if (!ImGui::CollapsingHeader("Variables")) return;
    int removeIdx = -1;
    for (size_t i = 0; i < m_vars.size(); ++i) {
        ImGui::PushID((int)i);
        char nb[64];
        std::strncpy(nb, m_vars[i].name.c_str(), sizeof(nb) - 1);
        nb[sizeof(nb) - 1] = '\0';
        ImGui::SetNextItemWidth(130);
        if (ImGui::InputText("##name", nb, sizeof(nb))) m_vars[i].name = nb;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::DragFloat("##init", &m_vars[i].init, 0.1f);
        ImGui::SameLine();
        if (ImGui::SmallButton("remove")) removeIdx = (int)i;
        ImGui::PopID();
    }
    if (removeIdx >= 0) m_vars.erase(m_vars.begin() + removeIdx);
    if (ImGui::SmallButton("+ Add Variable")) m_vars.push_back({"var", 0.0f});
    ImGui::Separator();
}

void ScriptGraph::Draw(ed::EditorContext* ctx) {
    DrawVariablesUI();

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
    for (const auto& v : m_vars)
        doc["vars"].push_back({{"name", v.name}, {"init", v.init}});

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
    m_vars.clear();
    for (const json& jv : doc.value("vars", json::array()))
        m_vars.push_back({jv.value("name", std::string("var")), jv.value("init", 0.0f)});
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
        case NodeKind::Mul: case NodeKind::Div:
        case NodeKind::Greater: case NodeKind::Less: case NodeKind::Equal:
        case NodeKind::LessEqual: case NodeKind::GreaterEqual:
        case NodeKind::And: case NodeKind::Or: {
            const char* op = (n.kind == NodeKind::Add)          ? " + "   :
                             (n.kind == NodeKind::Sub)          ? " - "   :
                             (n.kind == NodeKind::Mul)          ? " * "   :
                             (n.kind == NodeKind::Div)          ? " / "   :
                             (n.kind == NodeKind::Greater)      ? " > "   :
                             (n.kind == NodeKind::Less)         ? " < "   :
                             (n.kind == NodeKind::Equal)        ? " == "  :
                             (n.kind == NodeKind::LessEqual)    ? " <= "  :
                             (n.kind == NodeKind::GreaterEqual) ? " >= "  :
                             (n.kind == NodeKind::And)          ? " and " : " or ";
            std::string a = ExprForInput(PinId(n.id, SlotDataIn));
            std::string b = ExprForInput(PinId(n.id, SlotDataIn + 1));
            return "(" + a + op + b + ")";
        }
        case NodeKind::KeyDown: {
            std::string key(n.text), esc;
            for (char c : key) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
            return "input.key_down(\"" + esc + "\")";
        }
        case NodeKind::GetVar:
            return n.text[0] ? std::string(n.text) : "0";   // the variable's name
        case NodeKind::Param:
            // Read the tunable back from the generated properties table.
            return n.text[0] ? ("properties." + std::string(n.text)) : "0";

        case NodeKind::PosX: return "entity.transform.position.x";
        case NodeKind::PosY: return "entity.transform.position.y";
        case NodeKind::PosZ: return "entity.transform.position.z";
        case NodeKind::FwdX: return "entity.transform:forward().x";
        case NodeKind::FwdY: return "entity.transform:forward().y";
        case NodeKind::FwdZ: return "entity.transform:forward().z";
        case NodeKind::UpX:  return "entity.transform:up().x";
        case NodeKind::UpY:  return "entity.transform:up().y";
        case NodeKind::UpZ:  return "entity.transform:up().z";
        case NodeKind::Sqrt:
            return "math.sqrt(" + ExprForInput(PinId(n.id, SlotDataIn)) + ")";
        case NodeKind::Exp:
            return "math.exp(" + ExprForInput(PinId(n.id, SlotDataIn)) + ")";
        case NodeKind::IsPlayerNear:
            return "(scene.nearest(\"player\", entity.transform.position.x, "
                   "entity.transform.position.y, entity.transform.position.z, " +
                   ExprForInput(PinId(n.id, SlotDataIn)) + ") ~= nil)";
        case NodeKind::AimedAtPlayer: {
            // Self-contained expression: find the nearest player, then test that
            // it is both within "range" and within the "angle" firing cone (the
            // off-nose angle from a clamped forward-vs-direction dot product).
            std::string range = ExprForInput(PinId(n.id, SlotDataIn));
            std::string angle = ExprForInput(PinId(n.id, SlotDataIn + 1));
            return
              "(function() "
              "local tp = scene.nearest(\"player\", entity.transform.position.x, entity.transform.position.y, entity.transform.position.z, 100000) "
              "if tp == nil then return false end "
              "local dx = tp.transform.position.x - entity.transform.position.x "
              "local dy = tp.transform.position.y - entity.transform.position.y "
              "local dz = tp.transform.position.z - entity.transform.position.z "
              "local dd = math.sqrt(dx*dx + dy*dy + dz*dz) "
              "local f = entity.transform:forward() "
              "local inv = 0 if dd > 0.0001 then inv = 1 / dd end "
              "local dot = f.x*dx*inv + f.y*dy*inv + f.z*dz*inv "
              "if dot > 1 then dot = 1 end if dot < -1 then dot = -1 end "
              "local ang = math.deg(math.acos(dot)) "
              "return dd < " + range + " and ang < " + angle + " end)()";
        }
        default:
            return "0";
    }
}

// The expression wired into a data-input pin. If nothing is connected, use a
// sensible default for the pin's type (false for a Bool input, 0 for a Float).
std::string ScriptGraph::ExprForInput(int inputPin) const {
    const GraphNode* src = SourceOf(inputPin);
    if (!src) return PinTypeOf(inputPin) == PinType::Bool ? "false" : "0";
    return ExprForNode(*src);
}

// Emit every action wired to `fromExecPin`, in order. An exec output may fan
// out to several targets (so On Update can drive several independent checks);
// each target emits its statement and then its own continuation. This is
// recursive so a Branch can emit an if/else. `depth` guards against a loop.
void ScriptGraph::EmitExecChain(std::string& lua, int fromExecPin, int depth) const {
    if (depth > 256) return;

    for (const auto& l : m_links) {
        if (l.fromPin != fromExecPin) continue;
        const GraphNode* n = FindNode(PinToNode(l.toPin));
        if (!n) continue;

        switch (n->kind) {
            case NodeKind::Yaw:
                lua += "    entity.transform:rotate(0, 1, 0, " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::Pitch:
                lua += "    entity.transform:rotate(1, 0, 0, " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::Roll:
                lua += "    entity.transform:rotate(0, 0, 1, " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::MoveForward:
                lua += "    entity.transform:translate_local(0, 0, -(" +
                       ExprForInput(PinId(n->id, SlotDataIn)) + "))\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::Print: {
                std::string msg(n->text), esc;
                for (char c : msg) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                lua += "    print(\"" + esc + "\")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::DestroySelf:
                lua += "    scene.destroy(entity)\n";
                break;
            case NodeKind::SetVar:
                if (n->text[0])
                    lua += "    " + std::string(n->text) + " = " +
                           ExprForInput(PinId(n->id, SlotDataIn)) + "\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::SetPosX:
                lua += "    entity.transform.position.x = " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + "\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::SetPosY:
                lua += "    entity.transform.position.y = " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + "\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::SetPosZ:
                lua += "    entity.transform.position.z = " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + "\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::TurnToPlayer: {
                // Find the nearest player and rotate gradually toward it. A
                // unique local (by node id) avoids clashes between two of these.
                char buf[320];
                snprintf(buf, sizeof(buf),
                    "    local tgt%d = scene.nearest(\"player\", entity.transform.position.x, entity.transform.position.y, entity.transform.position.z, 100000)\n"
                    "    if tgt%d ~= nil then entity.transform:rotate_toward(tgt%d.transform.position.x, tgt%d.transform.position.y, tgt%d.transform.position.z, %s) end\n",
                    n->id, n->id, n->id, n->id, n->id,
                    ExprForInput(PinId(n->id, SlotDataIn)).c_str());
                lua += buf;
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::Fire: {
                // Spawn a bullet a little ahead of the entity, facing forward.
                std::string script(n->text), esc;
                for (char c : script) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                char buf[420];
                snprintf(buf, sizeof(buf),
                    "    local ff%d = entity.transform:forward()\n"
                    "    local pp%d = entity.transform.position\n"
                    "    scene.spawn(\"Bullet\", pp%d.x+ff%d.x*3, pp%d.y+ff%d.y*3, pp%d.z+ff%d.z*3, ff%d.x, ff%d.y, ff%d.z, \"%s\")\n",
                    n->id, n->id, n->id, n->id, n->id, n->id, n->id, n->id, n->id, n->id, n->id,
                    esc.c_str());
                lua += buf;
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::SetScale: {
                // One number drives all three axes, so a Number node feeding
                // "value" makes a uniform scale.
                std::string v = ExprForInput(PinId(n->id, SlotDataIn));
                lua += "    entity.transform.scale.x = " + v + "\n";
                lua += "    entity.transform.scale.y = " + v + "\n";
                lua += "    entity.transform.scale.z = " + v + "\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::HitNearest: {
                // Find the nearest entity of the given tag within reach; if one
                // exists, damage it and remove this entity (a bullet's impact).
                // A unique local (by node id) avoids clashes between two of these.
                std::string tag(n->text), esc;
                for (char c : tag) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                char buf[400];
                snprintf(buf, sizeof(buf),
                    "    local hit%d = scene.nearest(\"%s\", entity.transform.position.x, entity.transform.position.y, entity.transform.position.z, %s)\n"
                    "    if hit%d ~= nil then scene.damage(hit%d, %.4g); scene.destroy(entity) end\n",
                    n->id, esc.c_str(),
                    ExprForInput(PinId(n->id, SlotDataIn)).c_str(),
                    n->id, n->id, n->value);
                lua += buf;
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::AvoidCrowd: {
                // Push away from the nearest neighbour of the same tag when it
                // crowds within range, so a squadron spreads out. Node-scoped
                // locals (by id) keep two of these from clashing.
                std::string i = std::to_string(n->id);
                std::string tag(n->text), esc;
                for (char c : tag) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                std::string rng = ExprForInput(PinId(n->id, SlotDataIn));
                char fbuf[32]; snprintf(fbuf, sizeof(fbuf), "%.4g", n->value);
                std::string force = fbuf;
                lua +=
                  "    local rng" + i + " = " + rng + "\n"
                  "    local oth" + i + " = scene.nearest_other(entity, \"" + esc + "\", rng" + i + ")\n"
                  "    if oth" + i + " ~= nil then\n"
                  "        local sx" + i + " = entity.transform.position.x - oth" + i + ".transform.position.x\n"
                  "        local sy" + i + " = entity.transform.position.y - oth" + i + ".transform.position.y\n"
                  "        local sz" + i + " = entity.transform.position.z - oth" + i + ".transform.position.z\n"
                  "        local sd" + i + " = math.sqrt(sx" + i + "*sx" + i + " + sy" + i + "*sy" + i + " + sz" + i + "*sz" + i + ")\n"
                  "        if sd" + i + " > 0.001 then\n"
                  "            local st" + i + " = (rng" + i + " - sd" + i + ") / rng" + i + "\n"
                  "            local pu" + i + " = " + force + " * st" + i + " * dt / sd" + i + "\n"
                  "            entity.transform.position.x = entity.transform.position.x + sx" + i + "*pu" + i + "\n"
                  "            entity.transform.position.y = entity.transform.position.y + sy" + i + "*pu" + i + "\n"
                  "            entity.transform.position.z = entity.transform.position.z + sz" + i + "*pu" + i + "\n"
                  "        end\n"
                  "    end\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::ChaseTarget: {
                // Ease toward a point behind and above the named target, then
                // look at it. Node-scoped locals (by id) avoid clashes.
                std::string i = std::to_string(n->id);
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                std::string dist = ExprForInput(PinId(n->id, SlotDataIn));
                std::string hgt  = ExprForInput(PinId(n->id, SlotDataIn + 1));
                std::string stf  = ExprForInput(PinId(n->id, SlotDataIn + 2));
                lua +=
                  "    local jet" + i + " = scene.find(\"" + esc + "\")\n"
                  "    if jet" + i + " ~= nil then\n"
                  "        local cd" + i + " = " + dist + "\n"
                  "        local ch" + i + " = " + hgt + "\n"
                  "        local cs" + i + " = " + stf + "\n"
                  "        local jf" + i + " = jet" + i + ".transform:forward()\n"
                  "        local dx" + i + " = jet" + i + ".transform.position.x - jf" + i + ".x * cd" + i + "\n"
                  "        local dy" + i + " = jet" + i + ".transform.position.y - jf" + i + ".y * cd" + i + " + ch" + i + "\n"
                  "        local dz" + i + " = jet" + i + ".transform.position.z - jf" + i + ".z * cd" + i + "\n"
                  "        local ca" + i + " = 1 - math.exp(-cs" + i + " * dt)\n"
                  "        entity.transform.position.x = entity.transform.position.x + (dx" + i + " - entity.transform.position.x) * ca" + i + "\n"
                  "        entity.transform.position.y = entity.transform.position.y + (dy" + i + " - entity.transform.position.y) * ca" + i + "\n"
                  "        entity.transform.position.z = entity.transform.position.z + (dz" + i + " - entity.transform.position.z) * ca" + i + "\n"
                  "        entity.transform:look_at(jet" + i + ".transform.position.x, jet" + i + ".transform.position.y, jet" + i + ".transform.position.z)\n"
                  "    end\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::HudSet: {
                // Publish a number for the C++ HUD to read by name.
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                lua += "    hud.set(\"" + esc + "\", " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::Branch: {
                lua += "    if " + ExprForInput(PinId(n->id, SlotDataIn)) + " then\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut),  depth + 1);
                // Only emit an "else" arm when something is actually wired to the
                // false output; a bare guard (if ... then ... end) reads cleaner.
                int falsePin = PinId(n->id, SlotExecOut2);
                bool hasElse = false;
                for (const auto& e : m_links)
                    if (e.fromPin == falsePin) { hasElse = true; break; }
                if (hasElse) {
                    lua += "    else\n";
                    EmitExecChain(lua, falsePin, depth + 1);
                }
                lua += "    end\n";
                break;
            }
            default:
                break;
        }
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

    // Collect the Param nodes into a `properties` table. Each unique name (the
    // first occurrence wins its default) becomes an Inspector-editable field the
    // engine reads at load. Param value nodes reference these as properties.<name>.
    std::vector<std::pair<std::string, float>> props;
    for (const GraphNode& n : m_nodes) {
        if (n.kind != NodeKind::Param || n.text[0] == '\0') continue;
        bool seen = false;
        for (const auto& p : props) if (p.first == n.text) { seen = true; break; }
        if (!seen) props.push_back({n.text, n.value});
    }
    if (!props.empty()) {
        lua += "properties = {\n";
        for (const auto& p : props) {
            char buf[160];
            snprintf(buf, sizeof(buf), "    %s = %.4g,\n", p.first.c_str(), p.second);
            lua += buf;
        }
        lua += "}\n\n";
    }

    // Declare the graph's variables as file-scope locals so they keep their
    // value between frames.
    for (const GraphVar& v : m_vars) {
        char buf[128];
        snprintf(buf, sizeof(buf), "local %s = %.4g\n", v.name.c_str(), v.init);
        lua += buf;
    }
    if (!m_vars.empty()) lua += "\n";

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
