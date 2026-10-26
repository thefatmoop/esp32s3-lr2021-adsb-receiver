"""Build a compact USA map using only Python's standard library.
Pinned input URLs: map-sources/sources.json. No runtime map dependency.
"""
import json,math,gzip,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
# Coverage includes all 50 states on both sides of the dateline, plus the
# inhabited US territories. Neighboring features inside each box aid orientation.
AREAS=[[-125,24,-66,50],[-180,50,-129,72],[172,50,180,55],[-161,18,-154,23],
       [-68,17,-64,19],[144,13,147,21],[-172,-15,-168,-10]]
BOUNDS=AREAS[0]
source=ROOT/'map-sources'
def rounded(c):
 if isinstance(c,(int,float)):return round(c,4)
 return [rounded(p) for p in c]
def simplify(points,tolerance=.0006):
 if len(points)<3:return points
 a,b=points[0],points[-1];dx,dy=b[0]-a[0],b[1]-a[1];length=dx*dx+dy*dy
 best,index=0,0
 for i,p in enumerate(points[1:-1],1):
  t=max(0,min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/length)) if length else 0
  d=(p[0]-a[0]-t*dx)**2+(p[1]-a[1]-t*dy)**2
  if d>best:best,index=d,i
 if best>tolerance*tolerance:return simplify(points[:index+1],tolerance)[:-1]+simplify(points[index:],tolerance)
 return [a,b]
def segment(a,b):
 dx,dy=b[0]-a[0],b[1]-a[1];lo,hi=0,1
 for p,q in [(-dx,a[0]-BOUNDS[0]),(dx,BOUNDS[2]-a[0]),(-dy,a[1]-BOUNDS[1]),(dy,BOUNDS[3]-a[1])]:
  if p==0:
   if q<0:return None
  elif p<0:lo=max(lo,q/p)
  else:hi=min(hi,q/p)
  if lo>hi:return None
 return [[a[0]+lo*dx,a[1]+lo*dy],[a[0]+hi*dx,a[1]+hi*dy]]
def clip_line(line):
 result=[];current=[]
 for a,b in zip(line,line[1:]):
  pair=segment(a,b)
  if not pair:
   if len(current)>1:result.append(simplify(current))
   current=[];continue
  a,b=pair
  if current and math.dist(current[-1],a)>.000001:
   if len(current)>1:result.append(simplify(current))
   current=[]
  if not current:current=[a]
  current.append(b)
 if len(current)>1:result.append(simplify(current))
 return result
def clip_ring(ring):
 poly=ring[:-1]
 for axis,value,sign in [(0,BOUNDS[0],1),(0,BOUNDS[2],-1),(1,BOUNDS[1],1),(1,BOUNDS[3],-1)]:
  result=[]
  if not poly:return []
  for a,b in zip(poly[-1:]+poly[:-1],poly):
   ai=(a[axis]-value)*sign>=0;bi=(b[axis]-value)*sign>=0
   if ai!=bi:
    t=(value-a[axis])/(b[axis]-a[axis]);result.append([a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])])
   if bi:result.append(b)
  poly=result
 if len(poly)<3:return []
 poly=simplify(poly+[poly[0]])
 return poly if len(poly)>=4 else []
def points(c):
 if not c:return
 if isinstance(c[0],(int,float)):yield c
 else:
  for child in c:yield from points(child)
def bbox(c):
 p=list(points(c));return [min(v[0] for v in p),min(v[1] for v in p),max(v[0] for v in p),max(v[1] for v in p)]
def layer(name,kind):
 global BOUNDS
 features=json.loads((source/('ne_10m_'+name+'.geojson')).read_text(encoding='utf-8'))['features']
 for f in features:
  g=f['geometry']
  if not g or not g['coordinates']:continue
  bounds=bbox(g['coordinates'])
  for BOUNDS in AREAS:
   if bounds[2]<BOUNDS[0] or bounds[0]>BOUNDS[2] or bounds[3]<BOUNDS[1] or bounds[1]>BOUNDS[3]:continue
   groups=g['coordinates'] if g['type']=='Multi'+kind else [g['coordinates']]
   for c in groups:
    if kind=='LineString':clipped=clip_line(c)
    elif kind=='Polygon':
     outer=clip_ring(c[0]);clipped=[[outer]+[r for r in (clip_ring(r) for r in c[1:]) if r]] if outer else []
    else:clipped=[c]
    for part in clipped:yield f['properties'],{'g':rounded(part),'b':rounded(bbox(part))}
out={'bounds':[-180,-15,180,72],'coverage':AREAS,'source':'Natural Earth 1:10m','roads':[],'rivers':[],'lakes':[],'urban':[],'places':[]}
for p,e in layer('roads','LineString'):
 e['n']=p.get('name') or '';e['k']=2 if p.get('level')=='Interstate' else 1 if p.get('type')=='Major Highway' else 0
 out['roads'].append(e)
for p,e in layer('rivers_lake_centerlines','LineString'):
 e['n']=p.get('name_en') or p.get('name') or '';out['rivers'].append(e)
for name,key in [('lakes','lakes'),('urban_areas','urban')]:
 for p,e in layer(name,'Polygon'):
  e['n']=(p.get('name_en') or p.get('name') or '') if key=='lakes' else '';out[key].append(e)
for p,e in layer('populated_places','Point'):
 out['places'].append([p['NAME'],*e['g'],p.get('POP_MAX',0)])
out['places'].sort(key=lambda p:-p[3])
data={'land':[],'regions':[],'detail':out}
countries=json.loads((source/'ne_10m_admin_0_countries.geojson').read_text(encoding='utf-8'))['features']
for f in countries:
 g=f['geometry'];polys=g['coordinates'] if g['type']=='MultiPolygon' else [g['coordinates']]
 # Retain fine US coastlines (especially islands); keep a coarse world backdrop.
 tolerance=.0006 if f['properties'].get('SOV_A3') in ('US1','USA') else .035
 for polygon in polys:
  rings=[rounded(simplify(r,tolerance)) for r in polygon]
  rings=[r for r in rings if len(r)>=4]
  if rings:data['land'].append(rings)
states=json.loads((source/'ne_10m_admin_1_states_provinces_lines.geojson').read_text(encoding='utf-8'))['features']
for f in states:
 if f['properties'].get('ADM0_A3')!='USA':continue
 g=f['geometry'];lines=g['coordinates'] if g['type']=='MultiLineString' else [g['coordinates']]
 data['regions'].extend(rounded(simplify(line)) for line in lines)
data['landBounds']=[rounded(bbox(p)) for p in data['land']]
data['regionBounds']=[rounded(bbox(p)) for p in data['regions']]
world=ROOT/'main/world.json'
world.write_text(json.dumps(data,separators=(',',':'),ensure_ascii=True),encoding='utf-8')
packed=gzip.compress(world.read_bytes(),compresslevel=9,mtime=0)
assert len(packed)<2500000,'Compressed map exceeds flash budget'
world.with_suffix('.json.gz').write_bytes(packed)
(ROOT/'main/map_version.h').write_text('#define MAP_ETAG "\\"'+hashlib.sha256(packed).hexdigest()[:16]+'\\""\n',encoding='utf-8')
print({k:len(out[k]) for k in ['roads','rivers','lakes','urban','places']},'JSON bytes',world.stat().st_size,'flash bytes',len(packed),'land polygons',len(data['land']),'state boundaries',len(data['regions']))
