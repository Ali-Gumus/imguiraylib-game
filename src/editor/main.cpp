// The editor program. It creates the window (via the engine's Application
// base class) and fills in what to draw each frame: the 3D scene, and the
// ImGui panels (Hierarchy, Inspector, Viewport, Game, Node Editor, Toolbar).

#include "engine/Application.h"   // window + main loop base class
#include "engine/Scene.h"         // the world of entities
#include "engine/Components.h"    // ShapeComponent, CameraComponent, ...
#include "engine/ModelDefs.h"      // named model set-ups, re-read on Play
#include "engine/FileDialog.h"    // native open/save dialogs
#include "engine/Lighting.h"      // the directional light and its shader
#include "engine/Particles.h"     // explosion / spark / muzzle-flash effects
#include "engine/Audio.h"         // sound playback and the mute toggle
#include "engine/Physics.h"       // the rigid-body simulation (Jolt)
#include "engine/LuaApiRegistry.h" // the scripting API catalogue, for the Script API panel

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
#include <cctype>       // tolower, for the case-insensitive API filter
#include <cstring>      // strncpy for text-edit buffers
#include <deque>        // the Play/Stop snapshot mirrors Scene's entity store
#include <filesystem>   // set the working directory at startup

// EditorApp is our program. It inherits Application (which owns the window and
// loop) and overrides the three per-frame hooks to add the editor's behavior.
class EditorApp : public eng::Application {
public:
    // The constructor runs once at startup. ": eng::Application(...)" first
    // constructs the base class (creating the window), then this body runs.
    EditorApp(int w, int h, const char* title) : eng::Application(w, h, title) {
        // Teach the engine how to turn a graph file into Lua, FIRST - before any
        // entity or scene exists. The engine cannot do this itself: the code
        // generator lives here in the editor, so the engine declares the seam
        // and we fill it in. A Graph component then compiles through whatever is
        // registered, without the engine ever including an editor header.
        //
        // Registering it before everything else matters: any graph compiled
        // earlier in startup - a component created here, a scene loaded on the
        // command line later - would otherwise find no compiler and fail.
        eng::SetGraphCompiler([](const std::string& graphPath,
                                 std::string& outLua, std::string& outError) -> bool {
            // A throwaway graph object: it loads the file, generates, and is
            // gone. Deliberately NOT the graph open in the panel, so compiling
            // never disturbs what the developer is editing.
            edtr::ScriptGraph g;
            if (!g.Load(graphPath)) {
                outError = "Could not read the graph file: " + graphPath;
                return false;
            }
            outLua = g.GenerateLuaSource();
            return true;
        });

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

        // The sun. A directional light is aimed by rotating its entity, so the
        // entity is turned until its forward axis (local -Z) points the way the
        // light should travel: downward and off to one side, like afternoon
        // sun. QuaternionFromVector3ToVector3 builds exactly the rotation that
        // carries the first vector onto the second.
        id = m_scene.CreateEntity("Sun");
        e = m_scene.Find(id);
        e->transform.position = {0.0f, 3.0f, 0.0f};   // only for the gizmo's sake
        e->transform.rotation = QuaternionFromVector3ToVector3(
            {0.0f, 0.0f, -1.0f}, Vector3Normalize({0.35f, -0.85f, -0.4f}));
        e->AddComponent<eng::LightComponent>();

        // The node editor library keeps its own per-canvas state (pan, zoom,
        // where nodes sit) inside a context object that we create and own.
        ed::Config cfg;
        cfg.SettingsFile = nullptr;   // don't write a settings file to disk
        m_nodeCtx = ed::CreateEditor(&cfg);

        // A second off-screen texture, separate from the engine's viewport
        // one, used to render the "Game" view through an in-scene camera.
        m_gameRT = LoadRenderTexture(1280, 720);

        // A procedural gradient skybox: a unit cube drawn around the camera and
        // colored by view direction in the shader (no texture needed). If the
        // shader fails to compile we just skip drawing it.
        // Load the shader that shades everything in the world by its angle to
        // the sun. It must happen after the window exists, because a shader is
        // a GPU object. If it fails the engine renders unlit, as it did before
        // lighting existed, so there is nothing to handle here.
        eng::InitLighting();

        // The particle system's dot texture also lives on the GPU, so it is
        // created here for the same reason.
        eng::InitParticles();

        // Open the audio device and read the sound definitions. Failure here is
        // not fatal: the game plays silently and says so in the toolbar.
        eng::InitAudio();

        // Build the rigid-body world. This one needs no window - physics never
        // touches the GPU - but it lives here with the other subsystems so
        // there is a single place that starts everything.
        eng::InitPhysics();

        // Read the named model set-ups that spawning scripts refer to.
        eng::ReloadModelDefs();

        m_skyShader = LoadShader("assets/shaders/skybox.vs", "assets/shaders/skybox.fs");
        m_skyReady  = IsShaderValid(m_skyShader);
        if (m_skyReady) {
            m_sky = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
            m_sky.materials[0].shader = m_skyShader;
            // Look the uniform's location up ONCE. GetShaderLocation queries the
            // driver by name, which is far too slow to repeat every frame, so
            // the handle is cached and only the value is pushed per draw.
            m_skyScaleLoc = GetShaderLocation(m_skyShader, "skyScale");
        }
    }

    // The destructor runs at shutdown. `override` documents that it replaces
    // the base class's virtual destructor.
    ~EditorApp() override {
        UnloadRenderTexture(m_gameRT);
        if (m_skyReady) { UnloadModel(m_sky); UnloadShader(m_skyShader); }
        eng::ShutdownLighting();
        eng::ShutdownParticles();
        eng::ShutdownAudio();
        eng::ShutdownPhysics();
        // Model files are shared and outlive the components that drew them, so
        // they are freed here rather than by any one component.
        eng::ClearModelCache();
        ed::DestroyEditor(m_nodeCtx);
    }

    // Find the scene's light entity and push its settings to the lighting
    // shader. Only the first Light in the scene is used: this engine supports
    // one directional light, the way an outdoor scene has one sun. With no
    // light entity at all, the shader's built-in defaults are restored, so a
    // scene authored before lighting existed still looks sensible.
    void ApplySceneLight() {
        for (eng::Entity& e : m_scene.Entities()) {
            auto* light = e.GetComponent<eng::LightComponent>();
            if (!light) continue;

            eng::SunSettings sun;
            // The light travels along the entity's forward axis. In a world
            // matrix the three basis vectors sit in known slots: (m8,m9,m10)
            // is the entity's local +Z in world space, and this engine's
            // convention is that forward is local -Z, hence the minus signs.
            Matrix wm = m_scene.WorldMatrix(e, /*ignoreScale=*/true);
            sun.direction = {-wm.m8, -wm.m9, -wm.m10};

            // Intensity scales the colour rather than being its own uniform,
            // which keeps the shader simpler: brightness and hue arrive as one
            // number per channel.
            sun.color   = {light->color.x * light->intensity,
                           light->color.y * light->intensity,
                           light->color.z * light->intensity};
            sun.ambient = light->ambient;
            sun.sky     = light->sky;
            sun.ground  = light->ground;
            eng::SetSun(sun);
            return;                       // first light wins; stop looking
        }
        eng::SetSun(eng::SunSettings{});   // no light in the scene: use defaults
    }

