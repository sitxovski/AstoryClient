# Copyright (c) 2026 @sitxovski (Discord)
# asda_jui_to_umg.py — генератор Widget Blueprint (UMG) из распарсенного Login.JUI (окно 199 + логин).
# Запуск ТОЛЬКО внутри UnrealEditor-Cmd -ExecutePythonScript (нужен модуль unreal + UAsdaUmgBuilder).
#
#   UnrealEditor-Cmd.exe AsdaReborn.uproject -ExecutePythonScript="...asda_jui_to_umg.py" \
#       -unattended -nopause -nosplash -nosound -NullRHI -stdout -abslog=<log>
#
# Модель раскладки (подтверждено разбором login_jui.ini + эталоном Slate AsdaServerSelect/AsdaLoginScreen):
#   - Декомп login_jui.ini вкладывает контролы цепочкой (CHS/CHE), НЕ по логической иерархии, поэтому
#     накопленную вложенность НЕ используем. Логический родитель определяется по IDNum-диапазону.
#   - Окно 199 (SERVER_SELECT_WIN): центрируется на экране 1024×768 -> left=394 top=219 (AlignX/Y mode 16/32).
#   - Контролы окна = дети окна: экран = окно + Origin(из JUI). Server Origin.y шаг 40 (данные 1:1),
#     Channel Origin.y шаг 24. Busy-бар (320) + сегменты (321..) — дети своей канал-кнопки.
#   - tex/UV/Diffuse — резолв по Name из JuiSkin.ini (asda_jui_parse.resolve_skin). UV региона поверх атласа.
#
# Что генерим:
#   /Game/AsdaAssets/UI/Generated/WBP_ServerSelect — окно 199 (оба списка сервер+канал в одном полотне,
#       как в оригинале одно окно; C++ переключает видимость через WidgetSwitcher/SetVisibility).
#   /Game/AsdaAssets/UI/Generated/WBP_LoginScreen  — экран логина (панель OK, поля ID/PW, чекбокс, бар-кнопки).
#
# Имена виджетов = ID контрола из JUI (SERVER_BTN00, CHANNEL_BTN00, LOGIN_CONFORM ...) — по ним C++ биндит
# события (см. заметку в конце файла / отчёт).
import os, sys
import unreal

import asda_jui_parse as J

def _login_blocked(path):
    """★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): True — пакет `path` принадлежит сцене логина (asda_login_guard: UI/Login, кроме
    LOGIN_SHARED_EXCEPTIONS, и др.), а ASDA_ALLOW_LOGIN_WRITE != 1: пакет пропустить, строка [LOGIN-GUARD] уже в логе."""
    import sys as _sys_lg
    _here = os.path.dirname(os.path.abspath(__file__))   # Tools — для asda_login_guard
    if _here not in _sys_lg.path:
        _sys_lg.path.insert(0, _here)
    from asda_login_guard import package_blocked_for   # ★ дедуп №2 Т18: имя инструмента — из __file__
    return package_blocked_for(__file__, path)


def log(m):
    unreal.log("[jui2umg] " + str(m))

B = unreal.AsdaUmgBuilder
# ★ Дерево UI → CLASS → NAME (CLASS = исходный JUI). Логин-экраны = класс Login.
GEN_PKG = "/Game/AsdaAssets/UI/Login"

# --- кэш загруженных текстур ---
_texcache = {}
def load_tex(tex_path):
    if not tex_path:
        return None
    if tex_path in _texcache:
        return _texcache[tex_path]
    t = unreal.load_asset(tex_path)
    _texcache[tex_path] = t
    return t

def V2(x, y): return unreal.Vector2D(float(x), float(y))
def LC(rgba): return unreal.LinearColor(rgba[0], rgba[1], rgba[2], rgba[3])
def BOX(uv):  return unreal.Box2D(unreal.Vector2D(uv[0], uv[1]), unreal.Vector2D(uv[2], uv[3]))

# ── ПОЛНЫЕ ПЛАНЫ СКИНОВ (кадры SS0..SS3 каждого плана ES) — нужны стейт-виджетам ────────────────
#   `J.load_skin()` отдаёт ТОЛЬКО кадр 0, поэтому состояния (наведение/нажатие/отмечено) берём из
#   `parse_skins_full`. Разбор ленивый: файл читается один раз при первом обращении.
_skins_full = {}
def _skins_all():
    if not _skins_full:
        _skins_full.update(J.parse_skins_full())
    return _skins_full

def jui_frames4(skin_name):
    """Четыре кадра-состояния плана 0 скина → кортеж `unreal.AsdaJuiFrame` (normal/hover/checked/checked+hover).
    Раскладку по шести состояниям Slate делает `UAsdaUmgBuilder::AddCheckBox` (button_states_re.md)."""
    sk = _skins_all().get(skin_name.lower())
    pl = (sk or {}).get("planes", [None])[0] if sk and sk.get("planes") else None
    out = []
    for i in range(4):
        fr = unreal.AsdaJuiFrame()
        f = (pl or {}).get("frames", {}).get(i) if pl else None
        if f is None and pl:
            f = pl["frames"].get(0)
        if f:
            tn = f.get("tex") or pl["tex"]
            fr.set_editor_property("texture", load_tex("/Game/AsdaAssets/Textures/JUI/%s.%s" % (tn, tn)))
            fr.set_editor_property("uv", BOX(f["uv"]))
            fr.set_editor_property("tint", LC(f["diffuse"]))
        out.append(fr)
    return tuple(out)

