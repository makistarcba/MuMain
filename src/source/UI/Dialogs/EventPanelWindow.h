#pragma once

// VORTHANIA: the event panel (key H). The server sends the events in C2 F5 49
// (GameLogic/Events/EventPanelState.h); this window shows them in RmlUi
// (event_panel.rml). Opening subscribes with "/eventos abrir", closing with "/eventos cerrar".

#include "UI/Core/WindowManager.h"
#include "UI/Dialogs/EventPanelWindowRmlModel.h"
#include "UI/RmlBridge/RmlThemedView.h"

namespace mu::ui::window
{
class CEventPanelWindow : public CObject
{
public:
    CEventPanelWindow();
    virtual ~CEventPanelWindow();

    bool Create(CManager* pNewUIMng);
    void Release();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    float GetLayerDepth();
    float GetKeyEventOrder();

    void OpenningProcess();
    void ClosingProcess();

private:
    void SyncRmlModel();
    void RebuildModel();
    void BindRmlModel(Rml::DataModelConstructor& c, EventPanelWindowRmlModel& model);

    CManager* m_pNewUIMng = nullptr;

    UI::RmlBridge::ThemedView<EventPanelWindowRmlModel> m_RmlView{"event_panel",
        [this](Rml::DataModelConstructor& c, EventPanelWindowRmlModel& model) { BindRmlModel(c, model); },
        {{"Data/Interface/RmlUi/event_panel.rml"}}, {.afterReload = [this] { m_LastBuildSecond = -1; }}};

    // The rows are rebuilt once a second (the countdowns) or when the text size changed.
    long long m_LastBuildSecond = -1;
    float m_BuiltTextPx = 0.f;
};
} // namespace mu::ui::window
