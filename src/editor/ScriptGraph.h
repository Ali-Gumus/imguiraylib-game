#pragma once

#include <imgui_node_editor.h>

#include <string>
#include <vector>

namespace edtr {

// The type a pin carries. Exec pins pass control flow (the white "then do
// this" wires); Float pins pass a number along a data wire. More types (Bool,
// Vector3) arrive in later stages.
enum class PinType { Exec, Float, Bool };

// Every kind of node the graph can hold. "Event" nodes are the entry points
// the engine triggers. "Value" nodes are pure (no exec pins) and just compute
// a number. "Action" nodes run in the exec flow and read numbers from their
// data inputs.
enum class NodeKind {
    // events (exec output; Update also outputs dt)
    EventCreate, EventUpdate, EventDestroy,
    // value nodes (data only)
    Number, Add, Sub, Mul, Div,
    // action nodes (exec flow, with float inputs)
    Yaw, Pitch, Roll, MoveForward, Print, DestroySelf,
    // Stage 2: booleans and branching
    KeyDown,                 // value: is a named key held? -> Bool
    Greater, Less, Equal,    // value: compare two floats -> Bool
    Branch,                  // action: run one of two exec outputs by a Bool
    // Stage 3: variables (persistent float state)
    GetVar,                  // value: read a variable -> Float
    SetVar,                  // action: write a Float into a variable
    // Stage 4: engine access + math + canned calls
    PosX, PosY, PosZ,        // value: this entity's position component -> Float
    FwdX, FwdY, FwdZ,        // value: this entity's forward vector component -> Float
    UpX, UpY, UpZ,           // value: this entity's up vector component -> Float
    Sqrt, Exp,               // value: unary math (sqrt / e^x) -> Float
    SetPosX, SetPosY, SetPosZ, // action: write a position component
    TurnToPlayer,            // action: rotate toward the nearest "player"
    Fire,                    // action: spawn a bullet forward
    IsPlayerNear,            // value: is a "player" within range? -> Bool
    // Stage 5: projectile helpers. Canned engine calls (like Fire above), so
    // they need no new pin types.
    SetScale,                // action: set all three scale components at once
    HitNearest,              // action: damage the nearest tagged entity in range,
                             //         then remove self (a bullet's impact test)
};

// One pin on a node. `slot` is its fixed position within the node (see the
// encoding note on GraphNode). Pins are described by each node's signature,
// not stored per node.
struct Pin {
    int         slot;
    PinType     type;
    bool        input;     // true = an input (left side), false = output (right)
    const char* name;
};

// One node placed on the canvas.
struct GraphNode {
    int       id = 0;
    NodeKind  kind{};
    float     value = 0.0f;              // the constant for a Number node
    char      text[128] = "hello";       // the message for a Print node
    float     x = 0.0f, y = 0.0f;        // canvas position (saved)
};

// A wire between two pins (an output pin to an input pin).
struct GraphLink {
    int id      = 0;
    int fromPin = 0;
    int toPin   = 0;
};

// A persistent variable the graph declares. It becomes a file-scope Lua local
// that keeps its value between frames (throttle, cooldown timers, ...). Get/Set
// nodes reference it by name.
struct GraphVar {
    std::string name = "var";
    float       init = 0.0f;   // starting value
};

// The whole graph: nodes + wires, drawn on the canvas and compiled to Lua.
class ScriptGraph {
public:
    ScriptGraph();
    void Reset();   // clear back to just the three event nodes

    void Draw(ax::NodeEditor::EditorContext* ctx);

    bool Save(const std::string& path) const;
    bool Load(const std::string& path);
    bool GenerateLua(const std::string& path) const;

private:
    // A pin id packs the node id and the pin's slot: id = node*16 + slot.
    // 16 slots per node leaves room for a few exec and data pins each.
    static int  PinId(int node, int slot) { return node * 16 + slot; }
    static int  PinToNode(int pin)        { return pin / 16; }
    static int  PinToSlot(int pin)        { return pin % 16; }

    // Slot layout used by the signatures below.
    static constexpr int SlotExecIn   = 0;
    static constexpr int SlotExecOut  = 1;
    static constexpr int SlotExecOut2 = 2;   // second exec output (Branch's "false")
    static constexpr int SlotDataIn   = 4;   // first data-input slot (4, 5, ...)
    static constexpr int SlotDataOut  = 10;  // first data-output slot (10, 11, ...)

    // The pins a node kind has. Computed on demand rather than stored.
    static std::vector<Pin> Signature(NodeKind kind);
    static bool IsPure(NodeKind kind);       // a value node with no exec pins?
    static const char* Title(NodeKind kind);
    // The type of a pin (by its full pin id), for validating that a wire only
    // joins matching pin types.
    PinType PinTypeOf(int pin) const;

    GraphNode*       FindNode(int id);
    const GraphNode* FindNode(int id) const;
    // The node/pin wired into an input pin, or nullptr if nothing is.
    const GraphNode* SourceOf(int inputPin) const;

    void DrawNode(GraphNode& n);
    void HandleEdits();
    void HandleContextMenu();

    // Codegen helpers.
    std::string ExprForInput(int inputPin) const;       // expression feeding a data input
    std::string ExprForNode(const GraphNode& n) const;  // a value node's expression
    void        EmitExecChain(std::string& lua, int fromExecPin, int depth = 0) const;
    void        EmitEvent(std::string& lua, NodeKind ev,
                          const char* header, bool provideDt) const;

    void DrawVariablesUI();   // the little "Variables" list at the top of the panel

    std::vector<GraphNode> m_nodes;
    std::vector<GraphLink> m_links;
    std::vector<GraphVar>  m_vars;
    int   m_nextID = 100;
    bool  m_restorePositions = false;
    float m_popupX = 0, m_popupY = 0;
};

} // namespace edtr
