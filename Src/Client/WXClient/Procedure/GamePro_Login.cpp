#include "StdAfx.h"
#include "Gamepro_login.h"
#include "CysHostQueries.h"
#if defined(CYS_HAVE_NETCORE)
#include "../../NetCore/LoginClient.h"
#include "../../NetCore/ServerProfile.h" // CYS: Patch/CysServerProfile.txt endpoint policy.
#include "PgPackets_login.h"
#include <fstream>
#include <iterator>
#endif
#include "..\Network\CysModernGame.h" // CYS: game socket hand-off after CLAskCharLogin.
#include "..\Network\CysWorldReplay.h" // CYS: offline world replay (CYS_WORLD_REPLAY), no socket.
#include "Gamepro_ChangeScene.h"
#include "TDUtil.h"
#include "GamePro_CharCreate.h"
#include "CysLoginList.h" // CYS: UTF-8 plaintext modern login list.
#include "GamePro_Enter.h"
#include "GamePro_CharSel.h"
#include "CysLoginScene.h" // CYS-CHARSEL: CY 3D login background.
#include "..\Global.h"
#include "..\Network\NetManager.h"
#include "..\World\WorldManager.h"
#include "..\Object\ObjectManager.h"
#include "..\Object\Character\Obj_PlayerMySelf.h"
#include "CGConnect.h"
#include "CGEnterScene.h"
#include "TDVariable.h"
#include "TDInputSystem.h"
#include "TDEventDefine.h"
#include "TDUISystem.h"
#include "AxProfile.h"
#include "..\Event\GMEventSystem.h"
#include <string>


#include "CLConnect.h"					// 登录__服务器同步消息
#include "CLAskLogin.h"					// 登录__发送验证用户名密码
#include "CLAskCharList.h"				// 登录__发送得到角色消息
#include "..\WxRender\RenderSystem.h"

#if defined(CYS_HAVE_NETCORE)
// CYS: server profile resolution for the modern (skynet) login. Endpoints, game-port policy,
// host allow-list and account filter come from Patch/CysServerProfile.txt (written by
// Tools/game/stage_wsl_test.py); nothing is compiled in. Legacy compatibility: without that
// file, CYS_WSL_LOGIN_TEST=1 keeps the historic clone profile of older stages.
namespace
{
    enum class CysProfileLookup { None, Found, Rejected };
    const char* const kCysServerProfilePath = "..\\Patch\\CysServerProfile.txt";

    CysProfileLookup CysResolveServerProfile(const LOGINSERVER_INOF* server, const char* account,
                                             cys::net::ServerProfile& out, const char*& message)
    {
        char optIn[8] = {};
        const bool legacyOptIn = GetEnvironmentVariableA("CYS_WSL_LOGIN_TEST", optIn, sizeof(optIn)) == 1 && optIn[0] == '1';
        const DWORD attributes = GetFileAttributesA(kCysServerProfilePath);
        message = u8"Cấu hình đăng nhập thử nghiệm không khớp.";
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            {
                TDU_Log("CYS server profile: cannot inspect Patch/CysServerProfile.txt error=%lu", error);
                message = u8"Không đọc được hồ sơ máy chủ (Patch/CysServerProfile.txt).";
                return CysProfileLookup::Rejected;
            }
            if (!legacyOptIn) return CysProfileLookup::None;
            // Compatibility profile of stages built before CysServerProfile.txt existed.
            out = cys::net::ServerProfile();
            out.name = "wsl-test-legacy";
            out.login.host = "127.0.0.1"; out.login.port = 15733;
            out.gamePort = 15734;
            out.allowedHosts.push_back("127.0.0.1");
            out.accounts.push_back("test00");
        }
        else
        {
            std::ifstream file(kCysServerProfilePath, std::ios::binary);
            std::string text;
            if (file)
            {
                file.seekg(0, std::ios::end);
                const std::streamoff size = file.tellg();
                file.seekg(0, std::ios::beg);
                if (size >= 0 && static_cast<size_t>(size) <= cys::net::MaxServerProfileBytes)
                    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                else
                    text.assign(cys::net::MaxServerProfileBytes + 1, ' ');
            }
            else
            {
                TDU_Log("CYS server profile: Patch/CysServerProfile.txt could not be opened");
                message = u8"Không đọc được hồ sơ máy chủ (Patch/CysServerProfile.txt).";
                return CysProfileLookup::Rejected;
            }
            const cys::net::ServerProfileParse parsed = cys::net::ParseServerProfiles(text);
            if (!parsed.ok)
            {
                TDU_Log("CYS server profile: invalid file line=%d error=%s", parsed.line, parsed.error);
                message = u8"Hồ sơ máy chủ (Patch/CysServerProfile.txt) không hợp lệ.";
                return CysProfileLookup::Rejected;
            }
            const cys::net::ServerProfile* found = server ?
                cys::net::FindServerProfile(parsed.profiles, server->szIp, server->iPort) : nullptr;
            if (!found)
            {
                if (!legacyOptIn) return CysProfileLookup::None; // this server keeps the 0.85 path
                TDU_Log("CYS server profile: CYS_WSL_LOGIN_TEST=1 but the selected server has no profile");
                return CysProfileLookup::Rejected;
            }
            out = *found;
        }
        if (!server || server->szIp != out.login.host || server->iPort != static_cast<int>(out.login.port))
        {
            TDU_Log("CYS server profile: %s does not match the selected server", out.name.c_str());
            return CysProfileLookup::Rejected;
        }
        if (!account || !cys::net::ProfileAllowsAccount(out, account))
        {
            // CYS-CY (cs2-world): the account name is not a secret; log it (length-capped) so a GUI layer that rewrites
            // the editbox (CY LogOn.lua) can be diagnosed without a debugger.
            TDU_Log("CYS server profile: %s account filter rejected the typed account '%.*s' (%u bytes)", out.name.c_str(),
                account ? (int)std::min<size_t>(strlen(account), 48) : 0, account ? account : "",
                account ? (unsigned)strlen(account) : 0u);
            message = u8"Tài khoản này không được phép trên máy chủ đã chọn.";
            return CysProfileLookup::Rejected;
        }
        return CysProfileLookup::Found;
    }
}
#endif


CGamePro_Login::CGamePro_Login()
{
	//m_Status = LOGIN_DEBUG_SETTING;
	m_Status = LOGIN_SELECT_SERVER;
	m_bReLogin = FALSE;
	m_pSoundSource = NULL;
	m_iCurSelAreaId = m_iCurSelLoginServerId = -1;
	m_szLoginServerAddr[0] = 0;
	m_nLoginServerPort = 0;

	// 读取服务器配置信息.
    // CYS: prefer the modern plaintext list; never hide an invalid file by
    // falling back to the legacy INI. This happens before any connection.
    const char* modernPath = "..\\Patch\\LoginServer.txt";
    const DWORD attributes = GetFileAttributesA(modernPath);
    const DWORD fileError = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        const auto areas = CysLoginList::LoadFile(modernPath);
        std::vector<AREA_INFO> loaded;
        for (const auto& area : areas)
        {
            AREA_INFO info;
            info.szAreaName = area.name;
            info.szAreaDescription = area.description;
            info.iRecommendLevel = area.recommendLevel;
            info.iProvide = area.servers.empty() ? NET_PROVIDE_OTHER : static_cast<NET_PROVIDE>(area.servers[0].provider);
            for (const auto& server : area.servers)
            {
                LOGINSERVER_INOF item;
                item.szLoginServerName = server.name;
                item.iProvider = server.provider;
                item.iServerId = server.serverId;
                item.iRowOrdinal = server.rowOrdinal;
                item.iRecommendLevel = server.recommendLevel;
                item.bIsNew = server.isNew;
                item.szIp = server.endpoints[0].host;
                item.iPort = server.endpoints[0].port;
                item.iLoginServerStatus = static_cast<LOGIN_SERVER_STATUS>(server.status);
                info.LoginInfo.push_back(item);
            }
            loaded.push_back(info);
        }
        m_pAreaInfo.swap(loaded);
        m_iAreaCount = static_cast<int>(m_pAreaInfo.size());
    }
    else if (fileError == ERROR_FILE_NOT_FOUND || fileError == ERROR_PATH_NOT_FOUND)
        LoadLoginServerInfo();
    else
        TDThrow("Cannot inspect the modern login list.");

	// 构造自动选择服务器查找表.
	BuildAutoSelTable();

	// 用户名
	m_strUserName = "";

	// 密码
	m_strPassword = "";

	for(int i = 0; i < PROVIDE_COUNT; i++ )
	{
		// 当前选择的区域
		m_iAutoAreaIndex[i] = -1;

		// 当前选择的login server 索引
		m_iAutoLoginServerIndex[i] = -1;
	}

	// 前一次找到网络状态空闲的索引.
	m_iPreFindIdle = -1;

	// 前一次找到网络状态正常的索引.
	m_iPreFindNor  = -1;

	// 前一次找到网络状态拥挤的索引.
	m_iPreFindBusy = -1;

	// 前一次找到网络状态爆满的索引.
	m_iPreFindFull = -1;

}

