
#include "stdafx.h"
#include "Core/Content/ContentStore.h"
#include "Data/DataHandler/DataFileIO.h"
#include "UI/Placement/WindowPlacement.h"
#include "I18N/All.h"

#include "UI/HUD/MiniMap.h"
#include "UI/Core/WindowSystem.h"
#include "UI/Dialogs/CommonMessageBox.h"
#include "UI/Dialogs/CustomMessageBox.h"
#include "Audio/DSPlaySound.h"

#include "Guild/GuildInfoWindow.h"
#include "UI/Inventory/MyInventory.h"
#include "GameLogic/Items/CSItemOption.h"
#include "World/MapInfra/MapManager.h"

#include "Core/Utilities/StringUtils.h"
#include "Render/RmlUi/RmlUiRuntime.h"
#include "UI/HUD/MiniMapLayout.h"
#include "UI/RmlBridge/RmlDocumentVisibility.h"
#include "UI/RmlBridge/RmlNativeTextSize.h"
#include "UI/RmlBridge/RmlTheme.h"
#include "Render/Text/CUIRenderText.h"

#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

// VORTHANIA: to walk from the map.
#include "Engine/AI/ZzzAI.h"            // PathFinding2
#include "Engine/Object/ZzzInterface.h" // SendMove
#include "Render/Terrain/ZzzLodTerrain.h" // TerrainWall

extern BYTE m_OccupationState;

using namespace SEASON3B;
using namespace mu::ui::window;

namespace
{
// The part of the screen the original's mouse handler kept to itself.
constexpr int kMapAreaHeight = 430;
// Marker sizes, physical px (the original never scaled them).
constexpr float kNpcSize = 15.f;
constexpr float kPortalSize = 30.f;
// The side border tiles: 35 x 6 reference units, 20 down each side, turned a quarter.
constexpr float kTileWidth = 35.f;
constexpr float kTileHeight = 6.f;
constexpr int kSideTiles = 20;
// .edge-side's element size in mini_map.rcss (the tile's texel rectangle).
constexpr float kSideElementWidth = 41.7f;
constexpr float kSideElementHeight = 8.f;

template <typename Model, typename T>
void Sync(RmlModelBinder<Model>& binder, T Model::* field, const char* name, T value)
{
    Model& model = binder.GetModel();
    if (model.*field == value)
        return;
    model.*field = std::move(value);
    binder.MarkDirty(name);
}

Rml::String MatrixText(const UI::MiniMap::CssMatrix& m)
{
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer), "matrix(%.5f, %.5f, %.5f, %.5f, %.3f, %.3f)", m.a, m.b, m.c, m.d, m.e, m.f);
    return buffer;
}

// The original drew the map under the screen overlay's W/640 x H/480 stretch.
UI::Scaling::Transform OverlayTransform()
{
    return UI::Scaling::ScreenOverlayTransform(static_cast<int>(WindowWidth), static_cast<int>(WindowHeight));
}

UI::MiniMap::Screen CurrentScreen()
{
    const UI::Scaling::Transform transform = OverlayTransform();
    return {static_cast<float>(WindowWidth),
            static_cast<float>(WindowHeight),
            transform.scaleX,
            transform.scaleY,
            transform.offsetX,
            transform.offsetY};
}

// The marker the original drew: Kind 1 an NPC (hidden in Crywolf while it is occupied, but the
// one at 228/48), Kind 2 a gate.
bool MarkerDrawn(const MINI_MAP& data)
{
    if (data.Kind == 2)
        return true;
    return !(gMapManager.WorldActive == WD_34CRYWOLF_1ST && m_OccupationState > 0) ||
           (data.Location[0] == 228 && data.Location[1] == 48 && gMapManager.WorldActive == WD_34CRYWOLF_1ST);
}

// ---------------------------------------------------------------------------------------------
// VORTHANIA: click on the map to walk there, the route drawn on the map and the spots
// (Data\Local\VorthaniaSpots.txt, written by the same script that creates the spawns in the
// database). Ported from the old client's NewUIMiniMap.cpp; the walking logic is the same.
// ---------------------------------------------------------------------------------------------
bool g_VorthAutoMove = false;
int g_VorthAutoMoveX = 0;
int g_VorthAutoMoveY = 0;
int g_VorthAutoMoveTries = 0;
// Where the stretch we asked for ends: if the hero stops anywhere else, the player took over.
int g_VorthOurPathEnd[2] = {-1, -1};

