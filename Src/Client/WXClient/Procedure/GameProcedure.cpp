#include "StdAfx.h"
#include "..\Resource.h"
#include "Gameprocedure.h"
#include "GamePro_Login.h"
#include "CysOfflineUiCapture.h"
#include "GamePro_CharSel.h"
#include "GamePro_CharCreate.h"
#include "GamePro_Enter.h"
#include "GamePro_Main.h"
#include "Gamepro_ChangeScene.h"

#include "..\Interface\GMGameInterface.h"

#include "..\Global.h"

#include "..\World\WorldManager.h"

#include "..\Network\NetManager.h"
#include "CGEnterScene.h"

#include "..\Object\ObjectManager.h"
#include "..\Object\Object.h"
#include "..\Object\Surface\Obj_Surface.h"
#include "..\Object\Surface\Obj_Effect.h"
#include "..\Object\Surface\Obj_Bullet.h"
#include "..\Object\Surface\Obj_SkillObj.h"
#include "..\Object\Surface\Obj_Special.h"
#include "..\Object\Obj_Map.h"
#include "..\Object\Obj_Static.h"
#include "..\Object\Obj_Building.h"
#include "..\Object\Obj_Dynamic.h"
#include "..\Object\Character\Obj_Character.h"
#include "..\Object\Character\Obj_PlayerMySelf.h"
#include "..\Object\Character\Obj_PlayerNPC.h"
#include "..\Object\Character\Obj_PlayerOther.h"
#include "..\Object\ProjTex\Obj_ProjTex.h"
#include "..\Object\TripperObj\GMTripperObj_Transport.h"
#include "..\Object\TripperObj\GMTripperObj_ItemBox.h"
#include "..\Object\TripperObj\GMTripperObj_Resource.h"
#include "..\Object\TripperObj\GMTripperObj_Platform.h"

#include "..\Input\GMInputSystem.h"
#include "..\Sound\GMSoundSystem.h"
#include "..\DBC\GMDataBase.h"
#include "..\Variable\GMVariable.h"

#include "BuffImpactMgr.h"
#include "DirectlyImpactMgr.h"
#include "..\Object\BulletDataMgr.h"
#include "SkillDataMgr.h"
#include "..\Object\ActionSetMgr.h"

#include "TDTimeSystem.h"
#include "TDException.h"
#include "TDUISystem.h"
#include "..\DataPool\GMUIDataPool.h"
#include "..\DataPool\GMDataPool.h"
#include "TDdebuger.h"
#include "..\Cursor\CursorMng.h"
#include "..\DataPool\GMUIDataPool.h"
#include "..\Script\GMScriptSystem.h"
#include "..\Event\GMEventSystem.h"
#include "..\Action\GMActionSystem.h"
#include "TDProfile.h"
#include "AxProfile.h"
#include "..\FakeObject\GMFakeObjSystem.h"
#include "..\Helper\GMHelperSystem.h"
#include "..\Interface\GMInterface_Script_SystemSetup.h"

#include "..\WXRender\ResourceProvider.h"
// CYS-PERF-FIX pacer: frame pacing on the QueryPerformanceCounter timeline (InitStaticMemeber, MainLoop).
namespace
{
    bool s_cysPacerActive = false;
    int s_cysPacerFront = 60;
    int s_cysPacerBack = 25;
    LONGLONG s_cysPacerFreq = 0;
    LONGLONG s_cysPacerNext = 0;

    bool cysPacerLegacy(void)
    {
        static int s_legacy = -1;
        if (s_legacy < 0)
        {
            char mode[16] = {0};
            const DWORD n = ::GetEnvironmentVariableA("CYS_PERF_PACER", mode, sizeof(mode));
            s_legacy = (n && n < sizeof(mode) && _stricmp(mode, "legacy") == 0) ? 1 : 0;
        }
        return s_legacy == 1;
    }
    // client_GL ships 90/25; an absent key (0) falls back to 60/25, other values are clamped to 5..240.
    void cysPacerConfigure(int front, int back)
    {
        s_cysPacerFront = front <= 0 ? 60 : (front < 5 ? 5 : (front > 240 ? 240 : front));
        s_cysPacerBack = back <= 0 ? 25 : (back < 5 ? 5 : (back > 240 ? 240 : back));
        LARGE_INTEGER frequency;
        ::QueryPerformanceFrequency(&frequency);
        s_cysPacerFreq = frequency.QuadPart;
        s_cysPacerNext = 0;
        s_cysPacerActive = true;
    }
    HANDLE cysPacerCreateTimer(bool& highResolution)
    {
        typedef HANDLE (WINAPI *CreateTimerExW)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
        CreateTimerExW createEx = reinterpret_cast<CreateTimerExW>(
            ::GetProcAddress(::GetModuleHandleA("kernel32.dll"), "CreateWaitableTimerExW"));
        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Windows 10 1803+), TIMER_ALL_ACCESS; auto-reset like the old event
        HANDLE timer = createEx ? createEx(NULL, NULL, 0x00000002, 0x001F0003) : NULL;
        highResolution = timer != NULL;
        if (!timer)
        {
            timer = ::CreateWaitableTimerW(NULL, FALSE, NULL);
            if (timer)
                ::timeBeginPeriod(1);   // 1 ms scheduler granularity for the standard timer
        }
        return timer;
    }
    // Next deadline one period after the previous one (steady cadence, timer latency does not accumulate).
    // After a frame longer than a period the cadence restarts from now: no catch-up burst of short frames.
    void cysPacerArm(HANDLE timer, bool foreground)
    {
        LARGE_INTEGER now;
        ::QueryPerformanceCounter(&now);
        const LONGLONG period = s_cysPacerFreq / (foreground ? s_cysPacerFront : s_cysPacerBack);
        if (!s_cysPacerNext)
            s_cysPacerNext = now.QuadPart;
        else
        {
            s_cysPacerNext += period;
            if (s_cysPacerNext < now.QuadPart)
                s_cysPacerNext = now.QuadPart + period;
        }
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(double(s_cysPacerNext - now.QuadPart) * 1.0e7 / double(s_cysPacerFreq));
        if (due.QuadPart >= 0)
            due.QuadPart = -1;
        ::SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
    }
}

#define CYS_PERF_NO_IMPORT // CYS-PERF: layout only, Game.exe does not link OgreMain
#include "..\..\..\Engine\OGRE\OgreMain\include\OgreCysPerf.h"
#include <stdlib.h>
#include <string.h>

// CYS-PERF: frame phases and marks for the CYS_PERF collector (Engine/WXCore/CYS/Perf). The block is
// exported by OgreMain.dll, which Game.exe does not link: it is looked up once Render.dll loaded it.
namespace
{
    Ogre::CysPerf::Block* cysPerfBlock(void)
    {
        static Ogre::CysPerf::Block* s_block = NULL;
        static bool s_looked = false;
        if (!s_looked)
        {
            HMODULE ogre = ::GetModuleHandleA("OgreMain.dll");
            if (ogre)
            {
                s_looked = true;
                typedef Ogre::CysPerf::Block* (*GetBlock)(void);
                GetBlock get = reinterpret_cast<GetBlock>(::GetProcAddress(ogre, "CysPerfBlock"));
                Ogre::CysPerf::Block* block = get ? get() : NULL;
                if (block && block->version == Ogre::CysPerf::BLOCK_VERSION)
                    s_block = block;
            }
        }
        return (s_block && s_block->enabled) ? s_block : NULL;
    }
    LONGLONG cysPerfNow(void)
    {
        LARGE_INTEGER t;
        ::QueryPerformanceCounter(&t);
        return t.QuadPart;
    }
    LONGLONG cysPerfBegin(void) { return cysPerfBlock() ? cysPerfNow() : 0; }
    void cysPerfEnd(int phase, LONGLONG start)
    {
        Ogre::CysPerf::Block* block = start ? cysPerfBlock() : NULL;
        if (block)
        {
            block->phaseTicks[phase] += cysPerfNow() - start;
            ++block->phaseCalls[phase];
        }
    }
    class CysPerfGameScope
    {
    public:
        explicit CysPerfGameScope(int phase) : m_phase(phase), m_start(cysPerfBegin()) {}
        ~CysPerfGameScope() { cysPerfEnd(m_phase, m_start); }
    private:
        int m_phase;
        LONGLONG m_start;
    };
    void cysPerfMark(const char* label)
    {
        Ogre::CysPerf::Block* block = cysPerfBlock();
        if (block && block->mark)
            block->mark(label);
    }
    const char* cysPerfProcName(CGameProcedure* p)
    {
        if (!p) return "None";
        if (p == CGameProcedure::s_pProcLogIn) return "Login";
        if (p == CGameProcedure::s_pProcCharSel) return "CharSel";
        if (p == CGameProcedure::s_pProcCharCreate) return "CharCreate";
        if (p == CGameProcedure::s_pProcEnter) return "Enter";
        if (p == CGameProcedure::s_pProcMain) return "Main";
        if (p == CGameProcedure::s_pProcChangeScene) return "ChangeScene";
        return "Other";
    }
    // CYS_PERF_HOLD="Login:15,CharSel:20": seconds the scripted UI smoke pauses after the named procedure
    // becomes active, so perf runs sample idle states. Only read in smoke runs.
    DWORD cysPerfHoldMs(const char* procedure)
    {
        char spec[256] = {0};
        const DWORD n = ::GetEnvironmentVariableA("CYS_PERF_HOLD", spec, sizeof(spec));
        if (!n || n >= sizeof(spec) || !procedure) return 0;
        const size_t length = strlen(procedure);
        for (const char* p = spec; *p; )
        {
            while (*p == ',' || *p == ' ') ++p;
            const char* colon = strchr(p, ':');
            if (!colon) break;
            if (size_t(colon - p) == length && _strnicmp(p, procedure, length) == 0)
                return DWORD(atof(colon + 1) * 1000.0);
            const char* comma = strchr(colon, ',');
            if (!comma) break;
            p = comma + 1;
        }
        return 0;
    }
}
#include "..\Sound\GMSoundSystem.h"
#include "..\WxRender\RenderSystem.h"
#ifdef CYS_HAVE_LUAAPIGEN
#include "LuaApiGen.h"	// CYS: stubs for host functions of the modern client missing from 0.85 (Src/Client/LuaApiGen)
#include "LuaApiGen_Cy.h"	// CYS-CYGUI: stubs for the host functions only the CY 3.71 UI scripts call
#include "CysHostGlobals.h"
#include "CysHostProvinces.h"
#include "CysHostTimers.h"
#include "CysHostTimerLua.h"
#include "CysHostQueries.h"
#include "CysHostHotKeys.h" // CYS: real DataPool shortcut setter.
#include "CysHostInput.h" // CYS: GL login reads actual local input state.
#include "CysHost_Plumbing.h" // CYS-PLUMB: shared host plumbing measured on the live client (17/09).
#include "CysHost_Accel.h" // CYS-ACCEL: read-only key list of the CY remap window.
#include "CysHost_Sefit.h" // CYS-SEFIT: host functions whose generated stub has the wrong SHAPE.
#include "CysHostRelogin.h" // CYS-RELOGIN: ReturnServerLogin (system menu "doi nhan vat").
#include "CysUiInputBridge.h"
#include "TDObjectSystem.h"
#include <stdexcept>
#include <memory>
#endif
#include "CysGlLoginSmoke.h"
#include "..\Network\CysWorldReplay.h" // CYS: offline world replay driver (CYS_WORLD_REPLAY).
#include <set>

const CHAR MAINWINDOW_CLASS[]	= "WuXiaShiJie WndClass";

const INT DEFWINDOW_WIDTH		= 800;
const INT DEFWINDOW_HEIGHT		= 600;

const INT MINWINDOW_WIDTH		= 800;
const INT MINWINDOW_HEIGHT		= 600;

const UINT DEFWINDOW_STYLE		= WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX ; 


