"""Run the selection controller against reordered native slot responses.

Uses the production Lua DLL and original CY control names, without Game/network.
The independent C++ role-list suite checks how native derives these slots by GUID.
"""
import argparse
import json
import sys

from create_role_profile_tests import Lua, MOCK, ROOT, profile

sys.path.insert(0, str(ROOT / 'Tools/ui'))
import create_role_layout


def empty_slot_hover_check(dll):
    """CY SelectRole.lua must survive GetRoleInfo for a slot with no character.

    The mock pins the contract native GetRoleInfo returns for empty slots: ("", 0, 0, 0, "").
    """
    vm = Lua(dll)
    try:
        vm.run(MOCK)
        vm.run((ROOT / 'Assets/CanonicalCY/Data/InterfaceFolder/SelectRole/SelectRole.lua').read_bytes())
        vm.run('''
SelectRole_Info=window()
SelectRole_Info.SetText=function(self,value) self.text=value end
local created=0
SelectRole_CreateRole=function() created=created+1 end
roleCount=2
function GameProduceLogin:GetRoleInfo(index)
    if index>=roleCount then return '',0,0,0,'' end
    return 'Role'..index,1,10,0,'img'
end
local ok,err=pcall(SelectRole_MouseEnterCharArea,2)
assert(ok,err)
assert(SelectRole_Info.text=='#{DLJM_XML_5}')
ok,err=pcall(SelectRole_DoubleClicked,2)
assert(ok,err)
assert(created==1)
''')
    finally:
        vm.close()
    return 'empty slot hover and double click use the empty-slot tuple'


