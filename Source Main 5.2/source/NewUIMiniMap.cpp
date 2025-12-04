// NewUIGuildInfoWindow.cpp: implementation of the CNewUIGuildInfoWindow class.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "NewUIMiniMap.h"
#include "NewUISystem.h"
#include "NewUICommonMessageBox.h"
#include "NewUICustomMessageBox.h"
#include "DSPlaySound.h"

#include "NewUIGuildInfoWindow.h"
#include "NewUIButton.h"
#include "NewUIMyInventory.h"
#include "CSitemOption.h"
#include "MapManager.h"
#include "ZzzAI.h"
#include "ZzzInterface.h"
#include "ZzzLodTerrain.h"
#include "ZzzPath.h"
#include "GMCrywolf1st.h"
#include "ZzzOpenglUtil.h"

extern BYTE m_OccupationState;
extern int TargetX;
extern int TargetY;
extern bool MouseLButtonPush;
extern bool MouseRButtonPush;
extern bool MouseRButton;
extern bool MouseRButtonPop;

using namespace SEASON3B;

namespace
{
    constexpr int AUTO_WALK_SEARCH_WITH_ERROR = TERRAIN_SIZE * TERRAIN_SIZE;
    constexpr int AUTO_WALK_SEARCH_WITHOUT_ERROR = TERRAIN_SIZE * TERRAIN_SIZE;
    constexpr int AUTO_WALK_PREFETCH_THRESHOLD = 3;

    PATH g_AutoWalkPathfinder;
    bool g_AutoWalkPathfinderReady = false;

    void EnsureAutoWalkPathfinderReady()
    {
        if (g_AutoWalkPathfinderReady == false)
        {
            g_AutoWalkPathfinder.SetMapDimensions(TERRAIN_SIZE, TERRAIN_SIZE, TerrainWall);
            g_AutoWalkPathfinder.SetMaxSearchCount(AUTO_WALK_SEARCH_WITH_ERROR, AUTO_WALK_SEARCH_WITHOUT_ERROR);
            g_AutoWalkPathfinderReady = true;
        }
    }

    constexpr float FIELD_MINIMAP_SIZE = 170.f;
    constexpr float FIELD_MINIMAP_MARGIN = 12.f;
    constexpr float FIELD_MINIMAP_PADDING = 0.f;
    constexpr float FIELD_MINIMAP_TEXT_HEIGHT = 18.f;
    constexpr float FIELD_MINIMAP_MAP_ALPHA = 0.8f;
    constexpr float FIELD_MINIMAP_VERTICAL_OFFSET = 48.f;
    constexpr float FIELD_MINIMAP_WORLD_OFFSET_X = -6.f;
    constexpr float ENTITY_MARKER_UV = 17.5f / 32.f;

    struct MiniMapEntityStyle
    {
        float Size;
        float ColorR;
        float ColorG;
        float ColorB;
    };

    bool ShouldRenderEntityOnMap(const CHARACTER* entity, MiniMapEntityStyle& outStyle)
    {
        if (entity == NULL)
            return false;

        if (entity == Hero)
            return false;

        if (entity->Object.Live == false)
            return false;

        const BYTE kind = entity->Object.Kind;

        if ((kind & KIND_PLAYER) == KIND_PLAYER)
        {
            outStyle = { 9.f, 0.f, 0.75f, 1.f };
            return true;
        }

        if ((kind & KIND_NPC) == KIND_NPC)
        {
            outStyle = { 10.f, 0.2f, 1.f, 0.4f };
            return true;
        }

        if ((kind & KIND_MONSTER) == KIND_MONSTER)
        {
            outStyle = { 8.f, 1.f, 0.35f, 0.35f };
            return true;
        }

        return false;
    }

    bool TryGetEntityMapCoordinates(const CHARACTER* entity, float mapWidth, float mapHeight, float& outX, float& outY)
    {
        if (mapWidth <= 0.f || mapHeight <= 0.f)
            return false;

        const int positionX = entity->PositionX;
        const int positionY = entity->PositionY;

        if (positionX < 0 || positionX > 255 || positionY < 0 || positionY > 255)
            return false;

        outY = (static_cast<float>(positionX) / 256.f) * mapHeight;
        outX = (static_cast<float>(positionY) / 256.f) * mapWidth;
        return true;
    }

    void RenderDetectedEntities(float centerX, float centerY, float mapWidth, float mapHeight, float horizontalOffset = 0.f)
    {
        if (Hero == NULL)
            return;

        for (int i = 0; i < MAX_CHARACTERS_CLIENT; ++i)
        {
            CHARACTER* entity = &CharactersClient[i];
            if (entity == nullptr)
                continue;

            MiniMapEntityStyle style;
            if (ShouldRenderEntityOnMap(entity, style) == false)
                continue;

            float entityMapX = 0.f;
            float entityMapY = 0.f;
            if (TryGetEntityMapCoordinates(entity, mapWidth, mapHeight, entityMapX, entityMapY) == false)
                continue;

            entityMapX += horizontalOffset;

            glColor4f(style.ColorR, style.ColorG, style.ColorB, 1.f);
            RenderPointRotate(SEASON3B::CNewUIMiniMap::IMAGE_MINIMAP_INTERFACE + 5, entityMapX, entityMapY, style.Size, style.Size, centerX, centerY, mapWidth, mapHeight, 0.f, 0.f, ENTITY_MARKER_UV, ENTITY_MARKER_UV);
        }

        glColor4f(1.f, 1.f, 1.f, 1.f);
    }

