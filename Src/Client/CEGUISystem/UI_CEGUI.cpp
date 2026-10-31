// UI_CEGUI.cpp : Defines the entry point for the DLL application.
//

#include "stdafx.h"
#include "UIGlobal.h"
#include "UISystem.h"
#include "TDPlugin.h"
#include "TDException.h"
#include "TDKernel.h"
#include "CysUiInputBridge.h"
#include "CysUiInputAccess.h"
#include "CysUiEdit.h"
#include <type_traits>
#include <CEGUISystem.h>
#include <cmath>
#include "FalActionButton.h"
#include <CEGUIImageset.h>
#include <CEGUIImagesetManager.h>
#include <falagard/CEGUIFalWidgetLookManager.h>
#include <falagard/CEGUIFalWidgetLookFeel.h>
#include "CysUiWheelFixture.h"
#include "CysNpcIconFixture.h"
#include "CysNameboardFixture.h"
#include "UIWindowMng.h"

extern "C" __declspec(dllexport) void __cdecl CysUiBeginShutdown()
{
    CUIWindowMng::BeginShutdown();
}

extern "C" __declspec(dllexport) int __cdecl CysUiReadActionButtonUtf8(const char* name, int out[4])
{
    if (!name || !out || !g_hMainWnd || GetWindowThreadProcessId(g_hMainWnd, nullptr) != GetCurrentThreadId()) return 0;
    CEGUI::WindowManager* manager = CEGUI::WindowManager::getSingletonPtr();
    if (!manager || !manager->isWindowPresent(name)) return 0;
    CEGUI::FalagardActionButton* button = dynamic_cast<CEGUI::FalagardActionButton*>(manager->getWindow(name));
    if (!button || !button->isVisible()) return 0;
    out[0] = static_cast<int>(reinterpret_cast<INT_PTR>(button->getLogicItemData()));
    out[1] = button->isEmpty() ? 1 : 0;
    const CEGUI::String look = button->getLookNFeel();
    out[2] = !look.empty() && CEGUI::WidgetLookManager::getSingleton().getWidgetLook(look).isStateImageryPresent("BackgroundImage") ? 1 : 0;
    // CEGUI stores two colour control codepoints before the numeric caption.
    // Read codepoints, not atoi on the colour-prefixed UTF-8 representation.
    out[3] = 0;
    const CEGUI::String caption = button->getCornerChar(3);
    for (size_t i = 0; i < caption.size(); ++i)
        if (caption[i] >= '0' && caption[i] <= '9') out[3] = out[3] * 10 + static_cast<int>(caption[i] - '0');
    return 1;
}
static_assert(std::is_same<decltype(&CysUiReadActionButtonUtf8), CysUiInputBridge::ReadActionButtonUtf8>::value, "Action button snapshot bridge mismatch");

extern "C" __declspec(dllexport) const char* __cdecl CysUiGetActiveInputName()
{ return CysUiInputAccess::activeInputName(); }

extern "C" __declspec(dllexport) int __cdecl CysUiCopyWindowTextUtf8(
    const char* name, char* out, unsigned capacity)
{ return CysUiInputAccess::copyWindowTextUtf8(name, out, capacity); }

extern "C" __declspec(dllexport) int __cdecl CysUiCopySimpleTextUtf8(
    const char* name, char* out, unsigned capacity)
{ return CysUiInputAccess::copySimpleTextUtf8(name, out, capacity); }

extern "C" __declspec(dllexport) int __cdecl CysUiCopyCaptionUtf8(
    const char* name, char* out, unsigned capacity)
{
    if (out && capacity) out[0] = 0;
    if (!g_hMainWnd || GetWindowThreadProcessId(g_hMainWnd, nullptr) != GetCurrentThreadId()) return -3;
    return CysUiInputAccess::copyCaptionUtf8(name, out, capacity);
}

extern "C" __declspec(dllexport) int __cdecl CysUiGetWindowRectUtf8(
    const char* name, float out[4])
{ return CysUiInputAccess::getWindowRectUtf8(name, out); }

// CYS-GUIHARNESS 19/09: see CysUiInputBridge.h. The smoke bounds its targets with this, not GetClientRect.
extern "C" __declspec(dllexport) int __cdecl CysUiGetDisplaySize(float out[2])
{ return CysUiInputAccess::getDisplaySize(out); }

// Read-only atlas geometry for the owned UI lifecycle smoke; never loads pixels.
extern "C" __declspec(dllexport) int __cdecl CysUiReadImageMetricsUtf8(const char* set, const char* image, float out[3])
{
    if (!set || !image || !out || !g_hMainWnd || GetWindowThreadProcessId(g_hMainWnd, nullptr) != GetCurrentThreadId()) return 0;
    auto* manager = CEGUI::ImagesetManager::getSingletonPtr();
    if (!manager || !manager->isImagesetPresent(set)) return 0;
    const auto* atlas = manager->getImageset(set);
    if (!atlas->isImageDefined(image)) return 0;
    const auto& item = atlas->getImage(image);
    out[0] = item.getWidth(); out[1] = item.getHeight(); out[2] = atlas->isAutoScaled() ? 1.f : 0.f;
    return 1;
}

