# mod-temucraft-starting-zone

Begin in any race's starting zone. A few seconds after a new character first enters the world it is asked where to
begin: its own race's zone or another one of its faction (`Temucraft.StartingZone.AnyFaction` adds the other
faction's). The choice sets the hearthstone too. `.startzone` asks again up to level 5. Death knights keep their start.

The zones and spots come from `playercreateinfo`. The module's SQL opens the starting-zone quests to the whole
faction (quests for some races of one faction become open to all of it; old values in
`mod_temucraft_starting_zone_quest_backup`), so every zone has its quests for whoever begins there.

C++ and SQL. Settings: `conf/mod-temucraft-starting-zone.conf.dist`.

## Texts and translations

The 4 player-facing texts (chat messages, gossip options) are in the `module_string` table under the module name
`mod-temucraft-starting-zone`, so they can be changed without rebuilding. Translations go in `module_string_locale` with the same
module and id plus the locale (`deDE`, `esES`, `frFR`, ...). If a row is missing the English text in the code is used.
