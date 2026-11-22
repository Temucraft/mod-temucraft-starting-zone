-- mod-temucraft-starting-zone: new characters that haven't picked their starting zone yet (with
-- Temucraft.StartingZone.Enforce the menu comes back until they pick, also after logging out).
CREATE TABLE IF NOT EXISTS `mod_temucraft_starting_zone_pending` (
  `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
