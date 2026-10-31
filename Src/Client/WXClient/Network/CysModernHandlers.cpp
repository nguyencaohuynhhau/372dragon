#include "StdAfx.h"
#include "CysEquipCraft.h"
#include "CysWuhunNet.h"
#include "CysDualSouls.h"
#include "CysShenDing.h"
#include "CysPetSoul.h"
#include "CysModernHandlers.h"
#include "CysTemBank.h"
#include "CysPlayerShopNet.h"
#include "../../LuaApiGen/CysHost_EquipmentSkills.h"
#include "CysTooltipContract.h"
#include "CysSelfEquipData.h"
#include "CysPvpNet.h"

bool CysSelfEquipData::Enabled()
{
    static const bool old = []() { char value[4] = {}; return ::GetEnvironmentVariableA("CYS_SELFEQUIP_STATS_OLD", value, sizeof(value)) == 1 && value[0] == '1'; }();
    return !old;
}

#if defined(CYS_HAVE_NETCORE)
// Ports of the 0.85 handlers (Network/PacketHandler/GC*Handler.cpp) to ProtocolGen
// structs. Behaviour follows the legacy code; differences are marked:
//  * invalid positions are skipped and counted instead of TDThrow;
//  * movement keeps the full modern path instead of the 0.85 single target;
//  * keys, IPs and ports of the game hand-off never enter the variable system.
#include "PgPackets_combat.h"
#include "PgPackets_core.h"
#include "PgPackets_misc.h"
#include "PgPackets_movement.h"
#include "TDTimeSystem.h"
#include "TDVariable.h"
#include "TDEventDefine.h"
#include "TDDBC_Struct.h"
#include "TDUtil.h"
#include "..\Procedure\GameProcedure.h"
#include "..\Procedure\Gamepro_ChangeScene.h"
#include "..\Procedure\GamePro_Enter.h"
#include "..\Procedure\GamePro_Main.h"
#include "..\Procedure\GamePro_Login.h" // CYS-RELOGIN: ReturnFromWorld
#include "..\Procedure\CysWorldTime.h" // CYS-SKY
#include "..\Object\ObjectManager.h"
#include "..\Object\ObjectCommandDef.h"
#include "..\Object\GMObjectLoadQueue.h"
#include "..\Object\Character\Obj_PlayerMySelf.h"
#include "..\Object\Character\Obj_PlayerOther.h"
#include "..\Object\Character\Obj_PlayerNPC.h"
#include "..\DataPool\GMDP_CharacterData.h"
#include "..\World\WorldManager.h"
#include "..\Event\GMEventSystem.h"
#include "..\WxRender\RenderSystem.h"
#include "..\GameCommand.h"
#include "..\Global.h"
#include "CGEnterScene.h"
// CYS-CORE: core loop (chat, skill, damage, death, drop box, loot, bag, skill list)
#include "PgPackets_item.h"
#include "CysItemWireCount.h"
#include "PgPackets_social.h"
#include "..\DataPool\GMDataPool.h"
#include "..\DataPool\GMUIDataPool.h"
#include "..\Object\Item\Obj_Item.h"
#include "..\Object\Surface\Obj_Special.h"
#include "..\Object\TripperObj\GMTripperObj_ItemBox.h"
#include "..\Object\TripperObj\GMTripperObj_Resource.h"
#include "..\Action\GMActionSystem.h"
#include "..\Sound\GMSoundSystem.h"
#include "..\DBC\GMDataBase.h"
#include "GCChat.h"
#include "GCTargetListAndHitFlags.h"
#include "TDUISystem.h"
#include "CysInventoryNet.h"
#include "CysInspectNet.h" // CYS-INV
#include "CysQuestNet.h" // CYS-QUEST
#include "CysCommunityNet.h"
#include "CysPartyNet.h"
#include "CysRaidNet.h"
#include "CysAllianceNet.h"
#include "CysGuildWarNet.h"
#include "CysGuildNet.h"
#include "CysSkillNet.h" // CYS-SKILL
#include "CysPowerNet.h" // CYS-POWER
#include "CysRideNet.h" // CYS-RIDE
#include "CysLeapNet.h" // CYS-MOVE2: 轻功 (GCCharFly 1262)
#include "CysPetNet.h" // CYS-PET: GCDetailAttrib_Pet 512 / GCNewPet 1216 / GCRemovePet 52 / GCManipulatePetRet 666
#include "..\DataPool\TeamOrGroup.h" // CYS-SFX: GCCharImpactListUpdate -> team impact list
#include "GameStruct_Skill.h" // CYS-REVIVE: _BUFF_IMPACT_INFO for the own buff pool (GCDetailBuff / GCDetailImpactListUpdate)
#include <cstdarg>
#include <map>
#include <vector>

// CYS-SFX: skill/impact timing diagnostics only with CYS_SFX_LOG=1 (skill-fx-parity scenario runs)
static bool CysSfxLog() { static const int on = []() { char t[4] = {}; return (::GetEnvironmentVariableA("CYS_SFX_LOG", t, sizeof(t)) == 1 && t[0] == '1') ? 1 : 0; }(); return on != 0; }

namespace
{
    unsigned g_invalidPositions = 0, g_selfDeletes = 0, g_detailAttribLogs = 0;
    CysSelfEquipData::Snapshot g_selfEquipData;
    bool g_transferTarget = false; // Scene_ID/Scene_EnterPos already come from GCNotifyChangeScene.

    bool IsMain() { return CGameProcedure::GetActiveProcedure() == (CGameProcedure*)CGameProcedure::s_pProcMain; }

    bool ValidPosition(float x, float z)
    {
        CScene* scene = (CScene*)CWorldManager::GetMe()->GetActiveScene();
        if (scene && scene->IsValidPosition(fVector2(x, z))) return true;
        ++g_invalidPositions;
        return false;
    }

    fVector3 PlanToGame(float x, float z)
    {
        fVector3 game;
        CGameProcedure::s_pGfxSystem->Axis_Trans(CRenderSystem::AX_PLAN, fVector3(x, 0.0f, z), CRenderSystem::AX_GAME, game);
        return game;
    }

    template <class Path>
    std::vector<WORLD_POS> Nodes(const Path& path)
    {
        std::vector<WORLD_POS> nodes;
        for (const auto& node : path)
        {
            if (nodes.size() >= MAX_CHAR_PATH_NODE_NUMBER) break;
            if (!ValidPosition(node.x, node.y)) return std::vector<WORLD_POS>();
            WORLD_POS pos;
            pos.m_fX = node.x; pos.m_fZ = node.y;
            nodes.push_back(pos);
        }
        return nodes;
    }

    void PushMove(CObject* object, UINT startTime, INT handle, std::vector<WORLD_POS>& nodes)
    {
        if (nodes.empty()) return;
        SCommand_Object command;
        command.m_wID = OC_MOVE;
        command.m_auParam[0] = startTime;
        command.m_anParam[1] = handle;
        command.m_anParam[2] = static_cast<INT>(nodes.size());
        command.m_apParam[3] = nodes.data(); // Copied by CObjectCommand_Move::Init.
        object->PushCommand(&command);
    }

    // GCNewPlayerHandler / GCNewPlayer_MoveHandler: create or refresh another player.
    CObject_PlayerOther* PlayerAt(uint32_t id, float x, float z, float dir, bool withDir, UINT equipVer, float speed)
    {
        CObjectManager* objects = CObjectManager::GetMe();
        CObject_PlayerOther* other = (CObject_PlayerOther*)(objects->FindServerObject((INT)id));
        const fVector3 game = PlanToGame(x, z);
        if (!other)
        {
            other = objects->NewPlayerOther((INT)id);
            SObjectInit init;
            init.m_fvPos = game;
            if (withDir) init.m_fvRot = fVector3(0.0f, dir, 0.0f);
            other->Initial(&init);
            other->SetMapPosition(fVector2(game.x, game.z));
        }
        else
        {
            other->SetMapPosition(fVector2(game.x, game.z));
            if (withDir) other->SetFaceDir(dir);
            other->Enable(OSF_VISIABLE);
            other->Disalbe(OSF_OUT_VISUAL_FIELD);
        }
        other->GetCharacterData()->Set_EquipVer((INT)equipVer);
        other->GetCharacterData()->Set_MoveSpeed(speed);
        return other;
    }

    // GCNewMonsterHandler / GCNewMonster_MoveHandler. The modern packet has no NPC
    // flag (the server fills constants), so the type stays MONSTER as in 0.85's default path.
    CObject_PlayerNPC* NpcAt(uint32_t id, float x, float z, float dir, float speed)
    {
        CObjectManager* objects = CObjectManager::GetMe();
        CObject_PlayerNPC* npc = (CObject_PlayerNPC*)objects->FindServerObject((INT)id);
        const fVector3 game = PlanToGame(x, z);
        if (!npc)
        {
            npc = objects->NewPlayerNPC((INT)id);
            SObjectInit init;
            init.m_fvPos = game;
            init.m_fvRot = fVector3(0.0f, dir, 0.0f);
            npc->Initial(&init);
        }
        else
        {
            npc->SetMapPosition(fVector2(game.x, game.z));
            npc->SetFaceDir(dir);
            npc->Enable(OSF_VISIABLE);
            npc->Disalbe(OSF_OUT_VISUAL_FIELD);
        }
        if (npc->GetNpcType() == NPC_TYPE_INVALID) npc->SetNpcType(NPC_TYPE_MONSTER);
        npc->GetCharacterData()->Set_MoveSpeed(speed);
        return npc;
    }

    void Idle(CObject* object, const fVector3& game)
    {
        SCommand_Object command;
        command.m_wID = OC_IDLE;
        command.m_afParam[0] = game.x;
        command.m_afParam[1] = game.z;
        command.m_abParam[2] = FALSE;
        object->PushCommand(&command);
    }

    void Stamp(CObject* object, const char* text)
    {
        object->PushDebugString(text);
        object->SetMsgTime(CGameProcedure::s_pTimeSystem->GetTimeNow());
    }

    void OnGCConnect(const pg::GCConnect& packet, uint32_t generation)
    {
        if (CGameProcedure::GetActiveProcedure() != (CGameProcedure*)CGameProcedure::s_pProcChangeScene)
        {
            TDU_Log("CYS world: GCConnect outside ChangeScene ignored generation=%u", generation);
            return;
        }
        // 0.85: only the first connection's scene/position are meaningful; after a
        // transfer the values of GCNotifyChangeScene are used.
        if (generation == 1 || !g_transferTarget)
        {
            CGameProcedure::s_pVariableSystem->SetAs_Int("Scene_ID", (INT)packet.sceneid);
            CGameProcedure::s_pVariableSystem->SetAs_Vector2("Scene_EnterPos", packet.position_x, packet.position_y);
        }
        g_transferTarget = false;
        TDU_Log("CYS world: GCConnect generation=%u scene=%u", generation, packet.sceneid);
        CGameProcedure::s_pProcChangeScene->SetStatus(CGamePro_ChangeScene::CHANGESCENE_RECEIVE_CGCONNECT_SUCCESS);
    }

