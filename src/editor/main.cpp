#include "engine/Application.h"
#include "engine/Scene.h"
#include "engine/Components.h"
#include "engine/FileDialog.h"

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include "ScriptGraph.h"

#include <imgui_node_editor.h>
namespace ed = ax::NodeEditor;   // the library's own suggested alias

#include <algorithm>    // std::clamp
#include <cmath>        // cosf/sinf for the orbit camera
#include <cstring>      // strncpy for the name-edit buffer
#include <filesystem>   // current_path at startup

class EditorApp : public eng::Application {
public:
    EditorApp(int w, int h, const char* title) : eng::Application(w, h, title) {
        // The editor camera: orbiting viewpoint for the scene view.
        m_camera.position   = {8.0f, 8.0f, 8.0f};
        m_camera.target     = {0.0f, 0.0f, 0.0f};
        m_camera.up         = {0.0f, 1.0f, 0.0f};   // which way is "up" (+Y)
        m_camera.fovy       = 45.0f;                // vertical field of view, degrees
        m_camera.projection = CAMERA_PERSPECTIVE;

        // Seed the world. Note the pattern: create entity, then ATTACH
        // what it needs. An entity without a Shape exists but is invisible.
        auto id = m_scene.CreateEntity("Player");
        eng::Entity* e = m_scene.Find(id);
        e->transform.position = {0.0f, 0.5f, 0.0f};
        e->AddComponent<eng::ShapeComponent>();

        id = m_scene.CreateEntity("Enemy");
        e = m_scene.Find(id);
        e->transform.position = {3.0f, 0.5f, -2.0f};
        e->AddComponent<eng::ShapeComponent>().tint = DARKGREEN;

        // The node editor keeps per-canvas state (pan, zoom, node
        // positions) in a context object we own.
        ed::Config cfg;
        cfg.SettingsFile = nullptr;   // don't write a NodeEditor.json yet
        m_nodeCtx = ed::CreateEditor(&cfg);

        // Second render target: the Game view (rendered through whatever
        // entity has a CameraComponent — the Scene viewport stays on the
        // editor's own camera, Unity-style).
        m_gameRT = LoadRenderTexture(1280, 720);
    }

    ~EditorApp() override {
        UnloadRenderTexture(m_gameRT);
        ed::DestroyEditor(m_nodeCtx);
    }

    void OnUpdate(float dt) override {
        // Editor camera: OUR orbit code, mouse-only. raylib's
        // UpdateCamera(CAMERA_THIRD_PERSON) also reads WASD internally —
        // it fought the game for the keyboard (drive cube = drive camera).
        // Cursor capture: RMB down over the viewport locks the pointer
        // (hidden, can't leave the window, deltas keep coming) until
        // release. While locked, ImGui hover tests go blind — so flight
        // is tracked by m_flyLock, not by hover.
        if (m_viewportHovered && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            DisableCursor();
            m_flyLock = true;
        }
        if (m_flyLock && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
            EnableCursor();
            m_flyLock = false;
        }

        bool flying = m_flyLock;
        if (m_viewportHovered || m_flyLock) {
            if (flying) {
                Vector2 d = GetMouseDelta();
                m_camYaw   -= d.x * 0.005f;   // radians per pixel dragged
                m_camPitch += d.y * 0.005f;
                // Clamp pitch short of straight up/down (gimbal flip).
                m_camPitch = std::clamp(m_camPitch, -1.5f, 1.5f);

                // Unity-style fly: WASD moves the look TARGET on the
                // ground plane while RMB is held (camera follows below).
                float fwdIn   = (IsKeyDown(KEY_W) ? 1.0f : 0.0f) - (IsKeyDown(KEY_S) ? 1.0f : 0.0f);
                float rightIn = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
                Vector3 fwd   = {-sinf(m_camYaw), 0.0f, -cosf(m_camYaw)};   // view dir, flattened
                Vector3 right = { cosf(m_camYaw), 0.0f, -sinf(m_camYaw)};
                float speed = m_camDist * 0.75f;   // faster when zoomed out
                m_camera.target.x += (fwd.x * fwdIn + right.x * rightIn) * speed * dt;
                m_camera.target.z += (fwd.z * fwdIn + right.z * rightIn) * speed * dt;
            }
            m_camDist -= GetMouseWheelMove() * 1.0f;
            m_camDist  = std::clamp(m_camDist, 2.0f, 60.0f);

            // E/Q raise/lower the LOOK TARGET; the camera follows.
            float lift = (IsKeyDown(KEY_E) ? 1.0f : 0.0f) -
                         (IsKeyDown(KEY_Q) ? 1.0f : 0.0f);
            m_camera.target.y += lift * 5.0f * dt;

            // Spherical -> cartesian: place the eye on a sphere of radius
            // m_camDist around the target, at the yaw/pitch angles.
            m_camera.position = {
                m_camera.target.x + m_camDist * cosf(m_camPitch) * sinf(m_camYaw),
                m_camera.target.y + m_camDist * sinf(m_camPitch),
                m_camera.target.z + m_camDist * cosf(m_camPitch) * cosf(m_camYaw),
            };
        }

        // Script input flows only when the GAME panel is hovered/focused
        // (Unity's rule), nobody is typing, and the editor camera isn't
        // claiming the keyboard.
        eng::SetScriptInputEnabled(m_gameActive &&
                                   !ImGui::GetIO().WantTextInput && !flying);

        // Scripts only run in play mode, like Unity. In edit mode the
        // world is frozen — the viewport is for arranging, not simulating.
        if (m_playing) m_scene.Update(dt);

        // Game view render pass: through the scene's camera entity, if
        // any. (Render textures can be drawn to outside BeginDrawing.)
        if (eng::Entity* camEnt = FindCameraEntity()) {
            // ignoreScale: a scaled parent must not push the camera away
            // or skew its aim — position/rotation inherit, scale doesn't.
            Camera3D cam = camEnt->GetComponent<eng::CameraComponent>()
                                 ->ToCamera3D(m_scene.WorldMatrix(*camEnt, true));
            BeginTextureMode(m_gameRT);
            ClearBackground(Color{15, 15, 20, 255});
            BeginMode3D(cam);
            m_scene.Draw();               // no grid: this is the player's view
            EndMode3D();
            EndTextureMode();
        }
    }