//静态变量初始化
CGamePro_Login*			CGameProcedure::s_pProcLogIn		= NULL;
CGamePro_CharSel*		CGameProcedure::s_pProcCharSel		= NULL;	
CGamePro_CharCreate*	CGameProcedure::s_pProcCharCreate	= NULL;
CGamePro_ChangeScene*   CGameProcedure::s_pProcChangeScene  = NULL;
CGamePro_Enter*			CGameProcedure::s_pProcEnter		= NULL;
CGamePro_Main*			CGameProcedure::s_pProcMain			= NULL;
CGameProcedure*         CGameProcedure::s_pActiveProcedure  = NULL;


// 当前激活的游戏流程
CGameProcedure*			CGameProcedure::s_pProcActive		= NULL;
// 前一次激活的游戏流程.
CGameProcedure*			CGameProcedure::s_pProcPrev			= NULL;

tInputSystem*			CGameProcedure::s_pInputSystem		= NULL;
CNetManager*			CGameProcedure::s_pNetManager		= NULL;
tWorldSystem*			CGameProcedure::s_pWorldManager		= NULL;
tObjectSystem*			CGameProcedure::s_pObjectManager	= NULL;
tDebuger*				CGameProcedure::s_pDebuger 			= NULL;
CRenderSystem*			CGameProcedure::s_pGfxSystem		= NULL;
tUISystem*				CGameProcedure::s_pUISystem			= NULL;
tTimeSystem*			CGameProcedure::s_pTimeSystem		= NULL;
tSoundSystem*			CGameProcedure::s_pSoundSystem		= NULL;
tDataBaseSystem*		CGameProcedure::s_pDataBaseSystem	= NULL;
tVariableSystem*		CGameProcedure::s_pVariableSystem	= NULL;
tGameInterfaceBase*		CGameProcedure::s_pGameInterface    = NULL;
tDataPool*				CGameProcedure::s_pDataPool			= NULL;
tDataPool_*				CGameProcedure::s_pDataPool_		= NULL;
tCursorSystem*			CGameProcedure::s_pCursorMng		= NULL;
tScriptSystem*			CGameProcedure::s_pScriptSystem		= NULL;
tEventSystem*			CGameProcedure::s_pEventSystem		= NULL;
tActionSystem*			CGameProcedure::s_pActionSystem		= NULL;
tFakeObjSystem*			CGameProcedure::s_pFakeObjSystem	= NULL;
tHelperSystem*			CGameProcedure::s_pHelperSystem		= NULL;
CResourceProvider*      CGameProcedure::s_pResourceProvider = NULL;

CBuffImpactMgr			*CGameProcedure::s_pBuffImpactMgr	= NULL;
CDirectlyImpactMgr		*CGameProcedure::s_pDirectlyImpactMgr	= NULL;
CBulletDataMgr			*CGameProcedure::s_pBulletDataMgr	= NULL;
CSkillDataMgr			*CGameProcedure::s_pSkillDataMgr	= NULL;
CMissionDataMgr			*CGameProcedure::s_pMissionDataMgr	= NULL;
CampAndStandDataMgr_T	*CGameProcedure::s_pCampDataMgr		= NULL;
CActionSetMgr			*CGameProcedure::s_pActionSetMgr	= NULL;
BOOL					CGameProcedure::m_bMinimized		= FALSE;
BOOL					CGameProcedure::m_bMaximized		= FALSE;
BOOL					CGameProcedure::m_bFullScreen		= FALSE;
BOOL					CGameProcedure::m_bActive			= true;
RECT					CGameProcedure::m_rectWindow;
RECT					CGameProcedure::m_rectFCOffset;
FLOAT					CGameProcedure::m_fWindowFOV		= (FLOAT)DEFWINDOW_HEIGHT/(FLOAT)DEFWINDOW_WIDTH;
BOOL					CGameProcedure::m_bRenderingPaused	= FALSE;
BOOL					CGameProcedure::m_bUIHandleInput	= FALSE;
MMRESULT				CGameProcedure::m_hEventTimer		= NULL;
HANDLE					CGameProcedure::m_hTickEvent		= NULL;
bool					CGameProcedure::m_bWaitNeedFreshMinimap = false;
bool					CGameProcedure::m_bNeedFreshMinimap		= false;


////  不可使用的字符串表
//UNUSE_STRING_VECTOR		CGameProcedure::m_UnUseStrVector;

#ifdef CYS_HAVE_LUAAPIGEN
// CYS: LuaApiGen log lines ("missing host function <Receiver>:<Name>", once per function) go to WXClient.log.
static void _CysLuaApiGenLog(const char* szLine)
{
	TDU_Log("%s", szLine);
}

// CYS: use the loaded UI module without changing the shared tUISystem vtable.
static const char* _CysActiveInputName(void*)
{
    HMODULE module = GetModuleHandleA("UI_CEGUI.dll");
    const auto query = reinterpret_cast<CysUiInputBridge::GetActiveInputName>(
        module ? GetProcAddress(module, "CysUiGetActiveInputName") : nullptr);
    if (!query) TDThrow("GL input bridge is unavailable in UI_CEGUI.dll");
    return query();
}

static int _CysCapsLockStatus(void*)
{
    return GetKeyState(VK_CAPITAL) & 1;
}

// CYS: owned on the message-pump thread, released before any script/UI service.
static std::shared_ptr<CysHostTimers::Manager> s_cysHostTimers;
static bool s_cysTimerVmFailure = false;

static void _CysEvaluateHostTimer(const std::string& owner, const std::string& expression)
{
	CScriptSystem* scripts = CScriptSystem::GetMe();
	if(!scripts || !scripts->GetLuaState())
		throw std::runtime_error("timer script service unavailable");
	const CysHostTimerLua::Result result = scripts->EvaluateTimer(owner, expression);
	if(!result.ok())
	{
		TDU_Log("CYS Lua timer: phase=%s status=%d restore=%s restore_status=%d state_usable=%d",
			CysHostTimerLua::PhaseName(result.phase), result.status,
			CysHostTimerLua::PhaseName(result.restorePhase), result.restoreStatus,
			result.stateUsable ? 1 : 0);
		if(!result.stateUsable)
		{
			// CYS: leave Win32 first, then let the existing Game fatal handler capture
			// the failure before any further message/Tick can re-enter this Lua VM.
			s_cysTimerVmFailure = true;
			const auto timers = s_cysHostTimers;
			s_cysHostTimers.reset();
			if(timers) timers->Clear();
		}
	}
}

static void _CysReportHostTimerError(const std::string& message)
{
	TDU_Log("CYS Lua timer: %s", message.c_str());
}

// CYS: real host adapters use the existing UI and event services on the game thread.
static void _CysPushHostEvent(void*, const std::string& name, const std::vector<std::string>& args)
{
	CEventSystem* events = CEventSystem::GetMe();
	if(!events)
		throw std::runtime_error("event service unavailable or event name unknown");
	// CYS-CYGUI2: the CY 3.71 scripts push their own UI events (3HUAN_OPEN_UI_RESULT, JIYUANSHOP_BUY_ITEM_CONFIRM, ...)
	// that the 0.85 event table does not name; nobody can listen to an unnamed event, so CY's no-op is the right
	// answer (the throw failed SancaiMini/XuanShop/TargetFrame chunks, replay-8). Logged once per name.
	if(!events->PushEventByName(name, args))
	{
		static std::set<std::string> s_logged;
		if (s_logged.insert(name).second)
			TDU_Log("CYS Lua event: '%s' is not a 0.85 event; ignored", name.c_str());
	}
}

static std::unique_ptr<CysHostProvinces::Catalog> s_cysProvinces;

static void _CysLoadProvinceCatalog()
{
    char* data = NULL;
    const DWORD size = CGameProcedure::s_pResourceProvider->loadResource("CityIdOfChina.txt", data, "General");
    try
    {
        s_cysProvinces.reset(new CysHostProvinces::Catalog(CysHostProvinces::Parse(data, size)));
    }
    catch (...)
    {
        if(data) CGameProcedure::s_pResourceProvider->unloadResource(data, size);
        throw;
    }
    CGameProcedure::s_pResourceProvider->unloadResource(data, size);
}

static bool _CysOpenWindowBlocked(void*)
{
    tWorldSystem* world = CGameProcedure::s_pWorldManager;
    tScene* scene = world ? world->GetActiveScene() : NULL;
    const _DBC_SCENE_DEFINE* definition = scene ? scene->GetSceneDefine() : NULL;
    return definition && definition->nServerID == 77;	// CYS: client_GL SceneDefineEx column 0
}

static std::string _CysDictionaryText(void*, const std::string& key)
{
    if (!CGameProcedure::s_pUISystem) throw std::runtime_error("dictionary service unavailable");
    return CGameProcedure::s_pUISystem->ParserString_NoVarParam(key.c_str());
}

static std::string _CysStripTextColour(void*, const std::string& text)
{
    if (!CGameProcedure::s_pUISystem) throw std::runtime_error("dictionary service unavailable");
    STRING result;
    CGameProcedure::s_pUISystem->ParserString_NoColor(text, result, FALSE);
    return result;
}

// CYS: read the existing bag item only; its lifetime stays with DataPool.
// CYS: the legacy CY bag has 60 slots (B has 240); do not expand packet layouts here.
static std::int32_t _CysBagItemTableIndex(void*, std::int32_t index)
{
	CDataPool* pool = CDataPool::GetMe();
	if(!pool) throw std::runtime_error("bag service unavailable");
	tObject_Item* item = pool->UserBag_GetItem(index);
	return item ? item->GetIdTable() : -1;
}

// CYS: description comes from the current game-owned region list.
static const char* _CysLoginAreaDescription(void*, std::int32_t index)
{
    if (!CGameProcedure::s_pProcLogIn) throw std::runtime_error("login procedure unavailable");
    return CGameProcedure::s_pProcLogIn->GetAreaDescription(index);
}

// CYS: GL queries share the loaded login catalog and its stable row indices.
static CGamePro_Login& _CysLoginProcedure()
{
    if (!CGameProcedure::s_pProcLogIn) throw std::runtime_error("login procedure unavailable");
    return *CGameProcedure::s_pProcLogIn;
}
static std::int32_t _CysAreaRecommendLevel(void*, std::int32_t index)
{
    return _CysLoginProcedure().GetAreaRecommendLevel(index);
}
static void _CysSetCurrentServerPage(void*, std::int32_t page)
{
    _CysLoginProcedure().SetCurrentServerPage(page);
}
static std::int32_t _CysSetLoginServerKeyword(void*, const std::string& keyword)
{
    return _CysLoginProcedure().SetLoginServerKeyword(keyword);
}
static bool _CysKeywordLoginServerInfo(void*, std::int32_t index, CysHostQueries::LoginServerInfo& result)
{
    return _CysLoginProcedure().GetKeywordLoginServerInfo(index, result);
}

static void _CysSetCanUseHotKey(void*, std::int32_t value)
{
	CInputSystem* input = CInputSystem::GetMe();
	if(!input) throw std::runtime_error("hotkey input service unavailable");
	input->SetCanUseHotKey(value);
}

// CYS-PLUMB: level of the local player, -1 while CObjectManager has no MySelf (login / character screens).
// Same source as SCRIPT_SANDBOX::Character::GetLevel, read on the local player instead of the target.
static int _CysPlayerLevel(void*)
{
	CObjectManager* objects = CObjectManager::GetMe();
	if(!objects) return -1;
	CObject_PlayerMySelf* mySelf = objects->GetMySelf();
	if(!mySelf || !mySelf->GetCharacterData()) return -1;
	return mySelf->GetCharacterData()->Get_Level();
}

// CYS-PLUMB: B's Player:IsVaild (0x1402d7810) is "the active procedure is the main one AND MySelf exists".
static bool _CysPlayerValid(void*)
{
	if(!CGameProcedure::s_pProcMain || CGameProcedure::GetActiveProcedure() != (CGameProcedure*)CGameProcedure::s_pProcMain)
		return false;
	CObjectManager* objects = CObjectManager::GetMe();
	return objects && objects->GetMySelf() != NULL;
}