    class FieldMiniMapTransformGuard
    {
    public:
        FieldMiniMapTransformGuard(float mapX, float mapY, float mapSize)
        {
            glGetIntegerv(GL_MATRIX_MODE, &m_PreviousMatrixMode);
            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();

            const float halfSize = mapSize * 0.5f;
            const float centerX = mapX + halfSize;
            const float centerY = mapY + halfSize;
            const float windowWidthF = static_cast<float>(WindowWidth);
            const float windowHeightF = static_cast<float>(WindowHeight);
            const float scaledCenterX = centerX * (windowWidthF / 640.f);
            const float scaledCenterY = centerY * (windowHeightF / 480.f);
            const float targetX = scaledCenterX;
            const float targetY = windowHeightF - scaledCenterY;

            glTranslatef(targetX - (windowWidthF * 0.5f), targetY - (windowHeightF * 0.5f), 0.f);
        }

        ~FieldMiniMapTransformGuard()
        {
            glMatrixMode(GL_MODELVIEW);
            glPopMatrix();
            glMatrixMode(m_PreviousMatrixMode);
        }

    private:
        GLint m_PreviousMatrixMode;
    };
}

static const wchar_t AUTO_WALK_FAIL_MESSAGE[] = L"No hay un camino disponible hacia ese punto.";

