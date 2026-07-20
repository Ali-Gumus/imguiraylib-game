#include "engine/Scene.h"
#include "engine/Components.h"   // MakeComponent — the registry, for loading

#include <algorithm>
#include <filesystem>
#include <fstream>

using nlohmann::json;

namespace eng {

// Small helpers: Vector3 <-> json array [x, y, z].
static json ToJson(const Vector3& v) { return {v.x, v.y, v.z}; }
static Vector3 Vec3FromJson(const json& j, Vector3 fallback) {
    if (!j.is_array() || j.size() != 3) return fallback;
    return {j[0], j[1], j[2]};
}

bool Scene::Save(const std::string& path) const {
    json doc;
    doc["nextID"] = m_nextID;         // so loaded scenes keep ids unique

    for (const Entity& e : m_entities) {
        json je;
        je["id"]        = e.id;
        je["name"]      = e.name;
        je["transform"] = {{"position", ToJson(e.transform.position)},
                           {"rotation", ToJson(e.transform.rotation)},
                           {"scale",    ToJson(e.transform.scale)}};
        for (const auto& c : e.components) {
            json jc;
            jc["type"] = c->Name();   // the registry key for loading
            c->Serialize(jc);         // component adds its own fields
            je["components"].push_back(jc);
        }
        doc["entities"].push_back(je);
    }

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream file(path);
    if (!file) return false;
    file << doc.dump(2);              // 2 = indent: human-readable, diffable
    return true;
}

bool Scene::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;

    // Parse BEFORE touching m_entities: a corrupt file must not eat the
    // scene the user currently has open.
    json doc = json::parse(file, /*callback=*/nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) return false;

    std::vector<Entity> loaded;
    for (const json& je : doc.value("entities", json::array())) {
        Entity e;
        e.id   = je.value("id", 0u);
        e.name = je.value("name", "unnamed");
        if (je.contains("transform")) {
            const json& jt = je["transform"];
            e.transform.position = Vec3FromJson(jt.value("position", json()), {0, 0, 0});
            e.transform.rotation = Vec3FromJson(jt.value("rotation", json()), {0, 0, 0});
            e.transform.scale    = Vec3FromJson(jt.value("scale",    json()), {1, 1, 1});
        }
        for (const json& jc : je.value("components", json::array())) {
            auto comp = MakeComponent(jc.value("type", ""));
            if (!comp) continue;      // unknown type: skip, don't fail the load
            comp->Deserialize(jc);
            e.components.push_back(std::move(comp));
        }
        loaded.push_back(std::move(e));
    }

    m_entities = std::move(loaded);
    m_nextID   = doc.value("nextID", (EntityID)(m_entities.size() + 1));
    return true;
}

EntityID Scene::CreateEntity(const std::string& name) {
    Entity e;
    e.id   = m_nextID++;              // ids are never reused, even after destroy
    e.name = name;
    m_entities.push_back(std::move(e));   // move: Entity owns unique_ptrs, no copies
    return m_entities.back().id;
}

void Scene::DestroyEntity(EntityID id) {
    // Give components a last word (scripts' on_destroy) BEFORE the memory goes.
    if (Entity* e = Find(id))
        for (auto& c : e->components)
            c->OnDestroy(*e);

    // erase + remove_if: the standard C++ idiom for "delete matching
    // elements from a vector" (remove_if compacts, erase truncates).
    m_entities.erase(
        std::remove_if(m_entities.begin(), m_entities.end(),
                       [id](const Entity& e) { return e.id == id; }),
        m_entities.end());
}

Entity* Scene::Find(EntityID id) {
    for (Entity& e : m_entities)
        if (e.id == id) return &e;
    return nullptr;
}

void Scene::Start() {
    for (Entity& e : m_entities)
        for (auto& c : e.components)
            c->OnStart(e);
}

void Scene::Update(float dt) {
    for (Entity& e : m_entities)
        for (auto& c : e.components)
            c->OnUpdate(dt, e);
}

void Scene::Draw() const {
    // The scene knows nothing about cubes or models anymore — it just
    // gives every component the chance to draw itself.
    for (const Entity& e : m_entities)
        for (const auto& c : e.components)
            c->OnDraw(e);
}

} // namespace eng
