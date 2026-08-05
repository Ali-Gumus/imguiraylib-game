#pragma once

// A catalogue of everything a script can call.
// ----------------------------------------------------------------------------
// Every binding file carries TWO functions that sit next to each other:
//
//   registerXxxBindings(sol::state&)   wires the API into a Lua state
//   describeXxxBindings(LuaApiRegistry&)  says what that API is
//
// Keeping them in the same file is the entire point. A binding and its
// documentation drift apart the moment they live apart, and a list of calls
// written somewhere else goes stale the first time someone adds a function
// without remembering the list exists.
//
// The catalogue is built once, on first use, and is what an editor panel or a
// generated reference page would read.
//
// ONE HONEST LIMITATION: nothing forces describeXxx to match registerXxx.
// Adding a binding and forgetting to describe it is still possible - the
// compiler cannot check prose. What it does buy is that both live in the same
// file, so the omission is visible while you are editing rather than a month
// later.

#include <string>
#include <vector>

namespace eng {

// One entry: what to type, how to call it, and what it does.
struct LuaApiEntry {
    std::string name;        // the insertable token, e.g. "scene.spawn"
    std::string signature;   // how it is called, e.g. "scene.spawn(name, x, y, z)"
    std::string description; // a one-line summary
};

// Collects LuaApiEntry items. The fluent helpers exist so a binding writes its
// table or type name ONCE instead of repeating it on every line - repetition is
// how a prefix ends up spelled two different ways in the same file.
class LuaApiRegistry {
public:
    // The raw form. Returns *this so calls can be chained.
    LuaApiRegistry& Add(std::string name, std::string signature,
                        std::string description);

    // Scoped helper for a usertype - a C++ type Lua can hold and call methods
    // on. `typeName` is what it is, `varName` is what an example calls it:
    //   Prop("position", ...)        -> "transform.position"
    //   Method("lookAt(x, y, z)")    -> name "lookAt",
    //                                   signature "transform:lookAt(x, y, z)"
    // The dot-versus-colon distinction is not cosmetic: getting it wrong in Lua
    // is a runtime error about a nil value, so the docs have to model it.
    struct TypeDoc {
        LuaApiRegistry& reg;
        std::string type;
        std::string var;
        TypeDoc& Prop(const std::string& field, const std::string& description);
        TypeDoc& Method(const std::string& callSig, const std::string& description);
    };
    TypeDoc Usertype(std::string typeName, std::string varName);

    // Scoped helper for a table used as a namespace - `scene`, `input`, `draw`:
    //   Fn("spawn(name, x, y, z)")   -> "scene.spawn"
    //   Value("Perspective", ...)    -> "Projection.Perspective"
    struct TableDoc {
        LuaApiRegistry& reg;
        std::string ns;
        TableDoc& Fn(const std::string& callSig, const std::string& description);
        TableDoc& Value(const std::string& key, const std::string& description);
    };
    TableDoc Table(std::string tableName);

    const std::vector<LuaApiEntry>& Entries() const { return m_entries; }

private:
    std::vector<LuaApiEntry> m_entries;
};

// Per-binding documentation, implemented beside the matching registerXxxBindings
// in src/engine/bindings/*.cpp.
void DescribeTransformBindings(LuaApiRegistry& api);
void DescribeEntityBindings(LuaApiRegistry& api);
void DescribeSceneBindings(LuaApiRegistry& api);
void DescribeInputBindings(LuaApiRegistry& api);
void DescribeHudBindings(LuaApiRegistry& api);
void DescribeDrawBindings(LuaApiRegistry& api);
void DescribeFxBindings(LuaApiRegistry& api);
void DescribeAudioBindings(LuaApiRegistry& api);
void DescribePhysicsBindings(LuaApiRegistry& api);
void DescribeLightBindings(LuaApiRegistry& api);
void DescribeHealthBindings(LuaApiRegistry& api);
void DescribeRigidBodyBindings(LuaApiRegistry& api);
void DescribeColliderBindings(LuaApiRegistry& api);
void DescribeModelBindings(LuaApiRegistry& api);
void DescribeShapeBindings(LuaApiRegistry& api);
void DescribeTerrainBindings(LuaApiRegistry& api);
void DescribeMinimapBindings(LuaApiRegistry& api);
void DescribeScriptBindings(LuaApiRegistry& api);
void DescribeJSBSimBindings(LuaApiRegistry& api);
void DescribeCameraBindings(LuaApiRegistry& api);

// The whole catalogue, built once on first call. Anything that wants to show or
// print the scripting API reads this rather than assembling its own list.
const std::vector<LuaApiEntry>& GetLuaApiEntries();

} // namespace eng
