// NewUIEventPanelWindow.cpp: VORTHANIA - the event panel (key H).
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "UI/NewUI/Dialogs/NewUIEventPanelWindow.h"

#include "Audio/DSPlaySound.h"
#include "GameLogic/Events/EventPanelState.h"
#include "UI/NewUI/NewUISystem.h"

#include <algorithm>

using namespace SEASON3B;
using GameLogic::Events::EventPanel;
using GameLogic::Events::EventPanelEntry;
using GameLogic::Events::EventPanelEventState;
using GameLogic::Events::EventPanelKind;

namespace
{
struct TextColor
{
    BYTE Red;
    BYTE Green;
    BYTE Blue;
};

constexpr TextColor TitleColor = {255, 220, 120};
constexpr TextColor ClockColor = {170, 170, 170};
constexpr TextColor EventColor = {220, 220, 220};
constexpr TextColor RunningColor = {120, 255, 120};
constexpr TextColor DetailColor = {170, 190, 210};
constexpr TextColor MapColor = {255, 200, 120};
constexpr TextColor MonsterColor = {230, 230, 230};
constexpr TextColor MonsterDoneColor = {120, 120, 120};

void UseTextColor(const TextColor& color)
{
    g_pRenderText->SetTextColor(color.Red, color.Green, color.Blue, 255);
    g_pRenderText->SetBgColor(0);
}

void RenderLine(int x, int y, const std::wstring& text, int boxWidth, int sort = RT3_SORT_LEFT)
{
    // An empty text leaves a stray glyph behind when a box width is given.
    if (text.empty())
    {
        return;
    }

    g_pRenderText->RenderText(x, y, text.c_str(), boxWidth, 0, sort);
}

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
    {
        return std::to_wstring(seconds) + L" s";
    }

    const auto minutes = seconds / 60;
    if (minutes < 60)
    {
        return std::to_wstring(minutes) + L" min";
    }

    return std::to_wstring(minutes / 60) + L" h " + std::to_wstring(minutes % 60) + L" min";
}

// "12:34" for a running countdown, so that it visibly moves.
std::wstring CountdownText(int64_t seconds)
{
    const auto minutes = seconds / 60;
    if (minutes >= 60)
    {
        return DurationText(seconds);
    }

    return std::to_wstring(minutes) + L":" + TwoDigits(static_cast<int>(seconds % 60));
}
} // namespace

CNewUIEventPanelWindow::CNewUIEventPanelWindow()
{
    m_pNewUIMng = nullptr;
    m_Pos.x = 0;
    m_Pos.y = 0;
    m_scrollOffset = 0;
    m_rowCount = 0;
}

CNewUIEventPanelWindow::~CNewUIEventPanelWindow()
{
    Release();
}

bool CNewUIEventPanelWindow::Create(CNewUIManager* pNewUIMng, int x, int y)
{
    if (pNewUIMng == nullptr)
    {
        return false;
    }

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_EVENT_PANEL, this);

    LoadImages();
    SetPos(x, y);

    m_BtnExit.ChangeButtonImgState(true, IMAGE_EVENTPANEL_BTN_EXIT);
    m_BtnExit.ChangeToolTipText(L"Cerrar (H)", true);

    Show(false);
    return true;
}

void CNewUIEventPanelWindow::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

void CNewUIEventPanelWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
    m_BtnExit.ChangeButtonInfo(m_Pos.x + EXIT_BUTTON_X, m_Pos.y + EXIT_BUTTON_Y, EXIT_BUTTON_WIDTH, EXIT_BUTTON_HEIGHT);
}

float CNewUIEventPanelWindow::GetLayerDepth()
{
    return LayerDepth;
}

float CNewUIEventPanelWindow::GetKeyEventOrder()
{
    return 10.f;
}

void CNewUIEventPanelWindow::OpenningProcess()
{
    m_scrollOffset = 0;
    EventPanel().Open();
}

void CNewUIEventPanelWindow::ClosingProcess()
{
    EventPanel().Close();
}

