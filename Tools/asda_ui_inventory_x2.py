# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_inventory_x2.py — ★★★ [ПОРТ] ИНВЕНТАРЬ ×2: СЕТКА ОКНА СУМКИ 6×5 → 12×5 (60 ЯЧЕЕК) В АССЕТЕ (29.10.2026, решение
#   владельца; план `Reports/inventory_x2/PLAN.md` §2 п.3, §3.4; разбор `_research/client.md` §1). Правка АССЕТА
#   `WBP_INVEN_WIN` (JUI `INVEN_WIN` 400), кода не касается (закон no-runtime-widget-fixes).
#
# ═══ ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════════════════════════════
#   Сумка (контейнер 1) 30 → 60 ячеек, расходники (контейнер 2) — 60 без прокрутки. Обе вкладки показывают 12 столбцов ×
#   5 строк, нумерация построчно: ряд 1 = 0…11, ряд 2 = 12…23 … ряд 5 = 48…59. Код (`UAsdaInventorySubsystem::SetupControls`)
#   держится за ячейки ТОЛЬКО по именам: TextBlock `INVEN_SET{C}` (число в стопке) и план-UImage (не UAsdaAnimatedPlane)
#   `INVEN_SET{C}_{idx}_0` (разбор `AsdaUi::ParsePlaneName`), C < kGridCells; позицию берёт из ассета. Значит нумерацию
#   12×5 задаёт РАСКЛАДКА: `INVEN_SET{r·12+c}` в абсолюте (132+33c, 320+33r) базы 1024×768 (★ 30.10: +6 — поля внутри подложки равны).
#
# ═══ ЧТО ДЕЛАЕТ ШАГ (закрытая таблица «было → стало» + клоны; числа «было» сняты со свежего дампа 30.10.2026) ═════════
#   Окно шире ЦЕЛИКОМ и СИММЕТРИЧНО на +198 px (6·33), высота прежняя; ячейки НЕ растягиваются, а добавляются
#   (закон widen-windows-whole-keep-symmetry):
#   • рамка 9 кусков: середины `INVEN_WIN_0_1/_4/_7` растут на 198, правая колонка `_2/_5/_8` — +198 (окно 116…542);
#   • шапка: C-бар `Inven_Base_190_0` растёт, `Inven_RBase_grp` (с крестиком) — +198; шестерёнка — +198;
#     ручка `INVEN_WIN_MOVE` (+`_195_DRAG`) растёт до крестика;
#   • дерево-подложка `Inven_WoodBase_grp` и 4 ряда: середины растут, правые части (болты, кнопка раскрытия) — +198;
#     скрытый `Wood_RBase2_grp` — тоже (целостность при раскрытой панели);
#   • манекен `EQUIP_TRAYICON_grp`, вкладки (вариант А: ширина 108 прежняя, пара по центру), их подписи (шаг
#     `asda_ui_tabbar_captions`) — +99 (центр окна 230 → 329); нижний блок кнопок — значок питомца + сетка 2×2 по центру
#     свободной области (★ 30.10, см. таблицу);
#   • подложка сетки `INVEN_ITEMTRAY1_grp` (Win_Tab) — растёт серединой, деньги (`INVEN_MONEYTXT`/`GOLDTXT`/`CASHTXT`) — +198;
#     группы сетки `INVEN_ITEMTRAY2_grp`/`INVEN_SET_Base_grp`/`INVEN_Icon_Base_grp` — растут; ячейковые группы, курсор 409 и эффекты 498/10009 ещё +6 по X (★ 30.10: поля внутри подложки 5.9/5.8);
#   • правая панель `INVEN_RIGHISIDEBACK_grp`, пристыкованные `Inven_Fishrod_Win_grp`/`Inven_Strength_Win_grp`/
#     `Inven_auto_array_box_grp`/`Inven_Gfont1/2` — +198 (раскрытая панель кончается на 958 < 1024);
#   • 24 существующие ячейки 6…29 (подложка `Inven_BaseSet{n}_*`, план `INVEN_SET{n}_*_0`, текст `INVEN_SET{n}`)
#     переставляются из (n%6, n/6) в (n%12, n/12); ячейки 0…5 уже на месте;
#   • 30 новых ячеек 30…59 — клоны образца n = 0 (`asda_ui_fishbook_clones.make_like/place_like`: кисть, стиль, шрифт,
#     цвет, Z, видимость): подложки `Inven_BaseSet{n}_{570+n}_0` (idx 600…629) в `INVEN_SET_Base_grp`, планы
#     `INVEN_SET{n}_{600+n}_0` (idx 630…659) и тексты `INVEN_SET{n}` в `INVEN_Icon_Base_grp` — сразу за ячейкой 29
#     (раньше `INVEN_Strength_Icon_Ani_259_0`, как у импортёра), 90 узлов. idx вне занятых в окне (максимум импортёра 582).
#   НЕ трогаются (кода нет / туториал): `Inven_Strength_BOX2_grp`, `Inven_Strength_BOX3_grp`, `TUTO_MSG_BOX1_grp`;
#   корзина `INVEN_TRASHBUT` и автосортировка `Inven_auto_array` — слева, на месте. Узла `INVEN_WHEELWIN` в ассете НЕТ
#   (полосу прокрутки строит код), растягивать нечего.
#
# ═══ БЕЗОПАСНОСТЬ (закон owner-asset-edits-never-lose) ═══════════════════════════════════════════════════════════════
#   • Узел таблицы трогается, только если его слот В ТОЧНОСТИ = «было» (точечный анкор, alignment 0, без авторазмера,
#     прежний родитель). Уже «стало» — готово (идемпотентно). Иное — «осмотреть»: окно НЕ пишется целиком, прогон падает.
#   • Клон есть и совпадает с образцом (класс, родитель, слот, вид) — готово; отличается — «осмотреть». Чужой узел с
#     именем клона — СТОП. Образцы n = 0 обязаны стоять на импортном месте, иначе СТОП (клоны считаются от них).
#   • Снимок дерева до/после в памяти, ДО сохранения: узлов не потеряно; новые — ровно созданные; у правленых изменились
#     только смещения слота; у прочих — ничего (полная строка `DumpWidgetTree` + доп. свойства `asda_ui_step_dryrun`),
#     порядок прежних детей сохранён; клоны = образец с точностью до смещения; остаток плана в памяти = 0; геометрия
#     60 планов по цепочке групп = (132+33c, 320+33r).
#   • Ручной запуск пишет только после бэкапа (`asda_headless_tool.backup_file`, набор `<дата>_inventory_x2`;
#     `ASDA_INVX2_BACKUP_DIR` — готовая папка набора).
#   • Сторож пост-импорта по значениям `verify()` (`asda_ui_post_import.VALUE_GUARDS`); куски рамки, расширенные этим
#     шагом, шаг `asda_ui_frame_seam_overlap` принимает через `frame_seam_done()` (иначе повторный прогон цепочки без
#     переимпорта счёл бы их «осмотреть»); стиль клонов-текстов — `asda_ui_text_style926.CLONE_TEMPLATES`.
#
# Запуск (редактор и игра ЗАКРЫТЫ: соседний `-game` молча не даёт сохранить ассет):
#   python Tools\run_headless_safe.py Tools\asda_ui_inventory_x2.py --marker "ЗАВЕРШЕНО: инвентарь x2" ^
#       --expect-asset /Game/AsdaAssets/UI/Player_UI/Inventory/WBP_INVEN_WIN
#   Сухой прогон: ASDA_INVX2_DRYRUN=1 (или общий ASDA_POSTSTEPS_DRYRUN=1 / --dry-run) — печатает план, ничего не пишет.
import os
import re
import sys
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))   # папка Tools
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_ui_step_dryrun as DR        # noqa: E402  общий сухой режим шагов пост-импорта, строки дерева для сверки
import asda_headless_tool as HT         # noqa: E402  единая резервная копия `backup_file`
import asda_ui_fishbook_clones as FC    # noqa: E402  `make_like` / `place_like` — одна реализация клонов на все шаги