    void OnRenderScene() override {
        BeginMode3D(m_camera);
        DrawGrid(20, 1.0f);           // floor reference: 20x20 cells, 1 unit each
        m_scene.Draw();
        EndMode3D();
    }

    void OnRenderUI() override {
        DrawToolbarPanel();
        DrawViewportPanel();
        DrawGamePanel();
        DrawHierarchyPanel();
        DrawInspectorPanel();
        DrawNodeEditorPanel();
    }

private:
    // First entity carrying a CameraComponent (nullptr if none). Unity
    // uses the "main camera" tag for this; first-found is fine for now.
    eng::Entity* FindCameraEntity() {
        for (auto& e : m_scene.Entities())
            if (e.GetComponent<eng::CameraComponent>()) return &e;
        return nullptr;
    }

    // Unity's play/stop cycle = snapshot/restore:
    //   Play: deep-copy the scene (the "authored" version), then simulate.
    //   Stop: throw the simulated state away, put the copy back.
    void StartPlay() {
        m_backup.clear();
        for (const auto& e : m_scene.Entities())
            m_backup.push_back(e.Clone());
        m_scene.Start();              // fires OnStart (scripts load here)
        m_playing = true;
    }

    void StopPlay() {
        m_scene.Entities() = std::move(m_backup);   // restore authored scene
        m_playing = false;
        // m_selected survives: ids were preserved by Clone().
    }

