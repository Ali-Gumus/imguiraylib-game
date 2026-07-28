#include "engine/Physics.h"

// The scene side of the bridge: physics has to read colliders and rigid bodies
// off entities, and write the resulting motion back onto their transforms.
#include "engine/Components.h"
#include "engine/Scene.h"

#include "raymath.h"   // MatrixDecompose / MatrixInvert / MatrixMultiply

// Jolt's own headers. Jolt.h MUST come first - it configures the library
// (which maths types, which assertions) and every other Jolt header assumes it
// has already been seen.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>      // std::max
#include <chrono>         // measuring how long a step took
#include <cmath>          // std::fabs
#include <cstdarg>        // va_list, for Jolt's printf-style trace callback
#include <cstdio>         // vprintf
#include <memory>         // std::unique_ptr
#include <string>         // std::string
#include <unordered_map>  // the entity -> body table
#include <vector>         // std::vector

// Jolt compiles clean, but it enables warnings that the surrounding project
// does not. This macro (defined by Jolt.h) silences the ones its own headers
// would otherwise trigger here. It must sit at file scope.
JPH_SUPPRESS_WARNINGS

// NOTE, and it matters: there is deliberately NO `using namespace JPH` in this
// file. raylib.h arrives through Physics.h and puts `Color`, `Ray`, `Plane`
// and friends in the GLOBAL namespace, and Jolt has types with those exact
// names. Pulling JPH in wholesale would make every one of them ambiguous. Every
// Jolt name below is therefore written out in full.

namespace eng {
namespace {

// ============================================================================
// Collision layers
// ============================================================================
// A layer is a category, and the pair of filters below decides which
// categories are allowed to collide at all. This is a performance tool, not a
// gameplay one: the broadphase can discard a whole class of pair before doing
// any real work.
//
// Two layers is the standard minimum and all this engine needs yet:
//   NonMoving - scenery: the ground, terrain, buildings. Static bodies.
//   Moving    - anything that actually moves: aircraft, projectiles, debris.
//
// The rule encoded in ObjectLayerPairFilterImpl is that Moving collides with
// everything, and NonMoving collides only with Moving. Two static objects can
// never touch (neither of them can move), so testing them would be wasted work
// on every single frame.
namespace Layers {
    static constexpr JPH::ObjectLayer kNonMoving = 0;
    static constexpr JPH::ObjectLayer kMoving    = 1;
    static constexpr JPH::ObjectLayer kCount     = 2;
}

// The BROADPHASE layers are a second, coarser grouping, used only by the "what
// is near what" acceleration structure. Jolt keeps a separate spatial tree per
// broadphase layer, which is why static scenery gets its own: that tree is
// built once and then never has to be updated, while the tree of moving
// objects is rebuilt constantly.
//
// Here they map one-to-one onto the object layers, which is the usual starting
// point. They are a distinct concept because a game with many object layers
// (player, enemy, bullet, pickup, trigger...) would still want only two or
// three trees.
namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer kNonMoving(0);
    static constexpr JPH::BroadPhaseLayer kMoving(1);
    static constexpr JPH::uint            kCount = 2;
}

// Tells Jolt which broadphase layer each object layer belongs to.
class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerMap() {
        m_map[Layers::kNonMoving] = BroadPhaseLayers::kNonMoving;
        m_map[Layers::kMoving]    = BroadPhaseLayers::kMoving;
    }

    JPH::uint GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::kCount;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        JPH_ASSERT(layer < Layers::kCount);
        return m_map[layer];
    }

    // Only compiled when Jolt's profiler is on; it exists so a profile capture
    // shows readable layer names instead of numbers.
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        switch ((JPH::BroadPhaseLayer::Type)layer) {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::kNonMoving: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::kMoving:    return "MOVING";
            default:                                                       return "UNKNOWN";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer m_map[Layers::kCount];
};

// "Can an object in this object layer touch anything in that broadphase
// layer?" - the coarse test, applied per tree.
class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer objLayer,
                       JPH::BroadPhaseLayer bpLayer) const override {
        switch (objLayer) {
            // Scenery only cares about things that move.
            case Layers::kNonMoving: return bpLayer == BroadPhaseLayers::kMoving;
            // Moving things care about everything.
            case Layers::kMoving:    return true;
            default:                 return false;
        }
    }
};

