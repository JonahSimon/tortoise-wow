---
status: pending
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