EAL = unreal.EditorAssetLibrary         # загрузка ассета
B = unreal.AsdaUmgBuilder               # корневой канвас, перестановка детей, компиляция
ASSET = "/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_INVEN_WIN"   # окно сумки INVEN_WIN 400 (`kInvenWbp` в коде)
CONTENT_DIR = os.path.normpath(os.path.join(HERE, "..", "Content"))   # Content проекта (файл ассета для бэкапа)
DRYRUN_ENV = "ASDA_INVX2_DRYRUN"        # собственный ключ сухого прогона шага (таблица DRY_ENV раннера пост-импорта)
BACKUP_SET = "inventory_x2"             # имя набора бэкапа `@@ASTORY_DEV_ROOT@@\_backup\<дата>_inventory_x2`
BACKUP_DIR_ENV = "ASDA_INVX2_BACKUP_DIR"   # готовая папка набора бэкапа (переопределение)
TAG = "asda_ui_inventory_x2"            # метка строк сухого режима / итога
MARKER = "ЗАВЕРШЕНО: инвентарь x2"      # маркер успеха для run_headless_safe
EPS = 0.01                              # допуск сравнения чисел слота

DX = 198.0                              # прирост ширины окна: 6 столбцов × 33
DC = 99.0                               # сдвиг «центровых» блоков: половина прироста
STEP = 33.0                             # шаг ячейки (32 + 1)
COLS_OLD = 6                            # столбцов было (сетка 6×5)
COLS_NEW = 12                           # столбцов стало (сетка 12×5)
CELLS_OLD = 30                          # ячеек в ассете импортёра
CELLS_NEW = 60                          # ячеек после шага
GRID_SHIFT_X = 6.0                      # ★ 30.10 (ревизия координатора, симметрия): сетка +6 внутри подложки — поля 5.9/5.8
GRID_ABS = (126.0 + GRID_SHIFT_X, 320.0)   # абсолютный левый-верх ячейки 0 (база 1024×768): 132, 320
BASE_IDX0 = 600                         # idx подложки клона n = 30 (`Inven_BaseSet{n}_{570+n}_0`, 600…629)
PLANE_IDX0 = 630                        # idx плана клона n = 30 (`INVEN_SET{n}_{600+n}_0`, 630…659)
BASE_GRP = "INVEN_SET_Base_grp"         # родитель подложек ячеек
ICON_GRP = "INVEN_Icon_Base_grp"        # родитель планов и текстов ячеек
TPL_BASE_OFF = (5.0, 5.0, 32.0, 32.0)   # импортное место подложки n = 0 в `INVEN_SET_Base_grp`
TPL_PLANE_OFF = (0.0, 0.0, 32.0, 32.0)  # импортное место плана n = 0 в `INVEN_Icon_Base_grp`
TPL_TEXT_OFF = (0.0, 18.0, 32.0, 32.0)  # импортное место текста n = 0 в `INVEN_Icon_Base_grp`
WINDOW_SPAN = (116.0, 542.0)            # габарит окна по X после шага (свёрнутая панель)
RIGHT_PANEL = "INVEN_RIGHISIDEBACK_grp" # правая панель (габарит раскрытой — по видимым картинкам)
SCREEN_W = 1024.0                       # ширина базы экрана

