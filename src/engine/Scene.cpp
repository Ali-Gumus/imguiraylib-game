#include "engine/Scene.h"
#include "engine/Components.h"   // MakeComponent, used when loading a scene file
#include "engine/Lighting.h"     // the shared lighting shader applied while drawing

#include "raymath.h"   // matrix and quaternion math
#include "rlgl.h"      // raylib's lower-level matrix stack (rlPushMatrix, ...)

#include <algorithm>   // std::remove_if
#include <filesystem>  // create directories for a save path
#include <fstream>     // read and write files

// Bring the JSON library's main type into scope as plain `json`.
using nlohmann::json;

namespace eng {

// --- Small conversion helpers between our math types and JSON arrays --------
// A Vector3 is saved as a 3-element array [x, y, z].
static json ToJson(const Vector3& v) { return {v.x, v.y, v.z}; }
static Vector3 Vec3FromJson(const json& j, Vector3 fallback) {
    if (!j.is_array() || j.size() != 3) return fallback;   // wrong shape -> default
    return {j[0], j[1], j[2]};
}

// A rotation quaternion is saved as a 4-element array [x, y, z, w].
static json ToJson(const Quaternion& q) { return {q.x, q.y, q.z, q.w}; }
static Quaternion QuatFromJson(const json& j) {
    if (j.is_array() && j.size() == 4) return {j[0], j[1], j[2], j[3]};
    // An older save format stored rotation as three euler angles in degrees.
    // Detect that (3 elements) and convert it so old scenes still open.
    if (j.is_array() && j.size() == 3)
        return QuaternionFromEuler((float)j[0] * DEG2RAD,
                                   (float)j[1] * DEG2RAD,
                                   (float)j[2] * DEG2RAD);
    return QuaternionIdentity();   // anything else -> no rotation
}

// Write the whole scene to a JSON file. Returns false if the file can't be
// opened for writing.
bool Scene::Save(const std::string& path) const {
    json doc;                       // the top-level JSON object
    doc["nextID"] = m_nextID;       // remember the id counter so ids stay unique

    // Turn every entity into a JSON object and append it to an "entities" list.
    for (const Entity& e : m_entities) {
        json je;
        je["id"]        = e.id;
        je["name"]      = e.name;
        je["tag"]       = e.tag;
        je["parent"]    = e.parent;
        je["transform"] = {{"position", ToJson(e.transform.position)},
                           {"rotation", ToJson(e.transform.rotation)},
                           {"scale",    ToJson(e.transform.scale)}};
        // Each component writes its own fields under its type name, so loading
        // knows which class to rebuild.
        for (const auto& c : e.components) {
            json jc;
            jc["type"] = c->Name();
            c->Serialize(jc);
            je["components"].push_back(jc);
        }
        doc["entities"].push_back(je);
    }

    // Make sure the folder exists, then write the text out.
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream file(path);
    if (!file) return false;
    file << doc.dump(2);   // dump(2) = pretty-print with 2-space indentation
    return true;
}

// Load a scene from a JSON file, replacing the current one. Returns false and
// changes nothing if the file is missing or not valid JSON.
bool Scene::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;

    // Parse the whole file FIRST, into a temporary. Passing allow_exceptions
    // = false means a broken file yields a "discarded" value rather than
    // throwing. We check that before touching the live scene, so a corrupt
    // file can never wipe what you already have open.
    json doc = json::parse(file, /*callback=*/nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) return false;

    // Build the new entity list in a separate vector first.
    std::vector<Entity> loaded;
    for (const json& je : doc.value("entities", json::array())) {
        Entity e;
        // .value(key, fallback) reads a field or uses the fallback if absent,
        // so partially-written or older files still load.
        e.id     = je.value("id", 0u);
        e.name   = je.value("name", "unnamed");
        e.tag    = je.value("tag", "");
        e.parent = je.value("parent", kInvalidEntity);
        if (je.contains("transform")) {
            const json& jt = je["transform"];
            e.transform.position = Vec3FromJson(jt.value("position", json()), {0, 0, 0});
            e.transform.rotation = QuatFromJson(jt.value("rotation", json()));
            e.transform.scale    = Vec3FromJson(jt.value("scale",    json()), {1, 1, 1});
        }
        // Rebuild each component from its saved type name.
        for (const json& jc : je.value("components", json::array())) {
            auto comp = MakeComponent(jc.value("type", ""));
            if (!comp) continue;      // an unknown type is skipped, not fatal
            comp->Deserialize(jc);
            e.components.push_back(std::move(comp));
        }
        loaded.push_back(std::move(e));
    }

