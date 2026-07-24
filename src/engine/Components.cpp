#include "engine/Components.h"
#include "engine/FileDialog.h"   // native "open file" dialog for the Browse button
#include "engine/Scene.h"        // the full Entity/Scene definitions

#include "imgui.h"        // Dear ImGui: the immediate-mode UI used by the editor
#include "raymath.h"      // vector/quaternion/matrix math helpers

#include <algorithm>      // std::sort
#include <cmath>          // std::sqrt
#include <cstring>        // strncpy
#include <unordered_map>  // the HUD value store

namespace eng {

// The shared store of HUD values (see SetHudValue/GetHudValue). A function-
// local static so it is created the first time it's used.
static std::unordered_map<std::string, float>& HudValues() {
    static std::unordered_map<std::string, float> values;
    return values;
}
void SetHudValue(const std::string& key, float value) { HudValues()[key] = value; }
float GetHudValue(const std::string& key, float fallback) {
    auto it = HudValues().find(key);
    return it != HudValues().end() ? it->second : fallback;
}
void ClearHudValues() { HudValues().clear(); }

// A single on/off flag shared by this file. `static` at file scope means it is
// private to this .cpp (other files can't see the variable directly). The two
// functions below are the controlled way to set and read it.
static bool s_scriptInputEnabled = true;
void SetScriptInputEnabled(bool enabled) { s_scriptInputEnabled = enabled; }
static bool ScriptInputEnabled() { return s_scriptInputEnabled; }

// Translate a friendly key name that a script uses ("W", "SPACE", "UP") into
// the numeric key code raylib expects. Unknown names return KEY_NULL, so a
// typo simply reads as "not pressed" instead of causing an error.
static int KeyFromName(const std::string& name) {
    if (name.size() == 1) {                          // single character like "W" or "5"
        char c = (char)toupper((unsigned char)name[0]);   // make it upper-case
        // raylib's letter key codes are consecutive, so KEY_A + offset works.
        if (c >= 'A' && c <= 'Z') return KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return KEY_ZERO + (c - '0');
    }
    if (name == "SPACE")  return KEY_SPACE;
    if (name == "ENTER")  return KEY_ENTER;
    if (name == "SHIFT")  return KEY_LEFT_SHIFT;
    if (name == "CTRL")   return KEY_LEFT_CONTROL;
    if (name == "UP")     return KEY_UP;
    if (name == "DOWN")   return KEY_DOWN;
    if (name == "LEFT")   return KEY_LEFT;
    if (name == "RIGHT")  return KEY_RIGHT;
    return KEY_NULL;
}

// The component factory: build a component object from its type name. Called
// while loading a scene file, which stores each component by name.
std::unique_ptr<Component> MakeComponent(const std::string& name) {
    if (name == "Shape")  return std::make_unique<ShapeComponent>();
    if (name == "Script") return std::make_unique<ScriptComponent>();
    if (name == "Camera") return std::make_unique<CameraComponent>();
    if (name == "Health") return std::make_unique<HealthComponent>();
    if (name == "Hitbox") return std::make_unique<HitboxComponent>();
    if (name == "Model")  return std::make_unique<ModelComponent>();
    if (name == "Terrain") return std::make_unique<TerrainComponent>();
    return nullptr;   // unknown type: caller skips it
}

// ---- TerrainComponent ------------------------------------------------------

TerrainComponent::~TerrainComponent() {
    if (m_built) UnloadModel(m_model);
}

void TerrainComponent::Rebuild() {
    if (m_built) UnloadModel(m_model);
    m_built = false;
    m_tried = false;   // regenerate on the next draw
}

void TerrainComponent::EnsureBuilt() {
    if (m_tried) return;
    m_tried = true;
    // A grayscale Perlin-noise image: brighter pixels become higher ground.
    Image img = GenImagePerlinNoise(resolution, resolution, seed, seed, noiseScale);
    // Turn that heightmap into a 3D mesh spanning worldSize across, maxHeight tall.
    Mesh mesh = GenMeshHeightmap(img, {worldSize, maxHeight, worldSize});
    UnloadImage(img);                       // the image isn't needed once the mesh exists
    m_model = LoadModelFromMesh(mesh);       // wrap the mesh in a drawable model
    m_built = true;
}

void TerrainComponent::OnDraw(const Entity& owner) {
    EnsureBuilt();
    if (!m_built) return;
    // GenMeshHeightmap builds the terrain starting at a corner; offset by half
    // its width/depth so it's centered under this entity.
    Vector3 off = {-worldSize * 0.5f, 0.0f, -worldSize * 0.5f};
    DrawModel(m_model, off, 1.0f, tint);
    // Optional darker contour lines so the elevation is easy to read without
    // any lighting.
    if (wire) DrawModelWires(m_model, off, 1.0f, Color{0, 0, 0, 60});
}

void TerrainComponent::OnInspector() {
    ImGui::DragFloat("World size", &worldSize, 5.0f, 20.0f, 4000.0f);
    ImGui::DragFloat("Max height", &maxHeight, 0.5f, 0.0f, 500.0f);
    ImGui::DragInt("Resolution", &resolution, 1.0f, 8, 400);
    ImGui::DragFloat("Hill scale", &noiseScale, 0.1f, 0.5f, 40.0f);
    ImGui::DragInt("Seed", &seed);

    float col[4] = {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", col))
        tint = {(unsigned char)(col[0] * 255), (unsigned char)(col[1] * 255),
                (unsigned char)(col[2] * 255), (unsigned char)(col[3] * 255)};
    ImGui::Checkbox("Contour lines", &wire);

    // Regenerating the mesh is expensive, so it happens only when you ask,
    // not on every slider tweak.
    if (ImGui::Button("Regenerate")) Rebuild();
}

// ---- ModelComponent --------------------------------------------------------

ModelComponent::~ModelComponent() {
    if (m_loaded) UnloadModel(m_model);   // free the GPU resources
}

void ModelComponent::SetPath(const std::string& p) {
    if (m_loaded) UnloadModel(m_model);   // drop the old model
    path     = p;
    m_loaded = false;
    m_tried  = false;                     // load the new one on the next draw
}

void ModelComponent::EnsureLoaded() {
    if (m_tried) return;                  // only attempt the load once per path
    m_tried = true;
    if (path.empty()) return;
    m_model = LoadModel(path.c_str());
    // LoadModel returns a model with zero meshes if the file was missing or
    // invalid; treat that as "not loaded" so we don't try to draw nothing.
    m_loaded = (m_model.meshCount > 0);
    m_baseTransform = m_model.transform;   // remember the file's own transform
}

void ModelComponent::OnDraw(const Entity& owner) {
    EnsureLoaded();
    if (!m_loaded) return;
    // Scene::Draw already applied this entity's world matrix, so we draw at the
    // origin, unscaled. The rotation offset is folded into the model's own
    // transform so an oddly-authored model can be turned to face -Z; a model
    // that was already correct keeps offset {0,0,0} and is unchanged.
    Matrix offset = MatrixRotateXYZ({rotationOffset.x * DEG2RAD,
                                     rotationOffset.y * DEG2RAD,
                                     rotationOffset.z * DEG2RAD});
    m_model.transform = MatrixMultiply(m_baseTransform, offset);
    DrawModel(m_model, {0, 0, 0}, 1.0f, tint);
}

void ModelComponent::OnInspector() {
    char buf[256];
    strncpy(buf, path.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Path", buf, sizeof(buf)))
        SetPath(buf);

    if (ImGui::Button("Browse...")) {
        std::string picked = OpenFileDialog(
            "Models (*.obj *.glb *.gltf)\0*.obj;*.glb;*.gltf\0All files\0*.*\0", "obj");
        if (!picked.empty()) SetPath(picked);
    }
    ImGui::SameLine();
    if (m_loaded)              ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "loaded");
    else if (!path.empty())    ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "not found");
    else                       ImGui::TextDisabled("no model");

    // Tint color (float 0..1 in ImGui, byte 0..255 in raylib Color).
    float col[4] = {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", col))
        tint = {(unsigned char)(col[0] * 255), (unsigned char)(col[1] * 255),
                (unsigned char)(col[2] * 255), (unsigned char)(col[3] * 255)};

    // A fixed rotation to align the model with -Z forward / +Y up. Use this when
    // an imported model flies sideways or upside-down: a common fix is 90 or 180
    // on Y (turn it about the vertical) or -90 on X (for a Z-up model).
    float rot[3] = {rotationOffset.x, rotationOffset.y, rotationOffset.z};
    if (ImGui::DragFloat3("Rot offset", rot, 1.0f))
        rotationOffset = {rot[0], rot[1], rot[2]};
}

void HealthComponent::OnInspector() {
    // ImGui::DragFloat draws a number you can click-drag to change. Arguments:
    // label, pointer to the value, drag speed, minimum, maximum.
    ImGui::DragFloat("HP",  &hp,  0.1f, 0.0f, 10000.0f);
    ImGui::DragFloat("Max", &max, 0.1f, 1.0f, 10000.0f);
}

void HitboxComponent::OnInspector() {
    // The wireframe sphere in the viewport shows this radius; drag to fit it to
    // the model.
    ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 1000.0f);
}

// ---- CameraComponent -------------------------------------------------------

Camera3D CameraComponent::ToCamera3D(const Matrix& world) const {
    // `world` already includes every parent transform. We find two points in
    // world space: where the camera sits (its local origin) and a point one
    // unit ahead of it (its local -Z, which is "forward"). Vector3Transform
    // applies the matrix to a point. Feeding the eye and a forward point to
    // raylib is enough to describe the view.
    Vector3 pos = Vector3Transform({0.0f, 0.0f, 0.0f}, world);
    Vector3 tgt = Vector3Transform({0.0f, 0.0f, -1.0f}, world);

    Camera3D cam{};                         // zero-initialize all fields
    cam.position   = pos;                   // where the eye is
    cam.target     = tgt;                   // the point it looks at
    cam.up         = {0.0f, 1.0f, 0.0f};    // which way is "up" for the view
    cam.fovy       = fovy;                  // field of view (zoom)
    cam.projection = CAMERA_PERSPECTIVE;    // normal 3D perspective
    return cam;
}

void CameraComponent::OnInspector() {
    ImGui::DragFloat("FOV", &fovy, 0.5f, 10.0f, 140.0f);
    // TextDisabled draws greyed-out helper text.
    ImGui::TextDisabled("position/rotation come from Transform");
}

// ---- ScriptComponent -------------------------------------------------------

// (Re)load this component's Lua file, building a brand-new interpreter and
// re-exposing the C++ API to it. Safe to call repeatedly (that's "reload").
void ScriptComponent::Load() {
    // The m_onStart/Update/Destroy handles point INTO the current m_lua. We
    // must clear those handles before replacing m_lua, otherwise destroying
    // the old interpreter would try to clean up references that still think
    // they live in it. Assigning {} makes each handle empty.
    m_onStart   = {};
    m_onUpdate  = {};
    m_onDestroy = {};
    m_lua       = sol::state{};   // a fresh, empty Lua interpreter
    m_loaded   = false;
    m_error.clear();

    // Open only a safe subset of Lua's standard library. We deliberately do
    // NOT open `io` or `os`, so a script cannot read or delete files. This is
    // a basic sandbox.
    m_lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                         sol::lib::string);

