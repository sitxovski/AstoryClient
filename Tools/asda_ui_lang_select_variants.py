# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_lang_select_variants.py — ★ 11.10.2026 ВАРИАНТЫ ОКНА ПЕРВОГО ВЫБОРА ЯЗЫКА (на выбор владельцу, owner_1011/lang_window).
#
# ═══ ЧТО И ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════
#   Владелец: три кнопки окна `WBP_LANG_SELECT` стоят на нижней кромке рамки и третья накрывает значок-печать правого
#   нижнего угла; «как минимум приподнять, как максимум — красивее». Окно наше (в оригинале его нет), переделку
#   владелец разрешил. Этот шаг делает ВРЕМЕННЫЕ варианты-копии (рабочий ассет и исходник `/UI/Login/` не трогаются):
#     • V1 — те же рамка и кнопки; ряд кнопок поднят над нижним рядом рамки (печать свободна), вопрос — по центру тела;
#     • V2 — та же чёрная рамка, окно шире; в шапке — оливковая табличка заголовка с двумя винтами (детали окна выбора
#            сервера `WBP_ServerSelect`), на ней вопрос стилем заголовка сервера; кнопки шире, ряд по центру тела;
#     • V3 — рамка и бурая полоса заголовка окна настроек (`WBP_SYSTEM_WIN`, атлас n_01) с его значком; вопрос — подписью
#            заголовка; кнопки столбцом.
#   Все детали — КЛОНЫ узлов оригинальных окон (кисти атласов JUI копируются со свойствами узла; ничего не рисуется).
#   Торцы кнопок, углы рамок и торцы полос не растягиваются — тянутся только середины. Имена узлов, которые ищет код
#   (`MESSAGE2_TXT00`, `MESSAGE2_BUT00..02(_Text)`), сохранены во всех вариантах.
#
# ═══ ПРОВЕРКА ═══════════════════════════════════════════════════════════════════════════════════════
#   Вариант уже есть — пропуск. Образцы (`WBP_LANG_SELECT`, `WBP_SYSTEM_WIN`, `WBP_ServerSelect`) только читаются;
#   дата файла рабочего `WBP_LANG_SELECT` сверяется до/после. Маркер: «ВАРИАНТЫ ОКНА ЯЗЫКА: ГОТОВО».
# Запуск (редактор закрыт, -game не запущен, окон не показывать; под замком UE):
#   python Tools\run_headless_safe.py Tools\asda_ui_lang_select_variants.py --marker "ВАРИАНТЫ ОКНА ЯЗЫКА: ГОТОВО"
import os
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_option_extra_rows as X   # общие помощники клонирования узлов (без побочных эффектов при импорте)

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

SRC = "/Game/AsdaAssets/UI/CommonWin/WBP_LANG_SELECT"                    # рабочий ассет — только как исходник копий
DIR = "/Game/AsdaAssets/UI/CommonWin/_LangVariants/"                      # папка временных вариантов
SYS_WIN = "/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_WIN"          # образец: рамка n_01 + полоса заголовка
SRV_WIN = "/Game/AsdaAssets/UI/Login/WBP_ServerSelect"                    # образец: табличка, винты, подпись (ТОЛЬКО чтение)
CX = 512.0                                                                # центр окна по X (база 1024)
CY = 384.0                                                                # центр окна по Y (база 768)
BTN_H = 28.0                                                              # высота группы кнопки (скин ne_skillbar1)


def log(m):
    """Строка журнала шага."""
    unreal.log("[варианты окна языка] %s" % m)


# ★ 11.10.2026: общие помощники раскладки и разметка V3 — в генераторе рабочего окна (V3 выбран владельцем).
import asda_ui_lang_select_window as LW   # без побочных эффектов при импорте
frame9, button, text_box, nodes_of, index_after = LW.frame9, LW.button, LW.text_box, LW.nodes_of, LW.index_after


def clone_from(st, bp, src, new_name, left, top, w, h, index):
    """Клон узла окна-образца в корень варианта (`LW.clone_from`) и его прямоугольник (left, top, w, h)."""
    w_ = LW.clone_from(st, bp, src, new_name, index)
    X.set_offsets(w_, left, top, w, h)
    return w_


def make_variant(name):
    """Дубликат исходника под именем варианта; (None, путь) — уже есть."""
    dst = DIR + name
    if EAL.does_asset_exist(dst):
        log("%s уже есть — пропуск" % dst)
        return None, dst
    if not EAL.duplicate_asset(SRC, dst):
        raise RuntimeError("не продублирован %s → %s" % (SRC, dst))
    bp = EAL.load_asset(dst)
    return bp, dst


def save(bp, dst):
    """Компиляция и запись варианта, дамп дерева в журнал."""
    B.compile_and_save(bp)
    unreal.log("ASDA_WRITES_ASSET: %s" % dst)
    if not EAL.save_loaded_asset(bp, False):
        raise RuntimeError("не сохранён %s" % dst)
    log("записан %s" % dst)
    unreal.log(B.dump_widget_tree(bp))


