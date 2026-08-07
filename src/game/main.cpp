// game/main.cpp
// =============================================================================
// THE STANDALONE GAME RUNTIME -- the .exe a player runs.
//
// The editor and this program are two executables built from ONE engine
// library. The editor is a workshop: it shows panels, lets a scene be arranged,
// and runs the world only while Play is held down. This is the finished
// article: it opens a window, loads one scene, starts it, and never stops until
// the player quits. There is no ImGui here, no viewport texture, no selection
// outline and no grid -- a player is not editing anything.
//
// WHY IT IS A SEPARATE EXECUTABLE RATHER THAN THE EDITOR IN A "GAME MODE".
// A shipped game should not contain the editor at all. Keeping them apart means
// the game does not link the node-graph canvas, does not carry the file
// dialogs, and cannot accidentally depend on something only the editor sets up.
// It also makes the engine's boundary honest: anything this file needs and
// cannot reach is something that was wrongly living in the editor.
//
// WHAT IT DELIBERATELY SHARES WITH THE EDITOR'S PLAY MODE. The order of a frame
// is a contract, not a detail, and this file reproduces the editor's exactly:
// lighting, then particles, then the audio listener, then scripts, then the
// rigid-body step. Anything that drifts between the two shows up as a game that
// behaves differently from what was tested in the editor, which is the single
// worst class of bug a tool like this can have.
//
// HOW IT FINDS ITS FILES. The editor bakes the project folder in at build time
// and works directly on the source `assets/`, so editing a script and pressing
// Play picks it up. A shipped game has no project folder: it uses the `assets`
// directory sitting NEXT TO THE EXE, and the working directory is pointed there
// on the first line of main so that double-clicking the exe from anywhere --
// Explorer, a shortcut, a different drive -- resolves the same paths.
// =============================================================================

#include "raylib.h"
#include "raymath.h"    // Vector3Transform, for placing the audio listener
#include "rlgl.h"       // rlSetClipPlanes and the depth/cull toggles the sky needs

#include "engine/Scene.h"
#include "engine/Components.h"
#include "engine/Particles.h"
#include "engine/Audio.h"
#include "engine/Physics.h"
#include "engine/Lighting.h"
#include "engine/ModelDefs.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// What to launch, and how.
//
// These live in a small `assets/game.json` rather than being compiled in, so
// which scene ships and what the window is called can be changed without a
// rebuild -- the equivalent of Unity's Build Settings and Player Settings. Every
// field has a default, so a missing or partial file still produces a running
// game rather than an error.
// -----------------------------------------------------------------------------
struct GameConfig {
    std::string scene      = "assets/scenes/JSBFlight.json";
    std::string title      = "Jet Combat";
    int         width      = 1280;
    int         height     = 720;
    bool        fullscreen = false;
    int         targetFps  = 60;
};

static GameConfig LoadConfig(const char* path) {
    GameConfig cfg;

    std::ifstream in(path);
    if (!in) return cfg;      // no file at all: the defaults above are the game

    // A malformed config must not stop the game starting. A player cannot fix a
    // JSON syntax error, so the sane failure is to run with the defaults; the
    // message goes to the console for whoever is building the thing.
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        TraceLog(LOG_WARNING, "game.json could not be read (%s) - using defaults",
                 e.what());
        return cfg;
    }

    // value() returns the stored entry or the fallback, so each key is
    // independently optional rather than the file having to be complete.
    cfg.scene      = j.value("scene",      cfg.scene);
    cfg.title      = j.value("title",      cfg.title);
    cfg.width      = j.value("width",      cfg.width);
    cfg.height     = j.value("height",     cfg.height);
    cfg.fullscreen = j.value("fullscreen", cfg.fullscreen);
    cfg.targetFps  = j.value("targetFps",  cfg.targetFps);
    return cfg;
}