void VorthRememberOurPath()
{
    const int n = static_cast<int>(Hero->Path.PathNum);
    if (n > 0 && n <= MAX_PATH_FIND)
    {
        g_VorthOurPathEnd[0] = static_cast<int>(Hero->Path.PathX[n - 1]);
        g_VorthOurPathEnd[1] = static_cast<int>(Hero->Path.PathY[n - 1]);
    }
    else
    {
        g_VorthOurPathEnd[0] = -1;
        g_VorthOurPathEnd[1] = -1;
    }
}

bool VorthWalkable(int mapX, int mapY)
{
    if (mapX < 0 || mapX > 255 || mapY < 0 || mapY > 255)
        return false;
    const WORD att = TerrainWall[(mapY * 256) + mapX];
    if ((att & TW_NOMOVE) == TW_NOMOVE)
        return false;
    if ((att & TW_NOGROUND) == TW_NOGROUND)
        return false;
    return true;
}

// A map tile (PositionX, PositionY) as a window pixel: the centre of a zero-sized marker, so it
// follows exactly the same turn as the NPC icons.
UI::MiniMap::Point VorthMapToWindow(const UI::MiniMap::Screen& screen, float heroX, float heroY, float tileX,
                                    float tileY)
{
    const float length = UI::MiniMap::MapLength;
    const float pointY = (tileX / 256.f) * length;
    const float pointX = (tileY / 256.f) * length;
    return UI::MiniMap::MarkerQuad(screen, heroX, heroY, pointX, pointY, 0.f, 0.f, length,
                                   UI::MiniMap::MapRotation, false)
        .quad.topLeft;
}

// The inverse: three known tiles give the 2x2 matrix to the window, which is inverted (no hand
// inverted rotation, where a sign is easy to get wrong).
bool VorthWindowToMap(const UI::MiniMap::Screen& screen, float heroX, float heroY, float wx, float wy, int& outX,
                      int& outY)
{
    const UI::MiniMap::Point o = VorthMapToWindow(screen, heroX, heroY, 0.f, 0.f);
    const UI::MiniMap::Point a = VorthMapToWindow(screen, heroX, heroY, 256.f, 0.f);
    const UI::MiniMap::Point b = VorthMapToWindow(screen, heroX, heroY, 0.f, 256.f);
    const float m00 = a.x - o.x;
    const float m01 = b.x - o.x;
    const float m10 = a.y - o.y;
    const float m11 = b.y - o.y;
    const float det = (m00 * m11) - (m01 * m10);
    if (std::fabs(det) < 0.00001f)
        return false;
    const float rx = wx - o.x;
    const float ry = wy - o.y;
    const float u = ((m11 * rx) - (m01 * ry)) / det;
    const float v = ((m00 * ry) - (m10 * rx)) / det;
    const int mapX = static_cast<int>((u * 256.f) + 0.5f);
    const int mapY = static_cast<int>((v * 256.f) + 0.5f);
    if (mapX < 0 || mapX > 255 || mapY < 0 || mapY > 255)
        return false;
    outX = mapX;
    outY = mapY;
    return true;
}

constexpr const char* VORTH_COL_ROUTE = "#FFC832";       // the stretch the path finder computed
constexpr const char* VORTH_COL_REST = "#8A6A20";        // from there to the destination
constexpr const char* VORTH_COL_DESTINATION = "#FFFFFF"; // the destination cross
constexpr const char* VORTH_COL_HOVER = "#6A7A8A";       // where a click would go
constexpr const char* VORTH_COL_SPOT = "#64D2FF";        // a spot
constexpr const char* VORTH_COL_SPOT_7 = "#FFA000";      // a spot of 7, the strongest of the map

void VorthLine(std::vector<MiniMapLineEntry>& out, UI::MiniMap::Point p, UI::MiniMap::Point q, float thickness,
               const char* color)
{
    const float dx = q.x - p.x;
    const float dy = q.y - p.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.5f)
        return;
    const float nx = (-dy / length) * thickness * 0.5f;
    const float ny = (dx / length) * thickness * 0.5f;
    UI::MiniMap::Quad quad;
    quad.topLeft = {p.x + nx, p.y + ny};
    quad.topRight = {q.x + nx, q.y + ny};
    quad.bottomLeft = {p.x - nx, p.y - ny};
    out.push_back({MatrixText(UI::MiniMap::ElementToQuad(quad, 1.f, 1.f)), color});
}

void VorthCross(std::vector<MiniMapLineEntry>& out, UI::MiniMap::Point c, float r, float thickness, const char* color)
{
    VorthLine(out, {c.x - r, c.y - r}, {c.x + r, c.y + r}, thickness, color);
    VorthLine(out, {c.x - r, c.y + r}, {c.x + r, c.y - r}, thickness, color);
}

