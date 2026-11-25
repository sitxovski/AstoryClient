# Copyright (c) 2026 @sitxovski (Discord)
# asda_material_policy.py — ЕДИНАЯ политика выбора мастер-материала (импортируемый модуль, без побочных
# действий при импорте). Используется генераторами материалов: asda_fix_model_materials.py,
# asda_assign_materials.py, import_equip_parts.py, asda_fix_char_material_blend.py.
#
# ★ ЭТАЛОН — шейдер клиента system/EmptyProject.fx (основная геометрия рисуется ЧЕРЕЗ него:
#   CRenderer_CacheEffectHandles@0x111E610 кэширует техники tNoSkin/tTerrain/tRigid/tSkin/tWater/...):
#
#   tSkin / tRigid  → PShade()   — ПЕРСОНАЖ, ЭКИП, ОРУЖИЕ, ПРИЧЁСКА, ЛИЦО (скин и жёсткие части на костях):
#       Output.RGBColor.w = (1 * (In.fog * 4.0f)) * (NoSkinTexScroll.w * 0.25f);
#       ↑ альфа-выхода НЕ зависит от текстуры (автор ЯВНО заменил Tex.w на 1) — только туман и
#         глобальный фейд объекта (NoSkinTexScroll.w).
#       Tex.w (альфа текстуры) используется ТОЛЬКО как МАСКА БЛИКА и КУБМАП-ОТРАЖЕНИЯ:
#           ... + ((vSpecular.xyz * Tex.w) * ShadowAlpha.z)
#           Output.RGBColor += (texCUBE(CubeTextureSampler, In.TexCube) * (Tex.w * 0.3f)) * ShadowAlpha.z;
#     ⇒ ПЕРСОНАЖ/ОРУЖИЕ/ЭКИП РИСУЮТСЯ НЕПРОЗРАЧНО. Альфа = служебная маска блеска, НЕ прозрачность.
#
#   tNoSkin → PShadeNoSkin() — объекты карты (деревья, трава, декор):
#       Output.RGBColor.w = (Tex.w * (In.fog * 2.0f)) * (NoSkinTexScroll.w * 0.5f);
#       ↑ здесь Tex.w ЕСТЬ в альфе ⇒ у tNoSkin альфа текстуры = НАСТОЯЩАЯ прозрачность.
#
# ★★★ ДОБАВЛЕНО 2026-08-18 (исследование `Reports/jbm_blend_flag_study.md`, вердикт §5):
#   среди tNoSkin оригинал делит меши ЕЩЁ РАЗ — по рендер-флагу `mr+0x004`. Без бита меш идёт
#   обычным непрозрачным проходом (вырезка по альфа-тесту), С битом — в отсортированную по глубине
#   очередь с блендингом. Поэтому мастера три, а не два:
#       нет бита            → MASTER_MASKED (M_AsdaObjectLit, clip 0.5)
#       бит И tNoSkin       → MASTER_BLEND  (M_AsdaObjectBlendLit) ← метка `_blend` в имени слота
#       tSkin/tRigid        → MASTER_CHAR   (M_AsdaCharLit), бит ИГНОРИРУЕТСЯ (альфа = маска блика)
#
# Практическое следствие (баг 2026-07-31): 93 текстуры оружия и 386 текстур брони содержат зоны
# alpha=0 (служебная маска блеска). Masked-мастер (OpacityMask = Tex.A, clip 0.5) вырезал их →
# «частично прозрачное оружие/броня». Правильный мастер для семейства ch1_* — НЕПРОЗРАЧНЫЙ,
# с альфой, заведённой в Specular (маска блика), как в PShade.

# Мастера
MASTER_MASKED = "/Game/Materials/M_AsdaObjectLit"   # tNoSkin без бита блендинга: вырезка по альфе
MASTER_CHAR   = "/Game/Materials/M_AsdaCharLit"     # tSkin/tRigid: персонаж, экип, оружие, NPC, мобы
# ★★★ ПРОЗРАЧНЫЙ мастер для мешей с `mr+0x004 & 0x1|0x2` И техникой tNoSkin (asda_jbm.is_alpha_blend).
#   Собирается `asda_build_blend_material.py`; воспроизводит состояние очереди оригинала
#   (`CRenderer_RenderFrame@0x975EF0`): альфа = Tex.A × фейд, отсечка ALPHAREF=8/255, ЗАПИСЬ ГЛУБИНЫ.
MASTER_BLEND  = "/Game/Materials/M_AsdaObjectBlendLit"

