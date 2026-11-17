// VORTHANIA: the event panel (key H), ported to RmlUi from the old client's
// NewUIEventPanelWindow (same rows, texts and colours).

#include "stdafx.h"
#include "UI/Dialogs/EventPanelWindow.h"
#include "UI/Core/WindowSystem.h"
#include "Audio/DSPlaySound.h"
#include "GameLogic/Events/EventPanelState.h"

#include "Core/Utilities/StringUtils.h"
#include "Render/Text/CUIRenderTextSDLTtf.h"
#include "UI/RmlBridge/RmlDocumentVisibility.h"
#include "UI/RmlBridge/RmlNativeTextSize.h"
#include "UI/Scaling/UITransform.h"

#include <RmlUi/Core/ElementDocument.h>

#include <chrono>
#include <string>
#include <vector>

using namespace SEASON3B;
using namespace mu::ui::window;
using GameLogic::Events::EventPanel;
using GameLogic::Events::EventPanelEventState;
using GameLogic::Events::EventPanelKind;

namespace
{
// The old panel's colours.
constexpr const char* ClockColor = "rgb(170,170,170)";
constexpr const char* EventColor = "rgb(220,220,220)";
constexpr const char* RunningColor = "rgb(120,255,120)";
constexpr const char* DetailColor = "rgb(170,190,210)";
constexpr const char* MapColor = "rgb(255,200,120)";
constexpr const char* MonsterColor = "rgb(230,230,230)";
constexpr const char* MonsterDoneColor = "rgb(120,120,120)";

// More rows than this don't fit the screen; the rest is cut with "...".
constexpr size_t MaxLines = 40;

std::wstring TwoDigits(int value)
{
    wchar_t buffer[8] = {};
    mu_swprintf(buffer, L"%02d", value);
    return buffer;
}

std::wstring ClockText(int hour, int minute)
{
    return TwoDigits(hour) + L":" + TwoDigits(minute);
}

// "1 h 48 min", "48 min", "35 s".
std::wstring DurationText(int64_t seconds)
{
    if (seconds < 60)
        return std::to_wstring(seconds) + L" s";

    const auto minutes = seconds / 60;
    if (minutes < 60)
        return std::to_wstring(minutes) + L" min";

    return std::to_wstring(minutes / 60) + L" h " + std::to_wstring(minutes % 60) + L" min";
}

// "12:34" for a running countdown, so that it visibly moves.
std::wstring CountdownText(int64_t seconds)
{
    const auto minutes = seconds / 60;
    if (minutes >= 60)
        return DurationText(seconds);

    return std::to_wstring(minutes) + L":" + TwoDigits(static_cast<int>(seconds % 60));
}

Rml::String Narrow(const std::wstring& text)
{
    return StringUtils::WideToNarrow(text.c_str());
}

void AddLine(std::vector<EventPanelLineEntry>& lines, const std::wstring& left, const std::wstring& right,
             const char* color, int indent, const char* rightColor = nullptr)
{
    EventPanelLineEntry entry;
    entry.left = Narrow(left);
    entry.right = Narrow(right);
    entry.color = color;
    entry.rightColor = rightColor != nullptr ? rightColor : color;
    entry.indent = indent;
    lines.push_back(std::move(entry));
}

std::vector<EventPanelLineEntry> BuildLines()
{
    std::vector<EventPanelLineEntry> lines;
    const auto& panel = EventPanel();
    if (!panel.HasData())
    {
        AddLine(lines, L"Cargando...", L"", DetailColor, 0);
        return lines;
    }

    if (panel.GetEntries().empty())
    {
        AddLine(lines, L"No hay eventos programados.", L"", DetailColor, 0);
        return lines;
    }

    for (const auto& entry : panel.GetEntries())
    {
        const auto remaining = panel.GetRemainingSeconds(entry);
        switch (entry.State)
        {
        case EventPanelEventState::Running:
        {
            // The right column always holds a clock time (here: when it started), so that a
            // countdown like "12:44" isn't mistaken for one.
            AddLine(lines, entry.Name, ClockText(entry.StartHour, entry.StartMinute), RunningColor, 0, ClockColor);
            std::wstring detail = entry.Kind == EventPanelKind::MiniGame ? L"Entrada abierta" : L"En curso";
            detail += L", quedan " + CountdownText(remaining);
            AddLine(lines, detail, L"", DetailColor, 1);

            if (!entry.Monsters.empty())
            {
                int alive = 0;
                int total = 0;
                for (const auto& group : entry.Monsters)
                {
                    alive += group.Alive;
                    total += group.Total;
                }

                AddLine(lines, L"Vivos", std::to_wstring(alive) + L"/" + std::to_wstring(total), DetailColor, 1);
            }

            // The groups arrive sorted by map: a map header whenever the map changes.
            const std::wstring* lastMap = nullptr;
            for (const auto& group : entry.Monsters)
            {
                if (lastMap == nullptr || *lastMap != group.MapName)
                {
                    AddLine(lines, group.MapName, L"", MapColor, 1);
                    lastMap = &group.MapName;
                }

                AddLine(lines, group.MonsterName, std::to_wstring(group.Alive) + L"/" + std::to_wstring(group.Total),
                        group.Alive > 0 ? MonsterColor : MonsterDoneColor, 2);
            }
            break;
        }

        case EventPanelEventState::Starting:
            AddLine(lines, entry.Name, L"ya", RunningColor, 0);
            AddLine(lines, L"Empieza en segundos", L"", DetailColor, 1);
            break;

        case EventPanelEventState::Scheduled:
            AddLine(lines, entry.Name, ClockText(entry.StartHour, entry.StartMinute), EventColor, 0, ClockColor);
            AddLine(lines, L"en " + DurationText(remaining), L"", DetailColor, 1);
            break;

        default:
            AddLine(lines, entry.Name, L"--:--", EventColor, 0, ClockColor);
            AddLine(lines, L"Sin horario", L"", DetailColor, 1);
            break;
        }
    }

    if (lines.size() > MaxLines)
    {
        lines.resize(MaxLines - 1);
        AddLine(lines, L"...", L"", DetailColor, 0);
    }

    return lines;
}

long long NowSecond()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

CEventPanelWindow::CEventPanelWindow() = default;

CEventPanelWindow::~CEventPanelWindow()
{
    Release();
}

bool CEventPanelWindow::Create(CManager* pNewUIMng)
{
    if (pNewUIMng == nullptr)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(mu::ui::window::INTERFACE_EVENT_PANEL, this);

    m_RmlView.Ensure();

    Show(false);
    return true;
}

void CEventPanelWindow::Release()
{
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }

    m_RmlView.Release();
}

bool CEventPanelWindow::UpdateMouseEvent()
{
    // The panel only shows text; the mouse goes through it, as with the F1 help.
    return true;
}

bool CEventPanelWindow::UpdateKeyEvent()
{
    if (IsVisible() && IsPress(VK_ESCAPE) == true)
    {
        g_pNewUISystem->Hide(mu::ui::window::INTERFACE_EVENT_PANEL);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }

    return true;
}

bool CEventPanelWindow::Update()
{
    if (IsVisible())
        EventPanel().Update(); // renews the subscription every minute

    SyncRmlModel();
    return true;
}

bool CEventPanelWindow::Render()
{
    // Nothing native: the panel is an RmlUi document.
    return true;
}

void CEventPanelWindow::BindRmlModel(Rml::DataModelConstructor& c, EventPanelWindowRmlModel& model)
{
    c.Bind("text_px", &model.textPx);
    c.Bind("bold_text_px", &model.boldTextPx);
    c.Bind("line_height", &model.lineHeight);
    c.Bind("bold_line_height", &model.boldLineHeight);
    c.Bind("server_time", &model.serverTime);
    c.Bind("panel_left", &model.panelLeft);
    c.Bind("panel_top", &model.panelTop);
    c.Bind("panel_width", &model.panelWidth);
    c.Bind("gap", &model.gap);

    auto line = c.RegisterStruct<EventPanelLineEntry>();
    line.RegisterMember("left", &EventPanelLineEntry::left);
    line.RegisterMember("right", &EventPanelLineEntry::right);
    line.RegisterMember("color", &EventPanelLineEntry::color);
    line.RegisterMember("right_color", &EventPanelLineEntry::rightColor);
    line.RegisterMember("indent", &EventPanelLineEntry::indent);
    c.RegisterArray<std::vector<EventPanelLineEntry>>();
    c.Bind("lines", &model.lines);
}

void CEventPanelWindow::SyncRmlModel()
{
    m_RmlView.Ensure();
    if (!m_RmlView.Document())
        return;

    const bool visible = IsVisible();
    UI::RmlBridge::SyncDocumentVisibilityInFront(m_RmlView.Document(), visible);
    if (!visible)
        return;

    RebuildModel();
}

void CEventPanelWindow::RebuildModel()
{
    const float textPx = UI::RmlBridge::NativeTextPx(UI::Scaling::FontRole::Normal);
    const long long second = NowSecond();
    if (second == m_LastBuildSecond && textPx == m_BuiltTextPx)
        return;

    // The box: 200 reference units wide at the UI scale, 6 from the window's top-right corner.
    {
        EventPanelWindowRmlModel& place = m_RmlView.GetModel();
        const float scale = UI::Scaling::TypographyScale(static_cast<int>(WindowWidth), static_cast<int>(WindowHeight));
        place.panelWidth = 200.f * scale;
        place.gap = 6.f * scale;
        place.panelLeft = static_cast<float>(WindowWidth) - place.panelWidth - place.gap;
        place.panelTop = place.gap;
    }

    m_LastBuildSecond = second;
    m_BuiltTextPx = textPx;

    EventPanelWindowRmlModel& model = m_RmlView.GetModel();
    model.lines = BuildLines();

    model.serverTime.clear();
    if (EventPanel().HasData())
    {
        const int seconds = EventPanel().GetServerSecondsOfDay();
        model.serverTime = Narrow(L"Hora del servidor " + ClockText(seconds / 3600, (seconds / 60) % 60));
    }

    model.textPx = textPx;
    model.boldTextPx = UI::RmlBridge::NativeTextPx(UI::Scaling::FontRole::Bold);
    model.lineHeight = static_cast<float>(CUIRenderTextSDLTtf::LineHeight(UI::Scaling::FontRole::Normal));
    model.boldLineHeight = static_cast<float>(CUIRenderTextSDLTtf::LineHeight(UI::Scaling::FontRole::Bold));

    for (const char* field : {"lines", "server_time", "text_px", "bold_text_px", "line_height", "bold_line_height",
                              "panel_left", "panel_top", "panel_width", "gap"})
        m_RmlView.MarkDirty(field);
}

float CEventPanelWindow::GetLayerDepth()
{
    return 8.2f;
}

float CEventPanelWindow::GetKeyEventOrder()
{
    return 10.f;
}

void CEventPanelWindow::OpenningProcess()
{
    m_LastBuildSecond = -1;
    EventPanel().Open();
}

void CEventPanelWindow::ClosingProcess()
{
    EventPanel().Close();
}