// "Can these two object layers touch?" - the same rule at the finer level.
class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        switch (a) {
            case Layers::kNonMoving: return b == Layers::kMoving;
            case Layers::kMoving:    return true;
            default:                 return false;
        }
    }
};

// ============================================================================
// Jolt's diagnostic callbacks
// ============================================================================
// Jolt reports problems through two function pointers rather than by throwing,
// so that a game can route them wherever it likes. Left unset they are silent,
// which is the worst option while learning the library.

void TraceImpl(const char* fmt, ...) {
    va_list list;
    va_start(list, fmt);
    std::vprintf(fmt, list);
    va_end(list);
    std::printf("\n");
}

#ifdef JPH_ENABLE_ASSERTS
// Called when an internal Jolt assertion fails. Returning true asks the
// debugger to break on the offending line, which is what you want while
// developing: a physics assertion nearly always means the data handed in was
// wrong (a zero-size shape, a NaN position), and the stack at that moment says
// exactly who handed it in.
bool AssertFailedImpl(const char* expr, const char* message,
                      const char* file, JPH::uint line) {
    std::printf("%s:%u: (%s) %s\n", file, line, expr,
                message != nullptr ? message : "");
    return true;
}
#endif

// ============================================================================
// The world itself
// ============================================================================

// How many bodies the world can ever hold at once. Jolt allocates this up
// front - it does not grow - so it is a ceiling, not a starting size. A few
// thousand is far beyond what a jet dogfight needs and costs only a little
// memory.
constexpr JPH::uint kMaxBodies = 4096;

// How many mutexes guard the body list. 0 means "use Jolt's default". It only
// matters for multi-threaded simulation, which this engine does not do yet.
constexpr JPH::uint kNumBodyMutexes = 0;

// The most PAIRS of bodies that may be near each other simultaneously, and the
// most CONTACT constraints (actual touching points) that may be resolved in
// one step. Both are pre-allocated ceilings. If either is exceeded Jolt drops
// the excess, which shows up as objects sinking through each other under a
// large pile-up.
constexpr JPH::uint kMaxBodyPairs         = 4096;
constexpr JPH::uint kMaxContactConstraints = 2048;

// The size of the per-step scratch buffer Jolt uses for temporary allocations
// during a simulation step. It is claimed once and reused every step, so that
// stepping the world does no heap allocation at all.
constexpr JPH::uint kTempAllocatorBytes = 10 * 1024 * 1024;   // 10 MB

// --- The fixed timestep -----------------------------------------------------
// A physics simulation must be advanced by a CONSTANT amount of time. Feeding
// it the real frame time makes the result depend on the frame rate: the same
// scene behaves differently at 144 fps and 30 fps, a stutter can shove an
// object through a wall, and a stack of boxes that is stable on one machine
// jitters apart on another. That is not a tuning problem, it is inherent - the
// integrator's error grows with the step size, and the constraint solver
// assumes each step resembles the last.
//
// The fix is the standard one: keep a running account of real time that has
// not been simulated yet, and spend it in equal fixed-size steps, leaving the
// remainder in the account for next frame.
constexpr float kFixedStep = 1.0f / 60.0f;   // simulate 60 times a second

// The ceiling on how many fixed steps one frame may run. Without it, a frame
// that took a long time (a breakpoint, a scene load, a window drag) hands over
// a huge backlog, which takes even longer to simulate, which makes the next
// backlog bigger still - the "spiral of death", where the game never catches
// up and locks solid. Discarding the excess instead means the world runs a
// little slow for a moment, which nobody notices.
constexpr int kMaxStepsPerFrame = 4;

// How many collision sub-steps Jolt takes within each fixed step. 1 is the
// documented recommendation for a 60 Hz step; more only helps very fast or
// very heavy contacts.
constexpr int kCollisionSteps = 1;

// Everything the world owns. Grouped in one struct so that starting over is a
// matter of destroying it and building a new one - there is no way to reset
// half of it by accident.
struct World {
    // Jolt requires these three filter objects to stay alive for as long as
    // the PhysicsSystem does: it stores references to them, not copies. Making
    // them members of the same struct is what guarantees that.
    BroadPhaseLayerMap      bpLayerMap;
    ObjectVsBroadPhaseFilter objVsBpFilter;
    ObjectLayerPairFilter    objPairFilter;

