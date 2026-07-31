-- Hud.lua
-- =============================================================================
-- The heads-up display, drawn from a SCRIPT instead of from C++.
--
-- Put this on any entity in the scene - the player's aircraft is the natural
-- home, since some readings come from it. Everything below is drawn through the
-- `Draw.*` API from the onDrawHud hook, which runs once the 3D world has been
-- drawn, so adding, moving or restyling an element means editing this file and
-- pressing Play. No rebuild.
--
-- WHAT THE ARGUMENTS MEAN. onDrawHud(entity, w, h) hands over the size of the
-- surface being drawn into, in pixels, with (0,0) at the top left. That surface
-- is the Game panel, NOT the window, and the panel can be dragged to any size -
-- so nothing here is written as a fixed coordinate. Anything against an edge is
-- measured back from `w` or `h`, and anything centred is worked out from half of
-- them. An element positioned absolutely would sit correctly at one panel size
-- and drift off the edge at every other.
--
-- WHERE THE NUMBERS COME FROM. `Hud.get(name)` reads the shared value store that
-- scripts publish into with Hud.set / Hud.add - throttle from flight_sim.lua,
-- score and wave from gamemanager.lua. Reading a value that was never published
-- returns the fallback, which is how an element can hide itself rather than show
-- a meaningless zero: pass -1 and skip drawing when that is what comes back.
--
-- Colours are names, not numbers. The palette lives in C++ but any script may
-- add to or override it with Draw.defineColor, so a retint is one line here
-- rather than an edit at every call.
-- =============================================================================

properties = {
    show_crosshair = 1,   -- 1 or 0; a switch, since the graph language has no bools
    text_size      = 20,  -- height of the readouts in pixels
}

function onDrawHud(entity, w, h)
    local P  = properties
    local cx = w * 0.5
    local cy = h * 0.5

    -- --- Crosshair ----------------------------------------------------------
    -- Four short ticks with a gap in the middle rather than a solid cross: the
    -- gap is what keeps a distant target visible instead of covered by the very
    -- mark meant to aim at it.
    if P.show_crosshair > 0 then
        Draw.line(cx - 16, cy, cx - 5, cy)
        Draw.line(cx + 5,  cy, cx + 16, cy)
        Draw.line(cx, cy - 16, cx, cy - 5)
        Draw.line(cx, cy + 5,  cx, cy + 16)
        Draw.circleLines(cx, cy, 3)
    end

    -- --- Airspeed, on the left ----------------------------------------------
    -- Published by flight_sim.lua. Taking it from the store rather than
    -- measuring the aircraft's movement here means any craft running a flight
    -- script gets a working readout without this file knowing anything about it.
    local spd = Hud.get("speed", -1)
    if spd >= 0 then
        Draw.text(string.format("SPD %3.0f", spd), 24, cy - 10, P.text_size)
    end

    -- --- Engine power, below the speed --------------------------------------
    -- Only appears once a flight script has published it, so a scene with no
    -- aircraft is not cluttered with a gauge reading zero forever.
    local thr = Hud.get("throttle", -1)
    if thr >= 0 then
        if thr > 1 then thr = 1 end
        Draw.text(string.format("PWR %3.0f%%", thr * 100), 24, cy + 16, P.text_size)
        local bx, by, bw, bh = 24, cy + 40, 150, 10
        Draw.rectLines(bx, by, bw, bh)
        Draw.rect(bx + 2, by + 2, (bw - 4) * thr, bh - 4, "dim")
    end

    -- --- Altitude, on the right ---------------------------------------------
    -- Right-aligned, so the text is measured rather than guessed: the digits
    -- change width as the number grows, and a fixed offset would leave the
    -- readout creeping in and out from the edge as the aircraft climbs.
    local alt = entity.transform.position.y
    local txt = string.format("ALT %4.0f", alt)
    Draw.text(txt, w - 24 - Draw.textWidth(txt, P.text_size), cy - 10, P.text_size)

    -- --- Score and wave, across the top ------------------------------------
    local score = Hud.get("score", -1)
    if score >= 0 then
        local s = string.format("SCORE %d", score)
        Draw.text(s, cx - Draw.textWidth(s, 24) * 0.5, 18, 24)
    end
    local wave = Hud.get("wave", -1)
    if wave >= 0 then
        local s = string.format("WAVE %d", wave)
        Draw.text(s, cx - Draw.textWidth(s, 18) * 0.5, 44, 18)
    end

    -- --- Health bar along the bottom ----------------------------------------
    -- Scene.health returns current and maximum together. An entity with no
    -- Health component reports 0, 0 - which is why the guard is on the MAXIMUM:
    -- a maximum of zero means there is nothing to show, whereas a current of
    -- zero is a real reading and means dead.
    local hp, hpmax = Scene.health(entity)
    if hpmax > 0 then
        local frac = hp / hpmax
        if frac < 0 then frac = 0 elseif frac > 1 then frac = 1 end
        local bw, bh = 220, 16
        local bx, by = cx - bw * 0.5, h - 44
        -- Turn the bar amber then red as it empties. Colour carries urgency
        -- faster than a length does - you register "red" before you have read
        -- how much bar is left.
        local tone = "hud"
        if frac < 0.25 then tone = "bad" elseif frac < 0.5 then tone = "warn" end
        Draw.text("HP", bx - 34, by - 2, 20)
        Draw.rectLines(bx, by, bw, bh)
        Draw.rect(bx + 2, by + 2, (bw - 4) * frac, bh - 4, tone)
    end

    -- --- Game over ----------------------------------------------------------
    -- Drawn last so it covers everything above it. gamemanager.lua sets the
    -- flag; the editor watches the same flag to know that R should restart.
    if Hud.get("game_over", 0) > 0 then
        Draw.rect(0, 0, w, h, "dark")           -- dim the frozen world

        local t1 = "GAME OVER"
        Draw.text(t1, cx - Draw.textWidth(t1, 48) * 0.5, cy - 70, 48, "bad")

        local t2 = string.format("SCORE %d", Hud.get("score", 0))
        Draw.text(t2, cx - Draw.textWidth(t2, 28) * 0.5, cy - 6, 28, "white")

        local t3 = "Press R to restart"
        Draw.text(t3, cx - Draw.textWidth(t3, 20) * 0.5, cy + 36, 20, "white")
    end
end
