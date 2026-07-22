// The editor program. It creates the window (via the engine's Application
// base class) and fills in what to draw each frame: the 3D scene, and the
// ImGui panels (Hierarchy, Inspector, Viewport, Game, Node Editor, Toolbar).

#include "engine/Application.h"   // window + main loop base class
#include "engine/Scene.h"         // the world of entities
#include "engine/Components.h"    // ShapeComponent, CameraComponent, ...
#include "engine/FileDialog.h"    // native open/save dialogs

#include "imgui.h"      // the UI library
#include "raylib.h"     // window, input, drawing, camera
#include "raymath.h"    // Vector3Transform, MatrixInvert, quaternion<->euler
#include "rlImGui.h"    // draws a render texture as an ImGui image
#include "rlgl.h"       // low-level matrix stack, for the selection outline

#include "ScriptGraph.h"           // the visual scripting graph

#include <imgui_node_editor.h>
namespace ed = ax::NodeEditor;     // a shorter alias for the node-editor namespace

#include <algorithm>    // std::clamp
#include <cmath>        // sinf / cosf for the orbit camera
#include <cstring>      // strncpy for text-edit buffers
#include <filesystem>   // set the working directory at startup

// EditorApp is our program. It inherits Application (which owns the window and
// loop) and overrides the three per-frame hooks to add the editor's behavior.
class EditorApp : public eng::Application {
public:
    // The constructor runs once at startup. ": eng::Application(...)" first
    // constructs the base class (creating the window), then this body runs.
    EditorApp(int w, int h, const char* title) : eng::Application(w, h, title) {
        // Set up the editor's own camera: an eye that orbits the scene so you
        // can look around while arranging objects.
        m_camera.position   = {8.0f, 8.0f, 8.0f};
        m_camera.target     = {0.0f, 0.0f, 0.0f};
        m_camera.up         = {0.0f, 1.0f, 0.0f};   // +Y is up
        m_camera.fovy       = 45.0f;                // field of view in degrees
        m_camera.projection = CAMERA_PERSPECTIVE;

        // Put a couple of starter entities in the world. The pattern is:
        // create the entity, then attach the components it needs. An entity
        // with no visual component exists but can't be seen.
        auto id = m_scene.CreateEntity("Player");
        eng::Entity* e = m_scene.Find(id);
        e->transform.position = {0.0f, 0.5f, 0.0f};
        e->AddComponent<eng::ShapeComponent>();

        id = m_scene.CreateEntity("Enemy");
        e = m_scene.Find(id);
        e->transform.position = {3.0f, 0.5f, -2.0f};
        e->AddComponent<eng::ShapeComponent>().tint = DARKGREEN;

        // The node editor library keeps its own per-canvas state (pan, zoom,
        // where nodes sit) inside a context object that we create and own.
        ed::Config cfg;
        cfg.SettingsFile = nullptr;   // don't write a settings file to disk
        m_nodeCtx = ed::CreateEditor(&cfg);

        // A second off-screen texture, separate from the engine's viewport
        // one, used to render the "Game" view through an in-scene camera.
        m_gameRT = LoadRenderTexture(1280, 720);
    }

    // The destructor runs at shutdown. `override` documents that it replaces
    // the base class's virtual destructor.
    ~EditorApp() override {
        UnloadRenderTexture(m_gameRT);
        ed::DestroyEditor(m_nodeCtx);
    }