void VorthDiamond(std::vector<MiniMapLineEntry>& out, UI::MiniMap::Point c, float r, float thickness, const char* color)
{
    VorthLine(out, {c.x, c.y - r}, {c.x + r, c.y}, thickness, color);
    VorthLine(out, {c.x + r, c.y}, {c.x, c.y + r}, thickness, color);
    VorthLine(out, {c.x, c.y + r}, {c.x - r, c.y}, thickness, color);
    VorthLine(out, {c.x - r, c.y}, {c.x, c.y - r}, thickness, color);
}

// Format, one spot per line: map x y count monster name. Lines starting with # are comments.
struct VorthSpot
{
    int map = 0;
    int x = 0;
    int y = 0;
    int count = 0;
    std::wstring name;
};

std::vector<VorthSpot> g_VorthSpots;
bool g_VorthSpotsRead = false;

void VorthLoadSpots()
{
    if (g_VorthSpotsRead)
        return;
    // Tried once: without the file the map works the same, without spots.
    g_VorthSpotsRead = true;

    const std::optional<std::string> text = Content::ReadFile(std::filesystem::path(L"Data\\Local\\VorthaniaSpots.txt"));
    if (!text)
        return;

    std::istringstream lines(*text);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.empty() || line[0] == '#' || line[0] == '\r')
            continue;
        int map = 0, x = 0, y = 0, count = 0;
        char name[96] = {0};
        if (std::sscanf(line.c_str(), "%d %d %d %d %95[^\r\n]", &map, &x, &y, &count, name) < 4)
            continue;
        VorthSpot spot;
        spot.map = map;
        spot.x = x;
        spot.y = y;
        spot.count = count;
        for (const char* c = name; *c != 0; ++c)
            spot.name.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*c)));
        g_VorthSpots.push_back(std::move(spot));
    }
}
} // namespace

mu::ui::window::CMiniMap::CMiniMap()
{
    m_pNewUIMng = NULL;
    m_bSuccess = false;
    for (auto& data : m_Mini_Map_Data)
        data.Kind = 0;
    for (auto& box : m_Btn_Loc)
        box[0] = box[1] = box[2] = box[3] = 0.f;
}

mu::ui::window::CMiniMap::~CMiniMap()
{
    Release();
}

bool mu::ui::window::CMiniMap::Create(CManager* pNewUIMng)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(mu::ui::window::INTERFACE_MINI_MAP, this);
    m_bSuccess = false;

    BuildRmlUi();
    return true;
}

void mu::ui::window::CMiniMap::ClosingProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

float mu::ui::window::CMiniMap::GetLayerDepth()
{
    return 8.1f;
}

void mu::ui::window::CMiniMap::OpenningProcess()
{
    m_PendingClose = false;
}

void mu::ui::window::CMiniMap::Release()
{
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }

    m_RmlView.Release();
}

void mu::ui::window::CMiniMap::SetBtnPos(int Num, float x, float y, float nx, float ny)
{
    m_Btn_Loc[Num][0] = x;
    m_Btn_Loc[Num][1] = y;
    m_Btn_Loc[Num][2] = nx;
    m_Btn_Loc[Num][3] = ny;
}

