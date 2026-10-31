"""Count UI errors in client logs (acceptance gate: ok == no UI errors)."""
import argparse
import json
import re
import sys
from pathlib import Path

# UISystem.log: "19/10/2026 20:13:26 (Error)\t<msg>"; CYS-* diagnostics also use (Error) and are NOT counted.
UI_LINE = re.compile(r'^(\d\d)/\d\d/\d{4} (\d\d:\d\d:\d\d) \(Error\)\t(Exception: |MapImage: ChildImage .* ignored)')
# WXClient.log: "[19_17:44:32]: <msg>"
CLIENT_LINE = re.compile(r'^\[(\d\d_\d\d:\d\d:\d\d)\]: (CYS Lua failure env=|TDThrow: )')


def _lines(path):
    # WXClient.log is not valid UTF-8
    return Path(path).read_text(encoding='utf-8', errors='replace').splitlines()


def check(ui_log, client_log, since=None):
    # shortcut: since compared as "DD_HH:MM:SS" strings, so it cannot span a month boundary.
    after = lambda ts: since is None or ts >= since
    r = {'exceptions': [], 'lua_failures': [], 'tdthrow': []}
    for ln in _lines(ui_log):
        m = UI_LINE.match(ln)
        if m and after(f'{m[1]}_{m[2]}'):
            r['exceptions'].append(ln)
    for ln in _lines(client_log):
        m = CLIENT_LINE.match(ln)
        if m and after(m[1]):
            r['lua_failures' if m[2].startswith('CYS') else 'tdthrow'].append(ln)
    r['ok'] = not (r['exceptions'] or r['lua_failures'] or r['tdthrow'])
    return r


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--ui', required=True)
    p.add_argument('--client', required=True)
    p.add_argument('--since')
    p.add_argument('--out', required=True)
    a = p.parse_args()
    r = check(a.ui, a.client, a.since)
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(json.dumps(r, indent=2, ensure_ascii=False), encoding='utf-8')
    print({k: len(v) for k, v in r.items() if k != 'ok'}, 'ok' if r['ok'] else 'FAIL')
    return 0 if r['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