// CYS-CYGUI2: evidence for the HUD census of Game.exe replays - every visible window of the GUI sheet (depth 0
// and, for containers wider than half the screen, depth 1) as "name|type|l,t,w,h|text;" lines. Read-only.
extern "C" __declspec(dllexport) int __cdecl CysUiDumpVisibleWindowsUtf8(char* out, unsigned capacity)
{ return CysUiInputAccess::dumpVisibleWindowsUtf8(out, capacity); }

// CYS-RELOGIN: leaving the world for the account screen hides every managed layout. CloseAllWindow is not enough:
// it keeps DemiseType 0/6 (the HUD). Returns how many layouts are still shown (0 expected), -1 without a manager.
extern "C" __declspec(dllexport) int __cdecl CysUiHideAllWindows()
{
    if (!g_hMainWnd || GetWindowThreadProcessId(g_hMainWnd, nullptr) != GetCurrentThreadId()) return -1;
    CUIWindowMng* manager = CUIWindowMng::GetMe();
    if (!manager) return -1;
    manager->HideAllWindow();
    return manager->CountShownWindows();
}

// CYS-RELOGIN: set an Editbox text (UTF-8); the account prefill after the LogOn script cleared it. 1 = set.
extern "C" __declspec(dllexport) int __cdecl CysUiSetEditTextUtf8(const char* name, const char* text)
{
    if (!name || !text || !g_hMainWnd || GetWindowThreadProcessId(g_hMainWnd, nullptr) != GetCurrentThreadId()) return 0;
    CEGUI::WindowManager* manager = CEGUI::WindowManager::getSingletonPtr();
    if (!manager || !manager->isWindowPresent(name)) return 0;
    CEGUI::Window* window = manager->getWindow(name);
    if (!window->testClassName("Editbox")) return 0;
    window->setText(reinterpret_cast<const CEGUI::utf8*>(text));
    return 1;
}

static_assert(std::is_same<decltype(&CysUiGetWindowRectUtf8),
    CysUiInputBridge::GetWindowRectUtf8>::value, "Input rectangle bridge mismatch");
static_assert(std::is_same<decltype(&CysUiGetDisplaySize),
    CysUiInputBridge::GetDisplaySize>::value, "Display size bridge mismatch");

static_assert(std::is_same<decltype(&CysUiGetActiveInputName),
    CysUiInputBridge::GetActiveInputName>::value, "Input name bridge mismatch");
static_assert(std::is_same<decltype(&CysUiCopyWindowTextUtf8),
    CysUiInputBridge::CopyWindowTextUtf8>::value, "Input text bridge mismatch");
static_assert(std::is_same<decltype(&CysUiCopySimpleTextUtf8),
    CysUiInputBridge::CopyWindowTextUtf8>::value, "SimpleText bridge mismatch");
static_assert(std::is_same<decltype(&CysUiCopyCaptionUtf8),
    CysUiInputBridge::CopyCaptionUtf8>::value, "Caption bridge mismatch");

// Test input reaches the same CEGUI injection endpoint as CUISystem::InjectInput.
// It does not alter the user's desktop cursor, async key state or game input queue.
extern "C" __declspec(dllexport) int __cdecl CysUiSmokePointer(unsigned action, float x, float y)
{
    char enabled[4] = {};
    const HWND window = g_hMainWnd;
    if (GetEnvironmentVariableA("CYS_GL_LOGIN_SMOKE", enabled, sizeof(enabled)) != 1 || enabled[0] != '1' ||
        !strstr(GetCommandLineA(), "-cys-gl-login-smoke") || !window ||
        GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() ||
        action > 4 || !std::isfinite(x) || !std::isfinite(y)) return 0; // CYS-UNEQUIP: 3/4 = right button
    // CYS-GUIHARNESS 19/09: bound against the CEGUI display area, not GetClientRect. injectMousePosition takes
    // CEGUI pixels (the render target), so the render target is the space the point has to be inside; the Win32
    // client rect is a different rectangle that only happens to match while the window sits at its created size.
    // The safety property is unchanged and is enforced above: explicit smoke mode, this process, this GUI thread -
    // nothing here touches the desktop cursor, the async key state or any other window.
    float display[2] = {};
    if (!CysUiInputAccess::getDisplaySize(display) ||
        x < 0 || y < 0 || x >= display[0] || y >= display[1]) return 0;
    CEGUI::System* system = CEGUI::System::getSingletonPtr();
    if (!system) return 0;
    system->injectMousePosition(x, y);
    if (action == 1 && !CUISystem::GetMe()->TryEndActionDrag(x, y, FALSE))
        system->injectMouseButtonDown(CEGUI::LeftButton);
    if (action == 2) {
        system->injectMouseButtonUp(CEGUI::LeftButton);
        CUISystem::GetMe()->TryEndActionDrag(x, y, TRUE);
    }
    // CYS-UNEQUIP 20/09/2026: the right button, through the same two calls CUISystem::InjectInput makes for
    // EVENT_MOUSE_RBDOWN / EVENT_MOUSE_RBCLICK (UISystem.cpp). A rig needs it to take the player's own door to
    // every MouseRClick / RBClicked handler (take equipment off, use a bag item, cancel a buff) instead of
    // calling the C++ behind the door. Same guards as the left button: explicit smoke mode, this GUI thread.
    if (action == 3) system->injectMouseButtonDown(CEGUI::RightButton);
    if (action == 4) system->injectMouseButtonUp(CEGUI::RightButton);
    return 1;
}
static_assert(std::is_same<decltype(&CysUiSmokePointer),
    CysUiInputBridge::SmokePointer>::value, "Smoke input bridge mismatch");

