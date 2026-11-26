/*
 * mod-temucraft-starting-zone: start in any race's starting zone. A new character is asked, a few seconds after
 * first entering the world, where to begin: where it stands ("Stay in Valley of Trials") or another race's starting
 * zone of its faction (any faction with Temucraft.StartingZone.AnyFaction). The choice also sets the hearthstone.
 * The zones and spots come from playercreateinfo; each is named after its starting subzone (Valley of Trials,
 * Deathknell, Camp Narache, Sunstrider Isle, Northshire, ...). Death knights keep their own start.
 *
 * Two ways to ask:
 *  - the "Embark on your Journey" window of the Temucraft client (TemucraftStartingZone.lua, prefix TSZ): the client
 *    says HELLO when it enters the world; a character whose client said HELLO gets the window (picture cards), the
 *    click is the choice (no "Teleport to:" prompt);
 *  - otherwise the gossip menu (no HELLO within a few seconds: an old client or the patch missing). Its lines that
 *    move the player ask "Teleport to: <zone>" first (unless the player turned prompts off).
 *
 * Once a choice is made (staying counts) the character is marked in mod_temucraft_starting_zone_chosen and never asked
 * again. .temucraft startzone shows the choice again (up to MaxLevel); .temucraft startzone reset [name] clears the
 * mark so the character is asked at its next login (GM).
 * With Temucraft.StartingZone.Enforce a character that hasn't chosen is asked again: after closing the window, every
 * GOSSIP_ASK_AGAIN_MS with the gossip menu, and at every login (mod_temucraft_starting_zone_pending).
 */
