// NewUIGuildInfoWindow.cpp: implementation of the CNewUIGuildInfoWindow class.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "I18N/All.h"

#include "UI/NewUI/HUD/NewUIMiniMap.h"
#include "UI/NewUI/NewUISystem.h"
#include "UI/NewUI/Dialogs/NewUICommonMessageBox.h"
#include "UI/NewUI/Dialogs/NewUICustomMessageBox.h"
#include "Audio/DSPlaySound.h"

#include "Guild/NewUIGuildInfoWindow.h"
#include "UI/NewUI/Widgets/NewUIButton.h"
#include "UI/NewUI/Inventory/NewUIMyInventory.h"
#include "GameLogic/Items/CSItemOption.h"
#include "World/MapInfra/MapManager.h"
// VORTHANIA: para caminar desde el mapa.
#include "Engine/AI/ZzzAI.h"                 // PathFinding2
#include "Engine/Object/ZzzInterface.h"      // SendMove
#include "Render/Terrain/ZzzLodTerrain.h"    // TerrainWall, TW_*

// VORTHANIA ------------------------------------------------------------------
// El estado del "caminar hasta el clic" vive ACA, en variables de archivo, y no
// como miembros de CNewUIMiniMap.
//
// La primera version los puso adentro de la clase y el cliente empezo a morir al
// terminar de cargar, con codigo de salida 0xC0000374 (corrupcion de heap).
// Agregar campos cambia sizeof(CNewUIMiniMap), y el objeto se reserva con new:
// si algun otro archivo del proyecto quedo compilado contra el header viejo,
// reserva el tamano viejo mientras el constructor escribe el nuevo, y eso pisa
// memoria que no le pertenece. El proceso no muere ahi: muere despues, cuando
// el administrador de heap se da cuenta. Por eso no dejaba rastro en el log.
//
// Con las variables acá el header no se toca, sizeof no cambia, y el problema no
// puede volver. Y no se pierde nada: hay un solo mapa, asi que este estado era
// un singleton de todas formas.
// -----------------------------------------------------------------------------
namespace
{
    bool g_bVorthAutoMove = false;
    int  g_VorthAutoMoveX = 0;
    int  g_VorthAutoMoveY = 0;
    int  g_VorthAutoMoveTries = 0;

    // El final del camino que pusimos NOSOTROS la ultima vez. Sirve para darnos
    // cuenta de que el jugador tomo el control: si el camino del personaje ya no
    // termina donde nosotros lo dejamos, es que hizo clic en otro lado o fue a
    // pegarle a algo. Ver VorthCancelarSiElJugadorTomoElControl().
    int  g_VorthFinDeNuestroCamino[2] = { -1, -1 };

    void VorthRecordarNuestroCamino()
    {
        const int n = (int)Hero->Path.PathNum;
        if (n > 0 && n <= MAX_PATH_FIND)
        {
            g_VorthFinDeNuestroCamino[0] = (int)Hero->Path.PathX[n - 1];
            g_VorthFinDeNuestroCamino[1] = (int)Hero->Path.PathY[n - 1];
        }
        else
        {
            g_VorthFinDeNuestroCamino[0] = -1;
            g_VorthFinDeNuestroCamino[1] = -1;
        }
    }

    // Replica el camino que hace RenderPointRotate, para poder invertirlo.
    // mapX es la coordenada que el juego llama PositionX; mapY es la otra.
    void VorthMapToScreen(float L_x, float L_y, float mapX, float mapY,
                          float& outX, float& outY)
    {
        const float Ty = ConvertY(((float)Hero->PositionX / 256.f) * L_y);
        const float Tx = ConvertX(((float)Hero->PositionY / 256.f) * L_x);

        const float W = ConvertX(L_x);
        const float H = ConvertY(L_y);

        float ix = ConvertX((mapY / 256.f) * L_x);
        float iy = ConvertY((mapX / 256.f) * L_y);

        float x = W - Tx;
        float y = H - Ty;

        // Los dos volteos de Y que hace RenderPointRotate.
        y = H - y;
        iy = H - iy;

        vec3_t p, p3, Angle;
        float Matrix[3][4];

        Vector((ix - (W * 0.5f)) + ((W / 2.f) - (W - x)),
               (iy - (H * 0.5f)) + ((H / 2.f) - (H - y)), 0.f, p);

        Vector(0.f, 0.f, 45.f, Angle);
        AngleMatrix(Angle, Matrix);
        VectorRotate(p, Matrix, p3);

        const float halfW = WindowWidth / 2.f;
        const float halfH = WindowHeight / 2.f;

        const float dx = p3[0] + 25.f + halfW;
        const float dy = p3[1] + halfH;

        outX = (dx - g_fScreenOffset_x) / g_fScreenRate_x;
        outY = REFERENCE_HEIGHT - ((dy - g_fScreenOffset_y) / g_fScreenRate_y);
    }

