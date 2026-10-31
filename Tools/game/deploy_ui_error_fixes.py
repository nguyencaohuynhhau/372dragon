"""Stage and publish the E7/E8 UI data fixes (ui_error_fixes.RELS) over the repo-root Play/.
stage   -> copy Play/Overlay + Play/Bin64 to Build/world-gui/stage-ui-error-fixes-01, apply transforms, write sidecar manifest.
check   -> verify the stage against Play and the manifest.
publish -> back up and write the staged files to Play/Overlay/Pre and Post (Game must not be running)."""
from pathlib import Path
import argparse,datetime,json,shutil
from deploy_mount_flight import idle
from publish_gameplay_fixes import ROOT,sha
from ui_error_fixes import RELS
PLAY=ROOT/'Play';DATA=ROOT/'Assets/CanonicalCY/Data';STAGE=ROOT/'Build/world-gui/stage-ui-error-fixes-01'
LOG=ROOT/'Build/Logs/2026-10-19-login-ui';MANIFEST=LOG/'ui-error-fixes-manifest.json'
SLOTS=('Pre','Post')
def source(root,rel):
 p=root/'Overlay/Pre'/rel;return p if p.exists() else DATA/rel  # Pre wins, else Canonical
def transformed(root,rel):
 return RELS[rel](source(root,rel).read_bytes().decode('utf-8')).encode('utf-8')
def targets():return [f'Overlay/{s}/{rel}' for rel in RELS for s in SLOTS]
def stage():
 assert not STAGE.exists(),f'{STAGE} exists';STAGE.mkdir(parents=True)
 shutil.copytree(PLAY/'Overlay',STAGE/'Overlay')  # Data is a symlink in Play; never copied
 shutil.copytree(PLAY/'Bin64',STAGE/'Bin64',ignore=shutil.ignore_patterns('*.log','*.dmp'))
 rows=[]
 for rel in RELS:
  data=transformed(PLAY,rel);src=source(PLAY,rel)
  for s in SLOTS:
   t=f'Overlay/{s}/{rel}';d=STAGE/t;d.parent.mkdir(parents=True,exist_ok=True);d.write_bytes(data)
   rows.append(dict(path=t,sha256=sha(d),bytes=len(data),origin='ui error fixes',source=src.relative_to(ROOT).as_posix(),
    play_before=sha(PLAY/t) if (PLAY/t).exists() else None))
 LOG.mkdir(parents=True,exist_ok=True);MANIFEST.write_text(json.dumps(rows,indent=1),encoding='utf8');print('Candidate ready',STAGE)
def check():
 rows={r['path']:r for r in json.loads(MANIFEST.read_text(encoding='utf8'))};assert set(rows)==set(targets())
 for t,r in rows.items():assert sha(STAGE/t)==r['sha256'],t
 diff=[f.relative_to(STAGE).as_posix() for f in (STAGE/'Overlay').rglob('*') if f.is_file() and (not (PLAY/f.relative_to(STAGE)).exists() or sha(f)!=sha(PLAY/f.relative_to(STAGE)))]
 assert set(diff)<=set(targets()),sorted(set(diff)-set(targets()));print('PASS stage differs from Play only in',sorted(diff))
def publish():
 idle();check();rows=json.loads(MANIFEST.read_text(encoding='utf8'))
 for r in rows:  # refuse if Play moved since staging
  p=PLAY/r['path'];assert (sha(p) if p.exists() else None)==r['play_before'],f'Play changed: {r["path"]}'
 stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S');backup=ROOT/'Build/backups/ui-error-fixes-play'/stamp
 for r in rows:
  p=PLAY/r['path']
  if p.exists():b=backup/r['path'];b.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,b)
 for r in rows:
  p=PLAY/r['path'];p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(STAGE/r['path'],p);assert sha(p)==r['sha256']
 print('Published ui error fixes, backup',backup)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('mode',choices=['stage','check','publish']);a=p.parse_args()
 {'stage':stage,'check':check,'publish':publish}[a.mode]()