    void OnGCEnterScene(const pg::GCEnterScene& packet)
    {
        if (CGameProcedure::GetActiveProcedure() != (CGameProcedure*)CGameProcedure::s_pProcEnter)
        {
            TDU_Log("CYS world: GCEnterScene outside Enter ignored");
            return;
        }
        CGameProcedure::s_pVariableSystem->SetAs_Int("MySelf_ID", (INT)packet.m_objID);
        CGameProcedure::s_pVariableSystem->SetAs_Vector2("MySelf_Pos", packet.m_posWorld_x, packet.m_posWorld_y);
        // CYS-SCENERES: client_res chi duoc dung khi SceneDefineEx khong co dong nao o sceneid.
        // Pho ban duoc cap ID chay 10001..10020 (define.COPY_SCENE_BEGIN) nen khong bao gio co dong.
        if (packet.is_city) CGameProcedure::s_pProcEnter->SetSceneID((INT)packet.sceneid, 0, (INT)packet.client_res);
        else CGameProcedure::s_pProcEnter->SetSceneID((INT)packet.sceneid, -1, (INT)packet.client_res);
        TDU_Log("CYS world: GCEnterScene scene=%u res=%u obj=%u city=%u", packet.sceneid, packet.client_res,
                packet.m_objID, packet.is_city);
        // The Enter procedure's ENTERSCENE_OK branch runs right after this tick's
        // network pump and calls EnterScene() once (the 0.85 handler also called it).
        CGameProcedure::s_pProcEnter->SetStatus(CGamePro_Enter::ENTERSCENE_OK);
    }

    void OnGCNewPlayer(const pg::GCNewPlayer& packet)
    {
        if (!ValidPosition(packet.m_posWorld_x, packet.m_posWorld_y)) return;
        CObjectManager* objects = CObjectManager::GetMe();
        CObject_PlayerOther* other = PlayerAt(packet.m_objID, packet.m_posWorld_x, packet.m_posWorld_y,
                                              packet.m_fDir, true, packet.m_wEquipVer, packet.m_fMoveSpeed);
        Idle(other, PlanToGame(packet.m_posWorld_x, packet.m_posWorld_y));
        objects->GetLoadQueue()->TryAddLoadTask(other->GetID(), CObject_Character::CT_PLAYEROTHER);
        if (other == (CObject_PlayerOther*)objects->GetMySelf())
            CGameProcedure::s_pGfxSystem->Scene_SetPostFilter_Death(FALSE);
        Stamp(other, "GCNewPlayer");
    }

    void OnGCNewPlayerMove(const pg::GCNewPlayer_Move& packet)
    {
        if (!ValidPosition(packet.m_posWorld_x, packet.m_posWorld_y)) return;
        std::vector<WORLD_POS> nodes = Nodes(packet.path);
        CObject_PlayerOther* other = PlayerAt(packet.m_objID, packet.m_posWorld_x, packet.m_posWorld_y,
                                              packet.dir, false, packet.m_wEquipVer, packet.speed);
        PushMove(other, 0, (INT)packet.handle, nodes);
        CObjectManager::GetMe()->GetLoadQueue()->TryAddLoadTask(other->GetID(), CObject_Character::CT_PLAYEROTHER);
        Stamp(other, "GCNewPlayer_Move");
    }

    void OnGCNewMonster(const pg::GCNewMonster& packet)
    {
        if (!ValidPosition(packet.worldPos_x, packet.worldPos_y)) return;
        CObject_PlayerNPC* npc = NpcAt(packet.m_objID, packet.worldPos_x, packet.worldPos_y, packet.dir, packet.speed);
        Idle(npc, PlanToGame(packet.worldPos_x, packet.worldPos_y));
        CObjectManager::GetMe()->GetLoadQueue()->TryAddLoadTask(npc->GetID(), CObject_Character::CT_MONSTER);
        Stamp(npc, "GCNewMonster");
    }

    void OnGCNewMonsterMove(const pg::GCNewMonster_Move& packet)
    {
        if (!ValidPosition(packet.world_pos_x, packet.world_pos_y)) return;
        std::vector<WORLD_POS> nodes = Nodes(packet.path);
        CObject_PlayerNPC* npc = NpcAt(packet.m_objID, packet.world_pos_x, packet.world_pos_y, packet.dir, packet.speed);
        PushMove(npc, 0, (INT)packet.handle, nodes);
        CObjectManager::GetMe()->GetLoadQueue()->TryAddLoadTask(npc->GetID(), CObject_Character::CT_MONSTER);
        Stamp(npc, "GCNewMonster_Move");
    }

    void OnGCDelObject(const pg::GCDelObject& packet)
    {
        CObjectManager* objects = CObjectManager::GetMe();
        const CScene* scene = (const CScene*)CWorldManager::GetMe()->GetActiveScene();
        // CYS-SCENERES: so voi ID canh phia SERVER, khong phai nServerID cua dong dinh nghia
        // (pho ban: dinh nghia tra bang client_res nen hai so khac nhau).
        if (!scene || CWorldManager::GetMe()->GetActiveSceneID() != (INT)packet.m_idScene) return;
        CObject_Character* object = (CObject_Character*)(objects->FindServerObject((INT)packet.m_objID));
        if (!object) return;
        if (object == (CObject_Character*)objects->GetMySelf())
        {
            ++g_selfDeletes; // Never destroy the own object from a delete notice.
            return;
        }
        if (object == objects->GetMainTarget()) objects->SetMainTarget(-1);
        CysPvpNet::Remove((int)packet.m_objID);
        CysInspectNet::Remove((int)packet.m_objID);
        object->Release();
        objects->DestroyObject(object);
    }

    void OnGCCharMove(const pg::GCCharMove& packet)
    {
        CObject* object = (CObject*)(CObjectManager::GetMe()->FindServerObject((INT)packet.m_objID));
        if (!object) return; // 0.85 sent CGEnvRequest here (debug); no modern mapping.
        std::vector<WORLD_POS> nodes = Nodes(packet.path);
        if (packet.flag & 0x4)
        {
            SCommand_Object stop;
            stop.m_wID = OC_STOP_MOVE;
            stop.m_anParam[0] = (INT)packet.stop_logic_count;
            stop.m_anParam[1] = 0;
            stop.m_afParam[2] = object->GetPosition().x; // 0.85 used the current position too.
            stop.m_afParam[3] = object->GetPosition().z;
            object->PushCommand(&stop);
        }
        // The own echo carries the local logic count: PushCommand ignores counts
        // that are not newer, so only server-driven moves of the own object apply.
        PushMove(object, packet.start_time, packet.handleID, nodes);
        object->Enable(OSF_VISIABLE);
        Stamp(object, "GCCharMove");
    }

    void OnGCCharMoveResult(const pg::GCCharMoveResult& packet)
    {
        CObject_PlayerMySelf* self = CObjectManager::GetMe()->GetMySelf();
        if (!self || (packet.m_objID && (INT)packet.m_objID != self->GetServerID())) return;
        // The modern result has no corrected path (0.85 OR_CUT_PATHROUTE nodes): stop on failure.
        if (OR_FAILED(packet.result)) self->OnMoveStop();
    }

    void OnGCCharBaseAttrib(const pg::GCCharBaseAttrib& packet)
    {
        CysPvpNet::Base(packet);
        CysCommunityNet::Base(packet);
        CysInspectNet::Base(packet);
        CObjectManager* objects = CObjectManager::GetMe();
        CObject* object = (CObject*)(objects->FindServerObject((INT)packet.m_objID));
        if (!object || !g_theKernel.IsKindOf(object->GetClass(), GETCLASS(CObject_Character))) return;
        int64_t f[11] = {};
        int64_t peak1 = 0, peak2 = 0;
        pg::deriveCharBaseAttribFlags(packet.new_flags.data(), f, &peak1, &peak2);
        CCharacterData* data = ((CObject_Character*)object)->GetCharacterData();
        const bool self = object == (CObject*)objects->GetMySelf();
        if(self && (f[6]&1)) CysAllianceNet::OnMembership(packet.confederate_id);
        // Bit layout from the server setters (combat.lua GCCharBaseAttrib set_*).
        if (f[0] & 0x01) data->Set_RaceID((INT)packet.model);
        if (f[0] & 0x02) data->Set_Name(packet.m_szName.c_str());
        if (f[0] & 0x04)
        {
            data->Set_CurTitle(packet.m_szTitle.c_str());
            data->Set_CurTitleType((BYTE)packet.m_TitleType);
            CEventSystem::GetMe()->PushEvent(GE_CUR_TITLE_CHANGEED);
        }
        if (f[0] & 0x08) data->Set_Level((INT)packet.level);
        if (f[0] & 0x10) data->Set_HPPercent(packet.hp_percent / 100.0f);
        if (f[0] & 0x20) data->Set_MPPercent(packet.mp_percent / 100.0f);
        if (f[0] & 0x40) data->Set_Rage((INT)packet.rage);
        if (f[0] & 0x80) data->Set_StealthLevel((INT)packet.stealth_level);
        if (f[1] & 0x02) data->Set_MoveSpeed(packet.speed);
        if ((f[1] & 0x04) && self) data->Set_AttackSpeed((INT)packet.attack_speed);
        if (f[1] & 0x08) data->Set_PortraitID((INT)packet.portrait_id);
        if (f[1] & 0x10) data->Set_ModelID(packet.model_id);
        if (f[1] & 0x20) data->Set_MountID(packet.ride_model);
        if ((f[1] & 0x40) && !self) data->Set_AIType(packet.ai_type);
        if (f[1] & 0x80) data->Set_FaceMesh((INT)packet.face_style);
        if (f[2] & 0x01) data->Set_HairMesh((INT)packet.hair_style);
        if (f[2] & 0x02) data->Set_HairColor((UINT)packet.hair_color);
        if (f[2] & 0x04) data->Set_IsInStall(packet.stall_is_open != 0);
        if (f[2] & 0x08) data->Set_StallName(packet.stall_name.c_str());
        if (f[2] & 0x10) data->Set_OccupantGUID((GUID_t)packet.occupant_guid);
        if (f[2] & 0x20) data->Set_OwnerID((ObjID_t)packet.owner_id);
        // CYS-SKILL: camp (uFlags[5] & 0x20; -1 = absent). Without it every CalcRelationType call answers FRIEND, so
        // Tab (Debug_LockNearestTarget) and the enemy cursor never saw the Vo Luong Son monsters (gui-live-2).
        if ((f[4] & 0x20) && packet.camp_id != -1) // at1(uFlags, 11, 5) & 32 of the codec
        {
            _CAMP_DATA camp;
            camp.m_nCampID = packet.camp_id;
            data->Set_CampData(&camp);
        }
        else if (!self && (f[4] & 0x20) && (f[4] & 0x40) && g_theKernel.IsKindOf(object->GetClass(), GETCLASS(CObject_PlayerNPC)))
        {
            // scenecore.lua ask_base_attrib hides monster camps <= 15 (sends -1) but marks NPCs with mp_percent 100 and
            // sends the reputation; human.lua is_enemy: reputation 0 is never an enemy. -> NPC side camp 8, else 9
            // (CampAndStand: player camp 0 is hostile to 9, friendly to 8). [SL] no CY client reference for this rule.
            _CAMP_DATA camp;
            camp.m_nCampID = (packet.mp_percent == 100 || packet.reputation == 0) ? 8 : 9;
            data->Set_CampData(&camp);
        }
        if(((CObject_Character*)object)->GetCharacterType()!=CObject_Character::CT_MONSTER) {
            if(f[9]&8) data->Set_Ornament(0,packet.exterior_head_id,packet.exterior_head_pos);
            if(f[9]&4) data->Set_Ornament(1,packet.exterior_back_id,packet.exterior_back_pos);
        }
        Stamp(object, "GCCharBaseAttrib");
    }

