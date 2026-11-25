# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_light_node.py — ОДНА точка правды по освещению моделей премиум-гачи в UE.
#
# ★★★ ГЛАВНЫЙ ФАКТ: АВТОМАТЫ ГАЧИ В ОРИГИНАЛЕ РИСУЮТСЯ БЕЗ ОСВЕЩЕНИЯ. Множитель к текстуре
#   ровно **1.0**, а не «ambient + N·L·diffuse». Вывод сделан ПО КОДУ И ПО ДАННЫМ, в три шага.
#
#   1) КАКОЙ ШЕЙДЕР РАБОТАЕТ. Сцена гачи ставит `g_bUseEffectPath = 0`
#      (`PetWindow_RenderToTarget@0xB6D5B0`, npc_mob_pet.c:56-181), то есть `.fx`-путь ВЫКЛЮЧЕН и
#      `system/EmptyProject.fx` (техники tSkin/tRigid/tNoSkin, `PShade`) к автоматам НЕ ОТНОСИТСЯ.
#      Модели идут через вершинные шейдеры vs.1.1, ЗАШИТЫЕ В `system/AsdaStory.exe`.
#
#   2) КАКОЙ ИМЕННО ИЗ НИХ. `renderApplyMeshState@0x1116FE0` (render_core.c:39781) выбирает пару
#      «шейдер + декларация» по `meshType` (`*flags_ptr`, поле `mr+0x000`) и ВНУТРИ каждой ветки —
#      по биту `0x8` поля `pStateFlags` (узел +0x30 → **`mr+0x004`**):
#
#          if (meshType & 0x80 ) -> (mr4 & 8) ? g_pSkinShaderA : g_pVtxShaderA
#          if (meshType & 0x100) -> (mr4 & 8) ? g_pSkinShaderB : g_pVtxShaderB
#          if (meshType & 0x200) -> (mr4 & 8) ? g_pVtxShaderC_Alt : g_pVtxShaderC
#
#      «Skin/Alt»-варианты — ОСВЕЩЁННЫЕ, у них блок
#          m3x3 r1, N, c[a0.x] ; dp3 r1.x, r1, c95 ; max r0.x, r1.x, c94.w
#          mul r0, c87, r0.x   ; add r0, c94, r0   ; add oD0, r1, r0
#      то есть `oD0 = c94 + c87·max(N·c95, c94.w) + c91·затухание` (c94 = ambient карты,
#      c87 = цвет света карты, c95 = направление НА свет; `CRenderer_SetDirLight@0x1124F60`,
#      `CRenderer_SetFogAndLightConstants@0x1124D30`). Вторые варианты — НЕОСВЕЩЁННЫЕ, у них
#      вместо всего блока одна строка:
#
#          mov oD0.xyz, c95.w          ; c95.w = 1.0 (задаётся в CRenderer_SetFogAndLightConstants)
#          mov oD0.w,   c92.w          ; альфа = фейд объекта
#
#   3) ЧТО СТОИТ У АВТОМАТОВ. Прочитаны `md_gachya_{a..e}_00.jbm` (см. `assert_all_unlit` ниже):
#      у ВСЕХ 20 подсеток ВСЕХ ПЯТИ моделей `mr+0x004 & 0x8 == 0`. meshType — 0x100/0x200/0x201.
#      ⇒ выбирается `g_pVtxShaderB` / `g_pVtxShaderC`, `oD0.xyz = 1.0`.
#
#   4) ЧТО ДЕЛАЕТ ПИКСЕЛЬНАЯ СТАДИЯ. Фиксированный конвейер:
#      `D3DTSS_COLOROP = 4 (D3DTOP_MODULATE)`, `COLORARG1 = 2 (TEXTURE)`, `COLORARG2 = 0/1
#      (DIFFUSE/CURRENT)` — render_core.c:40994-40996, 40847-40849, 1506-1508.
#      ⇒ `out = Tex · oD0 = Tex · 1.0` — **РОВНО БАЙТЫ ТЕКСТУРЫ**.
#
#   ⇒ Правильный порт: `Emissive = Tex`, Unlit. Никакой гаммы-жонглирования не нужно: UE
#     декодирует sRGB-текстуру в линейное, тонмаппер с `ToneCurveAmount = 0` кодирует обратно в
#     sRGB, и этот круг ЗАМЕРЕН как точный (кадр при множителе ≡ 1 совпал с байтами текстуры,
#     `Reports/gacha_grey_fix2.md` §4.1 и `gacha_color_fix.md` §3).
#
# ⚠ ЧТО БЫЛО РАНЬШЕ И ПОЧЕМУ ЭТО БЫЛО НЕВЕРНО. Здесь стояла формула `.fx`-пути
#   `lite = m_MatAmbient + max(0,N·L)·m_MatDiffuse` с числами из живого дампа (0.6 / 0.8) и
#   умножением в гамма-пространстве. Числа верные — но для ДРУГОГО шейдера, который у гачи
#   выключен. Вдобавок направление света подобрать было невозможно (оно per-map), и при
#   `LightDir = (−1,0,0)` весь видимый силуэт получал плоский **×0.6**: −40 % яркости и ноль
#   светотени. Именно это владелец видел как «бледные, вымытые, без контраста».
#
# ★★ ОБЩАЯ ТОЧКА ПОРТА [[port-shared-points]]. Формула МИРА (`M_AsdaTerrain`, `M_AsdaObject`,
#   `Tools/asda_build_materials.py`) остаётся своей: у мировых мешей бит `mr+0x004 & 0x8`
#   встречается, и там освещение реально работает. Этот модуль отвечает ТОЛЬКО за гачу и
#   ЗАЩИЩЁН проверкой `assert_all_unlit()` — если однажды у автомата появится подсетка с битом,
#   сборка упадёт с внятным сообщением, а не тихо соврёт.
#
# Модуль ТОЛЬКО собирает узлы и проверяет данные; ассеты не создаёт и не сохраняет.
# Импортируется сборщиками `asda_build_gacha_machine_material.py`,
# `asda_build_gacha_glass_material.py`, `asda_build_gacha_shadow_material.py`.
import os
import sys

