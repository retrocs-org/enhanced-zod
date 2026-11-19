"""Project-authored synthetic levels for tests; contains no original game layout."""
import ctypes,pathlib,struct
class Header(ctypes.Structure):
    _fields_=[('width',ctypes.c_ushort),('height',ctypes.c_ushort),('name',ctypes.c_char*50),('players',ctypes.c_ubyte),('objects',ctypes.c_ushort),('terrain',ctypes.c_ubyte),('zones',ctypes.c_ushort)]
class Object(ctypes.Structure):
    _fields_=[('x',ctypes.c_ushort),('y',ctypes.c_ushort),('owner',ctypes.c_byte),('type',ctypes.c_ubyte),('id',ctypes.c_ubyte),('level',ctypes.c_byte),('links',ctypes.c_ushort),('health',ctypes.c_int)]
def write_map(path):
    path=pathlib.Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    # Two forts and factories in three simple rectangular zones. All flat terrain.
    objects=[Object(6,6,1,2,0,0,0,100),Object(50,65,2,2,0,0,0,100),Object(18,15,1,2,4,0,0,100),Object(43,65,2,2,4,0,0,100),Object(14,14,1,5,0,0,0,100),Object(49,72,2,5,0,0,0,100),Object(24,16,1,4,1,0,0,100),Object(45,75,2,4,1,0,0,100)]
    header=Header(64,86,b'Authored test level',2,len(objects),0,3)
    zones=b''.join(struct.pack('=HHHH',5,y,54,h) for y,h in [(5,20),(30,20),(60,25)])
    path.write_bytes(bytes(header)+zones+b''.join(bytes(x) for x in objects)+struct.pack('=H',0)*(64*86))
def workspace(root,destination):
    import shutil
    root=pathlib.Path(root);destination=pathlib.Path(destination);destination.mkdir(parents=True,exist_ok=True)
    for name in ['map_list.txt','default_settings.txt']:shutil.copy2(root/name,destination/name)
    fonts=destination/'assets/frontend/fonts';fonts.mkdir(parents=True,exist_ok=True)
    for font in (root/'assets/frontend/fonts').glob('*'):
        if font.is_file():shutil.copy2(font,fonts/font.name)
    # Authored flat-colour terrain atlas, not original game pixels.
    width,height=320,384;row=bytes([40,60,90])*width;pixels=row*height
    bmp=struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)+struct.pack('<IiiHHIIiiII',40,width,height,1,24,0,len(pixels),0,0,0,0)+pixels
    planets=destination/'assets/planets';planets.mkdir(parents=True,exist_ok=True)
    for planet in ['desert','volcanic','arctic','jungle','city']:
        (planets/(planet+'.bmp')).write_bytes(bmp)
        info=b''.join(struct.pack('=6BHBhB',int(400<=i<440),1,1,int(i>=460),0,0,0,1,-1,int(i<10)) for i in range(480))
        (planets/(planet+'.tileinfo')).write_bytes(info)
    for line in (root/'map_list.txt').read_text().splitlines():
        if line.startswith('maps/') and line.endswith('.map'):write_map(destination/line)
    for name in ['p03_bb_p03m01.map','p04_bb_p04m01.map','p08_bb_p08m01.map','p08_sc_hunters.map']:write_map(destination/'maps'/name)