    // Called once per frame BEFORE anything is drawn. Handles input and,
    // during play, advances the world. `dt` is the frame time in seconds.
    void OnUpdate(float dt) override {
        // --- Editor camera control ------------------------------------------
        // Holding the right mouse button over the viewport enters "fly" mode:
        // DisableCursor hides and locks the mouse pointer (so it can't leave
        // the window) while still reporting how far it moved. Because a locked
        // cursor hovers nothing, we track fly mode with our own flag instead
        // of asking ImGui whether the mouse is over the viewport.
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
                // Mouse movement turns the camera (yaw = left/right, pitch =
                // up/down). The 0.005 factor converts pixels moved to radians.
                Vector2 d = GetMouseDelta();
                m_camYaw   -= d.x * 0.005f;
                m_camPitch += d.y * 0.005f;
                // Keep pitch just short of straight up/down so the view can't
                // flip over.
                m_camPitch = std::clamp(m_camPitch, -1.5f, 1.5f);

                // WASD slides the point the camera looks at across the ground.
                // Each key contributes +1 or 0, so opposite keys cancel out.
                float fwdIn   = (IsKeyDown(KEY_W) ? 1.0f : 0.0f) - (IsKeyDown(KEY_S) ? 1.0f : 0.0f);
                float rightIn = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
                // Flatten the view direction onto the ground (no vertical part).
                Vector3 fwd   = {-sinf(m_camYaw), 0.0f, -cosf(m_camYaw)};
                Vector3 right = { cosf(m_camYaw), 0.0f, -sinf(m_camYaw)};
                float speed = m_camDist * 0.75f;   // move faster when zoomed further out
                m_camera.target.x += (fwd.x * fwdIn + right.x * rightIn) * speed * dt;
                m_camera.target.z += (fwd.z * fwdIn + right.z * rightIn) * speed * dt;
            }
            // The mouse wheel zooms by changing the orbit distance.
            m_camDist -= GetMouseWheelMove() * 1.0f;
            m_camDist  = std::clamp(m_camDist, 2.0f, 60.0f);

            // E and Q raise and lower the look-at point (and the camera with it).
            float lift = (IsKeyDown(KEY_E) ? 1.0f : 0.0f) -
                         (IsKeyDown(KEY_Q) ? 1.0f : 0.0f);
            m_camera.target.y += lift * 5.0f * dt;