    // GCDetailAttribHandler (0.85) on the modern GCDetailAttrib (1772). Presence bits are the server's
    // flags[i] (1-based, combat.lua set_*); only fields with a 0.85 CCharacterData setter are applied.
    // limit_move: combat.lua set_can_move writes limit_move = (can_move == 1) ? 0 : 1 and sets flags[8] 0x40,
    // the same meaning as 0.85 DETAIL_ATTRIB_LIMIT_MOVE (CAI_MySelf::AI_MoveTo refuses to move while TRUE).
    void OnGCDetailAttrib(const pg::GCDetailAttrib& packet)
    {
        CysPvpNet::Detail(packet);
        CysWuhunNet::Detail(packet);
        if (!IsMain()) return;
        CObject* object = (CObject*)(CObjectManager::GetMe()->FindServerObject((INT)packet.m_objID));
        if (!object || !g_theKernel.IsKindOf(object->GetClass(), GETCLASS(CObject_Character))) return;
        CCharacterData* data = ((CObject_Character*)object)->GetCharacterData();
        const bool self = object == (CObject*)CObjectManager::GetMe()->GetMySelf();
        auto has = [&](int64_t index, int64_t mask) { return (pg::at1(packet.flags, index) & mask) == mask; };
        if (has(16, 32) && ((CObject_Character*)object)->GetCharacterType()!=CObject_Character::CT_MONSTER)
            data->Set_ExteriorWeapon(packet.exterior_weapon_visual_id);
        if (has(1, 1)) data->Set_Level((INT)packet.level);
        if (has(1, 2)) data->Set_HP((INT)packet.hp);
        if (has(1, 4)) data->Set_MP((INT)packet.mp);
        if (has(2, 128)) data->Set_MaxHP((INT)packet.hp_max);
        if (has(3, 1)) data->Set_MaxMP((INT)packet.mp_max);
        if (has(5, 16)) data->Set_MoveSpeed(packet.speed);
        if (!self && has(7, 64)) data->Set_MenPai((INT)packet.menpai);
        if (self)
        {
            // CY display values have their own presence-aware storage; no gameplay aliases.
            if(has(9,32))CysPowerNet::OnGongli(packet.gongli);
            const unsigned statsChanged = g_selfEquipData.Apply(packet, true);
            if (statsChanged)
            {
                static const bool statsLog = []() { char value[4] = {}; return ::GetEnvironmentVariableA("CYS_SELFEQUIP_STATS_LIVE", value, sizeof(value)) == 1 && value[0] == '1'; }();
                if (statsLog) TDU_Log("CYS selfequip stats: obj=%u present=%u changed=%u flags3=%u flags9=%u critical=%u/%u vigor=%u/%u energy=%u/%u old=%d",
                    packet.m_objID, g_selfEquipData.present, statsChanged, packet.flags[2], packet.flags[8],
                    g_selfEquipData.values[0], g_selfEquipData.values[1], g_selfEquipData.values[2],
                    g_selfEquipData.values[3], g_selfEquipData.values[4], g_selfEquipData.values[5], CysSelfEquipData::Enabled() ? 0 : 1);
                if (CysSelfEquipData::Enabled())
                {
                    if (statsChanged & 1) CEventSystem::GetMe()->PushEvent(GE_UNIT_CRITICAL_ATTACK, "player");
                    if (statsChanged & 2) CEventSystem::GetMe()->PushEvent(GE_UNIT_CRITICAL_DEFENCE, "player");
                    if (statsChanged & 12) CEventSystem::GetMe()->PushEvent(GE_UNIT_VIGOR, "player");
                    if (statsChanged & 48) CEventSystem::GetMe()->PushEvent(GE_UNIT_ENERGY, "player");
                }
            }
            // 0.85 applied the rest only to the own character (the other-player branch was commented out).
            if (has(1, 8)) data->Set_Exp((INT)packet.exp);
            if (has(1, 16)) data->Set_Money((INT)packet.money);
            if (has(1, 32)) data->Set_STR((INT)packet.str);
            if (has(1, 64)) data->Set_SPR((INT)packet.spr);
            if (has(1, 128)) data->Set_CON((INT)packet.con);
            if (has(2, 1)) data->Set_INT((INT)packet.int_);
            if (has(2, 2)) data->Set_DEX((INT)packet.dex);
            if (has(2, 4)) data->Set_PointRemain((INT)packet.point_remain);
            if (has(3, 2)) data->Set_HPRespeed((INT)packet.hp_re_speed);
            if (has(3, 4)) data->Set_MPRespeed((INT)packet.mp_re_speed);
            if (has(2, 8)) data->Set_AttPhysics((INT)packet.attrib_att_physics);
            if (has(2, 32)) data->Set_AttMagic((INT)packet.attrib_att_magic);
            if (has(2, 16)) data->Set_DefPhysics((INT)packet.attrib_def_physics);
            if (has(2, 64)) data->Set_DefMagic((INT)packet.attrib_def_magic);
            if (has(3, 8)) data->Set_Hit((INT)packet.attrib_hit);
            if (has(3, 16)) data->Set_Miss((INT)packet.attrib_miss);
            if (has(5, 4)) { data->Set_Rage((INT)packet.rage); CEventSystem::GetMe()->PushEvent(GE_UNIT_RAGE); }
            if (has(5, 8)) data->Set_StrikePoint((INT)packet.strike_point);
            if (has(5, 64)) data->Set_AttCold((INT)packet.att_cold);
            if (has(5, 128)) data->Set_DefCold((INT)packet.def_cold);
            if (has(6, 4)) data->Set_AttFire((INT)packet.att_fire);
            if (has(6, 8)) data->Set_DefFire((INT)packet.def_fire);
            if (has(6, 64)) data->Set_AttLight((INT)packet.att_light);
            if (has(6, 128)) data->Set_DefLight((INT)packet.def_light);
            if (has(7, 4)) data->Set_AttPosion((INT)packet.att_poison);
            if (has(7, 8)) data->Set_DefPosion((INT)packet.def_poison);
            if (has(7, 64)) {
                const bool changed = data->Get_MenPai() != (INT)packet.menpai;
                data->Set_MenPai((INT)packet.menpai);
                if (changed) CysSkillNet::OnMenpaiChanged();
            }
            if (has(11, 16)) { _CAMP_DATA camp; camp.m_nCampID = packet.camp_id; data->Set_CampData(&camp); } // CYS-SKILL: own camp
            if (has(7, 128)) data->Set_Guild((INT)packet.guild_id);
            if (has(13, 16)) CysAllianceNet::OnMembership(packet.confederate_id);
            if (has(8, 4)) data->Set_ModelID((INT)packet.model_id);
            if (has(8, 8)) data->Set_MountID((INT)packet.ride_model);
            if (has(8, 16)) CysPetNet::OnCurrentPetGuid(packet.pet_guid.m_uHighSection, packet.pet_guid.m_uLowSection); // CYS-PET: 0.85 GCDetailAttribHandler.cpp:313 Set_CurrentPetGUID
            if (has(8, 32)) CysPetNet::OnSoulPetGuid(packet.soul_melting_pet_guid.m_uHighSection, packet.soul_melting_pet_guid.m_uLowSection);
            if (has(8, 64)) data->SetLimitMove(packet.limit_move != 0);
            if (has(12, 16)) data->Set_RMB((INT)packet.yuanbao);
            // CYS-ITEMTEXT: the other CY currencies of the bag (Packet.lua): bind yuanbao, zengdian, jiaozi (shop 8)
            if (has(12, 32)) data->Set_BindRMB((INT)packet.bind_yuanbao);
            if (has(15, 8)) data->Set_ZengDian((INT)packet.zengdian);
            if (has(14, 4)) data->Set_MoneyJZ((INT)packet.jiaozi);
        }
        if (g_detailAttribLogs++ < 8)
            TDU_Log("CYS world: GCDetailAttrib obj=%u self=%d level=%d limit_move=%s speed=%s",
                    packet.m_objID, self ? 1 : 0, has(1, 1) ? (int)packet.level : -1,
                    has(8, 64) ? (packet.limit_move ? "1" : "0") : "absent",
                    has(5, 16) ? "set" : "absent");
        Stamp(object, "GCDetailAttrib");
    }

    // CYS-CORE ==========================================================================================
    CysModernHandlers::CoreCounters g_core = {};
    std::map<uint64_t, uint16_t> g_itemGuidMask; // (world<<40 | server<<32 | serial) -> CY guid mask
    CysEquipmentTooltip::Cache g_equipmentTooltip;
    unsigned g_coreLogs = 0;

    uint64_t GuidKey(uint8_t world, uint8_t server, uint32_t serial)
    {
        return (static_cast<uint64_t>(world) << 40) | (static_cast<uint64_t>(server) << 32) | serial;
    }

    // pg::ItemInfo_bos (CY item_types.lua bos) -> 0.85 _ITEM. Head fields are identical (guid, index, rule, bind
    // flags, 3 params); the class blob of CY is opaque here (unknow_19..23), so only the fields the UI reads are set.
    void ToLegacyItem(const pg::ItemInfo_bos& in, _ITEM& out)
    {
        CysEquipmentSkills::Remember(in);
        CysPetSoul::Remember(in);
        out.CleanUp();
        out.m_ItemGUID.m_World = in.guid.world;
        out.m_ItemGUID.m_Server = in.guid.server;
        out.m_ItemGUID.m_Serial = static_cast<INT>(in.guid.series);
        out.m_ItemIndex = static_cast<UINT>(in.item_index);
        out.m_RulerID = static_cast<CHAR>(in.rule);
        out.m_nsBind = static_cast<CHAR>(in.status & ~IEL_CREATOR); // creator text is copied below, never re-read from a stream
        memcpy(out.m_Param, in.unknow_4.data(), sizeof(out.m_Param));
        const std::string creator = in.creator_text();
        strncpy(out.m_Creator, creator.c_str(), MAX_ITEM_CREATOR_NAME - 1);
        g_itemGuidMask[GuidKey(in.guid.world, in.guid.server, in.guid.series)] = in.guid.mask;
        switch (out.ItemClass())
        {
        case ICLASS_EQUIP:
            if (in.equip_type == 0 || in.equip_type == 1) // normal/Anqi share the CY common attribute block
            {
                const bool kept = g_equipmentTooltip.Remember(in.guid.world, in.guid.server, in.guid.series,
                    in.item_index, in.attr_type, in.attr_count, in.attr_values);
                if (kept) g_equipmentTooltip.Metadata(in.guid.world,in.guid.server,in.guid.series,
                    in.quality,in.slot_count,in.enhancement_level,in.list_2,in.creator_text());
                if (kept) g_equipmentTooltip.Details(in.guid.world,in.guid.server,in.guid.series,in.status,
                    in.qualifications,static_cast<uint16_t>(in.diaowen_id*8+(static_cast<uint16_t>(in.dmc)&7)),
                    in.reshuffle_count,in.dw_advance_level,in.dw_rankexp,in.dw_featuresid,in.equip_type,in.aq_xiulian);
                if (CysTooltipContract::Probe()) TDU_Log("CYS-FT-TOOLTIP: instance item=%d guid=%u/%u/%u count=%u mask=%I64u kept=%d equip_type=%u",
                    in.item_index, (unsigned)in.guid.world, (unsigned)in.guid.server, (unsigned)in.guid.series,
                    (unsigned)in.attr_count, in.attr_type, kept ? 1 : 0, (unsigned)in.equip_type);
                if (kept && CysTooltipContract::Probe())
                {
                    unsigned value = 0;
                    for (unsigned bit = 0; bit < 58; ++bit)
                        if (in.attr_type & (uint64_t(1) << bit))
                            TDU_Log("CYS-FT-TOOLTIP: instance-value item=%d type=%u value=%u", in.item_index, bit, (unsigned)in.attr_values[value++]);
                }
            }
            else g_equipmentTooltip.Forget(in.guid.world, in.guid.server, in.guid.series);
            out.GetEquipData()->m_CurDurPoint = in.dur;
            out.GetEquipData()->m_MaxDurPoint = in.max_dur;
            // GL item.lua: unknow_7 is failed high repairs, persisted per instance.
            out.GetEquipData()->m_FaileTimes = in.unknow_7;
            out.GetEquipData()->m_GemMax = in.slot_count;
            out.GetEquipData()->m_StoneCount = in.gem_count > MAX_ITEM_GEM ? MAX_ITEM_GEM : in.gem_count;
            out.GetEquipData()->m_AttrCount = 0; // CY values kept separately; bits 54..57 differ from legacy gameplay.
            break;
        case ICLASS_COMITEM: case ICLASS_MATERIAL: case ICLASS_TASKITEM:
            out.GetMedicData()->m_nCount = static_cast<BYTE>(CysItemWireCount::Get(in));
            break;
        default:
            break;
        }
    }

