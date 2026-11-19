#!/usr/bin/env python3
"""Reject game media in the Git index and, optionally, reachable history."""
import argparse,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import source_policy
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--history',action='store_true');args=parser.parse_args()
root=Path(subprocess.check_output(['git','rev-parse','--show-toplevel'],text=True).strip())
paths=subprocess.check_output(['git','-C',str(root),'ls-files','-z'],text=True).split('\0')
blocked=sorted({p for p in paths if p and source_policy.forbidden(p)})
if args.history:blocked=sorted(set(blocked+source_policy.history_paths(root)))
if blocked:
 print('Source audit blocked: imported media exists in the index/history.',file=sys.stderr)
 for name in blocked[:12]:print('  '+name,file=sys.stderr)
 print('Create a source-only export with tools/export_source.py; do not push inherited history.',file=sys.stderr);sys.exit(1)
print('Source audit passed: no imported media in '+('index or history' if args.history else 'index'))
