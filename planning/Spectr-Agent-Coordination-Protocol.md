# Spectr Agent-Coordination Protocol

> How two agents working on Spectr + its upstream (Pulp) keep each other
> informed without a realtime channel. Minimum-viable protocol that
> worked for us; adjust as patterns evolve.
>
> Future upgrade path: [pulp#727](https://github.com/danielraffel/pulp/issues/727)
> proposes a proper MCP-based coordination mechanism. This doc
> describes what we do TODAY, until that ships.

## Problem

Spectr is a Pulp consumer. Non-trivial work often involves two
independent Claude Code agents in different worktrees:

- **Pulp-side agent** — adding a framework feature (e.g., a new
  `pulp::view::*` class, a CLI command, an SDK subsystem)
- **Spectr-side agent** — integrating that feature as soon as it
  lands (pin bump, cutover, A/B verification, gap filing)

They operate async across sessions that may never overlap in time.
No shared memory, no shared session, no realtime channel (today).
They coordinate via **GitHub comments on tracking issues and PRs**.

## The checkpoint protocol

Each side posts **state-change comments** on the shared tracking
issue at a well-known set of moments. The other side reads and
responds. Neither polls real-time — each agent picks up the queue
when it next resumes work on that thread.

### Canonical checkpoints (framework-agent side)

On the tracking issue (e.g., pulp#709, #468, #661):

1. **Kicked off** — "Starting this. Worktree at <branch> from main."
   Consumer reads: "now is not the time to start speculative work
   against this API."

2. **API frozen** — "Proposed public surface below is stable for
   this PR. Changing it will require a follow-up issue." Include
   the header diff or inline the proposed signatures. Consumer
   reads: "you can start writing a stand-in against this API with
   confidence."

3. **Tests green** — "Implementation complete, N tests pass
   covering M scenarios." Consumer reads: "behavior is verified;
   safe to port your own tests against this API."

4. **Fixture audit** — when a specific downstream project is the
   consumer of record, post a diff audit: "I ported <project>'s
   current implementation to the new API; here are the N
   divergences I found and how each resolves." Consumer reads:
   "the pulp-side agent already thought through my cutover;
   respond to the enumerated divergences, don't re-audit from
   scratch."

5. **PR opened** — "Review welcome. Spec-level questions still
   answerable; implementation-level changes welcome but harder."
   Consumer reads: "if I had API-shape objections, now's my last
   chance."

6. **Merged** — "Landed at commit <sha>. Next tagged release will
   ship this." Consumer reads: "start the local cutover against
   the feature branch now; flip to the release pin when the tag
   goes out."

### Canonical checkpoints (project-agent side)

On the same tracking issue (or a reference in the project's
tracker):

1. **Readiness signal** — "Consumer-side driver locked in. When
   your PR merges + SDK tag ships, I open the cutover PR within
   <X> hours." Framework-agent reads: "I have a real downstream
   driver who will integrate fast; design decisions have a
   concrete stakeholder."

2. **Divergence audit response** — reply to the framework's
   fixture audit: "Items 1/3/5 fit cleanly; item 2 would benefit
   from <X>; item 4 is a blocker that needs <Y>." Framework-agent
   reads: "here's what to adjust before freezing vs what to
   absorb as follow-up issues."

3. **Cutover in-flight** — "Pulling the PR locally, verifying
   against <project>'s tests. Will post results within <X>
   hours." Framework-agent reads: "someone is actively exercising
   this; hold off on further breaking changes."

4. **Integration findings** — "N/M tests green after cutover.
   Gaps filed: <list of new issues>." Framework-agent reads: "the
   feature works; here's the follow-up backlog from a real
   consumer."

5. **Cutover merged** — "Downstream now uses the new API in
   production/trunk. Thank-you note + cross-link to the consumer
   PR." Framework-agent reads: "integration proven; close this
   issue."

### When to break protocol

- **Blocking question** — if you're stuck and need an answer in
  minutes rather than hours, ping the developer (human) directly
  rather than wait on the other agent's next session.
- **Design reversal** — if an earlier checkpoint claim needs to
  be retracted (e.g., "API frozen" → "API changed"), post an
  explicit "RETRACTION" comment. Don't edit old comments.
- **Out-of-scope finding** — file a separate issue; don't let the
  tracking thread accumulate unrelated gaps.

## What this doc does NOT replace

- GitHub's normal PR review flow. Checkpoint comments are on the
  **tracking issue**; the PR itself gets normal reviews.
- Project-internal planning docs. The tracking issue is for
  consumer-facing state; the project's own `planning/` dir holds
  internal scope and status.

## Example from 2026-04-23/24

Full trace: [pulp#709](https://github.com/danielraffel/pulp/issues/709)
(EditorBridge framework proposal) → [pulp#711](https://github.com/danielraffel/pulp/pull/711)
(implementation PR) → [Spectr PR #17](https://github.com/danielraffel/spectr/pull/17)
(consumer cutover).

Key checkpoint comments:

- Spectr-side readiness signal: [pulp#468 comment 4311225456](https://github.com/danielraffel/pulp/issues/468#issuecomment-4311225456)
- Pulp-side "API frozen + tests green" checkpoint: [pulp#709 Checkpoint 1+2 comment](https://github.com/danielraffel/pulp/issues/709#issuecomment-4311373819)
- Spectr-side divergence audit: [pulp#709 consumer audit](https://github.com/danielraffel/pulp/issues/709#issuecomment-4311448632)
- Pulp-side PR-opened signal implicit via PR creation at #711
- Spectr-side cutover-merged reply: closing comment on Spectr PR #17

That entire cycle happened without real-time contact between the
two agent sessions — only checkpoint comments on GitHub.

## Future: pulp#727

[pulp#727](https://github.com/danielraffel/pulp/issues/727)
proposes a proper MCP-based agent-coordination channel (relay
server, per-agent identity, channel-scoped messages). When that
lands, this doc updates to describe how to use the MCP primitives
to route the same six checkpoint types, with GitHub retained as
the durable audit trail.

Until then: this protocol is the way. File an issue if you find a
case it doesn't cover and link it here.
