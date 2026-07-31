#include "ScriptGraph.h"

#include "imgui.h"
#include "engine/Particles.h"   // the list of effects an FX Burst node can pick
#include "engine/Audio.h"       // the list of sounds a Play Sound node can pick
#include "engine/ModelDefs.h"   // the list of models a Spawn node can pick
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>

namespace ed = ax::NodeEditor;
using nlohmann::json;

namespace edtr {

// ---- Node descriptions -----------------------------------------------------

const char* ScriptGraph::Title(NodeKind k) {
    switch (k) {
        case NodeKind::EventCreate:  return "On Create";
        case NodeKind::EventUpdate:  return "On Update";
        case NodeKind::EventDestroy: return "On Destroy";
        case NodeKind::Number:       return "Number";
        case NodeKind::Add:          return "Add";
        case NodeKind::Sub:          return "Subtract";
        case NodeKind::Mul:          return "Multiply";
        case NodeKind::Div:          return "Divide";
        case NodeKind::Yaw:          return "Yaw";
        case NodeKind::Pitch:        return "Pitch";
        case NodeKind::Roll:         return "Roll";
        case NodeKind::MoveForward:  return "Move Forward";
        case NodeKind::Print:        return "Print";
        case NodeKind::DestroySelf:  return "Destroy Self";
        case NodeKind::KeyDown:      return "Key Down";
        case NodeKind::Greater:      return "A > B";
        case NodeKind::Less:         return "A < B";
        case NodeKind::Equal:        return "A == B";
        case NodeKind::Branch:       return "Branch";
        case NodeKind::GetVar:       return "Get";
        case NodeKind::SetVar:       return "Set";
        case NodeKind::PosX:         return "Pos X";
        case NodeKind::PosY:         return "Pos Y";
        case NodeKind::PosZ:         return "Pos Z";
        case NodeKind::FwdX:         return "Forward X";
        case NodeKind::FwdY:         return "Forward Y";
        case NodeKind::FwdZ:         return "Forward Z";
        case NodeKind::UpX:          return "Up X";
        case NodeKind::UpY:          return "Up Y";
        case NodeKind::UpZ:          return "Up Z";
        case NodeKind::Sqrt:         return "Sqrt";
        case NodeKind::Exp:          return "Exp";
        case NodeKind::SetPosX:      return "Set Pos X";
        case NodeKind::SetPosY:      return "Set Pos Y";
        case NodeKind::SetPosZ:      return "Set Pos Z";
        case NodeKind::TurnToPlayer: return "Turn To Player";
        case NodeKind::Fire:         return "Fire";
        case NodeKind::IsPlayerNear: return "If Player Near";
        case NodeKind::SetScale:     return "Set Scale";
        case NodeKind::HitNearest:   return "Hit Nearest";
        case NodeKind::HudSet:       return "HUD Set";
        case NodeKind::FxBurst:      return "FX Burst";
        case NodeKind::SetLightIntensity: return "Set Light";
        case NodeKind::PlaySound:    return "Play Sound";
        case NodeKind::LoopStart:    return "Loop Start";
        case NodeKind::LoopSet:      return "Loop Set";
        case NodeKind::LoopStop:     return "Loop Stop";
        case NodeKind::AvoidCrowd:   return "Avoid Crowd";
        case NodeKind::AimedAtPlayer:return "Aimed At Player";
        case NodeKind::ChaseTarget:  return "Chase Target";
        case NodeKind::And:          return "And";
        case NodeKind::Or:           return "Or";
        case NodeKind::LessEqual:    return "A <= B";
        case NodeKind::GreaterEqual: return "A >= B";
        case NodeKind::Param:        return "Param";
        case NodeKind::For:          return "For";
        case NodeKind::Sin:          return "Sin";
        case NodeKind::Cos:          return "Cos";
        case NodeKind::Floor:        return "Floor";
        case NodeKind::SpawnCube:    return "Spawn Cube";
        case NodeKind::Spawn:        return "Spawn";
        case NodeKind::CountTag:     return "Count Tag";
        case NodeKind::EventCollision:  return "On Collision";
        case NodeKind::OtherTagIs:      return "Hit Tag Is";
        case NodeKind::DamageOther:     return "Damage Hit";
        case NodeKind::SetBody:         return "Set Body";
        case NodeKind::SetVelocity:     return "Set Velocity";
        case NodeKind::ApplyForce:      return "Apply Force";
        case NodeKind::ApplyLocalForce: return "Apply Local Force";
        case NodeKind::Speed:           return "Speed";
        case NodeKind::PlaySoundAt:     return "Play Sound At";
        case NodeKind::LoopAt:          return "Loop At";
        case NodeKind::FxBurstAt:       return "FX Burst At";
        case NodeKind::SetCollider:     return "Set Collider";
        case NodeKind::HudAdd:          return "HUD Add";
        case NodeKind::HudGet:          return "HUD Get";
        case NodeKind::EventDrawHud:    return "On Draw HUD";
        case NodeKind::DrawText:        return "Draw Text";
        case NodeKind::DrawValue:       return "Draw Value";
        case NodeKind::DrawBar:         return "Draw Bar";
        case NodeKind::DrawRect:        return "Draw Rect";
        case NodeKind::DrawLine:        return "Draw Line";
        case NodeKind::DrawCircle:      return "Draw Circle";
    }
    return "?";
}

// Value nodes (pure) have no exec pins; they only compute a number.
bool ScriptGraph::IsPure(NodeKind k) {
    switch (k) {
        case NodeKind::Number:
        case NodeKind::Add:
        case NodeKind::Sub:
        case NodeKind::Mul:
        case NodeKind::Div:
        case NodeKind::KeyDown:
        case NodeKind::Greater:
        case NodeKind::Less:
        case NodeKind::Equal:
        case NodeKind::GetVar:
        case NodeKind::PosX: case NodeKind::PosY: case NodeKind::PosZ:
        case NodeKind::FwdX: case NodeKind::FwdY: case NodeKind::FwdZ:
        case NodeKind::UpX:  case NodeKind::UpY:  case NodeKind::UpZ:
        case NodeKind::Sqrt: case NodeKind::Exp:
        case NodeKind::IsPlayerNear:
        case NodeKind::AimedAtPlayer:
        case NodeKind::And: case NodeKind::Or:
        case NodeKind::LessEqual: case NodeKind::GreaterEqual:
        case NodeKind::Param:
        case NodeKind::Sin: case NodeKind::Cos: case NodeKind::Floor:
        case NodeKind::CountTag:
        case NodeKind::OtherTagIs:
        case NodeKind::Speed:
        case NodeKind::HudGet:
            return true;
        default:
            return false;
    }
}

// The list of pins a node kind has, with fixed slots.
std::vector<Pin> ScriptGraph::Signature(NodeKind k) {
    switch (k) {
        case NodeKind::EventCreate:
        case NodeKind::EventDestroy:
            return {{SlotExecOut, PinType::Exec, false, "then"}};
        case NodeKind::EventUpdate:
            return {{SlotExecOut,  PinType::Exec,  false, "then"},
                    {SlotDataOut,  PinType::Float, false, "dt"}};

        // Draws over the finished 3D view. It hands out the surface size because
        // every anchored position is measured from it, and because a graph that
        // wants something at, say, a third of the width has no other way to know
        // how wide the view is.
        case NodeKind::EventDrawHud:
            return {{SlotExecOut,     PinType::Exec,  false, "then"},
                    {SlotDataOut,     PinType::Float, false, "w"},
                    {SlotDataOut + 1, PinType::Float, false, "h"}};

        // The drawing nodes. All of them take an OFFSET from their anchor rather
        // than an absolute position, so they survive the view being resized.
        case NodeKind::DrawText:
            return {{SlotExecIn,      PinType::Exec,  true,  "in"},
                    {SlotExecOut,     PinType::Exec,  false, "then"},
                    {SlotDataIn,      PinType::Float, true,  "dx"},
                    {SlotDataIn + 1,  PinType::Float, true,  "dy"}};
        case NodeKind::DrawValue:
            return {{SlotExecIn,      PinType::Exec,  true,  "in"},
                    {SlotExecOut,     PinType::Exec,  false, "then"},
                    {SlotDataIn,      PinType::Float, true,  "value"},
                    {SlotDataIn + 1,  PinType::Float, true,  "dx"},
                    {SlotDataIn + 2,  PinType::Float, true,  "dy"}};
        case NodeKind::DrawBar:
            return {{SlotExecIn,      PinType::Exec,  true,  "in"},
                    {SlotExecOut,     PinType::Exec,  false, "then"},
                    {SlotDataIn,      PinType::Float, true,  "fill"},
                    {SlotDataIn + 1,  PinType::Float, true,  "dx"},
                    {SlotDataIn + 2,  PinType::Float, true,  "dy"},
                    {SlotDataIn + 3,  PinType::Float, true,  "width"},
                    {SlotDataIn + 4,  PinType::Float, true,  "height"}};
        case NodeKind::DrawRect:
            return {{SlotExecIn,      PinType::Exec,  true,  "in"},
                    {SlotExecOut,     PinType::Exec,  false, "then"},
                    {SlotDataIn,      PinType::Float, true,  "dx"},
                    {SlotDataIn + 1,  PinType::Float, true,  "dy"},
                    {SlotDataIn + 2,  PinType::Float, true,  "width"},
                    {SlotDataIn + 3,  PinType::Float, true,  "height"}};
        case NodeKind::DrawLine:
            return {{SlotExecIn,      PinType::Exec,  true,  "in"},
                    {SlotExecOut,     PinType::Exec,  false, "then"},
                    {SlotDataIn,      PinType::Float, true,  "x1"},
                    {SlotDataIn + 1,  PinType::Float, true,  "y1"},
                    {SlotDataIn + 2,  PinType::Float, true,  "x2"},
                    {SlotDataIn + 3,  PinType::Float, true,  "y2"}};
        case NodeKind::DrawCircle:
            return {{SlotExecIn,      PinType::Exec,  true,  "in"},
                    {SlotExecOut,     PinType::Exec,  false, "then"},
                    {SlotDataIn,      PinType::Float, true,  "dx"},
                    {SlotDataIn + 1,  PinType::Float, true,  "dy"},
                    {SlotDataIn + 2,  PinType::Float, true,  "radius"}};

        case NodeKind::Number:
        case NodeKind::Param:
            return {{SlotDataOut, PinType::Float, false, "value"}};
        case NodeKind::Add:
        case NodeKind::Sub:
        case NodeKind::Mul:
        case NodeKind::Div:
            return {{SlotDataIn,     PinType::Float, true,  "a"},
                    {SlotDataIn + 1, PinType::Float, true,  "b"},
                    {SlotDataOut,    PinType::Float, false, "result"}};

        case NodeKind::Yaw:
        case NodeKind::Pitch:
        case NodeKind::Roll:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "deg"}};
        case NodeKind::MoveForward:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "dist"}};
        case NodeKind::Print:
        case NodeKind::DestroySelf:
            return {{SlotExecIn,  PinType::Exec, true,  "in"},
                    {SlotExecOut, PinType::Exec, false, "out"}};

        case NodeKind::KeyDown:
            return {{SlotDataOut, PinType::Bool, false, "held"}};
        case NodeKind::Greater:
        case NodeKind::Less:
        case NodeKind::Equal:
        case NodeKind::LessEqual:
        case NodeKind::GreaterEqual:
            return {{SlotDataIn,     PinType::Float, true,  "a"},
                    {SlotDataIn + 1, PinType::Float, true,  "b"},
                    {SlotDataOut,    PinType::Bool,  false, "result"}};
        // Boolean combiners: two Bool inputs, one Bool output.
        case NodeKind::And:
        case NodeKind::Or:
            return {{SlotDataIn,     PinType::Bool, true,  "a"},
                    {SlotDataIn + 1, PinType::Bool, true,  "b"},
                    {SlotDataOut,    PinType::Bool, false, "result"}};
        case NodeKind::Branch:
            return {{SlotExecIn,   PinType::Exec, true,  "in"},
                    {SlotDataIn,    PinType::Bool, true,  "cond"},
                    {SlotExecOut,   PinType::Exec, false, "true"},
                    {SlotExecOut2,  PinType::Exec, false, "false"}};

        case NodeKind::GetVar:
            return {{SlotDataOut, PinType::Float, false, "value"}};
        case NodeKind::SetVar:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "value"}};

        // Value nodes that read something about this entity (no inputs).
        case NodeKind::PosX: case NodeKind::PosY: case NodeKind::PosZ:
        case NodeKind::FwdX: case NodeKind::FwdY: case NodeKind::FwdZ:
        case NodeKind::UpX:  case NodeKind::UpY:  case NodeKind::UpZ:
            return {{SlotDataOut, PinType::Float, false, "value"}};
        // Unary math: one number in, one out.
        case NodeKind::Sqrt: case NodeKind::Exp:
        case NodeKind::Sin:  case NodeKind::Cos: case NodeKind::Floor:
            return {{SlotDataIn,  PinType::Float, true,  "x"},
                    {SlotDataOut, PinType::Float, false, "result"}};

        // Counting loop: run "body" once per step from..to; "i" is the current
        // value, readable by nodes in the body. "done" runs after the loop.
        case NodeKind::For:
            return {{SlotExecIn,     PinType::Exec,  true,  "in"},
                    {SlotExecOut,    PinType::Exec,  false, "body"},
                    {SlotExecOut2,   PinType::Exec,  false, "done"},
                    {SlotDataIn,     PinType::Float, true,  "from"},
                    {SlotDataIn + 1, PinType::Float, true,  "to"},
                    {SlotDataOut,    PinType::Float, false, "i"}};
        // Spawn a plain cube at a position; the name is a node field.
        case NodeKind::SpawnCube:
            return {{SlotExecIn,     PinType::Exec,  true,  "in"},
                    {SlotExecOut,    PinType::Exec,  false, "out"},
                    {SlotDataIn,     PinType::Float, true,  "x"},
                    {SlotDataIn + 1, PinType::Float, true,  "y"},
                    {SlotDataIn + 2, PinType::Float, true,  "z"}};
        // General spawn: position (x,y,z) and facing (dx,dy,dz) are inputs; the
        // script and tag are node fields, and health is the node's value.
        case NodeKind::Spawn:
            return {{SlotExecIn,     PinType::Exec,  true,  "in"},
                    {SlotExecOut,    PinType::Exec,  false, "out"},
                    {SlotDataIn,     PinType::Float, true,  "x"},
                    {SlotDataIn + 1, PinType::Float, true,  "y"},
                    {SlotDataIn + 2, PinType::Float, true,  "z"},
                    {SlotDataIn + 3, PinType::Float, true,  "dx"},
                    {SlotDataIn + 4, PinType::Float, true,  "dy"},
                    {SlotDataIn + 5, PinType::Float, true,  "dz"}};
        // Count entities carrying a tag (the tag is a node field).
        case NodeKind::CountTag:
            return {{SlotDataOut, PinType::Float, false, "count"}};

        // --- Stage 13: physics and collisions -------------------------------
        // The collision event hands on everything the impact knew: how hard it
        // was, and where on the two surfaces it happened - which is where an
        // explosion or a shower of sparks belongs.
        case NodeKind::EventCollision:
            return {{SlotExecOut,     PinType::Exec,  false, "then"},
                    {SlotDataOut,     PinType::Float, false, "speed"},
                    {SlotDataOut + 1, PinType::Float, false, "x"},
                    {SlotDataOut + 2, PinType::Float, false, "y"},
                    {SlotDataOut + 3, PinType::Float, false, "z"}};
        // Was the thing we hit tagged this? The tag is a node field.
        case NodeKind::OtherTagIs:
            return {{SlotDataOut, PinType::Bool, false, "is"}};
        // How fast this entity is travelling, from the simulation.
        case NodeKind::Speed:
            return {{SlotDataOut, PinType::Float, false, "speed"}};
        // Read a HUD value back, so a graph can branch on shared game state.
        case NodeKind::HudGet:
            return {{SlotDataOut, PinType::Float, false, "value"}};
        // Damage whatever we just hit.
        case NodeKind::DamageOther:
            return {{SlotExecIn,  PinType::Exec,  true,  ""},
                    {SlotDataIn,  PinType::Float, true,  "amount"},
                    {SlotExecOut, PinType::Exec,  false, "then"}};
        // Hand this entity to the physics simulation. Motion type and the
        // continuous-collision flag are node fields; mass and gravity are
        // wired, so a Param can drive them.
        case NodeKind::SetBody:
            return {{SlotExecIn,     PinType::Exec,  true,  ""},
                    {SlotDataIn,     PinType::Float, true,  "mass"},
                    {SlotDataIn + 1, PinType::Float, true,  "gravity"},
                    {SlotExecOut,    PinType::Exec,  false, "then"}};
        // Three-component vector actions: velocity and the two force kinds all
        // take the same shape.
        case NodeKind::SetVelocity:
        case NodeKind::ApplyForce:
        case NodeKind::ApplyLocalForce:
            return {{SlotExecIn,     PinType::Exec,  true,  ""},
                    {SlotDataIn,     PinType::Float, true,  "x"},
                    {SlotDataIn + 1, PinType::Float, true,  "y"},
                    {SlotDataIn + 2, PinType::Float, true,  "z"},
                    {SlotExecOut,    PinType::Exec,  false, "then"}};
        // A one-shot sound or a particle burst at a point in the world. The
        // name is a node field, chosen from a dropdown.
        case NodeKind::PlaySoundAt:
        case NodeKind::FxBurstAt:
            return {{SlotExecIn,     PinType::Exec,  true,  ""},
                    {SlotDataIn,     PinType::Float, true,  "x"},
                    {SlotDataIn + 1, PinType::Float, true,  "y"},
                    {SlotDataIn + 2, PinType::Float, true,  "z"},
                    {SlotExecOut,    PinType::Exec,  false, "then"}};
        // A collision volume. The shape is a node field; its size is wired, so
        // a Param can drive it from the Inspector.
        case NodeKind::SetCollider:
            return {{SlotExecIn,   PinType::Exec,  true,  ""},
                    {SlotDataIn,   PinType::Float, true,  "size"},
                    {SlotExecOut,  PinType::Exec,  false, "then"}};
        // Add to a HUD value rather than replacing it - awarding score.
        case NodeKind::HudAdd:
            return {{SlotExecIn,   PinType::Exec,  true,  ""},
                    {SlotDataIn,   PinType::Float, true,  "delta"},
                    {SlotExecOut,  PinType::Exec,  false, "then"}};
        // A moving loop: position, plus the volume and pitch that make an
        // engine note follow the throttle.
        case NodeKind::LoopAt:
            return {{SlotExecIn,     PinType::Exec,  true,  ""},
                    {SlotDataIn,     PinType::Float, true,  "x"},
                    {SlotDataIn + 1, PinType::Float, true,  "y"},
                    {SlotDataIn + 2, PinType::Float, true,  "z"},
                    {SlotDataIn + 3, PinType::Float, true,  "volume"},
                    {SlotDataIn + 4, PinType::Float, true,  "pitch"},
                    {SlotExecOut,    PinType::Exec,  false, "then"}};
        // Is a player within range? number in, bool out.
        case NodeKind::IsPlayerNear:
            return {{SlotDataIn,  PinType::Float, true,  "range"},
                    {SlotDataOut, PinType::Bool,  false, "near"}};
        // Write a position component.
        case NodeKind::SetPosX: case NodeKind::SetPosY: case NodeKind::SetPosZ:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "value"}};
        case NodeKind::TurnToPlayer:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "deg"}};
        case NodeKind::Fire:
            return {{SlotExecIn,  PinType::Exec, true,  "in"},
                    {SlotExecOut, PinType::Exec, false, "out"}};

        // Set every scale axis to one number wired into "value".
        case NodeKind::SetScale:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "value"}};
        // Impact test: the tag to hit and the hit points are node fields; the
        // reach is the "radius" data Input.
        // "on hit" is a second exec output that runs ONLY when something was
        // actually struck, and runs inside that test. Anything chained to the
        // ordinary "out" runs every frame, hit or not - which is why a sound or
        // an effect placed there would fire constantly.
        case NodeKind::HitNearest:
            return {{SlotExecIn,   PinType::Exec,  true,  "in"},
                    {SlotExecOut,  PinType::Exec,  false, "out"},
                    {SlotExecOut2, PinType::Exec,  false, "on hit"},
                    {SlotDataIn,   PinType::Float, true,  "radius"}};
        // Publish one number to the HUD; the display name is a node field.
        case NodeKind::HudSet:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "value"}};
        // The effect name and how far ahead of the entity to place it are node
        // fields; only the size is worth wiring, so that it can be driven by
        // something else (a bigger blast for a bigger enemy, say).
        case NodeKind::FxBurst:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "scale"}};
        // Brightness is a wired input, because the useful thing to do with it is
        // drive it from something that changes: a timer, a health value.
        case NodeKind::SetLightIntensity:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "brightness"}};
        // The sound is a node field; only the loudness is worth wiring, so it
        // can follow something (a distance, a damage amount).
        case NodeKind::PlaySound:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "volume"}};
        // Starting and stopping a loop need only the sound, which is a field.
        case NodeKind::LoopStart:
        case NodeKind::LoopStop:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"}};
        // Adjusting a running loop is the per-frame one, so both numbers are
        // wired: an engine note follows the throttle.
        case NodeKind::LoopSet:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,     PinType::Float, true, "volume"},
                    {SlotDataIn + 1, PinType::Float, true, "pitch"}};
        // Separation: the neighbour tag and push strength are node fields; the
        // reach is the "range" data Input.
        case NodeKind::AvoidCrowd:
            return {{SlotExecIn,  PinType::Exec,  true,  "in"},
                    {SlotExecOut, PinType::Exec,  false, "out"},
                    {SlotDataIn,  PinType::Float, true,  "range"}};
        // Firing test: within "range" of the player AND within the "angle" cone.
        case NodeKind::AimedAtPlayer:
            return {{SlotDataIn,     PinType::Float, true,  "range"},
                    {SlotDataIn + 1, PinType::Float, true,  "angle"},
                    {SlotDataOut,    PinType::Bool,  false, "aimed"}};
        // Chase camera: the target name is a node field; the three follow
        // numbers are data inputs.
        case NodeKind::ChaseTarget:
            return {{SlotExecIn,     PinType::Exec,  true,  "in"},
                    {SlotExecOut,    PinType::Exec,  false, "out"},
                    {SlotDataIn,     PinType::Float, true,  "distance"},
                    {SlotDataIn + 1, PinType::Float, true,  "height"},
                    {SlotDataIn + 2, PinType::Float, true,  "stiffness"}};
    }
    return {};
}