    // El camino inverso: de un punto de la pantalla a una casilla del mapa.
    //
    // No invierto la rotacion de 45 grados a mano -- es facil equivocarse con un
    // signo y el error no se nota hasta que el personaje camina para otro lado.
    // En vez de eso muestreo tres puntos que conozco (0,0), (256,0) y (0,256),
    // saco la matriz de 2x2 que los lleva a pantalla, y la doy vuelta. Si algun
    // dia cambia como se dibuja el mapa, esto se acomoda solo.
    bool VorthScreenToMap(float L_x, float L_y, float sx, float sy,
                          int& outMapX, int& outMapY)
    {
        float ox, oy, ax, ay, bx, by;
        VorthMapToScreen(L_x, L_y, 0.f,   0.f,   ox, oy);
        VorthMapToScreen(L_x, L_y, 256.f, 0.f,   ax, ay);
        VorthMapToScreen(L_x, L_y, 0.f,   256.f, bx, by);

        const float m00 = ax - ox;
        const float m01 = bx - ox;
        const float m10 = ay - oy;
        const float m11 = by - oy;

        const float det = (m00 * m11) - (m01 * m10);
        if (det < 0.00001f && det > -0.00001f)
            return false;

        const float rx = sx - ox;
        const float ry = sy - oy;

        const float u = ((m11 * rx) - (m01 * ry)) / det;
        const float v = ((m00 * ry) - (m10 * rx)) / det;

        const int mapX = (int)((u * 256.f) + 0.5f);
        const int mapY = (int)((v * 256.f) + 0.5f);

        if (mapX < 0 || mapX > 255 || mapY < 0 || mapY > 255)
            return false;

        outMapX = mapX;
        outMapY = mapY;
        return true;
    }

    // Si la casilla no se puede pisar, no tiene sentido ni dibujarla ni caminarla.
    bool VorthWalkable(int mapX, int mapY)
    {
        if (mapX < 0 || mapX > 255 || mapY < 0 || mapY > 255)
            return false;

        const WORD att = TerrainWall[(mapY * 256) + mapX];
        if ((att & TW_NOMOVE) == TW_NOMOVE)   return false;
        if ((att & TW_NOGROUND) == TW_NOGROUND) return false;
        return true;
    }

    // Una cruz, para marcar un destino.
    void VorthCross(float x, float y, float radio, float grosor, unsigned int color)
    {
        RenderColorLineARGB(x - radio, y - radio, x + radio, y + radio, grosor, color);
        RenderColorLineARGB(x - radio, y + radio, x + radio, y - radio, grosor, color);
    }

    // Colores del camino. Todos opacos a proposito: RenderColorLineARGB no toca
    // el estado de blending, asi que un alpha parcial quedaria a merced de lo que
    // haya dejado puesto el dibujo anterior. Se diferencia por color y grosor.
    const unsigned int VORTH_COL_RUTA    = 0xFFFFC832u;  // dorado fuerte: el tramo calculado
    const unsigned int VORTH_COL_RESTO   = 0xFF8A6A20u;  // dorado apagado: lo que falta
    const unsigned int VORTH_COL_DESTINO = 0xFFFFFFFFu;  // blanco: la cruz del destino
    const unsigned int VORTH_COL_HOVER   = 0xFF6A7A8Au;  // gris azulado: la vista previa

    // --- VORTHANIA: los spots dibujados en el minimapa ----------------------
    //
    // La lista vive en Data\Local\VorthaniaSpots.txt y la genera el MISMO
    // script que crea los spawns en la base. Es a proposito: si los spots se
    // escribieran a mano de los dos lados, el dia que se mueva uno el mapa
    // seguiria mostrando el lugar viejo. Y un mapa que miente es peor que no
    // tener mapa: el jugador camina cinco minutos hasta un lugar vacio.
    //
    // Formato, una linea por spot:   mapa x y cantidad nombre del bicho
    // Las lineas que empiezan con # son comentarios.
    struct VorthSpot
    {
        int          mapa;
        int          x;
        int          y;
        int          cantidad;
        std::wstring nombre;
    };

    std::vector<VorthSpot> g_VorthSpots;
    bool g_VorthSpotsLeidos = false;