    // --- Expose C++ types to Lua ------------------------------------------
    // new_usertype tells sol2 how a C++ type looks from Lua: which fields and
    // methods are reachable. After this, Lua code can read and write these
    // objects directly, e.g. `entity.transform.position.x = 5`.
    m_lua.new_usertype<Vector3>("Vector3",
        "x", &Vector3::x, "y", &Vector3::y, "z", &Vector3::z);

    // The rotation quaternion is exposed read-only in spirit: scripts should
    // turn things with transform:rotate (below), not by editing x/y/z/w, which
    // would break the quaternion's unit-length requirement.
    m_lua.new_usertype<Quaternion>("Quaternion",
        "x", &Quaternion::x, "y", &Quaternion::y,
        "z", &Quaternion::z, "w", &Quaternion::w);

    // The Transform type, plus several helper METHODS defined inline as
    // "lambdas" (anonymous functions written as [](args){ body }). Each takes
    // the Transform it is called on as its first argument.
    m_lua.new_usertype<Transform3D>("Transform",
        "position", &Transform3D::position,
        "rotation", &Transform3D::rotation,
        "scale",    &Transform3D::scale,

        // transform:rotate(ax, ay, az, degrees) — rotate `degrees` around the
        // local axis (ax,ay,az). It builds a small rotation quaternion and
        // multiplies it in, so calling it repeatedly (every frame) accumulates
        // cleanly without gimbal lock.
        "rotate", [](Transform3D& t, float ax, float ay, float az, float deg) {
            float len = std::sqrt(ax * ax + ay * ay + az * az);
            if (len < 1e-6f) return;                 // ignore a zero-length axis
            Quaternion dq = QuaternionFromAxisAngle(Vector3Normalize({ax, ay, az}),
                                                    deg * DEG2RAD);   // degrees -> radians
            t.rotation = QuaternionMultiply(t.rotation, dq);
        },

        // The three facing directions in WORLD space, each a unit vector.
        // "forward" is the local -Z axis rotated by the orientation; an
        // unrotated object faces -Z. Scripts use these to thrust, aim and fire.
        "forward", [](Transform3D& t) {
            return Vector3RotateByQuaternion({0.0f, 0.0f, -1.0f}, t.rotation);
        },
        "right", [](Transform3D& t) {
            return Vector3RotateByQuaternion({1.0f, 0.0f, 0.0f}, t.rotation);
        },
        "up", [](Transform3D& t) {
            return Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, t.rotation);
        },