# ═══ ЗАКРЫТАЯ ТАБЛИЦА «было → стало» (без ячеек; L, T, ширина, высота слота; родитель — для сторожа) ═══════════════════
#   (имя узла, родитель, вид правки "move"/"grow", было, стало). Числа «было» — свежий дамп 30.10.2026
#   (`Reports/inventory_x2/asset/dump_before.json`), уже с правкой шва рамок `asda_ui_frame_seam_overlap`.
TABLE = (
    # ── рамка окна 9 кусков (середины после шва: 0_1 136/188, 0_4 и 0_7 135/190) ──
    ("INVEN_WIN_0_1", "RootCanvas", "grow", (136.0, 56.0, 188.0, 20.0), (136.0, 56.0, 386.0, 20.0)),
    ("INVEN_WIN_0_2", "RootCanvas", "move", (324.0, 56.0, 20.0, 20.0), (522.0, 56.0, 20.0, 20.0)),
    ("INVEN_WIN_0_4", "RootCanvas", "grow", (135.0, 75.0, 190.0, 552.0), (135.0, 75.0, 388.0, 552.0)),
    ("INVEN_WIN_0_5", "RootCanvas", "move", (324.0, 75.0, 20.0, 552.0), (522.0, 75.0, 20.0, 552.0)),
    ("INVEN_WIN_0_7", "RootCanvas", "grow", (135.0, 626.0, 190.0, 20.0), (135.0, 626.0, 388.0, 20.0)),
    ("INVEN_WIN_0_8", "RootCanvas", "move", (324.0, 626.0, 20.0, 20.0), (522.0, 626.0, 20.0, 20.0)),
    # ── шестерёнка (правый верх) ──
    ("Inven_Gear_1_0", "RootCanvas", "move", (304.0, 46.0, 50.0, 50.0), (502.0, 46.0, 50.0, 50.0)),
    ("Inven_AniGear_2_0", "RootCanvas", "move", (304.0, 46.0, 50.0, 50.0), (502.0, 46.0, 50.0, 50.0)),
    # ── дерево-подложка манекена: середины растут, правые части (болты, кнопка раскрытия 408) едут ──
    ("Inven_WoodBase_grp", "RootCanvas", "grow", (116.0, 56.0, 225.0, 239.0), (116.0, 56.0, 423.0, 239.0)),
    ("Inven_WoodCBase1_grp", "Inven_WoodBase_grp", "grow", (5.0, 39.0, 219.0, 60.0), (5.0, 39.0, 417.0, 60.0)),
    ("Inven_WoodCBase1_4_0", "Inven_WoodCBase1_grp", "grow", (41.0, 0.0, 137.0, 60.0), (41.0, 0.0, 335.0, 60.0)),
    ("Inven_WoodRBase1_grp", "Inven_WoodCBase1_grp", "move", (178.0, 0.0, 41.0, 60.0), (376.0, 0.0, 41.0, 60.0)),
    ("Inven_WoodCBase2_grp", "Inven_WoodBase_grp", "grow", (5.0, 99.0, 219.0, 40.0), (5.0, 99.0, 417.0, 40.0)),
    ("Inven_WoodCBase2_9_0", "Inven_WoodCBase2_grp", "grow", (41.0, 0.0, 137.0, 40.0), (41.0, 0.0, 335.0, 40.0)),
    ("Inven_WoodRBase2_11_0", "Inven_WoodCBase2_grp", "move", (178.0, 0.0, 41.0, 40.0), (376.0, 0.0, 41.0, 40.0)),
    ("Inven_WoodCBase3_grp", "Inven_WoodBase_grp", "grow", (5.0, 138.0, 220.0, 41.0), (5.0, 138.0, 418.0, 41.0)),
    ("Inven_WoodCBase3_12_0", "Inven_WoodCBase3_grp", "grow", (41.0, 1.0, 98.0, 40.0), (41.0, 1.0, 296.0, 40.0)),
    ("Inven_WoodRBase3_grp", "Inven_WoodCBase3_grp", "move", (139.0, 0.0, 81.0, 41.0), (337.0, 0.0, 81.0, 41.0)),
    ("Wood_RBase2_grp", "Inven_WoodCBase3_grp", "grow", (80.0, 0.0, 139.0, 40.0), (80.0, 0.0, 337.0, 40.0)),
    ("Wood_RBase2_18_0", "Wood_RBase2_grp", "move", (98.0, 0.0, 41.0, 40.0), (296.0, 0.0, 41.0, 40.0)),
    ("Wood_CBase2_19_0", "Wood_RBase2_grp", "grow", (0.0, 0.0, 98.0, 40.0), (0.0, 0.0, 296.0, 40.0)),
    ("Inven_WoodCBase4_grp", "Inven_WoodBase_grp", "grow", (5.0, 179.0, 219.0, 60.0), (5.0, 179.0, 417.0, 60.0)),
    ("Inven_WoodCBase4_20_0", "Inven_WoodCBase4_grp", "grow", (41.0, 0.0, 137.0, 60.0), (41.0, 0.0, 335.0, 60.0)),
    ("Inven_WoodRBase4_grp", "Inven_WoodCBase4_grp", "move", (178.0, 0.0, 41.0, 60.0), (376.0, 0.0, 41.0, 60.0)),
    # ── манекен по центру (+99) ──
    ("EQUIP_TRAYICON_grp", "RootCanvas", "move", (121.0, 95.0, 209.0, 193.0), (220.0, 95.0, 209.0, 193.0)),
    # ── подложка сетки Win_Tab (середины после шва 10/198, правая колонка 207) и деньги у правого края ──
    ("INVEN_ITEMTRAY1_grp", "RootCanvas", "grow", (121.0, 315.0, 218.0, 194.0), (121.0, 315.0, 416.0, 194.0)),
    ("INVEN_ITEMTRAY1_45_1", "INVEN_ITEMTRAY1_grp", "grow", (10.0, 0.0, 198.0, 5.0), (10.0, 0.0, 396.0, 5.0)),
    ("INVEN_ITEMTRAY1_45_2", "INVEN_ITEMTRAY1_grp", "move", (207.0, 0.0, 11.0, 5.0), (405.0, 0.0, 11.0, 5.0)),
    ("INVEN_ITEMTRAY1_45_4", "INVEN_ITEMTRAY1_grp", "grow", (10.0, 4.0, 198.0, 180.0), (10.0, 4.0, 396.0, 180.0)),
    ("INVEN_ITEMTRAY1_45_5", "INVEN_ITEMTRAY1_grp", "move", (207.0, 4.0, 11.0, 180.0), (405.0, 4.0, 11.0, 180.0)),
    ("INVEN_ITEMTRAY1_45_7", "INVEN_ITEMTRAY1_grp", "grow", (10.0, 183.0, 198.0, 11.0), (10.0, 183.0, 396.0, 11.0)),
    ("INVEN_ITEMTRAY1_45_8", "INVEN_ITEMTRAY1_grp", "move", (207.0, 183.0, 11.0, 11.0), (405.0, 183.0, 11.0, 11.0)),
    ("INVEN_CASHTXT", "INVEN_ITEMTRAY1_grp", "move", (53.0, 174.5, 60.0, 15.0), (251.0, 174.5, 60.0, 15.0)),
    ("INVEN_MONEYTXT", "INVEN_ITEMTRAY1_grp", "move", (112.0, 174.5, 80.0, 15.0), (310.0, 174.5, 80.0, 15.0)),
    ("INVEN_GOLDTXT_48_0", "INVEN_ITEMTRAY1_grp", "move", (194.0, 176.0, 9.0, 9.0), (392.0, 176.0, 9.0, 9.0)),
    # ── вкладки (вариант А: ширина 108, пара по центру +99) и их подписи (шаг `asda_ui_tabbar_captions`) ──
    ("INVEN_CHOICEBUT_49_0", "RootCanvas", "move", (121.0, 297.0, 108.0, 22.0), (220.0, 297.0, 108.0, 22.0)),
    ("INVEN_CHOICEBUT_49_1", "RootCanvas", "move", (231.0, 297.0, 108.0, 22.0), (330.0, 297.0, 108.0, 22.0)),
    ("INVEN_CHOICEBUT_OPT0", "RootCanvas", "move", (121.0, 297.0, 108.0, 22.0), (220.0, 297.0, 108.0, 22.0)),
    ("INVEN_CHOICEBUT_OPT1", "RootCanvas", "move", (231.0, 297.0, 108.0, 22.0), (330.0, 297.0, 108.0, 22.0)),
    ("INVEN_CHOICEBUT_Text", "RootCanvas", "move", (121.0, 300.0, 108.0, 22.0), (220.0, 300.0, 108.0, 22.0)),
    ("INVEN_CHOICEBUT_Text2", "RootCanvas", "move", (231.0, 300.0, 108.0, 22.0), (330.0, 300.0, 108.0, 22.0)),
    # ── правая панель целиком (+198): кнопка свёртки встаёт на место кнопки раскрытия ──
    ("INVEN_RIGHISIDEBACK_grp", "RootCanvas", "move", (115.0, 56.0, 722.0, 585.0), (313.0, 56.0, 722.0, 585.0)),
    # ── шапка: правый край с крестиком (+198), C-бар растёт, ручка перетаскивания растёт до крестика ──
    ("Inven_RBase_grp", "RootCanvas", "move", (256.0, 58.0, 86.0, 37.0), (454.0, 58.0, 86.0, 37.0)),
    ("Inven_Base_190_0", "RootCanvas", "grow", (166.0, 58.0, 90.0, 37.0), (166.0, 58.0, 288.0, 37.0)),
    ("INVEN_WIN_MOVE", "RootCanvas", "grow", (116.0, 68.0, 202.0, 38.0), (116.0, 68.0, 400.0, 38.0)),
    ("INVEN_WIN_MOVE_195_DRAG", "RootCanvas", "grow", (116.0, 56.0, 202.0, 38.0), (116.0, 56.0, 400.0, 38.0)),
    # ── группы сетки растут (ячейки — отдельным списком ниже) ──
    ("INVEN_ITEMTRAY2_grp", "RootCanvas", "grow", (121.0, 315.0, 203.0, 194.0), (121.0, 315.0, 401.0, 194.0)),
    #   ★ 30.10 (ревизия координатора, симметрия по просьбе владельца): сетка ещё +6 по X внутри подложки. Замер по пикселям
    #   n_01 кусков 45_3/45_5: чёрное поле подложки 125.9…532.8, белая рамка 122.0…537.0; сетка 126…521 давала поля
    #   0.1/11.8 (внутр.) и 4.0/16.0 (наруж.) — бывшее место полосы прокрутки справа. +6 → 5.9/5.8 и 10.0/10.0.
    #   Корзина `INVEN_TRASHBUT`, «A» и деньги не едут: сдвигаются только группы ячеек и привязанные к ячейкам курсор/эффекты.
    ("INVEN_SET_Base_grp", "INVEN_ITEMTRAY2_grp", "grow", (0.0, 0.0, 202.0, 169.0), (6.0, 0.0, 400.0, 169.0)),
    ("INVEN_Icon_Base_grp", "INVEN_ITEMTRAY2_grp", "grow", (5.0, 5.0, 197.0, 164.0), (11.0, 5.0, 395.0, 164.0)),
    ("INVEN_ITEMTRAYSEL_260_0", "INVEN_ITEMTRAY2_grp", "move", (5.0, 5.0, 32.0, 32.0), (11.0, 5.0, 32.0, 32.0)),
    ("INVEN_HOLYEFFECT_261_0", "INVEN_ITEMTRAY2_grp", "move", (0.0, 0.0, 120.0, 120.0), (6.0, 0.0, 120.0, 120.0)),
    ("INVEN_SOULSTONEEFFECT_262_0", "INVEN_ITEMTRAY2_grp", "move", (96.0, 62.0, 48.0, 48.0), (102.0, 62.0, 48.0, 48.0)),
    # ── нижний блок кнопок (★ 30.10, просьба владельца «красиво расположить по новому свободному месту»): значок клетки
    #   питомца слева, справа сетка 2×2 — ряд 1: 449 «Улучшение» | 452 «Создание», ряд 2: 453 «Демонтаж» | 10082 «Элитный
    #   почтовый ящик». Без растяжения — только сдвиг групп (подписи, рамки BtnBase, тёмные маски — их дети). Зазор g = 8
    #   везде (значок↔колонка 1, колонка 1↔колонка 2, ряд↔ряд) — как прежний зазор значок↔кнопки (181 − 129 − 44).
    #   По X: видимые края — значок 0…44, колонка 1 — 146, колонка 2 — по левому краю, ширина по видимой рамке 10082
    #   (BtnBase_297: 0…182); итого 44+8+146+8+182 = 388, центр окна 329 (чёрное поле рамки 118…541) ⇒ 135…523.
    #   По Y: область между низом белой рамки подложки сетки (508.8, текстура n_01 куска 45_7) и чёрным полем над нижней
    #   рамкой окна (645.1, кусок INVEN_WIN_0_7: белое с px 19) — центр 577.0; блок 28+8+28 = 64 ⇒ ряды 545 и 581,
    #   значок 48 по центру двух рядов ⇒ 553.
    #   Скрытые 471/472/473 (44×48, тот же вид, что клетка питомца; в разметке рядом с ней с шагом 52/104/155; кода, который
    #   их показывает, нет ни в эталоне, ни в порту) — тем же смещением относительно значка, что в разметке.
    ("Inven_PetCageBtn_grp", "RootCanvas", "move", (129.0, 517.0, 44.0, 48.0), (135.0, 553.0, 44.0, 48.0)),
    ("Inven_ItemUpgrade_Btn_grp", "RootCanvas", "move", (181.0, 518.0, 146.0, 28.0), (187.0, 545.0, 146.0, 28.0)),
    ("Inven_Smelting_Btn_grp", "RootCanvas", "move", (181.0, 549.0, 146.0, 28.0), (341.0, 545.0, 146.0, 28.0)),
    ("Inven_Disassembly_Btn_grp", "RootCanvas", "move", (181.0, 580.0, 146.0, 28.0), (187.0, 581.0, 146.0, 28.0)),
    ("Inven_strength_Btn_grp", "RootCanvas", "move", (181.0, 517.0, 44.0, 48.0), (187.0, 553.0, 44.0, 48.0)),
    ("Inven_break_Btn_grp", "RootCanvas", "move", (233.0, 517.0, 44.0, 48.0), (239.0, 553.0, 44.0, 48.0)),
    ("Inven_move_Btn_grp", "RootCanvas", "move", (284.0, 517.0, 44.0, 48.0), (290.0, 553.0, 44.0, 48.0)),
    ("Inven_primium_item_btn_grp", "RootCanvas", "move", (145.0, 611.0, 184.0, 28.0), (341.0, 581.0, 184.0, 28.0)),
    # ── всё, что пристыковано справа к окну (+198) ──
    ("Inven_Gfont1_294_0", "RootCanvas", "move", (338.0, 291.0, 98.0, 40.0), (536.0, 291.0, 98.0, 40.0)),
    ("Inven_Gfont2_295_0", "RootCanvas", "move", (342.0, 329.0, 83.0, 41.0), (540.0, 329.0, 83.0, 41.0)),
    ("Inven_Fishrod_Win_grp", "RootCanvas", "move", (352.0, 296.0, 203.0, 191.0), (550.0, 296.0, 203.0, 191.0)),
    ("Inven_Strength_Win_grp", "RootCanvas", "move", (352.0, 296.0, 203.0, 191.0), (550.0, 296.0, 203.0, 191.0)),
    ("Inven_auto_array_box_grp", "RootCanvas", "move", (340.0, 435.0, 160.0, 76.0), (538.0, 435.0, 160.0, 76.0)),
)
# Допустимые «было» сверх таблицы: вид ПЕРВОЙ версии шага (30.10 00:45, сетка 12×5 без сдвига +6) — повторный прогон по такому
#   ассету доводит его до «стало», а не уходит в «осмотреть». Имя узла → список прежних слотов.
ALT_BEFORE = {
    "INVEN_SET_Base_grp": [(0.0, 0.0, 400.0, 169.0)],
    "INVEN_Icon_Base_grp": [(5.0, 5.0, 395.0, 164.0)],
    # Нижний блок кнопок в виде первой версии (весь блок столбиком +99) — до раскладки 2×2 (30.10).
    "Inven_PetCageBtn_grp": [(228.0, 517.0, 44.0, 48.0)],
    "Inven_ItemUpgrade_Btn_grp": [(280.0, 518.0, 146.0, 28.0)],
    "Inven_Smelting_Btn_grp": [(280.0, 549.0, 146.0, 28.0)],
    "Inven_Disassembly_Btn_grp": [(280.0, 580.0, 146.0, 28.0)],
    "Inven_strength_Btn_grp": [(280.0, 517.0, 44.0, 48.0)],
    "Inven_break_Btn_grp": [(332.0, 517.0, 44.0, 48.0)],
    "Inven_move_Btn_grp": [(383.0, 517.0, 44.0, 48.0)],
    "Inven_primium_item_btn_grp": [(244.0, 611.0, 184.0, 28.0)],
}
# Узлы, которые шаг сознательно НЕ трогает (кода нет / туториал / на месте) — для отчёта шага.
UNTOUCHED = ("Inven_Strength_BOX2_grp", "Inven_Strength_BOX3_grp", "TUTO_MSG_BOX1_grp", "INVEN_TRASHBUT",
             "Inven_auto_array", "Inven_auto_array_Text")
