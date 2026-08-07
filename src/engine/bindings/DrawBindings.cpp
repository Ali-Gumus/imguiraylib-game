#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Components.h"  // HudColor, HudDrawAllowed, DefineHudColor

#include "raylib.h"

#include <string>

namespace eng {

void RegisterDrawBindings(sol::state& lua) {
    // ---- The `draw` table: 2D drawing, for building a HUD in script --------
    //
    // Everything here is in PIXELS of whatever surface the game view is being
    // drawn into, with (0,0) at the top left. The size of that surface arrives
    // as the `w, h` arguments of onDrawHud, and it is NOT the window: the
    // Game panel can be dragged to any size, so anything meant to sit against
    // an edge or in the middle must be worked out from those two numbers rather
    // than written as a fixed coordinate.
    //
    // These are only legal from inside onDrawHud. Called from onUpdate they
    // would land in the middle of the 3D pass, where a flat rectangle drawn in
    // pixel coordinates is meaningless and would corrupt the frame - so they
    // refuse to run outside the HUD pass and say so once, rather than producing
    // a frame of garbage that is hard to trace back here.
    sol::table dr = lua.create_named_table("Draw");

    dr["text"] = [](const std::string& s, float x, float y, float size,
                    sol::optional<std::string> col) {
        if (!HudDrawAllowed()) return;
        DrawText(s.c_str(), (int)x, (int)y, (int)size, HudColor(col.value_or(std::string())));
    };
    // Draw.textWidth(s, size): how wide that text would be, in pixels. Needed
    // to right-align or centre anything, since text has no fixed character
    // width - guessing it leaves a readout drifting as its digits change.
    dr["textWidth"] = [](const std::string& s, float size) {
        return (float)MeasureText(s.c_str(), (int)size);
    };
    dr["rect"] = [](float x, float y, float w, float h,
                    sol::optional<std::string> col) {
        if (!HudDrawAllowed()) return;
        DrawRectangle((int)x, (int)y, (int)w, (int)h, HudColor(col.value_or(std::string())));
    };
    dr["rectLines"] = [](float x, float y, float w, float h,
                          sol::optional<std::string> col) {
        if (!HudDrawAllowed()) return;
        DrawRectangleLines((int)x, (int)y, (int)w, (int)h, HudColor(col.value_or(std::string())));
    };
    dr["circle"] = [](float x, float y, float r,
                      sol::optional<std::string> col) {
        if (!HudDrawAllowed()) return;
        DrawCircle((int)x, (int)y, r, HudColor(col.value_or(std::string())));
    };
    dr["circleLines"] = [](float x, float y, float r,
                            sol::optional<std::string> col) {
        if (!HudDrawAllowed()) return;
        DrawCircleLines((int)x, (int)y, r, HudColor(col.value_or(std::string())));
    };

    // Draw.worldToScreen(x, y, z) -> sx, sy   (or nil when it cannot be seen)
    //
    // Turns a place IN THE WORLD into a place ON THE GLASS, using the camera the
    // world was just drawn through. This is what a marker needs in order to sit
    // on top of the thing it refers to - an aiming mark, a target box, a label
    // over a building.
    //
    // It is NOT how the pitch ladder or the flight path marker should be placed.
    // Those are instruments: they answer "where is this relative to my
    // aircraft", which is a question about the aircraft and not about where a
    // camera happens to be standing.
    //
    // Returns TWO values, or nil. Returning nil rather than an off-screen
    // coordinate forces the caller to decide what to do about a point that
    // cannot be seen, instead of silently drawing it in the wrong place - which
    // for anything behind the camera would be mirrored to the opposite side of
    // the display.
    dr["worldToScreen"] = [](float x, float y, float z,
                             sol::this_state s) -> sol::variadic_results {
        sol::variadic_results out;
        Vector2 p{};
        if (!WorldToHudScreen(Vector3{x, y, z}, p)) {
            out.push_back({s, sol::in_place, sol::lua_nil});
            return out;
        }
        // Two numbers, matching how the rest of this API talks about positions -
        // loose components rather than a table the caller has to unpack.
        out.push_back({s, sol::in_place, p.x});
        out.push_back({s, sol::in_place, p.y});
        return out;
    };
    dr["line"] = [](float x1, float y1, float x2, float y2,
                    sol::optional<std::string> col) {
        if (!HudDrawAllowed()) return;
        DrawLine((int)x1, (int)y1, (int)x2, (int)y2, HudColor(col.value_or(std::string())));
    };
    dr["triangle"] = [](float x1, float y1, float x2, float y2,
                        float x3, float y3, sol::optional<std::string> col) {
        if (!HudDrawAllowed()) return;
        // raylib discards back-facing triangles, so the three corners have to
        // be given anticlockwise on screen to be visible. Rather than make
        // every caller remember that, the winding is measured and flipped when
        // it comes out the wrong way round.
        const float cross = (x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1);
        if (cross > 0.0f) DrawTriangle({x1, y1}, {x3, y3}, {x2, y2}, HudColor(col.value_or(std::string())));
        else              DrawTriangle({x1, y1}, {x2, y2}, {x3, y3}, HudColor(col.value_or(std::string())));
    };
    // Draw.defineColor(name, r, g, b, a): add or replace a named colour, so a
    // palette lives in a script rather than in C++ - the same arrangement
    // effects.lua and sounds.lua already use for their own data. Channels are
    // 0..255. Redefining a built-in name is allowed and is how you retint the
    // whole HUD from one place.
    dr["defineColor"] = [](const std::string& name, float r, float g, float b,
                            sol::optional<float> a) {
        DefineHudColor(name, Color{(unsigned char)r, (unsigned char)g,
                                   (unsigned char)b,
                                   (unsigned char)a.value_or(255.0f)});
    };
}

void DescribeDrawBindings(LuaApiRegistry& api) {
    auto d = api.Table("Draw");
    d.Fn("text(s, x, y, size [, color])", "Draw text. Its top-left corner sits at x, y");
    d.Fn("textWidth(s, size) -> number",
         "How wide that text would be. Needed to centre or right-align, since digits change width");
    d.Fn("rect(x, y, w, h [, color])",        "A filled rectangle");
    d.Fn("rectLines(x, y, w, h [, color])",  "A rectangle outline");
    d.Fn("circle(x, y, r [, color])",         "A filled circle");
    d.Fn("circleLines(x, y, r [, color])",   "A circle outline");
    d.Fn("line(x1, y1, x2, y2 [, color])",    "A straight line");
    d.Fn("worldToScreen(x, y, z) -> sx, sy",
         "Where a world point appears on screen, through the camera the world was "
         "drawn with. nil when it is behind the camera. Use it to put a mark ON "
         "something you can see - an aiming pipper, a target box. NOT for "
         "instruments like a pitch ladder, which are relative to the aircraft");
    d.Fn("triangle(x1, y1, x2, y2, x3, y3 [, color])",
         "A filled triangle. Winding is corrected for you");
    d.Fn("defineColor(name, r, g, b [, a])",
         "Add or replace a palette colour, so a HUD's colours live in script");
}

} // namespace eng
