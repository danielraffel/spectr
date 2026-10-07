function PatternDetail({ pattern, N, isDefault, onApply, onRename, onDuplicate, onDelete, onOverwrite, onSetDefault, onExport }) {
    const [editName, setEditName] = usePM(false);
    const [draft, setDraft] = usePM(pattern.name);
    usePE(() => {
        setDraft(pattern.name);
        setEditName(false);
    }, [pattern.id]);
    const gains = useMemoPM(() => window.Spectr.resolveGains(pattern, N), [pattern, N, pattern.updatedAt]);
    const isFactory = pattern.source === "factory";
    return /* @__PURE__ */ React.createElement("div", { "data-spectr-manager-detail": true, style: { flex: 1, minWidth: 0, minHeight: 0, display: "flex", flexDirection: "column", gap: 12 } },
        React.createElement("div", { "data-spectr-manager-heading": true, style: { display: "flex", alignItems: "center", gap: 8, minHeight: 26 } },
            editName && !isFactory ? /* @__PURE__ */ React.createElement("input", { id: "spectr-manager-rename", "data-spectr-manager-rename": true, autoFocus: true, value: draft, onChange: (e) => setDraft(String(spectrInputValue(e) ?? "")), onBlur: () => {
                    onRename(draft);
                    setEditName(false);
                }, onKeyDown: (e) => {
                    if (e.key === "Enter")
                        e.target.blur();
                    if (e.key === "Escape") {
                        setDraft(pattern.name);
                        setEditName(false);
                    }
                }, style: {
                    background: "rgba(255,255,255,0.05)",
                    border: "1px solid rgba(180,210,255,0.4)",
                    color: "#fff",
                    fontFamily: "var(--mono)",
                    fontSize: 14,
                    letterSpacing: 1,
                    padding: "4px 8px",
                    borderRadius: 3,
                    outline: "none",
                    flex: 1
                } }) : /* @__PURE__ */ React.createElement("div", { "data-spectr-manager-title": true, "data-spectr-pattern-id": pattern.id, style: { fontSize: 14, letterSpacing: 1, fontWeight: 500, lineHeight: 1, minHeight: 26, display: "flex", alignItems: "center", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis", flexShrink: 0, maxWidth: 190 } }, pattern.name),
            React.createElement("div", { "data-spectr-manager-source": true, style: {
                    display: "flex",
                    alignItems: "center",
                    minHeight: 26,
                    lineHeight: 1,
                    flexShrink: 0,
                    whiteSpace: "nowrap",
                    fontSize: 8.5,
                    letterSpacing: 1.5,
                    opacity: 0.6,
                    padding: "2px 6px",
                    border: "1px solid rgba(255,255,255,0.15)",
                    borderRadius: 2
                } }, isFactory ? "FACTORY" : "USER"),
            !isFactory && !editName && /* @__PURE__ */ React.createElement("button", { "data-spectr-manager-action": "rename-start", onClick: () => setEditName(true), style: iconBtn }, "\u270E")),
        React.createElement("div", { "data-spectr-manager-preview": true, "data-spectr-pattern-id": pattern.id, style: {
                background: "rgba(0,0,0,0.35)",
                border: "1px solid rgba(255,255,255,0.06)",
                borderRadius: 3,
                padding: 10,
                height: 110,
                display: "flex",
                alignItems: "center",
                justifyContent: "center"
            } },
            React.createElement(MiniPreview, { gains: gains, w: 380, h: 86 })),
        React.createElement("div", { "data-spectr-manager-meta": true, style: { fontSize: 9.5, opacity: 0.55, display: "flex", gap: 14, flexWrap: "wrap" } },
            React.createElement("span", null,
                "BANDS (current): ",
                React.createElement("span", { className: "tnum" }, N)),
            !isFactory && pattern.createdAt && /* @__PURE__ */ React.createElement("span", null,
                "CREATED: ",
                pattern.createdAt.slice(0, 10)),
            !isFactory && pattern.updatedAt && /* @__PURE__ */ React.createElement("span", null,
                "UPDATED: ",
                pattern.updatedAt.slice(0, 10))),
        React.createElement("div", { style: { flex: 1, minHeight: 12 } }),
        React.createElement("div", { "data-spectr-manager-actions": true, style: { display: "flex", gap: 6, flexWrap: "wrap", alignItems: "center", minHeight: 58 } },
            React.createElement(MBtn, { action: "apply", primary: true, onClick: onApply }, "APPLY"),
            React.createElement(MBtn, { action: "set-default", onClick: onSetDefault }, isDefault ? "\u2605 DEFAULT" : "SET AS DEFAULT"),
            React.createElement(MBtn, { action: "duplicate", onClick: onDuplicate }, "DUPLICATE"),
            !isFactory && /* @__PURE__ */ React.createElement(MBtn, { onClick: onOverwrite }, "UPDATE FROM CURRENT"),
            !isFactory && /* @__PURE__ */ React.createElement(MBtn, { action: "delete", danger: true, onClick: onDelete }, "DELETE"),
            React.createElement("div", { style: { flex: 1 } }),
            React.createElement(MBtn, { action: "export-file", onClick: () => onExport("file") }, "EXPORT (FILE)"),
            React.createElement(MBtn, { action: "export-clip", onClick: () => onExport("clipboard") }, "EXPORT (CLIP)")));
}