    void DrawToolbarPanel() {
        ImGui::Begin("Toolbar");
        if (m_playing) {
            if (ImGui::Button("Stop")) StopPlay();
            ImGui::SameLine();
            ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "playing");
        } else {
            if (ImGui::Button("Play")) StartPlay();
            // Save/Load only in edit mode: saving mid-play would capture
            // simulated state as if it were authored (Unity forbids it too).
            ImGui::SameLine();
            if (ImGui::Button("Save")) {
                std::string p = eng::SaveFileDialog(kSceneFilter, "json", "scene.json");
                if (!p.empty()) m_scene.Save(p);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                std::string p = eng::OpenFileDialog(kSceneFilter, "json");
                if (!p.empty() && m_scene.Load(p))
                    m_selected = eng::kInvalidEntity;   // old selection id may not exist
            }
        }
        ImGui::End();
    }

    void DrawNodeEditorPanel() {
        ImGui::Begin("Node Editor");

        // Graph toolbar. The JSON is the SOURCE (reopenable in this
        // panel); the .lua is a BUILD ARTIFACT the entities reference.
        if (ImGui::Button("Save Graph")) {
            std::string p = eng::SaveFileDialog(kGraphFilter, "json", "graph.json");
            if (!p.empty()) m_graph.Save(p);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Graph")) {
            std::string p = eng::OpenFileDialog(kGraphFilter, "json");
            if (!p.empty()) m_graph.Load(p);
        }
        ImGui::SameLine();
        if (ImGui::Button("Generate Lua")) {
            std::string p = eng::SaveFileDialog(kLuaFilter, "lua", "myscript.lua");
            if (!p.empty()) m_graph.GenerateLua(p);
        }

        m_graph.Draw(m_nodeCtx);
        ImGui::End();
    }

    void DrawViewportPanel() {
        ImGui::Begin("Viewport");
        m_viewportHovered = ImGui::IsWindowHovered();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ResizeViewport((int)avail.x, (int)avail.y);
        rlImGuiImageRenderTexture(&m_viewport);   // flips Y for OpenGL
        ImGui::End();
    }

    // What the player will see: rendered through the scene's camera
    // entity. Scripts move that entity -> this view moves. Unity's "Game".
    void DrawGamePanel() {
        ImGui::Begin("Game");
        // Does the game "own" the keyboard? Hovered or focused counts.
        m_gameActive = ImGui::IsWindowFocused() || ImGui::IsWindowHovered();
        if (FindCameraEntity()) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ResizeRenderTexture(m_gameRT, (int)avail.x, (int)avail.y);
            rlImGuiImageRenderTexture(&m_gameRT);
        } else {
            ImGui::TextDisabled("No camera in scene.");
            ImGui::TextDisabled("Add Component -> Camera on an entity.");
        }
        ImGui::End();
    }

    // Lists every entity; click to select. Unity's "Hierarchy" window.
    void DrawHierarchyPanel() {
        ImGui::Begin("Hierarchy");

        if (ImGui::Button("+ Add Entity"))
            m_selected = m_scene.CreateEntity("New Entity");

        ImGui::Separator();

        // Roots first; each recurses into its children, indented.
        for (auto& e : m_scene.Entities())
            if (e.parent == eng::kInvalidEntity)
                DrawHierarchyRow(e, 0);

        ImGui::End();
    }

    // Change parent while keeping the entity's VISIBLE size: divide its
    // world scale by the new parent chain's scale to get the local value.
    // (Position/rotation aren't compensated yet — the entity may move on
    // reparent; scale was the painful one.)
    void Reparent(eng::Entity& e, eng::EntityID newParent) {
        Vector3 world = m_scene.WorldScale(e);       // size before, chain included
        e.parent = newParent;
        Vector3 chain = {1, 1, 1};
        if (const eng::Entity* p = m_scene.FindConst(newParent)) {
            chain = m_scene.WorldScale(*p);
        }
        e.transform.scale = {world.x / chain.x, world.y / chain.y, world.z / chain.z};
    }

    void DrawHierarchyRow(eng::Entity& e, int depth) {
        ImGui::PushID((int)e.id);   // entities may share a name
        if (depth > 0) ImGui::Indent(depth * 16.0f);
        if (ImGui::Selectable(e.name.c_str(), e.id == m_selected))
            m_selected = e.id;
        if (depth > 0) ImGui::Unindent(depth * 16.0f);
        ImGui::PopID();

        for (auto& child : m_scene.Entities())
            if (child.parent == e.id)
                DrawHierarchyRow(child, depth + 1);
    }

    // Edits the selected entity. Generic on purpose: it knows about name
    // + transform, and asks each component to draw ITSELF. Adding new
    // component types never requires touching this code again.
    void DrawInspectorPanel() {
        ImGui::Begin("Inspector");

        eng::Entity* e = m_scene.Find(m_selected);
        if (!e) {
            ImGui::TextDisabled("Nothing selected");
            ImGui::End();
            return;
        }

        // --- Name -----------------------------------------------------
        char buf[64];
        strncpy(buf, e->name.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("Name", buf, sizeof(buf)))
            e->name = buf;

        // --- Parent (the scene-graph link) ----------------------------
        // Cycle-creating choices (own descendants) are filtered out.
        // On change, local scale is recomputed so the entity keeps its
        // VISIBLE size (Unity does the same when you reparent).
        const eng::Entity* curParent = m_scene.FindConst(e->parent);
        if (ImGui::BeginCombo("Parent", curParent ? curParent->name.c_str() : "(none)")) {
            if (ImGui::Selectable("(none)", e->parent == eng::kInvalidEntity))
                Reparent(*e, eng::kInvalidEntity);
            for (auto& other : m_scene.Entities()) {
                if (other.id == e->id) continue;                  // not yourself
                if (m_scene.WouldCycle(e->id, other.id)) continue; // no loops
                ImGui::PushID((int)other.id);
                if (ImGui::Selectable(other.name.c_str(), e->parent == other.id))
                    Reparent(*e, other.id);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        // --- Transform (built-in, always present) ---------------------
        ImGui::SeparatorText("Transform");
        ImGui::DragFloat3("Position", &e->transform.position.x, 0.1f);
        ImGui::DragFloat3("Rotation", &e->transform.rotation.x, 1.0f);
        ImGui::DragFloat3("Scale", &e->transform.scale.x, 0.05f);

        // --- Components (each draws its own UI) -----------------------
        // Index loop, not range-for: the X button erases while iterating,
        // and erasing invalidates range-for iterators.
        for (size_t i = 0; i < e->components.size(); ) {
            eng::Component* c = e->components[i].get();
            ImGui::PushID((int)i);

            // AllowOverlap: without it the header eats every click on its
            // row and the X button underneath never fires.
            bool open = ImGui::CollapsingHeader(c->Name(),
                                                ImGuiTreeNodeFlags_DefaultOpen |
                                                ImGuiTreeNodeFlags_AllowOverlap);
            // Small "X" on the same line to remove the component.
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
            bool removed = ImGui::SmallButton("X");
            if (open && !removed) c->OnInspector();

            ImGui::PopID();

            if (removed) e->components.erase(e->components.begin() + i);
            else ++i;
        }

        // --- Add Component (Unity's button) ---------------------------
        ImGui::Separator();
        if (ImGui::Button("Add Component", ImVec2(-1, 0)))   // -1 = full width
            ImGui::OpenPopup("AddComponentPopup");
        if (ImGui::BeginPopup("AddComponentPopup")) {
            // Hard-coded menu for now; becomes a registry when the list grows.
            // Entries are greyed out if the entity already has one and the
            // component type doesn't allow duplicates.
            bool hasShape = e->GetComponent<eng::ShapeComponent>() != nullptr;
            if (ImGui::MenuItem("Shape", nullptr, false, !hasShape))
                e->AddComponent<eng::ShapeComponent>();
            if (ImGui::MenuItem("Script"))   // multiple allowed, never greyed
                e->AddComponent<eng::ScriptComponent>();
            bool hasCam = e->GetComponent<eng::CameraComponent>() != nullptr;
            if (ImGui::MenuItem("Camera", nullptr, false, !hasCam))
                e->AddComponent<eng::CameraComponent>();
            ImGui::EndPopup();
        }

        // --- Danger zone ----------------------------------------------
        ImGui::Separator();
        if (ImGui::Button("Delete Entity")) {
            m_scene.DestroyEntity(m_selected);   // `e` is dangling after this!
            m_selected = eng::kInvalidEntity;
        }

        ImGui::End();
    }

    // Win32 filter strings: (label, pattern) pairs, double-null separated.
    static constexpr const char* kSceneFilter = "Scene (*.json)\0*.json\0All files\0*.*\0";
    static constexpr const char* kGraphFilter = "Node graph (*.json)\0*.json\0All files\0*.*\0";
    static constexpr const char* kLuaFilter   = "Lua script (*.lua)\0*.lua\0All files\0*.*\0";

    eng::Scene    m_scene;
    std::vector<eng::Entity> m_backup;   // authored scene, held during play
    bool          m_playing  = false;
    eng::EntityID m_selected = eng::kInvalidEntity;
    Camera3D      m_camera{};
    // Orbit state; initial values reproduce the old (8,8,8) view.
    float         m_camYaw   = 0.785f;   // 45 degrees
    float         m_camPitch = 0.615f;
    float         m_camDist  = 13.9f;
    bool          m_viewportHovered = false;
    bool          m_flyLock   = false;   // RMB flight in progress (cursor captured)
    bool          m_gameActive = false;  // Game panel hovered/focused last frame
    ed::EditorContext* m_nodeCtx = nullptr;
    edtr::ScriptGraph  m_graph;
    RenderTexture2D    m_gameRT{};   // the Game view's canvas
};

int main() {
    // Run from the project root, wherever the exe actually lives. Every
    // relative path ("assets/...") now hits the REAL source assets — the
    // editor edits the project, not a copy next to the exe.
    std::filesystem::current_path(PROJECT_ROOT_DIR);

    EditorApp app(1280, 720, "MyEngine Editor");
    app.Run();
    return 0;
}
