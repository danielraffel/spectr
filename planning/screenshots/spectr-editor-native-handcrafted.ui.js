// Hand-crafted Spectr editor v2 — uses the actual flex API the bridge
// exposes (gap, padding, align_items, min_width, setBorder with radius,
// setTextColor). Targets pixel-fidelity with Chrome reference within
// the limits of pulp::view widget primitives today.

setTheme('dark');

// ── color tokens (sampled from Chrome render) ───────────────────────
var BG_BASE     = '#05070a';
var BG_BAR      = '#0a0e14';
var BG_BTN      = '#1a1f28';
var BG_BTN_OFF  = '#0e1218';
var TXT_PRIMARY = '#ffffff';
var TXT_DIM     = '#a3a8b5';
var TXT_MUTED   = '#6e7588';
var BORDER_DIM  = '#1b1f28';
var ACCENT      = '#47b4eb';

// ── root: vertical column, fills viewport ──────────────────────────
createCol('root', '');
setFlex('root', 'flex_grow', 1);
setBackground('root', BG_BASE);

// ═══════════════════════════════════════════════════════════════════
// Top header bar (44pt, horizontal flex, gap 18, padding 0 20)
// ═══════════════════════════════════════════════════════════════════
createRow('topbar', 'root');
setFlex('topbar', 'height', 44);
setFlex('topbar', 'min_height', 44);
setFlex('topbar', 'gap', 14);
setFlex('topbar', 'padding_left', 20);
setFlex('topbar', 'padding_right', 20);
setFlex('topbar', 'align_items', 'center');
setBackground('topbar', BG_BAR);
setBorderSide('topbar', 'bottom', 1, '#191e25');

// brand: SPECTR · ZOOMABLE FILTER BANK
createRow('brand', 'topbar');
setFlex('brand', 'gap', 8);
setFlex('brand', 'align_items', 'center');

createLabel('logo', 'SPECTR', 'brand');
setTextColor('logo', TXT_PRIMARY);

createLabel('logo_dot', '·', 'brand');
setTextColor('logo_dot', TXT_MUTED);
setOpacity('logo_dot', 0.5);

createLabel('logo_sub', 'ZOOMABLE FILTER BANK', 'brand');
setTextColor('logo_sub', TXT_DIM);
setOpacity('logo_sub', 0.7);

// flexible spacer
createCol('spacer1', 'topbar');
setFlex('spacer1', 'flex_grow', 1);

// LIVE / PRECISION segmented control (active = LIVE)
createRow('seg_lp', 'topbar');
setFlex('seg_lp', 'height', 24);
setBorder('seg_lp', BORDER_DIM, 1, 3);

createPanel('btn_live', 'seg_lp');
setFlex('btn_live', 'min_width', 56);
setFlex('btn_live', 'padding_left', 10);
setFlex('btn_live', 'padding_right', 10);
setFlex('btn_live', 'align_items', 'center');
setFlex('btn_live', 'justify_content', 'center');
setBackground('btn_live', BG_BTN);
createLabel('btn_live_l', 'LIVE', 'btn_live');
setTextColor('btn_live_l', TXT_PRIMARY);

createPanel('btn_precision', 'seg_lp');
setFlex('btn_precision', 'min_width', 80);
setFlex('btn_precision', 'padding_left', 10);
setFlex('btn_precision', 'padding_right', 10);
setFlex('btn_precision', 'align_items', 'center');
setFlex('btn_precision', 'justify_content', 'center');
createLabel('btn_precision_l', 'PRECISION', 'btn_precision');
setTextColor('btn_precision_l', TXT_DIM);

// IIR / FFT / HYBRID segmented control (active = IIR)
createRow('seg_dsp', 'topbar');
setFlex('seg_dsp', 'height', 24);
setBorder('seg_dsp', BORDER_DIM, 1, 3);

createPanel('btn_iir', 'seg_dsp');
setFlex('btn_iir', 'min_width', 44);
setFlex('btn_iir', 'padding_left', 10);
setFlex('btn_iir', 'padding_right', 10);
setFlex('btn_iir', 'align_items', 'center');
setFlex('btn_iir', 'justify_content', 'center');
setBackground('btn_iir', BG_BTN);
createLabel('btn_iir_l', 'IIR', 'btn_iir');
setTextColor('btn_iir_l', TXT_PRIMARY);

createPanel('btn_fft', 'seg_dsp');
setFlex('btn_fft', 'min_width', 44);
setFlex('btn_fft', 'padding_left', 10);
setFlex('btn_fft', 'padding_right', 10);
setFlex('btn_fft', 'align_items', 'center');
setFlex('btn_fft', 'justify_content', 'center');
createLabel('btn_fft_l', 'FFT', 'btn_fft');
setTextColor('btn_fft_l', TXT_DIM);

createPanel('btn_hybrid', 'seg_dsp');
setFlex('btn_hybrid', 'min_width', 60);
setFlex('btn_hybrid', 'padding_left', 10);
setFlex('btn_hybrid', 'padding_right', 10);
setFlex('btn_hybrid', 'align_items', 'center');
setFlex('btn_hybrid', 'justify_content', 'center');
createLabel('btn_hybrid_l', 'HYBRID', 'btn_hybrid');
setTextColor('btn_hybrid_l', TXT_DIM);

// 64 bands ▾ button
createPanel('bands', 'topbar');
setFlex('bands', 'height', 22);
setFlex('bands', 'padding_left', 7);
setFlex('bands', 'padding_right', 7);
setFlex('bands', 'align_items', 'center');
setFlex('bands', 'justify_content', 'center');
setBorder('bands', BORDER_DIM, 1, 3);
createLabel('bands_l', '64 bands ▾', 'bands');
setTextColor('bands_l', TXT_DIM);

