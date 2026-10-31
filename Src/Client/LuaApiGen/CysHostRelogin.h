#pragma once
// CYS-RELOGIN: _G.ReturnServerLogin, the "doi nhan vat" item of the CY system menu
// (Operation.lua Operation_Ret2SelServer_Clicked). Replaces the generated LuaApiGen stub.
#include "LuaPlus.h"

namespace CysHostRelogin
{
    // Registers ReturnServerLogin when the global is still nil (CysHostGlobals rule); returns 1 when added.
    unsigned Register(LuaPlus::LuaObject& globals);
}