import unreal

MEL = unreal.MaterialEditingLibrary

# ── ФЛАГИ МЕША ─────────────────────────────────────────────────────────────────────────────────
# `mr+0x004` бит 3: 1 = освещённый вершинный шейдер, 0 = неосвещённый (`oD0.xyz = 1.0`).
MESH_LIT_BIT = 0x8
# `mr+0x000` (meshType) бит 0: 1 = `CULLMODE = NONE` (двусторонний), 0 = `CULLMODE = CW`.
# `d3dApplyMaterialRenderState@0x111DA90` → `CRenderer_EnableCullCW@0x976D20` (CULLMODE=1=NONE)
# либо `gfxApplyColorWriteState@0x97ACE0` (CULLMODE=2=CW).
MESH_TWOSIDED_BIT = 0x1

GACHA_MODELS = ("md_gachya_a_00", "md_gachya_b_00", "md_gachya_c_00",
                "md_gachya_d_00", "md_gachya_e_00")


def _tools_on_path():
    """Каталог этого модуля — в `sys.path`, чтобы соседние `asda_*` импортировались из UE."""
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    return here


def _jbm_dir():
    """Каталог исходных `.jbm` автоматов — тот же, из которого собран `.glb` импортёра."""
    _tools_on_path()
    import asda_paths
    return asda_paths.CHAR_MODEL_ORIG_DIR


def read_mesh_flags(path):
    """Вернуть список `(meshType, renderFlags)` по всем подсеткам `.jbm`.

    Разбор делегирован общему парсеру `asda_jbm.parse_header` (закон «одна точка правды»):
    `mr+0x000` = meshType (ключ `mtype`), `mr+0x004` = renderFlags (ключ `flags`).
    ★ Именно `parse_header`, а НЕ `parse_file`: второй читает геометрию через `numpy`, которого
      во встроенном Python редактора UE нет (ModuleNotFoundError при сборке мастеров).
    """
    _tools_on_path()
    import asda_jbm
    with open(path, "rb") as f:
        hdr = asda_jbm.parse_header(f.read())
    if not hdr:
        raise RuntimeError("не разобрался как .jbm: %s" % path)
    return [(int(r["mtype"]), int(r["flags"])) for r in hdr["recs"]]


