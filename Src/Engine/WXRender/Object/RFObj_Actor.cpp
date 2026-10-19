#include "StdAfx.h"

#include "RFObj_Actor.h"

#include "../Global.h"
#include "../RenderSystem.h"
#include "../WXObj_ProjTex.h"

#include <OgreVector3.h>
#include <OgreSceneNode.h>
#include <OgreSceneManager.h>
#include <OgreLogmanager.h>
#include <OgreException.h>

#include "Core/WXSystem.h"
#include "Core/WXObjectFactoryManager.h"
#include "Core/WXLogicModelObject.h"
#include "Core/WXVariant.h"
#include "Core/WXFlags.h"

#include "ModelSystem/WXLogicModel.h"
#include "EffectSystem/WXEffectBasic.h"

#include "TDException.h"
#include "TDDBC_Struct.h"
#include "TDDataBase.h"
#include "TDException.h"
#include "TDKernel.h"
#include "TDUtil.h"		// CYS-CRASHANIM: TDU_Log
#include "TDWorldSystem.h"
#include "AxTrace.h"

#include <set>			// CYS-CRASHANIM: bounded "log this name once" set
#include <string>
#define		FOBJ_ACTOR_FILE			_T("logic model name")			//人物模型资源名

namespace
{
	// CYS-CRASHANIM 19/09/2026 - measurement switch, default OFF, read ONCE.
	// 1 restores the HEAD behaviour (TDAssert throws std::exception, a missing model kills the
	// process). It declares itself in WXClient.log the first time it is asked, so no run can be
	// read as the wrong arm.
	bool cysActorFileNoCatch(void)
	{
		static int s_cached = -1;
		if (s_cached < 0)
		{
			CHAR szValue[8] = {0};
			const DWORD nLen = ::GetEnvironmentVariableA("CYS_ACTORFILE_NOCATCH", szValue, sizeof(szValue));
			s_cached = (nLen == 1 && szValue[0] == '1') ? 1 : 0;
			TDU_Log("CYS-CRASHANIM: ARM=%s", s_cached
				? "control (CYS_ACTORFILE_NOCATCH=1, a missing actor model kills the process)"
				: "patched (a missing actor model is logged and skipped)");
		}
		return s_cached != 0;
	}

	// One line per distinct model name, bounded, so a monster that respawns every few seconds
	// cannot flood the log. Mirrors the "log once then return, never throw" shape of ef3c2b9e.
	bool cysFirstTimeSeen(const CHAR* szName)
	{
		static std::set<std::string> s_seen;
		if (s_seen.size() >= 256) return false;
		return s_seen.insert(szName ? szName : "").second;
	}

	// CYS-THROWSWEEP 19/09/2026 - the hole 1eb6858a opened and did not close.
	// That commit's message says "every other Actor_* method starts with if(!mModel) return;".
	// Counted on the file (throw-sweep evidence/80-mmodel-guards.json, independently reproduced
	// by red-team): 31 methods, 30 touch mModel, and SIX dereference it with no NULL test at all:
	//   SetOrientation - Object.cpp:95, EVERY turn        | Actor_SetMouseHover - ObjectManager.cpp:492, EVERY hover
	//   SetLeftWeaponEffect / SetRightWeaponEffect / SetLeftWeaponMat / SetRightWeaponMat
	// Before 1eb6858a an actor with mModel == NULL could not exist (Actor_SetFile threw first),
	// so the six were unreachable. 1eb6858a made such actors survive - which turned a DEFINITE
	// throw into an access violation one frame later. Same class of bug, quieter symptom.
	// Nothing has hit it yet (0/54 missing model names are placed in an active scene, and the
	// 19/09 patched session logged 0 "actor model ... not created"), so this closes it before
	// it is reachable, not after.
	// Never a silent return: one bounded line per call site, same shape as ef3c2b9e.
	bool cysNoModel(const CHAR* szWhere)
	{
		static std::set<std::string> s_logged;
		if (s_logged.size() < 64 && s_logged.insert(szWhere ? szWhere : "").second)
			TDU_Log("CYS-THROWSWEEP: %s called on an actor with no model; skipped "
				"(would have dereferenced NULL since 1eb6858a)", szWhere ? szWhere : "?");
		return true;
	}
}