CGamePro_Login::~CGamePro_Login()
{
}

VOID CGamePro_Login::FirstLogin(VOID)
{
	CGameProcedure::s_pVariableSystem->SetAs_Int("Login_Mode", (INT)LOGIN_FIRST_LOGIN);
}

VOID CGamePro_Login::LoginForChangeScene(VOID)
{
	CGameProcedure::s_pVariableSystem->SetAs_Int("Login_Mode", (INT)LOGIN_CHANGE_SCENE);
}

// CYS-RELOGIN: CysModernHandlers::OnSessionClosed after the quit_complete of ReturnServerLogin. Only switches the
// procedure here (inside the network pump); Init does the teardown after CGamePro_Main::Release.
void CGamePro_Login::ReturnFromWorld()
{
    m_cysReturnFromWorld = true;
    SetActiveProc(s_pProcLogIn);
    SetStatus(LOGIN_INPUT_ACCOUNT);
}

VOID CGamePro_Login::Init(VOID)
{
    if (m_cysReturnFromWorld)
    {
        m_cysReturnFromWorld = false;
        // Same teardown as a scene change (CWorldManager::EnterScene): leave the scene and its objects, so the next
        // world entry loads the scene again instead of hitting the "same scene" early return.
        CWorldManager::GetMe()->Release();
        typedef int (__cdecl *HideAllFn)();
        HMODULE ui = GetModuleHandleA("UI_CEGUI.dll");
        HideAllFn hideAll = ui ? reinterpret_cast<HideAllFn>(GetProcAddress(ui, "CysUiHideAllWindows")) : NULL;
        TDU_Log("CYS relogin: visible_game_windows=%d", hideAll ? hideAll() : -1);
        s_pEventSystem->PushEvent(GE_GAMELOGIN_OPEN_COUNT_INPUT);
        // Init's own Tick runs before ProcessGameEvent, so the prefill waits for the second Tick. The password is never
        // kept (CheckAccount wipes it).
        m_cysPrefillTicks = m_strUserName.empty() ? 0 : 2;
    }
	//fujia 2008.1.16
	/*if (CGameProcedure::s_pVariableSystem->GetAs_Int("View_FullScreen"))
	{
		s_pEventSystem->PushEvent(GE_VARIABLE_CHANGED, "View_FullScreen", "1");
	}
	else
	{
		const STRING& strVariable = CGameProcedure::s_pVariableSystem->GetAs_String("View_Resoution");
		fVector2 fResoution = s_pVariableSystem->GetAs_Vector2("View_Resoution");

		s_pEventSystem->PushEvent(GE_VARIABLE_CHANGED,"View_Resoution", strVariable.c_str());
		s_pEventSystem->PushEvent(GE_CHAT_ADJUST_MOVE_CTL, (INT)fResoution.x, (INT)fResoution.y);
	}*/
	
	// 禁止改变窗口大小。
	// CYS: *LongPtrW on the Unicode main window (x64 checklist: Set/GetWindowLong -> *Ptr; A on a W window would
	// CYS: also be wrong for the WNDPROC family).
	LONG_PTR lStyle = ::GetWindowLongPtrW(g_hMainWnd, GWL_STYLE);
	lStyle = lStyle & ~WS_THICKFRAME ;
	lStyle = lStyle & ~WS_MAXIMIZEBOX ;
	::SetWindowLongPtrW(g_hMainWnd, GWL_STYLE, lStyle);//
	// CYS-LOGIN: CY 3.71 then gives the login window a fixed 1280x720 client (FUN_1406302b0), see GameProcedure.h.
	CysLoginWindowFixed();
	//Rresh title
	::PostMessage(g_hMainWnd, WM_NCACTIVATE, TRUE, 0);

	//--- for debug
	if(CGameProcedure::s_pVariableSystem->GetAs_Int("GameServer_ConnectDirect") == 1) return;
	//--- for debug

	// CYS-CHARSEL: CY 3.71 CGamePro_Login::Init (Game.exe FUN_14062f940) loads the fixed 3D login background erengu_DL.Scene
	// (scene id -1) behind the server-selection and account screens, with the static CY login camera.
	CysLoginScene::Enter(CysLoginScene::Login);

	//打开登录界面
	if(LOGIN_SELECT_SERVER == m_Status)
	{
		s_pEventSystem->PushEvent(GE_GAMELOGIN_OPEN_SELECT_SERVER);
	}

	// 选择旧的服务器
	SelectOldServer();
}

// 在界面上选择服务器
VOID CGamePro_Login::SelectOldServer()
{
	int iPreCurSelArea		  = CGameProcedure::s_pVariableSystem->GetAs_Int("Login_Area");
    int iPreCurSelLoginServer = CGameProcedure::s_pVariableSystem->GetAs_Int("Login_Server");

	if((iPreCurSelArea < m_iAreaCount)&&(iPreCurSelArea >= 0))
	{
		int iLoginServerCount = GetAreaLoginServerCount(iPreCurSelArea);

		if((iPreCurSelLoginServer < iLoginServerCount)&&(iPreCurSelLoginServer >= 0))
		{
			// 选择前一次选择的服务器.
			s_pEventSystem->PushEvent(GE_GAMELOGIN_SELECT_AREA,			iPreCurSelArea);		// 选择区域
			s_pEventSystem->PushEvent(GE_GAMELOGIN_SELECT_LOGINSERVER,	iPreCurSelLoginServer);	// 选择Login Server
			return;
		}
	}
}

VOID CGamePro_Login::Tick(VOID)
{
	CGameProcedure::Tick();
    if (m_cysPrefillTicks && --m_cysPrefillTicks == 0)
    {
        // CYS-RELOGIN: the frame after Init, so the LogOn script has already cleared LogOn_ID on OPEN_COUNT_INPUT.
        typedef int (__cdecl *SetEditFn)(const char*, const char*);
        HMODULE ui = GetModuleHandleA("UI_CEGUI.dll");
        SetEditFn setEdit = ui ? reinterpret_cast<SetEditFn>(GetProcAddress(ui, "CysUiSetEditTextUtf8")) : NULL;
        TDU_Log("CYS relogin: account prefilled=%d", setEdit ? setEdit("LogOn_ID", m_strUserName.c_str()) : -1);
    }
	if (m_modernLogin) return; // Pumped once by NetManager across login/create/select.

	switch(m_Status)
	{
	case LOGIN_DEBUG_SETTING:
		{
			if(!CGameProcedure::s_pUISystem)
			{
				SetStatus(CGamePro_Login::LOGIN_DISCONNECT);
			}
			else
			{
				//DO NOTING,WAIT UI...
			}
		}
		break;

	case LOGIN_SELECT_SERVER:// 选择服务器状态
		{
			//--- for debug
			if(CGameProcedure::s_pVariableSystem->GetAs_Int("GameServer_ConnectDirect") == 1)
			{
				//直接切换到Change-Server流程
				CGameProcedure::SetActiveProc((CGameProcedure*)CGameProcedure::s_pProcChangeScene);
				return;
			}
			//--- for debug
			break;
		}
	case LOGIN_DISCONNECT:
		{
			s_pGfxSystem->PushDebugString("Connect to login server %s:%d...", m_szLoginServerAddr, m_nLoginServerPort);

			//开始登录
			SetStatus(LOGIN_CONNECTING);
			CNetManager::GetMe()->ConnectToServer(m_szLoginServerAddr, m_nLoginServerPort);

		}
		break;

	case LOGIN_CONNECTING:

		break;
	
	//连接成功
	case LOGIN_CONNECTED_OK:
		{

			// 设置正在验证密码
			// SetStatus(LOGIN_ACCOUNT_REQUESTING);

		}
		break;

	//连接失败
	case LOGIN_CONNECT_FAILED:

		CNetManager::GetMe()->Close();
		SetStatus(LOGIN_SELECT_SERVER);
		break;

	
	// 正在验证用户名和密码.
	case LOGIN_ACCOUNT_REQUESTING:
		{

			// 判断是否超时, 超时就提示错误信息.
			break;
		}
	case LOGIN_ACCOUNT_BEGIN_REQUESTING:
		{
			break;
		}
	//登录信息验证成功
	case LOGIN_ACCOUNT_OK:
		{
			// 保存选择的服务器
			CGameProcedure::s_pVariableSystem->SetAs_Int("Login_Area",   CGameProcedure::s_pProcLogIn->m_iCurSelAreaId, FALSE);
			CGameProcedure::s_pVariableSystem->SetAs_Int("Login_Server", CGameProcedure::s_pProcLogIn->m_iCurSelLoginServerId, FALSE);

			if(m_bReLogin)
			{
				//直接进入场景
				CGameProcedure::s_pProcEnter->SetStatus(CGamePro_Enter::ENTERSCENE_READY);
				CGameProcedure::s_pProcEnter->SetEnterType(ENTER_TYPE_FIRST);
				CGameProcedure::SetActiveProc((CGameProcedure*)CGameProcedure::s_pProcEnter);
			}
			else
			{
				// 设置登录状态为首次登录（以区分切换场景的登录状态）
				CGameProcedure::s_pProcLogIn->FirstLogin();

				//转入人物选择循环
				CGameProcedure::SetActiveProc((CGameProcedure*)s_pProcCharSel);
								
			}
		}
		break;
	default:
		break;
	}
}

