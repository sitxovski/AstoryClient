# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_icon_caption_fit.py — ★★ РУССКИЙ ТЕКСТ ШИРЕ КОРЕЙСКОГО: ПОДПИСЬ КНОПКИ СО ЗНАЧКОМ И ЗАГОЛОВОК ПОД КРЕСТИКОМ
#   (07.10.2026, ревизия `Reports/owner_1007/new_windows_audit.md`, дефекты Д-1 и Д-2; отчёт правки —
#   `Reports/owner_1007/audit_fixes.md`). [ИСПРАВЛЕНИЕ ДЕФЕКТА РАЗМЕТКИ ПОД RU] — правка АССЕТОВ, кода не касается
#   (закон no-runtime-widget-fixes).
#
# ═══ Д-1. ПОДПИСЬ КНОПКИ СО ЗНАЧКОМ ═══════════════════════════════════════════════════════════════════════════
#   В .JUI у кнопок `WkBtn_W*/RdBtn_W*` значок 16×16 (`WkBtn_IconCheck`, `RdBtn_IconCancel`, `WkBtn_IconMinus` …)
#   стоит у ЛЕВОГО края, а подпись (`CaptionInfo` «x 0 … 5») центрируется во ВСЮ ширину кнопки. Корейские 2–4 знака
#   помещались между значком и правым краем, русская строка — нет: «О» в «Отмена» и «У» в «Установить ставку»
#   уходят под значок. Замер — путём игрока (`asda.UI.DumpTextMetrics`, шрифт порта Regular 8), таблица случаев
#   и ширины строк — в отчёте.
#   ПРАВКА (тот же приём, что решение владельца 27.09 для «Создание предмета», шаг `asda_ui_text_block_align` (а)):
#   прямоугольник подписи `{idc}_Text` начинается правее значка (+GAP_L), кончается за PAD_R до правого края
#   кнопки, строка центрируется в оставшейся части. Кегль: 8 (как у импортёра), а если самая длинная строка,
#   которую ставит код, при 8 не входит — 7 (верх сдвигается на CAPTION_TOP_SHIFT, центр строки на месте).
#   Кегль выбран ЗАМЕРОМ и записан в таблицу `ICON_CAPTIONS` — шаг не измеряет (в коммандлете нет шрифтового
#   сервиса Slate), он только применяет.
#
# ═══ Д-2. ЗАГОЛОВОК ОКНА ПОД КРЕСТИКОМ ═══════════════════════════════════════════════════════════════════════
#   Узел-заголовок (`*_Text_Move`, `*_Title` …) занимает всю ширину шапки ВМЕСТЕ с крестиком (`CloseBtn_*` —
#   ребёнок/сосед заголовка), клипа нет: длинная русская строка уходит под ×.
#   ПРАВКА: правая граница узла — левее крестика на TITLE_GAP_R, `Clipping = ClipToBounds`. При строке шире поля
#   Slate кладёт её от левого края (FTextLayout::JustifyLayout: ширина раскладки = max(текст, поле)), конец
#   обрезается по границе — до крестика.
#
# ═══ БЕЗОПАСНОСТЬ (закон owner-asset-edits-never-lose) ═══════════════════════════════════════════════════════
#   • Узел трогается, только если он в ТОЧНОСТИ в виде импортёра (слот = формула от кнопки/.JUI, кегль 8,
#     выравнивание центр, интервал 0, клип INHERIT). Уже исправленный — «готово» (идемпотентно). Любой иной вид —
#     «осмотреть», узел НЕ трогается (возможна ручная правка владельца).
#   • Снимок всего дерева окна до/после (класс, родитель, слот, видимость, непрозрачность, кисть, текст) — у прочих
#     узлов без изменений, «потеряно узлов: 0» — иначе исключение ДО сохранения.
#   • Ручной запуск пишет ассет только после бэкапа `T._ensure_backup` → `asda_headless_tool.backup_file` (закон backup-before-edit).
#   • Только ассеты из таблиц; окна сцены логина — под `asda_login_guard` (раннер пост-импорта).
#
# Запуск (редактор и игра ЗАКРЫТЫ; коммандлет, не -ExecutePythonScript — иначе грузится Login.umap):
#   python Tools\run_headless_safe.py Tools\asda_ui_icon_caption_fit.py --commandlet-rhi --marker "ПОДПИСИ-ЗНАЧКИ: ГОТОВО"
#   ASDA_ICONCAP_DRYRUN=1 — сухой прогон. Вне редактора: `python asda_ui_icon_caption_fit.py --plan`.
import os
import sys
import traceback