        // translate_local(dx,dy,dz) — move by an offset given in the object's
        // OWN axes. The offset is rotated by the orientation first, so
        // translate_local(0,0,-d) always means "d units forward".
        "translate_local", [](Transform3D& t, float dx, float dy, float dz) {
            Vector3 o = Vector3RotateByQuaternion({dx, dy, dz}, t.rotation);
            t.position.x += o.x;  t.position.y += o.y;  t.position.z += o.z;
        },

        // look_at(x,y,z) — turn so the object's forward points at a world
        // point, staying upright. Handy for cameras, turrets, homing missiles.
        "look_at", [](Transform3D& t, float x, float y, float z) {
            float dx = x - t.position.x, dy = y - t.position.y, dz = z - t.position.z;
            if (dx * dx + dy * dy + dz * dz < 1e-8f) return;   // aimed at ourselves: skip
            // MatrixLookAt builds a "view" matrix (world seen from the eye).
            // Inverting it gives the eye's own orientation matrix, whose
            // rotation part is exactly the facing we want.
            Matrix view = MatrixLookAt(t.position, {x, y, z}, {0.0f, 1.0f, 0.0f});
            t.rotation = QuaternionFromMatrix(MatrixInvert(view));
        },
        // rotate_toward(x,y,z, max_degrees) — turn PART-WAY toward facing a
        // world point, by at most max_degrees this call. Unlike look_at (which
        // snaps instantly), this gives a limited turn rate, so an AI plane
        // banks toward its target and can overshoot if it can't turn fast
        // enough. Returns having rotated as far as allowed.
        "rotate_toward", [](Transform3D& t, float x, float y, float z, float maxDeg) {
            float dx = x - t.position.x, dy = y - t.position.y, dz = z - t.position.z;
            if (dx * dx + dy * dy + dz * dz < 1e-8f) return;   // target is here: skip
            // The orientation we would have if we faced the target directly.
            Matrix view = MatrixLookAt(t.position, {x, y, z}, {0.0f, 1.0f, 0.0f});
            Quaternion target = QuaternionFromMatrix(MatrixInvert(view));
            // The angle between our current orientation and that target one.
            // (For unit quaternions, the dot product's arccos, times two, is
            // the rotation angle between them.)
            float dot = t.rotation.x * target.x + t.rotation.y * target.y +
                        t.rotation.z * target.z + t.rotation.w * target.w;
            dot = std::fabs(dot);
            if (dot > 0.9995f) { t.rotation = target; return; }   // essentially aligned
            float angle  = 2.0f * std::acos(dot < 1.0f ? dot : 1.0f);   // radians
            float maxRad = maxDeg * DEG2RAD;
            // Slerp is smooth rotation interpolation; the fraction is how far of
            // the way to the target we're allowed to go this call (capped at 1).
            float frac = (angle > 0.0f) ? (maxRad / angle) : 1.0f;
            if (frac > 1.0f) frac = 1.0f;
            t.rotation = QuaternionSlerp(t.rotation, target, frac);
        });

    // Expose Entity: a script's `entity` argument can read/write these.
    m_lua.new_usertype<Entity>("Entity",
        "name",      &Entity::name,
        "tag",       &Entity::tag,
        "transform", &Entity::transform);

    // --- Expose the engine "API" as Lua global tables ---------------------
    // A Lua table works like a namespace. `input.key_down("W")` calls the
    // function stored under "key_down" in the global `input` table.

    sol::table input = m_lua.create_named_table("input");
    // Each key query is ANDed with ScriptInputEnabled(), so when the editor
    // has closed the gate (you're typing, etc.) every key reads as not pressed.
    input["key_down"]    = [](const std::string& k) { return ScriptInputEnabled() && IsKeyDown(KeyFromName(k)); };
    input["key_pressed"] = [](const std::string& k) { return ScriptInputEnabled() && IsKeyPressed(KeyFromName(k)); };

    // hud.set(name, value): publish a number for the editor's HUD to display
    // (e.g. hud.set("throttle", throttle) from the flight script).
    sol::table hud = m_lua.create_named_table("hud");
    hud["set"] = [](const std::string& k, float v) { SetHudValue(k, v); };
    // hud.get(name[, fallback]): read a published value back (0 if never set).
    // This makes the HUD store double as shared game state a script can read.
    hud["get"] = [](const std::string& k, sol::optional<float> fb) {
        return GetHudValue(k, fb.value_or(0.0f));
    };
    // hud.add(name, delta): add to a value (e.g. hud.add("score", 1)).
    hud["add"] = [](const std::string& k, float d) {
        SetHudValue(k, GetHudValue(k, 0.0f) + d);
    };

    // The `scene` table lets scripts change the world. Creating and destroying
    // entities only ENQUEUES the request; the scene carries it out after the
    // update loop, so it is safe even to destroy the very entity that asked.
    sol::table scn = m_lua.create_named_table("scene");

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
                      sol::optional<std::string> tag, sol::optional<float> hp) {
        if (!Scene::Current()) return;
        Quaternion rot = QuaternionIdentity();       // default: unrotated
        if (dx * dx + dy * dy + dz * dz > 1e-4f) {   // if a real direction was given
            Matrix view = MatrixLookAt({0, 0, 0}, {dx, dy, dz}, {0, 1, 0});
            rot = QuaternionFromMatrix(MatrixInvert(view));   // face that direction
        }
        Scene::Current()->QueueSpawn(name, {x, y, z}, rot, script,
                                     tag.value_or(std::string()), hp.value_or(0.0f));
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
    // hit(tag, x,y,z, reach): like nearest, but each candidate is treated as a
    // ball of its own hitRadius, so a shot lands anywhere inside a big model,
    // not just near its origin. This is the projectile hit test.
    scn["hit"] = [](const std::string& tag, float x, float y, float z,
                    float reach) -> Entity* {
        return Scene::Current()
                   ? Scene::Current()->FindHitWithTag(tag, {x, y, z}, reach)
                   : nullptr;
    };
    // set_hitbox(entity, radius): ensure the entity has a hitbox, using `radius`
    // as the DEFAULT only when it has none (e.g. a freshly spawned enemy). If it
    // already has a HitboxComponent -- one added and sized in the editor -- that
    // authored radius is kept, so pressing Play doesn't reset it.
    scn["set_hitbox"] = [](Entity& e, float radius) {
        if (!e.GetComponent<HitboxComponent>())
            e.AddComponent<HitboxComponent>().radius = radius;
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

    // --- Actually run the file --------------------------------------------
    // safe_script_file runs the .lua file. Using the "pass on error" form
    // means a mistake in the script is returned as an invalid result instead
    // of throwing, so a bad script shows an error rather than crashing.
    sol::protected_function_result r = m_lua.safe_script_file(path, sol::script_pass_on_error);
    if (!r.valid()) {
        m_error = r.get<sol::error>().what();   // human-readable message + line
        return;                                  // stay unloaded
    }

    // Fetch the optional lifecycle functions the script may have defined.
    // Any that the script didn't define come back empty and are simply never
    // called.
    m_onStart   = m_lua["on_start"];
    m_onUpdate  = m_lua["on_update"];
    m_onDestroy = m_lua["on_destroy"];

    // Read the optional global `properties` table: each numeric entry becomes
    // an editable field in the Inspector. We keep any value the user already
    // tuned (stored in m_props) instead of resetting it to the script default,
    // and write the effective value back into the table so the script uses it.
    std::vector<std::pair<std::string, float>> merged;
    sol::object propObj = m_lua["properties"];
    if (propObj.is<sol::table>()) {
        sol::table pt = propObj.as<sol::table>();
        for (auto& kv : pt) {
            if (kv.first.is<std::string>() && kv.second.is<double>()) {
                std::string name  = kv.first.as<std::string>();
                float       value = kv.second.as<float>();
                for (const auto& pr : m_props)      // keep a prior tuned value
                    if (pr.first == name) { value = pr.second; break; }
                pt[name] = value;                    // the script reads this back
                merged.push_back({name, value});
            }
        }
        // Sort by name so the fields keep a stable order in the Inspector
        // (a Lua table has no defined iteration order).
        std::sort(merged.begin(), merged.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    }
    m_props = std::move(merged);

    m_loaded = true;
}

// Call one Lua hook safely. If it isn't loaded or doesn't exist, do nothing.
// If calling it errors, record the message and mark the script unloaded so the
// broken hook isn't called again every frame. This is a template so it accepts
// any set of arguments to forward to the Lua function.
template <typename... Args>
static void CallHook(sol::protected_function& fn, bool& loaded,
                     std::string& error, Args&&... args) {
    if (!loaded || !fn.valid()) return;
    sol::protected_function_result r = fn(std::forward<Args>(args)...);
    if (!r.valid()) {
        error  = r.get<sol::error>().what();
        loaded = false;
    }
}

void ScriptComponent::OnStart(Entity& owner) {
    Load();   // (re)load the file first, then run its on_start
    // `owner` is passed by reference, so the script edits the real entity.
    CallHook(m_onStart, m_loaded, m_error, owner);
}

void ScriptComponent::OnUpdate(float dt, Entity& owner) {
    CallHook(m_onUpdate, m_loaded, m_error, owner, dt);
}

void ScriptComponent::OnDestroy(Entity& owner) {
    CallHook(m_onDestroy, m_loaded, m_error, owner);
}

void ScriptComponent::OnInspector() {
    // Load the script the first time it's shown (unless it already failed), so
    // its properties appear without pressing a button. This only runs the
    // script's top level, not the per-frame hooks.
    if (!m_loaded && m_error.empty() && !path.empty())
        Load();

    // ImGui edits text through a fixed char buffer, not a std::string, so we
    // copy the path into a buffer, let the user edit it, then copy back.
    char buf[256];
    strncpy(buf, path.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';                 // ensure it ends with a 0 byte
    if (ImGui::InputText("Path", buf, sizeof(buf)))
        path = buf;

    // A button that opens the native file picker filtered to .lua files.
    if (ImGui::Button("Browse...")) {
        std::string picked = OpenFileDialog(
            "Lua scripts (*.lua)\0*.lua\0All files\0*.*\0", "lua");
        if (!picked.empty()) {        // empty string = the user cancelled
            path = picked;
            Load();                   // choosing a file also loads it
        }
    }
    ImGui::SameLine();               // keep the next widget on the same row
    if (ImGui::Button("Load / Reload")) Load();

    // A colored status word next to the buttons.
    ImGui::SameLine();
    if (m_loaded)              ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "loaded");
    else if (!m_error.empty()) ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "error");
    else                       ImGui::TextDisabled("not loaded");

    // If there was an error, print the full message, wrapped to the panel.
    if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());

    // The script's exposed properties, as editable fields. Editing one writes
    // the new value straight into the running script's `properties` table, so
    // changes take effect immediately (including mid-play).
    if (m_loaded && !m_props.empty()) {
        ImGui::SeparatorText("Properties");
        for (auto& pr : m_props) {
            if (ImGui::DragFloat(pr.first.c_str(), &pr.second, 0.05f))
                m_lua["properties"][pr.first] = pr.second;
        }
    }
}