    JPH::PhysicsSystem      system;
    JPH::TempAllocatorImpl  tempAllocator{kTempAllocatorBytes};

    // The job system parcels out the work of a step. Jolt is built to spread
    // that across threads, but this engine uses the SINGLE-THREADED
    // implementation on purpose: with a few hundred bodies the threading
    // overhead outweighs the gain, and single-threaded execution is
    // deterministic and vastly easier to debug. Swapping in
    // JobSystemThreadPool later is a one-line change here.
    JPH::JobSystemSingleThreaded jobSystem{JPH::cMaxPhysicsJobs};

    float accumulator = 0.0f;   // unsimulated real time, in seconds
    float lastStepMs  = 0.0f;   // how long the previous frame's stepping took

    // What the simulation knows about one entity.
    struct Tracked {
        JPH::BodyID id;
        MotionType  motion = MotionType::Dynamic;

        // The force and torque scripts have asked for this frame, held here
        // rather than handed to Jolt immediately.
        //
        // The reason is the fixed timestep. Jolt clears a body's accumulated
        // force after every step it takes, but a frame can run zero, one or
        // several steps. Keeping the request on our side lets it be re-applied
        // before EVERY step of the frame, so a continuous force acts for
        // exactly as much simulated time as actually elapsed - which is what
        // makes the result independent of frame rate. Handing it straight to
        // Jolt would apply it to the first step of the frame only, quietly
        // weakening every force as the frame rate rose.
        JPH::Vec3 pendingForce  = JPH::Vec3::sZero();
        JPH::Vec3 pendingTorque = JPH::Vec3::sZero();
    };

    // Which entity owns which simulated body.
    //
    // This table lives HERE and not on RigidBodyComponent on purpose. A body
    // handle is runtime state that means nothing outside a run, and a
    // component field must be copied by Clone() and written by Serialize() to
    // stay consistent - a handle would be meaningless in a saved file and
    // actively wrong in a Play/Stop snapshot, where the copy would claim to
    // own a body the original already owns. Keeping it out of the component
    // sidesteps that whole class of mistake.
    std::unordered_map<EntityID, Tracked> bodies;
};

// The one world, or null when physics is unavailable. unique_ptr because a
// PhysicsSystem is large and must be built after Jolt's global registration
// has run - which rules out a plain global object, whose constructor would run
// before main() and therefore before that registration.
std::unique_ptr<World> g_world;

// Whether Jolt's PROCESS-WIDE state (the allocator, the type factory) has been
// set up. That registration is global and must happen exactly once per run,
// which is why it is tracked separately from the world above.
bool g_backendReady = false;

std::string g_error;

// ============================================================================
// Translating between the engine's world and Jolt's
// ============================================================================