    void VorthCargarSpots()
    {
        if (g_VorthSpotsLeidos)
            return;

        // Se intenta UNA sola vez, ande o no. Si el archivo no esta, el
        // minimapa funciona igual sin marcadores -- no es motivo para molestar
        // al jugador ni para llenar el log cuadro a cuadro.
        g_VorthSpotsLeidos = true;

        FILE* f = fopen("Data\\Local\\VorthaniaSpots.txt", "rt");
        if (f == nullptr)
            return;

        char linea[256];
        while (fgets(linea, sizeof(linea), f) != nullptr)
        {
            if (linea[0] == '#' || linea[0] == '\n' || linea[0] == '\r')
                continue;

            int mapa = 0, x = 0, y = 0, cantidad = 0;
            char nombre[96] = { 0 };
            const int leidos = sscanf(linea, "%d %d %d %d %95[^\r\n]",
                                      &mapa, &x, &y, &cantidad, nombre);
            if (leidos < 4)
                continue;

            VorthSpot s;
            s.mapa = mapa;
            s.x = x;
            s.y = y;
            s.cantidad = cantidad;
            for (const char* c = nombre; *c != 0; ++c)
                s.nombre.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*c)));
            g_VorthSpots.push_back(s);
        }
        fclose(f);
    }

    const unsigned int VORTH_COL_SPOT   = 0xFF64D2FFu;  // celeste: spot normal
    const unsigned int VORTH_COL_SPOT_7 = 0xFFFFA000u;  // naranja: el de 7, el mas alto del mapa

    // Un rombo. Se distingue del icono de NPC de un vistazo y no lo tapa.
    void VorthRombo(float x, float y, float r, float grosor, unsigned int color)
    {
        RenderColorLineARGB(x,     y - r, x + r, y,     grosor, color);
        RenderColorLineARGB(x + r, y,     x,     y + r, grosor, color);
        RenderColorLineARGB(x,     y + r, x - r, y,     grosor, color);
        RenderColorLineARGB(x - r, y,     x,     y - r, grosor, color);
    }
}

extern BYTE m_OccupationState;

using namespace SEASON3B;

SEASON3B::CNewUIMiniMap::CNewUIMiniMap()
{
    m_pNewUIMng = NULL;
}

SEASON3B::CNewUIMiniMap::~CNewUIMiniMap()
{
    Release();
}

bool SEASON3B::CNewUIMiniMap::Create(CNewUIManager* pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_MINI_MAP, this);

    LoadBitmap(L"Interface\\mini_map_ui_corner.tga", IMAGE_MINIMAP_INTERFACE + 1, GL_LINEAR);
    LoadBitmap(L"Interface\\mini_map_ui_line.jpg", IMAGE_MINIMAP_INTERFACE + 2, GL_LINEAR);
    LoadBitmap(L"Interface\\mini_map_ui_cha.tga", IMAGE_MINIMAP_INTERFACE + 3, GL_LINEAR);
    LoadBitmap(L"Interface\\mini_map_ui_portal.tga", IMAGE_MINIMAP_INTERFACE + 4, GL_LINEAR);
    LoadBitmap(L"Interface\\mini_map_ui_npc.tga", IMAGE_MINIMAP_INTERFACE + 5, GL_LINEAR);
    LoadBitmap(L"Interface\\mini_map_ui_cancel.tga", IMAGE_MINIMAP_INTERFACE + 6, GL_LINEAR);

    m_BtnExit.ChangeButtonImgState(true, IMAGE_MINIMAP_INTERFACE + 6, false);
    m_BtnExit.ChangeButtonInfo(m_Pos.x + 610, 3, 85, 85);
    m_BtnExit.ChangeToolTipText(&I18N::Game::Close388, true);	// 1002 "�ݱ�"

    SetPos(x, y);

    m_Lenth[0].x = 800;
    m_Lenth[1].x = 1000;
    m_Lenth[2].x = 1200;
    m_Lenth[3].x = 1400;
    m_Lenth[4].x = 1600;
    m_Lenth[5].x = 1800;
    m_Lenth[0].y = 800;
    m_Lenth[1].y = 1000;
    m_Lenth[2].y = 1200;
    m_Lenth[3].y = 1400;
    m_Lenth[4].y = 1600;
    m_Lenth[5].y = 1800;
    m_MiniPos = 0;
    m_bSuccess = false;
    return true;
}

void SEASON3B::CNewUIMiniMap::ClosingProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

float SEASON3B::CNewUIMiniMap::GetLayerDepth()
{
    return 8.1f;
}