    // Only now, after a fully successful parse, swap in the new data.
    m_entities = std::move(loaded);
    m_nextID   = doc.value("nextID", (EntityID)(m_entities.size() + 1));
    return true;
}

EntityID Scene::CreateEntity(const std::string& name) {
    Entity e;
    e.id   = m_nextID++;   // hand out the next id, then increment for the following one
    e.name = name;
    // std::move hands the entity's contents (including its owned components)
    // into the vector without copying.
    m_entities.push_back(std::move(e));
    return m_entities.back().id;
}

EntityID Scene::DuplicateEntity(EntityID id) {
    const Entity* src = FindConst(id);
    if (!src) return kInvalidEntity;      // nothing to duplicate

    Entity copy = src->Clone();           // deep copy (Clone copies the id too)
    EntityID newID = m_nextID++;          // but a duplicate needs its own id
    copy.id   = newID;
    copy.name = src->name + " (copy)";
    m_entities.push_back(std::move(copy));
    return newID;
}

void Scene::DestroyEntity(EntityID id) {
    // Let the entity's components react (a script's on_destroy runs here)
    // while the entity still exists.
    if (Entity* e = Find(id))
        for (auto& c : e->components)
            c->OnDestroy(*e);

    // Any entity that was a child of this one loses its parent (but survives).
    for (Entity& e : m_entities)
        if (e.parent == id) e.parent = kInvalidEntity;

    // Remove the entity from the vector. This is the standard C++
    // "erase-remove" idiom: std::remove_if shuffles all the entities we want
    // to KEEP to the front and returns where the leftovers begin; erase then
    // deletes from there to the end. The [id](...) part is a lambda used as
    // the match test.
    m_entities.erase(
        std::remove_if(m_entities.begin(), m_entities.end(),
                       [id](const Entity& e) { return e.id == id; }),
        m_entities.end());
}

// Look an entity up by id (writable version).
Entity* Scene::Find(EntityID id) {
    for (Entity& e : m_entities)
        if (e.id == id) return &e;
    return nullptr;
}

// Same lookup, read-only. `const` after the function name means it doesn't
// change the scene, so it can be called on a const Scene.
const Entity* Scene::FindConst(EntityID id) const {
    for (const Entity& e : m_entities)
        if (e.id == id) return &e;
    return nullptr;
}

Entity* Scene::FindByName(const std::string& name) {
    for (Entity& e : m_entities)
        if (e.name == name) return &e;
    return nullptr;
}

// Find the closest entity that carries `tag` and is within `maxDist`.
Entity* Scene::FindNearestWithTag(const std::string& tag, Vector3 pos, float maxDist,
                                  EntityID exclude) {
    Entity* best = nullptr;
    // We compare SQUARED distances so we never need the (slower) square root:
    // if a^2 < b^2 then a < b for non-negative distances. Start the best at
    // maxDist^2 so anything farther than maxDist is ignored.
    float bestDist = maxDist * maxDist;
    for (Entity& e : m_entities) {
        if (e.id == exclude) continue;              // skip the searcher itself
        if (e.tag != tag) continue;                 // wrong category, skip
        float dx = e.transform.position.x - pos.x;
        float dy = e.transform.position.y - pos.y;
        float dz = e.transform.position.z - pos.z;
        float d2 = dx * dx + dy * dy + dz * dz;     // squared distance
        if (d2 <= bestDist) { bestDist = d2; best = &e; }
    }
    return best;
}

