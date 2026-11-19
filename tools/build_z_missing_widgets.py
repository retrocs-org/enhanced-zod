#!/usr/bin/env python3
"""Provide procedural artwork for widget paths that were absent from the old tree."""
import hashlib,json
from pathlib import Path
from z_media import render_extended_recipe

root=Path(__file__).resolve().parent.parent
path=root/'tools/z_graphics_recipes.json';manifest=json.loads(path.read_text())
recipes={}
for kind,width in [('left',16),('center',13),('right',15)]:
 start=7 if kind=='left' else 0;end=7 if kind=='right' else width-1
 recipes[f'other/main_menu_gui/radio/radio_{kind}.png']={
  'size':[width,9],'fill':[0,0,0,0],'rgba_parts':[], 'shapes':[
   {'kind':'rectangle','box':[start,3,end,5],'colour':[32,35,40,255]},
   {'kind':'line','points':[[start,3],[end,3]],'colour':[180,188,199,255]}]}
recipes['other/main_menu_gui/radio/radio_selector.png']={'size':[5,7],'fill':[235,153,33,255],
 'rgba_parts':[],'shapes':[{'kind':'line','points':[[0,6],[0,0],[4,0]],'colour':[255,225,150,255]},
                         {'kind':'line','points':[[4,0],[4,6],[0,6]],'colour':[53,34,12,255]}]}
for team,colour in [('null',[76,79,84,255]),('red',[223,0,0,255])]:
 recipes[f'other/main_menu_gui/team_color_{team}.png']={'size':[19,12],'fill':colour,'rgba_parts':[],
  'shapes':[{'kind':'line','points':[[0,11],[0,0],[18,0]],'colour':[189,195,205,255]},
            {'kind':'line','points':[[18,0],[18,11],[0,11]],'colour':[25,27,31,255]}]}
output=root/'build-asset-cleanup/replacement-reference/assets'
for name,recipe in recipes.items():
 recipe.update(mode='RGBA',legacy_equivalence='new-control',replacement_reason='Previously missing runtime widget artwork')
 im=render_extended_recipe(recipe,{},{});recipe['output_rgba_sha256']=hashlib.sha256(im.tobytes()).hexdigest()
 dest=output/name;dest.parent.mkdir(parents=True,exist_ok=True);im.save(dest)
 manifest['images'][name]=recipe
path.write_text(json.dumps(manifest,separators=(',',':'))+'\n')
prune=root/'packaging/imported-assets.txt';lines=prune.read_text().splitlines()
for name in recipes:
 if name not in lines:lines.append(name)
prune.write_text('\n'.join(lines)+'\n')
print('Added',len(recipes),'previously missing control recipes')
