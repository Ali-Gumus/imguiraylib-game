#include "engine/Particles.h"

#include "raymath.h"    // Vector3 maths
#include "rlgl.h"       // depth-mask control and render-batch flushing
#include "sol/sol.hpp"  // runs effects.lua, where the effect recipes live

#include <algorithm>    // std::sort
#include <cstdlib>      // rand
#include <string>
#include <unordered_map>
#include <vector>

namespace eng {

// ---------------------------------------------------------------------------
// One particle. Everything needed to move it and colour it, and nothing else.
// ---------------------------------------------------------------------------
struct Particle {
    Vector3 pos{};          // where it is now
    Vector3 vel{};          // its OWN motion: the outward spread, in units/second
    // The motion of whatever emitted it - a firing aircraft, an exploding
    // vehicle - carried separately from `vel` and deliberately NOT subject to
    // drag or gravity.
    //
    // WHY IT IS A SECOND VECTOR RATHER THAN PART OF THE FIRST. Drag exists to
    // slow a particle's spread down so a burst spreads and settles. Applied to
    // the emitter's motion as well it does something quite different: it brakes
    // the whole effect to a halt in mid-air while the thing that fired it flies
    // on, so the effect slides backwards relative to its own emitter. The
    // distance it slides is proportional to the emitter's SPEED, which is what
    // makes this invisible on anything slow and glaring on anything fast - at
    // 100 units a second a muzzle flash lagged 8 metres and hid inside the nose,
    // and at an aircraft's real 685 it lagged 59 and swept back through the
    // whole airframe.
    //
    // Keeping it separate means the burst spreads and settles exactly as its
    // recipe says, in a frame of reference that travels with the emitter.
    Vector3 carrier{};
    float   age   = 0.0f;   // seconds since it was created
    float   life  = 1.0f;   // seconds it will exist in total
    float   size0 = 1.0f;   // width in world units at birth
    float   size1 = 0.0f;   // width at death (shrinking reads as fading energy)
    Color   c0{255, 255, 255, 255};   // colour at birth
    Color   c1{255, 255, 255, 0};     // colour at death
    float   gravity = 0.0f; // downward acceleration, world units per second^2
    float   drag    = 0.0f; // how quickly it loses speed, per second