// -----------------------------------------------------------------------------
// WHAT THE PROGRAM IS DOING RIGHT NOW.
//
// The menu is a state of the RUNTIME rather than something inside the scene, and
// that is the whole reason it works simply. A menu drawn by a script would need
// the world to be running in order to draw it - so the aircraft would already be
// flying, burning fuel and being shot at behind the title. Keeping the state out
// here means the scene is loaded but untouched until Start is chosen, and the
// menu can show it as a still backdrop without it being a game in progress.
//
// GameOver is deliberately NOT a state. Whether the run has ended is a fact the
// gameplay owns - gamemanager.lua sets the `game_over` HUD value and hud.lua
// draws the screen for it - so asking that value is the single source of truth.
// A second copy of it out here could disagree with the one on screen.
// -----------------------------------------------------------------------------
enum class State {
    Menu,        // the title screen, nothing running
    Controls,    // the key list, reachable from the menu and from a pause
    Playing,     // the world is advancing (this covers the game-over freeze too)
    Paused,      // mid-run, world frozen, menu over the top
};

// -----------------------------------------------------------------------------
// The runtime itself.
// -----------------------------------------------------------------------------
class GameApp {
public:
    explicit GameApp(GameConfig cfg) : m_cfg(std::move(cfg)) {}

    // Bring up the window and every subsystem, in the order the editor uses.
    // Returns false if something the game cannot run without is missing, so
    // main can report it rather than opening a window onto an empty world.
    bool Startup() {
        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(m_cfg.width, m_cfg.height, m_cfg.title.c_str());
        // Fullscreen is toggled AFTER the window exists. raylib borrows the
        // monitor's current mode when it switches, so asking before there is a
        // window to switch has nothing to measure.
        if (m_cfg.fullscreen) ToggleFullscreen();
        SetTargetFPS(m_cfg.targetFps);

        // ESCAPE MUST STOP CLOSING THE WINDOW. raylib treats it as the exit key
        // by default, which was right when the game had no menu and wrong the
        // moment it has one: Escape now pauses, and quitting is a decision made
        // in the menu. Without this the first press of Escape ends the program
        // instead of opening the pause screen.
        SetExitKey(KEY_NULL);

        // These three are GPU objects and need the window's OpenGL context, so
        // none of them can be created before InitWindow above.
        eng::InitLighting();
        eng::InitParticles();

        // Audio failing is not fatal: the game is playable in silence, and a
        // machine with no sound device is a machine that should still run it.
        eng::InitAudio();

        // The rigid-body world needs no window - physics never touches the GPU -
        // but it is started here with the rest so there is one place that brings
        // everything up.
        eng::InitPhysics();

        LoadSky();

        if (!m_scene.Load(m_cfg.scene)) {
            TraceLog(LOG_ERROR, "could not load scene '%s'", m_cfg.scene.c_str());
            return false;
        }

        // A game has no editor stealing the keyboard for text boxes or a fly
        // camera, so scripts read input for the whole life of the program. The
        // editor has to keep opening and closing this; here it is opened once.
        eng::SetScriptInputEnabled(true);

        // Read the data files and pull every model off disk BEFORE the menu
        // appears. Two reasons, and the second is the one that matters: the menu
        // shows the scene as a backdrop, and a model that has not loaded yet
        // draws as a fallback primitive - so without this the title screen is a
        // field of grey boxes that turn into aircraft the moment you press
        // Start. It also puts the several megabytes of loading in the one place
        // where waiting is expected.
        PrepareAssets();

        m_state = State::Menu;
        return true;
    }

    void Shutdown() {
        if (m_skyReady) { UnloadModel(m_sky); UnloadShader(m_skyShader); }
        eng::ShutdownLighting();
        eng::ShutdownParticles();
        eng::ShutdownAudio();
        eng::ShutdownPhysics();
        // Model files are shared between entities and outlive any one of them,
        // so they are released here rather than by whatever drew them last.
        eng::ClearModelCache();
        CloseWindow();
    }

    // The main loop. WindowShouldClose() covers the window's close button; Escape
    // no longer ends the program (see SetExitKey above), so quitting from the
    // menu sets its own flag - which is also the only way out of a fullscreen
    // game, where there is no close button to click.
    void Run() {
        while (!WindowShouldClose() && !m_quit) {
            Update(GetFrameTime());
            Render();
        }
    }

private:
    // -- starting and restarting a run ---------------------------------------
    // The authored scene is deep-copied before anything runs, and a restart puts
    // that copy back. This is the editor's Play/Stop snapshot, for the same
    // reason: scripts change the live scene as they run -- spawning enemies,
    // destroying camps, moving the aircraft -- so the only way to begin again is
    // to have kept what it looked like before any of that happened.
    // Re-read the data files and pull every model off disk. Called once before
    // the menu appears and again on each run, so retuning an effect is a matter
    // of editing its file and starting a new run. Loading a model is cached, so
    // every call after the first is nearly free.
    void PrepareAssets() {
        eng::ReloadEffectPresets();
        eng::ReloadSoundDefs();
        eng::ReloadModelDefs();
        eng::PreloadModelDefs();
    }

