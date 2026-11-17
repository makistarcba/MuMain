#pragma once

#include <RmlUi/Core/Types.h>

#include <vector>

namespace mu::ui::window
{
// A rectangle of the window, physical px: one of the regions the map may paint (see
// CMiniMap::SyncClips()).
struct MiniMapClipEntry
{
    float left = 0.f, top = 0.f, width = 0.f, height = 0.f;
    // -left / -top: .world's offset back to window coordinates (the data expressions have no
    // unary minus).
    float worldLeft = 0.f, worldTop = 0.f;
};

struct MiniMapMarkerEntry
{
    bool portal = false;   // MINI_MAP::Kind 2 (a gate), else an NPC
    float size = 15.f;     // the icon's side, physical px (the original never scaled it)
    Rml::String transform; // CSS matrix() placing the icon element
};

// VORTHANIA: a straight stroke drawn over the map (the walk route, the hover preview, the
// destination cross and the spot diamonds): a 1x1 element stretched by its matrix.
struct MiniMapLineEntry
{
    Rml::String transform;
    Rml::String color;
};

struct MiniMapRmlModel
{
    float textPx = 0.f; // native text size in physical px (RmlNativeTextSize.h)

    std::vector<MiniMapClipEntry> clips;

    Rml::String mapSource;    // the world's mini_map texture, relative to the document
    Rml::String mapTransform; // CSS matrix() placing the 800x800 map element
    std::vector<MiniMapMarkerEntry> markers;
    std::vector<Rml::String> sideLines; // CSS matrix() of each left/right border tile
    std::vector<MiniMapLineEntry> vorthLines; // VORTHANIA: route, preview and spots
    Rml::String closeHint;

    // The hovered marker's name (CMiniMap::Check_Btn()), physical px.
    bool hintVisible = false;
    Rml::String hintText;
    float hintLeft = 0.f, hintTop = 0.f, hintWidth = 0.f, hintHeight = 0.f;
};
} // namespace mu::ui::window
