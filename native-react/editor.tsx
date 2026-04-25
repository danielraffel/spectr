// Spectr native editor — implemented via @pulp/react instead of an
// embedded WebView. Renders directly through Pulp's WidgetBridge →
// Yoga → Skia → Dawn pipeline. See pulp #772 + spectr #25.
//
// v0 scope: render the Spectr chrome (header, spectrum placeholder,
// band field placeholder, footer controls). The full pulpit-port of
// the Claude Design editor.html will iterate from here. This file is
// the new contract — when we cut over, Spectr's create_view stops
// returning a WebView-backed EditorView and instead loads the IIFE
// bundle produced by `npm run build` in this directory.

import { render, View, Row, Label, Spectrum, Knob, createMockBridge } from '@pulp/react';
import { createElement } from 'react';

// Suppress unused-imports lint while the v0 stub doesn't use Spectrum/Knob.
void Spectrum;
void Knob;
void createMockBridge;

interface EditorState {
    // Will be populated from Spectr's analyzer feed via setSpectrumData
    // once the C++-side bridge wires up post-cutover. Stub for v0.
    analyzerData?: Float32Array;
}

function App({ analyzerData: _analyzerData }: EditorState) {
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
            </Row>

            {/* Spectrum + band-field area (filler for v0) */}
            <View width={1200} flexGrow={1} background="#070a0e">
                <Label textColor="#6b7380">native editor stub — bridge wire-up pending</Label>
            </View>

            {/* Footer / transport controls (placeholder) */}
            <Row
                width={1200}
                height={56}
                paddingLeft={20}
                paddingRight={20}
                alignItems="center"
                gap={12}
                background="#0a0e14"
            >
                <Label textColor="#6b7380">a / b / morph / snapshots / patterns</Label>
            </Row>
        </View>
    );
}

// Pulp's WidgetBridge runs this script directly — boot React at load.
render(createElement(App, { analyzerData: new Float32Array(64) }));
