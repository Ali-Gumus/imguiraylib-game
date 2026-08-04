-- hud.lua
-- =============================================================================
-- A COMBAT AIRCRAFT HEAD-UP DISPLAY, drawn from a SCRIPT instead of from C++.
--
-- PUT THIS ON SOMETHING THAT OUTLIVES THE PLAYER - the Game Manager, not the
-- aircraft. That is not a style preference, it is the difference between having
-- a game-over screen and not having one.
--
-- When the player dies the entity is DESTROYED, and a component on a destroyed
-- entity stops being drawn. A HUD living on the aircraft therefore vanishes at
-- the exact moment it has something important to say. So this script does not
-- assume it owns the player: it looks one up with Scene.findByTag("player") and
-- copes with there being none, which is a normal state here rather than an
-- error - the readings that need an aircraft hide themselves, and everything
-- else, game over included, carries on.
--
-- WHAT A REAL HUD IS. A head-up display is a piece of glass in front of the
-- pilot with symbols projected onto it, so it is fixed to the AIRFRAME, not to
-- the world. That single fact decides the whole layout below:
--
--   * The centre of the display is where the NOSE points. It never moves.
--   * The horizon line and the pitch ladder are drawn where the real horizon
--     and the real climb angles appear THROUGH that glass, so they roll and
--     slide about the centre as the aircraft manoeuvres. They are the only
--     things here that move with the world.
--   * The tapes, the bank scale and the readouts are painted on the glass and
--     stay put.
--
-- Read together, that gives the pilot attitude without looking down: if the
-- ladder is steeply tilted you are banked, if it has slid far below centre you
-- are climbing.
--
-- THE ONE HONEST COMPROMISE. This game is drawn from a CHASE CAMERA sitting
-- behind and above the aircraft, which also takes only part of its roll (see
-- chasecam.lua). So the horizon drawn here cannot line up pixel-for-pixel with
-- the horizon visible out of the window - no third-person view allows that.
-- What is drawn is correct with respect to the AIRCRAFT, which is what an
-- instrument is for, and it reads the way a HUD reads. Matching the camera
-- instead would mean projecting through the camera's own orientation and would
-- make the display lie about the aeroplane.
--
-- HOW THE SYMBOLS ARE PLACED, in one idea. Every angle is turned into pixels by
-- one number, `pixels_per_degree`. A climb angle of ten degrees is drawn ten
-- times that many pixels from the centre. Because one scale serves the ladder,
-- the flight path marker and the angle of attack alike, they stay consistent
-- with each other automatically: the marker sitting on the "10" bar really does
-- mean the aircraft is travelling ten degrees uphill.
--
-- WHY THERE ARE TWO HOOKS. onDrawHud draws, and is handed the pixel size of the
-- surface - but NOT how much time has passed, which is exactly what is needed
-- to work out a velocity or a G-load. onUpdate is handed `dt` and nothing else.
-- So onUpdate does the measuring and keeps the answers in the locals below, and
-- onDrawHud only draws them. That also keeps the measurement at a steady rate
-- rather than tying it to how often the game view happens to be visible.
--
-- Measuring the player's motion here, rather than having flight_sim.lua publish
-- it, is deliberate: this display then works for ANY aircraft, including one
-- flown by physics or by a node graph, and the flight model stays unaware that
-- a HUD exists.
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

    -- Height in pixels of the LIVE READOUTS - the airspeed and altitude boxes
    -- and the score. The small labels on the tapes and the pitch ladder are not
    -- scaled by this and are deliberately fixed: they are a scale to read the
    -- boxed value against, and a scale that competes with its own readout for
    -- attention has stopped being a scale.
    text_size = 20,

    -- The scale that ties angles to pixels, described above. Larger spreads the
    -- pitch ladder out and makes small attitude changes easier to read, at the
    -- cost of fewer degrees fitting on the display at once. Seven puts a ten
    -- degree ladder step 70 pixels apart, which is legible without the bars
    -- crowding each other.
    pixels_per_degree = 7,

    -- Individual elements, so a scene can turn off whatever it does not want
    -- without editing this file. All are 1 or 0.
    show_ladder = 1,   -- horizon line and climb/dive bars
    show_tapes  = 1,   -- airspeed, altitude and heading scales
    show_fpm    = 1,   -- the flight path marker
    show_bank   = 1,   -- the bank scale and its pointer

    -- How many degrees apart the pitch ladder bars are. Ten is the usual choice;
    -- five gives a finer ladder for gentle flying and clutters a fast one.
    ladder_step = 10,

    -- The airspeed below which the STALL caption starts flashing. The F-16 that
    -- flight_sim.lua models stalls at roughly 57 metres per second, so 70 gives
    -- a little warning before it actually happens rather than after.
    stall_speed = 70,

    -- The speed of sound in metres per second, used only to turn airspeed into a
    -- Mach number. 340 is the sea-level figure; it genuinely falls with altitude
    -- (to about 295 in the stratosphere), which this deliberately ignores - the
    -- readout is a sense of speed, not a flight-planning instrument.
    sound_speed = 340,
}