void SEASON3B::CNewUIMiniMap::OpenningProcess()
{
}

void SEASON3B::CNewUIMiniMap::Release()
{
    UnloadImages();

    for (int i = 1; i < 7; i++)
    {
        DeleteBitmap(IMAGE_MINIMAP_INTERFACE + i);
    }

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void SEASON3B::CNewUIMiniMap::SetPos(int x, int y)
{
    m_BtnExit.ChangeButtonInfo(REFERENCE_WIDTH - 27, 3, 30, 25);
}

void SEASON3B::CNewUIMiniMap::SetBtnPos(int Num, float x, float y, float nx, float ny)
{
    m_Btn_Loc[Num][0] = x;
    m_Btn_Loc[Num][1] = y;
    m_Btn_Loc[Num][2] = nx;
    m_Btn_Loc[Num][3] = ny;
}

bool SEASON3B::CNewUIMiniMap::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MINI_MAP))
    {
        if (IsPress(VK_ESCAPE) == true || IsPress(VK_TAB) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_MINI_MAP);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool SEASON3B::CNewUIMiniMap::Render()
{
    float Rot = 45.f;

    if (m_bSuccess == false)
        return m_bSuccess;

    EnableAlphaTest();
    // VORTHANIA: 0.85 tapaba el juego entero. 0.35 deja ver lo de abajo.
    RenderColor(0, 0, REFERENCE_WIDTH, 430, 0.35f, 1);
    DisableAlphaBlend();
    EnableAlphaTest();

    auto Ty = (float)(((float)Hero->PositionX / 256.f) * m_Lenth[m_MiniPos].y);
    auto Tx = (float)(((float)Hero->PositionY / 256.f) * m_Lenth[m_MiniPos].x);
    float Ty1;
    float Tx1;
    float uvxy = (41.7f / 64.f);
    float uvxy_Line = 8.f / 8.f;
    float Ui_wid = 35.f;
    float Ui_Hig = 6.f;
    float Rot_Loc = 45.f;
    int i = 0;

    // VORTHANIA: mapa translucido, para ver el personaje por debajo.
    RenderBitRotate(IMAGE_MINIMAP_INTERFACE, m_Lenth[m_MiniPos].x - Tx, m_Lenth[m_MiniPos].y - Ty, m_Lenth[m_MiniPos].x, m_Lenth[m_MiniPos].y, Rot, 0.65f);

    int NpcWidth = 15;
    int NpcWidthP = 30;
    for (i = 0; i < MAX_MINI_MAP_DATA; i++)
    {
        if (m_Mini_Map_Data[i].Kind > 0)
        {
            Ty1 = (float)(((float)m_Mini_Map_Data[i].Location[0] / 256.f) * m_Lenth[m_MiniPos].y);
            Tx1 = (float)(((float)m_Mini_Map_Data[i].Location[1] / 256.f) * m_Lenth[m_MiniPos].x);
            Rot_Loc = (float)m_Mini_Map_Data[i].Rotation;

            if (m_Mini_Map_Data[i].Kind == 1) //npc
            {
                if (!(gMapManager.WorldActive == WD_34CRYWOLF_1ST && m_OccupationState > 0) || (m_Mini_Map_Data[i].Location[0] == 228 && m_Mini_Map_Data[i].Location[1] == 48 && gMapManager.WorldActive == WD_34CRYWOLF_1ST))
                    RenderPointRotate(IMAGE_MINIMAP_INTERFACE + 5, Tx1, Ty1, NpcWidth, NpcWidth, m_Lenth[m_MiniPos].x - Tx, m_Lenth[m_MiniPos].y - Ty, m_Lenth[m_MiniPos].x, m_Lenth[m_MiniPos].y, Rot, Rot_Loc, 17.5f / 32.f, 17.5f / 32.f, i);
            }
            else
                if (m_Mini_Map_Data[i].Kind == 2)
                    RenderPointRotate(IMAGE_MINIMAP_INTERFACE + 4, Tx1, Ty1, NpcWidthP, NpcWidthP, m_Lenth[m_MiniPos].x - Tx, m_Lenth[m_MiniPos].y - Ty, m_Lenth[m_MiniPos].x, m_Lenth[m_MiniPos].y, Rot, Rot_Loc, 17.5f / 32.f, 17.5f / 32.f, 100 + i);
        }
        else
            break;
    }

    // VORTHANIA: el camino ---------------------------------------------------
    // Se dibuja aca a proposito: encima del mapa y de los NPC, pero debajo del
    // marcador del personaje y del marco, para que nada importante quede tapado.
    {
        const float L_x = (float)m_Lenth[m_MiniPos].x;
        const float L_y = (float)m_Lenth[m_MiniPos].y;

        float hx, hy;
        VorthMapToScreen(L_x, L_y, (float)Hero->PositionX, (float)Hero->PositionY, hx, hy);

        // a) Vista previa: a donde iria si hago clic ahora.
        //    Es lo unico que se ve ANTES de hacer clic, porque al hacerlo el mapa
        //    se cierra.
        int hoverX = 0, hoverY = 0;
        if (VorthScreenToMap(L_x, L_y, (float)MouseX, (float)MouseY, hoverX, hoverY)
            && VorthWalkable(hoverX, hoverY))
        {
            float px, py;
            VorthMapToScreen(L_x, L_y, (float)hoverX, (float)hoverY, px, py);
            RenderColorLineARGB(hx, hy, px, py, 1.5f, VORTH_COL_HOVER);
            VorthCross(px, py, 4.f, 1.5f, VORTH_COL_HOVER);
        }

        // b) La ruta activa, si vamos caminando a algun lado.
        if (g_bVorthAutoMove)
        {
            float dx2, dy2;
            VorthMapToScreen(L_x, L_y, (float)g_VorthAutoMoveX, (float)g_VorthAutoMoveY, dx2, dy2);

            // El tramo que el pathfinder ya calculo. Son como maximo 15 pasos
            // (MAX_PATH_FIND), por eso casi nunca llega hasta el final.
            float px = hx, py = hy;
            const int desde = (int)Hero->Path.CurrentPath;
            const int hasta = (int)Hero->Path.PathNum;

            for (int n = desde; n < hasta && n < MAX_PATH_FIND; ++n)
            {
                float nx, ny;
                VorthMapToScreen(L_x, L_y,
                                 (float)Hero->Path.PathX[n],
                                 (float)Hero->Path.PathY[n], nx, ny);
                RenderColorLineARGB(px, py, nx, ny, 2.5f, VORTH_COL_RUTA);
                px = nx;
                py = ny;
            }

            // Y desde donde termina ese tramo hasta el destino final, en tenue:
            // es la intencion, no un camino confirmado. Update() va a ir pidiendo
            // los tramos siguientes a medida que camina.
            RenderColorLineARGB(px, py, dx2, dy2, 1.5f, VORTH_COL_RESTO);

            VorthCross(dx2, dy2, 6.f, 2.5f, VORTH_COL_DESTINO);
        }
    }
    // ------------------------------------------------------------------------

    // --- VORTHANIA: los spots -----------------------------------------------
    {
        VorthCargarSpots();

        const float L_x = (float)m_Lenth[m_MiniPos].x;
        const float L_y = (float)m_Lenth[m_MiniPos].y;
        const int   mapaActual = (int)gMapManager.WorldActive;

        const VorthSpot* debajoDelMouse = nullptr;
        float mejorDistancia = 14.f * 14.f;   // radio de 14 para "el mouse esta encima"
        float mouseX = 0.f, mouseY = 0.f;

        for (size_t s_i = 0; s_i < g_VorthSpots.size(); ++s_i)
        {
            const VorthSpot& sp = g_VorthSpots[s_i];
            if (sp.mapa != mapaActual)
                continue;

            float sx, sy;
            VorthMapToScreen(L_x, L_y, (float)sp.x, (float)sp.y, sx, sy);

            const bool esDeSiete = (sp.cantidad >= 7);
            VorthRombo(sx, sy,
                       esDeSiete ? 5.0f : 3.5f,
                       esDeSiete ? 2.0f : 1.5f,
                       esDeSiete ? VORTH_COL_SPOT_7 : VORTH_COL_SPOT);

            const float ddx = sx - (float)MouseX;
            const float ddy = sy - (float)MouseY;
            const float dist2 = (ddx * ddx) + (ddy * ddy);
            if (dist2 < mejorDistancia)
            {
                mejorDistancia = dist2;
                debajoDelMouse = &sp;
                mouseX = sx;
                mouseY = sy;
            }
        }

        // El nombre se muestra solo del spot que tenes debajo del mouse.
        // Dibujar los doce carteles de los corrales de la Arena al mismo tiempo
        // los deja uno encima del otro y no se lee ninguno.
        if (debajoDelMouse != nullptr)
        {
            wchar_t cartel[128] = { 0 };
            swprintf(cartel, 128, L"%ls x%d",
                     debajoDelMouse->nombre.c_str(), debajoDelMouse->cantidad);

            g_pRenderText->SetFont(g_hFont);
            const SIZE tam = g_pRenderText->MeasureText(cartel, (int)wcslen(cartel));

            const DWORD colorPrevio = g_pRenderText->GetTextColor();
            const DWORD fondoPrevio = g_pRenderText->GetBgColor();

            g_pRenderText->SetTextColor(RGBA(255, 255, 255, 255));
            g_pRenderText->SetBgColor(RGBA(0, 0, 0, 180));
            g_pRenderText->RenderText((int)(mouseX - (tam.cx / 2)),
                                      (int)(mouseY - (tam.cy + 6)),
                                      cartel, tam.cx + 6, 0, RT3_SORT_CENTER);

            g_pRenderText->SetTextColor(colorPrevio);
            g_pRenderText->SetBgColor(fondoPrevio);
        }
    }
    // ------------------------------------------------------------------------

    float Ch_wid = 12;
    RenderImage(IMAGE_MINIMAP_INTERFACE + 3, 325, 230, Ch_wid, Ch_wid, 0.f, 0.f, 17.5f / 32.f, 17.5f / 32.f);

    for (i = 0; i < 25; i++)
    {
        RenderImage(IMAGE_MINIMAP_INTERFACE + 2, i * Ui_wid, 0, Ui_wid, Ui_Hig, 0.f, 1.f, uvxy, -uvxy_Line);
        RenderImage(IMAGE_MINIMAP_INTERFACE + 2, i * Ui_wid, 430 - Ui_Hig, Ui_wid, Ui_Hig, 0.f, 0.f, uvxy, uvxy_Line);
    }
    for (i = 0; i < 20; i++)
    {
        RenderBitmapRotate(IMAGE_MINIMAP_INTERFACE + 2, (Ui_Hig / 2.f), i * (Ui_wid - 3.f), Ui_wid, Ui_Hig, -90.f, 0.f, 0.f, uvxy, uvxy_Line);
        RenderBitmapRotate(IMAGE_MINIMAP_INTERFACE + 2, REFERENCE_WIDTH - (Ui_Hig / 2.f), i * (Ui_wid - 3.f), Ui_wid, Ui_Hig, 90.f, 0.f, 0.f, uvxy, uvxy_Line);
    }

    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, 0, 0, Ui_wid, Ui_wid, 0.f, 0.f, uvxy, uvxy);
    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, REFERENCE_WIDTH - Ui_wid, 0, Ui_wid, Ui_wid, uvxy, 0.f, -uvxy, uvxy);
    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, 0, 430 - Ui_wid, Ui_wid, Ui_wid, 0.f, uvxy, uvxy, -uvxy);
    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, REFERENCE_WIDTH - Ui_wid, 430 - Ui_wid, Ui_wid, Ui_wid, uvxy, uvxy, -uvxy, -uvxy);

    m_BtnExit.Render(true);

    DisableAlphaBlend();

    Check_Btn(MouseX, MouseY);
    return true;
}