void CGamePro_Login::CancelModernLogin()
{
#if defined(CYS_HAVE_NETCORE)
    if (m_loginClient) { m_loginClient->cancel(); m_loginClient.reset(); }
#endif
    m_modernLogin = false;
    m_modernCreatePending = false;
    m_modernCreateSucceeded = false;
    m_modernCharLoginPending = false;
}

void CGamePro_Login::PumpModernLogin()
{
#if defined(CYS_HAVE_NETCORE)
    if (!m_loginClient) return;
    bool disconnect = false;
    std::unique_ptr<cys::net::GameTicket> granted; // CYS: login hand-off, consumed after the pump.
    m_loginClient->pump([this, &disconnect, &granted](cys::net::Event&& event) {
        if (!m_modernLogin || disconnect) return;
        using cys::net::EventType;
        if (event.type == EventType::Connected)
        {
            SetStatus(LOGIN_ACCOUNT_REQUESTING);
            s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO_NO_BUTTON, u8"Đang kiểm tra tài khoản thử nghiệm...");
            TDU_Log("CYS WSL login: connected");
        }
        else if (event.type == EventType::Authenticated)
        {
            // Keep the procedure alive until the worker delivers the actual list.
            s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO_NO_BUTTON, u8"Đang tải danh sách nhân vật...");
            TDU_Log("CYS WSL login: authenticated");
        }
        else if (event.type == EventType::CharacterList)
        {
            const auto* list = dynamic_cast<const pg::LCRetCharList*>(event.packet.get());
            if (!list || !s_pProcCharSel->SetModernCharacterList(*list))
            {
                disconnect = true;
                SetStatus(LOGIN_INPUT_ACCOUNT);
                s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, u8"Danh sách nhân vật không hợp lệ.");
                TDU_Log("CYS WSL login: invalid list");
                return;
            }
            TDU_Log("CYS WSL login: charlist count=%u tx=%u rx=%u closed=%d", static_cast<unsigned>(list->charlist.size()),
                event.counters.txFrames, event.counters.rxFrames, event.counters.socketClosed ? 1 : 0);
            const bool created = m_modernCreatePending && m_modernCreateSucceeded;
            m_modernCreatePending = false;
            m_modernCreateSucceeded = false;
            s_pProcCharCreate->m_iCamp = -1;
            FirstLogin();
            ChangeToCharacterSel(false);
            s_pProcCharSel->m_bClearUIModel = FALSE;
            s_pEventSystem->PushEvent(GE_GAMELOGIN_CLOSE_CREATE_CHARACTOR);
            if (created)
            {
                s_pEventSystem->PushEvent(GE_GAMELOGIN_CREATE_CLEAR_NAME);
                s_pEventSystem->PushEvent(GE_GAMELOGIN_CREATE_ROLE_OK);
            }
            s_pEventSystem->PushEvent(GE_GAMELOGIN_REFRESH_ROLE_SELECT_CHARACTOR);
            TDU_Log("CYS WSL login: character selection opened count=%u created=%d",
                static_cast<unsigned>(list->charlist.size()), created ? 1 : 0);
        }
        else if (event.type == EventType::CreatePermission)
        {
            const auto* permission = dynamic_cast<const pg::LCRetCheckAllowCreateChar*>(event.packet.get());
            if (!permission || permission->flag != event.permissionFlag)
            { disconnect = true; return; }
            TDU_Log("CYS WSL create: permission=%d", permission->flag);
            if (permission->flag == 1 && GetActiveProcedure() == s_pProcCharSel)
                s_pProcCharSel->ChangeToCreateRole();
            else
                s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, u8"Hiện không thể tạo nhân vật.");
        }
        else if (event.type == EventType::CharacterCreated)
        {
            const auto* result = dynamic_cast<const pg::LCRetCreateChar*>(event.packet.get());
            if (!result || result->result != event.createResult || !m_modernCreatePending)
            { disconnect = true; return; }
            TDU_Log("CYS WSL create: result=%d tx=%u rx=%u", result->result,
                event.counters.txFrames, event.counters.rxFrames);
            if (result->result == 0)
            {
                m_modernCreateSucceeded = true;
                s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO_NO_BUTTON, u8"Đã tạo nhân vật. Đang tải lại danh sách...");
            }
            else
            {
                m_modernCreatePending = false;
                const char* message = u8"Máy chủ thử nghiệm từ chối tạo nhân vật.";
                if (result->result == 4) message = u8"Tên nhân vật đã có người dùng. Hãy chọn tên khác.";
                else if (result->result == 8) message = u8"Tên nhân vật không hợp lệ.";
                else if (result->result == 2) message = u8"Vui lòng chờ một lát trước khi tạo nhân vật khác.";
                else if (result->result == 3) message = u8"Tài khoản đã hết ô nhân vật trống.";
                s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, message);
            }
        }
        else if (event.type == EventType::CharacterLoginGranted)
        {
            // CYS: terminal; the login socket is closed and the ticket continues its sequence.
            if (!event.ticket || !event.ticket->valid()) { disconnect = true; return; }
            TDU_Log("CYS WSL charlogin: granted tx=%u next_idx=%04x closed=%d", event.counters.txFrames,
                event.counters.nextTxIndex, event.counters.socketClosed ? 1 : 0);
            granted = std::move(event.ticket);
        }
        else if (event.type == EventType::CharacterLoginRefused)
        {
            // CYS: server tips are never shown verbatim; the list stays usable for an explicit retry.
            m_modernCharLoginPending = false;
            s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO,
                u8"Máy chủ chưa cho nhân vật này vào game. Vui lòng thử lại sau giây lát.");
            TDU_Log("CYS WSL charlogin: refused by server; no automatic retry");
        }
        else if (event.type == EventType::Refused || event.type == EventType::Error)
        {
            disconnect = true;
            SetStatus(LOGIN_INPUT_ACCOUNT);
            if (event.type == EventType::Refused && event.retryAfterSeconds)
                m_loginRetryAt = GetTickCount64() + static_cast<ULONGLONG>(event.retryAfterSeconds) * 1000;
            const char* message = event.type == EventType::Refused ?
                u8"Máy chủ thử nghiệm từ chối đăng nhập. Kiểm tra tài khoản/mật khẩu hoặc thử lại sau." :
                u8"Kết nối máy chủ thử nghiệm thất bại. Vui lòng thử lại.";
            s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, message);
            TDU_Log("CYS WSL login: failed type=%d code=%d flag=%d wait=%u closed=%d", static_cast<int>(event.type),
                static_cast<int>(event.error), event.loginFlag, event.retryAfterSeconds, event.counters.socketClosed ? 1 : 0);
        }
        else if (event.type == EventType::Cancelled)
        {
            disconnect = true;
            SetStatus(LOGIN_INPUT_ACCOUNT);
        }
    });
    // Defer destruction until pump has returned; callbacks never destroy their owner.
    if (disconnect)
    {
        CancelModernLogin();
        s_pEventSystem->PushEvent(GE_GAMELOGIN_CLOSE_CREATE_CHARACTOR);
        s_pEventSystem->PushEvent(GE_GAMELOGIN_CLOSE_SELECT_CHARACTOR);
        s_pEventSystem->PushEvent(GE_GAMELOGIN_OPEN_COUNT_INPUT);
        SetActiveProc(s_pProcLogIn);
        SetStatus(LOGIN_INPUT_ACCOUNT);
    }
    else if (granted)
    {
        // CYS: the login worker has finished; the game worker owns the next socket.
        m_loginClient.reset();
        m_modernLogin = false;
        m_modernCharLoginPending = false;
        const unsigned int guid = granted->guid();
        if (!CysModernGame::Start(std::move(*granted), m_modernGamePort, m_modernAllowedHosts)) // CYS: profile policy
        {
            s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, u8"Không mở được kết nối vào game.");
            TDU_Log("CYS WSL charlogin: game session start failed");
            s_pEventSystem->PushEvent(GE_GAMELOGIN_CLOSE_SELECT_CHARACTOR);
            s_pEventSystem->PushEvent(GE_GAMELOGIN_OPEN_COUNT_INPUT);
            SetActiveProc(s_pProcLogIn);
            SetStatus(LOGIN_INPUT_ACCOUNT);
            return;
        }
        s_pVariableSystem->SetAs_Int("User_GUID", (INT)guid);
        s_pProcCharSel->m_EnterGameGUID = (GUID_t)guid;
        s_pProcEnter->SetEnterType(ENTER_TYPE_FIRST);
        s_pEventSystem->PushEvent(GE_GAMELOGIN_CLEAR_ACCOUNT);
        SetActiveProc((CGameProcedure*)s_pProcChangeScene);
        s_pProcChangeScene->SetStatus(CGamePro_ChangeScene::CHANGESCENE_CONNECTING);
        TDU_Log("CYS WSL charlogin: guid=%u handed to the game socket", guid);
    }