bool mu::ui::window::CMiniMap::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(mu::ui::window::INTERFACE_MINI_MAP))
    {
        if (IsPress(VK_ESCAPE) == true || IsPress(VK_TAB) == true)
        {
            g_pNewUISystem->Hide(mu::ui::window::INTERFACE_MINI_MAP);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool mu::ui::window::CMiniMap::Render()
{
    // Nothing native left: the map, its markers, the frame and the hint are RmlUi. Kept because
    // CObject requires the override.
    return m_bSuccess;
}

bool mu::ui::window::CMiniMap::Update()
{
    SyncRmlModel();

    // VORTHANIA
    if (m_PendingWalk)
    {
        m_PendingWalk = false;
        if (IsVisible())
            WalkToWindowPoint(m_PendingWalkX, m_PendingWalkY);
    }
    UpdateAutoMove();

    if (m_PendingClose)
    {
        m_PendingClose = false;
        if (IsVisible())
            g_pNewUISystem->Hide(mu::ui::window::INTERFACE_MINI_MAP);
    }
    return true;
}

void mu::ui::window::CMiniMap::LoadImages(const wchar_t* Filename)
{
    wchar_t Fname[300];
    int i = 0;
    mu_swprintf(Fname, L"Data\\%ls\\mini_map.ozt", Filename);
    if (Content::Probe(Fname) != Content::Found::File)
    {
        m_bSuccess = false;
        m_WorldName.clear();
        return;
    }
    else
    {
        m_bSuccess = true;
        // The texture itself is loaded by the document (mini_map.rml's .map).
        m_WorldName = Filename;
    }

    mu_swprintf(Fname, L"Data\\Local\\%ls\\Minimap\\Minimap_%ls_%ls.bmd", g_strSelectedML.c_str(), Filename, g_strSelectedML.c_str());

    for (i = 0; i < MAX_MINI_MAP_DATA; i++)
    {
        m_Mini_Map_Data[i].Kind = 0;
    }

    const Content::Stream fp = Content::OpenIO(Fname);

    if (fp != nullptr)
    {
        int Size = sizeof(MINI_MAP_FILE);
        BYTE* Buffer = new BYTE[Size * MAX_MINI_MAP_DATA + 45];
        DWORD dwCheckSum;
        const bool complete = DataFileIO::ReadExact(fp.get(), Buffer, (Size * MAX_MINI_MAP_DATA) + 45) &&
                              DataFileIO::ReadExact(fp.get(), &dwCheckSum, sizeof(DWORD));

        if (!complete || dwCheckSum != GenerateCheckSum2(Buffer, (Size * MAX_MINI_MAP_DATA) + 45, 0x2BC1))
        {
            DataFileIO::ReportUnusableFile(Fname, L"File corrupted.");
        }
        else
        {
            BYTE* pSeek = Buffer;

            for (i = 0; i < MAX_MINI_MAP_DATA; i++)
            {
                BuxConvert(pSeek, Size);

                MINI_MAP_FILE current{ };
                auto target = &(m_Mini_Map_Data[i]);
                memcpy(&current, pSeek, Size);
                memcpy(target, pSeek, Size);

                CMultiLanguage::ConvertFromUtf8(target->Name, current.Name);
                pSeek += Size;
            }
        }

        delete[] Buffer;
    }
}

void mu::ui::window::CMiniMap::UnloadImages()
{
    m_WorldName.clear();
}

bool mu::ui::window::CMiniMap::UpdateMouseEvent()
{
    // The close button's click is RmlUi's (minimap_close); like the original, the pointer over the
    // top 430 rows goes to nothing behind the map.
    const UI::Scaling::Transform overlay = OverlayTransform();
    const float pointerY = UI::Scaling::LogicalY(overlay, g_fWindowMouseY);
    return !(pointerY >= 0.f && pointerY < static_cast<float>(kMapAreaHeight));
}

void mu::ui::window::CMiniMap::BindRmlModel(Rml::DataModelConstructor& c, MiniMapRmlModel& model)
{
    c.Bind("text_px", &model.textPx);
    c.Bind("close_hint", &model.closeHint);

    auto clip = c.RegisterStruct<MiniMapClipEntry>();
    clip.RegisterMember("left", &MiniMapClipEntry::left);
    clip.RegisterMember("top", &MiniMapClipEntry::top);
    clip.RegisterMember("width", &MiniMapClipEntry::width);
    clip.RegisterMember("height", &MiniMapClipEntry::height);
    clip.RegisterMember("world_left", &MiniMapClipEntry::worldLeft);
    clip.RegisterMember("world_top", &MiniMapClipEntry::worldTop);
    c.RegisterArray<std::vector<MiniMapClipEntry>>();
    c.Bind("clips", &model.clips);

    c.Bind("map_source", &model.mapSource);
    c.Bind("map_transform", &model.mapTransform);

    auto marker = c.RegisterStruct<MiniMapMarkerEntry>();
    marker.RegisterMember("portal", &MiniMapMarkerEntry::portal);
    marker.RegisterMember("size", &MiniMapMarkerEntry::size);
    marker.RegisterMember("transform", &MiniMapMarkerEntry::transform);
    c.RegisterArray<std::vector<MiniMapMarkerEntry>>();
    c.Bind("markers", &model.markers);

    c.RegisterArray<std::vector<Rml::String>>();
    c.Bind("side_lines", &model.sideLines);

    // VORTHANIA
    auto vline = c.RegisterStruct<MiniMapLineEntry>();
    vline.RegisterMember("transform", &MiniMapLineEntry::transform);
    vline.RegisterMember("color", &MiniMapLineEntry::color);
    c.RegisterArray<std::vector<MiniMapLineEntry>>();
    c.Bind("vorth_lines", &model.vorthLines);
    c.BindEventCallback("minimap_walk",
                        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&)
                        {
                            m_PendingWalk = true;
                            m_PendingWalkX = event.GetParameter<float>("mouse_x", 0.f);
                            m_PendingWalkY = event.GetParameter<float>("mouse_y", 0.f);
                        });
    c.RegisterArray<std::vector<float>>();

    c.Bind("hint_visible", &model.hintVisible);
    c.Bind("hint_text", &model.hintText);
    c.Bind("hint_left", &model.hintLeft);
    c.Bind("hint_top", &model.hintTop);
    c.Bind("hint_width", &model.hintWidth);
    c.Bind("hint_height", &model.hintHeight);

    c.BindEventCallback("minimap_close", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&)
                        { m_PendingClose = true; });
}