    void CoreLog(const char* fmt, ...)
    {
        if (g_coreLogs++ >= 64) return;
        char text[512] = {};
        va_list args; va_start(args, fmt);
        _vsnprintf(text, sizeof(text) - 1, fmt, args);
        va_end(args);
        TDU_Log("CYS core: %s", text);
    }

    // GCChatHandler: the 0.85 Talk sandbox needs a legacy GCChat; the CY frame has no camp, so the own camp is
    // used (CalcRelationType must yield RELATION_FRIEND for the line to be shown, as for every visible chat).
    void OnGCChat(const pg::GCChat& packet)
    {
        ++g_core.chats;
        if (!IsMain()) return;
        CObject_PlayerMySelf* self = CObjectManager::GetMe()->GetMySelf();
        if (!self) return;
        Packets::GCChat legacy;
        legacy.SetChatType(packet.ChatType);
        std::string text = packet.Contex;
        if (text.size() > MAX_CHAT_SIZE - 1) text.resize(MAX_CHAT_SIZE - 1);
        legacy.SetContex(const_cast<CHAR*>(text.c_str()));
        legacy.SetContexSize(static_cast<BYTE>(text.size()));
        legacy.SetSourName(packet.SourceName.c_str());
        legacy.SetSourNameSize(static_cast<BYTE>(packet.SourceName.size() < MAX_CHARACTER_NAME ? packet.SourceName.size() : MAX_CHARACTER_NAME - 1));
        legacy.SetSourID(static_cast<ObjID_t>((packet.ChatType == 0 || packet.ChatType == 26) ? packet.Sourceid : 0));
        legacy.SetSourCamp(self->GetCampData() ? self->GetCampData()->m_nCampID : -1);
        legacy.SetWorldChatID(0);
        CoreLog("GCChat type=%u from=%s obj=%u bytes=%u", static_cast<unsigned>(packet.ChatType), packet.SourceName.c_str(),
                static_cast<unsigned>(packet.Sourceid), static_cast<unsigned>(text.size()));
        SCRIPT_SANDBOX::Talk::s_Talk.HandleRecvTalkPacket(&legacy);
    }

    // GCCharSkill_Send / _Gather / _Lead handlers: OC_MAGIC_SEND / CHARGE / CHANNEL object commands.
    void OnSkillAction(uint32_t objId, WORD command, uint32_t logic, uint32_t skill, int32_t target, float tx, float ty, float dir, uint32_t total, const char* stamp)
    {
        if (!IsMain()) return;
        CObject* object = (CObject*)CObjectManager::GetMe()->FindServerObject((INT)objId);
        if (!object) return;
        SCommand_Object cmd;
        cmd.m_wID = command;
        cmd.m_auParam[0] = 0;
        cmd.m_anParam[1] = (INT)logic;
        cmd.m_anParam[2] = (INT)skill;
        cmd.m_anParam[3] = target;
        cmd.m_afParam[4] = tx;
        cmd.m_afParam[5] = ty;
        cmd.m_afParam[6] = dir;
        cmd.m_auParam[7] = total;
        const BOOL pushed = object->PushCommand(&cmd);
        if (CysSfxLog()) TDU_Log("CYS-SFX: t=%lu %s obj=%u skill=%u target=%d logic=%u pushed=%d", (unsigned long)::GetTickCount(), stamp, objId, skill, target, logic, pushed ? 1 : 0); // CYS-SFX
        Stamp(object, stamp);
        ++g_core.skillSends;
    }

    void PushDamage(uint32_t receiver, const _DAMAGE_INFO& info, const char* stamp)
    {
        if (!IsMain()) return;
        CObject* object = (CObject*)CObjectManager::GetMe()->FindServerObject((INT)receiver);
        if (!object) return;
        _LOGIC_EVENT logicEvent;
        logicEvent.Init(info.m_nSenderID, info.m_nSenderLogicCount, &info);
        SCommand_Object cmd;
        cmd.m_wID = OC_LOGIC_EVENT;
        cmd.m_apParam[0] = (VOID*)(&logicEvent);
        object->PushCommand(&cmd);
        Stamp(object, stamp);
    }

    // GCDetailHealsAndDamagesHandler
    void OnGCDetailHealsAndDamages(const pg::GCDetailHealsAndDamages& packet)
    {
        ++g_core.damages;
        _DAMAGE_INFO info;
        info.m_nSkillID = INVALID_ID;
        info.m_nTargetID = (ObjID_t)packet.reciver;
        info.m_nSenderID = (ObjID_t)packet.sender;
        info.m_nSenderLogicCount = (INT)packet.logic_count;
        info.m_nImpactID = INVALID_ID;
        info.m_nType = _DAMAGE_INFO::TYPE_HEAL_AND_DAMAGE;
        if (packet.dirty_flags & 1) { info.m_bHealthDirty = TRUE; info.m_nHealthIncrement = packet.hp_modification; }
        if (packet.dirty_flags & 2) { info.m_bManaDirty = TRUE; info.m_nManaIncrement = packet.mp_modification; }
        if (packet.dirty_flags & 4) { info.m_bRageDirty = TRUE; info.m_nRageIncrement = packet.rage_modification; }
        if (packet.dirty_flags & 8) { info.m_bStrikePointDirty = TRUE; info.m_nStrikePointIncrement = packet.strike_point_modification; }
        info.m_bIsCriticalHit = packet.is_critical_hit != 0;
        CoreLog("GCDetailHealsAndDamages receiver=%u sender=%d hp=%d crit=%u", packet.reciver, packet.sender,
                (packet.dirty_flags & 1) ? packet.hp_modification : 0, static_cast<unsigned>(packet.is_critical_hit));
        PushDamage(packet.reciver, info, "GCDetailHealsAndDamages");
    }

    // GCCharSkill_MissedHandler (text effect) and GCCharDirectImpactHandler (effect).
    void OnGCCharSkillMissed(const pg::GCCharSkill_Missed& packet)
    {
        _DAMAGE_INFO info;
        info.m_nSkillID = packet.skill_id;
        info.m_nTargetID = (ObjID_t)packet.reciver;
        info.m_nSenderID = (ObjID_t)packet.sender;
        info.m_nSenderLogicCount = (INT)packet.sender_logic_count;
        info.m_nType = _DAMAGE_INFO::TYPE_SKILL_TEXT;
        info.m_nImpactID = packet.flag;
        PushDamage((uint32_t)packet.reciver, info, "GCCharSkill_Missed");
    }

    void OnGCCharDirectImpact(const pg::GCCharDirectImpact& packet)
    {
        _DAMAGE_INFO info;
        info.m_nSkillID = packet.skill_id;
        info.m_nTargetID = (ObjID_t)packet.reciver;
        info.m_nSenderID = (ObjID_t)packet.sender;
        info.m_nSenderLogicCount = packet.logic_count;
        info.m_nImpactID = packet.impact_id;
        info.m_nType = _DAMAGE_INFO::TYPE_EFFECT;
        PushDamage(packet.reciver, info, "GCCharDirectImpact");
    }

    // CYS-SFX: GCCharImpactListUpdateHandler (0.85) on the modern 324: every buff of the owner becomes
    // OC_UPDATE_IMPACT_EX (ImpactSEData_V1 continuous effect + bind point), after RemoveAllImpact; team list refresh.
    void OnGCCharImpactListUpdate(const pg::GCCharImpactListUpdate& packet)
    {
        if (!IsMain()) return;
        CObject* object = (CObject*)CObjectManager::GetMe()->FindServerObject((INT)packet.m_nOwnerID);
        if (!object) return;
        if (g_theKernel.IsKindOf(object->GetClass(), GETCLASS(CObject_Character)))
            ((CObject_Character*)object)->RemoveAllImpact();
        SimpleImpactList impacts;
        for (size_t i = 0; i < packet.m_aImpact.size() && i < packet.size; ++i)
        {
            SCommand_Object cmd;
            cmd.m_wID = OC_UPDATE_IMPACT_EX;
            cmd.m_adwParam[0] = (DWORD)(INT)packet.m_aImpact[i].buffer_id;
            object->PushCommand(&cmd);
            impacts.AddImpact(packet.m_aImpact[i].buffer_id);
        }
        if (CTeamOrGroup* team = CUIDataPool::GetMe()->GetTeamOrGroup())
            team->UpdateImpactsList((INT)packet.m_nOwnerID, &impacts);
        TDU_Log("CYS world: GCCharImpactListUpdate owner=%u impacts=%u", packet.m_nOwnerID, static_cast<unsigned>(packet.m_aImpact.size()));
        Stamp(object, "GCCharImpactListUpdate");
    }

    // CYS-BULLETLIVE 20/09/2026 (bullet-live): chot 1 cua duong dan, tat mac dinh.
    bool CysBulletLog() { static const int on = []() { char t[4] = {}; return (::GetEnvironmentVariableA("CYS_BULLET_LOG", t, sizeof(t)) == 1 && t[0] == '1') ? 1 : 0; }(); return on != 0; }

    // GCTargetListAndHitFlagsHandler: bullet logic events per target (or per position).
    void OnGCTargetListAndHitFlags(const pg::GCTargetListAndHitFlags& packet)
    {
        ++g_core.targetLists;
        if (!IsMain()) return;
        CObject* object = (CObject*)CObjectManager::GetMe()->FindServerObject((INT)packet.m_objID);
        if (!object) return;
        const CHAR* senderLocator = NULL;
        BOOL hitCharacter = FALSE;
        INT bulletId = INVALID_ID;
        if (packet.data_type == GCTargetListAndHitFlags::SPECIAL_OBJ_ACTIVATE)
        {
            DBC_DEFINEHANDLE(s_pSpecialDataDBC, DBC_SPECIAL_OBJ_DATA);
            const _DBC_SPECIAL_OBJ_DATA* special = (const _DBC_SPECIAL_OBJ_DATA*)s_pSpecialDataDBC->Search_Index_EQU((INT)packet.skill_or_special_obj_id);
            if (special) { bulletId = special->m_nBulletID; hitCharacter = TRUE; }
        }
        else
        {
            DBC_DEFINEHANDLE(s_pSkillTable, DBC_SKILL_DATA);
            const _DBC_SKILL_DATA* skill = (const _DBC_SKILL_DATA*)s_pSkillTable->Search_Index_EQU((INT)packet.skill_or_special_obj_id);
            if (skill)
            {
                bulletId = skill->m_nBulletID;
                senderLocator = skill->m_pszBulletSendLocator;
                hitCharacter = !(skill->m_nSelectType == SELECT_TYPE_POS || skill->m_nSelectType == SELECT_TYPE_DIR);
            }
        }
        // CYS-BULLETLIVE: chot 1 - goi 909 co toi khong, va no doc ra bullet id nao.
        if (CysBulletLog())
            TDU_Log("CYS-BULLETLIVE: pkt909 t=%lu obj=%u skill_or_special=%u data_type=%u logic=%u bullet=%d "
                "locator=[%s] hit_character=%d targets=%u is_character=%d",
                (unsigned long)::GetTickCount(), (unsigned)packet.m_objID, (unsigned)packet.skill_or_special_obj_id,
                (unsigned)packet.data_type, (unsigned)packet.logic_count, (int)bulletId,
                senderLocator ? senderLocator : "", (int)hitCharacter, (unsigned)packet.target_list.size(),
                g_theKernel.IsKindOf(object->GetClass(), GETCLASS(CObject_Character)) ? 1 : 0);
        if (bulletId != INVALID_ID && g_theKernel.IsKindOf(object->GetClass(), GETCLASS(CObject_Character)))
        {
            _LOGIC_EVENT_BULLET bullet;
            bullet.m_nBulletID = bulletId;
            bullet.m_pszSenderLocator = senderLocator;
            bullet.m_bHitTargetObj = hitCharacter;
            _LOGIC_EVENT logicEvent;
            logicEvent.Init((ObjID_t)packet.m_objID, (INT)packet.logic_count, &bullet);
            SCommand_Object cmd;
            cmd.m_wID = OC_LOGIC_EVENT;
            cmd.m_apParam[0] = (VOID*)(&logicEvent);
            CObject_Character* character = (CObject_Character*)object;
            if (hitCharacter)
            {
                for (size_t i = 0; i < packet.target_list.size(); ++i)
                {
                    logicEvent.m_bullet.m_nTargetID = (ObjID_t)packet.target_list[i];
                    character->PushCommand(&cmd);
                }
            }
            else
            {
                logicEvent.m_bullet.m_fTargetX = packet.target_pos_x;
                logicEvent.m_bullet.m_fTargetZ = packet.target_pos_y;
                character->PushCommand(&cmd);
            }
        }
        Stamp(object, "GCTargetListAndHitFlags");
    }

