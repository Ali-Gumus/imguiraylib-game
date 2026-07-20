#include "engine/Application.h"

#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"

namespace eng {

Application::Application(int width, int height, const char* title) {
    // Flags must be set BEFORE InitWindow (they configure its creation).
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(width, height, title);    // window + OpenGL context; w/h = restored-size
    // Maximize AFTER creation: goes through the normal resize event, so
    // everything sees the real size (the MAXIMIZED config flag doesn't).
    MaximizeWindow();
    SetTargetFPS(60);
    rlImGuiSetup(true);                  // must come after InitWindow

    // ImGuiIO is ImGui's config & state hub (input, fonts, flags).
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // allow docking windows

    // Off-screen canvas for the scene. Fixed size for now; we'll make it
    // follow the panel size later.
    m_viewport = LoadRenderTexture(1280, 720);
}

Application::~Application() {
    // Reverse order of construction: created last -> destroyed first.
    UnloadRenderTexture(m_viewport);
    rlImGuiShutdown();
    CloseWindow();
}

void Application::ResizeRenderTexture(RenderTexture2D& rt, int width, int height) {
    // Ignore degenerate sizes (panel collapsed/minimized gives 0 or negative).
    if (width <= 0 || height <= 0) return;
    // The no-op check — THE line that makes this safe to call per frame.
    if (width == rt.texture.width && height == rt.texture.height) return;

    UnloadRenderTexture(rt);
    rt = LoadRenderTexture(width, height);
}

void Application::ResizeViewport(int width, int height) {
    ResizeRenderTexture(m_viewport, width, height);
}

void Application::Run() {
    while (!WindowShouldClose()) {       // false until ESC or window X
        // GetFrameTime() = seconds since last frame ("delta time").
        // Logic scaled by dt runs at the same speed on any machine.
        OnUpdate(GetFrameTime());

        // Redirect all drawing into the texture instead of the screen.
        BeginTextureMode(m_viewport);
        ClearBackground(Color{25, 25, 30, 255});   // dark scene background
        OnRenderScene();
        EndTextureMode();                // back to drawing on the real screen

        BeginDrawing();
        ClearBackground(DARKGRAY);       // editor background behind the panels

        rlImGuiBegin();                  // start feeding ImGui this frame
        // Turn the whole window into a dock area. PassthruCentralNode keeps
        // the empty middle transparent so the raylib scene stays visible.
        ImGui::DockSpaceOverViewport(0, nullptr,
                                     ImGuiDockNodeFlags_PassthruCentralNode);
        OnRenderUI();                    // subclass draws its panels
        rlImGuiEnd();                    // render ImGui on top

        EndDrawing();
    }
}

} // namespace eng
