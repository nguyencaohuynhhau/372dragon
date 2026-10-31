"""Tests for Tools/ui/ui_error_check.py using real log lines from 19/10/2026."""
import subprocess
import sys
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tools/ui'))
import ui_error_check as c

EXC = "19/10/2026 20:13:27 (Error)\tException: Imageset::getImage - The Image named 'QiXi_StoryLetter_QZ_2' could not be found in Imageset 'QiXi_StoryLetter_2'."
MAPIMG = "19/10/2026 20:14:25 (Error)\tMapImage: ChildImage 'name:JunXian1_1 set:JunXian01 image:JunXian01_1_1' ignored (image not found in the imageset)."
DIAG = [
    "19/10/2026 20:14:25 (Error)\tCYS-SCENEMAP: win='MiniMap_MapArea' base='jpg_dali_new_320' grid=5x5",
    "19/10/2026 20:14:25 (Error)\tCYS-MAPDOTS: dots=3",
    "19/10/2026 20:14:25 (Error)\tCYS-CRASHCEGUI: stage=draw",
]
LUA = "[19_17:44:32]: CYS Lua failure env=LoginSelectRole_Env phase=expression-call code=1 err=[string \"?...\"]:901: host function failed: GetRoleInfo: invalid character slot."
TD = "[19_17:44:32]: TDThrow: GetRoleInfo: invalid character slot."
OLD_TD = "[19_16:09:31]: TDThrow: old one."


def write(tmp_path, ui, client):
    u, w = tmp_path / 'UISystem.log', tmp_path / 'WXClient.log'
    u.write_text('\n'.join(ui) + '\n', encoding='utf-8')
    w.write_bytes(('\n'.join(client) + '\n').encode('utf-8') + b'\xff\xfe bad bytes\n')  # not valid UTF-8
    return u, w


def test_counts_each_category_and_ignores_diagnostics(tmp_path):
    u, w = write(tmp_path, [EXC, MAPIMG, *DIAG], [LUA, TD, 'unrelated line'])
    r = c.check(u, w, None)
    assert len(r['exceptions']) == 2  # Exception + MapImage ChildImage ignored
    assert len(r['lua_failures']) == 1 and 'LoginSelectRole_Env' in r['lua_failures'][0]
    assert len(r['tdthrow']) == 1 and 'invalid character slot' in r['tdthrow'][0]
    assert r['ok'] is False


def test_diagnostics_only_is_ok(tmp_path):
    u, w = write(tmp_path, DIAG, ['nothing here'])
    r = c.check(u, w, None)
    assert r == {'exceptions': [], 'lua_failures': [], 'tdthrow': [], 'ok': True}


def test_since_skips_earlier_lines(tmp_path):
    u, w = write(tmp_path, [EXC], [OLD_TD, TD])
    r = c.check(u, w, '19_17:44:00')
    assert len(r['tdthrow']) == 1 and 'invalid character slot' in r['tdthrow'][0]
    assert c.check(u, w, '19_20:13:28')['exceptions'] == []  # UISystem date format is converted
    assert len(c.check(u, w, '19_20:13:27')['exceptions']) == 1


def test_cli_exit_code_and_json(tmp_path):
    u, w = write(tmp_path, [EXC], [])
    out = tmp_path / 'o.json'
    cmd = [sys.executable, str(ROOT / 'Tools/ui/ui_error_check.py'), '--ui', str(u), '--client', str(w), '--out', str(out)]
    assert subprocess.run(cmd).returncode == 1
    assert json.loads(out.read_text(encoding='utf-8'))['ok'] is False
    u.write_text('', encoding='utf-8')
    assert subprocess.run(cmd).returncode == 0