#include "TemucraftLocked.h"
#include "Chat.h"
#include "TemucraftAddonMessage.h"
#include "TemucraftModuleText.h"
#include "TemucraftPlayerSettings.h"
#include "ChatCommand.h"
#include "CharacterCache.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "MapMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    constexpr uint32 START_MENU = 608500;
    constexpr char const* PREFIX = "TSZ";
    constexpr char const* MOD = "mod-temucraft-starting-zone";

    constexpr uint32 FIRST_ASK_MS = 4000;          // after entering the world
    constexpr uint32 HELLO_WAIT_MS = 4000;         // no HELLO from the client yet: wait this long once more, then gossip
    constexpr uint32 WINDOW_ASK_AGAIN_MS = 6000;   // Enforce: the window comes back this long after it was closed
    constexpr uint32 GOSSIP_ASK_AGAIN_MS = 45000;  // Enforce: the gossip menu (we can't see it close) comes back

    struct StartSpot
    {
        uint8 race; uint32 map, zone, area; float x, y, z, o; TeamId team;
        std::string name;   // the starting subzone: "Valley of Trials", not "Durotar"
    };

    struct StartSettings { bool enable = true; bool anyFaction = false; uint32 maxLevel = 5; bool enforce = true; } scfg;
    std::vector<StartSpot> spots;                                  // one per zone (playercreateinfo)
    Temucraft::LockedMap<ObjectGuid, uint32> pendingAsk;             // player -> ms until asked
    Temucraft::LockedSet<ObjectGuid> mustChoose;                     // Enforce: asked again until they pick
    Temucraft::LockedSet<ObjectGuid> hasWindow;                      // the client said HELLO (TemucraftStartingZone.lua)
    Temucraft::LockedSet<ObjectGuid> waitedForHello;                 // waited once for the HELLO
    Temucraft::LockedSet<ObjectGuid> offered;                        // the window / menu is up: a pick is allowed
    Temucraft::LockedSet<uint32> chosen;                             // characters.guid that have chosen (also in the DB)

    // The starting subzone names of the stock zones (by the zone the spot is in), in case the map data at the spot
    // gives the big zone or a building inside the subzone.
    char const* StockSubzone(uint32 zone)
    {
        switch (zone)
        {
            case 14:   return "Valley of Trials";
            case 85:   return "Deathknell";
            case 215:  return "Camp Narache";
            case 3430: return "Sunstrider Isle";
            case 12:   return "Northshire";
            case 1:    return "Coldridge Valley";
            case 141:  return "Shadowglen";
            case 3524: return "Ammen Vale";
            default:   return nullptr;
        }
    }

    std::string AreaName(uint32 id)
    {
        AreaTableEntry const* a = id ? sAreaTableStore.LookupEntry(id) : nullptr;
        return a && a->area_name[LOCALE_enUS] && *a->area_name[LOCALE_enUS] ? a->area_name[LOCALE_enUS] : std::string();
    }

    bool IsChosen(Player* player) { return chosen.count(player->GetGUID().GetCounter()) != 0; }

    void MarkPicked(Player* player, uint32 zone)
    {
        ObjectGuid guid = player->GetGUID();
        mustChoose.erase(guid);
        pendingAsk.erase(guid);
        offered.erase(guid);
        chosen.insert(guid.GetCounter());
        CharacterDatabase.Execute("DELETE FROM `mod_temucraft_starting_zone_pending` WHERE `guid` = {}", guid.GetCounter());
        CharacterDatabase.Execute("REPLACE INTO `mod_temucraft_starting_zone_chosen` (`guid`, `zone`) VALUES ({}, {})", guid.GetCounter(), zone);
    }

    void LoadSpots()
    {
        spots.clear();
        std::unordered_set<uint32> zones;
        if (QueryResult r = WorldDatabase.Query("SELECT `race`, `map`, `zone`, `position_x`, `position_y`, `position_z`, `orientation` "
                                                "FROM `playercreateinfo` WHERE `class` <> 6 ORDER BY `race`, `class`"))
        {
            do
            {
                Field* f = r->Fetch();
                uint8 race = f[0].Get<uint8>();
                uint32 map = f[1].Get<uint32>(), dbZone = f[2].Get<uint32>();
                float x = f[3].Get<float>(), y = f[4].Get<float>(), z = f[5].Get<float>(), o = f[6].Get<float>();
                // the zone and subzone really at the spot (playercreateinfo.zone is sometimes the subzone: Sunstrider Isle)
                uint32 zone = 0, area = 0;
                if (sMapStore.LookupEntry(map))
                    sMapMgr->GetZoneAndAreaId(PHASEMASK_NORMAL, zone, area, map, x, y, z);
                if (!zone)
                {
                    AreaTableEntry const* e = sAreaTableStore.LookupEntry(dbZone);
                    zone = e && e->zone ? e->zone : dbZone;
                    area = dbZone;
                }
                if (!zones.insert(zone).second)
                    continue;
                std::string name;
                if (char const* stock = StockSubzone(zone))
                    name = stock;
                else if (area != zone && !AreaName(area).empty())
                    name = AreaName(area);
                else if (!AreaName(zone).empty())
                    name = AreaName(zone);
                else
                    name = "Zone " + std::to_string(zone);
                spots.push_back({ race, map, zone, area, x, y, z, o, Player::TeamIdForRace(race), name });
            } while (r->NextRow());
        }
    }

    bool Allowed(Player* player, StartSpot const& s) { return scfg.anyFaction || s.team == player->GetTeamId(); }

    bool IsHere(Player* player, StartSpot const& s, uint32 zone, uint32 area)
    {
        if (s.zone == zone || (s.area && s.area == area))
            return true;
        return player->GetMapId() == s.map && player->GetExactDist2d(s.x, s.y) < 400.0f;
    }

    // the zones this player may pick, the one he stands in first
    std::vector<std::pair<size_t, bool>> Offer(Player* player)
    {
        uint32 zone = 0, area = 0;
        player->GetZoneAndAreaId(zone, area);
        std::vector<std::pair<size_t, bool>> out;
        for (size_t i = 0; i < spots.size(); ++i)
            if (Allowed(player, spots[i]))
            {
                bool here = IsHere(player, spots[i], zone, area);
                if (here && (out.empty() || !out.front().second))
                    out.insert(out.begin(), { i, true });
                else
                    out.push_back({ i, false });
            }
        return out;
    }

    void SendGossip(Player* player)
    {
        ClearGossipMenuFor(player);
        for (auto const& [i, here] : Offer(player))
        {
            StartSpot const& s = spots[i];
            // a line that moves the player asks "Teleport to: <zone>" first (unless he turned prompts off)
            AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                std::string(here ? TemucraftModuleText(MOD, player, 1, "Stay in ") : TemucraftModuleText(MOD, player, 2, "Begin in ")) + s.name,
                GOSSIP_SENDER_MAIN, uint32(i), here ? std::string() : TemucraftTeleportBox(player, s.name), 0, false);
        }
        player->PlayerTalkClass->GetGossipMenu().SetMenuId(START_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, player->GetGUID());
    }

    // the window: BEGIN~<count>, Z~<index>~<here>~<map>~<zone>~<area>~<x>~<y>~<name> per zone, END
    void SendWindow(Player* player)
    {
        auto list = Offer(player);
        TemucraftAddonSend(player, PREFIX, "BEGIN~" + std::to_string(list.size()));
        for (auto const& [i, here] : list)
        {
            StartSpot const& s = spots[i];
            TemucraftAddonSend(player, PREFIX, Acore::StringFormat("Z~{}~{}~{}~{}~{}~{:.1f}~{:.1f}~{}", i, here ? 1 : 0, s.map, s.zone, s.area,
                s.x, s.y, TemucraftAddonClean(s.name)));
        }
        TemucraftAddonSend(player, PREFIX, "END");
    }

    // shows the choice (window or gossip); false = gossip was used
    bool Ask(Player* player)
    {
        offered.insert(player->GetGUID());
        if (hasWindow.count(player->GetGUID()))
        {
            SendWindow(player);
            return true;
        }
        SendGossip(player);
        return false;
    }

    void Teleport(Player* player, StartSpot const& s, bool here)
    {
        MarkPicked(player, s.zone);
        if (!here)
        {
            player->SetHomebind(WorldLocation(s.map, s.x, s.y, s.z, s.o), s.area ? s.area : s.zone);
            player->TeleportTo(s.map, s.x, s.y, s.z, s.o);
        }
        ChatHandler(player->GetSession()).PSendSysMessage(TemucraftModuleText(MOD, player->GetSession(), 3, "|cff66ccff[Start]|r You begin in {}. Your hearthstone is set there."), s.name);
    }

    // a pick from the window or the gossip menu
    void Pick(Player* player, uint32 action)
    {
        ObjectGuid guid = player->GetGUID();
        if (!offered.count(guid) || action >= spots.size())
            return;
        if (player->GetLevel() > scfg.maxLevel)
        {
            MarkPicked(player, player->GetZoneId());   // past the starting levels: nothing left to pick
            return;
        }
        StartSpot const& s = spots[action];
        if (!Allowed(player, s))
            return;
        uint32 zone = 0, area = 0;
        player->GetZoneAndAreaId(zone, area);
        bool here = IsHere(player, s, zone, area);
        if (!here && player->IsInCombat())
        {
            ChatHandler(player->GetSession()).SendSysMessage(TemucraftModuleText(MOD, player->GetSession(), 5, "|cff66ccff[Start]|r Not while in combat. You'll be asked again in a moment."));
            offered.erase(guid);
            if (!IsChosen(player))
                pendingAsk[guid] = WINDOW_ASK_AGAIN_MS;
            return;
        }
        if (hasWindow.count(guid))
            TemucraftAddonSend(player, PREFIX, "DONE");
        Teleport(player, s, here);
    }

    void ClearPlayer(ObjectGuid guid)
    {
        pendingAsk.erase(guid);
        mustChoose.erase(guid);
        hasWindow.erase(guid);
        waitedForHello.erase(guid);
        offered.erase(guid);
    }
}