    // Put the authored scene back and drop everything the run built up, so the
    // menu's backdrop is the world as it was designed rather than the wreckage
    // the last run left, and so the next Start begins from the same place the
    // first one did.
    void ReturnToMenu() {
        if (!m_backup.empty()) {
            m_scene.Entities().clear();
            for (const eng::Entity& e : m_backup)
                m_scene.Entities().push_back(e.Clone());
        }
        eng::ClearHudValues();
        eng::ClearParticles();
        eng::StopAllAudio();
        eng::ResetPhysics();

        m_started = false;
        m_state   = State::Menu;
        m_sel     = 0;
    }

    void StartRun() {
        if (m_backup.empty()) {
            for (const eng::Entity& e : m_scene.Entities())
                m_backup.push_back(e.Clone());
        } else {
            // A restart: throw away the played-in scene and put the saved one
            // back, then re-copy it so the NEXT restart has a clean original
            // too (Start() is about to run scripts over what we just restored).
            m_scene.Entities().clear();
            for (const eng::Entity& e : m_backup)
                m_scene.Entities().push_back(e.Clone());
        }

        // Everything that is RUNTIME state rather than authored state has to go,
        // or the new run inherits the old one's score, smoke and rigid bodies.
        eng::ClearHudValues();
        eng::ClearParticles();
        eng::StopAllAudio();
        eng::ResetPhysics();

        PrepareAssets();

        m_scene.Start();
        ReportScriptErrors();

        m_started = true;
        m_state   = State::Playing;
    }

    // Say out loud which scripts are broken.
    //
    // A FAILING SCRIPT DOES NOT THROW. The error is caught and stored on the
    // component that owns it, so a scene whose scripts are all failing runs
    // perfectly happily and simply does nothing -- no crash, no message, an
    // aircraft that will not fly and no clue why. The editor surfaces this as a
    // badge on the component; a shipped game has no Inspector to look at, so the
    // equivalent is to write it to the log where whoever built it can find it.
    //
    // Checked after Start rather than continuously: `onStart` is where the bulk
    // of setup failures happen, and polling every entity every frame to read
    // error strings would cost more than it is worth.
    void ReportScriptErrors() {
        for (const eng::Entity& e : m_scene.Entities()) {
            for (const auto& c : e.components) {
                const auto* s = dynamic_cast<const eng::ScriptComponent*>(c.get());
                if (!s) continue;
                const char* err = s->ErrorText();
                if (err && *err)
                    TraceLog(LOG_WARNING, "script error on '%s': %s",
                             e.name.c_str(), err);
            }
        }
    }