-- ---------------------------------------------------------------------------
-- STATE MEASURED IN onUpdate AND DRAWN IN onDrawHud
--
-- These are file-scope locals, so they persist from frame to frame - a local
-- declared inside a function would be born and lost every call and could never
-- hold a previous position to compare against.
-- ---------------------------------------------------------------------------

-- Where the player was last frame. `nil` means "no previous frame to compare
-- with", which happens on the first update and again after every death, and is
-- the signal to measure nothing this time rather than to compare against stale
-- coordinates from a previous life.
local prev_x, prev_y, prev_z = nil, nil, nil

-- The player's velocity in world space, in metres per second, and the value it
-- held last frame (needed to work out acceleration, which is what a G-load is).
local vel_x, vel_y, vel_z = 0, 0, 0
local pvel_x, pvel_y, pvel_z = 0, 0, 0

-- Load factor: how many times the aircraft's own weight the airframe is pulling.
-- Straight and level flight is 1, not 0 - the wings are always holding the
-- aeroplane up. Starting the value at 1 avoids a frame of nonsense on the first
-- update before anything has been measured.
local gload = 1

-- A clock that runs for as long as the scene is playing, used to blink the
-- warning captions. A steady flash draws the eye far better than static text,
-- which is exactly why real warning panels flash.
local clock = 0

-- ---------------------------------------------------------------------------
-- SMALL HELPERS
-- ---------------------------------------------------------------------------

-- The dot product of two vectors given as loose numbers. Used constantly below
-- to ask "how much of this world direction lies along that aircraft axis", which
-- is the whole trick behind placing world-referenced symbols on the glass.
local function dot(ax, ay, az, bx, by, bz)
    return ax * bx + ay * by + az * bz
end

local function clamp(v, lo, hi)
    if v < lo then return lo elseif v > hi then return hi end
    return v
end

-- Exponential smoothing that closes the same PROPORTION of the gap per second
-- whatever the frame rate. Written out rather than inlined because the naive
-- version (`value + (target - value) * 0.1`) smooths twice as fast at 120 frames
-- a second as at 60, so the display would feel different on different machines.
local function ease(current, target, rate, dt)
    return current + (target - current) * (1 - math.exp(-rate * dt))
end

-- Text placed by its LEFT edge and vertically centred on y. Draw.text positions
-- text by its top-left corner, so every call that wants to line up with a tick
-- mark or a box would otherwise have to subtract half the size itself.
local function text_l(s, x, y, size, col)
    Draw.text(s, x, y - size * 0.5, size, col)
end

-- Text placed by its RIGHT edge. The width has to be measured rather than
-- guessed, because digits are not all the same width and a fixed offset leaves
-- a readout creeping in and out from the edge as its value changes.
local function text_r(s, x, y, size, col)
    Draw.text(s, x - Draw.textWidth(s, size), y - size * 0.5, size, col)
end

-- Text centred on x.
local function text_c(s, x, y, size, col)
    Draw.text(s, x - Draw.textWidth(s, size) * 0.5, y - size * 0.5, size, col)
end

-- A readout in a box: a dark panel so the digits stay legible against bright
-- terrain, an outline, and the value inside. This is how every live number on a
-- real HUD is presented - the box is what separates "the current value" from the
-- scale it is being read against.
local function boxed(s, x, y, wdt, hgt, size, col)
    Draw.rect(x, y - hgt * 0.5, wdt, hgt, "dark")
    Draw.rectLines(x, y - hgt * 0.5, wdt, hgt, col)
    text_c(s, x + wdt * 0.5, y, size, col)
end

-- A broken line, drawn as `n` dashes with gaps between them. Dive bars use this:
-- solid means climbing and dashed means descending, so the ladder can be read at
-- a glance without finding its numbers.
local function dashed(x1, y1, x2, y2, n, col)
    local dx, dy = (x2 - x1) / n, (y2 - y1) / n
    for i = 0, n - 1 do
        local sx, sy = x1 + dx * i, y1 + dy * i
        -- Six tenths of each slot is line and the rest is gap.
        Draw.line(sx, sy, sx + dx * 0.6, sy + dy * 0.6, col)
    end
end

-- ---------------------------------------------------------------------------
-- MEASUREMENT
-- ---------------------------------------------------------------------------

