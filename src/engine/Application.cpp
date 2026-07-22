#include "engine/Application.h"

#include "raylib.h"     // window, timing, render textures
#include "imgui.h"      // Dear ImGui core
#include "rlImGui.h"    // glue that makes ImGui draw through raylib

namespace eng {

Application::Application(int width, int height, const char* title) {
    // Window flags have to be set BEFORE the window is created.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(width, height, title);   // create the window and its OpenGL context
    // Maximize afterwards (not via a config flag) so the maximize goes through
    // the normal resize path and every later size query sees the real size.
    MaximizeWindow();
    SetTargetFPS(60);                   // aim for 60 frames per second
    rlImGuiSetup(true);                 // initialize ImGui (true = dark theme); needs the window first

    // ImGuiIO holds ImGui's global settings. Enabling the docking flag lets
    // panels be dragged, split and tabbed together.
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Create the off-screen texture the scene will be rendered into. It gets
    // resized to match the viewport panel each frame.
    m_viewport = LoadRenderTexture(1280, 720);
}

Application::~Application() {
    // Clean up in the reverse order things were created.
    UnloadRenderTexture(m_viewport);
    rlImGuiShutdown();
    CloseWindow();
}

void Application::ResizeRenderTexture(RenderTexture2D& rt, int width, int height) {
    // A collapsed or minimized panel can report a zero or negative size;
    // ignore those so we never try to make an invalid texture.
    if (width <= 0 || height <= 0) return;
    // If the size is unchanged, do nothing. This is what makes the function
    // safe to call every single frame.
    if (width == rt.texture.width && height == rt.texture.height) return;

    UnloadRenderTexture(rt);                 // free the old one
    rt = LoadRenderTexture(width, height);   // make a new one at the new size
}

void Application::ResizeViewport(int width, int height) {
    ResizeRenderTexture(m_viewport, width, height);
}

void Application::Run() {
    // WindowShouldClose() becomes true when the user presses the window's X
    // (or Escape). Until then, run one frame per loop iteration.
    while (!WindowShouldClose()) {
        // GetFrameTime() is the number of seconds the last frame took ("delta
        // time"). Multiplying movement by it keeps speeds the same whether the
        // machine runs fast or slow.
        OnUpdate(GetFrameTime());

        // --- Pass 1: draw the 3D scene into the off-screen texture ----------
        BeginTextureMode(m_viewport);               // redirect drawing to the texture
        ClearBackground(Color{25, 25, 30, 255});    // dark background for the scene
        OnRenderScene();
        EndTextureMode();                           // stop redirecting

        // --- Pass 2: draw the editor to the real window ---------------------
        BeginDrawing();
        ClearBackground(DARKGRAY);                  // background behind the panels

        rlImGuiBegin();                             // begin an ImGui frame
        // Make the whole window a docking area. PassthruCentralNode leaves the
        // empty center see-through so anything drawn behind stays visible.
        ImGui::DockSpaceOverViewport(0, nullptr,
                                     ImGuiDockNodeFlags_PassthruCentralNode);
        OnRenderUI();                               // the subclass draws its panels
        rlImGuiEnd();                               // render all the ImGui panels

        EndDrawing();                               // present the finished frame
    }
}

} // namespace eng
