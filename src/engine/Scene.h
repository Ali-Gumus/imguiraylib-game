#pragma once

// The component base class, so an Entity can hold a list of components.
#include "engine/Component.h"

#include "raylib.h"    // Vector3, Matrix, and the rest of the game framework
#include "raymath.h"   // Quaternion type + vector/matrix math (header-only)

#include <memory>      // std::unique_ptr
#include <string>      // std::string
#include <deque>       // std::deque - the entity store; see the note on m_entities
#include <vector>      // std::vector (a growable array)
#include <cstdint>     // fixed-width integer types like std::uint32_t

namespace eng {

// A "forward declaration": it promises the compiler that a class with this name
// exists somewhere, which is all that is needed to mention it in a function
// signature below. The full definition lives in Components.h; declaring it this
// way keeps Scene.h from having to include that (much larger) header.
class ColliderComponent;

// A Transform3D is where an object sits in 3D space, made of three parts.
struct Transform3D {
    // Location in space. The `{...}` gives each field a default value, so a
    // fresh Transform3D starts at the origin, unrotated, at normal size.
    Vector3 position{0.0f, 0.0f, 0.0f};

    // Orientation, stored as a QUATERNION: four numbers (x,y,z,w) that encode
    // a rotation. {0,0,0,1} means "no rotation". Quaternions are used instead
    // of three angles because they can be combined over and over (spin +
    // pitch + roll) without the axes ever collapsing together ("gimbal lock").
    // The Inspector still shows this as friendly X/Y/Z degrees.
    Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};

    // Size multiplier per axis. 1,1,1 = original size; 2,2,2 = twice as big.
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

// Every entity is identified by a number. `using` makes EntityID a readable
// alias for a 32-bit unsigned integer.
using EntityID = std::uint32_t;
// The id 0 is reserved to mean "no entity / invalid". `constexpr` = a
// compile-time constant; `inline` lets this live safely in a header.
inline constexpr EntityID kInvalidEntity = 0;

// An Entity is one object in the world. It holds only the essentials that
// EVERYTHING needs (an id, a name, a place in space); all other abilities are
// added as Components. This mirrors how engines like Unity model objects.
struct Entity {
    EntityID    id = kInvalidEntity;   // unique number for this entity
    std::string name;                  // human label, shown in the Hierarchy

    // A shared category such as "enemy", "bullet" or "player". Unlike `name`
    // (usually unique) a tag is meant to be reused, so gameplay code can ask
    // "the nearest enemy" instead of hunting for one specific name.
    std::string tag;

    // The id of this entity's PARENT, or kInvalidEntity if it has none.
    // With a parent, `transform` is interpreted RELATIVE to that parent (a
    // "scene graph"): move the parent and the child follows. WorldMatrix()
    // below turns this relative transform into an absolute world position.
    EntityID    parent = kInvalidEntity;

    Transform3D transform;             // local position/rotation/scale

    // The list of attached components. We store unique_ptr<Component> (base
    // class pointers) rather than the components by value, for two reasons:
    //  * polymorphism: we need to call the correct derived OnUpdate/OnDraw,
    //    which only works through a pointer or reference to the base;
    //  * storing derived objects by value in a Component vector would "slice"
    //    them, chopping off the derived part. The pointer avoids that, and
    //    unique_ptr frees each component automatically with the entity.
    std::vector<std::unique_ptr<Component>> components;

    // Create a component of type T, attach it, and return a reference to it.
    // This is a TEMPLATE: T is filled in at the call site, e.g.
    //     entity.AddComponent<ShapeComponent>();
    // The `Args&&... args` / std::forward part is "perfect forwarding": it
    // passes any constructor arguments straight through to T's constructor,
    // unchanged. std::make_unique<T>(...) builds the component on the heap.
    template <typename T, typename... Args>
    T& AddComponent(Args&&... args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *comp;                        // remember it before we move it
        components.push_back(std::move(comp));  // hand ownership to the vector
        return ref;                             // let the caller configure it
    }

    // Make a full independent copy of this entity, including copies of every
    // component. The id is copied too, which lets the editor snapshot the
    // scene on Play and restore the exact same entities on Stop.
    Entity Clone() const {
        Entity c;
        c.id        = id;
        c.name      = name;
        c.tag       = tag;
        c.parent    = parent;
        c.transform = transform;
        for (const auto& comp : components)     // clone each component in turn
            c.components.push_back(comp->Clone());
        return c;
    }

    // Return this entity's component of type T, or nullptr if it has none.
    // dynamic_cast<T*> asks at run time "is this base pointer really a T?" and
    // yields a T* if yes, nullptr if no. It is the simple, readable way to
    // look a component up by type.
    template <typename T>
    T* GetComponent() {
        for (auto& c : components)
            if (T* t = dynamic_cast<T*>(c.get())) return t;
        return nullptr;
    }