    // --- ATTACHED effects ------------------------------------------------
    // The entity this particle is pinned to, or kInvalidEntity for the usual
    // free-flying kind. An attached particle ignores `carrier` entirely: every
    // frame it is placed at its entity's current position and orientation plus
    // `anchor`, with only its own spread added on top.
    //
    // WHY CARRYING THE EMITTER'S VELOCITY IS NOT ENOUGH, which is not obvious.
    // A carried particle is released with the emitter's velocity and then flies
    // STRAIGHT. An aircraft does not: lift bends its path continuously. So the
    // particle is left behind in the direction opposite the lift - toward the
    // belly - and since lift points out of the canopy whatever the aircraft is
    // doing, the effect slides bellyward at ANY roll or pitch. Measured on a
    // 0.2 s effect: 0.19 m at 2.2 g, 0.45 m at 3.9 g, and the other way in
    // negative g.
    //
    // That is right for something genuinely released into the air - smoke, a
    // spark, a shell case. It is wrong for a flame, which is combustion held at
    // the nozzle and does not fly off the back. Those are attached instead.
    EntityID follow = kInvalidEntity;
    Vector3  anchor{};      // where on the entity, in its OWN axes
    Vector3  spread{};      // how far its own motion has carried it from there
};

// The recipe for one kind of effect. Values given as a low..high pair are
// picked randomly per particle, which is what stops a burst from looking like
// a machine-stamped pattern.
struct Preset {
    int   count;                    // how many particles per burst
    float speedMin, speedMax;       // initial outward speed
    float lifeMin,  lifeMax;        // seconds alive
    float size0,    size1;          // world size at birth and at death
    Color c0,       c1;             // colour at birth and at death
    float gravity;                  // downward pull (negative = falls)
    float drag;                     // air resistance
    float upBias;                   // how much the burst favours going upward
};

// ---------------------------------------------------------------------------
// File-scope state. `static` keeps these names private to this file.
// ---------------------------------------------------------------------------

// The pool never grows past this. 2048 particles is enough for several
// simultaneous explosions; beyond that the oldest are recycled.
static constexpr int kMaxParticles = 2048;

static std::vector<Particle> s_particles;      // live particles only
static Texture2D             s_dot{};          // the soft round dot they draw as
static bool                  s_ready = false;

// A random float between lo and hi. rand() returns a whole number from 0 to
// RAND_MAX, so dividing by RAND_MAX gives a fraction from 0 to 1, which is then
// stretched across the range asked for.
static float RandRange(float lo, float hi) {
    float t = (float)rand() / (float)RAND_MAX;
    return lo + (hi - lo) * t;
}

// A random direction: a point picked anywhere on the surface of a sphere, so a
// burst spreads evenly in every direction instead of favouring the corners of a
// cube (which is what picking three independent random numbers would do).
static Vector3 RandDirection() {
    // Keep drawing points inside a cube until one lands inside the sphere that
    // fits in it, then push that point out to the sphere's surface. Simple, and
    // it needs on average only two tries.
    for (int i = 0; i < 8; i++) {
        Vector3 v{RandRange(-1, 1), RandRange(-1, 1), RandRange(-1, 1)};
        float len2 = Vector3LengthSqr(v);
        if (len2 > 0.0001f && len2 <= 1.0f)
            return Vector3Scale(v, 1.0f / sqrtf(len2));
    }
    return {0.0f, 1.0f, 0.0f};   // gave up: straight up is a fine fallback
}

// Every effect recipe, looked up by name. These come from
// assets/scripts/effects.lua, which is the ONLY place the numbers live: there is
// deliberately no second copy in C++ to drift out of step with it.
static std::unordered_map<std::string, Preset> s_presets;
static std::string              s_presetError;
static std::vector<std::string> s_presetNames;   // sorted, for the node editor

// The one recipe built into the engine. It is not a copy of any real effect -
// it is the "something is wrong" effect, used when a script asks for a name
// nothing defines, or when effects.lua could not be read at all. A small
// neutral grey puff: clearly visible so the mistake is noticed, clearly not an
// explosion so it is never mistaken for the real thing.
static const Preset& FallbackPreset() {
    static const Preset kFallback{
        /*count*/     8,
        /*speed*/     2.0f, 6.0f,
        /*life*/      0.2f, 0.4f,
        /*size*/      0.4f, 0.1f,
        /*colours*/   Color{200, 200, 210, 180}, Color{120, 120, 130, 0},
        /*gravity*/   0.0f,
        /*drag*/      3.0f,
        /*upBias*/    0.0f,
    };
    return kFallback;
}

// Read one colour from a Lua table written as {r, g, b, a}, keeping the current
// value if the field is absent or malformed. Channels are 0..255, matching the
// numbers the editor's colour pickers show.
static Color ColorFromLua(const sol::table& t, const char* key, Color current) {
    sol::optional<sol::table> c = t[key];
    if (!c || !c->valid()) return current;
    auto chan = [&](int i, unsigned char fallback) -> unsigned char {
        sol::optional<float> v = (*c)[i];
        if (!v) return fallback;
        float f = *v;
        if (f < 0.0f)   f = 0.0f;
        if (f > 255.0f) f = 255.0f;
        return (unsigned char)f;
    };
    // Lua tables are indexed from 1, not 0.
    return { chan(1, current.r), chan(2, current.g),
             chan(3, current.b), chan(4, current.a) };
}

bool ReloadEffectPresets() {
    s_presetError.clear();

    // Start empty every time. The file is the whole truth, so an effect deleted
    // from it stops existing rather than lingering from an earlier load.
    s_presets.clear();
    s_presetNames.clear();

    const char* path = "assets/scripts/effects.lua";
    if (!FileExists(path)) {
        s_presetError = "assets/scripts/effects.lua not found; using built-in effects";
        return false;
    }

    // A small private interpreter. It exists only to run this one file, and is
    // thrown away as soon as it has: the recipes it defines live in C++.
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math);

