#include "engine/Components.h"
#include "engine/FileDialog.h"   // native "open file" dialog for the Browse button
#include "engine/Audio.h"        // sound playback, exposed to scripts as audio.*
#include "engine/Lighting.h"     // the shared lighting shader, applied to materials
#include "engine/Particles.h"    // visual effect bursts, exposed to scripts as fx.*
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
    if (name == "Graph")  return std::make_unique<GraphComponent>();
    if (name == "Camera") return std::make_unique<CameraComponent>();
    if (name == "Health") return std::make_unique<HealthComponent>();
    if (name == "Collider") return std::make_unique<ColliderComponent>();
    // "Hitbox" is the name an older scene format used for a sphere-only
    // collision volume. A Collider set to Sphere is exactly that shape, and its
    // Deserialize reads the same "radius" key, so old files keep working; the
    // next save rewrites them under the new name.
    if (name == "Hitbox") return std::make_unique<ColliderComponent>();
    if (name == "RigidBody") return std::make_unique<RigidBodyComponent>();
    if (name == "Light")  return std::make_unique<LightComponent>();
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
    ApplyLightingShader(m_model);            // shade the hills instead of flat green
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
    ImGui::DragInt("Resolution", &resolution, 1.0f, 8, 1000);
    ImGui::DragFloat("Hill scale", &noiseScale, 0.1f, 0.5f, 40.0f);
    ImGui::DragInt("Seed", &seed);

    // Show what the current resolution actually costs. Terrain is usually the
    // heaviest thing in a scene, and the cost is not obvious from the number:
    // resolution 500 is not "a bit more" than 250, it is four times as much.
    // Worse, the mesh is not indexed - every triangle carries its own three
    // vertices - so the vertex count is three times the triangle count.
    int tris = TriangleCount();
    if (tris >= 200000)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f),
                           "%d triangles - very heavy", tris);
    else if (tris >= 60000)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                           "%d triangles", tris);
    else
        ImGui::TextDisabled("%d triangles", tris);

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
    ApplyLightingShader(m_model);          // shade it like everything else
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

