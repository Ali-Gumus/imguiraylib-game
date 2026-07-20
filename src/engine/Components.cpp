#include "engine/Components.h"
#include "engine/FileDialog.h"
#include "engine/Scene.h"     // full Entity definition (Component.h only forward-declares)

#include "imgui.h"
#include "raymath.h"          // Vector3Transform for the camera

#include <cmath>
#include <cstring>

namespace eng {

static bool s_scriptInputEnabled = true;
void SetScriptInputEnabled(bool enabled) { s_scriptInputEnabled = enabled; }
static bool ScriptInputEnabled() { return s_scriptInputEnabled; }

// Map a friendly key name ("W", "SPACE", "UP") to a raylib key code.
// Returns 0 (KEY_NULL) for names we don't know — key_down then just
// returns false instead of erroring, which is the kind thing at runtime.
static int KeyFromName(const std::string& name) {
    if (name.size() == 1) {
        char c = (char)toupper((unsigned char)name[0]);
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

std::unique_ptr<Component> MakeComponent(const std::string& name) {
    // An if-chain is honest at two types. When this grows annoying, it
    // becomes a static map<string, factory-lambda> that components
    // register themselves into — same idea, more machinery.
    if (name == "Shape")  return std::make_unique<ShapeComponent>();
    if (name == "Script") return std::make_unique<ScriptComponent>();
    if (name == "Camera") return std::make_unique<CameraComponent>();
    return nullptr;
}

// ---- CameraComponent -------------------------------------------------

Camera3D CameraComponent::ToCamera3D(const Matrix& world) const {
    // The world matrix (parents included) places the camera: transform
    // the local origin -> eye position, and a point one unit down the
    // local -Z -> what it looks at. Parenting the camera "just works".
    Vector3 pos = Vector3Transform({0.0f, 0.0f, 0.0f}, world);
    Vector3 tgt = Vector3Transform({0.0f, 0.0f, -1.0f}, world);

    Camera3D cam{};
    cam.position   = pos;
    cam.target     = tgt;
    cam.up         = {0.0f, 1.0f, 0.0f};
    cam.fovy       = fovy;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

void CameraComponent::OnInspector() {
    ImGui::DragFloat("FOV", &fovy, 0.5f, 10.0f, 140.0f);
    ImGui::TextDisabled("position/rotation come from Transform");
}

// ---- ScriptComponent -------------------------------------------------

void ScriptComponent::Load() {
    // Order matters: the hooks are references INTO m_lua. Release them
    // BEFORE destroying the state they point at, or the release walks
    // freed memory (access violation deep inside Lua).
    m_onStart   = {};
    m_onUpdate  = {};
    m_onDestroy = {};
    m_lua       = sol::state{};       // fresh state: reload = clean slate
    m_loaded   = false;
    m_error.clear();

    // Which Lua standard libraries scripts may use. Deliberately small:
    // no io/os = scripts can't delete files. (Sandboxing, day one.)
    m_lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                         sol::lib::string);

    // --- Expose our C++ types to Lua (this is sol2's whole job) -------
    // After this, Lua can do: entity.transform.position.x = 5
    m_lua.new_usertype<Vector3>("Vector3",
        "x", &Vector3::x, "y", &Vector3::y, "z", &Vector3::z);
    m_lua.new_usertype<Transform3D>("Transform",
        "position", &Transform3D::position,
        "rotation", &Transform3D::rotation,
        "scale",    &Transform3D::scale);
    m_lua.new_usertype<Entity>("Entity",
        "name",      &Entity::name,
        "transform", &Entity::transform);

    // The engine API scripts get, grouped in tables (like Unity's Input
    // class). Grows over time: audio, spawning, physics queries...
    // Every query respects the editor-controlled gate (see
    // SetScriptInputEnabled) — typing in the UI must not drive the game.
    sol::table input = m_lua.create_named_table("input");
    input["key_down"]    = [](const std::string& k) { return ScriptInputEnabled() && IsKeyDown(KeyFromName(k)); };
    input["key_pressed"] = [](const std::string& k) { return ScriptInputEnabled() && IsKeyPressed(KeyFromName(k)); };

    // Run the file. protected = collect the error, don't throw/crash.
    sol::protected_function_result r = m_lua.safe_script_file(path, sol::script_pass_on_error);
    if (!r.valid()) {
        m_error = r.get<sol::error>().what();   // e.g. syntax error with line number
        return;
    }

    // Grab whatever lifecycle hooks the script defined — each one is
    // optional (a script with only on_start is perfectly legal).
    m_onStart   = m_lua["on_start"];
    m_onUpdate  = m_lua["on_update"];
    m_onDestroy = m_lua["on_destroy"];
    m_loaded    = true;
}

// Shared safe-call: run a hook if it exists; on error, record it and
// disable the script (so a broken hook doesn't spam every frame).
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
    Load();
    // The entity crosses the language border BY REFERENCE — Lua edits
    // the real C++ object, no copies.
    CallHook(m_onStart, m_loaded, m_error, owner);
}

void ScriptComponent::OnUpdate(float dt, Entity& owner) {
    CallHook(m_onUpdate, m_loaded, m_error, owner, dt);
}

void ScriptComponent::OnDestroy(Entity& owner) {
    CallHook(m_onDestroy, m_loaded, m_error, owner);
}

void ScriptComponent::OnInspector() {
    char buf[256];
    strncpy(buf, path.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Path", buf, sizeof(buf)))
        path = buf;

    if (ImGui::Button("Browse...")) {
        std::string picked = OpenFileDialog(
            "Lua scripts (*.lua)\0*.lua\0All files\0*.*\0", "lua");
        if (!picked.empty()) {        // empty = user cancelled: change nothing
            path = picked;
            Load();                   // picking a file implies "use it now"
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load / Reload")) Load();

    ImGui::SameLine();
    if (m_loaded)             ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "loaded");
    else if (!m_error.empty()) ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "error");
    else                       ImGui::TextDisabled("not loaded");

    // Full error text (word-wrapped) so you can read the Lua stack trace.
    if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
}

// ---- ShapeComponent --------------------------------------------------

void ShapeComponent::OnDraw(const Entity& owner) {
    // Scene::Draw has already pushed this entity's WORLD matrix (with
    // all parent transforms multiplied in) — so we just draw unit-sized
    // primitives at the origin. Components stay hierarchy-oblivious.
    switch (kind) {
        case Kind::Cube:
            DrawCube({0, 0, 0}, 1.0f, 1.0f, 1.0f, tint);
            if (wireframe) DrawCubeWires({0, 0, 0}, 1.0f, 1.0f, 1.0f, BLACK);
            break;
        case Kind::Sphere:
            DrawSphere({0, 0, 0}, 0.5f, tint);
            if (wireframe) DrawSphereWires({0, 0, 0}, 0.5f, 12, 12, BLACK);
            break;
        case Kind::Cylinder:   // raylib grows cylinders upward from base
            DrawCylinder({0, -0.5f, 0}, 0.5f, 0.5f, 1.0f, 16, tint);
            if (wireframe) DrawCylinderWires({0, -0.5f, 0}, 0.5f, 0.5f, 1.0f, 16, BLACK);
            break;
        case Kind::Cone:       // a cylinder whose top radius is zero
            DrawCylinder({0, -0.5f, 0}, 0.0f, 0.5f, 1.0f, 16, tint);
            if (wireframe) DrawCylinderWires({0, -0.5f, 0}, 0.0f, 0.5f, 1.0f, 16, BLACK);
            break;
        case Kind::Plane:      // flat ground piece; no wire version exists
            DrawPlane({0, 0, 0}, {1.0f, 1.0f}, tint);
            break;
    }
}

void ShapeComponent::OnInspector() {
    // Combo indices must match the Kind enum order exactly.
    static const char* kKindNames[] = {"Cube", "Sphere", "Cylinder", "Cone", "Plane"};
    int k = (int)kind;
    // Not labeled "Shape": the CollapsingHeader above already owns that
    // ID, and headers don't scope their contents — same-window siblings.
    if (ImGui::Combo("Type", &k, kKindNames, 5))
        kind = (Kind)k;

    // ImGui wants colors as float[4] 0..1; raylib Color is byte 0..255.
    float col[4] = {tint.r / 255.0f, tint.g / 255.0f,
                    tint.b / 255.0f, tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", col)) {
        tint = {(unsigned char)(col[0] * 255), (unsigned char)(col[1] * 255),
                (unsigned char)(col[2] * 255), (unsigned char)(col[3] * 255)};
    }
    ImGui::Checkbox("Wireframe", &wireframe);
}

} // namespace eng