void mu::ui::window::CMiniMap::BuildRmlUi()
{
    m_RmlView.Ensure();
}

void mu::ui::window::CMiniMap::SyncRmlModel()
{
    BuildRmlUi();
    if (!m_RmlView.Document())
        return;

    const bool visible = IsVisible() && m_bSuccess && Hero != nullptr;
    // Over the location bar, the logs and the buff strip, under the bottom HUD: the stacking table
    // (UI/RmlBridge/RmlStackingOrder.cpp) orders it as the original's layer depths did.
    UI::RmlBridge::SyncDocumentVisibilityInFront(m_RmlView.Document(), visible);

    if (!visible)
        return;

    SyncScreen();
    SyncClips();
    SyncMap();
    SyncHint();
}

void mu::ui::window::CMiniMap::SyncScreen()
{
    UI::RmlBridge::SyncNativeTextSize(m_RmlView.Binder());
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::closeHint, "close_hint", StringUtils::WideToNarrow(I18N::Game::Close388));

    // The border tiles depend on the screen alone: rebuilt when it changes, or when a theme reload
    // left the model empty.
    const UI::MiniMap::Screen screen = CurrentScreen();
    if (screen == m_SideLinesScreen && !m_RmlView.GetModel().sideLines.empty())
        return;
    m_SideLinesScreen = screen;
    std::vector<Rml::String> sideLines;
    sideLines.reserve(kSideTiles * 2);
    for (int i = 0; i < kSideTiles; ++i)
    {
        const float y = static_cast<float>(i) * (kTileWidth - 3.f);
        sideLines.push_back(MatrixText(UI::MiniMap::ElementToQuad(
            UI::MiniMap::RotatedQuad(screen, kTileHeight / 2.f, y, kTileWidth, kTileHeight, -90.f), kSideElementWidth,
            kSideElementHeight)));
        sideLines.push_back(MatrixText(UI::MiniMap::ElementToQuad(
            UI::MiniMap::RotatedQuad(screen, REFERENCE_WIDTH - kTileHeight / 2.f, y, kTileWidth, kTileHeight, 90.f),
            kSideElementWidth, kSideElementHeight)));
    }
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::sideLines, "side_lines", std::move(sideLines));
}

void mu::ui::window::CMiniMap::SyncClips()
{
    // The original drew the bottom HUD strip over the map, so the map paints only outside it:
    // above the strip, and beside it down to the screen's bottom edge.
    const float width = static_cast<float>(WindowWidth);
    const float height = static_cast<float>(WindowHeight);
    UI::Placement::PlacementParticipant::Box strip;
    const bool hasStrip = UI::Placement::SlotBox("main_hud", strip);
    const float bandTop = hasStrip ? std::clamp(strip.top, 0.f, height) : height;
    const float bandBottom = hasStrip ? std::clamp(strip.top + strip.height, bandTop, height) : height;
    const float bandLeft = std::clamp(strip.left, 0.f, width);
    const float bandRight = std::clamp(strip.left + strip.width, bandLeft, width);

    std::vector<MiniMapClipEntry> clips;
    clips.push_back({0.f, 0.f, width, bandTop, 0.f, 0.f});
    if (bandLeft > 0.f && bandBottom > bandTop)
        clips.push_back({0.f, bandTop, bandLeft, bandBottom - bandTop, 0.f, -bandTop});
    if (bandRight < width && bandBottom > bandTop)
        clips.push_back({bandRight, bandTop, width - bandRight, bandBottom - bandTop, -bandRight, -bandTop});
    if (bandBottom < height)
        clips.push_back({0.f, bandBottom, width, height - bandBottom, 0.f, -bandBottom});

    MiniMapRmlModel& model = m_RmlView.GetModel();
    const bool same =
        model.clips.size() == clips.size() &&
        std::equal(clips.begin(), clips.end(), model.clips.begin(),
                   [](const MiniMapClipEntry& a, const MiniMapClipEntry& b)
                   { return a.left == b.left && a.top == b.top && a.width == b.width && a.height == b.height; });
    if (same)
        return;
    model.clips = std::move(clips);
    m_RmlView.MarkDirty("clips");
}