# ★★★ ВЫКЛЮЧАТЕЛЬ РАСКАТКИ ПРОЗРАЧНОСТИ (2026-08-18, решение владельца после осмотра в PIE:
#   «Оставь прозрачность пока только в gacha. На других объектах это ужасно выглядит»).
#
#   ЭТО ЕДИНСТВЕННАЯ ТОЧКА, КОТОРУЮ НАДО ПЕРЕКЛЮЧИТЬ, ЧТОБЫ ВЕРНУТЬ РАСКАТКУ:
#       BLEND_ROLLOUT_ENABLED = True   → класс BLEND уходит на MASTER_BLEND (как было 2026-08-18 днём)
#       BLEND_ROLLOUT_ENABLED = False  → класс BLEND трактуется как обычный tNoSkin ⇒ MASTER_MASKED
#   После переключения достаточно прогнать `Tools/asda_rollback_blend.py` (он переподвязывает
#   существующие MIC по действующей политике в обе стороны) — переимпорт моделей НЕ нужен, потому
#   что метка `_blend` в ИМЕНАХ СЛОТОВ и весь критерий (`asda_jbm.is_alpha_blend`,
#   `asda_mesh_render_class.mesh_class`) сохранены НЕТРОНУТЫМИ. Выключатель гасит только ВЫБОР
#   МАСТЕРА, а не наработку по определению прозрачных подсеток.
#
#   ★ АВТОМАТЫ ПРЕМИУМ-ГАЧИ ПОД ВЫКЛЮЧАТЕЛЬ НЕ ПОПАДАЮТ: у них своя пара мастеров
#   (`M_AsdaGachaGlass` для купола `Bowl`, `M_AsdaGachaShadow` для `G_shadow`/`Ball_shadow`),
#   которую назначает `asda_import_gachya_models.py` СВОИМ правилом и БЕЗ обращения к этому модулю.
#   Прозрачность гачи остаётся включённой при любом значении флага — это и есть исключение,
#   которое затребовал владелец.
#
#   ★ Почему падение именно в MASTER_MASKED, а не в MASTER_CHAR: класс BLEND по построению —
#   это техника tNoSkin (`asda_jbm.is_alpha_blend` требует meshType 0x200/0x201/0x800/0x801),
#   а tNoSkin без бита блендинга оригинал рисует обычным непрозрачным проходом с альфа-тестом —
#   т.е. ровно masked-мастером. Разделение CHAR/MASK по технике (фикс маски блика у NPC/мобов,
#   `Reports/blend_rollout_npc_mobs.md`) выключателем НЕ затрагивается.
BLEND_ROLLOUT_ENABLED = False

# Куда падает класс BLEND, когда раскатка выключена (см. обоснование выше).
MASTER_BLEND_DISABLED = MASTER_MASKED


def blend_rollout_enabled():
    """True, если раскатка прозрачности вне гачи включена (см. BLEND_ROLLOUT_ENABLED)."""
    return bool(BLEND_ROLLOUT_ENABLED)


def blend_master():
    """Мастер, который ДЕЙСТВИТЕЛЬНО получает класс BLEND при текущей политике.

    Всем потребителям (импортёры, билдер задника, переподвязчик, проверяльщик) полагается
    спрашивать мастер прозрачности ЗДЕСЬ, а не подставлять MASTER_BLEND напрямую — иначе
    выключатель перестанет быть одной точкой.
    """
    return MASTER_BLEND if BLEND_ROLLOUT_ENABLED else MASTER_BLEND_DISABLED


# ★ МЕТКА В ИМЕНИ СЛОТА. Экспортёры .glb (asda_test_export_one.build_fixed, asda_skinned_to_gltf.build,
#   asda_jbm_to_gltf.to_glb) дописывают этот суффикс имени материала у помеченных подсеток. Зачем:
#   1) у экспортёров с ДЕДУПОМ материалов по имени текстуры (build/to_glb-семейство, задник,
#      шкатулка) полупрозрачная подсетка иначе делит ОДИН слот с непрозрачным корпусом и физически
#      не может стать прозрачной (случай купола гачи `Bowl`);
#   2) метка переживает переимпорт: импортёр выбирает мастер ПО ИМЕНИ СЛОТА, поэтому результат
#      воспроизводится, а не «делается один раз руками в редакторе».
BLEND_SUFFIX  = "_blend"