bool SEASON3B::CNewUIMiniMap::Update()
{
    // VORTHANIA: el buscador de caminos de MU manda como mucho 15 pasos por
    // pedido (MAX_PATH_FIND). Si el destino esta lejos, el personaje camina un
    // tramo y se planta. Aca se vuelve a pedir camino cada vez que se detiene.
    //
    // La guarda importa: CNewUIManager::Update() recorre los objetos
    // HABILITADOS, no solo los visibles, y llega aca tambien en la pantalla de
    // login, donde Hero todavia no es un personaje de verdad. Ojo que Hero NUNCA
    // es NULL -- apunta a una posicion de un arreglo estatico -- asi que
    // chequearlo contra NULL no sirve de nada.
    //   m_pNewUIMng != NULL  -> Create() ya corrio
    //   m_bSuccess           -> LoadImages() cargo un mapa, o sea estamos en mundo
    if (m_pNewUIMng == NULL || m_bSuccess == false)
    {
        return true;
    }

    if (g_bVorthAutoMove)
    {
        if (Hero->Object.Live == false)
        {
            g_bVorthAutoMove = false;
            return true;
        }

        // VORTHANIA (26/09, segundo intento): cancelar si el jugador tomo el control.
        //
        // PRIMER INTENTO, Y POR QUE FALLO:
        // comparaba el final del camino actual contra el que habiamos puesto
        // nosotros. Suena razonable, pero EL CAMINO SE CONSUME: cuando el
        // personaje termina de caminar, PathNum vuelve a cero y no queda nada
        // que comparar. Justo ahi -el momento en que terminas de matar al bicho
        // y te quedas quieto- la comparacion no se evaluaba, y el codigo de
        // abajo te mandaba de vuelta al destino viejo. Andaba en todos los
        // momentos menos en el unico que importaba.
        //
        // ESTE INTENTO pregunta algo que siempre se puede responder:
        // cuando el personaje se detiene, ¿esta parado donde termina el tramo
        // que le pedimos nosotros?
        //
        //   Si  -> completo nuestro tramo. Seguimos al siguiente.
        //   No  -> lo movio otra cosa. Cancelamos.
        //
        // La posicion del personaje existe siempre, no se "consume", y no
        // depende de adivinar en que estado quedo una estructura interna.
        if (Hero->PositionX == g_VorthAutoMoveX && Hero->PositionY == g_VorthAutoMoveY)
        {
            g_bVorthAutoMove = false;
            return true;
        }

        if (Hero->Movement)
        {
            g_VorthAutoMoveTries = 0;
        }
        else
        {
            // El personaje esta quieto. Antes de mandarlo a seguir viaje:
            // ¿quedo donde terminaba el tramo que le pedimos?
            if (g_VorthFinDeNuestroCamino[0] >= 0
                && ((int)Hero->PositionX != g_VorthFinDeNuestroCamino[0]
                 || (int)Hero->PositionY != g_VorthFinDeNuestroCamino[1]))
            {
                g_bVorthAutoMove = false;
                return true;
            }

            // Si se queda quieto muchas veces seguidas, es que no puede llegar.
            if (++g_VorthAutoMoveTries > 60)
            {
                g_bVorthAutoMove = false;
                return true;
            }

            if (PathFinding2(Hero->PositionX, Hero->PositionY,
                             g_VorthAutoMoveX, g_VorthAutoMoveY, &Hero->Path))
            {
                Hero->MovementType = MOVEMENT_MOVE;
                SendMove(Hero, &Hero->Object);
                VorthRecordarNuestroCamino();
            }
            else
            {
                g_bVorthAutoMove = false;
            }
        }
    }

    return true;
}