// ---- ShapeComponent --------------------------------------------------------

void ShapeComponent::OnDraw(const Entity& owner) {
    // Before calling this, Scene::Draw pushed this entity's world matrix onto
    // raylib's matrix stack. That matrix already encodes position, rotation
    // and scale (including parents'), so here we simply draw a unit-sized
    // primitive centered at the origin and it appears in the right place.
    switch (kind) {
        case Kind::Cube:
            DrawCube({0, 0, 0}, 1.0f, 1.0f, 1.0f, tint);
            if (wireframe) DrawCubeWires({0, 0, 0}, 1.0f, 1.0f, 1.0f, BLACK);
            break;
        case Kind::Sphere:
            DrawSphere({0, 0, 0}, 0.5f, tint);
            if (wireframe) DrawSphereWires({0, 0, 0}, 0.5f, 12, 12, BLACK);
            break;
        case Kind::Cylinder:   // raylib builds cylinders upward from the base,
                               // so we offset down by half to center it
            DrawCylinder({0, -0.5f, 0}, 0.5f, 0.5f, 1.0f, 16, tint);
            if (wireframe) DrawCylinderWires({0, -0.5f, 0}, 0.5f, 0.5f, 1.0f, 16, BLACK);
            break;
        case Kind::Cone:       // a cone is a cylinder whose top radius is 0
            DrawCylinder({0, -0.5f, 0}, 0.0f, 0.5f, 1.0f, 16, tint);
            if (wireframe) DrawCylinderWires({0, -0.5f, 0}, 0.0f, 0.5f, 1.0f, 16, BLACK);
            break;
        case Kind::Plane:      // a flat square; raylib has no wireframe plane
            DrawPlane({0, 0, 0}, {1.0f, 1.0f}, tint);
            break;
    }
}

void ShapeComponent::OnInspector() {
    // A dropdown to pick the shape. The names array order must match the Kind
    // enum order, because the dropdown works with the integer index.
    static const char* kKindNames[] = {"Cube", "Sphere", "Cylinder", "Cone", "Plane"};
    int k = (int)kind;
    // Labeled "Type", not "Shape", so its ImGui id doesn't collide with the
    // component's "Shape" header just above it.
    if (ImGui::Combo("Type", &k, kKindNames, 5))
        kind = (Kind)k;

    // ImGui color pickers use four floats in the 0..1 range; raylib's Color
    // uses four bytes in 0..255. Convert one way in, the other way out.
    float col[4] = {tint.r / 255.0f, tint.g / 255.0f,
                    tint.b / 255.0f, tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", col)) {
        tint = {(unsigned char)(col[0] * 255), (unsigned char)(col[1] * 255),
                (unsigned char)(col[2] * 255), (unsigned char)(col[3] * 255)};
    }
    ImGui::Checkbox("Wireframe", &wireframe);
}

} // namespace eng