SEASON3B::CNewUIMiniMap::CNewUIMiniMap()
{
    m_pNewUIMng = NULL;
    m_bAutoWalkActive = false;
    m_AutoWalkTarget.x = 0;
    m_AutoWalkTarget.y = 0;
    m_AutoWalkPreviewPath.clear();
    m_AutoWalkCurrentIndex = 0;
    m_MiniMapCount = 0;
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
    m_BtnExit.ChangeToolTipText(GlobalText[1002], true);	// 1002 "´Ý±â"

    SetPos(x, y);

    const int zoomSteps[] = { 200, 250, 300, 350, 400, 450, 500, 550, 600, 650, 700, 750, 800, 850, 900, 950, 1000, 1050, 1100, 1150, 1200, 1250, 1300, 1350, 1400, 1450, 1500, 1550, 1600, 1650, 1700, 1750, 1800 };
    const size_t zoomCount = sizeof(zoomSteps) / sizeof(zoomSteps[0]);
    for (size_t i = 0; i < zoomCount; ++i)
    {
        m_Lenth[i].x = zoomSteps[i];
        m_Lenth[i].y = zoomSteps[i];
    }
    m_MiniPos = 12;
    m_bSuccess = false;
    m_MiniMapCount = 0;
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
    m_BtnExit.ChangeButtonInfo(640 - 27, 3, 30, 25);
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

    if (m_bSuccess == false)
        return m_bSuccess;

    EnableAlphaTest();
    RenderColor(0, 0, 640, 430, 0.85f, 1);
    DisableAlphaBlend();
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    const float mapScaleX = static_cast<float>(m_Lenth[m_MiniPos].x);
    const float mapScaleY = static_cast<float>(m_Lenth[m_MiniPos].y);

    auto Ty = (float)(((float)Hero->PositionX / 256.f) * mapScaleY);
    auto Tx = (float)(((float)Hero->PositionY / 256.f) * mapScaleX);
    float Ty1;
    float Tx1;
    float uvxy = (41.7f / 64.f);
    float uvxy_Line = 8.f / 8.f;
    float Ui_wid = 35.f;
    float Ui_Hig = 6.f;
    float Rot_Loc = 0.f;
    int i = 0;

    RenderBitRotate(IMAGE_MINIMAP_INTERFACE, mapScaleX * 0.5f, mapScaleY * 0.5f, mapScaleX, mapScaleY, 0.f);

    int NpcWidth = 15;
    int NpcWidthP = 30;
    for (i = 0; i < MAX_MINI_MAP_DATA; i++)
    {
        if (m_Mini_Map_Data[i].Kind > 0)
        {
            Ty1 = (float)(((float)m_Mini_Map_Data[i].Location[0] / 256.f) * mapScaleY);
            Tx1 = (float)(((float)m_Mini_Map_Data[i].Location[1] / 256.f) * mapScaleX);
            Rot_Loc = (float)m_Mini_Map_Data[i].Rotation;

            if (m_Mini_Map_Data[i].Kind == 1) //npc
            {
                if (!(gMapManager.WorldActive == WD_34CRYWOLF_1ST && m_OccupationState > 0) || (m_Mini_Map_Data[i].Location[0] == 228 && m_Mini_Map_Data[i].Location[1] == 48 && gMapManager.WorldActive == WD_34CRYWOLF_1ST))
                    RenderPointRotate(IMAGE_MINIMAP_INTERFACE + 5, Tx1, Ty1, NpcWidth, NpcWidth, mapScaleX * 0.5f, mapScaleY * 0.5f, mapScaleX, mapScaleY, 0.f, Rot_Loc, 17.5f / 32.f, 17.5f / 32.f, i);
            }
            else
                if (m_Mini_Map_Data[i].Kind == 2)
                    RenderPointRotate(IMAGE_MINIMAP_INTERFACE + 4, Tx1, Ty1, NpcWidthP, NpcWidthP, mapScaleX * 0.5f, mapScaleY * 0.5f, mapScaleX, mapScaleY, 0.f, Rot_Loc, 17.5f / 32.f, 17.5f / 32.f, 100 + i);
        }
        else
            break;
    }

    if (m_bAutoWalkActive && m_AutoWalkPreviewPath.size() > 1)
    {
        glColor4f(0.f, 0.85f, 1.f, 0.85f);
        RenderAutoWalkPath(mapScaleX * 0.5f, mapScaleY * 0.5f, mapScaleX, mapScaleY, 0.f);
        glColor4f(1.f, 1.f, 1.f, 1.f);
    }

    RenderDetectedEntities(mapScaleX * 0.5f, mapScaleY * 0.5f, mapScaleX, mapScaleY);

    float Ch_wid = 12;
    RenderPointRotate(IMAGE_MINIMAP_INTERFACE + 3, Tx, Ty, Ch_wid, Ch_wid, mapScaleX * 0.5f, mapScaleY * 0.5f, mapScaleX, mapScaleY, 0.f, 0.f, 17.5f / 32.f, 17.5f / 32.f);

    for (i = 0; i < 25; i++)
    {
        RenderImage(IMAGE_MINIMAP_INTERFACE + 2, i * Ui_wid, 0, Ui_wid, Ui_Hig, 0.f, 1.f, uvxy, -uvxy_Line);
        RenderImage(IMAGE_MINIMAP_INTERFACE + 2, i * Ui_wid, 430 - Ui_Hig, Ui_wid, Ui_Hig, 0.f, 0.f, uvxy, uvxy_Line);
    }
    for (i = 0; i < 20; i++)
    {
        RenderBitmapRotate(IMAGE_MINIMAP_INTERFACE + 2, (Ui_Hig / 2.f), i * (Ui_wid - 3.f), Ui_wid, Ui_Hig, -90.f, 0.f, 0.f, uvxy, uvxy_Line);
        RenderBitmapRotate(IMAGE_MINIMAP_INTERFACE + 2, 640 - (Ui_Hig / 2.f), i * (Ui_wid - 3.f), Ui_wid, Ui_Hig, 90.f, 0.f, 0.f, uvxy, uvxy_Line);
    }

    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, 0, 0, Ui_wid, Ui_wid, 0.f, 0.f, uvxy, uvxy);
    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, 640 - Ui_wid, 0, Ui_wid, Ui_wid, uvxy, 0.f, -uvxy, uvxy);
    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, 0, 430 - Ui_wid, Ui_wid, Ui_wid, 0.f, uvxy, uvxy, -uvxy);
    RenderImage(IMAGE_MINIMAP_INTERFACE + 1, 640 - Ui_wid, 430 - Ui_wid, Ui_wid, Ui_wid, uvxy, uvxy, -uvxy, -uvxy);

    m_BtnExit.Render(true);

    const bool cursorInMiniMap = CheckMouseIn(0, 0, 640, 430);
    const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    int coordX = (Hero != NULL) ? Hero->PositionX : 0;
    int coordY = (Hero != NULL) ? Hero->PositionY : 0;
    bool hasCursorCoords = false;

    if (cursorInMiniMap)
    {
        float worldX = 0.f;
        float worldY = 0.f;
        if (GetWorldPositionFromScreen(static_cast<float>(MouseX), static_cast<float>(MouseY), worldX, worldY))
        {
            coordX = static_cast<int>(worldX + 0.5f);
            coordY = static_cast<int>(worldY + 0.5f);
            hasCursorCoords = true;
        }
    }

    wchar_t coordBuffer[160];
    if (shiftHeld && hasCursorCoords)
    {
        swprintf(coordBuffer, L"Shift: clic izquierdo para caminar a X: %d  Y: %d  (clic derecho cancela)", coordX, coordY);
    }
    else
    {
        swprintf(coordBuffer, L"X: %d  Y: %d", coordX, coordY);
    }

    const DWORD backupTextColor = g_pRenderText->GetTextColor();
    const DWORD backupBgColor = g_pRenderText->GetBgColor();

    g_pRenderText->SetFont(g_hFont);
    g_pRenderText->SetTextColor(RGBA(255, 255, 255, 255));
    g_pRenderText->SetBgColor(RGBA(0, 0, 0, 160));
    g_pRenderText->RenderText(0, 8, coordBuffer, 640, 0, RT3_SORT_CENTER);

    g_pRenderText->SetTextColor(backupTextColor);
    g_pRenderText->SetBgColor(backupBgColor);

    DisableAlphaBlend();

    Check_Btn(MouseX, MouseY);
    return true;
}

bool SEASON3B::CNewUIMiniMap::Update()
{
    UpdateAutoWalk();
    return true;
}

