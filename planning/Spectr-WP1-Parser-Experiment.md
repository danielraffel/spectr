# Spectr WP-1 parser-backed dependency experiment

This bounded experiment addresses the source-conversion gap found by the WP-1
adversarial review. `tools/wp1_dependency_manifest.mjs` parses JavaScript and
JSX with the pinned `@babel/parser` toolchain, discovers both function
components and uppercase arrow components nested in another component, and
emits a deterministic owner/capture closure for explicitly requested roots.

The manifest identity is `component:<name>:<sha256 of the exact declaration
slice>`. A nested component records its lexical owner, direct component
references, captures from the owner scope, runtime globals, and unresolved
identifiers. Closure construction fails closed when a requested component has
an unresolved identifier. The current positive runtime case is the leaf
`MBtn` component from `native-ui/materialized/materialized-document.runtime.json`;
it resolves only the runtime `React` binding and has no unresolved references.

The fixture `tools/fixtures/wp1-nested-components.jsx` proves a nested `Inner`
arrow component captures `label`, depends on `MBtn`, and is reached through the
`Panel` root. The negative test replaces `MBtn` with `MissingButton`; the parser
still parses the source, but closure construction rejects the unresolved
identifier. This keeps syntax parsing and dependency resolution as separate,
visible gates.

The experiment does not rewrite `native-ui/materialized/materialized-document.runtime.json`,
delete patch scripts, or claim authored TSX conversion. Install the pinned
parser with `npm ci --prefix tools/wp1-parser` before running the tests. The
CLI exits with a setup error when that dependency is absent; it never falls
back to regular-expression extraction.
