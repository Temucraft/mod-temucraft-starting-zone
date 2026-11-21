-- mod-temucraft-starting-zone: its commands (all under .temucraft) as RBAC permissions 103250-103299. Every command is admin only by
-- default: linked to the administrator roles 192 and 1014. To give one to players, link it to 195 and 1010 here (or
-- grant it per account with .rbac account grant <account> <id>). "Temucraft: use ..." permissions are not
-- commands: they stay linked to the player roles so the items keep working.
DELETE FROM `rbac_linked_permissions` WHERE `linkedId` BETWEEN 103250 AND 103299;
DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 103250 AND 103299;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(103250, 'Command: temucraft startzone');
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(192, 103250),
(1014, 103250);