void SEASON3B::CNewUIMiniMap::LoadImages(const wchar_t* Filename)
{
    CancelAutoWalk();
    m_MiniMapCount = 0;

    wchar_t Fname[300];
    int i = 0;
    swprintf(Fname, L"Data\\%s\\mini_map.ozt", Filename);
    FILE* pFile = _wfopen(Fname, L"rb");

    if (pFile == NULL)
    {
        m_bSuccess = false;
        m_MiniMapCount = 0;
        return;
    }
    else
    {
        m_bSuccess = true;
        fclose(pFile);
        swprintf(Fname, L"%s\\mini_map.tga", Filename);
        LoadBitmap(Fname, IMAGE_MINIMAP_INTERFACE, GL_LINEAR);
    }

    swprintf(Fname, L"Data\\Local\\%s\\Minimap\\Minimap_%s_%s.bmd", g_strSelectedML.c_str(), Filename, g_strSelectedML.c_str());

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
            swprintf(Text, L"%s - File corrupted.", Fname);
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
                if (target->Kind > 0)
                {
                    ++m_MiniMapCount;
                }
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
    m_bSuccess = false;
    m_MiniMapCount = 0;
}

bool SEASON3B::CNewUIMiniMap::UpdateMouseEvent()
{
    bool ret = true;

    if (m_BtnExit.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_MINI_MAP);
        return true;
    }

    const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool cursorInMiniMap = CheckMouseIn(0, 0, 640, 430);

    if (cursorInMiniMap && MouseWheel != 0)
    {
        const int maxZoomIndex = static_cast<int>((sizeof(m_Lenth) / sizeof(m_Lenth[0])) - 1);

        if (MouseWheel > 0 && m_MiniPos > 0)
        {
            --m_MiniPos;
        }
        else if (MouseWheel < 0 && m_MiniPos < maxZoomIndex)
        {
            ++m_MiniPos;
        }

        MouseWheel = 0;
    }

    if (shiftHeld && cursorInMiniMap)
    {
        if (MouseRButtonPush)
        {
            CancelAutoWalk();

            if (Hero)
            {
                PATH_t& heroPath = Hero->Path;
                heroPath.Lock.lock();
                heroPath.PathNum = 0;
                heroPath.CurrentPath = 0;
                heroPath.CurrentPathFloat = 0;
                heroPath.Lock.unlock();

                Hero->Movement = false;
                Hero->MovementType = MOVEMENT_MOVE;
                TargetX = Hero->PositionX;
                TargetY = Hero->PositionY;
            }

            MouseRButtonPush = false;
            MouseRButton = false;
            MouseRButtonPop = false;

            PlayBuffer(SOUND_CLICK01);
            return false;
        }

        if (MouseLButtonPush)
        {
            float worldX = 0.f;
            float worldY = 0.f;
            if (GetWorldPositionFromScreen(static_cast<float>(MouseX), static_cast<float>(MouseY), worldX, worldY))
            {
                StartAutoWalk(static_cast<int>(worldX + 0.5f), static_cast<int>(worldY + 0.5f));
                PlayBuffer(SOUND_CLICK01);
            }
            return false;
        }
    }

    if (IsPress(VK_LBUTTON))
    {
        ret = Check_Mouse(MouseX, MouseY);
        if (ret == false)
        {
            PlayBuffer(SOUND_CLICK01);
        }
    }

    if (cursorInMiniMap)
    {
        return false;
    }

    return ret;
}