try:
    import unreal
except ImportError:
    unreal = None

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_jui_parse as J  # noqa: E402
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

# Бэкап ДО записи (★ Т3, 11.10.2026): набор `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_icon_caption_fit\<путь от @@ASTORY_DEV_ROOT@@>`
#   (была прошитая папка `2026-10-07_audit_fixes`)
BACKUP_SET = "icon_caption_fit"
INI_DIR = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui"

GAP_L = 1.0                 # зазор между правым краем значка и прямоугольником подписи, px базы (как шаг ремесла)
PAD_R = 3.0                 # правое поле подписи до края кнопки (рамка скина), px базы (как шаг ремесла)
FONT_PX_IMPORTER = 8        # кегль подписей импортёра (`FONT_PX` в asda_jui_generic_umg.py)
CAPTION_TOP_SHIFT_7 = 1.5   # кегль 7: строка на 3 px ниже (13 → 10) — верх вниз на половину, центр строки на месте
LINE_PX = 14                # высота строки, по которой импортёр центрирует однострочный текст
TITLE_GAP_R = 2.0           # зазор между правой границей заголовка и левым краем крестика, px базы
EPS = 0.05                  # допуск сравнения чисел слота

# ★ Д-1: кнопки со значком, у которых русская подпись налезает на значок (замер путём игрока 07.10.2026).
#   (ассет, ID окна .JUI, ID кнопки .JUI, кегль | (кегль, GAP_L, PAD_R)) — кегль 8 или 7 по замеру самой длинной
#   строки, которую ставит код (или текста ассета, если код подпись не трогает); зазоры — только где 7 не входит в 78 px.
#   Список ЗАКРЫТЫЙ: новые случаи добавлять только по замеру (отчёт audit_fixes.md, раздел «Д-1»).
ICON_CAPTIONS = [
    # ★ 07.10.2026 (решение владельца — кнопку/окно расширять, а не кегль 7): СНЯТЫ отсюда шесть кнопок кегля 7 —
    #   CLAN_MAKE_BTN, CMTY_CLAN_SEC_BTN, Cmty_Who_FaddBtn, SMate_ReqAnsr_WhisBtn, TWOBUT_BUT00, Town_as_okbtn: их кнопки
    #   расширены шагом `asda_ui_widen_windows` (кегль 8, подпись правее значка так же, +1 / −3 px). Тот шаг принимает
    #   подпись и в виде импортёра, и в прежнем виде этого шага. Кегль 7 у MESSAGE2_BUT01 оставлен: окно в папке Login.
    #   ★ 07.10.2026 (правки ревизии, `widen_fixes.md`, разрешение владельца на ОДНУ кнопку логина): снята и MESSAGE2_BUT01
    #   «Отменить» обоих окон (MESSAGE2_WIN и _NEUTRAL) — кнопка +3 px и подпись кеглем 8 в шаге `asda_ui_widen_windows`
    #   (тот шаг принимает подпись и в виде импортёра, и в прежнем виде этого шага с кеглем 7).
    ("/Game/AsdaAssets/UI/Login/WBP_AD_Win", "AD_Win", "AD_ClsBtn2", 8),   # «Закрыть» 43 из 50
    ("/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN", "COMMUNITY_WIN", "Soul_Msg_ClBtn", 8),   # «Отмена» 40 из 49
    ("/Game/AsdaAssets/UI/Login/WBP_MESSAGE2_WIN", "MESSAGE2_WIN", "MESSAGE2_BUT00", 8),   # «Обычный» 49 / «Удалить» 42 из 50
    ("/Game/AsdaAssets/UI/Login/WBP_MESSAGE2_WIN_NEUTRAL", "MESSAGE2_WIN", "MESSAGE2_BUT00", 8),   # то же, нейтральный вариант
    ("/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYBOX_WIN", "PARTYBOX_WIN", "PARTYBOX_NO", 8),   # «Отмена» 40 из 49
    ("/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYDISPERSION", "PARTYDISPERSION", "PARTYDISPERSION_CANCEL", 8),   # «Отмена» 40 из 49 (Д-1 ревизии, 2256)
    ("/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYTAKE_WIN", "PARTYTAKE_WIN", "PARTYTAKE_NO", 8),   # «Отмена» 40 из 49
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_SHELLITEM_STORE2_WIN", "SHELLITEM_STORE2_WIN", "SHELLITEM_STORE2_BUT02", 8),   # «Надеть куплен. вещи» 108 из 118
    ("/Game/AsdaAssets/UI/Player_UI/Social/WBP_SHOUTING_WIN", "SHOUTING_WIN", "Shouting_CacelBtn", 8),   # «Отмена» 40 из 49
    ("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_SOULMATE_LINK_WIN", "SOULMATE_LINK_WIN", "SOULMATE_LINK_BTN0", 8),   # «Отмена» 40 из 49 (Д-1 ревизии, 9100)
    ("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_SOWELL_SELBOX", "SOWELL_SELBOX", "SOWELL_BUT01", 8),   # «Отмена» 40 из 49
    ("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Title_Win", "Title_Win", "Title_List_AppBtn", 8),   # «Применить» 57 из 58
]

