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
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
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

// Snap a terrain's visual resolution down to a grid the heightfield can use:
// the largest power of two that does not exceed it, capped so that a very
// detailed landscape does not buy a collision surface far finer than anything
// needs to stand on it. 512 across is a quarter of a million samples, which is
// already well past the point where extra precision changes how the ground
// feels underfoot.
//
// A terrain whose resolution is itself a power of two up to that cap therefore
// gets a collision surface sample-for-sample identical to the mesh, which is the
// arrangement worth aiming for.
//
// This sits OUTSIDE the file's anonymous namespace below, because the Inspector
// calls it to report when the two grids disagree; everything in that namespace
// is private to this file by design.
int TerrainCollisionGrid(int resolution) {
    int n = 4;                                  // the smallest grid the field allows
    while (n * 2 <= resolution && n < 512) n *= 2;
    return n;
}

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
// Contact reporting
// ============================================================================
// One impact, as recorded during a simulation step and replayed afterwards.
//
// The two-stage arrangement is not optional. Jolt calls its contact listener
// from INSIDE the simulation step, while the body list is locked and being
// solved. Creating or destroying an entity there - which is exactly what a
// gameplay script does on an impact - would corrupt the very structures being
// walked. So the listener does nothing but write the facts down, and the
// dispatch happens once the step has finished and the world is quiet again.
struct ContactEvent {
    EntityID  a = kInvalidEntity;
    EntityID  b = kInvalidEntity;
    float     speed = 0.0f;    // how fast they were closing, along the impact
    JPH::Vec3 point = JPH::Vec3::sZero();
};

// Collects contacts during the step. Deliberately does no more than that.
class ContactCollector final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
                        const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override {
        // Which entities these bodies belong to. The id was stored on the body
        // as "user data" when it was created, which saves searching a table
        // from inside the step.
        const EntityID a = (EntityID)body1.GetUserData();
        const EntityID b = (EntityID)body2.GetUserData();
        if (a == kInvalidEntity || b == kInvalidEntity) return;

        // How hard the impact was. Not the speed of either object - a jet and
        // a missile flying side by side at 300 are not colliding at 300 - but
        // how fast they were CLOSING along the direction of the impact. That
        // is the relative velocity at the contact point, projected onto the
        // contact normal, and it is what separates a scrape from a crash.
        const JPH::Vec3 p = manifold.GetWorldSpaceContactPointOn1(0);
        const JPH::Vec3 relative = body1.GetPointVelocity(p) -
                                   body2.GetPointVelocity(p);
        // The manifold's normal points FROM body 1 TOWARDS body 2 - it is the
        // direction body 2 must move to separate. So (v1 - v2) projected onto
        // it is already the rate at which the gap is closing, and needs no
        // negating. Getting this sign backwards is silent: every impact simply
        // reads as zero, because the clamp below then discards them all.
        const float closing = relative.Dot(manifold.mWorldSpaceNormal);

        // A negative value means they are separating rather than approaching,
        // which is not an impact; report zero instead of a phantom speed.

        events.push_back(ContactEvent{a, b, closing > 0.0f ? closing : 0.0f, p});
    }

    // Only new contacts are reported. OnContactPersisted would fire every
    // single step for as long as two things stayed touching, so an object
    // resting on the ground would report a collision sixty times a second
    // forever - which is noise, not an event.

    std::vector<ContactEvent> events;
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

// The fastest any body is allowed to travel, in world units per second.
//
// Jolt enforces a speed limit per body and its default is 500. The limit is
// there for a real reason - something crossing a whole level in a single step
// breaks the assumptions the solver rests on - but 500 suits human-scale games
// and this one fires bullets. Left at the default, a round asked to travel at
// 1000 simply comes out at 500: no error, no warning, just a shot moving at
// half the speed the script asked for.
//
// So it is raised, not removed. Continuous collision is what makes fast bodies
// safe to simulate; this ceiling still catches a runaway force before it sends
// something to infinity.
constexpr float kMaxLinearVelocity = 4000.0f;

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
    ContactCollector         contacts;

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

