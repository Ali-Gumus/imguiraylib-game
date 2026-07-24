-- GENERATED from a node graph. Edit the GRAPH, not this file --

properties = {
    respawn_delay = 2,
    base_count = 3,
    per_wave = 1,
    spawn_radius = 60,
    spawn_height = 20,
}

local wave = 0
local timer = 0
local had_player = 0
local over = 0
local wcount = 0
local ang = 0
local xx = 0
local zz = 0

function on_start(entity)
    hud.set("score", 0)
    hud.set("wave", 0)
    hud.set("game_over", 0)
    wave = 0
    timer = 1
    had_player = 0
    over = 0
end

function on_update(entity, dt)
    if (scene.count("player") > 0) then
    had_player = 1
    end
    if ((had_player > 0) and (scene.count("player") == 0)) then
    hud.set("game_over", 1)
    over = 1
    end
    if ((over <= 0) and (scene.count("enemy") == 0)) then
    timer = (timer - dt)
    if (timer <= 0) then
    wave = (wave + 1)
    timer = properties.respawn_delay
    hud.set("wave", wave)
    wcount = (properties.base_count + ((wave - 1) * properties.per_wave))
    for i163 = 1, wcount do
    ang = ((i163 / wcount) * 6.283)
    xx = (math.cos(ang) * properties.spawn_radius)
    zz = (math.sin(ang) * properties.spawn_radius)
    scene.spawn("enemy", xx, properties.spawn_height, zz, (0 - xx), 0, (0 - zz), "assets/scripts/enemy.lua", "enemy", 3)
    end
    end
    end
end

