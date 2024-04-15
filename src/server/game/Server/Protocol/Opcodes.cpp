/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright
 * information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Opcodes.h"
#include "Log.h"
#include "Packets/AllPackets.h"
#include "WorldSession.h"
#include <iomanip>
#include <sstream>

template <class PacketClass,
          void (WorldSession::*HandlerFunction)(PacketClass&)>
class PacketHandler : public ClientOpcodeHandler {
public:
    PacketHandler(char const*      name,
                  SessionStatus    status,
                  PacketProcessing processing)
        : ClientOpcodeHandler(name, status, processing)
    {
    }

    void Call(WorldSession* session, WorldPacket& packet) const override
    {
        PacketClass nicePacket(std::move(packet));
        nicePacket.Read();
        (session->*HandlerFunction)(nicePacket);
    }
};

template <void (WorldSession::*HandlerFunction)(WorldPacket&)>
class PacketHandler<WorldPacket, HandlerFunction> : public ClientOpcodeHandler {
public:
    PacketHandler(char const*      name,
                  SessionStatus    status,
                  PacketProcessing processing)
        : ClientOpcodeHandler(name, status, processing)
    {
    }

    void Call(WorldSession* session, WorldPacket& packet) const override
    {
        (session->*HandlerFunction)(packet);
    }
};

OpcodeTable opcodeTable;

template <typename T>
struct get_packet_class {};

template <typename PacketClass>
struct get_packet_class<void (WorldSession::*)(PacketClass&)> {
    using type = PacketClass;
};

OpcodeTable::OpcodeTable()
{
    memset(_internalTableClient, 0, sizeof(_internalTableClient));
}

OpcodeTable::~OpcodeTable()
{
    for (uint16 i = 0; i < NUM_OPCODE_HANDLERS; ++i)
        delete _internalTableClient[i];
}

template <typename Handler, Handler HandlerFunction>
void OpcodeTable::ValidateAndSetClientOpcode(OpcodeClient     opcode,
                                             char const*      name,
                                             SessionStatus    status,
                                             PacketProcessing processing)
{
    if (uint32(opcode) == NULL_OPCODE) {
        LOG_ERROR("network", "Opcode {} does not have a value", name);
        return;
    }

    if (uint32(opcode) >= NUM_OPCODE_HANDLERS) {
        LOG_ERROR("network",
                  "Tried to set handler for an invalid opcode {}",
                  uint32(opcode));
        return;
    }

    if (_internalTableClient[opcode] != nullptr) {
        LOG_ERROR("network",
                  "Tried to override client handler of {} with {} (opcode {})",
                  opcodeTable[opcode]->Name,
                  name,
                  uint32(opcode));
        return;
    }

    _internalTableClient[opcode] =
        new PacketHandler<typename get_packet_class<Handler>::type,
                          HandlerFunction>(name, status, processing);
}

void OpcodeTable::ValidateAndSetServerOpcode(OpcodeServer  opcode,
                                             char const*   name,
                                             SessionStatus status)
{
    if (uint32(opcode) == NULL_OPCODE) {
        LOG_ERROR("network", "Opcode {} does not have a value", name);
        return;
    }

    if (uint32(opcode) >= NUM_OPCODE_HANDLERS) {
        LOG_ERROR("network",
                  "Tried to set handler for an invalid opcode {}",
                  uint32(opcode));
        return;
    }

    if (_internalTableClient[opcode] != nullptr) {
        LOG_ERROR("network",
                  "Tried to override server handler of {} with {} (opcode {})",
                  opcodeTable[opcode]->Name,
                  name,
                  uint32(opcode));
        return;
    }

    _internalTableClient[opcode] =
        new PacketHandler<WorldPacket, &WorldSession::Handle_ServerSide>(
            name, status, PROCESS_INPLACE);
}

