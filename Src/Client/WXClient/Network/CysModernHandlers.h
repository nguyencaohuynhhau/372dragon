#pragma once
// CYS: 0.85 GC handlers ported from the legacy packet classes to ProtocolGen structs.
// Called only by CysModernGame on the main thread, inside CNetManager::Tick.

#include <cstdint>
#include "CysEquipmentTooltipData.h"

namespace pg { class Packet; class GCRetChangeScene; struct ItemInfo_bos; }
struct _ITEM;

namespace CysModernHandlers
{
    // Main procedure owns a loaded scene and the own object: world packets may run.
    bool WorldReady();
    // A world packet id that must wait (stash) until WorldReady().
    bool IsWorldPacket(uint16_t id);
    void OnConnected(uint32_t generation);
    void OnTransfer(const pg::GCRetChangeScene& accepted);
    void OnSessionClosed(bool quitComplete, bool returnToLogin = false);
    // Returns true when a ported handler consumed the packet. procedureChanged asks
    // the caller to stop the batch so the procedure switch happens before more packets.
    bool Dispatch(const pg::Packet& packet, uint32_t generation, bool& procedureChanged);
    // Counters for the session summary line.
    unsigned int InvalidPositions();
    unsigned int IgnoredSelfDeletes();
    // CYS-CORE: the CY item GUID has a u16 mask the 0.85 _ITEM_GUID cannot hold; the loot list keeps it per
    // (world, server, serial) so CGPickBoxItem can echo the full GUID.
    unsigned short ItemGuidMask(unsigned char world, unsigned char server, unsigned int serial);
    // CYS-CORE: counters of the core loop for logs/tests.
    struct CoreCounters { unsigned chats, skillSends, damages, targetLists, itemBoxes, boxLists, picksOk, itemInfos, bagLists, skillLists, dies, relives, levelUps, operateResults; };
    CoreCounters Core();
    // CYS-INV: CY ItemInfo (bos) -> 0.85 _ITEM for the UI data pool; also records the GUID mask.
    void ItemInfoToLegacy(const pg::ItemInfo_bos& in, _ITEM& out);
    // Main-thread display snapshot only; rejects GUID 0, a different item ID, and stale sessions.
    bool EquipmentTooltip(unsigned char world, unsigned char server, unsigned int serial, int itemIndex,
                          CysEquipmentTooltip::Snapshot& out);
}
