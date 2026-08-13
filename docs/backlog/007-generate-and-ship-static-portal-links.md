---
status: failed
risk: medium
area: playerbots/transport
---

# Static portal (spellcaster GO) travel links are never generated, so bots never use them

**Problem:** Static portals — `GAMEOBJECT_TYPE_SPELLCASTER` gameobjects whose
spell teleports the user — are never routed to by bots. The execution side is
fine and faithful: `MoveTo` sends a real `CMSG_GAMEOBJ_USE` packet for a
`staticPortal` path point. But the shipped travel-node store contains **zero**
`staticPortal` (type 6) links, because the code that generates them,
`TravelNodeMap::generatePortalNodes`, only runs during a full graph
regeneration (`hasToFullGen`), which never fires — the node tables are
pre-seeded from SQL and only regenerate when empty. On a server that adds any
custom portal hub, none of its portals are reachable by bots.

**Suspected cause / area:**
`src/modules/PlayerBots/playerbot/TravelNode.cpp:2289`
(`generatePortalNodes`); missing rows in `ai_playerbot_travelnode` /
`ai_playerbot_travelnode_link` (link type 6).

**Acceptance criteria:**
- A narrow debug command exists (alongside the existing `generateNodes` /
  `generatePaths` / `saveNodeStore` commands in `DebugAction.cpp`) that runs
  `sTravelNodeMap.generatePortalNodes()` followed by `saveNodeStore(true)`
  **without** triggering a full graph regeneration — the seeded elevator/tram
  nodes must survive running it (they cannot currently be rebuilt; see the
  `LocalTransport` backlog item).
- Running the command once against a scratch/test world DB produces new
  `staticPortal` (type 6) rows in `ai_playerbot_travelnode_link`.
- The resulting delta is committed as a SQL migration under
  `sql/database_updates/`.
- After the migration is applied, a bot routes through and successfully uses
  at least one static portal GO that previously had no graph link at all.

**Notes:** Per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` ("P1"). This
is a data migration, not just a code change — validate the generated rows on
a scratch DB (row counts, no duplicate/self-referencing nodes) before
committing them.

**Failure notes:** blocking findings not addressed — acceptance criteria 3 and
4 (ship a SQL migration under `sql/database_updates/`, with validated row
counts and an observed bot routing through a portal) are not satisfiable as
scoped, and the reviewer judged them unsatisfiable with the shipped world data
at all:

- The shipped data spawns only two teleporting spellcaster GOs — 176296
  "Portal to Stormwind" (guid 5007923) and 176499 "Portal to Orgrimmar" (guid
  5007924), both on map 42 (`sql/base/tw_world_gameobject.sql`) — while the
  shipped node store
  (`src/modules/PlayerBots/sql/world/classic/ai_playerbot_travel_nodes.sql`)
  has **zero** nodes on map 42. A generated portal entrance node therefore has
  no same-map node to build a walk path to or from, and no bot can reach it.
  Making it routable requires generating a node set for map 42, which is the
  full `gen node` regeneration that acceptance criterion 1 explicitly forbids.
- Producing and validating the delta needs a world DB, a built core, and a
  mysql client. None exist in this tree (no compiler either), and
  `saveNodeStore` renumbers every node id on each save, so the delta cannot be
  hand-written against the current dump. Committing hand-derived, unvalidated
  INSERTs would contradict this artifact's own note above.

The remaining findings (2–5) were fixed, and the evidence plus a
recommendation to re-scope AC 3/4 into a separate item run against a real
scratch server is recorded in commit `bb5aa26` on the failed branch.

Worktree left at
`D:/CodingProjects/tortoise-wow/tortoise-wow/.claude/worktrees/wf_aaf838b7-624-1`,
branch `backlog/generate-and-ship-static-portal-links` (never pushed to
origin, so `bb5aa26` exists only there — read it with
`git -C <worktree> show bb5aa26` before discarding). Remove both with
`git worktree remove <path>` and `git branch -D
backlog/generate-and-ship-static-portal-links` before resetting this artifact
to pending.
