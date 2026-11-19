#!/usr/bin/env python3
"""Fetch hash-locked OFL UI font dependencies into ignored local build inputs."""
import hashlib,json,shutil,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def bootstrap(root=ROOT):
    for name,entry in json.loads((root/'packaging/font-sources.json').read_text()).items():
        target=root/name
        if target.is_file():continue
        data=urllib.request.urlopen(entry['url'],timeout=60).read()
        if hashlib.sha256(data).hexdigest()!=entry['sha256']:raise ValueError('Font checksum mismatch: '+name)
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    target=root/'web-service/php/public/assets/heading.ttf';target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists():shutil.copy2(root/'assets/frontend/fonts/Rajdhani-SemiBold.ttf',target)
if __name__=='__main__':bootstrap()
