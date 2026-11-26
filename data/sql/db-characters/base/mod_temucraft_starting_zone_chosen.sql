-- mod-temucraft-starting-zone: characters that have chosen their starting zone (staying counts). They are never asked
-- again; .temucraft startzone reset <name> removes the row. Re-runnable.
CREATE TABLE IF NOT EXISTS `mod_temucraft_starting_zone_chosen` (
  `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
  `zone` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'the zone they began in',
  `chosen_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Clean-up for characters made while the menu kept coming back (PTR, before the fix): a character that is past level 1
-- or has played more than 10 minutes counts as having chosen where it stands; rows of deleted characters go.
CREATE TABLE IF NOT EXISTS `mod_temucraft_starting_zone_pending` (
  `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
INSERT IGNORE INTO `mod_temucraft_starting_zone_chosen` (`guid`, `zone`)
SELECT p.`guid`, c.`zone` FROM `mod_temucraft_starting_zone_pending` p JOIN `characters` c ON c.`guid` = p.`guid`
WHERE c.`level` > 1 OR c.`totaltime` > 600;
DELETE p FROM `mod_temucraft_starting_zone_pending` p JOIN `mod_temucraft_starting_zone_chosen` k ON k.`guid` = p.`guid`;
DELETE p FROM `mod_temucraft_starting_zone_pending` p LEFT JOIN `characters` c ON c.`guid` = p.`guid` WHERE c.`guid` IS NULL;