// Small conversions. raylib and Jolt each have their own vector and quaternion
// types holding exactly the same numbers, so these are pure relabelling.
inline JPH::Vec3 ToJolt(Vector3 v)    { return JPH::Vec3(v.x, v.y, v.z); }
inline JPH::Quat ToJolt(Quaternion q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline Vector3    ToRay(JPH::Vec3 v)  { return {v.GetX(), v.GetY(), v.GetZ()}; }
inline Quaternion ToRay(JPH::Quat q)  { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

// Build the Jolt shape described by a ColliderComponent, sized by the entity's
// world scale.
//
// Two things here are worth understanding rather than skimming:
//
// SCALE IS BAKED IN, ONCE. A Jolt shape has no scale of its own - it is a
// fixed piece of geometry, deliberately, because shapes are shared between
// bodies and pre-processed for fast collision. So the entity's scale is
// multiplied into the shape's dimensions when it is built. The consequence is
// that resizing an entity while the game is running does NOT resize its
// collision volume; the body would have to be rebuilt.
//
// SPHERES AND CAPSULES CANNOT BE SQUASHED. A sphere scaled 1,3,1 is an
// ellipsoid, which is not a sphere, and no amount of parameter-fiddling makes
// SphereShape into one. Rather than silently producing the wrong volume, the
// largest axis is used, so a non-uniformly scaled sphere comes out too big
// rather than the wrong shape - too big is at least visible.
JPH::ShapeRefC MakeShape(const ColliderComponent& c, Vector3 scale) {
    // Negative scale would mirror the shape, which collision maths cannot
    // express; magnitude is all that matters here.
    const float sx = std::fabs(scale.x);
    const float sy = std::fabs(scale.y);
    const float sz = std::fabs(scale.z);

    // A zero-sized shape makes Jolt assert and produces degenerate collisions,
    // so every dimension is held just above zero.
    constexpr float kMin = 0.01f;

    JPH::ShapeRefC inner;
    switch (c.shape) {
        case ColliderShape::Box: {
            inner = new JPH::BoxShape(JPH::Vec3(
                std::max(c.halfExtents.x * sx, kMin),
                std::max(c.halfExtents.y * sy, kMin),
                std::max(c.halfExtents.z * sz, kMin)));
            break;
        }
        case ColliderShape::Capsule: {
            // A capsule is round about its own Y axis, so its radius follows
            // the two axes across that circle and its length follows Y.
            const float r = std::max(c.radius * std::max(sx, sz), kMin);
            // Jolt asks for HALF the straight middle section, matching the
            // engine's own `height` field being the full middle section.
            const float halfCyl = std::max(c.height * 0.5f * sy, kMin);
            inner = new JPH::CapsuleShape(halfCyl, r);
            break;
        }
        case ColliderShape::Sphere:
        default: {
            const float r = std::max(c.radius * std::max({sx, sy, sz}), kMin);
            inner = new JPH::SphereShape(r);
            break;
        }
    }

    // The collider's own offset and rotation inside the entity. Jolt expresses
    // that by WRAPPING the shape rather than by storing a transform on the
    // body, which is why this produces a second shape object around the first.
    const bool rotated   = c.rotation.x != 0.0f || c.rotation.y != 0.0f ||
                           c.rotation.z != 0.0f;
    const bool translated = c.offset.x != 0.0f || c.offset.y != 0.0f ||
                            c.offset.z != 0.0f;
    if (!rotated && !translated) return inner;

    // Euler degrees to a quaternion, in the same X-then-Y-then-Z order the
    // Inspector and ColliderComponent::LocalMatrix use.
    const Quaternion q = QuaternionFromEuler(c.rotation.x * DEG2RAD,
                                             c.rotation.y * DEG2RAD,
                                             c.rotation.z * DEG2RAD);
    // The offset is measured in the entity's local space, so it scales with
    // the entity just as the shape's dimensions do.
    const JPH::Vec3 off(c.offset.x * sx, c.offset.y * sy, c.offset.z * sz);

    return new JPH::RotatedTranslatedShape(off, ToJolt(q), inner);
}

// Which collision layer a body belongs to. Only the static bodies go in the
// non-moving layer; kinematic bodies move, so they belong with the movers even
// though no force acts on them.
inline JPH::ObjectLayer LayerFor(MotionType m) {
    return m == MotionType::Static ? Layers::kNonMoving : Layers::kMoving;
}

inline JPH::EMotionType JoltMotion(MotionType m) {
    switch (m) {
        case MotionType::Static:    return JPH::EMotionType::Static;
        case MotionType::Kinematic: return JPH::EMotionType::Kinematic;
        default:                    return JPH::EMotionType::Dynamic;
    }
}

// The entity's absolute position and orientation, which is what the simulation
// works in. A physics world is flat: it has no notion of parents and children,
// so everything must be handed to it in world space.
void WorldPose(Scene& scene, const Entity& e, Vector3& pos, Quaternion& rot) {
    const Matrix world = scene.WorldMatrix(e, /*ignoreScale=*/true);
    Vector3    ignoredScale;
    MatrixDecompose(world, &pos, &rot, &ignoredScale);
}

} // anonymous namespace

// ============================================================================
// Public interface
// ============================================================================

bool InitPhysics() {
    if (g_world) return true;          // already built; nothing to do
    g_error.clear();

    // --- Jolt's global setup, in the order the library requires -------------
    if (!g_backendReady) {
        // 1. Install the default memory allocator. Jolt routes every
        //    allocation through its own hooks so a game can supply a custom
        //    allocator; the default simply forwards to malloc/free. Something
        //    must be installed before ANY other Jolt call, because the very
        //    next steps allocate.
        JPH::RegisterDefaultAllocator();

        // 2. Point Jolt's diagnostics at the callbacks above, so that internal
        //    warnings and failed assertions reach the console instead of
        //    vanishing.
        JPH::Trace = TraceImpl;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

        // 3. Create the factory: Jolt's registry of its own serializable
        //    types, which the shape system uses to construct shapes by type id.
        JPH::Factory::sInstance = new JPH::Factory();

        // 4. Register those types with the factory. Without this, creating any
        //    shape fails.
        JPH::RegisterTypes();

        g_backendReady = true;
    }

    // --- The world ----------------------------------------------------------
    g_world = std::make_unique<World>();

    // Hand the pre-allocated ceilings and the three filters to the system.
    // After this call the world exists and can accept bodies.
    g_world->system.Init(kMaxBodies, kNumBodyMutexes,
                         kMaxBodyPairs, kMaxContactConstraints,
                         g_world->bpLayerMap,
                         g_world->objVsBpFilter,
                         g_world->objPairFilter);

    // Earth gravity, pointing down. See the note in Physics.h about units.
    g_world->system.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    return true;
}

void ShutdownPhysics() {
    // Order matters and is the exact reverse of construction: the world holds
    // shapes built by the factory, so it must be gone before the factory is.
    g_world.reset();

    if (g_backendReady) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;   // never leave a dangling global
        g_backendReady = false;
    }
}

