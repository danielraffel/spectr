# WP-1 parser toolchain

The dependency manifest experiment uses the pinned `@babel/parser` package to
parse JavaScript/JSX. Install the lockfile before running the parser-backed
checks:

```sh
npm ci --prefix tools/wp1-parser
python3 tools/test_wp1_dependency_manifest.py
```

The parser is intentionally scoped to this experiment. It does not compile or
rewrite Spectr's materialized runtime. A missing install is a hard failure in
the parser CLI, so a regex fallback cannot produce a false-green manifest.