// Build the collision surface of a landscape from a TerrainComponent.
//
// A heightfield is a grid of ground heights rather than a solid volume, which
// is both the natural way to describe terrain and far cheaper to test than the
// tens of thousands of triangles that draw it. Jolt stores it compressed and
// can find the ground under a point almost immediately.
//
// The one constraint that shapes this function: Jolt requires the grid to be a
// POWER OF TWO across (at least 4), because it subdivides the field in half
// repeatedly to search it. The terrain's own `resolution` is a free number, so
// the collision grid is snapped to a power of two and the heights re-sampled
// smoothly onto it. Collision detail and visual detail are therefore allowed
// to differ - which is normal and usually desirable, since a landscape needs
// far less precision to stand on than to look at.
JPH::ShapeRefC MakeHeightfieldShape(const TerrainComponent& t, Vector3 scale) {
    const int n = TerrainCollisionGrid(t.resolution);

    const std::vector<float> heights = t.SampleHeights(n);
    if (heights.empty()) return nullptr;

    const float sx = std::fabs(scale.x);
    const float sy = std::fabs(scale.y);
    const float sz = std::fabs(scale.z);

    // The terrain mesh is drawn shifted by half its width so that it is
    // CENTRED on its entity. The collision surface has to be shifted by
    // exactly the same amount or the ground would sit half a landscape away
    // from the hills you can see.
    const float sizeX = t.worldSize * sx;
    const float sizeZ = t.worldSize * sz;
    const JPH::Vec3 offset(-sizeX * 0.5f, 0.0f, -sizeZ * 0.5f);

    // Jolt reads a sample as: position = offset + scale * (column, sample, row).
    // The samples run 0..1, so the Y scale is the terrain's full height, and
    // the X/Z scales are the spacing between neighbouring grid points - the
    // span divided by the number of GAPS, which is one less than the number of
    // samples.
    const JPH::Vec3 gridScale(sizeX / (float)(n - 1),
                              t.maxHeight * sy,
                              sizeZ / (float)(n - 1));

    JPH::HeightFieldShapeSettings settings(heights.data(), offset, gridScale,
                                           (JPH::uint32)n);
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    // Unlike the primitive shapes, this one can genuinely fail (a bad sample
    // count, an impossible scale), so the result is checked rather than
    // assumed. A failure leaves the entity with no body at all, which is the
    // safe outcome: no collision rather than wrong collision.
    if (result.HasError()) return nullptr;
    return result.Get();
}

