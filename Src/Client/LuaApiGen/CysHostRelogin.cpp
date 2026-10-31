// CYS-RELOGIN: see CysHostRelogin.h. In world the call sends CGAskQuit with the return-to-login flag; the
// quit_complete then switches to the account screen (CysModernHandlers::OnSessionClosed). Elsewhere it is a no-op.
#include "CysHostRelogin.h"
#include "Network/CysModernGame.h"
#include "TDUtil.h"

namespace CysHostRelogin
{
    namespace
    {
        int ReturnServerLogin(LuaPlus::LuaState*)
        {
            const bool sent = CysModernGame::RequestReturnToLogin();
            TDU_Log("CYS relogin: ReturnServerLogin requested=%d", sent ? 1 : 0);
            return 0;
        }
    }

    unsigned Register(LuaPlus::LuaObject& globals)
    {
        if (!globals.GetByName("ReturnServerLogin").IsNil()) return 0;
        globals.Register("ReturnServerLogin", ReturnServerLogin);
        return 1;
    }
}
