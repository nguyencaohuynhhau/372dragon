"""Pure text->text transforms for in-game UI errors E7 (QiXi animate) and E8 (XinShou reward flash).
Callers read/write bytes as UTF-8 without newline translation so BOM and CRLF survive."""
import re

def fix_qixi_animate(text):
    # QZ_1..5 only live in imageset QiXi_StoryLetter_3; set _2 does not contain them.
    def block(m):return m.group(0).replace('set:QiXi_StoryLetter_2 ','set:QiXi_StoryLetter_3 ')
    return re.sub(r'<Animate Name="QiXi_StoryLetter_AnimateQZ".*?</Animate>',block,text,flags=re.S)

def fix_xinshou_animate(text):
    return text.replace('XinShouNew_SheJiaoDaRen_RewardFrame_Flash','XinShouYue_SheJiaoDaRen_RewardFrame_Flash')

BODY_FONT='YouYuan11.25'  # D2: 15px body; YouYuan13 only if ui-fit shows overflow
_TOK=re.compile(r'<Window\b(?:[^>"]|"[^"]*")*>|</Window>|<Property\s+Name="(Font|Text)"\s+[Vv]alue="([^"]*)"\s*/>')
_OLD_FONTS=('CysCR_Control','SongTiBmp12')

def login_fonts(layout_text,body_font,title_font='YouYuan18'):
    """E5: a window with Text and no Font, or with CysCR_Control/SongTiBmp12, gets body_font; title_font and other fonts stay.
    Only Font properties change. shortcut: one-line <Property Name= Value= /> tags (the CY layout style), no comments with tags."""
    eol='\r\n' if '\r\n' in layout_text else '\n'
    edits=[];stack=[]  # frame per open Window: [Text match, Font value span]
    for m in _TOK.finditer(layout_text):
        s=m.group(0)
        if s.startswith('</'):
            text,font=stack.pop()
            if font and font[2] in _OLD_FONTS:edits.append((font[0],font[1],body_font))
            elif not font and text:
                ls=layout_text.rfind('\n',0,text.start())+1;ind=layout_text[ls:text.start()]
                edits.append((text.end(),text.end(),f'{eol}{ind if not ind.strip() else " "}<Property Name="Font" Value="{body_font}" />'))
        elif s.startswith('<Window'):
            if not s.endswith('/>'):stack.append([None,None])
        elif m.group(1)=='Text':stack[-1][0]=m
        else:stack[-1][1]=(m.start(2),m.end(2),m.group(2))
    for a,b,r in sorted(edits,reverse=True):layout_text=layout_text[:a]+r+layout_text[b:]
    return layout_text

def fix_login_fonts(text):return login_fonts(text,BODY_FONT)

# path relative to Data root (and to Overlay/Pre|Post) -> transform.
RELS={
 'InterfaceFolder/Schema/wowanimate.xml':fix_qixi_animate,
 'InterfaceFolder/XinShouNew/XinShouNew.layout.xml':fix_xinshou_animate,
 **{f'InterfaceFolder/{d}/{n}.layout.xml':fix_login_fonts for d,n in (('Logon','LogOn'),('SelectServer','SelectServer'),('SelectRole','SelectRole'),('CreateRole','CreateRole'))},
}