# Рамки, расширенные этим шагом поверх правки шва `asda_ui_frame_seam_overlap` (имя рамки в его плане).
FRAME_SEAM_FRAMES = ("INVEN_WIN_0", "INVEN_ITEMTRAY1_45")


def log(m):
    """Строка журнала шага."""
    unreal.log("[инвентарь x2] %s" % m)


def near(a, b):
    """Покомпонентное равенство четвёрок чисел с допуском EPS."""
    return a is not None and b is not None and len(a) == len(b) and all(abs(float(x) - float(y)) <= EPS
                                                                       for x, y in zip(a, b))


def slot_of(w):
    """Слот канваса узла (None — не канвасный)."""
    try:
        s = w.get_editor_property("slot")
    except Exception:                                            # noqa: BLE001
        return None
    return s if isinstance(s, unreal.CanvasPanelSlot) else None


def slot_state(w):
    """(смещения L,T,W,H; True — точечный анкор (0,0), alignment (0,0), без авторазмера) или (None, False)."""
    s = slot_of(w)
    if s is None:
        return None, False
    lay = s.get_editor_property("layout_data")
    o, a, al = lay.offsets, lay.anchors, lay.alignment
    point = (abs(a.minimum.x) <= EPS and abs(a.minimum.y) <= EPS and abs(a.maximum.x) <= EPS and abs(a.maximum.y) <= EPS
             and abs(al.x) <= EPS and abs(al.y) <= EPS and not bool(s.get_editor_property("auto_size")))
    return (float(o.left), float(o.top), float(o.right), float(o.bottom)), point