    // Put the listener where the world is being heard from, so that a sound's
    // distance and side are measured against the right place.
    //
    // The listener follows whichever camera the player is looking through, so
    // what is heard always agrees with what is seen. While play is running that
    // is the scene's CameraComponent - the chase camera behind the jet. While
    // stopped, it is the editor's own fly camera, so clicking around the scene
    // in the viewport still gives an idea of where things sound from.
    void UpdateAudioListener() {
        if (m_playing) {
            if (eng::Entity* camEnt = FindCameraEntity()) {
                Matrix w = m_scene.WorldMatrix(*camEnt, /*ignoreScale=*/true);
                // A world matrix carries the entity's axes in known slots.
                // (m8,m9,m10) is its local +Z in world space and this engine's
                // convention is that forward is local -Z, hence the negation;
                // (m4,m5,m6) is its local +Y, which is "up".
                eng::SetAudioListener(Vector3Transform({0.0f, 0.0f, 0.0f}, w),
                                      {-w.m8, -w.m9, -w.m10},
                                      { w.m4,  w.m5,  w.m6});
                return;
            }
        }
        // No camera entity, or not playing: use the editor's viewport camera.
        eng::SetAudioListener(m_camera.position,
                              Vector3Subtract(m_camera.target, m_camera.position),
                              m_camera.up);
    }

    // Called once per frame BEFORE anything is drawn. Handles input and,
    // during play, advances the world. `dt` is the frame time in seconds.
    void OnUpdate(float dt) override {
        // Refresh the sun before anything is rendered this frame, so moving the
        // light entity updates the shading immediately.
        ApplySceneLight();

        // Age the visual effects. This runs whether or not the game is playing:
        // a burst fired on the last frame before Stop should still finish
        // gracefully rather than freeze in mid-air.
        eng::UpdateParticles(dt);

        // Place the listener before anything can play a sound this frame, or
        // positioned sounds would be judged against where the listener was on
        // the PREVIOUS frame - which at 200 units a second is a long way off.
        UpdateAudioListener();

        // Looping sounds decode as they play and hold only a small buffer, so
        // they must be topped up every frame or they stutter and stop.
        eng::UpdateAudio();


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

                // THE WHEEL SETS THE SPEED WHILE FLYING, not the zoom. This is
                // the Unity arrangement and it is the one that works: the two
                // things you want to change while moving are where you are going
                // and how fast, and the hand on the mouse can only do one of
                // them. Zoom stays on the wheel when NOT holding the right
                // button, so nothing is lost.
                //
                // Adjusted by a FACTOR rather than a fixed step, because the
                // useful range runs from a couple of metres a second for
                // inspecting a cockpit to thousands for crossing the map. A
                // fixed step is either uselessly fine at one end or unusable at
                // the other; multiplying gives even control across the whole
                // range - the same reason the terrain sliders are logarithmic.
                const float wheel = GetMouseWheelMove();
                if (wheel != 0.0f) {
                    m_flySpeed *= powf(1.2f, wheel);
                    m_flySpeed  = std::clamp(m_flySpeed, 1.0f, 20000.0f);
                }

                // Each key contributes +1 or 0, so opposite keys cancel out.
                float fwdIn   = (IsKeyDown(KEY_W) ? 1.0f : 0.0f) - (IsKeyDown(KEY_S) ? 1.0f : 0.0f);
                float rightIn = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
                float upIn    = (IsKeyDown(KEY_E) ? 1.0f : 0.0f) - (IsKeyDown(KEY_Q) ? 1.0f : 0.0f);

                // W and S now follow where the camera is actually LOOKING,
                // including its pitch, instead of sliding along the ground. Aim
                // at something and fly at it - which is what makes reaching a
                // camp on a hillside one movement rather than a translate and a
                // separate climb.
                Vector3 fwd = {-cosf(m_camPitch) * sinf(m_camYaw),
                               -sinf(m_camPitch),
                               -cosf(m_camPitch) * cosf(m_camYaw)};
                // Strafing stays horizontal. A right vector that tilted with the
                // pitch would roll the view sideways as you flew, and there is
                // no reason to want that.
                Vector3 right = {cosf(m_camYaw), 0.0f, -sinf(m_camYaw)};

                // SHIFT for a burst of speed, CONTROL to creep. Holding a key is
                // the right control for a temporary change: it needs no undoing
                // and cannot be left switched on by accident.
                float speed = m_flySpeed;
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) speed *= 5.0f;
                if (IsKeyDown(KEY_LEFT_CONTROL))                             speed *= 0.2f;

