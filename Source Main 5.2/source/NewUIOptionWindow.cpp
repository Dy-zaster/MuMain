// NewUIOptionWindow.cpp: implementation of the CNewUIOptionWindow class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "NewUIOptionWindow.h"
#include "NewUISystem.h"
#include "ZzzTexture.h"
#include "DSPlaySound.h"
#include "ZzzScene.h"
#include "ZzzOpenglUtil.h"
#include "Winmain.h"
#include "./ExternalObject/leaf/regkey.h"
#include "ResolutionOptions.h"
#include <array>
#include <cmath>

using namespace SEASON3B;

namespace
{
    constexpr float kOptionWindowWidth = 240.f;
    constexpr float kOptionWindowHeight = 320.f;
    constexpr int kSectionLineInset = 18;
    constexpr int kGeneralTextStartOffset = 64;
    constexpr int kGraphicsTextStartOffset = 64;
    constexpr int kRowSpacing = 22;
    constexpr int kLeftPointOffsetX = 20;
    constexpr int kLeftTextOffsetX = 40;
    constexpr int kLeftCheckboxOffsetX = 130;
    constexpr int kRightPointOffsetX = 140;
    constexpr int kRightTextOffsetX = 160;
    constexpr int kRightCheckboxOffsetX = 210;
    constexpr int kCheckboxSize = 15;
    constexpr int kVolumeTextOffsetY = 136;
    constexpr int kVolumeSliderOffsetY = 152;
    constexpr int kRenderTextOffsetY = 188;
    constexpr int kRenderSliderOffsetY = 206;
    constexpr int kResolutionAreaOffsetY = 242;
    constexpr int kResolutionAreaWidth = 170;
    constexpr int kResolutionAreaHeight = 32;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

SEASON3B::CNewUIOptionWindow::CNewUIOptionWindow()
{
    m_pNewUIMng = NULL;
    m_Pos.x = 0;
    m_Pos.y = 0;

    m_bAutoAttack = true;
    m_bWhisperSound = false;
    m_bSlideHelp = true;
    m_iVolumeLevel = 0;
    m_iRenderLevel = 4;
    m_bRenderAllEffects = true;
    m_bShowFPSCounter = false;
    m_bVerticalSync = IsVSyncEnabled();
    m_bShowMonsterHPBar = true;
    m_iResolutionIndex = 1;
}

SEASON3B::CNewUIOptionWindow::~CNewUIOptionWindow()
{
    Release();
}

bool SEASON3B::CNewUIOptionWindow::Create(CNewUIManager* pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_OPTION, this);
    SetPos(x, y);
    LoadImages();
    SetButtonInfo();
    Show(false);
    return true;
}

void SEASON3B::CNewUIOptionWindow::SetButtonInfo()
{
    m_BtnClose.ChangeTextBackColor(RGBA(255, 255, 255, 0));
    m_BtnClose.ChangeButtonImgState(true, IMAGE_OPTION_BTN_CLOSE, true);
    m_BtnClose.ChangeButtonInfo(
        m_Pos.x + static_cast<int>((kOptionWindowWidth - 54.f) * 0.5f),
        m_Pos.y + static_cast<int>(kOptionWindowHeight) - 55,
        54,
        30);
    m_BtnClose.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
    m_BtnClose.ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
}