// CYS-PLUMB: client area of the game window, step for step like B's GetCurClientSize (0x140227de0):
// GetClientRect normally, the restored window rect minus the frame while iconic, then a floor of 800x600.
static bool _CysClientSize(void*, int* width, int* height)
{
	if(!width || !height || !g_hMainWnd) return false;
	int w = 0, h = 0;
	if(::IsIconic(g_hMainWnd))
	{
		WINDOWPLACEMENT wp;
		::ZeroMemory(&wp, sizeof(wp));
		wp.length = sizeof(wp);
		if(!::GetWindowPlacement(g_hMainWnd, &wp)) return false;
		RECT frame = {0, 0, 0, 0};
		const LONG_PTR style   = ::GetWindowLongPtr(g_hMainWnd, GWL_STYLE);
		const LONG_PTR exStyle = ::GetWindowLongPtr(g_hMainWnd, GWL_EXSTYLE);
		::AdjustWindowRectEx(&frame, (DWORD)style, FALSE, (DWORD)exStyle);
		w = (int)((wp.rcNormalPosition.right - wp.rcNormalPosition.left) - (frame.right - frame.left));
		h = (int)((wp.rcNormalPosition.bottom - wp.rcNormalPosition.top) - (frame.bottom - frame.top));
	}
	else
	{
		RECT rc = {0, 0, 0, 0};
		if(!::GetClientRect(g_hMainWnd, &rc)) return false;
		w = (int)(rc.right - rc.left);
		h = (int)(rc.bottom - rc.top);
	}
	*width  = w < 800 ? 800 : w;   // B: .rdata 0x1407cb354 = 800
	*height = h < 600 ? 600 : h;   // B: .rdata 0x1407cb384 = 600
	return true;
}

static void _CysCloseHostWindow(void*, const std::string& name, bool immediate)
{
	if(immediate)
	{
		if(!CGameProcedure::s_pUISystem || !CGameProcedure::s_pUISystem->CloseWindowByName(name.c_str()))
			throw std::runtime_error("window service unavailable or window name unknown");
	}
	else
	{
		_CysPushHostEvent(NULL, "CLOSE_WINDOW", {name});
	}
}
#endif

// CYS: the CMake build (like every shipped client) names the plugins Render.dll / UI_CEGUI.dll, 0.85 loaded
// CYS: WXRender.dll / UISystem.dll. Use the new name when that file sits next to Game.exe, else the old one.
static LPCTSTR _CysPluginFile(LPCTSTR szNew, LPCTSTR szOld)
{
	TCHAR szPath[MAX_PATH] = {0};
	::GetModuleFileName(NULL, szPath, MAX_PATH);
	TCHAR* pSlash = _tcsrchr(szPath, _T('\\'));
	if(pSlash) *(pSlash+1) = 0; else szPath[0] = 0;
	_tcsncat(szPath, szNew, MAX_PATH - _tcslen(szPath) - 1);
	return (::GetFileAttributes(szPath) != INVALID_FILE_ATTRIBUTES) ? szNew : szOld;
}