bool IsPhysicsReady() { return g_world != nullptr; }

const char* PhysicsError() { return g_error.c_str(); }

void ResetPhysics() {
    if (!g_world) return;

    // Every body belongs to the run that created it. Destroying them all here
    // is what makes Play start from the authored scene rather than from
    // wherever the last run happened to finish - and it is why this is called
    // on Stop as well: the entities those bodies described have just been
    // replaced wholesale by the restored originals, so every id in the table
    // now refers to something that no longer exists.
    JPH::BodyInterface& bi = g_world->system.GetBodyInterface();
    for (const auto& [entityId, tracked] : g_world->bodies) {
        bi.RemoveBody(tracked.id);
        bi.DestroyBody(tracked.id);
    }
    g_world->bodies.clear();

    g_world->accumulator = 0.0f;
    g_world->lastStepMs  = 0.0f;
}

namespace {

// --- Step 1: keep the set of bodies matching the set of entities ------------
// Runs every frame rather than once at the start of play, so that an entity
// spawned mid-run (a bullet, a wave of enemies) gets a body automatically and
// a destroyed one gives its body back. Nothing that spawns entities has to
// know that physics exists.
void ReconcileBodies(Scene& scene) {
    JPH::BodyInterface& bi = g_world->system.GetBodyInterface();

    // Add bodies for entities that should have one and don't.
    for (Entity& e : scene.Entities()) {
        auto* rb  = e.GetComponent<RigidBodyComponent>();
        auto* col = e.GetComponent<ColliderComponent>();
        // A RigidBody without a Collider has no shape, so there is nothing to
        // simulate. This is silently skipped rather than treated as an error:
        // it is a normal in-between state while an entity is being built up in
        // the Inspector.
        if (!rb || !col) continue;
        if (g_world->bodies.count(e.id)) continue;   // already has one

        Vector3    pos;
        Quaternion rot;
        WorldPose(scene, e, pos, rot);

        JPH::BodyCreationSettings bcs(MakeShape(*col, scene.WorldScale(e)),
                                      ToJolt(pos), ToJolt(rot),
                                      JoltMotion(rb->motion),
                                      LayerFor(rb->motion));
        bcs.mFriction       = rb->friction;
        bcs.mRestitution    = rb->restitution;
        bcs.mLinearDamping  = rb->linearDamping;
        bcs.mAngularDamping = rb->angularDamping;
        bcs.mGravityFactor  = rb->gravityFactor;

        // By default Jolt derives mass from the shape's volume assuming a
        // fixed density. We want the mass the Inspector says instead, but we
        // still want Jolt to work out the INERTIA - how hard the body is to
        // spin, which depends on how that mass is distributed through the
        // shape and is not something anyone wants to type in by hand.
        // CalculateInertia means exactly that: take my mass, compute the rest.
        bcs.mOverrideMassProperties =
            JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = std::max(rb->mass, 0.001f);

        // A static body is created asleep: activating it would only cost work,
        // since it is never going to move.
        const JPH::EActivation activation = (rb->motion == MotionType::Static)
            ? JPH::EActivation::DontActivate
            : JPH::EActivation::Activate;

        const JPH::BodyID id = bi.CreateAndAddBody(bcs, activation);
        if (!id.IsInvalid())
            g_world->bodies[e.id] = World::Tracked{id, rb->motion};
    }

    // Remove bodies whose entity is gone, or which no longer has the
    // components that earned it one.
    //
    // The doomed ids are collected first and erased afterwards, because
    // erasing from a map while iterating over it invalidates the iterator -
    // the same hazard that once crashed Scene::Start when a script added a
    // component mid-loop.
    std::vector<EntityID> doomed;
    for (const auto& [entityId, tracked] : g_world->bodies) {
        Entity* e = scene.Find(entityId);
        if (!e || !e->GetComponent<RigidBodyComponent>()
               || !e->GetComponent<ColliderComponent>())
            doomed.push_back(entityId);
    }
    for (EntityID id : doomed) {
        const JPH::BodyID body = g_world->bodies[id].id;
        // Two calls, and both are needed: RemoveBody takes it out of the
        // simulation, DestroyBody releases it. Removing alone would leak.
        bi.RemoveBody(body);
        bi.DestroyBody(body);
        g_world->bodies.erase(id);
    }
}

// --- Step 2a: carry script-driven motion into the simulation ----------------
// A kinematic body is positioned by game code, but the simulation still has to
// be told - and told as a VELOCITY, not a teleport. MoveKinematic works out
// the velocity that would carry the body from where it is to where the entity
// now is over `dt`, which is what lets it shove dynamic bodies aside instead
// of passing through them or flinging them at infinite speed.
void PushKinematicTargets(Scene& scene, float dt) {
    if (dt <= 0.0f) return;
    JPH::BodyInterface& bi = g_world->system.GetBodyInterface();

    for (const auto& [entityId, tracked] : g_world->bodies) {
        if (tracked.motion != MotionType::Kinematic) continue;
        Entity* e = scene.Find(entityId);
        if (!e) continue;

        Vector3    pos;
        Quaternion rot;
        WorldPose(scene, *e, pos, rot);
        bi.MoveKinematic(tracked.id, ToJolt(pos), ToJolt(rot), dt);
    }
}

// --- Step 3: carry simulated motion back onto the entities ------------------
// Only DYNAMIC bodies are written back. A static body never moved, and a
// kinematic one is already exactly where the script put it - copying the
// simulation's idea of its position back over the script's would fight the
// script for control.
void WriteBackTransforms(Scene& scene) {
    const JPH::BodyInterface& bi = g_world->system.GetBodyInterface();

    for (const auto& [entityId, tracked] : g_world->bodies) {
        if (tracked.motion != MotionType::Dynamic) continue;
        Entity* e = scene.Find(entityId);
        if (!e) continue;

        JPH::RVec3 p;
        JPH::Quat  q;
        bi.GetPositionAndRotation(tracked.id, p, q);

        // The simulation works in WORLD space; an entity stores a LOCAL
        // transform relative to its parent. With no parent the two are the
        // same thing and the values go straight across.
        if (e->parent == kInvalidEntity) {
            e->transform.position = ToRay(p);
            e->transform.rotation = ToRay(q);
            continue;
        }

        // With a parent, the world pose has to be expressed relative to it.
        // Since world = local * parentWorld, it follows that
        // local = world * inverse(parentWorld) - which is the whole of the
        // world/local bridge that lets a parented object be simulated at all.
        const Entity* parent = scene.FindConst(e->parent);
        if (!parent) {                     // parent vanished: treat as root
            e->transform.position = ToRay(p);
            e->transform.rotation = ToRay(q);
            continue;
        }

        const Matrix parentWorld = scene.WorldMatrix(*parent, /*ignoreScale=*/true);
        const Matrix world = MatrixMultiply(
            QuaternionToMatrix(ToRay(q)),
            MatrixTranslate(p.GetX(), p.GetY(), p.GetZ()));
        const Matrix local = MatrixMultiply(world, MatrixInvert(parentWorld));

        Vector3    lpos, lscale;
        Quaternion lrot;
        MatrixDecompose(local, &lpos, &lrot, &lscale);
        e->transform.position = lpos;
        e->transform.rotation = lrot;
        // Scale is deliberately NOT written back. The simulation was never
        // told about it (it was baked into the shape), so it has no opinion,
        // and overwriting the authored scale with a decomposed approximation
        // would make objects creep in size over a long run.
    }
}

} // anonymous namespace

