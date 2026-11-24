#!/usr/bin/env python3
"""Package a native Linux headless Zod server with its ELF library closure."""
import argparse,os,pathlib,platform,re,shutil,subprocess,tarfile,tempfile,sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from runtime_data import copy_runtime
from notices import inventory
p=argparse.ArgumentParser();p.add_argument('--engine',required=True,type=pathlib.Path);p.add_argument('--agent',required=True,type=pathlib.Path);p.add_argument('--root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[2]);p.add_argument('--out',required=True,type=pathlib.Path);args=p.parse_args()
if platform.system()!='Linux':p.error('Run this packager on Linux against a native Linux engine')
arch={'aarch64':'arm64','x86_64':'x86_64'}.get(platform.machine(),platform.machine())
with tempfile.TemporaryDirectory(prefix='zod-package-') as temporary:
 dest=pathlib.Path(temporary)/('zod-enhanced-server-linux-'+arch);dest.mkdir();lib=dest/'lib';lib.mkdir()
 closure=subprocess.check_output(['ldd',str(args.engine.resolve())],env={**os.environ,'LC_ALL':'C'},text=True)
 if 'not found' in closure:raise RuntimeError('Missing runtime dependency:\n'+closure)
 libraries=set(re.findall(r'(/[^\s()]+)',closure));loader=None
 for item in sorted(libraries):
  source=pathlib.Path(item)
  if not source.is_file():continue
  shutil.copy2(source,lib/source.name,follow_symlinks=True)
  if 'ld-linux' in source.name or source.name.startswith('ld-musl'):loader=source.name
 if not loader:raise RuntimeError('Could not identify ELF loader')
 shutil.copy2(args.engine,dest/'zod-enhanced-bin');shutil.copy2(args.agent,dest/'zod-agent')
 copy_runtime(args.root,dest,website=True)
 shutil.copy2(args.root/'docs/NETWORK-SERVICES.md',dest/'CLOUD-README.md')
 (dest/'zod-enhanced').write_text('#!/bin/sh\nset -eu\nroot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec "$root/lib/'+loader+'" --library-path "$root/lib" "$root/zod-enhanced-bin" "$@"\n')
 (dest/'start-server').write_text('''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
state=${XDG_STATE_HOME:-"$HOME/.local/state"}/zod-server
exec "$root/zod-agent" --engine "$root/zod-enhanced" --data "$root" --state "$state" "$@"
''')
 (dest/'README.txt').write_text('''zod-enhanced Linux headless server

Private server (guests welcome; no website needed):
  ./start-server

Public server (every human must use a Zod cloud join ticket):
  ./start-server --mode public --hub https://YOUR-ZOD-SITE --id YOUR-SERVER-ID --key-file /path/to/key --name "My Zod server"

Open/forward TCP 2137 for remote clients. Default map is maps/p02_bb_orig01.map.
Use --map and --port to change these; --state selects a per-server writable directory.
Extra engine options follow -- (for example -- -b blue -D 2).

This package includes the engine's Linux shared libraries and ELF loader.
It starts no window, audio device, X server or GPU context. Run as an ordinary
user, never root. See CLOUD-README.md for the independent Zod web service,
PHP/MariaDB account service, deployment and current limitations. It does not use OpenFP.
''')
 for relative in (args.root/'packaging/imported-assets.txt').read_text().splitlines():
  if relative and not relative.startswith('#'):
   imported=dest/'assets'/relative
   if imported.is_file():imported.unlink()
 for name in ['zod-enhanced','start-server','zod-enhanced-bin','zod-agent']:
  path=dest/name
  if path.exists():path.chmod(0o755)
 inventory(dest,dest/"ThirdParty",[pathlib.Path(p) for p in libraries])
 subprocess.run([sys.executable,str(args.root/"tools/package_smoke.py"),"--package",str(dest),"--binary","zod-enhanced"],check=True)
 args.out.parent.mkdir(parents=True,exist_ok=True)
 with tarfile.open(args.out,'w:gz') as archive:archive.add(dest,arcname=dest.name)
 print(args.out)
