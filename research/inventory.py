import urllib.request, re, json, concurrent.futures
from html.parser import HTMLParser
from pathlib import Path
class Parser(HTMLParser):
 def __init__(self):
  super().__init__(); self.parts=[]; self.links=[]; self.href=None; self.label=[]; self.skip=0
 def handle_starttag(self,t,a):
  a=dict(a)
  if t in ('script','style'): self.skip+=1
  if t=='a': self.href=a.get('href'); self.label=[]
  if t in ('p','h1','h2','h3','li','tr','br'): self.parts.append('\n')
 def handle_endtag(self,t):
  if t in ('script','style'): self.skip=max(0,self.skip-1)
  if t=='a' and self.href: self.links.append((self.href,''.join(self.label).strip())); self.href=None
 def handle_data(self,d):
  if not self.skip:
   self.parts.append(d)
   if self.href:self.label.append(d)
def get(url):
 p=Parser(); p.feed(urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0'}),timeout=35).read().decode()); return p
p=get('https://lzxindustries.net/modules')
links=[]
for u,n in p.links:
 if (u.startswith('/modules/') and u!='/modules/specs') or ('community.lzxindustries.net/t/' in u and 'External' in n) or 'github.com/lzxindustries' in u:
  u='https://lzxindustries.net'+u if u.startswith('/') else u
  if u not in [x[0] for x in links]:links.append((u,n))
def fetch(item):
 u,n=item
 try:
  p=get(u); s=re.sub(r'\n\s*\n','\n',''.join(p.parts)); slug=u.rstrip('/').split('/')[-1]; Path('research/'+slug+'.txt').write_text(s)
  # product main content lies before footer
  start=s.find('\n'+n.split(' Legacy')[0])
  s=s[:s.find('ResourcesVideomancer')] if 'ResourcesVideomancer' in s else s
  return {'name':n,'url':u,'text':s}
 except Exception as e:return {'name':n,'url':u,'error':str(e)}
with concurrent.futures.ThreadPoolExecutor(max_workers=10) as pool: rows=list(pool.map(fetch,links))
Path('research/inventory.json').write_text(json.dumps(rows,indent=2))
for r in rows:
 print('\nURL',r['url'],'LABEL',r['name']); print(r.get('text',r.get('error',''))[:6500])
print('COUNT',len(rows))