    // -- one frame of simulation ---------------------------------------------
    void Update(float dt) {
        // The sun first, so moving a light entity shades the same frame it moved.
        ApplySceneLight();

        // Effects age whether or not the world is advancing, so a burst thrown
        // out on the last frame before a game over finishes gracefully instead
        // of hanging in the air.
        eng::UpdateParticles(dt);

        // The listener is placed BEFORE anything can play a sound this frame.
        // Positioned audio is judged against where the listener is, and at
        // several hundred metres a second, last frame's position is a long way
        // from this one's.
        UpdateAudioListener();

        // Looping sounds decode as they play and hold only a small buffer, so
        // they must be topped up every frame or they stutter and stop.
        eng::UpdateAudio();

        // SCRIPTS ONLY GET THE KEYBOARD WHILE THE WORLD IS RUNNING. Without
        // this the aircraft is being flown behind the pause screen: the scripts
        // are not updating, but the moment play resumes the controls have
        // whatever was pressed while navigating the menu, and the throttle in
        // particular ramps the whole time Shift is held.
        eng::SetScriptInputEnabled(m_state == State::Playing);

        switch (m_state) {
            case State::Menu:     UpdateMenu();     return;
            case State::Controls: UpdateControls(); return;
            case State::Paused:   UpdatePaused();   return;
            case State::Playing:  break;
        }

        // Once a script sets "game_over" the world freezes -- scripts stop
        // advancing, so the wreck stays where it fell. hud.lua draws the screen
        // and its prompts; the two keys it names are handled here.
        if (eng::GetHudValue("game_over", 0.0f) > 0.0f) {
            if (IsKeyPressed(KEY_R))      StartRun();
            if (IsKeyPressed(KEY_ESCAPE)) ReturnToMenu();
            return;
        }

        // Escape pauses. Checked before the world advances so the frame the key
        // is pressed is not also a frame of flying.
        if (IsKeyPressed(KEY_ESCAPE)) {
            m_state = State::Paused;
            m_sel   = 0;
            return;
        }

        m_scene.Update(dt);

        // The rigid-body step runs AFTER the scripts. The order is the whole
        // contract between them: a script decides what forces to apply this
        // frame, and the simulation works out the motion those forces produce.
        // Stepping first would act on last frame's decisions and leave the
        // controls feeling a frame late.
        eng::UpdatePhysics(m_scene, dt);
    }

    // -- the menus -------------------------------------------------------------

    // Move the highlight and report whether this frame's key was "choose".
    //
    // `count` is passed rather than read from a member because each screen has a
    // different number of items, and wrapping at the wrong count either skips an
    // entry or selects one that is not drawn.
    // W AND S MOVE THE HIGHLIGHT AS WELL AS THE ARROWS, and that is not padding:
    // W and S are the pitch axis in the air, so a player's hand is already there
    // and reaching for the arrows to choose a menu item is a context switch for
    // no reason. Accepting both also means the menu still works for anyone whose
    // arrow keys arrive as numpad keys, which is what happens when the extended-
    // key flag is missing from synthesised input.
    bool MenuInput(int count) {
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
            m_sel = (m_sel + 1) % count;
        // Adding count before the modulo keeps the result positive: in C++ the
        // remainder of a negative number is negative, so moving up from the top
        // item would otherwise land on index -1.
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
            m_sel = (m_sel + count - 1) % count;
        return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
               IsKeyPressed(KEY_SPACE);
    }

    void UpdateMenu() {
        const bool chose = MenuInput(3);
        if (!chose) return;
        switch (m_sel) {
            case 0: StartRun();                break;   // Start
            case 1: OpenControls(State::Menu); break;
            case 2: m_quit = true;             break;
        }
    }

    void UpdatePaused() {
        // Escape backs out of a pause, which is what pressing it again should
        // obviously do.
        if (IsKeyPressed(KEY_ESCAPE)) { m_state = State::Playing; return; }

        const bool chose = MenuInput(4);
        if (!chose) return;
        switch (m_sel) {
            case 0: m_state = State::Playing;    break;   // Resume
            case 1: OpenControls(State::Paused); break;
            case 2: ReturnToMenu();              break;
            case 3: m_quit = true;               break;
        }
    }

