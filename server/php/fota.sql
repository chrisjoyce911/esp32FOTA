SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
START TRANSACTION;
SET time_zone = "+00:00";

--
-- Datenbank: `FOTA_firmware`
--

-- --------------------------------------------------------

--
-- Tabellenstruktur für Tabelle `devices`
--

CREATE TABLE IF NOT EXISTS `devices` (
  `id` int(8) NOT NULL AUTO_INCREMENT,
  `firmware_id` int(8) UNSIGNED DEFAULT NULL,
  `device_id` varchar(24) NOT NULL,
  `comment` varchar(250) DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `device_id` (`device_id`),
  KEY `firmware_id` (`firmware_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

--
-- RELATIONEN DER TABELLE `devices`:
--   `firmware_id`
--       `firmware` -> `firmware_id`
--

-- --------------------------------------------------------

--
-- Tabellenstruktur für Tabelle `firmware`
--

CREATE TABLE IF NOT EXISTS `firmware` (
  `firmware_id` int(8) UNSIGNED NOT NULL AUTO_INCREMENT,
  `version` int(3) UNSIGNED NOT NULL,
  `private` enum('0','1') NOT NULL DEFAULT '0',
  `firmware_type` enum('project1','project2','hobby_project','livingroom','workplace') NOT NULL DEFAULT 'hobby_project' COMMENT 'For convenience put your various project names here. Or change it to varchar.',
  `payload` mediumblob NOT NULL,
  `comment` varchar(200) DEFAULT NULL,
  PRIMARY KEY (`firmware_id`),
  UNIQUE KEY `version` (`version`,`private`,`firmware_type`),
  KEY `type` (`firmware_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

--
-- RELATIONEN DER TABELLE `firmware`:
--

--
-- Constraints der exportierten Tabellen
--

--
-- Constraints der Tabelle `devices`
--
ALTER TABLE `devices`
  ADD CONSTRAINT `devices_to_firmware` FOREIGN KEY (`firmware_id`) REFERENCES `firmware` (`firmware_id`) ON DELETE SET NULL ON UPDATE CASCADE;
COMMIT;