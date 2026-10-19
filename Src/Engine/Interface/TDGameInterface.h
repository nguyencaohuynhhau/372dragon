#pragma once

#include "TDBasicType.h"
#include "TDNode.h"
#include "TDMath.h"

#ifndef _GUID_T_DEFINED_
#define _GUID_T_DEFINED_
typedef UINT GUID_t;
#endif

#ifndef _ENUM_RELATION_DEFINED
#define _ENUM_RELATION_DEFINED
enum ENUM_RELATION
{
	RELATION_INVALID	= -1,
	RELATION_ENEMY,			// 敌对
	RELATION_FRIEND,		// 友好

	RELATION_NUMBERS
};
#endif

#ifndef MAKE_COLOR
#define MAKE_COLOR(r,g,b,a)		(((r&0xFF)<<24)|((g&0xFF)<<16)|((b&0xFF)<<8)|(a&0xFF))
#endif
#ifndef COLOR_R
#define COLOR_R(color)			((color>>24)&0x000000FF)
#endif
#ifndef COLOR_G
#define COLOR_G(color)			((color>>16)&0x000000FF)
#endif
#ifndef COLOR_B
#define COLOR_B(color)			((color>>8)&0x000000FF)
#endif
#ifndef COLOR_A
#define COLOR_A(color)			((color)&0x000000FF)
#endif

class CObject_ProjTex_MouseTarget;
class tActionItem;
class tObject_Item;
class CObject;

class tGameInterfaceBase : public tNode
{
public:
	tGameInterfaceBase() {}
	virtual ~tGameInterfaceBase() {}

	//------------------------------------------------
	//系统操作
	//------------------------------------------------
	// 物体操作系列
	
	//将某个物体选择为主要目标
	virtual VOID	Object_SelectAsMainTarget(INT idObj, int iType = 0) {}
	//显示右键菜单
	virtual VOID	Object_ShowContexMenu(INT idObj, BOOL showMyself = FALSE) {}

	//------------------------------------------------
	// 玩家

	//移动到场景中某个位置
	virtual VOID	Player_MoveTo(const fVector2& fvAimPos) {}
	virtual VOID	Player_AUTOHit(const int AUTOHit) {}
	//使用技能(瞬发)
	virtual VOID	Player_UseSkill(INT idSkill) {}
	//使用技能(对某个Obj)
	virtual VOID	Player_UseSkill(INT idSkill, INT idObj) {}
	//使用技能(对某个范围)
	virtual VOID	Player_UseSkill(INT idSkill, const fVector2& fvPos) {}
	//使用技能(对某个方向)
	virtual VOID	Player_UseSkill(INT idSkill, FLOAT fDir) {}
	//使用技能(对某个队友)
	virtual VOID	Player_UseSkill(INT idSkill, const GUID_t& guid) {}
	//合成某配方()
	virtual VOID	Player_UseLifeAbility(INT idPrescription, INT nMakeCount) {}
	virtual VOID	Player_UseLifeAbility(INT idPrescription) {}
	//销毁身上的装备
	virtual VOID	Player_DestroyEquip(const tObject_Item* pEquip) {}
	//卸载身上的装备
	virtual VOID	Player_UnEquip(const tObject_Item* pEquip) {}
	//请求升级
	virtual VOID	Player_AskLevelUp(VOID) {}
	//请求洗一级属性点
	virtual VOID	Player_AskCleanBat1Point(VOID) {}
	//谈话(对某个NPC)
	virtual VOID	Player_Speak(INT idObj) {}

	//------------------------------------------------
	//试图对某个TripperObj进行操作，如果距离过远，会走过去
	virtual VOID	TripperObj_Active(UINT nId) {}
	//试图捡起当前盒子里的第几项
	virtual VOID	ItemBox_PickItem(tObject_Item* pItem) {}

	//------------------------------------------------
	//使用包裹里的物品_通用
	virtual VOID	PacketItem_UserItem(tActionItem* pActionItem, int nTargetServerID, fVector2& fvPos) {}
	//使用包裹里的物品_对象
	virtual VOID	PacketItem_UserItem_ToTarget(INT nIndex, UINT nTargetID) {}
	//使用包裹里的物品_装备
	virtual VOID	PacketItem_UserEquip(INT nIndex) {}
	//使用包裹里的物品_宝石
	virtual VOID	PacketItem_UserGem(INT nGemIndex, INT EquipIndex) {}
	//销毁包裹里的物品
	virtual VOID	PacketItem_Destroy(INT nIndex) {}

	//------------------------------------------------
	//发送GM指令
	virtual VOID	GM_SendCommand(LPCSTR strCommand) {}

	virtual VOID	SendChatCommand(INT nChannelID, INT nChatType, INT nTeamID, LPCTSTR szContex, LPCTSTR szTargetName) {}

	//------------------------------------------------
	//场景

	//设置全局渲染器
	virtual VOID	Scene_SetPostFilter(LPCTSTR szFilterName) {}
	//设置LightMap
	virtual VOID	Scene_SetLightmapQuality(INT nQuality) {}

	//------------------------------------------------
	//设置当前激活技能
	virtual VOID			Skill_SetActive(tActionItem* pActiveSkill) {}
	virtual tActionItem*	Skill_GetActive(VOID) { return NULL; }

	//恢复到缺省技能操作，用于退出修理，鉴定，等特殊鼠标命令状态
	virtual VOID			Skill_CancelAction(VOID) {}

	//------------------------------------------------
	//购买物品
	virtual VOID			Booth_BuyItem(tObject_Item* pItem) {}
	//卖出商品
	virtual VOID			Booth_SellItem(tObject_Item* pItem) {}
	//完成交易
	virtual VOID			Booth_Close(VOID) {}
	//修理
	virtual VOID			Booth_Repair(BOOL bAll, INT bagindex, BOOL isInBag = TRUE) {}
	//进入修理状态
	virtual VOID			Booth_EnterRepair(VOID) {}

	//-------------------------------------------------
	//更新称号
	virtual VOID			Title_UpdateCurTitle(BYTE titleType, SHORT titleData) {}

	virtual VOID			Stall_OpenStallSale(INT nObjId) {}

	//------------------------------------------------
	//阵营判断
	virtual ENUM_RELATION	GetCampType(CObject* pObj_A, CObject* pObj_B) { return RELATION_INVALID; }

	//------------------------------------------------
	//核心继承
	//------------------------------------------------
	virtual VOID	Initial(VOID*) {}
	virtual VOID	Tick(VOID) {}
	virtual VOID	Release(VOID) {}

	WX_DECLARE_DYNAMIC(tGameInterfaceBase);
};