VOID CGameProcedure::InitStaticMemeber(VOID)
{
	//-------------------------------------------------------------------
	//初始化随即变量种子
	srand( (unsigned)timeGetTime() );

	//-------------------------------------------------------------------
	// 注册本地类
	g_theKernel.ReisgerClass(GETCLASS(tNode));
	//网络管理器
	g_theKernel.ReisgerClass(GETCLASS(CNetManager));
	//场景管理器
	g_theKernel.ReisgerClass(GETCLASS(CWorldManager));
	//物体管理器
	g_theKernel.ReisgerClass(GETCLASS(CObjectManager));
	g_theKernel.ReisgerClass(GETCLASS(CObject));
	g_theKernel.ReisgerClass(GETCLASS(CObject_Surface));
	g_theKernel.ReisgerClass(GETCLASS(CObject_Effect));
	g_theKernel.ReisgerClass(GETCLASS(CObject_Bullet));
	//g_theKernel.ReisgerClass(GETCLASS(CObject_SkillObj));
	g_theKernel.ReisgerClass(GETCLASS(CObject_Special));
	g_theKernel.ReisgerClass(GETCLASS(CObject_Map));
	g_theKernel.ReisgerClass(GETCLASS(CObject_Static));
	g_theKernel.ReisgerClass(GETCLASS(CObject_Building));
	g_theKernel.ReisgerClass(GETCLASS(CObject_Dynamic));
	g_theKernel.ReisgerClass(GETCLASS(CObject_Character));
	g_theKernel.ReisgerClass(GETCLASS(CObject_PlayerMySelf));
	g_theKernel.ReisgerClass(GETCLASS(CObject_PlayerNPC));
	g_theKernel.ReisgerClass(GETCLASS(CObject_PlayerOther));
	g_theKernel.ReisgerClass(GETCLASS(CObject_ProjTex));
	g_theKernel.ReisgerClass(GETCLASS(CObject_ProjTex_MouseTarget));
	g_theKernel.ReisgerClass(GETCLASS(CObject_ProjTex_AuraDure));
	g_theKernel.ReisgerClass(GETCLASS(CTripperObject_Transport));
	g_theKernel.ReisgerClass(GETCLASS(CTripperObject_ItemBox));
	g_theKernel.ReisgerClass(GETCLASS(CTripperObject_Resource));
	g_theKernel.ReisgerClass(GETCLASS(CTripperObject_Platform));

	//输入管理器
	g_theKernel.ReisgerClass(GETCLASS(CInputSystem));
	//计时器
	g_theKernel.ReisgerClass(GETCLASS(tTimeSystem));
	//声音管理器
	g_theKernel.ReisgerClass(GETCLASS(CSoundSystemFMod));
	//数据库管理器
	g_theKernel.ReisgerClass(GETCLASS(CDataBaseSystem));
	//数据库管理器
	g_theKernel.ReisgerClass(GETCLASS(CVariableSystem));
	//UI操作接口
	g_theKernel.ReisgerClass(GETCLASS(CGameInterface));
	//UI数据池
	g_theKernel.ReisgerClass(GETCLASS(CUIDataPool));
	g_theKernel.ReisgerClass(GETCLASS(CDataPool));
	//UI鼠标操作类
	g_theKernel.ReisgerClass(GETCLASS(CCursorMng));
	//脚本系统
	g_theKernel.ReisgerClass(GETCLASS(CScriptSystem));
	//事件系统
	g_theKernel.ReisgerClass(GETCLASS(CEventSystem));
	//操作管理
	g_theKernel.ReisgerClass(GETCLASS(CActionSystem));
	//UI模型显示管理
	g_theKernel.ReisgerClass(GETCLASS(CFakeObjSystem));
	//外接帮助系统
	g_theKernel.ReisgerClass(GETCLASS(CHelperSystem));

	//-------------------------------------------------------------------
	// 加载插件中的类
//	PROFILE_PUSH(LoadPluginDll);
//	g_theKernel.LoadPlugin(_T("Debuger.dll"), &g_theKernel);
	g_theKernel.LoadPlugin(_CysPluginFile(_T("Render.dll"), _T("WXRender.dll")), &g_theKernel);	// CYS: was _T("WXRender.dll")
	g_theKernel.LoadPlugin(_CysPluginFile(_T("UI_CEGUI.dll"), _T("UISystem.dll")), &g_theKernel);	// CYS: was _T("UISystem.dll")
//	PROFILE_POP(LoadPluginDll);

	//-------------------------------------------------------------------
	//初始化所有的循环实例
	s_pProcLogIn			= new CGamePro_Login();			//!< 登录循环
	s_pProcCharSel			= new CGamePro_CharSel();		//!< 人物选择流程
	s_pProcCharCreate		= new CGamePro_CharCreate();	//!< 人物创建流程
	s_pProcEnter			= new CGamePro_Enter();			//!< 等待进入场景流程
	s_pProcMain				= new CGamePro_Main();			//!< 主游戏循环
	s_pProcChangeScene		= new CGamePro_ChangeScene();	//!< 服务器切换流程



	
	// 初始化数据核心
//0. 输入管理器
	s_pInputSystem	= (tInputSystem*)	g_theKernel.NewNode(_T("CInputSystem"), _T("bin"), _T("input"));  
//1. 计时器
	s_pTimeSystem	= (tTimeSystem*)	g_theKernel.NewNode(_T("tTimeSystem"), _T("bin"), _T("time"));  
//2. 网络管理器
	s_pNetManager	= (CNetManager*)	g_theKernel.NewNode(_T("CNetManager"), _T("bin"), _T("netman"));  
//3. 音效管理器
	s_pSoundSystem  = (tSoundSystem*)	g_theKernel.NewNode(_T("CSoundSystemFMod"), _T("bin"), _T("snd"));
//4. 渲染器节点
	s_pGfxSystem	= (CRenderSystem*)	g_theKernel.NewNode(_T("CRenderSystem"), _T("bin"), _T("gfx"));
//5. 调试器节点
//	s_pDebuger		= (tDebuger*)		g_theKernel.NewNode(_T("TDDebuger"), _T("bin"), _T("debuger"));
//6. 物体管理器
	s_pObjectManager= (tObjectSystem*)	g_theKernel.NewNode(_T("CObjectManager"), _T("bin"), _T("objman"));  
//7. UI管理器
	s_pUISystem		= (tUISystem*)		g_theKernel.NewNode(_T("CUISystem"), _T("bin"), _T("ui_"));
//8. 世界管理器
	s_pWorldManager = (tWorldSystem*)	g_theKernel.NewNode(_T("CWorldManager"), _T("bin"), _T("worldman"));  
//9. 数据库管理器
	s_pDataBaseSystem	= (tDataBaseSystem*)	g_theKernel.NewNode(_T("CDataBaseSystem"), _T("bin"), _T("dbc"));
//10. 系统变量管理器
	s_pVariableSystem	= (tVariableSystem*)	g_theKernel.NewNode(_T("CVariableSystem"), _T("bin"), _T("var"));
//11. 接口管理器
	s_pGameInterface	= ( tGameInterfaceBase* ) g_theKernel.NewNode( _T("CGameInterface"), _T("bin"), _T("interface"));
//12. UI数据池
	s_pDataPool			= ( tDataPool* ) g_theKernel.NewNode( _T( "CUIDataPool" ), _T( "bin" ), _T( "datapool" ) );
	s_pDataPool_		= ( tDataPool_* ) g_theKernel.NewNode( _T( "CDataPool" ), _T( "bin" ), _T( "datapool_" ) );
//13. 鼠标指针管理器
	s_pCursorMng		= ( tCursorSystem* ) g_theKernel.NewNode( _T( "CCursorMng"), _T( "bin" ), _T("cursor" ) );
//14. 脚本系统
	s_pScriptSystem		= ( tScriptSystem* ) g_theKernel.NewNode( _T( "CScriptSystem"), _T( "bin" ), _T("script" ) );
//15. 事件系统
	s_pEventSystem		= ( tEventSystem* ) g_theKernel.NewNode( _T( "CEventSystem"), _T( "bin" ), _T("event" ) );
//16. 操作管理
	s_pActionSystem		= ( tActionSystem* ) g_theKernel.NewNode( _T( "CActionSystem"), _T( "bin" ), _T("action" ) );
//17. UI模型显示管理
	s_pFakeObjSystem	= ( tFakeObjSystem* ) g_theKernel.NewNode( _T( "CFakeObjSystem"), _T( "bin" ), _T("fake" ) );
//18. 外接帮助系统
	s_pHelperSystem		= ( tHelperSystem* ) g_theKernel.NewNode( _T( "CHelperSystem"), _T( "bin" ), _T("helper" ) );

//19. 资源提供
	s_pResourceProvider	= ( CResourceProvider* ) g_theKernel.NewNode( _T( "CResourceProvider"), _T( "bin" ), _T("resprovider" ) );

	//-------------------------------------------------------------------
	//初始化工作节点
	s_pVariableSystem	->Initial(NULL);
	s_pEventSystem		->Initial(NULL);
	//-------------------------------------------------------------------
	// 创建主窗口
	CreateMainWnd();

	//-------------------------------------------------------------------
	s_pGfxSystem		->Initial(&g_hMainWnd);
	s_pScriptSystem		->Initial(NULL);
	s_pDataBaseSystem	->Initial(NULL);
	s_pTimeSystem		->Initial(NULL);
	s_pNetManager		->Initial(NULL); 
	s_pWorldManager		->Initial(NULL); 
	s_pObjectManager	->Initial(NULL); 
	if(s_pDebuger)		s_pDebuger->Initial(NULL); 
#ifdef CYS_HAVE_LUAAPIGEN
	// CYS-CYGUI2: the UI scripts run load-time chunks while CUISystem::Initial loads the 2.043 CY layouts (YuanbaoShop,
	// Memo, BieYeFurnitureCreate call SystemSetup:IsClassic / Lua_SetNeiGuaMemoId / BieYeFurniture:GetFurCreateInfo
	// before the registration below: replay-1 of cy-gui-2). Register the stubs once now for the receivers that already
	// exist or are CY-only globals; the full registration after every Initial() below extends the game-layer objects
	// that replace them (RegisterCyExtras never overrides an existing function, so both calls are safe).
	// The real global host services (ScriptGlobal_Format, ToggleWindow, timers, input) go FIRST: their registration is
	// "only if absent", so a GL stub registered before them would win (ToggleWindow -> the soft keyboard never opened,
	// smoke-login-1). Services bound to receivers (DataPool, GameProduceLogin) follow after every Initial() below.
	{
		LuaApiGen::SetLogSink(_CysLuaApiGenLog);
		CysHostGlobals::Services hostServices;
		hostServices.pushEvent = _CysPushHostEvent;
		hostServices.closeWindow = _CysCloseHostWindow;
		hostServices.openWindowBlocked = _CysOpenWindowBlocked;
		hostServices.dictionaryText = _CysDictionaryText;
		hostServices.stripTextColour = _CysStripTextColour;
		LuaPlus::LuaObject hostGlobals = CScriptSystem::GetMe()->GetLuaState()->GetGlobals();
		CysHostGlobals::Register(hostGlobals, hostServices);
		CysHostGlobals::RegisterTalk(hostGlobals, hostServices);
		// CYS-RELOGIN: before the first RegisterAll, so a regenerated ReturnServerLogin stub can never win.
		TDU_Log("CYS relogin: ReturnServerLogin registered=%u", CysHostRelogin::Register(hostGlobals));
		CysHostInput::Services inputServices;
		inputServices.activeInputName = _CysActiveInputName;
		inputServices.capsLockStatus = _CysCapsLockStatus;
		CysHostInput::Register(hostGlobals, inputServices);
		s_cysTimerVmFailure = false;
		CysHostTimers::WindowsCallbacks timerCallbacks;
		timerCallbacks.evaluate = _CysEvaluateHostTimer;
		timerCallbacks.reportError = _CysReportHostTimerError;
		s_cysHostTimers = CysHostTimers::Manager::Create(CysHostTimers::MakeWindowsBackend(timerCallbacks));
		CysHostTimers::Register(hostGlobals, s_cysHostTimers);
		LuaApiGen::RegisterAll(CScriptSystem::GetMe()->GetLuaState());
		LuaApiGen::RegisterCyExtras(CScriptSystem::GetMe()->GetLuaState());
	}
#endif
	if(s_pUISystem)		s_pUISystem->Initial(NULL);
	s_pInputSystem		->Initial(NULL); 
	s_pSoundSystem		->Initial(&g_hMainWnd);
	if(s_pUISystem) s_pUISystem->SetClickSoundCallback(&CSoundSystemFMod::_PlayUISoundIndexFunc);	// CYS-AUDIO: CY ClickSound = SoundUI row
	s_pDataPool			->Initial(NULL);
	s_pDataPool_		->Initial(NULL);
	s_pCursorMng		->Initial(&g_hInstance);
	s_pActionSystem		->Initial(NULL);
	s_pGameInterface	->Initial(NULL);
	s_pFakeObjSystem	->Initial(NULL);
	s_pHelperSystem		->Initial(NULL);
	s_pResourceProvider	->Initial(NULL);

#ifdef CYS_HAVE_LUAAPIGEN
	// CYS: register the generated stubs for the Lua host functions of the modern client that 0.85 lacks. Done after
	// CYS: every system's Initial(): DataPool, GameInterface, UI ... create their Lua objects/metatables there, and a
	// CYS: receiver created by LuaApiGen first would be replaced by them (UI control metatables must already exist
	// CYS: to be extended). RegisterAll never overrides an existing function, so every real implementation wins.
	LuaApiGen::SetLogSink(_CysLuaApiGenLog);
	// CYS-CYGUI2: globals, talk, input and timers are registered before CUISystem::Initial (see above).
	LuaPlus::LuaObject hostGlobals = CScriptSystem::GetMe()->GetLuaState()->GetGlobals();
	_CysLoadProvinceCatalog();
	CysHostProvinces::Register(hostGlobals, s_cysProvinces.get());
	CysHostQueries::Services queryServices;
	queryServices.bagItemTableIndex = _CysBagItemTableIndex;
	CysHostQueries::Register(hostGlobals, queryServices);
	queryServices.areaDescription = _CysLoginAreaDescription;
	CysHostQueries::RegisterLogin(hostGlobals, queryServices);
	queryServices.areaRecommendLevel = _CysAreaRecommendLevel;
	queryServices.setCurrentServerPage = _CysSetCurrentServerPage;
	queryServices.setLoginServerKeyword = _CysSetLoginServerKeyword;
	queryServices.keywordLoginServerInfo = _CysKeywordLoginServerInfo;
	CysHostQueries::RegisterGlLogin(hostGlobals, queryServices);
	CysHostHotKeys::Services hotKeyServices;
	hotKeyServices.setCanUseHotKey = _CysSetCanUseHotKey;
	CysHostHotKeys::Register(hostGlobals, hotKeyServices);
	// CYS-PLUMB: shared plumbing (Player:GetLevel / IsVaild, GetCurClientSize, DataPool:GetServerDayTime).
	// Registered BEFORE RegisterAll so IsMissing() keeps the stubs out; it also overwrites the stubs the first
	// RegisterAll (before CUISystem::Initial) may already have put into _G.
	{
		CysHostPlumbing::Services plumbing;
		plumbing.playerLevel = _CysPlayerLevel;
		plumbing.playerValid = _CysPlayerValid;
		plumbing.clientSize = _CysClientSize;
		const int n = CysHostPlumbing::Register(CScriptSystem::GetMe()->GetLuaState(), plumbing);
		TDU_Log("CYS-PLUMB: shared host functions registered=%d", n);
		// CYS-ACCEL: read-only key list of the CY remap window (AcceSetup). Rebinding stays a stub on purpose.
		const int accel = CysHostAccel::Register(CScriptSystem::GetMe()->GetLuaState());
		TDU_Log("CYS-ACCEL: GetInputSetup registered=%d rows_dispatched=%d/79", accel, CysHostAccel::SupportedRowCount());
		// CYS-SEFIT: WGAch:GetCJMaxCur is a DATA RECORD, so the generated null-object stub makes the CY
		// scripts' own `cur ~= nil` guard pass and then compare a function with a number. Measured: that
		// killed Equip_OnUpdateShow() at SelfEquip.lua:1465 on every open of the character frame, leaving
		// every attribute after that line showing the layout's design-time placeholder. See CysHost_Ach.h.
		const int sefit = CysHostSefit::Register(CScriptSystem::GetMe()->GetLuaState());
		TDU_Log("CYS-SEFIT: shape-corrected host functions registered=%d (WGAch:GetCJMaxCur, Lua_GetMyMaxLevelLimit)", sefit);
	}
	LuaApiGen::RegisterAll(CScriptSystem::GetMe()->GetLuaState());
	// CYS-CYGUI: the CY 3.71 UI scripts call host functions the GL client never registered (KaJi, Pneuma, BieYe,
	// Exterior 2018+, ...). Same telemetry, never overrides an existing function.
	LuaApiGen::RegisterCyExtras(CScriptSystem::GetMe()->GetLuaState());
#endif


	s_pSkillDataMgr	= new CSkillDataMgr;
	s_pSkillDataMgr->Init( s_pDataBaseSystem->GetDataBase( DBC_SKILL_DATA )->GetDBCFile() );

	s_pBuffImpactMgr	= new CBuffImpactMgr;
	s_pBuffImpactMgr->Init( s_pDataBaseSystem->GetDataBase( DBC_BUFF_IMPACT )->GetDBCFile() );

	s_pDirectlyImpactMgr	= new CDirectlyImpactMgr;
	s_pDirectlyImpactMgr->Init( s_pDataBaseSystem->GetDataBase( DBC_DIRECTLY_IMPACT )->GetDBCFile() );

	s_pBulletDataMgr	= new CBulletDataMgr;
	s_pBulletDataMgr->Init( s_pDataBaseSystem->GetDataBase( DBC_BULLET_DATA ) );
	//camp
	s_pCampDataMgr	= new CampAndStandDataMgr_T;
	s_pCampDataMgr->InitFromDBMemory(s_pDataBaseSystem->GetDataBase(DBC_CAMP_AND_STAND)->GetDBCFile());

	s_pActionSetMgr	= new CActionSetMgr;
	s_pActionSetMgr->Init( "ActionSet/" );

	//-------------------------------------------------------------------
	//创建Tick驱动
	INT nMaxFPS = s_pVariableSystem->GetAs_Int("System_MaxFPS");
	if(nMaxFPS < 5 || nMaxFPS >150) nMaxFPS = 30;

	m_hTickEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	::ResetEvent(m_hTickEvent);
	m_hEventTimer = ::timeSetEvent((INT)(1000.0f/nMaxFPS), (INT)(1000.0f/nMaxFPS), EventTimerProc, 0, TIME_PERIODIC|TIME_CALLBACK_FUNCTION); 
	TDU_Log("CYS-PERF pacer: System_MaxFPS=%d -> timeSetEvent period %d ms", nMaxFPS, (INT)(1000.0f/nMaxFPS)); // CYS-PERF
	// CYS-PERF-FIX pacer: B (client_GL) paces the loop with System_MaxFPS_Front / System_MaxFPS_Back; its
	// Game.exe no longer reads System_MaxFPS (the fixed 30 fps tick of 0.85). The tick event becomes a
	// high-resolution waitable timer re-armed every frame. CYS_PERF_PACER=legacy keeps the 0.85 tick.
	if (!cysPacerLegacy())
	{
		BOOL haveFront = FALSE, haveBack = FALSE;
		const INT front = s_pVariableSystem->GetAs_Int("System_MaxFPS_Front", &haveFront);
		const INT back = s_pVariableSystem->GetAs_Int("System_MaxFPS_Back", &haveBack);
		bool highResolution = false;
		HANDLE timer = cysPacerCreateTimer(highResolution);
		if (timer)
		{
			if (m_hEventTimer) ::timeKillEvent(m_hEventTimer);
			m_hEventTimer = NULL;
			::CloseHandle(m_hTickEvent);
			m_hTickEvent = timer;
			cysPacerConfigure(haveFront ? front : 0, haveBack ? back : 0);
			cysPacerArm(m_hTickEvent, true);
			TDU_Log("CYS-PERF pacer: front %d fps, back %d fps (System_MaxFPS_Front/Back), %s waitable timer",
				s_cysPacerFront, s_cysPacerBack, highResolution ? "high-resolution" : "standard");
		}
	}

	// 2006-2-27
	// 初试化 人物创建过程, 在这里面创建了男 , 女人物主角, 避免在进入游戏过程中, 创建很慢.
	if(s_pProcCharCreate)
	{
		s_pProcCharCreate->CallInit();
	}//
}

