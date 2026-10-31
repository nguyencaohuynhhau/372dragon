"""Tests for the E7/E8 in-game UI error data transforms (Tools/game/ui_error_fixes.py)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tools/game'))
import pytest
import ui_error_fixes as f

DATA = ROOT / 'Assets/CanonicalCY/Data'


def src(rel):
    """Canonical (pristine) data. Overlay Pre holds our own published output after deploy, so it is not a 'before' input."""
    return DATA / rel


def qixi_block(text):
    return re.search(r'<Animate Name="QiXi_StoryLetter_AnimateQZ".*?</Animate>', text, re.S).group(0)


def test_rels_table():
    assert f.RELS['InterfaceFolder/Schema/wowanimate.xml'] is f.fix_qixi_animate
    assert f.RELS['InterfaceFolder/XinShouNew/XinShouNew.layout.xml'] is f.fix_xinshou_animate


def test_qixi_replaces_only_target_animate():
    text = src('InterfaceFolder/Schema/wowanimate.xml').read_bytes().decode('utf-8')
    old = qixi_block(text)
    n_images = old.count('image:QiXi_StoryLetter_QZ_')
    n_set2 = old.count('set:QiXi_StoryLetter_2 ')
    assert n_images > 0 and n_set2 > 0
    out = f.fix_qixi_animate(text)
    new = qixi_block(out)
    assert 'set:QiXi_StoryLetter_2 ' not in new
    assert new.count('set:QiXi_StoryLetter_3 ') == old.count('set:QiXi_StoryLetter_3 ') + n_set2
    assert new.count('image:QiXi_StoryLetter_QZ_') == n_images
    # everything outside the animate is byte-identical
    assert out.replace(new, old) == text


def test_xinshou_replaces_exactly_five():
    text = src('InterfaceFolder/XinShouNew/XinShouNew.layout.xml').read_bytes().decode('utf-8')
    assert text.count('XinShouNew_SheJiaoDaRen_RewardFrame_Flash') == 5
    out = f.fix_xinshou_animate(text)
    assert 'XinShouNew_SheJiaoDaRen_RewardFrame_Flash' not in out
    assert out.count('XinShouYue_SheJiaoDaRen_RewardFrame_Flash') == 5
    assert out.replace('XinShouYue_', 'XinShouNew_') == text


@pytest.mark.parametrize('fix,rel', [(v, k) for k, v in f.RELS.items()])
def test_idempotent_on_real_files(fix, rel):
    once = fix(src(rel).read_bytes().decode('utf-8'))
    assert fix(once) == once


def test_crlf_and_bom_preserved():
    bom = '﻿<?xml version="1.0"?>\r\n'
    a = bom + '<Animate Name="QiXi_StoryLetter_AnimateQZ" Loop="false">\r\n<Image ImageName="set:QiXi_StoryLetter_2 image:QiXi_StoryLetter_QZ_2"/>\r\n</Animate>\r\n'
    out = f.fix_qixi_animate(a)
    assert out.startswith(bom) and out.count('\r\n') == a.count('\r\n') == out.count('\n') and 'set:QiXi_StoryLetter_3 ' in out
    b = bom + '<Property Name="Animate" Value="XinShouNew_SheJiaoDaRen_RewardFrame_Flash" />\r\n'
    out = f.fix_xinshou_animate(b)
    assert out.startswith(bom) and out.endswith('\r\n') and 'XinShouYue_' in out


def loaded_images():
    """image names per loaded imageset: scheme (Pre else Canonical) -> imageset files (Pre else Canonical)."""
    scheme = src('InterfaceFolder/Schema/WoWLookSkin.scheme.xml').read_text(encoding='utf-8')
    sets = {}
    for name, fn in re.findall(r'<Imageset Name="([^"]+)" Filename="([^"]+)"', scheme):
        p = src('InterfaceFolder/' + fn)
        if p.exists():
            sets.setdefault(name, set()).update(re.findall(r'<Image Name="([^"]+)"', p.read_text(encoding='utf-8', errors='replace')))
    return sets


def test_xinshou_target_animate_is_valid():
    """E8 pre-check: the rename target exists and every image it uses is in a loaded imageset."""
    text = src('InterfaceFolder/Schema/wowanimate.xml').read_bytes().decode('utf-8')
    m = re.search(r'<Animate Name="XinShouYue_SheJiaoDaRen_RewardFrame_Flash".*?</Animate>', text, re.S)
    assert m, 'target animate XinShouYue_SheJiaoDaRen_RewardFrame_Flash missing: E8 must switch to deleting the property'
    refs = set(re.findall(r'set:(\S+) image:([^"]+)"', m.group(0)))
    assert refs
    sets = loaded_images()
    missing = [r for r in sorted(refs) if r[1] not in sets.get(r[0], ())]
    assert not missing, f'images not in a loaded imageset: {missing}'
    print('XinShou animate images OK:', sorted(refs))


def test_qixi_target_images_exist_after_fix():
    text = f.fix_qixi_animate(src('InterfaceFolder/Schema/wowanimate.xml').read_bytes().decode('utf-8'))
    sets = loaded_images()
    for s, i in set(re.findall(r'set:(\S+) image:([^"]+)"', qixi_block(text))):
        assert i in sets.get(s, ()), (s, i)


# ---- Task 6 / E5: one body font across the login flow -------------------------------------------------------------
LOGIN_RELS = ['InterfaceFolder/Logon/LogOn.layout.xml', 'InterfaceFolder/SelectServer/SelectServer.layout.xml',
              'InterfaceFolder/SelectRole/SelectRole.layout.xml', 'InterfaceFolder/CreateRole/CreateRole.layout.xml']
BODY = 'YouYuan11.25'
FONT_LINE = re.compile(r'[ \t]*<Property Name="Font" Value="[^"]*" />\r?\n')


def font_of_text_windows(text):
    import xml.etree.ElementTree as ET
    root = ET.fromstring(text.lstrip('﻿').encode('utf-8'))
    out = []
    for w in root.iter('Window'):
        props = {p.get('Name'): p.get('Value') for p in w.findall('Property')}
        if 'Text' in props:
            out.append((w.get('Name'), props.get('Font')))
    return out


def test_login_rels_registered():
    for rel in LOGIN_RELS:
        assert f.RELS[rel] is f.fix_login_fonts


def test_login_fonts_rules():
    a = ('<GUILayout>\r\n'
         '  <Window Type="X" Name="NoFont">\r\n    <Property Name="Text" Value="a&gt;b" />\r\n'
         '    <Window Type="X" Name="Cy">\r\n      <Property Name="Font" Value="CysCR_Control" />\r\n      <Property Name="Text" Value="c" />\r\n    </Window>\r\n'
         '    <Window Type="X" Name="NoTextCy">\r\n      <Property Name="Font" Value="CysCR_Control" />\r\n    </Window>\r\n'
         '    <Window Type="X" Name="Song"><Property Name="Font" Value="SongTiBmp12" /><Property Name="Text" Value="s" /></Window>\r\n'
         '    <Window Type="X" Name="Title">\r\n      <Property Name="Text" Value="t" />\r\n      <Property Name="Font" Value="YouYuan18" />\r\n    </Window>\r\n'
         '    <Window Type="X" Name="Plain">\r\n      <Property Name="Size" Value="w:1 h:1" />\r\n    </Window>\r\n'
         '  </Window>\r\n</GUILayout>\r\n')
    out = f.login_fonts(a, 'YouYuan13')
    assert font_of_text_windows(out) == [('NoFont', 'YouYuan13'), ('Cy', 'YouYuan13'), ('Song', 'YouYuan13'), ('Title', 'YouYuan18')]
    assert 'CysCR_Control' not in out and 'SongTiBmp12' not in out
    assert out.count('\r\n') == out.count('\n')                        # CRLF kept, inserted line too
    assert '    <Property Name="Font" Value="YouYuan13" />\r\n' in out  # inserted with the Text line's indent
    assert 'Name="Plain">\r\n      <Property Name="Size"' in out and out.count('Name="Font"') == 5  # no Font added to text-less windows
    assert f.login_fonts(out, 'YouYuan13') == out                       # idempotent
    assert f.login_fonts(a, 'YouYuan13') == f.login_fonts(a, body_font='YouYuan13')


@pytest.mark.parametrize('rel', LOGIN_RELS)
def test_login_fonts_real_layouts(rel):
    text = src(rel).read_bytes().decode('utf-8')
    out = f.fix_login_fonts(text)
    assert all(font in (BODY, 'YouYuan18') for _, font in font_of_text_windows(out))
    assert 'CysCR_Control' not in out and 'SongTiBmp12' not in out
    assert FONT_LINE.sub('', out) == FONT_LINE.sub('', text), 'only Font properties may differ'