    // Read-only version, for code that only has a `const Entity&` - a drawing
    // hook, for instance, which must not change the entity it is drawing.
    template <typename T>
    const T* GetComponent() const {
        for (const auto& c : components)
            if (const T* t = dynamic_cast<const T*>(c.get())) return t;
        return nullptr;
    }
};

// A Scene owns every entity in one world and provides the operations on them.
//
// IMPORTANT rule enforced throughout: refer to entities by their EntityID,
// not by a saved Entity*. The entities live in a std::vector, and when a
// vector grows it may move its contents to new memory, which would leave any
// old pointer dangling. An id stays valid; look the entity up when you need it.
class Scene {
public:
    // Create a new, empty entity (just a transform) with the given name, and
    // return its id.
    EntityID CreateEntity(const std::string& name);

    // Remove an entity (and fire its components' OnDestroy). Its children are
    // detached rather than deleted.
    void     DestroyEntity(EntityID id);

    // Copy an existing entity: the copy gets a fresh id and a "(copy)" name
    // but the same transform and components. Returns the new id, or
    // kInvalidEntity if `id` didn't exist.
    EntityID DuplicateEntity(EntityID id);

    // Look an entity up by id. Returns nullptr if not found. The returned
    // pointer is only safe to use until the entity list changes next.
    Entity*       Find(EntityID id);
    const Entity* FindConst(EntityID id) const;   // const version for read-only code

    // First entity with an exact matching name (nullptr if none). Names are
    // not guaranteed unique, so use distinct names for things you look up.
    Entity* FindByName(const std::string& name);

    // The nearest entity carrying `tag` within `maxDist` of `pos`, or nullptr.
    // This is the game's cheap stand-in for collision detection: instead of a
    // physics engine, a bullet just asks "is any enemy within my hit radius?".
    // `exclude` optionally skips one entity by id (used so an entity searching
    // for others of its own tag doesn't just find itself).
    Entity* FindNearestWithTag(const std::string& tag, Vector3 pos, float maxDist,
                               EntityID exclude = kInvalidEntity);

    // Like FindNearestWithTag, but tests against each candidate's COLLIDER
    // shape instead of its origin point: it hits when the point `pos` comes
    // within `reach` of the collider's volume. This is what a projectile uses,
    // so a shot registers anywhere on a large model - including out at a
    // wingtip - and not only near the model's pivot.
    Entity* FindHitWithTag(const std::string& tag, Vector3 pos, float reach,
                           EntityID exclude = kInvalidEntity);

    // The point of `e`'s collider that lies closest to the world-space point
    // `p`. If `p` is inside the volume, `p` itself is returned (distance zero).
    // Every collision test in the engine is built on this one function: the
    // distance from a point to a shape is the distance from the point to its
    // closest point on that shape.
    Vector3 ClosestPointOnCollider(const Entity& e, const ColliderComponent& c,
                                   Vector3 p) const;

    // Save the whole scene to a JSON file, or load one back. Save returns
    // false if the file can't be written; Load returns false on a missing or
    // corrupt file and leaves the current scene untouched in that case.
    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

    // --- Deferred world edits (what scripts are allowed to request) --------
    // Scripts run in the middle of Update()'s loop over the entity vector.
    // Adding or removing entities during that loop would invalidate the loop,
    // so these calls only RECORD a request; Update() carries them out after
    // the loop finishes.
    void QueueDestroy(EntityID id);
    // Spawn a plain cube at a position (used by the visual node editor).
    void QueueSpawnCube(const std::string& name, Vector3 position);
    // Spawn a cube facing a given orientation and optionally running a script.
    // Projectiles use this: a bullet spawns aimed along the shot and carrying
    // its own flight/hit script. `tag` and `hp` are optional: a non-empty tag
    // labels the entity (e.g. "enemy" so bullets can hit it), and hp > 0 gives
    // it a Health component so it can be damaged and killed.
    // `model` optionally names an entry in assets/scripts/models.lua; the new
    // entity is given that model, with its offsets and scale already set, in
    // place of the default cube. An empty or unknown name leaves the cube.
    //
    // `velocity` is the motion of whatever created the entity, and it is ADDED
    // to whatever the new entity's own script launches it with, after that
    // script's OnStart has run. That ordering is the whole point: a gun's muzzle
    // velocity is measured RELATIVE TO THE GUN, so a round leaves a moving
    // aircraft at its muzzle speed PLUS the aircraft's, exactly as it does in
    // the air. Without it a projectile's speed is measured against the ground,
    // and the faster the shooter flies the less its own fire outruns it.
    //
    // It needs the new entity to end up with a RigidBodyComponent - normally
    // added by its own script during OnStart - because it is applied as that
    // body's starting velocity. An entity that is not simulated ignores it,
    // since there is nothing for a velocity to mean.
    void QueueSpawn(const std::string& name, Vector3 position,
                    Quaternion rotation, const std::string& script,
                    const std::string& tag = "", float hp = 0.0f,
                    const std::string& model = "",
                    Vector3 velocity = {0.0f, 0.0f, 0.0f});

