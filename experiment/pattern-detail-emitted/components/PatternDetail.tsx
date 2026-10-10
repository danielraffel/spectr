function PatternDetail({ pattern, N, isDefault, onApply, onRename, onDuplicate, onDelete, onOverwrite, onSetDefault, onExport }) {
  const [editName, setEditName] = usePM(false);
  const [draft, setDraft] = usePM(pattern.name);
  usePE(() => {
    setDraft(pattern.name);
    setEditName(false);
  }, [pattern.id]);
  const gains = useMemoPM(() => window.Spectr.resolveGains(pattern, N), [pattern, N, pattern.updatedAt]);
  const isFactory = pattern.source === "factory";
  return /* @__PURE__ */ <div data-spectr-manager-detail style={{ flex: 1, minWidth: 0, minHeight: 0, display: "flex", flexDirection: "column", gap: 12 }}><div data-spectr-manager-heading style={{ display: "flex", alignItems: "center", gap: 8, minHeight: 26 }}>{editName && !isFactory ? /* @__PURE__ */ <input id={"spectr-manager-rename"} data-spectr-manager-rename autoFocus value={draft} onChange={(e) => setDraft(String(spectrInputValue(e) ?? ""))} onBlur={() => {
        onRename(draft);
        setEditName(false);
      }} onKeyDown={(e) => {
        if (e.key === "Enter") e.target.blur();
        if (e.key === "Escape") {
          setDraft(pattern.name);
          setEditName(false);
        }
      }} style={{
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
      }} /> : /* @__PURE__ */ <div data-spectr-manager-title data-spectr-pattern-id={pattern.id} style={{ fontSize: 14, letterSpacing: 1, fontWeight: 500, lineHeight: 1, minHeight: 26, display: "flex", alignItems: "center", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis", flexShrink: 0, maxWidth: 190 }}>{pattern.name}</div>}<div data-spectr-manager-source style={{
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
  }}>{isFactory ? "FACTORY" : "USER"}</div>{!isFactory && !editName && /* @__PURE__ */ <button data-spectr-manager-action={"rename-start"} onClick={() => setEditName(true)} style={iconBtn}>{"\u270E"}</button>}</div><div data-spectr-manager-preview data-spectr-pattern-id={pattern.id} style={{
    background: "rgba(0,0,0,0.35)",
    border: "1px solid rgba(255,255,255,0.06)",
    borderRadius: 3,
    padding: 10,
    height: 110,
    display: "flex",
    alignItems: "center",
    justifyContent: "center"
  }}><MiniPreview gains={gains} w={380} h={86} /></div><div data-spectr-manager-meta style={{ fontSize: 9.5, opacity: 0.55, display: "flex", gap: 14, flexWrap: "wrap" }}><span>{"BANDS (current): "}<span className={"tnum"}>{N}</span></span>{!isFactory && pattern.createdAt && /* @__PURE__ */ <span>{"CREATED: "}{pattern.createdAt.slice(0, 10)}</span>}{!isFactory && pattern.updatedAt && /* @__PURE__ */ <span>{"UPDATED: "}{pattern.updatedAt.slice(0, 10)}</span>}</div><div style={{ flex: 1, minHeight: 12 }} /><div data-spectr-manager-actions style={{ display: "flex", gap: 6, flexWrap: "wrap", alignItems: "center", minHeight: 58 }}><MBtn action={"apply"} primary onClick={onApply}>{"APPLY"}</MBtn><MBtn action={"set-default"} onClick={onSetDefault}>{isDefault ? "\u2605 DEFAULT" : "SET AS DEFAULT"}</MBtn><MBtn action={"duplicate"} onClick={onDuplicate}>{"DUPLICATE"}</MBtn>{!isFactory && /* @__PURE__ */ <MBtn onClick={onOverwrite}>{"UPDATE FROM CURRENT"}</MBtn>}{!isFactory && /* @__PURE__ */ <MBtn action={"delete"} danger onClick={onDelete}>{"DELETE"}</MBtn>}<div style={{ flex: 1 }} /><MBtn action={"export-file"} onClick={() => onExport("file")}>{"EXPORT (FILE)"}</MBtn><MBtn action={"export-clip"} onClick={() => onExport("clipboard")}>{"EXPORT (CLIP)"}</MBtn></div></div>;
}