CRFObj_Actor::CRFObj_Actor()
	: CWXObj_Object("LogicModel")
{
    mModel = NULL;
	m_bVisible = TRUE;
	m_rightWeaponModel = NULL;
	m_leftWeaponModel = NULL;
    m_leftWeaponEffect=m_rightWeaponEffect=0;
}

CRFObj_Actor::~CRFObj_Actor()
{
}

VOID CRFObj_Actor::Destroy(VOID)
{
    CWXObj_Object::Destroy();
}

VOID CRFObj_Actor::Actor_SetFile(LPCTSTR szActorFile)
{
    if(mModel) for(UINT handle:mDressEffects) mModel->delEffect(handle);
    mDressEffects.clear();mDressEffectSpec.clear();
    m_leftWeaponModel=m_rightWeaponModel=NULL;
    m_leftWeaponEffect=m_rightWeaponEffect=0;
	// CYS-CRASHANIM 19/09/2026: this function used to kill the process over one missing model.
	// TDAssert is NOT a no-op in this build (Src/Engine/Interface/TDException.h:32 is "#if 1"):
	// it calls tThrowAssertException, which throws a bare std::exception WITHOUT writing a line
	// anywhere. Nothing between here and CGameProcedure::MainLoop catches it, so the whole client
	// died silently. Measured on the user's 19/09 20:18:48 crash: the minidump stack is
	// CObjectManager::Tick -> CObject_Character::Tick -> Start_Dead -> CTripperObject_ItemBox::Initial
	// -> Actor_SetFile -> tThrowAssertException, and WXC_SML.TXT Param[2] = Render.dll+0x0020A080,
	// whose _ThrowInfo has exactly one catchable type, ".?AVexception@std@@".
	// 54 further model names of CharModelEx.txt are absent from all.obj and would each do the same.
	// Every other Actor_* method here already starts with "if(!mModel) return;", and the original
	// code even re-tests "if (mModel)" two lines below the assert: a NULL model was always meant
	// to be survivable. Now it is: log it once and leave the object without a model.
	// The control arm keeps HEAD's two asserts on HEAD's own conditions and in HEAD's own places,
	// so the two arms differ in exactly one thing: whether a missing model is fatal.
	const bool bNoCatch = cysActorFileNoCatch();
	if (bNoCatch) TDAssert(szActorFile && m_pWXObject);
	if (!szActorFile || !m_pWXObject)
	{
		TDU_Log("CYS-CRASHANIM: Actor_SetFile without a model name or render object; actor left unmodelled");
		mModel = NULL;
		return;
	}

	try
	{
		m_pWXObject->setProperty(FOBJ_ACTOR_FILE, WX::String(szActorFile));
	}
	catch (const Ogre::Exception& e)
	{
		TDU_Log("CYS-CRASHANIM: setProperty '%s' failed: %s", szActorFile ? szActorFile : "", e.getFullDescription().c_str());
	}
	catch (...)
	{
		TDU_Log("CYS-CRASHANIM: setProperty '%s' unknown exception", szActorFile ? szActorFile : "");
	}

//	AxTrace(0, 0, "%s", szActorFile);
	//检测资源Load情况
	mModel = ((WX::LogicModelObject*)m_pWXObject.get())->getLogicModel();

	if (!mModel)
	{
		if (cysActorFileNoCatch()) TDAssert(mModel);
		if (cysFirstTimeSeen(szActorFile))
			TDU_Log("CYS-CRASHANIM: actor model '%s' not created (no such <Object> in all.obj?); "
				"object kept without a model", szActorFile);
		return;
	}
	// CYS-PARITY (d3d9-trace L6): game characters (players, NPCs, monsters, pets) are actors: imodel_userdata.x = 1,
	// vertex-lit authored programs, no shadow receiving. FACT CY 3.71 Dali trace: the NPC models (赵天师, 木人, ...)
	// draw with x = 1. The LogicModelObject creates an auto-named model (scene object, x = 0), hence the override.
	if (mModel)
		mModel->setActorLighting(1);
}

VOID CRFObj_Actor::SetVisible(BOOL bVisible)
{
	//确认资源已经Load
	if(!mModel) return;

    mModel->setVisible(bVisible==TRUE);
	
	m_bVisible = bVisible;
}