VOID CGameProcedure::ReleaseStaticMember(VOID)
{
	// World and fake-object release can emit UI events after ScriptSystem has
	// deleted its environments. Quiesce the owned UI plugin before any teardown.
	HMODULE uiModule = GetModuleHandleA("UI_CEGUI.dll");
	typedef void (__cdecl *BeginUiShutdown)();
	const auto beginUiShutdown = reinterpret_cast<BeginUiShutdown>(
		uiModule ? GetProcAddress(uiModule, "CysUiBeginShutdown") : nullptr);
	if (beginUiShutdown)
	{
		beginUiShutdown();
		TDU_Log("CYS UI: script dispatch stopped before node teardown");
	}
#ifdef CYS_HAVE_LUAAPIGEN
	// CYS: destructor kills timers and removes routes before Lua/UI teardown.
	s_cysHostTimers.reset();
	// CYS: session summary of the calls that reached LuaApiGen stubs (the distinct names are logged on first call)
	LuaApiGen::DumpMissingCallCounts(0); // CYS-HOST2: final snapshot lists EVERY stub (20 cut the counts of 77/114 functions).
	TDU_Log("LuaApiGen: %u calls to %u missing host functions this session",
		LuaApiGen::GetMissingCallCount(), LuaApiGen::GetMissingDistinctCount());
#endif
	//关闭驱动函数
	if(m_hEventTimer) ::timeKillEvent(m_hEventTimer);
	m_hEventTimer = NULL;
	CloseHandle(m_hTickEvent);
	m_hTickEvent = NULL;

	if ( s_pActionSetMgr != NULL )
	{
		s_pActionSetMgr->CleanUp();
		delete s_pActionSetMgr;
		s_pActionSetMgr = NULL;
	}
	delete s_pCampDataMgr;		s_pCampDataMgr = NULL;
	delete s_pSkillDataMgr;		s_pSkillDataMgr = NULL;
	delete s_pBulletDataMgr;	s_pBulletDataMgr = NULL;
	delete s_pBuffImpactMgr;	s_pBuffImpactMgr = NULL;
	delete s_pDirectlyImpactMgr;s_pDirectlyImpactMgr = NULL;

	//释放所有的循环实例
	if(s_pProcLogIn)			delete s_pProcLogIn;		s_pProcLogIn = NULL;
	if(s_pProcCharSel)			delete s_pProcCharSel;		s_pProcCharSel = NULL;
	if(s_pProcCharCreate)		delete s_pProcCharCreate;	s_pProcCharCreate = NULL;
	if(s_pProcEnter)			delete s_pProcEnter;		s_pProcEnter = NULL;
	if(s_pProcMain)				delete s_pProcMain;			s_pProcMain = NULL;
	if(s_pProcChangeScene)		delete s_pProcChangeScene;	s_pProcChangeScene = NULL;

	s_pProcPrev = s_pProcActive = NULL;

	// CYS: clear the cross-module callback before node teardown.
	if(s_pUISystem) s_pUISystem->SetClickSoundCallback(NULL);

	//关闭所有工作的节点
//19. 资源提供
	SAFE_RELEASE(s_pResourceProvider);
//18. 外接帮助系统
	SAFE_RELEASE(s_pHelperSystem);
//17. UI模型显示管理
	SAFE_RELEASE(s_pFakeObjSystem);
//16. 操作管理
	SAFE_RELEASE( s_pActionSystem );
//15. 事件系统
	SAFE_RELEASE( s_pEventSystem );
//14. 脚本系统
	SAFE_RELEASE( s_pScriptSystem );
#ifdef CYS_HAVE_LUAAPIGEN
	s_cysProvinces.reset(); // CYS: the Lua closures no longer borrow this catalog.
#endif
//13. 鼠标指针管理器
	SAFE_RELEASE( s_pCursorMng );
//12. UI数据池
	SAFE_RELEASE(s_pDataPool_);
	SAFE_RELEASE(s_pDataPool);
//11. 接口管理器
	SAFE_RELEASE( s_pGameInterface );
//10. 系统变量管理器
	SAFE_RELEASE( s_pVariableSystem );
//9. 数据库管理器
	SAFE_RELEASE( s_pDataBaseSystem );
//8. 世界管理器
	SAFE_RELEASE( s_pWorldManager );
//7. UI管理器
	SAFE_RELEASE( s_pUISystem );				
//6. 物体管理器
	SAFE_RELEASE( s_pObjectManager );
//5. 调试器节点
	SAFE_RELEASE( s_pDebuger );
//4. 渲染器节点
	SAFE_RELEASE( s_pGfxSystem );
//3. 音效管理器
	SAFE_RELEASE( s_pSoundSystem );
//2. 网络管理器
	SAFE_RELEASE( s_pNetManager );
//1. 计时器
	SAFE_RELEASE( s_pTimeSystem );
//0. 输入管理器
	SAFE_RELEASE( s_pInputSystem );

	//核心关闭
	g_theKernel.ShutDown();

	//释放主窗口
	DestroyMainWnd();
}

