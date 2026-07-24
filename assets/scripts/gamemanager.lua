-- gamemanager.lua
-- =============================================================================
-- The game's rules live here, separate from any single jet or bullet. Attach it
-- to one empty entity in the scene. It uses the shared HUD value store (hud.set
-- / hud.get / hud.add) as game state, so other scripts and the C++ HUD can read
-- the same numbers:
--   * "score" -- points earned; bullets add to it on a kill
--   * "wave"  -- the current wave number
--
-- Waves: the field starts empty; once it is clear of enemies (and a short delay
-- passes) the next, larger wave spawns in a ring around the origin. Each enemy
-- is spawned tagged "enemy" with health, so the player's bullets can kill it.
-- (Game-over handling will be added here in the next step.)
-- =============================================================================

-- Tunable numbers, shown as editable fields in the Inspector.
properties = {
    base_count    = 3,    -- enemies in the first wave
    per_wave      = 1,    -- extra enemies added each following wave
    spawn_radius  = 60,   -- how far from the origin enemies appear
    spawn_height  = 20,   -- the height they appear at
    enemy_hp      = 3,    -- hit points per enemy (bullet damage is 1)
    respawn_delay = 2,    -- seconds between clearing a wave and the next
}

local wave       = 0      -- current wave number (runtime state)
local timer      = 0      -- seconds until the next wave may start
local had_player = false  -- have we seen the player alive yet?

function on_start(entity)
    -- A fresh run starts at zero. Play deep-clones the scene and runs on_start,
    -- so this resets every time you press Play.
    hud.set("score", 0)
    hud.set("wave", 0)
    hud.set("game_over", 0)
    wave       = 0
    timer      = 1        -- a short beat before the first wave
    had_player = false
end

function on_update(entity, dt)
    -- Nothing more to do once the game is over (the engine freezes the world
    -- and waits for the restart key).
    if hud.get("game_over") > 0 then return end

    -- Watch the player: once one has existed, its disappearance (health gone,
    -- entity destroyed) means the player died -> game over.
    if scene.count("player") > 0 then
        had_player = true
    elseif had_player then
        hud.set("game_over", 1)
        return
    end

    -- Waves only advance when the field is clear. While enemies remain, wait.
    if scene.count("enemy") > 0 then return end

    -- Field is clear: count down the between-waves delay.
    timer = timer - dt
    if timer > 0 then return end

    -- Start the next wave.
    wave  = wave + 1
    timer = properties.respawn_delay
    hud.set("wave", wave)

    local count = properties.base_count + (wave - 1) * properties.per_wave
    local r     = properties.spawn_radius
    for i = 1, count do
        -- Spread them evenly around a ring so they don't stack up.
        local ang = (i / count) * 6.2831853
        local x = math.cos(ang) * r
        local z = math.sin(ang) * r
        -- Spawn facing roughly toward the origin so they head inward, tagged
        -- "enemy" with health so bullets can hit and kill them.
        scene.spawn("Enemy", x, properties.spawn_height, z, -x, 0, -z,
                    "assets/scripts/enemy.lua", "enemy", properties.enemy_hp)
    end
end