void UpdatePhysics(Scene& scene, float dt) {
    if (!g_world) return;

    const auto begin = std::chrono::steady_clock::now();

    ReconcileBodies(scene);
    PushKinematicTargets(scene, dt);

    // Bank this frame's real time, then spend it in fixed-size steps. Any
    // leftover smaller than one step stays in the accumulator and is carried
    // into the next frame, so no time is ever lost or double-counted.
    g_world->accumulator += dt;

    int steps = 0;
    while (g_world->accumulator >= kFixedStep && steps < kMaxStepsPerFrame) {
        g_world->accumulator -= kFixedStep;
        ++steps;

        // Re-apply this frame's forces before each step, since Jolt clears
        // them after every one. See the note on Tracked::pendingForce.
        JPH::BodyInterface& bi = g_world->system.GetBodyInterface();
        for (const auto& [entityId, tracked] : g_world->bodies) {
            if (tracked.motion != MotionType::Dynamic) continue;
            if (!tracked.pendingForce.IsNearZero())
                bi.AddForce(tracked.id, tracked.pendingForce,
                            JPH::EActivation::Activate);
            if (!tracked.pendingTorque.IsNearZero())
                bi.AddTorque(tracked.id, tracked.pendingTorque,
                             JPH::EActivation::Activate);
        }

        g_world->system.Update(kFixedStep, kCollisionSteps,
                               &g_world->tempAllocator,
                               &g_world->jobSystem);
    }

    // Forces last one frame. A script that wants a continuous push must ask
    // for it again next frame, exactly as it would in Unity - which is what
    // makes "stop applying thrust" work by simply not calling apply_force.
    //
    // Discarding a frame's force when no step ran is correct, not a loss: no
    // simulated time passed either, and a force only means anything multiplied
    // by a duration.
    for (auto& [entityId, tracked] : g_world->bodies) {
        tracked.pendingForce  = JPH::Vec3::sZero();
        tracked.pendingTorque = JPH::Vec3::sZero();
    }

    // If the loop hit its ceiling there is still a backlog in the account, and
    // keeping it would guarantee another full frame of catching up next time -
    // the spiral described above. Throw it away: the world runs momentarily
    // slow, which is invisible, instead of progressively further behind.
    if (steps == kMaxStepsPerFrame) g_world->accumulator = 0.0f;

    WriteBackTransforms(scene);

    const auto end = std::chrono::steady_clock::now();
    g_world->lastStepMs =
        std::chrono::duration<float, std::milli>(end - begin).count();
}