#endif
}

bool CGamePro_Login::RequestModernCreatePermission()
{
#if defined(CYS_HAVE_NETCORE)
    if (m_modernLogin && m_loginClient && GetActiveProcedure() == s_pProcCharSel &&
        s_pProcCharSel->GetCurRoleCount() < MAX_SHOW_IN_UI && m_loginClient->requestCreatePermission())
    {
        s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO_NO_BUTTON, u8"Đang kiểm tra quyền tạo nhân vật...");
        return true;
    }
#endif
    return false;
}

bool CGamePro_Login::SubmitModernCharacter(const cys::net::CreateCharacterRequest& request)
{
#if defined(CYS_HAVE_NETCORE)
    if (!m_modernLogin || !m_loginClient || m_modernCreatePending ||
        GetActiveProcedure() != s_pProcCharCreate || !m_loginClient->createCharacter(request)) return false;
    m_modernCreatePending = true;
    m_modernCreateSucceeded = false;
    TDU_Log("CYS WSL create: submitted once from UI");
    s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO_NO_BUTTON, u8"Đang tạo nhân vật...");
    return true;
#else
    return false;
#endif
}

bool CGamePro_Login::RequestModernCharacterLogin(unsigned int guid)
{
#if defined(CYS_HAVE_NETCORE)
    // CYS: exactly one explicit request from the selection UI; never an automatic retry.
    if (!m_modernLogin || !m_loginClient || m_modernCreatePending || m_modernCharLoginPending ||
        GetActiveProcedure() != s_pProcCharSel || !m_loginClient->requestCharacterLogin(guid)) return false;
    m_modernCharLoginPending = true;
    TDU_Log("CYS WSL charlogin: requested once from UI guid=%u", guid);
    return true;
#else
    (void)guid;
    return false;
#endif
}

VOID CGamePro_Login::Render(VOID)
{
	if(s_pGfxSystem) s_pGfxSystem->RenderFrame();
}

VOID CGamePro_Login::Release(VOID)
{
    CysLoginScene::Leave(CysLoginScene::Login); // CYS-CHARSEL: the scene stays until selection/creation/world replace it
    // CYS: login owns the authenticated socket while its creation/selection UIs run.
    if (m_modernLogin && (GetActiveProcedure() == s_pProcCharSel || GetActiveProcedure() == s_pProcCharCreate)) return;
    CancelModernLogin();
}

VOID CGamePro_Login::ProcessInput(VOID)
{
	//分析事件队列
	InputEventQueue& inputQueue = s_pInputSystem->GetInputQueue();
	for(INT i=0; i<(INT)inputQueue.size(); i++)
	{
		InputEvent& event = inputQueue[i];
		if(event.bProcessed) continue;
		if(event.nEvent != EVENT_KEY_UP) continue;
		switch(event.kIndex)
		{
		case KC_GRAVE:
			CGameProcedure::s_pEventSystem->PushEvent(GE_TOGLE_CONSOLE);
			event.bProcessed = true;
			break;

		case KC_D:
			{
				if(!(s_pInputSystem->IsKeyDown(KC_LMENU))) 
					break;

				if(s_pUISystem)
					s_pUISystem->Debug_SaveFontTexture();
			}
			break;

		default:
			break;
		}
	}
}



//------------------------------------------------------------------------------------------------------------
//
// 从配置文件得到, login server 的ip地址和端口号
//
//

// 设置端口号.
VOID  CGamePro_Login::SetPort(INT iPort)
{
	m_nLoginServerPort = iPort;
}

// 设置ip地址
VOID  CGamePro_Login::SetIpAddr(LPCTSTR szServerAddr)
{
	if (!szServerAddr || strlen(szServerAddr) >= sizeof(m_szLoginServerAddr))
        TDThrow("Invalid login endpoint length.");
    memcpy(m_szLoginServerAddr, szServerAddr, strlen(szServerAddr) + 1);
}


// 读取login server信息
//
// 返回值: 
//			0 -- 读取成功
//			1 -- 失败找不到配置文件
//          2 -- 其他错误    
int  CGamePro_Login::LoadLoginServerInfo(std::string strLoginServerInfoFile)
{
	m_keywordServers.clear();
	m_pAreaInfo.resize(0);
	char szAppName[100];
	char szName[128];
	char szIP[32];
	DWORD dwMerchant;
	DWORD dwPort;
	DWORD dwBelong;
	DWORD dwStat;
	for( int i = 1; i < 256; i ++ )
	{
		sprintf( szAppName, "REGION%d", i );
		if( ::GetPrivateProfileString(szAppName, "NAME", "", szName, 120, strLoginServerInfoFile.c_str()) == 0 ) continue;
		dwMerchant = (DWORD)::GetPrivateProfileInt(szAppName, "MERCHANT", 0XFFFFFFFF, strLoginServerInfoFile.c_str());
		if( dwMerchant == 0XFFFFFFFF ) continue;
		AREA_INFO info;
		info.szAreaName = szName;
		info.iProvide = (NET_PROVIDE)dwMerchant;
		m_pAreaInfo.push_back( info );
	}

	m_iAreaCount = (int)m_pAreaInfo.size();
	for( int i = 1; i < 1024; i ++ )
	{
		sprintf( szAppName, "LOGIN%d", i );
		if( ::GetPrivateProfileString(szAppName, "NAME", "", szName, 120, strLoginServerInfoFile.c_str()) == 0 ) continue;
		if( ::GetPrivateProfileString(szAppName, "IP", "", szIP, 30, strLoginServerInfoFile.c_str()) == 0 ) continue;
		dwPort = (DWORD)::GetPrivateProfileInt(szAppName, "PORT", 0XFFFFFFFF, strLoginServerInfoFile.c_str());
		if( dwPort == 0XFFFFFFFF ) continue;
		dwStat = (DWORD)::GetPrivateProfileInt(szAppName, "STATE", 0XFFFFFFFF, strLoginServerInfoFile.c_str());
		if( dwStat == 0XFFFFFFFF ) continue;
		dwBelong = (DWORD)::GetPrivateProfileInt(szAppName, "BELONG", 0XFFFFFFFF, strLoginServerInfoFile.c_str());
		if( dwBelong == 0XFFFFFFFF ) continue;

		if( dwBelong == 0 || dwBelong > (DWORD)m_iAreaCount ) continue;
		AREA_INFO& info = m_pAreaInfo[dwBelong-1];

		LOGINSERVER_INOF li;
		li.iProvider = static_cast<int>(info.iProvide);
        li.iServerId = i; // Legacy LOGIN section identity, no modern recommendation metadata.
        li.iRowOrdinal = i - 1;
		li.szLoginServerName = szName;
		li.szIp = szIP;
		li.iPort = dwPort;
		li.iLoginServerStatus = (LOGIN_SERVER_STATUS)dwStat;
		info.LoginInfo.push_back( li );
	}

	return LOAD_LOGIN_SERVER_SUCCESS;
}