// Look up a pin's type from its node's signature.
PinType ScriptGraph::PinTypeOf(int pin) const {
    const GraphNode* n = FindNode(PinToNode(pin));
    if (n) {
        int slot = PinToSlot(pin);
        for (const Pin& p : Signature(n->kind))
            if (p.slot == slot) return p.type;
    }
    return PinType::Exec;
}

static bool IsEvent(NodeKind k) {
    return k == NodeKind::EventCreate || k == NodeKind::EventUpdate ||
           k == NodeKind::EventDestroy || k == NodeKind::EventCollision ||
           k == NodeKind::EventDrawHud;
}

// ---- HUD anchors -----------------------------------------------------------
//
// A HUD element must NOT be positioned with fixed coordinates. The surface it is
// drawn into is the Game panel, which can be dragged to any size, so an element
// written as "x = 24, y = 300" sits correctly at one size and wrong at every
// other. Instead each drawing node picks an anchor - a named point on the
// surface - and offsets from it. The generated code works the anchor out from the
// `w` and `h` that onDrawHud receives, so it follows any resize.
//
// The names are stored in the node, so they must stay stable: renaming one would
// silently reset every graph using it back to the first entry.
static const char* const kAnchorNames[] = {
    "top-left", "top", "top-right",
    "left",     "center", "right",
    "bottom-left", "bottom", "bottom-right",
};
static constexpr int kAnchorCount = 9;

// The Lua expressions for an anchor's x and y. `w` and `h` are in scope inside
// the generated onDrawHud, so they can be referred to directly.
static void AnchorExpr(const char* name, std::string& ax, std::string& ay) {
    std::string a = (name && name[0]) ? name : "top-left";
    ax = "0";  ay = "0";
    if (a == "top")          { ax = "w*0.5"; ay = "0"; }
    else if (a == "top-right")    { ax = "w";     ay = "0"; }
    else if (a == "left")         { ax = "0";     ay = "h*0.5"; }
    else if (a == "center")       { ax = "w*0.5"; ay = "h*0.5"; }
    else if (a == "right")        { ax = "w";     ay = "h*0.5"; }
    else if (a == "bottom-left")  { ax = "0";     ay = "h"; }
    else if (a == "bottom")       { ax = "w*0.5"; ay = "h"; }
    else if (a == "bottom-right") { ax = "w";     ay = "h"; }
}

