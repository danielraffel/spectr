# DSPX-08 Spectr Forge adapter receipt

**Receipt date:** 2026-10-08

**Spectr source head:** `2ddccee98a51773f6b9e2a3bcbc14aadcfeb65a4`

**Pulp SDK source head:** `78b214f871f73a9842f80e140a887aff2de24609`

**SDK:** `0.935.1`, Release, exact merged SDK prefix
`/Volumes/Workshop/Code/pulp-sdk-merged-78b214f871f73a9842f80e140a887aff2de24609`

**SDK provenance SHA-256:**
`65e1d0699fb8813f83e377348d3776e9c2bfe7578863f832249754a5a44c0ac4`

**Distribution eligibility:** `false` (development SDK; this receipt is a
consumer and graph contract proof, not a release-package claim).

## Exact build and test

The merged PR251 head was checked out cleanly and configured with the exact
SDK and source pins:

```sh
cmake -S . -B build-dspx08-forge-adapter-receipt -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPulp_DIR=/Volumes/Workshop/Code/pulp-sdk-merged-78b214f871f73a9842f80e140a887aff2de24609/lib/cmake/Pulp \
  -DPULP_SDK_SOURCE_GIT_SHA=78b214f871f73a9842f80e140a887aff2de24609 \
  -DSPECTR_EXPECTED_PRODUCT_GIT_SHA=2ddccee98a51773f6b9e2a3bcbc14aadcfeb65a4 \
  -DSPECTR_EXPECTED_PULP_SDK_SHA=78b214f871f73a9842f80e140a887aff2de24609 \
  -DSPECTR_BUILD_FORGE_ADAPTER=ON
/Volumes/Workshop/Code/pulp/tools/ci/governed-build.sh \
  cmake --build build-dspx08-forge-adapter-receipt \
  --target Spectr-forge-adapter-test
ctest --test-dir build-dspx08-forge-adapter-receipt \
  -R '^Spectr Forge adapter' --output-on-failure
```

Result: **4/4 passed** (factory, product controls, SignalGraph
`ProcessorNode` render against an independent host oracle, and the typed GPU
capability negative).

The resulting test executable is 51,645,064 bytes with SHA-256
`4c604f791363a1156b0ecf231448a4945c4381aaf1f7fd1870e52ed92c66a5d7`.

## Ownership boundary

`Spectr::ForgeAdapter` is an opt-in interface over Spectr's existing
`Processor` factory. Spectr remains authoritative for DSP, parameters, GPU
provider policy, and CPU fallback. A graph host owns only `ProcessorNode`
placement and routing. This receipt does not add Spectr to the Forge catalog,
copy Spectr sources into Forge, or claim a Forge product bundle. Any future
Forge catalog registration needs a separate owner-reviewed integration and
package receipt.