// 初始化区域信息
void CGamePro_Login::InitLoginAreaInfo()
{
    m_keywordServers.clear();
	m_pAreaInfo.resize( 0 );
	m_iAreaCount = 0;
}


// 读取区域信息.
/*int CGamePro_Login::ReadAreaInfo(std::string& strLoginServer, int iAreaIndex)
{
	if(iAreaIndex >= m_iAreaCount)
	{
		return LOAD_LOGIN_SERVER_OTHER_ERR;
	}

	char bufInfo[512];
	char bufTempInfo1[128];
	char bufTempInfo2[128];
	char bufTempInfo3[128];


	while(!feof(pFile))
	{
		
		// 得到区域的名字.
		::ZeroMemory(bufInfo, sizeof(bufInfo));
		if(NULL == ::fgets(bufInfo, sizeof(bufInfo), pFile))
		{
			return LOAD_LOGIN_SERVER_OTHER_ERR;
		}
		::ZeroMemory(bufTempInfo1, sizeof(bufTempInfo1));
		::ZeroMemory(bufTempInfo2, sizeof(bufTempInfo2));
		::ZeroMemory(bufTempInfo2, sizeof(bufTempInfo3));
		::sscanf(bufInfo, "%s %s %s", bufTempInfo1, bufTempInfo2, bufTempInfo3);
		if(strcmp("区域名字", bufTempInfo1))
		{
			// 如果没有得到正确的区域, 一直读取错误的区域.
			continue;
		}

		// 保存区域的名字.
		strncpy(m_pAreaInfo[iAreaIndex].szAreaName, bufTempInfo3, sizeof(m_pAreaInfo[iAreaIndex].szAreaName));

		// 读取供应商.
		::ZeroMemory(bufInfo, sizeof(bufInfo));
		if(NULL == ::fgets(bufInfo, sizeof(bufInfo), pFile))
		{
			return LOAD_LOGIN_SERVER_AREA_NOT_FINDED;
		}
		::ZeroMemory(bufTempInfo1, sizeof(bufTempInfo1));
		::ZeroMemory(bufTempInfo2, sizeof(bufTempInfo2));
		::ZeroMemory(bufTempInfo3, sizeof(bufTempInfo3));
		::sscanf(bufInfo, "%s %s %d", bufTempInfo1, bufTempInfo2, &(m_pAreaInfo[iAreaIndex].iProvide));


		// 读取login server 的个数.
		::ZeroMemory(bufInfo, sizeof(bufInfo));
		if(NULL == ::fgets(bufInfo, sizeof(bufInfo), pFile))
		{
			return LOAD_LOGIN_SERVER_AREA_NOT_FINDED;
		}
		::ZeroMemory(bufTempInfo1, sizeof(bufTempInfo1));
		::ZeroMemory(bufTempInfo2, sizeof(bufTempInfo2));
		::ZeroMemory(bufTempInfo3, sizeof(bufTempInfo3));
		::sscanf(bufInfo, "%s %s %d", bufTempInfo1, bufTempInfo2, &(m_pAreaInfo[iAreaIndex].iLoginServerCount));

		int iReadRes = 0;
		for(int i = 0; i < m_pAreaInfo[iAreaIndex].iLoginServerCount; i++)	
		{
			iReadRes = ReadLoginInfo(pFile, iAreaIndex, i);
			if(iReadRes)
			{
				return iReadRes;
			}
		}

		// 读完一个区域信息就返回
		return LOAD_LOGIN_SERVER_SUCCESS;

	}
	
	// 没有读取完整区域的名字.
	return LOAD_LOGIN_SERVER_AREA_NOT_FINDED;
}*/


// 读取login server信息.
/*int CGamePro_Login::ReadLoginInfo(FILE* pFile, int iAreaIndex, int iLoginServerIndex)
{
	
	//if(NULL == pFile)
	//{
	//	return LOAD_LOGIN_SERVER_FILE_NOT_FINDED;
	//}

	//char bufInfo[512];
	//char bufTempInfo1[128];
	//char bufTempInfo2[128];
	//char bufTempInfo3[128];


	//while(!feof(pFile))
	//{
	//	//-------------------------------------------------------
	//	// 得到login server 的名字.
	//	::ZeroMemory(bufInfo, sizeof(bufInfo));
	//	if(NULL == ::fgets(bufInfo, sizeof(bufInfo), pFile))
	//	{
	//		return LOAD_LOGIN_SERVER_OTHER_ERR;
	//	}

	//	::ZeroMemory(bufTempInfo1, sizeof(bufTempInfo1));
	//	::ZeroMemory(bufTempInfo2, sizeof(bufTempInfo2));
	//	::ZeroMemory(bufTempInfo3, sizeof(bufTempInfo3));
	//	::sscanf(bufInfo, "%s %s %s", bufTempInfo1, bufTempInfo2, bufTempInfo3);
	//	if(strcmp("LoginServer名字", bufTempInfo1))
	//	{
	//		// 如果没有得到login server, 一直读取错误的行.
	//		continue;
	//	}

	//	// 保存login server 的名字.
	//	//strncpy(
	//	//		m_pAreaInfo[iAreaIndex].LoginInfo[iLoginServerIndex].szLoginServerName, 
	//	//		bufTempInfo3, 
	//	//		sizeof(m_pAreaInfo[iAreaIndex].LoginInfo[iLoginServerIndex].szLoginServerName)
	//	//		);

	//	//--------------------------------------------------------
	//	// 读取状态
	//	::ZeroMemory(bufInfo, sizeof(bufInfo));
	//	if(NULL == ::fgets(bufInfo, sizeof(bufInfo), pFile))
	//	{
	//		return LOAD_LOGIN_SERVER_AREA_NOT_FINDED;
	//	}
	//	::ZeroMemory(bufTempInfo1, sizeof(bufTempInfo1));
	//	::ZeroMemory(bufTempInfo2, sizeof(bufTempInfo2));
	//	::ZeroMemory(bufTempInfo3, sizeof(bufTempInfo3));
	//	::sscanf(
	//				bufInfo, 
	//				"%s %s %d", 
	//				bufTempInfo1, 
	//				bufTempInfo2, 
	//				&(m_pAreaInfo[iAreaIndex].LoginInfo[iLoginServerIndex].iLoginServerStatus)
	//				);


	//	//--------------------------------------------------------
	//	// 读取ip地址
	//	::ZeroMemory(bufInfo, sizeof(bufInfo));
	//	if(NULL == ::fgets(bufInfo, sizeof(bufInfo), pFile))
	//	{
	//		return LOAD_LOGIN_SERVER_AREA_NOT_FINDED;
	//	}
	//	::ZeroMemory(bufTempInfo1, sizeof(bufTempInfo1));
	//	::ZeroMemory(bufTempInfo2, sizeof(bufTempInfo2));
	//	::ZeroMemory(bufTempInfo3, sizeof(bufTempInfo3));
	//	::sscanf(
	//				bufInfo, 
	//				"%s %s %s", 
	//				bufTempInfo1, 
	//				bufTempInfo2, 
	//				m_pAreaInfo[iAreaIndex].LoginInfo[iLoginServerIndex].szIp
	//				);


	//	//---------------------------------------------------------
	//	// 读取端口号
	//	::ZeroMemory(bufInfo, sizeof(bufInfo));
	//	if(NULL == ::fgets(bufInfo, sizeof(bufInfo), pFile))
	//	{
	//		return LOAD_LOGIN_SERVER_AREA_NOT_FINDED;
	//	}
	//	::ZeroMemory(bufTempInfo1, sizeof(bufTempInfo1));
	//	::ZeroMemory(bufTempInfo2, sizeof(bufTempInfo2));
	//	::ZeroMemory(bufTempInfo3, sizeof(bufTempInfo3));
	//	//::sscanf(
	//	//			bufInfo, 
	//	//			"%s %s %d", 
	//	//			bufTempInfo1, 
	//	//			bufTempInfo2, 
	//	//			&(m_pAreaInfo[iAreaIndex].LoginInfo[iLoginServerIndex].iPort)
	//	//			);

	//	// 读完一个login server 返回.
	//	return LOAD_LOGIN_SERVER_SUCCESS;

	//}
	
	// 没有读取完整区域的名字.
	return LOAD_LOGIN_SERVER_AREA_NOT_FINDED;//

}*/


