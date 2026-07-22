#pragma once

#include "raylib.h"   // RenderTexture2D and the window/drawing functions

namespace eng {

// Application owns the program window and the main loop. It is meant to be
// used as a BASE CLASS: a subclass (the editor) overrides the hook functions
// below to fill in what happens each frame, then calls Run() to start looping.
class Application {
public:
    // Create the window with a size and a title bar caption.
    Application(int width, int height, const char* title);
    // Virtual destructor so deleting through an Application* also runs the
    // subclass's destructor (see the note in Component.h about why).
    virtual ~Application();

    // Start the main loop. Returns only when the window is closed.
    void Run();

protected:
    // These "hooks" have empty default bodies; a subclass overrides the ones
    // it needs. This is the "template method" design: the base class fixes the
    // shape of each frame, and the subclass supplies the details.
    virtual void OnUpdate(float dt) {}     // per-frame logic; dt = seconds since last frame
    virtual void OnRenderScene() {}        // draw the 3D world (into the off-screen texture)
    virtual void OnRenderUI() {}           // draw the editor panels (ImGui calls)

    // Resize the built-in scene texture to (width, height). It is cheap to
    // call every frame because it does nothing unless the size actually
    // changed (recreating a GPU texture is expensive, so we avoid it).
    void ResizeViewport(int width, int height);

    // The same "only recreate if the size changed" logic for ANY render
    // texture. `static` because it doesn't touch a particular Application;
    // the editor reuses it for its separate Game-view texture.
    static void ResizeRenderTexture(RenderTexture2D& rt, int width, int height);

    // An off-screen image the 3D scene is drawn into, which the editor then
    // shows inside a panel. Protected so the subclass can access it.
    RenderTexture2D m_viewport{};
};

} // namespace eng