def apply_group_anchor(canvas, prefix, afx, afy, base_w=1024.0, base_h=768.0):
    """Переанкорить все контролы canvas с именем на prefix как КОГЕЗИВНЫЙ блок к доле-экрана (afx,afy).
    Слот стоит point-анкором (0,0): offsets.left/top = дизайн-позиция (база 1024×768), right/bottom = размер.
    Новый анкор = (afx,afy), смещение = дизайн-позиция − anchor·База → экран_поз = anchor·РеалРазмер + смещение
    (при 1024×768 = исходная позиция; иначе блок целиком сдвигается к своей области). Размер сохраняется (px).
    Guard: уже переанкоренные (anchor≠0) пропускаем — специфичный префикс вызывать ПЕРВЫМ."""
    for i in range(canvas.get_children_count()):
        child = canvas.get_child_at(i)
        if child is None:
            continue
        nm = child.get_name()
        if not nm.startswith(prefix):
            continue
        slot = child.slot
        if not isinstance(slot, unreal.CanvasPanelSlot):
            continue
        a = slot.get_anchors()
        if abs(a.minimum.x) > 1e-4 or abs(a.minimum.y) > 1e-4 or abs(a.maximum.x) > 1e-4 or abs(a.maximum.y) > 1e-4:
            continue   # уже переанкорен
        off = slot.get_offsets()   # Margin(left=posX, top=posY, right=sizeX, bottom=sizeY)
        new_l = off.left - afx * base_w
        new_t = off.top  - afy * base_h
        slot.set_anchors(unreal.Anchors(unreal.Vector2D(afx, afy), unreal.Vector2D(afx, afy)))
        slot.set_alignment(unreal.Vector2D(0.0, 0.0))
        slot.set_offsets(unreal.Margin(new_l, new_t, off.right, off.bottom))

# hex ARGB (напр. "C8FFCD00") → LinearColor. Звук клика UI (эталон S_e_button09).
def hexargb(h):
    a=int(h[0:2],16)/255.0; r=int(h[2:4],16)/255.0; g=int(h[4:6],16)/255.0; b=int(h[6:8],16)/255.0
    return unreal.LinearColor(r,g,b,a)
CLICK_SND = "/Game/AsdaAssets/Sounds/UI/S_e_button09.S_e_button09"

def styled_btn(bp, canvas, name, pos, size, skin, tn_hex, to_hex, tp_hex):
    """Стилизованная кнопка: тот же UV скина, тинты Nomal/Over/Press + звук клика."""
    tex = load_tex(skin["tex_path"]) if skin else None
    uv = BOX(skin["uv"]) if skin else NO_UV
    return B.add_styled_button(bp, canvas, name, V2(pos[0],pos[1]), V2(size[0],size[1]), tex,
                               uv, hexargb(tn_hex), uv, hexargb(to_hex), uv, hexargb(tp_hex), CLICK_SND)

# бар-кнопки ne_skillbar3: состояния = РАЗНЫЕ UV (Normal/Over/Press) из эталонного логина, тинт белый.
BAR_UV_N=(0.000,0.453,0.480,0.551); BAR_UV_O=(0.000,0.563,0.480,0.660); BAR_UV_P=(0.000,0.449,0.480,0.547)
def styled_btn_uv(bp, canvas, name, pos, size, tex, uvn, uvo, uvp, tint=(1,1,1,1)):
    return B.add_styled_button(bp, canvas, name, V2(pos[0],pos[1]), V2(size[0],size[1]), tex,
                               BOX(uvn), LC(tint), BOX(uvo), LC(tint), BOX(uvp), LC(tint), CLICK_SND)

def add_skin_image(bp, canvas, name, pos, size, skin, tint_override=None):
    """Добавляет Image по скин-записи (tex/UV/Diffuse). tint_override — заменить Diffuse при нужде."""
    tex = load_tex(skin["tex_path"]) if skin else None
    uv  = BOX(skin["uv"]) if skin else NO_UV
    tint = tint_override if tint_override is not None else (LC(skin["diffuse"]) if skin else unreal.LinearColor(1,1,1,1))
    return B.add_image(bp, canvas, name, V2(pos[0], pos[1]), V2(size[0], size[1]),
                       tex, uv, tint, False, NO_BOXM)