# ★ Д-2: заголовки, уходящие под крестик (замер путём игрока 07.10.2026).
#   (ассет, ID окна .JUI, ID узла-заголовка .JUI, ID крестика .JUI).
TITLE_CLIPS = [
    # ★ 07.10.2026: 56450 `Hunt report_Text_Move` СНЯТ отсюда — окно расширено целиком шагом `asda_ui_widen_windows`
    #   (решение владельца: расширять окно, а не обрезать), заголовок входит полностью кеглем 8.
    ("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_INVEN_WIN", "INVEN_WIN", "Inven_Strength_TitleTxt", "Inven_Strength_CloseBtn"),   # текст ассета 167 px центром в 199, крестик с 180 (панель портом пока не показывается)
]


def log(m):
    """Строка журнала шага (в редакторе — в лог UE, вне — в stdout)."""
    if unreal:
        unreal.log("[подписи-значки] %s" % m)
    else:
        print("[подписи-значки] %s" % m)


def _ini(cls="gamemain"):
    """Тот же .ini, что читает импортёр (приоритет локализованного): gamemain / commonwin / temp / login."""
    p = os.path.join(INI_DIR, "%s_loc.ini" % cls)
    return p if os.path.isfile(p) else os.path.join(INI_DIR, "%s_jui.ini" % cls)


def _all_roots():
    """Окна всех четырёх .ini (окна логина AD_Win/MESSAGE2_WIN лежат в login_*.ini)."""
    out = []
    for cls in ("gamemain", "commonwin", "temp", "login"):
        out += J.parse_jui(_ini(cls))
    return out


_walk_nodes = _UWC.walk_jui_nodes   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def _find_idc(nodes, idc):
    """Узел .JUI по строковому ID (первое совпадение)."""
    for r in nodes:
        for n in _walk_nodes(r):
            if n.idc == idc:
                return n
    return None