def assert_all_unlit(log=None):
    """★ ЗАЩИТА ОТ ТИХОЙ ЛЖИ. Проверить по САМИМ ДАННЫМ, что ни одна подсетка автоматов не
    просит освещённый шейдер (`mr+0x004 & 0x8`). Возвращает сводку; при нарушении — RuntimeError.

    Закон [[false-negatives-method]]: утверждение «света нет» опирается не на «мы не нашли», а на
    прочитанный бит у всех 100 подсеток (5 моделей × 20).
    """
    root = _jbm_dir()
    total = 0
    lit = []
    twosided = []
    for name in GACHA_MODELS:
        p = os.path.join(root, name + ".jbm")
        if not os.path.isfile(p):
            raise RuntimeError("нет исходной модели для проверки флагов: %s" % p)
        for i, (mtype, flags) in enumerate(read_mesh_flags(p)):
            total += 1
            if flags & MESH_LIT_BIT:
                lit.append("%s[%d] mr4=0x%08X" % (name, i, flags))
            if mtype & MESH_TWOSIDED_BIT:
                twosided.append("%s[%d] type=0x%04X" % (name, i, mtype))
    if lit:
        raise RuntimeError(
            "у подсеток автоматов ВЗВЕДЁН бит освещения mr+0x004&0x8 — мастера гачи собраны как "
            "неосвещённые и дадут неверный кадр. Список: " + ", ".join(lit))
    msg = ("проверка флагов: подсеток %d, с битом освещения (mr4&0x8) — 0, "
           "двусторонних (meshType&1) — %d [%s]" % (total, len(twosided), ", ".join(twosided)))
    if log:
        log(msg)
    return msg


