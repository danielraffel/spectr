import React, { useLayoutEffect, useRef, useState } from 'react';
import { Button, Canvas, Label, Row, View, render } from '@pulp/react';

type Band = { gainDb: number; muted: boolean };
type Hydration = {
  bands: Band[];
  viewport: { minHz: number; maxHz: number };
};

declare global {
  // These are deliberately narrow native ingress seams. N1 does not pretend
  // to have a general bidirectional product bridge.
  var __spectrHydrate: ((payload: Hydration) => void) | undefined;
  var __spectrAnalyzer: ((magnitudes: number[]) => void) | undefined;
}

const DESIGN_WIDTH = 1320;
const DESIGN_HEIGHT = 860;
const BAND_COUNT = 32;

function clampGain(value: number): number {
  return Math.max(-24, Math.min(24, value));
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
    for (let x = 0; x <= 1000; x += 125) {
      call('canvasStrokeLine', x, 0, x, 300, '#202a35', 1);
    }
    for (let y = 0; y <= 300; y += 60) {
      call('canvasStrokeLine', 0, y, 1000, y, '#202a35', 1);
    }

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
  const [bands, setBands] = useState<Band[]>(
    () => Array.from({ length: BAND_COUNT }, () => ({ gainDb: 0, muted: false })),
  );
  const [magnitudes, setMagnitudes] = useState<number[]>([]);
  const [viewport, setViewport] = useState({ minHz: 20, maxHz: 20000 });
  const sculpting = useRef(false);

  useLayoutEffect(() => {
    globalThis.__spectrHydrate = payload => {
      if (!payload || !Array.isArray(payload.bands) || payload.bands.length !== BAND_COUNT) return;
      setBands(payload.bands.map(band => ({
        gainDb: clampGain(Number.isFinite(band.gainDb) ? band.gainDb : 0),
        muted: band.muted === true,
      })));
      if (payload.viewport && Number.isFinite(payload.viewport.minHz)
          && Number.isFinite(payload.viewport.maxHz)) {
        setViewport(payload.viewport);
      }
    };
    globalThis.__spectrAnalyzer = next => {
      if (Array.isArray(next) && next.length > 1 && next.every(Number.isFinite))
        setMagnitudes(next);
    };
    return () => {
      globalThis.__spectrHydrate = undefined;
      globalThis.__spectrAnalyzer = undefined;
    };
  }, []);

  const sculpt = (index: number, event: any) => {
    const bounds = event.currentTarget?.getBoundingClientRect?.();
    if (!bounds || bounds.height <= 0 || !Number.isFinite(event.clientY)) return;
    const normalized = 1 - (event.clientY - bounds.top) / bounds.height;
    const gainDb = clampGain(normalized * 48 - 24);
    setBands(current => current.map((band, i) => i === index ? { ...band, gainDb } : band));
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
          <Label textColor="#7e8b98">{Math.round(viewport.minHz)} Hz</Label>
          <Label textColor="#2be1ff">32 BANDS</Label>
          <Label textColor="#7e8b98">{Math.round(viewport.maxHz / 1000)} kHz</Label>
        </Row>
      </Row>

      <View height={350} background="#080b10" border={{ color: '#1c2630', width: 1, radius: 8 }}
            padding={18}>
        <AnalyzerCanvas magnitudes={magnitudes} />
      </View>

      <Row height={330} gap={5} alignItems="end">
        {bands.map((band, index) => {
          const fill = band.muted ? '#32151b' : band.gainDb >= 0 ? '#123943' : '#17212b';
          const height = 120 + (band.gainDb + 24) * 3.75;
          return (
            <Button key={index} id={`spectr-band-${index}`} width={34} height={height}
                    background={fill} border={{ color: band.muted ? '#ff5964' : '#2be1ff', width: 1, radius: 3 }}
                    textColor={band.muted ? '#ff7a84' : '#d8f8ff'}
                    onClick={() => setBands(current => current.map((item, i) =>
                      i === index ? { ...item, muted: !item.muted } : item))}
                    onPointerDown={(event: any) => { sculpting.current = true; sculpt(index, event); }}
                    onPointerMove={(event: any) => { if (sculpting.current) sculpt(index, event); }}
                    onPointerUp={() => { sculpting.current = false; }}
                    onPointerCancel={() => { sculpting.current = false; }}>
              {String(index + 1)}
            </Button>
          );
        })}
      </Row>

      <Row height={32} justifyContent="space-between" alignItems="center">
        <Label textColor="#596673">INPUT PROBE: TAP / SCULPT IS UI-LOCAL IN N1</Label>
        <Label textColor="#596673">LIVE ANALYZER DATA FROM VISUALIZATIONBRIDGE</Label>
      </Row>
    </View>
  );
}

render(<App />);
