#!/usr/bin/env python3
"""Verify installed macOS package products against an exact release source."""

from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path


PRODUCTS = {
    "Spectr.app.pkg/Payload/Applications/Spectr.app/Contents/Resources/pulp-build-info.json": "Standalone",
    "Spectr.au.pkg/Payload/Spectr.component/Contents/Resources/pulp-build-info.json": "AU",
    "Spectr.vst3.pkg/Payload/Spectr.vst3/Contents/Resources/pulp-build-info.json": "VST3",
    "Spectr.clap.pkg/Payload/Spectr.clap/Contents/Resources/pulp-build-info.json": "CLAP",
}


def check_package(path: Path, version: str, source_sha: str) -> None:
    with tempfile.TemporaryDirectory(prefix="spectr-release-pkg-") as directory:
        expanded = Path(directory) / "expanded"
        subprocess.run(["pkgutil", "--expand-full", str(path), str(expanded)],
                       check=True, capture_output=True)
        for relative, fmt in PRODUCTS.items():
            info_path = expanded / relative
            if not info_path.is_file():
                raise ValueError(f"{path}: missing {fmt} build provenance")
            info = json.loads(info_path.read_text(encoding="utf-8"))
            product = info.get("product", {})
            build = info.get("build", {})
            if (info.get("schema") != "pulp.build-info.v1"
                    or product.get("name") != "Spectr"
                    or product.get("format") != fmt
                    or product.get("version") != version
                    or product.get("source_git_sha") != source_sha
                    or product.get("source_git_dirty") is not False
                    or build.get("type") != "Release"
                    or "arm64" not in build.get("archs", [])):
                raise ValueError(f"{path}: {fmt} does not match release source/version")
