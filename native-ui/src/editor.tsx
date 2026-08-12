import React, { useLayoutEffect, useRef, useState } from 'react';
import { Button, Canvas, Label, Row, View, render } from '@pulp/react';

type EditorState = {
  revision: number;
  n_visible: number;
  gain_db: number[];
  muted: boolean[];
  min_hz: number;
  max_hz: number;
};
type BridgeResponse = EditorState & { ok: boolean; error?: string };
type Gesture = {
  index: number;
  startY: number;
  startValue: number;
  dragging: boolean;
};

declare global {
  var __spectrHydrate: ((payload: EditorState) => void) | undefined;
  var __spectrAnalyzer: ((magnitudes: number[]) => void) | undefined;
  var __spectrEditorDispatch: ((message: string) => string) | undefined;
}

const DESIGN_WIDTH = 1320;
const DESIGN_HEIGHT = 860;
const BAND_COUNT = 32;

function clampGain(value: number): number {
  return Math.max(-24, Math.min(24, value));
}

function initialState(): EditorState {
  return {
    revision: 0,
    n_visible: BAND_COUNT,
    gain_db: Array.from({ length: BAND_COUNT }, () => 0),
    muted: Array.from({ length: BAND_COUNT }, () => false),
    min_hz: 20,
    max_hz: 20000,
  };
}

function AnalyzerCanvas({ magnitudes }: { magnitudes: number[] }) {
  useLayoutEffect(() => {
    const call = (name: string, ...args: unknown[]) => {
      const fn = (globalThis as any)[name];
      if (typeof fn !== 'function')
        throw new Error(`required CanvasWidget bridge op ${name} is unavailable`);
      fn('spectr-analyzer-canvas', ...args);
    };
    call('canvasClear');
    call('canvasFillRect', 0, 0, 1000, 300, '#080b10');
    for (let x = 0; x <= 1000; x += 125)
      call('canvasStrokeLine', x, 0, x, 300, '#202a35', 1);
    for (let y = 0; y <= 300; y += 60)
      call('canvasStrokeLine', 0, y, 1000, y, '#202a35', 1);

    if (magnitudes.length > 1) {
      call('canvasSetStrokeColor', '#2be1ff');
      call('canvasSetLineWidth', 2);
      call('canvasBeginPath');
      magnitudes.forEach((magnitude, index) => {
        const x = index * 1000 / (magnitudes.length - 1);
        const y = 294 - Math.max(0, Math.min(1, magnitude)) * 270;
        call(index === 0 ? 'canvasMoveTo' : 'canvasLineTo', x, y);
      });
      call('canvasStrokePath');
    }
  }, [magnitudes]);

  return <Canvas id="spectr-analyzer-canvas" width={1000} height={300} />;
}