// Clamp a single number into the range [-limit, +limit].
static float ClampAbs(float v, float limit) {
    if (v >  limit) return  limit;
    if (v < -limit) return -limit;
    return v;
}

// The point of a collider that lies closest to the world-space point `p`.
//
// The trick that makes this simple is a change of coordinates. A rotated box in
// world space is awkward, but if we move `p` INTO the box's own frame - where
// the box is centred on the origin with its faces square to the axes - the
// answer is just "clamp each coordinate". So the routine always:
//   1. transforms `p` into the collider's local space,
//   2. solves the easy, axis-aligned version of the problem there,
//   3. transforms the answer back out to world space.
Vector3 Scene::ClosestPointOnCollider(const Entity& e, const ColliderComponent& c,
                                      Vector3 p) const {
    // The entity's placement in the world (position + rotation of it and all
    // its parents). Scale is deliberately left out: a collider is authored in
    // world units, so scaling the entity must not silently resize its volume.
    Matrix world = WorldMatrix(e, /*ignoreScale=*/true);
    // Combine that with the shape's own offset and rotation inside the entity,
    // giving one matrix that maps the shape's private frame all the way out to
    // the world.
    Matrix shapeToWorld = MatrixMultiply(c.LocalMatrix(), world);
    // The INVERSE matrix undoes that mapping: multiplying a world point by it
    // gives the same point expressed in the shape's own frame, where the shape
    // is centred on the origin and square to the axes.
    Matrix inv = MatrixInvert(shapeToWorld);

    // Step 1: world -> the shape's frame.
    Vector3 local = Vector3Transform(p, inv);

    // Step 2: the closest point on the origin-centred shape.
    Vector3 closest{0.0f, 0.0f, 0.0f};
    switch (c.shape) {
        case ColliderShape::Sphere: {
            // Everything within `radius` of the centre is inside, so a point
            // farther out is pulled straight back along the same direction
            // until its length equals the radius.
            float len = Vector3Length(local);
            closest = (len <= c.radius || len <= 0.0001f)
                        ? local                                        // already inside
                        : Vector3Scale(local, c.radius / len);         // pull to the surface
            break;
        }
        case ColliderShape::Box: {
            // A box is independent on each axis: the nearest point simply keeps
            // each coordinate if it is within the half-extent, or snaps it to
            // the nearest face if it is beyond.
            closest = { ClampAbs(local.x, c.halfExtents.x),
                        ClampAbs(local.y, c.halfExtents.y),
                        ClampAbs(local.z, c.halfExtents.z) };
            break;
        }
        case ColliderShape::Capsule: {
            // A capsule is "every point within `radius` of a line segment". The
            // segment runs along the local Y axis from -height/2 to +height/2
            // (Y-up is the convention physics engines use for capsules).
            // So: find the closest point on that segment, then treat the rest
            // exactly like the sphere case around it.
            float half = c.height * 0.5f;
            Vector3 onAxis{0.0f, ClampAbs(local.y, half), 0.0f};
            Vector3 toPoint = Vector3Subtract(local, onAxis);
            float   len = Vector3Length(toPoint);
            closest = (len <= c.radius || len <= 0.0001f)
                        ? local
                        : Vector3Add(onAxis, Vector3Scale(toPoint, c.radius / len));
            break;
        }
        case ColliderShape::Heightfield: {
            // A landscape is deliberately invisible to these gameplay queries.
            // They exist to answer "did this shot hit that aircraft?", and
            // every one of them searches by TAG - so terrain, which carries no
            // gameplay tag, is never a candidate in the first place. Answering
            // with the shape's origin leaves it effectively unhittable rather
            // than reporting a hit everywhere, which is what returning the
            // query point itself would do.
            //
            // Ground collision is the physics engine's job instead: the
            // heightfield is a real solid surface there, and things land on it.
            closest = {0.0f, 0.0f, 0.0f};
            break;
        }
    }

    // Step 3: the shape's frame -> world.
    return Vector3Transform(closest, shapeToWorld);
}

