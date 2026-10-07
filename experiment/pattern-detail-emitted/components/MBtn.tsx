function MBtn({ children, onClick, primary, danger, action }) {
  return /* @__PURE__ */ <button data-spectr-manager-action={action} onClick={onClick} style={{
    background: primary ? "rgba(80,140,210,0.22)" : danger ? "rgba(210,80,100,0.15)" : "rgba(255,255,255,0.04)",
    border: "1px solid " + (primary ? "rgba(140,190,240,0.4)" : danger ? "rgba(240,150,160,0.35)" : "rgba(255,255,255,0.12)"),
    color: primary ? "#fff" : danger ? "rgba(255,200,210,0.95)" : "rgba(255,255,255,0.85)",
    padding: "5px 9px",
    fontFamily: "var(--mono)",
    fontSize: 10,
    letterSpacing: 1,
    borderRadius: 3,
    cursor: "pointer",
    height: 26
  }}>{children}</button>;
}