// Build the Jolt shape described by a ColliderComponent.
//
// THE ENTITY'S SCALE IS DELIBERATELY NOT APPLIED HERE. A collider in this
// engine is authored directly in world units, so scaling an entity does not
// resize its collision volume - the rule stated in
// Scene::ClosestPointOnCollider and followed by the viewport's wireframe
// gizmo. Multiplying by the scale would make the simulated shape disagree with
// both: an entity scaled 8x to size its model would collide as something eight
// times larger than the outline drawn around it, shoving things aside from far
// outside anything visible.
//
// The heightfield above is the one exception, and for a reason that does not
// apply here: the terrain MESH is drawn through the entity's full world
// matrix, scale included, so that landscape really does grow with its entity
// and its collision surface has to grow with it.
//
// One consequence either way: a Jolt shape has no scale of its own - it is a
// fixed piece of geometry, so that shapes can be shared and pre-processed for
// fast collision. Resizing a collider while the game is running therefore does
// nothing until the body is rebuilt.
JPH::ShapeRefC MakeShape(const ColliderComponent& c) {
    // A zero-sized shape makes Jolt assert and produces degenerate collisions,
    // so every dimension is held just above zero.
    constexpr float kMin = 0.01f;

    JPH::ShapeRefC inner;
    switch (c.shape) {
        case ColliderShape::Box: {
            inner = new JPH::BoxShape(JPH::Vec3(
                std::max(c.halfExtents.x, kMin),
                std::max(c.halfExtents.y, kMin),
                std::max(c.halfExtents.z, kMin)));
            break;
        }
        case ColliderShape::Capsule: {
            const float r = std::max(c.radius, kMin);
            // Jolt asks for HALF the straight middle section, matching the
            // engine's own `height` field being the full middle section.
            const float halfCyl = std::max(c.height * 0.5f, kMin);
            inner = new JPH::CapsuleShape(halfCyl, r);
            break;
        }
        case ColliderShape::Heightfield:
            // Handled before this function is reached, because a heightfield
            // is built from a different component entirely. Refusing here
            // rather than falling through means a mistake shows up as "no
            // collision" instead of as a mysterious sphere.
            return nullptr;
        case ColliderShape::Sphere:
        default: {
            inner = new JPH::SphereShape(std::max(c.radius, kMin));
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
    // The offset is in the same authored world units as the shape's own
    // dimensions, and is left unscaled for the same reason - it must place the
    // volume exactly where ColliderComponent::LocalMatrix puts the wireframe.
    const JPH::Vec3 off(c.offset.x, c.offset.y, c.offset.z);

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

    // Register the contact listener, so impacts are recorded as they happen.
    // Like the layer filters, Jolt stores a pointer rather than a copy, which
    // is why the collector is a member of this same struct and outlives the
    // system it is handed to.
    g_world->system.SetContactListener(&g_world->contacts);

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
    // Impacts recorded but not yet delivered belong to the run that is ending.
    g_world->contacts.events.clear();

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

        // A heightfield takes its shape from the Terrain component beside it,
        // not from the collider's own numbers, so it is built separately.
        JPH::ShapeRefC shape;
        MotionType     motion = rb->motion;
        if (col->shape == ColliderShape::Heightfield) {
            auto* terrain = e.GetComponent<TerrainComponent>();
            // No terrain on this entity means there is no landscape to stand
            // on. Skipping leaves the entity unsimulated, which is the honest
            // result; the Inspector warns about this arrangement directly.
            if (!terrain) continue;
            shape = MakeHeightfieldShape(*terrain, scene.WorldScale(e));

            // A heightfield is a surface, not a solid: it has an inside and an
            // outside but no volume, so it has no mass and cannot be thrown
            // around. Jolt refuses to build a moving body from one. Forcing it
            // static here means a scene that asks for a dynamic landscape gets
            // a working solid landscape instead of no landscape at all.
            motion = MotionType::Static;
        } else {
            shape = MakeShape(*col);
        }
        // A shape that could not be built (bad terrain settings, an
        // unbuildable size) leaves the entity out of the simulation rather
        // than crashing or substituting something wrong.
        if (shape == nullptr) continue;

        JPH::BodyCreationSettings bcs(shape,
                                      ToJolt(pos), ToJolt(rot),
                                      JoltMotion(motion),
                                      LayerFor(motion));
        bcs.mFriction       = rb->friction;
        bcs.mRestitution    = rb->restitution;
        bcs.mLinearDamping  = rb->linearDamping;
        bcs.mAngularDamping = rb->angularDamping;
        bcs.mGravityFactor  = rb->gravityFactor;
        // Jolt's own default here is 500, which is slower than a bullet.
        bcs.mMaxLinearVelocity = kMaxLinearVelocity;

        // By default Jolt derives mass from the shape's volume assuming a
        // fixed density. We want the mass the Inspector says instead, but we
        // still want Jolt to work out the INERTIA - how hard the body is to
        // spin, which depends on how that mass is distributed through the
        // shape and is not something anyone wants to type in by hand.
        // CalculateInertia means exactly that: take my mass, compute the rest.
        bcs.mOverrideMassProperties =
            JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = std::max(rb->mass, 0.001f);

        // Stamp the entity's id onto the body. The contact listener runs deep
        // inside a simulation step and needs to name the two things that hit
        // each other; reading a number off the body is immediate, where
        // searching the entity table from in there would be both slow and
        // awkward to do safely.
        bcs.mUserData = (JPH::uint64)e.id;

        // Sweep the path rather than sampling its end, for bodies fast enough
        // to skip past thin geometry between one step and the next.
        if (rb->continuous)
            bcs.mMotionQuality = JPH::EMotionQuality::LinearCast;

        // A static body is created asleep: activating it would only cost work,
        // since it is never going to move.
        const JPH::EActivation activation = (motion == MotionType::Static)
            ? JPH::EActivation::DontActivate
            : JPH::EActivation::Activate;

        const JPH::BodyID id = bi.CreateAndAddBody(bcs, activation);
        if (id.IsInvalid()) continue;
        g_world->bodies[e.id] = World::Tracked{id, motion};

        // Launch it, if it was asked to start moving. This is how a projectile
        // leaves the barrel at speed: the script that spawned it had no body
        // to push at the time, so it recorded the velocity instead.
        if (motion == MotionType::Dynamic &&
            (rb->initialVelocity.x != 0.0f || rb->initialVelocity.y != 0.0f ||
             rb->initialVelocity.z != 0.0f))
            bi.SetLinearVelocity(id, ToJolt(rb->initialVelocity));
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

// --- Step 2b: tell the game about the impacts that just happened ------------
// Runs after the simulation has finished stepping, when it is safe for a
// script to spawn an explosion or destroy the thing it just hit.
void DispatchContacts(Scene& scene) {
    // These hooks are script code, and scripts reach the world through
    // Scene::Current(). Physics is stepped from outside Scene::Update, so
    // without this the scene would not be marked active and every scene.* call
    // made from a collision would silently do nothing - a bullet unable to
    // destroy itself, an enemy that takes damage but never dies.
    ActiveScene active(scene);

    // Take the list, leaving the collector empty for the next frame. Swapping
    // rather than iterating in place matters: a script reached from in here
    // may cause more contacts to be recorded later this frame, and those
    // belong to the next dispatch, not this one.
    std::vector<ContactEvent> events;
    events.swap(g_world->contacts.events);

    for (const ContactEvent& ev : events) {
        Entity* a = scene.Find(ev.a);
        Entity* b = scene.Find(ev.b);
        // Either may already have been destroyed by an earlier event in this
        // same batch - two bullets can strike the same target in one frame.
        if (!a || !b) continue;

        const Vector3 point = ToRay(ev.point);

        // Both sides are told, each about the other, because either may want
        // to react: the bullet destroys itself, the target takes damage.
        //
        // The component pointers are SNAPSHOT before any hook runs. A script
        // that adds a component while being notified would otherwise reallocate
        // the vector being walked and leave this loop holding freed memory -
        // the same hazard Scene::Start and Scene::Update guard against.
        std::vector<Component*> comps;
        comps.reserve(a->components.size());
        for (const auto& c : a->components) comps.push_back(c.get());
        for (Component* c : comps) c->OnCollision(*a, *b, ev.speed, point);

        // `a` may have been invalidated by the hooks above if a script caused
        // the entity list to move, so both are looked up again.
        a = scene.Find(ev.a);
        b = scene.Find(ev.b);
        if (!a || !b) continue;

        comps.clear();
        comps.reserve(b->components.size());
        for (const auto& c : b->components) comps.push_back(c.get());
        for (Component* c : comps) c->OnCollision(*b, *a, ev.speed, point);
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

    // Impacts are reported last, after the transforms are up to date, so that
    // a script reacting to a crash sees the entity where it actually stopped
    // rather than where it was before the collision resolved it.
    DispatchContacts(scene);

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
        return;
    }

    // No body yet. That is the normal situation for an entity spawned during
    // this frame: it joins the simulation at the end of it. Rather than
    // silently dropping the request - which would leave a bullet hanging in
    // the air at the muzzle - record it as the velocity the body should be
    // born with. A script therefore does not have to know or care whether the
    // entity it is launching has reached the simulation yet.
    if (Scene* s = Scene::Current())
        if (Entity* e = s->Find(id))
            if (auto* rb = e->GetComponent<RigidBodyComponent>())
                rb->initialVelocity = v;
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
