// NewUIEventPanelWindow.h: VORTHANIA - the event panel (key H).
//////////////////////////////////////////////////////////////////////

#pragma once

#include "UI/NewUI/Inventory/NewUIMyInventory.h"
#include "UI/NewUI/Dialogs/NewUIMessageBox.h"
#include "UI/NewUI/UILayoutPolicy.h"
#include "UI/NewUI/Widgets/NewUIButton.h"
#include "UI/Scaling/UITransform.h"

#include <string>
#include <vector>

namespace SEASON3B
{
// Shows the next events of the server with a countdown, and while an invasion
// runs, how many of its monsters are left on each map. The data comes from
// GameLogic::Events::EventPanel(); this window only draws it.
//
// It uses the frame of the chat command window (key J), so both look alike.
class CNewUIEventPanelWindow : public CNewUIObj
{
    enum eIMAGE_LIST
    {
        IMAGE_EVENTPANEL_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK,
        IMAGE_EVENTPANEL_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP,
        IMAGE_EVENTPANEL_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_EVENTPANEL_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_EVENTPANEL_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_EVENTPANEL_BTN_EXIT = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN,
    };

    enum eWINDOW_SIZE
    {
        WINDOW_WIDTH = 190,
        FRAME_TOP_HEIGHT = 64,
        FRAME_SIDE_WIDTH = 21,
        FRAME_BOTTOM_HEIGHT = 45,
        FRAME_SIDE_TEXTURE_HEIGHT = 320,
    };

    enum eLAYOUT
    {
        TITLE_Y = 12,
        CONTENT_LEFT = 22,
        CONTENT_WIDTH = WINDOW_WIDTH - 2 * CONTENT_LEFT,
        INDENT = 8,
        CLOCK_Y = 36,
        CONTENT_TOP = 52,
        ROW_HEIGHT = 13,
        CONTENT_BOTTOM = 384,
        VISIBLE_ROWS = (CONTENT_BOTTOM - CONTENT_TOP) / ROW_HEIGHT,
        EXIT_BUTTON_X = 13,
        EXIT_BUTTON_Y = 392,
        EXIT_BUTTON_WIDTH = 36,
        EXIT_BUTTON_HEIGHT = 29,
    };

    enum class eROW_STYLE
    {
        EventName,
        RunningName,
        Detail,
        MapHeader,
        Monster,
        MonsterDone,
    };

    struct Row
    {
        std::wstring Left;
        std::wstring Right;
        eROW_STYLE Style;
        int Indent;
    };

public:
    static constexpr float LayerDepth = UI::Layout::ForegroundPanelLayerDepth;
    static constexpr int WindowHeight = UI::Scaling::DockLogicalBottom;

    CNewUIEventPanelWindow();
    ~CNewUIEventPanelWindow() override;

    bool Create(CNewUIManager* pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent() override;
    bool UpdateKeyEvent() override;
    bool Update() override;
    bool Render() override;

    float GetLayerDepth() override;
    float GetKeyEventOrder() override;

    void OpenningProcess();
    void ClosingProcess();

private:
    // Rebuilt every frame: the countdowns change, and a new message from the
    // server may have arrived. There are only a few dozen rows.
    std::vector<Row> BuildRows() const;

    void RenderBaseWindow();
    void RenderTexts(const std::vector<Row>& rows);

    void LoadImages();
    void UnloadImages();

    CNewUIManager* m_pNewUIMng;
    POINT m_Pos;
    int m_scrollOffset;
    int m_rowCount;

    CNewUIButton m_BtnExit;
};
} // namespace SEASON3B
