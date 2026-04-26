// editor-port.tsx — Phase-1 host for spectr-editor-extracted.js.
//
// Order matters: host-shims.ts side-effects (React/ReactDOM/window/document)
// MUST land on globalThis before the extracted code imports. ESM evaluates
// imports in source order; we rely on that.
//
// pulp #779 / spectr #28.

import './host-shims.js';                 // populate globals
import './spectr-editor-extracted.js';    // boots the App via ReactDOM.createRoot