def set_offsets(w, want):
    """Записать смещения слота (анкоры, alignment, авторазмер и Z не меняются)."""
    s = slot_of(w)
    lay = s.get_editor_property("layout_data")
    lay.set_editor_property("offsets", unreal.Margin(want[0], want[1], want[2], want[3]))
    s.set_editor_property("layout_data", lay)


def walk(root):
    """Имя → узел для всего дерева (первый по обходу)."""
    out, st = {}, [root]
    while st:
        w = st.pop()
        if w is None:
            continue
        out.setdefault(w.get_name(), w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))
    return out


def cell_pos(n, cols):
    """(столбец, ряд) ячейки n при `cols` столбцах."""
    return n % cols, n // cols


def plane_rx(n):
    """Regex имени плана ячейки n `INVEN_SET{n}_{idx}_0`."""
    return re.compile(r"^INVEN_SET%d_(\d+)_0$" % n)


def base_rx(n):
    """Regex имени подложки ячейки n `Inven_BaseSet{n}_{idx}_0`."""
    return re.compile(r"^Inven_BaseSet%d_(\d+)_0$" % n)


def find_cell_nodes(nodes, n):
    """(подложка, план, текст) ячейки n — по именам импортёра/клона; план — UImage, но не UAsdaAnimatedPlane."""
    base = plane = None
    for nm, w in nodes.items():
        if base is None and base_rx(n).match(nm) and isinstance(w, unreal.Image):
            base = w
        if plane is None and plane_rx(n).match(nm) and isinstance(w, unreal.Image) \
                and not isinstance(w, unreal.AsdaAnimatedPlane):
            plane = w
    text = nodes.get("INVEN_SET%d" % n)
    if text is not None and not isinstance(text, unreal.TextBlock):
        text = None
    return base, plane, text


def cell_offsets(kind, c, r):
    """Слот ячейки (столбец c, ряд r) в её группе: подложка (5+33c, 5+33r), план (33c, 33r), текст (33c, 33r+18)."""
    tpl = {"base": TPL_BASE_OFF, "plane": TPL_PLANE_OFF, "text": TPL_TEXT_OFF}[kind]
    return (tpl[0] + STEP * c, tpl[1] + STEP * r, tpl[2], tpl[3])


def clone_names(n):
    """Имена клонов ячейки n ≥ 30: (подложка, план, текст)."""
    return ("Inven_BaseSet%d_%d_0" % (n, BASE_IDX0 + n - CELLS_OLD),
            "INVEN_SET%d_%d_0" % (n, PLANE_IDX0 + n - CELLS_OLD),
            "INVEN_SET%d" % n)


_OFF_RX = re.compile(r"off\([^)]*\)")        # смещения слота в строке дерева (оба вида: `L.. T..` и `l,t,w,h`)
_ORDER_RX = re.compile(r" #\d+(?= \|\||$)")  # порядковый номер среди братьев в строке дерева


def row_no_order(row):
    """Строка дерева без порядкового номера среди братьев."""
    return _ORDER_RX.sub("", row or "")


def row_no_offsets(row):
    """Строка дерева без номера среди братьев и без смещений слота."""
    return _OFF_RX.sub("off(*)", row_no_order(row))


