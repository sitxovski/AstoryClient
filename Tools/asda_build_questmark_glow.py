# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_questmark_glow.py — ОБВОДКА (glow-контур) КВЕСТОВЫХ МЕТОК «!»/«?» — порт эталона 1:1.
#
# ═══ РЕВЕРС (итог двух проходов; вторая редакция ИСПРАВЛЯЕТ первую) ═══════════════════════════════
# Отдельного .jeb/частиц у метки НЕТ (полный поиск `exclam` по архивам клиента: 3 меша .jbm,
# 3 клипа .jbb, 3 текстуры .tex; в EffectModel.bnz записи нет). Эффект складывается из ДВУХ вещей:
#
#  (A) КЛИП `eff_0_npc_99_exclam-a_0N_0.jbb` — 1 кость, 121 кадр, 30 fps, петля 4.0 с:
#      pos.Y 0 → 11.57 (парение) + равномерный спин вокруг Y 81°/с. Уже играется в порту
#      (AAsdaNpcActor::SetQuestMark → PlayAnimation лупом).
#
#  (B) ★ ОБВОДКА = ВТОРОЙ ПРОХОД ПО ТОЙ ЖЕ ГЕОМЕТРИИ С ВЫДАВЛИВАНИЕМ ПО НОРМАЛИ.
#      ★★ ВАЖНАЯ ПОПРАВКА к первой редакции: объект рисуется ЗА КАДР ДВАЖДЫ, а не только glow —
#      `entity_char.c:8986-8990`:
#          CRenderQueue_AddObject(..., *(entity[5]+20) + 668*j + 384, ...);        // ОБЫЧНЫЙ меш
#          if ( (*(_DWORD *)entity[5] & 2) != 0 )                                  // флаг pbb
#            CRenderQueue_AddObject(..., *(*(entity[5]+28)+352) + 116*j,
#                                        *(entity[5]+28)+356, ...);                // GLOW-узел
#      ⇒ ТЕЛО метки рисуется ОБЫЧНЫМ путём со своей текстурой (жёлто-зелёный градиент —
#        численно подтверждено разбором .tex: верх (255,255,255) → низ (225,217,0), альфа 255
#        во всех 4096 пикселях), а поверх идёт ВТОРОЙ проход — обводка.
#
#      pbbStripBatchInit@0x11CE570 (model_anim.c:38011-38056) НЕ создаёт геометрию: только
#      `new[](116*partCount+4)` (массив узлов, pbb+352) и `new[](4*partCount)` (флаги).
#      pbbBuildStripBatch@0x11C15F0 (entity_char.c:65909-66027) отдаёт renderBufferInitDesc
#      ИСХОДНЫЕ буферы модели (:65989 индексы, :65992 вершины, :65994 FVF) и те же скин-матрицы.
#      Выдавливание делает ВЕРШИННЫЙ ШЕЙДЕР (vs.1.1, ассемблируется из строки @0x15C40B0,
#      ставится как dword_35292B0 + g_pVtxDeclC, model_anim.c:23874):
#          mad r0.xyz, r2.xyz, c92.w, r1.xyz    ; pos_world += normal_world * c92.w
#          mov oD0.xyzw, c91.xyzw               ; ПЛОСКИЙ цвет+альфа обводки (освещения нет)
#      Константы: c92.w = *(float*)(node+76) = pbb+72 — ТОЛЩИНА; c91 = pbb+76 = {R,G,B,альфа}.
#
#      ПАРАМЕТРЫ вызова у метки (entity_char.c:59348/59366/59384, дубль 60101/60119/60137):
#          Vector3_Set(v222, 1.0f, 0.5f, 0.0f);
#          pbbBuildStripBatch(model, R,G,B, 3.0, 5.0, 0.1, 0.15, 0.2, 3000.0, 1500.0, 500.0, ...);
#      Смысл (сверено с вызовом у заточки оружия, item_inventory.c:2848-2854):
#          3.0   → pbb+52  толщина, НАЧАЛО            0.1  → pbb+64  альфа, ключ 0
#          5.0   → pbb+56  толщина, КОНЕЦ             0.15 → pbb+392 альфа, ключ 1 (средний)
#          3000  → pbb+60  длительность фазы, мс      0.2  → pbb+68  альфа, ключ 2
#          1500  → pbb+396 время среднего ключа, мс   500  → pbb+40  длительность удержания, мс
#      ★ 3000/1500/500 — МИЛЛИСЕКУНДЫ, а не LOD-дистанции (у заточки туда кладут 1500..2000 и
#        300..400 — те же роли).
#
#      АНИМАТОР — uiTweenUpdatePhase@0x1250ED0 (ui_core.c:103744-103890), 4-фазный ping-pong:
#        фаза 0: 3000 мс, толщина 3.0→5.0, альфа 0.1→0.15 (0..1500 мс) → 0.15→0.2 (1500..3000)
#        фаза 1: удержание 500 мс на (5.0, 0.2)
#        фаза 2: 3000 мс, обратный ход 5.0→3.0, альфа 0.2→0.15→0.1
#        фаза 3: удержание 500 мс на (3.0, 0.1)      ⇒ полный цикл 7000 мс
#      Порт этой развёртки — в C++ (AAsdaNpcActor::TickQuestMarkOutline), 1:1 с константами выше.
#
#      ЦВЕТ обводки (c91): _00 (жёлтый «!») и _01 (красный «!») → (1.0, 0.5, 0.0) оранжевый;
#                          _02 («?»)                            → (1.0, 0.5, 0.7529412) розовый.
#
# ═══ ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ПОРТА [ПОРТ] 2026-09-02 (находка soulmate_soul_aura_render.md §9) ═══
#      РЕНДЕР-СОСТОЯНИЯ первой редакции были описаны НЕВЕРНО: «ALPHABLENDENABLE=1
#      SRCALPHA/INVSRCALPHA (translucent), ZWRITE=1, backface cull». Это тот же glow-проход
#      pbb, что у «ауры» силуэта «души»; дизасм того же пути ПЕРЕПРОВЕРЕН своими глазами
#      (fix_soul_aura_impl.md §1, capstone по AsdaStory.exe):
#        • d3dSetAlphaBlendState@0x111DCE0: push 5/push 0x13 (SRCBLEND=SRCALPHA) и
#          ★ push 2/push 0x14 (DESTBLEND=D3DBLEND_ONE) ⇒ АДДИТИВ, НЕ INVSRCALPHA;
#        • CRenderer_EnableCullCW@0x976D20: push 1/push 0x16 (CULLMODE=D3DCULL_NONE) ⇒ ДВУСТОРОННИЙ;
#        • rendApplyZWriteOnState@0x979910: push 0/push 0xe (ZWRITEENABLE=0) ⇒ запись глубины ВЫКЛ.
#      ЧТО ДЕЛАЕТ ОРИГИНАЛ: аддитив / two-sided / zwrite-off. ЧТО ДЕЛАЛ ПОРТ: translucent /
#      односторонний. ЧТО ДЕЛАЕМ: приводим мастер обводки к аддитиву+двусторонний (у Additive в UE
#      запись глубины и так выкл, депт-тест остаётся — совпадает с оригиналом).
#      ЗАМЫСЕЛ СОХРАНЁН: обводка — это ГЛОУ (самосветящийся контур поверх тела), аддитив её только
#      усиливает и не затемняет; альфа мелкая (0.1..0.2) ⇒ вклад субтильный, как и был.
#      КЛЮЧ ОТКАТА (ассет-мастер, CVar неприменим): восстановить из
#      Tools/asda_build_questmark_glow.py.bak-2026-09-02-aura (translucent/two_sided=False) и
#      пересобрать материал.
#      депт-биас −10 живёт в КЛЮЧЕ СОРТИРОВКИ (порядок отрисовки, депт-тест не отключается).
#
# ═══ ЧТО ДЕЛАЕТ ЭТОТ СКРИПТ ══════════════════════════════════════════════════════════════════════
#  1. ВОЗВРАЩАЕТ три материала меток на прежний мастер M_AsdaObjectLit. Первая редакция перевела их
#     на «глоу-тинт» (Unlit + текстура×оранжевый), считая, что тинт красит ТЕЛО — это оказалось
#     неверно: тинт принадлежит ОБВОДКЕ, а тело рисуется обычным путём. Заодно снимается
#     ★★★ ПРИЧИНА «СЕРЫХ МЕТОК» (юзер, живой тест): мастер, созданный MaterialFactoryNew, имел
#     used_with_skeletal_mesh=False, а метка — СКЕЛЕТАЛ-меш; такой материал движок на скелетал-меше
#     не применяет и подменяет на DefaultMaterial (ровный СЕРЫЙ, текстура не сэмплится вовсе).
#     Сверено пробой Tools/_probe_mat_usage.py: у рабочих M_AsdaObject / M_AsdaObjectLit флаг True.
#  2. СОЗДАЁТ мастер обводки M_AsdaQuestMarkOutline (★ рендер-состояния исправлены 2026-09-02, см.
#     блок ИСПРАВЛЕНИЕ ДЕФЕКТА ПОРТА выше):
#       Unlit + ★Additive (SRCALPHA/ONE оригинала), ★двусторонний (CULLMODE=NONE оригинала),
#       WorldPositionOffset = normalize(VertexNormalWS) * Thickness   ← выдавливание по нормали,
#       EmissiveColor = OutlineColor * OutlineAlpha, Opacity = 1  (★ФОЛД альфы в Emissive —
#         идентично `dst += color*alpha` оригинала и надёжно в аддитиве UE; как в target-highlight),
#       ★ used_with_skeletal_mesh = True (иначе тот же серый DefaultMaterial).
#     Thickness/OutlineAlpha/OutlineColor — параметры, их покадрово гонит C++ (тween эталона).
#
#   Запуск: Tools/run_headless.bat Tools/asda_build_questmark_glow.py
import unreal
import os as _au_os, sys as _au_sys   # ★ волна 2 п. 7 (14.09.2026): реестр путей ассетов (Tools/asda_ue_paths.py)
_au_sys.path.insert(0, _au_os.path.dirname(_au_os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

EAL = unreal.EditorAssetLibrary
MLIB = unreal.MaterialEditingLibrary

BODY_MASTER = _U.M_OBJECT_LIT          # тело метки — как у всех NPC-моделей
OUTLINE_PATH = _U.G_MATERIALS
OUTLINE_NAME = "M_AsdaQuestMarkOutline"
OUTLINE = "%s/%s" % (OUTLINE_PATH, OUTLINE_NAME)
OBSOLETE = _U.M_GLOW_TINT          # мастер первой (ошибочной) редакции

MIC_FMT = "/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_%02d/Materials/eff_0_npc_99_exclam-a_%02d"
TEX_FMT = "/Game/AsdaAssets/Textures/NPC/Texture/eff_0_npc_99_exclam-a_%02d"


def enforce(mat, want):
    """Идемпотентно выставить свойства мастера (работает и на уже созданном ассете)."""
    changed = False
    for name, val in want:
        if mat.get_editor_property(name) != val:
            mat.set_editor_property(name, val)
            unreal.log("[qmark-glow]   %s: %s -> %s" % (mat.get_name(), name, val))
            changed = True
    return changed


def build_outline_master():
    # ★ Граф мастера поменялся (Emissive = OutlineColor*OutlineAlpha, Opacity=1) — простого
    #   enforce() свойств поверх старого ассета мало, поэтому ассет ПЕРЕСОЗДаётся начисто.
    #   Так же поступает asda_build_soul_silhouette_mat.py.
    was = EAL.does_asset_exist(OUTLINE)
    if was:
        EAL.delete_asset(OUTLINE)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(OUTLINE_NAME, OUTLINE_PATH, unreal.Material, unreal.MaterialFactoryNew())
    # ★ Рендер-состояния 1:1 с дизасмом оригинала (см. блок ИСПРАВЛЕНИЕ ДЕФЕКТА ПОРТА в шапке):
    #   аддитив (SRCALPHA/ONE), двусторонний (CULLMODE=NONE), zwrite off (у Additive в UE — сам).
    enforce(mat, [("shading_model", unreal.MaterialShadingModel.MSM_UNLIT),
                  ("blend_mode", unreal.BlendMode.BLEND_ADDITIVE),
                  ("two_sided", True),
                  # ★★★ без этого флага скелетал-меш получает DefaultMaterial (серый) — см. шапку
                  ("used_with_skeletal_mesh", True),
                  ("used_with_morph_targets", True),
                  ("used_with_static_lighting", True),
                  ("used_with_instanced_static_meshes", True)])

    # OutlineColor (плоский цвет: в эталоне oD0 = c91, освещения в шейдере нет)
    col = MLIB.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, -200)
    col.set_editor_property("parameter_name", "OutlineColor")
    col.set_editor_property("default_value", unreal.LinearColor(1.0, 0.214041, 0.0, 1.0))

    # OutlineAlpha (альфа из того же c91.w, гоняется тween-ом эталона 0.1..0.2)
    alpha = MLIB.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 0)
    alpha.set_editor_property("parameter_name", "OutlineAlpha")
    alpha.set_editor_property("default_value", 0.1)

    # ★ EmissiveColor = OutlineColor * OutlineAlpha — ★ФОЛД альфы: в аддитиве даёт `dst += color*alpha`
    #   оригинала надёжно (не зависит от того, множит ли версия UE Emissive на Opacity). Opacity=1.
    emis = MLIB.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, -120)
    MLIB.connect_material_expressions(col, "", emis, "A")     # VectorParameter: выход = RGBA
    MLIB.connect_material_expressions(alpha, "", emis, "B")
    MLIB.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    one = MLIB.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 60)
    one.set_editor_property("r", 1.0)
    MLIB.connect_material_property(one, "", unreal.MaterialProperty.MP_OPACITY)

    # ★ WorldPositionOffset = normalize(VertexNormalWS) * Thickness — ровно `mad r0.xyz, normal, c92.w`
    nrm = MLIB.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -700, 200)
    norm = MLIB.create_material_expression(mat, unreal.MaterialExpressionNormalize, -500, 200)
    thick = MLIB.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 340)
    thick.set_editor_property("parameter_name", "Thickness")
    thick.set_editor_property("default_value", 3.0)
    mul = MLIB.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 220)
    MLIB.connect_material_expressions(nrm, "", norm, "")
    MLIB.connect_material_expressions(norm, "", mul, "A")
    MLIB.connect_material_expressions(thick, "", mul, "B")
    MLIB.connect_material_property(mul, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    MLIB.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat, (not was)


def restore_body_materials():
    """Вернуть тело метки на прежний мастер: тело рисуется ОБЫЧНЫМ путём, тинта на нём нет."""
    body = unreal.load_asset(BODY_MASTER)
    if not body:
        unreal.log_error("[qmark-glow] нет %s" % BODY_MASTER)
        return
    for i in range(3):
        p = MIC_FMT % (i, i)
        mi = EAL.load_asset(p)
        if not mi:
            unreal.log_warning("[qmark-glow] нет материала %s — пропуск" % p)
            continue
        old = mi.get_editor_property("parent")
        if old != body:
            mi.set_editor_property("parent", body)
            MLIB.update_material_instance(mi)
        tp = TEX_FMT % i
        if EAL.does_asset_exist(tp):
            MLIB.set_material_instance_texture_parameter_value(mi, "Tex", unreal.load_asset(tp))
        MLIB.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        got = MLIB.get_material_instance_texture_parameter_value(mi, "Tex")
        unreal.log("[qmark-glow] тело %s: parent %s -> %s | Tex=%s"
                   % (p.split("/")[-1], old.get_name() if old else "?", body.get_name(),
                      got.get_name() if got else "None"))
        if (not got) or got.get_name() == "DefaultTexture":
            unreal.log_error("[qmark-glow]   ★ Tex НЕ ПРИВЯЗАН — метка будет СЕРОЙ")


def main():
    restore_body_materials()
    mat, created = build_outline_master()
    unreal.log("[qmark-glow] мастер обводки %s: %s" % (OUTLINE, "СОЗДАН" if created else "обновлён"))
    # мастер первой (ошибочной) редакции больше ничем не используется
    if EAL.does_asset_exist(OBSOLETE):
        EAL.delete_asset(OBSOLETE)
        unreal.log("[qmark-glow] удалён устаревший мастер %s" % OBSOLETE)
    unreal.log("[qmark-glow] ГОТОВО")


if __name__ == "__main__":
    main()
