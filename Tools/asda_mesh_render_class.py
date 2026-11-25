# Copyright (c) 2026 @sitxovski (Discord)
# asda_mesh_render_class.py — КЛАСС ОТРИСОВКИ ПОДСЕТКИ .jbm и ИМЯ ЕЁ МАТЕРИАЛА.
#
# ЗАЧЕМ. Мастер-материал в порте выбирался по ПРЕФИКСУ ИМЕНИ ассета (`ch1_*` → непрозрачный,
# остальное → masked). Это неверно: прозрачность решает ТЕХНИКА ШЕЙДЕРА, а техника выбирается
# ПО-МЕШНО из `meshType` (`mr+0x000`), см. ниже. Из-за префиксного правила все меши NPC и мобов
# сидели на masked-мастере, хотя у техник `tSkin`/`tRigid` альфа текстуры — НЕ прозрачность,
# а маска блика. Тот же баг чинили 2026-07-31 для семейства `ch1_*`
# (`Reports/weapon_texture_alpha_fix.md`); здесь он закрыт и для NPC/мобов.
#
# ★ ЭТАЛОН 1 — ВЫБОР ТЕХНИКИ ПО `meshType`. `renderApplyMeshState @0x1116FE0`
#   (`RE/etalon/render_core.c:39781-39813`) читает `*(_DWORD *)*this` = `meshType` меша и в этом
#   порядке проверяет биты:
#       & 0x80  → VtxDecl A  → tSkin   (скин, две позиции на вершину)
#       & 0x100 → VtxDecl B  → tRigid  (жёсткая привязка к одной кости)
#       & 0x200 → VtxDecl C  → tNoSkin (объекты сцены)
#       иначе   → фиксированный конвейер (SetFVF)
#   Бит `0x8` в `mr+0x004` выбирает АЛЬТЕРНАТИВНЫЙ вершинный шейдер той же техники — на
#   прозрачность не влияет. Бит `0x1` в `meshType` — CULLMODE=NONE (двусторонность).
#   Имена техник кэшируются в `CRenderer_CacheEffectHandles @0x111E610` (`render_core.c:40401-40404`).
#
# ★ ЭТАЛОН 2 — ЧТО ТЕХНИКА ДЕЛАЕТ С АЛЬФОЙ (шипнутый с клиентом `system/EmptyProject.fx`):
#     technique tSkin  → VShade()      + PShade()        (строки 1051-1060)
#     technique tRigid → VShadeRigid() + PShade()        (строки 1063-1072)
#     technique tNoSkin→ VShadeNoSkin()+ PShadeNoSkin()  (строки 1086-1095)
#   PShade()        : Output.RGBColor.w = (1 * (In.fog * 4.0f)) * (NoSkinTexScroll.w * 0.25f);
#                     ↑ вместо `Tex.w` стоит ЕДИНИЦА. `Tex.w` идёт ТОЛЬКО в блик и кубмап:
#                       ... + ((vSpecular.xyz * Tex.w) * ShadowAlpha.z)
#                       Output.RGBColor += (texCUBE(...) * (Tex.w * 0.3f)) * ShadowAlpha.z;
#                     ⇒ альфа текстуры = МАСКА БЛИКА, меш НЕПРОЗРАЧЕН.
#   PShadeNoSkin()  : Output.RGBColor.w = (Tex.w * (In.fog * 2.0f)) * (NoSkinTexScroll.w * 0.5f);
#                     ⇒ здесь `Tex.w` ЕСТЬ в альфе — вырезка/прозрачность настоящие.
#
# ★ ЭТАЛОН 3 — КОГДА tNoSkin РИСУЕТСЯ С БЛЕНДИНГОМ. Бит `0x1` (и рантайм-бит `0x2`) в `mr+0x004`
#   исключает меш из непрозрачного прохода и отправляет в отсортированную по глубине очередь
#   (`meshNodeRenderDispatch @0x111E110`, `entityRenderQueueAddType23 @0xF770F0`), которая
#   сбрасывается в `CRenderer_RenderFrame @0x975EF0` (`render_core.c:820-827`) с состоянием
#   ALPHABLENDENABLE=1, SRCBLEND=SRCALPHA, DESTBLEND=INVSRCALPHA, ALPHATESTENABLE=1,
#   ALPHAREF=8, ALPHAFUNC=GREATEREQUAL, при ВКЛЮЧЁННОЙ записи глубины (ZWRITE снимает только
#   бит `0x10`, `CSceneNode_Dispatch @0x1143AA0`).
#   Разбор всего поля и инвентаризация: `Reports/jbm_blend_flag_study.md`.
#
# ⇒ ТРИ КЛАССА ОТРИСОВКИ:
#     CHAR  = tSkin/tRigid           → НЕПРОЗРАЧНЫЙ мастер, альфа заведена в Specular
#     MASK  = tNoSkin без бита 0x1   → masked-мастер (вырезка по альфе)
#     BLEND = tNoSkin с битом 0x1|0x2→ translucent-мастер (альфа-блендинг + отсечка 8/255)
#
# ★ СЛОТ МЕША В UE = УНИКАЛЬНОЕ ИМЯ МАТЕРИАЛА (доказано замером: у 133 моделей NPC и 99 моделей
#   MobSkel список слотов ПОБИТОВО равен списку уникальных имён текстур в порядке первого
#   появления — `Tools/_probe_npc_mob_masters.py`). Экспортёры называют материал по имени
#   текстуры подсетки, поэтому подсетки РАЗНЫХ классов с ОДНОЙ текстурой сливаются в один слот,
#   и развести их мастера нельзя. Для таких моделей класс дописывается в ИМЯ МАТЕРИАЛА
#   (тот же приём, что `BLEND_MAT_SUFFIX` у автоматов гачи) — тогда UE заводит отдельный слот.
#
# ★ Модуль БЕЗ побочных эффектов при импорте (правило tools-no-import-side-effects) и без
#   зависимостей от numpy/unreal — годится и для офлайн-сборки .glb, и для headless-скриптов UE.