def plan():
    """Случаи по данным .JUI: [{kind: 'caption'|'title', asset, idc, …геометрия…}]. Без unreal."""
    roots = _all_roots()
    out = []
    for asset, win_idc, btn_idc, spec in ICON_CAPTIONS:
        font, gap_l, pad_r = spec if isinstance(spec, tuple) else (spec, GAP_L, PAD_R)
        win = _find_idc(roots, win_idc)
        if win is None:
            raise RuntimeError("в .ini нет окна %s" % win_idc)
        J.compute_anchors(win)
        parent = {}
        for n in _walk_nodes(win):
            for c in n.children:
                parent[id(c)] = n
        btn = _find_idc([win], btn_idc)
        if btn is None:
            raise RuntimeError("в окне %s нет кнопки %s" % (win_idc, btn_idc))
        bl, bt, bw, bh = btn.abs
        pool = list(btn.children) + list(parent[id(btn)].children if id(btn) in parent else [])
        icons = [c for c in pool if c is not btn and "icon" in (c.name or "").lower() and 0 < c.abs[2] <= 24
                 and bl - 2 <= c.abs[0] < bl + bw / 2.0 and bt <= c.abs[1] + c.abs[3] / 2.0 <= bt + bh]
        if len(icons) != 1:
            raise RuntimeError("кнопка %s: значков слева %d (ожидался 1)" % (btn_idc, len(icons)))
        ic = icons[0]
        out.append({"kind": "caption", "asset": asset, "win": win_idc, "idc": btn_idc, "size": (float(bw), float(bh)),
                    "off": tuple(float(v) for v in btn.cap_off), "mask": J.label_mask(btn),  # ★ 08.10 МЕЛК-2
                    "icon": ic.name, "icon_rel": float(ic.abs[0] + ic.abs[2] - bl), "font": float(font),
                    "gap_l": float(gap_l), "pad_r": float(pad_r)})
    for asset, win_idc, txt_idc, x_idc in TITLE_CLIPS:
        win = _find_idc(roots, win_idc)
        if win is None:
            raise RuntimeError("в .ini нет окна %s" % win_idc)
        J.compute_anchors(win)
        t = _find_idc([win], txt_idc)
        x = _find_idc([win], x_idc)
        if t is None or x is None:
            raise RuntimeError("в окне %s нет заголовка %s или крестика %s" % (win_idc, txt_idc, x_idc))
        tp = getattr(t, "txt_pos", None) or (0, 0)
        out.append({"kind": "title", "asset": asset, "win": win_idc, "idc": txt_idc, "x": x_idc,
                    "size": (float(t.abs[2]), float(t.abs[3])), "offx": float(tp[0]),
                    "x_rel": float(x.abs[0] - t.abs[0])})
    return out


def print_plan():
    """Печать случаев без редактора."""
    for c in plan():
        if c["kind"] == "caption":
            print("подпись %s %s: кнопка %s, значок %s до %.0f px → подпись %.0f…%.0f, кегль %.0f"
                  % (c["asset"].rsplit("/", 1)[-1], c["idc"], c["size"], c["icon"], c["icon_rel"],
                     c["icon_rel"] + c["gap_l"], c["size"][0] - c["pad_r"], c["font"]))
        else:
            print("заголовок %s %s: поле %s, крестик %s на %.0f px → ширина %.0f, клип"
                  % (c["asset"].rsplit("/", 1)[-1], c["idc"], c["size"], c["x"], c["x_rel"],
                     c["x_rel"] - c["offx"] - TITLE_GAP_R))


# ───────────────────────────── РЕДАКТОР ─────────────────────────────
def _tb():
    """Общие помощники шага `asda_ui_text_block_align` (снимок, слоты, бэкап) — одна реализация на оба шага."""
    import asda_ui_text_block_align as T
    return T


def _clip_name(w):
    """Режим клипа виджета строкой (`INHERIT`, `CLIP_TO_BOUNDS` …)."""
    return str(w.get_editor_property("clipping")).split(".")[-1].split(":")[0].strip().upper()


