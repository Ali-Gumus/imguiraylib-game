#include "engine/Scene.h"
#include "engine/Components.h"   // MakeComponent — the registry, for loading

#include "raymath.h"             // Matrix math (raylib's bundled glm-equivalent)
#include "rlgl.h"                // matrix stack for Draw

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
        je["parent"]    = e.parent;
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
        e.id     = je.value("id", 0u);
        e.name   = je.value("name", "unnamed");
        e.parent = je.value("parent", kInvalidEntity);
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

    // Children lose their parent, not their life. (Unity destroys the
    // whole subtree instead — a policy we can adopt later if we want.)
    for (Entity& e : m_entities)
        if (e.parent == id) e.parent = kInvalidEntity;

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

const Entity* Scene::FindConst(EntityID id) const {
    for (const Entity& e : m_entities)
        if (e.id == id) return &e;
    return nullptr;
}

void Scene::Start() {
    for (Entity& e : m_entities)
        for (auto& c : e.components)
            c->OnStart(e);
}

static Scene* s_current = nullptr;
Scene* Scene::Current() { return s_current; }

void Scene::QueueDestroy(EntityID id) { m_destroyQueue.push_back(id); }
void Scene::QueueSpawnCube(const std::string& name, Vector3 position) {
    m_spawnQueue.push_back({name, position});
}

void Scene::Update(float dt) {
    s_current = this;                 // scripts' scene.* calls land here
    for (Entity& e : m_entities)
        for (auto& c : e.components)
            c->OnUpdate(dt, e);
    s_current = nullptr;

    // Process deferred edits now that no one is iterating the vector.
    for (EntityID id : m_destroyQueue)
        DestroyEntity(id);            // fires on_destroy hooks as usual
    m_destroyQueue.clear();

    for (const SpawnRequest& req : m_spawnQueue) {
        Entity* e = Find(CreateEntity(req.name));
        e->transform.position = req.position;
        e->AddComponent<ShapeComponent>();
    }
    m_spawnQueue.clear();
}

// Local TRS matrix, matching the old rl-stack order: scale first, then
// rotation Z,X,Y (roll, pitch, yaw), then translation.
// ignoreScale = pretend scale is 1,1,1 (see WorldMatrix).
static Matrix LocalMatrix(const Transform3D& t, bool ignoreScale) {
    Matrix rot = MatrixMultiply(
        MatrixMultiply(MatrixRotateZ(t.rotation.z * DEG2RAD),
                       MatrixRotateX(t.rotation.x * DEG2RAD)),
        MatrixRotateY(t.rotation.y * DEG2RAD));
    Matrix scale = ignoreScale ? MatrixIdentity()
                               : MatrixScale(t.scale.x, t.scale.y, t.scale.z);
    return MatrixMultiply(
        MatrixMultiply(scale, rot),
        MatrixTranslate(t.position.x, t.position.y, t.position.z));
}

Matrix Scene::WorldMatrix(const Entity& e, bool ignoreScale) const {
    Matrix world = LocalMatrix(e.transform, ignoreScale);
    // Walk up the ancestor chain, multiplying local into each parent.
    // Scale PROPAGATES (Unity's model) — the editor compensates local
    // scale on reparenting so things don't visibly resize; the camera
    // opts out entirely via ignoreScale.
    // Depth guard: a corrupt file with a parent loop must not hang us.
    const Entity* p = FindConst(e.parent);
    for (int guard = 0; p && guard < 64; ++guard) {
        world = MatrixMultiply(world, LocalMatrix(p->transform, ignoreScale));
        p = FindConst(p->parent);
    }
    return world;
}

Vector3 Scene::WorldScale(const Entity& e) const {
    // Component-wise product up the chain. (Approximation: treats scale
    // axes as aligned — exact when ancestors aren't rotated relative to
    // each other, close enough for editor compensation otherwise.)
    Vector3 s = e.transform.scale;
    const Entity* p = FindConst(e.parent);
    for (int guard = 0; p && guard < 64; ++guard) {
        s.x *= p->transform.scale.x;
        s.y *= p->transform.scale.y;
        s.z *= p->transform.scale.z;
        p = FindConst(p->parent);
    }
    return s;
}

bool Scene::WouldCycle(EntityID child, EntityID newParent) const {
    // Walk up from the proposed parent; meeting `child` means a loop.
    for (const Entity* p = FindConst(newParent); p; p = FindConst(p->parent))
        if (p->id == child) return true;
    return false;
}

void Scene::Draw() const {
    // The scene owns the hierarchy math: push each entity's WORLD matrix,
    // components draw in local space (unit-sized at the origin) and don't
    // even know parents exist.
    for (const Entity& e : m_entities) {
        Matrix world = WorldMatrix(e);
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(world));
        for (const auto& c : e.components)
            c->OnDraw(e);
        rlPopMatrix();
    }
}

} // namespace eng
