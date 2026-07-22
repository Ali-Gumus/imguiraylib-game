#pragma once

#include <imgui_node_editor.h>   // the third-party node-canvas library

#include <string>
#include <vector>

namespace edtr {   // "edtr" = editor code, separate from the engine's "eng"

// The kinds of node a graph can contain. "Event" nodes are entry points that
// the engine triggers (they become the Lua on_start / on_update / on_destroy
// functions). "Action" nodes are ready-made steps the user chains after an
// event. "Cond" nodes are gates that only let the chain continue under a
// condition.
enum class NodeType {
    EventCreate,        // start of the on_start function
    EventUpdate,        // start of the on_update function
    EventDestroy,       // start of the on_destroy function
    ActionSpin,         // rotate the entity around its Y axis
    ActionMove,         // move the entity by a fixed world offset
    ActionPrint,        // print a text message (handy for testing)
    CondKey,            // continue only while a named key is held
    CondEvery,          // continue once every N seconds
    ActionDestroySelf,  // destroy this entity
    ActionSpawnCube,    // spawn a cube near this entity
    ActionMoveForward,  // move along the entity's own facing
};

// Our own description of one node. The node-editor library only draws things
// and reports user interactions; the actual data (which nodes exist, their
// parameters, how they connect) lives in these structs, which is what lets us
// save the graph and generate code from it.
struct GraphNode {
    int      id = 0;        // unique id for this node
    NodeType type{};        // which kind of node it is

    // Parameter values. Which of these a node actually uses depends on its
    // type (a Spin node uses `speed`, a Print node uses `text`, and so on).
    float speed     = 90.0f;                 // Spin: degrees per second; MoveForward: units/sec
    float offset[3] = {1.0f, 0.0f, 0.0f};    // Move / SpawnCube: an x,y,z offset
    char  text[128] = "entity destroyed!";   // Print message / SpawnCube name
    char  key[8]    = "W";                   // CondKey: key name like "W" or "SPACE"
    float interval  = 1.0f;                  // CondEvery: seconds between firings

    float x = 0.0f, y = 0.0f;   // the node's position on the canvas (saved to file)
};

// A connection from one node's output to another node's input.
struct GraphLink {
    int id      = 0;    // unique id for this link
    int fromPin = 0;    // the output pin it starts at
    int toPin   = 0;    // the input pin it ends at
};

// A ScriptGraph is one visual program. It can be saved as JSON, reloaded, and
// turned ("generated") into a .lua script that entities run.
class ScriptGraph {
public:
    ScriptGraph();     // starts with the three event nodes present

    // Clear everything back to just the three event nodes (the "New" button).
    void Reset();

    // Draw the canvas and handle the user's interactions for this frame.
    // `ctx` is the node-editor library's canvas state. Call between an
    // ImGui::Begin/End pair.
    void Draw(ax::NodeEditor::EditorContext* ctx);

    bool Save(const std::string& path) const;         // write the graph as JSON
    bool Load(const std::string& path);               // read a graph from JSON
    bool GenerateLua(const std::string& path) const;  // turn the graph into a .lua file

private:
    // Every node needs unique ids for its input and output pins. Instead of
    // storing them, we DERIVE them from the node id with simple arithmetic:
    // input pin = id*4+1, output pin = id*4+2, and dividing any pin id by 4
    // gives back its node id.
    static int InPin(int node)   { return node * 4 + 1; }
    static int OutPin(int node)  { return node * 4 + 2; }
    static int PinToNode(int pin) { return pin / 4; }

    GraphNode*       FindNode(int id);          // look a node up by id (writable)
    const GraphNode* FindNode(int id) const;    // read-only version
    // All nodes connected downstream of a node (a node may branch into several).
    std::vector<const GraphNode*> NextInChain(int fromNodeId) const;

    void DrawNode(GraphNode& n);    // draw one node with its pins and fields
    void HandleEdits();             // process the user creating/deleting links and nodes
    void HandleContextMenu();       // the right-click "add node" menu

    std::vector<GraphNode> m_nodes;   // every node in the graph
    std::vector<GraphLink> m_links;   // every connection
    int   m_nextID = 100;             // next id for a new node (1..99 are the event nodes)
    bool  m_restorePositions = false; // after loading, push saved x/y onto the canvas once
    float m_popupX = 0, m_popupY = 0; // canvas position where the add-node menu opened
};

} // namespace edtr