            // Place the camera on a sphere of radius m_camDist around the
            // look-at point, at the current yaw/pitch angles (spherical to
            // cartesian coordinates).
            m_camera.position = {
                m_camera.target.x + m_camDist * cosf(m_camPitch) * sinf(m_camYaw),
                m_camera.target.y + m_camDist * sinf(m_camPitch),
                m_camera.target.z + m_camDist * cosf(m_camPitch) * cosf(m_camYaw),
            };
        }

        // --- Route keyboard input to the game only when appropriate ---------
        // Scripts should react to keys only when the Game panel has focus/hover,
        // nobody is typing in a text box, and the editor camera isn't using the
        // keyboard to fly. SetScriptInputEnabled toggles the scripting input API.
        eng::SetScriptInputEnabled(m_gameActive &&
                                   !ImGui::GetIO().WantTextInput && !flying);

        // --- Editor keyboard shortcuts --------------------------------------
        // Only when not typing, not flying, and something is selected.
        if (!ImGui::GetIO().WantTextInput && !flying &&
            m_selected != eng::kInvalidEntity) {
            if (IsKeyPressed(KEY_DELETE)) {           // Delete removes the entity
                m_scene.DestroyEntity(m_selected);
                m_selected = eng::kInvalidEntity;
            }
            if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_D)) {   // Ctrl+D duplicates
                eng::EntityID dup = m_scene.DuplicateEntity(m_selected);
                if (dup != eng::kInvalidEntity) m_selected = dup;       // select the copy
            }
        }

        // --- Advance the simulation while playing ---------------------------
        // In edit mode the world is frozen so you can arrange it; only in play
        // mode do scripts run.
        if (m_playing) {
            m_scene.Update(dt);
            // Track the player's speed for the HUD by measuring how far it
            // moved this frame (distance / time). This avoids reaching into
            // the flight script for its internal velocity.
            if (eng::Entity* pl = FindPlayer()) {
                Vector3 p = pl->transform.position;
                if (m_hasLastPos && dt > 0.0001f) {
                    float dx = p.x - m_lastPlayerPos.x;
                    float dy = p.y - m_lastPlayerPos.y;
                    float dz = p.z - m_lastPlayerPos.z;
                    m_playerSpeed = sqrtf(dx * dx + dy * dy + dz * dz) / dt;
                }
                m_lastPlayerPos = p;
                m_hasLastPos = true;
            }
        } else {
            m_hasLastPos  = false;   // reset so speed doesn't jump when play resumes
            m_playerSpeed = 0.0f;
        }

        // --- Render the Game view -------------------------------------------
        // If some entity is a camera, render the scene from its point of view
        // into m_gameRT. (Render textures can be drawn to before BeginDrawing.)
        if (eng::Entity* camEnt = FindCameraEntity()) {
            // Pass ignoreScale = true so a scaled parent can't stretch or shove
            // the camera; only its position and rotation should matter.
            Camera3D cam = camEnt->GetComponent<eng::CameraComponent>()
                                 ->ToCamera3D(m_scene.WorldMatrix(*camEnt, true));
            BeginTextureMode(m_gameRT);
            ClearBackground(Color{15, 15, 20, 255});
            BeginMode3D(cam);
            m_scene.Draw();               // the player's view has no editor grid
            EndMode3D();
            // Draw the 2D HUD on top of the 3D view (after EndMode3D so it's
            // flat screen-space, not in the world).
            if (eng::Entity* player = FindPlayer())
                DrawGameHud(player);
            EndTextureMode();
        }
    }

    // Draw the heads-up display over the Game view: a crosshair in the middle,
    // airspeed on the left, altitude on the right, and a health bar at the
    // bottom. Coordinates are in the game texture's pixels.
    void DrawGameHud(eng::Entity* player) {
        const int   w   = m_gameRT.texture.width;
        const int   h   = m_gameRT.texture.height;
        const int   cx  = w / 2;
        const int   cy  = h / 2;
        const Color hud = {90, 255, 130, 220};   // translucent green

        // Crosshair: four short ticks and a small center ring.
        DrawLine(cx - 16, cy, cx - 5, cy, hud);
        DrawLine(cx + 5, cy, cx + 16, cy, hud);
        DrawLine(cx, cy - 16, cx, cy - 5, hud);
        DrawLine(cx, cy + 5, cx, cy + 16, hud);
        DrawCircleLines(cx, cy, 3, hud);

        // Airspeed (units per second) on the left, vertically centered.
        DrawText(TextFormat("SPD %3.0f", m_playerSpeed), 24, cy - 10, 20, hud);

        // Engine power (throttle) below the speed, if the flight script posted
        // it. Shown as a percentage and a small bar so the player can fine-tune
        // their speed with the throttle keys.
        float throttle = eng::GetHudValue("throttle", -1.0f);
        if (throttle >= 0.0f) {
            if (throttle > 1.0f) throttle = 1.0f;
            DrawText(TextFormat("PWR %3.0f%%", throttle * 100.0f), 24, cy + 16, 20, hud);
            const int bx = 24, by = cy + 40, bw = 150, bh = 10;
            DrawRectangleLines(bx, by, bw, bh, hud);
            DrawRectangle(bx + 2, by + 2, (int)((bw - 4) * throttle), bh - 4,
                          Color{90, 255, 130, 170});
        }

        // Altitude (the jet's world height) on the right, right-aligned.
        const char* alt = TextFormat("ALT %4.0f", player->transform.position.y);
        DrawText(alt, w - 24 - MeasureText(alt, 20), cy - 10, 20, hud);

        // Health bar along the bottom, if the player has a Health component.
        if (auto* hpc = player->GetComponent<eng::HealthComponent>()) {
            float frac = (hpc->max > 0.0f) ? hpc->hp / hpc->max : 0.0f;
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            const int bw = 220, bh = 16;
            const int bx = cx - bw / 2, by = h - 44;
            DrawText("HP", bx - 34, by - 2, 20, hud);
            DrawRectangleLines(bx, by, bw, bh, hud);                     // outline
            DrawRectangle(bx + 2, by + 2, (int)((bw - 4) * frac), bh - 4, // fill
                          Color{90, 255, 130, 170});
        }
    }

    // Called each frame to draw the 3D scene into the engine's viewport texture.
    void OnRenderScene() override {
        BeginMode3D(m_camera);            // view the world through the editor camera
        DrawGrid(20, 1.0f);              // a 20x20 reference grid on the ground
        m_scene.Draw();

        // Draw a yellow wireframe box around the selected entity so you can see
        // what's selected. It's drawn in a scale-free frame (position+rotation
        // only) and sized to the entity's actual world size plus a small fixed
        // padding, so the outline hugs the object evenly at any size.
        if (eng::Entity* sel = m_scene.Find(m_selected)) {
            Vector3 ws = m_scene.WorldScale(*sel);
            const float pad = 0.15f;
            rlPushMatrix();
            rlMultMatrixf(MatrixToFloat(m_scene.WorldMatrix(*sel, /*ignoreScale=*/true)));
            DrawCubeWires({0, 0, 0}, ws.x + pad, ws.y + pad, ws.z + pad, YELLOW);
            rlPopMatrix();
        }
        EndMode3D();
    }

    // Called each frame to draw all the editor panels.
    void OnRenderUI() override {
        DrawToolbarPanel();
        DrawViewportPanel();
        DrawGamePanel();
        DrawHierarchyPanel();
        DrawInspectorPanel();
        DrawNodeEditorPanel();
    }

