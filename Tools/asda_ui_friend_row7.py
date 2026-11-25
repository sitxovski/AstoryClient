# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_friend_row7.py — ★ [ИСПРАВЛЕНИЕ] 7-я строка списка приятелей в окне сообщества `WBP_COMMUNITY_WIN` (08.11.2026,
#   пачка 17, баг 7; решение владельца «friend-list — make 7»).
#
# ЗАЧЕМ. В разметке оригинала (`gamemain_jui.ini:198699`) у вкладки приятелей СЕМЬ строк, но седьмая `CMTY_FRND_LIST7` мёртвая:
#   IDNum = −1, Visible = 0, а код оригинала привязывает и листает только шесть (`social.c:5179-5194`, `uiTradeListRefreshPage`
#   `ui_core.c:44839`: страница = размер/6). Под шестой строкой остаётся пустое место высотой в строку. Импортёр перенёс 7-ю
#   строку целиком (24 узла, `CMTY_FRND_LIST7_grp` … `CMTY_FRND_STAT7`), но группу оставил свёрнутой (Visible = 0).
#   Структурный дамп 08.11 (`Reports/batch17/community/dump_full.txt`): поддерево 7-й строки совпадает с 6-й узел в узел
#   (классы, родители, слоты, браши, цвета, шрифты) — отличаются только смещение группы (T 371 = 314 + 57, шаг строк) и
#   тексты-заглушки дизайнера (код их чистит при каждой перерисовке).
#
# ЧТО ДЕЛАЕТ. Одно значение: видимость группы `CMTY_FRND_LIST7_grp` Collapsed → SelfHitTestInvisible (как у строк 1…6).
#   Ловлю мыши строке даёт код (`EnsureRowHit`), как у остальных строк. Логика 7 строк — `UAsdaCommunityFriendSubsystem`,
#   ключ `asda.Community.FriendRows7` (0 = 6 строк оригинала; седьмую группу тогда гасит код).
#
# ИДЕМПОТЕНТНОСТЬ. Допустимы только исходное (Collapsed) и целевое (SelfHitTestInvisible) значения. Иное — чужая правка:
#   ничего не пишем, шаг падает («осмотреть»). Снимок дерева до/после — изменился ровно один узел.
#
# Запуск (редактор и игра ЗАКРЫТЫ):
#   python Tools/run_headless_safe.py Tools/asda_ui_friend_row7.py --marker "FRIEND_ROW7_DONE" --require "FRIEND_ROW7_DONE" ^
#       --expect-asset /Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN
#   Сухой прогон: ASDA_FRIEND_ROW7_DRYRUN=1 (или общий ASDA_POSTSTEPS_DRYRUN=1).
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # Tools: запуск скриптом, без раннера пост-импорта
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта, снимки дерева

WBP = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN"   # окно сообщества 6400 (`kWindowWbp` в коде)
NODE = "CMTY_FRND_LIST7_grp"                                      # группа 7-й строки приятелей
PEER = "CMTY_FRND_LIST6_grp"                                      # образец — 6-я строка (её видимость = целевая)
VIS_SRC = unreal.SlateVisibility.COLLAPSED                        # значение импортёра (Visible = 0 в .JUI)
VIS_DST = unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE          # как у строк 1…6
STEP = "asda_ui_friend_row7"                                      # имя шага
DRYRUN_ENV = "ASDA_FRIEND_ROW7_DRYRUN"                            # собственный ключ сухого прогона
MARK = "FRIEND_ROW7_DONE"                                         # маркер успеха для run_headless_safe


def log(m):
    """Строка журнала шага."""
    unreal.log_warning("[friend-row7] " + str(m))


def find_node(bp, name):
    """Узел окна по имени (обход всего дерева от корневого канваса)."""
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    found = []

    def cb(w):
        """Запомнить узел с нужным именем."""
        if w.get_name() == name:
            found.append(w)
    if root is not None:
        SD._walk_widgets(root, cb)
    return found[0] if len(found) == 1 else None


def verify():
    """Сторож пост-импорта по значениям: список проблем (пусто — 7-я строка видима, как 6-я)."""
    bp = unreal.EditorAssetLibrary.load_asset(WBP)
    if not bp:
        return ["%s не загружен" % WBP]
    node, peer = find_node(bp, NODE), find_node(bp, PEER)
    if node is None or peer is None:
        return ["нет узла %s или %s" % (NODE, PEER)]
    out = []
    if node.get_editor_property("visibility") != VIS_DST:
        out.append("%s: видимость %s (ждём %s)" % (NODE, node.get_editor_property("visibility"), VIS_DST))
    if peer.get_editor_property("visibility") != VIS_DST:
        out.append("%s: видимость %s (образец изменился — осмотреть)" % (PEER, peer.get_editor_property("visibility")))
    return out


def main():
    """Включить 7-ю строку; при неожиданном значении ничего не писать."""
    dry = SD.is_dry(DRYRUN_ENV)
    delta = SD.WidgetDelta(STEP, dry)
    bp = unreal.EditorAssetLibrary.load_asset(WBP)
    if not bp:
        raise RuntimeError("[friend-row7] не загружен %s" % WBP)
    delta.take(bp)
    node, peer = find_node(bp, NODE), find_node(bp, PEER)
    if node is None or peer is None:
        raise RuntimeError("[friend-row7] нет узла %s или %s — осмотреть" % (NODE, PEER))
    if peer.get_editor_property("visibility") != VIS_DST:
        raise RuntimeError("[friend-row7] образец %s: видимость %s, ждали %s — осмотреть"
                           % (PEER, peer.get_editor_property("visibility"), VIS_DST))
    cur = node.get_editor_property("visibility")
    if cur == VIS_DST:
        log("правка уже применена (%s), ассет не трогаю" % cur)
    elif cur != VIS_SRC:
        raise RuntimeError("[friend-row7] НЕОЖИДАННАЯ видимость %s у %s (ни %s, ни %s): чужая правка, ничего не пишу"
                           % (cur, NODE, VIS_SRC, VIS_DST))
    else:
        node.set_editor_property("visibility", VIS_DST)
        changed = delta.done(bp)
        if changed != 1:
            raise RuntimeError("[friend-row7] сверка: изменено узлов %d (ждём ровно 1) — НЕ сохраняю" % changed)
        if not dry:
            unreal.AsdaUmgBuilder.compile_and_save(bp)   # компиляция блюпринта окна (как у шага caramake_topwin_z)
        if not SD.save(bp, dry, STEP, False):
            raise RuntimeError("[friend-row7] НЕ сохранён %s" % WBP)
        log("%s: видимость %s → %s%s" % (NODE, VIS_SRC, VIS_DST, " [СУХОЙ]" if dry else ""))
    delta.finish()
    log(MARK)


# ★ Закон tools-no-import-side-effects: при импорте main() не запускается, раннер пост-импорта зовёт его сам.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