void SetPhysicsGravity(Vector3 g) {
    if (!g_world) return;
    g_world->system.SetGravity(JPH::Vec3(g.x, g.y, g.z));
}

Vector3 GetPhysicsGravity() {
    if (!g_world) return {0.0f, -9.81f, 0.0f};
    const JPH::Vec3 g = g_world->system.GetGravity();
    return {g.GetX(), g.GetY(), g.GetZ()};
}

// ============================================================================
// Driving a body
// ============================================================================

namespace {

// The one lookup every function below shares: find the entity's tracked body,
// but only if it is DYNAMIC. A static or kinematic body is not moved by
// forces, and Jolt asserts if you try, so this filters them out at the door.
World::Tracked* FindDynamic(EntityID id) {
    if (!g_world) return nullptr;
    auto it = g_world->bodies.find(id);
    if (it == g_world->bodies.end()) return nullptr;
    if (it->second.motion != MotionType::Dynamic) return nullptr;
    return &it->second;
}

// Any tracked body, dynamic or not - for the queries, which are meaningful
// whatever the motion type.
World::Tracked* FindAny(EntityID id) {
    if (!g_world) return nullptr;
    auto it = g_world->bodies.find(id);
    return it == g_world->bodies.end() ? nullptr : &it->second;
}

// Turn a vector given in the entity's own frame into world space, by rotating
// it by the body's current orientation.
JPH::Vec3 LocalToWorldDir(const World::Tracked& t, Vector3 v) {
    const JPH::Quat q = g_world->system.GetBodyInterface().GetRotation(t.id);
    return q * ToJolt(v);
}

} // anonymous namespace

