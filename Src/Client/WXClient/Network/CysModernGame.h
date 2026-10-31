#pragma once
// CYS: modern world session seam for Game.exe (NetCore GameClient). Main thread only.
// While a session exists, CNetManager never touches the 0.85 socket, packet factory
// or handlers: GC packets arrive as owned pg:: structs and are dispatched to ported
// handlers (CysModernHandlers.cpp); outbound 0.85 packets are translated or dropped.

#include "Packet.h"
#include <memory>
#include <string>
#include <vector>

namespace cys { namespace net { class GameTicket; } }
namespace pg { class Packet; }

namespace CysModernGame
{
    // Hand-off from CLAskCharLogin. Starts the worker; CGConnect is its first frame.
    // CYS: approvedGamePort 0 = the ticket port; allowedHosts = server profile allow-list (empty = 127/8).
    bool Start(cys::net::GameTicket&& ticket, unsigned short approvedGamePort,
               const std::vector<std::string>& allowedHosts = std::vector<std::string>());
    // A session object exists (connecting, in world, transferring or quitting).
    bool IsActive();
    bool IsInWorld();
    // CNetManager::Tick: dispatch pumped events, replay stashed world packets.
    void Pump();
    // CNetManager::SendPacket: translate a whitelisted 0.85 packet; drop the rest.
    void Send(Packet* legacy);
    // CYS-INV: send a ProtocolGen packet that has no 0.85 counterpart (CGAskMyBagSize); false when not sent.
    bool SendModern(std::unique_ptr<pg::Packet> packet);
    // One "stats" line now (the replay driver logs it before closing the window).
    void LogStatsNow();
    // The client could not load the scene the server placed it in: leave cleanly.
    void OnEnterSceneFailed();
    // CYS-RELOGIN: in world, CGAskQuit with the return-to-login flag; its quit_complete reopens the account screen.
    bool RequestReturnToLogin();
    // CNetManager::Release: CGAskQuit in world, bounded wait for the server close.
    void Shutdown(unsigned int timeoutMs);
    // CYS-CORE: nearest live monster of the session's WorldModel (server object id; 0 = none).
    unsigned int NearestMonster(float* distance);
    unsigned int NearestItemBox(float radius, float* x, float* z);
    unsigned int CurrentSceneId();
    // CYS-INV: nearest object (NPCs included) to a plan position of the session's WorldModel (0 = none).
    unsigned int NearestObjectAt(float x, float z, float radius, float* distance);
    // CYS-NPCINT: moi id doi tuong trong ban kinh quanh mot diem (sap xep theo khoang cach tang dan).
    // Tra so id da ghi vao `out` (<= max). Dung de do TUNG NPC quanh nguoi choi, khong chi con gan nhat.
    unsigned int ObjectsNear(float x, float z, float radius, unsigned int* out, unsigned int max);
}