    // GCPlayerDieHandler / GCPlayerReliveHandler: the relive dialog (Lua answers with CGPlayerDieResult).
    void OnGCPlayerDie(const pg::GCPlayerDie& packet)
    {
        ++g_core.dies;
        if (!IsMain()) return;
        CHAR seconds[32];
        _snprintf(seconds, 32, "%d", (INT)(packet.utime / 1000));
        CoreLog("GCPlayerDie can_relive=%u time=%s s", packet.can_relive, seconds);
        CEventSystem::GetMe()->PushEvent(GE_RELIVE_SHOW, packet.can_relive ? "1" : "0", seconds, -1);
    }

    void OnGCPlayerRelive()
    {
        ++g_core.relives;
        if (!IsMain()) return;
        CEventSystem::GetMe()->PushEvent(GE_RELIVE_HIDE);
        CUIDataPool::GetMe()->EndOutGhostTimer();
    }

    // CYS-REVIVE 20/09/2026: the player's OWN buff pool (CDataPool::BuffImpact_*) was filled only by the 0.85
    // GCDetailBuffHandler / GCDetailImpactListUpdateHandler, and those never run in a modern session. So
    // Player:GetBuffNumber() stayed 0, PlayerImpactFrame hid itself (buff_num == 0 -> this:Hide()), and the
    // player saw none of their own buffs. That also closed the MouseRClick that cancels a buff whose
    // StandardImpact row says 是否可以被手动取消 = 1 - the only way out of 59901 地府束缚 (CanAction1/2 = CanMove = 0,
    // 总持续时间 = -1, 下线就消失否 = 0: its own flags block both automatic exits, 开始动作 and 开始移动).
    // BuffImpact_Add / _Remove / _RemoveAll each push GE_IMPACT_SELF_UPDATE, so the frame refreshes itself.
    void OnGCDetailBuff(const pg::GCDetailBuff& packet)
    {
        if (!IsMain()) return;
        CDataPool* pool = CDataPool::GetMe();
        if (packet.enable)
        {
            _BUFF_IMPACT_INFO info;
            info.m_nReceiverID = (ObjID_t)packet.reciver;
            info.m_nSenderID = (ObjID_t)packet.sender;
            info.m_nBuffID = (ImpactID_t)packet.buffer_id;
            info.m_nSkillID = (SkillID_t)packet.skill_id;
            info.m_nSenderLogicCount = (INT)packet.logic_count;
            info.m_nSN = (UINT)packet.sn;
            info.m_nTimer = (INT)packet.continuance;
            pool->BuffImpact_Add(&info);
            CoreLog("GCDetailBuff add sn=%u buff=%d skill=%d time=%d", packet.sn, (int)packet.buffer_id,
                    (int)packet.skill_id, (int)packet.continuance);
        }
        else
        {
            pool->BuffImpact_Remove((INT)packet.sn);
            CoreLog("GCDetailBuff remove sn=%u", packet.sn);
        }
    }

    void OnGCDetailImpactListUpdate(const pg::GCDetailImpactListUpdate& packet)
    {
        if (!IsMain()) return;
        // scenecore.lua:1753 send_char_detail_impact_list(who, to) may describe ANOTHER character to me, and the
        // 0.85 handler wiped the own pool and refilled it from whoever `who` was. Only the own list belongs here.
        CObjectManager* objects = CObjectManager::GetMe();
        CObject* owner = objects ? (CObject*)objects->FindServerObject((INT)packet.m_objID) : NULL;
        if (!objects || !owner || owner != (CObject*)objects->GetMySelf())
        {
            CoreLog("GCDetailImpactListUpdate owner=%u is not MySelf; own buff pool untouched", packet.m_objID);
            return;
        }
        CDataPool* pool = CDataPool::GetMe();
        pool->BuffImpact_RemoveAll();
        size_t added = 0;
        for (size_t i = 0; i < packet.list.size() && i < packet.size; ++i)
        {
            _BUFF_IMPACT_INFO info;
            info.m_nReceiverID = (ObjID_t)packet.m_objID;
            info.m_nSenderID = (ObjID_t)packet.list[i].sender;
            info.m_nBuffID = (ImpactID_t)packet.list[i].buffer_id;
            info.m_nSkillID = (SkillID_t)packet.list[i].skill_id;
            info.m_nSenderLogicCount = -1;
            info.m_nSN = (UINT)packet.list[i].sn;
            info.m_nTimer = (INT)packet.list[i].continuance;
            pool->BuffImpact_Add(&info);
            ++added;
        }
        CoreLog("GCDetailImpactListUpdate owner=%u buffs=%u", packet.m_objID, static_cast<unsigned>(added));
    }

    // GCNewItemBoxHandler: a drop box waits for the corpse animation of a live monster; otherwise it is created now.
    void OnGCNewItemBox(const pg::GCNewItemBox& packet)
    {
        ++g_core.itemBoxes;
        if (!IsMain() || !ValidPosition(packet.world_pos_x, packet.world_pos_y)) return;
        CObjectManager* objects = CObjectManager::GetMe();
        CoreLog("GCNewItemBox obj=%u owner=%d monster=%d type=%d at %.1f,%.1f", packet.m_objID, packet.owner_guid, packet.monster_id,
                packet.item_box_type, packet.world_pos_x, packet.world_pos_y);
        if (packet.item_box_type == ITYPE_DROPBOX)
        {
            CObject* monster = (CObject*)objects->FindServerObject(packet.monster_id);
            if (monster && g_theKernel.IsKindOf(monster->GetClass(), GETCLASS(CObject_Character)) && !((CObject_Character*)monster)->IsDie())
            {
                WORLD_POS pos; pos.m_fX = packet.world_pos_x; pos.m_fZ = packet.world_pos_y;
                ((CObject_Character*)monster)->AddDropBoxEvent((ObjID_t)packet.m_objID, (GUID_t)packet.owner_guid, &pos);
                return;
            }
            CTripperObject_ItemBox* box = (CTripperObject_ItemBox*)objects->NewObject("CTripperObject_ItemBox", (INT)packet.m_objID);
            if (!box) return;
            // The drop-box model (StrDictionary Obj_ItemBox_DropBox) may be missing in the CY data set: keep the
            // object (loot works through CGOpenItemBox) and log instead of dropping the packet.
            try { box->Initial(NULL); }
            catch (...) { CoreLog("GCNewItemBox obj=%u: drop-box model not created (Obj_ItemBox_DropBox); object kept without a model", packet.m_objID); }
            box->SetMapPosition(fVector2(packet.world_pos_x, packet.world_pos_y));
            box->SetOwnerGUID((UINT)packet.owner_guid);
            return;
        }
        CTripperObject_Resource* resource = (CTripperObject_Resource*)objects->NewObject("CTripperObject_Resource", (INT)packet.m_objID);
        if (!resource) return;
        if (!resource->SetResourceID(packet.item_box_type)) { objects->DestroyObject(resource); return; }
        resource->Initial(NULL);
        resource->SetMapPosition(fVector2(packet.world_pos_x, packet.world_pos_y));
    }

    // GCBoxItemListHandler: fill the loot data pool and open the loot window (ActionSystem::ItemBox_Created).
    void OnGCBoxItemList(const pg::GCBoxItemList& packet)
    {
        ++g_core.boxLists;
        if (!IsMain()) return;
        CDataPool::GetMe()->ItemBox_Clear();
        CDataPool::GetMe()->ItemBox_SetID((INT)packet.m_objID);
        INT slot = 0;
        for (size_t i = 0; i < packet.item_list.size(); ++i)
        {
            _ITEM item;
            ToLegacyItem(packet.item_list[i], item);
            CObject_Item* object = CObject_Item::NewItem(item.m_ItemIndex);
            if (!object) { CoreLog("GCBoxItemList item %u unknown to the item tables", item.m_ItemIndex); continue; }
            object->SetGUID((WORD)item.m_ItemGUID.m_World, (WORD)item.m_ItemGUID.m_Server, (GUID_t)item.m_ItemGUID.m_Serial);
            object->SetExtraInfo(&item);
            object->SetTypeOwner(tObject_Item::IO_ITEMBOX);
            object->SetPosIndex(slot);
            CDataPool::GetMe()->ItemBox_SetItem(slot, object);
            ++slot;
        }
        CoreLog("GCBoxItemList box=%u items=%u shown=%d", packet.m_objID, static_cast<unsigned>(packet.item_list.size()), slot);
        tObject* box = CObjectManager::GetMe()->FindServerObject((INT)packet.m_objID);
        CActionSystem::GetMe()->ItemBox_Created(box ? box->GetID() : -1);
    }

    // GCPickResultHandler (server: 0 = success; other codes are refusals).
    void OnGCPickResult(const pg::GCPickResult& packet)
    {
        if (!IsMain()) return;
        const BOOL log = CGameProcedure::s_pUISystem != NULL;
        CoreLog("GCPickResult box=%u result=%u bag_index=%u", packet.m_objID, static_cast<unsigned>(packet.result), static_cast<unsigned>(packet.bag_index));
        if (packet.result != 0)
        {
            if (log) ADDNEWDEBUGMSG(STRING(packet.result == 1 ? "背包已满!" : "非法物品!"));
            if (packet.result == 1) CSoundSystemFMod::_PlayUISoundFunc(96);
            return;
        }
        ++g_core.picksOk;
        INT index = 0;
        const tObject_Item* item = CDataPool::GetMe()->ItemBox_GetItem(packet.item_guid.world, packet.item_guid.server, (INT)packet.item_guid.series, index);
        if (!item) { if (log) ADDNEWDEBUGMSG(STRING("内部错误，非法的物品GUID!")); return; }
        ((CObject_Item*)item)->ChangeClientID();
        ((CObject_Item*)item)->SetTypeOwner(tObject_Item::IO_MYSELF_PACKET);
        tObject_Item* inBag = CDataPool::GetMe()->UserBag_GetItem(packet.bag_index);
        if (inBag)
        {
            if (item->GetIdTable() != inBag->GetIdTable()) return;
            CDataPool::GetMe()->ItemBox_SetItem(index, NULL, FALSE);
        }
        else
        {
            CDataPool::GetMe()->UserBag_SetItem(packet.bag_index, (tObject_Item*)item);
            CDataPool::GetMe()->ItemBox_SetItem(index, NULL, FALSE);
        }
        CActionSystem::GetMe()->UserBag_Update();
        CActionSystem::GetMe()->ItemBox_Update();
        // CYS-MSGEMPTY: `WIN_ITEM` khong co trong tu dien nao, cung khong co trong `Game.exe` cua
        // CY 3.71 lan cua 0.85 da phat hanh. Ca hai dung ho khoa `battle_get_*`, va ca ho deu co
        // trong hai binary: battle_get_myself (1 dac ta, ta nhat) / battle_get_other (2, nguoi khac
        // nhat) / battle_get_exp / battle_get_money / battle_nomore_exp.
        if (log) ADDTALKMSG(COLORMSGFUNC("battle_get_myself", ((CObject_Item*)item)->GetName()));
        CEventSystem::GetMe()->PushEvent(GE_LOOT_SLOT_CLEARED);
        CEventSystem::GetMe()->PushEvent(GE_PACKAGE_ITEM_CHANGED, packet.bag_index);
        if (CDataPool::GetMe()->ItemBox_GetNumber() == 0) CEventSystem::GetMe()->PushEvent(GE_LOOT_CLOSED);
    }