    sol::table fx = lua.create_named_table("Fx");
    // fx.define(name, settings): create or retune an effect. Every field is
    // optional and anything left out keeps its present value, so a file can
    // adjust one number without restating the whole recipe.
    fx["define"] = [](const std::string& name, sol::table t) {
        // Start from the existing recipe if this name has already been defined
        // (so a later call can adjust one field), otherwise from the neutral
        // fallback, which every unset field then inherits.
        Preset p = s_presets.count(name) ? s_presets[name] : FallbackPreset();

        // A tiny helper so each line below reads as "this field, or what it
        // already was".
        auto num = [&](const char* key, float current) -> float {
            sol::optional<float> v = t[key];
            return v ? *v : current;
        };

        p.count    = (int)num("count", (float)p.count);
        p.speedMin = num("speed_min", p.speedMin);
        p.speedMax = num("speed_max", p.speedMax);
        p.lifeMin  = num("life_min",  p.lifeMin);
        p.lifeMax  = num("life_max",  p.lifeMax);
        p.size0    = num("size_start", p.size0);
        p.size1    = num("size_end",   p.size1);
        p.gravity  = num("gravity",    p.gravity);
        p.drag     = num("drag",       p.drag);
        p.upBias   = num("up_bias",    p.upBias);
        p.c0       = ColorFromLua(t, "color_start", p.c0);
        p.c1       = ColorFromLua(t, "color_end",   p.c1);

        // Guard the values that would break the maths or the pool: a negative
        // count, a zero lifetime (which would divide by zero when fading), or
        // a single burst large enough to empty the pool by itself.
        if (p.count < 0)         p.count = 0;
        if (p.count > 512)       p.count = 512;
        if (p.lifeMin < 0.01f)   p.lifeMin = 0.01f;
        if (p.lifeMax < p.lifeMin) p.lifeMax = p.lifeMin;

        s_presets[name] = p;
    };

    // safe_script_file reports an error instead of throwing, so a mistake in
    // the file leaves the built-in recipes in place rather than stopping play.
    sol::protected_function_result r = lua.safe_script_file(path, sol::script_pass_on_error);
    if (!r.valid()) {
        s_presetError = r.get<sol::error>().what();
        return false;
    }

    // Collect the names for the node editor's dropdown, sorted so the menu does
    // not reshuffle itself between runs (a hash map has no reliable order).
    for (const auto& kv : s_presets) s_presetNames.push_back(kv.first);
    std::sort(s_presetNames.begin(), s_presetNames.end());
    return true;
}

const char* EffectPresetError() { return s_presetError.c_str(); }

const std::vector<std::string>& EffectPresetNames() { return s_presetNames; }

bool InitParticles() {
    s_particles.reserve(kMaxParticles);
    ReloadEffectPresets();

    // Build the particle image instead of loading one from disk: a small soft
    // dot, bright in the middle and fading to nothing at the edge. Generating
    // it means there is no asset file to ship, no path to get wrong, and
    // nothing that can fail to load.
    Image img = GenImageGradientRadial(32, 32, 0.0f, WHITE, Color{255, 255, 255, 0});
    s_dot = LoadTextureFromImage(img);
    UnloadImage(img);            // the CPU-side copy is not needed once uploaded

    // Smooth the texture when it is drawn larger than 32 pixels, so a close-up
    // particle is a soft blob rather than a blocky square.
    if (IsTextureValid(s_dot)) SetTextureFilter(s_dot, TEXTURE_FILTER_BILINEAR);

    s_ready = IsTextureValid(s_dot);
    return s_ready;
}