int ModelComponent::TriangleCount() const {
    if (!m_loaded) return 0;
    int total = 0;
    for (int i = 0; i < m_model.meshCount; i++)
        total += m_model.meshes[i].triangleCount;
    return total;
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

void LightComponent::OnInspector() {
    // ColorEdit3 shows a colour swatch that opens a picker. It works on three
    // floats in 0..1 order R, G, B - the same layout as a Vector3 - so the
    // address of the first field can be handed to it directly.
    ImGui::ColorEdit3("Color", &color.x);
    // Brightness is separate from hue so the sun can be dimmed for a dusk look
    // without turning it a different colour.
    ImGui::DragFloat("Intensity", &intensity, 0.02f, 0.0f, 8.0f);
    ImGui::ColorEdit3("Ambient", &ambient.x);
    ImGui::ColorEdit3("Sky Fill", &sky.x);
    ImGui::ColorEdit3("Ground Fill", &ground.x);
    // The direction is not edited here: it comes from the entity's rotation, so
    // the light is aimed with the same Rotation fields as any other object.
    ImGui::TextDisabled("Direction = the entity's forward axis");
    ImGui::TextDisabled("Rotate the entity to aim the sun.");
}

Matrix ColliderComponent::LocalMatrix() const {
    // Degrees are friendly to type in the Inspector; the maths needs radians.
    Matrix rot = MatrixRotateXYZ({rotation.x * DEG2RAD,
                                  rotation.y * DEG2RAD,
                                  rotation.z * DEG2RAD});
    Matrix move = MatrixTranslate(offset.x, offset.y, offset.z);
    // MatrixMultiply(A, B) means "apply A, then B" for the way raylib
    // transforms points. Rotating first and moving second spins the shape
    // about its OWN centre and then puts it in place; doing it the other way
    // round would swing the shape around the entity's origin instead.
    return MatrixMultiply(rot, move);
}

void ColliderComponent::OnInspector() {
    // The order of these strings must match the ColliderShape enum, because
    // ImGui::Combo works on the INDEX of the selected item.
    static const char* kShapeNames[] = { "Sphere", "Box", "Capsule" };

    // ImGui::Combo wants an int it can write into, so convert to and from the
    // enum around the call.
    int current = static_cast<int>(shape);
    if (ImGui::Combo("Shape", &current, kShapeNames, IM_ARRAYSIZE(kShapeNames)))
        shape = static_cast<ColliderShape>(current);

    // Only show the fields the chosen shape actually uses, so the panel never
    // presents a number that does nothing.
    switch (shape) {
        case ColliderShape::Sphere:
            ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 1000.0f);
            break;
        case ColliderShape::Box:
            // DragFloat3 edits three floats at once; &halfExtents.x points at
            // the first of the three, which sit next to each other in memory.
            ImGui::DragFloat3("Half Extents", &halfExtents.x, 0.05f, 0.0f, 1000.0f);
            break;
        case ColliderShape::Capsule:
            ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 1000.0f);
            // The straight part between the two round caps; the pill's total
            // length is this plus twice the radius.
            ImGui::DragFloat("Height", &height, 0.05f, 0.0f, 1000.0f);
            break;
    }

    // Applies to every shape: shifts the volume away from the entity's origin.
    ImGui::DragFloat3("Offset", &offset.x, 0.05f, -1000.0f, 1000.0f);

    // A sphere is the same shape whichever way you turn it, so only offer the
    // rotation where it can actually change something.
    if (shape != ColliderShape::Sphere) {
        // Drag speed 1.0 = one degree per pixel dragged, matching the other
        // rotation fields in the editor.
        ImGui::DragFloat3("Rotation", &rotation.x, 1.0f);
        // A capsule stands along its own Y axis. Aircraft in this engine face
        // local -Z, so the common case of a nose-to-tail capsule is X = 90.
        if (shape == ColliderShape::Capsule && ImGui::Button("Lay Along Forward"))
            rotation = {90.0f, 0.0f, 0.0f};
    }
}

// ---- RigidBodyComponent ----------------------------------------------------