class TemucraftStartingZone_Player : public PlayerScript
{
public:
    TemucraftStartingZone_Player() : PlayerScript("TemucraftStartingZone_Player",
        { PLAYERHOOK_ON_FIRST_LOGIN, PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_GOSSIP_SELECT,
          PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

    void OnPlayerFirstLogin(Player* player) override
    {
        if (!scfg.enable || player->getClass() == CLASS_DEATH_KNIGHT || IsChosen(player))
            return;
        pendingAsk[player->GetGUID()] = FIRST_ASK_MS;
        if (scfg.enforce)
        {
            mustChoose.insert(player->GetGUID());
            CharacterDatabase.Execute("INSERT IGNORE INTO `mod_temucraft_starting_zone_pending` (`guid`) VALUES ({})", player->GetGUID().GetCounter());
        }
    }

    // Enforce: a character that logged out before picking is asked again; one that has chosen never is
    void OnPlayerLogin(Player* player) override
    {
        uint32 low = player->GetGUID().GetCounter();
        if (!IsChosen(player))
            if (CharacterDatabase.Query("SELECT 1 FROM `mod_temucraft_starting_zone_chosen` WHERE `guid` = {}", low))
                chosen.insert(low);
        if (IsChosen(player))
        {
            ClearPlayer(player->GetGUID());
            CharacterDatabase.Execute("DELETE FROM `mod_temucraft_starting_zone_pending` WHERE `guid` = {}", low);
            return;
        }
        if (!scfg.enable || !scfg.enforce || player->getClass() == CLASS_DEATH_KNIGHT || mustChoose.count(player->GetGUID()))
            return;
        if (!CharacterDatabase.Query("SELECT 1 FROM `mod_temucraft_starting_zone_pending` WHERE `guid` = {}", low))
            return;
        if (player->GetLevel() > scfg.maxLevel)
        {
            MarkPicked(player, player->GetZoneId());   // past the starting levels: nothing left to pick
            return;
        }
        mustChoose.insert(player->GetGUID());
        pendingAsk[player->GetGUID()] = FIRST_ASK_MS;
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        auto itr = pendingAsk.find(player->GetGUID());
        if (itr == pendingAsk.end())
            return;
        if (itr->second > diff)
        {
            itr->second -= diff;
            return;
        }
        ObjectGuid guid = player->GetGUID();
        pendingAsk.erase(itr);
        if (!player->IsInWorld() || player->IsBeingTeleported())
        {
            pendingAsk[guid] = 1000;
            return;
        }
        if (IsChosen(player))   // chose meanwhile (or a GM's .temucraft startzone did): never asked again by itself
        {
            mustChoose.erase(guid);
            return;
        }
        if (player->GetLevel() > scfg.maxLevel)
        {
            if (mustChoose.count(guid))
                MarkPicked(player, player->GetZoneId());
            return;
        }
        // give the Temucraft client a moment to say HELLO before falling back to the gossip menu
        if (!hasWindow.count(guid) && !waitedForHello.count(guid))
        {
            waitedForHello.insert(guid);
            pendingAsk[guid] = HELLO_WAIT_MS;
            return;
        }
        bool window = Ask(player);
        // Enforce with the gossip menu: we can't see it closed, so it comes back after a while until a zone is picked.
        // (The window says CLOSE when it is closed, see OnPlayerCanUseChat.)
        if (!window && mustChoose.count(guid))
            pendingAsk[guid] = GOSSIP_ASK_AGAIN_MS;
    }

    void OnPlayerLogout(Player* player) override
    {
        ClearPlayer(player->GetGUID());   // the pending DB row stays: asked again at the next login
    }

    void OnPlayerGossipSelect(Player* player, uint32 menuId, uint32 /*sender*/, uint32 action) override
    {
        if (menuId != START_MENU)
            return;
        CloseGossipMenuFor(player);
        Pick(player, action);
    }

    using PlayerScript::OnPlayerCanUseChat;
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
    {
        std::string body;
        if (!TemucraftAddonParse(player, type, language, msg, receiver, PREFIX, body))
            return true;
        std::vector<std::string> a = TemucraftAddonFields(body);
        if (a.empty() || !scfg.enable)
            return false;
        ObjectGuid guid = player->GetGUID();
        if (a[0] == "HELLO")
        {
            hasWindow.insert(guid);
            // a /reload while the window was up: show it again
            if (offered.count(guid) && !pendingAsk.count(guid))
                pendingAsk[guid] = 1000;
            else if (pendingAsk.count(guid) && waitedForHello.count(guid))
                pendingAsk[guid] = 500;   // was waiting for this HELLO
        }
        else if (a[0] == "PICK" && a.size() >= 2)
        {
            if (Optional<uint32> idx = Acore::StringTo<uint32>(a[1]))
                Pick(player, *idx);
        }
        else if (a[0] == "CLOSE")
        {
            if (offered.count(guid))
            {
                offered.erase(guid);
                if (mustChoose.count(guid) && !IsChosen(player))
                    pendingAsk[guid] = WINDOW_ASK_AGAIN_MS;
            }
        }
        return false;
    }
};

class TemucraftStartingZone_World : public WorldScript
{
public:
    TemucraftStartingZone_World() : WorldScript("TemucraftStartingZone_World", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        scfg.enable = sConfigMgr->GetOption<bool>("Temucraft.StartingZone.Enable", true);
        scfg.anyFaction = sConfigMgr->GetOption<bool>("Temucraft.StartingZone.AnyFaction", false);
        scfg.maxLevel = sConfigMgr->GetOption<uint32>("Temucraft.StartingZone.MaxLevel", 5);
        scfg.enforce = sConfigMgr->GetOption<bool>("Temucraft.StartingZone.Enforce", true);
    }

    void OnStartup() override { LoadSpots(); }
};

class TemucraftStartingZone_Commands : public CommandScript
{
public:
    TemucraftStartingZone_Commands() : CommandScript("TemucraftStartingZone_Commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "temucraft startzone reset", HandleReset, 103251, Console::Yes },
            { "temucraft startzone", HandleStartZone, 103250, Console::No },
        };
        return commandTable;
    }

    static bool HandleStartZone(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!scfg.enable || player->getClass() == CLASS_DEATH_KNIGHT)
            return true;
        if (player->GetLevel() > scfg.maxLevel)
        {
            handler->PSendSysMessage(TemucraftModuleText(MOD, handler, 4, "Only up to level {}."), scfg.maxLevel);
            return true;
        }
        Ask(player);
        return true;
    }

    // GM: forget the character's choice; it is asked again at its next login (now, if it is online)
    static bool HandleReset(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!target)
            target = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!target)
        {
            handler->SendErrorMessage(LANG_PLAYER_NOT_FOUND);
            return false;
        }
        uint32 low = target->GetGUID().GetCounter();
        chosen.erase(low);
        CharacterDatabase.Execute("DELETE FROM `mod_temucraft_starting_zone_chosen` WHERE `guid` = {}", low);
        CharacterDatabase.Execute("INSERT IGNORE INTO `mod_temucraft_starting_zone_pending` (`guid`) VALUES ({})", low);
        if (Player* p = target->GetConnectedPlayer())
        {
            if (scfg.enforce)
                mustChoose.insert(p->GetGUID());
            pendingAsk[p->GetGUID()] = 1000;
        }
        handler->PSendSysMessage(TemucraftModuleText(MOD, handler, 6, "|cff66ccff[Start]|r {} will be asked for a starting zone again."), target->GetName());
        return true;
    }
};

void AddTemucraftStartingZoneScripts()
{
    new TemucraftStartingZone_Player();
    new TemucraftStartingZone_World();
    new TemucraftStartingZone_Commands();
}