bool SEASON3B::CNewUIMiniMap::Check_Mouse(int mx, int my)
{
    return true;
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
                SIZE Fontsize;
                m_TooltipText = (std::wstring)m_Mini_Map_Data[i].Name;
                g_pRenderText->SetFont(g_hFont);
                GetTextExtentPoint32(g_pRenderText->GetFontDC(), m_TooltipText.c_str(), m_TooltipText.size(), &Fontsize);

                Fontsize.cx = Fontsize.cx / ((float)WindowWidth / 640);
                Fontsize.cy = Fontsize.cy / ((float)WindowHeight / 480);

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

bool SEASON3B::CNewUIMiniMap::BuildAutoWalkPath(int startX, int startY, int targetX, int targetY)
{
    m_AutoWalkPreviewPath.clear();
    m_AutoWalkCurrentIndex = 0;

    if (Hero == NULL)
        return false;

    if (startX < 0 || startX > 255 || startY < 0 || startY > 255)
        return false;

    if (targetX < 0 || targetX > 255 || targetY < 0 || targetY > 255)
        return false;

    if (startX == targetX && startY == targetY)
    {
        POINT startPoint = { startX, startY };
        m_AutoWalkPreviewPath.push_back(startPoint);
        return true;
    }

    EnsureAutoWalkPathfinderReady();

    bool cryWolfValue = false;
    if (M34CryWolf1st::Get_State_Only_Elf() && M34CryWolf1st::IsCyrWolf1st())
    {
        if (TargetNpc >= 0 && TargetNpc < MAX_CHARACTERS_CLIENT)
        {
            const int objectType = CharactersClient[TargetNpc].Object.Type;
            if (objectType >= MODEL_CRYWOLF_ALTAR1 && objectType <= MODEL_CRYWOLF_ALTAR5)
            {
                cryWolfValue = true;
            }
        }
    }

    int wallFlags = TW_CHARACTER;
    bool pathFound = g_AutoWalkPathfinder.FindPath(startX, startY, targetX, targetY, true, wallFlags, cryWolfValue, 0.0f);
    if (pathFound == false)
    {
        const WORD startAttr = TerrainWall[TERRAIN_INDEX_REPEAT(startX, startY)];
        const WORD targetAttr = TerrainWall[TERRAIN_INDEX_REPEAT(targetX, targetY)];

        if (((startAttr & TW_SAFEZONE) == TW_SAFEZONE || (targetAttr & TW_SAFEZONE) == TW_SAFEZONE) &&
            (targetAttr & TW_CHARACTER) != TW_CHARACTER)
        {
            wallFlags = TW_NOMOVE;
        }

        pathFound = g_AutoWalkPathfinder.FindPath(startX, startY, targetX, targetY, false, wallFlags, cryWolfValue, 0.0f);
    }

    if (pathFound == false)
        return false;

    const int fullPathLength = g_AutoWalkPathfinder.GetFullPathLength();
    if (fullPathLength <= 1)
        return false;

    unsigned char* px = g_AutoWalkPathfinder.GetPathX();
    unsigned char* py = g_AutoWalkPathfinder.GetPathY();

    m_AutoWalkPreviewPath.reserve(fullPathLength);
    for (int i = 0; i < fullPathLength; ++i)
    {
        POINT node = { px[i], py[i] };
        m_AutoWalkPreviewPath.push_back(node);
    }

    const POINT& first = m_AutoWalkPreviewPath.front();
    const POINT& last = m_AutoWalkPreviewPath.back();
    if (first.x != startX || first.y != startY || last.x != targetX || last.y != targetY)
    {
        m_AutoWalkPreviewPath.clear();
        return false;
    }

    return true;
}

bool SEASON3B::CNewUIMiniMap::SyncAutoWalkIndexWithHero()
{
    if (m_AutoWalkPreviewPath.empty() || Hero == NULL)
        return false;

    const int heroX = Hero->PositionX;
    const int heroY = Hero->PositionY;

    if (m_AutoWalkCurrentIndex >= 0 && m_AutoWalkCurrentIndex < static_cast<int>(m_AutoWalkPreviewPath.size()))
    {
        const POINT& currentNode = m_AutoWalkPreviewPath[m_AutoWalkCurrentIndex];
        if (currentNode.x == heroX && currentNode.y == heroY)
        {
            return true;
        }
    }

    for (size_t i = 0; i < m_AutoWalkPreviewPath.size(); ++i)
    {
        const POINT& node = m_AutoWalkPreviewPath[i];
        if (node.x == heroX && node.y == heroY)
        {
            m_AutoWalkCurrentIndex = static_cast<int>(i);
            return true;
        }
    }

    return false;
}

bool SEASON3B::CNewUIMiniMap::GetWorldPositionFromScreen(float screenX, float screenY, float& worldX, float& worldY) const
{
    if (m_bSuccess == false || Hero == NULL)
        return false;

    const float mapWidth = static_cast<float>(m_Lenth[m_MiniPos].x);
    const float mapHeight = static_cast<float>(m_Lenth[m_MiniPos].y);

    if (mapWidth <= 0.f || mapHeight <= 0.f)
        return false;

    const float mapCenterX = mapWidth * 0.5f;
    const float mapCenterY = mapHeight * 0.5f;
    const float windowWidthF = static_cast<float>(WindowWidth);
    const float windowHeightF = static_cast<float>(WindowHeight);

    if (windowWidthF <= 0.f || windowHeightF <= 0.f)
        return false;

    vec3_t angle = { 0.f, 0.f, 0.f };
    float rotationMatrix[3][4];
    AngleMatrix(angle, rotationMatrix);

    const float actualX = screenX * windowWidthF / 640.f;
    const float actualYFromBottom = (480.f - screenY) * windowHeightF / 480.f;

    const float mapCenterOffsetX = 25.f;
    const float mapCenterOffsetY = 0.f;

    vec3_t cursorVector =
    {
        (actualX - (windowWidthF * 0.5f)) - mapCenterOffsetX,
        (actualYFromBottom - (windowHeightF * 0.5f)) - mapCenterOffsetY,
        0.f
    };

    vec3_t unrotatedVector;
    VectorRotate(cursorVector, rotationMatrix, unrotatedVector);

    const float mapDiffX = unrotatedVector[0] * (640.f / windowWidthF);
    const float mapDiffY = unrotatedVector[1] * (480.f / windowHeightF);

    const float mapX = mapCenterX + mapDiffX;
    const float mapY = mapCenterY - mapDiffY;

    float computedWorldX = (mapY / mapHeight) * 256.f;
    float computedWorldY = (mapX / mapWidth) * 256.f;

    if (computedWorldX < 0.f) computedWorldX = 0.f;
    if (computedWorldX > 255.f) computedWorldX = 255.f;
    if (computedWorldY < 0.f) computedWorldY = 0.f;
    if (computedWorldY > 255.f) computedWorldY = 255.f;

    worldX = computedWorldX;
    worldY = computedWorldY;
    return true;
}

void SEASON3B::CNewUIMiniMap::StartAutoWalk(int targetX, int targetY)
{
    if (Hero == NULL)
        return;

    CancelAutoWalk();

    if (targetX < 0) targetX = 0;
    else if (targetX > 255) targetX = 255;
    if (targetY < 0) targetY = 0;
    else if (targetY > 255) targetY = 255;

    if (Hero->PositionX == targetX && Hero->PositionY == targetY)
        return;

    if (BuildAutoWalkPath(Hero->PositionX, Hero->PositionY, targetX, targetY) == false)
    {
        if (g_pSystemLogBox)
        {
            g_pSystemLogBox->AddText(AUTO_WALK_FAIL_MESSAGE, SEASON3B::TYPE_ERROR_MESSAGE);
        }
        return;
    }

    m_AutoWalkTarget.x = targetX;
    m_AutoWalkTarget.y = targetY;
    m_bAutoWalkActive = true;
    TargetX = targetX;
    TargetY = targetY;

    SyncAutoWalkIndexWithHero();

    if (Hero->Movement == false)
    {
        TryAutoWalkStep();
    }
}

void SEASON3B::CNewUIMiniMap::CancelAutoWalk()
{
    m_bAutoWalkActive = false;
    m_AutoWalkPreviewPath.clear();
    m_AutoWalkCurrentIndex = 0;
}

void SEASON3B::CNewUIMiniMap::UpdateAutoWalk()
{
    if (m_bAutoWalkActive == false || Hero == NULL)
        return;

    if (Hero->Dead > 0)
    {
        CancelAutoWalk();
        return;
    }

    SyncAutoWalkIndexWithHero();

    if (Hero->PositionX == m_AutoWalkTarget.x && Hero->PositionY == m_AutoWalkTarget.y)
    {
        CancelAutoWalk();
        return;
    }

    if (m_AutoWalkPreviewPath.size() <= 1)
    {
        CancelAutoWalk();
        return;
    }

    if (Hero->Movement)
    {
        if (ShouldPrefetchAutoWalkPath())
        {
            if (TryAutoWalkStep() == false)
            {
                CancelAutoWalk();
            }
        }
        return;
    }

    if (SyncAutoWalkIndexWithHero() == false)
    {
        if (BuildAutoWalkPath(Hero->PositionX, Hero->PositionY, m_AutoWalkTarget.x, m_AutoWalkTarget.y) == false)
        {
            if (g_pSystemLogBox)
            {
                g_pSystemLogBox->AddText(AUTO_WALK_FAIL_MESSAGE, SEASON3B::TYPE_ERROR_MESSAGE);
            }
            CancelAutoWalk();
            return;
        }
    }

    if (TryAutoWalkStep() == false)
    {
        CancelAutoWalk();
    }
}

bool SEASON3B::CNewUIMiniMap::TryAutoWalkStep()
{
    if (m_bAutoWalkActive == false || Hero == NULL)
        return false;

    if (g_pNewUISystem && g_pNewUISystem->IsImpossibleSendMoveInterface())
    {
        return false;
    }

    if (SyncAutoWalkIndexWithHero() == false)
    {
        return false;
    }

    const size_t totalNodes = m_AutoWalkPreviewPath.size();
    if (totalNodes <= 1 || m_AutoWalkCurrentIndex >= static_cast<int>(totalNodes - 1))
    {
        return false;
    }

    size_t nodesAvailable = totalNodes - static_cast<size_t>(m_AutoWalkCurrentIndex);
    if (nodesAvailable < 2)
    {
        return false;
    }

    if (nodesAvailable > MAX_PATH_FIND)
    {
        nodesAvailable = MAX_PATH_FIND;
    }

    PATH_t& heroPath = Hero->Path;
    heroPath.Lock.lock();
    heroPath.PathNum = static_cast<int>(nodesAvailable);
    heroPath.CurrentPath = 0;
    heroPath.CurrentPathFloat = 0;

    for (size_t i = 0; i < nodesAvailable; ++i)
    {
        const POINT& node = m_AutoWalkPreviewPath[m_AutoWalkCurrentIndex + i];
        heroPath.PathX[i] = static_cast<unsigned char>(node.x);
        heroPath.PathY[i] = static_cast<unsigned char>(node.y);
    }
    heroPath.Lock.unlock();

    Hero->MovementType = MOVEMENT_MOVE;
    TargetX = m_AutoWalkTarget.x;
    TargetY = m_AutoWalkTarget.y;

    SendMove(Hero, &Hero->Object);
    return true;
}

bool SEASON3B::CNewUIMiniMap::ShouldPrefetchAutoWalkPath() const
{
    if (Hero == NULL)
        return false;

    if (m_AutoWalkPreviewPath.size() <= 1)
        return false;

    PATH_t& heroPath = Hero->Path;
    heroPath.Lock.lock();
    const int totalNodesInChunk = heroPath.PathNum;
    const int currentNodeInChunk = heroPath.CurrentPath;
    heroPath.Lock.unlock();

    if (totalNodesInChunk <= 1)
        return false;

    const int transitionsRemaining = (totalNodesInChunk - 1) - currentNodeInChunk;
    if (transitionsRemaining > AUTO_WALK_PREFETCH_THRESHOLD)
        return false;
    if (transitionsRemaining < 0)
        return false;

    if (m_AutoWalkCurrentIndex < 0)
        return false;

    const int chunkStartIndex = m_AutoWalkCurrentIndex - currentNodeInChunk;
    if (chunkStartIndex < 0)
        return false;

    const size_t chunkEndIndex = static_cast<size_t>(chunkStartIndex + totalNodesInChunk - 1);
    if (chunkEndIndex + 1 >= m_AutoWalkPreviewPath.size())
        return false;

    return true;
}

void SEASON3B::CNewUIMiniMap::RenderAutoWalkPath(float centerX, float centerY, float mapWidth, float mapHeight, float rotation)
{
    const size_t totalNodes = m_AutoWalkPreviewPath.size();
    if (totalNodes <= 1)
        return;

    const float markerSize = 10.f;
    const float uv = 17.5f / 32.f;

    size_t startIndex = 1;
    if (m_AutoWalkCurrentIndex >= 0 && m_AutoWalkCurrentIndex < static_cast<int>(totalNodes))
    {
        startIndex = static_cast<size_t>(m_AutoWalkCurrentIndex + 1);
    }

    if (startIndex >= totalNodes)
        return;

    for (size_t i = startIndex; i < totalNodes; ++i)
    {
        const POINT& pt = m_AutoWalkPreviewPath[i];
        float ty = ((float)pt.x / 256.f) * mapHeight;
        float tx = ((float)pt.y / 256.f) * mapWidth;
        const bool isDestination = (i == totalNodes - 1);
        const float size = isDestination ? (markerSize + 6.f) : markerSize;
        const int imageId = isDestination ? (IMAGE_MINIMAP_INTERFACE + 4) : (IMAGE_MINIMAP_INTERFACE + 5);
        RenderPointRotate(imageId, tx, ty, size, size, centerX, centerY, mapWidth, mapHeight, rotation, 0.f, uv, uv);
    }
}

bool SEASON3B::CNewUIMiniMap::HasValidMiniMap() const
{
    return m_bSuccess;
}

const MINI_MAP* SEASON3B::CNewUIMiniMap::GetMiniMapData() const
{
    return m_Mini_Map_Data;
}

int SEASON3B::CNewUIMiniMap::GetMiniMapDataCount() const
{
    return m_MiniMapCount;
}

int SEASON3B::CNewUIMiniMap::GetMiniMapTextureId() const
{
    return IMAGE_MINIMAP_INTERFACE;
}

CNewUIFieldMiniMap::CNewUIFieldMiniMap()
{
    m_pNewUIMng = nullptr;
    m_pMiniMap = nullptr;
}

CNewUIFieldMiniMap::~CNewUIFieldMiniMap()
{
    Release();
}

bool CNewUIFieldMiniMap::Create(CNewUIManager* pNewUIMng, CNewUIMiniMap* pMiniMap)
{
    if (pNewUIMng == nullptr || pMiniMap == nullptr)
    {
        return false;
    }

    m_pNewUIMng = pNewUIMng;
    m_pMiniMap = pMiniMap;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_FIELD_MINI_MAP, this);
    Show(true);
    Enable(true);
    return true;
}

