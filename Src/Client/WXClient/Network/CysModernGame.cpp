#include "StdAfx.h"
#include "CysEquipCraft.h"
#include "CysShenDing.h"
#include "CysPetSoul.h"
#include "CysModernGame.h"
#include "CysWuhunNet.h"
#include "CysDualSouls.h"
#include "CysTooltipCompare.h"
#include "../../LuaApiGen/CysHost_EquipmentSkills.h"

#if defined(CYS_HAVE_NETCORE)
#include "CysModernHandlers.h"
#include "CysWorldReplay.h" // CYS: offline replay stream (only when CYS_WORLD_REPLAY is set).
#include "GameClient.h"
#include "WorldModel.h"
#include "PgPackets_combat.h"
#include "PgPackets_core.h"
#include "PgPackets_misc.h"
#include "PgPackets_mission.h"
#include "PgPackets_movement.h"
#include "..\Procedure\GameProcedure.h"
#include "..\Procedure\GamePro_Main.h"
#include "..\World\WorldManager.h"
#include "TDUtil.h"
#include "CGCharMove.h"
#include "CGEnterScene.h"
#include "CGAskChangeScene.h"
#include "CGCharAskBaseAttrib.h"
#include "CGAskDetailAttrib.h"
#include "CGCharAskEquipment.h"
#include "CGAskMissionList.h"
#include "CGAskSetting.h"
// CYS-CORE: core-loop commands translated from the 0.85 packet classes
#include "PgPackets_item.h"
#include "PgPackets_social.h"
#include "CGLockTarget.h"
#include "CGCharUseSkill.h"
#include "CGChat.h"
#include "CGExecuteScript.h"
#include "CysHost_NbShop.h"
#include "CGOpenItemBox.h"
#include "CGPickBoxItem.h"
#include "CGAskMyBagList.h"
#include "CGAskDetailSkillList.h"
#include "CGPlayerDieResult.h"
#include "CGDispelBuffReq.h" // CYS-REVIVE: right-click a buff icon to cancel it (PlayerImpactFrame MouseRClick)
#include "CysInventoryNet.h"
#include "CysForge.h"
#include "CysTemBank.h"
#include "CysInspectNet.h" // CYS-INV: bag / equipment / NPC shop
#include "CysQuestNet.h" // CYS-QUEST: NPC dialogue / missions
#include "CysCommunityNet.h"
#include "CysPartyNet.h"
#include "CysPlayerShopNet.h"
#include "CysRaidNet.h"
#include "CysAllianceNet.h"
#include "CysGuildWarNet.h"
#include "CysGuildNet.h"
#include "../Interface/CysSettings.h"
#include "../Script/GMScriptSystem.h"
#include "CysSkillNet.h" // CYS-SKILL: skills / xinfa / quick bar / cooldowns
#include "CysRideNet.h" // CYS-RIDE: mounts (CGCharAutoRide / GCCharAutoRide)
#include "CysPowerNet.h" // CYS-POWER: cultivation (XiuLian)
#include "CysPetNet.h" // CYS-PET: pet (珍兽) host functions of the CY Pet* windows
#include "CysPvpNet.h"
#include "CysHost_Ride.h" // CYS-RIDE: mount host functions (LuaApiGen)
#include "CysHost_Host2.h" // CYS-HOST2: item names / wuhun page / default mouse (LuaApiGen)
#include "CysHost_MeshWin.h" // CYS-MESHWIN: DataPool:KFS_UpdateKFSModel (wuhun avatar model) + MeshWindow live probe
#include "CysHost_Wardrobe.h" // CYS-WARD2: fashion depot + fitting set of the CY wardrobe (NewExterior_*)
#include "CysHost_ActionItem.h" // CYS-UIFRAMES: DataPool:CreateActionItemForShow (item icons, 419 frames)
#include <algorithm> // CYS-NPCINT: ObjectsNear (std::sort)
#include <cmath>     // CYS-NPCINT: ObjectsNear (std::sqrt)
#include <deque>
#include <map>
#include <memory>
#include <utility>   // CYS-NPCINT: ObjectsNear (std::pair)
#include <vector>

namespace
{
    using cys::net::GameClient;
    using cys::net::GameEvent;
    using cys::net::GameEventType;
    using cys::net::GamePhase;

    const size_t kMaxStash = 4096;
    const DWORD kStatsIntervalMs = 10000;

