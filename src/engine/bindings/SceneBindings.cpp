#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Components.h"
#include "engine/Scene.h"

#include "raymath.h"            // MatrixLookAt, for facing a spawn direction

#include <string>

namespace eng {

void RegisterSceneBindings(sol::state& lua) {
    // The `scene` table lets scripts change the world. Creating and destroying
    // entities only ENQUEUES the request; the scene carries it out after the
    // update loop, so it is safe even to destroy the very entity that asked.
    sol::table scn = lua.create_named_table("Scene");

    scn["destroy"] = [](Entity& e) {
        if (Scene::Current()) Scene::Current()->QueueDestroy(e.id);
    };
    scn["spawnCube"] = [](const std::string& name, float x, float y, float z) {
        if (Scene::Current()) Scene::Current()->QueueSpawnCube(name, {x, y, z});
    };
    // Find another entity by name, or nil. Call it fresh each frame; never
    // store the result, because the entity it points to may be destroyed.
    // Scene.createEntity([name]) -> entity, existing NOW rather than at the end
    // of the frame.
    //
    // This is the one creation call that is not queued, and it can only work
    // because entities live in a deque: appending to one leaves references to
    // every existing entity valid, so a script creating an entity from inside
    // its own update does not pull the ground out from under the loop running
    // it. Scene::Update and Scene::Start walk by index over a count taken
    // before they start, so the new entity simply waits for the next frame to
    // begin updating - the same rule a queued spawn follows.
    //
    // Returning it straight away is the whole point: the script can add
    // components and set the transform there and then, instead of creating
    // something blind and hunting for it next frame.
    scn["createEntity"] = [](sol::optional<std::string> name) -> Entity* {
        Scene* s = Scene::Current();
        if (!s) return nullptr;
        return s->Find(s->CreateEntity(name.value_or(std::string("Entity"))));
    };

    scn["find"] = [](const std::string& name) -> Entity* {
        return Scene::Current() ? Scene::Current()->FindByName(name) : nullptr;
    };
    // spawn(name, x,y,z, dx,dy,dz, script): create an entity at a position,
    // oriented so its forward faces the direction (dx,dy,dz), running `script`.
    // Firing a bullet spawns it facing the shot direction.
    // The next two arguments are optional: a tag (e.g. "enemy") and starting
    // health. Bullets omit them; a wave spawner passes them so the new enemy is
    // tagged and killable.
    //
    // The LAST THREE are the velocity of whatever is doing the spawning, and
    // they are added to whatever the new entity's own script launches it with.
    // A gun on a moving aircraft passes the aircraft's velocity here, which is
    // what makes its rounds leave at their muzzle speed relative to the JET
    // rather than relative to the ground.
    //
    // They come last because they were added last, and everything before them
    // had to keep working untouched - including the Lua the node editor
    // generates. A caller that wants a velocity but no tag, health or model
    // therefore passes nil for those three, which is ugly but honest: the
    // alternative was renumbering arguments that existing graphs already emit.
    scn["spawn"] = [](const std::string& name, float x, float y, float z,
                      float dx, float dy, float dz, const std::string& script,
                      sol::optional<std::string> tag, sol::optional<float> hp,
                      sol::optional<std::string> model,
                      sol::optional<float> vx, sol::optional<float> vy,
                      sol::optional<float> vz) {
        if (!Scene::Current()) return;
        Quaternion rot = QuaternionIdentity();       // default: unrotated
        if (dx * dx + dy * dy + dz * dz > 1e-4f) {   // if a real direction was given
            Matrix view = MatrixLookAt({0, 0, 0}, {dx, dy, dz}, {0, 1, 0});
            rot = QuaternionFromMatrix(MatrixInvert(view));   // face that direction
        }
        // The last argument names an entry in assets/scripts/models.lua, which
        // carries the file, the scale and both offsets - so a spawning script
        // says "heli" and never has to know any of that. Left out, the entity
        // is the usual cube.
        Scene::Current()->QueueSpawn(name, {x, y, z}, rot, script,
                                     tag.value_or(std::string()), hp.value_or(0.0f),
                                     model.value_or(std::string()),
                                     {vx.value_or(0.0f), vy.value_or(0.0f),
                                      vz.value_or(0.0f)});
    };
    // count(tag): how many live entities carry `tag`. A wave is cleared when
    // Scene.count("enemy") reaches zero.
    scn["count"] = [](const std::string& tag) -> int {
        return Scene::Current() ? Scene::Current()->CountWithTag(tag) : 0;
    };
    // findByTag(tag): the first entity carrying `tag`, or nil.
    //
    // Scene.find matches a NAME, which is per-entity, and Scene.nearest wants a
    // position and a radius it does not always have anything sensible to put in.
    // What was missing was the plain question "where is the player" - asked by
    // anything that reports on the player without being attached to it, which is
    // exactly what a HUD must be if it is to outlive the player's death.
    //
    // Returns nil when nothing matches, and a caller has to handle that: the
    // player being GONE is a normal state in a game with a game-over screen, not
    // an error.
    scn["findByTag"] = [](const std::string& tag) -> Entity* {
        Scene* s = Scene::Current();
        if (!s) return nullptr;
        for (Entity& e : s->Entities())
            if (e.tag == tag) return &e;
        return nullptr;
    };
    // nearest(tag, x,y,z, radius): the closest entity carrying `tag` within
    // `radius`, or nil. This is the bullet's simple hit test.
    scn["nearest"] = [](const std::string& tag, float x, float y, float z,
                        float radius) -> Entity* {
        return Scene::Current()
                   ? Scene::Current()->FindNearestWithTag(tag, {x, y, z}, radius)
                   : nullptr;
    };
    // hit(tag, x,y,z, reach): like nearest, but tests the candidate's collider
    // VOLUME rather than its origin point, so a shot lands anywhere on a big
    // model - out at a wingtip included. This is the projectile hit test.
    scn["hit"] = [](const std::string& tag, float x, float y, float z,
                    float reach) -> Entity* {
        return Scene::Current()
                   ? Scene::Current()->FindHitWithTag(tag, {x, y, z}, reach)
                   : nullptr;
    };
    // setHitbox(entity, radius): ensure the entity is hittable, giving it a
    // SPHERE collider of `radius` only when it has no collider at all (e.g. a
    // freshly spawned enemy). If one already exists -- added and sized in the
    // editor -- it is left alone, so pressing Play never resets authored values.
    scn["setHitbox"] = [](Entity& e, float radius) {
        if (!e.GetComponent<ColliderComponent>()) {
            ColliderComponent& c = e.AddComponent<ColliderComponent>();
            c.shape  = ColliderShape::Sphere;
            c.radius = radius;
        }
    };
    // setCollider(entity, shape, a, b, c): the full version of setHitbox for
    // shapes other than a sphere. `shape` is "sphere", "box" or "capsule"; the
    // three numbers mean different things per shape:
    //   sphere  -> a = radius              (b, c unused)
    //   box     -> a, b, c = half extents  (half the size on X, Y, Z)
    //   capsule -> a = radius, b = height  (c unused)
    // Like setHitbox it only ADDS: an authored collider is never overwritten.
    // `b` and `c` are sol::optional, meaning the script may leave them out:
    // Scene.setCollider(e, "sphere", 2) is valid.
    //
    // A capsule is laid along the entity's FORWARD axis, which is worth
    // spelling out because it is not what the raw shape does. A capsule is a
    // cylinder with a hemisphere on each end, and it is defined along its OWN
    // Y axis -- straight up. That is the right shape for something built
    // upright, like a walking character, and exactly wrong for anything built
    // long, like an aircraft or a vehicle: left alone it stands a tall pole
    // through the middle of the airframe, so a round passing the nose or the
    // tail misses while one passing well above the cockpit hits.
    //
    // Turning it a quarter circle about X tips that axis over onto Z, which is
    // this engine's forward direction. There is no vertical-capsule caller to
    // preserve, and a script that wants one can set the component's `rotation`
    // itself afterwards.
    scn["setCollider"] = [](Entity& e, const std::string& shape, float a,
                             sol::optional<float> b, sol::optional<float> c) {
        if (e.GetComponent<ColliderComponent>()) return;
        // value_or(x) reads the number the script passed, or x if it passed none.
        float bv = b.value_or(a);
        float cv = c.value_or(a);
        ColliderComponent& col = e.AddComponent<ColliderComponent>();
        if (shape == "box") {
            col.shape       = ColliderShape::Box;
            col.halfExtents = {a, bv, cv};
        } else if (shape == "capsule") {
            col.shape    = ColliderShape::Capsule;
            col.radius   = a;
            col.height   = bv;
            // Lay it nose to tail. This is the same 90 degrees the Inspector's
            // "Lay Along Forward" button writes, so a capsule built by a script
            // and one built by hand in the editor describe the same volume.
            col.rotation = {90.0f, 0.0f, 0.0f};
        } else {                       // anything else is treated as a sphere
            col.shape  = ColliderShape::Sphere;
            col.radius = a;
        }
    };
    // nearestOther(self, tag, radius): like nearest, but searches from the
    // `self` entity's position and never returns `self`. Used so a group of
    // same-tag agents (e.g. enemies) can steer apart instead of overlapping.
    scn["nearestOther"] = [](Entity& self, const std::string& tag, float radius) -> Entity* {
        return Scene::Current()
                   ? Scene::Current()->FindNearestWithTag(tag, self.transform.position,
                                                          radius, self.id)
                   : nullptr;
    };
    // damage(entity, amount): reduce an entity's Health; if it drops to zero
    // the entity is destroyed (queued). No Health component means no effect.
    // Returns true if this hit destroyed the entity, so a script can react to
    // a kill (e.g. award score). No Health component means no effect (false).
    scn["damage"] = [](Entity& e, float amount) -> bool {
        if (auto* h = e.GetComponent<HealthComponent>()) {
            h->hp -= amount;
            if (h->hp <= 0.0f && Scene::Current()) {
                Scene::Current()->QueueDestroy(e.id);
                return true;
            }
        }
        return false;
    };
    // Scene.health(entity) -> current, maximum. Two values, so a health bar can
    // be drawn as a fraction without a second call. An entity with no Health
    // component reports 0, 0 - which reads as "nothing to show" rather than as
    // full health, so a HUD hides the bar instead of claiming the thing is fine.
    //
    // This exists so a HUD can be written in script: the display used to reach
    // into the component from C++, which meant every HUD element needed C++.
    scn["health"] = [](Entity& e) -> std::tuple<float, float> {
        if (auto* h = e.GetComponent<HealthComponent>())
            return {h->hp, h->max};
        return {0.0f, 0.0f};
    };
}

void DescribeSceneBindings(LuaApiRegistry& api) {
    auto s = api.Table("Scene");
    s.Fn("createEntity([name]) -> entity",
         "Create an empty entity and return it IMMEDIATELY, ready to have "
         "components added. It begins updating next frame");
    s.Fn("find(name) -> entity",  "The first entity with this name, or nil");
    s.Fn("findByTag(tag) -> entity",
         "The first entity carrying this tag, or nil. How something reports on the "
         "player without being attached to it");
    s.Fn("count(tag) -> number",  "How many live entities carry a tag");
    s.Fn("nearest(tag, x, y, z, radius) -> entity",
         "The closest entity with a tag within a radius, or nil");
    s.Fn("nearestOther(entity, tag, radius) -> entity",
         "The closest OTHER entity with a tag - used to keep a squadron apart");
    s.Fn("hit(tag, x, y, z, reach) -> entity",
         "The first entity whose collider is within reach of a point");
    s.Fn("spawn(name, x, y, z, dx, dy, dz, script [, tag [, hp [, model [, vx, vy, vz]]]])",
         "Create an entity facing a direction. QUEUED until the update loop ends. "
         "vx, vy, vz is the spawner's own velocity, ADDED to whatever the new "
         "entity launches itself with - how a gun on a jet gives its rounds the "
         "jet's motion");
    s.Fn("spawnCube(name, x, y, z)", "Create a plain cube, for quick tests");
    s.Fn("destroy(entity)", "Remove an entity. Queued, so it is safe to destroy yourself");
    s.Fn("damage(entity, amount) -> bool",
         "Take hit points off. Returns true if this killed it, so a script can award score");
    s.Fn("health(entity) -> current, max",
         "Its health. An entity with no Health reports 0, 0 - which reads as nothing to show");
    s.Fn("setHitbox(entity, radius)", "Give it a sphere collider if it has none");
    s.Fn("setCollider(entity, shape, a [, b [, c]])",
         "Give it a collider: \"sphere\" a=radius, \"box\" a,b,c=half extents, "
         "\"capsule\" a=radius b=height (laid along forward)");
}

} // namespace eng
