-- gamemanager.lua
-- =============================================================================
-- The game's rules live here, separate from any single jet or bullet. Attach it
-- to one empty entity in the scene. It uses the shared HUD value store (hud.set
-- / hud.get / hud.add) as game state, so other scripts and the C++ HUD can read
-- the same numbers:
--   * "score"  -- points earned; bullets add to it on a kill
-- (Enemy waves and game-over handling will be added here in later steps.)
-- =============================================================================

function on_start(entity)
    -- A fresh run starts at zero. Play deep-clones the scene and runs on_start,
    -- so this resets the score every time you press Play.
    hud.set("score", 0)
end

function on_update(entity, dt)
    -- Nothing yet; wave spawning and game-over checks will go here.
end
