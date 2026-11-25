# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _dump_gacha_bars.py — дамп веток полос слотов 68010+10i окна премиум-гачи (WBP_Premium_PickBAR_Win).
#   ЗАЧЕМ: доказать, что у полос 3…5 всё поддерево помечено `Collapsed` импортёром
#   (в `gamemain_jui.ini` у 68030/68040/68050 стоит `Visible = "0"`, а `eff_hidden` в
#   `asda_jui_generic_umg.py` протаскивается рекурсией на union-группу и всех детей).
#   Дерево печатает билдер порта (`UAsdaUmgBuilder::DumpWidgetTree`): `UWidgetBlueprint.widget_tree`
#   из Python недоступно. Читает ассет С ДИСКА.
import re

import unreal

WBP = '/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Premium_PickBAR_Win'


def log(m):
    unreal.log('[полосы] ' + str(m))


def main():
    bp = unreal.load_object(None, WBP + '.' + WBP.rsplit('/', 1)[1])
    if bp is None:
        unreal.log_error('[полосы] не загрузился %s' % WBP)
        return
    dump = unreal.AsdaUmgBuilder.dump_widget_tree(bp)
    lines = dump.split('\n')

    # ── 1. Родительские цепочки: собрать поддерево каждой группы полосы ──
    parent = {}
    info = {}
    for ln in lines:
        m = re.match(r'\s+(\S+)\s+(\S+)\s+род=(\S+)\s+вид=(\S+)\s', ln)
        if not m:
            continue
        nm, cls, par, vis = m.groups()
        parent[nm] = par
        info[nm] = (cls, vis, ln.strip())

    def bar_of(nm):
        seen = 0
        cur = nm
        while cur in parent and seen < 64:
            if re.match(r'Premium_PickBAR_\d_grp$', cur):
                return cur
            cur = parent[cur]
            seen += 1
        return None

    for i in range(1, 6):
        grp = 'Premium_PickBAR_%d_grp' % i
        if grp not in info:
            unreal.log_error('[полосы] НЕТ группы %s' % grp)
            continue
        kids = [n for n in info if bar_of(n) == grp]
        hidden = [n for n in kids if info[n][1] in ('Collapsed', 'Hidden')]
        log('===== %s : вид=%s ; узлов в поддереве %d ; спрятано %d ====='
            % (grp, info[grp][1], len(kids), len(hidden)))
        for n in sorted(kids):
            log('  ' + info[n][2])

    # ── 2. Планы цифр: у каждой полосы должен быть СВОЙ UV-фрагмент атласа ──
    log('--- планы цифр GOLD_roullete_FONT3_* (номер автомата = кусок атласа) ---')
    for n in sorted(info):
        if n.startswith('GOLD_roullete_FONT3'):
            log('  бар=%s %s' % (bar_of(n), info[n][2]))


if __name__ == '__main__' or globals().get('__spec__') is None:
    main()