# ---------------------------------------------------------------------------
# WBP_ServerSelect — окно 199
# ---------------------------------------------------------------------------
def build_server_select(roots, skins):
    name = "WBP_ServerSelect"
    full = GEN_PKG + "/" + name
    if _login_blocked(full):   # ★ 15.09.2026: окно логина — пересборка (удаление/создание/сохранение) только с разрешением
        return None
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    bp = B.create_widget_bp(GEN_PKG, name)
    if not bp:
        log("FAIL create " + name); return None
    canvas = B.get_root_canvas(bp)

    win = J.find_by_idnum(roots, 199)
    if not win:
        log("окно 199 не найдено"); return None
    # окно центрируется на экране
    J.calc_positions(win, parent_abs=None, is_root_window=True)
    WL, WT, WW, WH = win.abs               # 394,219,236,330
    log("окно 199 abs=%s" % (win.abs,))

    def sk(n): return J.resolve_skin(skins, n)

    # плоский обход дерева окна (собираем все узлы по IDNum независимо от сломанной вложенности)
    allnodes = []
    def walk(n):
        allnodes.append(n)
        for c in n.children: walk(c)
    walk(win)
    byid = {}
    for n in allnodes:
        if n.idnum not in byid:      # первое вхождение
            byid[n.idnum] = n
    def orig(idnum, dx=0.0, dy=0.0):
        n = byid.get(idnum)
        return (n.origin[0]+dx, n.origin[1]+dy) if n else (dx, dy)
    def size_of(idnum, dw=0, dh=0):
        n = byid.get(idnum)
        return (n.size[0], n.size[1]) if n else (dw, dh)

    # --- фон окна (Log_YellowWin 9-slice; используем tpl_yellowwin как в эталоне логина) ---
    tpl = load_tex("/Game/AsdaAssets/Textures/JUI/tpl_yellowwin.tpl_yellowwin")
    B.add_image(bp, canvas, "SERVER_SELECT_WIN", V2(WL, WT), V2(WW, WH), tpl, NO_UV,
                unreal.LinearColor(1,1,1,1), True, unreal.Margin(21.0/46, 21.0/46, 21.0/46, 21.0/46))

    # --- хелпер: 3-частный заголовок (L 5px + C растяжка + R 5px) + 2 болта + текст ---
    def add_titlebar(prefix, base_id, title_txt):
        # base_id: 200 (server) / 300 (channel). Origin из INI относительно окна.
        n = byid.get(base_id)
        if not n: return
        ox, oy = n.origin           # 13,8
        cw, ch = n.size             # 210,29
        L = WL + ox; T = WT + oy
        skC = sk("Log_Title2Caver_C"); skL = sk("Log_Title2Caver_L"); skR = sk("Log_Title2Caver_R")
        skBolt = sk("Log_Bolt")
        # C растяжка
        add_skin_image(bp, canvas, prefix+"_TitleC", (L, T), (cw, ch), skC)
        # L / R капы
        add_skin_image(bp, canvas, prefix+"_TitleL", (L-5, T), (5, ch), skL)
        add_skin_image(bp, canvas, prefix+"_TitleR", (L+cw, T), (5, ch), skR)
        # болты слева/справа
        add_skin_image(bp, canvas, prefix+"_BoltL", (L+4, T+3), (21, 22), skBolt)
        add_skin_image(bp, canvas, prefix+"_BoltR", (L+cw-25, T+3), (21, 22), skBolt)
        # текст заголовка (loc 114/115; тут дефолт-подпись — C++ подменит SetText по имени виджета)
        B.add_text(bp, canvas, prefix+"_TitleText", V2(L, T), V2(cw, ch),
                   title_txt, 11, unreal.LinearColor(0xC0/255.0, 0xC0/255.0, 0xC0/255.0, 1.0), 1)

    add_titlebar("SERVER",  200, "Выбор сервера")
    add_titlebar("CHANNEL", 300, "Выбор канала")

    # --- нижние бар-кнопки «выбор» (SERVER_SELECT_BTN1 202 / CHANNEL_SELECT_BTN1 302) ---
    skBar = sk("Log_Nomal_BarBtn")
    for bid, wname in ((202, "SERVER_SELECT_BTN1"), (302, "CHANNEL_SELECT_BTN1")):
        n = byid.get(bid)
        if not n: continue
        L = WL + n.origin[0]; T = WT + n.origin[1]; w, h = n.size
        styled_btn_uv(bp, canvas, wname, (L, T), (w, h), load_tex(skBar["tex_path"]), BAR_UV_N, BAR_UV_O, BAR_UV_P)
        B.add_text(bp, canvas, wname+"_Text", V2(L, T), V2(w, h),
                   "Выбор", 10, unreal.LinearColor(0xCD/255.0, 0xCD/255.0, 0xCD/255.0, 1.0), 1)

    # --- 5 серверных кнопок (IDNum 210,220,230,240,250) — Origin из JUI (шаг 40) ---
    skSrv = sk("Log_Sever_Base_Nomal")
    for s in range(5):
        idnum = 210 + 10*s
        n = byid.get(idnum)
        if not n: continue
        L = WL + n.origin[0]; T = WT + n.origin[1]; w, h = n.size
        wname = "SERVER_BTN%02d" % s
        # ★ Стилизованная кнопка: Nomal C8FFCD00 / Over FFFFDD53 / Press FF372D00 (JuiSkin) + звук.
        styled_btn(bp, canvas, wname, (L, T), (w, h), skSrv, "C8FFCD00", "FFFFDD53", "FF372D00")
        # подпись сервера (loc 11000+s; C++ подменит по имени виджета SERVER_BTN0s_Text)
        B.add_text(bp, canvas, wname+"_Text", V2(L, T), V2(w, h),
                   "", 11, unreal.LinearColor(0,0,0,1), 1)

    # --- 12 канал-кнопок (IDNum 310,330,...,530) — Origin из JUI (шаг 24) + бар онлайна ---
    skCh = sk("Log_Channel_Base_Nomal")
    skBusyBase = sk("Log_Channel_BusyBase")
    skBusySeg  = sk("Log_Channel_Busy5")
    for c in range(12):
        idnum = 310 + 20*c
        n = byid.get(idnum)
        if not n:
            continue
        L = WL + n.origin[0]; T = WT + n.origin[1]; w, h = n.size
        wname = "CHANNEL_BTN%02d" % c
        # ★ Стилизованная кнопка: Nomal C8FF5100 / Over FFFF7533 / Press FF391300 (JuiSkin) + звук.
        styled_btn(bp, canvas, wname, (L, T), (w, h), skCh, "C8FF5100", "FFFF7533", "FF391300")
        B.add_text(bp, canvas, wname+"_Text", V2(L+8, T), V2(w-8, h),
                   "Канал %d" % (c+1), 10, unreal.LinearColor(0,0,0,1), 0)
        # бар онлайна: подложка 320 (Origin отн. кнопки 84,2, 73×17) + 5 сегментов (321.. шаг 14)
        busy = byid.get(320)
        bx = (busy.origin[0] if busy else 84.0); by = (busy.origin[1] if busy else 2.0)
        bw = (busy.size[0] if busy else 73.0);   bh = (busy.size[1] if busy else 17.0)
        BL = L + bx; BT = T + by
        add_skin_image(bp, canvas, wname+"_BusyBase", (BL, BT), (bw, bh), skBusyBase,
                       tint_override=unreal.LinearColor(0,0,0, skBusyBase["diffuse"][3] if skBusyBase else 1.0))
        for seg in range(5):
            add_skin_image(bp, canvas, "%s_Busy%d" % (wname, seg),
                           (BL + 2 + 14*seg, BT + 2), (13, 13), skBusySeg)

    ok = B.compile_and_save(bp)
    saved = unreal.EditorAssetLibrary.save_loaded_asset(bp)
    log("%s compiled=%s saved=%s -> %s" % (name, ok, saved, full))
    return saved