void CNewUIFieldMiniMap::Release()
{
    if (m_pNewUIMng != nullptr)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
    m_pMiniMap = nullptr;
}

bool CNewUIFieldMiniMap::UpdateMouseEvent()
{
    return true;
}

bool CNewUIFieldMiniMap::UpdateKeyEvent()
{
    return true;
}

bool CNewUIFieldMiniMap::Update()
{
    return true;
}

bool CNewUIFieldMiniMap::Render()
{
    if (m_pMiniMap == nullptr)
    {
        return true;
    }

    const float frameX = GetFrameX();
    const float frameY = GetFrameY();
    const float frameSize = GetMapSize();
    const float mapPadding = FIELD_MINIMAP_PADDING;
    const float mapSize = frameSize - (mapPadding * 2.f);
    const float mapX = frameX + mapPadding;
    const float mapY = frameY + mapPadding;

    EnableAlphaTest();
    RenderMiniMapFrame(frameX, frameY, frameSize, frameSize);
    EndRenderColor();
    glColor4f(1.f, 1.f, 1.f, 1.f);
    EnableAlphaTest();

    if (m_pMiniMap->HasValidMiniMap())
    {
        RenderMiniMapContents(mapX, mapY, mapSize);
        RenderMarkers(mapX, mapY, mapSize);
        {
            FieldMiniMapTransformGuard transform(mapX, mapY, mapSize);
            RenderDetectedEntities(mapSize * 0.5f, mapSize * 0.5f, mapSize, mapSize, FIELD_MINIMAP_WORLD_OFFSET_X);
        }
        RenderHeroMarker(mapX, mapY, mapSize);
    }
    else
    {
        RenderUnavailableText(mapX, mapY, mapSize, mapSize);
    }

    DisableAlphaBlend();

    return true;
}

