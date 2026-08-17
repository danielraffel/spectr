# Spectr N0 Native Audit Receipt

Date: 2026-08-11
Status: Complete; N1 authorized only under the corrected live-native contract

## Frozen reference

- Spectr WebView baseline: `5486b009d7431ef3a28257500613f5d3c9371d25`
- Current native branch base before this receipt:
  `c38ed0c09314b22eb01b28af830a0d5b3755e946`
- Original Claude HTML SHA-256:
  `7ae6f1d807f2f356b8473d9e672a95535adbe7affea58af360f9ac5c211daf9e`
- Frozen adapted `resources/editor.html` SHA-256:
  `189e6ebd65c3d18dec9a043ff7bd1fb8d96f5906e4120bf2105a9e67805ee6dd`
- Frozen representative screenshot SHA-256:
  `da6eee1f90bdb8170a38cbe5ecfc45ca71a6d17017bfc34accaa6d012fde613d`
- Frozen 139/139 Release CTest log SHA-256:
  `984c7b14f11ceeaabee52c21bb2ad1108986cafe01f3b67de25ebf5806d1bb64`

## Pulp audit truth

- Audited Pulp `origin/main`:
  `34f879e1a71aec8a34cea13f62600586d0eb79a7`
- Isolated resource-hint importer fix:
  `87aa81be0e8c41f6736d784502f93cf01650fefb`
- Release `pulp-test-design-import`: 3,008 assertions / 424 cases, green.
- Runtime walker completed without its static-parser fallback and emitted a
  94-node DesignIR snapshot, proving that the negative result below is an
  architectural limitation rather than an import crash.

## Negative oracle

The live-bundle-to-static-DesignIR experiment produced an inert upper-left
stack of controls with a blank canvas at 1320x860. It lost the React controller,
listeners, effects, animation loop, analyzer program, and interaction state.

- Runtime DesignIR SHA-256:
  `2ab41181f3575376d23205c6e4495d954f142424bcfe1ced2092fb1408582fe6`
- Runtime-native PNG SHA-256:
  `c254be68109ec2aba2fa55209bba3406769185cb8b210fc7eefe7d3c78acb6c2`
- Browser-capture DesignIR SHA-256:
  `8f8dc09c28e23ee2c4b93de07c0b572a8d97e80147d1295e32a0e0c53a23defd`
- Browser-capture native-panel PNG SHA-256:
  `3d3e78df680c52351523db3ffea8c4ef3578c49faa0f3c542bd15b9f82cb2123`

## Architecture decision

Spectr native uses a live QuickJS controller with `@pulp/react` native Views and
CanvasWidgets, painted by Skia Graphite on Dawn. DesignIR and browser capture are
audit/evidence tools only. The native artifact fails closed on static,
screenshot, blank/error, CPU-renderer, unavailable-bridge, or WebView fallback.

The first acceptable N1 slice must prove real finite C++ hydration, a real
analyzer frame, the real 32-band canvas, tap-to-mute, and sculpt-drag through a
revisioned C++ state round trip. Static chrome or screenshot similarity is not
acceptance evidence.

The first native-only product scaffold also proved that excluding Spectr's
WebView sources and HTML is not enough: the installed Pulp `view-core` link
interface still placed `WebKit.framework` in the final executable alongside
JavaScriptCore and wgpu. Removing that transitive dependency is a generalized
Pulp link-floor prerequisite and an explicit N5 binary-inspection gate.
