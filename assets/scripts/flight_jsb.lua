-- flight_jsb.lua
-- =============================================================================
-- Flies the aircraft through the JSBSim flight model.
--
-- This is the ALTERNATIVE to flight_sim.lua, and the difference between them is
-- the whole point. flight_sim.lua IS a flight model: it carries a velocity
-- vector, adds thrust, takes drag off, and turns the aircraft at a fixed number
-- of degrees per second. This script contains no aerodynamics at all. It reads
-- the keyboard, moves four controls, and a real aerodynamic simulation of an
-- F-16 - lift and drag curves, engine thrust, mass and inertia, read from an
-- aircraft description on disk - decides what the aircraft does about it.
--
-- What that changes, in the air:
--   * the aircraft turns differently at 200 knots than at 500, because the wing
--     really does make less lift when there is less air going over it;
--   * pulling too hard STALLS it - the wing gives up, the nose drops, and no
--     amount of back stick brings it up until speed is regained;
--   * a turn bleeds speed, a dive builds it, and holding altitude in a hard
--     bank needs power, none of which is coded anywhere;
--   * the engine takes seconds to spool up, so the throttle is a request.
--
-- -----------------------------------------------------------------------------
-- SETUP. Put this on the Jet, and give the same entity a JSBSim component
-- (Add Component -> JSBSim Flight Model).
--
-- ONLY ONE THING MAY MOVE THE AIRCRAFT. If flight_sim.lua is also attached,
-- both will write the transform every frame and the result is jitter, not a
-- blend of two models. Switching between them means: stop playing, take
-- flight_sim.lua off (or untick the JSBSim component's `enabled` box), and play
-- again. It does not have to work while playing.
--
-- CONTROLS (with the Game panel focused) - the same keys flight_sim.lua uses:
--   W / S        : nose up / down
--   A / D        : roll left / right
--   Q / E        : rudder left / right
--   SHIFT / CTRL : throttle up / down
--   G            : landing gear up / down
-- =============================================================================

-- properties: a global table of tunable numbers, each shown as an editable
-- field in the Inspector so it can be adjusted per aircraft - even while
-- flying - without editing this file.
--
-- Note how FEW of these there are, and what is missing from the list. There is
-- no thrust, no drag, no turn rate and no stall speed, because none of those is
-- this script's business any more: they are properties of the AIRCRAFT and they
-- live in its description. What is left here is only the feel of the controls.
--
-- (The keys are deliberately snake_case. Scene files store per-entity overrides
-- under these exact names, so renaming one silently discards its tuning.)
properties = {
    -- How far each control is allowed to deflect, 0 to 1. Full authority is 1.
    -- Reducing one makes the aircraft gentler without changing its aerodynamics
    -- - it is the difference between a smaller stick movement and a smaller
    -- wing, and only the first is what a "less twitchy" aircraft usually needs.
    --
    -- The rudder is small on purpose. A jet's rudder coordinates a turn; it
    -- does not steer the aircraft, and a bootful of it mostly makes the
    -- aircraft skid sideways and lose speed.
    pitch_authority = 1.0,
    roll_authority  = 1.0,
    yaw_authority   = 0.5,

    -- A keyboard has two positions and a control stick has a continuum, so the
    -- controls are RAMPED toward what the keys ask for rather than snapped to
    -- it. Without this, tapping W applies full back stick for one frame, which
    -- on an aerodynamic model is a genuine 9 g snatch rather than a small
    -- correction - it is the single biggest reason a real flight model can feel
    -- unflyable from a keyboard.
    --
    -- Deflections per second: how long to reach the stop, and how quickly a
    -- released control returns to centre. Centring is faster because a real
    -- stick is sprung and comes back by itself.
    control_rate = 3.0,
    centre_rate  = 6.0,

    -- How fast the throttle lever moves, in fractions of its travel per second.
    -- 0.4 means two and a half seconds from idle to full - a lever, not a
    -- switch. What the ENGINE does about it is its own affair and takes longer.
    throttle_rate = 0.4,

    -- Where the throttle starts. High, because the run begins in the air at
    -- cruise speed and an aircraft that starts at idle is already decelerating.
    start_throttle = 0.8,

    -- The engine note's volume and pitch at idle, and how much each rises at
    -- full power. Deliberately narrow ranges: an engine at idle is quieter and
    -- lower, not silent and inaudible.
    sound_volume    = 1.0,
    sound_volume_up = 1.0,
    sound_pitch     = 1.5,
    sound_pitch_up  = 1.0,

    -- --- Meeting the ground -------------------------------------------------
    -- Three outcomes, decided by how fast the aircraft is going DOWN when it
    -- arrives and whether it is in any state to be landing.
    --
    -- A LANDING needs all three: the gear down, the wings roughly level, and a
    -- descent rate no worse than a firm arrival. Get all three right and it
    -- costs nothing. This is what the gear key is for, and it is genuinely
    -- possible now that the flight model knows where the ground is.
    landing_speed = 5,     -- metres per second of descent, at most
    landing_bank  = 15,    -- degrees of roll, at most

    -- A SCRAPE is everything gentle that is not a landing: clipping a hilltop,
    -- or putting it down wheels-up. It hurts but is survivable, so misjudging a
    -- low pass is a fright rather than an instant end to the run.
    scrape_speed  = 15,    -- descent rate below which contact only damages
    scrape_damage = 1,     -- hit points lost. The jet starts with 3

    -- A CRASH is anything faster. The number is large rather than exact
    -- because it must finish the aircraft whatever its remaining health is.
    crash_damage  = 999,
}

-- State that must persist between frames. The control positions live here
-- rather than being recomputed, because ramping is by its nature a memory of
-- where the control was last frame.
local elevator = 0
local aileron  = 0
local rudder   = 0
local throttle = 0
local gear_down = false
local wrecked   = false   -- has the ground already finished this aircraft off?

-- Move `current` toward `target` at `rate` units per second, without
-- overshooting. This is the whole of the ramping described above, and it is
-- written once here rather than three times below.
local function approach(current, target, rate, dt)
    local step = rate * dt
    if target > current then
        current = current + step
        if current > target then current = target end
    elseif target < current then
        current = current - step
        if current < target then current = target end
    end
    return current
end

-- One axis of the stick: read two opposing keys, work out what they are asking
-- for, and ramp toward it. Returns the new position.
--
-- `plus` is the key that drives the control positive. Releasing both keys asks
-- for zero, and a control returning to centre uses the faster rate.
local function axis(current, plus, minus, authority, dt)
    local target = 0
    if Input.keyDown(plus)  then target =  authority end
    if Input.keyDown(minus) then target = -authority end

    local rate = properties.control_rate
    if target == 0 then rate = properties.centre_rate end
    return approach(current, target, rate, dt)
end

function onStart(entity)
    throttle = properties.start_throttle
    elevator, aileron, rudder = 0, 0, 0
    gear_down = false
    wrecked   = false

    -- Start the engine note. It runs for as long as the aircraft exists; the
    -- volume and pitch are set every frame below.
    Audio.loopStart("jet")
end

function onUpdate(entity, dt)
    -- The flight model is a component on this same entity. Fetched each frame
    -- rather than remembered: a component reference is only guaranteed valid
    -- within the frame it was taken in.
    local jsb = entity:getComponent_JSBSim()
    if not jsb then return end

    local P = properties

    -- --- The stick ----------------------------------------------------------
    -- W is nose up, and nose up is a NEGATIVE elevator. That is not a mistake
    -- to be corrected: an elevator raises the nose by deflecting UPWARD, which
    -- is a negative surface angle in the aerospace sign convention JSBSim uses.
    -- The minus sign is here, at the one place that translates a key into a
    -- control, rather than being smuggled into the flight model.
    elevator = axis(elevator, "S", "W", P.pitch_authority, dt)
    aileron  = axis(aileron,  "D", "A", P.roll_authority,  dt)
    rudder   = axis(rudder,   "E", "Q", P.yaw_authority,   dt)

    -- --- The throttle -------------------------------------------------------
    if Input.keyDown("SHIFT") then throttle = throttle + P.throttle_rate * dt end
    if Input.keyDown("CTRL")  then throttle = throttle - P.throttle_rate * dt end
    if throttle < 0 then throttle = 0 end
    if throttle > 1 then throttle = 1 end

    -- --- The gear -----------------------------------------------------------
    -- keyPressed, not keyDown: this is a toggle, and a held key would flap it
    -- up and down once a frame.
    if Input.keyPressed("G") then gear_down = not gear_down end

    -- --- Hand it all to the aircraft ----------------------------------------
    -- Every frame, and all of it. Controls PERSIST in the flight model - they
    -- do not spring back on their own, exactly like real ones being held - so
    -- the ramped positions above are the complete truth about the stick and are
    -- written whole rather than only when they change.
    jsb:setStick(elevator, aileron, rudder)
    jsb.throttle = throttle
    jsb.gear = gear_down and 1 or 0

    -- --- Publish what the rest of the game needs ----------------------------
    -- This is the part that is easy to forget, and it fails quietly: none of it
    -- affects flying, so a missing line here leaves the aircraft handling
    -- perfectly with a dead instrument panel.
    --
    -- `speed` is in METRES PER SECOND, not knots. hud.lua divides it by the
    -- speed of sound to show a Mach number and compares it against a stall
    -- speed of 70, both of which are metres per second - so publishing the
    -- indicated airspeed in knots here would put the Mach readout out by a
    -- factor of four and stop the stall warning ever appearing.
    Hud.set("speed", jsb.speed)

    -- The throttle LEVER, not the engine's actual power, because this gauge
    -- shows what the pilot has asked for.
    Hud.set("throttle", throttle)

    -- Tie the engine note to how hard the engine is actually working rather
    -- than to the lever. A jet takes seconds to spool up, and a note that
    -- follows the lever instead sounds like a switch being flicked - the lag is
    -- most of what makes opening the throttle FEEL like acceleration.
    --
    -- Positioned at the aircraft and updated every frame because the aircraft
    -- moves: with a chase camera the engine sits just ahead of the listener so
    -- it stays loud and centred, but the same script on another aircraft is
    -- heard from wherever that one is.
    local power = jsb.enginePower
    local p = entity.transform.position
    Audio.loopAt("jet", p.x, p.y, p.z,
                 P.sound_volume + power * P.sound_volume_up,
                 P.sound_pitch  + power * P.sound_pitch_up)
end

-- Called by the engine when this aircraft strikes something.
--
-- TWO DIFFERENT KINDS OF EVENT ARRIVE HERE, and telling them apart is the first
-- thing this does:
--
--   * A PHYSICS contact, from the rigid-body simulation. An enemy round hitting
--     the aircraft reports to both sides, so this hook is called for it - but
--     the round's own script already applies the damage, plays the sound and
--     destroys itself. There is nothing to do on this side, and doing anything
--     would double it.
--
--   * A GROUND strike, from the flight model. This one has no rigid-body
--     contact behind it at all: a kinematic aircraft against static terrain is
--     a pair the physics simulation never tests, whatever motion type it is
--     given, so the flight model - which is the only thing that knows the shape
--     of the landscape - reports it instead.
--
-- The two are separated by asking whether what was hit IS the terrain, rather
-- than by its tag: a tag is a string anyone can rename in the Inspector, and
-- getting this test wrong means either being shot down by hilltops or flying
-- through them.
--
-- `speed` is the DOWNWARD closing speed, not the total. That distinction is the
-- whole of the landing/crash judgement: an aircraft crossing a valley at 600
-- metres per second is not hitting anything, and one settling onto the ground
-- at 2 is doing it on purpose.
function onCollision(entity, other, speed, x, y, z)
    if not other:hasComponent_Terrain() then return end

    -- The ground is large and the aircraft may touch it repeatedly while it
    -- slides to a halt. Once it has been written off, stop reacting.
    if wrecked then return end

    local P = properties
    local jsb = entity:getComponent_JSBSim()
    local roll = jsb and math.abs(jsb.roll) or 0

    -- --- A landing ----------------------------------------------------------
    -- Gear down, wings level, and arriving gently. Costs nothing, and is worth
    -- having as a real outcome rather than as a technicality: it is the only
    -- reason the gear key exists.
    if gear_down and speed <= P.landing_speed and roll <= P.landing_bank then
        Audio.playAt("impact", x, y, z)
        return
    end

    -- --- A scrape -----------------------------------------------------------
    -- Slow, but not a landing: wheels up, or a wing down, or a clipped hilltop.
    -- Survivable, so a misjudged low pass is a fright rather than the end.
    if speed < P.scrape_speed then
        Fx.burst("spark", x, y, z)
        Audio.playAt("impact", x, y, z)
        Scene.damage(entity, P.scrape_damage)
        return
    end

    -- --- A crash ------------------------------------------------------------
    -- Flying into the ground. Nothing survives this, so it is not scaled by
    -- speed: the aircraft is finished whatever health it had left.
    wrecked = true
    Fx.burst("explosion", x, y, z, 3.0)
    Audio.playAt("explosion", x, y, z)

    -- Stop the engine note before the aircraft goes. There is one stream per
    -- sound NAME rather than per source, and nothing stops it when the entity
    -- that started it is destroyed - so without this the wreck keeps howling
    -- over the game-over screen.
    Audio.loopStop("jet")

    -- Destroying the player is what ends the run: gamemanager.lua watches for
    -- the player tag disappearing and raises the game-over screen. Damage
    -- rather than a direct destroy, so the health bar and any other listener
    -- see the same thing they would from being shot down.
    Scene.damage(entity, P.crash_damage)
end

-- The aircraft is going away, for any reason - shot down, crashed, or the run
-- being stopped. Silence the engine, since the loop is keyed by name and would
-- otherwise outlive the thing making the noise.
function onDestroy(entity)
    Audio.loopStop("jet")
end