void SEASON3B::CNewUIOptionWindow::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void SEASON3B::CNewUIOptionWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool SEASON3B::CNewUIOptionWindow::UpdateMouseEvent()
{
    if (m_BtnClose.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_OPTION);
        return false;
    }

    auto isMouseInRect = [](const RECT& rect)
    {
        return CheckMouseIn(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    };

    if (SEASON3B::IsPress(VK_LBUTTON) && isMouseInRect(GetCheckboxRect(CheckboxId::AutoAttack)))
    {
        SetAutoAttack(!IsAutoAttack());
    }
    if (SEASON3B::IsPress(VK_LBUTTON) && isMouseInRect(GetCheckboxRect(CheckboxId::WhisperSound)))
    {
        SetWhisperSound(!IsWhisperSound());
    }
    if (SEASON3B::IsPress(VK_LBUTTON) && isMouseInRect(GetCheckboxRect(CheckboxId::SlideHelp)))
    {
        SetSlideHelp(!IsSlideHelp());
    }

    if (SEASON3B::IsPress(VK_LBUTTON) && isMouseInRect(GetCheckboxRect(CheckboxId::RenderFullEffects)))
    {
        SetRenderAllEffects(!GetRenderAllEffects());
    }
    if (SEASON3B::IsPress(VK_LBUTTON) && isMouseInRect(GetCheckboxRect(CheckboxId::MonsterHP)))
    {
        SetShowMonsterHPBar(!IsMonsterHpBarEnabled());
    }
    if (SEASON3B::IsPress(VK_LBUTTON) && isMouseInRect(GetCheckboxRect(CheckboxId::ShowFPS)))
    {
        SetShowFPSCounter(!IsShowFPSCounter());
    }
    if (IsVSyncAvailable() && SEASON3B::IsPress(VK_LBUTTON) && isMouseInRect(GetCheckboxRect(CheckboxId::VerticalSync)))
    {
        SetVerticalSync(!IsVerticalSyncEnabled());
    }

    const RECT resolutionRect = GetResolutionRect();
    if (isMouseInRect(resolutionRect))
    {
        if (SEASON3B::IsPress(VK_LBUTTON))
        {
            CycleResolution(1);
        }
        else if (SEASON3B::IsPress(VK_RBUTTON))
        {
            CycleResolution(-1);
        }
    }

    RECT volumeSlider = GetVolumeSliderRect();
    RECT volumeHitRect = volumeSlider;
    volumeHitRect.left -= 8;
    volumeHitRect.right += 8;
    if (CheckMouseIn(volumeHitRect.left, volumeHitRect.top, volumeHitRect.right - volumeHitRect.left, volumeHitRect.bottom - volumeHitRect.top))
    {
        int iOldValue = m_iVolumeLevel;
        if (MouseWheel > 0)
        {
            MouseWheel = 0;
            ++m_iVolumeLevel;
            if (m_iVolumeLevel > 10)
            {
                m_iVolumeLevel = 10;
            }
        }
        else if (MouseWheel < 0)
        {
            MouseWheel = 0;
            --m_iVolumeLevel;
            if (m_iVolumeLevel < 0)
            {
                m_iVolumeLevel = 0;
            }
        }
        if (SEASON3B::IsRepeat(VK_LBUTTON))
        {
            int x = MouseX - volumeSlider.left;
            if (x < 0)
            {
                m_iVolumeLevel = 0;
            }
            else
            {
                float fValue = (10.f * x) / 124.f;
                m_iVolumeLevel = static_cast<int>(fValue) + 1;
            }
        }

        if (iOldValue != m_iVolumeLevel)
        {
            SetEffectVolumeLevel(m_iVolumeLevel);
        }
    }

    const RECT renderSlider = GetRenderSliderRect();
    if (CheckMouseIn(renderSlider.left, renderSlider.top, renderSlider.right - renderSlider.left, renderSlider.bottom - renderSlider.top))
    {
        if (SEASON3B::IsRepeat(VK_LBUTTON))
        {
            int x = MouseX - renderSlider.left;
            float fValue = (5.f * x) / 141.f;
            m_iRenderLevel = static_cast<int>(fValue);
        }
    }

    if (CheckMouseIn(m_Pos.x, m_Pos.y, static_cast<int>(kOptionWindowWidth), static_cast<int>(kOptionWindowHeight)) == true)
    {
        return false;
    }

    return true;
}

bool SEASON3B::CNewUIOptionWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_OPTION) == true)
    {
        if (SEASON3B::IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_OPTION);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }

    return true;
}

bool SEASON3B::CNewUIOptionWindow::Update()
{
    return true;
}

bool SEASON3B::CNewUIOptionWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderFrame();
    RenderContents();
    RenderButtons();
    DisableAlphaBlend();
    return true;
}

float SEASON3B::CNewUIOptionWindow::GetLayerDepth()	//. 10.5f
{
    return 10.5f;
}

