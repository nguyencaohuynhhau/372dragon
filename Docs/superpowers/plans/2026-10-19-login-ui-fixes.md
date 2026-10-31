# Sửa lỗi UI luồng đăng nhập → tạo/chọn nhân vật → đổi nhân vật — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Luồng Logon → SelectServer → SelectRole → CreateRole → vào game → đổi nhân vật → vào lại chạy **không còn lỗi UI** (0 `Exception:`, 0 `CYS Lua failure`, 0 `TDThrow`), font chữ thống nhất giữa các màn hình.

**Architecture:** Ưu tiên sửa C++ (một chỗ sửa cho mọi caller). Lỗi data (Lua/XML) sửa bằng **bộ biến đổi có test** trong `Tools/game/` cộng một script deploy theo mẫu `deploy_runtime_repairs.py`. Không bao giờ sửa trực tiếp `Assets/CanonicalCY` (nguồn read-only theo `Assets/Sources/sources.yaml`) hay `Play/` (gitignored, sẽ bị ghi đè khi stage lại). Kết quả cuối cùng được nghiệm thu bằng một checker log tự động chạy trên smoke GUI.

**Tech Stack:** C++ (MSVC 2022, CEGUI 0.4.1 + Falagard, LuaPlus), Python 3 (tooling/test), PowerShell (smoke), Skynet/Lua (server, chỉ đọc trừ Task 7c).

**Spec:** yêu cầu của người dùng ngày 19/10/2026: "fix toàn bộ lỗi UI từ lúc tạo nhân vật, đổi nhân vật không bị lỗi UI, font chữ hợp lý không bị to nhỏ, đảm bảo không lỗi UI". Phần "Bằng chứng" bên dưới đóng vai trò spec chi tiết.

## Bằng chứng (từ log 19/10/2026 và đọc code)