import os
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_jbm as JBM
import asda_material_policy as POL

CLS_CHAR = "CHAR"     # tSkin/tRigid — непрозрачный (альфа = маска блика)
CLS_MASK = "MASK"     # tNoSkin без бита блендинга — masked (вырезка по альфе)
CLS_BLEND = "BLEND"   # tNoSkin с битом блендинга — очередь с блендингом

# ★ СУФФИКСЫ ИМЕНИ МАТЕРИАЛА. `_blend` — ОБЩЕПРОЕКТНОЕ соглашение (asda_material_policy.BLEND_SUFFIX,
#   его же понимают экспортёры `blend_mat_suffix` и `master_for`), поэтому берём его оттуда, а не
#   объявляем заново. `_msk` — добавлен здесь: он нужен ТОЛЬКО чтобы развести подсетки tNoSkin,
#   делящие текстуру с подсетками tSkin/tRigid той же модели (у NPC/мобов таких слотов 79).
MASK_SUFFIX = "_msk"
CLASS_SUFFIX = {CLS_CHAR: "", CLS_MASK: MASK_SUFFIX, CLS_BLEND: POL.BLEND_SUFFIX}
# Кто остаётся с ГОЛЫМ именем материала, если на одну текстуру приходятся разные классы.
# CHAR первым намеренно: это самый частый класс (284 из 484 слотов NPC/мобов), и голое имя у него
# означает, что переимпорт нужен ТОЛЬКО моделям со смешанными слотами, а не всему дереву.
PRIORITY = (CLS_CHAR, CLS_MASK, CLS_BLEND)

# Биты meshType, по которым renderApplyMeshState@0x1116FE0 выбирает технику (порядок проверки важен).
TECH_BITS = ((0x80, "tSkin"), (0x100, "tRigid"), (0x200, "tNoSkin"), (0x800, "tNoSkin"))


def technique(mesh_type):
    """Имя техники шейдера по `meshType` (mr+0x000) — порядок проверки как в эталоне."""
    for bit, name in TECH_BITS:
        if mesh_type & bit:
            return name
    return "fixed"            # ни один бит не взведён → фиксированный конвейер (SetFVF)


def mesh_class(rec, enable_blend=True):
    """Класс отрисовки подсетки: CHAR / MASK / BLEND (см. шапку).

    Принимает запись `asda_jbm.parse_header()['recs'][i]` или меш `parse_geometry()['meshes'][i]`.
    ★ Критерий блендинга НЕ дублируем — берём `asda_jbm.is_alpha_blend` (единственный источник:
      бит `mr+0x004 & 0x1|0x2` И техника tNoSkin).
    enable_blend=False — класс BLEND схлопывается в MASK (для групп, где прозрачность ещё не
      включена; тогда имена слотов и мастера не меняются).
    """
    t = technique(int(rec.get("mtype", 0)))
    if t in ("tSkin", "tRigid"):
        return CLS_CHAR
    if t == "fixed":
        return CLS_MASK       # фиксированный конвейер: альфа идёт из стадии текстуры → как masked
    return CLS_BLEND if (enable_blend and JBM.is_alpha_blend(rec)) else CLS_MASK