bool HasBody(EntityID id) { return FindAny(id) != nullptr; }

void ApplyForce(EntityID id, Vector3 force) {
    if (World::Tracked* t = FindDynamic(id))
        t->pendingForce += ToJolt(force);
}

void ApplyLocalForce(EntityID id, Vector3 force) {
    if (World::Tracked* t = FindDynamic(id))
        t->pendingForce += LocalToWorldDir(*t, force);
}

void ApplyForceAtPoint(EntityID id, Vector3 force, Vector3 worldPoint) {
    World::Tracked* t = FindDynamic(id);
    if (!t) return;

    // A force applied away from the centre of mass both pushes and turns. The
    // push is the force itself; the turn is the "moment", the cross product of
    // the arm (centre to the point) with the force. Splitting it here rather
    // than calling Jolt's AddForce-at-point keeps it going through the same
    // pending-force path as everything else, so it survives the fixed timestep
    // the same way.
    JPH::BodyInterface& bi = g_world->system.GetBodyInterface();
    const JPH::Vec3 com = bi.GetCenterOfMassPosition(t->id);
    const JPH::Vec3 f   = ToJolt(force);
    const JPH::Vec3 arm = ToJolt(worldPoint) - com;

    t->pendingForce  += f;
    t->pendingTorque += arm.Cross(f);
}

void ApplyTorque(EntityID id, Vector3 torque) {
    if (World::Tracked* t = FindDynamic(id))
        t->pendingTorque += ToJolt(torque);
}

void ApplyLocalTorque(EntityID id, Vector3 torque) {
    if (World::Tracked* t = FindDynamic(id))
        t->pendingTorque += LocalToWorldDir(*t, torque);
}

void ApplyImpulse(EntityID id, Vector3 impulse) {
    // An impulse is instantaneous, so unlike a force it goes straight to Jolt:
    // there is nothing to spread across the frame's steps. Activate, because a
    // body that has gone to sleep must wake up to feel it.
    if (World::Tracked* t = FindDynamic(id)) {
        JPH::BodyInterface& bi = g_world->system.GetBodyInterface();
        bi.ActivateBody(t->id);
        bi.AddImpulse(t->id, ToJolt(impulse));
    }
}

void ApplyAngularImpulse(EntityID id, Vector3 impulse) {
    if (World::Tracked* t = FindDynamic(id)) {
        JPH::BodyInterface& bi = g_world->system.GetBodyInterface();
        bi.ActivateBody(t->id);
        bi.AddAngularImpulse(t->id, ToJolt(impulse));
    }
}

Vector3 GetLinearVelocity(EntityID id) {
    World::Tracked* t = FindAny(id);
    if (!t) return {0.0f, 0.0f, 0.0f};
    return ToRay(g_world->system.GetBodyInterface().GetLinearVelocity(t->id));
}

void SetLinearVelocity(EntityID id, Vector3 v) {
    if (World::Tracked* t = FindDynamic(id)) {
        JPH::BodyInterface& bi = g_world->system.GetBodyInterface();
        bi.ActivateBody(t->id);
        bi.SetLinearVelocity(t->id, ToJolt(v));
    }
}

Vector3 GetAngularVelocity(EntityID id) {
    World::Tracked* t = FindAny(id);
    if (!t) return {0.0f, 0.0f, 0.0f};
    return ToRay(g_world->system.GetBodyInterface().GetAngularVelocity(t->id));
}

void SetAngularVelocity(EntityID id, Vector3 v) {
    if (World::Tracked* t = FindDynamic(id)) {
        JPH::BodyInterface& bi = g_world->system.GetBodyInterface();
        bi.ActivateBody(t->id);
        bi.SetAngularVelocity(t->id, ToJolt(v));
    }
}

int PhysicsBodyCount() {
    return g_world ? (int)g_world->system.GetNumBodies() : 0;
}

float PhysicsStepMs() {
    return g_world ? g_world->lastStepMs : 0.0f;
}

} // namespace eng
