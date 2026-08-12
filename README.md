
# Tortoise-WoW

This is an unofficial, community driven, restoration of the 1.18.1 patch of Turtle-WoW, with some additions for solo play.  
This project is not to be used for profit or to misrepresent itself, or anyone using it, as the original creators  
This project targets version 1.18.1 build 7272

## About this fork

**ChrisMiho's fork of [Shyalya/tortoise-wow](https://github.com/Shyalya/tortoise-wow)**
(branch `playerbots-integration-gh`), which itself carried
[r-o-sh's playerbot integration](https://github.com/r-o-sh/tortoise-wow/tree/playerbots-integration-gh)
— vendoring [ike3's playerbots][20] — onto Penqle/tortoise-wow's 1.18.1 restoration. This
fork runs a small private server with **~1000 playerbots** permanently online. Upstream
(Shyalya) is merged in periodically; everything below is what this fork adds on top. See
[`docs/BRANCHING.md`](docs/BRANCHING.md) for the two-branch layout and how the sync works.

**This is a personal experiment, not a something I plan to formally release.** It exists to
run one private server and to see how a thousand-bot AI holds up under real, sustained
load — the fixes below exist because something broke in play and got traced back to its
cause, which is why the commit messages read like bug reports rather than feature notes.
If any of it is useful to you, take it.

### Quickstart with Docker

The whole stack (`realmd`, `mangosd`, `mariadb`) runs in Docker; full detail, log-growth
caps, and the rebuild/rollback flow are in [`docs/DOCKER.md`](docs/DOCKER.md), but the
short version, run from **inside WSL Ubuntu** (Docker Desktop's engine is shared, but
bind-mount paths only resolve correctly from the WSL side):

```bash
cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow
cp .env.example .env        # fill in DB_PASS and the TW_DATA/TW_ETC/TW_LOGS paths
docker compose up -d        # start realmd + mangosd + mariadb
docker compose down         # stop them — NEVER add -v, it deletes the character database
```

A C++ change needs `./scripts/rebuild.sh` (~40 minutes) before `docker compose up -d`
picks it up — see `docs/DOCKER.md` for the full rebuild/verify/rollback cycle.

### Playerbots

Vendored under `src/modules/PlayerBots/`. Build with `-DBUILD_PLAYERBOTS=ON` (defaults
`OFF`, no warning if you forget); activation is gated by `AiPlayerbot.Enabled`.

Running ~1000 of them permanently surfaces load a few dozen players never reach — stale
cached pointers, an unsynchronised battleground queue, navmesh tiles unloaded mid-query.
The fixes cluster around a few themes:

- **Battlegrounds & dungeon finder** — bots that never queued or entered, dropped the flag
  on a PvP trinket, filled the wrong role, or sat at a 125-yard heal range (3x the real
  cap) instead of actually healing.
- **Stability under load** — a `recursive_mutex` left commented out during an ACE
  migration let a thousand bots queueing in parallel tear apart a `std::map`; a null
  anticheat pointer was dereferenced on every bot session; a bot name containing `%` was
  passed straight into `vfprintf` as a format string and aborted the server.
- **Hot-path performance** — `Engine::Init()` was rebuilding every strategy's triggers once
  per strategy in a list instead of once per change: 105 million trigger initialisations an
  hour, ~29,000/sec, inside 4.4 billion allocations.
- **Class/spec correctness** — druids never learned bear form, warriors specced 125 fury
  to 35 protection against a configured 50:50 (and protection warriors are the tank
  supply on a bot realm), talent links rejected by Turtle's reworked trees.

Each one started as something that broke in play and got traced back to its cause — the
full list, with file and line references, is in the git log.

### How the bot AI works

This is the actual focus of the experiment, so it's worth explaining rather than just
linking out. Full depth — line references, the two bugs found and fixed while filling a
party with bots, tier-by-tier config examples — lives in
[`docs/playerbots/PLAYERBOT-AI-HANDOFF.md`](docs/playerbots/PLAYERBOT-AI-HANDOFF.md); this
is the summary.

Each bot is a **priority-queue reactive planner**, not a behaviour tree or a state
machine. A `PlayerbotAI` holds one `Engine` per `BotState` (combat, non-combat, dead,
reaction), each with its own strategies and actions. A **strategy** contributes trigger
nodes of the form `trigger > action!priority`; every tick, every trigger in the active
engine is evaluated, firing triggers push their actions into a priority queue, a
**multiplier** pass scales or vetoes priorities for cross-cutting rules ("don't cast while
silenced", "healing outranks damage"), and the highest-priority action that reports itself
executable runs. **Values** are the ~195 lazily-evaluated, cached world-state accessors
("current target", "party member to heal") that both triggers and actions read, so state
is computed once per tick rather than once per consumer. Strategies toggle on and off by
name with `+`/`-` in `aiplayerbot.conf`:

```
AiPlayerbot.CombatStrategies    = +custom::say,+dps,+dps assist,-threat
AiPlayerbot.NonCombatStrategies = +grind,+loot,+custom::say,+return,+delayed roll,+tfish,+wander,+rpg craft
```

Three ways to change bot behaviour, in order of cost:

1. **Config** (`aiplayerbot.conf`) — live-reloadable with `.rndbot reload`, no restart.
2. **`custom::<name>` strategies** — `trigger>action!priority` lines stored in the
   `ai_playerbot_custom_strategy` table, composed from the existing ~300 actions and ~48
   triggers entirely in SQL, applied globally (`owner = 0`) or to one bot. Needs a bot AI
   reset, not a rebuild.
3. **C++** — adding a command is a small change (one handler entry plus one function), but
   the real cost is the ~30-minute image rebuild, not the code.

Spawning bots to test against: `.rndbot group login=1` fills a role-balanced party at your
level and logs everyone in (the bare command without `login=1` creates the bots but never
brings them online — see the handoff doc's §8 for why). `.rndbot` always creates from the
shared `RNDBOT` pool; `.bot` eats your own 9 character slots, so prefer `.rndbot` for
anything disposable.

### Server features

All off by default, all in `mangosd.conf`:

| Feature | Config keys | Also required |
|---|---|---|
| Zone-restricted world buffs on a timer | `AutoWorldBuff.*` | – |
| Hourly donation points | `AutoDonationPoints.*` | `sql/logon/donation_point_progress.sql` on the **login** database |
| Beginners guild for new characters | `BeginnersGuilds`, `BeginnersGuildHorde/Alliance` | the guilds must exist; the shipped ids are placeholders |
| Guild bank in every capital | `GuildBank.NpcEntriesAlliance/Horde` | nothing — the gossip trigger ships as a migration |
| Dungeon finder fills with bots | `LFT.BotFill.Enable`, `.DelaySeconds`, `.LevelRangeBelow/Above`, `.SeedRuns`, `.SeedDungeons`, `.SeedTeleport` | – |
| Solo dungeon resurrection, leech limits | `SoloDungeonRepopAlive.Enable`, `Leech.*` | – |
| Keep navmesh tiles loaded | `MMapTileUnload` | off by default; `removeTile` zeroes `tile->polys` and Detour reads it unvalidated, so a surviving polyRef resolves to `nullptr + index` |

Playerbot keys live in `src/modules/PlayerBots/playerbot/aiplayerbot.conf.dist.in`, the
rest in `src/mangosd/mangosd.conf.dist.in`. A config generated from an older checkout
will not contain them — regenerate it or copy the blocks across.

### Class, spell and item fixes

| | |
|---|---|
| Flurry | Never spent its charges above rank 1 |
| Shield Specialization | Granted one rage on every rank, because all five ranks trigger the same fixed-amount spell |
| Sweeping Strikes | Moved fully to a spell script, multiproc fixed |
| Embrace of the Viper | Both set bonuses were dead. The five-piece heal had neither condition nor cooldown; the six-piece did nothing at all and now applies a poison |
| Wild Regeneration | Checked health before the hit landed instead of after, so it refused exactly the hit it was meant to catch |
| Alterac items | Four effects that existed only as developer notes, now implemented |
| Disenchanting | Restored the disenchant ids this database had lost, plus 3450 items that never had one |
| Mage talents | A wide pass over 21 talents and spells — Ignite, Combustion, Amplify/Dampen Magic, Improved Blizzard, Arcane Meditation, Master of Elements, Magic Absorption, Arctic Reach, Hot Streak, Icicles and more. Taken from [faemwow/tortoise-wow](https://github.com/faemwow/tortoise-wow) |
| Mana gain modifiers | `SPELL_AURA_MOD_MANA_GAIN_PERCENT` was never applied when a spell restored mana, so the modifier did nothing for any class. Now applied to both the amount and the threat it generates |
| Damage on creatures | `Unit::DealDamage` branched on `!IsPlayer() && addThreat`, so a creature taking damage that carries no threat fell into the player-only half and was cast to `Player*` — durability loss on a creature, and an uncaught exception |
| Shatter | Read its crit bonus from five hardcoded per-rank values instead of the spell modifier |
| Healing Touch | `OnFinish` followed `mod->ownerAura`, a raw pointer captured when the modifier was applied. An aura expiring mid-cast left it dangling; `SpellModifier::spellId` carries the same id and is used instead |
| Guild bank | Money column was signed and parsing unchecked — deposits could overflow into a negative balance |

### Content and data

Ship as migrations, so a fresh setup gets them automatically:

- Graveyard coverage for The Barrens, Arathi, and the dungeon sub-zones Turtle splits up.
  Without it, releasing near the Crossroads guards puts the ghost on its own corpse, where
  it dies again immediately
- Eighteen trainers nobody could talk to, Survival's missing artisan rank, guard directions
  to the Survival trainer, and a trainer for Alah'Thalas
- The Syndicate quartermaster, which stocked one item out of thirteen
- Hellador Swiftluck, who pointed at equipment that does not exist
- The guild bank gossip trigger, and the PvP trinket no longer dropping the flag

Two are deliberately manual, in `sql/tools/`, because both depend on per-server data:

- `graveyards_turtle_dungeons.sql` — the five Turtle-built dungeons with no graveyard on
  their map. Run `tools/dbc/add_worldsafelocs.py` first; it references WorldSafeLocs ids a
  stock DBC does not have, which stops at 174
- `playerbot_bypass_crossroads.sql` — routes bots around a guard 21 yards from a travel
  node. Rewrites travel graph links by id, so check your own node ids first

### Build and documentation

- Release builds on MSVC ship debug symbols, so a crash dump is readable
- `INSTALL-LINUX.md` and `INSTALL-WINDOWS.md` are start-to-finish walkthroughs, including
  the OpenSSL 3 legacy provider, the database procedure that actually works, and reading a
  crash dump
- The **world database is in this repository** — `sql/base` holds 186 files, 131 MB, plus
  the migrations under `sql/database_updates`. Only client data (maps, DBC, vmaps, mmaps)
  has to be extracted from a game client, with the tools under `tools/`

Several of the fixes above are also kept as standalone patches, each one
self-contained, so they can be lifted onto any compatible tree without taking
the rest of this fork with them. Ask if you want one.

Work from other forks is pulled in where it fits and credited in the commit —
the mage pass comes from [faemwow/tortoise-wow](https://github.com/faemwow/tortoise-wow),
whose repository is also worth a look if you want to build this with Nix instead.

> **Note on the client:** the core must be built with `-DALLOW_TURTLE_ADDONS=ON`, otherwise
> the client crashes with "interface corrupt" on entering the world.

Everything below is upstream's own documentation and applies to this fork as well.
## Client Version

The client version targetted is patch 1.18.1, build 7272  
Any client that does not match this version or build will likely have a myriad of issues

## Additions
Additions will be added as the core code reaches feature completion

#### Current Additions

- **Autoscale** - Rudimentary toggleable dungeon/raid auto scaling system, found in mangosd.conf
- **Leech** - Basic toggleable leech system designed for solo play, found in mangosd.conf
- **Additional Talent Points** - Mostly used for testing, found in tw_char.characters
- **[Playerbots][20]** *(this fork)* - Integrated from [r-o-sh's branch](https://github.com/r-o-sh/tortoise-wow/tree/playerbots-integration-gh). Not experimental — ~1000 of them run permanently and the fork is built around them. Upstream still lists this as planned.

#### Planned Additions

- **[Eluna][19]** - The WoW lua engine

## Operating Systems

* **[Windows][15]**, 32 bit and 64 bit. Windows Server 2008 (or newer) or Windows 8 (or newer) is recommended.
* **Linux**, 32 bit and 64 bit. [Ubuntu 22.04 LTS][14] is recommended. Other distributions with similar package versions will work, too.
Of course, newer versions should work, too. In the case of Windows, matching
server versions will work, too.

## Dependencies

* **[Git][1] / [Github for Windows][2]**: This version control software allows you to get the source files in the first place.
* **[MySQL][3]** / **[MariaDB][4]**: These databases are used to store content and user data.
* **[ACE][5]**: aka Adaptive Communication Environment, provides us with a solid cross-platform framework for abstracting operating system specific details.
* **[Recast][21]**: In order to create navigation data from the client's map files, Recast is used to do the dirty work. It provides functions for rendering, pathing, etc.
* **[G3D][6]**: This engine provides the basic framework for handling 3D data and is used to handle basic map data.
* **[Stormlib][7]**: Provides an abstraction layer for reading from the client's data files.
* **[Zlib][8]/[Zlib for Windows][9]** provides compression algorithms used in both MPQ archive handling and the client/server protocol.
* **[Bzip2][10]/[Bzip2 for Windows][11]** provides compression algorithms used in MPQ archives.
* **[OpenSSL][12]/[OpenSSL for Windows][13]** provides encryption algorithms used when authenticating clients.

To build this project follow any MaNGOS/MaNGOS Zero build guide, with the addition of ACE  

## Database Setup

1. Manually import sql/create_databases.sql
2. Manually import all sql scripts in the sql/base folder
3. Run mangosd to automatically import and track updates  

This will be streamlined once the core is more up to date

> **Caveat for this fork:** step 3 relies on the DB auto-updater
> (`Database.AutoUpdate.Enabled` in mangosd.conf). That works on a database
> built up through the auto-updater from the start. On a database that was
> instead restored from a full dump, the `migrations` table won't line up with
> the files in `sql/database_updates/`, and enabling the auto-updater makes it
> try to replay old migrations until one fails on a duplicate key — the server
> then refuses to start. If that applies to you, keep it disabled and apply new
> migration files by hand, recording each one afterwards:
>
> ```sql
> INSERT INTO migrations (Name, Hash, AppliedAt)
> VALUES ('20260726112016_world', 'manual', NOW());
> ```

[1]: http://git-scm.com/ "Git - Distributed version control system"
[2]: http://windows.github.com/ "github - windows client"
[3]: https://dev.mysql.com/downloads/ "MySQL - The world's most popular open source database"
[4]: https://mariadb.org/download/ "MariaDB - An enhanced, drop-in replacement for MySQL"
[5]: http://www.dre.vanderbilt.edu/~schmidt/ACE.html "ACE - The ADAPTIVE Communication Environment"
[6]: http://sourceforge.net/projects/g3d/ "G3D - G3D Innovation Engine"
[7]: http://zezula.net/en/mpq/stormlib.html "Stormlib - A library for reading data from MPQ archives"
[8]: http://www.zlib.net/ "Zlib"
[9]: http://gnuwin32.sourceforge.net/packages/zlib.htm "Zlib for Windows"
[10]: http://www.bzip.org/ "Bzip2"
[11]: http://gnuwin32.sourceforge.net/packages/bzip2.htm "Bzip2 for Windows"
[12]: http://www.openssl.org/ "OpenSSL - The Open Source toolkit for SSL/TLS"
[13]: http://slproweb.com/products/Win32OpenSSL.html "OpenSSL for Windows"
[14]: http://www.ubuntu.com/ "Ubuntu - The world's most popular free OS"
[15]: http://windows.microsoft.com/ "Microsoft Windows"
[19]: https://github.com/ElunaLuaEngine/Eluna
[20]: https://github.com/ike3/mangosbot-bots
[21]: http://github.com/memononen/recastnavigation "Recast - Navigation-mesh Toolset for Games"