def make_or_reset_master(pkg, name):
    """Вернуть ПУСТОЙ мастер по пути `pkg/name`, не удаляя ассет с диска.

    ★★★ ПОЧЕМУ НЕ `delete_asset` + `create_asset`. Так было раньше, и это ломается ровно тогда,
      когда владелец держит редактор открытым: `.uasset` занят другим процессом
      (`LogFileManager: Error deleting file: ... (Error Code 32)`), удаление молча не проходит,
      а `create_asset` возвращает `None` → `AttributeError` посреди сборки.
      Вдобавок удаление ОТВЯЗЫВАЛО все `MaterialInstanceConstant`, висевшие на мастере, — ради
      этого и появилась точка входа `asda_build_gacha_all.py` с «правильным порядком».
      Пересборка НА МЕСТЕ (`delete_all_material_expressions` — В ЦИКЛЕ до пустого графа, см. ниже)
      сохраняет и путь, и родителей MIC.
    """
    path = "%s/%s" % (pkg, name)
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(path):
        mat = eal.load_asset(path)
        if mat is None:
            raise RuntimeError("мастер существует, но не загрузился: %s" % path)
        # ★★★ БАГ UE 5.8 (ревизия 03.09.2026): ОДИН `delete_all_material_expressions` удаляет только
        #   каждый второй узел (удаление по ходу итерации, MaterialEditingLibrary.cpp:559-568) →
        #   фантомы старого графа и дубли параметров. Чистим В ЦИКЛЕ до пустого (asda_mat_graph).
        _tools_on_path()
        import asda_mat_graph
        asda_mat_graph.clear_expressions(mat, log=unreal.log, tag="[gacha-mat]")
        return mat
    atools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = atools.create_asset(name, pkg, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("не создался мастер %s" % path)
    return mat


def save_master(mat):
    """Сохранить мастер и УПАСТЬ, если сохранить не удалось.

    ★★★ ЗАЧЕМ. `EditorAssetLibrary.save_loaded_asset` при занятом `.uasset` (открыт редактор
      владельца — `LogFileManager: MoveFile ... (Error Code 32)`) возвращает False, но сборщик
      этого не замечал: лог рапортовал «мастер готов», а на диске оставалась СТАРАЯ версия.
      Час замеров можно потратить, меряя ровно то, что чинишь. Теперь это жёсткая ошибка.
    """
    path = mat.get_path_name().split(".")[0]
    # ★ Контроль графа ДО сохранения: дублей имён параметров быть не должно (признак фантомов бага
    #   UE 5.8 из make_or_reset_master); число узлов у каждого сборщика своё — не фиксируем.
    _tools_on_path()
    import asda_mat_graph
    asda_mat_graph.check_graph(mat, expected=None, log=unreal.log, tag="[gacha-mat]")
    if not unreal.EditorAssetLibrary.save_loaded_asset(mat):
        raise RuntimeError(
            "НЕ СОХРАНИЛСЯ мастер %s — файл занят другим процессом. Закрой редактор UE и "
            "повтори сборку (asda_build_gacha_all.py): в памяти материал уже правильный, "
            "но на диске осталась прежняя версия." % path)
    return path


def connect(frm, frm_out, to, to_in):
    """★ Закон [[ue-material-script-gotchas]]: connect МОЛЧА возвращает False при неверном входе."""
    if not MEL.connect_material_expressions(frm, frm_out, to, to_in):
        raise RuntimeError("не соединилось: %s[%s] -> %s[%s]" % (frm, frm_out, to, to_in))


def build_unlit_color(mat, tex, tex_out="RGB"):
    """`Emissive = Tex` — ТОЧНАЯ формула неосвещённого пути оригинала (`oD0.xyz = 1.0`,
    `COLOROP = MODULATE(TEXTURE, DIFFUSE)` ⇒ `out = Tex · 1`).

    Возвращает пару `(узел, выход)`, которую надо вести в `MP_EMISSIVE_COLOR`. Промежуточных
    узлов нет намеренно: любой лишний множитель здесь — отход от оригинала.
    """
    return tex, tex_out


def apply_skeletal_usage(mat):
    """Флаги применимости мастера к СКЕЛЕТАЛЬНОМУ мешу.

    ★ Без `used_with_skeletal_mesh` в куке (`-game`) UE подставляет `DefaultMaterial` — ровный
      серый непрозрачный. В редакторе и в PIE `UMaterial::CheckMaterialUsage` дописывает флаг на
      лету, поэтому на жалобы владельца в PIE этот флаг НЕ влиял (проверено прошлым заходом),
      но для упакованного билда он обязателен.
    """
    mat.set_editor_property("used_with_skeletal_mesh", True)
    mat.set_editor_property("used_with_morph_targets", True)


# ★★★ Дальняя плоскость сцены гачи, единицы оригинала (= единицы UE, `AsdaCoord::GameToUe3D`
#   масштаба не вносит): `mathBuildPerspectiveFovLH(projMatrix, 0.78539819, aspect, 10.0, 4000.0)`
#   — `PetWindow_RenderToTarget@0xB6D5B0`, npc_mob_pet.c:148. Всё, что дальше неё, в оригинале
#   «пусто»: Z-буфер там остаётся очищенным.
SCENE_FAR_PLANE = 4000.0

# ★★★ ЗАТВОР ВЫКЛЮЧЕН (координатор, 2026-08-18). Модель «непрозрачное всегда кладётся ПОВЕРХ
#   прозрачного» не выдерживает простой проверки: КВАДЫ ТЕНИ лежат на полу сцены, а пол —
#   непрозрачная геометрия, то есть по этому правилу тень обязана гаситься ПОЛНОСТЬЮ. В оригинале
#   она видна. Владелец трижды сообщал ровно этот симптом: «тени серые», «лутбоксы светлые» —
#   затвор `saturate(SceneDepth − 4000)` съедал тень там, где за ней пол, и оставлял купол
#   плотным белым пятном там, где за ним пустой фон.
#   Пока порядок отрисовки оригинала не воспроизведён корректно, прозрачность работает штатно.
BEHIND_OPAQUE_GATE = False


def apply_behind_opaque(mat, log=None):
    """★★★ ПОРЯДОК ОРИГИНАЛА: прозрачные подсетки рисуются ПЕРВЫМИ и БЕЗ ЗАПИСИ Z, а всё
    непрозрачное кладётся ПОВЕРХ них — независимо от их настоящей глубины.

    Эталон — `PetWindow_RenderToTarget@0xB6D5B0` (npc_mob_pet.c:164-171) вызывает отрисовку сцены
    ДВАЖДЫ:

        g_bForceShadowRender = 1;  renderChildrenSavedAlphaTestState(...);   // проход 1
        g_bForceShadowRender = 0;  renderChildrenSavedAlphaTestState(...);   // проход 2

    а `meshNodeRenderDispatch@0x111E110` (model_anim.c:25098-25112) решает по каждому мешу так:

        if (g_bRenderShadowPass || g_bForceShadowRender || ((mr4 & 2) == 0 && (mr4 & 1) == 0)) {
            if (g_bForceShadowRender == 1) SetRenderState(D3DRS_ZWRITEENABLE, 1);
            if (g_bForceShadowRender == 1) {
                if ((mr4 & 2) || (mr4 & 1))  SetRenderState(D3DRS_ZWRITEENABLE, byte_3528C46 ? 1 : 0);
                else if (!byte_3528C46)      return;          // непрозрачный меш ПРОПУЩЕН
            }
            ... draw ...
        }

    `byte_3528C46` в бинаре только ЧИТАЕТСЯ — записи нет ни одной (xref-ы `sub_111E110`,
    `sub_111B730`; `RE/ida/export/globals_ranked.tsv`), то есть он тождественно 0. Отсюда:

      · проход 1 рисует ТОЛЬКО прозрачные подсетки (`mr4 & 3`) с `ZWRITEENABLE = 0`;
      · проход 2 рисует ТОЛЬКО непрозрачные — по ПУСТОМУ Z-буферу, а значит ПОВЕРХ уже
        нарисованных стекла и теней, какая бы у тех ни была глубина.

    ⇒ купол в оригинале НЕ ложится плёнкой ни на девять шаров (они лежат ВНУТРИ него), ни на
      корпус: он виден только там, где за ним НЕТ ничего непрозрачного. То же и у квадов тени.

    В UE прозрачность рисуется ПОСЛЕ непрозрачного и проходит обычный тест глубины — купол
    оказывался сверху. Точный эквивалент правила оригинала выражается прямо: гасить прозрачный
    пиксель там, где в буфере глубины ЕСТЬ непрозрачная геометрия, то есть где
    `SceneDepth < дальней плоскости`. Пустой фон даёт `SceneDepth` заведомо больше (UE рисует с
    бесконечной дальней плоскостью), автоматы стоят в 150…600 единицах — порог 4000 разделяет их
    с огромным запасом и взят не с потолка, а из матрицы проекции самого оригинала.

    Возвращает узел-множитель, который надо ДОМНОЖИТЬ на непрозрачность.
    """
    if not BEHIND_OPAQUE_GATE:
        one = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -640, 420)
        one.set_editor_property("r", 1.0)
        if log:
            log("затвор «за непрозрачным» ВЫКЛЮЧЕН — прозрачность штатная (множитель 1.0)")
        return one
    depth = MEL.create_material_expression(mat, unreal.MaterialExpressionSceneDepth, -640, 420)
    far = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -640, 540)
    far.set_editor_property("r", SCENE_FAR_PLANE)
    sub = MEL.create_material_expression(mat, unreal.MaterialExpressionSubtract, -460, 440)
    connect(depth, "", sub, "A")
    connect(far, "", sub, "B")
    # ★ `saturate(SceneDepth − 4000)` — это ровно ступенька 0/1: за автоматом глубина 150…600
    #   (разность ≤ −3400 ⇒ 0), на пустом фоне UE рисует с бесконечной дальней плоскостью
    #   (разность огромна ⇒ 1). Промежуточных значений быть не может, поэтому узел `If`
    #   (которому в UE 5.8 нужны ВСЕ три входа подключёнными) здесь не нужен.
    gate = MEL.create_material_expression(mat, unreal.MaterialExpressionSaturate, -300, 440)
    connect(sub, "", gate, "")
    if log:
        log("порядок оригинала: прозрачное гасится там, где SceneDepth < %.0f" % SCENE_FAR_PLANE)
    return gate