//-----------------------------------------------------------------------------------------------------------------------------
//
// 逻辑数据操作
//
//

// 得到区域的个数
int CGamePro_Login::GetAreaCount()
{
	return m_iAreaCount;
}

// 得到区域名字
const TCHAR* CGamePro_Login::GetAreaName(int iAreaIndex)
{
	if( iAreaIndex < 0 || iAreaIndex >= m_iAreaCount )
		return _T("");

	return m_pAreaInfo[iAreaIndex].szAreaName.c_str();
}

// 得到区域信息
// CYS: negative and out-of-range indices match the modern host's empty result.
const char* CGamePro_Login::GetAreaDescription(int iAreaIndex) const
{
    if (iAreaIndex < 0 || static_cast<std::size_t>(iAreaIndex) >= m_pAreaInfo.size()) return "";
    return m_pAreaInfo[iAreaIndex].szAreaDescription.c_str();
}

PAREA_INFO CGamePro_Login::GetAreaInfo(int iAreaIndex)
{
	if( iAreaIndex < 0 || iAreaIndex >= m_iAreaCount )
		return NULL;

	return &m_pAreaInfo[iAreaIndex];
}

// 通过网络供应商选择一个login server
int CGamePro_Login::AutoSelLoginServer(int iProvide)
{
	PPROVIDE_INFO pProvideInfo = NULL;
	int iAreaIndex        = -1;
	int iLoginServerIndex = -1;

	switch(iProvide)
	{
	case 0:// 电信供应商
		{
			pProvideInfo = &m_Provide0;
			break;
		}
	case 1:// 网通供应商
		{
			pProvideInfo = &m_Provide1;
			break;
		}
	case 2:// 其他供应商
		{
			pProvideInfo = &m_Provide2;
			break;
		}
	default:
		{
			break;
		}
	}

	int iLoginServerCount = (int)pProvideInfo->IdleLoginServerVector.size();
	// 查找空闲的服务器
	if(iLoginServerCount)
	{
		if(m_iPreFindIdle + 1 <  iLoginServerCount)
		{
			m_iPreFindIdle++;
			
		}
		else
		{
			m_iPreFindIdle = 0;
		}

		iAreaIndex        = pProvideInfo->IdleLoginServerVector[m_iPreFindIdle].iAreaIndex;
		iLoginServerIndex = pProvideInfo->IdleLoginServerVector[m_iPreFindIdle].iLoginServerIndex;
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SELECT_LOGIN_SERVER, iAreaIndex, iLoginServerIndex);//
		return 0;
	}

	iLoginServerCount = (int)pProvideInfo->NorLoginServerVector.size();
	// 查找正常的服务器
	if(iLoginServerCount)
	{
		if(m_iPreFindNor + 1 <  iLoginServerCount)
		{
			m_iPreFindNor++;
		}
		else
		{
			m_iPreFindNor = 0;
		}

		iAreaIndex        = pProvideInfo->NorLoginServerVector[m_iPreFindNor].iAreaIndex;
		iLoginServerIndex = pProvideInfo->NorLoginServerVector[m_iPreFindNor].iLoginServerIndex;
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SELECT_LOGIN_SERVER, iAreaIndex, iLoginServerIndex);//
		return 0;
	}

	iLoginServerCount = (int)pProvideInfo->BusyLoginServerVector.size();
	// 查找繁忙的服务器
	if(iLoginServerCount)
	{
		if(m_iPreFindBusy + 1 <  iLoginServerCount)
		{
			m_iPreFindBusy++;
		}
		else
		{
			m_iPreFindBusy = 0;
		}

		iAreaIndex        = pProvideInfo->BusyLoginServerVector[m_iPreFindBusy].iAreaIndex;
		iLoginServerIndex = pProvideInfo->BusyLoginServerVector[m_iPreFindBusy].iLoginServerIndex;
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SELECT_LOGIN_SERVER, iAreaIndex, iLoginServerIndex);//
		return 0;
	}

	iLoginServerCount = (int)pProvideInfo->FullLoginServerVector.size();
	// 查找爆满的服务器
	if(iLoginServerCount)
	{
		if(m_iPreFindFull + 1 <  iLoginServerCount)
		{
			m_iPreFindFull++;
		}
		else
		{
			m_iPreFindFull = 0;
		}

		iAreaIndex        = pProvideInfo->FullLoginServerVector[m_iPreFindFull].iAreaIndex;
		iLoginServerIndex = pProvideInfo->FullLoginServerVector[m_iPreFindFull].iLoginServerIndex;
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SELECT_LOGIN_SERVER, iAreaIndex, iLoginServerIndex);//
		return 0;
	}


	return 0;
}

// 构造自动选择login server需要的数据表
int CGamePro_Login::BuildAutoSelTable()
{
	//LOGIN_SERVER_POS LoginServerPos;
	//PPROVIDE_INFO			 pProvideInfo     = NULL;
	//LOGIN_SERVER_POS_VECTOR* pServerPosVector = NULL;
	//int iLoginServerCount = 0;
	//for(int i = 0; i < m_iAreaCount; i++)
	//{
		//switch(m_pAreaInfo[i].iProvide)
		//{
		//case 0:// 电信供应商
		//	{
		//		pProvideInfo = &m_Provide0;
		//		break;
		//	}
		//case 1:// 网通供应商
		//	{
		//		pProvideInfo = &m_Provide1;
		//		break;
		//	}
		//case 2:// 其他供应商
		//	{
		//		pProvideInfo = &m_Provide2;
		//		break;
		//	}
		//}

		//iLoginServerCount = m_pAreaInfo[i].iLoginServerCount;
		//for(int j = 0; j < iLoginServerCount; j++)
		//{
		//	LoginServerPos.iAreaIndex        = i;
		//	LoginServerPos.iLoginServerIndex = j;
		//
		//	switch(m_pAreaInfo[i].LoginInfo[j].iLoginServerStatus)
		//	{
		//	case LOGIN_SERVER_FULL:
		//		{
		//			pServerPosVector = &(pProvideInfo->FullLoginServerVector);
		//			break;
		//		}
		//	case LOGIN_SERVER_BUSY:
		//		{
		//			pServerPosVector = &(pProvideInfo->BusyLoginServerVector);
		//			break;
		//		}
		//	case LOGIN_SERVER_NORMAL:
		//		{
		//			pServerPosVector = &(pProvideInfo->NorLoginServerVector);
		//			break;
		//		}
		//	case LOGIN_SERVER_IDLE:
		//		{
		//			pServerPosVector = &(pProvideInfo->IdleLoginServerVector);
		//			break;
		//		}
		//	default:
		//		{

		//		}

		//	}// switch(m_pAreaInfo[i].LoginInfo[j].iLoginServerStatus)

		//	pServerPosVector->push_back(LoginServerPos);

		//}// for(int j = 0; j < iLoginServerCount; j++)

	//}// for(int i = 0; i < m_iAreaCount; i++)

	return 0;
}

// 得到区域中login server 的个数
int CGamePro_Login::GetAreaRecommendLevel(int index) const
{
    return index >= 0 && index < static_cast<int>(m_pAreaInfo.size()) ? m_pAreaInfo[index].iRecommendLevel : 0;
}

int CGamePro_Login::SetLoginServerKeyword(const std::string& keyword)
{
    m_keywordServers.clear();
    std::vector<CysLoginList::Area> catalog;
    for (const auto& area : m_pAreaInfo)
    {
        CysLoginList::Area entry;
        for (const auto& server : area.LoginInfo)
        {
            CysLoginList::Server item;
            item.name = server.szLoginServerName;
            item.status = server.iLoginServerStatus;
            entry.servers.push_back(item);
        }
        catalog.push_back(entry);
    }
    for (const auto& match : CysLoginList::Search(catalog, keyword))
        m_keywordServers.push_back(LOGIN_SERVER_POS{match.areaIndex, match.serverIndex});
    return static_cast<int>(m_keywordServers.size());
}