void ShutdownParticles() {
    if (s_ready) {
        UnloadTexture(s_dot);
        s_ready = false;
    }
    s_particles.clear();
}

// Fire a burst using a recipe by name. Everything funnels through here so that
// the C++ and Lua entry points cannot drift apart.
// The one emitter both public entry points funnel through. `follow` is
// kInvalidEntity for an ordinary burst, in which case `pos` is a world point
// and `inherit` the emitter's velocity; for an attached one `pos` is the anchor
// in the entity's own axes and `inherit` is unused.
static void BurstPreset(const Preset& r, Vector3 pos, float scale, Vector3 inherit,
                        EntityID follow = kInvalidEntity) {
    if (!s_ready) return;
    if (scale <= 0.0f) scale = 1.0f;

    for (int i = 0; i < r.count; i++) {
        Particle p;
        p.pos = pos;

        // Outward in a random direction, biased upward for the effects where
        // hot gas would rise.
        Vector3 dir = RandDirection();
        dir.y += r.upBias;
        dir = Vector3Normalize(dir);
        p.vel = Vector3Scale(dir, RandRange(r.speedMin, r.speedMax) * scale);
        // The motion of whatever fired the burst, so an effect from a moving
        // object travels with it instead of being left behind at the spot where
        // it was born. Kept apart from the spread above so that drag cannot bleed
        // it away - see the note on Particle::carrier.
        p.carrier = inherit;

        // An attached particle keeps its anchor instead, and starts with no
        // spread of its own accumulated yet. Its `pos` is filled in by the
        // first update from the entity it is pinned to, so it is left at the
        // anchor here rather than at a world point that would be meaningless.
        p.follow = follow;
        if (follow != kInvalidEntity) {
            p.anchor  = pos;
            p.spread  = {0.0f, 0.0f, 0.0f};
            p.carrier = {0.0f, 0.0f, 0.0f};
        }

        p.life    = RandRange(r.lifeMin, r.lifeMax);
        p.age     = 0.0f;
        p.size0   = r.size0 * scale;
        p.size1   = r.size1 * scale;
        p.c0      = r.c0;
        p.c1      = r.c1;
        p.gravity = r.gravity;
        p.drag    = r.drag;

        if ((int)s_particles.size() < kMaxParticles) {
            s_particles.push_back(p);
        } else {
            // The pool is full. Overwrite the particle nearest the end of its
            // life: it is the one whose disappearance nobody will notice, and
            // this way a new burst always shows rather than being dropped.
            int   oldest = 0;
            float most   = -1.0f;
            for (int j = 0; j < (int)s_particles.size(); j++) {
                float used = s_particles[j].age / s_particles[j].life;
                if (used > most) { most = used; oldest = j; }
            }
            s_particles[oldest] = p;
        }
    }
}

void BurstNamedOn(const char* preset, EntityID entity, Vector3 anchor,
                  float scale) {
    if (preset == nullptr || entity == kInvalidEntity) return;
    auto it = s_presets.find(preset);
    const Preset& r = (it != s_presets.end()) ? it->second : FallbackPreset();
    BurstPreset(r, anchor, scale, {0.0f, 0.0f, 0.0f}, entity);
}

void BurstNamed(const char* preset, Vector3 pos, float scale, Vector3 inherit) {
    if (preset == nullptr) return;
    auto it = s_presets.find(preset);
    if (it != s_presets.end()) {
        BurstPreset(it->second, pos, scale, inherit);
        return;
    }
    // Nothing defines that name. Draw the neutral fallback puff so the mistake
    // is visible on screen, rather than silently drawing nothing and leaving
    // someone hunting for a bug in their script.
    BurstPreset(FallbackPreset(), pos, scale, inherit);
}

