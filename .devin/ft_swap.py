import re, pathlib
root = pathlib.Path('src')
MAP = {
 'desk':'Desk','office_chair':'OfficeChair','visitor_chair':'VisitorChair',
 'monitor':'Monitor','keyboard':'Keyboard','mouse':'Mouse','pc_tower':'PcTower',
 'outlet':'Outlet','paper_stack':'PaperStack','rug':'Rug','mug':'Mug',
 'desk_lamp':'DeskLamp','bookshelf':'Bookshelf','filing_cabinet':'FilingCabinet',
 'printer':'Printer','plant':'Plant','vase':'Vase','poster':'Poster',
 'phone':'Phone','wall_clock':'WallClock','couch':'Couch','bench':'Bench',
 'water_cooler':'WaterCooler','blinds':'Blinds','door':'Door','bin':'Bin',
 'switch':'Switch','ceiling_light':'CeilingLight','note':'Note'}
names = '|'.join(sorted(MAP, key=len, reverse=True))
pat = re.compile(r'\b(\w+)\.type ([=!]=) "(' + names + r')"')
for f in list(root.glob('*.cpp')) + list(root.glob('*.h')):
    t = f.read_text(encoding='utf-8')
    n = pat.sub(lambda m: f"{m.group(1)}.ft {m.group(2)} FT::{MAP[m.group(3)]}", t)
    if n != t:
        f.write_text(n, encoding='utf-8')
        print(f, len(pat.findall(t)), 'swapped')