def _base_name(rec):
    """Базовое имя материала подсетки = имя её текстуры без расширения (так называют экспортёры)."""
    return os.path.splitext(rec.get("tex") or "")[0] or "mat"


def strip_class(name):
    """Имя слота/MIC без метки класса — по нему ищется ТЕКСТУРА.

    Разведённая подсетка носит ТУ ЖЕ текстуру, что и её сосед по слоту (мы разводим их только
    ради разных мастеров), поэтому перед поиском текстуры метку надо снять. Учитываем и хвост
    Interchange при совпадении имён (`..._msk`, `..._msk1`, `..._blend12`) — ищем вхождение,
    а не окончание.
    """
    n = str(name)
    for suf in (POL.BLEND_SUFFIX, MASK_SUFFIX):
        i = n.lower().find(suf)
        if i >= 0:
            n = n[:i] + n[i + len(suf):]
    return n


def material_names(recs, split=True, enable_blend=True):
    """Имена материалов подсеток модели, В ПОРЯДКЕ ПОДСЕТОК.

    split=True  — если на одну текстуру приходятся РАЗНЫЕ классы, приоритетный класс сохраняет
                  голое имя, остальные получают суффикс класса ⇒ UE заводит им отдельный слот.
    split=False — имена как раньше (голое имя текстуры); слот остаётся смешанным.
    ★ Класс BLEND получает суффикс ВСЕГДА (даже если слот однороден): так требует общепроектное
      соглашение — `asda_material_policy.master_for()` узнаёт прозрачный слот именно по метке.
    """
    classes = [mesh_class(r, enable_blend) for r in recs]
    bases = [_base_name(r) for r in recs]
    by_base = {}
    for b, c in zip(bases, classes):
        by_base.setdefault(b, set()).add(c)
    primary = {b: next(c for c in PRIORITY if c in cs) for b, cs in by_base.items()}
    out = []
    for b, c in zip(bases, classes):
        if c == CLS_BLEND:
            out.append(b + CLASS_SUFFIX[c])                     # метка блендинга — всегда
        elif split and len(by_base[b]) > 1 and c != primary[b]:
            out.append(b + CLASS_SUFFIX[c])                     # развод смешанного слота
        else:
            out.append(b)
    return out


def slot_classes(recs, split=True, enable_blend=True):
    """{имя слота: класс} модели. Слот = уникальное имя материала (см. шапку).

    При split=True каждый слот однороден. При split=False смешанный слот получает класс по
    PRIORITY — ровно то, что пришлось бы выбрать без переэкспорта.
    """
    names = material_names(recs, split=split, enable_blend=enable_blend)
    per_slot = {}
    for name, r in zip(names, recs):
        per_slot.setdefault(name, set()).add(mesh_class(r, enable_blend))
    return {n: next(c for c in PRIORITY if c in cs) for n, cs in per_slot.items()}


def slot_order(recs, split=True, enable_blend=True):
    """Имена слотов в порядке ПЕРВОГО ПОЯВЛЕНИЯ — ровно так их нумерует импорт UE."""
    out = []
    for n in material_names(recs, split=split, enable_blend=enable_blend):
        if n not in out:
            out.append(n)
    return out


def mixed_slots(recs, enable_blend=True):
    """{голое имя: отсортированные классы} — слоты, которые надо разводить переэкспортом."""
    by = {}
    for r in recs:
        by.setdefault(_base_name(r), set()).add(mesh_class(r, enable_blend))
    return {b: sorted(cs) for b, cs in by.items() if len(cs) > 1}


def needs_reexport(recs, enable_blend=True):
    """True, если имена материалов модели изменились относительно прежних (голых) ⇒ нужен .glb заново."""
    return material_names(recs, split=True, enable_blend=enable_blend) != \
        material_names(recs, split=False, enable_blend=False)


def archive_slot_classes(bnz_path, split=True, enable_blend=True, model_filter=None):
    """{модель(lower): {слот: класс}} по всему архиву моделей (.bnz — обычный ZIP).

    model_filter(имя_модели) -> bool — необязательный отбор (напр. только `mon_*` для мобов).
    """
    import zipfile
    out = {}
    with zipfile.ZipFile(bnz_path) as z:
        for n in z.namelist():
            if not n.lower().endswith(".jbm"):
                continue
            model = os.path.splitext(os.path.basename(n))[0]
            if model_filter and not model_filter(model):
                continue
            hdr = JBM.parse_header(z.read(n))
            if not hdr:
                continue
            out[model.lower()] = slot_classes(hdr["recs"], split=split, enable_blend=enable_blend)
    return out
