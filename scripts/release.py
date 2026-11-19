#!/usr/bin/env python3
"""Unified zod-ee release entry point; implementation lives in tools/release.py."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[1]/'tools/release.py'),run_name='__main__')