VOID CRFObj_Actor::Actor_SetObjectProperty(LPCTSTR szName, LPCTSTR szValue)
{
	TDAssert(m_pWXObject);

	//确认资源已经Load
	if(!mModel) return;

	try
	{
        // Remove only the gem effect we own before replacing its child model.
        // The object's authored level effects must survive a gem reset.
        if(szName && strcmp(szName,"RightWeaponObj")==0) SetRightWeaponEffect(NULL,0);
        if(szName && strcmp(szName,"LeftWeaponObj")==0) SetLeftWeaponEffect(NULL,0);
        if(szName && strcmp(szName,"CysDressEffects")==0) {
            const std::string spec=szValue?szValue:"";
            if(spec==mDressEffectSpec) return;
            for(UINT handle:mDressEffects) mModel->delEffect(handle);
            mDressEffects.clear();mDressEffectSpec=spec;
            size_t start=0;
            while(start<spec.size()) {
                size_t end=spec.find(',',start);if(end==std::string::npos) end=spec.size();
                const std::string pair=spec.substr(start,end-start);
                size_t colon=pair.find(':');
                if(colon!=std::string::npos && colon>0 && colon+1<pair.size()) {
                    UINT h=AddEffect(pair.substr(0,colon).c_str(),pair.substr(colon+1).c_str());
                    if(h) mDressEffects.push_back(h);
                }
                start=end+1;
            }
            return;
        }
        if(szName && (strcmp(szName,"CysOrnamentHeadOffset")==0 || strcmp(szName,"CysOrnamentBackOffset")==0 || strcmp(szName,"CysSoulSupportOffset")==0)) {
            Ogre::Vector3 offset;
            if(!szValue || sscanf(szValue,"%f %f %f",&offset.x,&offset.y,&offset.z)!=3) return;
            const char* slotName=strcmp(szName,"CysSoulSupportOffset")==0?"KFSObj":strcmp(szName,"CysOrnamentHeadOffset")==0?"HeadOrnamentsObj":"BackOrnamentsObj";
            auto it=mModel->getLocatorMapIterator();
            while(it.hasMoreElements()) {
                auto& slots=it.peekNextValuePtr()->mSlotMap; // getNext() returns an OWNING copy in this Ogre version
                it.moveNext();
                auto slot=slots.find(slotName);
                if(slot!=slots.end() && slot->second.mModel) { slot->second.mModel->setPosition(offset); return; }
            }
            return;
        }
        if(szName && strcmp(szName,"CysBackAction")==0) {
            auto it=mModel->getLocatorMapIterator();
            while(it.hasMoreElements()) {
                auto& slots=it.peekNextValuePtr()->mSlotMap;it.moveNext();
                auto slot=slots.find("BackOrnamentsObj");
                if(slot!=slots.end() && slot->second.mModel) {
                    slot->second.mModel->createSkill(szValue?szValue:"",false,false,0.3f);return;
                }
            }
            return;
        }
        mModel->changeAttribute(szName, szValue);
	}
	catch(...)
	{
		int a=0;
	}
}

VOID CRFObj_Actor::Actor_GetObjectProperty(LPCTSTR szName, STRING& szValue)
{
	TDAssert(m_pWXObject);

	//确认资源已经Load
	if(!mModel) return;

    if(szName && strcmp(szName,"CysCurrentAnimation")==0) {szValue=mModel->getCurrentAnimName();return;}
    if(szName && strcmp(szName,"CysBackAnimation")==0) {
        szValue.clear();auto it=mModel->getLocatorMapIterator();
        while(it.hasMoreElements()) {
            auto& slots=it.peekNextValuePtr()->mSlotMap;it.moveNext();auto slot=slots.find("BackOrnamentsObj");
            if(slot!=slots.end() && slot->second.mModel) {szValue=slot->second.mModel->getCurrentAnimName();return;}
        }
        return;
    }
    // Q8: query a named animation's authored duration without adding a virtual
    // method to the render interface (all existing object properties still pass through).
    static const char prefix[] = "CysAnimationLength:";
    if(szName && strncmp(szName, prefix, sizeof(prefix) - 1) == 0)
    {
        char length[64];
        _snprintf(length, sizeof(length), "%.9g", mModel->getAnimationLength(szName + sizeof(prefix) - 1));
        length[sizeof(length) - 1] = 0;
        szValue = length;
        return;
    }
    mModel->getAttribute(szName, szValue);
}