std::vector<CNewUIEventPanelWindow::Row> CNewUIEventPanelWindow::BuildRows() const
{
    std::vector<Row> rows;
    const auto& panel = EventPanel();
    if (!panel.HasData())
    {
        rows.push_back({L"Cargando...", L"", eROW_STYLE::Detail, 0});
        return rows;
    }

    if (panel.GetEntries().empty())
    {
        rows.push_back({L"No hay eventos programados.", L"", eROW_STYLE::Detail, 0});
        return rows;
    }

    for (const auto& entry : panel.GetEntries())
    {
        const auto remaining = panel.GetRemainingSeconds(entry);
        switch (entry.State)
        {
        case EventPanelEventState::Running:
        {
            // The right column always holds a clock time (here: when it started),
            // so that a countdown like "12:44" isn't mistaken for one.
            rows.push_back({entry.Name, ClockText(entry.StartHour, entry.StartMinute), eROW_STYLE::RunningName, 0});
            const auto* label = entry.Kind == EventPanelKind::MiniGame ? L"Entrada abierta" : L"En curso";
            std::wstring detail = label;
            detail += L", quedan " + CountdownText(remaining);
            if (!entry.Monsters.empty())
            {
                int alive = 0;
                int total = 0;
                for (const auto& group : entry.Monsters)
                {
                    alive += group.Alive;
                    total += group.Total;
                }

                // On its own row: next to the countdown it doesn't fit the width.
                rows.push_back({detail, L"", eROW_STYLE::Detail, INDENT});
                detail = L"Vivos";
                rows.push_back({detail, std::to_wstring(alive) + L"/" + std::to_wstring(total), eROW_STYLE::Detail, INDENT});
            }
            else
            {
                rows.push_back({detail, L"", eROW_STYLE::Detail, INDENT});
            }

            // The groups arrive sorted by map, so a map header is written
            // whenever the map changes.
            const std::wstring* lastMap = nullptr;
            for (const auto& group : entry.Monsters)
            {
                if (lastMap == nullptr || *lastMap != group.MapName)
                {
                    rows.push_back({group.MapName, L"", eROW_STYLE::MapHeader, INDENT});
                    lastMap = &group.MapName;
                }

                rows.push_back({group.MonsterName,
                                std::to_wstring(group.Alive) + L"/" + std::to_wstring(group.Total),
                                group.Alive > 0 ? eROW_STYLE::Monster : eROW_STYLE::MonsterDone, 2 * INDENT});
            }

            break;
        }

        case EventPanelEventState::Starting:
            rows.push_back({entry.Name, L"ya", eROW_STYLE::RunningName, 0});
            rows.push_back({L"Empieza en segundos", L"", eROW_STYLE::Detail, INDENT});
            break;

        case EventPanelEventState::Scheduled:
            rows.push_back({entry.Name, ClockText(entry.StartHour, entry.StartMinute), eROW_STYLE::EventName, 0});
            rows.push_back({L"en " + DurationText(remaining), L"", eROW_STYLE::Detail, INDENT});
            break;

        default:
            rows.push_back({entry.Name, L"--:--", eROW_STYLE::EventName, 0});
            rows.push_back({L"Sin horario", L"", eROW_STYLE::Detail, INDENT});
            break;
        }
    }

    return rows;
}

bool CNewUIEventPanelWindow::UpdateMouseEvent()
{
    if (g_pNewUISystem->HandleFrameCornerClose(m_Pos, SEASON3B::INTERFACE_EVENT_PANEL))
    {
        PlayBuffer(SOUND_CLICK01);
        return false;
    }

    if (m_BtnExit.UpdateMouseEvent())
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_EVENT_PANEL);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }

    if (!CheckMouseIn(m_Pos.x, m_Pos.y, WINDOW_WIDTH, WindowHeight))
    {
        return true;
    }

    const auto hiddenRows = m_rowCount - VISIBLE_ROWS;
    if (MouseWheel != 0 && hiddenRows > 0)
    {
        m_scrollOffset = std::max(0, std::min(m_scrollOffset - MouseWheel, hiddenRows));
        MouseWheel = 0;
    }

    // The window takes the clicks inside it, so they don't move the hero.
    return false;
}

bool CNewUIEventPanelWindow::UpdateKeyEvent()
{
    if (!g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_EVENT_PANEL))
    {
        return true;
    }

    if (IsPress(VK_ESCAPE))
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_EVENT_PANEL);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }

    const auto hiddenRows = m_rowCount - VISIBLE_ROWS;
    if (IsPress(VK_DOWN) && m_scrollOffset < hiddenRows)
    {
        ++m_scrollOffset;
        return false;
    }

    if (IsPress(VK_UP) && m_scrollOffset > 0)
    {
        --m_scrollOffset;
        return false;
    }

    return true;
}

bool CNewUIEventPanelWindow::Update()
{
    EventPanel().Update();
    return true;
}

bool CNewUIEventPanelWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderBaseWindow();

    const auto rows = BuildRows();
    m_rowCount = static_cast<int>(rows.size());
    m_scrollOffset = std::max(0, std::min(m_scrollOffset, m_rowCount - VISIBLE_ROWS));
    RenderTexts(rows);

    m_BtnExit.Render();
    DisableAlphaBlend();
    return true;
}