// Find the closest entity with `tag` whose collider comes within `reach` of the
// point `pos`. `reach` is the querying object's own size (a bullet's radius),
// so the test is "does a ball of radius `reach` centred at `pos` touch the
// collider?" - which is true exactly when the collider's closest point to `pos`
// is no farther away than `reach`.
Entity* Scene::FindHitWithTag(const std::string& tag, Vector3 pos, float reach,
                              EntityID exclude) {
    Entity* best = nullptr;
    float   bestDist = 0.0f;                          // squared dist of the best hit
    for (Entity& e : m_entities) {
        if (e.id == exclude) continue;
        if (e.tag != tag) continue;
        // Only entities that opted into a ColliderComponent can be hit.
        ColliderComponent* col = e.GetComponent<ColliderComponent>();
        if (!col) continue;

        Vector3 nearest = ClosestPointOnCollider(e, *col, pos);
        // Compare SQUARED distances so we never need the (slower) square root:
        // if a^2 <= b^2 then a <= b for non-negative distances.
        float dx = nearest.x - pos.x;
        float dy = nearest.y - pos.y;
        float dz = nearest.z - pos.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= reach * reach &&                    // touching
            (best == nullptr || d2 < bestDist)) {     // and the nearest so far
            bestDist = d2; best = &e;
        }
    }
    return best;
}

// Fire OnStart on every component of every entity (called once at Play).
void Scene::Start() {
    for (Entity& e : m_entities) {
        // Snapshot the component pointers first: a script's on_start may ADD a
        // component (e.g. scene.set_hitbox), which can reallocate e.components
        // and invalidate an iterator mid-loop. The Component objects themselves
        // don't move, so the raw pointers stay valid.
        std::vector<Component*> comps;
        comps.reserve(e.components.size());
        for (auto& c : e.components) comps.push_back(c.get());
        for (Component* c : comps) c->OnStart(e);
    }
}

// A file-scope pointer to whichever scene is currently running Update().
// The static Scene::Current() function exposes it to the script bindings.
static Scene* s_current = nullptr;
Scene* Scene::Current() { return s_current; }

// The three "queue" functions just record a request for later.
void Scene::QueueDestroy(EntityID id) { m_destroyQueue.push_back(id); }
void Scene::QueueSpawnCube(const std::string& name, Vector3 position) {
    m_spawnQueue.push_back({name, position, QuaternionIdentity(), ""});
}
void Scene::QueueSpawn(const std::string& name, Vector3 position,
                       Quaternion rotation, const std::string& script,
                       const std::string& tag, float hp) {
    m_spawnQueue.push_back({name, position, rotation, script, tag, hp});
}

int Scene::CountWithTag(const std::string& tag) const {
    int n = 0;
    for (const Entity& e : m_entities)
        if (e.tag == tag) ++n;
    return n;
}

void Scene::Update(float dt) {
    // Mark this as the active scene so scripts' scene.* calls know where to go.
    s_current = this;
    for (Entity& e : m_entities) {
        // Snapshot component pointers so a script that adds a component during
        // its update can't invalidate this loop (see the note in Start()).
        std::vector<Component*> comps;
        comps.reserve(e.components.size());
        for (auto& c : e.components) comps.push_back(c.get());
        for (Component* c : comps) c->OnUpdate(dt, e);
    }

    // NOTE: the active-scene marker deliberately stays set through everything
    // below. The destroy and spawn passes both run script code - a destroyed
    // entity's on_destroy, and a newly spawned one's on_start - and those are
    // script hooks like any other, so every scene.* call they make must still
    // find the scene. Clearing it before this point makes those calls silently
    // do nothing, which is invisible: the script runs, no error is raised, and
    // the effect simply never happens.

    // Now that the loop above is finished, it is safe to change the entity
    // list. First remove everything that was queued for destruction.
    for (EntityID id : m_destroyQueue)
        DestroyEntity(id);
    m_destroyQueue.clear();

    // Then create everything that was queued to spawn.
    for (const SpawnRequest& req : m_spawnQueue) {
        Entity* e = Find(CreateEntity(req.name));
        e->transform.position = req.position;
        e->transform.rotation = req.rotation;
        e->tag = req.tag;                           // label it (e.g. "enemy")
        e->AddComponent<ShapeComponent>();          // spawned things are visible cubes
        if (req.hp > 0.0f) {                        // give it health so it can be killed
            auto& hc = e->AddComponent<HealthComponent>();
            hc.hp = req.hp;
            hc.max = req.hp;
        }
        if (!req.script.empty()) {                  // and may run a script
            auto& sc = e->AddComponent<ScriptComponent>();
            sc.path = req.script;
            sc.OnStart(*e);   // load + start it immediately, since play is under way
        }
    }
    m_spawnQueue.clear();

    s_current = nullptr;
}

