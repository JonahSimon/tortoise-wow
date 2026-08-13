-- ==============================================
-- FILE: playerbot_dock_hop_transport_entry.sql
-- GENERATED: 20260812120000
-- ==============================================
-- Dock hops were seeded with object = 0, forcing UseTransport() into a whole-map GO scan.
-- Copy the entry from the matching ride link on the transport node the hop touches.
-- See docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md section 3.4.
--
-- The shipped seed (src/modules/PlayerBots/sql/world/classic/ai_playerbot_travel_nodes.sql)
-- already carries these entries, and it DROPs and recreates the table, so this migration only
-- matters for a database that was seeded from an older copy of that file. It is also why the
-- migration must not fail when `ai_playerbot_travelnode_link` is absent: INSTALL-LINUX.md has the
-- migrations applied before the PlayerBots module's own SQL is imported (and a server built
-- without -DBUILD_PLAYERBOTS=ON never imports it at all), and a migration that errors there gets
-- recorded as applied and never retried. Guard on the table's existence so the statement is a
-- no-op instead, and let the corrected seed cover those installs.
--
-- Exactly one endpoint of a dock hop is a transport node, so joining on either side resolves at
-- most one ride link and the result does not depend on statement order. 12 of the 147 seeded
-- dock hops touch a transport node that has no ride link at all (single-stop elevators, and one
-- dock node that acquired a dock node of its own); those keep object = 0, which the movement code
-- still handles.

SET @has_travelnode_link := (
    SELECT COUNT(*) FROM `information_schema`.`TABLES`
    WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'ai_playerbot_travelnode_link'
);

SET @backfill_dock_hops := IF(@has_travelnode_link = 0, 'DO 0', '
UPDATE `ai_playerbot_travelnode_link` AS dock
JOIN (
    SELECT `node_id`, MAX(`object`) AS `entry`
    FROM `ai_playerbot_travelnode_link`
    WHERE `type` = 3 AND `object` <> 0
    GROUP BY `node_id`
) AS ride ON ride.`node_id` IN (dock.`node_id`, dock.`to_node_id`)
SET dock.`object` = ride.`entry`
WHERE dock.`type` = 3 AND dock.`object` = 0');

PREPARE backfill_dock_hops FROM @backfill_dock_hops;
EXECUTE backfill_dock_hops;
DEALLOCATE PREPARE backfill_dock_hops;
