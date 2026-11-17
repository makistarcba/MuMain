#pragma once

// VORTHANIA: the data model of the event panel (key H), event_panel.rml.

#include <RmlUi/Core/Types.h>

#include <vector>

namespace mu::ui::window
{
// One row of the panel: a text on the left, an optional one on the right (a clock, "vivos/total"),
// its colour and how far it is indented (0, 1 or 2 steps).
struct EventPanelLineEntry
{
    Rml::String left;
    Rml::String right;
    Rml::String color;
    Rml::String rightColor;
    int indent = 0;
};

struct EventPanelWindowRmlModel
{
    float textPx = 0.f;
    float boldTextPx = 0.f;
    float lineHeight = 0.f;
    float boldLineHeight = 0.f;

    Rml::String serverTime;
    std::vector<EventPanelLineEntry> lines;
};
} // namespace mu::ui::window