function onUpdate(entity, dt)
    clock = clock + dt

    local player = Scene.findByTag("player")
    if player == nil then
        -- No aircraft: forget where it was, so that a respawn somewhere else
        -- does not register as one enormous frame of movement. A jet that
        -- reappears 3000 metres away would otherwise show a velocity of several
        -- hundred thousand metres per second for one frame, and the G-load
        -- derived from it would be worse.
        prev_x, prev_y, prev_z = nil, nil, nil
        return
    end

    local p = player.transform.position

    -- A zero or negative dt happens on a paused or stepped frame. Dividing by it
    -- would produce infinity, and infinity spreads through every value it
    -- touches and never washes out.
    if prev_x == nil or dt <= 0 then
        prev_x, prev_y, prev_z = p.x, p.y, p.z
        return
    end

    -- Velocity is simply how far it moved divided by how long that took. This
    -- measures the aircraft's ACTUAL motion, whatever moved it - the flight
    -- model, the physics engine or a node graph - which is why the display does
    -- not care which of those is flying.
    local mx = (p.x - prev_x) / dt
    local my = (p.y - prev_y) / dt
    local mz = (p.z - prev_z) / dt
    prev_x, prev_y, prev_z = p.x, p.y, p.z

    -- Smoothed lightly. The raw figure is a difference between two large world
    -- coordinates, so it carries the floating-point noise of both and makes the
    -- flight path marker shiver even in level flight. A rate of 20 removes the
    -- shiver while still following a real manoeuvre within a tenth of a second.
    pvel_x, pvel_y, pvel_z = vel_x, vel_y, vel_z
    vel_x = ease(vel_x, mx, 20, dt)
    vel_y = ease(vel_y, my, 20, dt)
    vel_z = ease(vel_z, mz, 20, dt)

    -- ---- Load factor ------------------------------------------------------
    -- G-load is NOT how fast the aircraft is accelerating. It is how hard the
    -- WINGS are working, and the wings have to fight gravity even when nothing
    -- is changing. So take the acceleration, subtract gravity's contribution
    -- (subtracting a downward 9.81 is adding an upward one), and measure what is
    -- left along the aircraft's own up axis.
    --
    -- Sitting level and unaccelerated that leaves exactly 9.81 upward, which is
    -- 1 G - correct, and the reason the formula is not simply |a|/9.81. Pushing
    -- the nose over gives a negative answer, which is also correct and is what
    -- pilots call negative G.
    local up = player.transform:up()
    local ax = (vel_x - pvel_x) / dt
    local ay = (vel_y - pvel_y) / dt + 9.81
    local az = (vel_z - pvel_z) / dt
    local g = dot(ax, ay, az, up.x, up.y, up.z) / 9.81

    -- Smoothed much harder than the velocity. A G reading taken from two
    -- successive frames is extremely jumpy - it is a difference of differences,
    -- so every bit of noise in the velocity is counted twice - and a number that
    -- flickers is unreadable however accurate it is.
    gload = ease(gload, g, 4, dt)
end

-- ---------------------------------------------------------------------------
-- DRAWING
-- ---------------------------------------------------------------------------