    struct Session
    {
        std::unique_ptr<GameClient> client;
        cys::net::WorldModel world;
        std::deque<GameEvent> stash;          // world packets waiting for a loaded scene
        std::map<unsigned, unsigned> dropped;  // legacy packet id -> count (not translated or refused)
        std::map<unsigned, unsigned> unhandled; // modern packet id -> count (no ported handler yet)
        unsigned stashDrops = 0, handlerErrors = 0, modelNotes = 0;
        bool stopBatch = false, terminal = false, shuttingDown = false;
        bool returnToLogin = false; // CYS-RELOGIN: armed by RequestReturnToLogin, read once by the terminal
        DWORD lastStats = 0;
        unsigned short approvedPort = 0;
    };
    Session& S() { static Session session; return session; }

    const char* TerminalName(GameEventType type)
    {
        switch (type)
        {
        case GameEventType::QuitComplete: return "quit_complete";
        case GameEventType::Error: return "error";
        case GameEventType::Cancelled: return "cancelled";
        default: return "other";
        }
    }

    size_t Total(const std::map<unsigned, unsigned>& counts)
    {
        size_t total = 0;
        for (const auto& entry : counts) total += entry.second;
        return total;
    }

    void LogStats(const char* label)
    {
        Session& s = S();
        const auto& st = s.world.stats();
        TDU_Log("CYS world: %s phase=%s scene=%u my=%u objects=%u created=%u deleted=%u moves=%u self_moves=%u "
                "base_attribs=%u stash=%u unhandled=%u/%u dropped=%u/%u handler_errors=%u invalid_pos=%u",
                label, s.client ? cys::net::GamePhaseName(s.client->phase()) : "none", s.world.sceneId(),
                s.world.myObjectId(), static_cast<unsigned>(s.world.objects().size()), st.created, st.deleted,
                st.moves, st.selfMoves, st.baseAttribs, static_cast<unsigned>(s.stash.size()),
                static_cast<unsigned>(s.unhandled.size()), static_cast<unsigned>(Total(s.unhandled)),
                static_cast<unsigned>(s.dropped.size()), static_cast<unsigned>(Total(s.dropped)),
                s.handlerErrors, CysModernHandlers::InvalidPositions());
    }

    void Dispatch(GameEvent&& event, bool fromStash)
    {
        Session& s = S();
        switch (event.type)
        {
        case GameEventType::Connected:
            s.world.onConnected(event.generation);
            TDU_Log("CYS world: game socket %u connected; CGConnect sent first next_idx=%04x",
                    event.generation, event.counters.nextTxIndex);
            CysModernHandlers::OnConnected(event.generation);
            break;
        case GameEventType::Transfer:
            s.world.onTransfer();
            s.stash.clear(); // Stale objects of the scene that is being left.
            TDU_Log("CYS world: scene transfer accepted; reconnecting game socket %u", event.generation + 1);
            if (event.packet)
                CysModernHandlers::OnTransfer(static_cast<const pg::GCRetChangeScene&>(*event.packet));
            s.stopBatch = true;
            break;
        case GameEventType::Packet:
        {
            if (!event.packet) break;
            const uint16_t id = event.packetId;
            if (!fromStash)
            {
                const char* reason = "";
                if (!s.world.apply(*event.packet, &reason) && s.modelNotes++ < 16)
                    TDU_Log("CYS world: model note id=%u %s", id, reason);
            }
            if (CysModernHandlers::IsWorldPacket(id) && !CysModernHandlers::WorldReady())
            {
                if (s.stash.size() >= kMaxStash) { s.stash.pop_front(); ++s.stashDrops; }
                s.stash.push_back(std::move(event));
                return;
            }
            bool handled = false, procedureChanged = false;
            LARGE_INTEGER cysPerfT0; ::QueryPerformanceCounter(&cysPerfT0); // CYS-PERF: slow handler log (CYS_PERF=1)
            try
            {
                handled = CysModernHandlers::Dispatch(*event.packet, event.generation, procedureChanged);
            }
            catch (...)
            {
                // Legacy TDThrow paths are validated before the call; keep the session alive.
                if (s.handlerErrors++ < 16) TDU_Log("CYS world: handler exception id=%u", id);
            }
            {
                static int s_cysPerfOn = -1; // CYS-PERF
                if (s_cysPerfOn < 0) { char v[4] = {0}; s_cysPerfOn = ::GetEnvironmentVariableA("CYS_PERF", v, sizeof(v)) ? 1 : 0; }
                if (s_cysPerfOn)
                {
                    LARGE_INTEGER t1, f; ::QueryPerformanceCounter(&t1); ::QueryPerformanceFrequency(&f);
                    const double ms = double(t1.QuadPart - cysPerfT0.QuadPart) * 1000.0 / double(f.QuadPart);
                    if (ms >= 15.0) TDU_Log("CYS-PERF slow packet id=%u %.1f ms stash=%d", id, ms, fromStash ? 1 : 0);
                }
            }
            if (!handled && s.unhandled[id]++ == 0 && s.unhandled.size() <= 64)
                TDU_Log("CYS world: first unhandled GC id=%u", id);
            if (procedureChanged) s.stopBatch = true;
            break;
        }
        default:
            s.terminal = true;
            s.stopBatch = true;
            TDU_Log("CYS world: terminal=%s code=%s phase=%s quit_flag=%d generation=%u tx=%u rx=%u heartbeats=%u "
                    "transfers=%u dropped_outbound=%u decode_failures=%u packet_id=%u field=%s offset=%u message=%s",
                    TerminalName(event.type), cys::net::ErrorCodeName(event.error), cys::net::GamePhaseName(event.phase),
                    event.quitFlag, event.generation, event.counters.txFrames, event.counters.rxFrames,
                    event.counters.heartbeats, event.counters.transfers, event.counters.droppedOutbound,
                    event.counters.decodeFailures, event.packetId, event.decodeField,
                    static_cast<unsigned>(event.errorOffset), event.message);
            LogStats("session summary");
            {
                const bool relogin = s.returnToLogin && event.type == GameEventType::QuitComplete;
                s.returnToLogin = false;
                if (!s.shuttingDown) CysModernHandlers::OnSessionClosed(event.type == GameEventType::QuitComplete, relogin);
            }
            break;
        }
    }

