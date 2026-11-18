-- mod-temucraft-starting-zone: player-facing texts. Translations go in module_string_locale (same module and id, plus the locale).
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-starting-zone';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-starting-zone', 1, 'Stay in '),
('mod-temucraft-starting-zone', 2, 'Begin in '),
('mod-temucraft-starting-zone', 3, '|cff66ccff[Start]|r You begin in {}. Your hearthstone is set there.'),
('mod-temucraft-starting-zone', 4, 'Only up to level {}.');