    // How many live entities carry `tag`. Used to tell when a wave is cleared.
    int CountWithTag(const std::string& tag) const;

    // The scene that is currently running Update(), or nullptr otherwise.
    // `static` means it belongs to the class, not one instance; script
    // bindings use it to reach "the active scene" without each script holding
    // a pointer.
    static Scene* Current();

    // Fire OnStart on every component. Call once when play mode begins.
    void Start();

    // Advance the world by one frame: run every component's OnUpdate, then
    // apply the queued spawns/destroys. `dt` is the frame time in seconds.
    void Update(float dt);

    // Draw every entity. The caller must already be inside raylib's
    // BeginMode3D/EndMode3D (i.e. a camera is set): the scene draws the world,
    // but deciding where the camera is is somebody else's job.
    void Draw() const;

    // Compute an entity's absolute (world) transform as a 4x4 matrix, by
    // combining its own local transform with all of its ancestors'. This is
    // the core scene-graph calculation.
    //   ignoreScale = build the matrix as if every scale were 1,1,1 (position
    //   and rotation still inherit). Cameras use this so a scaled parent
    //   doesn't stretch the view or push the camera away.
    Matrix WorldMatrix(const Entity& e, bool ignoreScale = false) const;

    // The entity's total scale, multiplied down the parent chain. The editor
    // uses it so that re-parenting an object doesn't visibly change its size.
    Vector3 WorldScale(const Entity& e) const;

    // Check whether making `child` a child of `newParent` would form a loop
    // (you can't parent an object underneath one of its own descendants).
    bool WouldCycle(EntityID child, EntityID newParent) const;

    // Direct access to the entity list, for the editor's panels to iterate.
    // Two versions: a writable one and a read-only (const) one.
    std::deque<Entity>&       Entities()       { return m_entities; }
    const std::deque<Entity>& Entities() const { return m_entities; }

private:
    // Every entity in the world.
    //
    // A DEQUE rather than a vector, and the difference is load-bearing. A
    // vector moves its whole contents when it grows, so creating an entity
    // while something holds an `Entity&` - which is exactly what a script does
    // when it creates one from inside its own update - leaves that reference
    // pointing at freed memory. A deque only ever appends to a new block, so
    // references to entities that already exist stay valid however many are
    // added after them.
    //
    // That is what allows Scene.createEntity to hand a script a usable entity
    // immediately, instead of the queue-it-and-hope-for-next-frame arrangement
    // a vector would force. Iterators are still invalidated, so the loops that
    // can run script hooks walk by INDEX over a count taken before they start.
    std::deque<Entity> m_entities;
    EntityID m_nextID = 1;             // next id to hand out (0 stays "invalid")

    // One queued spawn request: what to create once Update()'s loop is done.
    struct SpawnRequest {
        std::string name;
        Vector3     position;
        Quaternion  rotation{0, 0, 0, 1};   // identity = unrotated
        std::string script;                 // "" means no script attached
        std::string tag;                    // "" means untagged
        float       hp = 0.0f;              // > 0 adds a Health component
        std::string model;                  // "" means the default cube
        Vector3     velocity{};             // the spawner's motion, added on top
    };
    std::vector<EntityID>     m_destroyQueue;   // entities to remove after the loop
    std::vector<SpawnRequest> m_spawnQueue;     // entities to create after the loop

    // ActiveScene (below) is the only thing allowed to change which scene is
    // current, so it needs access to the private marker.
    friend class ActiveScene;
    static void SetCurrent(Scene* s);
};

// Marks a scene as "the current one" for as long as this object exists, and
// puts things back afterwards.
//
// Script bindings reach the world through Scene::Current(), so ANY code path
// that ends up running a script hook must have a scene marked active first. If
// it does not, every scene.* call the script makes does nothing whatsoever -
// no error, no warning, no clue. That failure has bitten this engine twice:
// once when spawned entities' on_start ran after the marker was cleared, and
// once when physics collisions were reported from outside the update entirely,
// which left bullets unable to destroy themselves and so bouncing off the
// scenery.
//
// Hence a scope guard rather than a pair of assignments: you cannot set it and
// forget to unset it, and it restores whatever was current before rather than
// clearing it, so nesting is safe.
//
// The rule: if your code can reach a script, wrap it in one of these.
class ActiveScene {
public:
    explicit ActiveScene(Scene& s) : m_previous(Scene::Current()) {
        Scene::SetCurrent(&s);
    }
    ~ActiveScene() { Scene::SetCurrent(m_previous); }

    // Copying a scope guard would restore the scene twice, so it is forbidden.
    ActiveScene(const ActiveScene&)            = delete;
    ActiveScene& operator=(const ActiveScene&) = delete;

private:
    Scene* m_previous;
};

} // namespace eng