void SEASON3B::CNewUIMiniMap::LoadImages(const wchar_t* Filename)
{
    wchar_t Fname[300];
    int i = 0;
    mu_swprintf(Fname, L"Data\\%ls\\mini_map.ozt", Filename);
    FILE* pFile = _wfopen(Fname, L"rb");

    if (pFile == NULL)
    {
        m_bSuccess = false;
        return;
    }
    else
    {
        m_bSuccess = true;
        fclose(pFile);
        mu_swprintf(Fname, L"%ls\\mini_map.tga", Filename);
        LoadBitmap(Fname, IMAGE_MINIMAP_INTERFACE, GL_LINEAR);
    }

    mu_swprintf(Fname, L"Data\\Local\\%ls\\Minimap\\Minimap_%ls_%ls.bmd", g_strSelectedML.c_str(), Filename, g_strSelectedML.c_str());

    for (i = 0; i < MAX_MINI_MAP_DATA; i++)
    {
        m_Mini_Map_Data[i].Kind = 0;
    }

    FILE* fp = _wfopen(Fname, L"rb");

    if (fp != NULL)
    {
        int Size = sizeof(MINI_MAP_FILE);
        BYTE* Buffer = new BYTE[Size * MAX_MINI_MAP_DATA + 45];
        fread(Buffer, (Size * MAX_MINI_MAP_DATA) + 45, 1, fp);

        DWORD dwCheckSum;
        fread(&dwCheckSum, sizeof(DWORD), 1, fp);
        fclose(fp);

        if (dwCheckSum != GenerateCheckSum2(Buffer, (Size * MAX_MINI_MAP_DATA) + 45, 0x2BC1))
        {
            wchar_t Text[256];
            mu_swprintf(Text, L"%ls - File corrupted.", Fname);
            g_ErrorReport.Write(Text);
            MessageBox(g_hWnd, Text, NULL, MB_OK);
            SendMessage(g_hWnd, WM_DESTROY, 0, 0);
        }
        else
        {
            BYTE* pSeek = Buffer;

            for (i = 0; i < MAX_MINI_MAP_DATA; i++)
            {
                BuxConvert(pSeek, Size);
                //memcpy(&(m_Mini_Map_Data[i]), pSeek, Size);

                MINI_MAP_FILE current{ };
                auto target = &(m_Mini_Map_Data[i]);
                memcpy(&current, pSeek, Size);
                memcpy(target, pSeek, Size);

                CMultiLanguage::ConvertFromUtf8(target->Name, current.Name);
                /*int wchars_num = MultiByteToWideChar(CP_UTF8, 0, current.Name, -1, NULL, 0);
                MultiByteToWideChar(CP_UTF8, 0, current.Name, -1, target->Name, wchars_num);
                target->Name[wchars_num] = L'\0';*/
                pSeek += Size;
            }
        }

        delete[] Buffer;
    }
}