def _handle_caption(case, nodes, dry):
    """Д-1: подпись `{idc}_Text` правее значка, кегль по таблице. ('fixed'|'ok'|'inspect', текст, имя узла)."""
    T = _tb()
    idc = J._san(case["idc"])
    tname = idc + "_Text"
    tw, bw = nodes.get(tname), nodes.get(idc)
    if not isinstance(tw, unreal.TextBlock) or bw is None:
        return "inspect", "%s: нет подписи-TextBlock %s или кнопки" % (idc, tname), tname
    if (tw.get_parent().get_name() if tw.get_parent() else "") != (bw.get_parent().get_name() if bw.get_parent() else ""):
        return "inspect", "%s: подпись и кнопка в разных группах" % tname, tname
    ts, bs = T._slot_sig(tw), T._slot_sig(bw)
    if ts is None or bs is None or ts[1] != bs[1] or bs[3] or ts[3]:
        return "inspect", "%s: слоты подписи/кнопки не канвасные, с разными анкорами или авторазмер" % tname, tname
    bl, bt, bwid, bh = bs[0]
    cw, ch = case["size"]
    if not T._near((bwid, bh), (cw, ch)):
        return "inspect", "%s: кнопка %s×%s ≠ .JUI %s×%s" % (tname, bwid, bh, cw, ch), tname
    capx, capy = case["off"]
    mask = case["mask"]
    # ★ 07.10.2026 (ревизия label_band МЕЛК-3): вид импортёра — общая точка `J.label_slot` (caption-ветка
    #   add_control), в обоих допустимых видах: прежний max(h, 14) и «по низу контрола» (узел за рамкой окна).
    old, old_cut = J.label_slot_forms(bl, bt, bwid, bh, capx, capy, mask)
    cut = T._near(ts[0], old_cut) and not T._near(ts[0], old)    # вход «по низу контрола» — итог тоже по низу
    old_t = old[1]
    left = bl + case["icon_rel"] + case["gap_l"]
    font_want = case["font"]
    top = old_t + (CAPTION_TOP_SHIFT_7 if font_want < FONT_PX_IMPORTER - EPS else 0.0)
    wide = (bl + bwid - case["pad_r"]) - left
    # Итог: высота по правилу `label_slot` в виде входа; «готово» — любой из двух видов (прежний итог шага тоже).
    want_forms = [(left, top, wide, J.label_height_like(bt, bh, top, c)) for c in (False, True)]
    want = want_forms[1] if cut else want_forms[0]
    font = tw.get_editor_property("font")
    ls = int(font.get_editor_property("letter_spacing"))
    size = float(font.get_editor_property("size"))
    just = str(tw.get_editor_property("justification")).split(".")[-1].split(":")[0].strip().upper()
    if just != "CENTER" or not T._near(ts[2], (0.0, 0.0)) or ls != 0:
        return "inspect", "%s: выравнивание %s / alignment %s / интервал %d — не вид импортёра" % (tname, just, ts[2], ls), tname
    if any(T._near(ts[0], wf) for wf in want_forms) and abs(size - font_want) <= EPS:
        return "ok", "%s: уже исправлено (подпись %.0f…%.0f, кегль %.0f)" % (tname, want[0], want[0] + want[2], size), tname
    if not (T._near(ts[0], old) or T._near(ts[0], old_cut)) or abs(size - FONT_PX_IMPORTER) > EPS:
        return "inspect", "%s: слот %s / кегль %.1f не совпадают с видом импортёра %s / %d — НЕ трогаю" \
            % (tname, ts[0], size, old, FONT_PX_IMPORTER), tname
    if not dry:
        T._set_slot(tw, want, (0.0, 0.0), False)
        if abs(font_want - size) > EPS:
            font.set_editor_property("size", font_want)
            tw.set_editor_property("font", font)
    return "fixed", "%s: слот %s → %s (правее значка %s), кегль %d → %.0f" \
        % (tname, ts[0], want, case["icon"], FONT_PX_IMPORTER, font_want), tname


