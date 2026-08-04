#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {
// ============================================================================
// MinimapComponent: a player-centred radar, drawn over the game view.
// ----------------------------------------------------------------------------
// Attach it to the entity it should be centred on - the player's aircraft. It
// draws in SCREEN space through OnDrawHud, not in the world, so it appears in
// the Game view over the finished 3D image.
//
// WHY IT IS DRAWN RATHER THAN RENDERED. The obvious way to build a minimap is a
// second camera looking straight down into a texture, which is what large
// engines offer. That costs a COMPLETE extra pass over the scene, and almost all
// of it is wasted here: at minimap size an aircraft is smaller than a pixel, so
// the only thing that pass really contributes is the landscape - which this
// engine can already produce as data, without drawing anything. So the terrain
// is baked into a small image ONCE and aircraft are drawn as symbols on top.
// That is also what a radar in a real aircraft is: symbols, not a picture.
//
// HEADING-UP. The map turns so the aircraft always points up the screen, the
// convention for anything you sit inside. It means a contact drawn above you is
// ahead of you, which is the question being asked mid-dogfight. The cost is that
// north moves, so a small tick marks where north has gone.
// ============================================================================
class MinimapComponent : public Component {
public:
    ~MinimapComponent() override;

    const char* Name() const override { return "Minimap"; }

    // Copied field by field rather than with the compiler's own copy, because
    // this component owns a TEXTURE. A blind copy would duplicate that handle
    // and both copies would later free the same image. The clone starts with no
    // image and bakes its own on first use.
    std::unique_ptr<Component> Clone() const override;

    void OnInspector() override;
    void OnDrawHud(const Entity& owner, int width, int height) override;

    void Serialize(nlohmann::json& out) const override {
        out["range"] = range;   out["size"]    = size;
        out["corner"] = corner; out["terrain"] = showTerrain;
        out["tag"] = blipTag;
        out["targetTag"] = targetTag; out["targetSize"] = targetSize;
    }
    void Deserialize(const nlohmann::json& in) override {
        range       = in.value("range",  range);
        size        = in.value("size",   size);
        corner      = in.value("corner", corner);
        showTerrain = in.value("terrain", showTerrain);
        blipTag     = in.value("tag", blipTag);
        targetTag   = in.value("targetTag",  targetTag);
        targetSize  = in.value("targetSize", targetSize);
    }

    // How far the radar reaches, in metres. Contacts beyond this are held at the
    // rim rather than dropped, so a threat never simply vanishes.
    float range = 5000.0f;
    int   size  = 200;          // width and height on screen, in pixels
    int   corner = 3;           // 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right
    bool  showTerrain = true;   // draw the baked landscape behind the contacts
    std::string blipTag = "enemy";   // which tag counts as a contact

    // A SECOND tag, drawn differently: as a ring marking an area to attack
    // rather than as a point contact to avoid.
    //
    // The two are not the same kind of thing and must not look the same. A blip
    // is something moving that may be about to shoot you; a target is a fixed
    // place you are trying to reach. Drawing both as dots would leave the radar
    // saying "there are things over there" without saying which of them matters,
    // which is most of what a radar is for.
    //
    // Empty turns it off, so a scene with no objectives shows nothing extra.
    std::string targetTag = "camp";
    // How big the ring is on the radar, in pixels. A target area is a PLACE, so
    // it is drawn at a size that reads as ground rather than as a pinpoint.
    float targetSize = 7.0f;

private:
    // Bake the landscape into an image, once. Returns false when there is no
    // terrain in the scene, in which case the radar simply has no backdrop.
    bool EnsureTerrain();

    Texture2D m_terrain{};      // the baked relief image
    bool  m_built = false;      // has it been baked?
    bool  m_tried = false;      // has baking been ATTEMPTED? (so a failure is not retried every frame)
    float m_worldSize = 0.0f;   // the terrain's span in metres, needed to map world to pixel
    Vector3 m_worldCentre{};    // where the terrain's middle sits in the world
};
} // namespace eng
