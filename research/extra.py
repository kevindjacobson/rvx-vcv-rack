exec(open('research/inventory.py').read().split("p=get('https://lzxindustries.net/modules')")[0])
urls=[
'https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352',
'https://lzxindustries.net/instruments/chromagnon',
'https://lzxindustries.net/instruments/videomancer',
'https://lzxindustries.net/instruments/vidiot',
'https://community.lzxindustries.net/t/all-about-bitvision-legacy/1353',
'https://lzxindustries.net/modules/sumdist/manual',
'https://api.github.com/repos/lzxindustries/lzxdocs/git/trees/master?recursive=1']
for u in urls:
 try:
  if 'api.github' in u:
   j=json.load(urllib.request.urlopen(u)); paths=[x['path'] for x in j.get('tree',[]) if any(z in x['path'].lower() for z in ['cadet','visionary','mapper','differentiator','function-generator','voltage-interface'])]; Path('research/repo-paths.json').write_text(json.dumps(paths)); print('\nREPO PATHS',json.dumps(paths)[:16000]);continue
  p=get(u);s=re.sub(r'\n\s*\n','\n',''.join(p.parts));Path('research/extra-'+u.rstrip('/').split('/')[-1]+'.txt').write_text(s)
  print('\nURL',u)
  if '1352' in u:
   print('SECTIONS',re.findall(r'\n([^\n]{3,65})\n',s)[:150])
  else: print(s.split('You May Also Like')[0][:10500])
 except Exception as e:print(u,e)