float CNewUIFieldMiniMap::GetLayerDepth()
{
    return 1.15f;
}

void CNewUIFieldMiniMap::OpenningProcess()
{
    Show(true);
}

void CNewUIFieldMiniMap::ClosingProcess()
{
    Show(false);
}

void CNewUIFieldMiniMap::RenderMiniMapFrame(float frameX, float frameY, float frameWidth, float frameHeight) const
{
    (void)frameX;
    (void)frameY;
    (void)frameWidth;
    (void)frameHeight;
}

void CNewUIFieldMiniMap::RenderMiniMapContents(float mapX, float mapY, float mapSize) const
{
    FieldMiniMapTransformGuard transform(mapX, mapY, mapSize);
    const float mapScale = mapSize;
    const float centerCoord = mapScale * 0.5f;
    glColor4f(1.f, 1.f, 1.f, FIELD_MINIMAP_MAP_ALPHA);
    RenderBitRotate(CNewUIMiniMap::IMAGE_MINIMAP_INTERFACE, centerCoord, centerCoord, mapScale, mapScale, 0.f);
    glColor4f(1.f, 1.f, 1.f, 1.f);
}

void CNewUIFieldMiniMap::RenderHeroMarker(float mapX, float mapY, float mapSize) const
{
    if (Hero == NULL)
    {
        return;
    }

    FieldMiniMapTransformGuard transform(mapX, mapY, mapSize);

    const float mapScaleX = mapSize;
    const float mapScaleY = mapSize;
    const float heroTy = (static_cast<float>(Hero->PositionX) / 256.f) * mapScaleY;
    float heroTx = (static_cast<float>(Hero->PositionY) / 256.f) * mapScaleX;
    heroTx += FIELD_MINIMAP_WORLD_OFFSET_X;
    const float centerCoordX = mapScaleX * 0.5f;
    const float centerCoordY = mapScaleY * 0.5f;
    const float iconSize = 12.f;
    RenderPointRotate(CNewUIMiniMap::IMAGE_MINIMAP_INTERFACE + 3, heroTx, heroTy, iconSize, iconSize, centerCoordX, centerCoordY, mapScaleX, mapScaleY, 0.f, 0.f, 17.5f / 32.f, 17.5f / 32.f);
}

