-- mod-temucraft-starting-zone: player-facing texts. Translations go in module_string_locale (same module and id, plus the locale).
-- (The window's own texts, "Embark on your Journey" and the card names, come from the client and the server's zone names.)
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-starting-zone';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-starting-zone', 1, 'Stay in '),
('mod-temucraft-starting-zone', 2, 'Begin in '),
('mod-temucraft-starting-zone', 3, '|cff66ccff[Start]|r You begin in {}. Your hearthstone is set there.'),
('mod-temucraft-starting-zone', 4, 'Only up to level {}.'),
('mod-temucraft-starting-zone', 5, '|cff66ccff[Start]|r Not while in combat. You''ll be asked again in a moment.'),
('mod-temucraft-starting-zone', 6, '|cff66ccff[Start]|r {} will be asked for a starting zone again.');