void RigidBodyComponent::OnInspector() {
    // The order of these strings must match the MotionType enum, because
    // ImGui::Combo works on the INDEX of the selected item.
    static const char* kMotionNames[] = { "Static", "Kinematic", "Dynamic" };

    int current = static_cast<int>(motion);
    if (ImGui::Combo("Motion", &current, kMotionNames, IM_ARRAYSIZE(kMotionNames)))
        motion = static_cast<MotionType>(current);

    // A one-line reminder of what the chosen type means, because picking the
    // wrong one produces behaviour that looks like a bug rather than a setting
    // (a "static" aircraft simply refuses to move, with no error anywhere).
    switch (motion) {
        case MotionType::Static:
            ImGui::TextDisabled("Never moves. For ground and scenery.");
            break;
        case MotionType::Kinematic:
            ImGui::TextDisabled("Moved by scripts; pushes dynamic bodies.");
            break;
        case MotionType::Dynamic:
            ImGui::TextDisabled("Moved by gravity, forces and collisions.");
            break;
    }

    // Everything below this point only affects a body the simulation actually
    // integrates, so hide it for a static one rather than offering numbers
    // that do nothing.
    if (motion == MotionType::Static) return;

    // Mass must stay above zero: dividing a force by zero mass is an infinite
    // acceleration, which turns the body's position into "not a number" and
    // then quietly corrupts everything it touches.
    ImGui::DragFloat("Mass (kg)", &mass, 0.5f, 0.001f, 100000.0f);

    ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Restitution", &restitution, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Gravity Factor", &gravityFactor, 0.05f, -5.0f, 5.0f);
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
    // Only clear a previous error when there is actually something to run. A
    // component with neither source nor path has usually just failed to produce
    // any (a graph that would not compile), and wiping the reason would leave
    // the Inspector saying nothing at all.
    if (!source.empty() || !path.empty()) m_error.clear();
    if (source.empty() && path.empty()) {
        if (m_error.empty()) m_error = "Nothing to run: no script file and no source.";
        return;
    }

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

    // The `fx` table fires visual effects. These are pure decoration: they
    // never touch the world, so a script can call one from anywhere without
    // worrying about what it might disturb.
    sol::table fx = m_lua.create_named_table("fx");
    // fx.burst(preset, x, y, z [, scale [, vx, vy, vz]]): throw a burst of
    // particles at a point. `preset` is any effect named in
    // assets/scripts/effects.lua; `scale` sizes the whole effect and defaults
    // to 1.
    //
    // vx,vy,vz are the velocity of whatever fired the effect, in world units
    // per second. Pass them when the emitter is moving: without them the burst
    // stays where it was born while the emitter flies on, so a muzzle flash
    // trails visibly behind a fast jet. A script can measure its own velocity
    // by remembering its position from the previous frame - see gun.lua.
    fx["burst"] = [](const std::string& preset, float x, float y, float z,
                     sol::optional<float> scale,
                     sol::optional<float> vx, sol::optional<float> vy,
                     sol::optional<float> vz) {
        BurstNamed(preset.c_str(), {x, y, z}, scale.value_or(1.0f),
                   {vx.value_or(0.0f), vy.value_or(0.0f), vz.value_or(0.0f)});
    };

    // The `audio` table plays sounds. Like fx, these are pure output: they never
    // touch the world, so they are safe to call from anywhere.
    sol::table au = m_lua.create_named_table("audio");
    // audio.play(name [, volume [, pitch]]): fire a one-shot sound. The names
    // come from assets/scripts/sounds.lua. `volume` scales the level set there;
    // `pitch` defaults to the definition's random range, which is what keeps
    // repeated gunfire from sounding like one stuttering sample.
    au["play"] = [](const std::string& name, sol::optional<float> volume,
                    sol::optional<float> pitch) {
        PlaySoundNamed(name.c_str(), volume.value_or(1.0f), pitch.value_or(0.0f));
    };
    // Looping sounds, for anything continuous like an engine note. Starting one
    // that is already running does nothing, so a script may call loop_start
    // every frame without stacking up copies.
    au["loop_start"] = [](const std::string& name) { LoopStart(name.c_str()); };
    au["loop_stop"]  = [](const std::string& name) { LoopStop(name.c_str()); };
    // audio.loop_set(name, volume, pitch): change a running loop. Called every
    // frame to tie an engine note to the throttle.
    au["loop_set"] = [](const std::string& name, float volume, float pitch) {
        LoopSet(name.c_str(), volume, pitch);
    };

    // The `light` table lets scripts change the sun while the game runs: dimming
    // it towards dusk, flashing it red when the player is hit, and so on.
    //
    // Every one of these writes into the scene's first Light COMPONENT rather
    // than into the shader. That is deliberate. The editor pushes the light
    // component to the shader at the start of every frame, so a value written
    // straight to the shader would be wiped before anything was drawn with it.
    // Writing to the component also means the Inspector keeps showing the truth.
    //
    // The sun's DIRECTION has no function here because it needs none: the light
    // travels along its entity's forward axis, so a script aims it by rotating
    // that entity like any other object.
    sol::table lt = m_lua.create_named_table("light");

    // Find the scene's light, or nullptr when the scene has none. Every
    // function below goes through this, so a scene without a light simply does
    // nothing instead of failing.
    auto findLight = []() -> LightComponent* {
        Scene* s = Scene::Current();
        if (!s) return nullptr;
        for (Entity& e : s->Entities())
            if (auto* l = e.GetComponent<LightComponent>()) return l;
        return nullptr;
    };

    // light.set_color(r, g, b): channels are 0..255, matching the numbers the
    // Inspector's colour picker shows, and are converted to the 0..1 the shader
    // works in.
    lt["set_color"] = [findLight](float r, float g, float b) {
        if (auto* l = findLight())
            l->color = {r / 255.0f, g / 255.0f, b / 255.0f};
    };
    // light.set_intensity(v): brightness, separate from colour. 1 is normal,
    // 0 is night, above 1 is brighter than the surface's own colour.
    lt["set_intensity"] = [findLight](float v) {
        if (auto* l = findLight()) l->intensity = v;
    };
    // light.set_ambient(r, g, b): the light reaching surfaces the sun cannot
    // see. Lower it for harsh, high-contrast shadows; raise it for a flat,
    // overcast look.
    lt["set_ambient"] = [findLight](float r, float g, float b) {
        if (auto* l = findLight())
            l->ambient = {r / 255.0f, g / 255.0f, b / 255.0f};
    };
    // light.get_intensity(): read it back, so a script can fade from wherever
    // the light currently is rather than from a number it assumed.
    lt["get_intensity"] = [findLight]() -> float {
        auto* l = findLight();
        return l ? l->intensity : 0.0f;
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

    // --- Actually run the script -------------------------------------------
    // Either from a string in memory or from a file on disk. The "pass on
    // error" form means a mistake in the script comes back as an invalid result
    // instead of throwing, so a bad script shows an error rather than crashing.
    //
    // The source path exists for generated code - a node graph compiled at Play
    // - which has no file and should not have one.
    sol::protected_function_result r =
        source.empty() ? m_lua.safe_script_file(path, sol::script_pass_on_error)
                       : m_lua.safe_script(source, sol::script_pass_on_error);
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
    std::vector<ScriptProp> merged;
    sol::object propObj = m_lua["properties"];
    if (propObj.is<sol::table>()) {
        sol::table pt = propObj.as<sol::table>();
        for (auto& kv : pt) {
            if (kv.first.is<std::string>() && kv.second.is<double>()) {
                std::string name  = kv.first.as<std::string>();
                float       value = kv.second.as<float>();   // the script's default
                bool        over  = false;

                // An OVERRIDDEN property keeps the value this entity was given.
                // Anything else takes the script's current default, so editing
                // the .lua actually shows up the next time the script loads.
                for (const auto& pr : m_props) {
                    if (pr.name == name && pr.overridden) {
                        value = pr.value;
                        over  = true;
                        break;
                    }
                }

                pt[name] = value;                    // the script reads this back
                merged.push_back({name, value, over});
            }
        }
        // Sort by name so the fields keep a stable order in the Inspector
        // (a Lua table has no defined iteration order).
        std::sort(merged.begin(), merged.end(),
                  [](const ScriptProp& a, const ScriptProp& b) { return a.name < b.name; });
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

    DrawPropertiesInspector();
}

// The script's exposed properties, as editable fields. Editing one writes the
// new value straight into the running script's `properties` table, so changes
// take effect immediately, including mid-play.
//
// This lives on its own so that any component backed by a Lua script presents
// its tunables the same way, whether the script came from a file or was
// generated from a node graph.
void ScriptComponent::DrawPropertiesInspector() {
    if (m_loaded && !m_props.empty()) {
        ImGui::SeparatorText("Properties");
        // Any property this entity has overridden shows a revert arrow and an
        // orange name. Without that mark there is no way to tell a value that
        // came from the script from one this entity is holding on to - which is
        // exactly the confusion of editing a .lua and seeing nothing change.
        bool anyOverride = false;

        for (int i = 0; i < (int)m_props.size(); i++) {
            ScriptProp& pr = m_props[i];
            // Two widgets share a row and would collide as ImGui identities,
            // since ImGui names widgets by their label. PushID makes this row's
            // widgets unique regardless of the property's name.
            ImGui::PushID(i);

            // The revert button, drawn first so the numbers still line up.
            if (pr.overridden) {
                anyOverride = true;
                if (ImGui::SmallButton("<")) {
                    // Drop the override and take the script's value again. The
                    // script is reloaded so the default is re-read from the
                    // file rather than remembered from before.
                    pr.overridden = false;
                    Load();
                    ImGui::PopID();
                    break;         // Load() rebuilt m_props; stop walking it
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Revert to the value in the script file");
                ImGui::SameLine();
            }

            if (ImGui::DragFloat(pr.name.c_str(), &pr.value, 0.05f)) {
                // Editing a field is what makes it an override: from now on
                // this entity keeps its own value and ignores the script's.
                pr.overridden = true;
                m_lua["properties"][pr.name] = pr.value;
            }
            // Colour the overridden ones so the difference is visible at a
            // glance, the way a changed setting is marked anywhere else.
            if (pr.overridden) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "*");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Overridden here; the script file is ignored\n"
                                      "for this value until you revert it.");
            }
            ImGui::PopID();
        }

        if (anyOverride && ImGui::SmallButton("Revert all to script")) {
            for (auto& pr : m_props) pr.overridden = false;
            Load();
        }
    }
}