                const float step = speed * dt;
                m_camera.target.x += (fwd.x * fwdIn + right.x * rightIn) * step;
                m_camera.target.y += (fwd.y * fwdIn + upIn) * step;
                m_camera.target.z += (fwd.z * fwdIn + right.z * rightIn) * step;
            } else {
                // Not flying: the wheel zooms, by changing the orbit distance.
                //
                // The step is a FRACTION of the current distance rather than a
                // fixed metre. At arm's length from a model a fixed step of one
                // metre is a lurch, and a kilometre out it is imperceptible -
                // the same scale problem the fly speed has, with the same fix.
                const float wheel = GetMouseWheelMove();
                if (wheel != 0.0f) {
                    m_camDist *= powf(0.85f, wheel);
                    // The old ceiling here was 60 metres, which in a world 40 km
                    // across meant the whole landscape could never be seen at
                    // once. The far clip plane is the real limit now.
                    m_camDist = std::clamp(m_camDist, 0.5f, 15000.0f);
                }

                // E and Q still raise and lower the view when not flying, at a
                // rate that follows how far out you are - a fixed one would
                // crawl when zoomed out and lurch when close in.
                float lift = (IsKeyDown(KEY_E) ? 1.0f : 0.0f) -
                             (IsKeyDown(KEY_Q) ? 1.0f : 0.0f);
                m_camera.target.y += lift * m_camDist * 0.75f * dt;
            }

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
            // Once a script sets "game_over", the world freezes on the game-over
            // screen (scripts stop advancing) and R restarts the run by
            // restoring the authored scene and playing it again.
            if (eng::GetHudValue("game_over", 0.0f) > 0.0f) {
                if (IsKeyPressed(KEY_R)) { StopPlay(); StartPlay(); }
            } else {
                m_scene.Update(dt);

                // Advance the rigid-body simulation, AFTER the scripts have
                // run. The order is the whole contract between the two: a
                // script's job is to decide what forces to apply this frame,
                // and the simulation's job is to work out the motion those
                // forces produce. Stepping first would act on last frame's
                // decisions and leave the controls feeling a frame late.
                eng::UpdatePhysics(m_scene, dt);
            }
        }

        // --- Render the Game view -------------------------------------------
        // If some entity is a camera, render the scene from its point of view
        // into m_gameRT. (Render textures can be drawn to before BeginDrawing.)
        // Only when the Game panel is actually on screen: rendering the whole
        // scene a second time into a texture nobody can see is the single
        // easiest frame to give back.
        if (eng::Entity* camEnt = m_gameVisible ? FindCameraEntity() : nullptr) {
            // Pass ignoreScale = true so a scaled parent can't stretch or shove
            // the camera; only its position and rotation should matter.
            eng::CameraComponent* camComp = camEnt->GetComponent<eng::CameraComponent>();
            Camera3D cam = camComp->ToCamera3D(m_scene.WorldMatrix(*camEnt, true));

            // Apply this camera's clipping planes. They are GLOBAL state inside
            // the graphics layer, read by BeginMode3D when it builds the
            // projection matrix, so they must be set before it and they persist
            // afterwards. Setting them immediately before each BeginMode3D is
            // what keeps the Game view and the editor Viewport independent -
            // whichever ran last would otherwise impose its view distance on the
            // other, and the symptom (a view distance that changes depending on
            // which panel is open) would be baffling.
            rlSetClipPlanes(camComp->nearClip, camComp->farClip);

            BeginTextureMode(m_gameRT);
            ClearBackground(Color{15, 15, 20, 255});
            BeginMode3D(cam);
            DrawSky(camComp->nearClip);   // gradient sky behind the world
            m_scene.Draw();               // the player's view has no editor grid
            // Effects draw after the world, so they are never shaded by the
            // sun: fire and sparks give off their own light.
            eng::DrawParticles(cam);
            EndMode3D();

            // Screen-space component overlays (the minimap, and anything else
            // that draws in pixels rather than metres). Run here, after
            // EndMode3D, because that is what "screen space" means: the 3D pass
            // is finished, so there is no camera or matrix left to fight.
            //
            // The size passed is the game TEXTURE's, not the window's - the Game
            // panel is whatever size it has been dragged to, and an overlay
            // placed against the window's corner would drift off the edge.
            // The scene must be marked ACTIVE here. An overlay looks up other
            // entities through Scene::Current() - a radar has to find its
            // contacts - and outside Update nothing has marked one, so that
            // returns null and every overlay silently draws nothing. See the
            // rule on ActiveScene in Scene.h.
            {
                eng::ActiveScene active(m_scene);
                // Opens the window in which the scripting `draw.*` calls work.
                // Outside it they do nothing, so a script drawing from the wrong
                // hook fails quietly instead of scribbling pixel-space shapes
                // into the middle of the 3D pass.
                eng::BeginHudPass();
                for (const eng::Entity& e : m_scene.Entities())
                    for (const auto& c : e.components)
                        c->OnDrawHud(e, m_gameRT.texture.width,
                                     m_gameRT.texture.height);
                eng::EndHudPass();
            }
            EndTextureMode();
        }
    }

    // Draw the gradient sky as a backdrop. Call right after BeginMode3D, while
    // the view/projection are active. Depth writes and back-face culling are off
    // so the cube fills the background without occluding the scene.
    //
    // `nearClip` is the near plane of the camera currently being drawn through.
    // The sky cube surrounds the camera, so it is only visible while its faces
    // sit beyond that plane - a cube smaller than the near distance is entirely
    // clipped and the sky vanishes. Sizing it from the near plane rather than
    // fixing it at some constant is what lets the near plane be raised freely to
    // buy depth precision without the background quietly disappearing.
    //
    // Ten times the near distance is comfortably clear of the front of the
    // frustum while staying far inside any sane far plane. The exact number does
    // not matter: the cube is a device for handing the shader a direction, and
    // with depth writes off it cannot occlude anything at any size.
    void DrawSky(float nearClip) {
        if (!m_skyReady) return;
        const float scale = nearClip * 10.0f;
        SetShaderValue(m_skyShader, m_skyScaleLoc, &scale, SHADER_UNIFORM_FLOAT);
        rlDisableBackfaceCulling();
        rlDisableDepthMask();
        DrawModel(m_sky, {0, 0, 0}, 1.0f, WHITE);
        rlEnableDepthMask();
        rlEnableBackfaceCulling();
    }

    // Called each frame to draw the 3D scene into the engine's viewport texture.
    void OnRenderScene() override {
        // The editor camera's own view distance. Set before BeginMode3D for the
        // same reason as the Game view: the planes are global state that the
        // projection matrix is built from, so each view claims them in turn.
        rlSetClipPlanes(m_viewNear, m_viewFar);
        BeginMode3D(m_camera);            // view the world through the editor camera
        DrawSky(m_viewNear);             // gradient sky behind the world
        DrawGrid(20, 1.0f);              // a 20x20 reference grid on the ground
        m_scene.Draw();
        eng::DrawParticles(m_camera);    // effects, unlit and after the world

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

        // Light gizmo: a sun icon plus an arrow showing which way its rays
        // travel. A directional light has no real position - only a direction
        // matters - so the icon is just a handle you can find and click.
        //
        // Two things make it reliably visible. First, DEPTH TESTING IS TURNED
        // OFF while drawing it, so the icon shows through anything in front of
        // it; an editor handle you cannot see is useless, and a light dropped
        // at the origin would otherwise be buried in the ground grid. Second,
        // the icon is SCALED BY ITS DISTANCE from the camera, so it keeps
        // roughly the same size on screen whether you are next to it or zoomed
        // far out.
        //
        // Note the two flush calls. raylib does not draw simple shapes the
        // moment you ask: it collects them into a batch and sends them to the
        // GPU later, all at once. A depth-test switch, by contrast, takes
        // effect immediately. Without forcing the batch out first, the switch
        // would apply to everything queued rather than to this gizmo alone -
        // and would have been undone again before the gizmo was ever drawn.
        rlDrawRenderBatchActive();   // send everything queued so far, depth on
        rlDisableDepthTest();
        for (eng::Entity& ent : m_scene.Entities()) {
            if (!ent.GetComponent<eng::LightComponent>()) continue;
            Matrix  wm  = m_scene.WorldMatrix(ent, /*ignoreScale=*/true);
            Vector3 pos = {wm.m12, wm.m13, wm.m14};                 // where it sits
            Vector3 dir = Vector3Normalize({-wm.m8, -wm.m9, -wm.m10});  // forward = -Z

            // Keep the icon a readable size at any zoom: grow it with distance,
            // but never smaller than 0.5 units or larger than 6.
            float s = std::clamp(Vector3Distance(pos, m_camera.position) * 0.06f,
                                 0.5f, 6.0f);

            Color col = (ent.id == m_selected) ? Color{255, 240, 150, 255}
                                               : Color{255, 205, 60, 220};

            // The disc of the sun.
            DrawSphereWires(pos, s * 0.35f, 6, 8, col);
            // Eight rays around it, in the plane facing the camera, so it reads
            // as a sun rather than a ball. Each ray runs from just outside the
            // disc to a little further out.
            Vector3 toCam = Vector3Normalize(Vector3Subtract(m_camera.position, pos));
            // Any two vectors perpendicular to toCam span that plane. Crossing
            // with world up gives the first; crossing again gives the second.
            Vector3 axisA = Vector3Normalize(Vector3CrossProduct(toCam, {0, 1, 0}));
            if (Vector3LengthSqr(axisA) < 0.001f)      // looking straight down
                axisA = {1, 0, 0};
            Vector3 axisB = Vector3CrossProduct(toCam, axisA);
            for (int i = 0; i < 8; i++) {
                float a  = (float)i * (PI * 2.0f / 8.0f);
                Vector3 d = Vector3Add(Vector3Scale(axisA, cosf(a)),
                                       Vector3Scale(axisB, sinf(a)));
                DrawLine3D(Vector3Add(pos, Vector3Scale(d, s * 0.5f)),
                           Vector3Add(pos, Vector3Scale(d, s * 0.85f)), col);
            }

            // The direction arrow: a shaft along the light's travel direction
            // with a cone on the end.
            Vector3 tip = Vector3Add(pos, Vector3Scale(dir, s * 3.0f));
            DrawLine3D(pos, tip, col);
            DrawCylinderEx(tip, Vector3Add(tip, Vector3Scale(dir, s * 0.7f)),
                           s * 0.22f, 0.0f, 8, col);
        }

        // Camera gizmo: a body you can find and click, and - for the selected
        // one - the VIEW VOLUME it actually renders, so "what does this camera
        // see" is answered by looking rather than by pressing Play.
        //
        // Every camera gets the icon; only the selected one gets the frustum.
        // Drawing every frustum at once buries the scene in wireframe, and the
        // question is nearly always about one camera at a time.
        for (eng::Entity& ent : m_scene.Entities()) {
            auto* cc = ent.GetComponent<eng::CameraComponent>();
            if (!cc) continue;

            const Matrix  wm  = m_scene.WorldMatrix(ent, /*ignoreScale=*/true);
            const Vector3 pos = {wm.m12, wm.m13, wm.m14};
            const bool    sel = (ent.id == m_selected);
            const Color   col = sel ? Color{120, 230, 255, 255}
                                    : Color{90, 180, 220, 190};

            // Same distance-scaling as the light icon: a handle is only useful
            // if it stays a readable size at any zoom.
            const float s = std::clamp(
                Vector3Distance(pos, m_camera.position) * 0.05f, 0.4f, 5.0f);

            // The body, drawn in the camera's own frame so it points where the
            // camera points. Everything below is in local coordinates, where
            // -Z is forward.
            rlPushMatrix();
            rlMultMatrixf(MatrixToFloat(wm));

            // A boxy body with a lens cone on the front and two reels on top -
            // the shape everyone reads as "camera" at a glance, which is the
            // whole job of an icon.
            DrawCubeWires({0.0f, 0.0f, 0.0f}, s * 1.4f, s * 1.0f, s * 1.6f, col);
            DrawCylinderWiresEx({0.0f, 0.0f, -s * 0.8f}, {0.0f, 0.0f, -s * 1.5f},
                                s * 0.30f, s * 0.5f, 10, col);
            for (int r = 0; r < 2; ++r) {
                const float rz = (r == 0) ? -s * 0.35f : s * 0.45f;
                DrawCylinderWiresEx({0.0f, s * 0.5f,  rz}, {0.0f, s * 0.72f, rz},
                                    s * 0.42f, s * 0.42f, 10, col);
            }

            if (sel) {
                // ---- The view volume -----------------------------------------
                // The aspect ratio is the GAME VIEW's, not the viewport's: this
                // volume has to describe what that camera renders, and the Game
                // panel is what it renders into. Taking the viewport's shape
                // would draw a frustum that quietly disagreed with the picture.
                const float aspect = (m_gameRT.texture.height > 0)
                    ? (float)m_gameRT.texture.width / (float)m_gameRT.texture.height
                    : 16.0f / 9.0f;

                // HOW FAR TO DRAW IT. Not to the far clip: that is 25000 by
                // default, and a frustum drawn to 25 km is four lines vanishing
                // into the distance - which says nothing about the shape of the
                // view and buries everything else. The drawn depth instead
                // follows how far away you are looking from, so the gizmo stays
                // the same useful size on screen at any zoom, and is capped by
                // the real far plane so it can never claim to see further than
                // the camera does.
                const float dist  = Vector3Distance(pos, m_camera.position);
                float       shown = std::clamp(dist * 1.6f, s * 6.0f, cc->farClip);
                const float nearD = std::max(cc->nearClip, 0.01f);
                if (shown <= nearD * 1.5f) shown = nearD * 1.5f;

                // Half-width and half-height of the volume at a given depth.
                // A perspective view spreads with distance - that spread IS the
                // field of view - while an orthographic one is a constant slab,
                // which is exactly the difference the two projections make and
                // the thing this gizmo should show.
                auto extents = [&](float d, float& hw, float& hh) {
                    if (cc->orthographic) hh = cc->orthoSize * 0.5f;
                    else hh = d * std::tan(cc->fovy * 0.5f * DEG2RAD);
                    hw = hh * aspect;
                };

                float nhw, nhh, fhw, fhh;
                extents(nearD, nhw, nhh);
                extents(shown, fhw, fhh);

                // The four corners of each plane, in the camera's own frame.
                const Vector3 nc[4] = {{-nhw,-nhh,-nearD}, { nhw,-nhh,-nearD},
                                       { nhw, nhh,-nearD}, {-nhw, nhh,-nearD}};
                const Vector3 fc[4] = {{-fhw,-fhh,-shown}, { fhw,-fhh,-shown},
                                       { fhw, fhh,-shown}, {-fhw, fhh,-shown}};

                const Color edge = sel ? Color{120, 230, 255, 160}
                                       : Color{90, 180, 220, 90};
                for (int i = 0; i < 4; ++i) {
                    const int j = (i + 1) % 4;
                    DrawLine3D(nc[i], nc[j], edge);   // the near plane
                    DrawLine3D(fc[i], fc[j], edge);   // the far end
                    DrawLine3D(nc[i], fc[i], edge);   // and the four long edges
                }

                // A small upright marker on the top edge of the far end, so the
                // volume's ROLL is visible. Without it a frustum looks the same
                // upside down, and a camera that has rolled over is one of the
                // things you most want a gizmo to reveal.
                const Vector3 topMid = {0.0f, fhh, -shown};
                DrawLine3D(fc[2], topMid, edge);
                DrawLine3D(fc[3], topMid, edge);
            }
            rlPopMatrix();
        }
        rlDrawRenderBatchActive();   // send the gizmo out while depth is still off
        rlEnableDepthTest();

        // Collider gizmos: a green wireframe of the collision volume of every
        // entity that has a Collider component, so the shape can be sized
        // against the model it is meant to cover. Editor-only (this runs for
        // the Viewport, not the Game view).
        for (eng::Entity& ent : m_scene.Entities()) {
            if (auto* col = ent.GetComponent<eng::ColliderComponent>()) {
                Color tone = (ent.id == m_selected) ? Color{0, 240, 120, 200}
                                                    : Color{0, 200, 110, 70};
                // Draw inside the entity's world matrix so the wireframe
                // inherits its position and rotation - a box collider must lean
                // over with the jet, not stay axis-aligned. Scale is excluded,
                // matching the collision maths, which treats collider sizes as
                // world units regardless of the entity's scale.
                rlPushMatrix();
                rlMultMatrixf(MatrixToFloat(m_scene.WorldMatrix(ent, /*ignoreScale=*/true)));
                // A second matrix for the collider's own offset and rotation
                // inside the entity. Each rlMultMatrixf applies INSIDE the ones
                // pushed before it, so this nests correctly: the shape is
                // placed within the entity, and the entity within the world.
                // It is the same matrix the collision test uses, so the
                // wireframe always shows exactly what can be hit.
                rlMultMatrixf(MatrixToFloat(col->LocalMatrix()));
                // Everything below is drawn at the shape's own origin, because
                // the matrix above has already moved and turned the frame.
                const Vector3 o = {0.0f, 0.0f, 0.0f};
                switch (col->shape) {
                    case eng::ColliderShape::Sphere:
                        // (slices, rings) = how many wire lines: enough to read
                        // the shape without cluttering the viewport.
                        DrawSphereWires(o, col->radius, 8, 12, tone);
                        break;
                    case eng::ColliderShape::Box:
                        // DrawCubeWires takes FULL side lengths, while the
                        // collider stores half-extents, hence the doubling.
                        DrawCubeWires(o, col->halfExtents.x * 2.0f,
                                         col->halfExtents.y * 2.0f,
                                         col->halfExtents.z * 2.0f, tone);
                        break;
                    case eng::ColliderShape::Capsule: {
                        // A capsule is defined by the two CENTRES of its end
                        // caps; they sit half the straight height above and
                        // below the shape's centre, along its own Y axis.
                        float   half = col->height * 0.5f;
                        Vector3 top    = {0.0f,  half, 0.0f};
                        Vector3 bottom = {0.0f, -half, 0.0f};
                        DrawCapsuleWires(bottom, top, col->radius, 8, 6, tone);
                        break;
                    }
                    case eng::ColliderShape::Heightfield:
                        // Nothing is drawn: the terrain mesh already shows
                        // exactly where this collider is, and outlining a
                        // whole landscape in wireframe would bury the scene.
                        // Turn on the Terrain component's contour lines to see
                        // the surface being collided against.
                        break;
                }
                rlPopMatrix();
            }
        }
        EndMode3D();
    }

    // Called each frame to draw all the editor panels.
    void OnRenderUI() override {
        // Handle any "new graph" / "edit graph" the Inspector recorded, before
        // the Node Editor draws, so the change is visible this frame.
        HandleGraphComponentRequests();

        DrawToolbarPanel();
        DrawViewportPanel();
        DrawGamePanel();
        DrawHierarchyPanel();
        DrawInspectorPanel();
        DrawNodeEditorPanel();
        DrawScriptApiPanel();
    }

    // A searchable reference for everything a script can call.
    //
    // The list is not written here: it comes from the catalogue each binding
    // file builds beside its own registrations (LuaApiRegistry). That is the
    // whole reason the catalogue exists - a list of calls maintained separately
    // from the calls themselves goes stale the first time somebody adds one,
    // and this project has watched exactly that happen to a hand-written API
    // section in its notes.
    void DrawScriptApiPanel() {
        ImGui::Begin("Script API");

        const auto& entries = eng::GetLuaApiEntries();

        // Filter first, and by SUBSTRING rather than prefix. Someone hunting
        // for the call that fires a bullet types "spawn", not "Scene." - the
        // useful search is on what you remember, which is rarely the namespace.
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##apifilter", "filter, e.g. spawn / force / draw",
                                 m_apiFilter, sizeof(m_apiFilter));

        std::string needle = m_apiFilter;
        for (char& c : needle) c = (char)tolower((unsigned char)c);

        int shown = 0;
        ImGui::BeginChild("##apilist", ImVec2(0, 0), false);
        for (const eng::LuaApiEntry& e : entries) {
            if (!needle.empty()) {
                std::string hay = e.signature + " " + e.description;
                for (char& c : hay) c = (char)tolower((unsigned char)c);
                if (hay.find(needle) == std::string::npos) continue;
            }
            ++shown;

            // The signature is the clickable part. Clicking copies the
            // insertable NAME rather than the whole signature, because that is
            // what you paste into a script - the argument list is documentation,
            // not something you want in your editor.
            if (ImGui::Selectable(e.signature.c_str()))
                ImGui::SetClipboardText(e.name.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n\nClick to copy \"%s\"",
                                  e.description.c_str(), e.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", e.description.c_str());
        }

        // Say so rather than showing an empty panel, which reads as the list
        // having failed to load rather than the filter having excluded it all.
        if (shown == 0)
            ImGui::TextDisabled("nothing matches \"%s\"", m_apiFilter);

        ImGui::EndChild();
        ImGui::End();
    }