VOID CRFObj_Actor::Actor_GetLocator(LPCTSTR szName, fVector3& fvPosition)
{
	TDAssert(m_pWXObject);

	//确认资源已经Load
	if(!mModel) return;

	Ogre::Matrix4 mx;
	bool bResult = mModel->getLocatorWorldTransform(szName, mx);
	if(bResult)
	{
		fVector3 fvGfx(mx[0][3], mx[1][3], mx[2][3]);

		CRenderSystem::GetMe()->Axis_Trans(CRenderSystem::AX_GFX, fvGfx, CRenderSystem::AX_GAME, fvPosition);
	}
}

inline float SMOOTH(float f1, float f2, float fper, float max_f)
{
	if(abs(f1-f2) > max_f)
	{
		return (int)(f2+0.5f);
	}

	return (int)((float)f1+(f2-f1)*(float)fper+0.5f);
}

BOOL CRFObj_Actor::Actor_GetInfoBoardPos(fVector2& fvPos, const fVector3 *pvObjPos,const float fObligeHeight)
{
	//确认资源已经Load
	if(!mModel) 
		return FALSE;

	Ogre::Vector3 vPos;
	if ( pvObjPos != NULL )
	{
		//坐标转化
		fVector3 fvGfxPos;
		CRenderSystem::GetMe()->Axis_Trans(CRenderSystem::AX_GAME, *pvObjPos, CRenderSystem::AX_GFX, fvGfxPos);

		vPos.x = fvGfxPos.x;
		vPos.y = fvGfxPos.y;
		vPos.z = fvGfxPos.z;
	}
	else
	{
		vPos =  mModel->getSceneNode()->_getDerivedPosition();
	}
	
	//非主角位置
	FLOAT s_fAddOffset = 0.1 * CRenderSystem::GetMe()->GetSacle().y;
	FLOAT s_fAddHeight = abs(mModel->getBoundingBox().getMaximum().y - mModel->getBoundingBox().getMinimum().y );
	
//	if(fObligeHeight > 0)
//		vPos.y += (fObligeHeight + s_fAddOffset);


	//使用数据表中指定高度
	if(fObligeHeight > 0)
		vPos.y += fObligeHeight*CRenderSystem::GetMe()->GetSacle().y;
	else
	//使用Boundbox高度
		vPos.y += abs(mModel->getBoundingBox().getMaximum().y - mModel->getBoundingBox().getMinimum().y );

	//增加0.1m
	vPos.y += 0.1 * CRenderSystem::GetMe()->GetSacle().y;
	
	fVector3 fvGfx(vPos.x, vPos.y, vPos.z);
	fVector3 fvScreen;
	BOOL bVisible = CRenderSystem::GetMe()->Axis_Trans(CRenderSystem::AX_GFX, fvGfx, CRenderSystem::AX_SCREEN, fvScreen);
	
	if(!bVisible) 
		return FALSE;

	//平滑处理
	fvPos.x = SMOOTH(fvPos.x, fvScreen.x, 0.8f, 3.f);
	fvPos.y = SMOOTH(fvPos.y, fvScreen.y, 0.8f, 3.f);

	return TRUE;	
}

VOID CRFObj_Actor::Actor_EnterSkill(BOOL bAnim, LPCTSTR szSkillName, BOOL bLoop, FLOAT fFuseParam)
{
	//确认资源已经Load
	if(!mModel) return;

	mModel->createSkill(szSkillName, bLoop==TRUE, bAnim==TRUE, fFuseParam);
}

// 切换动画的播放速度
// fRate		- 缩放比率
VOID CRFObj_Actor::Actor_ChangeActionRate(FLOAT fRate)
{
	if(!mModel) return;

	mModel->setGlobalAnimationRate(fRate);
}

//设置缺省动作
VOID CRFObj_Actor::Actor_SetDefaultAnim(LPCTSTR szAnimName)
{
	//确认资源已经Load
	if(!mModel) return;

	//mModel->SetDefaultAnimation(szAnimName);
}

VOID CRFObj_Actor::Actor_SetTransparent(FLOAT fTransparency, FLOAT fTime)
{
	if(!mModel) return;

	mModel->setTransparent(fTime, fTransparency);
}