    // The live create_multi_item_to_bag path sends 1013 before 623. 1013 owns
    // creating the slot/GUID; 623 owns the variable item details and stack count.
    void OnGCNotifyEquip(const pg::GCNotifyEquip& packet)
    {
        if (!IsMain() || packet.bag_index >= CYS_CLIENT_BAG_SLOTS || packet.item.item_index <= 0) return;
        CDataPool* pool = CDataPool::GetMe();
        tObject_Item* old = pool->UserBag_GetItem(packet.bag_index);
        USHORT world = 0, server = 0; UINT serial = 0;
        if (old) old->GetGUID(world, server, serial);
        const bool same = old && old->GetIdTable() == packet.item.item_index &&
            world == packet.item.guid.world && server == packet.item.guid.server && serial == packet.item.guid.series;
        if (!same)
        {
            CObject_Item* object = CObject_Item::NewItem(packet.item.item_index);
            if (!object)
            {
                CoreLog("GCNotifyEquip rejected item=%d bag_index=%u (factory)", packet.item.item_index, packet.bag_index);
                return;
            }
            object->SetGUID(packet.item.guid.world, packet.item.guid.server, packet.item.guid.series);
            object->SetTypeOwner(tObject_Item::IO_MYSELF_PACKET);
            object->SetPosIndex(packet.bag_index);
            object->SetNumber(0); // Await the paired GCItemInfo, never infer quantity from a purchase reply.
            if (old) g_equipmentTooltip.Forget(static_cast<uint8_t>(world), static_cast<uint8_t>(server), serial);
            pool->UserBag_SetItem(packet.bag_index, object);
        }
        g_itemGuidMask[GuidKey(packet.item.guid.world, packet.item.guid.server, packet.item.guid.series)] = packet.item.guid.mask;
        CoreLog("GCNotifyEquip bag_index=%u item=%d same_guid=%d", packet.bag_index, packet.item.item_index, same ? 1 : 0);
        CActionSystem::GetMe()->UserBag_Update(packet.bag_index);
        CEventSystem::GetMe()->PushEvent(GE_PACKAGE_ITEM_CHANGED, packet.bag_index);
    }

    // GCItemInfoHandler: one bag slot (item_index 0 = the slot became empty).
    void OnGCItemInfo(const pg::GCItemInfo& packet)
    {
        ++g_core.itemInfos;
        if (IsMain() && CysTemBank::ItemInfo(packet)) return;
        if (IsMain() && CysPlayerShopNet::ItemInfo(packet)) return;
        if (!IsMain() || packet.bagIndex < 0 || packet.bagIndex >= CYS_CLIENT_BAG_SLOTS) return;
        _ITEM item;
        ToLegacyItem(packet.item, item);
        CoreLog("GCItemInfo bag_index=%d item=%u bag_type=%d", packet.bagIndex, item.m_ItemIndex, packet.bag_type);
        // CYS-INV: an emptied slot is item_cls.new() (index -1/0) with unknow_1 = 1 (swap, sell) or -1 (ask)
        const bool emptied = packet.item.item_index <= 0 || packet.unknow_1 != 0;
        if (emptied)
        {
            tObject_Item* removed = CDataPool::GetMe()->UserBag_GetItem(packet.bagIndex);
            if (removed)
            {
                USHORT world = 0, server = 0; UINT serial = 0;
                removed->GetGUID(world, server, serial);
                g_equipmentTooltip.Forget(static_cast<uint8_t>(world), static_cast<uint8_t>(server), serial);
            }
            g_equipmentTooltip.Forget(packet.item.guid.world, packet.item.guid.server, packet.item.guid.series);
        }
        CDataPool::GetMe()->UserBag_SetItemInfo(packet.bagIndex, emptied, emptied ? NULL : &item);
        tObject_Item* updated = CDataPool::GetMe()->UserBag_GetItem(packet.bagIndex);
        if (!emptied && updated)
            updated->SetNumber(static_cast<INT>(CysItemWireCount::Get(packet.item))); // Legacy _ITEM cannot hold gem stack counts.
        CoreLog("GCItemInfo applied bag_index=%d item=%d count=%d empty=%d", packet.bagIndex,
            updated ? updated->GetIdTable() : 0, updated ? updated->GetNumber() : 0, emptied ? 1 : 0);
        CActionSystem::GetMe()->UserBag_Update(packet.bagIndex);
        CEventSystem::GetMe()->PushEvent(GE_PACKAGE_ITEM_CHANGED, packet.bagIndex);
        CEventSystem::GetMe()->PushEvent(GE_UPDATE_SUPERTOOLTIP);
    }

    // GCMyBagListHandler: rebuild the bag from (index, guid, item, count).
    void OnGCMyBagList(const pg::GCMyBagList& packet)
    {
        ++g_core.bagLists;
        if (!IsMain()) return;
        CDataPool::GetMe()->UserBag_Clear();
        INT shown = 0;
        for (size_t i = 0; i < packet.list.size(); ++i)
        {
            const pg::GCMyBagList::E_list& entry = packet.list[i];
            CObject_Item* object = CObject_Item::NewItem(entry.item_index);
            if (!object) continue;
            object->SetGUID(entry.guid.world, entry.guid.server, (UINT)entry.guid.series);
            g_itemGuidMask[GuidKey(entry.guid.world, entry.guid.server, entry.guid.series)] = entry.guid.mask;
            object->SetNumber(entry.count);
            object->SetTypeOwner(tObject_Item::IO_MYSELF_PACKET);
            object->SetPosIndex(entry.index);
            CDataPool::GetMe()->UserBag_SetItem(entry.index, object);
            CysEquipmentSkills::RequestBagDetails(entry.index);
            ++shown;
        }
        CoreLog("GCMyBagList items=%u shown=%d", static_cast<unsigned>(packet.list.size()), shown);
        // Empty authoritative snapshots must invalidate the previous actions too.
        CActionSystem::GetMe()->UserBag_Update();
        CEventSystem::GetMe()->PushEvent(GE_PACKAGE_ITEM_CHANGED);
    }

    // GCDetailSkillListHandler. CYS-SKILL: the CY list holds SkillTemplate ids (scenecore ask_detail_skill_list ->
    // human:get_total_skills_list; CYTest00: 0,1,2,3,21,22,34,35,43), not SkillData ids; the level follows the xinfa.
    void OnGCDetailSkillList(const pg::GCDetailSkillList& packet)
    {
        ++g_core.skillLists;
        if (!IsMain()) return;
        CObject_PlayerMySelf* self = CObjectManager::GetMe()->GetMySelf();
        if (!self || self->GetServerID() != (INT)packet.m_objID) return;
        CoreLog("GCDetailSkillList skills=%u", static_cast<unsigned>(packet.m_aSkill.size()));
        CysSkillNet::OnSkillList(packet.m_aSkill);
    }

    // GCLevelUpHandler
    void OnGCLevelUp(const pg::GCLevelUp& packet)
    {
        ++g_core.levelUps;
        if (!IsMain()) return;
        CObject* object = (CObject*)CObjectManager::GetMe()->FindServerObject((INT)packet.m_objID);
        if (!object) return;
        if(g_theKernel.IsKindOf(object->GetClass(),GETCLASS(CObject_Character)))
            static_cast<CObject_Character*>(object)->GetCharacterData()->Set_Level(packet.level);
        Stamp(object, "LevelUp!");
        if (object == (CObject*)CObjectManager::GetMe()->GetMySelf())
        {
            CGameProcedure::s_pGfxSystem->PushDebugString("LeveUP");
            CSoundSystemFMod::_PlayUISoundFunc(66);
        }
        SCommand_Object cmd;
        cmd.m_wID = OC_LEVEL_UP;
        object->PushCommand(&cmd);
    }