// Whether an anchor sits on the right edge or the horizontal middle. Text is
// drawn from its LEFT edge, so anchoring a readout to the right without
// subtracting its width leaves it hanging off the screen - and because digits
// change width as a number grows, the amount to subtract has to be measured at
// draw time rather than baked in.
static int AnchorAlign(const char* name) {
    std::string a = (name && name[0]) ? name : "top-left";
    if (a == "top-right" || a == "right" || a == "bottom-right") return 2;  // right
    if (a == "top" || a == "center" || a == "bottom")            return 1;  // centre
    return 0;                                                              // left
}

// The colour names a drawing node may choose from. These match the palette the
// engine defines for `Draw.*`; a script may add more with Draw.defineColor, but
// a dropdown can only offer what is known here.
static const char* const kHudColors[] = {"hud", "warn", "bad", "white", "dim", "dark"};
static constexpr int kHudColorCount = 6;

// The colour argument for a generated draw call, or nothing when the node uses
// the default - so an untouched node produces `Draw.text(s, x, y, 20)` rather
// than a noisier call with a redundant argument.
static std::string ColorArg(const char* name) {
    if (!name || !name[0] || std::strcmp(name, "hud") == 0) return "";
    return std::string(", \"") + name + "\"";
}

// ---- Construction ----------------------------------------------------------

ScriptGraph::ScriptGraph() { Reset(); }

void ScriptGraph::Reset() {
    m_nodes.clear();
    m_links.clear();
    m_nextID = 100;
    // The three event nodes always exist (ids 1,2,3), like fixed entry points.
    m_nodes.push_back({1, NodeKind::EventCreate});
    m_nodes.push_back({2, NodeKind::EventUpdate});
    m_nodes.push_back({3, NodeKind::EventDestroy});
    m_nodes[0].y = 0;  m_nodes[1].y = 120;  m_nodes[2].y = 240;
    m_restorePositions = true;
}

GraphNode* ScriptGraph::FindNode(int id) {
    for (auto& n : m_nodes) if (n.id == id) return &n;
    return nullptr;
}
const GraphNode* ScriptGraph::FindNode(int id) const {
    for (auto& n : m_nodes) if (n.id == id) return &n;
    return nullptr;
}

// Which node feeds the given input pin (following the one wire into it)?
const GraphNode* ScriptGraph::SourceOf(int inputPin) const {
    for (const auto& l : m_links)
        if (l.toPin == inputPin)
            return FindNode(PinToNode(l.fromPin));
    return nullptr;
}

// ---- Drawing ---------------------------------------------------------------

void ScriptGraph::DrawNode(GraphNode& n) {
    if (m_restorePositions)
        ed::SetNodePosition(n.id, ImVec2(n.x, n.y));

    ed::BeginNode(n.id);
    ImGui::PushID(n.id);
    ImGui::TextUnformatted(Title(n.kind));

    // Pins in two columns, Blueprints-style: inputs down the left edge (">- name")
    // and outputs down the right edge ("name -<"). Exec and data pins draw the
    // same here; the library colors the wire by which pin types linking accepts.
    std::vector<Pin> sig = Signature(n.kind);
    bool  hasIn = false, hasOut = false;
    float outW  = 0.0f;   // widest output label, so the column can right-align
    for (const Pin& p : sig) {
        if (p.input) { hasIn = true; continue; }
        hasOut = true;
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "%s -<", p.name);
        outW = (std::max)(outW, ImGui::CalcTextSize(lbl).x);
    }

    if (hasIn) {
        ImGui::BeginGroup();
        for (const Pin& p : sig) {
            if (!p.input) continue;
            ed::BeginPin(PinId(n.id, p.slot), ed::PinKind::Input);
            ImGui::Text(">- %s", p.name);
            ed::EndPin();
        }
        ImGui::EndGroup();
    }
    if (hasIn && hasOut) ImGui::SameLine(0.0f, 24.0f);   // gap between the columns
    if (hasOut) {
        ImGui::BeginGroup();
        for (const Pin& p : sig) {
            if (p.input) continue;
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "%s -<", p.name);
            float pad = outW - ImGui::CalcTextSize(lbl).x;   // right-align in the column
            if (pad > 0.0f) { ImGui::Dummy(ImVec2(pad, 0)); ImGui::SameLine(0.0f, 0.0f); }
            ed::BeginPin(PinId(n.id, p.slot), ed::PinKind::Output);
            ImGui::TextUnformatted(lbl);
            ed::EndPin();
        }
        ImGui::EndGroup();
    }

    // Editable fields for the nodes that carry a constant.
    ImGui::PushItemWidth(90);
    if (n.kind == NodeKind::Number)
        ImGui::DragFloat("##val", &n.value, 0.1f);
    else if (n.kind == NodeKind::Param) {
        ImGui::InputText("##pname", n.text, sizeof(n.text));  // the property name
        ImGui::DragFloat("##pdef", &n.value, 0.05f);          // its default value
    }
    else if (n.kind == NodeKind::Print || n.kind == NodeKind::KeyDown)
        ImGui::InputText("##txt", n.text, sizeof(n.text));   // message, or key name
    else if (n.kind == NodeKind::GetVar || n.kind == NodeKind::SetVar)
        ImGui::InputText("##var", n.text, sizeof(n.text));   // the variable name
    else if (n.kind == NodeKind::Fire) {
        ImGui::InputText("##bullet", n.text, sizeof(n.text)); // the bullet script
        // How far ahead of the entity the round is born. This is not cosmetic:
        // a bullet is a solid physical object, so one spawned inside the
        // collider of the thing that fired it starts the frame overlapping and
        // gets shoved aside instead of flying. It must clear the shooter's own
        // shape - at real-world scale that is several metres, not a token step.
        ImGui::DragFloat("##muzzle", &n.value, 0.1f, 0.0f, 500.0f);
    }
    else if (n.kind == NodeKind::HitNearest) {
        ImGui::InputText("##tag", n.text, sizeof(n.text));    // the tag to damage
        ImGui::DragFloat("##dmg", &n.value, 0.1f);            // hit points removed
        // An optional impact effect, fired only when something is actually hit.
        // It belongs on this node rather than as a separate one downstream,
        // because only this node knows whether the strike landed.
        if (ImGui::Button(n.text2[0] ? n.text2 : "(no effect)", ImVec2(140.0f, 0.0f))) {
            m_fxPickerNode   = n.id;
            m_fxPickerField  = 1;       // this node's effect lives in `text2`
            m_fxPickerList     = PickList::Effects;
            m_fxPickerOptional = true;   // "no effect" is a valid choice here
            m_fxPickerOpen   = true;
            ImVec2 bl{ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y};
            ImVec2 screen = ed::CanvasToScreen(bl);
            m_fxPickerX = screen.x;
            m_fxPickerY = screen.y;
            m_fxPickerW = ed::CanvasToScreen(ImGui::GetItemRectMax()).x - screen.x;
        }
    }
    else if (n.kind == NodeKind::HudSet)
        ImGui::InputText("##hud", n.text, sizeof(n.text));    // the HUD value name
    else if (n.kind == NodeKind::FxBurst) {
        // The effect is CHOSEN FROM A LIST, not typed. The list is whatever
        // assets/scripts/effects.lua defines, so inventing an effect there makes
        // it appear here by itself - which is the whole point: building a graph
        // should never require knowing what to type.
        //
        // The list CANNOT be drawn here. Everything inside a node is positioned
        // in the canvas's own coordinates, which pan and zoom with the view,
        // while a popup is placed in screen coordinates - so a menu opened from
        // inside a node lands somewhere else entirely and cannot be clicked.
        // Instead this button only records which node was asked; the list is
        // drawn later by HandleFxPicker, between Suspend and Resume, where
        // screen coordinates apply again.
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Button(n.text[0] ? n.text : "(pick effect)", ImVec2(140.0f, 0.0f))) {
            m_fxPickerNode   = n.id;
            m_fxPickerField  = 0;       // this node's effect lives in `text`
            m_fxPickerList     = PickList::Effects;
            m_fxPickerOptional = false;  // a burst must name an effect
            m_fxPickerOpen   = true;    // one frame only; see the member's comment
            // Remember where to drop the list: directly under the button, the
            // way a combo box opens. The button's rectangle is in CANVAS
            // coordinates, which pan and zoom, so it is converted to screen
            // coordinates here - before Suspend, because these conversions are
            // not allowed once the canvas is suspended.
            ImVec2 bl{ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y};
            ImVec2 screen = ed::CanvasToScreen(bl);
            m_fxPickerX = screen.x;
            m_fxPickerY = screen.y;
            m_fxPickerW = ed::CanvasToScreen(ImGui::GetItemRectMax()).x - screen.x;
        }
        ImGui::SetNextItemWidth(140.0f);
        ImGui::DragFloat("##fxahead", &n.value, 0.1f);   // how far ahead to place it
    }
    else if (n.kind == NodeKind::SetLightIntensity) { /* brightness is a wired input */ }
    else if (n.kind == NodeKind::PlaySound || n.kind == NodeKind::LoopStart ||
             n.kind == NodeKind::LoopSet   || n.kind == NodeKind::LoopStop) {
        // Same deferred-popup dance as FX Burst, for the same reason: a list
        // opened from inside a node lands where it cannot be clicked.
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Button(n.text[0] ? n.text : "(pick sound)", ImVec2(140.0f, 0.0f))) {
            m_fxPickerNode   = n.id;
            m_fxPickerField  = 0;
            m_fxPickerList     = PickList::Sounds;
            m_fxPickerOptional = false;
            m_fxPickerOpen   = true;
            ImVec2 bl{ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y};
            ImVec2 screen = ed::CanvasToScreen(bl);
            m_fxPickerX = screen.x;
            m_fxPickerY = screen.y;
            m_fxPickerW = ed::CanvasToScreen(ImGui::GetItemRectMax()).x - screen.x;
        }
    }
    else if (n.kind == NodeKind::AvoidCrowd) {
        ImGui::InputText("##ctag", n.text, sizeof(n.text));   // the neighbour tag
        ImGui::DragFloat("##force", &n.value, 0.1f);          // push strength
    }
    else if (n.kind == NodeKind::ChaseTarget)
        ImGui::InputText("##target", n.text, sizeof(n.text)); // the target entity name
    else if (n.kind == NodeKind::SpawnCube)
        ImGui::InputText("##cubename", n.text, sizeof(n.text)); // the spawned cube's name
    else if (n.kind == NodeKind::Spawn) {
        ImGui::InputText("##sscript", n.text,  sizeof(n.text));  // the script to run
        ImGui::InputText("##stag",    n.text2, sizeof(n.text2)); // the tag to give it
        ImGui::DragFloat("##shp", &n.value, 0.1f);               // starting health
        // What it should LOOK like. A name from models.lua rather than a file
        // path, so the graph carries no file names and the scale and offsets
        // that model needs live in one place. Optional: without one the spawned
        // entity is the default cube.
        //
        // Same deferred-popup dance as FX Burst, for the same reason: a list
        // opened from inside a node is placed in screen coordinates and would
        // land somewhere unclickable.
        if (ImGui::Button(n.text3[0] ? n.text3 : "(no model)", ImVec2(140.0f, 0.0f))) {
            m_fxPickerNode     = n.id;
            m_fxPickerField    = 2;      // this node's model lives in `text3`
            m_fxPickerList     = PickList::Models;
            m_fxPickerOptional = true;   // a cube is a legitimate choice
            m_fxPickerOpen     = true;
            ImVec2 bl{ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y};
            ImVec2 screen = ed::CanvasToScreen(bl);
            m_fxPickerX = screen.x;
            m_fxPickerY = screen.y;
            m_fxPickerW = ed::CanvasToScreen(ImGui::GetItemRectMax()).x - screen.x;
        }
    }
    else if (n.kind == NodeKind::HudAdd || n.kind == NodeKind::HudGet)
        ImGui::InputText("##hudname", n.text, sizeof(n.text));  // which HUD value
    else if (n.kind == NodeKind::DrawText  || n.kind == NodeKind::DrawValue ||
             n.kind == NodeKind::DrawBar   || n.kind == NodeKind::DrawRect  ||
             n.kind == NodeKind::DrawLine  || n.kind == NodeKind::DrawCircle) {
        // Text nodes carry the label (Draw Value appends the number to it).
        if (n.kind == NodeKind::DrawText || n.kind == NodeKind::DrawValue) {
            ImGui::SetNextItemWidth(140.0f);
            ImGui::InputText("##dlabel", n.text, sizeof(n.text));
            ImGui::SetNextItemWidth(140.0f);
            ImGui::DragFloat("##dsize", &n.value, 0.5f, 6.0f, 96.0f, "size %.0f");
        }
        // Filled or outlined, for the shapes that can be either.
        if (n.kind == NodeKind::DrawRect || n.kind == NodeKind::DrawCircle) {
            bool filled = n.value > 0.5f;
            if (ImGui::Checkbox("filled", &filled)) n.value = filled ? 1.0f : 0.0f;
        }
        // The anchor. This is the field that keeps an element in place when the
        // Game panel is resized, so it is offered on every drawing node.
        int a = 0;
        for (int k = 0; k < kAnchorCount; ++k)
            if (std::strcmp(n.text2, kAnchorNames[k]) == 0) a = k;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("##danchor", &a, kAnchorNames, kAnchorCount))
            std::strncpy(n.text2, kAnchorNames[a], sizeof(n.text2) - 1);
        // The colour, by name from the engine's HUD palette.
        int c = 0;
        for (int k = 0; k < kHudColorCount; ++k)
            if (std::strcmp(n.text3, kHudColors[k]) == 0) c = k;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("##dcolor", &c, kHudColors, kHudColorCount))
            std::strncpy(n.text3, kHudColors[c], sizeof(n.text3) - 1);
    }
    else if (n.kind == NodeKind::SetCollider) {
        static const char* kShapes[] = { "sphere", "box", "capsule" };
        int cur = 0;
        for (int i = 0; i < 3; ++i)
            if (std::strcmp(n.text, kShapes[i]) == 0) cur = i;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("##shape", &cur, kShapes, 3))
            std::strncpy(n.text, kShapes[cur], sizeof(n.text) - 1);
    }
    else if (n.kind == NodeKind::OtherTagIs)
        ImGui::InputText("##hittag", n.text, sizeof(n.text));   // the tag to test for
    else if (n.kind == NodeKind::SetBody) {
        // The three motion types, in the same order the engine's enum uses.
        static const char* kMotions[] = { "static", "kinematic", "dynamic" };
        int cur = 2;                                   // default: dynamic
        for (int i = 0; i < 3; ++i)
            if (std::strcmp(n.text, kMotions[i]) == 0) cur = i;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("##motion", &cur, kMotions, 3))
            std::strncpy(n.text, kMotions[cur], sizeof(n.text) - 1);
        // `value` doubles as the continuous-collision flag - a float standing in
        // for a bool, because a node carries one number and this needs no more.
        bool cont = (n.value != 0.0f);
        if (ImGui::Checkbox("sweep", &cont)) n.value = cont ? 1.0f : 0.0f;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Continuous collision: sweep the whole path each\n"
                              "step instead of only testing where it lands.\n"
                              "Needed for bullets; wasted on anything slow.");
    }
    else if (n.kind == NodeKind::PlaySoundAt || n.kind == NodeKind::LoopAt ||
             n.kind == NodeKind::FxBurstAt) {
        // Same deferred-popup dance as the unpositioned versions.
        const bool sound = (n.kind != NodeKind::FxBurstAt);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Button(n.text[0] ? n.text : (sound ? "(pick sound)" : "(pick effect)"),
                          ImVec2(140.0f, 0.0f))) {
            m_fxPickerNode     = n.id;
            m_fxPickerField    = 0;
            m_fxPickerList     = sound ? PickList::Sounds : PickList::Effects;
            m_fxPickerOptional = false;
            m_fxPickerOpen     = true;
            ImVec2 bl{ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y};
            ImVec2 screen = ed::CanvasToScreen(bl);
            m_fxPickerX = screen.x;
            m_fxPickerY = screen.y;
            m_fxPickerW = ed::CanvasToScreen(ImGui::GetItemRectMax()).x - screen.x;
        }
        if (n.kind == NodeKind::FxBurstAt) {
            ImGui::SetNextItemWidth(140.0f);
            ImGui::DragFloat("##fxatscale", &n.value, 0.05f, 0.0f, 100.0f);  // effect size
        }
    }
    else if (n.kind == NodeKind::CountTag)
        ImGui::InputText("##counttag", n.text, sizeof(n.text)); // the tag to count
    ImGui::PopItemWidth();

    ImGui::PopID();
    ed::EndNode();

    ImVec2 pos = ed::GetNodePosition(n.id);
    n.x = pos.x;  n.y = pos.y;
}