createLabel('zoom_dot', '·', 'topbar');
setTextColor('zoom_dot', TXT_MUTED);

createLabel('zoom', '1.00× zoom', 'topbar');
setTextColor('zoom', TXT_MUTED);

// ═══════════════════════════════════════════════════════════════════
// Middle: spectrum analyzer area (flex_grow 1)
// ═══════════════════════════════════════════════════════════════════
createPanel('analyzer', 'root');
setFlex('analyzer', 'flex_grow', 1);
setFlex('analyzer', 'align_items', 'center');
setFlex('analyzer', 'justify_content', 'center');
setBackground('analyzer', BG_BASE);

createLabel('analyzer_placeholder',
    '[ spectrum analyzer — wire to pulp::view::VisualizationBridge ]',
    'analyzer');
setTextColor('analyzer_placeholder', TXT_MUTED);
setOpacity('analyzer_placeholder', 0.35);

// ═══════════════════════════════════════════════════════════════════
// Bottom action rail (56pt, horizontal flex, gap 8, padding 0 20)
// ═══════════════════════════════════════════════════════════════════
createRow('bottomrail', 'root');
setFlex('bottomrail', 'height', 56);
setFlex('bottomrail', 'min_height', 56);
setFlex('bottomrail', 'gap', 6);
setFlex('bottomrail', 'padding_left', 20);
setFlex('bottomrail', 'padding_right', 20);
setFlex('bottomrail', 'align_items', 'center');
setBackground('bottomrail', BG_BAR);
setBorderSide('bottomrail', 'top', 1, '#191e25');

function chromeBtn(id, parent, text, minW) {
    createPanel(id, parent);
    setFlex(id, 'height', 26);
    setFlex(id, 'min_width', minW || 38);
    setFlex(id, 'padding_left', 10);
    setFlex(id, 'padding_right', 10);
    setFlex(id, 'align_items', 'center');
    setFlex(id, 'justify_content', 'center');
    setBackground(id, '#0d1117');
    setBorder(id, BORDER_DIM, 1, 3);
    createLabel(id + '_l', text, id);
    setTextColor(id + '_l', TXT_PRIMARY);
    setOpacity(id + '_l', 0.85);
}

chromeBtn('btn_clear', 'bottomrail', 'CLEAR', 56);
chromeBtn('btn_more', 'bottomrail', '⋯', 32);

// thin divider
createPanel('div1', 'bottomrail');
setFlex('div1', 'width', 1);
setFlex('div1', 'min_width', 1);
setFlex('div1', 'height', 22);
setBackground('div1', '#1b2028');

chromeBtn('btn_sculpt', 'bottomrail', 'SCULPT ▾', 88);
chromeBtn('btn_peak', 'bottomrail', 'PEAK ▾', 70);

createPanel('div2', 'bottomrail');
setFlex('div2', 'width', 1);
setFlex('div2', 'min_width', 1);
setFlex('div2', 'height', 22);
setBackground('div2', '#1b2028');

chromeBtn('btn_presets', 'bottomrail', 'PRESETS ▾', 90);

createPanel('div3', 'bottomrail');
setFlex('div3', 'width', 1);
setFlex('div3', 'min_width', 1);
setFlex('div3', 'height', 22);
setBackground('div3', '#1b2028');

createLabel('snap_label', 'SNAPSHOT', 'bottomrail');
setTextColor('snap_label', TXT_DIM);
setOpacity('snap_label', 0.55);

chromeBtn('btn_a', 'bottomrail', 'A', 34);
chromeBtn('btn_b', 'bottomrail', 'B', 34);
chromeBtn('btn_play_a', 'bottomrail', '▸ A', 34);
chromeBtn('btn_play_b', 'bottomrail', '▸ B', 34);

// morph slider track
createRow('morph', 'bottomrail');
setFlex('morph', 'gap', 6);
setFlex('morph', 'align_items', 'center');
setFlex('morph', 'margin_left', 6);
setOpacity('morph', 0.35);

createLabel('morph_a', 'A', 'morph');
setTextColor('morph_a', TXT_DIM);

createPanel('morph_track', 'morph');
setFlex('morph_track', 'width', 90);
setFlex('morph_track', 'min_width', 90);
setFlex('morph_track', 'height', 4);
setBackground('morph_track', '#2a3340');
setBorder('morph_track', '#2a3340', 0, 2);

createLabel('morph_b', 'B', 'morph');
setTextColor('morph_b', TXT_DIM);

// flexible spacer pushes settings/help to the right
createCol('spacer2', 'bottomrail');
setFlex('spacer2', 'flex_grow', 1);

createPanel('btn_settings', 'bottomrail');
setFlex('btn_settings', 'width', 26);
setFlex('btn_settings', 'min_width', 26);
setFlex('btn_settings', 'height', 26);
setFlex('btn_settings', 'align_items', 'center');
setFlex('btn_settings', 'justify_content', 'center');
setBorder('btn_settings', '#2a3340', 1, 13);
createLabel('btn_settings_l', '⚙', 'btn_settings');
setTextColor('btn_settings_l', TXT_DIM);

createPanel('btn_help', 'bottomrail');
setFlex('btn_help', 'width', 26);
setFlex('btn_help', 'min_width', 26);
setFlex('btn_help', 'height', 26);
setFlex('btn_help', 'align_items', 'center');
setFlex('btn_help', 'justify_content', 'center');
setBorder('btn_help', '#2a3340', 1, 13);
createLabel('btn_help_l', '?', 'btn_help');
setTextColor('btn_help_l', TXT_DIM);

void 0;