// The world pose of an entity an attached particle is pinned to, cached for the
// frame. Without the cache this would decompose a matrix once PER PARTICLE, and
// a plume is a hundred of them all pinned to the same aircraft.
namespace {
struct PoseCache {
    EntityID   id = kInvalidEntity;
    bool       found = false;
    Vector3    pos{};
    Quaternion rot{0.0f, 0.0f, 0.0f, 1.0f};
};
PoseCache s_pose;   // one entry is enough: particles are walked in creation
                    // order, so a burst's worth of them hit it in a row
}

static bool LookUpPose(Scene& scene, EntityID id, Vector3& pos, Quaternion& rot) {
    if (s_pose.id != id) {
        s_pose.id    = id;
        s_pose.found = false;
        if (const Entity* e = scene.FindConst(id)) {
            s_pose.found = true;
            if (e->parent == kInvalidEntity) {
                // The overwhelming case, and the cheap one: a local transform
                // with no parent already IS the world transform.
                s_pose.pos = e->transform.position;
                s_pose.rot = e->transform.rotation;
            } else {
                Vector3 scl;
                MatrixDecompose(scene.WorldMatrix(*e, /*ignoreScale=*/true),
                                &s_pose.pos, &s_pose.rot, &scl);
            }
        }
    }
    pos = s_pose.pos;
    rot = s_pose.rot;
    return s_pose.found;
}

void UpdateParticles(Scene& scene, float dt) {
    if (dt <= 0.0f) return;

    // A new frame: whatever was cached last frame is stale.
    s_pose.id = kInvalidEntity;

    for (int i = 0; i < (int)s_particles.size(); ) {
        Particle& p = s_particles[i];
        p.age += dt;

        if (p.age >= p.life) {
            // Retire it by moving the LAST particle into this slot and dropping
            // the end of the list. That is a single copy; erasing from the
            // middle would instead shuffle everything after it down one place.
            // Order does not matter here, so the cheap way is the right way.
            p = s_particles.back();
            s_particles.pop_back();
            continue;               // the moved particle now needs its own turn
        }

        // Gravity accelerates it downward; drag bleeds speed away. Multiplying
        // by dt is what keeps the motion the same at any frame rate. Both act on
        // the particle's OWN spread only - the carried motion of the emitter is
        // added afterwards and is left alone.
        p.vel.y += p.gravity * dt;
        float keep = 1.0f - p.drag * dt;
        if (keep < 0.0f) keep = 0.0f;      // a huge dt must not reverse it
        p.vel = Vector3Scale(p.vel, keep);

        if (p.follow != kInvalidEntity) {
            Vector3    ePos;
            Quaternion eRot;
            if (LookUpPose(scene, p.follow, ePos, eRot)) {
                // Placed afresh from the entity every frame rather than moved
                // along from where it was. That is the whole point: no amount
                // of manoeuvring can leave it behind, because its position is
                // never integrated in the first place.
                //
                // The anchor is in the entity's own axes so the effect stays on
                // the nozzle through any roll; the spread is added in WORLD
                // axes, because a puff that has already drifted outward should
                // not be swung about by the aircraft rolling afterwards.
                p.spread = Vector3Add(p.spread, Vector3Scale(p.vel, dt));
                p.pos = Vector3Add(
                    Vector3Add(ePos, Vector3RotateByQuaternion(p.anchor, eRot)),
                    p.spread);
            } else {
                // The entity has gone - shot down, or the run stopped. Cut the
                // particle loose exactly where it is and let it finish its life
                // as an ordinary one, so a burst never blinks out of existence
                // because the thing that made it was destroyed.
                p.follow = kInvalidEntity;
            }
        } else {
            p.pos = Vector3Add(p.pos,
                               Vector3Scale(Vector3Add(p.vel, p.carrier), dt));
        }

        i++;
    }
}

