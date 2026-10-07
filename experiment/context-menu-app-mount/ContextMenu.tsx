type ContextMenuProps = {
  x: number;
  y: number;
  band: number;
  N: number;
  selection?: Set<number> | null;
  editMode: string;
  onClose: () => void;
  onEditMode: (mode: string) => void;
  onMuteBand: (band: number) => void;
  onZeroBand: (band: number) => void;
  onSoloBand: (band: number) => void;
  onSelectAround: (band: number, radius: number) => void;
  onClearSel: () => void;
  onZeroSel: () => void;
  onMuteSel: () => void;
  onFitView: () => void;
};

type ItemProps = {
  action?: string;
  label: string;
  hint?: string;
  onClick: () => void;
  disabled?: boolean;
  danger?: boolean;
  sub?: string;
};

type DividerProps = { label: string };

export function ContextMenu({ x, y, band, N, selection, editMode, onClose, onEditMode, onMuteBand, onZeroBand, onSoloBand, onSelectAround, onClearSel, onZeroSel, onMuteSel, onFitView }: ContextMenuProps) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const consume = event => {
      event.preventDefault();
      event.stopPropagation();
      if (event.stopImmediatePropagation) event.stopImmediatePropagation();
    };
    const armOutsideActivationShield = () => {
      const types = ['mousedown', 'pointerup', 'mouseup', 'click'];
      let timer = 0;
      const release = () => {
        clearTimeout(timer);
        for (const type of types) document.removeEventListener(type, shield, true);
      };
      const shield = event => {
        consume(event);
        if (event.type === 'click') release();
      };
      for (const type of types) document.addEventListener(type, shield, true);
      timer = setTimeout(release, 1000);
    };
    const onDown = (event: PointerEvent) => {
      if (!ref.current || ref.current.contains(event.target)) return;
      consume(event);
      armOutsideActivationShield();
      onClose();
    };
    const onKey = (event: KeyboardEvent) => {
      if (event.key !== 'Escape') return;
      consume(event);
      onClose();
    };
    // Schedule so the click that opened it doesn't immediately close it.
    const t = setTimeout(() => document.addEventListener('pointerdown', onDown, true), 0);
    window.addEventListener('keydown', onKey, true);
    return () => {
      clearTimeout(t);
      document.removeEventListener('pointerdown', onDown, true);
      window.removeEventListener('keydown', onKey, true);
    };
  }, [onClose]);

  // Clamp to viewport
  const root = document.getElementById('root');
  const vw = root ? root.clientWidth : window.innerWidth, vh = root ? root.clientHeight : window.innerHeight;
  const W = 230, H = 380;
  const left = Math.min(x, vw - W - 8);
  const top = Math.min(y, vh - H - 8);

  const hasBand = band >= 0;
  const hasSel = selection && selection.size > 0;

  const modes = [
    { k: 'sculpt', label: 'Sculpt', hint: '1' },
    { k: 'level',  label: 'Level',  hint: '2' },
    { k: 'boost',  label: 'Boost',  hint: '3' },
    { k: 'flare',  label: 'Flare',  hint: '4' },
    { k: 'glide',  label: 'Glide',  hint: '5' },
  ];

  const Item = ({ action, label, hint, onClick, disabled, danger, sub }: ItemProps) => (
    <button role="menuitem" data-spectr-band-action={action}
      onClick={() => { if (!disabled) { onClick(); onClose(); } }}
      disabled={disabled}
      style={{
        display: 'flex', alignItems: 'center', gap: 10,
        width: '100%', padding: '6px 12px',
        background: 'transparent', border: 'none',
        color: disabled ? 'rgba(255,255,255,0.25)' : (danger ? 'rgba(255,180,190,0.9)' : 'rgba(255,255,255,0.88)'),
        cursor: disabled ? 'default' : 'pointer',
        fontFamily: 'var(--mono)', fontSize: 10.5, letterSpacing: 0.3,
        textAlign: 'left',
      }}
      onMouseEnter={e => { if (!disabled) e.currentTarget.style.background = 'rgba(120,180,255,0.14)'; }}
      onMouseLeave={e => { e.currentTarget.style.background = 'transparent'; }}
    >
      <span style={{ flex: 1 }}>{label}</span>
      {sub && <span style={{ opacity: 0.45, fontSize: 9.5 }}>{sub}</span>}
      {hint && <span style={{
        fontSize: 8.5, opacity: 0.5, padding: '1px 5px',
        border: '1px solid rgba(255,255,255,0.14)', borderRadius: 2,
      }}>{hint}</span>}
    </button>
  );
  const Divider = ({ label }: DividerProps) => (
    <div style={{
      fontSize: 8.5, letterSpacing: 2, opacity: 0.4,
      padding: '8px 12px 4px', textTransform: 'uppercase',
    }}>{label}</div>
  );

  return (
    <div ref={ref} data-spectr-overlay="true" data-spectr-band-context-menu="true" role="menu" aria-label="Band actions"
      style={{
        position: 'fixed', left, top, width: W,
        background: 'rgba(12,16,22,0.97)',
        border: '1px solid rgba(255,255,255,0.12)',
        borderRadius: 5, padding: '6px 0',
        boxShadow: '0 14px 40px rgba(0,0,0,0.6)',
        backdropFilter: 'blur(12px)',
        zIndex: 40, pointerEvents: 'auto',
      }}
    >
      {hasBand && (
        <>
          <Divider label={`BAND ${band + 1}`} />
          <Item action="mute-band" label="Mute / Unmute" onClick={() => onMuteBand(band)} />
          <Item action="reset-band" label="Reset to 0 dB" onClick={() => onZeroBand(band)} />
          <Item label="Solo" onClick={() => onSoloBand(band)} sub="mute others" />
          <Item label="Select ±3" onClick={() => onSelectAround(band, 3)} />
          <Item label="Select ±8" onClick={() => onSelectAround(band, 8)} />
        </>
      )}
      {hasSel && (
        <>
          <Divider label={`Selection · ${selection.size}`} />
          <Item label="Zero selection" onClick={onZeroSel} />
          <Item label="Mute selection" onClick={onMuteSel} />
          <Item label="Clear selection" onClick={onClearSel} />
        </>
      )}
      <Divider label="EDIT MODE" />
      {modes.map(m => (
        <Item key={m.k}
          label={(editMode === m.k ? '● ' : '   ') + m.label}
          hint={m.hint}
          onClick={() => onEditMode(m.k)} />
      ))}
      <Divider label="VIEW" />
      <Item label="Fit full range" onClick={onFitView} sub="20 Hz – 20 kHz" />
    </div>
  );
}