VOID CRFObj_Actor::Actor_SetEffectExtraTransformInfos(UINT uEffect, fVector3& fvPosition)
{
	if(!mModel) return;

	WX::Effect *pEffect = mModel->getEffect(uEffect);
	if(pEffect != NULL)
	{
		WX::TransformInfos tempTransformInfos;
		fVector3 fvGfxPos;
		CRenderSystem::GetMe()->Axis_Trans(CRenderSystem::AX_GAME, fvPosition, CRenderSystem::AX_GFX, fvGfxPos);
		WX::TransformInfo tempTransformInfo(Ogre::Vector3(fvGfxPos.x, fvGfxPos.y, fvGfxPos.z));
		tempTransformInfos.push_back(tempTransformInfo);
		pEffect->setExtraTransformInfos(tempTransformInfos);
	}
}

VOID CRFObj_Actor::Actor_SetHairColor(UINT HairColor)
{
	//确认资源已经Load
	if(!mModel) return;

	Ogre::ColourValue color;
	color.r = (HairColor >> 24)/ 255.0;
	color.g = ((HairColor >> 16) & 0xFF)/255.0;
	color.b = ((HairColor >> 8) & 0xFF)/255.0;
	color.a = (HairColor & 0xFF)/255.0;

	mModel->setHairColour(color);
}

//设置角色动画结束通知
void CRFObj_Actor::SetAnimationEndEvent(FUNC_ONANIMATIONEND pFunc, DWORD dwParam)
{
	//确认资源已经Load
	if(!mModel) return;

	mModel->SetAnimationFinishListener( pFunc, dwParam );
}

void CRFObj_Actor::SetAnimationCanBreakEvent(FUNC_ONANIMATIONEND pFunc, DWORD dwParam)
{
	//确认资源已经Load
	if(!mModel) return;

	//mModel->SetSkillBreakTimeListener( pFunc, dwParam );
}

// CYS-PARITY: skill hit time (all.skill HitTime) -> CObject_Character::OnAnimationHit, so the target's direct impact /
// damage logic events fire at the hit key frame instead of waiting for the end of the attack animation (0.85 source had
// the listener commented out). CYS_PARITY_SKILL_HIT=0 restores the old behaviour (skill-fx-parity, 15/09).
namespace
{
	FUNC_ONANIMATIONEND s_cysHitFunc = NULL;
	bool _CysSkillHitTrampoline(const char* pAnimationName, unsigned long info, unsigned short, unsigned short)
	{
		return s_cysHitFunc ? s_cysHitFunc(pAnimationName, (UINT)info) : true;
	}
	bool _CysSkillHitEnabled()
	{
		static int s_enabled = -1;
		if(s_enabled < 0)
		{
			char text[8] = {};
			const DWORD n = ::GetEnvironmentVariableA("CYS_PARITY_SKILL_HIT", text, sizeof(text));
			s_enabled = (n == 1 && text[0] == '0') ? 0 : 1;
		}
		return s_enabled == 1;
	}
}

void CRFObj_Actor::SetAnimationHitEvent(FUNC_ONANIMATIONEND pFunc, DWORD dwParam)
{
	//确认资源已经Load
	if(!mModel)
	{
		// CYS-CRASHANIM 19/09/2026: silent hole that MY OWN safety patch above just made reachable.
		// Before it, a character with mModel == NULL could not exist - Actor_SetFile killed the
		// process first - so this early return had no live caller. Now the object survives, and
		// without the hit listener CObject_Character::_OnAnimationHit never runs, so
		// m_bBeAttackEffectShowed stays FALSE, IsLogicReady() stays false for that character and
		// Update_LogicEvent drops its queued events after 10 s without ever running them
		// (bullet-fx owns that end; this end just stops being invisible).
		// Bounded: one line per object id, at most 64.
		static std::set<DWORD> s_quiet;
		if (s_quiet.size() < 64 && s_quiet.insert(dwParam).second)
			TDU_Log("CYS-CRASHANIM: no animation-hit listener for object %lu (actor has no model); "
				"its skill-hit driven effects will not fire", (unsigned long)dwParam);
		return;
	}

	if(!_CysSkillHitEnabled()) return;	// CYS-PARITY
	s_cysHitFunc = pFunc;	// every character registers the same static CObject_Character::_OnAnimationHit
	mModel->SetSkillHitTimeListener( _CysSkillHitTrampoline, dwParam );
}

