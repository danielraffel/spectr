You are reviewing the Spectr v2 planning package and deciding whether a v3 is actually necessary.

Your job is not to give loose commentary. Your job is to:

1. review the v2 docs against the prototypes and the current Pulp code
2. re-verify any Pulp gaps that may have changed after the in-flight format/MIDI work
3. either confirm the v2 package is aligned or produce a tighter v3 where it is not
4. if you agree Spectr still needs a framework capability from Pulp, file a GitHub issue against Pulp instead of only describing the gap

Review these files first:

- /Users/danielraffel/Code/spectr/planning/Spectr-V2-Product-Spec.md
- /Users/danielraffel/Code/spectr/planning/Spectr-V2-Pulp-Handoff.md
- /Users/danielraffel/Code/spectr/planning/Spectr-V2-Review-Notes.md
- /Users/danielraffel/Code/spectr/planning/README.md

Prototype and product ground truth:

- /Users/danielraffel/Code/spectr-design/Spectr-2/Spectr (standalone).html
- /Users/danielraffel/Code/spectr-design/Spectr-2/Spectr Sampler.html
- /Users/danielraffel/Code/spectr-design/Spectr-2/src/
- /Users/danielraffel/Code/spectr-design/Spectr-2/effect-ideas.txt
- /Users/danielraffel/Code/spectr-design/Spectr-2/sampler-ideas.txt

Pulp source of truth:

- /Users/danielraffel/Code/pulp/

Re-check these upstream Pulp workstreams specifically before repeating any old gap:

- feature/clap-midi-cc-coverage
- feature/au-v2-effect-midi-input
- feature/format-skills-clap-vst3-auv3

Hard constraints:

- prototype-visible effect features remain in scope
- do not improve the plan by cutting prototype effect functionality
- keep Spectr effect-first and sampler-forward
- prefer verified code over stale docs
- if the current Pulp state contract still cannot support Spectr's recall model cleanly, say so plainly

Specific questions to answer:

1. Does v2 now preserve all visible effect features from the prototype?
2. Is the frequency-slicer identity still crisp, or did the plan drift back toward EQ language?
3. Is the current recommendation on `StateStore`, `StateTree`, snapshots, patterns, and host/session recall now correct?
4. After the active Pulp branches, does Spectr still need a supplemental plugin-state capability from Pulp?
5. Is AU v2 still the right first Apple format, or did the upstream work materially change that answer?
6. Are the dependency recommendations still sensible and minimal?

If you agree a Pulp issue is still needed:

- file a GitHub issue against Pulp if you have GitHub access
- title it concretely
- include the exact Spectr-driven problem statement
- include acceptance criteria
- reference the relevant code paths

Deliverables:

- If v2 is materially correct, create:
  - /Users/danielraffel/Code/spectr/planning/Spectr-V3-Review-Notes.md
  with a concise confirmation plus only the corrections that still matter.

- If v3 is needed, create:
  - /Users/danielraffel/Code/spectr/planning/Spectr-V3-Product-Spec.md
  - /Users/danielraffel/Code/spectr/planning/Spectr-V3-Pulp-Handoff.md
  - /Users/danielraffel/Code/spectr/planning/Spectr-V3-Review-Notes.md

Output requirements:

- findings first
- separate critical from optional
- state clearly what changed between v2 and v3
- if no GitHub issue was filed, say whether that is because it is unnecessary or because access was unavailable
