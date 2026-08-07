#pragma once

// BuildRunner.h
// =============================================================================
// Runs a CMake build in the BACKGROUND and hands its output back a line at a
// time, so the editor can show a build log while still drawing at 60 fps.
//
// WHY THIS IS NOT JUST A CALL TO system(). A full Release build takes a minute
// or two. The editor is a frame loop: anything that blocks inside it stops the
// window redrawing, and Windows then paints the "not responding" ghost over it.
// The build therefore runs on its own thread, which does nothing but read the
// compiler's output and append it to a buffer; the editor drains that buffer
// once a frame and keeps drawing.
//
// WHAT THIS HEADER DELIBERATELY DOES NOT INCLUDE. No raylib, no ImGui, and no
// windows.h. The last one is the important one and is a project rule rather
// than a preference: FileDialog.cpp is currently the ONLY file that includes
// windows.h, because windows.h and raylib collide on common names
// (Rectangle, DrawText, CloseWindow, ShowCursor). Spawning a process the
// "proper" Win32 way - CreateProcess with an anonymous pipe - would need it
// here too, so this uses _popen from <cstdio> instead, which needs nothing.
// The same quarantine instinct that keeps Jolt inside Physics.cpp and JSBSim
// inside FlightModel.cpp.
//
// THREAD LIFETIME, AND WHY THE STATE IS A shared_ptr. A build outlives the
// button that started it, and it can still be running when the editor is
// closed. Joining in the destructor would hang the shutdown for as long as the
// compiler feels like taking. Instead the shared state is reference-counted and
// the thread is detached: if the BuildRunner goes away first, the thread keeps
// writing into state that is still alive because the thread itself holds a
// reference, and nothing dangles.
// =============================================================================

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace edtr {

class BuildRunner {
public:
    BuildRunner();

    // Start a build. `cmakeExe` is the full path to cmake.exe, `buildDir` the
    // build tree to build in, and `config`/`target` the usual CMake arguments.
    //
    // Returns false and changes nothing if a build is already running - two
    // compilers writing the same object files is not something to find out
    // about later.
    bool Start(const std::string& cmakeExe,
               const std::string& buildDir,
               const std::string& config,
               const std::string& target);

    // True from Start() until the process has exited AND its output has been
    // completely read. The two are not the same moment, and reporting success
    // at the first one is how a build log ends up truncated.
    bool Running() const;

    // True once a build has finished, whether it worked or not. False before
    // the first build of the session.
    bool Finished() const;

    // Only meaningful once Finished(). A build "succeeded" when the process
    // exited with code 0 - which is the ONLY reliable signal here, since
    // compilers print the word "error" in plenty of lines that are not fatal
    // and stay silent in some that are.
    bool Succeeded() const;
    int  ExitCode()  const;

    // Copy out the log. A copy rather than a reference because the writing
    // thread may append at any moment, and handing ImGui a container that can
    // reallocate underneath it is a crash waiting for a slow build.
    std::vector<std::string> Lines() const;

    // How many lines exist, for a caller that only wants to know whether to
    // scroll. Cheap enough to ask every frame.
    size_t LineCount() const;

    // The command that was run, for showing at the top of the log. Being able
    // to copy the exact command out and run it in a terminal is the first thing
    // anyone wants when a build behaves differently inside a tool.
    std::string CommandLine() const;

    // Throw away the previous log. Called before each build.
    void Clear();

private:
    // Everything the worker thread touches lives in here, so its lifetime can
    // be shared with the thread rather than tied to the BuildRunner.
    struct State {
        mutable std::mutex       mutex;
        std::vector<std::string> lines;
        std::string              command;
        std::atomic<bool>        running{false};
        std::atomic<bool>        finished{false};
        std::atomic<int>         exitCode{0};
    };

    std::shared_ptr<State> m_state;
};

} // namespace edtr
