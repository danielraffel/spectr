// Spectr native editor — implemented via @pulp/react instead of an
// embedded WebView. Renders directly through Pulp's WidgetBridge →
// Yoga → Skia → Dawn pipeline. See pulp #772 + spectr #25.
//
// v0.1 scope: full Spectr chrome layout exercising the bridge widgets
// that the actual editor will use — header, analyzer Spectrum, filter
// bank placeholder, transport row of Knob controls. Wire to live data
// (Spectr's StateStore params, VisualizationBridge analyzer feed)
// happens in a follow-up commit once the C++ side calls setSpectrumData
// + setValue from process().

import {
    render,
    View, Row, Label, Spectrum, Knob, Fader,
    createMockBridge,
} from '@pulp/react';
import { createElement } from 'react';

void createMockBridge;

interface EditorProps {
    analyzerData?: number[] | Float32Array;
    // Spectr StateStore param values, normalized 0..1 for the controls
    // when the C++ side starts pushing live values.
    mix?: number;
    output?: number;
    response?: number;
    engine?: number;
    bands?: number;
    morph?: number;
}

// Demo data so the v0.1 standalone smoke render shows something
// meaningful in the spectrum panel even without the live analyzer feed.
function makeStubSpectrum(n: number): number[] {
    const out: number[] = [];
    for (let i = 0; i < n; i++) {
        // Two-bump curve: low energy + a higher mid bump
        const t = i / (n - 1);
        const low = Math.exp(-Math.pow((t - 0.18) / 0.12, 2));
        const mid = 0.7 * Math.exp(-Math.pow((t - 0.55) / 0.18, 2));
        out.push(0.06 + 0.85 * (low + mid) / 1.4);
    }
    return out;
}

function App({
    analyzerData,
    mix = 1.0,
    output = 0.5,
    response = 1.0,
    engine = 0.5,
    bands = 0.0,
    morph = 0.0,
}: EditorProps) {
    const spectrumData = analyzerData ?? makeStubSpectrum(64);

    return (
        <View width={1200} height={800} background="#05070a">
            {/* Header strip */}
            <Row
                width={1200}
                height={44}
                paddingLeft={20}
                paddingRight={20}
                alignItems="center"
                gap={16}
                background="#0a0e14"
            >
                <Label textColor="#e8edf2">SPECTR</Label>
                <Label textColor="#6b7380">— zoomable filter bank</Label>
            </Row>

            {/* Spectrum analyzer band — stable id "spectrum" so the
                C++ side can call setSpectrumData('spectrum', [...])
                from VisualizationBridge per audio block. */}
            <View width={1200} height={220} background="#070a0e" paddingLeft={20} paddingRight={20} paddingTop={12} paddingBottom={12}>
                <Spectrum id="spectrum" data={spectrumData} width={1160} height={196} />
            </View>

            {/* Filter bank visualization area (placeholder for the band-field UI) */}
            <View width={1200} flexGrow={1} background="#05070a" paddingLeft={20} paddingRight={20} paddingTop={20} paddingBottom={20}>
                <Label textColor="#6b7380">filter bank — band field, viewport, edit modes (S/L/B/F/G)</Label>
            </View>

            {/* Transport / parameter row — Knobs for the six top-level
                Spectr params. Stable IDs match StateStore param names so
                C++ pushes via setValue('mix', 0.42) etc. The bridge's
                __dispatch__ routes user gestures back through onChange
                once the bridge wires the click → param-set path. */}
            <Row
                width={1200}
                height={92}
                paddingLeft={20}
                paddingRight={20}
                alignItems="center"
                gap={20}
                background="#0a0e14"
            >
                <Knob id="mix" value={mix} width={56} height={56} />
                <Label textColor="#a3a8b5">MIX</Label>

                <Knob id="output" value={output} width={56} height={56} />
                <Label textColor="#a3a8b5">OUTPUT</Label>

                <Knob id="response" value={response} width={56} height={56} />
                <Label textColor="#a3a8b5">RESPONSE</Label>

                <Knob id="engine" value={engine} width={56} height={56} />
                <Label textColor="#a3a8b5">ENGINE</Label>

                <Knob id="bands" value={bands} width={56} height={56} />
                <Label textColor="#a3a8b5">BANDS</Label>

                <Fader id="morph" value={morph} orientation="horizontal" width={160} height={28} />
                <Label textColor="#a3a8b5">A / B / MORPH</Label>
            </Row>
        </View>
    );
}

// Pulp's WidgetBridge runs this script directly — boot React at load.
render(createElement(App, {}));

// Per-frame analyzer push. NativeEditorView (C++) registers a global
// __spectrumTick that pulls the latest spectrum frame from
// VisualizationBridge and pushes via setSpectrumData('spectrum', ...).
// We just need to keep calling it — Pulp's host frame loop pumps
// requestAnimationFrame via service_frame_callbacks().
const g = globalThis as unknown as {
    __spectrumTick?: () => void;
    requestAnimationFrame?: (cb: () => void) => number;
};
if (typeof g.requestAnimationFrame === 'function' &&
    typeof g.__spectrumTick === 'function') {
    const loop = () => {
        try { g.__spectrumTick!(); } catch (_e) { /* swallow per-frame */ }
        g.requestAnimationFrame!(loop);
    };
    g.requestAnimationFrame(loop);
}
