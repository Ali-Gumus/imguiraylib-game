#pragma once

#include <imgui_node_editor.h>

#include <string>
#include <vector>

namespace edtr {

// What a node IS. Events are entry points (the engine fires them);
// actions are ready-made blocks the user chains after an event.
enum class NodeType {
    EventCreate,    // -> lua on_start
    EventUpdate,    // -> lua on_update
    EventDestroy,   // -> lua on_destroy
    ActionSpin,     // rotate around Y
    ActionMove,     // translate by offset
    ActionPrint,    // print a message (great for testing on_destroy)
    CondKey,        // gate: the rest of the chain runs only while a key is held
    CondEvery,      // gate: fires its branch once every N seconds
    ActionDestroySelf,  // queue this entity's destruction (safe mid-update)
    ActionSpawnCube,    // queue spawning a cube at entity position + offset
};

// Our OWN data model of the graph. The node-editor library only handles
// drawing/interaction; what nodes exist, their params, and the links are
// ours — which is exactly what makes save/load and codegen possible.
struct GraphNode {
    int      id = 0;
    NodeType type{};

    // Parameters (which ones matter depends on type).
    float speed     = 90.0f;                 // Spin: degrees/second
    float offset[3] = {1.0f, 0.0f, 0.0f};    // Move: units/second
    char  text[128] = "entity destroyed!";   // Print: message
    char  key[8]    = "W";                   // CondKey: key name (W, SPACE, UP...)
    float interval  = 1.0f;                  // CondEvery: seconds between firings

    float x = 0.0f, y = 0.0f;                // canvas position (saved)
};

struct GraphLink {
    int id      = 0;
    int fromPin = 0;   // an output pin
    int toPin   = 0;   // an input pin
};

// One graph == one generated .lua script.
class ScriptGraph {
public:
    ScriptGraph();     // seeds the three event nodes

    // Blank the graph back to just the three event nodes ("New").
    void Reset();

    // Draw the canvas + handle linking/menus. Call between ImGui::Begin/End.
    void Draw(ax::NodeEditor::EditorContext* ctx);

    bool Save(const std::string& path) const;   // graph as JSON (the source of truth)
    bool Load(const std::string& path);
    bool GenerateLua(const std::string& path) const;   // graph -> .lua (build artifact)

private:
    // Pin ids are derived from node ids — no separate bookkeeping:
    //   flow-in  = node*4 + 1     flow-out = node*4 + 2
    static int InPin(int node)   { return node * 4 + 1; }
    static int OutPin(int node)  { return node * 4 + 2; }
    static int PinToNode(int pin) { return pin / 4; }

    GraphNode*       FindNode(int id);
    const GraphNode* FindNode(int id) const;
    std::vector<const GraphNode*> NextInChain(int fromNodeId) const;   // follow flow links

    void DrawNode(GraphNode& n);
    void HandleEdits();          // link creation / deletion, node deletion
    void HandleContextMenu();    // right-click: add action blocks

    std::vector<GraphNode> m_nodes;
    std::vector<GraphLink> m_links;
    int  m_nextID = 100;              // 1..99 reserved for event nodes
    bool m_restorePositions = false;  // after Load: push saved x/y to canvas
    float m_popupX = 0, m_popupY = 0; // canvas pos of the last right-click
};

} // namespace edtr