def _handle_title(case, nodes, dry):
    """Д-2: заголовок до крестика + ClipToBounds. ('fixed'|'ok'|'inspect', текст, имя узла)."""
    T = _tb()
    name = J._san(case["idc"])
    tw = nodes.get(name)
    xw = nodes.get(J._san(case["x"]))
    if not isinstance(tw, unreal.TextBlock):
        return "inspect", "%s: нет TextBlock-заголовка" % name, name
    ts = T._slot_sig(tw)
    if ts is None or ts[3]:
        return "inspect", "%s: слот не канвасный или авторазмер" % name, name
    cw, ch = case["size"]
    want_w = case["x_rel"] - case["offx"] - TITLE_GAP_R
    # Сверка с живым крестиком: если он в той же группе и с теми же анкорами — правая граница по его слоту.
    if xw is not None and xw.get_parent() is not None and tw.get_parent() is not None \
            and xw.get_parent().get_name() == tw.get_parent().get_name():
        xs = T._slot_sig(xw)
        if xs is not None and xs[1] == ts[1] and not xs[3]:
            live_w = xs[0][0] - TITLE_GAP_R - ts[0][0]
            if abs(live_w - want_w) > 1.0 and abs(ts[0][2] - (cw - case["offx"])) <= EPS:
                return "inspect", "%s: ширина по .JUI %.1f ≠ по слоту крестика %.1f — НЕ трогаю" % (name, want_w, live_w), name
    clip = _clip_name(tw)
    if T._near((ts[0][2],), (want_w,)) and clip == "CLIP_TO_BOUNDS":
        return "ok", "%s: уже исправлено (ширина %.0f, клип)" % (name, want_w), name
    if not T._near((ts[0][2],), (max(cw - case["offx"], 8.0),)) or clip != "INHERIT":
        return "inspect", "%s: ширина %.1f / клип %s не вид импортёра (%.1f / INHERIT) — НЕ трогаю" \
            % (name, ts[0][2], clip, max(cw - case["offx"], 8.0)), name
    if not dry:
        T._set_slot(tw, (ts[0][0], ts[0][1], want_w, ts[0][3]), ts[2], False)
        tw.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)
    return "fixed", "%s: ширина %.0f → %.0f (до крестика %s), клип INHERIT → CLIP_TO_BOUNDS" \
        % (name, ts[0][2], want_w, case["x"]), name