// CYS: DWORD -> DWORD_PTR: LPTIMECALLBACK takes pointer-sized user/param arguments on x64 (C2664 otherwise)
VOID CALLBACK CGameProcedure::EventTimerProc(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{ 
	SetEvent(m_hTickEvent);
} 

VOID CGameProcedure::SetActiveProc(CGameProcedure* pProcToActive)
{
	if(NULL == pProcToActive || s_pProcActive == pProcToActive) return;

	s_pProcPrev = s_pProcActive;
	s_pProcActive = pProcToActive;
}

VOID CGameProcedure::MainLoop(VOID)
try
{
#ifdef CYS_HAVE_LUAAPIGEN
	// CYS: startup reached the registered hosts even if the first UI event fails.
	TDU_Log("CYS MainLoop: host registration completed; initial missing-host snapshot");
	LuaApiGen::DumpMissingCallCounts(20);
#endif
    if (g_cysOfflineUiSmoke)
    {
        const PLOGINSERVER_INOF server = s_pProcLogIn->GetAreaLoginServerInfo(0,0);
        if (s_pVariableSystem->GetAs_Int("GameServer_ConnectDirect") != 0 ||
            s_pVariableSystem->GetAs_String("GameServer_Address") != "127.0.0.1" ||
            s_pVariableSystem->GetAs_Int("GameServer_Port") != 14400 ||
            s_pVariableSystem->GetAs_Int("System_CodePage") != 65001 ||
            s_pProcLogIn->GetAreaCount() != 1 || s_pProcLogIn->GetAreaLoginServerCount(0) != 1 ||
            !server || server->szIp != "127.0.0.1" || server->iPort != 14400)
            TDThrow("Offline UI smoke requires the isolated UTF-8 stage and one dummy server.");
        TDU_Log("CYS offline UI smoke: native server selection and render capture enabled; no credentials supplied");
    }
    const DWORD smokeStart = GetTickCount();
    std::unique_ptr<CysGlLoginSmoke> glSmoke;
    if (g_cysGlLoginSmoke)
    {
        char optIn[8] = {};
        const bool live = GetEnvironmentVariableA("CYS_WSL_LOGIN_TEST",optIn,sizeof(optIn)) == 1 && optIn[0] == '1';
        const int port = live ? 15733 : 14400;
        const PLOGINSERVER_INOF server = s_pProcLogIn->GetAreaLoginServerInfo(0,0);
        if (s_pVariableSystem->GetAs_Int("GameServer_ConnectDirect") != 0 ||
            s_pVariableSystem->GetAs_String("GameServer_Address") != "127.0.0.1" ||
            s_pVariableSystem->GetAs_Int("GameServer_Port") != port ||
            s_pVariableSystem->GetAs_Int("System_CodePage") != 65001 ||
            s_pProcLogIn->GetAreaCount() != 1 || s_pProcLogIn->GetAreaLoginServerCount(0) != 1 ||
            !server || server->szIp != "127.0.0.1" || server->iPort != port)
            TDThrow("GL smoke requires one isolated UTF-8 loopback endpoint.");
        glSmoke.reset(new CysGlLoginSmoke(live));
    }
    unsigned smokeStep = 0;
    MSG msg;
    ZeroMemory(&msg, sizeof(MSG));

	while (TRUE)
    {
		AUTO_SAMPLING theAutoSampling;

//		AxProfile::AxProfile_PushNode("Msg");
		//读取出消息队列中的所有消息并处理
		// CYS: W message pump for the Unicode main window (see CreateMainWnd): PeekMessageA would convert the
		// CYS: WM_CHAR of TranslateMessage back to ANSI (ACP) before UI_CEGUI.dll sees it.
        const LONGLONG cysPerfMsg = cysPerfBegin(); // CYS-PERF: message pump
        while(PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
			//如果是退出消息,退出
            if (msg.message == WM_QUIT)  return;

            //处理其他消息
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
#ifdef CYS_HAVE_LUAAPIGEN
			if(s_cysTimerVmFailure)
				throw std::runtime_error("timer callback bypassed protected Lua execution");
#endif
		};
cysPerfEnd(Ogre::CysPerf::PH_GAME_MSG, cysPerfMsg); // CYS-PERF
//		AxProfile::AxProfile_PopNode("Msg");

		// 等待有新的消息进入消息队列或者到达渲染时间
		const LONGLONG cysPerfWait = cysPerfBegin(); // CYS-PERF: pacer / message wait
		UINT dwResult = MsgWaitForMultipleObjects(1, &m_hTickEvent, 
							FALSE, INFINITE, QS_ALLINPUT); 
		cysPerfEnd(Ogre::CysPerf::PH_GAME_WAIT, cysPerfWait); // CYS-PERF

        if (dwResult == WAIT_OBJECT_0)
        {
            if (s_cysPacerActive) cysPacerArm(m_hTickEvent, m_bActive != FALSE); // CYS-PERF-FIX pacer: next deadline
			//进入逻辑Tick和渲染
//			AxProfile::AxProfile_PushNode("Tick");
			{ CysPerfGameScope cysPerf(Ogre::CysPerf::PH_GAME_TICK); TickActive(); } // CYS-PERF
//			AxProfile::AxProfile_PopNode("Tick");

//			AxProfile::AxProfile_PushNode("Render");
			{ CysPerfGameScope cysPerf(Ogre::CysPerf::PH_GAME_RENDER); RenderActive(); } // CYS-PERF
//			AxProfile::AxProfile_PopNode("Render");

			// CYS-FPS 17/09/2026: hiện số khung/giây ngay trên thanh tiêu đề (2 lần/giây).
			{
				extern VOID CYS_TickMainWindowTitleFps(HWND hWnd);
				CYS_TickMainWindowTitleFps(g_hMainWnd);
			}

//			AxProfile::AxProfile_PushNode("Event");
            { CysPerfGameScope cysPerf(Ogre::CysPerf::PH_GAME_EVENTS); ProcessGameEvent(); } // CYS-PERF
            // CYS-PERF: CYS_PERF_HOLD pauses the scripted smoke for a while after each procedure change
            static CGameProcedure* s_cysPerfHoldProc = NULL;
            static DWORD s_cysPerfHoldEnd = 0;
            if (glSmoke && s_pProcActive != s_cysPerfHoldProc)
            {
                s_cysPerfHoldProc = s_pProcActive;
                const DWORD hold = cysPerfHoldMs(cysPerfProcName(s_pProcActive));
                s_cysPerfHoldEnd = hold ? GetTickCount() + hold : 0;
                if (hold) TDU_Log("CYS-PERF hold: %s %lu ms", cysPerfProcName(s_pProcActive), (unsigned long)hold);
            }
            if (s_cysPerfHoldEnd && LONG(GetTickCount() - s_cysPerfHoldEnd) >= 0)
            {
                s_cysPerfHoldEnd = 0;
                cysPerfMark("hold-end");
            }
            if (glSmoke && !s_cysPerfHoldEnd)
            {
                CysPerfGameScope cysPerfHarness(Ogre::CysPerf::PH_GAME_OTHER); // CYS-PERF: smoke clicks and PNG captures
                glSmoke->tick(s_pGfxSystem,s_pProcActive == s_pProcCharSel);
            }
            if (CysWorldReplay::Active()) CysWorldReplay::Tick(); // CYS: offline world replay: log drain + automatic driver
            // CYS: exercise the real selection event and render path in a hidden offline window.
            // Capture server selection first, then the account screen. No login/auth call is made.
            if (g_cysOfflineUiSmoke && s_pProcActive == s_pProcLogIn &&
                (s_pProcLogIn->GetStatus() == CGamePro_Login::LOGIN_SELECT_SERVER ||
                 s_pProcLogIn->GetStatus() == CGamePro_Login::LOGIN_INPUT_ACCOUNT))
            {
                const DWORD age = GetTickCount()-smokeStart;
                if ((smokeStep == 0 && age >= 2000) || (smokeStep == 1 && age >= 5000))
                {
                    CreateDirectoryA("..\\ScreenShots",0);
                    char shot[MAX_PATH] = {};
                    _snprintf(shot,MAX_PATH,"..\\ScreenShots\\cys-ui-%lu-%u.png",static_cast<unsigned long>(smokeStart),smokeStep);
                    cysCaptureOfflineUi(s_pGfxSystem,shot);
                    TDU_Log("CYS offline UI smoke: capture=%u file=%s",smokeStep,shot);
                    if (smokeStep == 0) s_pProcLogIn->SelectLoginServer(0,0);
                    ++smokeStep;
                }
            }
#ifdef CYS_HAVE_LUAAPIGEN
			// CYS: persist a bounded snapshot on the game thread, including forced-stop smoke runs.
			static DWORD lastMissingDump = 0;
			static unsigned int lastMissingCalls = 0;
			const DWORD now = GetTickCount();
			const unsigned int missingCalls = LuaApiGen::GetMissingCallCount();
			if (missingCalls != lastMissingCalls && (lastMissingDump == 0 || DWORD(now - lastMissingDump) >= 5000))
			{
				LuaApiGen::DumpMissingCallCounts(20);
				lastMissingCalls = missingCalls;
				lastMissingDump = now;
			}
#endif
//			AxProfile::AxProfile_PopNode("Event");
        }
		//有新消息进入消息循环,处理消息
		else continue;
	}

}

catch (...)
{
#ifdef CYS_HAVE_LUAAPIGEN
	// CYS: retain evidence on a fatal UI event before the five-second snapshot.
	// Preserve the original exception and the existing crash-report path.
	TDU_Log("CYS MainLoop: fatal exception; missing-host snapshot before rethrow");
	LuaApiGen::DumpMissingCallCounts(20);
#endif
	throw;
}

VOID CGameProcedure::TickActive(VOID)
{
	//如果要转入新的游戏循环...
	if(s_pProcActive != s_pProcPrev) 
	{
		//调用旧循环的释放函数
		if(s_pProcPrev) s_pProcPrev->Release();
		//调用新循环的初始化函数
		if(s_pProcActive) s_pProcActive->Init();
		if(s_pProcActive) { char cysPerfLabel[48]; _snprintf(cysPerfLabel, sizeof(cysPerfLabel), "proc:%s", cysPerfProcName(s_pProcActive)); cysPerfLabel[sizeof(cysPerfLabel)-1] = 0; cysPerfMark(cysPerfLabel); } // CYS-PERF
		//开始新的循环
		s_pProcPrev = s_pProcActive;
	}
	//执行激活循环的数据逻辑
	if(s_pProcActive)	s_pProcActive->Tick();
}

VOID CGameProcedure::ProcessGameEvent(VOID)
{
	if(s_pEventSystem) 
		s_pEventSystem->ProcessAllEvent();
}

VOID CGameProcedure::RenderActive(VOID)
{
	//执行激活循环的渲染函数
	if(s_pProcActive == s_pProcPrev && s_pProcActive && !m_bMinimized && !m_bRenderingPaused)
	{
		//激活中，直接渲染
		if(m_bActive || g_cysOfflineUiSmoke || g_cysGlLoginSmoke || CysWorldReplay::Active()) // CYS: replay renders unfocused too
		{
			s_pProcActive->Render();
		}
		else
		{
			//使用窗口非法方式重绘
			InvalidateRect(g_hMainWnd, 0, FALSE);
			PostMessage(g_hMainWnd, WM_NCPAINT, TRUE, 0);
		}
	}
}

VOID CGameProcedure::Tick(VOID)
{
	//所有可执行节点的逻辑轮询

	s_pTimeSystem		->Tick();

//	AxProfile::AxProfile_PushNode("Tick_Input");
	{ CysPerfGameScope cysPerf(Ogre::CysPerf::PH_GAME_INPUT); s_pInputSystem->Tick(); } // CYS-PERF
//	AxProfile::AxProfile_PopNode("Tick_Input");

//	AxProfile::AxProfile_PushNode("Tick_UI");
	if(s_pUISystem) { CysPerfGameScope cysPerf(Ogre::CysPerf::PH_GAME_UI); s_pUISystem->Tick(); } // CYS-PERF
//	AxProfile::AxProfile_PopNode("Tick_UI");

//	AxProfile::AxProfile_PushNode("Tick_NetManager");
	{ CysPerfGameScope cysPerf(Ogre::CysPerf::PH_GAME_NET); s_pNetManager->Tick(); } // CYS-PERF
//	AxProfile::AxProfile_PopNode("Tick_NetManager");

//	AxProfile::AxProfile_PushNode("Tick_World");
	{ CysPerfGameScope cysPerf(Ogre::CysPerf::PH_GAME_WORLD); s_pWorldManager->Tick(); } // CYS-PERF
//	AxProfile::AxProfile_PopNode("Tick_World");

//	AxProfile::AxProfile_PushNode("Tick_Object");
	{ CysPerfGameScope cysPerf(Ogre::CysPerf::PH_GAME_OBJECT); s_pObjectManager->Tick(); } // CYS-PERF
//	AxProfile::AxProfile_PopNode("Tick_Object");

//	AxProfile::AxProfile_PushNode("Tick_Debug");
	if(s_pDebuger)s_pDebuger->Tick(); 
//	AxProfile::AxProfile_PopNode("Tick_Debug");

//	AxProfile::AxProfile_PushNode("Tick_Sound");
	s_pSoundSystem		->Tick();
//	AxProfile::AxProfile_PopNode("Tick_Sound");

//	AxProfile::AxProfile_PushNode("Tick_Gfx");
	s_pGfxSystem		->Tick();
//	AxProfile::AxProfile_PopNode("Tick_Gfx");

//	AxProfile::AxProfile_PushNode("Tick_DataPool");
	s_pDataPool			->Tick();
	s_pDataPool_		->Tick();
//	AxProfile::AxProfile_PopNode("Tick_DataPool");

//	AxProfile::AxProfile_PushNode("Tick_Action");
	s_pActionSystem		->Tick();
//	AxProfile::AxProfile_PopNode("Tick_Action");

//	AxProfile::AxProfile_PushNode("Tick_Fake");
	s_pFakeObjSystem	->Tick();
//	AxProfile::AxProfile_PopNode("Tick_Fake");

	{ CysPerfGameScope cysPerf(Ogre::CysPerf::PH_GAME_SCRIPT); s_pScriptSystem->Tick(); } // CYS-PERF

//	PROFILE_POP(MainTick);

}

VOID CGameProcedure::ProcessActiveInput(VOID)
{
	//执行激活循环的键盘函数
	if(s_pProcActive == s_pProcPrev && s_pProcActive )
	{
		s_pProcActive->ProcessInput();
	}
}

VOID CGameProcedure::ProcessCloseRequest(VOID)
{
	//执行激活循环的渲染函数
	if(s_pProcActive == s_pProcPrev && s_pProcActive )
	{
		s_pProcActive->CloseRequest();
	}
}

VOID CGameProcedure::ProcessInput(VOID)
{

}

VOID CGameProcedure::CloseRequest(VOID)
{
}

// CYS-LOGIN: A/B switch CYS_LOGIN_WINDOW=0 keeps the View_Resoution window on the login screens (behaviour before 15/09).
static bool CysLoginWindowEnabled(VOID)
{
	const char* v = ::getenv("CYS_LOGIN_WINDOW");
	return !(v && v[0] == '0' && v[1] == 0);
}

VOID CGameProcedure::CreateMainWnd(VOID)
{
	//------------------------------------------------------
	//计算窗口大小
	m_bMinimized		= FALSE;
	m_bFullScreen		= TRUE;
	fVector2 fResoution;
	if (CysLoginWindowEnabled())
	{
		// CYS-LOGIN: CY 3.71 CreateMainWnd (Game.exe FUN_140623bd0) ignores View_Resoution / View_FullScreen: a
		// 1280x720 client, style 0xCA0000 (= DEFWINDOW_STYLE), centred on SM_CXFULLSCREEN / SM_CYFULLSCREEN, and no
		// resolution events; the login screens keep that size and CGamePro_CharSel/CharCreate::Init apply the
		// configured size (CysLoginWindowRestore).
		fResoution.x = 1280.0f;
		fResoution.y = 720.0f;
	}
	else if (CGameProcedure::s_pVariableSystem->GetAs_Int("View_FullScreen"))
	{
		fResoution.x  = (float)::GetSystemMetrics(SM_CXSCREEN);
		fResoution.y  = (float)::GetSystemMetrics(SM_CYSCREEN);	
	}
	else
	{
		fResoution = s_pVariableSystem->GetAs_Vector2("View_Resoution");
	}
	CGameProcedure::m_fWindowFOV = fResoution.x / fResoution.y;
	SetRect(&m_rectWindow, 0, 0, (int)fResoution.x, (int)fResoution.y);
	AdjustWindowRect(&m_rectWindow, DEFWINDOW_STYLE, FALSE);

	SetRect(&m_rectFCOffset, m_rectWindow.left, m_rectWindow.top, 
		m_rectWindow.right-(int)fResoution.x, m_rectWindow.bottom-(int)fResoution.y);

	UINT dwX = (::GetSystemMetrics(SM_CXFULLSCREEN)-(m_rectWindow.right-m_rectWindow.left))/2;
	UINT dwY = (::GetSystemMetrics(SM_CYFULLSCREEN)-(m_rectWindow.bottom-m_rectWindow.top))/2;
	OffsetRect(&m_rectWindow, -m_rectFCOffset.left, -m_rectFCOffset.top);
	OffsetRect(&m_rectWindow, dwX, dwY);

	m_bActive			= true;
	m_bRenderingPaused	= FALSE;

	//------------------------------------------------------
	//注册窗口类
	// CYS: the main window is a Unicode (W) window: WM_CHAR/IME input reaches UI_CEGUI.dll as UTF-16 (CEGUIIme injects
	// CYS: full code points) instead of being squeezed through the ANSI code page (1252 on this machine: '?' for
	// CYS: Vietnamese). Replaces the client_GL import patch A->W + SetWindowLongPtrW hook (Docs/08 section 10).
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEXW);
	wcex.style			= CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wcex.lpfnWndProc	= (WNDPROC)_MainWndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= g_hInstance;
	wcex.hIcon			= LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_GAME));	// CYS: main icon resource, not the dialog ID.
	wcex.hCursor		= LoadCursorW( g_hInstance, MAKEINTRESOURCEW(IDC_CURSOR_GL_NORMAL) ); // CYS: cursor
	wcex.hbrBackground	= (HBRUSH)NULL; //GetStockObject(WHITE_BRUSH);
	wcex.lpszMenuName	= NULL;
	wcex.lpszClassName	= L"WuXiaShiJie WndClass";	// CYS: = MAINWINDOW_CLASS
	wcex.hIconSm		= LoadIconW(wcex.hInstance, MAKEINTRESOURCEW(IDI_SMALL));

	RegisterClassExW(&wcex);

	//------------------------------------------------------
	//创建窗口
	CHAR szTitle[MAX_PATH];
	_snprintf(szTitle, MAX_PATH, "%s %s (%s %s)", GAME_TITLE, VERSION_INFO, __DATE__, __TIME__);
	// CYS: GAME_TITLE is a GBK literal (/execution-charset:.936), so the title is converted with code page 936.
	{	// CYS-FPS: ghi lại phần NỀN của tiêu đề để MainLoop nối hậu tố FPS vào sau.
		extern VOID CYS_SetMainWindowTitleBase(LPCSTR szTitle);
		CYS_SetMainWindowTitleBase(szTitle);
	}
	WCHAR wszTitle[MAX_PATH] = {0};
	::MultiByteToWideChar(936, 0, szTitle, -1, wszTitle, MAX_PATH);

	HWND hWnd = CreateWindowExW(0, wcex.lpszClassName, wszTitle,
		DEFWINDOW_STYLE,
		m_rectWindow.left, m_rectWindow.top,
		m_rectWindow.right-m_rectWindow.left,
		m_rectWindow.bottom-m_rectWindow.top,
		NULL, NULL, g_hInstance, NULL);

	if (!hWnd)
	{
		TDThrow(_T("Can't create main window!"));
	}

	if (CysLoginWindowEnabled())
	{
		// CYS-LOGIN: CY pushes no View_FullScreen / View_Resoution change here (they would resize the window back).
		s_pEventSystem->PushEvent(GE_CHAT_ADJUST_MOVE_CTL, (INT)fResoution.x, (INT)fResoution.y);
	}
	else if (CGameProcedure::s_pVariableSystem->GetAs_Int("View_FullScreen"))
	{
		s_pEventSystem->PushEvent(GE_VARIABLE_CHANGED, "View_FullScreen", "1");
		s_pEventSystem->PushEvent(GE_CHAT_ADJUST_MOVE_CTL, (INT)fResoution.x, (INT)fResoution.y);
	}else
	{
		const STRING& strVariable = CGameProcedure::s_pVariableSystem->GetAs_String("View_Resoution");
		s_pEventSystem->PushEvent(GE_VARIABLE_CHANGED,"View_Resoution", strVariable.c_str());
		s_pEventSystem->PushEvent(GE_CHAT_ADJUST_MOVE_CTL, (INT)fResoution.x, (INT)fResoution.y);
	}

	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);

	g_hMainWnd = hWnd;
}