void CRFObj_Actor::SetAnimationShakeEvent(FUNC_ONANIMATIONEND pFunc, DWORD dwParam)
{
	//确认资源已经Load
	if(!mModel) return;

	mModel->SetSkillShakeTimeListener(pFunc, dwParam );
}


//特效相关
UINT CRFObj_Actor::AddEffect( LPCSTR effectName, LPCSTR locatorName )
{
	if(!mModel)
		return NULL;
	// CYS-SFX: CY data has effects whose mesh renderer points at a missing mesh (g01_buff12_01 -> "ZHUAN DI1.mesh",
	// 07e 1.3); CY 3.71 shows the rest of the buff and keeps running (run-24-fx e1-buffs). The uncaught
	// Ogre::Exception ended the offline replay p24-1 with "CYS MainLoop: fatal exception". Skip the effect instead.
	try
	{
		return mModel->addEffect( Ogre::String(effectName), Ogre::String(locatorName) );
	}
	catch(const Ogre::Exception& e)
	{
		static int s_logged = 0;
		if(s_logged++ < 16)
			Ogre::LogManager::getSingleton().logMessage("[CYS-SFX] AddEffect '" + Ogre::String(effectName ? effectName : "") +
				"' skipped: " + e.getFullDescription());
		return NULL;
	}
}

VOID CRFObj_Actor::DelEffect( UINT effect )
{
	if(!mModel)
		return ;

	mModel->delEffect( effect );
}

VOID CRFObj_Actor::DelAllEffect( VOID )
{
	if(!mModel)
		return ;
	mModel->delAllEffect( );
}

VOID CRFObj_Actor::Attach_ProjTexture(PROJTEX_TYPE type, BOOL bShow, FLOAT Ring_Range, FLOAT fHeight )
{
    if (!mModel) return;
	bool show = bShow==TRUE && Ring_Range >=0.0f;
	
    switch (type)
    {
    case SELECT_RING:
        {
			if (show)
			{
				mModel->addProjector(WX::MPT_SELECTED, "selected_projector", show, Ring_Range);
			}else
			{
				mModel->removeProjector(WX::MPT_SELECTED, "selected_projector");
			}
            break;
        }
    case SHADOW_BLOB:
        {
            mModel->addProjector(WX::MPT_SHADOW, "shadow_projector", show, Ring_Range);
            break;
        }
    }
}

void CRFObj_Actor::Attach_Object(tEntityNode *pObject, LPCTSTR szAttachLocator)
{
	if ( mModel != NULL && pObject!= NULL && pObject->GetType() == ETYPE_ACTOR && szAttachLocator != NULL )
	{
		CRFObj_Actor *pRFObject = (CRFObj_Actor*)pObject;
		WX::LogicModel* pActorImpl = pRFObject->GetActorImpl();
		if ( pActorImpl != NULL )
		{

			mModel->attachModel( szAttachLocator, pActorImpl );
		}
	}
}

void CRFObj_Actor::Detach_Object(tEntityNode *pObject)
{
	if ( mModel != NULL && pObject!= NULL && pObject->GetType() == ETYPE_ACTOR )
	{
		CRFObj_Actor *pRFObject = (CRFObj_Actor*)pObject;
		WX::LogicModel* pActorImpl = pRFObject->GetActorImpl();
		if ( pActorImpl != NULL )
		{

			mModel->detachModel( pActorImpl );
		}
	}
}

//设置VisibleFlag
VOID CRFObj_Actor::Actor_SetUIVisibleFlag(VOID)
{
	//确认资源已经Load
	if(!mModel) return;

	mModel->setVisibleFlag((unsigned int)WX::OVF_GUI_ELEMENTS);
	// CYS: optional legacy property; modern CY models already default to Normal.
	if (mModel->hasAttribute("MovementType"))
		mModel->changeAttribute("MovementType", "Normal");
}

VOID CRFObj_Actor::SetPosition(const fVector3& vPos)
{
	TDAssert(m_pWXObject);
	//确认资源已经Load
	if(!mModel) return;

	//坐标转化
	fVector3 fvGfxPos;
	CRenderSystem::GetMe()->Axis_Trans(CRenderSystem::AX_GAME, vPos, CRenderSystem::AX_GFX, fvGfxPos);

    mModel->setPosition(Ogre::Vector3(fvGfxPos.x, fvGfxPos.y, fvGfxPos.z));
}