    // CYS-MSGEMPTY 20/09/2026 - thong bao KINH NGHIEM, dung cho cua CY.
    //
    // 0.85 (ban ma nguon ta co) suy thong bao tu DELTA cua `CCharacterData::Set_Exp`, dung khoa
    // `WIN_EXP` / `WIN_ADD_EXP`. Hai khoa do KHONG co trong bat ky tu dien nao va cung KHONG co
    // trong `Game.exe` cua CY 3.71 (`9278c7f3...`) lan cua client 0.85 DA PHAT HANH
    // (`0bf34493...`) => duong do da bi bo tu truoc CY.
    //
    // Cach cua CY: mot goi RIENG, `GCDetailExp` (1288). Cau truc duoi day KHONG phai suy dien tu
    // ten khoa - no doc tu chinh ma CY bang XREF CHUOI (luat 22.8: dinh vi bang chuoi, khong bang
    // ten ham). Ban dung: CY 3.71.9609, `Bin64\Game.exe` sha256 9278c7f3...; cong cu:
    // `Tools\pipeline\cys_msgempty_cyxref.py --near 0x14058BDA0`. Ket qua, theo dung thu tu ma:
    //     0x14058BCA8 / 0x14058BD06  GCDetailExp_Human_NormalAndAdditionalExp
    //     0x14058BD1B / 0x14058BD76  GCDetailExp_Human_NormalExp
    //     0x14058BDDE / 0x14058BE39  GCDetailExp_Human_AdditionalExp
    //     0x14058BFF1 / 0x14058C04B  GCDetailExp_Pet_NormalAndAdditionalExp
    //     0x14058C09C / 0x14058C0F3  GCDetailExp_Pet_NormalExp
    //     0x14058C10B / 0x14058C162  GCDetailExp_Pet_AdditionalExp
    //     0x14058C273 GCDetailExp_DARK_NAndAexp / 0x...284 _NormalExp / 0x...28D _AdditionalExp
    // Moi khoa xuat hien DOI - dung mau `COLORMSGFUNC` + `NOCOLORMSGFUNC` (chat + thong bao noi)
    // ma 0.85 dung o `EXP_OVERFLOW`. Va co BA khoa moi nhom, khong phai hai: khi CA HAI phan kinh
    // nghiem deu > 0 thi CY gop lai thanh MOT dong (`...NormalAndAdditionalExp`, 2 dac ta), khong
    // in hai dong. Neu chi doc ten khoa ma khong doc ma thi se lam sai cho nay.
    // (`GCDetailExp_DARK_NAndAexp` dung ten RUT GON, khong theo quy luat - da kiem tay.)
    //
    // So dac ta khop so doi so o ca chin khoa (2/1/1 moi nhom) - cong o
    // `Tools\pipeline\cys_msgempty_proposal.py` doc thang bang `groups[]` duoi day tu ma nguon.
    //
    // Server dang gui goi nay moi lan giet quai (`services/scene/obj/human.lua:3528`). Truoc ban va
    // nay `WorldModel.cpp:392` chi DEM no (`stats_.expPackets`) roi `CysModernHandlers::Dispatch`
    // tra `handled=false` => client ghi "first unhandled GC id=1288" va KHONG hien dong nao.
    void OnGCDetailExp(const pg::GCDetailExp& packet)
    {
        if (!IsMain()) return;
        if (CGameProcedure::s_pUISystem == NULL) return;
        CoreLog("GCDetailExp t=%lu exp=%u award=%u pet=%u pet_award=%u anqi=%u anqi_award=%u", (unsigned long)::GetTickCount(),
                packet.exp, packet.award_exp, packet.bb_exp, packet.bb_award_exp,
                packet.anqi_exp, packet.anqi_award_exp);
        struct Group { unsigned normal; unsigned additional; const char* both; const char* onlyNormal; const char* onlyAdditional; };
        const Group groups[] = {
            { packet.exp, packet.award_exp,
              "GCDetailExp_Human_NormalAndAdditionalExp", "GCDetailExp_Human_NormalExp", "GCDetailExp_Human_AdditionalExp" },
            { packet.bb_exp, packet.bb_award_exp,
              "GCDetailExp_Pet_NormalAndAdditionalExp", "GCDetailExp_Pet_NormalExp", "GCDetailExp_Pet_AdditionalExp" },
            { packet.anqi_exp, packet.anqi_award_exp,
              "GCDetailExp_DARK_NAndAexp", "GCDetailExp_DARK_NormalExp", "GCDetailExp_DARK_AdditionalExp" },
        };
        for (int i = 0; i < (int)(sizeof(groups) / sizeof(groups[0])); ++i)
        {
            const Group& g = groups[i];
            // Dau ngoac nhon la BAT BUOC: `ADDTALKMSG` (GMInterface_Script_Talk.h:128) tu no da
            // ket thuc bang dau `;`, nen `if (..) ADDTALKMSG(x); else` no ra `...; ; else` =>
            // C2181 "illegal else without matching if". Da sap that trong lan dung dau tien.
            //
            // HAI cua, khong phai mot - va day KHONG phai lam them cho chac, day la CY:
            // xref cua CY cho THAY moi khoa xuat hien DOI (0x14058BCA8 va 0x14058BD06 cho
            // `..._NormalAndAdditionalExp`, 0x14058BD1B/0x14058BD76 cho `..._NormalExp`, ...) -
            // dung mau `COLORMSGFUNC` + `NOCOLORMSGFUNC` ma 0.85 dung o `EXP_OVERFLOW`.
            // Lan dau toi chi lam mot cua (`ADDTALKMSG`) va do la LOI: trong bo du lieu CY,
            // `ChatFrame.lua:547` de dong `ChatFrame_InsertChatContent(...)` trong CHU THICH, nen
            // su kien `GE_CHAT_MESSAGE` khong co ai ve. Cua con lai - `ADDNEWDEBUGMSG` ->
            // `GE_NEW_DEBUGMESSAGE` -> `DebugListBox.lua:26` -> `AddInfo` - thi SONG (bo dem
            // census cua chinh luot do ghi `DebugListBox_ListBox|TLBB_InfoList|0,61,1024,154`,
            // tuc dang hien tren man hinh).
            const char* key = (g.normal && g.additional) ? g.both
                            : g.normal ? g.onlyNormal
                            : g.additional ? g.onlyAdditional : NULL;
            if (!key) continue;
            if (g.normal && g.additional)
            {
                ADDTALKMSG(COLORMSGFUNC(key, (INT)g.normal, (INT)g.additional));
                ADDNEWDEBUGMSG(NOCOLORMSGFUNC(key, (INT)g.normal, (INT)g.additional));
            }
            else
            {
                const INT value = (INT)(g.normal ? g.normal : g.additional);
                ADDTALKMSG(COLORMSGFUNC(key, value));
                ADDNEWDEBUGMSG(NOCOLORMSGFUNC(key, value));
            }
        }
    }

    void OnGCOperateResult(const pg::GCOperateResult& packet)
    {
        ++g_core.operateResults;
        CoreLog("GCOperateResult %d (define.lua OPERATE_RESULT)", packet.result);
    }

    void OnGCNotifyChangeScene(const pg::GCNotifyChangeScene& packet)
    {
        const CScene* scene = (const CScene*)CWorldManager::GetMe()->GetActiveScene();
        // CYS-SCENERES: `scene_from` la ID canh phia SERVER (scenecore.lua:9250 `ret.scene_from =
        // self.id`), nen phai so voi GetActiveSceneID() chu khong phai nServerID cua dong dinh nghia.
        if (!scene || CWorldManager::GetMe()->GetActiveSceneID() != (INT)packet.scene_from)
        {
            TDU_Log("CYS world: GCNotifyChangeScene for another scene ignored from=%u to=%u", packet.scene_from, packet.scene_to);
            return;
        }
        TDU_Log("CYS world: GCNotifyChangeScene from=%u to=%u", packet.scene_from, packet.scene_to);
        // Sends CGAskChangeScene through CNetManager::SendPacket (translated to 414).
        CWorldManager::GetMe()->ChangeScene((INT)packet.scene_to, fVector2(packet.world_pos_x, packet.world_pos_y), 0, 0);
    }
}

bool CysModernHandlers::WorldReady()
{
    CWorldManager* world = CWorldManager::GetMe();
    return IsMain() && world && world->GetActiveScene() && world->GetStation() == CWorldManager::WS_ENTER &&
        CObjectManager::GetMe() && CObjectManager::GetMe()->GetMySelf();
}

bool CysModernHandlers::IsWorldPacket(uint16_t id)
{
    switch (id)
    {
    case pg::GCNewMonster::PACKET_ID: case pg::GCNewMonster_Move::PACKET_ID:
    case pg::GCNewPlayer::PACKET_ID: case pg::GCNewPlayer_Move::PACKET_ID:
    case pg::GCDelObject::PACKET_ID: case pg::GCCharMove::PACKET_ID: case pg::GCCharMoveResult::PACKET_ID:
    case pg::GCCharBaseAttrib::PACKET_ID: case pg::GCNotifyChangeScene::PACKET_ID:
    case pg::GCDetailAttrib::PACKET_ID: // CYS: needs MySelf; the first one follows GCEnterScene
    // CYS-CORE: every core-loop packet touches scene objects or the own data pool
    case pg::GCChat::PACKET_ID: case pg::GCCharSkill_Send::PACKET_ID: case pg::GCCharSkill_Gather::PACKET_ID:
    case pg::GCCharSkill_Lead::PACKET_ID: case pg::GCCharSkill_Missed::PACKET_ID: case pg::GCCharDirectImpact::PACKET_ID:
    case pg::GCTargetListAndHitFlags::PACKET_ID: case pg::GCDetailHealsAndDamages::PACKET_ID:
    case pg::GCCharImpactListUpdate::PACKET_ID: // CYS-SFX
    case pg::GCDetailBuff::PACKET_ID: case pg::GCDetailImpactListUpdate::PACKET_ID: // CYS-REVIVE: own buff pool
    case pg::GCPlayerDie::PACKET_ID: case pg::GCPlayerRelive::PACKET_ID: case pg::GCNewItemBox::PACKET_ID:
    case pg::GCBoxItemList::PACKET_ID: case pg::GCPickResult::PACKET_ID: case pg::GCItemInfo::PACKET_ID: case pg::GCNotifyEquip::PACKET_ID:
    case pg::GCMyBagList::PACKET_ID: case pg::GCDetailSkillList::PACKET_ID: case pg::GCLevelUp::PACKET_ID:
        return true;
    default:
        if (CysPvpNet::IsWorldPacket(id) || CysPetNet::IsWorldPacket(id) || CysPlayerShopNet::IsWorldPacket(id)) return true;
        return CysGuildNet::IsWorldPacket(id) || CysPartyNet::IsWorldPacket(id) || CysRaidNet::IsWorldPacket(id) || CysAllianceNet::IsWorldPacket(id) || CysGuildWarNet::IsWorldPacket(id) || CysCommunityNet::IsWorldPacket(id) || CysInspectNet::IsWorldPacket(id) || CysEquipmentSkills::IsWorldPacket(id) || CysInventoryNet::IsWorldPacket(id) || CysSkillNet::IsWorldPacket(id) || CysQuestNet::IsWorldPacket(id)
            || CysRideNet::IsWorldPacket(id) || CysLeapNet::IsWorldPacket(id); // CYS-INV, CYS-SKILL, CYS-QUEST, CYS-RIDE, CYS-MOVE2
    }
}

bool CysSelfEquipData::Query(Key key, uint32_t& value)
{
    return Enabled() && g_selfEquipData.Get(key, value);
}

void CysModernHandlers::OnConnected(uint32_t generation)
{
    CysPvpNet::Reset();
    CysInspectNet::Reset();
    // Keep details across scene transfer: legacy items retain GET_MORE_INFO and will not ask again.
    // GameClient generation 1 starts a new login session; terminal close clears separately.
    g_equipmentTooltip.ConnectionOpened(generation);
    if(generation<=1)CysEquipmentSkills::Reset();
    g_selfEquipData.ConnectionOpened(generation);
    if (CGameProcedure::GetActiveProcedure() != (CGameProcedure*)CGameProcedure::s_pProcChangeScene) return;
    const CGamePro_ChangeScene::PLAYER_CHANGE_SERVER_STATUS status = CGameProcedure::s_pProcChangeScene->GetStatus();
    if (status == CGamePro_ChangeScene::CHANGESCENE_DISCONNECT || status == CGamePro_ChangeScene::CHANGESCENE_CONNECTING)
        CGameProcedure::s_pProcChangeScene->SetStatus(CGamePro_ChangeScene::CHANGESCENE_SENDING_CGCONNECT);
    (void)generation;
}

void CysModernHandlers::OnTransfer(const pg::GCRetChangeScene& accepted)
{
    (void)accepted; // ip/port were validated by the worker; the key never leaves it.
    CWorldManager* world = CWorldManager::GetMe();
    const bool asked = IsMain() && (world->GetStation() == CWorldManager::WS_ASK_CHANGE_SCANE ||
                                    world->GetStation() == CWorldManager::WS_RELIVE);
    if (asked)
    {
        CGameProcedure::s_pVariableSystem->SetAs_Int("Scene_ID", world->GetNextSenceID());
        CGameProcedure::s_pVariableSystem->SetAs_Vector2("Scene_EnterPos", world->GetNextScenePos().x, world->GetNextScenePos().y);
        g_transferTarget = true;
    }
    else
    {
        TDU_Log("CYS world: transfer without a pending local change; using GCConnect scene");
        g_transferTarget = false;
    }
    CGameProcedure::s_pProcEnter->SetEnterType(ENTER_TYPE_FROM_OTHER);
    // The worker already reconnects; the procedure only waits for GCConnect.
    CGameProcedure::s_pEventSystem->PushEvent(GE_ON_SERVER_TRANS);
}

void CysModernHandlers::OnSessionClosed(bool quitComplete, bool returnToLogin)
{
    CysPvpNet::Reset();
    CysInspectNet::Reset();
    g_equipmentTooltip.Clear();
    CysEquipmentSkills::Reset();
    g_selfEquipData.Clear();
    if (returnToLogin)
    {
        // CYS-RELOGIN: a clean quit asked by ReturnServerLogin; no NET_CLOSE box, back to the account screen.
        CGameProcedure::s_pEventSystem->PushEvent(GE_PLAYER_LEAVE_WORLD, "");
        CGameProcedure::s_pProcLogIn->ReturnFromWorld();
        return;
    }
    TDU_Log("CYS world: GE_NET_CLOSE pushed quit_complete=%d", quitComplete ? 1 : 0);
    if (quitComplete)
        CGameProcedure::s_pEventSystem->PushEvent(GE_NET_CLOSE, "The game session has ended.");
    else
        CGameProcedure::s_pEventSystem->PushEvent(GE_NET_CLOSE, "Disconnected from the test server.");
}