void ScriptGraph::HandleEdits() {
    if (ed::BeginCreate()) {
        ed::PinId a, b;
        if (ed::QueryNewLink(&a, &b)) {
            int pa = (int)a.Get(), pb = (int)b.Get();
            int sa = PinToSlot(pa), sb = PinToSlot(pb);
            auto isOut = [](int s) { return s == SlotExecOut || s == SlotExecOut2 || s >= SlotDataOut; };
            auto isIn  = [](int s) { return s == SlotExecIn || (s >= SlotDataIn && s < SlotDataOut); };
            // Make pa the output side, pb the input side.
            if (isOut(sb) && isIn(sa)) { std::swap(pa, pb); std::swap(sa, sb); }

            bool ok = isOut(sa) && isIn(sb)                 // output -> input
                   && PinToNode(pa) != PinToNode(pb)        // not to itself
                   && PinTypeOf(pa) == PinTypeOf(pb);       // same pin type

            if (ok && ed::AcceptNewItem()) {
                // An input pin holds only ONE wire; drop any existing one first.
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                    [pb](const GraphLink& l) { return l.toPin == pb; }), m_links.end());
                m_links.push_back({m_nextID++, pa, pb});
            } else if (!ok) {
                ed::RejectNewItem();
            }
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::LinkId lid;
        while (ed::QueryDeletedLink(&lid)) {
            if (ed::AcceptDeletedItem())
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                    [&](const GraphLink& l) { return l.id == (int)lid.Get(); }),
                    m_links.end());
        }
        ed::NodeId nid;
        while (ed::QueryDeletedNode(&nid)) {
            GraphNode* n = FindNode((int)nid.Get());
            if (n && IsEvent(n->kind)) { ed::RejectDeletedItem(); continue; }
            if (ed::AcceptDeletedItem()) {
                int id = (int)nid.Get();
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                    [id](const GraphLink& l) {
                        return PinToNode(l.fromPin) == id || PinToNode(l.toPin) == id;
                    }), m_links.end());
                m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(),
                    [id](const GraphNode& n) { return n.id == id; }), m_nodes.end());
            }
        }
    }
    ed::EndDelete();
}

// Draw the effect picker for an FX Burst node. This runs OUTSIDE the node
// canvas's coordinate system - between Suspend and Resume - because that is the
// only place a popup can be positioned and clicked correctly. A node body can
// only ask for the picker (by setting m_fxPickerNode); it cannot show one.
void ScriptGraph::HandleFxPicker() {
    ed::Suspend();

    // Open the list only on the frame the button was actually pressed. Asking
    // every frame while a node is selected would fight the user: clicking away
    // closes the popup, and the very same frame would open it again wherever
    // the mouse now is.
    if (m_fxPickerOpen) {
        ImGui::OpenPopup("FxPicker");
        m_fxPickerOpen = false;
        // Place it under the button, at the button's width, so it reads as a
        // dropdown rather than a menu that appeared out of nowhere.
        ImGui::SetNextWindowPos(ImVec2(m_fxPickerX, m_fxPickerY));
        ImGui::SetNextWindowSize(ImVec2(m_fxPickerW > 0 ? m_fxPickerW : 140.0f, 0.0f));
    }

    if (ImGui::BeginPopup("FxPicker")) {
        // The same picker serves every list; only the source differs.
        const auto& names =
            (m_fxPickerList == PickList::Sounds) ? eng::SoundNames() :
            (m_fxPickerList == PickList::Models) ? eng::ModelDefNames()
                                                 : eng::EffectPresetNames();
        if (names.empty()) {
            // Say why the list is empty rather than showing a blank menu that
            // simply looks broken.
            switch (m_fxPickerList) {
                case PickList::Sounds:
                    ImGui::TextDisabled("No sounds defined.");
                    ImGui::TextDisabled("Add one with sound.define in");
                    ImGui::TextDisabled("assets/scripts/sounds.lua");
                    break;
                case PickList::Models:
                    ImGui::TextDisabled("No models defined.");
                    ImGui::TextDisabled("Add one with model.define in");
                    ImGui::TextDisabled("assets/scripts/models.lua");
                    break;
                default:
                    ImGui::TextDisabled("No effects defined.");
                    ImGui::TextDisabled("Add one with Fx.define in");
                    ImGui::TextDisabled("assets/scripts/effects.lua");
                    break;
            }
        }
        // Writes the picked name into whichever of the node's two text fields
        // asked for it, and closes the list.
        auto choose = [&](const char* picked) {
            for (auto& n : m_nodes) {
                if (n.id != m_fxPickerNode) continue;
                char*  field = n.text;
                size_t cap   = sizeof(n.text);
                if (m_fxPickerField == 1) { field = n.text2; cap = sizeof(n.text2); }
                if (m_fxPickerField == 2) { field = n.text3; cap = sizeof(n.text3); }
                std::strncpy(field, picked, cap - 1);
                field[cap - 1] = '\0';
                break;
            }
            ImGui::CloseCurrentPopup();
            m_fxPickerNode = -1;
        };

        // Some of these choices are optional - a Hit Nearest's impact effect,
        // a Spawn's model - so those need a way back to "none at all".
        if (m_fxPickerOptional) {
            if (ImGui::Selectable(m_fxPickerList == PickList::Models
                                      ? "(no model)" : "(no effect)"))
                choose("");
            ImGui::Separator();
        }

        for (const std::string& name : names) {
            // Selectable, not MenuItem: this is a value being chosen from a
            // list, and the tick marks which one is currently set.
            bool current = false;
            for (const auto& n : m_nodes)
                if (n.id == m_fxPickerNode) {
                    current = (name == (m_fxPickerField == 1 ? n.text2 : n.text));
                    break;
                }

            if (ImGui::Selectable(name.c_str(), current)) choose(name.c_str());
        }
        ImGui::EndPopup();
    }

    ed::Resume();
}