private:
    // Return the first entity that has a CameraComponent, or nullptr if none.
    eng::Entity* FindCameraEntity() {
        for (auto& e : m_scene.Entities())
            if (e.GetComponent<eng::CameraComponent>()) return &e;
        return nullptr;
    }

    // Return the first entity tagged "player" (the one the HUD reports on).
    eng::Entity* FindPlayer() {
        for (auto& e : m_scene.Entities())
            if (e.tag == "player") return &e;
        return nullptr;
    }

    // Play/Stop works by snapshot and restore: on Play we deep-copy the whole
    // scene, then let scripts run and change the live copy. On Stop we throw
    // the changed copy away and put the saved one back, so play never
    // permanently alters what you authored.
    void StartPlay() {
        m_backup.clear();
        for (const auto& e : m_scene.Entities())
            m_backup.push_back(e.Clone());
        m_scene.Start();              // run every script's on_start
        m_playing = true;
    }

    void StopPlay() {
        m_scene.Entities() = std::move(m_backup);   // restore the saved scene
        m_playing = false;
        // The selection still works because Clone kept the same entity ids.
    }

    // The top toolbar: Play/Stop, and (in edit mode) Save/Load of the scene.
    void DrawToolbarPanel() {
        ImGui::Begin("Toolbar");                    // begin a panel window
        if (m_playing) {
            if (ImGui::Button("Stop")) StopPlay();
            ImGui::SameLine();
            ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "playing");
        } else {
            if (ImGui::Button("Play")) StartPlay();
            // Saving is only offered in edit mode; saving mid-play would
            // capture the simulated state instead of what you authored.
            ImGui::SameLine();
            if (ImGui::Button("Save")) {
                std::string p = eng::SaveFileDialog(kSceneFilter, "json", "scene.json");
                if (!p.empty()) m_scene.Save(p);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                std::string p = eng::OpenFileDialog(kSceneFilter, "json");
                if (!p.empty() && m_scene.Load(p))
                    m_selected = eng::kInvalidEntity;   // the loaded scene has different ids
            }
        }
        ImGui::End();                                // end the panel
    }

    // Save the node graph. If we already know its file (or forceDialog is
    // false and a path is set) write straight back; otherwise ask where.
    void SaveGraph(bool forceDialog) {
        std::string path = m_graphPath;
        if (forceDialog || path.empty()) {
            path = eng::SaveFileDialog(kGraphFilter, "json", "graph.json");
            if (path.empty()) return;      // cancelled: change nothing
        }
        if (m_graph.Save(path)) m_graphPath = path;
    }

    // The visual scripting panel: a small file toolbar plus the node canvas.
    void DrawNodeEditorPanel() {
        ImGui::Begin("Node Editor");

        // A document-style toolbar. The graph's JSON file is the source you
        // edit here; "Generate Lua" writes a .lua script from it that entities
        // then run.
        if (ImGui::Button("New")) {
            m_graph.Reset();
            m_graphPath.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open")) {
            std::string p = eng::OpenFileDialog(kGraphFilter, "json");
            if (!p.empty() && m_graph.Load(p)) m_graphPath = p;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) SaveGraph(/*forceDialog=*/false);
        ImGui::SameLine();
        if (ImGui::Button("Save As")) SaveGraph(/*forceDialog=*/true);
        ImGui::SameLine();
        if (ImGui::Button("Generate Lua")) {
            std::string p = eng::SaveFileDialog(kLuaFilter, "lua", "myscript.lua");
            if (!p.empty()) m_graph.GenerateLua(p);
        }
        ImGui::SameLine();
        // Show the open file's name, or a placeholder if unsaved.
        ImGui::TextDisabled("%s", m_graphPath.empty() ? "(unsaved graph)"
                                                      : m_graphPath.c_str());

        m_graph.Draw(m_nodeCtx);    // draw the node canvas itself
        ImGui::End();
    }

    // The Scene viewport panel: shows the editor-camera view (the texture the
    // engine rendered the scene into), and reports whether the mouse is over it.
    void DrawViewportPanel() {
        ImGui::Begin("Viewport");
        m_viewportHovered = ImGui::IsWindowHovered();
        // Match the render texture's size to the space the panel gives us.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ResizeViewport((int)avail.x, (int)avail.y);
        rlImGuiImageRenderTexture(&m_viewport);   // draws the texture (Y-flipped for OpenGL)
        ImGui::End();
    }

    // The Game view panel: what the player sees, rendered through the scene's
    // camera entity. Also tracks whether the game "owns" the keyboard.
    void DrawGamePanel() {
        ImGui::Begin("Game");
        m_gameActive = ImGui::IsWindowFocused() || ImGui::IsWindowHovered();
        if (FindCameraEntity()) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ResizeRenderTexture(m_gameRT, (int)avail.x, (int)avail.y);
            rlImGuiImageRenderTexture(&m_gameRT);
        } else {
            // No camera exists, so there's nothing to show; explain how to fix.
            ImGui::TextDisabled("No camera in scene.");
            ImGui::TextDisabled("Add Component -> Camera on an entity.");
        }
        ImGui::End();
    }

    // The Hierarchy panel: an indented list of every entity. Click to select.
    void DrawHierarchyPanel() {
        ImGui::Begin("Hierarchy");

        if (ImGui::Button("+ Add Entity"))
            m_selected = m_scene.CreateEntity("New Entity");

        ImGui::Separator();

        // Draw the top-level (parent-less) entities; each row recurses into its
        // children, indented one level deeper.
        for (auto& e : m_scene.Entities())
            if (e.parent == eng::kInvalidEntity)
                DrawHierarchyRow(e, 0);

        ImGui::End();
    }

    // Re-parent an entity while keeping it looking the same on screen. When you
    // change an object's parent, its local transform is measured relative to
    // the new parent, so we recompute it:
    //  - position: take the current WORLD position, then express it in the new
    //    parent's space by multiplying by the inverse of the parent's matrix.
    //  - scale: divide the world scale by the parent chain's scale.
    // (Rotation isn't adjusted, so under a rotated parent the orientation can
    // change; position and size are the parts that matter most here.)
    void Reparent(eng::Entity& e, eng::EntityID newParent) {
        Vector3 worldPos   = Vector3Transform({0, 0, 0}, m_scene.WorldMatrix(e));
        Vector3 worldScale = m_scene.WorldScale(e);

        e.parent = newParent;

        Vector3 chain       = {1, 1, 1};
        Matrix  parentWorld = MatrixIdentity();
        if (const eng::Entity* p = m_scene.FindConst(newParent)) {
            chain       = m_scene.WorldScale(*p);
            parentWorld = m_scene.WorldMatrix(*p);
        }
        e.transform.position = Vector3Transform(worldPos, MatrixInvert(parentWorld));
        e.transform.scale    = {worldScale.x / chain.x,
                                worldScale.y / chain.y,
                                worldScale.z / chain.z};
    }

    // Draw one entity's row in the Hierarchy, then its children recursively.
    void DrawHierarchyRow(eng::Entity& e, int depth) {
        // PushID gives this row a unique ImGui identity even if two entities
        // share a name (ImGui identifies widgets by their label otherwise).
        ImGui::PushID((int)e.id);
        if (depth > 0) ImGui::Indent(depth * 16.0f);     // indent children
        if (ImGui::Selectable(e.name.c_str(), e.id == m_selected))
            m_selected = e.id;                            // clicking selects it
        if (depth > 0) ImGui::Unindent(depth * 16.0f);
        ImGui::PopID();

        for (auto& child : m_scene.Entities())
            if (child.parent == e.id)
                DrawHierarchyRow(child, depth + 1);
    }

    // The Inspector panel: edit the selected entity. It knows only about the
    // built-in fields (name, tag, parent, transform); every component draws its
    // own editing UI, so adding a new component type never changes this code.
    void DrawInspectorPanel() {
        ImGui::Begin("Inspector");

        eng::Entity* e = m_scene.Find(m_selected);
        if (!e) {
            ImGui::TextDisabled("Nothing selected");
            ImGui::End();
            return;
        }

        // --- Name (edited through a char buffer, then copied back) ----------
        char buf[64];
        strncpy(buf, e->name.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("Name", buf, sizeof(buf)))
            e->name = buf;

        // --- Tag (a shared category used by gameplay lookups) ---------------
        char tagbuf[64];
        strncpy(tagbuf, e->tag.c_str(), sizeof(tagbuf) - 1);
        tagbuf[sizeof(tagbuf) - 1] = '\0';
        if (ImGui::InputText("Tag", tagbuf, sizeof(tagbuf)))
            e->tag = tagbuf;

        // --- Parent (choose from a dropdown of valid parents) ---------------
        // Entities that would create a loop (this entity's own descendants) are
        // left out. Selecting one re-parents while preserving position and size.
        const eng::Entity* curParent = m_scene.FindConst(e->parent);
        if (ImGui::BeginCombo("Parent", curParent ? curParent->name.c_str() : "(none)")) {
            if (ImGui::Selectable("(none)", e->parent == eng::kInvalidEntity))
                Reparent(*e, eng::kInvalidEntity);
            for (auto& other : m_scene.Entities()) {
                if (other.id == e->id) continue;                   // can't parent to itself
                if (m_scene.WouldCycle(e->id, other.id)) continue; // would form a loop
                ImGui::PushID((int)other.id);
                if (ImGui::Selectable(other.name.c_str(), e->parent == other.id))
                    Reparent(*e, other.id);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        // --- Transform (always present) -------------------------------------
        ImGui::SeparatorText("Transform");
        ImGui::DragFloat3("Position", &e->transform.position.x, 0.1f);
        // Rotation is stored as a quaternion but shown as three euler angles.
        // We keep those angles in a separate buffer and only refill it from the
        // quaternion when the selection changes. If we recomputed the angles
        // from the quaternion every frame, they would jump around past 90
        // degrees (several angle triples describe the same orientation); the
        // buffer avoids that so dragging is smooth.
        if (m_rotEulerFor != m_selected) {
            Vector3 e0 = QuaternionToEuler(e->transform.rotation);   // radians
            m_rotEuler = {e0.x * RAD2DEG, e0.y * RAD2DEG, e0.z * RAD2DEG};
            m_rotEulerFor = m_selected;
        }
        if (ImGui::DragFloat3("Rotation", &m_rotEuler.x, 1.0f))
            e->transform.rotation = QuaternionFromEuler(m_rotEuler.x * DEG2RAD,
                                                        m_rotEuler.y * DEG2RAD,
                                                        m_rotEuler.z * DEG2RAD);
        ImGui::DragFloat3("Scale", &e->transform.scale.x, 0.05f);

        // --- Components (each draws its own editing UI) ---------------------
        // We loop by index (not a range-for) because the X button can erase the
        // current component mid-loop, which would break a range-for's iterator.
        for (size_t i = 0; i < e->components.size(); ) {
            eng::Component* c = e->components[i].get();
            ImGui::PushID((int)i);

            // A collapsing header for the component. AllowOverlap lets the X
            // button sit on the same row without the header swallowing its click.
            bool open = ImGui::CollapsingHeader(c->Name(),
                                                ImGuiTreeNodeFlags_DefaultOpen |
                                                ImGuiTreeNodeFlags_AllowOverlap);
            // A small "X" at the right edge to remove this component.
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
            bool removed = ImGui::SmallButton("X");
            if (open && !removed) c->OnInspector();   // let the component draw itself

            ImGui::PopID();

            if (removed) e->components.erase(e->components.begin() + i);
            else ++i;                                 // only advance if we kept it
        }

        // --- Add Component menu ---------------------------------------------
        ImGui::Separator();
        if (ImGui::Button("Add Component", ImVec2(-1, 0)))   // width -1 = fill the row
            ImGui::OpenPopup("AddComponentPopup");
        if (ImGui::BeginPopup("AddComponentPopup")) {
            // Each entry is disabled (greyed out) if the entity already has one
            // and that type doesn't allow duplicates.
            bool hasShape = e->GetComponent<eng::ShapeComponent>() != nullptr;
            if (ImGui::MenuItem("Shape", nullptr, false, !hasShape))
                e->AddComponent<eng::ShapeComponent>();
            if (ImGui::MenuItem("Script"))   // multiple scripts are allowed
                e->AddComponent<eng::ScriptComponent>();
            bool hasCam = e->GetComponent<eng::CameraComponent>() != nullptr;
            if (ImGui::MenuItem("Camera", nullptr, false, !hasCam))
                e->AddComponent<eng::CameraComponent>();
            bool hasHealth = e->GetComponent<eng::HealthComponent>() != nullptr;
            if (ImGui::MenuItem("Health", nullptr, false, !hasHealth))
                e->AddComponent<eng::HealthComponent>();
            bool hasModel = e->GetComponent<eng::ModelComponent>() != nullptr;
            if (ImGui::MenuItem("Model", nullptr, false, !hasModel))
                e->AddComponent<eng::ModelComponent>();
            bool hasTerrain = e->GetComponent<eng::TerrainComponent>() != nullptr;
            if (ImGui::MenuItem("Terrain", nullptr, false, !hasTerrain))
                e->AddComponent<eng::TerrainComponent>();
            ImGui::EndPopup();
        }

        // --- Delete the whole entity ----------------------------------------
        ImGui::Separator();
        if (ImGui::Button("Delete Entity")) {
            m_scene.DestroyEntity(m_selected);   // after this, `e` points at freed memory
            m_selected = eng::kInvalidEntity;    // so clear the selection and don't touch e
        }

        ImGui::End();
    }

    // File-type filters for the dialogs, in the Windows (label,pattern) format.
    static constexpr const char* kSceneFilter = "Scene (*.json)\0*.json\0All files\0*.*\0";
    static constexpr const char* kGraphFilter = "Node graph (*.json)\0*.json\0All files\0*.*\0";
    static constexpr const char* kLuaFilter   = "Lua script (*.lua)\0*.lua\0All files\0*.*\0";

    // --- Editor state -------------------------------------------------------
    eng::Scene    m_scene;                              // the world being edited
    std::vector<eng::Entity> m_backup;                  // saved scene while playing
    bool          m_playing  = false;                   // are we in play mode?
    eng::EntityID m_selected = eng::kInvalidEntity;     // the selected entity (or none)
    Camera3D      m_camera{};                            // the editor's orbit camera
    // Orbit camera angles/distance (initial values give a nice 3/4 view).
    float         m_camYaw   = 0.785f;                  // ~45 degrees, in radians
    float         m_camPitch = 0.615f;
    float         m_camDist  = 13.9f;
    bool          m_viewportHovered = false;            // mouse over the viewport panel?
    bool          m_flyLock   = false;                  // right-drag fly in progress?
    bool          m_gameActive = false;                 // Game panel focused/hovered?

    // The euler-angle edit buffer for the Inspector's Rotation field, and the
    // id of the entity it currently reflects.
    Vector3       m_rotEuler{0, 0, 0};
    eng::EntityID m_rotEulerFor = eng::kInvalidEntity;

    ed::EditorContext* m_nodeCtx = nullptr;             // node-editor canvas state
    edtr::ScriptGraph  m_graph;                         // the graph being edited
    std::string        m_graphPath;                     // its file ("" if unsaved)
    RenderTexture2D    m_gameRT{};                       // the Game view's texture

    // HUD state: the player's position last frame and its measured speed, used
    // to show airspeed without reading the flight script's internal velocity.
    Vector3 m_lastPlayerPos{};
    bool    m_hasLastPos  = false;
    float   m_playerSpeed = 0.0f;
};

// The program's entry point.
int main() {
    // Make every relative path (like "assets/scripts/flight.lua") resolve
    // against the project folder, regardless of where the built .exe sits.
    // PROJECT_ROOT_DIR is a string baked in at build time by CMake.
    std::filesystem::current_path(PROJECT_ROOT_DIR);

    EditorApp app(1280, 720, "MyEngine Editor");
    app.Run();          // run the main loop until the window is closed
    return 0;
}