# ---------------------------------------------------------------------------
# WBP_LoginScreen — экран логина (окно LOGIN_WIN IDNum 0 + декор из эталона)
# ---------------------------------------------------------------------------
def build_login_screen(roots, skins):
    name = "WBP_LoginScreen"
    full = GEN_PKG + "/" + name
    if _login_blocked(full):   # ★ 15.09.2026: окно логина — пересборка (удаление/создание/сохранение) только с разрешением
        return None
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    bp = B.create_widget_bp(GEN_PKG, name)
    if not bp:
        log("FAIL create " + name); return None
    canvas = B.get_root_canvas(bp)
    def sk(n): return J.resolve_skin(skins, n)

    def img_uv(wname, pos, size, texname, uv, tint=(1,1,1,1)):
        tex = load_tex("/Game/AsdaAssets/Textures/JUI/%s.%s" % (texname, texname))
        return B.add_image(bp, canvas, wname, V2(pos[0],pos[1]), V2(size[0],size[1]),
                           tex, unreal.Box2D(unreal.Vector2D(uv[0],uv[1]), unreal.Vector2D(uv[2],uv[3])),
                           unreal.LinearColor(*tint), False, NO_BOXM)
    def box9(wname, pos, size, texname, m_px, tpl):
        tex = load_tex("/Game/AsdaAssets/Textures/JUI/%s.%s" % (texname, texname))
        return B.add_image(bp, canvas, wname, V2(pos[0],pos[1]), V2(size[0],size[1]),
                           tex, NO_UV, unreal.LinearColor(1,1,1,1), True,
                           unreal.Margin(m_px[0]/tpl[0], m_px[1]/tpl[1], m_px[2]/tpl[0], m_px[3]/tpl[1]))

    # Геометрия/UV из эталона AsdaLoginScreen.cpp (выверено с JUI/JuiSkin) — данные 1:1.
    # Виньетка GRAD_BG_FULL (booster, Diffuse 19FFFFFF)
    img_uv("GRAD_BG_FULL", (0,0), (1024,768), "ne_booster_center100",
           (0.000,0.625,0.477,1.000), (1,1,1,0x19/255.0))
    # Логотип + chapter
    img_uv("CHATER_X",   (372,60), (280,244), "n_03", (0.000,0.521,0.547,0.998))
    img_uv("ChapterText",(249,308),(512,125), "n_chapter", (0.000,0.098,1.000,0.342))
    # Панель OK (9-slice) + подложка + кнопка OK
    box9("LOGIN_CONFORM_BASE", (446,614),(132,105), "tpl_yellowwin",(21,21,21,21),(46,46))
    img_uv("LOGIN_CONFORM_BOT_BTN",(457,678),(110,29),"n_02",(0.785,0.561,1.000,0.617))
    # OK-кнопка со стилем состояний (n_02: Normal/Over/Press из эталонного логина) + звук.
    styled_btn_uv(bp, canvas, "LOGIN_CONFORM", (457,625), (110,53), load_tex("/Game/AsdaAssets/Textures/JUI/n_02.n_02"),
                  (0.000,0.617,0.215,0.721), (0.498,0.525,0.713,0.629), (0.227,0.617,0.441,0.721))
    # Поле ID: рамка + декор + лейбл + editbox-плейсхолдер
    box9("LOGIN_EDIT00_BASE",(354,494),(316,46),"tpl_yellowwin",(21,21,21,21),(46,46))
    img_uv("LOGIN_EDIT00_LBASE",(362,502),(29,29),"n_02",(0.385,0.729,0.441,0.785))
    img_uv("LOGIN_EDIT00_RBASE",(633,502),(29,29),"n_02",(0.385,0.729,0.441,0.785))
    img_uv("LOGIN_EDIT00_LBOLT",(366,506),(21,21),"ne_wheeltaget2",(0.734,0.449,0.816,0.531))
    img_uv("LOGIN_EDIT00_RBOLT",(637,506),(21,21),"ne_wheeltaget2",(0.734,0.449,0.816,0.531))
    img_uv("LOGIN_EDIT00_TXT",(472,468),(88,32),"n_chapter",(0.713,0.820,0.885,0.883))
    B.add_editable_text(bp, canvas, "LOGIN_EDIT00", V2(397,507), V2(230,19), "", 12,
                        unreal.LinearColor(0xC6/255.0,0xC6/255.0,0xC6/255.0,1.0), False)
    # Поле PW (+70 Y)
    box9("LOGIN_EDIT01_BASE",(354,564),(316,46),"tpl_yellowwin",(21,21,21,21),(46,46))
    img_uv("LOGIN_EDIT01_LBASE",(362,572),(29,29),"n_02",(0.385,0.729,0.441,0.785))
    img_uv("LOGIN_EDIT01_RBASE",(633,572),(29,29),"n_02",(0.385,0.729,0.441,0.785))
    img_uv("LOGIN_EDIT01_LBOLT",(366,576),(21,21),"ne_wheeltaget2",(0.734,0.449,0.816,0.531))
    img_uv("LOGIN_EDIT01_RBOLT",(637,576),(21,21),"ne_wheeltaget2",(0.734,0.449,0.816,0.531))
    img_uv("LOGIN_EDIT01_TXT",(472,539),(88,32),"n_chapter",(0.713,0.887,0.885,0.949))
    B.add_editable_text(bp, canvas, "LOGIN_EDIT01", V2(397,577), V2(230,19), "", 12,
                        unreal.LinearColor(0xC6/255.0,0xC6/255.0,0xC6/255.0,1.0), True)
    # Чекбокс «сохранить ID» + подложка + подпись
    img_uv("LOGIN_CHECK_BASE",(460,715),(104,21),"n_01",(0.547,0.059,0.566,0.078),(1,1,1,0xC8/255.0))
    # ★ Интерактивный UCheckBox со ВСЕМИ четырьмя кадрами скина `Log_Box_Check` (JuiSkin.ini):
    #   SS0 покой (UV 0.479,0.176 тинт FFE6E6E6 — приглушён), SS1 наведение (тот же UV, FFFFFFFF),
    #   SS2/SS3 отмечено и отмечено+наведение (UV 0.959,0.449 — бокс с галочкой).
    #   Значения НЕ хардкодим — читаем полный разбор планов, как это делает генерик-импортёр.
    B.add_check_box(bp, canvas, "LOGIN_CHECK", V2(465,711), V2(18,19),
                    *jui_frames4("Log_Box_Check"))
    B.add_text(bp, canvas, "LOGIN_CHECK_TXT", V2(486,719), V2(78,15), "Сохранить", 10,
               unreal.LinearColor(1,1,1,1), 0)
    # Бар-кнопки: слева Sign-up (6) / Reset (7), справа Quit (5)
    skBar = sk("Log_Nomal_BarBtn")
    for wname, pos, cap, col in (
        ("LOGIN_BUT01",(12,502),"Нов. учетная запись",(0xCD/255.0,0xCD/255.0,0xCD/255.0,1)),
        ("LOGIN_BUT02",(12,532),"Смена пароля",       (0xCD/255.0,0xCD/255.0,0xCD/255.0,1)),
        ("LOGIN_BUT00",(888,502),"Выход",              (0xFF/255.0,0xD7/255.0,0x1E/255.0,1)),
    ):
        styled_btn_uv(bp, canvas, wname, pos, (123,25), load_tex(skBar["tex_path"]), BAR_UV_N, BAR_UV_O, BAR_UV_P)
        B.add_text(bp, canvas, wname+"_Text", V2(pos[0],pos[1]), V2(123,25), cap, 10,
                   unreal.LinearColor(*col), 1)
    # Помощник: чёрный текст-бокс (9-slice) + хвост + робот
    box9("HELPER_BOX",(744,573),(270,120),"tpl_talkwin",(16,8,8,8),(32,24))
    img_uv("HELPER_TAIL",(914,693),(36,44),"ne_wheeltaget2",(0.859,0.316,1.000,0.488))
    B.add_text(bp, canvas, "HELPER_TEXT", V2(769,590), V2(228,86), "", 9, unreal.LinearColor(1,1,1,1), 0)
    img_uv("HELPER_ROBOT",(916,640),(128,128),"ne_godrobot03_sation",(0.0,0.0,0.25,0.25))

    ok = B.compile_and_save(bp)
    saved = unreal.EditorAssetLibrary.save_loaded_asset(bp)
    log("%s compiled=%s saved=%s -> %s" % (name, ok, saved, full))
    return saved