void mu::ui::window::CMiniMap::SyncMap()
{
    const UI::MiniMap::Screen screen = CurrentScreen();
    const float length = UI::MiniMap::MapLength;
    const float heroY = (static_cast<float>(Hero->PositionX) / 256.f) * length;
    const float heroX = (static_cast<float>(Hero->PositionY) / 256.f) * length;

    // Relative to the document: themes/<theme>/mini_map.rml -> Data/<World>/mini_map.tga.
    Rml::String source;
    if (!m_WorldName.empty())
        source = "../../../../" + StringUtils::WideToNarrow(m_WorldName.c_str()) + "/mini_map.tga";
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::mapSource, "map_source", std::move(source));

    Sync(m_RmlView.Binder(), &MiniMapRmlModel::mapTransform, "map_transform",
         MatrixText(UI::MiniMap::ElementToQuad(
             UI::MiniMap::MapQuad(screen, heroX, heroY, length, UI::MiniMap::MapRotation), length, length)));

    std::vector<MiniMapMarkerEntry> markers;
    for (int i = 0; i < MAX_MINI_MAP_DATA; i++)
    {
        const MINI_MAP& data = m_Mini_Map_Data[i];
        if (data.Kind <= 0)
            break;
        if (data.Kind != 1 && data.Kind != 2)
            continue;

        const bool portal = data.Kind == 2;
        const float size = portal ? kPortalSize : kNpcSize;
        const float pointY = (static_cast<float>(data.Location[0]) / 256.f) * length;
        const float pointX = (static_cast<float>(data.Location[1]) / 256.f) * length;
        const UI::MiniMap::Marker marker =
            UI::MiniMap::MarkerQuad(screen, heroX, heroY, pointX, pointY, size, static_cast<float>(data.Rotation),
                                    length, UI::MiniMap::MapRotation, portal);
        if (!MarkerDrawn(data))
            continue;
        // The original stored the name hint's box of every marker it drew.
        SetBtnPos(i, marker.hitBox[0], marker.hitBox[1], marker.hitBox[2], marker.hitBox[3]);
        markers.push_back({portal, size, MatrixText(UI::MiniMap::ElementToQuad(marker.quad, size, size))});
    }

    MiniMapRmlModel& model = m_RmlView.GetModel();
    const bool same = model.markers.size() == markers.size() &&
                      std::equal(markers.begin(), markers.end(), model.markers.begin(),
                                 [](const MiniMapMarkerEntry& a, const MiniMapMarkerEntry& b)
                                 { return a.portal == b.portal && a.size == b.size && a.transform == b.transform; });
    if (!same)
    {
        model.markers = std::move(markers);
        m_RmlView.MarkDirty("markers");
    }

    SyncVorthania(screen, heroX, heroY); // VORTHANIA
}

void mu::ui::window::CMiniMap::SyncHint()
{
    // Check_Btn(): the first marker whose stored box holds the pointer shows its name above that
    // box, white on RGBA(0, 0, 0, 180), in a box 6 units wider than the text, starting where the
    // text would be centred on the box (so the text sits 3 units right of centre).
    bool found = false;
    std::wstring name;
    float left = 0.f, top = 0.f, width = 0.f, height = 0.f;
    const UI::Scaling::Transform transform = OverlayTransform();
    // The names are measured in that stretch too.
    const UI::Scaling::ScopedScreenStretch screen(static_cast<int>(WindowWidth), static_cast<int>(WindowHeight));
    const float pointerX = UI::Scaling::LogicalX(transform, g_fWindowMouseX);
    const float pointerY = UI::Scaling::LogicalY(transform, g_fWindowMouseY);
    for (int i = 0; i < MAX_MINI_MAP_DATA && !found; i++)
    {
        if (m_Mini_Map_Data[i].Kind <= 0)
            break;
        const float* box = m_Btn_Loc[i];
        if (pointerX > box[0] && pointerX < (box[0] + box[2]) && pointerY > box[1] && pointerY < (box[1] + box[3]))
        {
            found = true;
            name = m_Mini_Map_Data[i].Name;
            g_pRenderText->SetFont(g_hFont);
            const SIZE size = g_pRenderText->MeasureText(name.c_str(), static_cast<int>(name.size()));
            // The original's int / float mix: the text half-width in whole units.
            const int x = static_cast<int>(box[0] + ((box[2] / 2) - static_cast<float>(size.cx / 2)));
            const int y = static_cast<int>(box[1] - static_cast<float>(size.cy + 2));
            left = UI::Scaling::PositionX(transform, static_cast<float>(x));
            top = UI::Scaling::PositionY(transform, static_cast<float>(y));
            width = UI::Scaling::SizeX(transform, static_cast<float>(size.cx + 6));
            height = UI::Scaling::SizeY(transform, static_cast<float>(size.cy));
        }
    }

    // VORTHANIA: no NPC under the pointer -> the spot under it, if any.
    if (!found)
    {
        if (!SyncSpotHint())
            Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintVisible, "hint_visible", false);
        return;
    }
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintVisible, "hint_visible", found);
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintText, "hint_text", StringUtils::WideToNarrow(name.c_str()));
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintLeft, "hint_left", left);
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintTop, "hint_top", top);
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintWidth, "hint_width", width);
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintHeight, "hint_height", height);
}

