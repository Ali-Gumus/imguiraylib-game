#pragma once
// `#pragma once` tells the compiler to include this file only once per build,
// even if several files #include it. It prevents "redefinition" errors.

// nlohmann/json is a third-party library that turns C++ data into JSON text
// (used to save/load scenes to disk) and back. `json` is its main type.
#include <nlohmann/json.hpp>

// <memory> provides std::unique_ptr, a smart pointer that owns a heap object
// and automatically deletes it when the pointer goes away (no manual delete).
#include <memory>

// Everything in this engine lives in the `eng` namespace so its names don't
// clash with the game libraries (raylib, ImGui, ...).
namespace eng {

// "Forward declaration": we promise the compiler that a type called Entity
// exists, without pulling in its full definition. That lets this header refer
// to Entity& / Entity* without #including Scene.h, which keeps the two files
// from depending on each other in a circle.
struct Entity;

// Component is the BASE CLASS for anything you can attach to an entity: a
// shape to draw, a script, a camera, a health bar, and so on. An entity by
// itself is just an id, a name and a transform (position/rotation/scale);
// every other capability is added as a Component.
//
// This is an "abstract" class: it defines an interface of virtual functions
// that concrete components (ShapeComponent, ScriptComponent, ...) override.
// Because the functions are `virtual`, calling them through a Component
// pointer runs the correct derived version at run time ("polymorphism").
class Component {
public:
    // Virtual destructor. Essential for a base class you delete through a
    // base pointer: it guarantees the derived class's destructor also runs,
    // so no memory leaks. `= default` asks the compiler to generate it.
    virtual ~Component() = default;

    // The name shown as this component's header row in the Inspector panel.
    // `= 0` makes it "pure virtual": Component provides no body, so every
    // concrete component is REQUIRED to supply one (and you cannot create a
    // bare Component, only its subclasses).
    virtual const char* Name() const = 0;

    // May an entity hold more than one of this component type? Most types
    // want only one (a single shape), so the default is false. A type that
    // allows several (e.g. multiple scripts) overrides this to return true.
    virtual bool AllowMultiple() const { return false; }

    // Make an independent copy of this component and return it as a
    // unique_ptr. Pure virtual so every component must say how to copy
    // itself; the editor uses this to snapshot the whole scene when you
    // press Play (so pressing Stop can restore the original).
    virtual std::unique_ptr<Component> Clone() const = 0;

    // Save / load this component's DATA to a JSON object. These have empty
    // default bodies (`{}`) so a component with nothing to save can simply
    // not override them. The scene stores each component's JSON under its
    // Name(), so two components never overwrite each other's keys.
    virtual void Serialize(nlohmann::json& out) const {}
    virtual void Deserialize(const nlohmann::json& in) {}

    // --- Lifecycle hooks: the engine calls these at set moments. ----------
    // `owner` is the entity this component is attached to. Empty default
    // bodies mean a component only overrides the hooks it actually cares
    // about.

    // Called once, when play mode begins (or when the entity is spawned
    // during play). Good place to initialize.
    virtual void OnStart(Entity& owner) {}

    // Called when the owning entity is about to be destroyed. Last chance
    // to react before its memory is freed.
    virtual void OnDestroy(Entity& owner) {}

    // Called every frame while playing, before anything is drawn. Game
    // logic and movement go here. `dt` is the seconds since the last frame.
    virtual void OnUpdate(float dt, Entity& owner) {}

    // Called every frame during 3D rendering. A visual component draws
    // itself here; `owner` is const because drawing must not change the
    // entity's data.
    virtual void OnDraw(const Entity& owner) {}

    // Called while the Inspector panel is open, so the component can draw
    // its own editing widgets (sliders, colors, ...). The Inspector loops
    // over a component list and calls this on each, never needing to know
    // the concrete type.
    virtual void OnInspector() {}
};

} // namespace eng