// Build the 4x4 matrix for a single transform, in the order scale, then
// rotate, then translate. Matrix multiplication is read right-to-left when
// applied to a point, so this places a point by first scaling it, then
// rotating, then moving it. ignoreScale builds it as if scale were 1,1,1.
static Matrix LocalMatrix(const Transform3D& t, bool ignoreScale) {
    Matrix rot   = QuaternionToMatrix(t.rotation);
    Matrix scale = ignoreScale ? MatrixIdentity()
                               : MatrixScale(t.scale.x, t.scale.y, t.scale.z);
    return MatrixMultiply(
        MatrixMultiply(scale, rot),
        MatrixTranslate(t.position.x, t.position.y, t.position.z));
}

// The world matrix of an entity: its own local matrix combined with all of
// its ancestors', so a child ends up positioned relative to its parent.
Matrix Scene::WorldMatrix(const Entity& e, bool ignoreScale) const {
    Matrix world = LocalMatrix(e.transform, ignoreScale);
    // Climb from parent to parent, folding each ancestor's local matrix in.
    // The `guard` counter is a safety limit: if a broken save file made a
    // parent loop, this stops after 64 steps instead of looping forever.
    const Entity* p = FindConst(e.parent);
    for (int guard = 0; p && guard < 64; ++guard) {
        world = MatrixMultiply(world, LocalMatrix(p->transform, ignoreScale));
        p = FindConst(p->parent);
    }
    return world;
}

// The entity's total scale, found by multiplying its scale by every ancestor's
// scale, axis by axis. (This ignores ancestor rotation, which is a fine
// approximation for the editor's purposes.)
Vector3 Scene::WorldScale(const Entity& e) const {
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

// Return true if parenting `child` under `newParent` would form a loop. We
// walk up from newParent; if we ever reach `child`, then child is already an
// ancestor of newParent and the link would be circular.
bool Scene::WouldCycle(EntityID child, EntityID newParent) const {
    for (const Entity* p = FindConst(newParent); p; p = FindConst(p->parent))
        if (p->id == child) return true;
    return false;
}

void Scene::Draw() const {
    // Switch the lighting shader on for the whole scene. raylib's simple
    // primitives (cubes, spheres) are drawn straight into a shared batch that
    // uses whichever shader is currently active, so wrapping the loop is what
    // gets them shaded. Loaded models ignore this: they carry their own
    // material, which is pointed at the same shader when the model loads.
    // If the shader failed to compile, this is skipped and everything renders
    // unlit exactly as before.
    if (IsLightingReady()) BeginShaderMode(GetLightingShader());

    // For each entity, put its world matrix on raylib's matrix stack, draw its
    // components (which just draw a unit shape at the origin), then pop the
    // matrix off. This is what makes every component appear at the right
    // place, size and angle without the component knowing about parents.
    for (const Entity& e : m_entities) {
        Matrix world = WorldMatrix(e);
        rlPushMatrix();                         // save the current matrix
        rlMultMatrixf(MatrixToFloat(world));    // apply this entity's world matrix
        for (const auto& c : e.components)
            c->OnDraw(e);
        rlPopMatrix();                          // restore for the next entity
    }

    if (IsLightingReady()) EndShaderMode();     // back to the default shader
}

} // namespace eng