def test_empty_slot_hover():
    dlls = [ROOT / 'Build/cmake' / d / 'bin/LuaPlus.dll' for d in ('ci-x64', 'public-x64')]
    dll = next((d for d in dlls if d.exists()), None)
    if dll is None:
        import pytest
        pytest.skip('LuaPlus.dll not built')
    empty_slot_hover_check(dll)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True)
    parser.add_argument('--lua-dll', default='Build/cmake/ci-x64/bin/LuaPlus.dll')
    args = parser.parse_args()
    out = profile.checked(args.out)
    out.mkdir(parents=True, exist_ok=False)
    dll = profile.checked(args.lua_dll)
    source = ROOT / 'Tools/ui/templates/CysSelectRole.lua'
    checks = []
    vm = Lua(dll)
    try:
        vm.run(MOCK)
        vm.run('\n'.join(w.attrib['Name'] + '=window()' for w in
                         create_role_layout.source_layout('SelectRole').iter('Window')))
        vm.run(source.read_bytes())
        vm.run('''
records={}
function GameProduceLogin:GetRoleInfo(index)
    local role=records[index+1]
    assert(role)
    return role, -1, 1, 0, 'set:CysCR_Portrait image:'..role
end
function GameProduceLogin:SelectRoleSlot(index)
    assert(index>=0 and index<roleCount)
    calls.role=index; nativeRole=index; calls.selected=records[index+1]
    return 1
end
LoginSelectRole_OnLoad()
''')

        def check(name, script):
            vm.run(script)
            checks.append(name)

        check('initial empty list does not select a fabricated record', '''
roleCount=0;nativeRole=-1;LoginSelectRole_OnEvent('GAMELOGIN_OPEN_SELECT_CHARACTOR')
assert(calls.selected==nil and SelectRole_TargetInfo_Create.visible)
''')
        check('create reply B C A selects native new GUID C in middle', '''
records={'B','C','A'};roleCount=3;nativeRole=1
LoginSelectRole_OnEvent('GAMELOGIN_CREATE_ROLE_OK')
assert(calls.selected=='C' and calls.role==1 and SelectRole_TargetInfo_Gaoliang2.visible)
assert(SelectRole_TargetInfo_Name_Text2.text=='C' and not SelectRole_Create.enabled)
''')
        check('create reply C B A selects native new GUID C first', '''
records={'C','B','A'};nativeRole=0
LoginSelectRole_OnEvent('GAMELOGIN_CREATE_ROLE_OK')
assert(calls.selected=='C' and calls.role==0 and SelectRole_TargetInfo_Gaoliang1.visible)
assert(not SelectRole_TargetInfo_Gaoliang2.visible)
''')
        check('refresh after create does not overwrite native selected slot', '''
LoginSelectRole_OnEvent('GAMELOGIN_REFRESH_ROLE_SELECT_CHARACTOR')
assert(calls.selected=='C' and calls.role==0)
''')
        check('normal refresh retains selected GUID after index changes', '''
SelectRole_Clicked(2);assert(calls.selected=='A')
records={'A','C','B'};nativeRole=0
LoginSelectRole_OnEvent('GAMELOGIN_REFRESH_ROLE_SELECT_CHARACTOR')
assert(calls.selected=='A' and calls.role==0 and SelectRole_TargetInfo_Name_Text.text=='A')
''')
        check('native absent-GUID fallback selects first remaining role', '''
records={'B'};roleCount=1;nativeRole=0
LoginSelectRole_OnEvent('GAMELOGIN_REFRESH_ROLE_SELECT_CHARACTOR')
assert(calls.selected=='B' and calls.role==0 and SelectRole_TargetInfo_Create2.visible)
''')
        check('click updates native state used when reopening selection', '''
records={'A','B','C'};roleCount=3;nativeRole=0
LoginSelectRole_OnEvent('GAMELOGIN_REFRESH_ROLE_SELECT_CHARACTOR')
SelectRole_Clicked(2);LoginSelectRole_OnEvent('GAMELOGIN_CLOSE_SELECT_CHARACTOR')
LoginSelectRole_OnEvent('GAMELOGIN_OPEN_SELECT_CHARACTOR')
assert(calls.selected=='C' and nativeRole==2 and SelectRole_TargetInfo_Gaoliang3.visible)
''')
        check('enter and double click send the native slot of the selected GUID', '''
records={'A','B','C'};roleCount=3;nativeRole=2
LoginSelectRole_OnEvent('GAMELOGIN_REFRESH_ROLE_SELECT_CHARACTOR')
local before=calls.enter or 0
SelectRole_EnterGame();assert(calls.enter==before+1 and calls.enterIndex==2 and calls.selected=='C')
records={'C','A','B'};nativeRole=0
LoginSelectRole_OnEvent('GAMELOGIN_REFRESH_ROLE_SELECT_CHARACTOR')
SelectRole_EnterGame();assert(calls.enter==before+2 and calls.enterIndex==0 and calls.selected=='C')
SelectRole_DoubleClicked(1);assert(calls.enter==before+3 and calls.enterIndex==1 and calls.selected=='A')
''')
        check('enter never sends an empty slot or an invalid native index', '''
records={'A'};roleCount=1;nativeRole=0
LoginSelectRole_OnEvent('GAMELOGIN_REFRESH_ROLE_SELECT_CHARACTOR')
local before=calls.enter
SelectRole_DoubleClicked(2);assert(calls.enter==before and calls.selected=='A')
nativeRole=4;SelectRole_EnterGame();assert(calls.enter==before)
nativeRole=0
''')
        check('invalid native index reports a contract error without selecting', '''
nativeRole=3;local before=calls.selected
local ok=pcall(LoginSelectRole_OnEvent,'GAMELOGIN_REFRESH_ROLE_SELECT_CHARACTOR')
assert(not ok and calls.selected==before)
''')
    finally:
        vm.close()
    checks.append(empty_slot_hover_check(dll))
    result = {'passed': len(checks), 'failed': 0, 'checks': checks,
              'lua_dll_sha256': profile.digest(dll.read_bytes()),
              'controller_sha256': profile.digest(source.read_bytes()),
              'scope': 'Production Lua controller with native-response fixtures; no Game, GPU or network.'}
    (out / 'result.json').write_bytes(profile.json_bytes(result))
    print(json.dumps(result, ensure_ascii=True))


if __name__ == '__main__':
    main()
