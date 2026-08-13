-- ==============================================
-- FILE: transport_animation.sql
-- GENERATED: 20260812120000
-- ==============================================
-- Server-side mirror of the client's local transport model animation, for
-- GAMEOBJECT_TYPE_TRANSPORT (type 11): elevators, lifts and the Deeprun Tram cars.
--
-- The 1.12 client animates these models itself on a fixed loop and reports its own
-- transport-relative position back to the server, so the server never had to know where they
-- are. A bot has no client, so nothing told the server where the car was and nothing carried
-- the bot along with it. LocalTransport (src/game/Transports/LocalTransport.cpp) mirrors the
-- loop from the keyframes below.
--
-- Retail shipped this as TransportAnim.dbc, which 1.12 client data does not have.
--
--   entry     gameobject_template.entry (type 11)
--   time_seg  ms into the loop; the highest time_seg closes the loop and is its length
--   x, y, z   offset from the object's spawn pose, in the object's local frame

DROP TABLE IF EXISTS `transport_animation`;
CREATE TABLE `transport_animation` (
  `entry` mediumint(8) unsigned NOT NULL,
  `time_seg` int(10) unsigned NOT NULL,
  `x` float NOT NULL DEFAULT 0,
  `y` float NOT NULL DEFAULT 0,
  `z` float NOT NULL DEFAULT 0,
  PRIMARY KEY (`entry`,`time_seg`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci ROW_FORMAT=DYNAMIC COMMENT='Client-side transport model animation, server mirror';

-- Where in the loop the *client* thinks a given entry is at server time 0. This cannot be
-- derived from static data: it has to be measured in-game once per entry, per
-- docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md (D3, "Calibration caveat"). Until an entry is
-- measured its offset is 0 and a bot rides a car that is out of phase with what players see.
--
-- To measure one: park a GM next to a car and, at the instant it reaches the end corresponding to
-- keyframe `time_seg` = t_end, log m = `WorldTimer::getMSTime() % TotalTime`. The server phase is
-- (getMSTime() + epoch_offset) % TotalTime, so store
--
--     epoch_offset = (t_end + TotalTime - m) % TotalTime
--
-- and NOT the raw m - that is only correct when t_end is 0, and otherwise leaves the entry
-- miscalibrated by t_end.
DROP TABLE IF EXISTS `transport_animation_phase`;
CREATE TABLE `transport_animation_phase` (
  `entry` mediumint(8) unsigned NOT NULL,
  `epoch_offset` int(10) unsigned NOT NULL DEFAULT 0 COMMENT 'ms into the loop at server time 0; measured in-game as (t_end + TotalTime - observed) % TotalTime',
  PRIMARY KEY (`entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci ROW_FORMAT=DYNAMIC COMMENT='Measured animation phase per transport_animation entry';

-- ---------------------------------------------------------------------------
-- Deeprun Tram cars (map 369, entries 176080-176085)
-- ---------------------------------------------------------------------------
-- Offsets are the recorded motion of a car in `ai_playerbot_travelnode_path` (28 samples,
-- world coordinates on map 369, captured on a build that did have TransportAnim.dbc), minus
-- the spawn pose, rotated into the local frame by the spawn orientation (1.5708 for every car,
-- so local +x is world +y and local +y is world -x).
--
-- One way takes 71.667 s (`ai_playerbot_travelnode_link`.`extra_cost`), so the loop is
-- 143334 ms: out and back, no dwell at the ends - the recorded samples do not carry one, and
-- the in-game calibration step above is what makes the loop line up with the client anyway.
-- Time is assigned in proportion to distance travelled, i.e. constant speed.
--
-- Three cars sit on the track at x = -45.4, parked at the Ironforge end and running towards
-- low y (176080, 176082, 176083); three sit at x = +4.5, parked at the Stormwind end and
-- running towards high y (176081, 176084, 176085). Same profile, opposite direction; each
-- car's offsets are relative to its own spawn, so the three cars on a track share one table.

INSERT INTO `transport_animation` (`entry`, `time_seg`, `x`, `y`, `z`) VALUES
(176080, 0, 0.0000, 0, 0.0000),
(176080, 4861, -170.8100, 0, -0.1073),
(176080, 5072, -178.1600, 0, -1.0149),
(176080, 8947, -308.5700, 0, -40.1721),
(176080, 9104, -314.0800, 0, -40.6213),
(176080, 15690, -545.5000, 0, -40.6788),
(176080, 15900, -552.8500, 0, -41.5043),
(176080, 19776, -683.2600, 0, -80.8189),
(176080, 19986, -690.6000, 0, -81.2348),
(176080, 26572, -922.0300, 0, -81.3230),
(176080, 26730, -927.5400, 0, -82.1531),
(176080, 30605, -1057.9500, 0, -121.3236),
(176080, 30815, -1065.2900, 0, -121.9646),
(176080, 39805, -1381.2100, 0, -121.8106),
(176080, 39963, -1386.7200, 0, -121.3716),
(176080, 43839, -1517.1260, 0, -81.9698),
(176080, 44049, -1524.4720, 0, -81.2522),
(176080, 50688, -1757.7350, 0, -81.0154),
(176080, 50846, -1763.2450, 0, -80.2285),
(176080, 54667, -1891.8150, 0, -41.5885),
(176080, 54877, -1899.1620, 0, -40.6784),
(176080, 61463, -2130.5880, 0, -40.6960),
(176080, 61674, -2137.9350, 0, -39.7825),
(176080, 65495, -2266.5050, 0, -1.0234),
(176080, 65706, -2273.8520, 0, -0.1154),
(176080, 71667, -2483.3199, 0, 0.1865),
(176080, 77628, -2273.8520, 0, -0.1154),
(176080, 77839, -2266.5050, 0, -1.0234),
(176080, 81660, -2137.9350, 0, -39.7825),
(176080, 81871, -2130.5880, 0, -40.6960),
(176080, 88457, -1899.1620, 0, -40.6784),
(176080, 88667, -1891.8150, 0, -41.5885),
(176080, 92488, -1763.2450, 0, -80.2285),
(176080, 92646, -1757.7350, 0, -81.0154),
(176080, 99285, -1524.4720, 0, -81.2522),
(176080, 99495, -1517.1260, 0, -81.9698),
(176080, 103371, -1386.7200, 0, -121.3716),
(176080, 103529, -1381.2100, 0, -121.8106),
(176080, 112519, -1065.2900, 0, -121.9646),
(176080, 112729, -1057.9500, 0, -121.3236),
(176080, 116604, -927.5400, 0, -82.1531),
(176080, 116762, -922.0300, 0, -81.3230),
(176080, 123348, -690.6000, 0, -81.2348),
(176080, 123558, -683.2600, 0, -80.8189),
(176080, 127434, -552.8500, 0, -41.5043),
(176080, 127644, -545.5000, 0, -40.6788),
(176080, 134230, -314.0800, 0, -40.6213),
(176080, 134387, -308.5700, 0, -40.1721),
(176080, 138262, -178.1600, 0, -1.0149),
(176080, 138473, -170.8100, 0, -0.1073),
(176080, 143334, 0.0000, 0, 0.0000);

-- The other two cars on the same track ride the same profile.
INSERT INTO `transport_animation` (`entry`, `time_seg`, `x`, `y`, `z`)
SELECT 176082, `time_seg`, `x`, `y`, `z` FROM `transport_animation` WHERE `entry` = 176080;
INSERT INTO `transport_animation` (`entry`, `time_seg`, `x`, `y`, `z`)
SELECT 176083, `time_seg`, `x`, `y`, `z` FROM `transport_animation` WHERE `entry` = 176080;

-- Track at x = +4.5: parked at the Stormwind end, runs towards high y.
INSERT INTO `transport_animation` (`entry`, `time_seg`, `x`, `y`, `z`) VALUES
(176081, 0, 0.0000, 0, 0.0000),
(176081, 5961, 209.4679, 0, -0.3019),
(176081, 6172, 216.8149, 0, -1.2100),
(176081, 9993, 345.3849, 0, -39.9690),
(176081, 10204, 352.7319, 0, -40.8825),
(176081, 16790, 584.1579, 0, -40.8649),
(176081, 17000, 591.5049, 0, -41.7750),
(176081, 20821, 720.0749, 0, -80.4150),
(176081, 20979, 725.5849, 0, -81.2019),
(176081, 27618, 958.8479, 0, -81.4387),
(176081, 27828, 966.1939, 0, -82.1563),
(176081, 31704, 1096.5999, 0, -121.5581),
(176081, 31862, 1102.1099, 0, -121.9971),
(176081, 40852, 1418.0299, 0, -122.1511),
(176081, 41062, 1425.3699, 0, -121.5101),
(176081, 44937, 1555.7799, 0, -82.3396),
(176081, 45095, 1561.2899, 0, -81.5095),
(176081, 51681, 1792.7199, 0, -81.4213),
(176081, 51891, 1800.0599, 0, -81.0054),
(176081, 55767, 1930.4699, 0, -41.6908),
(176081, 55977, 1937.8199, 0, -40.8653),
(176081, 62563, 2169.2399, 0, -40.8078),
(176081, 62720, 2174.7499, 0, -40.3586),
(176081, 66595, 2305.1599, 0, -1.2014),
(176081, 66806, 2312.5099, 0, -0.2938),
(176081, 71667, 2483.3199, 0, -0.1865),
(176081, 76528, 2312.5099, 0, -0.2938),
(176081, 76739, 2305.1599, 0, -1.2014),
(176081, 80614, 2174.7499, 0, -40.3586),
(176081, 80771, 2169.2399, 0, -40.8078),
(176081, 87357, 1937.8199, 0, -40.8653),
(176081, 87567, 1930.4699, 0, -41.6908),
(176081, 91443, 1800.0599, 0, -81.0054),
(176081, 91653, 1792.7199, 0, -81.4213),
(176081, 98239, 1561.2899, 0, -81.5095),
(176081, 98397, 1555.7799, 0, -82.3396),
(176081, 102272, 1425.3699, 0, -121.5101),
(176081, 102482, 1418.0299, 0, -122.1511),
(176081, 111472, 1102.1099, 0, -121.9971),
(176081, 111630, 1096.5999, 0, -121.5581),
(176081, 115506, 966.1939, 0, -82.1563),
(176081, 115716, 958.8479, 0, -81.4387),
(176081, 122355, 725.5849, 0, -81.2019),
(176081, 122513, 720.0749, 0, -80.4150),
(176081, 126334, 591.5049, 0, -41.7750),
(176081, 126544, 584.1579, 0, -40.8649),
(176081, 133130, 352.7319, 0, -40.8825),
(176081, 133341, 345.3849, 0, -39.9690),
(176081, 137162, 216.8149, 0, -1.2100),
(176081, 137373, 209.4679, 0, -0.3019),
(176081, 143334, 0.0000, 0, 0.0000);

INSERT INTO `transport_animation` (`entry`, `time_seg`, `x`, `y`, `z`)
SELECT 176084, `time_seg`, `x`, `y`, `z` FROM `transport_animation` WHERE `entry` = 176081;
INSERT INTO `transport_animation` (`entry`, `time_seg`, `x`, `y`, `z`)
SELECT 176085, `time_seg`, `x`, `y`, `z` FROM `transport_animation` WHERE `entry` = 176081;

-- ---------------------------------------------------------------------------
-- Elevators: Undercity, Thunder Bluff, the Great Lift
-- ---------------------------------------------------------------------------
-- The recorded samples for these collapse to their endpoints, so the run is taken from the pair
-- of `ai_playerbot_travelnode` heights on each shaft, as an offset from that entry's spawn pose
-- (which is one of the two ends, except 20649, whose spawn sits partway up its shaft).
--
-- The loop is `extra_cost` up plus `extra_cost` down, split as: dwell at the parked end, travel,
-- dwell at the far end, travel back. Travel takes the shorter of the two recorded costs - the
-- longer one is a ride plus a wait for the car to come back - and the remainder is the two
-- dwells. As with the tram, the phase is what the in-game calibration step fixes.
--
--   entry  location                     shaft            loop     travel
--   4170   Thunder Bluff, high shaft    130.08 -> 68.84  20000    5000
--   4171   Thunder Bluff, low shaft     68.59 -> 130.08  20000    5000
--   47296  Thunder Bluff, Spirit Rise   69.02 -> 140.50  20000    5000
--   47297  Thunder Bluff, Spirit Rise   140.50 -> 69.27  20000    5000
--   11898  Great Lift, top             85.41 -> -43.89   20000    5000
--   11899  Great Lift, bottom          -44.14 -> 85.41   20000    5000
--   20649  Undercity, mid shaft        14.68, -40.78 .. 55.72  15167  6833
--   20652  Undercity, bottom           -40.78 -> 55.72   16667    8167
--   20655  Undercity, bottom           -40.78 -> 55.72   16667    4834

INSERT INTO `transport_animation` (`entry`, `time_seg`, `x`, `y`, `z`) VALUES
-- Thunder Bluff, main mesa: 4170 starts at the top, 4171 at the bottom.
(4170,      0, 0, 0,    0.0000),
(4170,   5000, 0, 0,    0.0000),
(4170,  10000, 0, 0,  -61.2443),
(4170,  15000, 0, 0,  -61.2443),
(4170,  20000, 0, 0,    0.0000),
(4171,      0, 0, 0,    0.0000),
(4171,   5000, 0, 0,    0.0000),
(4171,  10000, 0, 0,   61.4982),
(4171,  15000, 0, 0,   61.4982),
(4171,  20000, 0, 0,    0.0000),
-- Thunder Bluff, Spirit Rise: 47296 starts at the bottom, 47297 at the top.
(47296,     0, 0, 0,    0.0000),
(47296,  5000, 0, 0,    0.0000),
(47296, 10000, 0, 0,   71.4764),
(47296, 15000, 0, 0,   71.4764),
(47296, 20000, 0, 0,    0.0000),
(47297,     0, 0, 0,    0.0000),
(47297,  5000, 0, 0,    0.0000),
(47297, 10000, 0, 0,  -71.2224),
(47297, 15000, 0, 0,  -71.2224),
(47297, 20000, 0, 0,    0.0000),
-- The Great Lift (Thousand Needles): 11898 starts at the top, 11899 at the bottom. The same
-- two entries are also spawned at the Shimmering Flats lift and on maps 47 and 209, parked at
-- the same ends, and the offsets are relative to each spawn, so one profile covers all of them.
(11898,     0, 0, 0,    0.0000),
(11898,  5000, 0, 0,    0.0000),
(11898, 10000, 0, 0, -129.2999),
(11898, 15000, 0, 0, -129.2999),
(11898, 20000, 0, 0,    0.0000),
(11899,     0, 0, 0,    0.0000),
(11899,  5000, 0, 0,    0.0000),
(11899, 10000, 0, 0,  129.5537),
(11899, 15000, 0, 0,  129.5537),
(11899, 20000, 0, 0,    0.0000),
-- Undercity. 20649 is spawned partway up its shaft, so both ends are a signed offset from it.
(20649,     0, 0, 0,  -55.4658),
(20649,   750, 0, 0,  -55.4658),
(20649,  7583, 0, 0,   41.0356),
(20649,  8334, 0, 0,   41.0356),
(20649, 15167, 0, 0,  -55.4658),
(20652,     0, 0, 0,    0.0000),
(20652,   166, 0, 0,    0.0000),
(20652,  8333, 0, 0,   96.5014),
(20652,  8500, 0, 0,   96.5014),
(20652, 16667, 0, 0,    0.0000),
(20655,     0, 0, 0,    0.0000),
(20655,  3499, 0, 0,    0.0000),
(20655,  8333, 0, 0,   96.5014),
(20655, 11833, 0, 0,   96.5014),
(20655, 16667, 0, 0,    0.0000);

-- Every seeded entry starts uncalibrated. Measure each one in-game and UPDATE its epoch_offset
-- using the formula in this table's header comment - not the raw logged value; until then a
-- bot-ridden car is in the right place along the right path, at the wrong time.
INSERT INTO `transport_animation_phase` (`entry`, `epoch_offset`) VALUES
(176080, 0), (176081, 0), (176082, 0), (176083, 0), (176084, 0), (176085, 0),
(4170, 0), (4171, 0), (47296, 0), (47297, 0), (11898, 0), (11899, 0),
(20649, 0), (20652, 0), (20655, 0);
