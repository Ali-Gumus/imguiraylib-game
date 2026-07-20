#pragma once

#include "raylib.h"   // for RenderTexture2D (leaks raylib into the public API; OK for now)

namespace eng {

// Owns the window and the main loop. Subclass it (the editor does),
// override the hooks, call Run().
class Application {
public:
    Application(int width, int height, const char* title);
    virtual ~Application();

    void Run();                          // the loop lives here

protected:
    // Hooks for subclasses — the "template method" pattern.
    virtual void OnUpdate(float dt) {}   // game/editor logic, before drawing
    virtual void OnRenderScene() {}      // draw the game world (into the viewport)
    virtual void OnRenderUI() {}         // ImGui calls go here

    // Recreate the viewport texture at a new size. Cheap to call every
    // frame: does nothing unless the size actually changed.
    void ResizeViewport(int width, int height);

    // Same lazily-recreate-on-change logic for ANY render texture (the
    // editor uses it for its Game view too).
    static void ResizeRenderTexture(RenderTexture2D& rt, int width, int height);

    // The off-screen canvas the scene renders into. Protected so the
    // editor subclass can hand it to ImGui as an image.
    RenderTexture2D m_viewport{};
};

} // namespace eng