private:
    // Return the first entity that has a CameraComponent, or nullptr if none.
    eng::Entity* FindCameraEntity() {
        for (auto& e : m_scene.Entities())
            if (e.GetComponent<eng::CameraComponent>()) return &e;
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
        eng::ClearHudValues();        // start each run fresh (score 0, no stale values)
        eng::ClearParticles();        // and with no effects left over from before
        eng::StopAllAudio();          // nor any sound still ringing from before
        eng::ResetPhysics();          // and with an empty simulation, not last run's
        // Re-read the effect and sound definitions, so retuning either is a
        // matter of editing its file and pressing Play again.
        eng::ReloadEffectPresets();
        eng::ReloadSoundDefs();
        eng::ReloadModelDefs();       // and the model set-ups spawns refer to
        // Read every model file now. Loading one costs several megabytes off
        // disk, and paying that here - where a pause is expected - keeps a wave
        // of enemies from stalling the game the moment it appears.
        eng::PreloadModelDefs();
        m_scene.Start();              // run every script's on_start
        m_playing = true;
    }

    void StopPlay() {
        m_scene.Entities() = std::move(m_backup);   // restore the saved scene
        eng::ClearParticles();        // effects are runtime state, like the HUD
        // A looping engine note must not outlive the run that started it.
        eng::StopAllAudio();
        // The simulation is runtime state too: the bodies it holds describe
        // entities that have just been replaced by the restored originals.
        eng::ResetPhysics();
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

        // Mute. The editor is launched dozens of times an hour, and an engine
        // note every single time is unbearable, so this earns its place in the
        // toolbar rather than hiding in a settings panel.
        ImGui::SameLine();
        bool muted = eng::IsMuted();
        if (ImGui::Button(muted ? "Unmute" : "Mute")) eng::SetMuted(!muted);

        // The editor camera's view distance, behind a small popup so it does not
        // eat a toolbar row it does not need. This pair governs the Viewport
        // only; the Game view reads the scene camera's own Clipping Planes in
        // the Inspector, and the two are meant to be set independently.
        ImGui::SameLine();
        if (ImGui::Button("View")) ImGui::OpenPopup("view_distance");
        if (ImGui::BeginPopup("view_distance")) {
            ImGui::TextDisabled("Editor camera clipping planes");
            ImGui::SetNextItemWidth(160.0f);
            ImGui::DragFloat("Near", &m_viewNear, 0.01f, 0.01f, 100.0f, "%.2f");
            ImGui::SetNextItemWidth(160.0f);
            ImGui::DragFloat("Far",  &m_viewFar,  10.0f,  1.0f,  200000.0f, "%.0f",
                             ImGuiSliderFlags_Logarithmic);
            if (m_viewNear >= m_viewFar) m_viewNear = m_viewFar * 0.5f;
            ImGui::TextDisabled("far/near %.0f:1", m_viewFar / m_viewNear);
            // A one-click way back, because these are easy to drag somewhere
            // useless and there is no undo on a toolbar popup.
            if (ImGui::Button("Reset")) { m_viewNear = 0.3f; m_viewFar = 25000.0f; }
            ImGui::EndPopup();
        }

        DrawStats();
        ImGui::End();                                // end the panel
    }

    // A performance readout. Guessing why a frame is slow is unreliable, so
    // the editor shows the things that actually drive the cost:
    //
    //  * FPS and MS - milliseconds per frame is the honest number. FPS is
    //    non-linear (60 to 55 fps is a far smaller change than 20 to 15), while
    //    milliseconds add up the way work does. The frame budget is 16.7 ms for
    //    60 frames per second.
    //  * TRIS - triangles in the scene, counted once. The scene is drawn once
    //    per visible view, so the real load is this times the number of views.
    //  * VIEWS - how many times the whole scene is being drawn this frame: the
    //    Viewport, plus the Game view when that panel is actually on screen.
    //  * ENTS - how many entities exist, which drives the per-frame script and
    //    transform work rather than the drawing.
    void DrawStats() {
        // Count triangles across the scene. Loaded models and terrain dominate;
        // the simple primitives are a dozen triangles each and are counted as a
        // separate number so a big mesh cannot hide behind them.
        int meshTris = 0, primitives = 0;
        for (const eng::Entity& e : m_scene.Entities()) {
            for (const auto& c : e.components) {
                if (auto* m = dynamic_cast<eng::ModelComponent*>(c.get()))
                    meshTris += m->TriangleCount();
                else if (auto* t = dynamic_cast<eng::TerrainComponent*>(c.get()))
                    meshTris += t->TriangleCount();
                else if (dynamic_cast<eng::ShapeComponent*>(c.get()))
                    primitives++;
            }
        }

        const float ms    = GetFrameTime() * 1000.0f;
        const int   views = 1 + (m_gameVisible && FindCameraEntity() ? 1 : 0);

        // Drawn on the same row as the toolbar buttons: the panel is often
        // docked only one row tall, and anything below the buttons would be
        // clipped away where it helps nobody.
        ImGui::SameLine();
        // Colour the frame time by the 60 fps budget: green inside it, amber
        // when it slips, red once a frame costs more than twice the budget.
        ImVec4 tone = (ms <= 16.7f) ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f)
                    : (ms <= 33.3f) ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
                                    : ImVec4(1.0f, 0.5f, 0.4f, 1.0f);
        // Only the frame time goes on the row itself. The toolbar is often docked
        // narrow, and a long line of counts is simply clipped away where it helps
        // nobody; the detail lives on the tooltip instead.
        ImGui::TextColored(tone, "%dfps %.1fms", GetFPS(), ms);
        // The full breakdown on hover, where there is room for words. This must
        // follow the frame time immediately: IsItemHovered always refers to the
        // widget drawn just before it, so anything inserted between the two
        // would quietly steal the tooltip.
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d mesh triangles, drawn %d time(s) per frame\n"
                              "%d primitive shapes\n%d entities\n"
                              "%d live effect particles\n%d sounds playing\n"
                              "%d physics bodies, %.2f ms simulating them\n\n"
                              "Views = Viewport, plus the Game panel when visible.\n"
                              "16.7 ms is the budget for 60 fps.",
                              meshTris, views, primitives,
                              (int)m_scene.Entities().size(),
                              eng::AliveParticleCount(),
                              eng::PlayingVoiceCount(),
                              eng::PhysicsBodyCount(),
                              eng::PhysicsStepMs());

        // If effects.lua could not be read, say so rather than silently falling
        // back to the built-in effects and leaving the developer wondering why
        // an edit had no result.
        const char* fxErr = eng::EffectPresetError();
        if (fxErr && fxErr[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "fx?");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("effects.lua: %s\n\nUsing the built-in effects.",
                                  fxErr);
        }

        // The same for audio. A missing sound file is silent by design, and
        // unexplained silence is the kind of thing that costs an hour of
        // hunting, so the names are listed here instead.
        const char* sndErr  = eng::SoundDefError();
        const auto& missing = eng::MissingSoundFiles();
        if ((sndErr && sndErr[0] != '\0') || !missing.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "snd?");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                if (sndErr && sndErr[0] != '\0')
                    ImGui::Text("sounds.lua: %s", sndErr);
                if (!missing.empty()) {
                    ImGui::Text("Defined but the file is missing (so silent):");
                    for (const std::string& m : missing)
                        ImGui::BulletText("%s", m.c_str());
                    ImGui::Separator();
                    ImGui::TextDisabled("Drop the files into assets/sounds/ and");
                    ImGui::TextDisabled("press Play; no code change is needed.");
                }
                ImGui::EndTooltip();
            }
        }

        // models.lua, same idea. A model definition that failed to load means a
        // spawned enemy silently arrives as a plain cube, which reads as "the
        // spawner is broken" rather than "the file has a syntax error in it".
        const char* mdlErr = eng::ModelDefError();
        if (mdlErr && mdlErr[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "mdl?");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("models.lua: %s\n\n"
                                  "Anything spawned by name will be a cube until this is fixed.",
                                  mdlErr);
        }

        // And the simulation. This one matters most of all: if the physics
        // world failed to start, every rigid body in the scene is inert and
        // nothing collides with anything. Without a badge that looks exactly
        // like a gameplay bug, and it is the last place anyone would think to
        // look.
        const char* physErr = eng::PhysicsError();
        if (physErr && physErr[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "phys!");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Physics: %s\n\n"
                                  "Nothing is being simulated: bodies will not fall or collide.",
                                  physErr);
        }
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

    // Act on the buttons a Graph component's Inspector offers. The component
    // cannot open the node editor or create a graph file itself - both live
    // here - so it records that the user asked and this notices.
    //
    // Runs before the panels are drawn, so a request made last frame is handled
    // before the Node Editor draws with the new graph.
    void HandleGraphComponentRequests() {
        for (eng::Entity& e : m_scene.Entities()) {
            for (auto& comp : e.components) {
                auto* gc = dynamic_cast<eng::GraphComponent*>(comp.get());
                if (!gc) continue;

                if (gc->newRequested) {
                    gc->newRequested = false;
                    // Ask where it goes straight away. A graph with no file
                    // cannot be compiled on Play, so leaving it nameless would
                    // only postpone the problem. The entity's name is a sensible
                    // default filename.
                    std::string suggested = e.name + "_graph.json";
                    std::string p = eng::SaveFileDialog(kGraphFilter, "json",
                                                        suggested.c_str());
                    if (!p.empty()) {
                        m_graph.Reset();            // an empty graph: just the events
                        if (m_graph.Save(p)) {
                            m_graphPath    = p;
                            m_graphOwner   = e.id;
                            gc->graphPath  = p;
                        }
                    }
                }

                if (gc->editRequested) {
                    gc->editRequested = false;
                    if (!gc->graphPath.empty() && m_graph.Load(gc->graphPath)) {
                        m_graphPath  = gc->graphPath;
                        m_graphOwner = e.id;
                    }
                }
            }
        }
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
            m_graphOwner = eng::kInvalidEntity;   // no longer any entity's graph
        }
        ImGui::SameLine();
        if (ImGui::Button("Open")) {
            std::string p = eng::OpenFileDialog(kGraphFilter, "json");
            if (!p.empty() && m_graph.Load(p)) {
                m_graphPath  = p;
                m_graphOwner = eng::kInvalidEntity;
            }
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
        // Show the open file's name, or a placeholder if unsaved - and say which
        // entity's graph this is, so it is never a mystery whose behaviour is
        // being edited.
        ImGui::TextDisabled("%s", m_graphPath.empty() ? "(unsaved graph)"
                                                      : m_graphPath.c_str());
        if (const eng::Entity* owner = m_scene.FindConst(m_graphOwner)) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "[%s]", owner->name.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("This is the graph on the entity \"%s\".\n"
                                  "Save it, then press Play to run the changes.",
                                  owner->name.c_str());
        }

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
        const ImVec2 imageTopLeft = ImGui::GetCursorScreenPos();
        rlImGuiImageRenderTexture(&m_viewport);   // draws the texture (Y-flipped for OpenGL)

        // While flying, show how fast. The wheel changes the speed rather than
        // the zoom in that mode, and a control with no readout is a control
        // nobody finds - you would turn the wheel, see the view not zoom, and
        // conclude it was broken rather than that it had done something else.
        //
        // Drawn over the image rather than beside it, so it costs no layout and
        // vanishes with the mode it describes.
        if (m_flyLock) {
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(imageTopLeft.x + 10.0f, imageTopLeft.y + 8.0f),
                IM_COL32(150, 235, 255, 230),
                TextFormat("%.0f m/s   wheel: speed   shift: faster   ctrl: slower",
                           m_flySpeed));
        }
        ImGui::End();
    }

    // The Game view panel: what the player sees, rendered through the scene's
    // camera entity. Also tracks whether the game "owns" the keyboard.
    void DrawGamePanel() {
        // ImGui::Begin returns false when the panel cannot be seen at all -
        // collapsed, or sitting behind another tab in the same dock. Recording
        // that lets the next frame skip rendering the game view entirely: the
        // scene is expensive to draw, and drawing it into a texture nobody is
        // looking at is pure waste.
        m_gameVisible = ImGui::Begin("Game");
        m_gameActive  = ImGui::IsWindowFocused() || ImGui::IsWindowHovered();
        // Skip the contents when the panel is hidden. End() below is still
        // called either way: ImGui requires every Begin to be matched.
        if (!m_gameVisible) {
            ImGui::End();
            return;
        }

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

            // A component can only see itself, so anything that depends on
            // ANOTHER component on the same entity has to be checked here,
            // where the whole entity is in view. A Heightfield collider reads
            // its shape from a Terrain component; without one it silently
            // contributes no collision at all, which looks exactly like
            // physics being broken. Say so instead.
            if (open && !removed) {
                auto* col = dynamic_cast<eng::ColliderComponent*>(c);
                if (col && col->shape == eng::ColliderShape::Heightfield &&
                    !e->GetComponent<eng::TerrainComponent>())
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
                                       "No Terrain component on this entity,\n"
                                       "so this collider has no shape.");
            }

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
            if (ImGui::MenuItem("Graph"))    // behaviour built as a node graph
                e->AddComponent<eng::GraphComponent>();
            bool hasCam = e->GetComponent<eng::CameraComponent>() != nullptr;
            if (ImGui::MenuItem("Camera", nullptr, false, !hasCam))
                e->AddComponent<eng::CameraComponent>();
            bool hasHealth = e->GetComponent<eng::HealthComponent>() != nullptr;
            if (ImGui::MenuItem("Health", nullptr, false, !hasHealth))
                e->AddComponent<eng::HealthComponent>();
            bool hasCollider = e->GetComponent<eng::ColliderComponent>() != nullptr;
            if (ImGui::MenuItem("Collider", nullptr, false, !hasCollider))
                e->AddComponent<eng::ColliderComponent>();
            bool hasRb = e->GetComponent<eng::RigidBodyComponent>() != nullptr;
            if (ImGui::MenuItem("Rigid Body", nullptr, false, !hasRb)) {
                e->AddComponent<eng::RigidBodyComponent>();
                // A rigid body is useless without a shape to occupy, and an
                // entity that has just been given one almost always wants a
                // default collider rather than an error. Only added when there
                // is none: an authored collider must never be replaced.
                if (!e->GetComponent<eng::ColliderComponent>())
                    e->AddComponent<eng::ColliderComponent>();
            }
            bool hasLight = e->GetComponent<eng::LightComponent>() != nullptr;
            if (ImGui::MenuItem("Light", nullptr, false, !hasLight)) {
                e->AddComponent<eng::LightComponent>();
                // A light points along its entity's forward axis, and a brand
                // new entity is unrotated - which aims the sun horizontally,
                // straight along -Z, lighting the world from the side and
                // leaving the ground unlit. An unrotated light is almost never
                // what anyone wants, so tilt it down to a natural afternoon
                // angle. An entity that has already been rotated deliberately
                // is left exactly as the user aimed it.
                Quaternion identity{0.0f, 0.0f, 0.0f, 1.0f};
                if (Vector4Equals(e->transform.rotation, identity))
                    e->transform.rotation = QuaternionFromVector3ToVector3(
                        {0.0f, 0.0f, -1.0f},
                        Vector3Normalize({0.35f, -0.85f, -0.4f}));
            }
            bool hasModel = e->GetComponent<eng::ModelComponent>() != nullptr;
            if (ImGui::MenuItem("Model", nullptr, false, !hasModel))
                e->AddComponent<eng::ModelComponent>();
            bool hasTerrain = e->GetComponent<eng::TerrainComponent>() != nullptr;
            if (ImGui::MenuItem("Terrain", nullptr, false, !hasTerrain))
                e->AddComponent<eng::TerrainComponent>();
            bool hasMinimap = e->GetComponent<eng::MinimapComponent>() != nullptr;
            if (ImGui::MenuItem("Minimap", nullptr, false, !hasMinimap))
                e->AddComponent<eng::MinimapComponent>();
            bool hasFdm = e->GetComponent<eng::JSBSimComponent>() != nullptr;
            if (ImGui::MenuItem("JSBSim Flight Model", nullptr, false, !hasFdm))
                e->AddComponent<eng::JSBSimComponent>();
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
    std::deque<eng::Entity>  m_backup;   // matches Scene::Entities(); see the note on m_entities                  // saved scene while playing
    bool          m_playing  = false;                   // are we in play mode?
    eng::EntityID m_selected = eng::kInvalidEntity;     // the selected entity (or none)
    Camera3D      m_camera{};                            // the editor's orbit camera
    // Orbit camera angles/distance (initial values give a nice 3/4 view).
    float         m_camYaw   = 0.785f;                  // ~45 degrees, in radians
    float         m_camPitch = 0.615f;
    float         m_camDist  = 13.9f;

    // How fast right-drag flying moves, in metres per second, adjusted with the
    // wheel while flying and remembered afterwards.
    //
    // It is its own number rather than being derived from the orbit distance,
    // which is what it used to be. Tying the two together meant the only way to
    // cross ground quickly was to zoom out first, and then you arrived unable to
    // see anything closely without becoming slow again. In a world 40 km across
    // that made simply GETTING to an enemy camp a chore. Unity keeps them
    // separate for the same reason.
    float         m_flySpeed = 60.0f;
    bool          m_viewportHovered = false;            // mouse over the viewport panel?
    bool          m_flyLock   = false;                  // right-drag fly in progress?
    bool          m_gameActive = false;                 // Game panel focused/hovered?
    // Is the Game panel actually on screen? Set while drawing the UI and read
    // by the next frame's render, which skips the game view when it is hidden.
    // Starts true so the first frame renders before any UI has been drawn.
    bool          m_gameVisible = true;

    // The euler-angle edit buffer for the Inspector's Rotation field, and the
    // id of the entity it currently reflects.
    Vector3       m_rotEuler{0, 0, 0};
    eng::EntityID m_rotEulerFor = eng::kInvalidEntity;

    ed::EditorContext* m_nodeCtx = nullptr;             // node-editor canvas state
    edtr::ScriptGraph  m_graph;                         // the graph being edited
    std::string        m_graphPath;                     // its file ("" if unsaved)
    // The entity whose Graph component this canvas is editing, or kInvalidEntity
    // when editing a graph file on its own (the original behaviour). Only used
    // to label the panel, so nothing breaks if that entity is deleted.
    eng::EntityID      m_graphOwner = eng::kInvalidEntity;
    RenderTexture2D    m_gameRT{};                       // the Game view's texture

    char m_apiFilter[64] = "";  // Script API panel search box

    Shader m_skyShader{};      // procedural gradient skybox shader
    Model  m_sky{};            // the unit cube it draws on
    bool   m_skyReady = false; // false if the shader failed to compile
    int    m_skyScaleLoc = -1; // cached location of the shader's skyScale uniform

    // Clipping planes for the EDITOR's own fly camera. The scene camera carries
    // its own pair on its CameraComponent, because a game view's view distance
    // is a property of the game; these are a workshop setting that belongs to
    // the person flying around the scene, so they are deliberately separate and
    // are not saved into the scene file.
    float m_viewNear = 0.3f;
    float m_viewFar  = 25000.0f;

};

// The program's entry point.
int main() {
    // Make every relative path (like "assets/scripts/flight_sim.lua") resolve
    // against the project folder, regardless of where the built .exe sits.
    // PROJECT_ROOT_DIR is a string baked in at build time by CMake.
    std::filesystem::current_path(PROJECT_ROOT_DIR);

    EditorApp app(1280, 720, "MyEngine Editor");
    app.Run();          // run the main loop until the window is closed
    return 0;
}
