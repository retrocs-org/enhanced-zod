"""Repository policy: source and authored web vectors, never imported game media."""
from pathlib import Path
import hashlib

MEDIA_SUFFIXES={'.png','.bmp','.jpg','.jpeg','.gif','.webp','.ico','.icns','.xcf','.wav','.ogg','.mp3','.mid','.midi','.xmi','.mp4','.avi','.sf2','.sf3','.ttf','.otf','.map','.iso','.bin','.cue','.rsc','.blk','.pal','.lbm','.dat','.raw','.lst','.zip','.rar','.7z','.tar','.gz','.bz2','.exe','.dll','.dylib','.so','.a','.o','.flac','.aiff','.tga','.mov','.webm'}

def forbidden(name):
    path=Path(name);lower=path.as_posix().lower()
    if lower.startswith(('assets/original-z/','assets/videos/')):
        return lower not in {'assets/original-z/readme.md','assets/videos/readme.md'}
    if lower.startswith(('assets/','maps/')):
        return path.suffix.lower() not in {'.md'} and not (path.suffix.lower()=='.txt' and ('license' in path.name.lower() or path.name=='README.txt' or path.name.startswith('OFL')))
    if lower.startswith('tools/z_graphics_patches/'):return True
    return path.suffix.lower() in MEDIA_SUFFIXES

def require_allowed(name):
    if forbidden(name):raise ValueError('Imported/binary content is not source: '+name)

def history_paths(root,ref='HEAD'):
    import subprocess
    output=subprocess.check_output(['git','-C',str(root),'log',ref,'--format=','--name-only','-z']).decode().split('\0')
    return sorted({name.strip('\n') for name in output if name.strip('\n') and forbidden(name.strip('\n'))})