    std::unique_ptr<pg::Packet> Translate(Packet* legacy)
    {
        switch (legacy->GetPacketID())
        {
        case PACKET_CG_CHARMOVE:
        {
            const CGCharMove* move = static_cast<const CGCharMove*>(legacy);
            std::unique_ptr<pg::CGCharMove> out(new pg::CGCharMove);
            out->handleID = static_cast<uint32_t>(move->getHandleID());
            out->m_objID = static_cast<uint32_t>(move->getObjID());
            out->posWorld_x = move->getWorldPos().m_fX;
            out->posWorld_y = move->getWorldPos().m_fZ;
            for (BYTE i = 0; i < move->getNumTargetPos() && i < 16; ++i)
            {
                pg::CGCharMove::E_targetPos node;
                node.x = move->getTargetPos()[i].m_fX;
                node.y = move->getTargetPos()[i].m_fZ;
                out->targetPos.push_back(node);
            }
            if (out->targetPos.empty()) return nullptr;
            out->numTargetPos = static_cast<uint8_t>(out->targetPos.size());
            out->unknow = 0;
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_ENTERSCENE:
        {
            CGEnterScene* enter = static_cast<CGEnterScene*>(legacy);
            std::unique_ptr<pg::CGEnterScene> out(new pg::CGEnterScene);
            out->m_nSceneID = static_cast<uint16_t>(enter->getSceneID());
            out->m_byEnterType = enter->getEnterType();
            out->m_posWorld_x = enter->getEnterPos()->m_fX;
            out->m_posWorld_y = enter->getEnterPos()->m_fZ;
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_ASKCHANGESCENE:
        {
            CGAskChangeScene* ask = static_cast<CGAskChangeScene*>(legacy);
            std::unique_ptr<pg::CGAskChangeScene> out(new pg::CGAskChangeScene);
            out->scene_from = static_cast<uint16_t>(ask->GetSourSceneID());
            out->scene_to = static_cast<uint16_t>(ask->GetDestSceneID());
            out->unknow_1 = 0; out->unknow_2 = 0;
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_CHARASKBASEATTRIB:
        {
            std::unique_ptr<pg::CGCharAskBaseAttrib> out(new pg::CGCharAskBaseAttrib);
            out->m_objID = static_cast<uint32_t>(static_cast<CGCharAskBaseAttrib*>(legacy)->getTargetID());
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_ASKDETAILATTRIB:
        {
            std::unique_ptr<pg::CGAskDetailAttrib> out(new pg::CGAskDetailAttrib);
            out->m_objID = static_cast<uint32_t>(static_cast<CGAskDetailAttrib*>(legacy)->getTargetID());
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_CHARASKEQUIPMENT:
        {
            std::unique_ptr<pg::CGCharAskEquipment> out(new pg::CGCharAskEquipment);
            out->m_objID = static_cast<uint32_t>(static_cast<CGCharAskEquipment*>(legacy)->getObjID());
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_ASKMISSIONLIST:
        {
            std::unique_ptr<pg::CGAskMissionList> out(new pg::CGAskMissionList);
            out->m_objID = static_cast<uint32_t>(static_cast<CGAskMissionList*>(legacy)->getTargetID());
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_ASKSETTING:
            return std::unique_ptr<pg::Packet>(new pg::CGAskSetting);
        // CYS-CORE ------------------------------------------------------------------------------------
        case PACKET_CG_LOCK_TARGET:
        {
            std::unique_ptr<pg::CGLockTarget> out(new pg::CGLockTarget);
            out->target_id = static_cast<int32_t>(static_cast<CGLockTarget*>(legacy)->getTargetID());
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_CHARUSESKILL:
        {
            // misc.lua CGCharUseSkill.bis: dir, skillID, target, m_objID, targetObjID, posTarget (the server reads
            // skillID/targetObjID/posTarget/dir; 0.85 carried the target GUID in the third field).
            const Packets::CGCharUseSkill* use = static_cast<const Packets::CGCharUseSkill*>(legacy);
            std::unique_ptr<pg::CGCharUseSkill> out(new pg::CGCharUseSkill);
            out->dir = use->getDir();
            out->skillID = CysSkillNet::TemplateOfSkillData(static_cast<int32_t>(use->getSkillDataID())); // CYS-SKILL: template id
            out->target = static_cast<int32_t>(use->getTargetID());
            out->m_objID = static_cast<uint32_t>(use->getObjID());
            out->targetObjID = static_cast<int32_t>(use->getTargetID());
            out->posTarget_x = use->getTargetPos().m_fX;
            out->posTarget_y = use->getTargetPos().m_fZ;
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_EXECUTESCRIPT:
        {
            if (CysHostNbShop::Off('x')) return nullptr;
            X_SCRIPT* script = static_cast<Packets::CGExecuteScript*>(legacy)->GetScript();
            const int size = script->GetFunNameSize();
            const unsigned count = script->GetParamCount();
            if (size <= 0 || size > MAX_FUNC_NAME_SIZE || count > MAX_INT_PARAM_COUNT) return nullptr;
            std::unique_ptr<pg::CGExecuteScript> out(new pg::CGExecuteScript);
            out->m_ScriptID = static_cast<uint32_t>(script->GetScriptID());
            out->set_m_szFunName(std::string(script->GetFunName(), size));
            out->m_uParamCount = static_cast<uint8_t>(count);
            for (unsigned i = 0; i < count; ++i) out->m_aParam.push_back(script->GetParam((BYTE)i));
            if (out->m_ScriptID == 809262 && getenv("CYS_FORGE_LIVE"))
                TDU_Log("CYS-FORGE: send function=%s count=%u args=%d,%d,%d",out->m_szFunName.c_str(),count,script->GetParam(0),script->GetParam(1),script->GetParam(2));
            // Only mall gestures are logged; general script execution is a hot path.
            if (out->m_ScriptID == 888902)
                TDU_Log("CYS-NBSHOP: CGExecuteScript id=%u function=%s count=%u", out->m_ScriptID, out->m_szFunName.c_str(), count);
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_CHAT:
        {
            Packets::CGChat* chat = static_cast<Packets::CGChat*>(legacy);
            std::unique_ptr<pg::CGChat> out(new pg::CGChat);
            out->ChatType = chat->GetChatType();
            out->set_Contex(std::string(chat->GetContex(), chat->GetContexSize()));
            out->unknow_2 = 0; out->unknow_8 = 0; out->unknow_9 = 0; out->unknow_10 = 0;
            out->teamID = static_cast<uint16_t>(chat->GetTeamID());
            if (chat->GetChatType() == CHAT_TYPE_TELL) out->set_TargetName(std::string(chat->GetTargetName(), chat->GetTargetSize()));
            out->uts.fill(0);
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_OPENITEMBOX:
        {
            std::unique_ptr<pg::CGOpenItemBox> out(new pg::CGOpenItemBox);
            out->m_objID = static_cast<uint32_t>(static_cast<Packets::CGOpenItemBox*>(legacy)->getObjID());
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_PICKBOXITEM:
        {
            Packets::CGPickBoxItem* pick = static_cast<Packets::CGPickBoxItem*>(legacy);
            const _ITEM_GUID guid = pick->getItemId();
            std::unique_ptr<pg::CGPickBoxItem> out(new pg::CGPickBoxItem);
            out->m_objID = static_cast<uint32_t>(pick->getItemBoxId());
            out->item_guid.world = guid.m_World;
            out->item_guid.server = guid.m_Server;
            out->item_guid.series = static_cast<uint32_t>(guid.m_Serial);
            out->item_guid.mask = CysModernHandlers::ItemGuidMask(guid.m_World, guid.m_Server, static_cast<unsigned>(guid.m_Serial));
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_ASKMYBAGLIST:
        {
            Packets::CGAskMyBagList* ask = static_cast<Packets::CGAskMyBagList*>(legacy);
            std::unique_ptr<pg::CGAskMyBagList> out(new pg::CGAskMyBagList);
            out->unknow_1 = static_cast<uint32_t>(ask->GetAskMode());
            for (BYTE i = 0; i < ask->GetAskCount(); ++i) out->list.push_back(ask->GetAskItemIndex(i));
            out->size = static_cast<uint8_t>(out->list.size());
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_ASKDETAILSKILLLIST:
        {
            std::unique_ptr<pg::CGAskDetailSkillList> out(new pg::CGAskDetailSkillList);
            out->m_objID = static_cast<uint32_t>(static_cast<CGAskDetailSkillList*>(legacy)->getTargetID());
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_PLAYER_DIE_RESULT:
        {
            std::unique_ptr<pg::CGPlayerDieResult> out(new pg::CGPlayerDieResult);
            out->code = static_cast<uint32_t>(static_cast<Packets::CGPlayerDieResult*>(legacy)->GetResultCode());
            return std::unique_ptr<pg::Packet>(out.release());
        }
        case PACKET_CG_DISPEL_BUFF:
        {
            // CYS-REVIVE 20/09/2026: right-clicking a buff icon (PlayerImpactFrame.layout.xml MouseRClick ->
            // PlayerImpactFrame_ImageN_Click -> Player:DispelBuffByIndex -> CDataPool::BuffImpact_DispelByIndex)
            // built a 0.85 CGDispelBuffReq (651) that had no modern mapping, so CysModernGame::Send dropped it
            // ("legacy packet 651 has no modern mapping; not sent"). That was the ONLY way out of an impact with
            // 可以被手动取消=1 whose own flags block both automatic exits (59901 地府束缚: CanAction1/2 = CanMove = 0,
            // infinite, survives logout). The server keys the removal on the SN alone
            // (msgagent.lua:1315 CGDispelBuffReq -> scenecore.lua:3616 char_diepel_buff ->
            // unregister_impact_by_sn(req.sn)), and BuffImpact_DispelByIndex only ever calls SetSN, so the two
            // id fields are sent as 0 instead of the uninitialised members of the 0.85 class.
            std::unique_ptr<pg::CGDispelBuffReq> out(new pg::CGDispelBuffReq);
            out->skill_id = 0;
            out->buffer_id = 0;
            out->sn = static_cast<uint32_t>(static_cast<Packets::CGDispelBuffReq*>(legacy)->GetSN());
            return std::unique_ptr<pg::Packet>(out.release());
        }
        default:
            // CGConnect, heartbeat and CGAskQuit belong to the worker; other 0.85
            // layouts have no verified modern mapping yet and are never sent raw.
            if (std::unique_ptr<pg::Packet> pet = CysPetNet::Translate(legacy)) return pet; // CYS-PET (CGManipulatePet 435: Pet:Go_Fight / Go_Relax / Go_Free)
            if (std::unique_ptr<pg::Packet> skill = CysSkillNet::Translate(legacy)) return skill; // CYS-SKILL
            if (auto guild = CysGuildNet::Translate(legacy)) return guild;
            if (auto party = CysPartyNet::Translate(legacy)) return party;
            if (auto shop = CysPlayerShopNet::Translate(legacy)) return shop;
            if (auto community = CysCommunityNet::Translate(legacy)) return community;
            if (std::unique_ptr<pg::Packet> inv = CysInventoryNet::Translate(legacy)) return inv; // CYS-INV
            return CysQuestNet::Translate(legacy); // CYS-QUEST (nullptr for every other packet)
        }
    }
}

bool CysModernGame::Start(cys::net::GameTicket&& ticket, unsigned short approvedGamePort,
                          const std::vector<std::string>& allowedHosts)
{
    Session& s = S();
    if (s.client) return false;
    s.world.reset(); s.stash.clear(); s.dropped.clear(); s.unhandled.clear();
    s.stashDrops = s.handlerErrors = s.modelNotes = 0;
    s.stopBatch = s.terminal = s.shuttingDown = s.returnToLogin = false;
    s.lastStats = GetTickCount();
    if (!approvedGamePort) approvedGamePort = ticket.endpoint().port; // CYS: profile game_port=reply
    s.approvedPort = approvedGamePort;
    const unsigned guid = ticket.guid();
    const unsigned nextIndex = ticket.nextTxIndex();
    cys::net::GameOptions options;
    options.approvedGamePort = approvedGamePort;
    options.allowedHosts = allowedHosts; // CYS: server profile allow-list
    // CYS: offline replay replaces the game socket by the recorded session; otherwise Winsock.
    if (CysWorldReplay::Active()) options.streamFactory = CysWorldReplay::GameFactory();
    s.client.reset(new GameClient);
    if (!s.client->start(std::move(ticket), options))
    {
        TDU_Log("CYS world: game session rejected code=%s", cys::net::ErrorCodeName(s.client->lastStartError()));
        s.client.reset();
        return false;
    }
    TDU_Log("CYS world: game session started guid=%u next_idx=%04x port=%u", guid, nextIndex, approvedGamePort);
    CysWardrobeCatalog::Reset();
    CysSkillNet::Reset();
    CysGuildNet::Reset();
    CysPartyNet::Reset();
    CysRaidNet::Reset();
    CysAllianceNet::Reset();
    CysGuildWarNet::Reset();
    CysCommunityNet::Reset();
    CysForge::RegisterHost();
    CysTemBank::RegisterHost();
    CysShenDing::RegisterHost();
    CysPetSoul::RegisterHost();
    CysEquipCraft::RegisterHost();
    CysInspectNet::RegisterHost();
    CysInventoryNet::RegisterHost(); // CYS-INV: bag/equip/shop host functions of the CY windows
    CysTooltipCompare::RegisterHost();
    CysHostNbShop::RegisterHost(); // overrides the placeholder NpcShop methods installed by inventory
    CysQuestNet::RegisterHost(); // CYS-QUEST: NPC dialogue / mission tracking host functions
    CysCommunityNet::RegisterHost();
    CysPartyNet::RegisterHost();
    CysPlayerShopNet::RegisterHost();
    CysRaidNet::RegisterHost();
    CysAllianceNet::RegisterHost();
    CysGuildWarNet::RegisterHost();
    CysGuildNet::RegisterHost();
    CysSettings::Register(CScriptSystem::GetMe()->GetLuaState());
    CysSkillNet::RegisterHost(); // CYS-SKILL: skill book / xinfa host functions of the CY windows
    CysPowerNet::RegisterHost(); // CYS-POWER: cultivation host functions of Xiulian / XiulianStudy
    CysPetNet::RegisterHost(); // CYS-PET: pet host functions of the CY Pet / PetList / PetBank windows
    CysPvpNet::RegisterHost();
    CysHostRide::RegisterHost(); // CYS-RIDE: mount host functions of the CY Ride window
    CysHostHost2::RegisterHost(); // CYS-HOST2: Lua_GetItemNameByIndex / ToggleWuhunPage / SetDefaultMouse
    CysEquipmentSkills::RegisterHost();
    CysWuhunNet::RegisterHost();
    CysDualSouls::RegisterHost();
    CysMeshWinHost::RegisterHost(); // CYS-MESHWIN: DataPool:KFS_UpdateKFSModel (model of the My_Wuhun avatar)
    CysWardrobeHost::RegisterHost(); // CYS-WARD2: fashion depot + fitting set of the CY wardrobe (NewExterior_*)
    CysHostActionItem::RegisterHost(); // CYS-UIFRAMES: item-icon actions for shops/rewards/wardrobe (419 frames)
    return true;
}

bool CysModernGame::IsActive() { return S().client != nullptr; }

bool CysModernGame::IsInWorld()
{
    Session& s = S();
    return s.client && s.client->phase() == GamePhase::InWorld;
}

void CysModernGame::Pump()
{
    Session& s = S();
    if (!s.client) return;
    CysRideNet::Tick(); // CYS-RIDE: expire a CGCharAutoRide the server answered on one of its silent paths
    CysSkillNet::TickAssist();
    s.stopBatch = false;
    size_t replayed = 0;
    while (!s.stash.empty() && !s.stopBatch && replayed < 512 && CysModernHandlers::WorldReady())
    {
        GameEvent event = std::move(s.stash.front());
        s.stash.pop_front();
        Dispatch(std::move(event), true);
        ++replayed;
    }
    // One event per call: a procedure-changing packet ends the batch so the next
    // packet is dispatched after the switch (the legacy stream could not do this).
    for (unsigned i = 0; i < 1024 && !s.stopBatch && s.client; ++i)
        if (s.client->pump([](GameEvent&& event) { Dispatch(std::move(event), false); }, 1) == 0) break;
    const DWORD now = GetTickCount();
    if (!s.terminal && now - s.lastStats >= kStatsIntervalMs) { s.lastStats = now; LogStats("stats"); }
    if (s.terminal)
    {
        s.client.reset(); // Deferred: never inside the client's own pump callback.
        s.stash.clear();
        s.terminal = false;
    }
}

void CysModernGame::Send(Packet* legacy)
{
    Session& s = S();
    if (!s.client || !legacy) return;
    const unsigned id = legacy->GetPacketID();
    // Same rule as the 0.85 SendPacket: nothing is sent while a scene change is pending.
    if (CGameProcedure::GetActiveProcedure() == (CGameProcedure*)CGameProcedure::s_pProcMain &&
        CWorldManager::GetMe()->GetStation() == CWorldManager::WS_ASK_CHANGE_SCANE &&
        id != PACKET_CG_ASKCHANGESCENE)
    {
        ++s.dropped[id];
        return;
    }
    std::unique_ptr<pg::Packet> modern = Translate(legacy);
    if (!modern)
    {
        if (s.dropped[id]++ == 0 && s.dropped.size() <= 64)
            TDU_Log("CYS world: legacy packet %u has no modern mapping; not sent", id);
        return;
    }
    const uint16_t modernId = modern->packetId();
    if (!s.client->send(std::move(modern)))
    {
        ++s.dropped[id];
        TDU_Log("CYS world: send refused modern_id=%u code=%s phase=%s", modernId,
                cys::net::ErrorCodeName(s.client->lastCommandError()), cys::net::GamePhaseName(s.client->phase()));
    }
}

bool CysModernGame::SendModern(std::unique_ptr<pg::Packet> packet) // CYS-INV
{
    Session& s = S();
    if (!s.client || !packet) return false;
    const uint16_t modernId = packet->packetId();
    if (!s.client->send(std::move(packet)))
    {
        TDU_Log("CYS world: send refused modern_id=%u code=%s", modernId, cys::net::ErrorCodeName(s.client->lastCommandError()));
        return false;
    }
    return true;
}

void CysModernGame::LogStatsNow()
{
    if (S().client) LogStats("stats");
}

void CysModernGame::OnEnterSceneFailed()
{
    Session& s = S();
    if (!s.client) return;
    TDU_Log("CYS world: scene load failed on the client; leaving through CGAskQuit");
    if (!s.client->requestQuit()) s.client->cancel();
}

bool CysModernGame::RequestReturnToLogin()
{
    Session& s = S();
    if (!s.client || s.returnToLogin || s.client->phase() != GamePhase::InWorld || !s.client->requestQuit()) return false;
    s.returnToLogin = true;
    TDU_Log("CYS relogin: CGAskQuit sent; waiting for quit_complete");
    return true;
}

void CysModernGame::Shutdown(unsigned int timeoutMs)
{
    Session& s = S();
    if (!s.client) return;
    s.shuttingDown = true;
    if (s.client->phase() == GamePhase::InWorld && s.client->requestQuit())
        TDU_Log("CYS world: CGAskQuit on shutdown; waiting for the server close");
    else
        s.client->cancel();
    const DWORD started = GetTickCount();
    bool finished = false;
    while (!finished && GetTickCount() - started < timeoutMs)
    {
        s.client->pump([&](GameEvent&& event)
        {
            if (event.type != GameEventType::Packet && event.type != GameEventType::Connected &&
                event.type != GameEventType::Transfer)
            {
                finished = true;
                Dispatch(std::move(event), false);
            }
            // World packets during teardown are discarded: the scene is gone.
        }, 64);
        if (!finished) Sleep(10);
    }
    if (!finished)
    {
        s.client->cancel();
        TDU_Log("CYS world: quit wait expired after %u ms; socket cancelled", timeoutMs);
    }
    s.client.reset();
    s.stash.clear();
    s.terminal = false;
}

unsigned int CysModernGame::NearestMonster(float* distance) // CYS-CORE
{
    Session& s = S();
    if (!s.client) { if (distance) *distance = 0; return 0; }
    return s.world.nearestMonster(distance);
}

unsigned int CysModernGame::NearestItemBox(float radius,float* x,float* z)
{
    auto& s=S();if(!s.client)return 0;
    const auto id=s.world.nearestItemBox(radius);
    const auto it=s.world.itemBoxes().find(id);
    if(it==s.world.itemBoxes().end())return 0;
    if(x)*x=it->second.x;if(z)*z=it->second.y;
    return id;
}
unsigned int CysModernGame::CurrentSceneId(){return S().world.sceneId();}

unsigned int CysModernGame::NearestObjectAt(float x, float z, float radius, float* distance) // CYS-INV
{
    Session& s = S();
    if (!s.client) { if (distance) *distance = 0; return 0; }
    return s.world.nearestObjectAt(x, z, radius, distance);
}

// CYS-NPCINT: `NearestObjectAt` chi tra MOT id, nen mot luot do bang no khong tra loi duoc cau hoi
// "hong voi MOI NPC hay chi mot nhom". Ham nay tra MOI id trong ban kinh, sap theo khoang cach tang dan.
unsigned int CysModernGame::ObjectsNear(float x, float z, float radius, unsigned int* out, unsigned int max)
{
    Session& s = S();
    if (!s.client || !out || !max) return 0;
    std::vector<std::pair<float, unsigned int> > found;
    const std::map<uint32_t, cys::net::WorldModel::Object>& objects = s.world.objects();
    for (std::map<uint32_t, cys::net::WorldModel::Object>::const_iterator it = objects.begin(); it != objects.end(); ++it)
    {
        if (it->first == s.world.myObjectId()) continue;
        if (it->second.x == 0 && it->second.y == 0) continue;
        const float dx = it->second.x - x, dz = it->second.y - z;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > radius) continue;
        found.push_back(std::make_pair(d, static_cast<unsigned int>(it->first)));
    }
    std::sort(found.begin(), found.end());
    const unsigned int count = static_cast<unsigned int>(found.size() < max ? found.size() : max);
    for (unsigned int i = 0; i < count; ++i) out[i] = found[i].second;
    return count;
}

#else // !CYS_HAVE_NETCORE

  unsigned int CysModernGame::NearestMonster(float* distance) { if (distance) *distance = 0; return 0; }
  unsigned int CysModernGame::NearestItemBox(float,float*,float*) { return 0; }
  unsigned int CysModernGame::CurrentSceneId() { return 0; }
unsigned int CysModernGame::NearestObjectAt(float, float, float, float* distance) { if (distance) *distance = 0; return 0; }
unsigned int CysModernGame::ObjectsNear(float, float, float, unsigned int*, unsigned int) { return 0; } // CYS-NPCINT
bool CysModernGame::Start(cys::net::GameTicket&&, unsigned short) { return false; }
bool CysModernGame::IsActive() { return false; }
bool CysModernGame::IsInWorld() { return false; }
void CysModernGame::Pump() {}
void CysModernGame::Send(Packet*) {}
bool CysModernGame::SendModern(std::unique_ptr<pg::Packet>) { return false; }
void CysModernGame::LogStatsNow() {}
void CysModernGame::OnEnterSceneFailed() {}
bool CysModernGame::RequestReturnToLogin() { return false; }
void CysModernGame::Shutdown(unsigned int) {}

#endif