// ---------------------------------------------------------------------------------------------
// VORTHANIA
// ---------------------------------------------------------------------------------------------
void mu::ui::window::CMiniMap::SyncVorthania(const UI::MiniMap::Screen& screen, float heroX, float heroY)
{
    std::vector<MiniMapLineEntry> lines;
    const UI::MiniMap::Point hero = VorthMapToWindow(screen, heroX, heroY, static_cast<float>(Hero->PositionX),
                                                     static_cast<float>(Hero->PositionY));

    // The spots of this map, under the route.
    VorthLoadSpots();
    const int world = static_cast<int>(gMapManager.WorldActive);
    for (const VorthSpot& spot : g_VorthSpots)
    {
        if (spot.map != world)
            continue;
        const UI::MiniMap::Point p =
            VorthMapToWindow(screen, heroX, heroY, static_cast<float>(spot.x), static_cast<float>(spot.y));
        const bool seven = spot.count >= 7;
        VorthDiamond(lines, p, seven ? 5.f : 3.5f, seven ? 2.f : 1.5f, seven ? VORTH_COL_SPOT_7 : VORTH_COL_SPOT);
    }

    // a) Where a click would go now (the only thing seen before the click: the click closes the map).
    int hoverX = 0, hoverY = 0;
    if (VorthWindowToMap(screen, heroX, heroY, g_fWindowMouseX, g_fWindowMouseY, hoverX, hoverY) &&
        VorthWalkable(hoverX, hoverY))
    {
        const UI::MiniMap::Point p =
            VorthMapToWindow(screen, heroX, heroY, static_cast<float>(hoverX), static_cast<float>(hoverY));
        VorthLine(lines, hero, p, 1.5f, VORTH_COL_HOVER);
        VorthCross(lines, p, 4.f, 1.5f, VORTH_COL_HOVER);
    }

    // b) The active route: the computed stretch (at most MAX_PATH_FIND steps) strong, the rest faint.
    if (g_VorthAutoMove)
    {
        const UI::MiniMap::Point destination = VorthMapToWindow(
            screen, heroX, heroY, static_cast<float>(g_VorthAutoMoveX), static_cast<float>(g_VorthAutoMoveY));
        UI::MiniMap::Point previous = hero;
        const int from = static_cast<int>(Hero->Path.CurrentPath);
        const int to = static_cast<int>(Hero->Path.PathNum);
        for (int n = from; n < to && n < MAX_PATH_FIND; ++n)
        {
            const UI::MiniMap::Point next = VorthMapToWindow(screen, heroX, heroY, static_cast<float>(Hero->Path.PathX[n]),
                                                             static_cast<float>(Hero->Path.PathY[n]));
            VorthLine(lines, previous, next, 2.5f, VORTH_COL_ROUTE);
            previous = next;
        }
        VorthLine(lines, previous, destination, 1.5f, VORTH_COL_REST);
        VorthCross(lines, destination, 6.f, 2.5f, VORTH_COL_DESTINATION);
    }

    MiniMapRmlModel& model = m_RmlView.GetModel();
    const bool same = model.vorthLines.size() == lines.size() &&
                      std::equal(lines.begin(), lines.end(), model.vorthLines.begin(),
                                 [](const MiniMapLineEntry& a, const MiniMapLineEntry& b)
                                 { return a.transform == b.transform && a.color == b.color; });
    if (!same)
    {
        model.vorthLines = std::move(lines);
        m_RmlView.MarkDirty("vorth_lines");
    }
}