float SEASON3B::CNewUIOptionWindow::GetKeyEventOrder()	// 10.f;
{
    return 10.0f;
}

void SEASON3B::CNewUIOptionWindow::OpenningProcess()
{
}

void SEASON3B::CNewUIOptionWindow::ClosingProcess()
{
}

void SEASON3B::CNewUIOptionWindow::LoadImages()
{
    LoadBitmap(L"Interface\\newui_button_close.tga", IMAGE_OPTION_BTN_CLOSE, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_msgbox_back.jpg", IMAGE_OPTION_FRAME_BACK, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_item_back03.tga", IMAGE_OPTION_FRAME_DOWN, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_option_top.tga", IMAGE_OPTION_FRAME_UP, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_option_back06(L).tga", IMAGE_OPTION_FRAME_LEFT, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_option_back06(R).tga", IMAGE_OPTION_FRAME_RIGHT, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_option_line.jpg", IMAGE_OPTION_LINE, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_option_point.tga", IMAGE_OPTION_POINT, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_option_check.tga", IMAGE_OPTION_BTN_CHECK, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_option_effect03.tga", IMAGE_OPTION_EFFECT_BACK, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_option_effect04.tga", IMAGE_OPTION_EFFECT_COLOR, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_option_volume01.tga", IMAGE_OPTION_VOLUME_BACK, GL_LINEAR);
    LoadBitmap(L"Interface\\newui_option_volume02.tga", IMAGE_OPTION_VOLUME_COLOR, GL_LINEAR);
}

void SEASON3B::CNewUIOptionWindow::UnloadImages()
{
    DeleteBitmap(IMAGE_OPTION_BTN_CLOSE);
    DeleteBitmap(IMAGE_OPTION_FRAME_BACK);
    DeleteBitmap(IMAGE_OPTION_FRAME_DOWN);
    DeleteBitmap(IMAGE_OPTION_FRAME_UP);
    DeleteBitmap(IMAGE_OPTION_FRAME_LEFT);
    DeleteBitmap(IMAGE_OPTION_FRAME_RIGHT);
    DeleteBitmap(IMAGE_OPTION_LINE);
    DeleteBitmap(IMAGE_OPTION_POINT);
    DeleteBitmap(IMAGE_OPTION_BTN_CHECK);
    DeleteBitmap(IMAGE_OPTION_EFFECT_BACK);
    DeleteBitmap(IMAGE_OPTION_EFFECT_COLOR);
    DeleteBitmap(IMAGE_OPTION_VOLUME_BACK);
    DeleteBitmap(IMAGE_OPTION_VOLUME_COLOR);
}

void SEASON3B::CNewUIOptionWindow::RenderFrame()
{
    float x, y;
    x = m_Pos.x;
    y = m_Pos.y;
    RenderImage(IMAGE_OPTION_FRAME_BACK, x, y, kOptionWindowWidth, kOptionWindowHeight);
    RenderImage(IMAGE_OPTION_FRAME_UP, x, y, kOptionWindowWidth, 64.f);
    y += 64.f;

    const float bodyHeight = kOptionWindowHeight - 64.f - 45.f;
    const int fillCount = static_cast<int>(std::ceil(bodyHeight / 10.f));
    for (int i = 0; i < fillCount; ++i)
    {
        RenderImage(IMAGE_OPTION_FRAME_LEFT, x, y, 21.f, 10.f);
        RenderImage(IMAGE_OPTION_FRAME_RIGHT, x + kOptionWindowWidth - 21.f, y, 21.f, 10.f);
        y += 10.f;
    }
    RenderImage(IMAGE_OPTION_FRAME_DOWN, x, m_Pos.y + kOptionWindowHeight - 45.f, kOptionWindowWidth, 45.f);

    const float lineWidth = kOptionWindowWidth - (kSectionLineInset * 2.f);
    const std::array<float, 4> lineOffsets = { 60.f, 130.f, 190.f, 260.f };
    for (float offset : lineOffsets)
    {
        RenderImage(IMAGE_OPTION_LINE, x + kSectionLineInset, m_Pos.y + offset, lineWidth, 2.f);
    }
}

void SEASON3B::CNewUIOptionWindow::RenderContents()
{
    g_pRenderText->SetFont(g_hFont);
    g_pRenderText->SetTextColor(255, 255, 255, 255);
    g_pRenderText->SetBgColor(0);

    g_pRenderText->RenderText(m_Pos.x + kLeftTextOffsetX, m_Pos.y + 32, L"General");
    g_pRenderText->RenderText(m_Pos.x + kRightTextOffsetX, m_Pos.y + 32, L"Graphics");

    const wchar_t* generalTexts[] = { GlobalText[386], GlobalText[387], GlobalText[919] };
    for (int i = 0; i < static_cast<int>(sizeof(generalTexts) / sizeof(generalTexts[0])); ++i)
    {
        const int textY = m_Pos.y + kGeneralTextStartOffset + i * kRowSpacing;
        RenderImage(IMAGE_OPTION_POINT, m_Pos.x + kLeftPointOffsetX, static_cast<float>(textY - 4), 10.f, 10.f);
        g_pRenderText->RenderText(m_Pos.x + kLeftTextOffsetX, textY, generalTexts[i]);
    }

    g_pRenderText->RenderText(m_Pos.x + kLeftTextOffsetX, m_Pos.y + kVolumeTextOffsetY, GlobalText[389]);
    g_pRenderText->RenderText(m_Pos.x + kLeftTextOffsetX, m_Pos.y + kRenderTextOffsetY, GlobalText[1840]);

    const wchar_t* graphicsTexts[] =
    {
        L"Render Full Effects",
        L"Show Monster HP Bars",
        L"Show FPS Counter",
        IsVSyncAvailable() ? L"Vertical Sync" : L"Vertical Sync (Unavailable)"
    };

    for (int i = 0; i < static_cast<int>(sizeof(graphicsTexts) / sizeof(graphicsTexts[0])); ++i)
    {
        const int textY = m_Pos.y + kGraphicsTextStartOffset + i * kRowSpacing;
        RenderImage(IMAGE_OPTION_POINT, m_Pos.x + kRightPointOffsetX, static_cast<float>(textY - 4), 10.f, 10.f);
        g_pRenderText->RenderText(m_Pos.x + kRightTextOffsetX, textY, graphicsTexts[i]);
    }

    wchar_t resolutionText[64];
    FormatResolutionText(resolutionText, sizeof(resolutionText) / sizeof(resolutionText[0]));
    const RECT resolutionRect = GetResolutionRect();
    g_pRenderText->RenderText(resolutionRect.left, resolutionRect.top, resolutionText);
    g_pRenderText->RenderText(resolutionRect.left, resolutionRect.top + 14, L"Left click: next  Right click: previous");
}

void SEASON3B::CNewUIOptionWindow::RenderButtons()
{
    m_BtnClose.Render();

    const RECT volumeSlider = GetVolumeSliderRect();
    RenderImage(IMAGE_OPTION_VOLUME_BACK, static_cast<float>(volumeSlider.left), static_cast<float>(volumeSlider.top), 124.f, 16.f);
    if (m_iVolumeLevel > 0)
    {
        RenderImage(IMAGE_OPTION_VOLUME_COLOR, static_cast<float>(volumeSlider.left), static_cast<float>(volumeSlider.top), 124.f * 0.1f * (m_iVolumeLevel), 16.f);
    }

    const RECT renderSlider = GetRenderSliderRect();
    RenderImage(IMAGE_OPTION_EFFECT_BACK, static_cast<float>(renderSlider.left), static_cast<float>(renderSlider.top), 141.f, 29.f);
    if (m_iRenderLevel >= 0)
    {
        RenderImage(IMAGE_OPTION_EFFECT_COLOR, static_cast<float>(renderSlider.left), static_cast<float>(renderSlider.top), 141.f * 0.2f * (m_iRenderLevel + 1), 29.f);
    }

    RenderCheckbox(GetCheckboxRect(CheckboxId::AutoAttack), m_bAutoAttack);
    RenderCheckbox(GetCheckboxRect(CheckboxId::WhisperSound), m_bWhisperSound);
    RenderCheckbox(GetCheckboxRect(CheckboxId::SlideHelp), m_bSlideHelp);
    RenderCheckbox(GetCheckboxRect(CheckboxId::RenderFullEffects), m_bRenderAllEffects);
    RenderCheckbox(GetCheckboxRect(CheckboxId::MonsterHP), m_bShowMonsterHPBar);
    RenderCheckbox(GetCheckboxRect(CheckboxId::ShowFPS), m_bShowFPSCounter);

    const bool bAllowVSync = IsVSyncAvailable();
    RenderCheckbox(GetCheckboxRect(CheckboxId::VerticalSync), m_bVerticalSync && bAllowVSync);
}

void SEASON3B::CNewUIOptionWindow::SetAutoAttack(bool bAuto)
{
    m_bAutoAttack = bAuto;
}

bool SEASON3B::CNewUIOptionWindow::IsAutoAttack()
{
    return m_bAutoAttack;
}

void SEASON3B::CNewUIOptionWindow::SetWhisperSound(bool bSound)
{
    m_bWhisperSound = bSound;
}

bool SEASON3B::CNewUIOptionWindow::IsWhisperSound()
{
    return m_bWhisperSound;
}

void SEASON3B::CNewUIOptionWindow::SetSlideHelp(bool bHelp)
{
    m_bSlideHelp = bHelp;
}

bool SEASON3B::CNewUIOptionWindow::IsSlideHelp()
{
    return m_bSlideHelp;
}

void SEASON3B::CNewUIOptionWindow::SetVolumeLevel(int iVolume)
{
    m_iVolumeLevel = iVolume;
}

int SEASON3B::CNewUIOptionWindow::GetVolumeLevel()
{
    return m_iVolumeLevel;
}

void SEASON3B::CNewUIOptionWindow::SetRenderLevel(int iRender)
{
    m_iRenderLevel = iRender;
}

int SEASON3B::CNewUIOptionWindow::GetRenderLevel()
{
    return m_iRenderLevel;
}

void SEASON3B::CNewUIOptionWindow::SetRenderAllEffects(bool bRenderAllEffects)
{
    m_bRenderAllEffects = bRenderAllEffects;
}

bool SEASON3B::CNewUIOptionWindow::GetRenderAllEffects()
{
    return m_bRenderAllEffects;
}

void SEASON3B::CNewUIOptionWindow::SetShowFPSCounter(bool bShowFPS)
{
    m_bShowFPSCounter = bShowFPS;
}

bool SEASON3B::CNewUIOptionWindow::IsShowFPSCounter() const
{
    return m_bShowFPSCounter;
}

void SEASON3B::CNewUIOptionWindow::SetVerticalSync(bool bEnabled)
{
    if (!IsVSyncAvailable())
    {
        m_bVerticalSync = false;
        return;
    }

    if (bEnabled)
    {
        EnableVSync();
        SetTargetFps(-1);
    }
    else
    {
        DisableVSync();
        SetTargetFps(60.0);
    }

    m_bVerticalSync = IsVSyncEnabled();
}

bool SEASON3B::CNewUIOptionWindow::IsVerticalSyncEnabled() const
{
    return m_bVerticalSync && IsVSyncAvailable();
}

void SEASON3B::CNewUIOptionWindow::SetShowMonsterHPBar(bool bShowHP)
{
    m_bShowMonsterHPBar = bShowHP;
}

bool SEASON3B::CNewUIOptionWindow::IsMonsterHpBarEnabled() const
{
    return m_bShowMonsterHPBar;
}

void SEASON3B::CNewUIOptionWindow::SetResolutionIndex(int index)
{
    m_iResolutionIndex = NormalizeResolutionIndex(index);
}

int SEASON3B::CNewUIOptionWindow::GetResolutionIndex() const
{
    return m_iResolutionIndex;
}

void SEASON3B::CNewUIOptionWindow::CycleResolution(int delta)
{
    if (delta == 0)
        return;

    SetResolutionIndex(m_iResolutionIndex + delta);
    ApplyResolutionChange();
}

void SEASON3B::CNewUIOptionWindow::FormatResolutionText(wchar_t* buffer, size_t count) const
{
    if (buffer == nullptr || count == 0)
    {
        return;
    }

    wchar_t resolutionOnly[32];
    GetResolutionOptionText(resolutionOnly, sizeof(resolutionOnly) / sizeof(resolutionOnly[0]));
    swprintf(buffer, count, L"Resolution: %s", resolutionOnly);
}

void SEASON3B::CNewUIOptionWindow::ApplyResolutionChange() const
{
    m_Resolution = m_iResolutionIndex;

    leaf::CRegKey regkey;
    regkey.SetKey(leaf::CRegKey::_HKEY_CURRENT_USER, L"SOFTWARE\\Webzen\\Mu\\Config");
    regkey.WriteDword(L"Resolution", m_Resolution);

    wchar_t resolutionText[64];
    GetResolutionOptionText(resolutionText, sizeof(resolutionText) / sizeof(resolutionText[0]));
    if (g_pSystemLogBox)
    {
        wchar_t message[128];
        swprintf(message, sizeof(message) / sizeof(message[0]), L"Resolution set to %s. Restart required to apply.", resolutionText);
        g_pSystemLogBox->AddText(message, SEASON3B::TYPE_SYSTEM_MESSAGE);
    }
}

void SEASON3B::CNewUIOptionWindow::GetResolutionOptionText(wchar_t* buffer, size_t count) const
{
    if (buffer == nullptr || count == 0)
    {
        return;
    }

    const auto option = GetResolutionOption(m_iResolutionIndex);
    swprintf(buffer, count, L"%d x %d", option.width, option.height);
}

RECT SEASON3B::CNewUIOptionWindow::GetCheckboxRect(CheckboxId id) const
{
    RECT rect{};

    const bool isGeneral = (id == CheckboxId::AutoAttack || id == CheckboxId::WhisperSound || id == CheckboxId::SlideHelp);
    if (isGeneral)
    {
        const int index = static_cast<int>(id) - static_cast<int>(CheckboxId::AutoAttack);
        rect.left = m_Pos.x + kLeftCheckboxOffsetX;
        rect.top = m_Pos.y + kGeneralTextStartOffset + index * kRowSpacing - 5;
    }
    else
    {
        const int index = static_cast<int>(id) - static_cast<int>(CheckboxId::RenderFullEffects);
        rect.left = m_Pos.x + kRightCheckboxOffsetX;
        rect.top = m_Pos.y + kGraphicsTextStartOffset + index * kRowSpacing - 5;
    }

    rect.right = rect.left + kCheckboxSize;
    rect.bottom = rect.top + kCheckboxSize;
    return rect;
}

RECT SEASON3B::CNewUIOptionWindow::GetVolumeSliderRect() const
{
    RECT rect{};
    rect.left = m_Pos.x + kLeftTextOffsetX;
    rect.top = m_Pos.y + kVolumeSliderOffsetY;
    rect.right = rect.left + 124;
    rect.bottom = rect.top + 16;
    return rect;
}

RECT SEASON3B::CNewUIOptionWindow::GetRenderSliderRect() const
{
    RECT rect{};
    rect.left = m_Pos.x + kLeftTextOffsetX;
    rect.top = m_Pos.y + kRenderSliderOffsetY;
    rect.right = rect.left + 141;
    rect.bottom = rect.top + 29;
    return rect;
}

RECT SEASON3B::CNewUIOptionWindow::GetResolutionRect() const
{
    RECT rect{};
    rect.left = m_Pos.x + kRightTextOffsetX;
    rect.top = m_Pos.y + kResolutionAreaOffsetY;
    rect.right = rect.left + kResolutionAreaWidth;
    rect.bottom = rect.top + kResolutionAreaHeight;
    return rect;
}

void SEASON3B::CNewUIOptionWindow::RenderCheckbox(const RECT& rect, bool checked) const
{
    const float v = checked ? 0.f : 15.f;
    RenderImage(IMAGE_OPTION_BTN_CHECK, static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(kCheckboxSize), static_cast<float>(kCheckboxSize), 0, v);
}
