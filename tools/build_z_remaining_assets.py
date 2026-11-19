#!/usr/bin/env python3
"""Replace the final legacy bitmaps with approved disc art and project UI recipes.

No legacy raster is embedded. Team palettes are semantic colour lookup tables.
Replacements preserve dimensions and control coordinates, not legacy pixels.
"""
import argparse, collections, hashlib, json, math
from pathlib import Path
from PIL import Image, ImageChops
from z_media import recipe_sources, render_recipe_set, render_extended_recipe, lbm_image, palette

ROOT = Path(__file__).resolve().parent.parent


def build(root):
    path = ROOT / 'tools/z_graphics_recipes.json'
    manifest = json.loads(path.read_text())
    basic = json.loads((ROOT / 'tools/z_graphics_map.json').read_text())
    originals = {p.relative_to(root).as_posix().upper(): p.read_bytes() for p in root.rglob('*')
                 if p.is_file() and p.suffix in ('.RSC', '.DAT', '.BLK', '.LBM', '.PAL')}
    raw = recipe_sources(originals)
    generated = dict(render_recipe_set(manifest, originals))
    sources = {**raw, **{'generated/' + n: i.convert('RGBA') for n, i in generated.items()}}
    remaining_path = ROOT / 'build-asset-cleanup/unmatched-graphics.json'
    remaining = json.loads(remaining_path.read_text())
    added = []

    def image(name):
        if name in generated:
            return generated[name].convert('RGBA')
        index, key, transparent = basic['sprites'][name]
        im = raw['sprite/' + str(index)].copy()
        im.putpalette(basic['palettes'][key])
        if transparent is not None:
            im.info['transparency'] = transparent
        return im.convert('RGBA')

    def part(name, crop=None, at=(0, 0), size=None):
        if name in generated:
            im = image(name)
            p = {'source': 'generated/' + name, 'copy': True,
                 'crop': crop or [0, 0, *im.size], 'at': list(at)}
        else:
            index, key, transparent = basic['sprites'][name]
            src = 'sprite/' + str(index)
            rgb = basic['palettes'][key]
            p = {'source': src, 'crop': crop or [0, 0, *raw[src].size], 'at': list(at),
                 'colours': {i: [*rgb[i*3:i*3+3], 0 if i == transparent else 255]
                             for i in set(raw[src].getdata())}}
        if size:
            p['resize'] = list(size)
        return p

    def rect(r, box, colour):
        r['shapes'].append({'kind': 'rectangle', 'box': list(box), 'colour': list(colour)})

    def line(r, points, colour, width=1):
        r['shapes'].append({'kind': 'line', 'points': [list(p) for p in points],
                            'colour': list(colour), 'width': width})

    def bevel(r, box=None, pressed=False, colour=(92, 94, 99, 255)):
        x, y, x2, y2 = box or (0, 0, r['size'][0]-1, r['size'][1]-1)
        rect(r, (x, y, x2, y2), colour)
        if x2 <= x or y2 <= y:
            return
        light, dark = (184, 190, 199, 255), (35, 37, 42, 255)
        if pressed:
            light, dark = dark, light
        line(r, [(x, y2), (x, y), (x2, y)], light)
        line(r, [(x2, y), (x2, y2), (x, y2)], dark)

    def text(r, value, box, font='small_white', scale=1):
        glyphs = []
        for c in value:
            n = f'fonts/{font}/char_{ord(c):03}.png'
            if n not in generated and n not in basic['sprites']:
                raise ValueError('Missing font glyph: ' + n)
            glyphs.append((n, image(n).size))
        width = sum(s[0] for _, s in glyphs)
        height = max((s[1] for _, s in glyphs), default=1)
        x, y, w, h = box
        factor = min(scale, w/max(1, width), h/height)
        sizes = [(max(1, round(s[0]*factor)), max(1, round(s[1]*factor))) for _, s in glyphs]
        px = x + max(0, (w-sum(s[0] for s in sizes))//2)
        py = y + max(0, (h-max(s[1] for s in sizes))//2)
        for (n, _), size in zip(glyphs, sizes):
            p = part(n, at=(px, py), size=size)
            p['blend'] = 'over'
            r['rgba_parts'].append(p)
            px += size[0]

    def caption(r, value):
        bevel(r)
        bevel(r, (3, 3, r['size'][0]-4, 16), colour=(119, 67, 22, 255))
        text(r, value, (4, 4, r['size'][0]-8, 11), 'yellow_menu')

    def source_piece(r, index, at, colours):
        name = 'sprite/' + str(index)
        im = raw[name]
        r['rgba_parts'].append({'source': name, 'crop': [0, 0, *im.size],
                                'at': list(at), 'colours': colours})

    for filename in remaining:
        original_path = ROOT / filename
        name = Path(filename).relative_to('assets').as_posix()
        original = Image.open(original_path)
        old = original.convert('RGBA')
        w, h = old.size
        r = {'size': [w, h], 'mode': original.mode, 'fill': [70, 73, 79, 255],
             'rgba_parts': [], 'shapes': [], 'legacy_equivalence': 'replacement',
             'replacement_reason': 'User approved close disc-based replacements for remaining artwork',
             'reference_sha256': hashlib.sha256(original_path.read_bytes()).hexdigest()}
        if name.startswith('teams/'):
            pairs = [{'from': list(old.getpixel((0,y))[:3]), 'to': list(old.getpixel((1,y))[:3])}
                     for y in range(h)]
            r = {'size': [2, h], 'mode': 'RGB', 'team_palette': pairs,
                 'legacy_equivalence': 'exact-config', 'reference_sha256': r['reference_sha256']}
        elif name in ('icon.png', 'icon_mac.png'):
            r = {**r, 'brand_icon': True, 'replacement_reason': 'Procedural project branding; no disc art required'}
        elif name.startswith('buildings/fort/'):
            biome = 'jungle' if 'jungle' in name else ('city' if 'city' in name else 'volcanic')
            base = {'jungle': 2613, 'city': 2431, 'volcanic': 2845}[biome]
            palname = {'jungle': 'JUNGLE', 'city': 'CITY', 'volcanic': 'VOLCAN'}[biome]
            rgb = palette(originals[f'UNPACKED/MAIN/{palname}.PAL'])
            colours = {i: rgb[i*3:i*3+3]+[255] for i in range(256)}
            # Original 120+40 pixel columns, header, body and front/rear floor.
            front = '_front_' in name
            lower = base+4 if front else base+8
            for i, at in [(base+2,(0,0)),(base+3,(120,0)),(base,(0,16)),(base+1,(120,16)),
                          (lower,(0,112)),(lower+1,(120,112))]:
                source_piece(r, i, at, colours)
            if front and raw['sprite/'+str(lower)].height == 79:
                for i,x in ((lower,0),(lower+1,120)):
                    width=raw['sprite/'+str(i)].width
                    r['rgba_parts'].append({'source':'sprite/'+str(i),'crop':[0,78,width,79],
                                            'at':[x,191],'colours':colours})
        elif name.startswith('other/map_items/'):
            from build_z_scenery_recipes import SCENERY
            if '/hut_' in name:
                biome = Path(name).stem.removeprefix('hut_')
                index = {'arctic':2935,'city':2936,'desert':2937,'jungle':2938,'volcanic':2939}[biome]
            else:
                index = SCENERY[int(Path(name).stem.removeprefix('map_object'))]
            src = raw['sprite/'+str(index)]
            buckets = collections.defaultdict(collections.Counter)
            for a,b in zip(src.getdata(),old.getdata()):
                buckets[a][b] += 1
            colours = {a:list(bucket.most_common(1)[0][0]) for a,bucket in buckets.items()}
            colours[0] = [0,0,0,0]
            r['fill'] = [0,0,0,0]
            source_piece(r,index,(0,0),colours)
        elif name.startswith('planets/'):
            planet = 'ARCTIC' if 'arctic' in name else 'VOLCAN'
            rgb = json.loads((ROOT/'tools/z_terrain_palettes.json').read_text())[planet]
            candidates = []
            for i in range(512):
                n=f'tile/{planet}/{i}';im=raw[n].copy();im.putpalette(rgb)
                candidates.append((n,im.convert('RGB')))
            for y in range(0,h,16):
                for x in range(0,w,16):
                    target=old.crop((x,y,x+16,y+16)).convert('RGB')
                    def loss(item):
                        hist=ImageChops.difference(item[1],target).histogram()
                        return sum((j%256)*v for j,v in enumerate(hist))
                    n,im=min(candidates,key=loss)
                    r['rgba_parts'].append({'source':n,'crop':[0,0,16,16],'at':[x,y],
                                            'colours':{i:rgb[i*3:i*3+3]+[255] for i in set(raw[n].getdata())}})
        elif name in ('splash.bmp','splash.png','summary_won.png','summary_lost.png'):
            member = ('UNPACKED/MAIN/DSTAT1.LBM' if name=='summary_won.png' else
                      'UNPACKED/MAIN/DLOSE1.LBM' if name=='summary_lost.png' else 'INSTALLED/LOADER.LBM')
            manifest['source_files'][member] = hashlib.sha256(originals[member]).hexdigest()
            picture=lbm_image(originals[member]).convert('RGBA')
            source='picture/'+(member[9:] if member.startswith('UNPACKED/') else member)
            counts={i:list(c) for i,c in zip(raw[source].getdata(),picture.getdata())}
            factor=min(w/picture.width,h/picture.height)
            size=[max(1,round(picture.width*factor)),max(1,round(picture.height*factor))]
            r['fill']=[15,17,20,255]
            r['rgba_parts'].append({'source':source,'crop':[0,0,*picture.size],
                                    'resize':size,'at':[(w-size[0])//2,(h-size[1])//2],'colours':counts})
            if name.startswith('summary_'):
                text(r,'MISSION COMPLETE' if 'won' in name else 'MISSION LOST',(20,16,w-40,45),'big_white',2)
        elif name=='fonts/loading_white/char_037.png':
            r['fill']=[0,0,0,0]
            r['rgba_parts'].append(part('fonts/big_white/char_037.png',size=(w,h)))
        elif name=='other/hud/grenade.bmp':
            r['fill']=[0,0,0,0]
            p=part('other/hud/gatling_icon.png',crop=[12,8,76,54],at=(0,2),size=(28,20))
            p['blend']='over'
            r['rgba_parts'].append(p)
        elif name=='other/production_gui/base_image_expanded.png':
            bevel(r)
            rect(r,(172,20,223,91),(47,49,53,255))
            r['rgba_parts'].append(part('other/production_gui/base_image.png',crop=[0,0,108,80]))
            r['rgba_parts'].append(part('other/production_gui/base_image.png',crop=[0,16,64,80],at=(108,16)))
            text(r,'Factory Build Order',(113,4,110,11))
        elif name.startswith('other/menus/'):
            title={'blank_menu_nobuttons.png':'ZOD','login_menu.png':'Login',
                   'create_user_menu.png':'Create User','vote_in_progress.png':'Vote in Progress'}[Path(name).name]
            caption(r,title)
            if title in ('Login','Create User'):
                fields=[('Login Name',35),('Login Password',64)] if title=='Login' else [('User Name',35),('Login Name',64),('Login Password',94),('Email',122)]
                for label,y in fields:
                    bevel(r,(5,y-1,106,y+12),True,(54,56,61,255))
                    text(r,label,(6,y-13,99,10),'yellow_menu')
            elif title=='Vote in Progress':
                text(r,'F1:Yes F2:No F3:Pass',(4,24,w-8,12),'yellow_menu')
        elif name=='other/comp_messages/click_to_resume.png':
            bevel(r,colour=(58,61,80,255))
            text(r,'Click to Start / Resume Game',(3,2,w-6,h-4))
        elif name.startswith('other/factory_gui/'):
            stem=Path(name).stem;pressed='pressed' in stem
            if stem.startswith(('fup','fdown')):
                r['fill']=[0,0,0,0]
                colour=[224,226,231,255] if pressed else [165,171,183,255]
                points=[[1,2],[w-2,2],[w//2,h-2]] if 'down' in stem else [[1,h-3],[w-2,h-3],[w//2,1]]
                r['shapes'].append({'kind':'polygon','points':points,'colour':colour})
            elif stem=='main_entry':
                bevel(r)
                for y in (6,22,38):
                    r['rgba_parts'].append(part('other/factory_gui/entry_bar_red.png',at=(10,y)))
            elif stem=='main_top':
                caption(r,'Factory List')
            elif stem=='entry_bar_grey':
                p=part('other/factory_gui/entry_bar_red.png');im=image('other/factory_gui/entry_bar_red.png')
                p['colour_map']=[[list(c),[round(sum(c[:3])/3)]*3+[c[3]]] for c in set(im.getdata())]
                r['rgba_parts'].append(p)
            else:
                bevel(r,pressed=True)
        elif name.startswith('other/production_gui/'):
            stem=Path(name).stem;pressed='pressed' in stem
            if 'button' in stem:
                bevel(r,pressed=pressed)
                shift=1 if pressed else 0
                if 'queue' in stem:
                    text(r,'Add',(2+shift,1+shift,w-4,h-3),'yellow_menu')
                else:
                    line(r,[(3+shift,h//2+shift),(w-4+shift,h//2+shift)],(232,230,196,255))
                    if stem.startswith('plus'):
                        line(r,[(w//2+shift,3+shift),(w//2+shift,h-4+shift)],(232,230,196,255))
            elif stem=='fus_full':
                caption(r,'Unit Selection');rect(r,(4,20,w-5,h-5),(47,49,53,255))
            elif stem.startswith('fus_top'):
                bevel(r)
                if stem=='fus_top_left':text(r,'Units',(3,3,w-6,h-6))
            else:
                bevel(r,pressed='left' in stem)
        elif name.startswith('other/main_menu_gui/'):
            stem=Path(name).stem;pressed='pressed' in stem
            if stem=='menu_warning':
                caption(r,'Warning!')
                r['rgba_parts']=[]
                for x in range(4,w-5,8):line(r,[(x,3),(min(w-5,x+6),15)],(222,128,26,255),2)
                text(r,'Warning!',(8,4,w-16,10),'yellow_menu')
            elif stem.startswith('close_button'):
                bevel(r,pressed=pressed)
                d=1 if pressed else 0
                line(r,[(3+d,3+d),(8+d,8+d)],(235,229,190,255))
                line(r,[(8+d,3+d),(3+d,8+d)],(235,229,190,255))
            elif stem in ('list_top_right','list_bottom_right'):
                bevel(r,pressed=True)
                arrow='up' if stem=='list_top_right' else 'down'
                r['rgba_parts'].append(part('other/main_menu_gui/list/list_button_'+arrow+'_normal.png',at=(2,4)))
            elif stem=='menu_center' or 'center' in stem:
                r['fill']=[26,103,20,255] if ('green' in stem or pressed) else [96,98,104,255]
            elif stem=='menu_left':r['fill']=[172,180,192,255]
            elif stem=='menu_right':r['fill']=[35,37,42,255]
            else:bevel(r,pressed=pressed)
        else:
            raise ValueError('Unclassified retained asset: '+name)
        # No sparse pixel patches are used for approved replacements.
        if r.get('legacy_equivalence')=='replacement':r['patch_pixels']=0
        manifest['images'][name]=r
        added.append(name)
        print('Prepared',r['legacy_equivalence'],name,flush=True)
    # Validate the complete dependency graph and all new renderer paths before saving.
    rendered=dict(render_recipe_set(manifest,originals))
    for n in added:
        if rendered[n].size!=tuple(manifest['images'][n]['size']):raise ValueError('Wrong dimensions: '+n)
        if manifest['images'][n]['legacy_equivalence']=='exact-config':
            if rendered[n].convert('RGBA').tobytes()!=Image.open(ROOT/'assets'/n).convert('RGBA').tobytes():
                raise ValueError('Team colour mapping changed: '+n)
        manifest['images'][n]['output_rgba_sha256']=hashlib.sha256(rendered[n].convert('RGBA').tobytes()).hexdigest()
    path.write_text(json.dumps(manifest,separators=(',',':'))+'\n')
    (ROOT/'build-asset-cleanup/remaining-replacements.json').write_text(json.dumps(added,indent=2)+'\n')
    print('Validated',len(added),'remaining recipes')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('originals',type=Path)
    build(p.parse_args().originals)
