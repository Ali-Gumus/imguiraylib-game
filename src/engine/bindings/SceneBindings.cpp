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
    sol::table scn = lua.create_named_table("scene");

    scn["destroy"] = [](Entity& e) {
        if (Scene::Current()) Scene::Current()->QueueDestroy(e.id);
    };
    scn["spawn_cube"] = [](const std::string& name, float x, float y, float z) {
        if (Scene::Current()) Scene::Current()->QueueSpawnCube(name, {x, y, z});
    };
    // Find another entity by name, or nil. Call it fresh each frame; never
    // store the result, because the entity it points to may be destroyed.
    scn["find"] = [](const std::string& name) -> Entity* {
        return Scene::Current() ? Scene::Current()->FindByName(name) : nullptr;
    };
    // spawn(name, x,y,z, dx,dy,dz, script): create an entity at a position,
    // oriented so its forward faces the direction (dx,dy,dz), running `script`.
    // Firing a bullet spawns it facing the shot direction.
    // The last two arguments are optional: a tag (e.g. "enemy") and starting
    // health. Bullets omit them; a wave spawner passes them so the new enemy is
    // tagged and killable.
    scn["spawn"] = [](const std::string& name, float x, float y, float z,
                      float dx, float dy, float dz, const std::string& script,
                      sol::optional<std::string> tag, sol::optional<float> hp,
                      sol::optional<std::string> model) {
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
                                     model.value_or(std::string()));
    };
    // count(tag): how many live entities carry `tag`. A wave is cleared when
    // scene.count("enemy") reaches zero.
    scn["count"] = [](const std::string& tag) -> int {
        return Scene::Current() ? Scene::Current()->CountWithTag(tag) : 0;
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
    // set_hitbox(entity, radius): ensure the entity is hittable, giving it a
    // SPHERE collider of `radius` only when it has no collider at all (e.g. a
    // freshly spawned enemy). If one already exists -- added and sized in the
    // editor -- it is left alone, so pressing Play never resets authored values.
    scn["set_hitbox"] = [](Entity& e, float radius) {
        if (!e.GetComponent<ColliderComponent>()) {
            ColliderComponent& c = e.AddComponent<ColliderComponent>();
            c.shape  = ColliderShape::Sphere;
            c.radius = radius;
        }
    };
    // set_collider(entity, shape, a, b, c): the full version of set_hitbox for
    // shapes other than a sphere. `shape` is "sphere", "box" or "capsule"; the
    // three numbers mean different things per shape:
    //   sphere  -> a = radius              (b, c unused)
    //   box     -> a, b, c = half extents  (half the size on X, Y, Z)
    //   capsule -> a = radius, b = height  (c unused)
    // Like set_hitbox it only ADDS: an authored collider is never overwritten.
    // `b` and `c` are sol::optional, meaning the script may leave them out:
    // scene.set_collider(e, "sphere", 2) is valid.
    scn["set_collider"] = [](Entity& e, const std::string& shape, float a,
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
            col.shape  = ColliderShape::Capsule;
            col.radius = a;
            col.height = bv;
        } else {                       // anything else is treated as a sphere
            col.shape  = ColliderShape::Sphere;
            col.radius = a;
        }
    };
    // nearest_other(self, tag, radius): like nearest, but searches from the
    // `self` entity's position and never returns `self`. Used so a group of
    // same-tag agents (e.g. enemies) can steer apart instead of overlapping.
    scn["nearest_other"] = [](Entity& self, const std::string& tag, float radius) -> Entity* {
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
    // scene.health(entity) -> current, maximum. Two values, so a health bar can
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
    auto s = api.Table("scene");
    s.Fn("find(name) -> entity",  "The first entity with this name, or nil");
    s.Fn("count(tag) -> number",  "How many live entities carry a tag");
    s.Fn("nearest(tag, x, y, z, radius) -> entity",
         "The closest entity with a tag within a radius, or nil");
    s.Fn("nearest_other(entity, tag, radius) -> entity",
         "The closest OTHER entity with a tag - used to keep a squadron apart");
    s.Fn("hit(tag, x, y, z, reach) -> entity",
         "The first entity whose collider is within reach of a point");
    s.Fn("spawn(name, x, y, z, dx, dy, dz, script [, tag [, hp [, model]]])",
         "Create an entity facing a direction. QUEUED until the update loop ends");
    s.Fn("spawn_cube(name, x, y, z)", "Create a plain cube, for quick tests");
    s.Fn("destroy(entity)", "Remove an entity. Queued, so it is safe to destroy yourself");
    s.Fn("damage(entity, amount) -> bool",
         "Take hit points off. Returns true if this killed it, so a script can award score");
    s.Fn("health(entity) -> current, max",
         "Its health. An entity with no Health reports 0, 0 - which reads as nothing to show");
    s.Fn("set_hitbox(entity, radius)", "Give it a sphere collider if it has none");
    s.Fn("set_collider(entity, shape, a [, b [, c]])",
         "Give it a collider: \"sphere\", \"box\" or \"capsule\"");
}

} // namespace eng