// ---- GraphComponent --------------------------------------------------------

// Whoever hosts the engine registers the graph code generator here. It stays
// null in a host that has no node editor, which GraphComponent reports plainly
// rather than failing in some confusing way.
static GraphCompiler s_graphCompiler = nullptr;

void SetGraphCompiler(GraphCompiler fn) { s_graphCompiler = fn; }
bool HasGraphCompiler() { return s_graphCompiler != nullptr; }

bool GraphComponent::CompileSource() {
    source.clear();
    if (graphPath.empty()) {
        m_error = "No graph chosen. Use New Graph or Open Graph.";
        return false;
    }
    if (!s_graphCompiler) {
        m_error = "No graph compiler available in this program.";
        return false;
    }
    std::string lua, err;
    if (!s_graphCompiler(graphPath, lua, err)) {
        m_error = err.empty() ? ("Could not compile " + graphPath) : err;
        return false;
    }
    source = lua;
    return true;
}

void GraphComponent::OnStart(Entity& owner) {
    // Compile fresh every time play begins, so the graph on disk is always what
    // runs. On failure the base class reports it through the same error field an
    // ordinary script would use.
    CompileSource();
    ScriptComponent::OnStart(owner);
}

bool GraphComponent::Recompile() {
    if (!CompileSource()) return false;
    Load();
    return m_loaded;
}