function onDrawHud(entity, w, h)
    local P  = properties
    local cx = w * 0.5
    local cy = h * 0.5

    -- The aircraft this HUD reports on, which is NOT the entity this script is
    -- attached to. It may be nil - the player is dead - and everything below
    -- that needs it checks first.
    local player = Scene.findByTag("player")

    -- =======================================================================
    -- THE AIRCRAFT'S ATTITUDE
    --
    -- Everything world-referenced on the display is placed from three unit
    -- vectors: where the nose points, where the right wing points, and where the
    -- top of the canopy points. They are the aircraft's own axes expressed in
    -- world coordinates, and the display's own axes are exactly those two last
    -- ones: screen-right IS the right wing, screen-up IS the canopy.
    --
    -- That equivalence is what makes the placement below simple. To find where a
    -- world direction appears on the glass, ask how much of it lies along the
    -- right wing (that is its horizontal position) and how much along the canopy
    -- (its vertical position, negated because pixel y counts downward).
    -- =======================================================================
    local fwd, rgt, up
    local pitch, bank, heading = 0, 0, 0

    -- The screen direction of world-DOWN, and of the horizon running to the
    -- aircraft's right. These two carry the entire roll and pitch of the ladder,
    -- and deriving them from dot products rather than from angles means there is
    -- no sign convention to get backwards.
    local down_x, down_y = 0, 1   -- straight down the screen when wings are level
    local horz_x, horz_y = 1, 0   -- straight across the screen when wings are level

    if player ~= nil then
        local t = player.transform
        fwd, rgt, up = t:forward(), t:right(), t:up()

        -- Pitch: how far the nose is above the horizontal. The vertical part of
        -- a unit vector IS the sine of its elevation, so the angle is its arc
        -- sine. Clamped because floating-point error can hand asin a number a
        -- hair outside its legal range, and asin(1.0000001) is not a number.
        pitch = math.deg(math.asin(clamp(fwd.y, -1, 1)))

        -- Heading: the compass direction of the nose, ignoring how steeply it
        -- points. The engine's forward axis is -Z, so -Z is taken as north and
        -- +X as east; that makes an aircraft with an unrotated transform read
        -- 000, which is the least surprising choice.
        heading = math.deg(math.atan(fwd.x, -fwd.z)) % 360

        -- World-down on the glass. Gravity points along world -Y, so its
        -- horizontal position is how much of -Y lies along the right wing
        -- (-rgt.y) and its vertical position is how much lies along the canopy,
        -- negated for pixel coordinates (-(-up.y), which is up.y).
        local dx, dy = -rgt.y, up.y
        local dl = math.sqrt(dx * dx + dy * dy)

        -- That length collapses to zero when the aircraft points straight up or
        -- straight down, because then neither wing nor canopy has any vertical
        -- component - gravity is along the nose, which the flat glass cannot
        -- show a direction for. At exactly the vertical the horizon is a circle
        -- around the pilot and bank stops meaning anything, so holding the
        -- wings-level default is as good an answer as exists.
        if dl > 0.0001 then
            down_x, down_y = dx / dl, dy / dl
            -- The horizon is at right angles to gravity, on the glass as in the
            -- world. Turning (x, y) into (y, -x) rotates it a quarter turn, and
            -- this is the quarter turn that gives (1, 0) - the right wing - when
            -- down is (0, 1).
            horz_x, horz_y = down_y, -down_x
        end

        -- Bank, as a number, for the pointer and the readout. It is just the
        -- angle by which world-down has rotated away from screen-down: zero with
        -- the wings level, positive banked right.
        bank = math.deg(math.atan(down_x, down_y))
    end

    -- Airspeed. Preferring the flight model's own published figure keeps the
    -- readout exactly consistent with what the model believes, and falling back
    -- to the measured velocity means an aircraft flown by anything else still
    -- gets a speed.
    --
    -- THE WHOLE THING IS GATED ON THERE BEING AN AIRCRAFT, and that guard is not
    -- redundant. The shared value store is not cleared when the player dies, so
    -- Hud.get("speed") happily keeps returning whatever the aircraft was doing
    -- at the moment it was destroyed - for as long as the game-over screen is
    -- up. Without the guard the airspeed tape would sit there frozen at 250
    -- beside an altitude tape that had correctly hidden itself.
    local speed = -1
    if player ~= nil then
        speed = Hud.get("speed", -1)
        if speed < 0 then
            speed = math.sqrt(vel_x * vel_x + vel_y * vel_y + vel_z * vel_z)
        end
    end

    local alt = 0
    if player ~= nil then alt = player.transform.position.y end

    -- How far from the centre the moving symbols are allowed to stray, so the
    -- ladder cannot climb over the tapes or out of the panel. Everything is a
    -- fraction of the panel size, so a small Game panel gets a small HUD rather
    -- than a clipped one.
    local ppd    = P.pixels_per_degree
    local band   = h * 0.34    -- vertical reach of the ladder, up and down
    local margin = math.max(66, w * 0.10)   -- where the side tapes sit

    -- =======================================================================
    -- PITCH LADDER
    --
    -- A line for every ladder_step degrees of climb or dive, plus the horizon.
    -- Each one is placed by the same rule: the ladder line for angle A appears
    -- (pitch - A) degrees below the nose. Check it against the two cases you can
    -- picture - flying level, the horizon (A = 0) is dead centre; pitched ten
    -- degrees up, the horizon has slid ten degrees BELOW the nose, and the "10"
    -- bar has arrived at the centre because that is the angle being flown.
    --
    -- `along` and `down` then carry the roll: the line runs along the horizon
    -- direction and is offset along the gravity direction, both of which were
    -- worked out above and are already rotated.
    -- =======================================================================
    if P.show_ladder > 0 and player ~= nil then
        -- Turns a position in ladder space - `s` sideways from the centre of the
        -- bar, `d` downward toward the ground - into a screen pixel.
        local function pt(s, d)
            return cx + horz_x * s + down_x * d,
                   cy + horz_y * s + down_y * d
        end

        local half  = math.min(120, w * 0.16)   -- half the width of a ladder bar
        local gap   = half * 0.42               -- the clear space either side of centre
        local tick  = 9                         -- length of the little end ticks
        local lsize = 14                        -- the size of the ladder numbers

        -- ---- The horizon itself --------------------------------------------
        -- Drawn longer than the other bars and with no number, because it is the
        -- one line whose meaning needs no label. The centre gap keeps it from
        -- crossing out the gunsight.
        local hd = pitch * ppd
        if math.abs(hd) < band then
            local ax1, ay1 = pt(-half * 1.9, hd)
            local ax2, ay2 = pt(-gap,        hd)
            local bx1, by1 = pt( gap,        hd)
            local bx2, by2 = pt( half * 1.9, hd)
            Draw.line(ax1, ay1, ax2, ay2)
            Draw.line(bx1, by1, bx2, by2)
            -- Short downward stubs at the outer ends, which is what tells you at
            -- a glance which side of the line is the ground.
            local ex, ey = pt(-half * 1.9, hd + tick)
            Draw.line(ax1, ay1, ex, ey)
            ex, ey = pt(half * 1.9, hd + tick)
            Draw.line(bx2, by2, ex, ey)
        end

        -- ---- The climb and dive bars ---------------------------------------
        local step = P.ladder_step
        if step < 1 then step = 10 end     -- a zero step would loop forever
        for a = -90, 90, step do
            if a ~= 0 then
                local d = (pitch - a) * ppd
                -- Only bars that fall inside the band are drawn. This is the
                -- clipping: there is no scissor rectangle in the Draw API, so a
                -- symbol is kept in bounds by not being asked for at all.
                if math.abs(d) < band then
                    -- The end ticks always point TOWARD the horizon: down on a
                    -- climb bar (the horizon is below it), up on a dive bar.
                    local tdir = (a > 0) and tick or -tick

                    local ax1, ay1 = pt(-half, d)
                    local ax2, ay2 = pt(-gap,  d)
                    local bx1, by1 = pt( gap,  d)
                    local bx2, by2 = pt( half, d)

                    if a > 0 then
                        -- Climb: solid. Above the horizon is where you want to
                        -- be, and a solid line is the more confident symbol.
                        Draw.line(ax1, ay1, ax2, ay2)
                        Draw.line(bx1, by1, bx2, by2)
                    else
                        -- Dive: broken, so descending is recognisable without
                        -- reading a single digit.
                        dashed(ax1, ay1, ax2, ay2, 4)
                        dashed(bx1, by1, bx2, by2, 4)
                    end

                    -- The ticks that close each bar off toward the horizon.
                    local ex, ey = pt(-half, d + tdir)
                    Draw.line(ax1, ay1, ex, ey)
                    ex, ey = pt(half, d + tdir)
                    Draw.line(bx2, by2, ex, ey)

                    -- The angle, at both ends. Both, because in a steep bank one
                    -- end swings off the display and the other stays readable.
                    -- No sign: the solid-or-dashed line already says which way,
                    -- and a minus sign is easy to miss at a glance.
                    local lbl = string.format("%d", math.abs(a))
                    local lx, ly = pt(-half - 6, d)
                    text_r(lbl, lx, ly, lsize)
                    lx, ly = pt(half + 6, d)
                    text_l(lbl, lx, ly, lsize)
                end
            end
        end
    end

    -- =======================================================================
    -- THE GUNSIGHT
    --
    -- Dead centre, because that is where the nose points and gun.lua fires
    -- straight along the nose. Four ticks with a hole in the middle rather than
    -- a solid cross: the hole is what keeps a distant target visible instead of
    -- covered by the very mark meant to aim at it.
    -- =======================================================================
    if P.show_crosshair > 0 then
        Draw.line(cx - 18, cy, cx - 6, cy)
        Draw.line(cx + 6,  cy, cx + 18, cy)
        Draw.line(cx, cy - 18, cx, cy - 6)
        Draw.line(cx, cy + 6,  cx, cy + 18)
        Draw.circle(cx, cy, 1.5)
    end

    -- =======================================================================
    -- FLIGHT PATH MARKER
    --
    -- The most useful symbol on a jet HUD, and the one that makes it recognisable
    -- as one: a small winged circle showing where the aircraft is actually GOING,
    -- as opposed to where it is pointing. The two are never quite the same - a
    -- wing has to meet the air at an angle to make lift, so a flying aircraft is
    -- always travelling slightly below its nose.
    --
    -- Read against the pitch ladder it answers the question that matters: put
    -- the marker on the horizon and you will hold your height, put it on the
    -- "10" bar and you are climbing at ten degrees whatever the nose is doing.
    -- Put it on a hill and you will hit the hill.
    --
    -- Its position is the velocity written in the aircraft's own axes: how far
    -- the velocity lies off the nose to the right, and how far below it. Those
    -- two angles are sideslip and angle of attack, and they are exactly the
    -- horizontal and vertical offsets of the marker.
    -- =======================================================================
    local aoa = 0
    if P.show_fpm > 0 and player ~= nil then
        local sp = math.sqrt(vel_x * vel_x + vel_y * vel_y + vel_z * vel_z)
        -- Below a walking pace the direction of travel is noise, and a marker
        -- skating around the display would be worse than none.
        if sp > 5 then
            local ux, uy, uz = vel_x / sp, vel_y / sp, vel_z / sp

            local f_dot = dot(ux, uy, uz, fwd.x, fwd.y, fwd.z)
            local r_dot = dot(ux, uy, uz, rgt.x, rgt.y, rgt.z)
            local u_dot = dot(ux, uy, uz, up.x, up.y, up.z)

            -- atan of "how far off to the side" over "how far ahead" gives the
            -- angle between the velocity and the nose. Angle of attack is the
            -- same calculation in the vertical: negated because travelling BELOW
            -- the nose is what counts as positive angle of attack.
            local slip = math.deg(math.atan(r_dot, f_dot))
            aoa        = math.deg(math.atan(-u_dot, f_dot))

            -- Held inside the display. Flying sideways or backwards would
            -- otherwise fling the marker off the panel; a real HUD pins it to
            -- the edge in the same situation rather than losing it.
            local fx = cx + clamp(slip * ppd, -w * 0.32, w * 0.32)
            local fy = cy + clamp(aoa  * ppd, -band, band)

            local rad = 7
            Draw.circleLines(fx, fy, rad)
            Draw.line(fx - rad, fy, fx - rad - 11, fy)  -- left wing
            Draw.line(fx + rad, fy, fx + rad + 11, fy)  -- right wing
            Draw.line(fx, fy - rad, fx, fy - rad - 8)   -- tail fin
        end
    end

    -- =======================================================================
    -- BANK SCALE
    --
    -- A fixed arc of ticks below the centre and a pointer that rides it. The
    -- SCALE is painted on the glass and never moves; the POINTER sits on the
    -- world's true vertical, so it swings as the aircraft rolls. `down` is
    -- already that direction, so the pointer needs no angle calculation at all -
    -- it is placed straight along it.
    --
    -- Ticks at ten, twenty, thirty, forty-five and sixty degrees, with the
    -- thirty and sixty marks drawn longer: those are the bank angles a turn is
    -- actually flown at, so they are the ones worth finding without counting.
    -- =======================================================================
    if P.show_bank > 0 and player ~= nil then
        local rad = math.min(band * 0.86, h * 0.30)
        for _, a in ipairs({ 0, 10, -10, 20, -20, 30, -30, 45, -45, 60, -60 }) do
            local ang = math.rad(a)
            -- Straight down the screen is the zero of this scale, so a tick at
            -- angle `a` is that direction turned by `a`.
            local tx, ty = math.sin(ang), math.cos(ang)
            local len = 7
            if a == 0 or math.abs(a) == 30 or math.abs(a) == 60 then len = 12 end
            Draw.line(cx + tx * rad, cy + ty * rad,
                      cx + tx * (rad + len), cy + ty * (rad + len))
        end

        -- The pointer: a triangle sitting on the arc, pointing inward at the
        -- centre, placed along world-down. Its two base corners are found by
        -- stepping sideways along the horizon direction, which keeps it square
        -- to the scale however far it has swung.
        local px_ = cx + down_x * rad
        local py_ = cy + down_y * rad
        local ox  = cx + down_x * (rad + 13)
        local oy  = cy + down_y * (rad + 13)
        Draw.triangle(px_, py_,
                      ox + horz_x * 6, oy + horz_y * 6,
                      ox - horz_x * 6, oy - horz_y * 6)
    end

    -- =======================================================================
    -- AIRSPEED TAPE, on the left
    --
    -- A scrolling ruler rather than a plain number. The number alone tells you
    -- what the speed IS; the ruler tells you what it is DOING, because the ticks
    -- visibly stream past as you accelerate. Every real aircraft display has
    -- moved this way for the same reason.
    --
    -- The tape is built by walking whole multiples of the tick spacing either
    -- side of the current value, so the marks slide smoothly past the pointer
    -- instead of jumping in steps.
    -- =======================================================================
    if P.show_tapes > 0 and speed >= 0 then
        local tape_h = math.min(300, h * 0.46)
        local range  = 200                     -- metres per second across the tape
        local pxu    = tape_h / range          -- pixels per metre per second
        local step   = 25
        local x      = margin

        Draw.line(x, cy - tape_h * 0.5, x, cy + tape_h * 0.5)

        -- The first tick at or below the bottom of the visible range. Rounding
        -- UP to a multiple of the step is what keeps the labels on round numbers.
        local first = math.ceil((speed - range * 0.5) / step) * step
        for v = first, speed + range * 0.5, step do
            if v >= 0 then
                -- Minus, because a higher speed belongs further UP the tape and
                -- pixel y counts downward.
                local y = cy - (v - speed) * pxu
                if v % 50 == 0 then
                    Draw.line(x, y, x + 13, y)
                    text_l(string.format("%d", v), x + 18, y, 14)
                else
                    Draw.line(x, y, x + 7, y)
                end
            end
        end

        -- The live value, in a box hanging off the outside of the tape with a
        -- pointer touching it. The box is where the eye goes for the number; the
        -- tape behind it is for the trend.
        boxed(string.format("%d", math.floor(speed + 0.5)), x - 60, cy, 54, 24, P.text_size)
        Draw.triangle(x, cy, x - 8, cy - 6, x - 8, cy + 6)

        -- Mach, under the box. Above about Mach 0.8 an aircraft behaves quite
        -- differently from the same aircraft at half the speed, so a fast jet is
        -- flown by Mach rather than by metres per second.
        text_l(string.format("M %.2f", speed / P.sound_speed), x - 60, cy + 26, 16)

        -- Load factor, above the box, and amber past 7 G. The F-16 is stressed
        -- to 9 G and its pilot rather less, so this is the number that says a
        -- turn is being overdone.
        local gcol = (math.abs(gload) > 7) and "warn" or "hud"
        text_l(string.format("G %.1f", gload), x - 60, cy - 26, 16, gcol)

        -- Angle of attack, below Mach. It is the wing's angle to the air, and it
        -- is what actually decides a stall - a wing stalls at an angle, not at a
        -- speed, which is why every fighter carries this gauge.
        text_l(string.format("A %.1f", aoa), x - 60, cy + 46, 16)
    end

    -- =======================================================================
    -- ALTITUDE TAPE, on the right
    --
    -- The mirror image of the airspeed tape: ticks and labels face inward from
    -- the right edge and the box hangs off the outside. Altitude here is height
    -- above the world origin, not above the ground beneath - the terrain rises
    -- to several hundred metres, so a low reading is not automatically safe.
    -- =======================================================================
    if P.show_tapes > 0 and player ~= nil then
        local tape_h = math.min(300, h * 0.46)
        local range  = 800                     -- metres across the whole tape
        local pxu    = tape_h / range
        local step   = 50
        local x      = w - margin

        Draw.line(x, cy - tape_h * 0.5, x, cy + tape_h * 0.5)

        local first = math.ceil((alt - range * 0.5) / step) * step
        for v = first, alt + range * 0.5, step do
            local y = cy - (v - alt) * pxu
            if v % 200 == 0 then
                Draw.line(x, y, x - 13, y)
                text_r(string.format("%d", v), x - 18, y, 14)
            else
                Draw.line(x, y, x - 7, y)
            end
        end

        boxed(string.format("%d", math.floor(alt + 0.5)), x + 6, cy, 66, 24, P.text_size)
        Draw.triangle(x, cy, x + 8, cy - 6, x + 8, cy + 6)

        -- Vertical speed, in metres per second, with an explicit sign. Climbing
        -- or descending is not obvious from an altitude that changes slowly, and
        -- this is the number that says whether the ground is getting closer.
        text_l(string.format("VS %+d", math.floor(vel_y + 0.5)), x + 6, cy + 26, 16)
    end

    -- =======================================================================
    -- HEADING TAPE, across the top
    --
    -- Compass headings in the aviation style: hundreds and tens only, so 090
    -- reads as "09" and 270 as "27". The caret under the centre marks the
    -- current heading, and because the tape scrolls under a fixed caret the
    -- direction of a turn is visible before the number has changed.
    -- =======================================================================
    if P.show_tapes > 0 and player ~= nil then
        local span = 60                        -- degrees visible across the tape
        local pxd  = math.min(w * 0.42, 380) / span
        local y    = 46

        local first = math.ceil((heading - span * 0.5) / 5) * 5
        for a = first, heading + span * 0.5, 5 do
            local x = cx + (a - heading) * pxd
            local aa = a % 360
            if aa % 10 == 0 then
                Draw.line(x, y, x, y - 10)
                -- Modulo 36 rather than 360 because the label is in tens: 360
                -- degrees is north and is written 36, and the next tick round
                -- must come out as 00 rather than 36 again.
                text_c(string.format("%02d", (math.floor(aa / 10)) % 36), x, y - 21, 16)
            else
                Draw.line(x, y, x, y - 5)
            end
        end

        Draw.line(cx - w * 0.21, y, cx + w * 0.21, y)
        -- The caret, pointing up at the tape it is reading.
        Draw.triangle(cx, y + 2, cx - 7, y + 12, cx + 7, y + 12)
        -- The heading as a number too, because reading a tape to the nearest
        -- degree is not something a tape is good for.
        text_c(string.format("%03d", math.floor(heading + 0.5) % 360), cx, y + 24, 18)
    end

    -- =======================================================================
    -- ENGINE POWER, bottom left
    --
    -- Published by flight_sim.lua, so it only appears once a flight script has
    -- set it and a scene with no aircraft is not cluttered with a gauge reading
    -- zero forever.
    -- =======================================================================
    local thr = Hud.get("throttle", -1)
    if thr >= 0 then
        thr = clamp(thr, 0, 1)
        local bx, by, bw, bh = 28, h - 52, 168, 12
        text_l(string.format("PWR %3.0f%%", thr * 100), bx, by - 16, 18)
        Draw.rectLines(bx, by, bw, bh)
        Draw.rect(bx + 2, by + 2, (bw - 4) * thr, bh - 4, "dim")
        -- A mark at the point where the afterburner would light on a real
        -- engine, so full military power is a position rather than a guess.
        Draw.line(bx + bw * 0.8, by - 3, bx + bw * 0.8, by + bh + 3)
    end

    -- =======================================================================
    -- DAMAGE, bottom centre
    --
    -- Scene.health returns current and maximum together. An entity with no
    -- Health component reports 0, 0 - which is why the guard is on the MAXIMUM:
    -- a maximum of zero means there is nothing to show, whereas a current of
    -- zero is a real reading and means dead.
    -- =======================================================================
    local hp, hpmax = 0, 0
    if player ~= nil then hp, hpmax = Scene.health(player) end
    if hpmax > 0 then
        local frac = clamp(hp / hpmax, 0, 1)
        local bw, bh = 240, 14
        local bx, by = cx - bw * 0.5, h - 40
        -- Turn the bar amber then red as it empties. Colour carries urgency
        -- faster than a length does - you register "red" before you have read
        -- how much bar is left.
        local tone = "hud"
        if frac < 0.25 then tone = "bad" elseif frac < 0.5 then tone = "warn" end
        text_r("DMG", bx - 8, by + bh * 0.5, 18)
        Draw.rectLines(bx, by, bw, bh)
        Draw.rect(bx + 2, by + 2, (bw - 4) * frac, bh - 4, tone)
        -- Quarter divisions, so "half gone" is something you see rather than
        -- something you estimate.
        for i = 1, 3 do
            local mx_ = bx + bw * i * 0.25
            Draw.line(mx_, by, mx_, by + bh)
        end
    end

    -- =======================================================================
    -- WARNING CAPTIONS
    --
    -- Flashing, at about three a second, because a steady caption on a busy
    -- display is simply more clutter while a flashing one is impossible to
    -- ignore. That is why real warning panels flash rather than merely light up.
    -- =======================================================================
    local flash = (clock * 3) % 2 < 1
    local wy = cy + band + 26
    if flash and player ~= nil then
        -- A wing stalls when it meets the air too steeply, and the airspeed
        -- below which that becomes unavoidable is what stall_speed names. Below
        -- it the aircraft sinks however hard the nose is held up, and the only
        -- cure is to lower the nose and let it accelerate.
        if speed >= 0 and speed < P.stall_speed then
            text_c("STALL", cx, wy, 26, "warn")
        elseif hpmax > 0 and hp / hpmax < 0.25 then
            text_c("DAMAGE", cx, wy, 26, "bad")
        end
    end

    -- =======================================================================
    -- SCORE AND WAVE, in the top corners
    --
    -- Out at the corners rather than across the middle, because the middle of
    -- the top edge now belongs to the heading tape. Game state is not flight
    -- information and does not deserve the centre of the display.
    -- =======================================================================
    local score = Hud.get("score", -1)
    if score >= 0 then
        text_l(string.format("SCORE %d", score), 28, 26, P.text_size)
    end
    local wave = Hud.get("wave", -1)
    if wave >= 0 then
        text_r(string.format("WAVE %d", wave), w - 28, 26, P.text_size)
    end

    -- How many enemy camps are still standing - the thing the whole sortie is
    -- about, so it sits under the wave counter rather than among the flight
    -- instruments. Published by camps.lua, and absent in a scene that has no
    -- camps, which is why it hides itself rather than reading zero.
    local camps = Hud.get("camps", -1)
    if camps >= 0 then
        local s = (camps > 0) and string.format("CAMPS %d", camps)
                              or "ALL CAMPS DESTROYED"
        text_r(s, w - 28, 26 + P.text_size + 6, 18,
               (camps > 0) and "hud" or "warn")
    end

    -- =======================================================================
    -- GAME OVER
    --
    -- Drawn last so it covers everything above it. gamemanager.lua sets the
    -- flag; the editor watches the same flag to know that R should restart.
    -- =======================================================================
    if Hud.get("game_over", 0) > 0 then
        Draw.rect(0, 0, w, h, "dark")           -- dim the frozen world

        local t1 = "GAME OVER"
        text_c(t1, cx, cy - 46, 48, "bad")

        local t2 = string.format("SCORE %d", Hud.get("score", 0))
        text_c(t2, cx, cy + 12, 28, "white")

        local t3 = "Press R to restart"
        text_c(t3, cx, cy + 52, 20, "white")
    end
end