void CNewUIEventPanelWindow::RenderBaseWindow()
{
    const auto x = static_cast<float>(m_Pos.x);
    const auto y = static_cast<float>(m_Pos.y);
    const auto middleHeight = static_cast<float>(WindowHeight - FRAME_TOP_HEIGHT - FRAME_BOTTOM_HEIGHT);

    RenderImage(IMAGE_EVENTPANEL_BACK, x, y, float(WINDOW_WIDTH), float(WindowHeight));
    RenderImage(IMAGE_EVENTPANEL_TOP, x, y, float(WINDOW_WIDTH), float(FRAME_TOP_HEIGHT));
    RenderImageStretch(IMAGE_EVENTPANEL_LEFT, x, y + float(FRAME_TOP_HEIGHT), float(FRAME_SIDE_WIDTH), middleHeight,
                       0.f, 0.f, float(FRAME_SIDE_WIDTH), float(FRAME_SIDE_TEXTURE_HEIGHT));
    RenderImageStretch(IMAGE_EVENTPANEL_RIGHT, x + float(WINDOW_WIDTH - FRAME_SIDE_WIDTH), y + float(FRAME_TOP_HEIGHT),
                       float(FRAME_SIDE_WIDTH), middleHeight, 0.f, 0.f, float(FRAME_SIDE_WIDTH),
                       float(FRAME_SIDE_TEXTURE_HEIGHT));
    RenderImage(IMAGE_EVENTPANEL_BOTTOM, x, y + float(WindowHeight - FRAME_BOTTOM_HEIGHT), float(WINDOW_WIDTH),
                float(FRAME_BOTTOM_HEIGHT));
}

void CNewUIEventPanelWindow::RenderTexts(const std::vector<Row>& rows)
{
    g_pRenderText->SetFont(g_hFontBold);
    UseTextColor(TitleColor);
    RenderLine(m_Pos.x, m_Pos.y + TITLE_Y, L"Eventos", WINDOW_WIDTH, RT3_SORT_CENTER);

    g_pRenderText->SetFont(g_hFont);
    if (EventPanel().HasData())
    {
        const auto seconds = EventPanel().GetServerSecondsOfDay();
        UseTextColor(ClockColor);
        RenderLine(m_Pos.x, m_Pos.y + CLOCK_Y,
                   L"Hora del servidor " + ClockText(seconds / 3600, (seconds / 60) % 60), WINDOW_WIDTH,
                   RT3_SORT_CENTER);
    }

    const auto last = std::min(static_cast<int>(rows.size()), m_scrollOffset + VISIBLE_ROWS);
    for (int i = m_scrollOffset; i < last; ++i)
    {
        const auto& row = rows[i];
        const auto y = m_Pos.y + CONTENT_TOP + (i - m_scrollOffset) * ROW_HEIGHT;
        const auto x = m_Pos.x + CONTENT_LEFT + row.Indent;
        const auto width = CONTENT_WIDTH - row.Indent;

        switch (row.Style)
        {
        case eROW_STYLE::EventName:
            g_pRenderText->SetFont(g_hFontBold);
            UseTextColor(EventColor);
            break;
        case eROW_STYLE::RunningName:
            g_pRenderText->SetFont(g_hFontBold);
            UseTextColor(RunningColor);
            break;
        case eROW_STYLE::Detail:
            g_pRenderText->SetFont(g_hFont);
            UseTextColor(DetailColor);
            break;
        case eROW_STYLE::MapHeader:
            g_pRenderText->SetFont(g_hFont);
            UseTextColor(MapColor);
            break;
        case eROW_STYLE::Monster:
            g_pRenderText->SetFont(g_hFont);
            UseTextColor(MonsterColor);
            break;
        case eROW_STYLE::MonsterDone:
            g_pRenderText->SetFont(g_hFont);
            UseTextColor(MonsterDoneColor);
            break;
        }

        RenderLine(x, y, row.Left, width);
        RenderLine(x, y, row.Right, width, RT3_SORT_RIGHT);
    }

    g_pRenderText->SetFont(g_hFont);
}

void CNewUIEventPanelWindow::LoadImages()
{
    LoadBitmap(L"Interface/newui_msgbox_back.jpg", IMAGE_EVENTPANEL_BACK, GL_LINEAR);
    LoadBitmap(L"Interface/newui_item_back01.tga", IMAGE_EVENTPANEL_TOP, GL_LINEAR);
    LoadBitmap(L"Interface/newui_item_back02-L.tga", IMAGE_EVENTPANEL_LEFT, GL_LINEAR);
    LoadBitmap(L"Interface/newui_item_back02-R.tga", IMAGE_EVENTPANEL_RIGHT, GL_LINEAR);
    LoadBitmap(L"Interface/newui_item_back03.tga", IMAGE_EVENTPANEL_BOTTOM, GL_LINEAR);
    LoadBitmap(L"Interface/newui_exit_00.tga", IMAGE_EVENTPANEL_BTN_EXIT, GL_LINEAR);
}

void CNewUIEventPanelWindow::UnloadImages()
{
    // The ids are shared with the inventory, the message boxes and the chat
    // command window, which delete them when they are released. Deleting them
    // here too would blank those windows while they are still in use.
}