class Step:
    """Состояние прогона: все решения — до записи (сухой проход), запись — `apply`."""

    def __init__(self, bp, dry):
        self.bp = bp                                  # окно
        self.dry = dry                                # сухой прогон
        self.root = B.get_root_canvas(bp)             # корневой канвас
        self.nodes = walk(self.root)                  # имя → узел
        self.moves = []                               # правки слота: (имя, было, стало, вид "move"/"grow"/"cell")
        self.creates = []                             # клоны к созданию: (образец, имя родителя, имя, dx, dy)
        self.ok = 0                                   # узлов «готово»
        self.ok_clones = 0                            # клонов «готово»
        self.inspect = []                             # «осмотреть» — окно не пишется
        self._rows = None                             # строки дерева окна (лениво, один дамп на прогон)

    def rows(self):
        """Строки дерева окна `asda_ui_step_dryrun._tree_rows` (один дамп на прогон — нужен только для сверки клонов)."""
        if self._rows is None:
            self._rows = DR._tree_rows(self.bp)
        return self._rows

    def plan_node(self, name, parent, kind, before, after):
        """Решение по одному узлу таблицы/ячейке: готово / править / осмотреть."""
        w = self.nodes.get(name)
        if w is None:
            self.inspect.append("%s: узла нет" % name)
            return
        par = w.get_parent()
        if (par.get_name() if par else "-") != parent:
            self.inspect.append("%s: родитель %s, ждали %s — НЕ трогаю" % (name, par.get_name() if par else "-", parent))
            return
        cur, point = slot_state(w)
        if cur is None or not point:
            self.inspect.append("%s: слот не канвасный / не точечный анкор / авторазмер — НЕ трогаю" % name)
            return
        if near(cur, after):
            self.ok += 1
        elif near(cur, before) or any(near(cur, alt) for alt in ALT_BEFORE.get(name, ())):
            self.moves.append((name, tuple(cur), after, kind))
        else:
            self.inspect.append("%s: слот %s ≠ «было» %s и ≠ «стало» %s — возможна ручная правка владельца, НЕ трогаю"
                                % (name, tuple(cur), before, after))

    def plan_clone(self, src, parent_name, name, dx, dy):
        """Клон `src`: есть — сверка вида и места («готово» / «осмотреть»), нет — к созданию."""
        w = self.nodes.get(name)
        so, _p = slot_state(src)
        want = (so[0] + dx, so[1] + dy, so[2], so[3])
        if w is None:
            self.creates.append((src, parent_name, name, dx, dy))
            return
        par = w.get_parent()
        if type(w) is not type(src) or par is None or par.get_name() != parent_name:
            raise RuntimeError("узел %s чужого вида (класс %s, родитель %s) — СТОП" % (
                name, type(w).__name__, par.get_name() if par else "-"))
        cur, point = slot_state(w)
        if not point or not near(cur, want):
            self.inspect.append("%s: клон на %s, ждали %s — возможна ручная правка, НЕ трогаю" % (name, cur, want))
            return
        bad = clone_diff(self.rows(), src, w)
        if bad:
            self.inspect.append("%s: клон отличается от образца %s (%s) — НЕ трогаю" % (name, src.get_name(), bad))
            return
        self.ok_clones += 1

    def run(self):
        """Решения по всем узлам шага (без записи)."""
        for name, parent, kind, before, after in TABLE:
            self.plan_node(name, parent, kind, before, after)
        # ── образцы n = 0 (обязаны стоять на импортном месте: клоны и перестановка считаются от них) ──
        tb, tp, tt = find_cell_nodes(self.nodes, 0)
        if tb is None or tp is None or tt is None:
            raise RuntimeError("нет образца ячейки 0 (подложка/план/текст) — СТОП")
        for w, grp, off in ((tb, BASE_GRP, TPL_BASE_OFF), (tp, ICON_GRP, TPL_PLANE_OFF), (tt, ICON_GRP, TPL_TEXT_OFF)):
            cur, point = slot_state(w)
            par = w.get_parent()
            if not point or not near(cur, off) or par is None or par.get_name() != grp:
                raise RuntimeError("образец %s: слот %s / родитель %s ≠ импортного %s в %s — СТОП (сетку не считаю)"
                                   % (w.get_name(), cur, par.get_name() if par else "-", off, grp))
        used = set()                                  # idx планов окна, кроме клонов этого шага
        clones_all = set()
        for n in range(CELLS_OLD, CELLS_NEW):
            clones_all.update(clone_names(n)[:2])
        for nm in self.nodes:
            m = re.search(r"_(\d+)_(?:\d+|HIT|DRAG)$", nm)
            if m and nm not in clones_all:
                used.add(int(m.group(1)))
        clash = sorted(i for i in used if BASE_IDX0 <= i < PLANE_IDX0 + CELLS_NEW - CELLS_OLD)
        if clash:
            raise RuntimeError("idx клонов %s уже заняты узлами окна — СТОП" % clash[:10])
        # ── существующие ячейки: 0…5 уже на месте, 6…29 переставляются из (n%6, n/6) в (n%12, n/12) ──
        for n in range(CELLS_OLD):
            b, p, t = find_cell_nodes(self.nodes, n)
            if b is None or p is None or t is None:
                self.inspect.append("ячейка %d: нет подложки/плана/текста" % n)
                continue
            c0, r0 = cell_pos(n, COLS_OLD)
            c1, r1 = cell_pos(n, COLS_NEW)
            for w, grp, kind in ((b, BASE_GRP, "base"), (p, ICON_GRP, "plane"), (t, ICON_GRP, "text")):
                self.plan_node(w.get_name(), grp, "cell", cell_offsets(kind, c0, r0), cell_offsets(kind, c1, r1))
        # ── новые ячейки 30…59: клоны образца n = 0 ──
        for n in range(CELLS_OLD, CELLS_NEW):
            c, r = cell_pos(n, COLS_NEW)
            nb, np_, nt = clone_names(n)
            dx, dy = STEP * c, STEP * r
            self.plan_clone(tb, BASE_GRP, nb, dx, dy)
            self.plan_clone(tp, ICON_GRP, np_, dx, dy)
            self.plan_clone(tt, ICON_GRP, nt, dx, dy)

    def apply(self):
        """Запись: перестановка/растяжение по таблице и создание недостающих клонов (в порядке ячеек)."""
        for name, _before, after, _kind in self.moves:
            set_offsets(self.nodes[name], after)
        made = []
        for src, parent_name, name, dx, dy in self.creates:
            parent = self.nodes.get(parent_name)
            if parent is None:
                raise RuntimeError("родитель %s клона %s не найден — СТОП" % (parent_name, name))
            w = FC.make_like(self.bp, src, parent, name)
            FC.place_like(w, src, dx, dy)
            # Общие свойства, которых нет в списках `make_like` (трансформ отрисовки, подсказка …): клон = образец.
            FC.copy_props(w, src, DR.COMMON_PROPS)
            # Клон рисуется сразу за последней ячейкой своей группы (как ячейки импортёра) — не поверх соседей-эффектов.
            pos = last_cell_index(parent, exclude=w) + 1
            B.move_child_to_index(parent, w, pos)
            self.nodes[name] = w
            made.append(name)
        return made


def last_cell_index(parent, exclude=None):
    """Индекс последнего ребёнка `parent`, относящегося к ячейке сетки (подложка/план/текст), без `exclude`."""
    rx = re.compile(r"^(INVEN_SET\d+(_\d+_0)?|Inven_BaseSet\d+_\d+_0)$")
    last = -1
    for i in range(parent.get_children_count()):
        k = parent.get_child_at(i)
        if k != exclude and rx.match(k.get_name()):
            last = i
    return last