bool mu::ui::window::CMiniMap::SyncSpotHint()
{
    const UI::MiniMap::Screen screen = CurrentScreen();
    const float length = UI::MiniMap::MapLength;
    const float heroY = (static_cast<float>(Hero->PositionX) / 256.f) * length;
    const float heroX = (static_cast<float>(Hero->PositionY) / 256.f) * length;
    const int world = static_cast<int>(gMapManager.WorldActive);

    // Only the spot under the pointer gets its name: all the names at once overlap and none reads.
    const VorthSpot* under = nullptr;
    UI::MiniMap::Point underPoint;
    const float radius = 14.f * screen.scaleX;
    float best = radius * radius;
    for (const VorthSpot& spot : g_VorthSpots)
    {
        if (spot.map != world)
            continue;
        const UI::MiniMap::Point p =
            VorthMapToWindow(screen, heroX, heroY, static_cast<float>(spot.x), static_cast<float>(spot.y));
        const float dx = p.x - g_fWindowMouseX;
        const float dy = p.y - g_fWindowMouseY;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best)
        {
            best = d2;
            under = &spot;
            underPoint = p;
        }
    }
    if (under == nullptr)
        return false;

    wchar_t text[128] = {0};
    swprintf(text, 128, L"%ls x%d", under->name.c_str(), under->count);

    const UI::Scaling::Transform transform = OverlayTransform();
    const UI::Scaling::ScopedScreenStretch stretch(static_cast<int>(WindowWidth), static_cast<int>(WindowHeight));
    g_pRenderText->SetFont(g_hFont);
    const SIZE size = g_pRenderText->MeasureText(text, static_cast<int>(wcslen(text)));
    const float refX = UI::Scaling::LogicalX(transform, underPoint.x);
    const float refY = UI::Scaling::LogicalY(transform, underPoint.y);
    const float x = refX - static_cast<float>(size.cx / 2) - 3.f;
    const float y = refY - static_cast<float>(size.cy + 6);

    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintVisible, "hint_visible", true);
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintText, "hint_text", StringUtils::WideToNarrow(text));
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintLeft, "hint_left", UI::Scaling::PositionX(transform, x));
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintTop, "hint_top", UI::Scaling::PositionY(transform, y));
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintWidth, "hint_width",
         UI::Scaling::SizeX(transform, static_cast<float>(size.cx + 6)));
    Sync(m_RmlView.Binder(), &MiniMapRmlModel::hintHeight, "hint_height",
         UI::Scaling::SizeY(transform, static_cast<float>(size.cy)));
    return true;
}

// A click on the map: the hero walks there and the map closes.
void mu::ui::window::CMiniMap::WalkToWindowPoint(float windowX, float windowY)
{
    if (!m_bSuccess)
        return;

    const UI::MiniMap::Screen screen = CurrentScreen();
    const float length = UI::MiniMap::MapLength;
    const float heroY = (static_cast<float>(Hero->PositionX) / 256.f) * length;
    const float heroX = (static_cast<float>(Hero->PositionY) / 256.f) * length;

    int destX = 0, destY = 0;
    if (!VorthWindowToMap(screen, heroX, heroY, windowX, windowY, destX, destY))
        return;
    // A tile that can't be stepped on: nothing, rather than the hero walking anywhere.
    if (!VorthWalkable(destX, destY))
        return;
    if (!PathFinding2(Hero->PositionX, Hero->PositionY, destX, destY, &Hero->Path))
        return;

    Hero->MovementType = MOVEMENT_MOVE;
    SendMove(Hero, &Hero->Object);
    VorthRememberOurPath();

    g_VorthAutoMove = true;
    g_VorthAutoMoveX = destX;
    g_VorthAutoMoveY = destY;
    g_VorthAutoMoveTries = 0;

    PlayBuffer(SOUND_CLICK01);
    g_pNewUISystem->Hide(mu::ui::window::INTERFACE_MINI_MAP);
}

// The path finder gives at most MAX_PATH_FIND steps per request: when the hero stops at the end of
// our stretch, the next one is asked for. Runs while the window is enabled, shown or not.
void mu::ui::window::CMiniMap::UpdateAutoMove()
{
    if (!g_VorthAutoMove)
        return;
    // Hero always points into a static array: only "in a world" (m_bSuccess) tells it is real.
    if (m_pNewUIMng == NULL || m_bSuccess == false || Hero->Object.Live == false)
    {
        g_VorthAutoMove = false;
        return;
    }

    if (Hero->PositionX == g_VorthAutoMoveX && Hero->PositionY == g_VorthAutoMoveY)
    {
        g_VorthAutoMove = false;
        return;
    }

    if (Hero->Movement)
    {
        g_VorthAutoMoveTries = 0;
        return;
    }

    // Stopped somewhere other than the end of our stretch: something else moved it (the player
    // clicked elsewhere or went to hit something). Cancel.
    if (g_VorthOurPathEnd[0] >= 0 &&
        (static_cast<int>(Hero->PositionX) != g_VorthOurPathEnd[0] ||
         static_cast<int>(Hero->PositionY) != g_VorthOurPathEnd[1]))
    {
        g_VorthAutoMove = false;
        return;
    }

    // Standing still many times in a row: it can't get there.
    if (++g_VorthAutoMoveTries > 60)
    {
        g_VorthAutoMove = false;
        return;
    }

    if (PathFinding2(Hero->PositionX, Hero->PositionY, g_VorthAutoMoveX, g_VorthAutoMoveY, &Hero->Path))
    {
        Hero->MovementType = MOVEMENT_MOVE;
        SendMove(Hero, &Hero->Object);
        VorthRememberOurPath();
    }
    else
    {
        g_VorthAutoMove = false;
    }
}
