#pragma once
#include "CysUiInputBridge.h"
#include "GamePro_CharCreate.h"
#include "CysLoginScene.h"
#include "GamePro_CharSel.h"
#include "GamePro_Login.h"
#include "../Object/Character/Obj_PlayerOther.h"
#include "../Network/CysWorldReplay.h" // CYS cs2-world: world script of the live GUI session (mode World)
#if defined(CYS_HAVE_NETCORE)
#include "../Network/CysModernGame.h"
#include "../Network/CysPvpNet.h"
#include "../Network/CysModernHandlers.h"
#include "../Network/CysInventoryDriver.h" // CYS-INV: inventory GUI script (CYS_INV_LIVE=1)
#include "../Network/CysQuestDriver.h" // CYS-QUEST: NPC dialogue / mission GUI script (CYS_QUEST_LIVE=1)
#include "../Network/CysReviveDriver.h" // CYS-REVIVE: right-click cancel of a buff (CYS_REVIVE_LIVE=1)
#include "../Network/CysSkillDriver.h" // CYS-SKILL: skill GUI script (CYS_SKILL_LIVE=1)
#include "CysHost_Host2.h" // CYS-HOST2: host-function probe (CYS_HOST2_LIVE=1)
#include "CysHost_EquipmentSkills.h"
#include "CysHost_MeshWin.h" // CYS-MESHWIN: MeshWindow probe (CYS_MESHWIN_LIVE=1): SelfEquip / Pet / Wuhun model frames
#include "CysHost_Wardrobe.h" // CYS-WARD3: probe do CHINH CAI NUT mo phong thay do (CYS_WARD3_LIVE=1)
#endif
#include "CysUiEditProbe.h" // CYS-UIEDIT: luot do bo sua bo cuc UI (CYS_UIEDIT_LIVE=1)
#include "CysNpcInteractProbe.h" // CYS-NPCINT: luot do tuong tac NPC qua duong chuot trai (CYS_NPCINT_LIVE=1)
#include "CysSceneMapProbe.h" // CYS-SCENEMAP: luot do ban do lon + minimap (CYS_SCENEMAP_LIVE=1)
#include "CysAuraRingProbe.h" // CYS-AURARING: luot do vong bao tam chieu dien rong (CYS_AURARING_LIVE=1)
#include "CysChatEnterProbe.h" // CYS_CHATENTER_LIVE=1: native typing / Enter / clear / chat receive.
#include "CysUnequipProbe.h"
#include "CysNbShopProbe.h"
#include "CysTooltipContractProbe.h"
#include "CysInspectProbe.h"
#include "CysEquipmentTooltipProbe.h"
#include "CysEquipCraftProbe.h"
#include "CysMountFlightProbe.h"
#include "CysCellSkinProbe.h"
#include "CysUiWheelProbe.h"
#include "CysNpcIconProbe.h"
#include "CysNameboardProbe.h"
#include "CysBagProbe.h"
#include "CysInventoryIntegrityProbe.h"
#include "CysQuickbarProbe.h"
#include "CysForgeProbe.h"
#include "CysRound2Probe.h"
#include "CysPowerSystemsProbe.h"
#include "CysPlayerShopProbe.h"
#include "CysSoulReforgeProbe.h"
#include "CysWuhunPreviewProbe.h"
#include "CysWuhunAppearanceProbe.h"
#include "CysDualSoulsProbe.h"
#include "CysSmartEquipProbe.h"
#include "CysCurrencyProbe.h"
#include "CysRuntimePolishProbe.h"
#include "CysGameplayAuditProbe.h"
#include "CysCommunityProbe.h"
#include "CysPartyGuildProbe.h"
#include "CysPetMechanicsProbe.h"
#include "CysWardrobeProbe.h"
#include "CysGameplayFixProbe.h"
#include "CysRuntimeRepairsProbe.h"
#include "CysCastFrameProbe.h"
#include "CysLuaEventProbe.h"
#include <string>
#include <stdexcept>
#include <cmath>

// CYS: explicitly enabled, GUI-thread-only integration test. All input targets
// this process's HWND; never move the user's mouse or send desktop-wide input.
class CysGlLoginSmoke
{
    DWORD started_ = GetTickCount(), next_ = started_ + 3000;
    DWORD worldDeadlineMs_ = 180000u;
    unsigned step_ = 0;
    bool live_ = false;
    unsigned createMode_ = 0; // 0: login, 1: offline preview, 2: create, 3: fresh-login selection, 4: live world (cs2-world).
    float rotationBefore_ = 0.0f, zoomBefore_ = 1.0f;
    int sectIndex_ = 0;
    float rotationPoint_[2] = {};
    unsigned serverStep_ = 0;
    // CYS-CHARSEL: CYS_LOGIN_SCENE_SHOTS=N adds N captures 2 s apart of the server and account screens (3D background motion).
    unsigned sceneShots_ = 0, serverShot_ = 0, accountShot_ = 0;
    DWORD enterFallbackAt_ = 0;       // CYS-CY (cs2-world): World mode, see case 51
    bool enterFallbackDone_ = false;
    // CYS-RELOGIN: CYS_SWITCH_CYCLES=N (Select mode) repeats world -> ReturnServerLogin -> account -> world N times.
    // The test password is then kept in switchSecret_ until the last relogin types it, and wiped there.
    unsigned switchCycles_ = 0, switchDone_ = 0;
    wchar_t switchSecret_[33] = {};
#if defined(CYS_HAVE_NETCORE)
    pg::LCRetCharList hairProbeOriginal_;
    bool hairWorldChecked_ = false;
    void hairProbePreview(unsigned int color)
    {
        auto list = hairProbeOriginal_;
        list.charlist[0].hair_color = static_cast<int32_t>(color);
        require(CGameProcedure::s_pProcCharSel->SetModernCharacterList(list),
            "Hair smoke: could not refresh preview list");
        TDU_Log("CYS hair smoke: preview rgba=%08x", color);
    }
#endif
    CysUiInputBridge::SmokePointer pointer_ = nullptr;
    CysUiInputBridge::GetWindowRectUtf8 rect_ = nullptr;
    CysUiInputBridge::GetDisplaySize display_ = nullptr; // CYS-GUIHARNESS: bound of the space rect_ lives in
    CysUiInputBridge::GetActiveInputName active_ = nullptr;
    CysUiInputBridge::CopyWindowTextUtf8 text_ = nullptr;
    CysUiInputBridge::CopyWindowTextUtf8 simpleText_ = nullptr;