bool CysModernHandlers::Dispatch(const pg::Packet& packet, uint32_t generation, bool& procedureChanged)
{
    if(CysRideNet::Dispatch(packet)) { procedureChanged=false;return true; }
    CysPvpNet::Observe(packet);
    procedureChanged = false;
    switch (packet.packetId())
    {
    case pg::KeyExchange::PACKET_ID:
        return true; // The client ignores it (annotation from msgagent.lua).
    case pg::GCConnect::PACKET_ID:
        OnGCConnect(static_cast<const pg::GCConnect&>(packet), generation);
        procedureChanged = true;
        return true;
    case pg::GCEnterScene::PACKET_ID:
        OnGCEnterScene(static_cast<const pg::GCEnterScene&>(packet));
        procedureChanged = true;
        return true;
    case pg::GCNewPlayer::PACKET_ID: OnGCNewPlayer(static_cast<const pg::GCNewPlayer&>(packet)); return true;
    case pg::GCNewPlayer_Move::PACKET_ID: OnGCNewPlayerMove(static_cast<const pg::GCNewPlayer_Move&>(packet)); return true;
    case pg::GCNewMonster::PACKET_ID: OnGCNewMonster(static_cast<const pg::GCNewMonster&>(packet)); return true;
    case pg::GCNewMonster_Move::PACKET_ID: OnGCNewMonsterMove(static_cast<const pg::GCNewMonster_Move&>(packet)); return true;
    case pg::GCDelObject::PACKET_ID: OnGCDelObject(static_cast<const pg::GCDelObject&>(packet)); return true;
    case pg::GCCharMove::PACKET_ID: OnGCCharMove(static_cast<const pg::GCCharMove&>(packet)); return true;
    case pg::GCCharMoveResult::PACKET_ID: OnGCCharMoveResult(static_cast<const pg::GCCharMoveResult&>(packet)); return true;
    case pg::GCCharBaseAttrib::PACKET_ID: OnGCCharBaseAttrib(static_cast<const pg::GCCharBaseAttrib&>(packet)); return true;
    case pg::GCDetailAttrib::PACKET_ID: OnGCDetailAttrib(static_cast<const pg::GCDetailAttrib&>(packet)); return true;
    case pg::GCNotifyChangeScene::PACKET_ID: OnGCNotifyChangeScene(static_cast<const pg::GCNotifyChangeScene&>(packet)); return true;
    case pg::GCRetChangeScene::PACKET_ID:
        // A pumped 280 is a refusal (result != 1). As 0.85 CSR_ERROR: stay in the scene.
        CGameProcedure::s_pGfxSystem->PushDebugString("ChangeScene Denied");
        TDU_Log("CYS world: scene change refused by the server");
        return true;
    case pg::GCRetAskQuit::PACKET_ID:
        TDU_Log("CYS world: GCRetAskQuit flag=%u; waiting for the server close",
                static_cast<unsigned>(static_cast<const pg::GCRetAskQuit&>(packet).flag));
        return true;
    // CYS-CORE
    case pg::GCChat::PACKET_ID: OnGCChat(static_cast<const pg::GCChat&>(packet)); return true;
    case pg::GCCharSkill_Send::PACKET_ID: {
        const auto& p = static_cast<const pg::GCCharSkill_Send&>(packet);
        OnSkillAction(p.m_objID, OC_MAGIC_SEND, p.logic_count, p.skill_id, p.target_id, p.target_pos_x, p.target_pos_y, 0.0f, 0, "GCCharSkill_Send");
        return true;
    }
    case pg::GCCharSkill_Gather::PACKET_ID: {
        const auto& p = static_cast<const pg::GCCharSkill_Gather&>(packet);
        OnSkillAction(p.m_objID, OC_MAGIC_CHARGE, p.logic_count, p.skill_id, p.target_id, p.target_pos_x, p.target_pos_y, p.dir, p.total_time, "GCCharSkill_Gather");
        return true;
    }
    case pg::GCCharSkill_Lead::PACKET_ID: {
        const auto& p = static_cast<const pg::GCCharSkill_Lead&>(packet);
        OnSkillAction(p.m_objID, OC_MAGIC_CHANNEL, p.logic_count, p.skill_id, (int32_t)p.target_id, p.tar_pos_x, p.tar_pos_y, p.dir, p.total_time, "GCCharSkill_Lead");
        return true;
    }
    case pg::GCCharSkill_Missed::PACKET_ID: OnGCCharSkillMissed(static_cast<const pg::GCCharSkill_Missed&>(packet)); return true;
    case pg::GCCharDirectImpact::PACKET_ID: OnGCCharDirectImpact(static_cast<const pg::GCCharDirectImpact&>(packet)); return true;
    case pg::GCCharImpactListUpdate::PACKET_ID: OnGCCharImpactListUpdate(static_cast<const pg::GCCharImpactListUpdate&>(packet)); return true; // CYS-SFX
    case pg::GCTargetListAndHitFlags::PACKET_ID: OnGCTargetListAndHitFlags(static_cast<const pg::GCTargetListAndHitFlags&>(packet)); return true;
    case pg::GCDetailHealsAndDamages::PACKET_ID: OnGCDetailHealsAndDamages(static_cast<const pg::GCDetailHealsAndDamages&>(packet)); return true;
    case pg::GCPlayerDie::PACKET_ID: OnGCPlayerDie(static_cast<const pg::GCPlayerDie&>(packet)); return true;
    case pg::GCPlayerRelive::PACKET_ID: OnGCPlayerRelive(); return true;
    case pg::GCDetailBuff::PACKET_ID: OnGCDetailBuff(static_cast<const pg::GCDetailBuff&>(packet)); return true; // CYS-REVIVE
    case pg::GCDetailImpactListUpdate::PACKET_ID: OnGCDetailImpactListUpdate(static_cast<const pg::GCDetailImpactListUpdate&>(packet)); return true; // CYS-REVIVE
    case pg::GCNewItemBox::PACKET_ID: OnGCNewItemBox(static_cast<const pg::GCNewItemBox&>(packet)); return true;
    case pg::GCBoxItemList::PACKET_ID: OnGCBoxItemList(static_cast<const pg::GCBoxItemList&>(packet)); return true;
    case pg::GCPickResult::PACKET_ID: OnGCPickResult(static_cast<const pg::GCPickResult&>(packet)); return true;
    case pg::GCItemInfo::PACKET_ID: OnGCItemInfo(static_cast<const pg::GCItemInfo&>(packet)); return true;
    case pg::GCNotifyEquip::PACKET_ID: OnGCNotifyEquip(static_cast<const pg::GCNotifyEquip&>(packet)); return true;
    case pg::GCMyBagList::PACKET_ID: OnGCMyBagList(static_cast<const pg::GCMyBagList&>(packet)); return true;
    case pg::GCDetailSkillList::PACKET_ID: OnGCDetailSkillList(static_cast<const pg::GCDetailSkillList&>(packet)); return true;
    case pg::GCLevelUp::PACKET_ID: OnGCLevelUp(static_cast<const pg::GCLevelUp&>(packet)); return true;
    case pg::GCDetailExp::PACKET_ID: OnGCDetailExp(static_cast<const pg::GCDetailExp&>(packet)); return true; // CYS-MSGEMPTY
    case pg::GCOperateResult::PACKET_ID: OnGCOperateResult(static_cast<const pg::GCOperateResult&>(packet)); return true;
    case pg::GCWorldTime::PACKET_ID: { // CYS-SKY: CY 3.71 GCWorldTime handler FUN_1405420a0 (world time of the sky)
        const auto& p = static_cast<const pg::GCWorldTime&>(packet);
        CysWorldTime::OnPacket(p.m_Time, static_cast<int>(p.counter), p.unknow);
        return true;
    }
    default:
        if (CysPvpNet::Dispatch(packet)) return true;
        if (CysWuhunNet::Dispatch(packet)) return true;
        if (CysDualSouls::Dispatch(packet)) return true;
        if (CysPlayerShopNet::Dispatch(packet)) return true;
        if (CysPetNet::Dispatch(packet)) return true; // CYS-PET (GCDetailAttrib_Pet 512, GCNewPet 1216, GCRemovePet 52, GCManipulatePetRet 666)
        if (CysQuestNet::Dispatch(packet)) return true; // CYS-QUEST (GCScriptCommand 0 falls through to CYS-INV)
        return CysGuildWarNet::Dispatch(packet) || CysAllianceNet::Dispatch(packet) || CysGuildNet::Dispatch(packet) || CysPartyNet::Dispatch(packet) || CysRaidNet::Dispatch(packet) || CysCommunityNet::Dispatch(packet) || CysEquipCraft::Dispatch(packet) || CysPetSoul::Dispatch(packet) || CysShenDing::Dispatch(packet) || CysInspectNet::Dispatch(packet) || CysEquipmentSkills::Dispatch(packet) || CysInventoryNet::Dispatch(packet) || CysSkillNet::Dispatch(packet)
            || CysPowerNet::Dispatch(packet) // CYS-INV, CYS-SKILL, CYS-POWER (GCDetailXiulianList)
            || CysRideNet::Dispatch(packet) || CysLeapNet::Dispatch(packet); // CYS-RIDE (GCCharAutoRide), CYS-MOVE2 (GCCharFly)
    }
}

unsigned int CysModernHandlers::InvalidPositions() { return g_invalidPositions; }
unsigned int CysModernHandlers::IgnoredSelfDeletes() { return g_selfDeletes; }

unsigned short CysModernHandlers::ItemGuidMask(unsigned char world, unsigned char server, unsigned int serial)
{
    std::map<uint64_t, uint16_t>::const_iterator found = g_itemGuidMask.find(GuidKey(world, server, serial));
    return found == g_itemGuidMask.end() ? 0 : found->second;
}

CysModernHandlers::CoreCounters CysModernHandlers::Core() { return g_core; }

void CysModernHandlers::ItemInfoToLegacy(const pg::ItemInfo_bos& in, _ITEM& out) { ToLegacyItem(in, out); } // CYS-INV

bool CysModernHandlers::EquipmentTooltip(unsigned char world, unsigned char server, unsigned int serial,
    int itemIndex, CysEquipmentTooltip::Snapshot& out)
{
    return g_equipmentTooltip.Find(world, server, serial, itemIndex, out);
}

#else // !CYS_HAVE_NETCORE
bool CysSelfEquipData::Query(Key, uint32_t&) { return false; }

bool CysModernHandlers::WorldReady() { return false; }
bool CysModernHandlers::IsWorldPacket(uint16_t) { return false; }
void CysModernHandlers::OnConnected(uint32_t) {}
void CysModernHandlers::OnTransfer(const pg::GCRetChangeScene&) {}
void CysModernHandlers::OnSessionClosed(bool, bool) {}
bool CysModernHandlers::Dispatch(const pg::Packet&, uint32_t, bool& procedureChanged) { procedureChanged = false; return false; }
unsigned int CysModernHandlers::InvalidPositions() { return 0; }
unsigned int CysModernHandlers::IgnoredSelfDeletes() { return 0; }
unsigned short CysModernHandlers::ItemGuidMask(unsigned char, unsigned char, unsigned int) { return 0; }
CysModernHandlers::CoreCounters CysModernHandlers::Core() { CoreCounters c = {}; return c; }
void CysModernHandlers::ItemInfoToLegacy(const pg::ItemInfo_bos&, _ITEM&) {}
bool CysModernHandlers::EquipmentTooltip(unsigned char, unsigned char, unsigned int, int, CysEquipmentTooltip::Snapshot&) { return false; }

#endif