void SEASON3B::CNewUIMiniMap::UnloadImages()
{
    DeleteBitmap(IMAGE_MINIMAP_INTERFACE);
}

bool SEASON3B::CNewUIMiniMap::UpdateMouseEvent()
{
    bool ret = true;

    if (m_BtnExit.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_MINI_MAP);
        return true;
    }

    if (IsPress(VK_LBUTTON))
    {
        ret = Check_Mouse(MouseX, MouseY);
        if (ret == false)
        {
            PlayBuffer(SOUND_CLICK01);
        }
    }

    if (CheckMouseIn(0, 0, REFERENCE_WIDTH, 430))
    {
        return false;
    }

    return ret;
}

// VORTHANIA: clic en el mapa -> el personaje camina hasta ahi y el mapa se cierra.
//
// La transformacion de mapa a pantalla es afin (escala, rotacion de 45 grados y
// traslacion). En vez de invertirla a mano -- que es donde uno se equivoca de
// signo -- se calculan tres puntos conocidos y se resuelve un sistema de 2x2.
// Si manana cambian el dibujo, esto se acomoda solo.
bool SEASON3B::CNewUIMiniMap::Check_Mouse(int mx, int my)
{
    if (m_bSuccess == false)
        return true;

    const float L_x = (float)m_Lenth[m_MiniPos].x;
    const float L_y = (float)m_Lenth[m_MiniPos].y;

    int destX = 0, destY = 0;
    if (!VorthScreenToMap(L_x, L_y, (float)mx, (float)my, destX, destY))
        return true;

    // Si el destino no se puede pisar no hacemos nada: mejor eso que ver al
    // personaje salir caminando a cualquier lado.
    if (!VorthWalkable(destX, destY))
        return true;

    if (!PathFinding2(Hero->PositionX, Hero->PositionY, destX, destY, &Hero->Path))
        return true;

    Hero->MovementType = MOVEMENT_MOVE;
    SendMove(Hero, &Hero->Object);
    VorthRecordarNuestroCamino();

    g_bVorthAutoMove = true;
    g_VorthAutoMoveX = destX;
    g_VorthAutoMoveY = destY;
    g_VorthAutoMoveTries = 0;

    // El sonido lo hace UpdateMouseEvent cuando ve que devolvimos false.
    g_pNewUISystem->Hide(SEASON3B::INTERFACE_MINI_MAP);

    return false;
}

