-- mod-temucraft-starting-zone: starting-zone quests open to the whole faction, so a character that begins in another
-- race's zone gets its quests too. Quests limited to some races of one faction become open to that faction.
-- The old masks are kept in mod_temucraft_starting_zone_quest_backup (first run only).
CREATE TABLE IF NOT EXISTS `mod_temucraft_starting_zone_quest_backup` (
  `ID` int unsigned NOT NULL, `AllowableRaces` int unsigned NOT NULL, PRIMARY KEY (`ID`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
INSERT IGNORE INTO `mod_temucraft_starting_zone_quest_backup` (`ID`, `AllowableRaces`)
SELECT `ID`, `AllowableRaces` FROM `quest_template`
WHERE `QuestSortID` IN (12, 1, 141, 3524, 14, 85, 215, 3430) AND `AllowableRaces` <> 0;
-- 1101 = every Alliance race, 690 = every Horde race
UPDATE `quest_template` q JOIN `mod_temucraft_starting_zone_quest_backup` b ON b.`ID` = q.`ID`
SET q.`AllowableRaces` = CASE WHEN (b.`AllowableRaces` & ~1101) = 0 THEN 1101 WHEN (b.`AllowableRaces` & ~690) = 0 THEN 690 ELSE b.`AllowableRaces` END;
