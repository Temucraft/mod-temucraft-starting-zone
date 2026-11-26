/*
 * mod-temucraft-starting-zone: start in any race's starting zone. A new character is asked, a few seconds after
 * first entering the world, where to begin: its own race's zone or another one (of its faction, unless
 * Temucraft.StartingZone.AnyFaction). The choice also sets the hearthstone. .temucraft startzone asks again while the
 * character is at most Temucraft.StartingZone.MaxLevel. The zones and spots come from playercreateinfo.
 * Death knights keep their own start. With Temucraft.StartingZone.Enforce the menu comes back until a zone is picked
 * (kept in mod_temucraft_starting_zone_pending, characters DB, so logging out doesn't skip it).
 */
#include "TemucraftLocked.h"
#include "Chat.h"
#include "TemucraftModuleText.h"
#include "TemucraftPlayerSettings.h"
#include "ChatCommand.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    constexpr uint32 START_MENU = 608500;

    struct StartSpot { uint8 race; uint32 map, zone; float x, y, z, o; TeamId team; std::string name; };

    constexpr uint32 ASK_AGAIN_MS = 6000;   // Enforce: the menu comes back this long after it was sent

    struct StartSettings { bool enable = true; bool anyFaction = false; uint32 maxLevel = 5; bool enforce = true; } scfg;
    std::vector<StartSpot> spots;                                  // one per zone
    Temucraft::LockedMap<ObjectGuid, uint32> pendingAsk;             // player -> ms until asked
    Temucraft::LockedSet<ObjectGuid> mustChoose;                     // Enforce: asked again until they pick

    void MarkPicked(Player* player)
    {
        mustChoose.erase(player->GetGUID());
        pendingAsk.erase(player->GetGUID());
        CharacterDatabase.Execute("DELETE FROM `mod_temucraft_starting_zone_pending` WHERE `guid` = {}", player->GetGUID().GetCounter());
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
                uint32 zone = f[2].Get<uint32>();
                if (!zones.insert(zone).second)
                    continue;
                uint8 race = f[0].Get<uint8>();
                AreaTableEntry const* area = sAreaTableStore.LookupEntry(zone);
                spots.push_back({ race, f[1].Get<uint32>(), zone, f[3].Get<float>(), f[4].Get<float>(), f[5].Get<float>(), f[6].Get<float>(),
                                  Player::TeamIdForRace(race), area ? area->area_name[LOCALE_enUS] : ("Zone " + std::to_string(zone)) });
            } while (r->NextRow());
        }
    }

    void SendMenu(Player* player)
    {
        ClearGossipMenuFor(player);
        for (size_t i = 0; i < spots.size(); ++i)
        {
            StartSpot const& s = spots[i];
            if (!scfg.anyFaction && s.team != player->GetTeamId())
                continue;
            bool home = s.zone == player->GetZoneId();
            // a line that moves the player asks "Teleport to: <zone>" first (unless he turned prompts off)
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, home ? TemucraftModuleText("mod-temucraft-starting-zone", player, 1, "Stay in " ) + s.name : TemucraftModuleText("mod-temucraft-starting-zone", player, 2, "Begin in " ) + s.name, GOSSIP_SENDER_MAIN, uint32(i),
                home ? std::string() : TemucraftTeleportBox(player, s.name), 0, false);
        }
        player->PlayerTalkClass->GetGossipMenu().SetMenuId(START_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, player->GetGUID());
    }
}

class TemucraftStartingZone_Player : public PlayerScript
{
public:
    TemucraftStartingZone_Player() : PlayerScript("TemucraftStartingZone_Player") { }

    void OnPlayerFirstLogin(Player* player) override
    {
        if (!scfg.enable || player->getClass() == CLASS_DEATH_KNIGHT)
            return;
        pendingAsk[player->GetGUID()] = 4000;
        if (scfg.enforce)
        {
            mustChoose.insert(player->GetGUID());
            CharacterDatabase.Execute("INSERT IGNORE INTO `mod_temucraft_starting_zone_pending` (`guid`) VALUES ({})", player->GetGUID().GetCounter());
        }
    }

    // Enforce: a character that logged out before picking is asked again
    void OnPlayerLogin(Player* player) override
    {
        if (!scfg.enable || !scfg.enforce || player->getClass() == CLASS_DEATH_KNIGHT || mustChoose.count(player->GetGUID()))
            return;
        if (!CharacterDatabase.Query("SELECT 1 FROM `mod_temucraft_starting_zone_pending` WHERE `guid` = {}", player->GetGUID().GetCounter()))
            return;
        if (player->GetLevel() > scfg.maxLevel)
        {
            MarkPicked(player);   // past the starting levels: nothing left to pick
            return;
        }
        mustChoose.insert(player->GetGUID());
        pendingAsk[player->GetGUID()] = 4000;
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
        pendingAsk.erase(itr);
        if (player->IsInWorld())
            SendMenu(player);
        // Enforce: closing the window doesn't skip it; it comes back until a zone is picked
        if (mustChoose.count(player->GetGUID()))
        {
            if (player->GetLevel() > scfg.maxLevel)
                MarkPicked(player);
            else
                pendingAsk[player->GetGUID()] = ASK_AGAIN_MS;
        }
    }

    void OnPlayerLogout(Player* player) override
    {
        pendingAsk.erase(player->GetGUID());
        mustChoose.erase(player->GetGUID());   // the DB row stays: asked again at the next login
    }

    void OnPlayerGossipSelect(Player* player, uint32 menuId, uint32 /*sender*/, uint32 action) override
    {
        if (menuId != START_MENU)
            return;
        CloseGossipMenuFor(player);
        if (action >= spots.size() || player->GetLevel() > scfg.maxLevel || player->IsInCombat())
            return;
        StartSpot const& s = spots[action];
        if (!scfg.anyFaction && s.team != player->GetTeamId())
            return;
        MarkPicked(player);
        if (s.zone == player->GetZoneId())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(TemucraftModuleText("mod-temucraft-starting-zone", player->GetSession(), 3, "|cff66ccff[Start]|r You begin in {}. Your hearthstone is set there."), s.name);
            return;
        }
        player->SetHomebind(WorldLocation(s.map, s.x, s.y, s.z, s.o), s.zone);
        player->TeleportTo(s.map, s.x, s.y, s.z, s.o);
        ChatHandler(player->GetSession()).PSendSysMessage(TemucraftModuleText("mod-temucraft-starting-zone", player->GetSession(), 3, "|cff66ccff[Start]|r You begin in {}. Your hearthstone is set there."), s.name);
    }
};

class TemucraftStartingZone_World : public WorldScript
{
public:
    TemucraftStartingZone_World() : WorldScript("TemucraftStartingZone_World") { }

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
            handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-starting-zone", handler, 4, "Only up to level {}."), scfg.maxLevel);
            return true;
        }
        SendMenu(player);
        return true;
    }
};

void AddTemucraftStartingZoneScripts()
{
    new TemucraftStartingZone_Player();
    new TemucraftStartingZone_World();
    new TemucraftStartingZone_Commands();
}