# ---------------------------------------------------------------------------
# WBP_CharSelect — экран выбора персонажа (state 5).
#   Два JUI-окна в одном полотне (как оригинал: оба слоя лежат поверх 3D-сцены шкатулки):
#     CARASTART_WIN (IDNum 10) — нижняя полоса управления: центр Go(играть)/Add(создать),
#         стрелки поворота слотов L/R, кнопки Удалить/Выход/Назад.
#     CARASTATE_WIN (IDNum 17) — правая панель инфо выбранного перса: имя, Ур/Класс/HP/MP,
#         статы (Сила/Ловк/Выносл/Энергия/Дух/Удача), опыт-гейдж, созвездие.
#   Позиции: root-окна центрируются (calc_positions), дети — mode-aware от ЛОГИЧЕСКОЙ вложенности
#   (декомп-CHS/CHE-цепочка сломана; логику берём из INI-отступов, разобранных при реверсе).
#   Тексты (кор.) 1:1 из JUI TextContent/CaptionText; значения-плейсхолдеры C++ подменит SetText по имени.
# ---------------------------------------------------------------------------
def build_char_select(roots, skins):
    name = "WBP_CharSelect"
    full = GEN_PKG + "/" + name
    if _login_blocked(full):   # ★ 15.09.2026: окно логина — пересборка (удаление/создание/сохранение) только с разрешением
        return None
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    bp = B.create_widget_bp(GEN_PKG, name)
    if not bp:
        log("FAIL create " + name); return None
    canvas = B.get_root_canvas(bp)
    def sk(n): return J.resolve_skin(skins, n)

    # ---- окно CARASTART_WIN (10): нижняя полоса ----
    w10 = J.find_by_idnum(roots, 10)
    if not w10:
        log("окно 10 (CARASTART_WIN) не найдено"); return None
    J.calc_positions(w10, parent_abs=None, is_root_window=True)
    SL, ST, SW, SH = w10.abs                 # (0, 230.5, 1023, 307)
    log("CARASTART_WIN abs=%s" % (w10.abs,))

    # центральная кнопка Go/Add: BUT00_BASE mode X=16 (центр окна) — ставим РОВНО в центр экрана (окно full-width),
    #   стрелки симметрично по бокам (см. ниже). Кластер [◄][Go][►] центрирован.
    baseW, baseH = 117.0, 123.0
    baseL = SL + (SW - baseW) * 0.5   # центр по X окна = центр экрана
    baseT = ST + 20.0
    add_skin_image(bp, canvas, "CARASTART_BUT00_BASE", (baseL, baseT), (baseW, baseH), sk("Log_GoBtn_Base"))

    # Go (играть, IDNum 11) — стилизованная кнопка (Nomal/Over/Press из JuiSkin SS0/1/2 + звук клика)
    goL, goT = baseL + 13.0, baseT + 16.0
    styled_btn(bp, canvas, "CARASTART_BUT00", (goL, goT), (90, 90), sk("Log_GoBtn"), "FFE6E6E6", "FFFFFFFF", "FF808080")
    # Add (создать, IDNum 9) — та же позиция; C++ переключает видимость Go/Add по состоянию слота.
    styled_btn(bp, canvas, "CARASTART_BUT04", (goL, goT), (90, 90), sk("Log_AddBtn"), "FFE6E6E6", "FFFFFFFF", "FF808080")

    # стрелка вправо: СПРАВА от базы (симметрично левой, +10 зазор) 96×102; RBUT внутри O=13,13 73×76
    rbL = baseL + baseW + 10.0
    rbT = baseT + 10.0
    add_skin_image(bp, canvas, "CARASTART_RBUT_BASE", (rbL, rbT), (96, 102), sk("Log_Arrow_RBase"))
    styled_btn(bp, canvas, "CARASTART_RBUT", (rbL + 13.0, rbT + 13.0), (73, 76), sk("Log_ArrowRBtn"), "FFE6E6E6", "FFFFFFFF", "FF808080")

    # стрелка влево: LBUT_BASE mode X=5 (лево BASE) O=-106,10 96×102; LBUT внутри O=10,13 73×76
    lbL = baseL + (-106.0)
    lbT = baseT + 10.0
    add_skin_image(bp, canvas, "CARASTART_LBUT_BASE", (lbL, lbT), (96, 102), sk("Log_Arrow_LBase"))
    styled_btn(bp, canvas, "CARASTART_LBUT", (lbL + 10.0, lbT + 13.0), (73, 76), sk("Log_ArrowLBtn"), "FFE6E6E6", "FFFFFFFF", "FF808080")

    # бар-кнопки: BUT03 «Выход»(16) mode X=5 O=137,50 mode Y=8(низ BASE); BUT01 «Удалить»(14) O=-143,50
    # mode Y=8: top = baseT + baseH - h + origin.y
    barH = 25.0
    but03T = baseT + baseH - barH + 50.0
    but03L = baseL + 137.0
    styled_btn(bp, canvas, "CARASTART_BUT03", (but03L, but03T), (123, barH), sk("Log_NomYellow_BarBtn"), "D8FFFFFF", "FFFFFFFF", "A0FFFFFF")
    B.add_text(bp, canvas, "CARASTART_BUT03_Text", V2(but03L, but03T), V2(123, barH),
               "Выход", 10, unreal.LinearColor(0xFF/255.0, 0xD7/255.0, 0x1E/255.0, 1.0), 1)  # 종료

    but01T = baseT + baseH - barH + 50.0
    but01L = baseL + (-143.0)
    styled_btn(bp, canvas, "CARASTART_BUT01", (but01L, but01T), (123, barH), sk("Log_Nomal_BarBtn"), "D8FFFFFF", "FFFFFFFF", "A0FFFFFF")
    B.add_text(bp, canvas, "CARASTART_BUT01_Text", V2(but01L, but01T), V2(123, barH),
               "Удалить", 10, unreal.LinearColor(0xCD/255.0, 0xCD/255.0, 0xCD/255.0, 1.0), 1)  # 캐릭삭제

    # BUT02 «back»(15) — прямой ребёнок окна, O=0,0 28×36
    styled_btn(bp, canvas, "CARASTART_BUT02", (SL, ST), (28, 36), sk("Log_IndexBack"), "D8FFFFFF", "FFFFFFFF", "A0FFFFFF")
    B.add_text(bp, canvas, "CARASTART_BUT02_Text", V2(SL, ST), V2(28, 36),
               "Назад", 8, unreal.LinearColor(0xCD/255.0, 0xCD/255.0, 0xCD/255.0, 1.0), 1)

    # ---- окно CARASTATE_WIN (17): правая панель инфо ----
    w17 = J.find_by_idnum(roots, 17)
    if not w17:
        log("окно 17 (CARASTATE_WIN) не найдено"); return None
    J.calc_positions(w17, parent_abs=None, is_root_window=True)
    PL, PT, PW, PH = w17.abs                 # (377, 181.5, 270, 405)
    log("CARASTATE_WIN abs=%s" % (w17.abs,))

    # фон панели (Log_YellowWin 9-slice → tpl_yellowwin, как server_select)
    tpl = load_tex("/Game/AsdaAssets/Textures/JUI/tpl_yellowwin.tpl_yellowwin")
    B.add_image(bp, canvas, "CARASTATE_WIN", V2(PL, PT), V2(PW, PH), tpl, NO_UV,
                unreal.LinearColor(1,1,1,1), True, unreal.Margin(21.0/46, 21.0/46, 21.0/46, 21.0/46))

    # заголовок имени: NAME_BAR (Log_Title2Caver_C) O=13,8 244×29 + L/R капы 5px + 2 болта + TXT01 имя
    nbL = PL + 13.0; nbT = PT + 8.0; nbW, nbH = 244.0, 29.0
    add_skin_image(bp, canvas, "CARASTATE_NAME_BAR",  (nbL, nbT), (nbW, nbH), sk("Log_Title2Caver_C"))
    add_skin_image(bp, canvas, "CARASTATE_NAME_LBAR", (nbL - 5.0, nbT), (5, nbH), sk("Log_Title2Caver_L"))
    add_skin_image(bp, canvas, "CARASTATE_NAME_RBAR", (nbL + nbW, nbT), (5, nbH), sk("Log_Title2Caver_R"))
    add_skin_image(bp, canvas, "CARASTATE_NAME_LBOLT", (nbL - 2.0, nbT + 3.0), (21, 22), sk("Log_Bolt"))
    add_skin_image(bp, canvas, "CARASTATE_NAME_RBOLT", (nbL + nbW - 19.0, nbT + 3.0), (21, 22), sk("Log_Bolt"))
    # TXT01 = имя перса (плейсхолдер 캐릭터이름), внутри BAR O=22,7 200×17, align центр
    B.add_text(bp, canvas, "CARASTATE_TXT01", V2(nbL + 22.0, nbT + 7.0), V2(200, 17),
               "", 11, unreal.LinearColor(1,1,1,1), 1)  # имя перса (заполняет C++ UpdateCharSelectInfo)

    # разделительные линии
    add_skin_image(bp, canvas, "CARASTATE_LINE1", (PL + 15.0, PT + 102.0), (240, 1), sk("Log_WhiteBox_Ot30"))
    add_skin_image(bp, canvas, "CARASTATE_LINE2", (PL + 15.0, PT + 345.0), (240, 1), sk("Log_WhiteBox_Ot30"))

    C_LBL = unreal.LinearColor(0xC0/255.0, 0xC0/255.0, 0xC0/255.0, 1.0)   # серый label
    C_YEL = unreal.LinearColor(1.0, 0xC8/255.0, 0x1B/255.0, 1.0)          # жёлтый (레벨/직업)
    C_VAL = unreal.LinearColor(0xC0/255.0, 0xC0/255.0, 0xC0/255.0, 1.0)   # значение
    C_JOB = unreal.LinearColor(0x80/255.0, 1.0, 1.0, 1.0)                 # класс (голубой)

    # пары label(mode5 слева)+value(mode2 справа): value.right = win_left + label.origin.x + label.size.w
    # value рисуем как право-выровненный TextBlock того же right-края; HAlign=2 (right).
    # (label_idnum, lox,loy,lw,lh, label_txt, label_col, val_wname, val_txt, val_col, val_w)
    rows = [
        # Уровень / Класс (над LINE1)
        (1101, 30, 40, 70, 20, "Уровень",   C_YEL, "CARASTATE_TXT03", "", C_YEL, 130),  # loc 11029 (значение золотое)
        (1102, 30, 60, 70, 20, "Профессия", C_YEL, "CARASTATE_TXT05", "", C_JOB, 130),  # loc 11030 (значение ГОЛУБОЕ=класс)
        # ОЗ..Удача (между LINE1 и LINE2). Лейблы = точные RU из error list.bin (loc 11031..11038). Значения ставит C++.
        (1103, 31, 108, 80, 20, "ОЗ",           C_LBL, "CARASTATE_TXT07", "", C_VAL, 120),  # loc 11031 (HP)
        (1104, 31, 138, 80, 20, "ОМ",           C_LBL, "CARASTATE_TXT09", "", C_VAL, 120),  # loc 11032 (MP)
        (1105, 31, 168, 80, 20, "Сила",         C_LBL, "CARASTATE_TXT11", "", C_VAL, 120),  # loc 11033
        (1106, 31, 198, 80, 20, "Ловкость",     C_LBL, "CARASTATE_TXT13", "", C_VAL, 120),  # loc 11034
        (1107, 31, 228, 80, 20, "Выносливость", C_LBL, "CARASTATE_TXT15", "", C_VAL, 120),  # loc 11035
        (1108, 31, 258, 80, 20, "Энергия",      C_LBL, "CARASTATE_TXT17", "", C_VAL, 120),  # loc 11036
        (1109, 31, 288, 80, 20, "Интеллект",    C_LBL, "CARASTATE_TXT19", "", C_VAL, 120),  # loc 11037 (Spirit)
        (1110, 31, 318, 80, 20, "Удача",        C_LBL, "CARASTATE_TXT21", "", C_VAL, 120),  # loc 11038
    ]
    for lidn, lox, loy, lw, lh, ltxt, lcol, vwname, vtxt, vcol, vw in rows:
        lx = PL + lox; ly = PT + loy
        # label по IDNum контейнера — пишем TextBlock с именем CARASTATE_TXTNN_L (label служебный).
        # wrap=False → однострочный, без autowrap-overflow в CanvasPanel (иначе «Выносливость» лезет на значение).
        B.add_text(bp, canvas, "CARASTATE_LBL_%d" % lidn, V2(lx, ly), V2(lw, lh), ltxt, 9, lcol, 0, False)
        # value: правый край = ПРАВЫЙ КРАЙ ПАНЕЛИ (JUI: значение aX=2 выровнено по борту панели,
        #   LINE1 = PL+15..PL+255). wrap=False обязателен, иначе right-justify (HAlign=2) не прижимает к борту.
        vright = PL + 255.0
        vx = vright - vw
        B.add_text(bp, canvas, vwname, V2(vx, ly), V2(vw, lh), vtxt, 9, vcol, 2, False)

    # опыт: label 경험치 (1111) O=30,348 + гейдж-подложка (Log_White1Stroke) O=36,370 200×6 + бар (Log_OrangeBar) O=+2,+2
    B.add_text(bp, canvas, "CARASTATE_LBL_1111", V2(PL + 30.0, PT + 348.0), V2(80, 20),
               "Опыт", 9, unreal.LinearColor(1.0, 0x88/255.0, 0.0, 1.0), 0, False)  # 경험치
    add_skin_image(bp, canvas, "CARASTATE_GAUGE_BASE", (PL + 36.0, PT + 370.0), (200, 6), sk("Log_White1Stroke_Ot30"))
    # заливка: вставка 1px в трек 200×6 → 198×4 (скрин-эталон: оранж почти на всю высоту трека; прежние 196×2
    #   рендерились 2px-нитью — неотличимо от рамки). Ширину клипует C++ (fullW·ExpPct, UpdateCharSelectInfo).
    #   ★ ЭТАЛОН JuiSkin Log_OrangeBar (ProgressBar type5): ES=1 = ЗАЛИВКА — Diffuse=FFFF8C00 (НЕпрозрачный оранж),
    #   UV=0.371 0.020..0.377 0.025 атласа N_01. Парсер сливал состояния (брал UV подложки ES=0 0.676.. → коричневый
    #   тексель × тинт). Рисуем точный спрайт ES=1 × его diffuse — как CProgressBar оригинала.
    B.add_image(bp, canvas, "CARASTATE_GAUGE", V2(PL + 37.0, PT + 371.0), V2(198, 4),
                load_tex("/Game/AsdaAssets/Textures/JUI/n_01.n_01"), BOX((0.371, 0.020, 0.377, 0.025)),
                unreal.LinearColor(1.0, 0x8C/255.0, 0.0, 1.0), False, NO_BOXM)

    # созвездие: 별자리(46, скрыт по умолч.) O=30,80 + значение 물병자리(47) mode2 справа O=0,0 100×20
    # (Visible=0 у 46 — оставляем текст, C++ управляет видимостью группы)
    B.add_text(bp, canvas, "CARASTATE_TXT23", V2(PL + 30.0, PT + 80.0), V2(110, 20),
               "Знак зодиака", 9, C_YEL, 0, False)  # loc 110631
    v47right = PL + 255.0   # значение созвездия — по правому борту панели (как остальные value aX=2)
    B.add_text(bp, canvas, "CARASTATE_TXT24", V2(v47right - 100.0, PT + 80.0), V2(100, 20),
               "", 9, C_YEL, 2, False)  # имя созвездия золотое (C++: Constellation.BIN по id)

    # ---- номер слота (CharacterSlot_Num 700, Log_NoImage_Icon 43×64, центр экрана) ----
    # AlignX="16 50 12" mode16 центр X с параметром → в оригинале индикатор номера над центральным слотом.
    # Позиция = центр экрана (1024/2) ± параметр; рисуем как текстовый индикатор «1/3».
    B.add_text(bp, canvas, "CharacterSlot_Num", V2(1024.0*0.5 - 21.0 + 12.0, 768.0*0.5 - 32.0 + 12.0),
               V2(43, 64), "", 14, unreal.LinearColor(1,1,1,1), 1)

    # ★ ЭКРАННЫЕ анкоры (как per-window генерик-окна): монолит был прибит абсолютом в угол → «квадрат вверху-слева».
    #   Каждая группа садится КОГЕЗИВНО к своей области экрана при любом разрешении (px-размер сохраняется):
    #   панель статов → ПРАВЫЙ край (верх-право), кластер Go/стрелки/бар → ЦЕНТР, back-стрелка → ЛЕВ.край, слот-номер → центр.
    #   Чистый Python (правка CanvasPanelSlot), C++ не нужен. Порядок: специфичный CARASTART_BUT02 ПЕРВЫМ,
    #   guard (уже переанкоренные пропускаем) не даёт общему CARASTART сдвинуть его повторно.
    apply_group_anchor(canvas, "CARASTART_BUT02",   0.0, ST / 768.0)   # back — левый край
    apply_group_anchor(canvas, "CARASTART",         0.5, ST / 768.0)   # Go/стрелки/бар-кнопки — центр
    apply_group_anchor(canvas, "CARASTATE",         1.0, PT / 768.0)   # панель статов — прижата к правому краю
    apply_group_anchor(canvas, "CharacterSlot_Num", 0.5, 0.5)          # индикатор слота — центр экрана

    ok = B.compile_and_save(bp)
    saved = unreal.EditorAssetLibrary.save_loaded_asset(bp)
    log("%s compiled=%s saved=%s -> %s" % (name, ok, saved, full))
    return saved