def main(standalone=False):
    """Применить Д-1/Д-2 к ассетам таблиц. `standalone` — ручной запуск: бэкап обязателен."""
    T = _tb()
    dry = bool(os.environ.get("ASDA_ICONCAP_DRYRUN"))
    EAL = unreal.EditorAssetLibrary
    B = unreal.AsdaUmgBuilder
    cases = plan()
    by_asset = {}
    for c in cases:
        by_asset.setdefault(c["asset"], []).append(c)
    fixed_total = ok_total = 0
    inspect = []
    from asda_login_guard import login_package_blocked
    for asset, lst in by_asset.items():
        if login_package_blocked(asset, "asda_ui_icon_caption_fit.py"):
            log("%s: окно сцены логина — пропуск (защита логина)" % asset)
            continue
        bp = EAL.load_asset(asset)
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            raise RuntimeError("нет корневого канваса у %s" % asset)
        nodes = {w.get_name(): w for w in T._walk(root)}
        before = T._snapshot(root)
        clip_before = {n: _clip_name(w) for n, w in nodes.items()}

        def handle(c, d):
            """Один случай любого вида."""
            return _handle_caption(c, nodes, d) if c["kind"] == "caption" else _handle_title(c, nodes, d)
        verdicts = [handle(c, True) for c in lst]
        for v, msg, _n in verdicts:
            log("%s [%s] %s" % (asset.rsplit("/", 1)[-1], {"fixed": "править", "ok": "готово", "inspect": "осмотреть"}[v], msg))
        todo = [c for c, (v, _m, _n) in zip(lst, verdicts) if v == "fixed"]
        ok_total += sum(1 for v, _m, _n in verdicts if v == "ok")
        inspect += ["%s: %s" % (asset, m) for v, m, _n in verdicts if v == "inspect"]
        if dry or not todo:
            continue
        if standalone:
            T._ensure_backup(asset, BACKUP_SET)
        changed_names = set()
        for c in todo:
            _v, _msg, name = handle(c, False)
            changed_names.add(name)
        root2 = B.get_root_canvas(bp)
        after = T._snapshot(root2)
        clip_after = {w.get_name(): _clip_name(w) for w in T._walk(root2)}
        lost = [n for n in before if n not in after]
        extra = [n for n in after if n not in before]
        changed = [n for n in before if n in after and (before[n] != after[n] or clip_before.get(n) != clip_after.get(n))]
        foreign = [n for n in changed if n not in changed_names]
        cap_names = set(J._san(c["idc"]) + "_Text" for c in todo if c["kind"] == "caption")

        def keep(sig, name):
            """Неизменяемая часть текста: (вид, строка, цвет, кегль (кроме подписей Д-1), начертание, интервал)."""
            if not sig or sig[0] != "TB":
                return sig
            return (sig[0], sig[1], sig[2], None if name in cap_names else sig[6], sig[7], sig[8])
        broken = [n for n in changed_names
                  if before[n][:2] != after[n][:2] or before[n][3:6] != after[n][3:6]
                  or keep(before[n][6], n) != keep(after[n][6], n)
                  or (before[n][6] and after[n][6] and before[n][6][3] != after[n][6][3])   # justification не трогаем
                  or (before[n][2] and after[n][2] and (before[n][2][1] != after[n][2][1] or before[n][2][4] != after[n][2][4]))]
        log("%s: узлов до %d, после %d; правлено узлов %d; потеряно узлов: %d; лишних: %d; изменено чужих: %d; "
            "испорчено у правленых: %d" % (asset.rsplit("/", 1)[-1], len(before), len(after), len(changed_names),
                                          len(lost), len(extra), len(foreign), len(broken)))
        if lost or extra or foreign or broken:
            raise RuntimeError("сверка не прошла: потеряно %s, лишних %s, чужих %s, испорчено %s — НЕ сохраняю"
                               % (lost, extra, foreign, broken))
        for n in sorted(changed_names):
            log("   %s: слот %s → %s; клип %s → %s; текст %s → %s" % (n, before[n][2], after[n][2], clip_before.get(n),
                                                                     clip_after.get(n), before[n][6], after[n][6]))
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % asset)
        log("ASDA_WRITES_ASSET: %s" % asset)
        fixed_total += len(changed_names)
    for s in inspect:
        log("осмотреть: %s" % s)
    log("случаев %d: исправлено %d, уже исправлено %d, осмотреть %d%s"
        % (len(cases), fixed_total, ok_total, len(inspect), " (СУХОЙ ПРОГОН)" if dry else ""))
    if inspect and not dry:
        # Раннер пост-импорта должен увидеть, что правка не легла (после перегенерации вид импортёра — правка ляжет;
        #   «осмотреть» значит чужая правка в узле — прогон останавливается, а не пишет «готово»).
        raise RuntimeError("подписи-значки: %d случаев «осмотреть» — узлы не в виде импортёра, см. лог" % len(inspect))
    log("ПОДПИСИ-ЗНАЧКИ: ГОТОВО")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля (раннер пост-импорта) — ничего.
if __name__ == "__main__":
    if "--plan" in sys.argv or unreal is None:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        print_plan()
    else:
        try:
            main(standalone=True)
        except Exception:
            unreal.log_error("[подписи-значки] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