VOID CGameProcedure::DestroyMainWnd(VOID)
{
	DestroyWindow( g_hMainWnd );
	UnregisterClassW( L"WuXiaShiJie WndClass", g_hInstance );	// CYS: W class (CreateMainWnd); was UnregisterClass(.., NULL)
	g_hMainWnd = NULL;
}

//消息响应函数
LRESULT CALLBACK CGameProcedure::_MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	//--------------------------------------------------------------
	//首先让输入管理器处理消息
	if(s_pInputSystem && s_pInputSystem->MessageProc( hWnd, message, wParam, lParam ))
		return 0;

	//--------------------------------------------------------------
	//UI处理文字输入
	if(s_pUISystem && s_pUISystem->MessageProc( hWnd, message, wParam, lParam ))
		return 0;

	//执行激活循环的消息响应函数
	if(s_pProcActive )
	{
		return s_pProcActive->MainWndProc(hWnd, message, wParam, lParam);
	}
	else return DefWindowProcW(hWnd, message, wParam, lParam);	// CYS: W window
}

//缺省消息响应函数,可被相应的流程继承
LRESULT CGameProcedure::MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	//--------------------------------------------------------------
	//处理一些缺省的消息
	switch (message) 
	{
	//窗口大小发生改变
	case WM_SIZE:
		{
			//最小化
            if( SIZE_MINIMIZED == wParam )
            {
                m_bRenderingPaused = true; // 最小化时渲染暂停

                m_bMinimized = true;
				m_bMaximized = FALSE;
            }
			//最大化
            else if( SIZE_MAXIMIZED == wParam )
            {
                m_bRenderingPaused = FALSE; //恢复渲染工作(可能上一个状态时最小化)

				m_bMinimized = FALSE;
				m_bMaximized = true;
				HandlePossibleSizeChange( message, wParam, lParam);
			}
			//恢复窗口
            else if( SIZE_RESTORED == wParam )
            {
                if( m_bMaximized )	//刚才是最大化
                {
                    m_bMaximized = FALSE;
                    HandlePossibleSizeChange(message, wParam, lParam);
                }
                else if( m_bMinimized )	//刚才是最小化
                {
                    m_bRenderingPaused = FALSE; 

					m_bMinimized = FALSE;
                    HandlePossibleSizeChange(message, wParam, lParam);
                }
                else
                {
					//如果上一个状态既不是最大化也不是最小化,显然这次的WM_SIZE
					//消息是因为用户拖动窗口边缘引起的，这种情况我们并不马上恢复
					//设备，我们等待用户停止拖动后在处理，也就是对WM_EXITSIZEMOVE
					//的处理...
                }
            }
		}
		break;

	//用户拖动窗口开始
	case WM_ENTERSIZEMOVE:
		{
			m_bRenderingPaused = true;
		}
		break;
	//用户拖动中
	case WM_SIZING:
		{
			RECT* pRect = (RECT*)lParam;

			switch(wParam)
			{
			case WMSZ_RIGHT:
			case WMSZ_BOTTOMRIGHT:
				{
					KeepWindowFOV(pRect, WMSZ_RIGHT, WMSZ_TOPLEFT);
				}
				break;
			case WMSZ_LEFT:
			case WMSZ_BOTTOMLEFT:
				{
					KeepWindowFOV(pRect, WMSZ_LEFT, WMSZ_TOPRIGHT);
				}
				break;

			case WMSZ_TOP:
 			case WMSZ_TOPRIGHT:
				{
					KeepWindowFOV(pRect, WMSZ_TOP, WMSZ_BOTTOMLEFT);
				}
				break;

			case WMSZ_BOTTOM:
				{
					KeepWindowFOV(pRect, WMSZ_BOTTOM, WMSZ_TOPLEFT);
				}
				break;

			case WMSZ_TOPLEFT:
				{
					KeepWindowFOV(pRect, WMSZ_TOP, WMSZ_BOTTOMRIGHT);
				}
				break;
			}
			return TRUE;
		}
		break;
	//窗口尺寸发生了改变后
	case WM_WINDOWPOSCHANGING:
		// fujia 2008.1.23 防止窗口模式窗口大小发生变化
		//{    
		//	WINDOWPOS* pPos = (WINDOWPOS*)lParam;
		//	//非大小改变
		//	if(pPos->flags & SWP_NOSIZE || pPos->flags & SWP_DRAWFRAME) break;
		//	//是“全屏”状态
		//	if(s_pVariableSystem && 
		//		s_pVariableSystem->GetAs_Int("View_FullScreen")) break;

		//	//计算新的Client
		//	RECT rectFrame;
		//	SetRect(&rectFrame, 0, 0, pPos->cx, pPos->cy);
		//	OffsetRect(&rectFrame, pPos->x, pPos->y);

		//	RECT rectNewFrame;
		//	CopyRect(&rectNewFrame, &rectFrame);
		//	KeepWindowFOV(&rectNewFrame, -1, WMSZ_TOPLEFT);

		//	//如果相同不处理
		//	if(EqualRect(&rectFrame, &rectNewFrame)) break;

		//	pPos->x = rectNewFrame.left;
		//	pPos->y = rectNewFrame.top;
		//	pPos->cx = rectNewFrame.right-rectNewFrame.left;
		//	pPos->cy = rectNewFrame.bottom-rectNewFrame.top;

		//	return 0;
		//}
		break;

	//桌面分辨率改变
	case WM_DISPLAYCHANGE:
		{
			if( s_pEventSystem && s_pVariableSystem->GetAs_Int("View_FullScreen"))
			{
				s_pEventSystem->PushEvent(GE_VARIABLE_CHANGED, "View_FullScreen", "1");
			}
			//重置分辨率
			if(m_bMaximized)
			{
				ShowWindow(g_hMainWnd, SW_RESTORE);
				ShowWindow(g_hMainWnd, SW_MAXIMIZE);
			}
			else 
			{
				s_pEventSystem->PushEvent(GE_VARIABLE_CHANGED, "View_Resoution", 
					s_pVariableSystem->GetAs_String("View_Resoution").c_str());
			}
			break;
		}
	//用户拖动窗口边缘结束
	case WM_EXITSIZEMOVE:
		{
			m_bRenderingPaused = FALSE;
			HandlePossibleSizeChange(message, wParam, lParam);

			//设置分辨率变量
			if(s_pVariableSystem)
			{
				RECT rect;
				GetClientRect(hWnd, &rect);
				char szTemp[MAX_PATH];
				_snprintf(szTemp, MAX_PATH, "%d,%d", rect.right-rect.left, rect.bottom-rect.top);

				s_pVariableSystem->SetVariable("View_Resoution", szTemp, FALSE, FALSE);
				
			}
		}
		break;

	//决定窗口最大最小尺寸
	case WM_GETMINMAXINFO:
		{
			MINMAXINFO* pMinMaxInfo = (MINMAXINFO*)lParam;

			pMinMaxInfo->ptMinTrackSize.x = MINWINDOW_WIDTH;
			pMinMaxInfo->ptMinTrackSize.y = MINWINDOW_HEIGHT;

			//最大尺寸为“窗口最大化”时的大小
			RECT rect;
			SetRect(&rect, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
			AdjustWindowRect(&rect, GetWindowStyle(g_hMainWnd), FALSE);

			pMinMaxInfo->ptMaxSize.x = rect.right-rect.left;
			pMinMaxInfo->ptMaxSize.y = rect.bottom-rect.top;

			pMinMaxInfo->ptMaxTrackSize.x = rect.right-rect.left;
			pMinMaxInfo->ptMaxTrackSize.y = rect.bottom-rect.top;
		}
		break;

	//设置光标
	case WM_SETCURSOR:
		{
			switch(LOWORD(lParam))
			{
			case HTCLIENT:
				{
					if(s_pCursorMng)
					{
						s_pCursorMng->OnSetCursor();
						return TRUE;
					}
				}
				break;

			default:
				break;
			}
		}
		break;

	//窗口激活切换
	case WM_ACTIVATEAPP:
		cysPerfMark(wParam ? "app-active" : "app-inactive"); // CYS-PERF-C2: pacer front/back switch
		if( wParam == TRUE )
		{
			m_bActive = TRUE;
		}
		else 
		{
			m_bActive = FALSE;
		}
		//通知输入器
		if(s_pInputSystem) ((CInputSystem*)s_pInputSystem)->OnActiveApp(m_bActive);
		break;
	//消息主循环移到菜单中
	case WM_ENTERMENULOOP:
		{
			m_bRenderingPaused = true;
		}
		break;

	//消息主循环移出菜单
	case WM_EXITMENULOOP:
		{
			m_bRenderingPaused = FALSE;
		}
		break;
	
	//系统按键
	case WM_SYSCOMMAND:
		{
			switch(wParam)
			{
			//屏蔽F10
			case SC_KEYMENU:
				return 1;

			default:
				break;
			}
		}
		break;
	
	//窗口重绘消息
	case WM_PAINT:
		{
			PAINTSTRUCT psStruct;
			::BeginPaint(hWnd, &psStruct);
			::EndPaint(hWnd, &psStruct);
			s_pGfxSystem->OnPaint();

		}
		break;

	//窗口背景刷
	case WM_ERASEBKGND:
		{
			return 1;
		}
		break;

	//处理玩家的退出请求
	case WM_CLOSE:
		{
			//主流程中,弹出设置菜单
			if(GetActiveProcedure() == s_pProcMain && s_pUISystem)
			{
				if(wParam==0xC0DE && lParam==0XC0DE)
				{
					ProcessCloseRequest();
					break;
				}
				else
				{
					s_pEventSystem->PushEvent( GE_TOGLE_SYSTEMFRAME );
					return TRUE;
				}
			}
		}
		break;
	//窗口销毁
	case WM_DESTROY:
		{
			PostQuitMessage(0);
		}
		break;
	//输入法改变
	case WM_INPUTLANGCHANGE:
		{
			if(s_pEventSystem)
			{
				s_pEventSystem->PushEvent(GE_CHAT_INPUTLANGUAGE_CHANGE);
			}
		}
		break;
	case WM_IME_NOTIFY:
		{
			if(wParam == IMN_SETOPENSTATUS || wParam == IMN_SETCONVERSIONMODE)
			{
				if(s_pEventSystem)
				{
					s_pEventSystem->PushEvent(GE_CHAT_INPUTLANGUAGE_CHANGE);
				}
			}
		}
		break;
	default:
		break;
	}

	return DefWindowProcW( hWnd, message, wParam, lParam );	// CYS: W window
}