def main():
    roots = J.parse_jui()
    skins = J.load_skin()
    log("roots=%d skins=%d" % (len(roots), len(skins)))
    r1 = build_server_select(roots, skins)
    r2 = build_login_screen(roots, skins)
    r3 = build_char_select(roots, skins)
    log("RESULT ServerSelect_saved=%s LoginScreen_saved=%s CharSelect_saved=%s" % (r1, r2, r3))
    # ★★★ ЛЮБОЙ путь пересборки окон обязан пройти через возврат ручных правок пользователя
    #   (регресс 2026-08-06, дважды). Раннер идемпотентен и падает сам, если правка не вернулась.
    import importlib.util
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("asda_ui_post_import",
                                                  os.path.join(here, "asda_ui_post_import.py"))
    pi = importlib.util.module_from_spec(spec)
    sys.modules["asda_ui_post_import"] = pi
    spec.loader.exec_module(pi)
    pi.main()


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global NO_BOXM, NO_UV
    # путь к парсеру (тот же каталог Tools)
    _HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools"
    if _HERE not in sys.path:
        sys.path.insert(0, _HERE)
    NO_UV   = unreal.Box2D(unreal.Vector2D(0,0), unreal.Vector2D(0,0))
    NO_BOXM = unreal.Margin(0,0,0,0)
    main()


# ★★★ ЗАКОН tools-no-import-side-effects (нарушение найдено 2026-08-06): здесь стоял ГОЛЫЙ `main()`
#   на уровне модуля — любой `import asda_jui_to_umg` молча пересобирал WBP_ServerSelect /
#   WBP_LoginScreen / WBP_CharSelect с нуля.
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
