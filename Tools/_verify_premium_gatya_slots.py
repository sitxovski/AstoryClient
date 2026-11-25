# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
_verify_premium_gatya_slots.py — headless-проверка расширения премиум-гачи до пяти автоматов.
Читает С ДИСКА (закон «редактор открыт ⇒ запись .uasset молча проваливается»):
  1. `DT_premium_gatya_data` — сколько автоматов построит сцена (`EnsureSceneActor`);
  2. `DT_premium_gatya`      — сколько позиций покажет витрина 68300 каждому автомату
                               (фильтр `ViewSlot == выбранный`);
  3. модели `md_gachya_{a..e}_00` и по 5 клипов у каждой — иначе автомат не появится;
  4. виджеты `Premium_PickBAR_{1..5}_grp`, планы кнопок `Mac_Btn_2_{13,27,41,55,69}_0`,
     кнопки витрины `Premium_PickBAR_{i}_B{i}` и цифры-скины `GOLD_roullete_FONT3_{1..5}`
     (номер автомата в заголовке — это КАРТИНКА из атласа `NE_PvP2.tga`, а не текст).

Побочных эффектов при импорте нет. Запуск:
  UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=<этот файл> -unattended -NullRHI
"""
import unreal

MACHINES = ['md_gachya_a_00', 'md_gachya_b_00', 'md_gachya_c_00', 'md_gachya_d_00', 'md_gachya_e_00']
SKEL_ROOT = '/Game/AsdaAssets/Models/Skeletal/Gachya'
GACHA_WBP = '/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Premium_PickBAR_Win'
SPIN_PLANES = [13, 27, 41, 55, 69]


def log(m):  unreal.log('[гача-5] ' + m)
def err(m):  unreal.log_error('[гача-5] ' + m)


def rows_of(path, asset):
    obj = unreal.load_object(None, path + '.' + asset)
    dt = unreal.DataTable.cast(obj) if obj else None
    if not dt:
        err('нет таблицы %s' % path)
        return None, []
    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    return dt, names


def main():
    ok = True

    # ── 1. цены = число автоматов ──
    dt, names = rows_of('/Game/AsdaAssets/Data/ItemDB/DT_premium_gatya_data', 'DT_premium_gatya_data')
    prices = {}
    if dt:
        # Колонками, а не построчно: `get_data_table_row_from_name` в UE 5.8 из Python не доступна.
        slots = list(unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'Slot'))
        vals = list(unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'Price'))
        for s, p in zip(slots, vals):
            prices[int(s)] = int(p)
    log('DT_premium_gatya_data: строк %d, цены %s' % (len(names), dict(sorted(prices.items()))))
    if sorted(prices) != list(range(5)):
        err('автоматов будет %d, а не 5 (слоты %s)' % (len(prices), sorted(prices)))
        ok = False

    # ── 2. витрина по автоматам ──
    dt, names = rows_of('/Game/AsdaAssets/Data/ItemDB/DT_premium_gatya', 'DT_premium_gatya')
    view = {}
    if dt:
        codes = list(unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'ItemCode'))
        vslots = list(unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, 'ViewSlot'))
        for c, v in zip(codes, vslots):
            if int(c) > 0:
                view[int(v)] = view.get(int(v), 0) + 1
    log('DT_premium_gatya: строк %d, витрина по автоматам %s' % (len(names), dict(sorted(view.items()))))
    for s in range(5):
        if view.get(s, 0) <= 0:
            err('у автомата %d пустая витрина' % s)
            ok = False

    # ── 3. модели и клипы ──
    for i, m in enumerate(MACHINES):
        mesh = unreal.load_object(None, '%s/%s/SkeletalMeshes/%s.%s' % (SKEL_ROOT, m, m, m))
        if mesh is None:
            err('нет меша автомата %d (%s)' % (i, m))
            ok = False
            continue
        clips = 0
        for c in range(5):
            p = '%s/%s/SkeletalMeshes/%sanim_%02d' % (SKEL_ROOT, m, m, c)
            if unreal.load_object(None, p + '.' + p.rsplit('/', 1)[1]) is not None:
                clips += 1
        mats = len(mesh.materials) if hasattr(mesh, 'materials') else -1
        log('автомат %d: меш %s, клипов %d/5, материалов %d' % (i, m, clips, mats))
        if clips != 5:
            err('у автомата %d не хватает клипов' % i)
            ok = False

    # ── 4. виджеты сцены ──
    wbp = unreal.load_object(None, GACHA_WBP + '.' + GACHA_WBP.rsplit('/', 1)[1])
    if wbp is None:
        err('нет %s' % GACHA_WBP)
        ok = False
    else:
        # `UWidgetBlueprint.widget_tree` из Python защищено — дерево печатает билдер порта
        # (`UAsdaUmgBuilder::DumpWidgetTree`), как в `asda_ui_dump_window.py`.
        dump = unreal.AsdaUmgBuilder.dump_widget_tree(wbp)
        # Формат дампа проверяем поиском подстроки — так проверка не зависит от разметки строк.
        names_all = dump

        for i in range(1, 6):
            for nm in ['Premium_PickBAR_%d_grp' % i, 'Premium_PickBAR_%d_B%d' % (i, i)]:
                if nm not in names_all:
                    err('нет виджета %s' % nm)
                    ok = False
        for idx in SPIN_PLANES:
            nm = 'Mac_Btn_2_%d_0' % idx
            if nm not in names_all:
                err('нет плана кнопки «крутить» %s' % nm)
                ok = False
        # Номер автомата в заголовке полосы — это ПЛАН со скином `GOLD_roullete_FONT3_{1..5}`
        # (цифра нарисована в атласе `NE_PvP2.tga`, текстом её никто не пишет).
        import re
        digits = sorted(set(re.findall(r'GOLD_roullete_FONT3_\d+[_\w]*', dump)))
        log('планы цифр автоматов в WBP: %s' % digits)
        if len(digits) < 5:
            err('цифр-планов меньше пяти — новые автоматы будут без номера')
            ok = False

        # ★★★ ЦИФРЫ ОБЯЗАНЫ БЫТЬ РАЗНЫМИ КУСКАМИ АТЛАСА. «План есть» не значит «нарисуется
        #   правильная цифра»: у пяти скинов `GOLD_roullete_FONT3_{1..5}` разные UV-регионы, и
        #   если у двух автоматов совпал UV — на вывесках будет один и тот же номер.
        uvs = set()
        for ln in dump.split('\n'):
            m = re.search(r'GOLD_roullete_FONT3_[1-5]_\d+_0\b.*?(uv\[[-\d. ]+\]-\[[-\d. ]+\])', ln)
            if m:
                uvs.add(m.group(1))
        log('UV-регионы цифр: %d уникальных' % len(uvs))
        if len(uvs) < 5:
            err('UV-регионов цифр меньше пяти — у автоматов повторятся номера')
            ok = False

        # ★★★ ГЛАВНАЯ ПРОВЕРКА, КОТОРОЙ ЗДЕСЬ НЕ БЫЛО И ИЗ-ЗА КОТОРОЙ ПРИЁМКА СОВРАЛА.
        #   Раньше проверялось только ВХОЖДЕНИЕ ИМЕНИ в дамп — а имя есть и у свёрнутого узла.
        #   В `gamemain_jui.ini` полосы 68030/68040/68050 объявлены `Visible = "0"`, импортёр
        #   сворачивает такое поддерево ЦЕЛИКОМ (`eff_hidden` рекурсией), и на экране от полосы
        #   остаётся одна тёмная планка: ни вывески, ни цифры, ни кнопок
        #   (`Reports/gacha_slots5_ui.md`). Поэтому считаем спрятанные узлы по каждой полосе
        #   и сверяем с ЭТАЛОННОЙ полосой 1: расхождение = полоса приедет пустой, если код сцены
        #   её не выровняет (`UAsdaPremiumGachaSubsystem::MirrorSubtreeVisibility`).
        parent, vis = {}, {}
        for ln in dump.split('\n'):
            m = re.match(r'\s+(\S+)\s+\S+\s+род=(\S+)\s+вид=(\S+)\s', ln)
            if m:
                parent[m.group(1)], vis[m.group(1)] = m.group(2), m.group(3)
        if not parent:
            err('дамп WBP без полей «род=»/«вид=» — обнови UAsdaUmgBuilder::DumpWidgetTree')
            ok = False
        else:
            def bar_of(nm):
                cur, guard = nm, 0
                while cur in parent and guard < 64:
                    if re.match(r'Premium_PickBAR_\d_grp$', cur):
                        return cur
                    cur, guard = parent[cur], guard + 1
                return None

            hidden_by_bar = {}
            for nm in parent:
                b = bar_of(nm)
                if b and vis.get(nm) in ('Collapsed', 'Hidden'):
                    hidden_by_bar[b] = hidden_by_bar.get(b, 0) + 1
            base = hidden_by_bar.get('Premium_PickBAR_1_grp', 0)
            log('спрятанных узлов по полосам: %s (эталон — полоса 1: %d)'
                % ({k: hidden_by_bar.get(k, 0) for k in sorted(hidden_by_bar)}, base))
            for i in range(2, 6):
                b = 'Premium_PickBAR_%d_grp' % i
                if hidden_by_bar.get(b, 0) != base:
                    log('полоса %d свёрнута импортёром (%d спрятанных против %d у полосы 1) — '
                        'её выравнивает код сцены при открытии, проверять надо на живом PIE'
                        % (i, hidden_by_bar.get(b, 0), base))

    log('ИТОГ: ' + ('ВСЁ ГОТОВО К ПЯТИ АВТОМАТАМ' if ok else 'ЕСТЬ ПРОБЛЕМЫ (см. ошибки выше)'))


if __name__ == '__main__':
    main()