# Семейство персонажа: ВСЕ ассеты клиента этого класса именуются ch1_* (тело ch1_{m|f}_cremmy_preview,
# броня ch1_{m|f}_NN_{chst|pant|shoe|grov|hcap}NNN, оружие ch1_{b|n|p}_NN_{swrd|bbow|...}, причёска
# ch1_*_hairNNN, лицо ch1_*_faceNNN). Прочее в Models/Char (cre_*, задники, флаги) — НЕ семейство.
CHAR_PREFIXES = ("ch1_",)


def is_char_family(asset_name):
    """True, если ассет (меш/материал/слот) принадлежит семейству персонажа (tSkin/tRigid)."""
    n = str(asset_name).lower()
    return any(n.startswith(p) for p in CHAR_PREFIXES)


def is_blend_slot(slot_or_mic_name):
    """True, если имя слота/MIC несёт метку полупрозрачной подсетки.

    ★ Проверять НАДО с учётом хвоста Interchange: при совпадении имён он дописывает номер
      (`natu_bigtree03-a_00_blend`, `..._blend1`, `..._blend12`), поэтому не `endswith`, а `in`.
    """
    return BLEND_SUFFIX in str(slot_or_mic_name).lower()


def strip_blend(name):
    """Имя слота/MIC без метки — им ищется ТЕКСТУРА (подсетка носит ту же текстуру, что и корпус)."""
    n = str(name)
    i = n.lower().find(BLEND_SUFFIX)
    return n if i < 0 else (n[:i] + n[i + len(BLEND_SUFFIX):])


def master_for(asset_name, slot_name=None):
    """Путь мастер-материала по правилу эталона (см. шапку).

    ★ ПОРЯДОК ПРОВЕРОК ВАЖЕН. Метка `_blend` ставится ТОЛЬКО там, где экспортёр уже проверил
      технику tNoSkin (asda_jbm.is_alpha_blend), а семейство ch1_* — это tSkin/tRigid. Пересечься
      они не могут по построению, но если слот всё же помечен — верим метке из данных, а не префиксу.

    ★★★ При BLEND_ROLLOUT_ENABLED=False метка `_blend` в имени слота СОХРАНЯЕТСЯ (её ставит
      экспортёр по данным .jbm), но мастер отдаётся masked — см. blend_master().
    """
    if slot_name is not None and is_blend_slot(slot_name):
        return blend_master()
    if is_blend_slot(asset_name):
        return blend_master()
    return MASTER_CHAR if is_char_family(asset_name) else MASTER_MASKED


# ★★★ ПОЛИТИКА ПО ТЕХНИКЕ, А НЕ ПО ПРЕФИКСУ ИМЕНИ (добавлено 2026-08-18, зона NPC/мобов).
#   `master_for()` выше выбирает мастер по имени ассета — это работает только там, где ВСЯ
#   модель принадлежит одному классу (семейство `ch1_*`). У NPC и мобов класс меняется ОТ МЕША
#   К МЕШУ внутри одной модели, поэтому им нужна функция от КЛАССА подсетки, который считает
#   `asda_mesh_render_class.mesh_class(meshType, renderFlags)` прямо из данных `.jbm`.
#   Отчёт: `Reports/blend_rollout_npc_mobs.md`.
#   ★★★ 2026-08-18 (откат раскатки): таблица СТРОИТСЯ ФУНКЦИЕЙ, а не константой — иначе значение
#   для BLEND зафиксировалось бы на момент импорта модуля и выключатель на неё бы не подействовал.
def master_by_class():
    """{класс отрисовки: путь мастера} при ТЕКУЩЕЙ политике (учитывает BLEND_ROLLOUT_ENABLED)."""
    return {"CHAR": MASTER_CHAR, "MASK": MASTER_MASKED, "BLEND": blend_master()}


def master_for_class(render_class):
    """Путь мастера по классу отрисовки подсетки (CHAR/MASK/BLEND) — см. asda_mesh_render_class.

    ★ CHAR и MASK выключателем НЕ затрагиваются: разделение по технике (маска блика у tSkin/tRigid
      против вырезки по альфе у tNoSkin) — отдельная работа, владелец её не откатывал.
    """
    return master_by_class()[render_class]