void ScriptGraph::HandleContextMenu() {
    ImVec2 canvasMouse = ed::ScreenToCanvas(ImGui::GetMousePos());
    ed::Suspend();
    if (ed::ShowBackgroundContextMenu() && !ImGui::IsPopupOpen("AddNode")) {
        m_popupX = canvasMouse.x;
        m_popupY = canvasMouse.y;
        ImGui::OpenPopup("AddNode");
    }
    if (ImGui::BeginPopup("AddNode")) {
        NodeKind picked{};
        bool add = false;
        auto item = [&](const char* label, NodeKind k) {
            if (ImGui::MenuItem(label)) { picked = k; add = true; }
        };
        if (ImGui::BeginMenu("Values")) {
            item("Number", NodeKind::Number);
            item("Param", NodeKind::Param);
            item("Add", NodeKind::Add);
            item("Subtract", NodeKind::Sub);
            item("Multiply", NodeKind::Mul);
            item("Divide", NodeKind::Div);
            ImGui::Separator();
            item("Key Down", NodeKind::KeyDown);
            item("A > B", NodeKind::Greater);
            item("A < B", NodeKind::Less);
            item("A == B", NodeKind::Equal);
            item("A >= B", NodeKind::GreaterEqual);
            item("A <= B", NodeKind::LessEqual);
            item("And", NodeKind::And);
            item("Or", NodeKind::Or);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Actions")) {
            item("Yaw", NodeKind::Yaw);
            item("Pitch", NodeKind::Pitch);
            item("Roll", NodeKind::Roll);
            item("Move Forward", NodeKind::MoveForward);
            item("Print", NodeKind::Print);
            item("Destroy Self", NodeKind::DestroySelf);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Flow")) {
            item("Branch (If)", NodeKind::Branch);
            item("For Loop", NodeKind::For);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Variables")) {
            item("Get", NodeKind::GetVar);
            item("Set", NodeKind::SetVar);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Read")) {
            item("Pos X", NodeKind::PosX);   item("Pos Y", NodeKind::PosY);   item("Pos Z", NodeKind::PosZ);
            item("Forward X", NodeKind::FwdX); item("Forward Y", NodeKind::FwdY); item("Forward Z", NodeKind::FwdZ);
            item("Up X", NodeKind::UpX);     item("Up Y", NodeKind::UpY);     item("Up Z", NodeKind::UpZ);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Math")) {
            item("Sqrt", NodeKind::Sqrt);
            item("Exp", NodeKind::Exp);
            item("Sin", NodeKind::Sin);
            item("Cos", NodeKind::Cos);
            item("Floor", NodeKind::Floor);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Engine")) {
            item("Set Pos X", NodeKind::SetPosX); item("Set Pos Y", NodeKind::SetPosY); item("Set Pos Z", NodeKind::SetPosZ);
            item("Spawn Cube", NodeKind::SpawnCube);
            item("Spawn", NodeKind::Spawn);
            item("Count Tag", NodeKind::CountTag);
            item("Turn To Player", NodeKind::TurnToPlayer);
            item("Fire", NodeKind::Fire);
            item("If Player Near", NodeKind::IsPlayerNear);
            item("Set Scale", NodeKind::SetScale);
            item("Hit Nearest", NodeKind::HitNearest);
            item("HUD Set", NodeKind::HudSet);
            item("HUD Add", NodeKind::HudAdd);
            item("HUD Get", NodeKind::HudGet);
            item("Avoid Crowd", NodeKind::AvoidCrowd);
            item("Aimed At Player", NodeKind::AimedAtPlayer);
            item("Chase Target", NodeKind::ChaseTarget);
            ImGui::EndMenu();
        }
        // Presentation: the parts of the engine that make the world look alive.
        if (ImGui::BeginMenu("Effects")) {
            item("FX Burst", NodeKind::FxBurst);
            item("Play Sound", NodeKind::PlaySound);
            item("Loop Start", NodeKind::LoopStart);
            item("Loop Set", NodeKind::LoopSet);
            item("Loop Stop", NodeKind::LoopStop);
            item("Set Light", NodeKind::SetLightIntensity);
            ImGui::Separator();
            // The positioned versions. These take a point in the world, so an
            // impact is heard and seen where it happened rather than at the
            // listener or at the entity's own origin.
            item("Play Sound At", NodeKind::PlaySoundAt);
            item("Loop At",       NodeKind::LoopAt);
            item("FX Burst At",   NodeKind::FxBurstAt);
            ImGui::EndMenu();
        }
        // Physics: an entity the simulation owns is never placed, only pushed.
        if (ImGui::BeginMenu("Physics")) {
            item("On Collision",      NodeKind::EventCollision);
            item("Hit Tag Is",        NodeKind::OtherTagIs);
            item("Damage Hit",        NodeKind::DamageOther);
            ImGui::Separator();
            item("Set Body",          NodeKind::SetBody);
            item("Set Velocity",      NodeKind::SetVelocity);
            item("Apply Force",       NodeKind::ApplyForce);
            item("Apply Local Force", NodeKind::ApplyLocalForce);
            item("Speed",             NodeKind::Speed);
            item("Set Collider",      NodeKind::SetCollider);
            ImGui::EndMenu();
        }
        // Everything needed to build a HUD element without writing any C++.
        // All of it belongs under On Draw HUD: the draw calls are only legal
        // inside that pass and do nothing anywhere else.
        if (ImGui::BeginMenu("HUD")) {
            item("On Draw HUD", NodeKind::EventDrawHud);
            ImGui::Separator();
            item("Draw Value",  NodeKind::DrawValue);
            item("Draw Text",   NodeKind::DrawText);
            item("Draw Bar",    NodeKind::DrawBar);
            ImGui::Separator();
            item("Draw Rect",   NodeKind::DrawRect);
            item("Draw Circle", NodeKind::DrawCircle);
            item("Draw Line",   NodeKind::DrawLine);
            ImGui::Separator();
            item("HUD Get",     NodeKind::HudGet);
            item("HUD Set",     NodeKind::HudSet);
            item("HUD Add",     NodeKind::HudAdd);
            ImGui::EndMenu();
        }
        if (add) {
            GraphNode n;
            n.id   = m_nextID++;
            n.kind = picked;
            n.x = m_popupX;  n.y = m_popupY;
            if (picked == NodeKind::Number) n.value = 1.0f;
            if (picked == NodeKind::Param) {
                std::strncpy(n.text, "param", sizeof(n.text) - 1);  // default property name
                n.value = 1.0f;                                     // default value
            }
            if (picked == NodeKind::KeyDown)
                std::strncpy(n.text, "W", sizeof(n.text) - 1);   // default key
            if (picked == NodeKind::Fire) {
                std::strncpy(n.text, "assets/scripts/bullet.lua", sizeof(n.text) - 1);
                n.value = 10.0f;   // muzzle distance; must clear the shooter's collider
            }
            // The drawing nodes all want an anchor and a colour from the moment
            // they exist. Leaving these blank would put a new node at the top-left
            // corner in the default colour anyway, but writing the names in means
            // the dropdowns show what is actually in force rather than an empty box.
            if (picked == NodeKind::DrawText  || picked == NodeKind::DrawValue ||
                picked == NodeKind::DrawBar   || picked == NodeKind::DrawRect  ||
                picked == NodeKind::DrawLine  || picked == NodeKind::DrawCircle) {
                std::strncpy(n.text2, "top-left", sizeof(n.text2) - 1);
                std::strncpy(n.text3, "hud",      sizeof(n.text3) - 1);
                if (picked == NodeKind::DrawText || picked == NodeKind::DrawValue) {
                    std::strncpy(n.text, "LABEL ", sizeof(n.text) - 1);
                    n.value = 20.0f;                 // readable text size
                } else if (picked == NodeKind::DrawRect ||
                           picked == NodeKind::DrawCircle) {
                    n.value = 0.0f;                  // outlined by default
                }
            }
            if (picked == NodeKind::TurnToPlayer) n.value = 65.0f;   // (unused, but tidy)
            if (picked == NodeKind::HitNearest) {
                std::strncpy(n.text, "enemy", sizeof(n.text) - 1);   // default target tag
                n.value = 1.0f;                                      // default damage
            }
            if (picked == NodeKind::OtherTagIs)
                std::strncpy(n.text, "enemy", sizeof(n.text) - 1);   // the usual test
            if (picked == NodeKind::DamageOther) n.value = 1.0f;      // default damage
            if (picked == NodeKind::SetBody) {
                std::strncpy(n.text, "dynamic", sizeof(n.text) - 1);  // the interesting kind
                n.value = 0.0f;                                       // continuous off
            }
            if (picked == NodeKind::FxBurstAt) n.value = 1.0f;        // default effect size
            if (picked == NodeKind::SetCollider)
                std::strncpy(n.text, "sphere", sizeof(n.text) - 1);   // the usual shape
            if (picked == NodeKind::HudAdd || picked == NodeKind::HudGet)
                std::strncpy(n.text, "score", sizeof(n.text) - 1);    // the usual target
            if (picked == NodeKind::HudSet)
                std::strncpy(n.text, "value", sizeof(n.text) - 1);   // default HUD name
            if (picked == NodeKind::AvoidCrowd) {
                std::strncpy(n.text, "enemy", sizeof(n.text) - 1);   // default neighbour tag
                n.value = 12.0f;                                     // default push strength
            }
            if (picked == NodeKind::ChaseTarget)
                std::strncpy(n.text, "Jet", sizeof(n.text) - 1);     // default target name
            if (picked == NodeKind::For) { n.value = 0; }            // (from/to are inputs)
            if (picked == NodeKind::SpawnCube)
                std::strncpy(n.text, "Cube", sizeof(n.text) - 1);    // default cube name
            if (picked == NodeKind::Spawn) {
                std::strncpy(n.text,  "assets/scripts/enemy.lua", sizeof(n.text) - 1);
                std::strncpy(n.text2, "enemy", sizeof(n.text2) - 1); // default tag
                n.value = 3.0f;                                      // default health
            }
            if (picked == NodeKind::CountTag)
                std::strncpy(n.text, "enemy", sizeof(n.text) - 1);   // default tag to count
            if (picked == NodeKind::FxBurst) {
                // Default to the first effect defined in effects.lua, so a new
                // node is immediately usable rather than pointing at nothing.
                const auto& names = eng::EffectPresetNames();
                std::strncpy(n.text, names.empty() ? "explosion" : names[0].c_str(),
                             sizeof(n.text) - 1);
                n.value = 0.0f;    // forward offset: at the entity itself
            }
            if (picked == NodeKind::SetLightIntensity) n.value = 1.0f;
            if (picked == NodeKind::PlaySound || picked == NodeKind::LoopStart ||
                picked == NodeKind::LoopSet   || picked == NodeKind::LoopStop) {
                // Default to the first sound defined, so a fresh node is usable
                // straight away instead of pointing at nothing.
                const auto& snames = eng::SoundNames();
                if (!snames.empty())
                    std::strncpy(n.text, snames[0].c_str(), sizeof(n.text) - 1);
            }
            m_nodes.push_back(n);
            m_restorePositions = true;
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

// The little "Variables" list drawn at the top of the Node Editor panel,
// above the canvas. Each row is a variable's name and starting value.
void ScriptGraph::DrawVariablesUI() {
    if (!ImGui::CollapsingHeader("Variables")) return;
    int removeIdx = -1;
    for (size_t i = 0; i < m_vars.size(); ++i) {
        ImGui::PushID((int)i);
        char nb[64];
        std::strncpy(nb, m_vars[i].name.c_str(), sizeof(nb) - 1);
        nb[sizeof(nb) - 1] = '\0';
        ImGui::SetNextItemWidth(130);
        if (ImGui::InputText("##name", nb, sizeof(nb))) m_vars[i].name = nb;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::DragFloat("##init", &m_vars[i].init, 0.1f);
        ImGui::SameLine();
        if (ImGui::SmallButton("remove")) removeIdx = (int)i;
        ImGui::PopID();
    }
    if (removeIdx >= 0) m_vars.erase(m_vars.begin() + removeIdx);
    if (ImGui::SmallButton("+ Add Variable")) m_vars.push_back({"var", 0.0f});
    ImGui::Separator();
}

void ScriptGraph::Draw(ed::EditorContext* ctx) {
    DrawVariablesUI();

    ed::SetCurrentEditor(ctx);
    ed::Begin("ScriptGraph");

    for (auto& n : m_nodes) DrawNode(n);
    m_restorePositions = false;

    for (const auto& l : m_links)
        ed::Link(l.id, l.fromPin, l.toPin);

    HandleEdits();
    HandleContextMenu();
    HandleFxPicker();

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

// ---- Persistence -----------------------------------------------------------

bool ScriptGraph::Save(const std::string& path) const {
    json doc;
    doc["nextID"] = m_nextID;
    for (const auto& n : m_nodes)
        doc["nodes"].push_back({{"id", n.id}, {"kind", (int)n.kind},
                                {"value", n.value}, {"text", n.text},
                                {"text2", n.text2},
                                {"text3", n.text3},
                                {"x", n.x}, {"y", n.y}});
    for (const auto& l : m_links)
        doc["links"].push_back({{"id", l.id}, {"from", l.fromPin}, {"to", l.toPin}});
    for (const auto& v : m_vars)
        doc["vars"].push_back({{"name", v.name}, {"init", v.init}});

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return false;
    f << doc.dump(2);
    return true;
}

bool ScriptGraph::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    json doc = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) return false;

    m_nodes.clear();
    m_links.clear();
    for (const json& jn : doc.value("nodes", json::array())) {
        GraphNode n;
        n.id    = jn.value("id", 0);
        n.kind  = (NodeKind)jn.value("kind", 0);
        n.value = jn.value("value", 0.0f);
        n.x     = jn.value("x", 0.0f);
        n.y     = jn.value("y", 0.0f);
        std::strncpy(n.text, jn.value("text", "").c_str(), sizeof(n.text) - 1);
        std::strncpy(n.text2, jn.value("text2", "").c_str(), sizeof(n.text2) - 1);
        std::strncpy(n.text3, jn.value("text3", "").c_str(), sizeof(n.text3) - 1);

        // Fire's muzzle distance used to be fixed at 3 in the generated code, so
        // graphs written before it became a field carry no value for it and would
        // load as 0 - which means spawning the round at the shooter's own centre,
        // inside its collider, where it is shoved aside instead of fired. Giving
        // those graphs the 3 they used to emit keeps them behaving exactly as
        // they did rather than silently breaking. A deliberate 0 is not a thing
        // anyone wants here, so nothing is lost by claiming it.
        if (n.kind == NodeKind::Fire && n.value == 0.0f) n.value = 3.0f;

        m_nodes.push_back(n);
    }
    for (const json& jl : doc.value("links", json::array()))
        m_links.push_back({jl.value("id", 0), jl.value("from", 0), jl.value("to", 0)});
    m_vars.clear();
    for (const json& jv : doc.value("vars", json::array()))
        m_vars.push_back({jv.value("name", std::string("var")), jv.value("init", 0.0f)});
    m_nextID = doc.value("nextID", 100);
    m_restorePositions = true;
    return true;
}

// ---- Code generation -------------------------------------------------------

// The Lua EXPRESSION produced by a value node (Number -> its number, Add ->
// "(a + b)", etc.). Reads its own inputs recursively.
std::string ScriptGraph::ExprForNode(const GraphNode& n) const {
    char buf[64];
    switch (n.kind) {
        case NodeKind::Number:
            snprintf(buf, sizeof(buf), "%.7g", n.value);
            return buf;
        case NodeKind::EventUpdate:
            return "dt";   // the update event's dt output
        case NodeKind::Add: case NodeKind::Sub:
        case NodeKind::Mul: case NodeKind::Div:
        case NodeKind::Greater: case NodeKind::Less: case NodeKind::Equal:
        case NodeKind::LessEqual: case NodeKind::GreaterEqual:
        case NodeKind::And: case NodeKind::Or: {
            const char* op = (n.kind == NodeKind::Add)          ? " + "   :
                             (n.kind == NodeKind::Sub)          ? " - "   :
                             (n.kind == NodeKind::Mul)          ? " * "   :
                             (n.kind == NodeKind::Div)          ? " / "   :
                             (n.kind == NodeKind::Greater)      ? " > "   :
                             (n.kind == NodeKind::Less)         ? " < "   :
                             (n.kind == NodeKind::Equal)        ? " == "  :
                             (n.kind == NodeKind::LessEqual)    ? " <= "  :
                             (n.kind == NodeKind::GreaterEqual) ? " >= "  :
                             (n.kind == NodeKind::And)          ? " and " : " or ";
            std::string a = ExprForInput(PinId(n.id, SlotDataIn));
            std::string b = ExprForInput(PinId(n.id, SlotDataIn + 1));
            return "(" + a + op + b + ")";
        }
        case NodeKind::KeyDown: {
            std::string key(n.text), esc;
            for (char c : key) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
            return "Input.keyDown(\"" + esc + "\")";
        }
        case NodeKind::GetVar:
            return n.text[0] ? std::string(n.text) : "0";   // the variable's name
        case NodeKind::OtherTagIs: {
            // `other` is the collision handler's own parameter, so this is only
            // meaningful inside an On Collision chain. Outside one it would
            // reference a nil, which Lua reports plainly enough.
            std::string tag(n.text), esc;
            for (char c : tag) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
            return "other.tag == \"" + esc + "\"";
        }
        case NodeKind::Speed:
            return "Physics.speed(entity)";
        case NodeKind::HudGet: {
            std::string name(n.text), esc;
            for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
            // A HUD value that was never set reads as 0, which is what makes a
            // guard like "game_over > 0" work before anything has set it.
            return "Hud.get(\"" + esc + "\", 0)";
        }
        case NodeKind::Param:
            // Read the tunable back from the generated properties table.
            return n.text[0] ? ("properties." + std::string(n.text)) : "0";

        case NodeKind::PosX: return "entity.transform.position.x";
        case NodeKind::PosY: return "entity.transform.position.y";
        case NodeKind::PosZ: return "entity.transform.position.z";
        case NodeKind::FwdX: return "entity.transform:forward().x";
        case NodeKind::FwdY: return "entity.transform:forward().y";
        case NodeKind::FwdZ: return "entity.transform:forward().z";
        case NodeKind::UpX:  return "entity.transform:up().x";
        case NodeKind::UpY:  return "entity.transform:up().y";
        case NodeKind::UpZ:  return "entity.transform:up().z";
        case NodeKind::Sqrt:
            return "math.sqrt(" + ExprForInput(PinId(n.id, SlotDataIn)) + ")";
        case NodeKind::Exp:
            return "math.exp(" + ExprForInput(PinId(n.id, SlotDataIn)) + ")";
        case NodeKind::Sin:
            return "math.sin(" + ExprForInput(PinId(n.id, SlotDataIn)) + ")";
        case NodeKind::Cos:
            return "math.cos(" + ExprForInput(PinId(n.id, SlotDataIn)) + ")";
        case NodeKind::Floor:
            return "math.floor(" + ExprForInput(PinId(n.id, SlotDataIn)) + ")";
        case NodeKind::For:
            // The loop variable, referenced by nodes inside the body. Its name
            // is unique per For node so nested loops don't collide.
            return "i" + std::to_string(n.id);
        case NodeKind::CountTag: {
            std::string tag(n.text), esc;
            for (char c : tag) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
            return "Scene.count(\"" + esc + "\")";
        }
        case NodeKind::IsPlayerNear:
            return "(Scene.nearest(\"player\", entity.transform.position.x, "
                   "entity.transform.position.y, entity.transform.position.z, " +
                   ExprForInput(PinId(n.id, SlotDataIn)) + ") ~= nil)";
        case NodeKind::AimedAtPlayer: {
            // Self-contained expression: find the nearest player, then test that
            // it is both within "range" and within the "angle" firing cone (the
            // off-nose angle from a clamped forward-vs-direction dot product).
            std::string range = ExprForInput(PinId(n.id, SlotDataIn));
            std::string angle = ExprForInput(PinId(n.id, SlotDataIn + 1));
            return
              "(function() "
              "local tp = Scene.nearest(\"player\", entity.transform.position.x, entity.transform.position.y, entity.transform.position.z, 100000) "
              "if tp == nil then return false end "
              "local dx = tp.transform.position.x - entity.transform.position.x "
              "local dy = tp.transform.position.y - entity.transform.position.y "
              "local dz = tp.transform.position.z - entity.transform.position.z "
              "local dd = math.sqrt(dx*dx + dy*dy + dz*dz) "
              "local f = entity.transform:forward() "
              "local inv = 0 if dd > 0.0001 then inv = 1 / dd end "
              "local dot = f.x*dx*inv + f.y*dy*inv + f.z*dz*inv "
              "if dot > 1 then dot = 1 end if dot < -1 then dot = -1 end "
              "local ang = math.deg(math.acos(dot)) "
              "return dd < " + range + " and ang < " + angle + " end)()";
        }
        default:
            return "0";
    }
}

// The expression wired into a data-input pin. If nothing is connected, use a
// sensible default for the pin's type (false for a Bool input, 0 for a Float).
std::string ScriptGraph::ExprForInput(int inputPin) const {
    const GraphNode* src = SourceOf(inputPin);
    if (!src) return PinTypeOf(inputPin) == PinType::Bool ? "false" : "0";

    // Almost every value node has a single output, so knowing the node is
    // enough to know the expression. On Collision is the exception: it hands
    // out four separate numbers, and which one is wanted depends on WHICH of
    // its pins the wire came from - information ExprForNode never sees, since
    // it is given only the node.
    if (src->kind == NodeKind::EventCollision) {
        for (const auto& l : m_links) {
            if (l.toPin != inputPin) continue;
            switch (PinToSlot(l.fromPin) - SlotDataOut) {
                case 0:  return "speed";
                case 1:  return "hx";
                case 2:  return "hy";
                default: return "hz";
            }
        }
    }
    // On Draw HUD is the other multi-output event, for the same reason: it hands
    // out the surface width and height, and only the wire says which.
    if (src->kind == NodeKind::EventDrawHud) {
        for (const auto& l : m_links) {
            if (l.toPin != inputPin) continue;
            return (PinToSlot(l.fromPin) - SlotDataOut == 0) ? "w" : "h";
        }
    }
    return ExprForNode(*src);
}

std::string ScriptGraph::ExprForInputOr(int inputPin, const char* fallback) const {
    return SourceOf(inputPin) ? ExprForInput(inputPin) : std::string(fallback);
}

// Emit every action wired to `fromExecPin`, in order. An exec output may fan
// out to several targets (so On Update can drive several independent checks);
// each target emits its statement and then its own continuation. This is
// recursive so a Branch can emit an if/else. `depth` guards against a loop.
void ScriptGraph::EmitExecChain(std::string& lua, int fromExecPin, int depth) const {
    if (depth > 256) return;

    for (const auto& l : m_links) {
        if (l.fromPin != fromExecPin) continue;
        const GraphNode* n = FindNode(PinToNode(l.toPin));
        if (!n) continue;

        switch (n->kind) {
            case NodeKind::Yaw:
                lua += "    entity.transform:rotate(0, 1, 0, " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::Pitch:
                lua += "    entity.transform:rotate(1, 0, 0, " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::Roll:
                lua += "    entity.transform:rotate(0, 0, 1, " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::MoveForward:
                lua += "    entity.transform:translateLocal(0, 0, -(" +
                       ExprForInput(PinId(n->id, SlotDataIn)) + "))\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::Print: {
                std::string msg(n->text), esc;
                for (char c : msg) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                lua += "    print(\"" + esc + "\")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::DestroySelf:
                lua += "    Scene.destroy(entity)\n";
                break;
            case NodeKind::SetVar:
                if (n->text[0])
                    lua += "    " + std::string(n->text) + " = " +
                           ExprForInput(PinId(n->id, SlotDataIn)) + "\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::SetPosX:
                lua += "    entity.transform.position.x = " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + "\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::SetPosY:
                lua += "    entity.transform.position.y = " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + "\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::SetPosZ:
                lua += "    entity.transform.position.z = " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + "\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::TurnToPlayer: {
                // Find the nearest player and rotate gradually toward it. A
                // unique local (by node id) avoids clashes between two of these.
                char buf[320];
                snprintf(buf, sizeof(buf),
                    "    local tgt%d = Scene.nearest(\"player\", entity.transform.position.x, entity.transform.position.y, entity.transform.position.z, 100000)\n"
                    "    if tgt%d ~= nil then entity.transform:rotateToward(tgt%d.transform.position.x, tgt%d.transform.position.y, tgt%d.transform.position.z, %s) end\n",
                    n->id, n->id, n->id, n->id, n->id,
                    ExprForInput(PinId(n->id, SlotDataIn)).c_str());
                lua += buf;
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::Fire: {
                // Spawn a bullet ahead of the entity, facing forward. The
                // distance is the node's own value, NOT a fixed step: a bullet
                // is a solid body, so one born inside the collider of whatever
                // fired it is shoved aside rather than launched. At real-world
                // scale an aircraft's capsule reaches many metres forward, so
                // this has to be authorable per graph. Load() gives graphs
                // written before the field existed the 3 they used to emit.
                std::string script(n->text), esc;
                for (char c : script) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                char buf[520];
                snprintf(buf, sizeof(buf),
                    "    local ff%d = entity.transform:forward()\n"
                    "    local pp%d = entity.transform.position\n"
                    "    Scene.spawn(\"Bullet\", pp%d.x+ff%d.x*%.7g, pp%d.y+ff%d.y*%.7g, pp%d.z+ff%d.z*%.7g, ff%d.x, ff%d.y, ff%d.z, \"%s\")\n",
                    n->id, n->id,
                    n->id, n->id, n->value, n->id, n->id, n->value, n->id, n->id, n->value,
                    n->id, n->id, n->id,
                    esc.c_str());
                lua += buf;
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::SetScale: {
                // One number drives all three axes, so a Number node feeding
                // "value" makes a uniform scale.
                std::string v = ExprForInput(PinId(n->id, SlotDataIn));
                lua += "    entity.transform.scale.x = " + v + "\n";
                lua += "    entity.transform.scale.y = " + v + "\n";
                lua += "    entity.transform.scale.z = " + v + "\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::HitNearest: {
                // Find the nearest entity of the given tag within reach; if one
                // exists, damage it and remove this entity (a bullet's impact).
                // A unique local (by node id) avoids clashes between two of these.
                std::string tag(n->text), esc;
                for (char c : tag) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }

                // The optional impact effect. It has to be emitted INSIDE the
                // "did we hit something" test: an effect placed after this node
                // instead would fire on every frame the entity existed, not on
                // the one frame it actually struck.
                std::string effect;
                if (n->text2[0] != '\0') {
                    std::string fxName(n->text2), esc2;
                    for (char c : fxName) { if (c == '"' || c == '\\') esc2 += '\\'; esc2 += c; }
                    effect = "\n    Fx.burst(\"" + esc2 +
                             "\", entity.transform.position.x, entity.transform.position.y,"
                             " entity.transform.position.z)";
                }

                char buf[600];
                snprintf(buf, sizeof(buf),
                    "    local hit%d = Scene.hit(\"%s\", entity.transform.position.x, entity.transform.position.y, entity.transform.position.z, %s)\n"
                    "    if hit%d ~= nil then\n"
                    "    Scene.damage(hit%d, %.7g)%s\n",
                    n->id, esc.c_str(),
                    ExprForInput(PinId(n->id, SlotDataIn)).c_str(),
                    n->id, n->id, n->value, effect.c_str());
                lua += buf;

                // Whatever is wired to "on hit" belongs INSIDE the test, before
                // this entity removes itself - a bullet that has already been
                // destroyed should not still be doing things.
                EmitExecChain(lua, PinId(n->id, SlotExecOut2), depth + 1);

                lua += "    Scene.destroy(entity)\n    end\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::AvoidCrowd: {
                // Push away from the nearest neighbour of the same tag when it
                // crowds within range, so a squadron spreads out. Node-scoped
                // locals (by id) keep two of these from clashing.
                std::string i = std::to_string(n->id);
                std::string tag(n->text), esc;
                for (char c : tag) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                std::string rng = ExprForInput(PinId(n->id, SlotDataIn));
                char fbuf[32]; snprintf(fbuf, sizeof(fbuf), "%.7g", n->value);
                std::string force = fbuf;
                lua +=
                  "    local rng" + i + " = " + rng + "\n"
                  "    local oth" + i + " = Scene.nearestOther(entity, \"" + esc + "\", rng" + i + ")\n"
                  "    if oth" + i + " ~= nil then\n"
                  "        local sx" + i + " = entity.transform.position.x - oth" + i + ".transform.position.x\n"
                  "        local sy" + i + " = entity.transform.position.y - oth" + i + ".transform.position.y\n"
                  "        local sz" + i + " = entity.transform.position.z - oth" + i + ".transform.position.z\n"
                  "        local sd" + i + " = math.sqrt(sx" + i + "*sx" + i + " + sy" + i + "*sy" + i + " + sz" + i + "*sz" + i + ")\n"
                  "        if sd" + i + " > 0.001 then\n"
                  "            local st" + i + " = (rng" + i + " - sd" + i + ") / rng" + i + "\n"
                  "            local pu" + i + " = " + force + " * st" + i + " * dt / sd" + i + "\n"
                  "            entity.transform.position.x = entity.transform.position.x + sx" + i + "*pu" + i + "\n"
                  "            entity.transform.position.y = entity.transform.position.y + sy" + i + "*pu" + i + "\n"
                  "            entity.transform.position.z = entity.transform.position.z + sz" + i + "*pu" + i + "\n"
                  "        end\n"
                  "    end\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::ChaseTarget: {
                // Hold a point behind and above the named target and look at it.
                // Node-scoped locals (by id) avoid clashes; the cox/coy/coz trio
                // is declared at file scope by GenerateLuaSource, because the
                // offset must survive between frames.
                //
                // The offset is what gets smoothed, NOT the camera's position.
                // Easing a position toward a point on a moving target settles a
                // standing `target speed / stiffness` behind it, so a fast target
                // ends up far further away than `distance` asks for and no value
                // of it helps. Smoothing the offset and adding the target's
                // position keeps the distance exact at any speed, and leaves
                // stiffness governing only the swing when the target ROTATES.
                std::string i = std::to_string(n->id);
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                std::string dist = ExprForInput(PinId(n->id, SlotDataIn));
                std::string hgt  = ExprForInput(PinId(n->id, SlotDataIn + 1));
                std::string stf  = ExprForInput(PinId(n->id, SlotDataIn + 2));
                lua +=
                  "    local jet" + i + " = Scene.find(\"" + esc + "\")\n"
                  "    if jet" + i + " ~= nil then\n"
                  "        local cd" + i + " = " + dist + "\n"
                  "        local ch" + i + " = " + hgt + "\n"
                  "        local cs" + i + " = " + stf + "\n"
                  "        local jf" + i + " = jet" + i + ".transform:forward()\n"
                  "        local wx" + i + " = -jf" + i + ".x * cd" + i + "\n"
                  "        local wy" + i + " = -jf" + i + ".y * cd" + i + " + ch" + i + "\n"
                  "        local wz" + i + " = -jf" + i + ".z * cd" + i + "\n"
                  "        if cox" + i + " == nil then cox" + i + ", coy" + i + ", coz" + i + " = wx" + i + ", wy" + i + ", wz" + i + " end\n"
                  "        local ca" + i + " = 1 - math.exp(-cs" + i + " * dt)\n"
                  "        cox" + i + " = cox" + i + " + (wx" + i + " - cox" + i + ") * ca" + i + "\n"
                  "        coy" + i + " = coy" + i + " + (wy" + i + " - coy" + i + ") * ca" + i + "\n"
                  "        coz" + i + " = coz" + i + " + (wz" + i + " - coz" + i + ") * ca" + i + "\n"
                  "        entity.transform.position.x = jet" + i + ".transform.position.x + cox" + i + "\n"
                  "        entity.transform.position.y = jet" + i + ".transform.position.y + coy" + i + "\n"
                  "        entity.transform.position.z = jet" + i + ".transform.position.z + coz" + i + "\n"
                  "        entity.transform:lookAt(jet" + i + ".transform.position.x, jet" + i + ".transform.position.y, jet" + i + ".transform.position.z)\n"
                  "    end\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::HudSet: {
                // Publish a number for the C++ HUD to read by name.
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                lua += "    Hud.set(\"" + esc + "\", " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::FxBurst: {
                // Fire a named particle effect. The position is this entity's,
                // pushed forward by the node's offset field so a muzzle flash
                // can sit at the nose rather than inside the model. Locals are
                // named per node id so two bursts in one graph cannot collide.
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }

                // An unwired data input reads as 0, which would mean "an effect
                // at zero size". Full size is the sensible reading of "I did not
                // connect anything here", and it keeps the generated Lua honest
                // rather than relying on the engine to reinterpret a 0.
                std::string scale = SourceOf(PinId(n->id, SlotDataIn))
                                        ? ExprForInput(PinId(n->id, SlotDataIn))
                                        : std::string("1");

                char buf[420];
                snprintf(buf, sizeof(buf),
                    "    local fxf%d = entity.transform:forward()\n"
                    "    local fxp%d = entity.transform.position\n"
                    "    Fx.burst(\"%s\", fxp%d.x+fxf%d.x*%.7g, fxp%d.y+fxf%d.y*%.7g, fxp%d.z+fxf%d.z*%.7g, %s)\n",
                    n->id, n->id, esc.c_str(),
                    n->id, n->id, n->value, n->id, n->id, n->value,
                    n->id, n->id, n->value,
                    scale.c_str());
                lua += buf;
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::PlaySound: {
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                // An unwired volume means "as defined in sounds.lua", which is 1,
                // not the 0 an unconnected input would otherwise read as.
                std::string vol = SourceOf(PinId(n->id, SlotDataIn))
                                      ? ExprForInput(PinId(n->id, SlotDataIn))
                                      : std::string("1");
                lua += "    Audio.play(\"" + esc + "\", " + vol + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::LoopStart:
            case NodeKind::LoopStop: {
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                const char* fn = (n->kind == NodeKind::LoopStart) ? "loopStart"
                                                                  : "loopStop";
                lua += std::string("    Audio.") + fn + "(\"" + esc + "\")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::LoopSet: {
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                // Unwired inputs mean "leave it as defined", which is 1 for both
                // volume and pitch - not the 0 an unconnected input reads as.
                std::string vol = SourceOf(PinId(n->id, SlotDataIn))
                                      ? ExprForInput(PinId(n->id, SlotDataIn))
                                      : std::string("1");
                std::string pit = SourceOf(PinId(n->id, SlotDataIn + 1))
                                      ? ExprForInput(PinId(n->id, SlotDataIn + 1))
                                      : std::string("1");
                lua += "    Audio.loopSet(\"" + esc + "\", " + vol + ", " + pit + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::SetLightIntensity: {
                // Set the scene light's brightness. It writes to the light
                // entity's component, so the change sticks frame to frame.
                lua += "    Light.setIntensity(" +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::For: {
                // Numeric for: run the body once per step, then the "done" chain.
                // The loop variable is i<id>, matched by ExprForNode above.
                std::string i    = std::to_string(n->id);
                std::string from = ExprForInput(PinId(n->id, SlotDataIn));
                std::string to   = ExprForInput(PinId(n->id, SlotDataIn + 1));
                lua += "    for i" + i + " = " + from + ", " + to + " do\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);   // body
                lua += "    end\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut2), depth + 1);  // done
                break;
            }
            case NodeKind::SpawnCube: {
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                lua += "    Scene.spawnCube(\"" + esc + "\", " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 1)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 2)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::Spawn: {
                // Scene.spawn(name, x,y,z, dx,dy,dz, script, tag, hp). The tag
                // doubles as the entity name (gameplay identifies by tag).
                std::string script(n->text), es1;
                for (char c : script) { if (c == '"' || c == '\\') es1 += '\\'; es1 += c; }
                std::string tag(n->text2), es2;
                for (char c : tag) { if (c == '"' || c == '\\') es2 += '\\'; es2 += c; }
                std::string model(n->text3), es3;
                for (char c : model) { if (c == '"' || c == '\\') es3 += '\\'; es3 += c; }
                char hp[32]; snprintf(hp, sizeof(hp), "%.7g", n->value);
                lua += "    Scene.spawn(\"" + es2 + "\", " +
                       ExprForInput(PinId(n->id, SlotDataIn))     + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 1)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 2)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 3)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 4)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 5)) + ", \"" +
                       es1 + "\", \"" + es2 + "\", " + hp;
                // The model is the last argument and is optional, so it is left
                // off entirely when unset rather than passed as an empty string.
                // That keeps the generated Lua identical to what it was before
                // this node gained the field, so old graphs regenerate unchanged.
                if (!es3.empty()) lua += ", \"" + es3 + "\"";
                lua += ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            // ---- HUD drawing ------------------------------------------------
            // Each of these resolves its anchor into `w`/`h` arithmetic, so the
            // element follows the view when it is resized instead of sitting at
            // a coordinate that was only ever right at one size.
            case NodeKind::DrawText:
            case NodeKind::DrawValue: {
                const bool isValue = (n->kind == NodeKind::DrawValue);
                std::string label(n->text), esc;
                for (char c : label) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }

                std::string ax, ay;
                AnchorExpr(n->text2, ax, ay);
                const int align = AnchorAlign(n->text2);
                // The text size lives in the node's own value; a size of 0 means
                // the node predates the field, so it gets a readable default
                // rather than being drawn invisibly small.
                const float sz = (n->value > 0.5f) ? n->value : 20.0f;

                const int base = isValue ? 1 : 0;   // Draw Value's first input is the number
                const std::string dx = ExprForInput(PinId(n->id, SlotDataIn + base));
                const std::string dy = ExprForInput(PinId(n->id, SlotDataIn + base + 1));

                const std::string i = std::to_string(n->id);
                char buf[64];
                snprintf(buf, sizeof(buf), "%.7g", sz);
                const std::string szs = buf;

                // Build the string first, then position it: right- and
                // centre-anchored text needs its own measured width, which is
                // only knowable once the text exists.
                if (isValue) {
                    const std::string v = ExprForInput(PinId(n->id, SlotDataIn));
                    lua += "    local ds" + i + " = \"" + esc + "\" .. string.format(\"%.0f\", " + v + ")\n";
                } else {
                    lua += "    local ds" + i + " = \"" + esc + "\"\n";
                }
                lua += "    local dw" + i + " = Draw.textWidth(ds" + i + ", " + szs + ")\n";
                std::string xExpr = ax + " + (" + dx + ")";
                if (align == 2)      xExpr += " - dw" + i;
                else if (align == 1) xExpr += " - dw" + i + "*0.5";
                lua += "    Draw.text(ds" + i + ", " + xExpr + ", " + ay + " + (" + dy + "), "
                     + szs + ColorArg(n->text3) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::DrawBar: {
                // An outline plus a fill clamped to 0..1. Two calls rather than
                // one node per piece, because a bar is always both and wiring
                // them separately every time would be busywork.
                std::string ax, ay;
                AnchorExpr(n->text2, ax, ay);
                const std::string i  = std::to_string(n->id);
                const std::string f  = ExprForInput(PinId(n->id, SlotDataIn));
                const std::string dx = ExprForInput(PinId(n->id, SlotDataIn + 1));
                const std::string dy = ExprForInput(PinId(n->id, SlotDataIn + 2));
                // Sizes fall back to something visible rather than to zero.
                const std::string bw = ExprForInputOr(PinId(n->id, SlotDataIn + 3), "150");
                const std::string bh = ExprForInputOr(PinId(n->id, SlotDataIn + 4), "12");
                lua += "    local bx" + i + " = " + ax + " + (" + dx + ")\n";
                lua += "    local by" + i + " = " + ay + " + (" + dy + ")\n";
                lua += "    local bw" + i + " = " + bw + "\n";
                lua += "    local bh" + i + " = " + bh + "\n";
                // Clamped, because a fraction outside 0..1 would draw a fill
                // spilling past its own outline.
                lua += "    local bf" + i + " = " + f + "\n";
                lua += "    if bf" + i + " < 0 then bf" + i + " = 0 elseif bf" + i + " > 1 then bf" + i + " = 1 end\n";
                lua += "    Draw.rectLines(bx" + i + ", by" + i + ", bw" + i + ", bh" + i + ColorArg(n->text3) + ")\n";
                lua += "    Draw.rect(bx" + i + "+2, by" + i + "+2, (bw" + i + "-4)*bf" + i
                     + ", bh" + i + "-4" + ColorArg(n->text3) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::DrawRect: {
                std::string ax, ay;
                AnchorExpr(n->text2, ax, ay);
                const std::string dx = ExprForInput(PinId(n->id, SlotDataIn));
                const std::string dy = ExprForInput(PinId(n->id, SlotDataIn + 1));
                const std::string rw = ExprForInputOr(PinId(n->id, SlotDataIn + 2), "100");
                const std::string rh = ExprForInputOr(PinId(n->id, SlotDataIn + 3), "20");
                // The value field doubles as the filled/outline switch, so the
                // two do not need separate node kinds.
                const char* fn = (n->value > 0.5f) ? "Draw.rect" : "Draw.rectLines";
                lua += std::string("    ") + fn + "(" + ax + " + (" + dx + "), "
                     + ay + " + (" + dy + "), " + rw + ", " + rh
                     + ColorArg(n->text3) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::DrawLine: {
                std::string ax, ay;
                AnchorExpr(n->text2, ax, ay);
                const std::string x1 = ExprForInput(PinId(n->id, SlotDataIn));
                const std::string y1 = ExprForInput(PinId(n->id, SlotDataIn + 1));
                // A line with both ends at the anchor would be invisible, so the
                // far end defaults to a short horizontal stroke.
                const std::string x2 = ExprForInputOr(PinId(n->id, SlotDataIn + 2), "24");
                const std::string y2 = ExprForInputOr(PinId(n->id, SlotDataIn + 3), "0");
                lua += "    Draw.line(" + ax + " + (" + x1 + "), " + ay + " + (" + y1 + "), "
                     + ax + " + (" + x2 + "), " + ay + " + (" + y2 + ")"
                     + ColorArg(n->text3) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::DrawCircle: {
                std::string ax, ay;
                AnchorExpr(n->text2, ax, ay);
                const std::string dx = ExprForInput(PinId(n->id, SlotDataIn));
                const std::string dy = ExprForInput(PinId(n->id, SlotDataIn + 1));
                const std::string r  = ExprForInputOr(PinId(n->id, SlotDataIn + 2), "6");
                const char* fn = (n->value > 0.5f) ? "Draw.circle" : "Draw.circleLines";
                lua += std::string("    ") + fn + "(" + ax + " + (" + dx + "), "
                     + ay + " + (" + dy + "), " + r + ColorArg(n->text3) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::SetCollider: {
                std::string shape(n->text);
                if (shape.empty()) shape = "sphere";
                lua += "    Scene.setCollider(entity, \"" + shape + "\", " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::HudAdd: {
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                lua += "    Hud.add(\"" + esc + "\", " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }

            // --- Stage 13: physics and collisions ------------------------
            case NodeKind::DamageOther:
                lua += "    Scene.damage(other, " +
                       ExprForInput(PinId(n->id, SlotDataIn)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            case NodeKind::SetBody: {
                // Motion type is a node field; `value` doubles as the
                // continuous-collision flag, drawn as a checkbox.
                std::string motion(n->text);
                if (motion.empty()) motion = "dynamic";

                // Only pass the optional arguments that were actually wired.
                // Emitting an unwired input as 0 would not be a harmless
                // default: Physics.setBody treats every argument it receives
                // as a decision, so a literal 0 would set the mass to zero
                // rather than leave the component's own value alone.
                const bool hasMass    = SourceOf(PinId(n->id, SlotDataIn))     != nullptr;
                const bool hasGravity = SourceOf(PinId(n->id, SlotDataIn + 1)) != nullptr;
                const bool sweep      = (n->value != 0.0f);

                lua += "    Physics.setBody(entity, \"" + motion + "\"";
                if (hasMass || hasGravity || sweep)
                    lua += ", " + ExprForInput(PinId(n->id, SlotDataIn));
                if (hasGravity || sweep)
                    lua += ", " + ExprForInput(PinId(n->id, SlotDataIn + 1));
                if (sweep) lua += ", true";
                lua += ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::SetVelocity:
            case NodeKind::ApplyForce:
            case NodeKind::ApplyLocalForce: {
                const char* fn = (n->kind == NodeKind::SetVelocity) ? "setVelocity"
                               : (n->kind == NodeKind::ApplyForce)  ? "applyForce"
                                                                    : "applyLocalForce";
                lua += std::string("    Physics.") + fn + "(entity, " +
                       ExprForInput(PinId(n->id, SlotDataIn))     + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 1)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 2)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::PlaySoundAt:
            case NodeKind::FxBurstAt: {
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                const bool sound = (n->kind == NodeKind::PlaySoundAt);
                lua += std::string("    ") + (sound ? "Audio.playAt(\"" : "Fx.burst(\"") +
                       esc + "\", " +
                       ExprForInput(PinId(n->id, SlotDataIn))     + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 1)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 2));
                // An effect takes a size; a sound does not.
                if (!sound) {
                    char sc[32]; snprintf(sc, sizeof(sc), "%.7g",
                                          n->value > 0.0f ? n->value : 1.0f);
                    lua += std::string(", ") + sc;
                }
                lua += ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }
            case NodeKind::LoopAt: {
                std::string name(n->text), esc;
                for (char c : name) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                lua += "    Audio.loopAt(\"" + esc + "\", " +
                       ExprForInput(PinId(n->id, SlotDataIn))     + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 1)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 2)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 3)) + ", " +
                       ExprForInput(PinId(n->id, SlotDataIn + 4)) + ")\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut), depth + 1);
                break;
            }

            case NodeKind::Branch: {
                lua += "    if " + ExprForInput(PinId(n->id, SlotDataIn)) + " then\n";
                EmitExecChain(lua, PinId(n->id, SlotExecOut),  depth + 1);
                // Only emit an "else" arm when something is actually wired to the
                // false output; a bare guard (if ... then ... end) reads cleaner.
                int falsePin = PinId(n->id, SlotExecOut2);
                bool hasElse = false;
                for (const auto& e : m_links)
                    if (e.fromPin == falsePin) { hasElse = true; break; }
                if (hasElse) {
                    lua += "    else\n";
                    EmitExecChain(lua, falsePin, depth + 1);
                }
                lua += "    end\n";
                break;
            }
            default:
                break;
        }
    }
}