def clone_diff(rows, src, w):
    """Отличия клона `w` от образца `src` по виду: строка дерева `rows` (дамп + доп. свойства) без смещений и порядка,
    снимок `asda_ui_text_block_align._snapshot` без смещений слота. Пусто — совпадает."""
    import asda_ui_text_block_align as T   # noqa: E402  снимок узла
    a, b = row_no_offsets(rows.get(src.get_name())), row_no_offsets(rows.get(w.get_name()))
    out = []
    if a != b:
        out.append("строка дерева: %s" % DR._short_diff(a, b))
    sa = T._snapshot(src)[src.get_name()]
    sb = T._snapshot(w)[w.get_name()]
    if sa[:2] != sb[:2] or sa[3:] != sb[3:] or sa[2][1:] != sb[2][1:]:
        out.append("снимок")
    return "; ".join(out)


def abs_left_top(w, root):
    """Абсолютный левый-верх узла по цепочке групп (сумма смещений слотов до корня; анкоры точечные)."""
    x = y = 0.0
    cur = w
    while cur is not None and cur != root:
        s = slot_of(cur)
        if s is None:
            return None
        o = s.get_editor_property("layout_data").offsets
        x += float(o.left)
        y += float(o.top)
        cur = cur.get_parent()
    return x, y


def geometry_problems(bp):
    """Геометрия после шага: 60 планов/подложек/текстов `INVEN_SET{r·12+c}` в абсолюте (132+33c, 320+33r) (+5 у подложки
    от группы — подложка в абсолюте тоже 132+33c), шаг 33; габарит окна 116…542; раскрытая правая панель ≤ 1024."""
    root = B.get_root_canvas(bp)
    nodes = walk(root)
    out = []
    for n in range(CELLS_NEW):
        c, r = cell_pos(n, COLS_NEW)
        want = (GRID_ABS[0] + STEP * c, GRID_ABS[1] + STEP * r)
        b, p, t = find_cell_nodes(nodes, n)
        if b is None or p is None or t is None:
            out.append("ячейка %d: нет узлов" % n)
            continue
        for w, dy in ((b, 0.0), (p, 0.0), (t, 18.0)):
            got = abs_left_top(w, root)
            if got is None or abs(got[0] - want[0]) > EPS or abs(got[1] - (want[1] + dy)) > EPS:
                out.append("%s: абсолют %s, ждали (%.0f, %.0f)" % (w.get_name(), got, want[0], want[1] + dy))
    right, span = window_extent(bp)
    if span is None:
        out.append("рамка INVEN_WIN_0_*: не все 9 кусков в канвасе")
    elif abs(span[0] - WINDOW_SPAN[0]) > EPS or abs(span[1] - WINDOW_SPAN[1]) > EPS:
        out.append("габарит рамки %.1f…%.1f, ждали %.0f…%.0f" % (span[0], span[1], WINDOW_SPAN[0], WINDOW_SPAN[1]))
    if right > SCREEN_W + EPS:
        out.append("раскрытая правая панель до %.1f > %.0f" % (right, SCREEN_W))
    return out


def window_extent(bp):
    """(правый край раскрытой панели по видимым картинкам, габарит рамки по X (мин, макс) или None — нет слота куска)."""
    root = B.get_root_canvas(bp)
    nodes = walk(root)
    right = 0.0
    rp = nodes.get(RIGHT_PANEL)
    if rp is not None:
        for nm, w in walk(rp).items():
            if isinstance(w, unreal.Image) and not nm.endswith("_HIT") and not nm.endswith("_DRAG"):
                lt = abs_left_top(w, root)
                o, _p = slot_state(w)
                if lt is not None and o is not None:
                    right = max(right, lt[0] + o[2])
    xs = []
    for i in range(9):
        w = nodes.get("INVEN_WIN_0_%d" % i)
        o, _p = slot_state(w) if w is not None else (None, False)
        if o is None:
            return right, None
        xs += [o[0], o[0] + o[2]]
    return right, (min(xs), max(xs))


def residual(bp):
    """Остаток плана по окну (без записи): после `apply` обязан быть нулевым."""
    st = Step(bp, True)
    st.run()
    return st


def verify():
    """СТОРОЖ ПОСТ-ИМПОРТА ПО ЗНАЧЕНИЯМ (`asda_ui_post_import.VALUE_GUARDS`). Возврат — список проблем (пусто = окно в
    виде 12×5): недостающие клоны, узлы таблицы не в «стало», «осмотреть», геометрия 60 ячеек."""
    if not EAL.does_asset_exist(ASSET):
        return ["%s: нет ассета" % ASSET]
    bp = EAL.load_asset(ASSET)
    try:
        st = residual(bp)
    except Exception as e:                                       # noqa: BLE001
        return ["%s: сверка 12×5 не выполнена: %s" % (ASSET, e)]
    out = []
    if st.creates:
        out.append("%s: нет %d клонов ячеек 30…59 (%s…) — прогнать шаг asda_ui_inventory_x2"
                   % (ASSET, len(st.creates), ", ".join(c[2] for c in st.creates[:4])))
    if st.moves:
        out.append("%s: %d узлов ещё в виде 6×5 (%s…) — прогнать шаг asda_ui_inventory_x2"
                   % (ASSET, len(st.moves), ", ".join(m[0] for m in st.moves[:4])))
    for m in st.inspect:
        out.append("%s: осмотреть: %s" % (ASSET, m))
    if not st.creates and not st.moves and not st.inspect:
        out += ["%s: %s" % (ASSET, g) for g in geometry_problems(bp)]
    unreal.log("[инвентарь x2] сверка: готово %d узлов + %d клонов, правок %d, к созданию %d, осмотреть %d"
               % (st.ok, st.ok_clones, len(st.moves), len(st.creates), len(st.inspect)))
    return out


def frame_seam_done(asset, name, offs, plan_new):
    """Для `asda_ui_frame_seam_overlap`: True — кусок рамки `name` окна `asset` расширен ЭТИМ шагом поверх правки шва:
    «было» таблицы = «стало» плана шва (`plan_new`), текущие смещения `offs` = «стало» таблицы. ZOrder сверяет вызывающий."""
    if asset != ASSET:
        return False
    for nm, _par, _kind, before, after in TABLE:
        if nm == name:
            return near(before, plan_new) and near(after, offs)
    return False


def backup_asset():
    """Бэкап файла ассета до записи (закон backup-before-edit): `asda_headless_tool.backup_file`, размер сверен;
    копия набора уже есть — не перезаписывается (при отличии снимается `<копия>.<время>.bak`)."""
    f = os.path.join(CONTENT_DIR, ASSET[len("/Game/"):].replace("/", os.sep) + ".uasset")
    bak = HT.backup_file(f, BACKUP_SET, set_dir=os.environ.get(BACKUP_DIR_ENV) or None, verify="size", log=log)
    if bak is None:
        raise RuntimeError("нет файла ассета %s — бэкап невозможен, запись запрещена" % f)
    return bak