// CYS: title setter for the Unicode main window. Titles are built from GAME_TITLE, a GBK literal
// CYS: (/execution-charset:.936), plus ASCII, so they are converted with code page 936.
//
// CYS-FPS 17/09/2026: tiêu đề nay gồm hai phần — phần NỀN do các luồng đặt (lúc tạo cửa sổ, lúc vào
// game) và hậu tố "| FPS: n" do MainLoop cập nhật. Giữ phần nền trong biến static để đổi cảnh xong
// vẫn còn FPS, và để hậu tố không bị nối chồng lên chính nó.
static CHAR s_cysTitleBase[MAX_PATH] = {0};

// CYS-FPS: chỉ ghi phần nền, không đụng tới cửa sổ (dùng lúc tạo cửa sổ, khi tiêu đề đã được
// CreateWindowExW đặt sẵn).
VOID CYS_SetMainWindowTitleBase(LPCSTR szTitle)
{
	if (!szTitle)
		return;
	_snprintf(s_cysTitleBase, MAX_PATH, "%s", szTitle);
	s_cysTitleBase[MAX_PATH - 1] = 0;
}

VOID CYS_SetMainWindowTitle(HWND hWnd, LPCSTR szTitle)
{
	if (szTitle)
	{
		_snprintf(s_cysTitleBase, MAX_PATH, "%s", szTitle);
		s_cysTitleBase[MAX_PATH - 1] = 0;
	}
	WCHAR wszTitle[MAX_PATH] = {0};
	::MultiByteToWideChar(936, 0, szTitle, -1, wszTitle, MAX_PATH);
	::SetWindowTextW(hWnd, wszTitle);
}

// CYS-FPS: gọi mỗi khung; chỉ thật sự đặt lại tiêu đề 2 lần/giây (SetWindowTextW đi qua hàng đợi
// thông điệp của cửa sổ, gọi mỗi khung là phí). Số lấy từ tTimeSystem::GetFPS (trung bình theo
// TDTimeSystem.cpp:73), không tự đếm lại.
VOID CYS_TickMainWindowTitleFps(HWND hWnd)
{
	if (!hWnd || !s_cysTitleBase[0] || !CGameProcedure::s_pTimeSystem)
		return;

	static DWORD s_dwNextUpdate = 0;
	const DWORD dwNow = ::GetTickCount();
	if (s_dwNextUpdate && LONG(dwNow - s_dwNextUpdate) < 0)
		return;
	s_dwNextUpdate = dwNow + 500;

	CHAR szTitle[MAX_PATH];
	_snprintf(szTitle, MAX_PATH, "%s | FPS: %.0f", s_cysTitleBase,
		CGameProcedure::s_pTimeSystem->GetFPS());
	szTitle[MAX_PATH - 1] = 0;

	WCHAR wszTitle[MAX_PATH] = {0};
	::MultiByteToWideChar(936, 0, szTitle, -1, wszTitle, MAX_PATH);
	::SetWindowTextW(hWnd, wszTitle);
}

//用以保持窗口比例不发生改变
//最大化除外
VOID CGameProcedure::KeepWindowFOV(RECT* pRect, UINT dwChanging, UINT dwAnchor)
{
	RECT rectTemp;
	CopyRect(&rectTemp, pRect);

	rectTemp.left -= m_rectFCOffset.left;
	rectTemp.top -= m_rectFCOffset.top;
	rectTemp.right -= m_rectFCOffset.right;
	rectTemp.bottom -= m_rectFCOffset.bottom;

	if(WMSZ_LEFT == dwChanging || WMSZ_RIGHT == dwChanging)
	{
		//宽不变
		rectTemp.bottom = rectTemp.top + (INT)((rectTemp.right-rectTemp.left)*m_fWindowFOV);
	}
	else if(WMSZ_TOP == dwChanging || WMSZ_BOTTOM == dwChanging)
	{
		//高不变
		rectTemp.right = rectTemp.left + (INT)((rectTemp.bottom-rectTemp.top)/m_fWindowFOV);
	}
	else
	{
		//根据比例自动调节
		FLOAT fFov = (FLOAT)(rectTemp.bottom-rectTemp.top)/(FLOAT)(rectTemp.right-rectTemp.left);
		if(fFov > m_fWindowFOV)
		{
			rectTemp.bottom = rectTemp.top + (INT)((rectTemp.right-rectTemp.left)*m_fWindowFOV);
		}
		else
		{
			rectTemp.right = rectTemp.left + (INT)((rectTemp.bottom-rectTemp.top)/m_fWindowFOV);
		}
	}

	AdjustWindowRect(&rectTemp, DEFWINDOW_STYLE, FALSE);

	switch(dwAnchor)
	{
	case WMSZ_TOPLEFT:
		{
			pRect->right = pRect->left + (rectTemp.right-rectTemp.left);
			pRect->bottom = pRect->top + (rectTemp.bottom-rectTemp.top);
		}
		break;

	case WMSZ_TOPRIGHT:
		{
			pRect->left = pRect->right - (rectTemp.right-rectTemp.left);
			pRect->bottom = pRect->top + (rectTemp.bottom-rectTemp.top);
		}
		break;

	case WMSZ_BOTTOMLEFT:
		{
			pRect->right = pRect->left + (rectTemp.right-rectTemp.left);
			pRect->top = pRect->bottom - (rectTemp.bottom-rectTemp.top);
		}
		break;

	case WMSZ_BOTTOMRIGHT:
		{
			pRect->left = pRect->right - (rectTemp.right-rectTemp.left);
			pRect->top = pRect->bottom - (rectTemp.bottom-rectTemp.top);
		}
		break;
	}
}

namespace
{
	// CYS-LOGIN: the move shared by CY FUN_1406302b0 / FUN_14063c8e0: client rect -> window rect with the current style,
	// centred in the work area (clamped to >= 0), window at least 800x600, SW_RESTORE, MoveWindow, then the size-change
	// notification CY sends as FUN_140620a30(WM_EXITSIZEMOVE) (renderer, UI, event) without touching View_Resoution.
	VOID CysMoveMainWindowClient(INT nWidth, INT nHeight)
	{
		RECT rectWindow;
		SetRect(&rectWindow, 0, 0, nWidth, nHeight);
		AdjustWindowRect(&rectWindow, (DWORD)::GetWindowLongPtrW(g_hMainWnd, GWL_STYLE), FALSE);
		RECT rectWork;
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &rectWork, 0);
		INT x = ((rectWindow.left - rectWindow.right) - rectWork.left + rectWork.right) / 2;
		INT y = ((rectWindow.top - rectWindow.bottom) - rectWork.top + rectWork.bottom) / 2;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		INT w = rectWindow.right - rectWindow.left;
		INT h = rectWindow.bottom - rectWindow.top;
		if (w < 800) w = 800;
		if (h < 600) h = 600;
		::ShowWindow(g_hMainWnd, SW_RESTORE);
		::MoveWindow(g_hMainWnd, x, y, w, h, TRUE);
		CGameProcedure::HandlePossibleSizeChange(WM_EXITSIZEMOVE, 0, 0);
	}
}

VOID CGameProcedure::CysLoginWindowFixed(VOID)
{
	if (!g_hMainWnd || !CysLoginWindowEnabled()) return;
	// CYS-LOGIN: FUN_1406302b0, called by CGamePro_Login::Init after clearing WS_THICKFRAME | WS_MAXIMIZEBOX (0x50000).
	// CY does it whatever View_FullScreen says; the CY login art (loginSplash*.jpg 1280x720) and the login camera
	// (aspect 16:9 read from the running CY process) are made for this size.
	RECT rectClient;
	GetClientRect(g_hMainWnd, &rectClient);
	if (rectClient.right - rectClient.left == 1280 && rectClient.bottom - rectClient.top == 720 && !IsZoomed(g_hMainWnd))
		return;
	CysMoveMainWindowClient(1280, 720);
}

VOID CGameProcedure::CysLoginWindowRestore(VOID)
{
	if (!g_hMainWnd || !s_pVariableSystem || !CysLoginWindowEnabled()) return;
	// CYS-LOGIN: FUN_14063c8e0 / FUN_140641c20: frame back (style | 0x50000), then full screen -> screen size, otherwise
	// the View_Resoution client size when it differs from the current client.
	LONG_PTR lStyle = ::GetWindowLongPtrW(g_hMainWnd, GWL_STYLE);
	::SetWindowLongPtrW(g_hMainWnd, GWL_STYLE, lStyle | WS_THICKFRAME | WS_MAXIMIZEBOX);
	fVector2 fResoution = s_pVariableSystem->GetAs_Vector2("View_Resoution");
	if (s_pVariableSystem->GetAs_Int("View_FullScreen") == 1)
	{
		RECT rectWindow;
		SetRect(&rectWindow, 0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
		AdjustWindowRect(&rectWindow, (DWORD)::GetWindowLongPtrW(g_hMainWnd, GWL_STYLE), FALSE);
		::ShowWindow(g_hMainWnd, SW_RESTORE);
		::MoveWindow(g_hMainWnd, rectWindow.left, rectWindow.top, rectWindow.right - rectWindow.left,
			rectWindow.bottom - rectWindow.top, TRUE);
		HandlePossibleSizeChange(WM_EXITSIZEMOVE, 0, 0);
		return;
	}
	RECT rectClient;
	GetClientRect(g_hMainWnd, &rectClient);
	if ((INT)fResoution.x != rectClient.right - rectClient.left || (INT)fResoution.y != rectClient.bottom - rectClient.top)
		CysMoveMainWindowClient((INT)fResoution.x, (INT)fResoution.y);
}

VOID CGameProcedure::HandlePossibleSizeChange(UINT message, WPARAM wParam, LPARAM lParam)
{
	//------------------------------------------------------
	//检查合法性
	if(!g_hMainWnd || !s_pGfxSystem) return;

	RECT rect;
	GetClientRect(g_hMainWnd, &rect);

	//------------------------------------------------------
	//恢复设备
	s_pGfxSystem->OnSizeChange(message, wParam, lParam);
	if(s_pUISystem)s_pUISystem->OnSizeChange(message, wParam, lParam);

	if(s_pEventSystem)
	{
		s_pEventSystem->PushEvent(GE_VIEW_RESOLUTION_CHANGED, (INT)message);
	}

	if(s_pObjectManager)
	{
		((CObjectManager*)s_pObjectManager)->OnScreenSizeChanged();
	}

	if(s_pDebuger)
	{
		if(rect.right>rect.left && rect.bottom>rect.top)
		{
			s_pDebuger->InfoTrack_Reset(rect.right-rect.left, rect.bottom-rect.top);
		}
	}
}

// 得到前一个游戏流程.
CGameProcedure*	CGameProcedure::GetPreProcedure()
{
	return s_pProcPrev;
}
