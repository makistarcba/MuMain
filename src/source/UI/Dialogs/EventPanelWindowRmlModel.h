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

    // Where the box goes, physical window px: computed in C++ (top-right corner of the window, at
    // the UI scale), so no theme transform can move it.
    float panelLeft = 0.f;
    float panelTop = 0.f;
    float panelWidth = 0.f;
    float gap = 0.f; // padding and indents step, physical px

    Rml::String serverTime;
    std::vector<EventPanelLineEntry> lines;
};
} // namespace mu::ui::window