void DrawParticles(const Camera3D& camera) {
    if (!s_ready || s_particles.empty()) return;

    // Send anything already queued to the GPU before changing render state.
    // raylib collects shapes into a batch and draws them later, but a state
    // change takes effect immediately - so without this flush the settings
    // below would be applied to the scene geometry instead of the particles.
    rlDrawRenderBatchActive();

    // ADDITIVE blending: each particle ADDS its colour to what is already on
    // screen instead of covering it. Overlapping particles therefore brighten
    // towards white, which is precisely how fire and sparks behave, and it also
    // means they can be drawn in any order without looking wrong.
    BeginBlendMode(BLEND_ADDITIVE);
    // Depth TESTING stays on, so particles behind a hill are correctly hidden.
    // Depth WRITING is turned off, so particles do not block each other: with
    // it on, whichever particle drew first would carve a hole in the ones
    // behind it, and the burst would break into visible plates.
    rlDisableDepthMask();

    // --- The billboard's own axes, worked out ONCE for the whole burst -------
    //
    // WHY THIS IS NOT DrawBillboard(). raylib's convenience version is
    // "billboard locked on axis-Y": it takes `right` from the camera but hard-
    // codes `up` to WORLD up, {0,1,0}. That is right for a tree or a bush,
    // which should stay upright however the camera moves - and quietly wrong
    // for a particle.
    //
    // The failure is not a small one, and it appears exactly when the game is
    // most interesting to look at. The chase camera ROLLS with the aircraft
    // (see chasecam.lua), so in a banked turn the camera's right vector tilts
    // toward the vertical - toward world up. The two vectors the quad is built
    // from therefore converge: the square shears into a parallelogram, and as
    // the bank approaches ninety degrees they become parallel and it collapses
    // to a line. Effects look like flat cut-outs and then vanish, and because
    // the quad is centred by stepping back along both axes, an unsquare one is
    // also centred in the wrong PLACE - which reads as the effect sitting
    // ahead of or behind where it was fired, depending which way the turn went.
    //
    // Taking BOTH axes from the view matrix fixes it. The quad is then always
    // square-on to the camera whatever the camera is doing, which is what a
    // billboard is for. The rows are the view matrix's first two columns, which
    // is where raylib itself reads `right` from.
    const Matrix matView = MatrixLookAt(camera.position, camera.target,
                                        camera.up);
    const Vector3 camUp{matView.m1, matView.m5, matView.m9};

    // The whole texture, every time - these particles are one soft dot.
    const Rectangle src{0.0f, 0.0f, (float)s_dot.width, (float)s_dot.height};

    for (const Particle& p : s_particles) {
        // How far through its life this particle is, from 0 at birth to 1 at
        // death. Everything visual is interpolated across that number.
        float t = p.age / p.life;

        float size = p.size0 + (p.size1 - p.size0) * t;
        Color col{
            (unsigned char)(p.c0.r + (p.c1.r - p.c0.r) * t),
            (unsigned char)(p.c0.g + (p.c1.g - p.c0.g) * t),
            (unsigned char)(p.c0.b + (p.c1.b - p.c0.b) * t),
            (unsigned char)(p.c0.a + (p.c1.a - p.c0.a) * t),
        };

        // A BILLBOARD is a flat square that always turns to face the camera, so
        // a two-dimensional dot passes for a three-dimensional puff from every
        // angle. It is the standard way to draw particles: real geometry per
        // particle would cost hundreds of times more for no visible gain.
        //
        // The origin is half the size along each axis, which is what centres
        // the square on the particle's position rather than hanging it from a
        // corner.
        DrawBillboardPro(camera, s_dot, src, p.pos, camUp, {size, size},
                         {size * 0.5f, size * 0.5f}, 0.0f, col);
    }

    // Flush again while the state is still set, then restore it - otherwise the
    // particles would sit in the queue until something else drew them under the
    // wrong settings.
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
}

void ClearParticles() { s_particles.clear(); }

int AliveParticleCount() { return (int)s_particles.size(); }

bool ParticlePosition(int index, Vector3& out) {
    if (index < 0 || index >= (int)s_particles.size()) return false;
    out = s_particles[index].pos;
    return true;
}

} // namespace eng
