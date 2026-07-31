#include "engine/LuaApiRegistry.h"

namespace eng {

namespace {
// The token before the first '(' in a call signature:
//   "spawn(name, x, y, z) -> entity"  ->  "spawn"
// The signature is written once, in full, and the insertable name is derived
// from it - so the two can never disagree about what the function is called.
std::string CallName(const std::string& callSig) {
    const size_t paren = callSig.find('(');
    return (paren == std::string::npos) ? callSig : callSig.substr(0, paren);
}
} // namespace

LuaApiRegistry& LuaApiRegistry::Add(std::string name, std::string signature,
                                    std::string description) {
    m_entries.push_back({std::move(name), std::move(signature),
                         std::move(description)});
    return *this;
}

LuaApiRegistry::TypeDoc LuaApiRegistry::Usertype(std::string typeName,
                                                 std::string varName) {
    return TypeDoc{*this, std::move(typeName), std::move(varName)};
}

LuaApiRegistry::TypeDoc& LuaApiRegistry::TypeDoc::Prop(
        const std::string& field, const std::string& description) {
    // A field is read and written with a DOT, and reads the same as it is
    // typed, so the token and the signature are identical.
    const std::string token = var + "." + field;
    reg.Add(token, token, description);
    return *this;
}

LuaApiRegistry::TypeDoc& LuaApiRegistry::TypeDoc::Method(
        const std::string& callSig, const std::string& description) {
    // A method is called with a COLON, which passes the object as the hidden
    // first argument. The insertable token is just the method name, because
    // that is what gets typed after the colon.
    reg.Add(CallName(callSig), var + ":" + callSig, description);
    return *this;
}

LuaApiRegistry::TableDoc LuaApiRegistry::Table(std::string tableName) {
    return TableDoc{*this, std::move(tableName)};
}

LuaApiRegistry::TableDoc& LuaApiRegistry::TableDoc::Fn(
        const std::string& callSig, const std::string& description) {
    reg.Add(ns + "." + CallName(callSig), ns + "." + callSig, description);
    return *this;
}

LuaApiRegistry::TableDoc& LuaApiRegistry::TableDoc::Value(
        const std::string& key, const std::string& description) {
    const std::string token = ns + "." + key;
    reg.Add(token, token, description);
    return *this;
}

const std::vector<LuaApiEntry>& GetLuaApiEntries() {
    // Built once, on first call, and kept. The order matches the order
    // ScriptComponent::Load registers the bindings, so a reader following the
    // catalogue meets the API in the same sequence a script does.
    static const std::vector<LuaApiEntry> entries = [] {
        LuaApiRegistry reg;
        DescribeTransformBindings(reg);
        DescribeEntityBindings(reg);
        DescribeInputBindings(reg);
        DescribeHudBindings(reg);
        DescribeDrawBindings(reg);
        DescribeFxBindings(reg);
        DescribeAudioBindings(reg);
        DescribePhysicsBindings(reg);
        DescribeLightBindings(reg);
        DescribeSceneBindings(reg);
        DescribeCameraBindings(reg);
        return reg.Entries();
    }();
    return entries;
}

} // namespace eng