def v1():
    """V1: прежние окно (270×170) и кнопки 78×28; ряд поднят на 6 px над нижним рядом рамки (печать свободна),
    вопрос — по центру между шапкой и кнопками."""
    bp, dst = make_variant("WBP_LANG_SELECT_V1")
    if bp is None:
        return
    st = X.Step(bp, False)
    o0, o6, o2 = X.offsets(st.get("MESSAGE2_WIN_0_0")), X.offsets(st.get("MESSAGE2_WIN_0_6")), X.offsets(st.get("MESSAGE2_WIN_0_2"))
    left, top = o0.left, o0.top
    w = o2.left + o2.right - left
    body_top = top + o0.bottom                      # низ полосы-градиента шапки
    by = o6.top - BTN_H - 6.0                       # ряд кнопок: 6 px над нижним рядом рамки
    gap = 9.0
    bw = (w - 4 * gap) / 3.0
    for n in range(3):
        button(st, n, left + gap + n * (bw + gap), by, bw)
    text_box(st, left + 20.0, (body_top + by) / 2.0 - 8.0, w - 40.0, 20.0)
    save(bp, dst)


def v2():
    """V2: та же чёрная рамка MESSAGE2, окно 320×130 (симметрично от центра); в шапке — оливковая табличка заголовка
    с двумя винтами окна выбора сервера (n_02 + ne_wheeltaget2), на ней вопрос стилем заголовка сервера;
    кнопки 92×28 рядом по центру тела, нижний ряд рамки (печать) свободен."""
    bp, dst = make_variant("WBP_LANG_SELECT_V2")
    if bp is None:
        return
    srv = nodes_of(SRV_WIN)
    st = X.Step(bp, False)
    W, H = 320.0, 130.0
    L, T = CX - W / 2.0, CY - H / 2.0
    frame9(st, L, T, W, H)
    # Табличка: торцы 5×29 (не тянутся), середина — по ширине; винты 21×22 у торцов (как SERVER_Title*/SERVER_Bolt*).
    pl, pt, pw, ph = L + 14.0, T + 6.0, W - 28.0, 29.0
    i = index_after(bp, "MESSAGE2_WIN_0_8")
    clone_from(st, bp, srv["SERVER_TitleL"], "LANG_TitleL", pl, pt, 5.0, ph, i)
    clone_from(st, bp, srv["SERVER_TitleC"], "LANG_TitleC", pl + 5.0, pt, pw - 10.0, ph, i + 1)
    clone_from(st, bp, srv["SERVER_TitleR"], "LANG_TitleR", pl + pw - 5.0, pt, 5.0, ph, i + 2)
    clone_from(st, bp, srv["SERVER_BoltL"], "LANG_BoltL", pl + 9.0, pt + 3.5, 21.0, 22.0, i + 3)
    clone_from(st, bp, srv["SERVER_BoltR"], "LANG_BoltR", pl + pw - 30.0, pt + 3.5, 21.0, 22.0, i + 4)
    t = st.get("MESSAGE2_TXT00")
    X.copy_props(t, srv["SERVER_TitleText"], ("font", "color_and_opacity", "shadow_offset", "shadow_color_and_opacity"))
    t.set_editor_property("auto_wrap_text", False)
    text_box(st, pl + 34.0, pt + 8.0, pw - 68.0, 18.0)
    body_top, body_bot = pt + ph, T + H - 26.0       # от низа таблички до верха нижнего ряда рамки
    by = (body_top + body_bot) / 2.0 - BTN_H / 2.0
    bw = 92.0
    gap = (W - 3 * bw) / 4.0
    for n in range(3):
        button(st, n, L + gap + n * (bw + gap), by, bw)
    save(bp, dst)


def v3():
    """V3: рамка окна настроек (n_01, углы 20) 280×172 и его бурая полоса заголовка (торец 48 + середина + торец 86,
    высота 37) с зелёным значком; вопрос — подписью заголовка; кнопки 180×28 столбцом по центру."""
    bp, dst = make_variant("WBP_LANG_SELECT_V3")
    if bp is None:
        return
    st = X.Step(bp, False)
    LW.v3_clone(st, bp, nodes_of(SYS_WIN))
    LW.v3_layout(st)
    save(bp, dst)


def main():
    """Создать недостающие варианты; рабочий ассет не меняется (проверка даты файла)."""
    src_file = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()),
                            "AsdaAssets", "UI", "CommonWin", "WBP_LANG_SELECT.uasset")
    before = os.path.getmtime(src_file)
    v1()
    v2()
    v3()
    if os.path.getmtime(src_file) != before:
        raise RuntimeError("рабочий WBP_LANG_SELECT изменился — так быть не должно")
    log("ВАРИАНТЫ ОКНА ЯЗЫКА: ГОТОВО")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        unreal.log_error("[варианты окна языка] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
