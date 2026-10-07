/* Parser-backed WP-1 fixture: nested uppercase arrow + transitive leaf. */
function MBtn({ children }) {
  return React.createElement("button", null, children);
}
function Panel({ label }) {
  const Inner = ({ prefix }) => React.createElement(MBtn, {
    children: prefix + label,
  });
  return React.createElement(Inner, { prefix: "fixture:" });
}
