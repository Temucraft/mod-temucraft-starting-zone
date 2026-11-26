# mod-temucraft-starting-zone

Begin in any race's starting zone. A few seconds after a new character first enters the world it is asked where to
begin: its own race's zone or another one of its faction (`Temucraft.StartingZone.AnyFaction` adds the other
faction's). The choice sets the hearthstone too. `.temucraft startzone` asks again up to level 5. Death knights keep their start.

The zones and spots come from `playercreateinfo`. The module's SQL opens the starting-zone quests to the whole
faction (quests for some races of one faction become open to all of it; old values in
`mod_temucraft_starting_zone_quest_backup`), so every zone has its quests for whoever begins there.

Each choice is named after the starting subzone (Valley of Trials, Deathknell, Camp Narache, Sunstrider Isle,
Northshire, Coldridge Valley, Shadowglen, Ammen Vale, and the subzone at the spot of any custom race's start), and the
one the character stands in comes first as "Stay in <subzone>" (no teleport).

How it asks:

- **The "Embark on your Journey" window** (Temucraft client, `TemucraftStartingZone.lua` in mod-temucraft-client-version):
  picture cards, a page per 6 zones; the click is the choice (no "Teleport to:" prompt). It talks over addon messages,
  prefix `TSZ`: the client says `HELLO` when it enters the world; the server sends `BEGIN~<n>`, then
  `Z~<index>~<here>~<map>~<zone>~<area>~<x>~<y>~<name>` per zone and `END`; the client answers `PICK~<index>` or `CLOSE`,
  and gets `DONE` after a pick.
- **The gossip menu** for a client that hasn't said `HELLO` within about 8 seconds (an old client, the patch missing).
  Each line that moves the character asks "Teleport to: <zone>" first (Accept / Cancel), from `TemucraftPlayerSettings.h`
  (same file as mod-temucraft-player-settings); players who turned "Ask before teleports and portals" off get it without the popup.

Once a choice is made (staying counts) the character is kept in `mod_temucraft_starting_zone_chosen` (characters DB)
and never asked again. With `Temucraft.StartingZone.Enforce` a character that hasn't chosen is asked again: a few
seconds after closing the window, every 45 seconds with the gossip menu, and at every login.

Commands (admin only by default, see the RBAC SQL): `.temucraft startzone` shows the choice again (up to MaxLevel);
`.temucraft startzone reset [name]` forgets a character's choice so it is asked again (now if online, else at its next login).

The characters SQL also cleans up characters from before the fix (the menu kept coming back): one that is past level 1
or has played more than 10 minutes counts as having chosen. It can be run again at any time.

C++ and SQL. Settings: `conf/mod-temucraft-starting-zone.conf.dist`.

## Texts and translations

The 6 player-facing texts (chat messages, gossip options) are in the `module_string` table under the module name
`mod-temucraft-starting-zone`, so they can be changed without rebuilding. Translations go in `module_string_locale` with the same
module and id plus the locale (`deDE`, `esES`, `frFR`, ...). If a row is missing the English text in the code is used.

## Shared headers

`TemucraftPlayerSettings.h`, `TemucraftModuleText.h`, `TemucraftAddonMessage.h` and `TemucraftLocked.h` in `src/` are shared with other Temucraft modules and must stay the same everywhere (all modules
share one include path). `_tools\check_shared_headers.py` checks the copies (`--fix` copies the canonical one over
them). The canonical `TemucraftPlayerSettings.h` is in mod-temucraft-player-settings, `TemucraftModuleText.h` is in mod-temucraft, `TemucraftAddonMessage.h` is in mod-temucraft-client-version, `TemucraftLocked.h` is in mod-temucraft-classes. Change the canonical copy, then run the check with `--fix`.

Part of the Temucraft modules for AzerothCore 3.3.5a.
