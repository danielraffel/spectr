#!/usr/bin/env python3
"""Write the digest of a just-built executable for an authenticated CTest row."""
import argparse
import hashlib
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--binary", required=True)
parser.add_argument("--output", required=True)
args = parser.parse_args()
binary = Path(args.binary).resolve()
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
Path(args.output).resolve().write_text(digest + "\n")