void ScriptGraph::EmitEvent(std::string& lua, NodeKind ev,
                            const char* header, bool provideDt) const {
    const GraphNode* evNode = nullptr;
    for (const auto& n : m_nodes) if (n.kind == ev) { evNode = &n; break; }
    if (!evNode) return;

    // Only emit the function if something is wired to the event's exec output.
    int execOut = PinId(evNode->id, SlotExecOut);
    bool hasChain = false;
    for (const auto& l : m_links) if (l.fromPin == execOut) { hasChain = true; break; }
    if (!hasChain) return;

    (void)provideDt;   // dt is already the onUpdate parameter; nothing to add
    lua += header;
    EmitExecChain(lua, execOut);
    lua += "end\n\n";
}

// Build the Lua for this graph and return it as a string.
//
// This is the whole code generator. It is separate from writing a file because
// a graph attached to an entity is compiled straight into memory and run - no
// .lua is produced at all in that case - while the Generate Lua button still
// wants one on disk.
std::string ScriptGraph::GenerateLuaSource() const {
    std::string lua =
        "-- GENERATED from a node graph. Edit the GRAPH, not this file --\n\n";

    // Collect the Param nodes into a `properties` table. Each unique name (the
    // first occurrence wins its default) becomes an Inspector-editable field the
    // engine reads at load. Param value nodes reference these as properties.<name>.
    std::vector<std::pair<std::string, float>> props;
    for (const GraphNode& n : m_nodes) {
        if (n.kind != NodeKind::Param || n.text[0] == '\0') continue;
        bool seen = false;
        for (const auto& p : props) if (p.first == n.text) { seen = true; break; }
        if (!seen) props.push_back({n.text, n.value});
    }
    if (!props.empty()) {
        lua += "properties = {\n";
        for (const auto& p : props) {
            char buf[160];
            snprintf(buf, sizeof(buf), "    %s = %.7g,\n", p.first.c_str(), p.second);
            lua += buf;
        }
        lua += "}\n\n";
    }

    // Declare the graph's variables as file-scope locals so they keep their
    // value between frames.
    for (const GraphVar& v : m_vars) {
        char buf[128];
        snprintf(buf, sizeof(buf), "local %s = %.7g\n", v.name.c_str(), v.init);
        lua += buf;
    }
    if (!m_vars.empty()) lua += "\n";

    // Chase Target keeps its camera OFFSET from one frame to the next, so each
    // such node needs its own file-scope storage - a local inside onUpdate would
    // be forgotten every frame and the smoothing would have nothing to smooth
    // from. They start as nil, which the node's own code reads as "no previous
    // offset yet" and snaps into place on the first frame.
    //
    // Why an offset rather than the camera's position: easing a position toward a
    // point on a MOVING target never catches it, and settles a standing
    // `speed / stiffness` behind - which for a fast aircraft is far enough to
    // swamp the distance setting completely. Smoothing the offset and adding the
    // target's position afterwards makes the follow exact at any speed.
    bool anyChase = false;
    for (const GraphNode& n : m_nodes) {
        if (n.kind != NodeKind::ChaseTarget) continue;
        char buf[160];
        const std::string i = std::to_string(n.id);
        snprintf(buf, sizeof(buf), "local cox%s, coy%s, coz%s = nil, nil, nil\n",
                 i.c_str(), i.c_str(), i.c_str());
        lua += buf;
        anyChase = true;
    }
    if (anyChase) lua += "\n";

    EmitEvent(lua, NodeKind::EventCreate,  "function onStart(entity)\n",      false);
    EmitEvent(lua, NodeKind::EventUpdate,  "function onUpdate(entity, dt)\n", true);
    EmitEvent(lua, NodeKind::EventDestroy, "function onDestroy(entity)\n",    false);
    // The collision handler takes more parameters than the others: what was
    // hit, how hard, and where. These names are exactly what On Collision's
    // output pins generate, so the two have to agree.
    EmitEvent(lua, NodeKind::EventCollision,
              "function onCollision(entity, other, speed, hx, hy, hz)\n", false);
    // The HUD pass. `w` and `h` are the surface size in pixels, and the drawing
    // nodes refer to them by name when they resolve an anchor - which is why the
    // parameters must be called exactly this.
    EmitEvent(lua, NodeKind::EventDrawHud,
              "function onDrawHud(entity, w, h)\n", false);

    return lua;
}

// Generate the Lua and write it to a file, for when a .lua on disk is what is
// wanted (the Generate Lua button, or baking a graph for a build).
bool ScriptGraph::GenerateLua(const std::string& path) const {
    std::string lua = GenerateLuaSource();

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return false;
    f << lua;
    return true;
}

} // namespace edtr
