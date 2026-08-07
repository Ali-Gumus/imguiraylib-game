// BuildRunner.cpp -- see BuildRunner.h for what this is and why it is shaped
// this way.

#include "BuildRunner.h"

#include <cstdio>
#include <thread>
#include <utility>

namespace edtr {
namespace {

// The most lines kept in memory. A clean build of this project prints a few
// hundred; a failing one can print thousands, and an unbounded log is a slow
// memory leak that only shows up on the worst day. When the cap is passed the
// OLDEST lines go, because the end of a build log is the part anyone reads.
constexpr size_t kMaxLines = 8000;

// Wrap a path in quotes so spaces in it survive.
std::string Quoted(const std::string& s) { return "\"" + s + "\""; }

// Build the command string for _popen.
//
// THE OUTER PAIR OF QUOTES IS NOT A MISTAKE, and leaving it off is the single
// most likely way for this to fail. _popen runs its argument through
// `cmd /c`, and cmd has an odd rule: when the string begins with a quote it
// strips the first and last quote character before running what is left. With
// a CMake path like
//     C:\Program Files\Microsoft Visual Studio\...\cmake.exe
// the command necessarily starts with a quote, so cmd removes it along with the
// closing quote of the LAST argument, and the result is a mangled command line
// and a message about 'C:\Program' not being recognised. Wrapping the whole
// thing in one more pair of quotes gives cmd a pair to eat that nobody needs.
//
// `2>&1` folds the error stream into the output stream. Compiler errors and
// MSBuild's own diagnostics go to stderr, so without it a failing build shows
// an empty log - which reads as "nothing happened" rather than "it failed".
// THE DEVELOPER ENVIRONMENT IS LOADED FIRST WHEN THE TOOLCHAIN NEEDS IT.
// Ninja runs cl.exe directly and relies on INCLUDE, LIB and PATH being set the
// way a Developer Command Prompt sets them. A GUI process has none of that, and
// the resulting error - a missing STANDARD header such as 'algorithm' - reads
// like a broken compiler install rather than an unset variable. vcvars64.bat
// sets them, and it in turn needs vswhere.exe on the PATH, which is why the
// installer folder is prepended before it is called.
//
// `>nul 2>&1` on the vcvars call keeps its banner out of the build log; it
// prints a dozen lines that say nothing about the build.
std::string MakeCommand(const BuildRunner::Request& req) {
    std::string inner;

    if (!req.vcvars.empty()) {
        if (!req.vsInstaller.empty())
            inner += "set \"PATH=%PATH%;" + req.vsInstaller + "\" && ";
        inner += "call " + Quoted(req.vcvars) + " >nul 2>&1 && ";
    }

    inner += Quoted(req.cmakeExe) + " --build " + Quoted(req.buildDir);
    // Omitted entirely for a single-config tree - see Request::config.
    if (!req.config.empty()) inner += " --config " + req.config;
    inner += " --target " + req.target + " 2>&1";

    return "\"" + inner + "\"";
}

} // namespace

BuildRunner::BuildRunner() : m_state(std::make_shared<State>()) {}

bool BuildRunner::Running() const  { return m_state->running.load();  }
bool BuildRunner::Finished() const { return m_state->finished.load(); }
int  BuildRunner::ExitCode() const { return m_state->exitCode.load(); }

bool BuildRunner::Succeeded() const {
    return m_state->finished.load() && m_state->exitCode.load() == 0;
}

std::vector<std::string> BuildRunner::Lines() const {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->lines;
}

size_t BuildRunner::LineCount() const {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->lines.size();
}

std::string BuildRunner::CommandLine() const {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->command;
}

void BuildRunner::Clear() {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    m_state->lines.clear();
}

bool BuildRunner::Start(const Request& req) {
    if (m_state->running.load()) return false;

    const std::string command = MakeCommand(req);

    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->lines.clear();
        m_state->command = command;
    }
    m_state->finished.store(false);
    m_state->exitCode.store(0);
    // Set BEFORE the thread starts. Setting it inside the thread leaves a
    // window in which the button is enabled and the build has already begun,
    // which is exactly long enough for a double-click to start two of them.
    m_state->running.store(true);

    // The state is captured BY VALUE, so the thread holds its own reference and
    // the shared block stays alive even if the BuildRunner is destroyed first.
    std::shared_ptr<State> state = m_state;

    std::thread([state, command]() {
        // "r" for reading the child's output; "t" for text mode, so the CRLF
        // line endings MSBuild writes arrive as plain newlines and every line
        // does not end up with a stray carriage return drawn as a box.
        FILE* pipe = _popen(command.c_str(), "rt");
        if (!pipe) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->lines.push_back("failed to start the build process");
            state->exitCode.store(-1);
            state->finished.store(true);
            state->running.store(false);
            return;
        }

        // Read a character buffer at a time and split on newlines ourselves,
        // rather than trusting one fgets to equal one line: a line longer than
        // the buffer arrives in pieces, and treating each piece as a line
        // scrambles the log exactly when it is most needed - a long compiler
        // error with a template type in it.
        char        buffer[4096];
        std::string pending;

        auto push = [&state](std::string line) {
            // Trim a trailing carriage return, for output that arrived with
            // CRLF despite the text-mode translation (redirected tool output
            // often does).
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::lock_guard<std::mutex> lock(state->mutex);
            state->lines.push_back(std::move(line));
            if (state->lines.size() > kMaxLines)
                state->lines.erase(state->lines.begin(),
                                   state->lines.begin() +
                                       (state->lines.size() - kMaxLines));
        };

        while (fgets(buffer, sizeof(buffer), pipe)) {
            pending += buffer;
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                push(pending.substr(0, nl));
                pending.erase(0, nl + 1);
            }
        }
        // A final line with no newline after it still counts.
        if (!pending.empty()) push(std::move(pending));

        // _pclose waits for the process and returns its exit status, so this is
        // the point at which the result is actually known. It comes AFTER the
        // read loop by necessity: the loop only ends when the pipe closes, and
        // the pipe only closes when the process is done writing.
        const int code = _pclose(pipe);
        state->exitCode.store(code);

        // `finished` before `running` is cleared. The editor tests Running()
        // to decide whether to show a result, so clearing it first would let a
        // frame in between see "not running, not finished" and report nothing.
        state->finished.store(true);
        state->running.store(false);
    }).detach();

    return true;
}

} // namespace edtr