/// Correspondence between opcodes and their names
void OpcodeTable::Initialize()
{
#define DEFINE_HANDLER(opcode, status, processing, handler)                    \
    ValidateAndSetClientOpcode<decltype(handler), handler>(                    \
        opcode, #opcode, status, processing)

#define DEFINE_SERVER_OPCODE_HANDLER(opcode, status)                           \
    static_assert(status == STATUS_NEVER || status == STATUS_UNHANDLED,        \
                  "Invalid status for server opcode");                         \
    ValidateAndSetServerOpcode(opcode, #opcode, status)

    /*0x001*/ DEFINE_HANDLER(
        CMSG_BOOTME, STATUS_NEVER, PROCESS_INPLACE, &WorldSession::Handle_NULL);
    /*0x002*/ DEFINE_HANDLER(CMSG_DBLOOKUP,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x003*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DBLOOKUP, STATUS_NEVER);
    /*0x004*/ DEFINE_HANDLER(CMSG_QUERY_OBJECT_POSITION,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x005*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUERY_OBJECT_POSITION,
                                           STATUS_NEVER);
    /*0x006*/ DEFINE_HANDLER(CMSG_QUERY_OBJECT_ROTATION,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x007*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUERY_OBJECT_ROTATION,
                                           STATUS_NEVER);
    /*0x008*/ DEFINE_HANDLER(CMSG_WORLD_TELEPORT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleWorldTeleportOpcode);
    /*0x009*/ DEFINE_HANDLER(CMSG_TELEPORT_TO_UNIT,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x00A*/ DEFINE_HANDLER(CMSG_ZONE_MAP,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x00B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ZONE_MAP, STATUS_NEVER);
    /*0x00C*/ DEFINE_HANDLER(CMSG_DEBUG_CHANGECELLZONE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x00D*/ DEFINE_HANDLER(CMSG_MOVE_CHARACTER_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x00E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_CHARACTER_CHEAT,
                                           STATUS_NEVER);
    /*0x00F*/ DEFINE_HANDLER(CMSG_RECHARGE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x010*/ DEFINE_HANDLER(CMSG_LEARN_SPELL,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x011*/ DEFINE_HANDLER(CMSG_CREATEMONSTER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x012*/ DEFINE_HANDLER(CMSG_DESTROYMONSTER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x013*/ DEFINE_HANDLER(CMSG_CREATEITEM,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x014*/ DEFINE_HANDLER(CMSG_CREATEGAMEOBJECT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x015*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHECK_FOR_BOTS, STATUS_NEVER);
    /*0x016*/ DEFINE_HANDLER(CMSG_MAKEMONSTERATTACKGUID,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x017*/ DEFINE_HANDLER(CMSG_BOT_DETECTED2,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x018*/ DEFINE_HANDLER(CMSG_FORCEACTION,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x019*/ DEFINE_HANDLER(CMSG_FORCEACTIONONOTHER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x01A*/ DEFINE_HANDLER(CMSG_FORCEACTIONSHOW,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x01B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCEACTIONSHOW, STATUS_NEVER);
    /*0x01C*/ DEFINE_HANDLER(CMSG_PETGODMODE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x01D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PETGODMODE, STATUS_NEVER);
    /*0x01E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_REFER_A_FRIEND_EXPIRED,
                                           STATUS_NEVER);
    /*0x01F*/ DEFINE_HANDLER(CMSG_WEATHER_SPEED_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x020*/ DEFINE_HANDLER(CMSG_UNDRESSPLAYER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x021*/ DEFINE_HANDLER(CMSG_BEASTMASTER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x022*/ DEFINE_HANDLER(CMSG_GODMODE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x023*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GODMODE, STATUS_NEVER);
    /*0x024*/ DEFINE_HANDLER(CMSG_CHEAT_SETMONEY,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x025*/ DEFINE_HANDLER(CMSG_LEVEL_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x026*/ DEFINE_HANDLER(CMSG_PET_LEVEL_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x027*/ DEFINE_HANDLER(CMSG_SET_WORLDSTATE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x028*/ DEFINE_HANDLER(CMSG_COOLDOWN_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x029*/ DEFINE_HANDLER(CMSG_USE_SKILL_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x02A*/ DEFINE_HANDLER(CMSG_FLAG_QUEST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x02B*/ DEFINE_HANDLER(CMSG_FLAG_QUEST_FINISH,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x02C*/ DEFINE_HANDLER(CMSG_CLEAR_QUEST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x02D*/ DEFINE_HANDLER(CMSG_SEND_EVENT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x02E*/ DEFINE_HANDLER(CMSG_DEBUG_AISTATE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x02F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DEBUG_AISTATE, STATUS_NEVER);
    /*0x030*/ DEFINE_HANDLER(CMSG_DISABLE_PVP_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x031*/ DEFINE_HANDLER(CMSG_ADVANCE_SPAWN_TIME,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x032*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DESTRUCTIBLE_BUILDING_DAMAGE,
                                           STATUS_NEVER);
    /*0x033*/ DEFINE_HANDLER(CMSG_AUTH_SRP6_BEGIN,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x034*/ DEFINE_HANDLER(CMSG_AUTH_SRP6_PROOF,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x035*/ DEFINE_HANDLER(CMSG_AUTH_SRP6_RECODE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x036*/ DEFINE_HANDLER(CMSG_CHAR_CREATE,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCharCreateOpcode);
    /*0x037*/ DEFINE_HANDLER(CMSG_CHAR_ENUM,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCharEnumOpcode);
    /*0x038*/ DEFINE_HANDLER(CMSG_CHAR_DELETE,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCharDeleteOpcode);
    /*0x039*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUTH_SRP6_RESPONSE,
                                           STATUS_NEVER);
    /*0x03A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAR_CREATE, STATUS_NEVER);
    /*0x03B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAR_ENUM, STATUS_NEVER);
    /*0x03C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAR_DELETE, STATUS_NEVER);
    /*0x03D*/ DEFINE_HANDLER(CMSG_PLAYER_LOGIN,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandlePlayerLoginOpcode);
    /*0x03E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_NEW_WORLD, STATUS_NEVER);
    /*0x03F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TRANSFER_PENDING, STATUS_NEVER);
    /*0x040*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TRANSFER_ABORTED, STATUS_NEVER);
    /*0x041*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHARACTER_LOGIN_FAILED,
                                           STATUS_NEVER);
    /*0x042*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOGIN_SETTIMESPEED,
                                           STATUS_NEVER);
    /*0x043*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GAMETIME_UPDATE, STATUS_NEVER);
    /*0x044*/ DEFINE_HANDLER(CMSG_GAMETIME_SET,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x045*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GAMETIME_SET, STATUS_NEVER);
    /*0x046*/ DEFINE_HANDLER(CMSG_GAMESPEED_SET,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x047*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GAMESPEED_SET, STATUS_NEVER);
    /*0x048*/ DEFINE_HANDLER(CMSG_SERVERTIME,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x049*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SERVERTIME, STATUS_NEVER);
    /*0x04A*/ DEFINE_HANDLER(CMSG_PLAYER_LOGOUT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandlePlayerLogoutOpcode);
    /*0x04B*/ DEFINE_HANDLER(CMSG_LOGOUT_REQUEST,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLogoutRequestOpcode);
    /*0x04C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOGOUT_RESPONSE, STATUS_NEVER);
    /*0x04D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOGOUT_COMPLETE, STATUS_NEVER);
    /*0x04E*/ DEFINE_HANDLER(CMSG_LOGOUT_CANCEL,
                             STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLogoutCancelOpcode);
    /*0x04F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOGOUT_CANCEL_ACK,
                                           STATUS_NEVER);
    /*0x050*/ DEFINE_HANDLER(CMSG_NAME_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleNameQueryOpcode);
    /*0x051*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_NAME_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x052*/ DEFINE_HANDLER(CMSG_PET_NAME_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandlePetNameQuery);
    /*0x053*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_NAME_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x054*/ DEFINE_HANDLER(CMSG_GUILD_QUERY,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildQueryOpcode);
    /*0x055*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GUILD_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x056*/ DEFINE_HANDLER(CMSG_ITEM_QUERY_SINGLE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleItemQuerySingleOpcode);
    /*0x057*/ DEFINE_HANDLER(CMSG_ITEM_QUERY_MULTIPLE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x058*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ITEM_QUERY_SINGLE_RESPONSE,
                                           STATUS_NEVER);
    /*0x059*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ITEM_QUERY_MULTIPLE_RESPONSE,
                                           STATUS_NEVER);
    /*0x05A*/ DEFINE_HANDLER(CMSG_PAGE_TEXT_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandlePageTextQueryOpcode);
    /*0x05B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PAGE_TEXT_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x05C*/ DEFINE_HANDLER(CMSG_QUEST_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestQueryOpcode);
    /*0x05D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUEST_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x05E*/ DEFINE_HANDLER(CMSG_GAMEOBJECT_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleGameObjectQueryOpcode);
    /*0x05F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GAMEOBJECT_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x060*/ DEFINE_HANDLER(CMSG_CREATURE_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleCreatureQueryOpcode);
    /*0x061*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CREATURE_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x062*/ DEFINE_HANDLER(CMSG_WHO,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleWhoOpcode);
    /*0x063*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_WHO, STATUS_NEVER);
    /*0x064*/ DEFINE_HANDLER(CMSG_WHOIS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleWhoisOpcode);
    /*0x065*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_WHOIS, STATUS_NEVER);
    /*0x066*/ DEFINE_HANDLER(CMSG_CONTACT_LIST,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleContactListOpcode);
    /*0x067*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CONTACT_LIST, STATUS_NEVER);
    /*0x068*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FRIEND_STATUS, STATUS_NEVER);
    /*0x069*/ DEFINE_HANDLER(CMSG_ADD_FRIEND,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAddFriendOpcode);
    /*0x06A*/ DEFINE_HANDLER(CMSG_DEL_FRIEND,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleDelFriendOpcode);
    /*0x06B*/ DEFINE_HANDLER(CMSG_SET_CONTACT_NOTES,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetContactNotesOpcode);
    /*0x06C*/ DEFINE_HANDLER(CMSG_ADD_IGNORE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAddIgnoreOpcode);
    /*0x06D*/ DEFINE_HANDLER(CMSG_DEL_IGNORE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleDelIgnoreOpcode);
    /*0x06E*/ DEFINE_HANDLER(CMSG_GROUP_INVITE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupInviteOpcode);
    /*0x06F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GROUP_INVITE, STATUS_NEVER);
    /*0x070*/ DEFINE_HANDLER(CMSG_GROUP_CANCEL,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x071*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GROUP_CANCEL, STATUS_NEVER);
    /*0x072*/ DEFINE_HANDLER(CMSG_GROUP_ACCEPT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupAcceptOpcode);
    /*0x073*/ DEFINE_HANDLER(CMSG_GROUP_DECLINE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupDeclineOpcode);
    /*0x074*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GROUP_DECLINE, STATUS_NEVER);
    /*0x075*/ DEFINE_HANDLER(CMSG_GROUP_UNINVITE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupUninviteOpcode);
    /*0x076*/ DEFINE_HANDLER(CMSG_GROUP_UNINVITE_GUID,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupUninviteGuidOpcode);
    /*0x077*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GROUP_UNINVITE, STATUS_NEVER);
    /*0x078*/ DEFINE_HANDLER(CMSG_GROUP_SET_LEADER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupSetLeaderOpcode);
    /*0x079*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GROUP_SET_LEADER, STATUS_NEVER);
    /*0x07A*/ DEFINE_HANDLER(CMSG_LOOT_METHOD,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLootMethodOpcode);
    /*0x07B*/ DEFINE_HANDLER(CMSG_GROUP_DISBAND,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupDisbandOpcode);
    /*0x07C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GROUP_DESTROYED, STATUS_NEVER);
    /*0x07D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GROUP_LIST, STATUS_NEVER);
    /*0x07E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PARTY_MEMBER_STATS,
                                           STATUS_NEVER);
    /*0x07F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PARTY_COMMAND_RESULT,
                                           STATUS_NEVER);
    /*0x080*/ DEFINE_HANDLER(UMSG_UPDATE_GROUP_MEMBERS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x081*/ DEFINE_HANDLER(CMSG_GUILD_CREATE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildCreateOpcode);
    /*0x082*/ DEFINE_HANDLER(CMSG_GUILD_INVITE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildInviteOpcode);
    /*0x083*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GUILD_INVITE, STATUS_NEVER);
    /*0x084*/ DEFINE_HANDLER(CMSG_GUILD_ACCEPT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildAcceptOpcode);
    /*0x085*/ DEFINE_HANDLER(CMSG_GUILD_DECLINE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildDeclineOpcode);
    /*0x086*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GUILD_DECLINE, STATUS_NEVER);
    /*0x087*/ DEFINE_HANDLER(CMSG_GUILD_INFO,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildInfoOpcode);
    /*0x088*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GUILD_INFO, STATUS_NEVER);
    /*0x089*/ DEFINE_HANDLER(CMSG_GUILD_ROSTER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildRosterOpcode);
    /*0x08A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GUILD_ROSTER, STATUS_NEVER);
    /*0x08B*/ DEFINE_HANDLER(CMSG_GUILD_PROMOTE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildPromoteOpcode);
    /*0x08C*/ DEFINE_HANDLER(CMSG_GUILD_DEMOTE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildDemoteOpcode);
    /*0x08D*/ DEFINE_HANDLER(CMSG_GUILD_LEAVE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildLeaveOpcode);
    /*0x08E*/ DEFINE_HANDLER(CMSG_GUILD_REMOVE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildRemoveOpcode);
    /*0x08F*/ DEFINE_HANDLER(CMSG_GUILD_DISBAND,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildDisbandOpcode);
    /*0x090*/ DEFINE_HANDLER(CMSG_GUILD_LEADER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildLeaderOpcode);
    /*0x091*/ DEFINE_HANDLER(CMSG_GUILD_MOTD,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildMOTDOpcode);
    /*0x092*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GUILD_EVENT, STATUS_NEVER);
    /*0x093*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GUILD_COMMAND_RESULT,
                                           STATUS_NEVER);
    /*0x094*/ DEFINE_HANDLER(UMSG_UPDATE_GUILD,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x095*/ DEFINE_HANDLER(CMSG_MESSAGECHAT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMessagechatOpcode);
    /*0x096*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MESSAGECHAT, STATUS_NEVER);
    /*0x097*/ DEFINE_HANDLER(CMSG_JOIN_CHANNEL,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleJoinChannel);
    /*0x098*/ DEFINE_HANDLER(CMSG_LEAVE_CHANNEL,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLeaveChannel);
    /*0x099*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHANNEL_NOTIFY, STATUS_NEVER);
    /*0x09A*/ DEFINE_HANDLER(CMSG_CHANNEL_LIST,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelList);
    /*0x09B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHANNEL_LIST, STATUS_NEVER);
    /*0x09C*/ DEFINE_HANDLER(CMSG_CHANNEL_PASSWORD,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelPassword);
    /*0x09D*/ DEFINE_HANDLER(CMSG_CHANNEL_SET_OWNER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelSetOwner);
    /*0x09E*/ DEFINE_HANDLER(CMSG_CHANNEL_OWNER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelOwner);
    /*0x09F*/ DEFINE_HANDLER(CMSG_CHANNEL_MODERATOR,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelModerator);
    /*0x0A0*/ DEFINE_HANDLER(CMSG_CHANNEL_UNMODERATOR,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelUnmoderator);
    /*0x0A1*/ DEFINE_HANDLER(CMSG_CHANNEL_MUTE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelMute);
    /*0x0A2*/ DEFINE_HANDLER(CMSG_CHANNEL_UNMUTE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelUnmute);
    /*0x0A3*/ DEFINE_HANDLER(CMSG_CHANNEL_INVITE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelInvite);
    /*0x0A4*/ DEFINE_HANDLER(CMSG_CHANNEL_KICK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelKick);
    /*0x0A5*/ DEFINE_HANDLER(CMSG_CHANNEL_BAN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelBan);
    /*0x0A6*/ DEFINE_HANDLER(CMSG_CHANNEL_UNBAN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelUnban);
    /*0x0A7*/ DEFINE_HANDLER(CMSG_CHANNEL_ANNOUNCEMENTS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelAnnouncements);
    /*0x0A8*/ DEFINE_HANDLER(CMSG_CHANNEL_MODERATE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChannelModerateOpcode);
    /*0x0A9*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_UPDATE_OBJECT, STATUS_NEVER);
    /*0x0AA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DESTROY_OBJECT, STATUS_NEVER);
    /*0x0AB*/ DEFINE_HANDLER(CMSG_USE_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleUseItemOpcode);
    /*0x0AC*/ DEFINE_HANDLER(CMSG_OPEN_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleOpenItemOpcode);
    /*0x0AD*/ DEFINE_HANDLER(CMSG_READ_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleReadItem);
    /*0x0AE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_READ_ITEM_OK, STATUS_NEVER);
    /*0x0AF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_READ_ITEM_FAILED, STATUS_NEVER);
    /*0x0B0*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ITEM_COOLDOWN, STATUS_NEVER);
    /*0x0B1*/ DEFINE_HANDLER(CMSG_GAMEOBJ_USE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleGameObjectUseOpcode);
    /*0x0B2*/ DEFINE_HANDLER(CMSG_DESTROY_ITEMS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0B3*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GAMEOBJECT_CUSTOM_ANIM,
                                           STATUS_NEVER);
    /*0x0B4*/ DEFINE_HANDLER(CMSG_AREATRIGGER,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleAreaTriggerOpcode);
    /*0x0B5*/ DEFINE_HANDLER(MSG_MOVE_START_FORWARD,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0B6*/ DEFINE_HANDLER(MSG_MOVE_START_BACKWARD,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0B7*/ DEFINE_HANDLER(MSG_MOVE_STOP,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0B8*/ DEFINE_HANDLER(MSG_MOVE_START_STRAFE_LEFT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0B9*/ DEFINE_HANDLER(MSG_MOVE_START_STRAFE_RIGHT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0BA*/ DEFINE_HANDLER(MSG_MOVE_STOP_STRAFE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0BB*/ DEFINE_HANDLER(MSG_MOVE_JUMP,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0BC*/ DEFINE_HANDLER(MSG_MOVE_START_TURN_LEFT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0BD*/ DEFINE_HANDLER(MSG_MOVE_START_TURN_RIGHT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0BE*/ DEFINE_HANDLER(MSG_MOVE_STOP_TURN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0BF*/ DEFINE_HANDLER(MSG_MOVE_START_PITCH_UP,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0C0*/ DEFINE_HANDLER(MSG_MOVE_START_PITCH_DOWN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0C1*/ DEFINE_HANDLER(MSG_MOVE_STOP_PITCH,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0C2*/ DEFINE_HANDLER(MSG_MOVE_SET_RUN_MODE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0C3*/ DEFINE_HANDLER(MSG_MOVE_SET_WALK_MODE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0C4*/ DEFINE_HANDLER(MSG_MOVE_TOGGLE_LOGGING,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0C5*/ DEFINE_HANDLER(MSG_MOVE_TELEPORT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0C6*/ DEFINE_HANDLER(MSG_MOVE_TELEPORT_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0C7*/ DEFINE_HANDLER(MSG_MOVE_TELEPORT_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMoveTeleportAck);
    /*0x0C8*/ DEFINE_HANDLER(MSG_MOVE_TOGGLE_FALL_LOGGING,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0C9*/ DEFINE_HANDLER(MSG_MOVE_FALL_LAND,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0CA*/ DEFINE_HANDLER(MSG_MOVE_START_SWIM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0CB*/ DEFINE_HANDLER(MSG_MOVE_STOP_SWIM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0CC*/ DEFINE_HANDLER(MSG_MOVE_SET_RUN_SPEED_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0CD*/ DEFINE_HANDLER(MSG_MOVE_SET_RUN_SPEED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0CE*/ DEFINE_HANDLER(MSG_MOVE_SET_RUN_BACK_SPEED_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0CF*/ DEFINE_HANDLER(MSG_MOVE_SET_RUN_BACK_SPEED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0D0*/ DEFINE_HANDLER(MSG_MOVE_SET_WALK_SPEED_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0D1*/ DEFINE_HANDLER(MSG_MOVE_SET_WALK_SPEED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0D2*/ DEFINE_HANDLER(MSG_MOVE_SET_SWIM_SPEED_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0D3*/ DEFINE_HANDLER(MSG_MOVE_SET_SWIM_SPEED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0D4*/ DEFINE_HANDLER(MSG_MOVE_SET_SWIM_BACK_SPEED_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0D5*/ DEFINE_HANDLER(MSG_MOVE_SET_SWIM_BACK_SPEED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0D6*/ DEFINE_HANDLER(MSG_MOVE_SET_ALL_SPEED_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0D7*/ DEFINE_HANDLER(MSG_MOVE_SET_TURN_RATE_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0D8*/ DEFINE_HANDLER(MSG_MOVE_SET_TURN_RATE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0D9*/ DEFINE_HANDLER(MSG_MOVE_TOGGLE_COLLISION_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0DA*/ DEFINE_HANDLER(MSG_MOVE_SET_FACING,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0DB*/ DEFINE_HANDLER(MSG_MOVE_SET_PITCH,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0DC*/ DEFINE_HANDLER(MSG_MOVE_WORLDPORT_ACK,
                             STATUS_TRANSFER,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMoveWorldportAckOpcode);
    /*0x0DD*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MONSTER_MOVE, STATUS_NEVER);
    /*0x0DE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_WATER_WALK, STATUS_NEVER);
    /*0x0DF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_LAND_WALK, STATUS_NEVER);
    /*0x0E0*/ DEFINE_HANDLER(CMSG_MOVE_CHARM_PORT_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0E1*/ DEFINE_HANDLER(CMSG_MOVE_SET_RAW_POSITION,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0E2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_RUN_SPEED_CHANGE,
                                           STATUS_NEVER);
    /*0x0E3*/ DEFINE_HANDLER(CMSG_FORCE_RUN_SPEED_CHANGE_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleForceSpeedChangeAck);
    /*0x0E4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_RUN_BACK_SPEED_CHANGE,
                                           STATUS_NEVER);
    /*0x0E5*/ DEFINE_HANDLER(CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleForceSpeedChangeAck);
    /*0x0E6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_SWIM_SPEED_CHANGE,
                                           STATUS_NEVER);
    /*0x0E7*/ DEFINE_HANDLER(CMSG_FORCE_SWIM_SPEED_CHANGE_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleForceSpeedChangeAck);
    /*0x0E8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_MOVE_ROOT, STATUS_NEVER);
    /*0x0E9*/ DEFINE_HANDLER(CMSG_FORCE_MOVE_ROOT_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMoveRootAck);
    /*0x0EA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_MOVE_UNROOT,
                                           STATUS_NEVER);
    /*0x0EB*/ DEFINE_HANDLER(CMSG_FORCE_MOVE_UNROOT_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMoveUnRootAck);
    /*0x0EC*/ DEFINE_HANDLER(MSG_MOVE_ROOT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0ED*/ DEFINE_HANDLER(MSG_MOVE_UNROOT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0EE*/ DEFINE_HANDLER(MSG_MOVE_HEARTBEAT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x0EF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_KNOCK_BACK, STATUS_NEVER);
    /*0x0F0*/ DEFINE_HANDLER(CMSG_MOVE_KNOCK_BACK_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMoveKnockBackAck);
    /*0x0F1*/ DEFINE_HANDLER(MSG_MOVE_KNOCK_BACK,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0F2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_FEATHER_FALL,
                                           STATUS_NEVER);
    /*0x0F3*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_NORMAL_FALL, STATUS_NEVER);
    /*0x0F4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_SET_HOVER, STATUS_NEVER);
    /*0x0F5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_UNSET_HOVER, STATUS_NEVER);
    /*0x0F6*/ DEFINE_HANDLER(CMSG_MOVE_HOVER_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMoveHoverAck);
    /*0x0F7*/ DEFINE_SERVER_OPCODE_HANDLER(MSG_MOVE_HOVER, STATUS_NEVER);
    /*0x0F8*/ DEFINE_HANDLER(CMSG_TRIGGER_CINEMATIC_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0F9*/ DEFINE_HANDLER(CMSG_OPENING_CINEMATIC,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x0FA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TRIGGER_CINEMATIC,
                                           STATUS_NEVER);
    /*0x0FB*/ DEFINE_HANDLER(CMSG_NEXT_CINEMATIC_CAMERA,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleNextCinematicCamera);
    /*0x0FC*/ DEFINE_HANDLER(CMSG_COMPLETE_CINEMATIC,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCompleteCinematic);
    /*0x0FD*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TUTORIAL_FLAGS, STATUS_NEVER);
    /*0x0FE*/ DEFINE_HANDLER(CMSG_TUTORIAL_FLAG,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleTutorialFlag);
    /*0x0FF*/ DEFINE_HANDLER(CMSG_TUTORIAL_CLEAR,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleTutorialClear);
    /*0x100*/ DEFINE_HANDLER(CMSG_TUTORIAL_RESET,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleTutorialReset);
    /*0x101*/ DEFINE_HANDLER(CMSG_STANDSTATECHANGE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleStandStateChangeOpcode);
    /*0x102*/ DEFINE_HANDLER(CMSG_EMOTE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleEmoteOpcode);
    /*0x103*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_EMOTE, STATUS_NEVER);
    /*0x104*/ DEFINE_HANDLER(CMSG_TEXT_EMOTE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleTextEmoteOpcode);
    /*0x105*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TEXT_EMOTE, STATUS_NEVER);
    /*0x106*/ DEFINE_HANDLER(CMSG_AUTOEQUIP_GROUND_ITEM,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x107*/ DEFINE_HANDLER(CMSG_AUTOSTORE_GROUND_ITEM,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x108*/ DEFINE_HANDLER(CMSG_AUTOSTORE_LOOT_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleAutostoreLootItemOpcode);
    /*0x109*/ DEFINE_HANDLER(CMSG_STORE_LOOT_IN_SLOT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x10A*/ DEFINE_HANDLER(CMSG_AUTOEQUIP_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleAutoEquipItemOpcode);
    /*0x10B*/ DEFINE_HANDLER(CMSG_AUTOSTORE_BAG_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleAutoStoreBagItemOpcode);
    /*0x10C*/ DEFINE_HANDLER(CMSG_SWAP_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleSwapItem);
    /*0x10D*/ DEFINE_HANDLER(CMSG_SWAP_INV_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleSwapInvItemOpcode);
    /*0x10E*/ DEFINE_HANDLER(CMSG_SPLIT_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleSplitItemOpcode);
    /*0x10F*/ DEFINE_HANDLER(CMSG_AUTOEQUIP_ITEM_SLOT,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleAutoEquipItemSlotOpcode);
    /*0x110*/ DEFINE_HANDLER(CMSG_UNCLAIM_LICENSE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x111*/ DEFINE_HANDLER(CMSG_DESTROYITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleDestroyItemOpcode);
    /*0x112*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INVENTORY_CHANGE_FAILURE,
                                           STATUS_NEVER);
    /*0x113*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_OPEN_CONTAINER, STATUS_NEVER);
    /*0x114*/ DEFINE_HANDLER(CMSG_INSPECT,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleInspectOpcode);
    /*0x115*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INSPECT_RESULTS_UPDATE,
                                           STATUS_NEVER);
    /*0x116*/ DEFINE_HANDLER(CMSG_INITIATE_TRADE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleInitiateTradeOpcode);
    /*0x117*/ DEFINE_HANDLER(CMSG_BEGIN_TRADE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBeginTradeOpcode);
    /*0x118*/ DEFINE_HANDLER(CMSG_BUSY_TRADE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBusyTradeOpcode);
    /*0x119*/ DEFINE_HANDLER(CMSG_IGNORE_TRADE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleIgnoreTradeOpcode);
    /*0x11A*/ DEFINE_HANDLER(CMSG_ACCEPT_TRADE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAcceptTradeOpcode);
    /*0x11B*/ DEFINE_HANDLER(CMSG_UNACCEPT_TRADE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleUnacceptTradeOpcode);
    /*0x11C*/ DEFINE_HANDLER(CMSG_CANCEL_TRADE,
                             STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCancelTradeOpcode);
    /*0x11D*/ DEFINE_HANDLER(CMSG_SET_TRADE_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetTradeItemOpcode);
    /*0x11E*/ DEFINE_HANDLER(CMSG_CLEAR_TRADE_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleClearTradeItemOpcode);
    /*0x11F*/ DEFINE_HANDLER(CMSG_SET_TRADE_GOLD,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetTradeGoldOpcode);
    /*0x120*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TRADE_STATUS, STATUS_NEVER);
    /*0x121*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TRADE_STATUS_EXTENDED,
                                           STATUS_NEVER);
    /*0x122*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INITIALIZE_FACTIONS,
                                           STATUS_NEVER);
    /*0x123*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SET_FACTION_VISIBLE,
                                           STATUS_NEVER);
    /*0x124*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SET_FACTION_STANDING,
                                           STATUS_NEVER);
    /*0x125*/ DEFINE_HANDLER(CMSG_SET_FACTION_ATWAR,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetFactionAtWar);
    /*0x126*/ DEFINE_HANDLER(CMSG_SET_FACTION_CHEAT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetFactionCheat);
    /*0x127*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SET_PROFICIENCY, STATUS_NEVER);
    /*0x128*/ DEFINE_HANDLER(CMSG_SET_ACTION_BUTTON,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetActionButtonOpcode);
    /*0x129*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ACTION_BUTTONS, STATUS_NEVER);
    /*0x12A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INITIAL_SPELLS, STATUS_NEVER);
    /*0x12B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LEARNED_SPELL, STATUS_NEVER);
    /*0x12C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SUPERCEDED_SPELL, STATUS_NEVER);
    /*0x12D*/ DEFINE_HANDLER(CMSG_NEW_SPELL_SLOT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x12E*/ DEFINE_HANDLER(CMSG_CAST_SPELL,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleCastSpellOpcode);
    /*0x12F*/ DEFINE_HANDLER(CMSG_CANCEL_CAST,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleCancelCastOpcode);
    /*0x130*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CAST_FAILED, STATUS_NEVER);
    /*0x131*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELL_START, STATUS_NEVER);
    /*0x132*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELL_GO, STATUS_NEVER);
    /*0x133*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELL_FAILURE, STATUS_NEVER);
    /*0x134*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELL_COOLDOWN, STATUS_NEVER);
    /*0x135*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COOLDOWN_EVENT, STATUS_NEVER);
    /*0x136*/ DEFINE_HANDLER(CMSG_CANCEL_AURA,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleCancelAuraOpcode);
    /*0x137*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_EQUIPMENT_SET_SAVED,
                                           STATUS_NEVER);
    /*0x138*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_CAST_FAILED, STATUS_NEVER);
    /*0x139*/ DEFINE_HANDLER(MSG_CHANNEL_START,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x13A*/ DEFINE_HANDLER(MSG_CHANNEL_UPDATE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x13B*/ DEFINE_HANDLER(CMSG_CANCEL_CHANNELLING,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleCancelChanneling);
    /*0x13C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AI_REACTION, STATUS_NEVER);
    /*0x13D*/ DEFINE_HANDLER(CMSG_SET_SELECTION,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleSetSelectionOpcode);
    /*0x13E*/ DEFINE_HANDLER(CMSG_DELETEEQUIPMENT_SET,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleEquipmentSetDelete);
    /*0x13F*/ DEFINE_HANDLER(CMSG_INSTANCE_LOCK_RESPONSE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleInstanceLockResponse);
    /*0x140*/ DEFINE_HANDLER(CMSG_DEBUG_PASSIVE_AURA,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x141*/ DEFINE_HANDLER(CMSG_ATTACKSWING,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleAttackSwingOpcode);
    /*0x142*/ DEFINE_HANDLER(CMSG_ATTACKSTOP,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleAttackStopOpcode);
    /*0x143*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ATTACKSTART, STATUS_NEVER);
    /*0x144*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ATTACKSTOP, STATUS_NEVER);
    /*0x145*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ATTACKSWING_NOTINRANGE,
                                           STATUS_NEVER);
    /*0x146*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ATTACKSWING_BADFACING,
                                           STATUS_NEVER);
    /*0x147*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INSTANCE_LOCK_WARNING_QUERY,
                                           STATUS_NEVER);
    /*0x148*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ATTACKSWING_DEADTARGET,
                                           STATUS_NEVER);
    /*0x149*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ATTACKSWING_CANT_ATTACK,
                                           STATUS_NEVER);
    /*0x14A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ATTACKERSTATEUPDATE,
                                           STATUS_NEVER);
    /*0x14B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEFIELD_PORT_DENIED,
                                           STATUS_NEVER);
    /*0x14C*/ DEFINE_HANDLER(CMSG_PERFORM_ACTION_SET,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x14D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RESUME_CAST_BAR, STATUS_NEVER);
    /*0x14E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CANCEL_COMBAT, STATUS_NEVER);
    /*0x14F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLBREAKLOG, STATUS_NEVER);
    /*0x150*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLHEALLOG, STATUS_NEVER);
    /*0x151*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLENERGIZELOG, STATUS_NEVER);
    /*0x152*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BREAK_TARGET, STATUS_NEVER);
    /*0x153*/ DEFINE_HANDLER(CMSG_SAVE_PLAYER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x154*/ DEFINE_HANDLER(CMSG_SETDEATHBINDPOINT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x155*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BINDPOINTUPDATE, STATUS_NEVER);
    /*0x156*/ DEFINE_HANDLER(CMSG_GETDEATHBINDZONE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x157*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BINDZONEREPLY, STATUS_NEVER);
    /*0x158*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAYERBOUND, STATUS_NEVER);
    /*0x159*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CLIENT_CONTROL_UPDATE,
                                           STATUS_NEVER);
    /*0x15A*/ DEFINE_HANDLER(CMSG_REPOP_REQUEST,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleRepopRequestOpcode);
    /*0x15B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RESURRECT_REQUEST,
                                           STATUS_NEVER);
    /*0x15C*/ DEFINE_HANDLER(CMSG_RESURRECT_RESPONSE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleResurrectResponseOpcode);
    /*0x15D*/ DEFINE_HANDLER(CMSG_LOOT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLootOpcode);
    /*0x15E*/ DEFINE_HANDLER(CMSG_LOOT_MONEY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLootMoneyOpcode);
    /*0x15F*/ DEFINE_HANDLER(CMSG_LOOT_RELEASE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLootReleaseOpcode);
    /*0x160*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_RESPONSE, STATUS_NEVER);
    /*0x161*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_RELEASE_RESPONSE,
                                           STATUS_NEVER);
    /*0x162*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_REMOVED, STATUS_NEVER);
    /*0x163*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_MONEY_NOTIFY,
                                           STATUS_NEVER);
    /*0x164*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_ITEM_NOTIFY, STATUS_NEVER);
    /*0x165*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_CLEAR_MONEY, STATUS_NEVER);
    /*0x166*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ITEM_PUSH_RESULT, STATUS_NEVER);
    /*0x167*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DUEL_REQUESTED, STATUS_NEVER);
    /*0x168*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DUEL_OUTOFBOUNDS, STATUS_NEVER);
    /*0x169*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DUEL_INBOUNDS, STATUS_NEVER);
    /*0x16A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DUEL_COMPLETE, STATUS_NEVER);
    /*0x16B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DUEL_WINNER, STATUS_NEVER);
    /*0x16C*/ DEFINE_HANDLER(CMSG_DUEL_ACCEPTED,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleDuelAcceptedOpcode);
    /*0x16D*/ DEFINE_HANDLER(CMSG_DUEL_CANCELLED,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleDuelCancelledOpcode);
    /*0x16E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOUNTRESULT, STATUS_NEVER);
    /*0x16F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DISMOUNTRESULT, STATUS_NEVER);
    /*0x170*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_REMOVED_FROM_PVP_QUEUE,
                                           STATUS_NEVER);
    /*0x171*/ DEFINE_HANDLER(CMSG_MOUNTSPECIAL_ANIM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMountSpecialAnimOpcode);
    /*0x172*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOUNTSPECIAL_ANIM,
                                           STATUS_NEVER);
    /*0x173*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_TAME_FAILURE, STATUS_NEVER);
    /*0x174*/ DEFINE_HANDLER(CMSG_PET_SET_ACTION,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandlePetSetAction);
    /*0x175*/ DEFINE_HANDLER(CMSG_PET_ACTION,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandlePetAction);
    /*0x176*/ DEFINE_HANDLER(CMSG_PET_ABANDON,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandlePetAbandon);
    /*0x177*/ DEFINE_HANDLER(CMSG_PET_RENAME,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandlePetRename);
    /*0x178*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_NAME_INVALID, STATUS_NEVER);
    /*0x179*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_SPELLS, STATUS_NEVER);
    /*0x17A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_MODE, STATUS_NEVER);
    /*0x17B*/ DEFINE_HANDLER(CMSG_GOSSIP_HELLO,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleGossipHelloOpcode);
    /*0x17C*/ DEFINE_HANDLER(CMSG_GOSSIP_SELECT_OPTION,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGossipSelectOptionOpcode);
    /*0x17D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GOSSIP_MESSAGE, STATUS_NEVER);
    /*0x17E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GOSSIP_COMPLETE, STATUS_NEVER);
    /*0x17F*/ DEFINE_HANDLER(CMSG_NPC_TEXT_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleNpcTextQueryOpcode);
    /*0x180*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_NPC_TEXT_UPDATE, STATUS_NEVER);
    /*0x181*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_NPC_WONT_TALK, STATUS_NEVER);
    /*0x182*/ DEFINE_HANDLER(CMSG_QUESTGIVER_STATUS_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleQuestgiverStatusQueryOpcode);
    /*0x183*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTGIVER_STATUS,
                                           STATUS_NEVER);
    /*0x184*/ DEFINE_HANDLER(CMSG_QUESTGIVER_HELLO,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestgiverHelloOpcode);
    /*0x185*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTGIVER_QUEST_LIST,
                                           STATUS_NEVER);
    /*0x186*/ DEFINE_HANDLER(CMSG_QUESTGIVER_QUERY_QUEST,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestgiverQueryQuestOpcode);
    /*0x187*/ DEFINE_HANDLER(CMSG_QUESTGIVER_QUEST_AUTOLAUNCH,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestgiverQuestAutoLaunch);
    /*0x188*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTGIVER_QUEST_DETAILS,
                                           STATUS_NEVER);
    /*0x189*/ DEFINE_HANDLER(CMSG_QUESTGIVER_ACCEPT_QUEST,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestgiverAcceptQuestOpcode);
    /*0x18A*/ DEFINE_HANDLER(CMSG_QUESTGIVER_COMPLETE_QUEST,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestgiverCompleteQuest);
    /*0x18B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTGIVER_REQUEST_ITEMS,
                                           STATUS_NEVER);
    /*0x18C*/ DEFINE_HANDLER(
        CMSG_QUESTGIVER_REQUEST_REWARD,
        STATUS_LOGGEDIN,
        PROCESS_INPLACE,
        &WorldSession::HandleQuestgiverRequestRewardOpcode);
    /*0x18D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTGIVER_OFFER_REWARD,
                                           STATUS_NEVER);
    /*0x18E*/ DEFINE_HANDLER(CMSG_QUESTGIVER_CHOOSE_REWARD,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestgiverChooseRewardOpcode);
    /*0x18F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTGIVER_QUEST_INVALID,
                                           STATUS_NEVER);
    /*0x190*/ DEFINE_HANDLER(CMSG_QUESTGIVER_CANCEL,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestgiverCancel);
    /*0x191*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTGIVER_QUEST_COMPLETE,
                                           STATUS_NEVER);
    /*0x192*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTGIVER_QUEST_FAILED,
                                           STATUS_NEVER);
    /*0x193*/ DEFINE_HANDLER(CMSG_QUESTLOG_SWAP_QUEST,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestLogSwapQuest);
    /*0x194*/ DEFINE_HANDLER(CMSG_QUESTLOG_REMOVE_QUEST,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestLogRemoveQuest);
    /*0x195*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTLOG_FULL, STATUS_NEVER);
    /*0x196*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTUPDATE_FAILED,
                                           STATUS_NEVER);
    /*0x197*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTUPDATE_FAILEDTIMER,
                                           STATUS_NEVER);
    /*0x198*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTUPDATE_COMPLETE,
                                           STATUS_NEVER);
    /*0x199*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTUPDATE_ADD_KILL,
                                           STATUS_NEVER);
    /*0x19A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTUPDATE_ADD_ITEM,
                                           STATUS_NEVER);
    /*0x19B*/ DEFINE_HANDLER(CMSG_QUEST_CONFIRM_ACCEPT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleQuestConfirmAccept);
    /*0x19C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUEST_CONFIRM_ACCEPT,
                                           STATUS_NEVER);
    /*0x19D*/ DEFINE_HANDLER(CMSG_PUSHQUESTTOPARTY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandlePushQuestToParty);
    /*0x19E*/ DEFINE_HANDLER(CMSG_LIST_INVENTORY,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleListInventoryOpcode);
    /*0x19F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LIST_INVENTORY, STATUS_NEVER);
    /*0x1A0*/ DEFINE_HANDLER(CMSG_SELL_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleSellItemOpcode);
    /*0x1A1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SELL_ITEM, STATUS_NEVER);
    /*0x1A2*/ DEFINE_HANDLER(CMSG_BUY_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleBuyItemOpcode);
    /*0x1A3*/ DEFINE_HANDLER(CMSG_BUY_ITEM_IN_SLOT,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleBuyItemInSlotOpcode);
    /*0x1A4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BUY_ITEM, STATUS_NEVER);
    /*0x1A5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BUY_FAILED, STATUS_NEVER);
    /*0x1A6*/ DEFINE_HANDLER(CMSG_TAXICLEARALLNODES,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1A7*/ DEFINE_HANDLER(CMSG_TAXIENABLEALLNODES,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1A8*/ DEFINE_HANDLER(CMSG_TAXISHOWNODES,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1A9*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SHOWTAXINODES, STATUS_NEVER);
    /*0x1AA*/ DEFINE_HANDLER(CMSG_TAXINODE_STATUS_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleTaxiNodeStatusQueryOpcode);
    /*0x1AB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TAXINODE_STATUS, STATUS_NEVER);
    /*0x1AC*/ DEFINE_HANDLER(CMSG_TAXIQUERYAVAILABLENODES,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleTaxiQueryAvailableNodes);
    /*0x1AD*/ DEFINE_HANDLER(CMSG_ACTIVATETAXI,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleActivateTaxiOpcode);
    /*0x1AE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ACTIVATETAXIREPLY,
                                           STATUS_NEVER);
    /*0x1AF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_NEW_TAXI_PATH, STATUS_NEVER);
    /*0x1B0*/ DEFINE_HANDLER(CMSG_TRAINER_LIST,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleTrainerListOpcode);
    /*0x1B1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TRAINER_LIST, STATUS_NEVER);
    /*0x1B2*/ DEFINE_HANDLER(CMSG_TRAINER_BUY_SPELL,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleTrainerBuySpellOpcode);
    /*0x1B3*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TRAINER_BUY_SUCCEEDED,
                                           STATUS_NEVER);
    /*0x1B4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TRAINER_BUY_FAILED,
                                           STATUS_NEVER);
    /*0x1B5*/ DEFINE_HANDLER(CMSG_BINDER_ACTIVATE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleBinderActivateOpcode);
    /*0x1B6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAYERBINDERROR, STATUS_NEVER);
    /*0x1B7*/ DEFINE_HANDLER(CMSG_BANKER_ACTIVATE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleBankerActivateOpcode);
    /*0x1B8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SHOW_BANK, STATUS_NEVER);
    /*0x1B9*/ DEFINE_HANDLER(CMSG_BUY_BANK_SLOT,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleBuyBankSlotOpcode);
    /*0x1BA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BUY_BANK_SLOT_RESULT,
                                           STATUS_NEVER);
    /*0x1BB*/ DEFINE_HANDLER(CMSG_PETITION_SHOWLIST,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandlePetitionShowListOpcode);
    /*0x1BC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PETITION_SHOWLIST,
                                           STATUS_NEVER);
    /*0x1BD*/ DEFINE_HANDLER(CMSG_PETITION_BUY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandlePetitionBuyOpcode);
    /*0x1BE*/ DEFINE_HANDLER(CMSG_PETITION_SHOW_SIGNATURES,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandlePetitionShowSignOpcode);
    /*0x1BF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PETITION_SHOW_SIGNATURES,
                                           STATUS_NEVER);
    /*0x1C0*/ DEFINE_HANDLER(CMSG_PETITION_SIGN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandlePetitionSignOpcode);
    /*0x1C1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PETITION_SIGN_RESULTS,
                                           STATUS_NEVER);
    /*0x1C2*/ DEFINE_HANDLER(MSG_PETITION_DECLINE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandlePetitionDeclineOpcode);
    /*0x1C3*/ DEFINE_HANDLER(CMSG_OFFER_PETITION,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleOfferPetitionOpcode);
    /*0x1C4*/ DEFINE_HANDLER(CMSG_TURN_IN_PETITION,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleTurnInPetitionOpcode);
    /*0x1C5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TURN_IN_PETITION_RESULTS,
                                           STATUS_NEVER);
    /*0x1C6*/ DEFINE_HANDLER(CMSG_PETITION_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandlePetitionQueryOpcode);
    /*0x1C7*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PETITION_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x1C8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FISH_NOT_HOOKED, STATUS_NEVER);
    /*0x1C9*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FISH_ESCAPED, STATUS_NEVER);
    /*0x1CA*/ DEFINE_HANDLER(CMSG_BUG,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBugOpcode);
    /*0x1CB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_NOTIFICATION, STATUS_NEVER);
    /*0x1CC*/ DEFINE_HANDLER(CMSG_PLAYED_TIME,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandlePlayedTime);
    /*0x1CD*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAYED_TIME, STATUS_NEVER);
    /*0x1CE*/ DEFINE_HANDLER(CMSG_QUERY_TIME,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQueryTimeOpcode);
    /*0x1CF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUERY_TIME_RESPONSE,
                                           STATUS_NEVER);
    /*0x1D0*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOG_XPGAIN, STATUS_NEVER);
    /*0x1D1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AURACASTLOG, STATUS_NEVER);
    /*0x1D2*/ DEFINE_HANDLER(CMSG_RECLAIM_CORPSE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleReclaimCorpseOpcode);
    /*0x1D3*/ DEFINE_HANDLER(CMSG_WRAP_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleWrapItemOpcode);
    /*0x1D4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LEVELUP_INFO, STATUS_NEVER);
    /*0x1D5*/ DEFINE_HANDLER(MSG_MINIMAP_PING,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMinimapPingOpcode);
    /*0x1D6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RESISTLOG, STATUS_NEVER);
    /*0x1D7*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ENCHANTMENTLOG, STATUS_NEVER);
    /*0x1D8*/ DEFINE_HANDLER(CMSG_SET_SKILL_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1D9*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_START_MIRROR_TIMER,
                                           STATUS_NEVER);
    /*0x1DA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PAUSE_MIRROR_TIMER,
                                           STATUS_NEVER);
    /*0x1DB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_STOP_MIRROR_TIMER,
                                           STATUS_NEVER);
    /*0x1DC*/ DEFINE_HANDLER(CMSG_PING,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_EarlyProccess);
    /*0x1DD*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PONG, STATUS_NEVER);
    /*0x1DE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CLEAR_COOLDOWN, STATUS_NEVER);
    /*0x1DF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GAMEOBJECT_PAGETEXT,
                                           STATUS_NEVER);
    /*0x1E0*/ DEFINE_HANDLER(CMSG_SET_SHEATHED,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleSetSheathedOpcode);
    /*0x1E1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COOLDOWN_CHEAT, STATUS_NEVER);
    /*0x1E2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELL_DELAYED, STATUS_NEVER);
    /*0x1E3*/ DEFINE_HANDLER(CMSG_QUEST_POI_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQuestPOIQuery);
    /*0x1E4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUEST_POI_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x1E5*/ DEFINE_HANDLER(
        CMSG_GHOST, STATUS_NEVER, PROCESS_INPLACE, &WorldSession::Handle_NULL);
    /*0x1E6*/ DEFINE_HANDLER(CMSG_GM_INVIS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1E7*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INVALID_PROMOTION_CODE,
                                           STATUS_NEVER);
    /*0x1E8*/ DEFINE_HANDLER(MSG_GM_BIND_OTHER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1E9*/ DEFINE_HANDLER(MSG_GM_SUMMON,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1EA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ITEM_TIME_UPDATE, STATUS_NEVER);
    /*0x1EB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ITEM_ENCHANT_TIME_UPDATE,
                                           STATUS_NEVER);
    /*0x1EC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUTH_CHALLENGE, STATUS_NEVER);
    /*0x1ED*/ DEFINE_HANDLER(CMSG_AUTH_SESSION,
                             STATUS_NEVER,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::Handle_EarlyProccess);
    /*0x1EE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUTH_RESPONSE, STATUS_NEVER);
    /*0x1EF*/ DEFINE_HANDLER(MSG_GM_SHOWLABEL,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1F0*/ DEFINE_HANDLER(CMSG_PET_CAST_SPELL,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandlePetCastSpellOpcode);
    /*0x1F1*/ DEFINE_HANDLER(MSG_SAVE_GUILD_EMBLEM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSaveGuildEmblemOpcode);
    /*0x1F2*/ DEFINE_HANDLER(MSG_TABARDVENDOR_ACTIVATE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleTabardVendorActivateOpcode);
    /*0x1F3*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAY_SPELL_VISUAL,
                                           STATUS_NEVER);
    /*0x1F4*/ DEFINE_HANDLER(CMSG_ZONEUPDATE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleZoneUpdateOpcode);
    /*0x1F5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PARTYKILLLOG, STATUS_NEVER);
    /*0x1F6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMPRESSED_UPDATE_OBJECT,
                                           STATUS_NEVER);
    /*0x1F7*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAY_SPELL_IMPACT,
                                           STATUS_NEVER);
    /*0x1F8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_EXPLORATION_EXPERIENCE,
                                           STATUS_NEVER);
    /*0x1F9*/ DEFINE_HANDLER(CMSG_GM_SET_SECURITY_GROUP,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1FA*/ DEFINE_HANDLER(CMSG_GM_NUKE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1FB*/ DEFINE_HANDLER(MSG_RANDOM_ROLL,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleRandomRollOpcode);
    /*0x1FC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ENVIRONMENTAL_DAMAGE_LOG,
                                           STATUS_NEVER);
    /*0x1FD*/ DEFINE_HANDLER(CMSG_CHANGEPLAYER_DIFFICULTY,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x1FE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RWHOIS, STATUS_NEVER);
    /*0x1FF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_PLAYER_REWARD,
                                           STATUS_NEVER);
    /*0x200*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_TELEPORT_DENIED,
                                           STATUS_NEVER);
    /*0x201*/ DEFINE_HANDLER(CMSG_UNLEARN_SPELL,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x202*/ DEFINE_HANDLER(CMSG_UNLEARN_SKILL,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleUnlearnSkillOpcode);
    /*0x203*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_REMOVED_SPELL, STATUS_NEVER);
    /*0x204*/ DEFINE_HANDLER(CMSG_DECHARGE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x205*/ DEFINE_HANDLER(CMSG_GMTICKET_CREATE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGMTicketCreateOpcode);
    /*0x206*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GMTICKET_CREATE, STATUS_NEVER);
    /*0x207*/ DEFINE_HANDLER(CMSG_GMTICKET_UPDATETEXT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGMTicketUpdateOpcode);
    /*0x208*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GMTICKET_UPDATETEXT,
                                           STATUS_NEVER);
    /*0x209*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ACCOUNT_DATA_TIMES,
                                           STATUS_NEVER);
    /*0x20A*/ DEFINE_HANDLER(CMSG_REQUEST_ACCOUNT_DATA,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleRequestAccountData);
    /*0x20B*/ DEFINE_HANDLER(CMSG_UPDATE_ACCOUNT_DATA,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleUpdateAccountData);
    /*0x20C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_UPDATE_ACCOUNT_DATA,
                                           STATUS_NEVER);
    /*0x20D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CLEAR_FAR_SIGHT_IMMEDIATE,
                                           STATUS_NEVER);
    /*0x20E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHANGEPLAYER_DIFFICULTY_RESULT,
                                           STATUS_NEVER);
    /*0x20F*/ DEFINE_HANDLER(CMSG_GM_TEACH,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x210*/ DEFINE_HANDLER(CMSG_GM_CREATE_ITEM_TARGET,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x211*/ DEFINE_HANDLER(CMSG_GMTICKET_GETTICKET,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGMTicketGetTicketOpcode);
    /*0x212*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GMTICKET_GETTICKET,
                                           STATUS_NEVER);
    /*0x213*/ DEFINE_HANDLER(CMSG_UNLEARN_TALENTS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x214*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT,
                                           STATUS_NEVER);
    /*0x215*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GAMEOBJECT_DESPAWN_ANIM,
                                           STATUS_NEVER);
    /*0x216*/ DEFINE_HANDLER(MSG_CORPSE_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCorpseQueryOpcode);
    /*0x217*/ DEFINE_HANDLER(CMSG_GMTICKET_DELETETICKET,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGMTicketDeleteOpcode);
    /*0x218*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GMTICKET_DELETETICKET,
                                           STATUS_NEVER);
    /*0x219*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAT_WRONG_FACTION,
                                           STATUS_NEVER);
    /*0x21A*/ DEFINE_HANDLER(CMSG_GMTICKET_SYSTEMSTATUS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGMTicketSystemStatusOpcode);
    /*0x21B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GMTICKET_SYSTEMSTATUS,
                                           STATUS_NEVER);
    /*0x21C*/ DEFINE_HANDLER(
        CMSG_SPIRIT_HEALER_ACTIVATE,
        STATUS_LOGGEDIN,
        PROCESS_THREADUNSAFE,
        &WorldSession::
            HandleSpiritHealerActivateOpcode); // pussywizard: corpse on other
                                               // map, GetAreaFlag, this
                                               // involved vmaps, grids and more
    /*0x21D*/ DEFINE_HANDLER(CMSG_SET_STAT_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x21E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUEST_FORCE_REMOVE,
                                           STATUS_NEVER);
    /*0x21F*/ DEFINE_HANDLER(CMSG_SKILL_BUY_STEP,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x220*/ DEFINE_HANDLER(CMSG_SKILL_BUY_RANK,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x221*/ DEFINE_HANDLER(CMSG_XP_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x222*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPIRIT_HEALER_CONFIRM,
                                           STATUS_NEVER);
    /*0x223*/ DEFINE_HANDLER(CMSG_CHARACTER_POINT_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x224*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GOSSIP_POI, STATUS_NEVER);
    /*0x225*/ DEFINE_HANDLER(CMSG_CHAT_IGNORED,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleChatIgnoredOpcode);
    /*0x226*/ DEFINE_HANDLER(CMSG_GM_VISION,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x227*/ DEFINE_HANDLER(CMSG_SERVER_COMMAND,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x228*/ DEFINE_HANDLER(CMSG_GM_SILENCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x229*/ DEFINE_HANDLER(CMSG_GM_REVEALTO,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x22A*/ DEFINE_HANDLER(CMSG_GM_RESURRECT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x22B*/ DEFINE_HANDLER(CMSG_GM_SUMMONMOB,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x22C*/ DEFINE_HANDLER(CMSG_GM_MOVECORPSE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x22D*/ DEFINE_HANDLER(CMSG_GM_FREEZE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x22E*/ DEFINE_HANDLER(CMSG_GM_UBERINVIS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x22F*/ DEFINE_HANDLER(CMSG_GM_REQUEST_PLAYER_INFO,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x230*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GM_PLAYER_INFO, STATUS_NEVER);
    /*0x231*/ DEFINE_HANDLER(CMSG_GUILD_RANK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildRankOpcode);
    /*0x232*/ DEFINE_HANDLER(CMSG_GUILD_ADD_RANK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildAddRankOpcode);
    /*0x233*/ DEFINE_HANDLER(CMSG_GUILD_DEL_RANK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildDelRankOpcode);
    /*0x234*/ DEFINE_HANDLER(CMSG_GUILD_SET_PUBLIC_NOTE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildSetPublicNoteOpcode);
    /*0x235*/ DEFINE_HANDLER(CMSG_GUILD_SET_OFFICER_NOTE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildSetOfficerNoteOpcode);
    /*0x236*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOGIN_VERIFY_WORLD,
                                           STATUS_NEVER);
    /*0x237*/ DEFINE_HANDLER(CMSG_CLEAR_EXPLORATION,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x238*/ DEFINE_HANDLER(CMSG_SEND_MAIL,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSendMail);
    /*0x239*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SEND_MAIL_RESULT, STATUS_NEVER);
    /*0x23A*/ DEFINE_HANDLER(CMSG_GET_MAIL_LIST,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGetMailList);
    /*0x23B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MAIL_LIST_RESULT, STATUS_NEVER);
    /*0x23C*/ DEFINE_HANDLER(CMSG_BATTLEFIELD_LIST,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBattlefieldListOpcode);
    /*0x23D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEFIELD_LIST, STATUS_NEVER);
    /*0x23E*/ DEFINE_HANDLER(CMSG_BATTLEFIELD_JOIN,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x23F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_SET_VEHICLE_REC_ID,
                                           STATUS_NEVER);
    /*0x240*/ DEFINE_HANDLER(CMSG_SET_VEHICLE_REC_ID_ACK,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x241*/ DEFINE_HANDLER(CMSG_TAXICLEARNODE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x242*/ DEFINE_HANDLER(CMSG_TAXIENABLENODE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x243*/ DEFINE_HANDLER(CMSG_ITEM_TEXT_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleItemTextQuery);
    /*0x244*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ITEM_TEXT_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x245*/ DEFINE_HANDLER(CMSG_MAIL_TAKE_MONEY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMailTakeMoney);
    /*0x246*/ DEFINE_HANDLER(CMSG_MAIL_TAKE_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMailTakeItem);
    /*0x247*/ DEFINE_HANDLER(CMSG_MAIL_MARK_AS_READ,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMailMarkAsRead);
    /*0x248*/ DEFINE_HANDLER(CMSG_MAIL_RETURN_TO_SENDER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMailReturnToSender);
    /*0x249*/ DEFINE_HANDLER(CMSG_MAIL_DELETE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMailDelete);
    /*0x24A*/ DEFINE_HANDLER(CMSG_MAIL_CREATE_TEXT_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMailCreateTextItem);
    /*0x24B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLLOGMISS, STATUS_NEVER);
    /*0x24C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLLOGEXECUTE, STATUS_NEVER);
    /*0x24D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DEBUGAURAPROC, STATUS_NEVER);
    /*0x24E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PERIODICAURALOG, STATUS_NEVER);
    /*0x24F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLDAMAGESHIELD,
                                           STATUS_NEVER);
    /*0x250*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLNONMELEEDAMAGELOG,
                                           STATUS_NEVER);
    /*0x251*/ DEFINE_HANDLER(CMSG_LEARN_TALENT,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleLearnTalentOpcode);
    /*0x252*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RESURRECT_FAILED, STATUS_NEVER);
    /*0x253*/ DEFINE_HANDLER(CMSG_TOGGLE_PVP,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleTogglePvP);
    /*0x254*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ZONE_UNDER_ATTACK,
                                           STATUS_NEVER);
    /*0x255*/ DEFINE_HANDLER(MSG_AUCTION_HELLO,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAuctionHelloOpcode);
    /*0x256*/ DEFINE_HANDLER(CMSG_AUCTION_SELL_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAuctionSellItem);
    /*0x257*/ DEFINE_HANDLER(CMSG_AUCTION_REMOVE_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAuctionRemoveItem);
    /*0x258*/ DEFINE_HANDLER(CMSG_AUCTION_LIST_ITEMS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleAuctionListItems);
    /*0x259*/ DEFINE_HANDLER(CMSG_AUCTION_LIST_OWNER_ITEMS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleAuctionListOwnerItems);
    /*0x25A*/ DEFINE_HANDLER(CMSG_AUCTION_PLACE_BID,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAuctionPlaceBid);
    /*0x25B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUCTION_COMMAND_RESULT,
                                           STATUS_NEVER);
    /*0x25C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUCTION_LIST_RESULT,
                                           STATUS_NEVER);
    /*0x25D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUCTION_OWNER_LIST_RESULT,
                                           STATUS_NEVER);
    /*0x25E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUCTION_BIDDER_NOTIFICATION,
                                           STATUS_NEVER);
    /*0x25F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUCTION_OWNER_NOTIFICATION,
                                           STATUS_NEVER);
    /*0x260*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PROCRESIST, STATUS_NEVER);
    /*0x261*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMBAT_EVENT_FAILED,
                                           STATUS_NEVER);
    /*0x262*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DISPEL_FAILED, STATUS_NEVER);
    /*0x263*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLORDAMAGE_IMMUNE,
                                           STATUS_NEVER);
    /*0x264*/ DEFINE_HANDLER(CMSG_AUCTION_LIST_BIDDER_ITEMS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleAuctionListBidderItems);
    /*0x265*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUCTION_BIDDER_LIST_RESULT,
                                           STATUS_NEVER);
    /*0x266*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SET_FLAT_SPELL_MODIFIER,
                                           STATUS_NEVER);
    /*0x267*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SET_PCT_SPELL_MODIFIER,
                                           STATUS_NEVER);
    /*0x268*/ DEFINE_HANDLER(CMSG_SET_AMMO,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleSetAmmoOpcode);
    /*0x269*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CORPSE_RECLAIM_DELAY,
                                           STATUS_NEVER);
    /*0x26A*/ DEFINE_HANDLER(CMSG_SET_ACTIVE_MOVER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetActiveMoverOpcode);
    /*0x26B*/ DEFINE_HANDLER(CMSG_PET_CANCEL_AURA,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandlePetCancelAuraOpcode);
    /*0x26C*/ DEFINE_HANDLER(CMSG_PLAYER_AI_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x26D*/ DEFINE_HANDLER(CMSG_CANCEL_AUTO_REPEAT_SPELL,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleCancelAutoRepeatSpellOpcode);
    /*0x26E*/ DEFINE_HANDLER(MSG_GM_ACCOUNT_ONLINE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x26F*/ DEFINE_HANDLER(MSG_LIST_STABLED_PETS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleListStabledPetsOpcode);
    /*0x270*/ DEFINE_HANDLER(CMSG_STABLE_PET,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleStablePet);
    /*0x271*/ DEFINE_HANDLER(CMSG_UNSTABLE_PET,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleUnstablePet);
    /*0x272*/ DEFINE_HANDLER(CMSG_BUY_STABLE_SLOT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBuyStableSlot);
    /*0x273*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_STABLE_RESULT, STATUS_NEVER);
    /*0x274*/ DEFINE_HANDLER(CMSG_STABLE_REVIVE_PET,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleStableRevivePet);
    /*0x275*/ DEFINE_HANDLER(CMSG_STABLE_SWAP_PET,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleStableSwapPet);
    /*0x276*/ DEFINE_HANDLER(MSG_QUEST_PUSH_RESULT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleQuestPushResult);
    /*0x277*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAY_MUSIC, STATUS_NEVER);
    /*0x278*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAY_OBJECT_SOUND,
                                           STATUS_NEVER);
    /*0x279*/ DEFINE_HANDLER(CMSG_REQUEST_PET_INFO,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleRequestPetInfo);
    /*0x27A*/ DEFINE_HANDLER(CMSG_FAR_SIGHT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleFarSightOpcode);
    /*0x27B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLDISPELLOG, STATUS_NEVER);
    /*0x27C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DAMAGE_CALC_LOG, STATUS_NEVER);
    /*0x27D*/ DEFINE_HANDLER(CMSG_ENABLE_DAMAGE_LOG,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x27E*/ DEFINE_HANDLER(CMSG_GROUP_CHANGE_SUB_GROUP,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupChangeSubGroupOpcode);
    /*0x27F*/ DEFINE_HANDLER(
        CMSG_REQUEST_PARTY_MEMBER_STATS,
        STATUS_LOGGEDIN,
        PROCESS_THREADUNSAFE,
        &WorldSession::HandleRequestPartyMemberStatsOpcode);
    /*0x280*/ DEFINE_HANDLER(CMSG_GROUP_SWAP_SUB_GROUP,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupSwapSubGroupOpcode);
    /*0x281*/ DEFINE_HANDLER(CMSG_RESET_FACTION_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x282*/ DEFINE_HANDLER(CMSG_AUTOSTORE_BANK_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleAutoStoreBankItemOpcode);
    /*0x283*/ DEFINE_HANDLER(CMSG_AUTOBANK_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleAutoBankItemOpcode);
    /*0x284*/ DEFINE_HANDLER(MSG_QUERY_NEXT_MAIL_TIME,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleQueryNextMailTime);
    /*0x285*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RECEIVED_MAIL, STATUS_NEVER);
    /*0x286*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RAID_GROUP_ONLY, STATUS_NEVER);
    /*0x287*/ DEFINE_HANDLER(CMSG_SET_DURABILITY_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x288*/ DEFINE_HANDLER(CMSG_SET_PVP_RANK_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x289*/ DEFINE_HANDLER(CMSG_ADD_PVP_MEDAL_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x28A*/ DEFINE_HANDLER(CMSG_DEL_PVP_MEDAL_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x28B*/ DEFINE_HANDLER(CMSG_SET_PVP_TITLE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x28C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PVP_CREDIT, STATUS_NEVER);
    /*0x28D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUCTION_REMOVED_NOTIFICATION,
                                           STATUS_NEVER);
    /*0x28E*/ DEFINE_HANDLER(CMSG_GROUP_RAID_CONVERT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupRaidConvertOpcode);
    /*0x28F*/ DEFINE_HANDLER(CMSG_GROUP_ASSISTANT_LEADER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGroupAssistantLeaderOpcode);
    /*0x290*/ DEFINE_HANDLER(CMSG_BUYBACK_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleBuybackItem);
    /*0x291*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAT_SERVER_MESSAGE,
                                           STATUS_NEVER);
    /*0x292*/ DEFINE_HANDLER(CMSG_SET_SAVED_INSTANCE_EXTEND,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetSavedInstanceExtend);
    /*0x293*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_OFFER_CONTINUE,
                                           STATUS_NEVER);
    /*0x294*/ DEFINE_HANDLER(CMSG_TEST_DROP_RATE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x295*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TEST_DROP_RATE_RESULT,
                                           STATUS_NEVER);
    /*0x296*/ DEFINE_HANDLER(CMSG_LFG_GET_STATUS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLfgGetStatus);
    /*0x297*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SHOW_MAILBOX, STATUS_NEVER);
    /*0x298*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RESET_RANGED_COMBAT_TIMER,
                                           STATUS_NEVER);
    /*0x299*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAT_NOT_IN_PARTY,
                                           STATUS_NEVER);
    /*0x29A*/ DEFINE_SERVER_OPCODE_HANDLER(CMSG_GMTICKETSYSTEM_TOGGLE,
                                           STATUS_NEVER);
    /*0x29B*/ DEFINE_HANDLER(CMSG_CANCEL_GROWTH_AURA,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCancelGrowthAuraOpcode);
    /*0x29C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CANCEL_AUTO_REPEAT,
                                           STATUS_NEVER);
    /*0x29D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_STANDSTATE_UPDATE,
                                           STATUS_NEVER);
    /*0x29E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_ALL_PASSED, STATUS_NEVER);
    /*0x29F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_ROLL_WON, STATUS_NEVER);
    /*0x2A0*/ DEFINE_HANDLER(CMSG_LOOT_ROLL,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLootRoll);
    /*0x2A1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_START_ROLL, STATUS_NEVER);
    /*0x2A2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_ROLL, STATUS_NEVER);
    /*0x2A3*/ DEFINE_HANDLER(CMSG_LOOT_MASTER_GIVE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleLootMasterGiveOpcode);
    /*0x2A4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_MASTER_LIST, STATUS_NEVER);
    /*0x2A5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SET_FORCED_REACTIONS,
                                           STATUS_NEVER);
    /*0x2A6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELL_FAILED_OTHER,
                                           STATUS_NEVER);
    /*0x2A7*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GAMEOBJECT_RESET_STATE,
                                           STATUS_NEVER);
    /*0x2A8*/ DEFINE_HANDLER(CMSG_REPAIR_ITEM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleRepairItemOpcode);
    /*0x2A9*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAT_PLAYER_NOT_FOUND,
                                           STATUS_NEVER);
    /*0x2AA*/ DEFINE_HANDLER(MSG_TALENT_WIPE_CONFIRM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleTalentWipeConfirmOpcode);
    /*0x2AB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SUMMON_REQUEST, STATUS_NEVER);
    /*0x2AC*/ DEFINE_HANDLER(CMSG_SUMMON_RESPONSE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSummonResponseOpcode);
    /*0x2AD*/ DEFINE_HANDLER(MSG_DEV_SHOWLABEL,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2AE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MONSTER_MOVE_TRANSPORT,
                                           STATUS_NEVER);
    /*0x2AF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_BROKEN, STATUS_NEVER);
    /*0x2B0*/ DEFINE_HANDLER(MSG_MOVE_FEATHER_FALL,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2B1*/ DEFINE_HANDLER(MSG_MOVE_WATER_WALK,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2B2*/ DEFINE_HANDLER(CMSG_SERVER_BROADCAST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2B3*/ DEFINE_HANDLER(CMSG_SELF_RES,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleSelfResOpcode);
    /*0x2B4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FEIGN_DEATH_RESISTED,
                                           STATUS_NEVER);
    /*0x2B5*/ DEFINE_HANDLER(CMSG_RUN_SCRIPT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2B6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SCRIPT_MESSAGE, STATUS_NEVER);
    /*0x2B7*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DUEL_COUNTDOWN, STATUS_NEVER);
    /*0x2B8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AREA_TRIGGER_MESSAGE,
                                           STATUS_NEVER);
    /*0x2B9*/ DEFINE_HANDLER(CMSG_SHOWING_HELM,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleShowingHelmOpcode);
    /*0x2BA*/ DEFINE_HANDLER(CMSG_SHOWING_CLOAK,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleShowingCloakOpcode);
    /*0x2BB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_ROLE_CHOSEN, STATUS_NEVER);
    /*0x2BC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAYER_SKINNED, STATUS_NEVER);
    /*0x2BD*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DURABILITY_DAMAGE_DEATH,
                                           STATUS_NEVER);
    /*0x2BE*/ DEFINE_HANDLER(CMSG_SET_EXPLORATION,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2BF*/ DEFINE_HANDLER(CMSG_SET_ACTIONBAR_TOGGLES,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetActionBarToggles);
    /*0x2C0*/ DEFINE_HANDLER(UMSG_DELETE_GUILD_CHARTER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2C1*/ DEFINE_HANDLER(MSG_PETITION_RENAME,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandlePetitionRenameOpcode);
    /*0x2C2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INIT_WORLD_STATES,
                                           STATUS_NEVER);
    /*0x2C3*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_UPDATE_WORLD_STATE,
                                           STATUS_NEVER);
    /*0x2C4*/ DEFINE_HANDLER(CMSG_ITEM_NAME_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleItemNameQueryOpcode);
    /*0x2C5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ITEM_NAME_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x2C6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_ACTION_FEEDBACK,
                                           STATUS_NEVER);
    /*0x2C7*/ DEFINE_HANDLER(CMSG_CHAR_RENAME,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCharRenameOpcode);
    /*0x2C8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAR_RENAME, STATUS_NEVER);
    /*0x2C9*/ DEFINE_HANDLER(CMSG_MOVE_SPLINE_DONE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMoveSplineDoneOpcode);
    /*0x2CA*/ DEFINE_HANDLER(CMSG_MOVE_FALL_RESET,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x2CB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INSTANCE_SAVE_CREATED,
                                           STATUS_NEVER);
    /*0x2CC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RAID_INSTANCE_INFO,
                                           STATUS_NEVER);
    /*0x2CD*/ DEFINE_HANDLER(CMSG_REQUEST_RAID_INFO,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleRequestRaidInfoOpcode);
    /*0x2CE*/ DEFINE_HANDLER(CMSG_MOVE_TIME_SKIPPED,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMoveTimeSkippedOpcode);
    /*0x2CF*/ DEFINE_HANDLER(CMSG_MOVE_FEATHER_FALL_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleFeatherFallAck);
    /*0x2D0*/ DEFINE_HANDLER(CMSG_MOVE_WATER_WALK_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMoveWaterWalkAck);
    /*0x2D1*/ DEFINE_HANDLER(CMSG_MOVE_NOT_ACTIVE_MOVER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMoveNotActiveMover);
    /*0x2D2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAY_SOUND, STATUS_NEVER);
    /*0x2D3*/ DEFINE_HANDLER(CMSG_BATTLEFIELD_STATUS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBattlefieldStatusOpcode);
    /*0x2D4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEFIELD_STATUS,
                                           STATUS_NEVER);
    /*0x2D5*/ DEFINE_HANDLER(CMSG_BATTLEFIELD_PORT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBattleFieldPortOpcode);
    /*0x2D6*/ DEFINE_HANDLER(MSG_INSPECT_HONOR_STATS,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleInspectHonorStatsOpcode);
    /*0x2D7*/ DEFINE_HANDLER(CMSG_BATTLEMASTER_HELLO,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBattlemasterHelloOpcode);
    /*0x2D8*/ DEFINE_HANDLER(CMSG_MOVE_START_SWIM_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2D9*/ DEFINE_HANDLER(CMSG_MOVE_STOP_SWIM_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2DA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_WALK_SPEED_CHANGE,
                                           STATUS_NEVER);
    /*0x2DB*/ DEFINE_HANDLER(CMSG_FORCE_WALK_SPEED_CHANGE_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleForceSpeedChangeAck);
    /*0x2DC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_SWIM_BACK_SPEED_CHANGE,
                                           STATUS_NEVER);
    /*0x2DD*/ DEFINE_HANDLER(CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleForceSpeedChangeAck);
    /*0x2DE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_TURN_RATE_CHANGE,
                                           STATUS_NEVER);
    /*0x2DF*/ DEFINE_HANDLER(CMSG_FORCE_TURN_RATE_CHANGE_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleForceSpeedChangeAck);
    /*0x2E0*/ DEFINE_HANDLER(MSG_PVP_LOG_DATA,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandlePVPLogDataOpcode);
    /*0x2E1*/ DEFINE_HANDLER(CMSG_LEAVE_BATTLEFIELD,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBattlefieldLeaveOpcode);
    /*0x2E2*/ DEFINE_HANDLER(CMSG_AREA_SPIRIT_HEALER_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAreaSpiritHealerQueryOpcode);
    /*0x2E3*/ DEFINE_HANDLER(CMSG_AREA_SPIRIT_HEALER_QUEUE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAreaSpiritHealerQueueOpcode);
    /*0x2E4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AREA_SPIRIT_HEALER_TIME,
                                           STATUS_NEVER);
    /*0x2E5*/ DEFINE_HANDLER(CMSG_GM_UNTEACH,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2E6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_WARDEN_DATA, STATUS_NEVER);
    /*0x2E7*/ DEFINE_HANDLER(CMSG_WARDEN_DATA,
                             STATUS_AUTHED,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleWardenDataOpcode);
    /*0x2E8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GROUP_JOINED_BATTLEGROUND,
                                           STATUS_NEVER);
    /*0x2E9*/ DEFINE_HANDLER(
        MSG_BATTLEGROUND_PLAYER_POSITIONS,
        STATUS_LOGGEDIN,
        PROCESS_THREADUNSAFE,
        &WorldSession::HandleBattlegroundPlayerPositionsOpcode);
    /*0x2EA*/ DEFINE_HANDLER(CMSG_PET_STOP_ATTACK,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandlePetStopAttack);
    /*0x2EB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BINDER_CONFIRM, STATUS_NEVER);
    /*0x2EC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEGROUND_PLAYER_JOINED,
                                           STATUS_NEVER);
    /*0x2ED*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEGROUND_PLAYER_LEFT,
                                           STATUS_NEVER);
    /*0x2EE*/ DEFINE_HANDLER(CMSG_BATTLEMASTER_JOIN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBattlemasterJoinOpcode);
    /*0x2EF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ADDON_INFO, STATUS_NEVER);
    /*0x2F0*/ DEFINE_HANDLER(CMSG_PET_UNLEARN,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2F1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_UNLEARN_CONFIRM,
                                           STATUS_NEVER);
    /*0x2F2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PARTY_MEMBER_STATS_FULL,
                                           STATUS_NEVER);
    /*0x2F3*/ DEFINE_HANDLER(CMSG_PET_SPELL_AUTOCAST,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandlePetSpellAutocastOpcode);
    /*0x2F4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_WEATHER, STATUS_NEVER);
    /*0x2F5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAY_TIME_WARNING,
                                           STATUS_NEVER);
    /*0x2F6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MINIGAME_SETUP, STATUS_NEVER);
    /*0x2F7*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MINIGAME_STATE, STATUS_NEVER);
    /*0x2F8*/ DEFINE_HANDLER(CMSG_MINIGAME_MOVE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x2F9*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MINIGAME_MOVE_FAILED,
                                           STATUS_NEVER);
    /*0x2FA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RAID_INSTANCE_MESSAGE,
                                           STATUS_NEVER);
    /*0x2FB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMPRESSED_MOVES, STATUS_NEVER);
    /*0x2FC*/ DEFINE_HANDLER(CMSG_GUILD_INFO_TEXT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildChangeInfoTextOpcode);
    /*0x2FD*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAT_RESTRICTED, STATUS_NEVER);
    /*0x2FE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_SET_RUN_SPEED,
                                           STATUS_NEVER);
    /*0x2FF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_SET_RUN_BACK_SPEED,
                                           STATUS_NEVER);
    /*0x300*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_SET_SWIM_SPEED,
                                           STATUS_NEVER);
    /*0x301*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_SET_WALK_SPEED,
                                           STATUS_NEVER);
    /*0x302*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_SET_SWIM_BACK_SPEED,
                                           STATUS_NEVER);
    /*0x303*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_SET_TURN_RATE,
                                           STATUS_NEVER);
    /*0x304*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_UNROOT,
                                           STATUS_NEVER);
    /*0x305*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_FEATHER_FALL,
                                           STATUS_NEVER);
    /*0x306*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_NORMAL_FALL,
                                           STATUS_NEVER);
    /*0x307*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_SET_HOVER,
                                           STATUS_NEVER);
    /*0x308*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_UNSET_HOVER,
                                           STATUS_NEVER);
    /*0x309*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_WATER_WALK,
                                           STATUS_NEVER);
    /*0x30A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_LAND_WALK,
                                           STATUS_NEVER);
    /*0x30B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_START_SWIM,
                                           STATUS_NEVER);
    /*0x30C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_STOP_SWIM,
                                           STATUS_NEVER);
    /*0x30D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_SET_RUN_MODE,
                                           STATUS_NEVER);
    /*0x30E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_SET_WALK_MODE,
                                           STATUS_NEVER);
    /*0x30F*/ DEFINE_HANDLER(CMSG_GM_NUKE_ACCOUNT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x310*/ DEFINE_HANDLER(MSG_GM_DESTROY_CORPSE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x311*/ DEFINE_HANDLER(CMSG_GM_DESTROY_ONLINE_CORPSE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x312*/ DEFINE_HANDLER(CMSG_ACTIVATETAXIEXPRESS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleActivateTaxiExpressOpcode);
    /*0x313*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SET_FACTION_ATWAR,
                                           STATUS_NEVER);
    /*0x314*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GAMETIMEBIAS_SET, STATUS_NEVER);
    /*0x315*/ DEFINE_HANDLER(CMSG_DEBUG_ACTIONS_START,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x316*/ DEFINE_HANDLER(CMSG_DEBUG_ACTIONS_STOP,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x317*/ DEFINE_HANDLER(CMSG_SET_FACTION_INACTIVE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetFactionInactiveOpcode);
    /*0x318*/ DEFINE_HANDLER(CMSG_SET_WATCHED_FACTION,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetWatchedFactionOpcode);
    /*0x319*/ DEFINE_HANDLER(MSG_MOVE_TIME_SKIPPED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x31A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_ROOT, STATUS_NEVER);
    /*0x31B*/ DEFINE_HANDLER(CMSG_SET_EXPLORATION_ALL,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x31C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INVALIDATE_PLAYER,
                                           STATUS_NEVER);
    /*0x31D*/ DEFINE_HANDLER(CMSG_RESET_INSTANCES,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleResetInstancesOpcode);
    /*0x31E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INSTANCE_RESET, STATUS_NEVER);
    /*0x31F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INSTANCE_RESET_FAILED,
                                           STATUS_NEVER);
    /*0x320*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_UPDATE_LAST_INSTANCE,
                                           STATUS_NEVER);
    /*0x321*/ DEFINE_HANDLER(MSG_RAID_TARGET_UPDATE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleRaidTargetUpdateOpcode);
    /*0x322*/ DEFINE_HANDLER(MSG_RAID_READY_CHECK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleRaidReadyCheckOpcode);
    /*0x323*/ DEFINE_HANDLER(CMSG_LUA_USAGE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x324*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_ACTION_SOUND, STATUS_NEVER);
    /*0x325*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_DISMISS_SOUND,
                                           STATUS_NEVER);
    /*0x326*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GHOSTEE_GONE, STATUS_NEVER);
    /*0x327*/ DEFINE_HANDLER(CMSG_GM_UPDATE_TICKET_STATUS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x328*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GM_TICKET_STATUS_UPDATE,
                                           STATUS_NEVER);
    /*0x329*/ DEFINE_HANDLER(MSG_SET_DUNGEON_DIFFICULTY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetDungeonDifficultyOpcode);
    /*0x32A*/ DEFINE_HANDLER(CMSG_GMSURVEY_SUBMIT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGMSurveySubmit);
    /*0x32B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_UPDATE_INSTANCE_OWNERSHIP,
                                           STATUS_NEVER);
    /*0x32C*/ DEFINE_HANDLER(CMSG_IGNORE_KNOCKBACK_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x32D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAT_PLAYER_AMBIGUOUS,
                                           STATUS_NEVER);
    /*0x32E*/ DEFINE_HANDLER(MSG_DELAY_GHOST_TELEPORT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x32F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLINSTAKILLLOG,
                                           STATUS_NEVER);
    /*0x330*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELL_UPDATE_CHAIN_TARGETS,
                                           STATUS_NEVER);
    /*0x331*/ DEFINE_HANDLER(CMSG_CHAT_FILTERED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x332*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_EXPECTED_SPAM_RECORDS,
                                           STATUS_NEVER);
    /*0x333*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELLSTEALLOG, STATUS_NEVER);
    /*0x334*/ DEFINE_HANDLER(CMSG_LOTTERY_QUERY_OBSOLETE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x335*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOTTERY_QUERY_RESULT_OBSOLETE,
                                           STATUS_NEVER);
    /*0x336*/ DEFINE_HANDLER(CMSG_BUY_LOTTERY_TICKET_OBSOLETE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x337*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOTTERY_RESULT_OBSOLETE,
                                           STATUS_NEVER);
    /*0x338*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHARACTER_PROFILE,
                                           STATUS_NEVER);
    /*0x339*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_CHARACTER_PROFILE_REALM_CONNECTED, STATUS_NEVER);
    /*0x33A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DEFENSE_MESSAGE, STATUS_NEVER);
    /*0x33B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INSTANCE_DIFFICULTY,
                                           STATUS_NEVER);
    /*0x33C*/ DEFINE_HANDLER(MSG_GM_RESETINSTANCELIMIT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x33D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOTD, STATUS_NEVER);
    /*0x33E*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY, STATUS_NEVER);
    /*0x33F*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_MOVE_UNSET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY, STATUS_NEVER);
    /*0x340*/ DEFINE_HANDLER(
        CMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY_ACK,
        STATUS_NEVER,
        PROCESS_INPLACE,
        &WorldSession::Handle_NULL);
    /*0x341*/ DEFINE_HANDLER(MSG_MOVE_START_SWIM_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x342*/ DEFINE_HANDLER(MSG_MOVE_STOP_SWIM_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x343*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_SET_CAN_FLY, STATUS_NEVER);
    /*0x344*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_UNSET_CAN_FLY,
                                           STATUS_NEVER);
    /*0x345*/ DEFINE_HANDLER(CMSG_MOVE_SET_CAN_FLY_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMoveSetCanFlyAckOpcode);
    /*0x346*/ DEFINE_HANDLER(CMSG_MOVE_SET_FLY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x347*/ DEFINE_HANDLER(CMSG_SOCKET_GEMS,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleSocketOpcode);
    /*0x348*/ DEFINE_HANDLER(CMSG_ARENA_TEAM_CREATE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x349*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ARENA_TEAM_COMMAND_RESULT,
                                           STATUS_NEVER);
    /*0x34A*/ DEFINE_HANDLER(
        MSG_MOVE_UPDATE_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY,
        STATUS_NEVER,
        PROCESS_INPLACE,
        &WorldSession::Handle_NULL);
    /*0x34B*/ DEFINE_HANDLER(CMSG_ARENA_TEAM_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleArenaTeamQueryOpcode);
    /*0x34C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ARENA_TEAM_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x34D*/ DEFINE_HANDLER(CMSG_ARENA_TEAM_ROSTER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleArenaTeamRosterOpcode);
    /*0x34E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ARENA_TEAM_ROSTER,
                                           STATUS_NEVER);
    /*0x34F*/ DEFINE_HANDLER(CMSG_ARENA_TEAM_INVITE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleArenaTeamInviteOpcode);
    /*0x350*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ARENA_TEAM_INVITE,
                                           STATUS_NEVER);
    /*0x351*/ DEFINE_HANDLER(CMSG_ARENA_TEAM_ACCEPT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleArenaTeamAcceptOpcode);
    /*0x352*/ DEFINE_HANDLER(CMSG_ARENA_TEAM_DECLINE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleArenaTeamDeclineOpcode);
    /*0x353*/ DEFINE_HANDLER(CMSG_ARENA_TEAM_LEAVE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleArenaTeamLeaveOpcode);
    /*0x354*/ DEFINE_HANDLER(CMSG_ARENA_TEAM_REMOVE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleArenaTeamRemoveOpcode);
    /*0x355*/ DEFINE_HANDLER(CMSG_ARENA_TEAM_DISBAND,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleArenaTeamDisbandOpcode);
    /*0x356*/ DEFINE_HANDLER(CMSG_ARENA_TEAM_LEADER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleArenaTeamLeaderOpcode);
    /*0x357*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ARENA_TEAM_EVENT, STATUS_NEVER);
    /*0x358*/ DEFINE_HANDLER(CMSG_BATTLEMASTER_JOIN_ARENA,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBattlemasterJoinArena);
    /*0x359*/ DEFINE_HANDLER(MSG_MOVE_START_ASCEND,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x35A*/ DEFINE_HANDLER(MSG_MOVE_STOP_ASCEND,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x35B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ARENA_TEAM_STATS, STATUS_NEVER);
    /*0x35C*/ DEFINE_HANDLER(CMSG_LFG_JOIN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLfgJoinOpcode);
    /*0x35D*/ DEFINE_HANDLER(CMSG_LFG_LEAVE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLfgLeaveOpcode);
    /*0x35E*/ DEFINE_HANDLER(CMSG_SEARCH_LFG_JOIN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLfrSearchJoinOpcode);
    /*0x35F*/ DEFINE_HANDLER(CMSG_SEARCH_LFG_LEAVE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLfrSearchLeaveOpcode);
    /*0x360*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_UPDATE_LFG_LIST, STATUS_NEVER);
    /*0x361*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_PROPOSAL_UPDATE,
                                           STATUS_NEVER);
    /*0x362*/ DEFINE_HANDLER(CMSG_LFG_PROPOSAL_RESULT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLfgProposalResultOpcode);
    /*0x363*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_ROLE_CHECK_UPDATE,
                                           STATUS_NEVER);
    /*0x364*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_JOIN_RESULT, STATUS_NEVER);
    /*0x365*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_QUEUE_STATUS, STATUS_NEVER);
    /*0x366*/ DEFINE_HANDLER(CMSG_SET_LFG_COMMENT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLfgSetCommentOpcode);
    /*0x367*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_UPDATE_PLAYER,
                                           STATUS_NEVER);
    /*0x368*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_UPDATE_PARTY, STATUS_NEVER);
    /*0x369*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_UPDATE_SEARCH,
                                           STATUS_NEVER);
    /*0x36A*/ DEFINE_HANDLER(CMSG_LFG_SET_ROLES,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLfgSetRolesOpcode);
    /*0x36B*/ DEFINE_HANDLER(CMSG_LFG_SET_NEEDS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x36C*/ DEFINE_HANDLER(CMSG_LFG_SET_BOOT_VOTE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLfgSetBootVoteOpcode);
    /*0x36D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_BOOT_PROPOSAL_UPDATE,
                                           STATUS_NEVER);
    /*0x36E*/ DEFINE_HANDLER(
        CMSG_LFD_PLAYER_LOCK_INFO_REQUEST,
        STATUS_LOGGEDIN,
        PROCESS_THREADUNSAFE,
        &WorldSession::HandleLfgPlayerLockInfoRequestOpcode);
    /*0x36F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_PLAYER_INFO, STATUS_NEVER);
    /*0x370*/ DEFINE_HANDLER(CMSG_LFG_TELEPORT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleLfgTeleportOpcode);
    /*0x371*/ DEFINE_HANDLER(
        CMSG_LFD_PARTY_LOCK_INFO_REQUEST,
        STATUS_LOGGEDIN,
        PROCESS_THREADUNSAFE,
        &WorldSession::HandleLfgPartyLockInfoRequestOpcode);
    /*0x372*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_PARTY_INFO, STATUS_NEVER);
    /*0x373*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TITLE_EARNED, STATUS_NEVER);
    /*0x374*/ DEFINE_HANDLER(CMSG_SET_TITLE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleSetTitleOpcode);
    /*0x375*/ DEFINE_HANDLER(CMSG_CANCEL_MOUNT_AURA,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleCancelMountAuraOpcode);
    /*0x376*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ARENA_ERROR, STATUS_NEVER);
    /*0x377*/ DEFINE_HANDLER(MSG_INSPECT_ARENA_TEAMS,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleInspectArenaTeamsOpcode);
    /*0x378*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DEATH_RELEASE_LOC,
                                           STATUS_NEVER);
    /*0x379*/ DEFINE_HANDLER(CMSG_CANCEL_TEMP_ENCHANTMENT,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleCancelTempEnchantmentOpcode);
    /*0x37A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCED_DEATH_UPDATE,
                                           STATUS_NEVER);
    /*0x37B*/ DEFINE_HANDLER(CMSG_CHEAT_SET_HONOR_CURRENCY,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x37C*/ DEFINE_HANDLER(CMSG_CHEAT_SET_ARENA_CURRENCY,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x37D*/ DEFINE_HANDLER(MSG_MOVE_SET_FLIGHT_SPEED_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x37E*/ DEFINE_HANDLER(MSG_MOVE_SET_FLIGHT_SPEED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x37F*/ DEFINE_HANDLER(MSG_MOVE_SET_FLIGHT_BACK_SPEED_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x380*/ DEFINE_HANDLER(MSG_MOVE_SET_FLIGHT_BACK_SPEED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x381*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_FLIGHT_SPEED_CHANGE,
                                           STATUS_NEVER);
    /*0x382*/ DEFINE_HANDLER(CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleForceSpeedChangeAck);
    /*0x383*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE,
                                           STATUS_NEVER);
    /*0x384*/ DEFINE_HANDLER(CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleForceSpeedChangeAck);
    /*0x385*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_SET_FLIGHT_SPEED,
                                           STATUS_NEVER);
    /*0x386*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_SET_FLIGHT_BACK_SPEED,
                                           STATUS_NEVER);
    /*0x387*/ DEFINE_HANDLER(CMSG_MAELSTROM_INVALIDATE_CACHE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x388*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FLIGHT_SPLINE_SYNC,
                                           STATUS_NEVER);
    /*0x389*/ DEFINE_HANDLER(CMSG_SET_TAXI_BENCHMARK_MODE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetTaxiBenchmarkOpcode);
    /*0x38A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_JOINED_BATTLEGROUND_QUEUE,
                                           STATUS_NEVER);
    /*0x38B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_REALM_SPLIT, STATUS_NEVER);
    /*0x38C*/ DEFINE_HANDLER(CMSG_REALM_SPLIT,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleRealmSplitOpcode);
    /*0x38D*/ DEFINE_HANDLER(CMSG_MOVE_CHNG_TRANSPORT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x38E*/ DEFINE_HANDLER(MSG_PARTY_ASSIGNMENT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandlePartyAssignmentOpcode);
    /*0x38F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_OFFER_PETITION_ERROR,
                                           STATUS_NEVER);
    /*0x390*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TIME_SYNC_REQ, STATUS_NEVER);
    /*0x391*/ DEFINE_HANDLER(CMSG_TIME_SYNC_RESP,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleTimeSyncResp);
    /*0x392*/ DEFINE_HANDLER(CMSG_SEND_LOCAL_EVENT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x393*/ DEFINE_HANDLER(CMSG_SEND_GENERAL_TRIGGER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x394*/ DEFINE_HANDLER(CMSG_SEND_COMBAT_TRIGGER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x395*/ DEFINE_HANDLER(CMSG_MAELSTROM_GM_SENT_MAIL,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x396*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RESET_FAILED_NOTIFY,
                                           STATUS_NEVER);
    /*0x397*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_REAL_GROUP_UPDATE,
                                           STATUS_NEVER);
    /*0x398*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LFG_DISABLED, STATUS_NEVER);
    /*0x399*/ DEFINE_HANDLER(CMSG_ACTIVE_PVP_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x39A*/ DEFINE_HANDLER(CMSG_CHEAT_DUMP_ITEMS_DEBUG_ONLY,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x39B*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_CHEAT_DUMP_ITEMS_DEBUG_ONLY_RESPONSE, STATUS_NEVER);
    /*0x39C*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_CHEAT_DUMP_ITEMS_DEBUG_ONLY_RESPONSE_WRITE_FILE, STATUS_NEVER);
    /*0x39D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_UPDATE_COMBO_POINTS,
                                           STATUS_NEVER);
    /*0x39E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_VOICE_SESSION_ROSTER_UPDATE,
                                           STATUS_NEVER);
    /*0x39F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_VOICE_SESSION_LEAVE,
                                           STATUS_NEVER);
    /*0x3A0*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_VOICE_SESSION_ADJUST_PRIORITY,
                                           STATUS_NEVER);
    /*0x3A1*/ DEFINE_HANDLER(CMSG_VOICE_SET_TALKER_MUTED_REQUEST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3A2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_VOICE_SET_TALKER_MUTED,
                                           STATUS_NEVER);
    /*0x3A3*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INIT_EXTRA_AURA_INFO_OBSOLETE,
                                           STATUS_NEVER);
    /*0x3A4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SET_EXTRA_AURA_INFO_OBSOLETE,
                                           STATUS_NEVER);
    /*0x3A5*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_SET_EXTRA_AURA_INFO_NEED_UPDATE_OBSOLETE, STATUS_NEVER);
    /*0x3A6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CLEAR_EXTRA_AURA_INFO_OBSOLETE,
                                           STATUS_NEVER);
    /*0x3A7*/ DEFINE_HANDLER(MSG_MOVE_START_DESCEND,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleMovementOpcodes);
    /*0x3A8*/ DEFINE_HANDLER(CMSG_IGNORE_REQUIREMENTS_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3A9*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_IGNORE_REQUIREMENTS_CHEAT,
                                           STATUS_NEVER);
    /*0x3AA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELL_CHANCE_PROC_LOG,
                                           STATUS_NEVER);
    /*0x3AB*/ DEFINE_HANDLER(CMSG_MOVE_SET_RUN_SPEED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3AC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DISMOUNT, STATUS_NEVER);
    /*0x3AD*/ DEFINE_HANDLER(MSG_MOVE_UPDATE_CAN_FLY,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3AE*/ DEFINE_HANDLER(MSG_RAID_READY_CHECK_CONFIRM,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3AF*/ DEFINE_HANDLER(CMSG_VOICE_SESSION_ENABLE,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleVoiceSessionEnableOpcode);
    /*0x3B0*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_VOICE_SESSION_ENABLE,
                                           STATUS_NEVER);
    /*0x3B1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_VOICE_PARENTAL_CONTROLS,
                                           STATUS_NEVER);
    /*0x3B2*/ DEFINE_HANDLER(CMSG_GM_WHISPER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3B3*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GM_MESSAGECHAT, STATUS_NEVER);
    /*0x3B4*/ DEFINE_HANDLER(MSG_GM_GEARRATING,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3B5*/ DEFINE_HANDLER(CMSG_COMMENTATOR_ENABLE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3B6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMMENTATOR_STATE_CHANGED,
                                           STATUS_NEVER);
    /*0x3B7*/ DEFINE_HANDLER(CMSG_COMMENTATOR_GET_MAP_INFO,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3B8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMMENTATOR_MAP_INFO,
                                           STATUS_NEVER);
    /*0x3B9*/ DEFINE_HANDLER(CMSG_COMMENTATOR_GET_PLAYER_INFO,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3BA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMMENTATOR_GET_PLAYER_INFO,
                                           STATUS_NEVER);
    /*0x3BB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMMENTATOR_PLAYER_INFO,
                                           STATUS_NEVER);
    /*0x3BC*/ DEFINE_HANDLER(CMSG_COMMENTATOR_ENTER_INSTANCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3BD*/ DEFINE_HANDLER(CMSG_COMMENTATOR_EXIT_INSTANCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3BE*/ DEFINE_HANDLER(CMSG_COMMENTATOR_INSTANCE_COMMAND,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3BF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CLEAR_TARGET, STATUS_NEVER);
    /*0x3C0*/ DEFINE_HANDLER(CMSG_BOT_DETECTED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3C1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CROSSED_INEBRIATION_THRESHOLD,
                                           STATUS_NEVER);
    /*0x3C2*/ DEFINE_HANDLER(CMSG_CHEAT_PLAYER_LOGIN,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3C3*/ DEFINE_HANDLER(CMSG_CHEAT_PLAYER_LOOKUP,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3C4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHEAT_PLAYER_LOOKUP,
                                           STATUS_NEVER);
    /*0x3C5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_KICK_REASON, STATUS_NEVER);
    /*0x3C6*/ DEFINE_HANDLER(MSG_RAID_READY_CHECK_FINISHED,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleRaidReadyCheckFinishedOpcode);
    /*0x3C7*/ DEFINE_HANDLER(CMSG_COMPLAIN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleComplainOpcode);
    /*0x3C8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMPLAIN_RESULT, STATUS_NEVER);
    /*0x3C9*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FEATURE_SYSTEM_STATUS,
                                           STATUS_NEVER);
    /*0x3CA*/ DEFINE_HANDLER(CMSG_GM_SHOW_COMPLAINTS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3CB*/ DEFINE_HANDLER(CMSG_GM_UNSQUELCH,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3CC*/ DEFINE_HANDLER(CMSG_CHANNEL_SILENCE_VOICE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3CD*/ DEFINE_HANDLER(CMSG_CHANNEL_SILENCE_ALL,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3CE*/ DEFINE_HANDLER(CMSG_CHANNEL_UNSILENCE_VOICE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3CF*/ DEFINE_HANDLER(CMSG_CHANNEL_UNSILENCE_ALL,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3D0*/ DEFINE_HANDLER(CMSG_TARGET_CAST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3D1*/ DEFINE_HANDLER(CMSG_TARGET_SCRIPT_CAST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3D2*/ DEFINE_HANDLER(CMSG_CHANNEL_DISPLAY_LIST,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleChannelDisplayListQuery);
    /*0x3D3*/ DEFINE_HANDLER(CMSG_SET_ACTIVE_VOICE_CHANNEL,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetActiveVoiceChannel);
    /*0x3D4*/ DEFINE_HANDLER(CMSG_GET_CHANNEL_MEMBER_COUNT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleGetChannelMemberCount);
    /*0x3D5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHANNEL_MEMBER_COUNT,
                                           STATUS_NEVER);
    /*0x3D6*/ DEFINE_HANDLER(CMSG_CHANNEL_VOICE_ON,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleChannelVoiceOnOpcode);
    /*0x3D7*/ DEFINE_HANDLER(CMSG_CHANNEL_VOICE_OFF,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3D8*/ DEFINE_HANDLER(CMSG_DEBUG_LIST_TARGETS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3D9*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DEBUG_LIST_TARGETS,
                                           STATUS_NEVER);
    /*0x3DA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AVAILABLE_VOICE_CHANNEL,
                                           STATUS_NEVER);
    /*0x3DB*/ DEFINE_HANDLER(CMSG_ADD_VOICE_IGNORE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3DC*/ DEFINE_HANDLER(CMSG_DEL_VOICE_IGNORE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3DD*/ DEFINE_HANDLER(CMSG_PARTY_SILENCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3DE*/ DEFINE_HANDLER(CMSG_PARTY_UNSILENCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3DF*/ DEFINE_HANDLER(MSG_NOTIFY_PARTY_SQUELCH,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3E0*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMSAT_RECONNECT_TRY,
                                           STATUS_NEVER);
    /*0x3E1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMSAT_DISCONNECT,
                                           STATUS_NEVER);
    /*0x3E2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_COMSAT_CONNECT_FAIL,
                                           STATUS_NEVER);
    /*0x3E3*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_VOICE_CHAT_STATUS,
                                           STATUS_NEVER);
    /*0x3E4*/ DEFINE_HANDLER(CMSG_REPORT_PVP_AFK,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleReportPvPAFK);
    /*0x3E5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_REPORT_PVP_AFK_RESULT,
                                           STATUS_NEVER);
    /*0x3E6*/ DEFINE_HANDLER(CMSG_GUILD_BANKER_ACTIVATE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildBankerActivate);
    /*0x3E7*/ DEFINE_HANDLER(CMSG_GUILD_BANK_QUERY_TAB,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildBankQueryTab);
    /*0x3E8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GUILD_BANK_LIST, STATUS_NEVER);
    /*0x3E9*/ DEFINE_HANDLER(CMSG_GUILD_BANK_SWAP_ITEMS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildBankSwapItems);
    /*0x3EA*/ DEFINE_HANDLER(CMSG_GUILD_BANK_BUY_TAB,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildBankBuyTab);
    /*0x3EB*/ DEFINE_HANDLER(CMSG_GUILD_BANK_UPDATE_TAB,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildBankUpdateTab);
    /*0x3EC*/ DEFINE_HANDLER(CMSG_GUILD_BANK_DEPOSIT_MONEY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildBankDepositMoney);
    /*0x3ED*/ DEFINE_HANDLER(CMSG_GUILD_BANK_WITHDRAW_MONEY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildBankWithdrawMoney);
    /*0x3EE*/ DEFINE_HANDLER(MSG_GUILD_BANK_LOG_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildBankLogQuery);
    /*0x3EF*/ DEFINE_HANDLER(CMSG_SET_CHANNEL_WATCH,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetChannelWatch);
    /*0x3F0*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_USERLIST_ADD, STATUS_NEVER);
    /*0x3F1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_USERLIST_REMOVE, STATUS_NEVER);
    /*0x3F2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_USERLIST_UPDATE, STATUS_NEVER);
    /*0x3F3*/ DEFINE_HANDLER(CMSG_CLEAR_CHANNEL_WATCH,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleClearChannelWatch);
    /*0x3F4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INSPECT_TALENT, STATUS_NEVER);
    /*0x3F5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GOGOGO_OBSOLETE, STATUS_NEVER);
    /*0x3F6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ECHO_PARTY_SQUELCH,
                                           STATUS_NEVER);
    /*0x3F7*/ DEFINE_HANDLER(CMSG_SET_TITLE_SUFFIX,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3F8*/ DEFINE_HANDLER(CMSG_SPELLCLICK,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleSpellClick);
    /*0x3F9*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_LIST, STATUS_NEVER);
    /*0x3FA*/ DEFINE_HANDLER(CMSG_GM_CHARACTER_RESTORE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3FB*/ DEFINE_HANDLER(CMSG_GM_CHARACTER_SAVE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x3FC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_VOICESESSION_FULL,
                                           STATUS_NEVER);
    /*0x3FD*/ DEFINE_HANDLER(MSG_GUILD_PERMISSIONS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildPermissions);
    /*0x3FE*/ DEFINE_HANDLER(MSG_GUILD_BANK_MONEY_WITHDRAWN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildBankMoneyWithdrawn);
    /*0x3FF*/ DEFINE_HANDLER(MSG_GUILD_EVENT_LOG_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGuildEventLogQueryOpcode);
    /*0x400*/ DEFINE_HANDLER(CMSG_MAELSTROM_RENAME_GUILD,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x401*/ DEFINE_HANDLER(CMSG_GET_MIRRORIMAGE_DATA,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleMirrorImageDataRequest);
    /*0x402*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MIRRORIMAGE_DATA, STATUS_NEVER);
    /*0x403*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_DISPLAY_UPDATE,
                                           STATUS_NEVER);
    /*0x404*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPELL_CHANCE_RESIST_PUSHBACK,
                                           STATUS_NEVER);
    /*0x405*/ DEFINE_HANDLER(CMSG_IGNORE_DIMINISHING_RETURNS_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x406*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_IGNORE_DIMINISHING_RETURNS_CHEAT, STATUS_NEVER);
    /*0x407*/ DEFINE_HANDLER(CMSG_KEEP_ALIVE,
                             STATUS_NEVER,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::Handle_EarlyProccess);
    /*0x408*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RAID_READY_CHECK_ERROR,
                                           STATUS_NEVER);
    /*0x409*/ DEFINE_HANDLER(CMSG_OPT_OUT_OF_LOOT,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleOptOutOfLootOpcode);
    /*0x40A*/ DEFINE_HANDLER(MSG_QUERY_GUILD_BANK_TEXT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleQueryGuildBankTabText);
    /*0x40B*/ DEFINE_HANDLER(CMSG_SET_GUILD_BANK_TEXT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetGuildBankTabText);
    /*0x40C*/ DEFINE_HANDLER(CMSG_SET_GRANTABLE_LEVELS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x40D*/ DEFINE_HANDLER(CMSG_GRANT_LEVEL,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGrantLevel);
    /*0x40E*/ DEFINE_HANDLER(CMSG_REFER_A_FRIEND,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x40F*/ DEFINE_HANDLER(MSG_GM_CHANGE_ARENA_RATING,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x410*/ DEFINE_HANDLER(CMSG_DECLINE_CHANNEL_INVITE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleChannelDeclineInvite);
    /*0x411*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GROUPACTION_THROTTLED,
                                           STATUS_NEVER);
    /*0x412*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_OVERRIDE_LIGHT, STATUS_NEVER);
    /*0x413*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TOTEM_CREATED, STATUS_NEVER);
    /*0x414*/ DEFINE_HANDLER(CMSG_TOTEM_DESTROYED,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleTotemDestroyed);
    /*0x415*/ DEFINE_HANDLER(CMSG_EXPIRE_RAID_INSTANCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x416*/ DEFINE_HANDLER(CMSG_NO_SPELL_VARIANCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x417*/ DEFINE_HANDLER(
        CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY,
        STATUS_LOGGEDIN,
        PROCESS_THREADUNSAFE,
        &WorldSession::HandleQuestgiverStatusMultipleQuery);
    /*0x418*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTGIVER_STATUS_MULTIPLE,
                                           STATUS_NEVER);
    /*0x419*/ DEFINE_HANDLER(CMSG_SET_PLAYER_DECLINED_NAMES,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetPlayerDeclinedNames);
    /*0x41A*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_SET_PLAYER_DECLINED_NAMES_RESULT, STATUS_NEVER);
    /*0x41B*/ DEFINE_HANDLER(CMSG_QUERY_SERVER_BUCK_DATA,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x41C*/ DEFINE_HANDLER(CMSG_CLEAR_SERVER_BUCK_DATA,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x41D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SERVER_BUCK_DATA, STATUS_NEVER);
    /*0x41E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SEND_UNLEARN_SPELLS,
                                           STATUS_NEVER);
    /*0x41F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PROPOSE_LEVEL_GRANT,
                                           STATUS_NEVER);
    /*0x420*/ DEFINE_HANDLER(CMSG_ACCEPT_LEVEL_GRANT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAcceptGrantLevel);
    /*0x421*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_REFER_A_FRIEND_FAILURE,
                                           STATUS_NEVER);
    /*0x422*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_SET_FLYING,
                                           STATUS_NEVER);
    /*0x423*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_UNSET_FLYING,
                                           STATUS_NEVER);
    /*0x424*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SUMMON_CANCEL, STATUS_NEVER);
    /*0x425*/ DEFINE_HANDLER(CMSG_CHANGE_PERSONAL_ARENA_RATING,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x426*/ DEFINE_HANDLER(CMSG_ALTER_APPEARANCE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAlterAppearance);
    /*0x427*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ENABLE_BARBER_SHOP,
                                           STATUS_NEVER);
    /*0x428*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BARBER_SHOP_RESULT,
                                           STATUS_NEVER);
    /*0x429*/ DEFINE_HANDLER(CMSG_CALENDAR_GET_CALENDAR,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarGetCalendar);
    /*0x42A*/ DEFINE_HANDLER(CMSG_CALENDAR_GET_EVENT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarGetEvent);
    /*0x42B*/ DEFINE_HANDLER(CMSG_CALENDAR_GUILD_FILTER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarGuildFilter);
    /*0x42C*/ DEFINE_HANDLER(CMSG_CALENDAR_ARENA_TEAM,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarArenaTeam);
    /*0x42D*/ DEFINE_HANDLER(CMSG_CALENDAR_ADD_EVENT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarAddEvent);
    /*0x42E*/ DEFINE_HANDLER(CMSG_CALENDAR_UPDATE_EVENT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarUpdateEvent);
    /*0x42F*/ DEFINE_HANDLER(CMSG_CALENDAR_REMOVE_EVENT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarRemoveEvent);
    /*0x430*/ DEFINE_HANDLER(CMSG_CALENDAR_COPY_EVENT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarCopyEvent);
    /*0x431*/ DEFINE_HANDLER(CMSG_CALENDAR_EVENT_INVITE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarEventInvite);
    /*0x432*/ DEFINE_HANDLER(CMSG_CALENDAR_EVENT_RSVP,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarEventRsvp);
    /*0x433*/ DEFINE_HANDLER(CMSG_CALENDAR_EVENT_REMOVE_INVITE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarEventRemoveInvite);
    /*0x434*/ DEFINE_HANDLER(CMSG_CALENDAR_EVENT_STATUS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarEventStatus);
    /*0x435*/ DEFINE_HANDLER(CMSG_CALENDAR_EVENT_MODERATOR_STATUS,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarEventModeratorStatus);
    /*0x436*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_SEND_CALENDAR,
                                           STATUS_NEVER);
    /*0x437*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_SEND_EVENT,
                                           STATUS_NEVER);
    /*0x438*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_FILTER_GUILD,
                                           STATUS_NEVER);
    /*0x439*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_ARENA_TEAM,
                                           STATUS_NEVER);
    /*0x43A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_EVENT_INVITE,
                                           STATUS_NEVER);
    /*0x43B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_EVENT_INVITE_REMOVED,
                                           STATUS_NEVER);
    /*0x43C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_EVENT_STATUS,
                                           STATUS_NEVER);
    /*0x43D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_COMMAND_RESULT,
                                           STATUS_NEVER);
    /*0x43E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_RAID_LOCKOUT_ADDED,
                                           STATUS_NEVER);
    /*0x43F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_RAID_LOCKOUT_REMOVED,
                                           STATUS_NEVER);
    /*0x440*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_EVENT_INVITE_ALERT,
                                           STATUS_NEVER);
    /*0x441*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_CALENDAR_EVENT_INVITE_REMOVED_ALERT, STATUS_NEVER);
    /*0x442*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_CALENDAR_EVENT_INVITE_STATUS_ALERT, STATUS_NEVER);
    /*0x443*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_EVENT_REMOVED_ALERT,
                                           STATUS_NEVER);
    /*0x444*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_EVENT_UPDATED_ALERT,
                                           STATUS_NEVER);
    /*0x445*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_CALENDAR_EVENT_MODERATOR_STATUS_ALERT, STATUS_NEVER);
    /*0x446*/ DEFINE_HANDLER(CMSG_CALENDAR_COMPLAIN,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarComplain);
    /*0x447*/ DEFINE_HANDLER(CMSG_CALENDAR_GET_NUM_PENDING,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarGetNumPending);
    /*0x448*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_SEND_NUM_PENDING,
                                           STATUS_NEVER);
    /*0x449*/ DEFINE_HANDLER(CMSG_SAVE_DANCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x44A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_NOTIFY_DANCE, STATUS_NEVER);
    /*0x44B*/ DEFINE_HANDLER(CMSG_PLAY_DANCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x44C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAY_DANCE, STATUS_NEVER);
    /*0x44D*/ DEFINE_HANDLER(CMSG_LOAD_DANCES,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x44E*/ DEFINE_HANDLER(CMSG_STOP_DANCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x44F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_STOP_DANCE, STATUS_NEVER);
    /*0x450*/ DEFINE_HANDLER(CMSG_SYNC_DANCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x451*/ DEFINE_HANDLER(CMSG_DANCE_QUERY,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x452*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DANCE_QUERY_RESPONSE,
                                           STATUS_NEVER);
    /*0x453*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_INVALIDATE_DANCE, STATUS_NEVER);
    /*0x454*/ DEFINE_HANDLER(CMSG_DELETE_DANCE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x455*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LEARNED_DANCE_MOVES,
                                           STATUS_NEVER);
    /*0x456*/ DEFINE_HANDLER(CMSG_LEARN_DANCE_MOVE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x457*/ DEFINE_HANDLER(CMSG_UNLEARN_DANCE_MOVE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x458*/ DEFINE_HANDLER(CMSG_SET_RUNE_COUNT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x459*/ DEFINE_HANDLER(CMSG_SET_RUNE_COOLDOWN,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x45A*/ DEFINE_HANDLER(MSG_MOVE_SET_PITCH_RATE_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x45B*/ DEFINE_HANDLER(MSG_MOVE_SET_PITCH_RATE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x45C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_PITCH_RATE_CHANGE,
                                           STATUS_NEVER);
    /*0x45D*/ DEFINE_HANDLER(CMSG_FORCE_PITCH_RATE_CHANGE_ACK,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x45E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_SET_PITCH_RATE,
                                           STATUS_NEVER);
    /*0x45F*/ DEFINE_HANDLER(CMSG_CALENDAR_EVENT_INVITE_NOTES,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x460*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_EVENT_INVITE_NOTES,
                                           STATUS_NEVER);
    /*0x461*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_CALENDAR_EVENT_INVITE_NOTES_ALERT, STATUS_NEVER);
    /*0x462*/ DEFINE_HANDLER(CMSG_UPDATE_MISSILE_TRAJECTORY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleUpdateMissileTrajectory);
    /*0x463*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_UPDATE_ACCOUNT_DATA_COMPLETE,
                                           STATUS_NEVER);
    /*0x464*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TRIGGER_MOVIE, STATUS_NEVER);
    /*0x465*/ DEFINE_HANDLER(CMSG_COMPLETE_MOVIE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x466*/ DEFINE_HANDLER(CMSG_SET_GLYPH_SLOT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x467*/ DEFINE_HANDLER(CMSG_SET_GLYPH,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x468*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ACHIEVEMENT_EARNED,
                                           STATUS_NEVER);
    /*0x469*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DYNAMIC_DROP_ROLL_RESULT,
                                           STATUS_NEVER);
    /*0x46A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CRITERIA_UPDATE, STATUS_NEVER);
    /*0x46B*/ DEFINE_HANDLER(CMSG_QUERY_INSPECT_ACHIEVEMENTS,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQueryInspectAchievements);
    /*0x46C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RESPOND_INSPECT_ACHIEVEMENTS,
                                           STATUS_NEVER);
    /*0x46D*/ DEFINE_HANDLER(CMSG_DISMISS_CONTROLLED_VEHICLE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleDismissControlledVehicle);
    /*0x46E*/ DEFINE_HANDLER(CMSG_COMPLETE_ACHIEVEMENT_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x46F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUESTUPDATE_ADD_PVP_KILL,
                                           STATUS_NEVER);
    /*0x470*/ DEFINE_HANDLER(CMSG_SET_CRITERIA_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x471*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_RAID_LOCKOUT_UPDATED,
                                           STATUS_NEVER);
    /*0x472*/ DEFINE_HANDLER(CMSG_UNITANIMTIER_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x473*/ DEFINE_HANDLER(CMSG_CHAR_CUSTOMIZE,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCharCustomize);
    /*0x474*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAR_CUSTOMIZE, STATUS_NEVER);
    /*0x475*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_RENAMEABLE, STATUS_NEVER);
    /*0x476*/ DEFINE_HANDLER(CMSG_REQUEST_VEHICLE_EXIT,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleRequestVehicleExit);
    /*0x477*/ DEFINE_HANDLER(
        CMSG_REQUEST_VEHICLE_PREV_SEAT,
        STATUS_LOGGEDIN,
        PROCESS_INPLACE,
        &WorldSession::HandleChangeSeatsOnControlledVehicle);
    /*0x478*/ DEFINE_HANDLER(
        CMSG_REQUEST_VEHICLE_NEXT_SEAT,
        STATUS_LOGGEDIN,
        PROCESS_INPLACE,
        &WorldSession::HandleChangeSeatsOnControlledVehicle);
    /*0x479*/ DEFINE_HANDLER(
        CMSG_REQUEST_VEHICLE_SWITCH_SEAT,
        STATUS_LOGGEDIN,
        PROCESS_INPLACE,
        &WorldSession::HandleChangeSeatsOnControlledVehicle);
    /*0x47A*/ DEFINE_HANDLER(CMSG_PET_LEARN_TALENT,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandlePetLearnTalent);
    /*0x47B*/ DEFINE_HANDLER(CMSG_PET_UNLEARN_TALENTS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x47C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SET_PHASE_SHIFT, STATUS_NEVER);
    /*0x47D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ALL_ACHIEVEMENT_DATA,
                                           STATUS_NEVER);
    /*0x47E*/ DEFINE_HANDLER(CMSG_FORCE_SAY_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x47F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_HEALTH_UPDATE, STATUS_NEVER);
    /*0x480*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_POWER_UPDATE, STATUS_NEVER);
    /*0x481*/ DEFINE_HANDLER(CMSG_GAMEOBJ_REPORT_USE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleGameobjectReportUse);
    /*0x482*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_HIGHEST_THREAT_UPDATE,
                                           STATUS_NEVER);
    /*0x483*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_THREAT_UPDATE, STATUS_NEVER);
    /*0x484*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_THREAT_REMOVE, STATUS_NEVER);
    /*0x485*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_THREAT_CLEAR, STATUS_NEVER);
    /*0x486*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CONVERT_RUNE, STATUS_NEVER);
    /*0x487*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_RESYNC_RUNES, STATUS_NEVER);
    /*0x488*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ADD_RUNE_POWER, STATUS_NEVER);
    /*0x489*/ DEFINE_HANDLER(CMSG_START_QUEST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x48A*/ DEFINE_HANDLER(CMSG_REMOVE_GLYPH,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleRemoveGlyph);
    /*0x48B*/ DEFINE_HANDLER(CMSG_DUMP_OBJECTS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x48C*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DUMP_OBJECTS_DATA,
                                           STATUS_NEVER);
    /*0x48D*/ DEFINE_HANDLER(CMSG_DISMISS_CRITTER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleDismissCritter);
    /*0x48E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_NOTIFY_DEST_LOC_SPELL_CAST,
                                           STATUS_NEVER);
    /*0x48F*/ DEFINE_HANDLER(CMSG_AUCTION_LIST_PENDING_SALES,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleAuctionListPendingSales);
    /*0x490*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AUCTION_LIST_PENDING_SALES,
                                           STATUS_NEVER);
    /*0x491*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MODIFY_COOLDOWN, STATUS_NEVER);
    /*0x492*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_UPDATE_COMBO_POINTS,
                                           STATUS_NEVER);
    /*0x493*/ DEFINE_HANDLER(CMSG_ENABLETAXI,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleTaxiQueryAvailableNodes);
    /*0x494*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PRE_RESURRECT, STATUS_NEVER);
    /*0x495*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AURA_UPDATE_ALL, STATUS_NEVER);
    /*0x496*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AURA_UPDATE, STATUS_NEVER);
    /*0x497*/ DEFINE_HANDLER(CMSG_FLOOD_GRACE_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x498*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SERVER_FIRST_ACHIEVEMENT,
                                           STATUS_NEVER);
    /*0x499*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_LEARNED_SPELL,
                                           STATUS_NEVER);
    /*0x49A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_UNLEARNED_SPELL,
                                           STATUS_NEVER);
    /*0x49B*/ DEFINE_HANDLER(
        CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE,
        STATUS_LOGGEDIN,
        PROCESS_INPLACE,
        &WorldSession::HandleChangeSeatsOnControlledVehicle);
    /*0x49C*/ DEFINE_HANDLER(CMSG_HEARTH_AND_RESURRECT,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADSAFE,
                             &WorldSession::HandleHearthAndResurrect);
    /*0x49D*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_ON_CANCEL_EXPECTED_RIDE_VEHICLE_AURA, STATUS_NEVER);
    /*0x49E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CRITERIA_DELETED, STATUS_NEVER);
    /*0x49F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ACHIEVEMENT_DELETED,
                                           STATUS_NEVER);
    /*0x4A0*/ DEFINE_HANDLER(CMSG_SERVER_INFO_QUERY,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4A1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SERVER_INFO_RESPONSE,
                                           STATUS_NEVER);
    /*0x4A2*/ DEFINE_HANDLER(CMSG_CHECK_LOGIN_CRITERIA,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4A3*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SERVER_BUCK_DATA_START,
                                           STATUS_NEVER);
    /*0x4A4*/ DEFINE_HANDLER(CMSG_SET_BREATH,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4A5*/ DEFINE_HANDLER(CMSG_QUERY_VEHICLE_STATUS,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4A6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEGROUND_INFO_THROTTLED,
                                           STATUS_NEVER);
    /*0x4A7*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PLAYER_VEHICLE_DATA,
                                           STATUS_NEVER);
    /*0x4A8*/ DEFINE_HANDLER(CMSG_PLAYER_VEHICLE_ENTER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleEnterPlayerVehicle);
    /*0x4A9*/ DEFINE_HANDLER(CMSG_CONTROLLER_EJECT_PASSENGER,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleEjectPassenger);
    /*0x4AA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PET_GUIDS, STATUS_NEVER);
    /*0x4AB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CLIENTCACHE_VERSION,
                                           STATUS_NEVER);
    /*0x4AC*/ DEFINE_HANDLER(CMSG_CHANGE_GDF_ARENA_RATING,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4AD*/ DEFINE_HANDLER(CMSG_SET_ARENA_TEAM_RATING_BY_INDEX,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4AE*/ DEFINE_HANDLER(CMSG_SET_ARENA_TEAM_WEEKLY_GAMES,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4AF*/ DEFINE_HANDLER(CMSG_SET_ARENA_TEAM_SEASON_GAMES,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4B0*/ DEFINE_HANDLER(CMSG_SET_ARENA_MEMBER_WEEKLY_GAMES,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4B1*/ DEFINE_HANDLER(CMSG_SET_ARENA_MEMBER_SEASON_GAMES,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4B2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ITEM_REFUND_INFO_RESPONSE,
                                           STATUS_NEVER);
    /*0x4B3*/ DEFINE_HANDLER(CMSG_ITEM_REFUND_INFO,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleItemRefundInfoRequest);
    /*0x4B4*/ DEFINE_HANDLER(CMSG_ITEM_REFUND,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleItemRefund);
    /*0x4B5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ITEM_REFUND_RESULT,
                                           STATUS_NEVER);
    /*0x4B6*/ DEFINE_HANDLER(CMSG_CORPSE_MAP_POSITION_QUERY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCorpseMapPositionQuery);
    /*0x4B7*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE, STATUS_NEVER);
    /*0x4B8*/ DEFINE_HANDLER(CMSG_UNUSED5,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::Handle_NULL);
    /*0x4B9*/ DEFINE_HANDLER(CMSG_UNUSED6,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4BA*/ DEFINE_HANDLER(CMSG_CALENDAR_EVENT_SIGNUP,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCalendarEventSignup);
    /*0x4BB*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CALENDAR_CLEAR_PENDING_ACTION,
                                           STATUS_NEVER);
    /*0x4BC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_EQUIPMENT_SET_LIST,
                                           STATUS_NEVER);
    /*0x4BD*/ DEFINE_HANDLER(CMSG_EQUIPMENT_SET_SAVE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleEquipmentSetSave);
    /*0x4BE*/ DEFINE_HANDLER(CMSG_UPDATE_PROJECTILE_POSITION,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleUpdateProjectilePosition);
    /*0x4BF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SET_PROJECTILE_POSITION,
                                           STATUS_NEVER);
    /*0x4C0*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TALENTS_INFO, STATUS_NEVER);
    /*0x4C1*/ DEFINE_HANDLER(CMSG_LEARN_PREVIEW_TALENTS,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleLearnPreviewTalents);
    /*0x4C2*/ DEFINE_HANDLER(CMSG_LEARN_PREVIEW_TALENTS_PET,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleLearnPreviewTalentsPet);
    /*0x4C3*/ DEFINE_HANDLER(CMSG_SET_ACTIVE_TALENT_GROUP_OBSOLETE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4C4*/ DEFINE_HANDLER(CMSG_GM_GRANT_ACHIEVEMENT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4C5*/ DEFINE_HANDLER(CMSG_GM_REMOVE_ACHIEVEMENT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4C6*/ DEFINE_HANDLER(CMSG_GM_SET_CRITERIA_FOR_PLAYER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4C7*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ARENA_UNIT_DESTROYED,
                                           STATUS_NEVER);
    /*0x4C8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_ARENA_TEAM_CHANGE_FAILED_QUEUED,
                                           STATUS_NEVER);
    /*0x4C9*/ DEFINE_HANDLER(CMSG_PROFILEDATA_REQUEST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4CA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PROFILEDATA_RESPONSE,
                                           STATUS_NEVER);
    /*0x4CB*/ DEFINE_HANDLER(CMSG_START_BATTLEFIELD_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4CC*/ DEFINE_HANDLER(CMSG_END_BATTLEFIELD_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4CD*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MULTIPLE_PACKETS, STATUS_NEVER);
    /*0x4CE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_GRAVITY_DISABLE,
                                           STATUS_NEVER);
    /*0x4CF*/ DEFINE_HANDLER(CMSG_MOVE_GRAVITY_DISABLE_ACK,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4D0*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_GRAVITY_ENABLE,
                                           STATUS_NEVER);
    /*0x4D1*/ DEFINE_HANDLER(CMSG_MOVE_GRAVITY_ENABLE_ACK,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4D2*/ DEFINE_SERVER_OPCODE_HANDLER(MSG_MOVE_GRAVITY_CHNG, STATUS_NEVER);
    /*0x4D3*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_GRAVITY_DISABLE,
                                           STATUS_NEVER);
    /*0x4D4*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SPLINE_MOVE_GRAVITY_ENABLE,
                                           STATUS_NEVER);
    /*0x4D5*/ DEFINE_HANDLER(CMSG_EQUIPMENT_SET_USE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleEquipmentSetUse);
    /*0x4D6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_EQUIPMENT_SET_USE_RESULT,
                                           STATUS_NEVER);
    /*0x4D7*/ DEFINE_HANDLER(CMSG_FORCE_ANIM,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4D8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_ANIM, STATUS_NEVER);
    /*0x4D9*/ DEFINE_HANDLER(CMSG_CHAR_FACTION_CHANGE,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCharFactionOrRaceChange);
    /*0x4DA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CHAR_FACTION_CHANGE,
                                           STATUS_NEVER);
    /*0x4DB*/ DEFINE_HANDLER(CMSG_PVP_QUEUE_STATS_REQUEST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4DC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_PVP_QUEUE_STATS, STATUS_NEVER);
    /*0x4DD*/ DEFINE_HANDLER(CMSG_SET_PAID_SERVICE_CHEAT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4DE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEFIELD_MGR_ENTRY_INVITE,
                                           STATUS_NEVER);
    /*0x4DF*/ DEFINE_HANDLER(
        CMSG_BATTLEFIELD_MGR_ENTRY_INVITE_RESPONSE,
        STATUS_LOGGEDIN,
        PROCESS_THREADUNSAFE,
        &WorldSession::HandleBfEntryInviteResponse); // pussywizard: unsafe,
                                                     // changes groups and much
                                                     // more >_>
    /*0x4E0*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEFIELD_MGR_ENTERED,
                                           STATUS_NEVER);
    /*0x4E1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEFIELD_MGR_QUEUE_INVITE,
                                           STATUS_NEVER);
    /*0x4E2*/ DEFINE_HANDLER(CMSG_BATTLEFIELD_MGR_QUEUE_INVITE_RESPONSE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBfQueueInviteResponse);
    /*0x4E3*/ DEFINE_HANDLER(CMSG_BATTLEFIELD_MGR_QUEUE_REQUEST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4E4*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_BATTLEFIELD_MGR_QUEUE_REQUEST_RESPONSE, STATUS_NEVER);
    /*0x4E5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEFIELD_MGR_EJECT_PENDING,
                                           STATUS_NEVER);
    /*0x4E6*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEFIELD_MGR_EJECTED,
                                           STATUS_NEVER);
    /*0x4E7*/ DEFINE_HANDLER(CMSG_BATTLEFIELD_MGR_EXIT_REQUEST,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleBfExitRequest);
    /*0x4E8*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_BATTLEFIELD_MGR_STATE_CHANGE,
                                           STATUS_NEVER);
    /*0x4E9*/ DEFINE_HANDLER(CMSG_BATTLEFIELD_MANAGER_ADVANCE_STATE,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4EA*/ DEFINE_HANDLER(CMSG_BATTLEFIELD_MANAGER_SET_NEXT_TRANSITION_TIME,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4EB*/ DEFINE_HANDLER(MSG_SET_RAID_DIFFICULTY,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleSetRaidDifficultyOpcode);
    /*0x4EC*/ DEFINE_HANDLER(CMSG_TOGGLE_XP_GAIN,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4ED*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TOGGLE_XP_GAIN, STATUS_NEVER);
    /*0x4EE*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GMRESPONSE_DB_ERROR,
                                           STATUS_NEVER);
    /*0x4EF*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GMRESPONSE_RECEIVED,
                                           STATUS_NEVER);
    /*0x4F0*/ DEFINE_HANDLER(CMSG_GMRESPONSE_RESOLVE,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleGMResponseResolve);
    /*0x4F1*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GMRESPONSE_STATUS_UPDATE,
                                           STATUS_NEVER);
    /*0x4F2*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_GMRESPONSE_CREATE_TICKET,
                                           STATUS_NEVER);
    /*0x4F3*/ DEFINE_HANDLER(CMSG_GMRESPONSE_CREATE_TICKET,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4F4*/ DEFINE_HANDLER(CMSG_SERVERINFO,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4F5*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SERVERINFO, STATUS_NEVER);
    /*0x4F6*/ DEFINE_HANDLER(CMSG_WORLD_STATE_UI_TIMER_UPDATE,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleWorldStateUITimerUpdate);
    /*0x4F7*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_WORLD_STATE_UI_TIMER_UPDATE,
                                           STATUS_NEVER);
    /*0x4F8*/ DEFINE_HANDLER(CMSG_CHAR_RACE_CHANGE,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleCharFactionOrRaceChange);
    /*0x4F9*/ DEFINE_HANDLER(MSG_VIEW_PHASE_SHIFT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4FA*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_TALENTS_INVOLUNTARILY_RESET,
                                           STATUS_NEVER);
    /*0x4FB*/ DEFINE_HANDLER(CMSG_DEBUG_SERVER_GEO,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4FC*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_DEBUG_SERVER_GEO, STATUS_NEVER);
    /*0x4FD*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_LOOT_SLOT_CHANGED,
                                           STATUS_NEVER);
    /*0x4FE*/ DEFINE_HANDLER(UMSG_UPDATE_GROUP_INFO,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x4FF*/ DEFINE_HANDLER(CMSG_READY_FOR_ACCOUNT_DATA_TIMES,
                             STATUS_AUTHED,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleReadyForAccountDataTimes);
    /*0x500*/ DEFINE_HANDLER(CMSG_QUERY_QUESTS_COMPLETED,
                             STATUS_LOGGEDIN,
                             PROCESS_INPLACE,
                             &WorldSession::HandleQueryQuestsCompleted);
    /*0x501*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_QUERY_QUESTS_COMPLETED_RESPONSE,
                                           STATUS_NEVER);
    /*0x502*/ DEFINE_HANDLER(CMSG_GM_REPORT_LAG,
                             STATUS_LOGGEDIN,
                             PROCESS_THREADUNSAFE,
                             &WorldSession::HandleReportLag);
    /*0x503*/ DEFINE_HANDLER(CMSG_AFK_MONITOR_INFO_REQUEST,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x504*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_AFK_MONITOR_INFO_RESPONSE,
                                           STATUS_NEVER);
    /*0x505*/ DEFINE_HANDLER(CMSG_AFK_MONITOR_INFO_CLEAR,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x506*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CORPSE_NOT_IN_INSTANCE,
                                           STATUS_NEVER);
    /*0x507*/ DEFINE_HANDLER(CMSG_GM_NUKE_CHARACTER,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x508*/ DEFINE_HANDLER(CMSG_SET_ALLOW_LOW_LEVEL_RAID1,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x509*/ DEFINE_HANDLER(CMSG_SET_ALLOW_LOW_LEVEL_RAID2,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x50A*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_CAMERA_SHAKE, STATUS_NEVER);
    /*0x50B*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SOCKET_GEMS_RESULT,
                                           STATUS_NEVER);
    /*0x50C*/ DEFINE_HANDLER(CMSG_SET_CHARACTER_MODEL,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x50D*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_REDIRECT_CLIENT, STATUS_NEVER);
    /*0x50E*/ DEFINE_HANDLER(CMSG_REDIRECTION_FAILED,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x50F*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SUSPEND_COMMS, STATUS_NEVER);
    /*0x510*/ DEFINE_HANDLER(CMSG_SUSPEND_COMMS_ACK,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x511*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_FORCE_SEND_QUEUED_PACKETS,
                                           STATUS_NEVER);
    /*0x512*/ DEFINE_HANDLER(CMSG_REDIRECTION_AUTH_PROOF,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x513*/ DEFINE_HANDLER(CMSG_DROP_NEW_CONNECTION,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x514*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_SEND_ALL_COMBAT_LOG,
                                           STATUS_NEVER);
    /*0x515*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_OPEN_LFG_DUNGEON_FINDER,
                                           STATUS_NEVER);
    /*0x516*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MOVE_SET_COLLISION_HGT,
                                           STATUS_NEVER);
    /*0x517*/ DEFINE_HANDLER(CMSG_MOVE_SET_COLLISION_HGT_ACK,
                             STATUS_UNHANDLED,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x518*/ DEFINE_HANDLER(MSG_MOVE_SET_COLLISION_HGT,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x519*/ DEFINE_HANDLER(CMSG_CLEAR_RANDOM_BG_WIN_TIME,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x51A*/ DEFINE_HANDLER(CMSG_CLEAR_HOLIDAY_BG_WIN_TIME,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x51B*/ DEFINE_HANDLER(CMSG_COMMENTATOR_SKIRMISH_QUEUE_COMMAND,
                             STATUS_NEVER,
                             PROCESS_INPLACE,
                             &WorldSession::Handle_NULL);
    /*0x51C*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_COMMENTATOR_SKIRMISH_QUEUE_RESULT1, STATUS_NEVER);
    /*0x51D*/ DEFINE_SERVER_OPCODE_HANDLER(
        SMSG_COMMENTATOR_SKIRMISH_QUEUE_RESULT2, STATUS_NEVER);
    /*0x51E*/ DEFINE_SERVER_OPCODE_HANDLER(SMSG_MULTIPLE_MOVES, STATUS_NEVER);

#undef DEFINE_HANDLER
#undef DEFINE_SERVER_OPCODE_HANDLER
}

template <typename T>
inline std::string GetOpcodeNameForLoggingImpl(T id)
{
    uint16             opcode = uint16(id);
    std::ostringstream ss;
    ss << '[';

    if (static_cast<uint16>(id) < NUM_OPCODE_HANDLERS) {
        if (OpcodeHandler const* handler = opcodeTable[id])
            ss << handler->Name;
        else
            ss << "UNKNOWN OPCODE";
    }
    else
        ss << "INVALID OPCODE";

    ss << " 0x" << std::hex << std::setw(4) << std::setfill('0')
       << std::uppercase << opcode << std::nouppercase << std::dec << " ("
       << opcode << ")]";
    return ss.str();
}

std::string GetOpcodeNameForLogging(Opcodes opcode)
{
    return GetOpcodeNameForLoggingImpl(opcode);
}