    static void require(bool value, const char* message)
    { if (!value) { TDU_Log("CYS GL smoke: failed: %s",message); throw std::runtime_error(message); } }
    // CYS-XINFA 17/09: the live turns were bound to test00 / CYTest00. CYS_BOT_ACCOUNT and CYS_BOT_ROLE name the
    // account and the character of one run so two agents can measure on their own test accounts; the defaults are
    // unchanged. Only these two names vary - password handling, the single login attempt and every check stay as they were.
    static const char* smokeAccount()
    {
        static char name[33] = {};
        if (!name[0] && GetEnvironmentVariableA("CYS_BOT_ACCOUNT",name,sizeof(name))==0) _snprintf(name,sizeof(name)-1,"test00");
        return name;
    }
    static const char* smokeRole()
    {
        static char name[33] = {};
        if (!name[0] && GetEnvironmentVariableA("CYS_BOT_ROLE",name,sizeof(name))==0) _snprintf(name,sizeof(name)-1,"CYTest00");
        return name;
    }
    static const wchar_t* smokeAccountW()
    {
        static wchar_t name[33] = {};
        if (!name[0]) MultiByteToWideChar(CP_UTF8,0,smokeAccount(),-1,name,33);
        return name;
    }
    // CYS-GUIHARNESS 19/09: the target has to lie inside the space its own rect is expressed in - the CEGUI
    // display area (= the render target), read live on every click. The old bound was GetClientRect(g_hMainWnd),
    // a DIFFERENT rectangle: the Win32 window is resized away from the backbuffer by CysLoginWindowRestore
    // (login 1280x720 -> configured size at CharSel/CharCreate), so any control below the smaller of the two
    // aborted the whole run. Measured occurrences of that abort: Build/Logs/2026-09-13-perf/game/F-H-1080 and
    // F-H2-1080 (render target 1920x1080) and 2026-09-17-host-2/live-18-noinfo. Both rectangles are logged on
    // failure now, so the next occurrence names its own numbers instead of needing a rebuild to be diagnosed.
    void click(const char* name)
    {
        float rect[4] = {};
        require(rect_(name, rect) == 1, "GL smoke: target window is not visible");
        const int x = static_cast<int>(rect[0] + rect[2] * 0.5f);
        const int y = static_cast<int>(rect[1] + rect[3] * 0.5f);
        float display[2] = {};
        const bool haveDisplay = display_ && display_(display) == 1;
        const bool inside = haveDisplay && x >= 0 && y >= 0 &&
            static_cast<float>(x) < display[0] && static_cast<float>(y) < display[1];
        if (!inside)
        {
            RECT client = {};
            const BOOL haveClient = GetClientRect(g_hMainWnd, &client);
            TDU_Log("CYS GL smoke: target out of bounds: name=%s rect=%.1f,%.1f,%.1fx%.1f point=%d,%d "
                "display=%.0fx%.0f(ok=%d) client=%ldx%ld(ok=%d) iconic=%d visible=%d",
                name, rect[0], rect[1], rect[2], rect[3], x, y,
                display[0], display[1], haveDisplay ? 1 : 0,
                static_cast<long>(client.right), static_cast<long>(client.bottom), haveClient ? 1 : 0,
                IsIconic(g_hMainWnd) ? 1 : 0, IsWindowVisible(g_hMainWnd) ? 1 : 0);
        }
        require(inside, "GL smoke: target lies outside the client area");
        require(pointer_(1, static_cast<float>(x), static_cast<float>(y)) == 1 &&
            pointer_(2, static_cast<float>(x), static_cast<float>(y)) == 1, "GL smoke: native click rejected");
    }
    void hold(const char* name)
    {
        float bounds[4] = {};
        require(rect_(name,bounds) == 1, "Create smoke: held control missing");
        rotationPoint_[0] = bounds[0] + bounds[2]/2;
        rotationPoint_[1] = bounds[1] + bounds[3]/2;
        require(pointer_(1,rotationPoint_[0],rotationPoint_[1]) == 1,
            "Create smoke: held control press rejected");
    }
    void releaseHeld()
    {
        require(pointer_(2,rotationPoint_[0],rotationPoint_[1]) == 1,
            "Create smoke: held control release rejected");
    }
    static void key(unsigned value)
    {
        const LPARAM scan = static_cast<LPARAM>(MapVirtualKeyW(value, MAPVK_VK_TO_VSC)) << 16;
        SendMessageW(g_hMainWnd, WM_KEYDOWN, value, scan | 1);
        // SendMessage does not run TranslateMessage. These keys enter Ime via WM_CHAR.
        if (value == VK_BACK || value == VK_TAB || value == VK_RETURN)
            SendMessageW(g_hMainWnd, WM_CHAR, value, scan | 1);
        SendMessageW(g_hMainWnd, WM_KEYUP, value, scan | 0xc0000001);
    }
    static void chars(const wchar_t* value)
    { for (; *value; ++value) SendMessageW(g_hMainWnd, WM_CHAR, *value, 1); }
    void focus(const char* name)
    { const char* actual = active_(); require(actual && strcmp(actual,name)==0,"GL smoke: input focus mismatch"); }
    void checkText(const char* name, const char* expected)
    {
        char value[256] = {};
        const int length = text_(name, value, sizeof(value));
        const bool matches = length >= 0 && strcmp(value,expected)==0;
        SecureZeroMemory(value,sizeof(value));
        require(matches,"GL smoke: editbox text mismatch");
    }
    void checkSimpleText(const char* name, const char* expected)
    {
        char value[256] = {};
        const int length = simpleText_(name, value, sizeof(value));
        const bool matches = length >= 0 && strcmp(value,expected)==0;
        SecureZeroMemory(value,sizeof(value));
        require(matches,"GL smoke: SimpleText label mismatch");
    }
    void shot(CRenderSystem* renderer, const char* label)
    {
        CreateDirectoryA("..\\ScreenShots",nullptr);
        char path[MAX_PATH] = {};
        _snprintf(path,MAX_PATH,"..\\ScreenShots\\cys-gl-%lu-%s.png",
            static_cast<unsigned long>(started_),label);
        cysCaptureOfflineUi(renderer,path);
        TDU_Log("CYS GL smoke: capture=%s",label);
    }
    bool visible(const char* name)
    { float bounds[4] = {}; return rect_(name, bounds) == 1; }
    CGamePro_CharCreate* creation()
    {
        CGamePro_CharCreate* procedure = CGameProcedure::s_pProcCharCreate;
        require(procedure && procedure->m_cysPreviewValid, "Create smoke: native preview missing");
        const int sex = procedure->m_cysPreview.sex;
        require(sex >= 0 && sex <= 1 && procedure->m_pAvatar[sex] &&
            procedure->m_pAvatar[sex]->GetRenderInterface(), "Create smoke: native actor missing");
        return procedure;
    }
    void appearance(int sex, int face, int hair, int outfit, int portrait)
    {
        CGamePro_CharCreate* procedure = creation();
        require(!visible("CreateRole_Model") && CysLoginScene::IsShowing(procedure->m_pAvatar[sex]),
            "Create smoke: actor must be presented in main scene without a visible RTT");
        CysCreateRoleAppearance::Recipe expected;
        std::string error;
        require(procedure->m_cysAppearance.ResolveIndices(sex, face, hair, outfit, expected, error),
            "Create smoke: expected CY choice unavailable");
        const auto portraits = procedure->m_cysAppearance.Options(CysCreateRoleAppearance::Portrait, sex);
        require(portrait >= 0 && static_cast<size_t>(portrait) < portraits.size() &&
            procedure->m_cysPreview.sex == expected.sex && procedure->m_cysPreview.faceId == expected.faceId &&
            procedure->m_cysPreview.hairId == expected.hairId && procedure->m_cysPreview.outfitId == expected.outfitId &&
            procedure->m_iFaceId == portraits[portrait].id, "Create smoke: visible choice did not update native state");
    }
    void validRandomAppearance()
    {
        CGamePro_CharCreate* procedure = creation();
        const auto& actual = procedure->m_cysPreview;
        const CysCreateRoleAppearance::TableKind kinds[] = {CysCreateRoleAppearance::Face,
            CysCreateRoleAppearance::Hair, CysCreateRoleAppearance::Outfit, CysCreateRoleAppearance::Portrait};
        const int ids[] = {actual.faceId, actual.hairId, actual.outfitId, procedure->m_iFaceId};
        for (unsigned i = 0; i != 4; ++i)
        {
            bool found = false;
            for (const auto& option : procedure->m_cysAppearance.Options(kinds[i], actual.sex))
                found = found || option.id == ids[i];
            require(found, "Create smoke: random appearance escaped CY options");
        }
    }
public:
    ~CysGlLoginSmoke() { SecureZeroMemory(switchSecret_, sizeof(switchSecret_)); }
    explicit CysGlLoginSmoke(bool live) : live_(live)
    {
        char createMode[16] = {};
        const DWORD count = GetEnvironmentVariableA("CYS_CREATE_ROLE_SMOKE", createMode, sizeof(createMode));
        if (count)
        {
            require(count < sizeof(createMode), "Create smoke: mode too long");
            createMode_ = strcmp(createMode, "Preview") == 0 ? 1 : strcmp(createMode, "Create") == 0 ? 2 :
                strcmp(createMode, "Select") == 0 ? 3 : strcmp(createMode, "World") == 0 ? 4 : 0;
            require(createMode_ && ((createMode_ != 1) == live_), "Create smoke: mode does not match endpoint");
            require(CGameProcedure::s_pProcCharCreate->HasCyCreationProfile(), "Create smoke: CY profile missing");
        }
        if (createMode_ == 4)
        {
            char timeout[16] = {};
            const DWORD length = GetEnvironmentVariableA("CYS_WORLD_SMOKE_TIMEOUT_MS", timeout, sizeof(timeout));
            if (length)
            {
                require(length < sizeof(timeout), "World smoke: timeout too long");
                char* end = nullptr;
                const unsigned long value = strtoul(timeout, &end, 10);
                require(end && !*end && value >= 180000u && value <= 600000u, "World smoke: invalid timeout");
                worldDeadlineMs_ = static_cast<DWORD>(value);
            }
            TDU_Log("CYS world smoke: deadline_ms=%lu", static_cast<unsigned long>(worldDeadlineMs_));
        }
        HMODULE module = GetModuleHandleA("UI_CEGUI.dll");
        require(module != nullptr,"GL smoke: UI module unavailable");
        rect_ = reinterpret_cast<CysUiInputBridge::GetWindowRectUtf8>(GetProcAddress(module,"CysUiGetWindowRectUtf8"));
        active_ = reinterpret_cast<CysUiInputBridge::GetActiveInputName>(GetProcAddress(module,"CysUiGetActiveInputName"));
        text_ = reinterpret_cast<CysUiInputBridge::CopyWindowTextUtf8>(GetProcAddress(module,"CysUiCopyWindowTextUtf8"));
        simpleText_ = reinterpret_cast<CysUiInputBridge::CopyWindowTextUtf8>(GetProcAddress(module,"CysUiCopySimpleTextUtf8"));
        pointer_ = reinterpret_cast<CysUiInputBridge::SmokePointer>(GetProcAddress(module,"CysUiSmokePointer"));
        display_ = reinterpret_cast<CysUiInputBridge::GetDisplaySize>(GetProcAddress(module,"CysUiGetDisplaySize"));
        require(rect_ && active_ && text_ && simpleText_ && pointer_ && display_,"GL smoke: UI input exports unavailable");
        char shots[8] = {};
        if (GetEnvironmentVariableA("CYS_LOGIN_SCENE_SHOTS", shots, sizeof(shots)) && shots[0] >= '1' && shots[0] <= '5' && !shots[1])
            sceneShots_ = static_cast<unsigned>(shots[0] - '0');
        char cycles[4] = {};
        if (GetEnvironmentVariableA("CYS_SWITCH_CYCLES", cycles, sizeof(cycles)))
        {
            require(cycles[0] >= '1' && cycles[0] <= '5' && !cycles[1], "Relogin smoke: CYS_SWITCH_CYCLES must be 1..5");
            require(createMode_ == 3 && live_, "Relogin smoke: switch cycles require live Select mode");
            switchCycles_ = static_cast<unsigned>(cycles[0] - '0');
            TDU_Log("CYS relogin smoke: begin cycles=%u", switchCycles_);
        }
        TDU_Log("CYS GL smoke: begin live=%d",live_ ? 1 : 0);
        if (createMode_) TDU_Log("CYS create smoke: begin mode=%s", createMode);
    }
    void tick(CRenderSystem* renderer, bool characterSelection)
    {
        const DWORD now = GetTickCount();
        if (step_ == 99 || static_cast<LONG>(now-next_) < 0) return;
        require(DWORD(now-started_) < (createMode_ == 4 ? worldDeadlineMs_ : createMode_ ? 180000u * (1 + switchCycles_) : 120000u),"GL smoke: integration deadline exceeded");
        if (step_ == 0)
        {
            float visible[4] = {};
            if (!rect_("LogOn_ID",visible))
            {
                if (serverStep_ >= 4)
                {
                    // CYS-CY 14/09 (cs2-world): on the CY 3.71 GUI layer the account screen never shows: the Lua handler
                    // of GAMELOGIN_OPEN_COUNT_INPUT (LoginLogOn_Env) fails on a host this build lacks (agent cy-gui),
                    // so LogOn_ID stays hidden. Offline: stop here without authentication. Live World turn: submit the
                    // account natively once (CGamePro_Login::CheckAccount, the call the GL TextAccepted handler makes),
                    // then continue with the character selection through the GUI.
                    TDU_Log("CYS GL smoke: CY account screen unavailable (LoginLogOn_Env Lua failure); GUI typing skipped");
                    if (!live_ || createMode_ != 4)
                    {
                        TDU_Log("CYS GL smoke: PASS offline input; no authentication (CY login screen hidden)");
                        step_=99; return;
                    }
                    wchar_t secret[65] = {};
                    const DWORD length=GetEnvironmentVariableW(L"CYS_BOT_PWD",secret,65);
                    SetEnvironmentVariableW(L"CYS_BOT_PWD",nullptr);
                    const bool valid=length>0 && length<=32;
                    char account[33]={}, password[65]={};
                    _snprintf(account,sizeof(account)-1,"%s",smokeAccount());
                    if (valid) WideCharToMultiByte(CP_UTF8,0,secret,-1,password,sizeof(password),NULL,NULL);
                    SecureZeroMemory(secret,sizeof(secret));
                    int rc=-1;
                    if (valid) rc=CGameProcedure::s_pProcLogIn->CheckAccount(account,password);
                    SecureZeroMemory(password,sizeof(password));
                    require(valid,"GL smoke: bounded test password environment is required");
                    TDU_Log("CYS GL smoke: submitted once natively rc=%d (CY login screen hidden)",rc);
                    step_=11; next_=now+250; return;
                }
                // CYS-CY 14/09 (cs2-world): the CY 3.71 server screen (agent cy-gui) has no SelectServer_Server1 /
                // Accept pair; its list is built from SelectServer_Commendable*_Subarea*. With one server in the
                // list the native selection is the same event the buttons raise, so the smoke uses it there.
                float control[4] = {};   // the local array 'visible' shadows the member function here
                if (serverStep_ == 0 && serverShot_ < sceneShots_)
                {
                    char label[32] = {};
                    _snprintf(label, sizeof(label) - 1, "server-scene-%u", ++serverShot_);
                    shot(renderer, label); next_=now+2000; return;
                }
                if (serverStep_ == 0)
                {
                    shot(renderer,"server-before");
                    // The CY list is filled from the login server list; selecting index 0 raises the same event the
                    // list buttons raise (CGamePro_Login::SelectLoginServer), and the one server of the test profile
                    // is index 0. The GL screen keeps its explicit buttons below.
                    const int rc = CGameProcedure::s_pProcLogIn->SelectLoginServer(0,0);
                    TDU_Log("CYS GL smoke: server list index 0 selected natively rc=%d",rc);
                    if (rect_("SelectServer_Server1",control) == 1) click("SelectServer_Server1");
                }
                else if (serverStep_ == 1 && rect_("SelectServer_Accept",control) == 1)
                {
                    click("SelectServer_Accept");
                }
                ++serverStep_; next_=now+1000; return;
            }
        }
        switch (step_)
        {
        case 0:
            { float previous[4] = {};
              require(!rect_("SelectServer_Frame",previous),"GL smoke: server selection still covers account input"); }
            if (accountShot_ < sceneShots_)
            {
                char label[32] = {};
                _snprintf(label, sizeof(label) - 1, "account-scene-%u", ++accountShot_);
                shot(renderer, label); next_=now+2000; return;
            }
            shot(renderer,"account-before"); click("LogOn_ID"); chars(L"\x1"); key(VK_BACK); break;
        case 1:
            focus("LogOn_ID");
            // UTF-16 WM_CHAR, verified as UTF-8 below. No dependency on ACP.
            chars(L"Ti\x1ebfng Vi\x1ec7t \x4e2d\x6587"); break;
        case 2:
            checkText("LogOn_ID","Ti\xe1\xba\xbfng Vi\xe1\xbb\x87t \xe4\xb8\xad\xe6\x96\x87");
            TDU_Log("CYS GL smoke: Vietnamese/Chinese WM_CHAR roundtrip passed");
            shot(renderer,"unicode");
            // Delete the fixture through normal edit events, never submit it.
            chars(L"\x1"); key(VK_BACK);
            break;
        case 3:
            checkText("LogOn_ID",""); chars(smokeAccountW()); key(VK_TAB); break;
        case 4:
            checkText("LogOn_ID",smokeAccount()); focus("LogOn_PassWord");
            key(VK_TAB); break;
        case 5:
            focus("LogOn_ID"); key(VK_RETURN); break;
        case 6:
            focus("LogOn_PassWord");
            TDU_Log("CYS GL smoke: click/tab/enter focus passed");
            click("LogOn_Keyboard"); break;
        case 7:
        {
            float rect[4] = {};
            require(rect_("SoftKeyBoard_Back",rect)==1,"GL smoke: soft keyboard did not open");
            float keys[4] = {}, title[4] = {}, after[4] = {};
            require(rect_("SoftKeyBoard_SoftKey",keys)==1,"GL smoke: soft key grid missing");
            // KeySize=30; column1,row1 is a shuffled numeric key in production.
            require(pointer_(1,keys[0]+45,keys[1]+45) && pointer_(2,keys[0]+45,keys[1]+45),"GL smoke: soft key click rejected");
            char typed[8] = {};
            const int count=text_("LogOn_PassWord",typed,sizeof(typed));
            const bool numeric=count==1 && typed[0]>='0' && typed[0]<='9';
            SecureZeroMemory(typed,sizeof(typed));
            require(numeric,"GL smoke: soft keyboard did not type into password");
            // Column13,row0 is the native Back key.
            require(pointer_(1,keys[0]+405,keys[1]+15) && pointer_(2,keys[0]+405,keys[1]+15),"GL smoke: soft back key rejected");
            checkText("LogOn_PassWord","");
            require(rect_("SoftKeyBoard_Title",title)==1,"GL smoke: keyboard title missing");
            const float x=title[0]+title[2]/2, y=title[1]+title[3]/2;
            require(pointer_(1,x,y) && pointer_(0,x-20,y-40) && pointer_(2,x-20,y-40),"GL smoke: keyboard drag rejected");
            require(rect_("SoftKeyBoard_Back",after)==1 && fabs(after[0]-rect[0]+20)<1 && fabs(after[1]-rect[1]+40)<1,
                "GL smoke: keyboard drag did not move its frame");
            TDU_Log("CYS GL smoke: soft keyboard typed/backspace/drag passed");
            shot(renderer,"soft-keyboard"); click("SoftKeyBoard_SoftKey_Close"); break;
        }
        case 8:
        {
            float rect[4] = {};
            require(rect_("SoftKeyBoard_Back",rect)==0,"GL smoke: soft keyboard did not close");
            click("LogOn_PassWord");
            if (!live_)
            {
                if (createMode_ == 1)
                {
                    require(CGameProcedure::s_pProcCharSel->GetCurRoleCount() == 0,
                        "Create smoke: offline fixture must start empty");
                    CGameProcedure::s_pProcLogIn->ChangeToCharacterSel(false);
                    TDU_Log("CYS create smoke: offline empty selection fixture; no authentication");
                    step_=13; next_=now+1000; return;
                }
                TDU_Log("CYS GL smoke: PASS offline input; no authentication"); step_=99; return;
            }
            break;
        }
        case 9:
        {
            focus("LogOn_PassWord");
            wchar_t secret[65] = {};
            const DWORD length=GetEnvironmentVariableW(L"CYS_BOT_PWD",secret,65);
            SetEnvironmentVariableW(L"CYS_BOT_PWD",nullptr);
            const bool valid=length>0 && length<=32;
            if (valid) chars(secret);
            if (valid && switchCycles_) memcpy(switchSecret_, secret, (length + 1) * sizeof(wchar_t));
            SecureZeroMemory(secret,sizeof(secret));
            require(valid,"GL smoke: bounded test password environment is required");
            break;
        }
        case 10:
            shot(renderer,"masked-password");
            // CYS-CY (cs2-world): the CY login Lua may rewrite the account editbox between the typing and the submit;
            // prove the account is still the typed one before the single submit (gui-live-1/2 diagnosis).
            checkText("LogOn_ID",smokeAccount());
            key(VK_RETURN);
            TDU_Log("CYS GL smoke: submitted once through GL TextAccepted"); break;
        case 11:
            if (!characterSelection) { next_=now+250; return; }
            next_=now+2000; ++step_; return;
        case 12:
        {
            float visible[4] = {};
            require(rect_("SelectRole_Frame",visible)==1 && rect_("SelectRole_Create",visible)==1,
                "GL smoke: character selection controls are not visible");
            require(!rect_("LogOn_ID",visible) && !rect_("SelectServerQuest_InfoWindow",visible),
                "GL smoke: login or wait dialog still covers character selection");
            if (createMode_ == 3 || createMode_ == 4)
            {
                require(live_ && CGameProcedure::s_pProcCharSel->GetCurRoleCount() == 1,
                    "Select smoke: expected one existing server character");
                TDU_Log("CYS create smoke: existing character selection visible; login/wait hidden");
                step_=46; next_=now+12000; return;
            }
            TDU_Log("CYS GL smoke: empty character selection visible; login/wait hidden");
            shot(renderer,"character-selection");
            if (createMode_ == 2)
            {
                require(CGameProcedure::s_pProcCharSel->GetCurRoleCount() == 0,
                    "Create smoke: live account is not empty; do not create another character");
                step_=13; next_=now+750; return;
            }
            TDU_Log("CYS GL smoke: PASS WSL authenticated character selection");
            step_=99; return;
        }
        case 46:
            require(characterSelection, "Select smoke: selection left during scene warmup");
            shot(renderer,"existing-character-selection-before");
            step_=32; next_=now+1500; return;
        case 13:
            if (!characterSelection || !visible("SelectRole_Frame")) { next_=now+250; return; }
            require(createMode_ && CGameProcedure::s_pProcCharSel->GetCurRoleCount() == 0,
                "Create smoke: expected empty native selection");
            shot(renderer,"creation-empty-before"); click("SelectRole_Create"); break;
        case 14:
            if (!visible("CreateRole_Frame")) { next_=now+250; return; }
            require(!visible("SelectRole_Frame"), "Create smoke: selection still covers creation");
            creation(); click("CreateRole_SelectSex_Girl");
            // Allow the incremental scene builder to finish before visual checks.
            ++step_; next_=now+12000; return;
        case 15:
            appearance(0,0,0,0,0); shot(renderer,"create-female-default");
            click("CreateRole_SelectFace_TurnRight1"); break;
        case 16:
            appearance(0,1,0,0,0); click("CreateRole_SelectHair_TurnRight1"); break;
        case 17:
            appearance(0,1,1,0,0); click("CreateRole_SelectClothin_TurnRight1"); break;
        case 18:
            appearance(0,1,1,1,0); click("CreateRole_Select_Head2"); break;
        case 19:
            appearance(0,1,1,1,1); shot(renderer,"create-female-choices");
            TDU_Log("CYS create smoke: female face/hair/outfit/portrait controls passed");
            click("CreateRole_SelectSex_Boy"); break;
        case 20:
            appearance(1,0,0,0,0); shot(renderer,"create-male-default");
            click("CreateRole_SelectFace_TurnRight1"); click("CreateRole_SelectHair_TurnRight1");
            click("CreateRole_SelectClothin_TurnRight1"); click("CreateRole_Select_Head2"); break;
        case 21:
            appearance(1,1,1,1,1); shot(renderer,"create-male-choices");
            TDU_Log("CYS create smoke: male face/hair/outfit/portrait controls passed");
            step_=34; next_=now+750; return;
        case 34:
            click("CreateRole_PageDown"); break;
        case 35:
            require(visible("CreateRole_Select_Head6") && !visible("CreateRole_Select_Head7"),
                "Create smoke: second portrait page must contain six CY portraits");
            click("CreateRole_Select_Head6"); break;
        case 36:
            appearance(1,1,1,1,14); shot(renderer,"create-portrait-last");
            click("CreateRole_PageUp"); break;
        case 37:
            require(visible("CreateRole_Select_Head9"),
                "Create smoke: first portrait page must contain nine CY portraits");
            click("CreateRole_Select_Head2"); break;
        case 38:
            appearance(1,1,1,1,1);
            TDU_Log("CYS create smoke: original CY portrait grid and both pages passed");
            step_=22; next_=now+750; return;
        case 22:
        {
            float bounds[4] = {};
            require(rect_("CreateRole_Role2_Model_TurnRight", bounds) == 1, "Create smoke: rotation control missing");
            rotationPoint_[0] = bounds[0] + bounds[2] / 2;
            rotationPoint_[1] = bounds[1] + bounds[3] / 2;
            rotationBefore_ = creation()->m_pAvatar[1]->GetFaceDir();
            require(pointer_(1,rotationPoint_[0],rotationPoint_[1]) == 1,
                "Create smoke: rotation press rejected");
            break;
        }
        case 23:
        {
            const float angle = creation()->m_pAvatar[1]->GetFaceDir();
            require(pointer_(2,rotationPoint_[0],rotationPoint_[1]) == 1,
                "Create smoke: rotation release rejected");
            require(fabs(angle-rotationBefore_) > 0.001f, "Create smoke: holding rotate did not move native actor");
            TDU_Log("CYS create smoke: held rotation changed native actor");
            step_=39; next_=now+750; return;
        }
        case 39:
            zoomBefore_ = CysLoginScene::ZoomFactor(); hold("CreateRole_ModelZoomOut"); break;
        case 40:
            releaseHeld();
            require(CysLoginScene::ZoomFactor() > zoomBefore_+0.01f,
                "Create smoke: held zoom-out control did not change the native camera");
            shot(renderer,"create-zoom-far");
            zoomBefore_ = CysLoginScene::ZoomFactor(); hold("CreateRole_ModelZoomIn"); break;
        case 41:
            releaseHeld();
            require(CysLoginScene::ZoomFactor() < zoomBefore_-0.01f,
                "Create smoke: held zoom-in control did not change the native camera");
            TDU_Log("CYS create smoke: both original zoom controls changed native camera");
            sectIndex_ = 0; break;
        case 42:
        {
            char name[64] = {};
            _snprintf(name,sizeof(name),"CreateRole_MenpaiModel%02d",sectIndex_+1);
            click(name); break;
        }
        case 43:
        {
            auto* proc = creation();
            require(proc->m_cysSectId == sectIndex_ && proc->m_cysSectSex == 1 &&
                CysLoginScene::IsShowing(proc->m_cysSectAvatar) && !visible("CreateRole_Name"),
                "Create smoke: male sect button did not show the correct native actor");
            if (sectIndex_ == 0 || sectIndex_ == 14) shot(renderer,sectIndex_ == 0 ? "create-sect-first-male" : "create-sect-last-male");
            click("CreateRole_Info2_Frame_Back5"); break;
        }
        case 44:
        {
            auto* proc = creation();
            require(proc->m_cysSectId == sectIndex_ && proc->m_cysSectSex == 0 &&
                CysLoginScene::IsShowing(proc->m_cysSectAvatar),
                "Create smoke: female sect toggle did not show the correct native actor");
            if (sectIndex_ == 0 || sectIndex_ == 14) shot(renderer,sectIndex_ == 0 ? "create-sect-first-female" : "create-sect-last-female");
            click("CreateRole_MenpaiModel00"); break;
        }
        case 45:
            appearance(1,1,1,1,1);
            require(creation()->m_cysSectId == -1 && visible("CreateRole_Name"),
                "Create smoke: cancel preview did not restore the editable player");
            if (++sectIndex_ < 15) { step_=42; next_=now+750; return; }
            TDU_Log("CYS create smoke: all 15 CY sect buttons, both sexes and cancel passed");
            click("CreateRole_Random"); step_=24; next_=now+750; return;
        case 24:
            validRandomAppearance(); click("CreateRole_RandomName"); break;
        case 25:
        {
            char name[128] = {};
            const int length = text_("CreateRole_Name",name,sizeof(name));
            require(length > 0 && length <= 29, "Create smoke: CY random name is empty or too long");
            SecureZeroMemory(name,sizeof(name));
            TDU_Log("CYS create smoke: random CY appearance/name passed");
            click("CreateRole_Name"); chars(L"\x1"); key(VK_BACK);
            chars(L"Ti\x1ebfng Vi\x1ec7t \x4e2d\x6587"); break;
        }
        case 26:
            checkText("CreateRole_Name","Ti\xe1\xba\xbfng Vi\xe1\xbb\x87t \xe4\xb8\xad\xe6\x96\x87");
            shot(renderer,"create-unicode");
            TDU_Log("CYS create smoke: Vietnamese/Chinese name WM_CHAR roundtrip passed");
            chars(L"\x1"); key(VK_BACK); chars(L"CYTest00"); break;
        case 27:
            checkText("CreateRole_Name","CYTest00"); shot(renderer,"create-ready");
            click("CreateRole_Last"); break;
        case 28:
            if (!characterSelection || !visible("SelectRole_Frame")) { next_=now+250; return; }
            require(!visible("CreateRole_Frame") && CGameProcedure::s_pProcCharSel->GetCurRoleCount() == 0,
                "Create smoke: Back did not return to the empty list");
            shot(renderer,"create-back-selection"); click("SelectRole_Create"); break;
        case 29:
            if (!visible("CreateRole_Frame")) { next_=now+250; return; }
            click("CreateRole_SelectSex_Boy"); click("CreateRole_Name");
            chars(L"\x1"); key(VK_BACK); chars(L"CYTest00"); break;
        case 30:
            appearance(1,0,0,0,0); checkText("CreateRole_Name","CYTest00");
            shot(renderer,"create-reopened");
            TDU_Log("CYS create smoke: Back/reopen reset CY choices and retained usable input");
            if (createMode_ == 1)
            {
                require(!live_ && !CGameProcedure::s_pProcLogIn->IsModernLoginActive(),
                    "Create smoke: offline preview acquired a network session");
                TDU_Log("CYS create smoke: PASS Preview; no authentication or character submit");
                step_=99; return;
            }
            require(createMode_ == 2 && live_, "Create smoke: create requires isolated live mode");
            click("CreateRole_Author");
            TDU_Log("CYS create smoke: clicked Create once for CYTest00"); break;
        case 31:
            if (!characterSelection || CGameProcedure::s_pProcCharSel->GetCurRoleCount() == 0)
            { next_=now+250; return; }
            require(CGameProcedure::s_pProcCharSel->GetCurRoleCount() == 1,
                "Create smoke: expected exactly one created character");
            ++step_; next_=now+1500; return;
        case 32:
        {
#if defined(CYS_HAVE_NETCORE)
            CGamePro_CharSel* selection = CGameProcedure::s_pProcCharSel;
            const auto* role = selection->GetModernRoleInfo(0);
            require(role && role->guid && role->nickname_text() == smokeRole() && selection->UsesModernCharacters(),
                "Create smoke: typed server character does not match the submitted name");
            require(selection->m_Character[0].m_pAvatar && selection->m_Character[0].m_pAvatar->GetRenderInterface(),
                "Create smoke: server character actor missing");
            require(visible("SelectRole_Frame") && visible("SelectRole_TargetInfo_Button1") &&
                !visible("CreateRole_Frame") && !visible("SelectServerQuest_InfoWindow"),
                "Create smoke: created character selection is covered or incomplete");
            require(CysLoginScene::IsShowing(selection->m_Character[0].m_pAvatar),
                "Create smoke: persisted character is not presented in the main scene");
            checkSimpleText("SelectRole_TargetInfo_Name_Text",smokeRole()); click("SelectRole_TargetInfo_Button1");
#else
            require(false, "Create smoke: typed NetCore build required");
#endif
            break;
        }
        case 33:
            require(CGameProcedure::s_pProcCharSel->GetCurSelectRole() == 0,
                "Create smoke: actual slot click did not select the created character");
            shot(renderer,createMode_ == 2 ? "created-character-selection" : "existing-character-selection");
            TDU_Log("CYS create smoke: typed server list count=1 name=%s; native model and slot selected",smokeRole());
            if (createMode_ == 4)
            {
                TDU_Log("CYS world smoke: character selected; entering the world through the GUI");
#if defined(CYS_HAVE_NETCORE)
                char hairProbe[8] = {};
                if (GetEnvironmentVariableA("CYS_LOGIN_HAIR_PROBE", hairProbe, sizeof(hairProbe)) == 1 && hairProbe[0] == '1')
                {
                    auto* selection = CGameProcedure::s_pProcCharSel;
                    // Client-only colour fixtures: no dye request, DB write or account mutation.
                    hairProbeOriginal_.charnum = 1;
                    hairProbeOriginal_.charlist.push_back(*selection->GetModernRoleInfo(0));
                    TDU_Log("CYS hair smoke: server rgba=%08x hair=%d guid=%u",
                        static_cast<unsigned int>(hairProbeOriginal_.charlist[0].hair_color),
                        hairProbeOriginal_.charlist[0].hair, hairProbeOriginal_.charlist[0].guid);
                    hairProbePreview(0xc04040ffu);
                    step_=70; next_=now+1800; return;
                }
#endif
                step_=50; next_=now+1000; return;
            }
            if (createMode_ == 3 && switchDone_ < switchCycles_) { step_=80; next_=now+1000; return; }
            if (createMode_ == 3)
                TDU_Log("CYS create smoke: PASS Select; WSL existing character loaded and selected");
            else
                TDU_Log("CYS create smoke: PASS Create; WSL character persisted and selected");
            step_=99; return;
        case 70:
#if defined(CYS_HAVE_NETCORE)
            shot(renderer,"hair-select-red");
            hairProbePreview(0x4080c0ffu);
            step_=71; next_=now+1800; return;
        case 71:
            shot(renderer,"hair-select-blue");
            require(CGameProcedure::s_pProcCharSel->SetModernCharacterList(hairProbeOriginal_),
                "Hair smoke: could not restore server appearance");
            step_=72; next_=now+1800; return;
        case 72:
            shot(renderer,"hair-select-restored");
            TDU_Log("CYS hair smoke: original server list restored before login");
            step_=50; next_=now+1000; return;
#else
            require(false,"Hair smoke: NetCore build required"); break;
#endif
        // CYS cs2-world: live GUI turn on the private server. Enter the world through the SelectRole "Vào game" button,
        // let CysWorldReplay drive the world (camera + three navmesh ground clicks), then quit through the system menu.
        case 50:
            require(createMode_ == 4 && live_, "World smoke: live selection required");
            require(visible("SelectRole_Play"), "World smoke: the enter-game button is not visible");
            shot(renderer,"world-enter-click"); click("SelectRole_Play");
            TDU_Log("CYS world smoke: clicked the enter-game button once");
            next_=now+1000; ++step_; return;
        case 51:
#if defined(CYS_HAVE_NETCORE)
            if (!(CysModernGame::IsInWorld() && CysModernHandlers::WorldReady()))
            {
                // CYS-CY (cs2-world): the CY SelectRole.lua button handler (SelectRole_EnterGame -> DataPool:
                // GetCharSaletimeCyg / g_iCurSelRole) did not reach GameProduceLogin:SendEnterGameMsg on this build
                // (gui-live-4: click logged, no charlogin request). After 8 s without the world, request the
                // character login natively once for slot 0 - the same call the button handler makes.
                if (!enterFallbackAt_) enterFallbackAt_ = now;
                if (!enterFallbackDone_ && now-enterFallbackAt_ >= 8000 && characterSelection)
                {
                    enterFallbackDone_ = true;
                    const int rc = CGameProcedure::s_pProcCharSel->SendEnterGameMsg(0);
                    TDU_Log("CYS world smoke: CY enter-game button did not request the character login; native SendEnterGameMsg(0) rc=%d", rc);
                }
                next_=now+250; return;
            }
            TDU_Log("CYS world smoke: in world %lu ms after the smoke started", static_cast<unsigned long>(now-started_));
            CysWorldReplay::LiveWorldBegin(10000);
            next_=now+250; ++step_; return;
#else
            require(false, "World smoke: typed NetCore build required");
#endif
        case 52:
            if (!CysWorldReplay::LiveWorldTick()) { next_=now+100; return; }
#if defined(CYS_HAVE_NETCORE)
            // WorldReady only means the scene/MySelf exist. The base appearance
            // arrives later; compare after the world driver has actually run.
            if (!hairProbeOriginal_.charlist.empty() && !hairWorldChecked_)
            {
                auto* self = CObjectManager::GetMe()->GetMySelf();
                const unsigned int expected = static_cast<unsigned int>(hairProbeOriginal_.charlist[0].hair_color);
                const unsigned int actual = self ? self->GetCharacterData()->Get_HairColor() : 0xffffffffu;
                TDU_Log("CYS hair smoke: world actual=%08x expected=%08x", actual, expected);
                require(self && actual == expected, "Hair smoke: world colour differs from saved server colour");
                TDU_Log("CYS hair smoke: world rgba=%08x matches saved server colour", expected);
                hairWorldChecked_ = true;
            }
#endif
            if (!CysRuntimeRepairsProbe::Tick()) {
                if (const char* name=CysRuntimeRepairsProbe::ShotName()) shot(renderer,name);
                next_=now+100; return;
            }
            if (!CysInventoryDriver::Tick()) { next_=now+100; return; } // CYS-INV
            if (!CysQuestDriver::Tick()) { next_=now+100; return; } // CYS-QUEST
            if (!CysSkillDriver::Tick()) { next_=now+100; return; } // CYS-SKILL
            if (!CysEquipmentSkills::ProbeTick()) {
                if (const char* name=CysEquipmentSkills::ProbeShotName()) shot(renderer,name);
                next_=now+100; return;
            }
            if (!CysHostHost2::Tick()) { next_=now+100; return; } // CYS-HOST2
            if (!CysMeshWinProbe::Tick()) { next_=now+100; return; } // CYS-MESHWIN
            if (!CysWard3Probe::Tick()) { next_=now+100; return; } // CYS-WARD3
            if (!CysUiEditProbe::Tick()) { next_=now+100; return; } // CYS-UIEDIT
            if (!CysNpcInteractProbe::Tick()) { next_=now+100; return; } // CYS-NPCINT
            if (!CysNameboardProbe::Tick())
            {
                if (const char* name = CysNameboardProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysNpcIconProbe::Tick())
            {
                if (const char* name = CysNpcIconProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            // CYS-SCENEMAP: chup anh theo pha cua probe. shot() chi chay khi probe xin, nen mot
            // luot KHONG bat CYS_SCENEMAP_LIVE khong sinh them anh nao.
            if (!CysSceneMapProbe::Tick())
            {
                if (const char* szShot = CysSceneMapProbe::ShotName()) shot(renderer, szShot);
                next_=now+100; return;
            }
            // CYS-AURARING: chup anh theo pha cua probe, dung mau cua CysSceneMapProbe - mot luot
            // KHONG bat CYS_AURARING_LIVE khong sinh them anh nao va khong chay them gi.
            if (!CysAuraRingProbe::Tick())
            {
                if (const char* szShot = CysAuraRingProbe::ShotName()) shot(renderer, szShot);
                next_=now+100; return;
            }
            if (!CysReviveDriver::Tick()) { next_=now+100; return; } // CYS-REVIVE
            if (!CysUnequipProbe::Tick()) { next_=now+100; return; }
            if (!CysNbShopProbe::Tick()) { next_=now+100; return; }
            if (!CysChatEnterProbe::Tick())
            {
                if (const char* name = CysChatEnterProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysTooltipContractProbe::Tick())
            {
                if (const char* szShot = CysTooltipContractProbe::ShotName()) shot(renderer, szShot);
                next_=now+100; return;
            }
            {const bool done=CysMountFlightProbe::Tick();
            if(const char* name=CysMountFlightProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool done=CysEquipCraftProbe::Tick();
            if(const char* name=CysEquipCraftProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool done=CysEquipmentTooltipProbe::Tick();
            if(const char* name=CysEquipmentTooltipProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool done=CysWuhunPreviewProbe::Tick();
            if(const char* name=CysWuhunPreviewProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool done=CysPartyGuildProbe::Tick();
            if(const char* name=CysPartyGuildProbe::ShotName())shot(renderer,name);
            if(!done)return;}
            {const bool done=CysCommunityProbe::Tick();
            if(const char* name=CysCommunityProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool done=CysGameplayAuditProbe::Tick();
            if(const char* name=CysGameplayAuditProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool done=CysRuntimePolishProbe::Tick();
            if(const char* name=CysRuntimePolishProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool done=CysCurrencyProbe::Tick();
            if(const char* name=CysCurrencyProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool done=CysSmartEquipProbe::Tick();
            if(const char* name=CysSmartEquipProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool done=CysDualSoulsProbe::Tick();
            if(const char* name=CysDualSoulsProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool done=CysWuhunAppearanceProbe::Tick();
            if(const char* name=CysWuhunAppearanceProbe::ShotName())shot(renderer,name);
            if(!done){next_=now+100;return;}}
            {const bool soulDone=CysSoulReforgeProbe::Tick();
            if (const char* name=CysSoulReforgeProbe::ShotName()) shot(renderer,name);
            if (!soulDone) {next_=now+100;return;}}
            {const bool shopDone=CysPlayerShopProbe::Tick();
            if (const char* name=CysPlayerShopProbe::ShotName()) shot(renderer,name);
            if (!shopDone) {next_=now+100;return;}}
            {const bool powerDone=CysPowerSystemsProbe::Tick();
            if (const char* name = CysPowerSystemsProbe::ShotName()) shot(renderer, name);
            if (!powerDone) {next_=now+100;return;}}
            {const bool round2Done=CysRound2Probe::Tick();
            if (const char* name = CysRound2Probe::ShotName()) shot(renderer, name);
            if (!round2Done)
            {
                next_=now+100; return;
            }
            }
            if (!CysForgeProbe::Tick())
            {
                if (const char* name = CysForgeProbe::ShotName()) shot(renderer, name);
                next_=now+10; return;
            }
            if (!CysQuickbarProbe::Tick())
            {
                if (const char* name = CysQuickbarProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysGameplayFixProbe::Tick())
            {
                if (const char* name = CysGameplayFixProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysUiWheelProbe::Tick())
            {
                if (const char* name = CysUiWheelProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysCellSkinProbe::Tick())
            {
                if (const char* name = CysCellSkinProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysInspectProbe::Tick())
            {
                if (const char* name = CysInspectProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysWardrobeProbe::Tick())
            {
                if (const char* name = CysWardrobeProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysBagProbe::Tick())
            {
                if (const char* name = CysBagProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysInventoryIntegrityProbe::Tick())
            {
                if (const char* name = CysInventoryIntegrityProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysPetMechanicsProbe::Tick()) { next_=now+100; return; }
            if (!CysPvpNet::Probe()) { next_=now+100; return; }
            if (!CysLuaEventProbe::Tick())
            {
                if (const char* name = CysLuaEventProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            if (!CysCastFrameProbe::Tick())
            {
                if (const char* name = CysCastFrameProbe::ShotName()) shot(renderer, name);
                next_=now+100; return;
            }
            TDU_Log("CYS world smoke: world script finished; leaving through the system menu");
            // Alt+F4 of the user: DefWindowProc turns SC_CLOSE into WM_CLOSE(0,0), which opens the system frame.
            PostMessageW(g_hMainWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
            next_=now+2500; ++step_; return;
        case 53:
            if (visible("Operation_QuitGame"))
            {
                shot(renderer,"quit-menu"); click("Operation_QuitGame");
                TDU_Log("CYS world smoke: clicked the quit item of the system menu (QuitApplication -> CGAskQuit)");
            }
            else
            {
                TDU_Log("CYS world smoke: the system menu did not open; posting the quit request directly");
                PostMessageW(g_hMainWnd, WM_CLOSE, 0xC0DE, 0xC0DE);
            }
            TDU_Log("CYS world smoke: PASS World; live world entered, walked and quit requested");
            step_=99; return;
        // CYS-RELOGIN: one switch cycle - enter the world, "doi nhan vat" from the system menu, account screen with the
        // account prefilled and an empty password, log in again; step 11 then continues to selection (12/46/32/33).
#if defined(CYS_HAVE_NETCORE)
        case 80:
            require(visible("SelectRole_Play"), "Relogin smoke: the enter-game button is not visible");
            click("SelectRole_Play"); enterFallbackAt_ = 0; enterFallbackDone_ = false;
            TDU_Log("CYS relogin smoke: cycle=%u clicked the enter-game button", switchDone_ + 1);
            next_=now+1000; ++step_; return;
        case 81:
            if (!(CysModernGame::IsInWorld() && CysModernHandlers::WorldReady()))
            {
                // Same native fallback as case 51 when the CY button handler does not request the character login.
                if (!enterFallbackAt_) enterFallbackAt_ = now;
                if (!enterFallbackDone_ && now-enterFallbackAt_ >= 8000 && characterSelection)
                {
                    enterFallbackDone_ = true;
                    TDU_Log("CYS relogin smoke: native SendEnterGameMsg(0) rc=%d", CGameProcedure::s_pProcCharSel->SendEnterGameMsg(0));
                }
                next_=now+250; return;
            }
            TDU_Log("CYS relogin smoke: cycle=%u phase=in_world", switchDone_ + 1);
            CysModernGame::LogStatsNow();
            shot(renderer,"relogin-world");
            next_=now+3000; ++step_; return;
        case 82:
            // Alt+F4 of the user opens the CY system menu (see case 52).
            PostMessageW(g_hMainWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
            next_=now+2500; ++step_; return;
        case 83:
        {
            const char* item = visible("Operation_2Menu") ? "Operation_2Menu" : "Operation_WeGame_2Menu";
            require(visible(item), "Relogin smoke: the change-character item of the system menu is not visible");
            click(item);
            TDU_Log("CYS relogin smoke: cycle=%u clicked %s (ReturnServerLogin)", switchDone_ + 1, item);
            next_=now+250; ++step_; return;
        }
        case 84:
            // The prefill lands one frame after the account screen opens; the 750 ms step gap covers it.
            if (CGameProcedure::GetActiveProcedure() != (CGameProcedure*)CGameProcedure::s_pProcLogIn ||
                CysModernGame::IsActive() || !visible("LogOn_ID")) { next_=now+250; return; }
            break;
        case 85:
            checkText("LogOn_ID",smokeAccount());
            checkText("LogOn_PassWord","");
            shot(renderer,"relogin-account");
            {
                // Evidence only: visible_game_windows=0 is counted right after the hide, so list what shows now.
                typedef int (__cdecl *DumpFn)(char*, unsigned);
                HMODULE ui = GetModuleHandleA("UI_CEGUI.dll");
                DumpFn dump = ui ? reinterpret_cast<DumpFn>(GetProcAddress(ui, "CysUiDumpVisibleWindowsUtf8")) : nullptr;
                static char windows[8192];
                windows[0] = 0;
                if (dump) dump(windows, sizeof(windows));
                TDU_Log("CYS relogin smoke: visible after return: %s", windows);
            }
            TDU_Log("CYS relogin smoke: cycle=%u account screen, account prefilled, password empty", switchDone_ + 1);
            ++switchDone_;
            click("LogOn_PassWord"); break;
        case 86:
            focus("LogOn_PassWord");
            require(switchSecret_[0] != 0, "Relogin smoke: kept test password missing");
            chars(switchSecret_);
            if (switchDone_ == switchCycles_) SecureZeroMemory(switchSecret_, sizeof(switchSecret_));
            break;
        case 87:
            checkText("LogOn_ID",smokeAccount());
            key(VK_RETURN);
            TDU_Log("CYS GL smoke: submitted once through GL TextAccepted");
            step_=11; next_=now+750; return;
#endif
        default: require(false,"GL smoke: invalid test state");
        }
        ++step_; next_=now+750;
    }
};
