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
    HudSet,                  // action: publish a number to the C++ HUD by name
    // Stage 6: enemy AI helpers (canned calls, still float/bool only)
    AvoidCrowd,              // action: steer away from the nearest same-tag
                             //         neighbour (squadron separation)
    AimedAtPlayer,           // value: is the nearest "player" both within range
                             //        and within a firing cone? -> Bool
    // Stage 7: a follow ("chase") camera behaviour in one node
    ChaseTarget,             // action: ease toward a point behind/above a named
                             //         target and look at it (a chase camera)
    // Stage 8: boolean combiners and the remaining comparisons, so compound
    // conditions need not be split into nested Branch nodes.
    And, Or,                 // value: combine two Bools -> Bool
    LessEqual, GreaterEqual, // value: compare two floats -> Bool
    // Stage 9: a tunable exposed to the Inspector. Each Param node contributes
    // one entry to a generated `properties` table (name = default) and reads
    // back as properties.<name>, so it is live-editable in the Inspector.
    Param,                   // value: a named, Inspector-tunable float
    // Stage 10: counting loops and the pieces that make them useful.
    For,                     // action: run a body once per step from..to, with
                             //         a loop-variable "i" output for the body
    Sin, Cos, Floor,         // value: more unary math (radians)
    SpawnCube,               // action: scene.spawn_cube(name, x, y, z)
    // Stage 11: general entity spawning and tag counting.
    Spawn,                   // action: scene.spawn with a script, tag and health
    CountTag,                // value: scene.count(tag) -> Float
    // Stage 12: presentation. These reach the parts of the engine that make the
    // game look alive, so a graph is not limited to moving things about.
    // NOTE: always APPEND to this list. Saved graphs store each node's kind as
    // the number it has here, so inserting a value in the middle would silently
    // turn every later node in every saved graph into a different node.
    FxBurst,                 // action: fire a named particle effect at this
                             //         entity (fx.burst)
    SetLightIntensity,       // action: set the scene light's brightness
    PlaySound,               // action: play a named sound (audio.play)
    LoopStart,               // action: start a looping sound (audio.loop_start)
    LoopSet,                 // action: set a running loop's volume and pitch
    LoopStop,                // action: stop a looping sound
    // Stage 13: physics, and the collision event that goes with it. These are
    // what let a graph describe an object that the simulation owns rather than
    // one the script pushes around by hand.
    EventCollision,          // event: something struck this entity. Outputs the
                             //        impact speed and where the surfaces met.
    OtherTagIs,              // value: is the thing we hit tagged this? -> Bool.
                             //        Only meaningful inside an On Collision.
    DamageOther,             // action: scene.damage on the thing we hit. Also
                             //         only meaningful inside an On Collision.
    SetBody,                 // action: hand this entity to the simulation
    SetVelocity,             // action: set its velocity outright
    ApplyForce,              // action: push it, in world axes
    ApplyLocalForce,         // action: push it along its own axes (thrust)
    Speed,                   // value: how fast it is moving -> Float
    // Positioned audio and effects: the same calls as above, but heard and seen
    // at a point in the world rather than at the listener or the entity.
    PlaySoundAt,             // action: audio.play_at(name, x, y, z)
    LoopAt,                  // action: audio.loop_at(name, x,y,z, vol, pitch)
    FxBurstAt,               // action: fx.burst(name, x, y, z, scale)
    // The two remaining gaps that stopped a graph reproducing its script.
    SetCollider,             // action: give this entity a collision volume
    HudAdd,                  // action: ADD to a HUD value (HudSet only sets, so
                             //         a score could not be awarded from a graph)
    HudGet,                  // value: read a HUD value back -> Float. Lets a
                             //        graph branch on shared game state, which
                             //        is how a game-over guard is written.
                             // (APPENDED, per the note above: putting this in
                             //  front of HudAdd would have renumbered it and
                             //  silently turned it into a different node in
                             //  every graph already using it.)
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
    char      text[128]  = "hello";      // the message for a Print node
    char      text2[128] = "";           // a second string (Spawn's tag)
    // A third string. Currently the Spawn node's model name - which entry in
    // assets/scripts/models.lua the new entity should look like, or empty for
    // the default cube.
    char      text3[128] = "";
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

    // The generated Lua as a string. Used when a graph attached to an entity is
    // compiled and run directly, with no file involved.
    std::string GenerateLuaSource() const;
    // The same, written to a .lua file.
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
    // Draws the effect list for an FX Burst node. Kept separate from the node
    // body because a popup can only be placed correctly outside the canvas's
    // coordinate system (see the comment on the function).
    void HandleFxPicker();

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
    // Which FX Burst node the effect list belongs to, or -1 for none. A node
    // body records the request; HandleFxPicker shows the list and writes the
    // choice back.
    int   m_fxPickerNode = -1;
    // Set for ONE frame when the button is pressed. Opening a popup must happen
    // exactly once: if the request stayed set, then the frame the user clicked
    // away - which closes the popup - would immediately reopen it, and the menu
    // could never be dismissed.
    bool  m_fxPickerOpen = false;
    // Which of the node's text fields the picker writes into: 0 = `text` (an
    // FX Burst node's effect), 1 = `text2` (a Hit Nearest node's optional
    // impact effect), 2 = `text3` (a Spawn node's model). One picker serves
    // them all rather than duplicating the list three times.
    int   m_fxPickerField = 0;
    // Which list the picker is showing. One picker serves all of them, since
    // the job is identical every time: choose a name the engine already knows
    // about, so a graph never has to have a file path typed into it.
    enum class PickList { Effects, Sounds, Models };
    PickList m_fxPickerList = PickList::Effects;
    // May the choice be "none"? An FX Burst must name an effect, but a Hit
    // Nearest's impact effect and a Spawn's model are both optional.
    bool  m_fxPickerOptional = false;
    // Where to put the list, in SCREEN coordinates, so it drops directly under
    // the button like an ordinary combo box. Captured while drawing the node,
    // because that is the only place the button's position is known, and
    // converted out of canvas space there too.
    float m_fxPickerX = 0, m_fxPickerY = 0, m_fxPickerW = 140.0f;
};

} // namespace edtr