VOID CRFObj_Actor::SetOrientation(const fVector3& vRotate)
{
	TDAssert(m_pWXObject);
	if (!mModel) { cysNoModel("SetOrientation"); return; }	// CYS-THROWSWEEP

	Ogre::Quaternion qu(Ogre::Radian(vRotate.y), Ogre::Vector3::UNIT_Y);
	mModel->setOrientation(qu);
}

VOID CRFObj_Actor::Actor_SetMouseHover(BOOL bHover)
{
	TDAssert(m_pWXObject);
	if (!mModel) { cysNoModel("Actor_SetMouseHover"); return; }	// CYS-THROWSWEEP

	mModel->setSelected(bHover==TRUE);
}

VOID CRFObj_Actor::SetLeftWeaponEffect(LPCTSTR szEffectName, UINT color)
{
    if(!mModel) return;
    WX::LogicModel* current=WX::VariantCast<WX::LogicModel*>(mModel->getAttribute("LeftWeaponObj"));
    if(current && current==m_leftWeaponModel && m_leftWeaponEffect)
        current->delEffect(m_leftWeaponEffect);
    m_leftWeaponModel=current;m_leftWeaponEffect=0;
    if(!current || !color || !szEffectName || !*szEffectName) return;
    const float alpha=(color & 255)/255.0f;
    Ogre::ColourValue colour(((color>>24)&255)/255.0f*alpha,
        ((color>>16)&255)/255.0f*alpha,((color>>8)&255)/255.0f*alpha,alpha);
    m_leftWeaponEffect=current->addEffect(szEffectName,"武器特效点",WX::LogicModel::GTIT_ALL);
    if(m_leftWeaponEffect) if(WX::Effect* effect=current->getEffect(m_leftWeaponEffect)) effect->setColour(colour);
}

VOID CRFObj_Actor::SetRightWeaponEffect(LPCTSTR szEffectName, UINT color)
{
    if(!mModel) return;
    WX::LogicModel* current=WX::VariantCast<WX::LogicModel*>(mModel->getAttribute("RightWeaponObj"));
    if(current && current==m_rightWeaponModel && m_rightWeaponEffect)
        current->delEffect(m_rightWeaponEffect);
    m_rightWeaponModel=current;m_rightWeaponEffect=0;
    if(!current || !color || !szEffectName || !*szEffectName) return;
    const float alpha=(color & 255)/255.0f;
    Ogre::ColourValue colour(((color>>24)&255)/255.0f*alpha,
        ((color>>16)&255)/255.0f*alpha,((color>>8)&255)/255.0f*alpha,alpha);
    m_rightWeaponEffect=current->addEffect(szEffectName,"武器特效点",WX::LogicModel::GTIT_ALL);
    if(m_rightWeaponEffect) if(WX::Effect* effect=current->getEffect(m_rightWeaponEffect)) effect->setColour(colour);
}

VOID CRFObj_Actor::SetRightWeaponMat(LPCTSTR szWeaponMat, LPCTSTR szMatName)
{
	if (!mModel) { cysNoModel("SetRightWeaponMat"); return; }	// CYS-THROWSWEEP

	//if(!m_rightWeaponModel)
		m_rightWeaponModel = WX::VariantCast<WX::LogicModel*>(mModel->getAttribute("RightWeaponObj"));

	if (m_rightWeaponModel)
	{
		m_rightWeaponModel->changeAttribute(szWeaponMat, szMatName);
	}
}

VOID CRFObj_Actor::SetLeftWeaponMat(LPCTSTR szWeaponMat, LPCTSTR szMatName)
{
	if (!mModel) { cysNoModel("SetLeftWeaponMat"); return; }	// CYS-THROWSWEEP

	//if(!m_leftWeaponModel)
		m_leftWeaponModel = WX::VariantCast<WX::LogicModel*>(mModel->getAttribute("LeftWeaponObj"));

	if (m_leftWeaponModel)
	{
		m_leftWeaponModel->changeAttribute(szWeaponMat, szMatName);
	}
}