# ═══════════════════════════════════════════════════════════════════════════════════════════════
# ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТОВ ИСХОДНЫХ ДАННЫХ: ССЫЛКА НА ТЕКСТУРУ ЧУЖОЙ МОДЕЛИ
# ═══════════════════════════════════════════════════════════════════════════════════════════════
# Разбор — `Reports/gacha_model_d_texture.md`.
#
# ЧТО В ОРИГИНАЛЕ. У автомата премиум-гачи `md_gachya_d_00.jbm` таблица материалов (blob@0x210,
# шаг 0x20) содержит ЧЕТЫРЕ записи вместо трёх; лишняя `[1] = "md_Gachya_e_00.TEX"`. Файл на
# 32 байта длиннее четырёх остальных (340552 против 340520), CRC совпадает в обеих сборках
# клиента (MINE и ORIG) — то есть это не порча копии, а так собрана модель. Шесть подсеток —
# корпус `gacha`, `Handle`, `L_Gear`, `R_Gear`, `Stand` и купол `Bowl` — ссылаются на неё через
# пер-меш `matIdx` (mr+0x1F4); своя `md_Gachya_d_00.TEX` остаётся только на девяти шарах
# `Object01…Object10`. У `a`/`b`/`c`/`e` `matCount=3` и все 14 подсеток идут на `matIdx=0`.
#
# ЧТО ДЕЛАЕТ ОРИГИНАЛ. Ничего не чинит. `meshLoad_BindSubsetTexturesWithState@0x11A32E0` →
# `animGetBoneTransformRecord@0x11A6EB0` берёт имя строго как `matTable[matIdx]`
# (`qmemcpy(dst, 32*matIdx + matTableBase, 0x20)`); запасного пути «по имени модели» в клиенте
# нет. Значит в оригинальной ИГРЕ четвёртый автомат ходит в раскраске пятого.
#
# ПОЧЕМУ ЭТО ОШИБКА, А НЕ ЗАМЫСЕЛ. Пять автоматов — ОДИН меш с пятью атласами: сверка Pass1 даёт
# `max|dUV| = 0` и `max|dPos| = 0` между всеми пятью моделями по всем узлам корпуса. Атлас
# `md_gachya_d_00.tex` (308335 б, crc DE90AC54) поставляется с клиентом, свёрстан под ту же
# развёртку и отличается от `md_gachya_e_00.tex` — пятая раскраска нарисована и потеряна при
# сборке модели.
#
# ПОЧЕМУ ЧИНИМ. Закон проекта `fix-original-defects`: дефекты оригинала исправляем с пометкой.
#
# ГДЕ ЖИВЁТ ПРАВИЛО. Здесь, а не в сборщике: одни и те же `.jbm` конвертируют ДВА тула —
# `asda_build_skinned_models.py` (скелеталки сцены гачи) и `asda_build_models.py` (статик-версии
# в `RawAssets/Models/Char`). Обе ветки обязаны чинить одинаково, иначе статик и скелеталка
# разъедутся по раскраске.
import re as _re_mod

# (регексп имени МОДЕЛИ, регексп имени ТЕКСТУРЫ с группой-«буквой»). Буква текстуры обязана
# совпадать с буквой модели; иначе текстура переписывается на свою. Правило именно такое, а не
# «список моделей»: если в архиве найдётся ещё один автомат с той же ошибкой, он тоже починится.
CROSS_MODEL_TEX_RULES = [
    (_re_mod.compile(r"^md_gachya_([a-e])_00$", _re_mod.I),
     _re_mod.compile(r"^(md_Gachya_)([a-e])(_00\.TEX)$", _re_mod.I)),
]


def fix_cross_model_tex(model, meshes):
    """Переписать в подсетках ссылку на текстуру ЧУЖОЙ модели на свою (см. блок выше).

    `model`  — имя модели без расширения (`md_gachya_d_00`);
    `meshes` — список подсеток из `asda_jbm.parse_geometry` (правится поле `tex` НА МЕСТЕ).
    Возвращает список `(узел, было, стало)` — пустой, если правило не сработало.
    Текстуры шаров (`ball_ani_NN`) и тени (`gachya_shadow`) под правило не подпадают.
    """
    changed = []
    for model_re, tex_re in CROSS_MODEL_TEX_RULES:
        mm = model_re.match(model or "")
        if not mm:
            continue
        own = mm.group(1).lower()
        for me in meshes:
            tm = tex_re.match(me.get("tex") or "")
            if not tm or tm.group(2).lower() == own:
                continue
            new = tm.group(1) + own + tm.group(3)
            changed.append((me.get("node"), me["tex"], new))
            me["tex"] = new
    return changed