void GraphComponent::OnInspector() {
    // Which graph this entity runs. Editable as text so a path can be pasted,
    // like the Script component's path field.
    //
    // The label must NOT be "Graph". ImGui identifies a widget by its label
    // within the current scope, and the Inspector draws each component inside
    // one scope whose collapsing header is already labelled with the component's
    // name - "Graph" here. Reusing that string makes two widgets with the same
    // identity, which ImGui reports as a conflicting ID.
    char buf[256];
    strncpy(buf, graphPath.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Graph file", buf, sizeof(buf))) graphPath = buf;

    if (ImGui::Button("Open Graph...")) {
        std::string picked = OpenFileDialog(
            "Node graphs (*.json)\0*.json\0All files\0*.*\0", "json");
        if (!picked.empty()) graphPath = picked;
    }
    ImGui::SameLine();
    // Creating a graph and opening one for editing both need the node editor,
    // which this component cannot reach. It records the request; the editor
    // notices it and acts.
    if (ImGui::Button("New Graph")) newRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Edit in Node Editor")) editRequested = true;

    ImGui::SameLine();
    if (ImGui::Button("Recompile")) Recompile();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Compile the graph and reload it now, without leaving\n"
                          "play mode. Play always compiles fresh anyway.");

    // Status, in the same words the Script component uses.
    if (m_loaded)              ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "compiled");
    else if (!m_error.empty()) ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "error");
    else                       ImGui::TextDisabled("not compiled yet");
    if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());

    ImGui::TextDisabled("Compiled to Lua in memory on Play; no file is written.");

    // The graph's Param nodes become properties, shown exactly as a script's are.
    DrawPropertiesInspector();
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