    void UpdateControls() {
        // Any of the "back" keys leaves, because a screen with nothing to choose
        // should not be a puzzle to escape from.
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) ||
            IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) {
            m_state = m_controlsReturn;
            // Land the highlight back on the entry that opened this screen, so
            // returning does not silently move the selection somewhere else.
            m_sel = 1;
        }
    }

    // Remembering where to go back to is what lets one Controls screen serve
    // both the title menu and a pause.
    void OpenControls(State from) {
        m_controlsReturn = from;
        m_state          = State::Controls;
    }

    // -- one frame of drawing -------------------------------------------------
    // The editor renders the game into an off-screen texture because it has to
    // show it inside a dockable panel. Here the window IS the game view, so the
    // world is drawn straight to it and the HUD is sized in real screen pixels.
    void Render() {
        BeginDrawing();
        ClearBackground(Color{15, 15, 20, 255});

        eng::Entity* camEnt = FindCameraEntity();
        if (!camEnt) {
            // A scene with no camera cannot be looked through. Saying so beats a
            // black window, which is indistinguishable from a crash.
            DrawText("This scene has no Camera component - nothing to look through.",
                     20, 20, 20, RAYWHITE);
            EndDrawing();
            return;
        }

        // ignoreScale = true so a scaled parent cannot stretch or shove the
        // camera; only where it is and which way it faces should matter.
        eng::CameraComponent* camComp = camEnt->GetComponent<eng::CameraComponent>();
        Camera3D cam = camComp->ToCamera3D(m_scene.WorldMatrix(*camEnt, true));

        // The clipping planes are GLOBAL state inside the graphics layer, read by
        // BeginMode3D when it builds the projection matrix. They must be set
        // before it, and they persist afterwards.
        rlSetClipPlanes(camComp->nearClip, camComp->farClip);

        BeginMode3D(cam);
        DrawSky(camComp->nearClip);
        m_scene.Draw();
        // Effects draw after the world so they are never shaded by the sun:
        // fire and sparks give off their own light.
        eng::DrawParticles(cam);
        EndMode3D();

        // Screen-space overlays -- the HUD, the radar, anything a script draws in
        // pixels rather than metres. After EndMode3D, because that is what
        // "screen space" means: the 3D pass is over, so there is no camera or
        // matrix left to fight.
        //
        // ONLY ONCE A RUN HAS STARTED. These come from scripts, and before
        // Scene::Start has run none of them have had their onStart - a HUD drawn
        // from uninitialised state is at best empty and at worst an error on
        // every frame of the title screen.
        //
        // THE SCENE MUST BE MARKED ACTIVE HERE. An overlay looks other entities
        // up through Scene::Current() -- a radar has to find its contacts -- and
        // outside Update nothing has marked one, so it would return null and
        // every overlay would silently draw nothing.
        if (m_started) {
            eng::ActiveScene active(m_scene);
            // Tell the HUD which camera the world was just drawn through, so a
            // script can place a mark ON something it can see. Set every frame
            // because the chase camera moves constantly.
            eng::SetHudCamera(cam, GetScreenWidth(), GetScreenHeight());
            // Opens the window in which the scripting draw calls work. Outside
            // it they do nothing, so a script drawing from the wrong hook fails
            // quietly instead of scribbling into the middle of the 3D pass.
            eng::BeginHudPass();
            for (const eng::Entity& e : m_scene.Entities())
                for (const auto& c : e.components)
                    c->OnDrawHud(e, GetScreenWidth(), GetScreenHeight());
            eng::EndHudPass();
        }

        switch (m_state) {
            case State::Menu:     DrawMenuScreen();     break;
            case State::Controls: DrawControlsScreen(); break;
            case State::Paused:   DrawPauseScreen();    break;
            case State::Playing:  break;
        }

        EndDrawing();
    }

    // -- menu drawing ----------------------------------------------------------

    // Text centred on x. MeasureText is asked for the width rather than the
    // string length being guessed at, because the font is not monospaced and a
    // title centred by character count is visibly off.
    static void TextCentred(const char* s, int cx, int y, int size, Color c) {
        DrawText(s, cx - MeasureText(s, size) / 2, y, size, c);
    }

    // Dim the world behind a menu. The backdrop is there to be recognisable, not
    // to be read, and text over an undimmed landscape is illegible wherever it
    // happens to cross a bright hillside.
    static void DimBackdrop() {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                      Color{0, 0, 0, 170});
    }

    // One column of choices, the selected one picked out. Returns nothing: the
    // selection itself lives in m_sel and was decided during the update, so that
    // drawing never changes state.
    void DrawItems(const char* const* items, int count, int cx, int top) {
        const int size = 30;
        const int step = 46;
        for (int i = 0; i < count; ++i) {
            const bool on = (i == m_sel);
            // The highlight is a colour change AND a marker, not colour alone:
            // a difference in brightness is easy to miss on a bright backdrop,
            // and colour alone excludes anyone who cannot separate the two.
            Color c = on ? Color{255, 220, 120, 255} : Color{200, 205, 210, 255};
            TextCentred(items[i], cx, top + i * step, size, c);
            if (on) {
                const int half = MeasureText(items[i], size) / 2;
                TextCentred(">", cx - half - 26, top + i * step, size, c);
                TextCentred("<", cx + half + 26, top + i * step, size, c);
            }
        }
    }

    void DrawMenuScreen() {
        DimBackdrop();
        const int cx = GetScreenWidth() / 2;
        const int cy = GetScreenHeight() / 2;

        TextCentred("WINCHESTER", cx, cy - 170, 72, Color{240, 240, 245, 255});
        TextCentred("out of ammunition", cx, cy - 92, 20, Color{150, 160, 170, 255});

        static const char* items[] = {"START", "CONTROLS", "QUIT"};
        DrawItems(items, 3, cx, cy - 20);

        TextCentred("W / S or UP / DOWN to move, ENTER to choose", cx,
                    GetScreenHeight() - 44, 18, Color{120, 130, 140, 255});
    }

    void DrawPauseScreen() {
        DimBackdrop();
        const int cx = GetScreenWidth() / 2;
        const int cy = GetScreenHeight() / 2;

        TextCentred("PAUSED", cx, cy - 150, 56, Color{240, 240, 245, 255});

        static const char* items[] = {"RESUME", "CONTROLS", "BACK TO MENU", "QUIT"};
        const int top = cy - 50;
        DrawItems(items, 4, cx, top);

        // Placed under the items rather than at the bottom of the window. The
        // HUD is still drawn underneath a pause, and the window's bottom edge is
        // where its damage bar lives - a hint pinned there lands on top of it.
        TextCentred("W / S or UP / DOWN to move, ENTER to choose, ESC to resume",
                    cx, top + 4 * 46 + 24, 18, Color{120, 130, 140, 255});
    }

    void DrawControlsScreen() {
        DimBackdrop();
        const int cx = GetScreenWidth() / 2;
        int y = GetScreenHeight() / 2 - 190;

        TextCentred("CONTROLS", cx, y, 48, Color{240, 240, 245, 255});
        y += 90;

        // Laid out as two columns around the centre line: keys right-aligned
        // against it, descriptions left-aligned after it, so the gap between
        // them is straight all the way down whatever the text lengths are.
        struct Row { const char* key; const char* what; };
        static const Row rows[] = {
            {"W / S",        "Pitch - W raises the nose"},
            {"A / D",        "Roll"},
            {"Q / E",        "Rudder"},
            {"SHIFT / CTRL", "Throttle - the top lights the afterburner"},
            {"G",            "Landing gear"},
            {"SPACE",        "Guns"},
            {"R",            "Restart, after a game over"},
            {"ESC",          "Pause"},
        };

        const int size = 20;
        const int step = 30;
        const int gap  = 24;      // half the space between the two columns
        for (const Row& r : rows) {
            DrawText(r.key, cx - gap - MeasureText(r.key, size), y, size,
                     Color{255, 220, 120, 255});
            DrawText(r.what, cx + gap, y, size, Color{200, 205, 210, 255});
            y += step;
        }

        // Under the table for the same reason as the pause hint: this screen can
        // be opened mid-run, with the HUD still drawn behind it.
        TextCentred("ESC to go back", cx, y + 24, 18, Color{120, 130, 140, 255});
    }

    // -- the pieces the editor also has ---------------------------------------

    eng::Entity* FindCameraEntity() {
        for (eng::Entity& e : m_scene.Entities())
            if (e.GetComponent<eng::CameraComponent>()) return &e;
        return nullptr;
    }

    // Push the scene's light to the shader. Only the first Light is used: this
    // engine has one directional light, the way an outdoor scene has one sun.
    void ApplySceneLight() {
        for (eng::Entity& e : m_scene.Entities()) {
            auto* light = e.GetComponent<eng::LightComponent>();
            if (!light) continue;

            eng::SunSettings sun;
            // A world matrix carries the entity's axes in known slots:
            // (m8,m9,m10) is its local +Z in world space, and this engine's
            // convention is that forward is local -Z, hence the negation.
            Matrix wm = m_scene.WorldMatrix(e, /*ignoreScale=*/true);
            sun.direction = {-wm.m8, -wm.m9, -wm.m10};
            sun.color   = {light->color.x * light->intensity,
                           light->color.y * light->intensity,
                           light->color.z * light->intensity};
            sun.ambient = light->ambient;
            sun.sky     = light->sky;
            sun.ground  = light->ground;
            eng::SetSun(sun);
            return;                        // first light wins; stop looking
        }
        eng::SetSun(eng::SunSettings{});    // no light: the shader's defaults
    }

    // Put the listener where the world is being heard from. Unlike the editor,
    // which switches between the scene camera and its own fly camera, there is
    // only ever one answer here: the camera the player is looking through.
    void UpdateAudioListener() {
        eng::Entity* camEnt = FindCameraEntity();
        if (!camEnt) return;
        Matrix w = m_scene.WorldMatrix(*camEnt, /*ignoreScale=*/true);
        eng::SetAudioListener(Vector3Transform({0.0f, 0.0f, 0.0f}, w),
                              {-w.m8, -w.m9, -w.m10},
                              { w.m4,  w.m5,  w.m6});
    }

    // The procedural gradient sky: a cube drawn around the camera and coloured
    // by view direction in the shader, so no texture is needed. If the shader
    // fails to compile the game simply renders without a backdrop.
    void LoadSky() {
        m_skyShader = LoadShader("assets/shaders/skybox.vs", "assets/shaders/skybox.fs");
        m_skyReady  = IsShaderValid(m_skyShader);
        if (!m_skyReady) return;
        m_sky = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
        m_sky.materials[0].shader = m_skyShader;
        // Look the uniform up ONCE. GetShaderLocation queries the driver by
        // name, far too slow to repeat every frame, so the handle is cached and
        // only the value is pushed per draw.
        m_skyScaleLoc = GetShaderLocation(m_skyShader, "skyScale");
    }

    // Call right after BeginMode3D, while the view and projection are active.
    // Depth writes and back-face culling are off so the cube fills the
    // background without occluding anything in the world.
    //
    // The cube is sized from the camera's NEAR PLANE because it surrounds the
    // camera and is only visible while its faces sit beyond that plane -- a cube
    // smaller than the near distance is entirely clipped and the sky vanishes.
    // Ten times the near distance is clear of the front of the frustum and far
    // inside any sane far plane; the exact number does not matter, since with
    // depth writes off the cube cannot occlude anything at any size.
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

    GameConfig m_cfg;
    eng::Scene m_scene;
    // The authored scene, kept aside so a run can be restarted. A vector of
    // clones rather than a second Scene, matching how the editor holds its
    // Play/Stop snapshot.
    std::vector<eng::Entity> m_backup;

    Shader m_skyShader{};
    Model  m_sky{};
    bool   m_skyReady    = false;
    int    m_skyScaleLoc = -1;

    // --- menu state ---------------------------------------------------------
    State m_state = State::Menu;
    // Where the Controls screen returns to, so one screen serves both the title
    // menu and a pause.
    State m_controlsReturn = State::Menu;
    int   m_sel     = 0;       // highlighted item on whichever menu is showing
    bool  m_quit    = false;   // set by the Quit item; ends the main loop
    // Whether Scene::Start has run. Guards the HUD pass, which is drawn by
    // scripts that have not been started yet on the title screen.
    bool  m_started = false;
};

int main(int argc, char** argv) {
    // EVERY RELATIVE PATH IS RESOLVED AGAINST THE EXE'S OWN FOLDER, not against
    // wherever the player happened to launch it from. A game started from a
    // desktop shortcut, from Explorer, or from another drive all inherit a
    // different working directory, and every one of them would fail to find
    // `assets/`. This is the shipped counterpart of the editor's baked-in
    // PROJECT_ROOT_DIR: the editor works on the source tree, the game works on
    // the assets copied next to it.
    std::error_code ec;
    std::filesystem::current_path(GetApplicationDirectory(), ec);
    if (ec) TraceLog(LOG_WARNING, "could not set the working directory: %s",
                     ec.message().c_str());

    GameConfig cfg = LoadConfig("assets/game.json");

    // A scene given on the command line wins, so one build can be pointed at a
    // different scene for testing without editing the config.
    if (argc > 1) cfg.scene = argv[1];

    GameApp app(std::move(cfg));
    if (!app.Startup()) {
        app.Shutdown();
        return 1;
    }
    app.Run();
    app.Shutdown();
    return 0;
}