bool CGamePro_Login::GetKeywordLoginServerInfo(int index, CysHostQueries::LoginServerInfo& result)
{
    if (index < 0 || index >= static_cast<int>(m_keywordServers.size())) return false;
    const auto& match = m_keywordServers[index];
    const auto* server = GetAreaLoginServerInfo(match.iAreaIndex, match.iLoginServerIndex);
    if (!server) return false;
    result.name = server->szLoginServerName;
    result.status = server->iLoginServerStatus;
    result.recommendLevel = server->iRecommendLevel;
    result.isNew = server->bIsNew;
    result.provider = server->iProvider;
    result.rowOrdinal = server->iRowOrdinal;
    result.areaIndex = match.iAreaIndex;
    result.serverId = server->iServerId;
    result.serverIndex = match.iLoginServerIndex;
    return true;
}

int CGamePro_Login::GetAreaLoginServerCount(int iAreaIndex)
{
	if( iAreaIndex < 0 || iAreaIndex >= m_iAreaCount )
		return 0;

	return (int)(m_pAreaInfo[iAreaIndex].LoginInfo.size());
}

// 得到区域中login server 的个数
PLOGINSERVER_INOF CGamePro_Login::GetAreaLoginServerInfo(int iAreaIndex, int iLoginServerIndex)
{
	if( iAreaIndex < 0 || iAreaIndex >= m_iAreaCount )
		return NULL;

	if( iLoginServerIndex < 0 || iLoginServerIndex >= (int)m_pAreaInfo[iAreaIndex].LoginInfo.size() )
		return NULL;

	return &(m_pAreaInfo[iAreaIndex].LoginInfo[iLoginServerIndex]);
}


// 连接到login server
int CGamePro_Login::ConnectToLoginServer(int iAreaIndex, int iLoginServerIndex)
{

	if( iAreaIndex < 0 || iAreaIndex >= m_iAreaCount )
		return 1;

	if( iLoginServerIndex < 0 || iLoginServerIndex >= (int)m_pAreaInfo[iAreaIndex].LoginInfo.size() )
		return 1;

	// 设置ip地址和端口号.
	SetIpAddr( m_pAreaInfo[iAreaIndex].LoginInfo[iLoginServerIndex].szIp.c_str() );
	SetPort( m_pAreaInfo[iAreaIndex].LoginInfo[iLoginServerIndex].iPort );

	// 设置当前的状态为非连接状态
	SetStatus(LOGIN_DISCONNECT);

	// 通知界面显示系统提示信息, 正在连接服务器.
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO_NO_BUTTON, "正在连接到服务器.....");

	return 0;

}


// 记录选择的服务器login server
int CGamePro_Login::SelectLoginServer(int iAreaIndex, int iLoginServerIndex)
{
    const auto* selected = GetAreaLoginServerInfo(iAreaIndex, iLoginServerIndex);
    if (!selected || selected->iLoginServerStatus == 10) return 1;

	// 记录选择的服务器
	m_iCurSelAreaId			= iAreaIndex;
	m_iCurSelLoginServerId  = iLoginServerIndex;

	OpenCountInputDlg();

	return 0;
}


// 连接到login server
int CGamePro_Login::ConnectToLoginServer()
{
	// 设置当前的状态为非连接状态
	SetStatus(LOGIN_DISCONNECT);

	// 通知界面显示系统提示信息, 正在连接服务器.
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO_NO_BUTTON, "正在连接到服务器.....");
	return 0;
}


//-------------------------------------------------------------------------------------------------------------------------------
//
// 界面操作
//

// 打开帐号输入界面
int CGamePro_Login::OpenCountInputDlg()
{
    CancelModernLogin();
    SetStatus(LOGIN_INPUT_ACCOUNT);
	// 打开帐号输入界面
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_OPEN_COUNT_INPUT);

	// 关闭系统界面
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_CLOSE_SYSTEM_INFO);

	// 关闭服务器选择界面
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_CLOSE_SELECT_SERVER);

	// 保存选择的服务器
	CGameProcedure::s_pVariableSystem->SetAs_Int("Login_Area",   CGameProcedure::s_pProcLogIn->m_iCurSelAreaId, FALSE);
	CGameProcedure::s_pVariableSystem->SetAs_Int("Login_Server", CGameProcedure::s_pProcLogIn->m_iCurSelLoginServerId, FALSE);

	// 设置帐号输入界面
	//SetStatus(LOGIN_ACCOUNT_BEGIN_REQUESTING);
	return 0;
}

// 打开服务器选择界面
int CGamePro_Login::OpenSelectServerDlg()
{
    CancelModernLogin();
	// 打开服务器选择界面
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_OPEN_SELECT_SERVER);

	// 关闭系统界面
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_CLOSE_SYSTEM_INFO);

	// 关闭帐号输入界面
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_CLOSE_COUNT_INPUT);

	// 设置服务器选择界面
	SetStatus(LOGIN_SELECT_SERVER);

	// 断开网络连接
	CNetManager::GetMe()->Close();

	SelectOldServer();
	return 0;
}


// 打开人物选择界面
int CGamePro_Login::OpenSelectCharacter()
{

	return 0;
}

// 切换到人物选择界面
int CGamePro_Login::ChangeToCharacterSel(bool requestLegacyList)
{

	
	// 保存选择的服务器
	CGameProcedure::s_pVariableSystem->SetAs_Int("Login_Area",   CGameProcedure::s_pProcLogIn->m_iCurSelAreaId, FALSE);
	CGameProcedure::s_pVariableSystem->SetAs_Int("Login_Server", CGameProcedure::s_pProcLogIn->m_iCurSelLoginServerId, FALSE);

	// 关闭系统界面
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_CLOSE_SYSTEM_INFO);

	// 关闭帐号输入界面
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_CLOSE_COUNT_INPUT);

	// 显示人物选择界面
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_OPEN_SELECT_CHARACTOR);

	//切换到人物选择界面
	CGameProcedure::SetActiveProc((CGameProcedure*)CGameProcedure::s_pProcCharSel);

	// 设置用户名
    if (s_pProcCharSel->m_strUserName != m_strUserName)
	CGameProcedure::s_pProcCharSel->SetUserName(m_strUserName.c_str());

	// 进入到人物选择界面， 清空ui模型。
	CGameProcedure::s_pProcCharSel->m_bClearUIModel = requestLegacyList ? TRUE : FALSE;

	// 发送得到角色信息消息.
	if (requestLegacyList)
    {
        CLAskCharList msg;
        msg.SetAccount((CHAR*)m_strUserName.c_str());
        CNetManager::GetMe()->SendPacket(&msg);
    }


	return 0;
}



//-------------------------------------------------------------------------------------------------------------------------------
//
// 网络命令消息操作
//

// 发送同步消息
int CGamePro_Login::SendClConnectMsg()
{
	CLConnect msg;

	// 发送网络连接消息
	CNetManager::GetMe()->SendPacket(&msg);
	return 0;
}

// 检查用户名和密码.
int CGamePro_Login::CheckAccount(CHAR* szUserName, CHAR* szPassword)
{
    if (GetStatus() != LOGIN_INPUT_ACCOUNT) return 0;
#if defined(CYS_HAVE_NETCORE)
    // CYS: offline world replay: the same LoginClient runs on the replay stream; any
    // account is accepted and the typed password is never used (a fixed placeholder
    // satisfies the transport's input policy). No socket is opened.
    if (CysWorldReplay::Active())
    {
        std::string account = szUserName ? szUserName : "";
        bool plain = !account.empty() && account.size() <= 16;
        for (size_t i = 0; plain && i < account.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(account[i]);
            plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        }
        if (!plain) account = "replay";
        if (!m_loginClient) m_loginClient.reset(new cys::net::LoginClient());
        cys::net::Endpoint endpoint;
        endpoint.host = "127.0.0.1";
        endpoint.port = 15733;
        cys::net::Options options;
        options.mode = cys::net::SessionMode::Interactive;
        options.overallTimeoutMs = 600000;
        options.idleTimeoutMs = 300000;
        options.approvedGamePort = CysWorldReplay::GamePort();
        options.streamFactory = CysWorldReplay::LoginFactory();
        m_modernGamePort = options.approvedGamePort; // CYS: replay fixture hand-off port
        m_modernAllowedHosts.assign(1, "127.0.0.1");
        m_modernProfileName = "world-replay";
        if (!m_loginClient->start(endpoint, account, "replay", options))
        {
            s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, u8"Không bắt đầu được đăng nhập phát lại.");
            TDU_Log("CYS replay: login start rejected code=%d", static_cast<int>(m_loginClient->lastStartError()));
            return 0;
        }
        m_strUserName = account;
        if (!m_strPassword.empty()) SecureZeroMemory(&m_strPassword[0], m_strPassword.size());
        m_strPassword.clear();
        m_modernLogin = true;
        SetStatus(LOGIN_CONNECTING);
        s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO_NO_BUTTON, u8"Đang phát lại phiên thế giới đã ghi...");
        TDU_Log("CYS replay: login started on the replay stream account_len=%u", static_cast<unsigned>(account.size()));
        return 0;
    }