| # | Lỗi | Nguồn log | Nguyên nhân gốc |
|---|---|---|---|
| E1 | `GetRoleInfo: invalid character slot` (15 lần, vẫn còn ở 19:57) | WXClient.log, `LoginSelectRole_Env` dòng 869/901 | Nhánh modern [GMGameinterface_Script_GameProduceLogin.cpp:359-371](../../../Src/Client/WXClient/Interface/GMGameinterface_Script_GameProduceLogin.cpp#L359-L371) ném lỗi khi ô nhân vật trống. `SelectRole.lua` của CY mong nhận `iLevel == 0` như nhánh legacy (dòng 391-399) |
| E2 | `attempt to perform arithmetic on local 'CurClickIndex' (a nil value)` | `LoginSelectServer_Env` dòng 1891 | [FalagardAchievementWindow.cpp:131](../../../Src/Client/CEGUIFalagardEx/FalagardAchievementWindow.cpp#L131) đặt tên `X__auto_element_3__`. Cả 74 chỗ trong Lua CY đều parse `tonumber(string.sub(arg0, ePos+1))`, nên tên phải kết thúc bằng số: `X__auto_element3` |
| E3 | `unknown state 'Normal' in look 'TLBB_Look_SelectServerCatalog_Button'` / `..._SelectServerText_Button` | UISystem.log | `FalagardCyItemButton::populateRenderCache` ([FalagardCyWidgets.cpp:320](../../../Src/Client/CEGUIFalagardEx/FalagardCyWidgets.cpp#L320)) không bao giờ được gọi vì `FalagardButton::drawSelf` (0.4.1, không virtual qua `doButtonRender`) đòi state `Normal`. Look CY chỉ có `EnabledBackground`/`DisabledBackground` |
| E4 | `There is no Property named 'AchievementWindowCatalogButtonHover'` / `'AchievementWindowButtonTextHover'` | UISystem.log | C++ chỉ đăng ký `HoverSection`. Lua (SelectServer.lua:2241,2252; Achievement, AchievementCompare, Agname) dùng tên CY. Giá trị truyền vào (`SelectServerHoverSection`) là **ImagerySection**, không phải StateImagery |
| E5 | Font lệch giữa các màn | đọc layout | SelectRole không đặt Font, nên dùng mặc định SongTiBmp12 = 12px. SelectServer dùng YouYuan11.25 = 15px. CreateRole dùng `CysCR_Control` = 11px. Thêm nữa, cửa sổ bị ép 1280×720 ở login rồi đổi sang `View_Resoution` 1920×1080 trong Init của CharSel/CharCreate ([GameProcedure.cpp:1275-1312](../../../Src/Client/WXClient/Procedure/GameProcedure.cpp#L1275-L1312)), nên chữ trông như to/nhỏ khác nhau |
| E6 | Đổi nhân vật không làm gì | đọc code | Operation.lua gọi `ReturnServerLogin()`, nhưng hàm này là stub ([LuaApiGen_Cy.cpp:3493](../../../Src/Client/LuaApiGen/LuaApiGen_Cy.cpp#L3493)). Mật khẩu bị xóa khỏi bộ nhớ sau đăng nhập (`Secret`, GamePro_Login.cpp:1484), nên không thể tự đăng nhập lại ngầm |
| E7 | `Image 'QiXi_StoryLetter_QZ_N' not found in Imageset 'QiXi_StoryLetter_2'` | UISystem.log | `Schema/wowanimate.xml:31856-31868` ghi sai set; ảnh nằm trong `QiXi_StoryLetter_3` |
| E8 | `No Animate named 'XinShouNew_SheJiaoDaRen_RewardFrame_Flash'` | UISystem.log | `XinShouNew.layout.xml:2466,2560,2655,2750,2845`; chỉ có `XinShouYue_SheJiaoDaRen_RewardFrame_Flash` |
| E9 | `ChildImage ... set:JunXian01 ... ignored` (374 lần) | UISystem.log | `TLBBLook_CreatureBoard.looknfeel.xml:434+` tham chiếu 17 ảnh `JunXian01_R_C`, nhưng imageset này không tồn tại (chỉ có `JunXian1` với 16 ảnh). **Không có nguồn đúng → BLOCKED** (xem D3) |

**Không còn tái diễn, chỉ đưa vào nghiệm thu:** `CY creation profile is not installed` và `CY sect preview model is unavailable`. Hai lỗi này chỉ xuất hiện 17:44–17:45, trước khi `create_role_profile.json` được stage lúc 17:54. Từ 17:58, "appearance tables loaded" và sect preview đều chạy bình thường.

## Quyết định cần người dùng duyệt

- **D1 — Cách đổi nhân vật (Task 7).** *Đề xuất A:* thoát world sạch, quay về màn đăng nhập tài khoản với tên tài khoản điền sẵn, người chơi nhập lại mật khẩu, rồi vào SelectRole. Cách này không sửa giao thức và không giữ mật khẩu trong RAM. *Phương án B:* server cấp token đăng nhập lại để về thẳng SelectRole. Cách này phải sửa `logind.lua` và giao thức nên nằm ngoài plan này.
- **D2 — Font và cỡ cửa sổ (Task 6).** *Đề xuất:* chữ thân trong 4 màn login dùng **YouYuan11.25 (15px)**, font chiếm đa số trong CY (1924 lượt dùng). Tiêu đề giữ YouYuan18. Cửa sổ giữ **1280×720 suốt Logon → SelectServer → SelectRole → CreateRole** và chỉ áp `View_Resoution` khi vào game. Con số 15px sẽ được xác nhận bằng đo ở Task 6 bước 1: nếu ui-fit báo tràn chữ ở CreateRole thì hạ xuống YouYuan13 (13px). **Không đổi** file `.font` toàn cục hay `AutoScaled` (ảnh hưởng khoảng 2.800 layout trong game). Đúng tinh thần `Docs/specs/ui/fonts_vi.md:16`.
- **D3 — JunXian01 (E9).** *Đề xuất:* để BLOCKED cho tới khi tìm được imageset gốc. Phương án khác: xóa 17 dòng `ChildImage` để log sạch; icon quân hàm vốn đang không hiện nên người chơi không thấy khác biệt.

## Global Constraints

- Không ghi vào `Assets/CanonicalCY/**`, `Reference/**`, `C:\TLBB\**`. Thay đổi data đi qua `Play/Overlay/Pre` **và** `Play/Overlay/Post` (quy tắc lookup: `Docs/specs/game/canonical-data_vi.md:64-81`), do script deploy ghi và cập nhật SHA trong `Play/Bin64/game_data_stage.json`.
- Script deploy phải từ chối chạy khi `Game.exe` đang chạy (dùng `idle()` từ `deploy_mount_flight.py`).
- Giữ nguyên encoding: Lua/layout CY là UTF-8 CRLF. `all.*` là GBK nhưng plan này không đụng tới.
- Bằng chứng (log, kết quả test) chỉ ghi dưới `Build/Logs/2026-10-19-login-ui/`.
- Không chạy client khi người dùng đang chơi (`Tests/README_vi.md`).
- Build client: `Tools\build\build.cmd x64 coord-x64 Release` (output `Build/cmake/coord-x64/bin`). Build chỉ chạy khi người dùng cho phép.

## Review Focus

1. **Đổi nhân vật 2 lần liên tiếp:** cửa sổ in-game phải đóng hết, SceneMap/MiniMap không tái dùng marker cũ, fake object của login scene được tạo lại. Được ghim bằng smoke `-SwitchCycles 2` ở Task 7.
2. **Đăng nhập lại ngay sau khi thoát:** `logind.lua:147-157` trả flag 8 (chờ 20s) hoặc 21 (chờ 180s) nếu uuid còn online. Người chơi mong vào lại ngay. Được ghim ở Task 7 bước 5 (đo thời gian) và 7c.
3. **Tài khoản 0 nhân vật hoặc đủ slot:** hover/double-click mọi ô trống không ném lỗi. Được ghim ở Task 2.
4. **Danh sách server bị xóa rồi nạp lại** (`ClearAllElement`): chỉ số phải đếm lại từ 0 và hover vẫn đúng item. Được ghim ở Task 3.
5. **Các nút CyItemButton khác** (ActionButtonItem, GuildButton, AchievementBar*): look có state `Normal` phải render y như cũ. Được ghim ở Task 4.

---

### Task 1: Checker "không lỗi UI" (cổng nghiệm thu)

**Files:**
- Create: `Tools/ui/ui_error_check.py`
- Test: `Tests/ui/ui_error_check_tests.py`

**Interfaces:**
- Produces: `check(ui_log: Path, client_log: Path, since: str | None) -> dict` trả về `{"exceptions": [...], "lua_failures": [...], "tdthrow": [...], "ok": bool}`. CLI: `python Tools/ui/ui_error_check.py --ui <UISystem.log> --client <WXClient.log> [--since "19_20:13:26"] --out <json>`, exit 0 khi `ok`.

- [ ] **Step 1: Viết test lỗi** với fixture dòng thật lấy từ log 19/10. Các assertion: dòng `(Error)\tException: ...` được đếm. `CYS Lua failure env=...` được đếm. `TDThrow: ...` được đếm. Các dòng `CYS-CRASHCEGUI`, `CYS-SCENEMAP`, `CYS-MAPDOTS` (chẩn đoán ghi ở mức Error) **không** được đếm. `MapImage: ChildImage ... ignored` **được** đếm. `--since` bỏ qua các dòng trước mốc. WXClient.log không phải UTF-8 hợp lệ nên phải đọc bằng `errors="replace"`.
- [ ] **Step 2:** `python -m pytest Tests/ui/ui_error_check_tests.py -q` → FAIL (chưa có module).
- [ ] **Step 3:** Cài đặt `check()` bằng regex trên từng dòng.
- [ ] **Step 4:** Chạy lại test → PASS. Chạy trên log hiện tại của `Play/Bin64` và lưu baseline vào `Build/Logs/2026-10-19-login-ui/baseline.json` (số lỗi kỳ vọng: E1, E2, E3, E4, E7, E8, E9 > 0).
- [ ] **Step 5:** Commit `test(ui): add UI error log checker`.

### Task 2: GetRoleInfo trả bộ giá trị "ô trống" thay vì ném lỗi (E1)

**Files:**
- Modify: `Src/Client/WXClient/Interface/GMGameinterface_Script_GameProduceLogin.cpp:359-371`
- Kiểm cùng mẫu lỗi: `Src/Client/WXClient/Procedure/GamePro_CharSel.cpp:406-412`, cùng mọi caller của `GetModernRoleInfo` (`grep -rn GetModernRoleInfo Src/Client`)
- Test: `Tests/ui/create_role_selection_tests.py` (thêm case)

- [ ] **Step 1: Viết test lỗi** `empty_slot_hover`: chạy `SelectRole.lua` của CY với mock `GameProduceLogin:GetRoleInfo(i)` trả `"",0,0,0,""` cho i ≥ số nhân vật. Gọi `SelectRole_MouseEnterCharArea(2)` và `SelectRole_DoubleClicked(2)`. Assert không có lỗi Lua; text `SelectRole_Info` thành `#{DLJM_XML_5}`; double-click gọi `SelectRole_CreateRole`. Test này ghim **hợp đồng** mà C++ phải trả.
- [ ] **Step 2:** Chạy `python Tests/ui/create_role_selection_tests.py` → case mới FAIL khi mock ném lỗi như C++ hiện tại (mock ban đầu chép đúng hành vi ném), sau đó sửa mock thành hợp đồng mới.
- [ ] **Step 3:** Trong nhánh `UsesModernCharacters()`: khi `role == nullptr`, push `""`, `0`, `0`, `0`, `""` và `return 5`. Không còn `TDThrow`. Áp cùng quy tắc cho các caller khác của `GetModernRoleInfo` nếu chúng cũng ném lỗi khi ô trống.
- [ ] **Step 4:** Test PASS. Biên dịch được kiểm ở Task 8.
- [ ] **Step 5:** Commit `fix(login): GetRoleInfo returns empty-slot tuple for modern role list`.

### Task 3: Tên phần tử AchievementWindow phải kết thúc bằng số (E2)

**Files:**
- Modify: `Src/Client/CEGUIFalagardEx/FalagardAchievementWindow.cpp:131`
- Test: `Tests/ui/widget_tests.cpp:861` (sửa tên kỳ vọng) và thêm case mới

- [ ] **Step 1: Viết test lỗi** trong `widget_tests.cpp`: tạo AchievementWindow `Ach`, thêm 3 element. Assert `wm.isWindowPresent("Ach__auto_element0")`, `...1`, `...2`. Sau `ClearAllElement()` rồi thêm 1 element thì `Ach__auto_element0` tồn tại lại (Review Focus 4). Assert phần đuôi sau `"_auto_element"` parse được thành số nguyên. Sửa dòng 861 thành `"Ach__auto_element0"`.
- [ ] **Step 2:** `powershell -File Tests/ui/run_widget_tests.ps1` → FAIL.
- [ ] **Step 3:** Đổi format thành `"__auto_element%d"`. Giữ tiền tố `__auto_` vì [CEGUIWindow.cpp:3875](../../../Src/ThirdParty/CEGUI-0.4.1/CEGUI/src/CEGUIWindow.cpp#L3875) nhận dạng con tự sinh bằng tiền tố này.
- [ ] **Step 4:** Test PASS.
- [ ] **Step 5:** Commit `fix(ui): CY-compatible AchievementWindow element names`.

### Task 4: FalagardCyItemButton render được look CY và nhận tên property CY (E3, E4)

**Files:**
- Modify: `Src/Client/CEGUIFalagardEx/FalagardCyWidgets.h:115-157`, `Src/Client/CEGUIFalagardEx/FalagardCyWidgets.cpp:250-333`
- Test: `Tests/ui/widget_tests.cpp` (+ looknfeel test inline giống `test.looknfeel` ở dòng 328)

**Interfaces:**
- Produces: `void FalagardCyItemButton::drawSelf(float z)`. Thêm property alias `AchievementWindowCatalogButtonHover` và `AchievementWindowButtonTextHover`, cả hai dùng chung `setHoverSection`/`getHoverSection`.

- [ ] **Step 1: Viết test lỗi:**
  - (a) Look chỉ có StateImagery `EnabledBackground`, `DisabledBackground` và ImagerySection `HoverSec`. Tạo `Falagard/AchievementWindowCatalogButton`, set `AchievementWindowCatalogButtonHover=HoverSec`, render khi enabled, hover và disabled. Assert không có exception và render cache khác rỗng.
  - (b) Tương tự cho `Falagard/AchievementWindowTextButton` với `AchievementWindowButtonTextHover`.
  - (c) Look có state `Normal`, dùng cho `Falagard/ActionButtonItem`. Assert render vẫn đi qua `FalagardButton::drawSelf` như cũ (Review Focus 5).
- [ ] **Step 2:** `run_widget_tests.ps1` → FAIL ở (a) và (b).
- [ ] **Step 3:** Cài đặt:
  - `drawSelf(z)`: nếu look có StateImagery `Normal` thì gọi `FalagardButton::drawSelf(z)`; ngược lại gọi `Window::drawSelf(z)`. `Window::drawSelf` là virtual (CEGUIWindow.h:3772) và sẽ gọi `populateRenderCache`.
  - Trong `populateRenderCache`: thứ tự state cơ bản là `Normal`, rồi `EnabledBackground`. Khi bị disable thì là `Disabled`, rồi `DisabledBackground`.
  - Hover: nếu là StateImagery thì render như hiện nay. Nếu không, gọi `wlf.getImagerySection(d_hoverSection).render(*this)` bên trong `try/catch (UnknownObjectException&)` (0.4.1 không có `isImagerySectionPresent`).
  - Đăng ký 2 `StringProp` alias.
- [ ] **Step 4:** Test PASS.
- [ ] **Step 5:** Commit `fix(ui): CyItemButton renders CY background looks and CY hover property names`.

### Task 5: Bộ biến đổi data cho lỗi in-game (E7, E8) và script deploy

**Files:**
- Create: `Tools/game/ui_error_fixes.py` (biến đổi thuần, text vào, text ra)
- Create: `Tools/game/deploy_ui_error_fixes.py` (theo mẫu `Tools/game/deploy_runtime_repairs.py`: `stage()` vào `Build/world-gui/stage-ui-error-fixes-01`, `publish()` có assert SHA và kiểm `idle()`)
- Test: `Tests/ui/ui_error_fixes_tests.py`

**Interfaces:**
- Produces:
  - `fix_qixi_animate(text: str) -> str`: trong animate `QiXi_StoryLetter_AnimateQZ`, đổi `QiXi_StoryLetter_2` thành `QiXi_StoryLetter_3`; phần còn lại giữ nguyên byte.
  - `fix_xinshou_animate(text: str) -> str`: `XinShouNew_SheJiaoDaRen_RewardFrame_Flash` → `XinShouYue_SheJiaoDaRen_RewardFrame_Flash`.
  - `RELS: dict[str, Callable]`: bảng đường dẫn tương đối → hàm biến đổi. Task 6 mở rộng bảng này.

- [ ] **Step 1: Viết test lỗi:**
  - Biến đổi chỉ thay đúng các chuỗi đích: số lần thay qixi bằng số ảnh QZ; xinshou thay đúng 5 lần.
  - Chạy lần hai không đổi gì (idempotent).
  - CRLF và BOM được giữ.
  - Trước khi đổi tên animate, test xác minh trong `wowanimate.xml` rằng `XinShouYue_..._Flash` tồn tại và mọi ảnh nó tham chiếu có trong imageset đã nạp. Nếu không, test fail và E8 chuyển sang phương án xóa property (báo lại người dùng).
- [ ] **Step 2:** `python -m pytest Tests/ui/ui_error_fixes_tests.py -q` → FAIL.
- [ ] **Step 3:** Cài đặt các hàm biến đổi và `deploy_ui_error_fixes.py`. Script đọc bản nguồn theo thứ tự ưu tiên: Overlay Pre nếu đang có, nếu không thì Canonical. Ghi cả Pre lẫn Post và cập nhật `game_data_stage.json` (`origin='ui error fixes'`).
- [ ] **Step 4:** Test PASS. Chạy `python Tools/game/deploy_ui_error_fixes.py stage` và kiểm diff của stage so với Play chỉ gồm các file trong `RELS`.
- [ ] **Step 5:** Commit `fix(data): QiXi animate imageset and XinShou reward flash name`.

### Task 6: Thống nhất font và cỡ cửa sổ ở luồng login (E5, chờ D2)

**Files:**
- Modify: `Tools/game/ui_error_fixes.py` (thêm `login_fonts`)
- Modify: `Src/Client/WXClient/Procedure/GamePro_CharSel.cpp` (Init quanh dòng 135), `Src/Client/WXClient/Procedure/GamePro_CharCreate.cpp` (Init quanh dòng 63), `GameProcedure.cpp:1275-1312`: chỉ áp `View_Resoution` khi chuyển sang `s_pProcEnter`/`s_pProcMain`
- Test: `Tests/ui/ui_error_fixes_tests.py`, `Tests/ui/run_window_size_tests.ps1`

**Interfaces:**
- Produces: `login_fonts(layout_text: str, body_font: str, title_font: str = "YouYuan18") -> str`. Cửa sổ có text mà không có `Font`, hoặc đang dùng `CysCR_Control`/`SongTiBmp12`, sẽ được gán `body_font`. Cửa sổ đang dùng `YouYuan18` giữ nguyên. Áp cho `Logon/LogOn.layout.xml`, `SelectServer/SelectServer.layout.xml`, `SelectRole/SelectRole.layout.xml`, `CreateRole/CreateRole.layout.xml`.

- [ ] **Step 1: Đo trước.** Chạy `CysUiViewer.exe --ui <InterfaceFolder> --data <Data> --dict StrDictionary.txt --size 1280x720 --strict-fonts --out Build/Logs/2026-10-19-login-ui/fonts-before` cho 4 layout. Sau đó chạy `Tools/ui-fit` (`measure_ui_text.py` rồi `report_ui_fit.py`) với `body_font` = YouYuan11.25. Nếu có chữ tràn hoặc bị cắt trong CreateRole thì dùng YouYuan13. Ghi lựa chọn cuối vào `fonts-decision.json`.
- [ ] **Step 2: Viết test lỗi:**
  - Sau `login_fonts`, mọi cửa sổ có thuộc tính `Text` trong 4 layout có `Font` thuộc `{body_font, "YouYuan18"}`.
  - Không còn `CysCR_Control`.
  - Không đụng thuộc tính nào khác; so sánh XML trước và sau chỉ khác ở `Font`.
  - `run_window_size_tests.ps1`: kích thước cửa sổ ở CharSel và CharCreate là 1280×720, và đổi sang `View_Resoution` sau khi vào scene.
- [ ] **Step 3:** Cài đặt `login_fonts` và thêm 4 layout vào `RELS`. SelectRole được overlay **chỉ layout, không đụng Lua**; khác với template placeholder mà `FORCED_GL_ONLY` loại bỏ. Sửa C++: CharSel/CharCreate Init không còn restore `View_Resoution`; việc restore chuyển sang lúc vào Enter/Main.
- [ ] **Step 4:** Test PASS. Chạy lại bước đo bằng ui-fit, kỳ vọng 0 tràn ở 4 màn.
- [ ] **Step 5:** Commit `fix(ui): one body font and one window size across the login flow`.

### Task 7: Đổi nhân vật (E6, chờ D1; mô tả theo phương án A)

**Files:**
- Create: `Src/Client/LuaApiGen/CysHostRelogin.cpp`: đăng ký global `ReturnServerLogin` theo mẫu `CysHostGlobals.cpp:264` (đăng ký trước khi stub cài; stub chỉ cài khi global còn nil)
- Modify: `Src/Client/LuaApiGen/LuaApiGen_Cy.cpp:3493,7026` (xóa stub `ReturnServerLogin`), `Src/Client/WXClient/Network/CysModernGame.cpp` (thêm `RequestReturnToLogin()`), `Src/Client/WXClient/Network/CysModernHandlers.cpp:1271` (`OnSessionClosed`), `Src/Client/WXClient/Procedure/GamePro_Login.cpp` (nhận lại account để điền sẵn)
- Modify (smoke): `Src/Client/WXClient/Procedure/CysGlLoginSmoke.h`, `Tests/ui/run_gl_login_smoke.ps1` (thêm `-SwitchCycles N`), `Tests/ui/validate_gl_login_result.ps1`
- Có thể sửa server (7c): `Server/GameServer/services/login/logind.lua:147-157`, đường logout của msgagent

**Interfaces:**
- Produces: `void CysModernGame::RequestReturnToLogin()`. Hàm gửi `requestQuit()` và đặt cờ `returnToLogin`. Khi nhận `QuitComplete` có cờ này, `OnSessionClosed` **không** đẩy `GE_NET_CLOSE` mà chuyển procedure về `s_pProcLogIn` ở trạng thái nhập tài khoản, với `Login_Account` đã điền sẵn.

- [ ] **Step 1: Viết test lỗi:** smoke `run_gl_login_smoke.ps1 -CreateMode Select -SwitchCycles 2` thực hiện login → vào world → `ReturnServerLogin()` → màn đăng nhập (account điền sẵn) → login lại → chọn nhân vật → vào world, lặp 2 vòng. `validate_gl_login_result.ps1` kiểm tra:
  - mỗi vòng có `phase=in_world`;
  - không có dòng `GE_NET_CLOSE`;
  - `ui_error_check.py` cho `ok` từ mốc bắt đầu smoke;
  - sau mỗi lần quay về login, số cửa sổ in-game đang hiện bằng 0 (log mới `CYS relogin: visible_game_windows=0`);
  - không có `CYS-CRASHCEGUI ... reused=1`.
- [ ] **Step 2:** Chạy smoke → FAIL vì `ReturnServerLogin` là stub.
- [ ] **Step 3:** Cài đặt luồng:
  - `ReturnServerLogin` gọi `CysModernGame::RequestReturnToLogin()`.
  - Khi `QuitComplete`: đóng toàn bộ cửa sổ game bằng `s_pUISystem` (hàm đóng tất cả hiện có; nếu chưa có thì thêm một hàm dùng danh sách layout đã nạp ở `UIWindowMng`), xóa object world (`CGamePro_Main::Release` cùng object manager), reset các module `Cys*Net` như `OnSessionClosed` đang làm, rồi `SetActiveProc(s_pProcLogIn)`. Login procedure tự `CysLoginScene::Enter` và ép 1280×720.
  - Mật khẩu **không** được giữ lại.
- [ ] **Step 4:** Đo thời gian từ `quit_complete` tới khi login lại được chấp nhận. Nếu server trả flag 8 hoặc 21 (uuid còn online) → làm **7c**: sửa đường logout server để `CGAskQuit` sạch xóa uuid khỏi danh sách online ngay. Test 7c bằng smoke đo được < 3s và log `logind` không có flag 8/21.
- [ ] **Step 5:** Smoke PASS cả 2 vòng.
- [ ] **Step 6:** Commit `feat(login): return to account login from in-game (đổi nhân vật)`.

### Task 8: Build, deploy và nghiệm thu toàn luồng

**Files:** không có file mới. Bằng chứng ghi vào `Build/Logs/2026-10-19-login-ui/`.

- [ ] **Step 1:** (người dùng cho phép) Đóng Game.exe. Chạy `Tools\build\build.cmd x64 coord-x64 Release`. Ghi SHA của Game.exe và các DLL thay đổi (`UI_CEGUI.dll`, `CEGUIFalagard*.dll`).
- [ ] **Step 2:** Chạy toàn bộ test: `run_widget_tests.ps1`, `run_window_size_tests.ps1`, `python -m pytest Tests/ui/ui_error_check_tests.py Tests/ui/ui_error_fixes_tests.py -q`, `python Tests/ui/create_role_selection_tests.py`, `python Tests/abi/test_dbc_schema.py --out-dir Build/Logs/2026-10-19-login-ui/dbc`. Tất cả phải PASS.
- [ ] **Step 3:** `python Tools/game/deploy_ui_error_fixes.py stage`, rồi chạy smoke trên stage cho từng `-CreateMode` trong `Preview`, `Create`, `Select`, sau đó `-SwitchCycles 2`.
- [ ] **Step 4:** `python Tools/ui/ui_error_check.py --since <mốc bắt đầu>` → `ok: true`. Các lỗi E1–E8 và hai lỗi đã ngừng tái diễn (profile, sect preview) đều bằng 0. E9 chỉ được phép còn nếu D3 = BLOCKED.
- [ ] **Step 5:** `python Tools/game/deploy_ui_error_fixes.py publish ...` để đưa vào `Play`. Chép binary đã build vào `Play/Bin64` theo mẫu `publish()` (assert SHA).
- [ ] **Step 6:** Checklist tay cho người dùng: tạo nhân vật mới (đổi giới tính, kiểu tóc, xem trước môn phái), hover các ô trống, chọn server, đổi nhân vật 2 lần, so sánh cỡ chữ 4 màn trên ảnh chụp `Build/Logs/2026-10-19-login-ui/shots/`.
- [ ] **Step 7:** Commit `chore(ui): login UI fixes evidence`. Chỉ commit file tracked; Play/Build vốn bị ignore.