function App() {
  const [editorState, setEditorState] = useState<EditorState>(initialState);
  const [magnitudes, setMagnitudes] = useState<number[]>([]);
  const current = useRef(editorState);
  const gesture = useRef<Gesture | null>(null);

  const applyAuthoritative = (payload: EditorState) => {
    if (!payload || payload.n_visible !== BAND_COUNT
        || !Array.isArray(payload.gain_db) || payload.gain_db.length !== BAND_COUNT
        || !Array.isArray(payload.muted) || payload.muted.length !== BAND_COUNT
        || !payload.gain_db.every(Number.isFinite)
        || !payload.muted.every(value => typeof value === 'boolean')
        || !Number.isFinite(payload.revision)
        || !Number.isFinite(payload.min_hz) || !Number.isFinite(payload.max_hz)
        || payload.min_hz <= 0 || payload.max_hz <= payload.min_hz
        || payload.revision < current.current.revision) return;
    const next: EditorState = {
      revision: payload.revision,
      n_visible: BAND_COUNT,
      gain_db: payload.gain_db.map(value => clampGain(Number(value))),
      muted: payload.muted.map(value => value === true),
      min_hz: Number(payload.min_hz),
      max_hz: Number(payload.max_hz),
    };
    current.current = next;
    setEditorState(next);
  };

  const dispatch = (type: string, payload: Record<string, unknown> = {}) => {
    if (typeof globalThis.__spectrEditorDispatch !== 'function')
      throw new Error('required native Spectr editor bridge is unavailable');
    const response = JSON.parse(globalThis.__spectrEditorDispatch(
      JSON.stringify({ type, payload }))) as BridgeResponse;
    if (!response.ok) throw new Error(response.error || `native ${type} failed`);
    if (Number.isFinite(response.revision)) applyAuthoritative(response);
    return response;
  };

  useLayoutEffect(() => {
    globalThis.__spectrHydrate = applyAuthoritative;
    globalThis.__spectrAnalyzer = next => {
      if (Array.isArray(next) && next.length > 1 && next.every(Number.isFinite))
        setMagnitudes(next);
    };
    try {
      dispatch('processing_state_get');
    } catch (error) {
      // ScriptedUiSession probes run with an intentionally unavailable native
      // handler. The committed realm is hydrated immediately after load.
      if (!String(error).includes('unavailable during validation')) throw error;
    }
    return () => {
      globalThis.__spectrHydrate = undefined;
      globalThis.__spectrAnalyzer = undefined;
    };
  }, []);

  const valueFromEvent = (event: any) => {
    const bounds = event.currentTarget?.getBoundingClientRect?.();
    const height = bounds?.height;
    const y = Number.isFinite(event.offsetY) ? event.offsetY
      : Number.isFinite(event.clientY) && bounds ? event.clientY - bounds.top : NaN;
    if (!Number.isFinite(y) || !Number.isFinite(height) || height <= 0) return null;
    return { y, gain: clampGain((1 - y / height) * 48 - 24) };
  };

  const beginGesture = (index: number, event: any) => {
    const value = valueFromEvent(event);
    if (!value) return;
    gesture.current = { index, startY: value.y, startValue: value.gain, dragging: false };
  };

  const moveGesture = (index: number, event: any) => {
    const active = gesture.current;
    const value = valueFromEvent(event);
    if (!active || active.index !== index || !value) return;
    if (!active.dragging && Math.abs(value.y - active.startY) < 2) return;
    if (!active.dragging) {
      dispatch('paint_start');
      active.dragging = true;
    }
    dispatch('paint', {
      mode: 'Sculpt', start_band: index, start_value: active.startValue,
      current_band: index, current_value: value.gain, n_visible: BAND_COUNT,
    });
  };

  const endGesture = (index: number) => {
    const active = gesture.current;
    gesture.current = null;
    if (!active || active.index !== index) return;
    if (active.dragging) {
      dispatch('paint_end');
      return;
    }
    const nextMuted = [...current.current.muted];
    nextMuted[index] = !nextMuted[index];
    dispatch('band_field_set', {
      n_visible: BAND_COUNT,
      gain_db: current.current.gain_db,
      muted: nextMuted,
    });
  };

  return (
    <View id="spectr-native-root" width={DESIGN_WIDTH} height={DESIGN_HEIGHT}
          background="#05070a" padding={24} gap={18}>
      <Row height={56} alignItems="center" justifyContent="space-between">
        <Row alignItems="center" gap={14}>
          <Label textColor="#f5f8fb">SPECTR</Label>
          <Label textColor="#65717e">NATIVE N1 / QUICKJS</Label>
        </Row>
        <Row alignItems="center" gap={18}>
          <Label textColor="#7e8b98">{Math.round(editorState.min_hz)} Hz</Label>
          <Label textColor="#2be1ff">32 BANDS</Label>
          <Label textColor="#7e8b98">{Math.round(editorState.max_hz / 1000)} kHz</Label>
        </Row>
      </Row>

      <View height={350} background="#080b10" border={{ color: '#1c2630', width: 1, radius: 8 }}
            padding={18}>
        <AnalyzerCanvas magnitudes={magnitudes} />
      </View>

      <Row height={330} gap={5} alignItems="end">
        {editorState.gain_db.map((gainDb, index) => {
          const muted = editorState.muted[index];
          const fill = muted ? '#32151b' : gainDb >= 0 ? '#123943' : '#17212b';
          const height = 120 + (gainDb + 24) * 3.75;
          return (
            <Button key={index} id={`spectr-band-${index}`} width={34} height={height}
                    background={fill} border={{ color: muted ? '#ff5964' : '#2be1ff', width: 1, radius: 3 }}
                    textColor={muted ? '#ff7a84' : '#d8f8ff'}
                    onPointerDown={(event: any) => beginGesture(index, event)}
                    onPointerMove={(event: any) => moveGesture(index, event)}
                    onPointerUp={() => endGesture(index)}
                    onPointerCancel={() => { gesture.current = null; }}>
              {String(index + 1)}
            </Button>
          );
        })}
      </Row>

      <Row height={32} justifyContent="space-between" alignItems="center">
        <Label textColor="#596673">TAP MUTE / SCULPT: AUTHORITATIVE C++ ROUND-TRIP</Label>
        <Label textColor="#596673">REV {editorState.revision} / LIVE NATIVE ANALYZER</Label>
      </Row>
    </View>
  );
}

render(<App />);