#endif
#if defined(CYS_HAVE_NETCORE)
    // CYS: the modern protocol is chosen by a server profile of the selected login endpoint
    // (Patch/CysServerProfile.txt), not by an environment switch or compiled-in ports.
    cys::net::ServerProfile profile;
    const char* profileMessage = "";
    // CYS-CY 14/09 (cs2-world): the CY 3.71 login screen appends its region suffix (LogOn_Region, "@changyou.com")
    // to the typed account (LogOn.lua LogOn_CheckAccount -> GameProduceLogin:CheckAccount(strTail)). The private
    // CY-Source server (and the GL/0.85 login) know plain accounts: for a profile login the suffix is dropped here,
    // so "test00@changyou.com" logs in as "test00" (the account filter of the profile compares the plain name too).
    std::string cysPlainAccount = szUserName ? szUserName : "";
    {
        const std::string::size_type at = cysPlainAccount.find('@');
        if (at != std::string::npos)
        {
            TDU_Log("CYS WSL login: CY region suffix '%s' dropped for the profile login", cysPlainAccount.c_str() + at);
            cysPlainAccount.erase(at);
        }
    }
    if (szUserName) szUserName = const_cast<CHAR*>(cysPlainAccount.c_str());
    const CysProfileLookup lookup = CysResolveServerProfile(
        GetAreaLoginServerInfo(m_iCurSelAreaId, m_iCurSelLoginServerId), szUserName, profile, profileMessage);
    if (lookup == CysProfileLookup::Rejected)
    {
        s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, profileMessage);
        TDU_Log("CYS WSL login: profile rejected");
        return 0;
    }
    if (lookup == CysProfileLookup::Found)
    {
        if (!szUserName || !szPassword)
        {
            s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, u8"Cấu hình đăng nhập thử nghiệm không khớp.");
            TDU_Log("CYS WSL login: profile rejected");
            return 0;
        }
        if (GetTickCount64() < m_loginRetryAt)
        {
            s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, u8"Vui lòng chờ một lát rồi đăng nhập lại.");
            return 0;
        }
        if (!m_loginClient) m_loginClient.reset(new cys::net::LoginClient());
        cys::net::Endpoint endpoint = profile.login; // CYS: equals the selected server (checked above)
        // The caller owns/wipes the secret; the worker retains only its bounded copy.
        struct Secret
        {
            std::string value;
            explicit Secret(const char* text) : value(text) {}
            ~Secret() { if (!value.empty()) SecureZeroMemory(&value[0], value.size()); }
        } secret(szPassword);
        cys::net::Options options;
        options.mode = cys::net::SessionMode::Interactive;
        options.overallTimeoutMs = 600000;
        options.idleTimeoutMs = 300000;
        options.approvedGamePort = profile.gamePort; // CYS: character login hand-off profile.
        options.gamePortFromReply = profile.gamePortFromReply;
        options.allowedHosts = profile.allowedHosts;
        if (!m_loginClient->start(endpoint, szUserName, secret.value, options))
        {
            s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, u8"Không bắt đầu được đăng nhập; kiểm tra tài khoản rồi thử lại.");
            TDU_Log("CYS WSL login: start rejected code=%d", static_cast<int>(m_loginClient->lastStartError()));
            return 0;
        }
        m_strUserName = szUserName;
        if (!m_strPassword.empty()) SecureZeroMemory(&m_strPassword[0], m_strPassword.size());
        m_strPassword.clear();
        m_modernGamePort = profile.gamePort;
        m_modernAllowedHosts = profile.allowedHosts;
        m_modernProfileName = profile.name;
        m_modernLogin = true;
        SetStatus(LOGIN_CONNECTING);
        s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO_NO_BUTTON, u8"Đang kết nối máy chủ thử nghiệm...");
        TDU_Log("CYS WSL login: started profile=%s game_port=%s%u hosts=%u", profile.name.c_str(),
            profile.gamePortFromReply ? "reply" : "", static_cast<unsigned>(profile.gamePort),
            static_cast<unsigned>(profile.allowedHosts.size()));
        return 0;
    }
#else
    char optIn[8] = {};
    if (GetEnvironmentVariableA("CYS_WSL_LOGIN_TEST", optIn, sizeof(optIn)) == 1 && optIn[0] == '1')
    {
        s_pEventSystem->PushEvent(GE_GAMELOGIN_SHOW_SYSTEM_INFO, u8"Bản dựng này chưa có lớp đăng nhập mới.");
        return 0;
    }
#endif

	if(NULL == szUserName)
	{
		// 通知界面显示系统提示信息, 用户名错误
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO, "输入用户名错误");
		return 0;
	}

	if(NULL == szPassword)
	{
		// 通知界面显示系统提示信息, 用户名错误
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO, "输入密码错误");
		return 0;
	}

	m_strUserName = szUserName;
	m_strPassword = szPassword;
	

	//////////////////////////////////////////////////////////////////////////////////////////////////////////
	//
	// 验证用户名
	//
	int iPos = (int)m_strUserName.find('\'');
	if(-1 != iPos)
	{
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO, "用户名不能含有 \" \' \" ");
		return 0;
	}

    iPos = (int)m_strUserName.find('"');
	if(-1 != iPos)
	{
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO, "用户名不能含有 \"  \"  \" ");
		return 0;
	}

	iPos = (int)m_strUserName.find('\\');
	if(-1 != iPos)
	{
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO, "用户名不能含有 \" \\ \" ");
		return 0;
	}//


	//// 到非法字符串表中查找。
	//int iFindPos = -1;
	//char bufInfo[512];
	//for(int i = 0; i < (int)m_UnUseStrVector.size(); i++)
	//{
	//	iFindPos = (int)m_strUserName.find(m_UnUseStrVector[i]);
	//	if(-1 != iFindPos)
	//	{
	//		memset(bufInfo, 0, sizeof(bufInfo));
	//		sprintf(bufInfo, "用户名包含非法符号%s", m_UnUseStrVector[i]);
	//		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO, bufInfo);
	//		return 0;
	//	}

	//}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////
	//
	// 验证密码
	//
	iPos = (int)m_strPassword.find('\'');
	if(-1 != iPos)
	{
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO, "密码不能含有 \" \' \" ");
		return 0;
	}

    iPos = (int)m_strPassword.find('"');
	if(-1 != iPos)
	{
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO, "密码不能含有 \"  \"  \" ");
		return 0;
	}

	iPos = (int)m_strPassword.find('\\');
	if(-1 != iPos)
	{
		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO, "密码不能含有 \" \\ \" ");
		return 0;
	}//

	//// 到非法字符串表中查找
	//for(int j = 0; j < (int)m_UnUseStrVector.size(); j++)
	//{
	//	iFindPos = (int)m_strPassword.find(m_UnUseStrVector[j]);
	//	if(-1 != iFindPos)
	//	{
	//		memset(bufInfo, 0, sizeof(bufInfo));
	//		sprintf(bufInfo, "密码包含非法符号%s", m_UnUseStrVector[j]);
	//		CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO, bufInfo);
	//		return 0;
	//	}
	//}

	// 与服务器进行socket 连接.
	ConnectToLoginServer(m_iCurSelAreaId, m_iCurSelLoginServerId);
	return 0;
}

// 发送check msg 消息
int CGamePro_Login::SendCheckAccountMsg()
{
	// 发送验证密码消息
	CLAskLogin msg;
	msg.SetAccount((CHAR*)m_strUserName.c_str());
	msg.SetPassWord((CHAR*)m_strPassword.c_str());
	msg.SetVersion(1005); // MagicKey
	CNetManager::GetMe()->SendPacket(&msg);
	CGameProcedure::s_pEventSystem->PushEvent( GE_GAMELOGIN_SHOW_SYSTEM_INFO_NO_BUTTON, "正在验证密码....");

	s_pVariableSystem->SetVariable( "User_NAME", m_strUserName.c_str() );
	return 0;
}


// 
// 
//
//------------------------------------------------------------------------------------------------------------