def check_snapshots(bp, rows0, made, st):
    """Сверка снимков до/после в памяти (до сохранения). Возврат — список нарушений."""
    rows1 = DR._tree_rows(bp)
    moved = set(m[0] for m in st.moves)
    made_s = set(made)
    bad = []
    lost = [n for n in rows0 if n not in rows1]
    extra = [n for n in rows1 if n not in rows0 and n not in made_s]
    missing = [n for n in made if n not in rows1]
    if lost:
        bad.append("потеряно узлов %d: %s" % (len(lost), lost[:10]))
    if extra:
        bad.append("лишних узлов %d: %s" % (len(extra), extra[:10]))
    if missing:
        bad.append("не создано %d: %s" % (len(missing), missing[:10]))
    foreign = [n for n in rows0 if n in rows1 and n not in moved and row_no_order(rows0[n]) != row_no_order(rows1[n])]
    if foreign:
        bad.append("изменено чужих %d: %s" % (len(foreign), foreign[:10]))
    broken = [n for n in moved if n in rows1 and row_no_offsets(rows0[n]) != row_no_offsets(rows1[n])]
    if broken:
        bad.append("у правленых изменилось не только смещение %d: %s" % (len(broken), broken[:10]))
    # Порядок прежних детей у каждого родителя сохранён (новые клоны лишь вставлены между ними).
    root = B.get_root_canvas(bp)
    for nm, w in walk(root).items():
        if not isinstance(w, unreal.PanelWidget):
            continue
        kids = [w.get_child_at(i).get_name() for i in range(w.get_children_count())]
        old = [k for k in kids if k not in made_s]
        idx0 = [int(_ORDER_RX.search(rows0[k]).group(0)[2:]) for k in old if k in rows0 and _ORDER_RX.search(rows0[k])]
        if idx0 != sorted(idx0):
            bad.append("нарушен порядок детей %s" % nm)
    # Клоны = образец (кроме смещения).
    nodes = walk(root)
    tb, tp, tt = find_cell_nodes(nodes, 0)
    for n in range(CELLS_OLD, CELLS_NEW):
        for src, nm in zip((tb, tp, tt), clone_names(n)):
            if nm in made_s:
                d = clone_diff(rows1, src, nodes[nm])
                if d:
                    bad.append("клон %s ≠ образец: %s" % (nm, d))
    return bad


def main(standalone=False):
    """Шаг пост-импорта. `standalone` — ручной запуск: бэкап ассета обязателен."""
    dry = DR.is_dry(DRYRUN_ENV)
    if not EAL.does_asset_exist(ASSET):
        raise RuntimeError("нет ассета %s" % ASSET)
    bp = EAL.load_asset(ASSET)
    st = Step(bp, dry)
    st.run()
    pre = "[сухой] " if dry else ""
    n_move = sum(1 for m in st.moves if m[3] == "move")
    n_grow = sum(1 for m in st.moves if m[3] == "grow")
    n_cell = sum(1 for m in st.moves if m[3] == "cell")
    for name, before, after, kind in st.moves:
        log("%s[%s] %s: %s → %s" % (pre, {"move": "сдвиг", "grow": "растяжение", "cell": "ячейка"}[kind], name,
                                    tuple(before), tuple(after)))
    for src, parent_name, name, dx, dy in st.creates:
        log("%s[клон] %s ← %s в %s, сдвиг (%.0f, %.0f)" % (pre, name, src.get_name(), parent_name, dx, dy))
    for m in st.inspect:
        log("осмотреть: %s" % m)
    log("не трогаются (кода нет / туториал / на месте): %s; узла INVEN_WHEELWIN в ассете нет" % ", ".join(UNTOUCHED))
    log("%sготово %d узлов + %d клонов; править: сдвиг %d, растяжение %d, ячеек %d; клонов к созданию %d; осмотреть %d"
        % (pre, st.ok, st.ok_clones, n_move, n_grow, n_cell, len(st.creates), len(st.inspect)))
    if st.inspect:
        if dry:
            log("[сухой] окно было бы НЕ записано: %d узлов «осмотреть»" % len(st.inspect))
            unreal.log("%s %s: [СУХОЙ] осмотреть %d" % (DR.MARK, TAG, len(st.inspect)))
            log(MARKER + " (сухой прогон, есть «осмотреть»)")
            return
        raise RuntimeError("инвентарь x2: %d узлов «осмотреть» — вид не совпал с таблицей, окно НЕ записано (см. лог)"
                           % len(st.inspect))
    if dry or (not st.moves and not st.creates):
        unreal.log("%s %s: правок %d, создано %d, осмотреть 0%s" % (
            DR.MARK, TAG, 0, 0, " (СУХОЙ ПРОГОН: правок было бы %d, клонов %d)" % (len(st.moves), len(st.creates))
            if dry else ""))
        if not dry:
            g = geometry_problems(bp)
            if g:
                raise RuntimeError("окно в виде «стало», но геометрия не сошлась: %s" % g[:5])
        log(MARKER + (" (сухой прогон)" if dry else " (правок нет)"))
        return
    if standalone:
        backup_asset()
    rows0 = DR._tree_rows(bp)
    made = st.apply()
    bad = check_snapshots(bp, rows0, made, st)
    log("узлов до %d; правлено %d; создано %d; нарушений сверки %d" % (len(rows0), len(st.moves), len(made), len(bad)))
    if bad:
        raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: %s" % "; ".join(bad[:8]))
    rest = residual(bp)
    log("остаток после правок (в памяти, до сохранения): правок %d, к созданию %d, осмотреть %d"
        % (len(rest.moves), len(rest.creates), len(rest.inspect)))
    if rest.moves or rest.creates or rest.inspect:
        raise RuntimeError("после правок остался невыполненный план — не сохраняю")
    geo = geometry_problems(bp)
    if geo:
        raise RuntimeError("геометрия не сошлась — не сохраняю: %s" % geo[:5])
    right, span = window_extent(bp)
    log("геометрия: 60 ячеек 12×5 на местах; окно %.0f…%.0f (ширина %.0f), раскрытая правая панель до %.0f"
        % (span[0], span[1], span[1] - span[0], right))
    B.compile_and_save(bp)   # ⚠ только компилирует и метит пакет — пишет `DR.save`
    if not DR.save(bp, dry, TAG, False):
        raise RuntimeError("не сохранён %s" % ASSET)
    unreal.log("ASDA_WRITES_ASSET: %s" % ASSET)
    unreal.log("%s %s: правлено %d (сдвиг %d, растяжение %d, ячеек %d), создано %d, осмотреть 0"
               % (DR.MARK, TAG, len(st.moves), n_move, n_grow, n_cell, len(made)))
    log(MARKER + " (правлено %d, создано %d)" % (len(st.moves), len(made)))


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    try:
        main(standalone=True)
    except Exception:                                            # noqa: BLE001
        unreal.log_error("[инвентарь x2] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