// CYS-UIEDIT 19/09: cua vao cho rig do. Chung goi DUNG loi ma ma cu keo chuot goi
// (CysUiEdit::Select / MoveBy / SizeBy / Save), khong phai mot loi ma song song — nen mot luot rig
// xanh chung minh duoc vong "chon -> doi hinh hoc -> luu -> nap lai" that su chay.
// Chung KHONG chung minh duoc cu keo chuot: cu keo di qua CUISystem::InjectInput, ma duong chuot
// vat ly bi `g_cysGlLoginSmoke` chan trong moi rig live (DIEU-PHOI 19/09 muc 11).
// Moi ham deu tra 0 khi `CYS_UIEDIT` khong bang "1".
extern "C" __declspec(dllexport) int __cdecl CysUiEditSetMode(int on)
{ return CysUiEdit::SetMode(on) ? 1 : 0; }

extern "C" __declspec(dllexport) int __cdecl CysUiEditMode(void)
{ return CysUiEdit::Mode(); }

extern "C" __declspec(dllexport) int __cdecl CysUiEditSelect(const char* windowName)
{ return CysUiEdit::Select(windowName) ? 1 : 0; }

extern "C" __declspec(dllexport) int __cdecl CysUiEditGetRect(const char* windowName, float out[4])
{ return CysUiEdit::GetRect(windowName, out) ? 1 : 0; }

extern "C" __declspec(dllexport) int __cdecl CysUiEditMoveBy(float dx, float dy)
{ return CysUiEdit::MoveBy(dx, dy) ? 1 : 0; }

extern "C" __declspec(dllexport) int __cdecl CysUiEditSizeBy(float dw, float dh)
{ return CysUiEdit::SizeBy(dw, dh) ? 1 : 0; }

extern "C" __declspec(dllexport) int __cdecl CysUiEditSave(char* err, unsigned capacity)
{ return CysUiEdit::Save(err, capacity); }

extern "C" __declspec(dllexport) int __cdecl CysUiEditOpen(const char* windowName)
{ return CysUiEditOpenWindow(windowName) ? 1 : 0; }

// So luot ghi de da ap tu luc khoi dong, va so dong bang dang giu: "da ap that" phai do duoc,
// khong duoc suy tu viec file co ton tai.
extern "C" __declspec(dllexport) int __cdecl CysUiEditApplied(void)
{ return CysUiEdit::AppliedCount(); }

extern "C" __declspec(dllexport) int __cdecl CysUiEditRows(void)
{ return CysUiEdit::TableRows(); }

//Magic Code
// CYS: exported again (CEGUISystem.def) - TDPlugin.h:20 asks every plugin for it and the modern client's
// UI_CEGUI.dll exports DllInit/DllMagicCode/DllRelease; tKernel::LoadPlugin itself only uses DllInit/DllRelease.
extern "C" UINT DllMagicCode = DLL_MAGIC_CODE|TD_ENGINE_VERSION;

//插件初始化
extern "C" VOID __stdcall DllInit( tKernel* pKernel )
{
	TDAssert(pKernel);

	g_pKernel = pKernel;

	pKernel->ReisgerClass(GETCLASS(CUISystem));
}

//插件释放
extern "C" VOID __stdcall DllRelease(VOID)
{
}

// CYS-CHATENTER D4: live CUISystem resolves the named ChatHistory and inserts
// parsed text directly. CY Lua deliberately does not draw CHAT_MESSAGE events.
#include "UIString.h"
#include "../CEGUIFalagardEx/FalChatHistory.h"
BOOL CUISystem::Chat_InsertHistoryString(LPCTSTR windowName, LPCTSTR text)
{
	CEGUI::WindowManager* windows = CEGUI::WindowManager::getSingletonPtr();
	CUIStringSystem* strings = CUIStringSystem::GetMe();
	if (!windows || !strings || !windowName || !text || !windows->isWindowPresent(windowName)) return FALSE;
	CEGUI::FalagardChatHistory* history = dynamic_cast<CEGUI::FalagardChatHistory*>(windows->getWindow(windowName));
	if (!history) return FALSE;
	CEGUI::String parsed;
	strings->ParserString_Runtime(text, parsed);
	history->insertChatString(parsed);
	return TRUE;
}
