#!/usr/bin/env python3
"""Assemble a native Windows runtime and its complete non-system DLL closure."""
import argparse
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'packaging'))
from runtime_data import copy_runtime
from notices import inventory

def package(build,helpers,destination,search):
    if destination.exists():raise RuntimeError('Choose a fresh package destination')
    destination.mkdir(parents=True)
    roots=[build/'zod-enhanced.exe',build/'zod_mapgen.exe',helpers/'zod-asset-importer.exe',helpers/'ffmpeg.exe',helpers/'ffprobe.exe',helpers/'fluidsynth.exe']
    if (build/'zod_map_editor.exe').is_file():roots.append(build/'zod_map_editor.exe')
    directories=[build,helpers,*search];lookup={}
    for directory in directories:
        for path in directory.glob('*.dll'):lookup.setdefault(path.name.lower(),path)
    system=Path(os.environ.get('SystemRoot','C:/Windows'))/'System32'
    queue=list(roots);seen=set();sources=[]
    while queue:
        source=queue.pop(0)
        if source.name.lower() in seen:continue
        if not source.is_file():raise RuntimeError('Missing runtime executable or dependency: '+source.name)
        sources.append(source);seen.add(source.name.lower());shutil.copy2(source,destination/source.name)
        output=subprocess.check_output(['objdump','-p',str(source)],text=True,errors='replace')
        for name in re.findall(r'DLL Name:\s*(\S+)',output,re.I):
            dependency=lookup.get(name.lower())
            if dependency is not None:queue.append(dependency)
            elif not (system/name).exists() and not name.lower().startswith(('api-ms-','ext-ms-')):raise RuntimeError('Unresolved DLL: '+name)
    copy_runtime(ROOT,destination)
    inventory(destination,destination/"ThirdParty",sources)
    (destination/'READ_ME_FIRST.txt').write_text('zod-enhanced — Zod Engine Enhanced\n\nRun zod-enhanced.exe. Import your original disc on first launch.\nCampaign packs go in Documents/Zod Campaigns.\n',encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--helpers',type=Path,required=True);p.add_argument('--destination',type=Path,required=True);p.add_argument('--search-dir',type=Path,action='append',default=[]);a=p.parse_args()
    package(a.build,a.helpers,a.destination,a.search_dir)