void CNewUIFieldMiniMap::RenderMarkers(float mapX, float mapY, float mapSize) const
{
    const MINI_MAP* data = m_pMiniMap->GetMiniMapData();
    int markerCount = m_pMiniMap->GetMiniMapDataCount();
    if (markerCount > MAX_MINI_MAP_DATA)
    {
        markerCount = MAX_MINI_MAP_DATA;
    }

    if (markerCount <= 0)
    {
        return;
    }

    FieldMiniMapTransformGuard transform(mapX, mapY, mapSize);

    const float mapScaleX = mapSize;
    const float mapScaleY = mapSize;
    const float centerCoordX = mapScaleX * 0.5f;
    const float centerCoordY = mapScaleY * 0.5f;

    for (int i = 0; i < markerCount; ++i)
    {
        const MINI_MAP& entry = data[i];
        if (entry.Kind <= 0)
        {
            continue;
        }

        const float markerTy = (static_cast<float>(entry.Location[0]) / 256.f) * mapScaleY;
        float markerTx = (static_cast<float>(entry.Location[1]) / 256.f) * mapScaleX;
        markerTx += FIELD_MINIMAP_WORLD_OFFSET_X;

        int textureId = CNewUIMiniMap::IMAGE_MINIMAP_INTERFACE + 5;
        float iconSize = 10.f;
        if (entry.Kind == 2)
        {
            textureId = CNewUIMiniMap::IMAGE_MINIMAP_INTERFACE + 4;
            iconSize = 12.f;
        }

        RenderPointRotate(textureId, markerTx, markerTy, iconSize, iconSize, centerCoordX, centerCoordY, mapScaleX, mapScaleY, 0.f, static_cast<float>(entry.Rotation), 17.5f / 32.f, 17.5f / 32.f);
    }
}

void CNewUIFieldMiniMap::RenderUnavailableText(float frameX, float frameY, float frameWidth, float frameHeight) const
{
    const wchar_t unavailableText[] = L"Minimapa no disponible";
    const DWORD backupTextColor = g_pRenderText->GetTextColor();
    const DWORD backupBgColor = g_pRenderText->GetBgColor();

    g_pRenderText->SetFont(g_hFont);
    g_pRenderText->SetTextColor(RGBA(255, 220, 220, 255));
    g_pRenderText->SetBgColor(RGBA(0, 0, 0, 160));
    g_pRenderText->RenderText(frameX + 10.f, frameY + (frameHeight * 0.5f) - 8.f, unavailableText, frameWidth - 20.f, 0, RT3_SORT_CENTER);

    g_pRenderText->SetTextColor(backupTextColor);
    g_pRenderText->SetBgColor(backupBgColor);
}

float CNewUIFieldMiniMap::GetFrameX() const
{
    return 640.f - FIELD_MINIMAP_MARGIN - GetMapSize();
}

float CNewUIFieldMiniMap::GetFrameY() const
{
    return 480.f - FIELD_MINIMAP_MARGIN - GetMapSize() - FIELD_MINIMAP_VERTICAL_OFFSET;
}

float CNewUIFieldMiniMap::GetMapSize() const
{
    return FIELD_MINIMAP_SIZE;
}