bool SEASON3B::CNewUIMiniMap::Check_Btn(int mx, int my)
{
    int i = 0;
    for (i = 0; i < MAX_MINI_MAP_DATA; i++)
    {
        if (m_Mini_Map_Data[i].Kind > 0)
        {
            if (mx > m_Btn_Loc[i][0] && mx < (m_Btn_Loc[i][0] + m_Btn_Loc[i][2]) && my > m_Btn_Loc[i][1] && my < (m_Btn_Loc[i][1] + m_Btn_Loc[i][3]))
            {
                m_TooltipText = (std::wstring)m_Mini_Map_Data[i].Name;
                g_pRenderText->SetFont(g_hFont);
                const SIZE Fontsize = g_pRenderText->MeasureText(
                    m_TooltipText.c_str(), static_cast<int>(m_TooltipText.size()));

                int x = m_Btn_Loc[i][0] + ((m_Btn_Loc[i][2] / 2) - (Fontsize.cx / 2));
                int y = m_Btn_Loc[i][1] + m_Btn_Loc[i][3] + 2;

                y = m_Btn_Loc[i][1] - (Fontsize.cy + 2);

                DWORD backuptextcolor = g_pRenderText->GetTextColor();
                DWORD backuptextbackcolor = g_pRenderText->GetBgColor();

                g_pRenderText->SetTextColor(RGBA(255, 255, 255, 255));
                g_pRenderText->SetBgColor(RGBA(0, 0, 0, 180));
                g_pRenderText->RenderText(x, y, m_TooltipText.c_str(), Fontsize.cx + 6, 0, RT3_SORT_CENTER);

                g_pRenderText->SetTextColor(backuptextcolor);
                g_pRenderText->SetBgColor(backuptextbackcolor);

                return true;
            }
        }
        else
            break;
    }
    return false;
}
