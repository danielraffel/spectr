import { MiniPreview } from './MiniPreview.cjs';

type Pattern = { id: string; name: string; source: string; gains?: number[]; updatedAt?: string };
type PatternRowProps = {
  pattern: Pattern;
  selected?: boolean;
  isDefault?: boolean;
  onClick?: (...args: unknown[]) => unknown;
  onDblClick?: (...args: unknown[]) => unknown;
  N: number;
};

export function PatternRow({ pattern, selected, isDefault, onClick, onDblClick, N }: PatternRowProps) {
  const gains = useMemoPM(() => window.Spectr.resolveGains(pattern, N), [pattern, pattern.updatedAt, pattern.id, N]);
  return (
    <div data-spectr-pattern-id={pattern.id} data-spectr-pattern-source={pattern.source} onClick={onClick} onDoubleClick={onDblClick}
      style={{
        padding: '6px 14px',
        background: selected ? 'rgba(120,180,255,0.14)' : 'transparent',
        borderLeft: selected ? '2px solid hsl(200,85%,65%)' : '2px solid transparent',
        cursor: 'pointer',
        display: 'flex', alignItems: 'center', gap: 10,
        fontSize: 10, letterSpacing: 0.5,
      }}
    >
      <MiniPreview gains={gains} />
      <div style={{ flex: 1, minWidth: 0, overflow: 'hidden' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          {isDefault && <span style={{ color: 'hsl(50,90%,65%)', fontSize: 10 }}>★</span>}
          <span style={{
            color: selected ? '#fff' : 'rgba(255,255,255,0.85)',
            whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis',
          }}>{pattern.name}</span>
        </div>
      </div>
      <span style={{
        fontSize: 8, letterSpacing: 1.5, opacity: 0.4,
        padding: '1px 4px', border: '1px solid rgba(255,255,255,0.12)', borderRadius: 2,
      }}>{pattern.source === 'factory' ? 'F' : 'U'}</span>
    </div>
  );
}
