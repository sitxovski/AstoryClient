# Copyright (c) 2026 @sitxovski (Discord)
# asda_model_import_ue.py — ЕДИНАЯ библиотека импорта моделей в UE (волна 2, п. 9 плана
# `Reports/dedup_audit/00_PLAN.md`, дефект E11): индекс текстур, поиск текстуры для слота/MIC,
# подвязка MIC (мастер + Tex), импорт .glb пачкой, снятие Nanite.
#
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает; `import unreal` —
#   только внутри функций, поэтому офлайн-скрипты и сухие прогоны могут импортировать модуль без UE.
#
# ★★★ ПРАВИЛО ПОБЕДИТЕЛЯ ПРИ СОВПАДЕНИИ ИМЁН (15.09.2026, решение владельца «сверь и выбери наиболее
#   правильную и эффективную»; отчёт `Reports/dedup_audit/tex_dup_rule.md`).
#   КАК РЕШАЕТ ОРИГИНАЛ: поиска текстуры «по имени среди всех папок» в клиенте НЕТ. Папка текстур модели
#   задаётся ПУТЁМ САМОЙ МОДЕЛИ: `<папка модели>\..\texture\Texture.btz` (эталон model_anim.c:27650,
#   :28787, :29056, :29465 — CModel_LoadWithTextures_119C560 / CModel_LoadFromFile), пары архивов прошиты
#   по типу сущности: NpcModel.bnz ↔ Data\NPC\Texture (npc_mob_pet.c:9399-9402), MobModel.bnz ↔ Data\Mob\Texture
#   (npc_mob_pet.c:7976-7979), CharModel.bnz ↔ Data\Character\Texture (item_inventory.c:358-361), предметы на
#   земле — Data\item\texture либо Data\Character\texture (effects_particles.c:57667-57669), объекты карты —
#   map\<карта>\Texture (world_map.c:846 CMap_BuildMapPath). Портреты Data\NPC\Image — только UI.
#   ПРАВИЛО БИБЛИОТЕКИ (детерминированное, от порядка реестра НЕ зависит, `dup="rank"` во всех пресетах):
#   1) индекс хранит ВСЕ копии имени (`TexIndex.copies`), значение словаря — копия с наименьшим рангом `_rank`;
#   2) потребитель, знающий тип модели, передаёт `kind` ("npc"/"mob"/"char"/"item"/"map:<карта>" или путь
#      пакета через `kind_for_package`) — побеждает копия из ПАПКИ СВОЕГО ТИПА (как оригинал);
#   3) нет копии в папке своего типа (или kind не задан) — копия с наименьшим рангом: модельные архивы
#      (NPC/Texture, Mobs/Texture, Characters/Texture, Items/Texture) → Effects → Backdrop → Maps/* по алфавиту
#      пути → прочие → UI-портреты NPC (Portrait, Image) последними; при равном ранге — алфавит пути.
#   Во всех текущих пересечениях модельных архивов пиксели копий совпадают (стенд отчёта), поэтому шаг 3 без
#   kind даёт ту же картинку, что и шаг 2. Прежние правила порядка реестра `last`/`first` и `prefer` оставлены
#   ТОЛЬКО как явная опция (сверка со старым поведением); ни один пресет их больше не использует.
#   До 15.09 пресеты повторяли порядок реестра — одно имя в разных MIC стояло на разных копиях, а 267 MIC
#   NPC носят копию из папки карты вместо атласа NPC/Texture (отчёт tex_dup_rule.md, список кандидатов).
import os as _os
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))

import asda_ue_paths as _U
import asda_material_policy as POL

# ═══════════════════════════════════════════════════════════════════════════════════════════════
# 1. ИНДЕКС ТЕКСТУР
# ═══════════════════════════════════════════════════════════════════════════════════════════════

# Правила дублей имён: last — последняя в порядке реестра, first — первая, prefer — по PREFER_DIRS.
DUP_LAST = "last"
DUP_FIRST = "first"
DUP_PREFER = "prefer"

# Папка UI-портретов NPC: портрет назван как модельный атлас и не должен перебивать его.
PORTRAIT_MARK = "/Portrait/"

# ★★★ ЗАЩИТА ОТ ВОЗВРАТА UI-ПОРТРЕТА (14.09.2026, `Reports/dedup_audit/wave2_npc_mic_portrait.md` §11).
#   `NPC/Image/<имя>` — выгрузка `Data\NPC\Image\Texture.btz` (TGA 256×512, портрет диалога), а `NPC/Texture/<имя>` —
#   DDS-атлас модели с тем же именем. Индекс по порядку реестра местами выбирал портрет (18 MIC NPC носили его).
#   Пропуск `/NPC/Image/` в индексе НЕ годится: он сдвигает выбор у чужих имён (декор карт уходит на копии `Maps/*`,
#   4 ключа `name_*` пропадают — стенд §5). Поэтому индекс не меняется, а правится только РЕЗУЛЬТАТ поиска:
#   если выбран `/NPC/Image/<имя>` или `/NPC/Portrait/<имя>` и в реестре под корнем есть `/NPC/Texture/<имя>` — вернуть атлас.
#   Меняются только результаты, равные `NPC/Image`/`NPC/Portrait` (доказано стендом §11). Выключить: find_tex(..., npc_atlas=False).
#   ★ step3 (14.09.2026, решение координатора): добавлен `/NPC/Portrait/` — пресет `all_last` его не пропускает и при части
#   порядков реестра выбирал портрет (FileMD5 Portrait == Image) у тех же MIC NPC.
NPC_IMAGE_MARK = "/NPC/Image/"
NPC_PORTRAIT_MARK = "/NPC/Portrait/"
NPC_PORTRAIT_MARKS = (NPC_IMAGE_MARK, NPC_PORTRAIT_MARK)   # папки UI-портретов NPC, подменяемые атласом
NPC_TEXTURE_MARK = "/NPC/Texture/"


class TexIndex(dict):
    """Индекс {имя.lower(): package} (обычный dict для всех потребителей) + `npc_texture`:
    {package.lower(): package} ВСЕХ `/NPC/Texture/` под корнем реестра (до пропусков и правила дублей) —
    чтобы проверить существование атласа, даже если по имени победила другая копия.
    `copies`: {имя.lower(): [package, …]} — ВСЕ копии имени после пропусков, отсортированы по `_rank`
    (★ 15.09: нужен выбору по типу модели `kind`, см. шапку и `tex_lookup`)."""
    __slots__ = ("npc_texture", "copies")

    def copy(self):
        """Копия с наборами `npc_texture` и `copies` (★ ревизия 15.09: `dict.copy()` терял их вместе с защитой
        от портрета и выбором по типу). ⚠ `dict(idx)` наборы всё равно теряет — потребителям передавать сам TexIndex."""
        c = TexIndex(self)
        c.npc_texture = getattr(self, "npc_texture", None)
        c.copies = getattr(self, "copies", None)
        return c


def npc_image_to_atlas(pkg, tex):
    """`…/NPC/Image/<имя>` или `…/NPC/Portrait/<имя>` → `…/NPC/Texture/<имя>`, если атлас есть в реестре;
    иначе pkg без изменений."""
    if not pkg:
        return pkg
    # ★ Ревизия 15.09: метка ищется без учёта регистра и только в корне `/Textures/NPC/…` (раньше — любая подстрока:
    #   `Maps/foo/NPC/Image/m` тоже подменялся, `npc/image` в нижнем регистре — нет). На реальном реестре результат прежний.
    low = pkg.lower()
    mark = next((m for m in NPC_PORTRAIT_MARKS if ("/textures" + m.lower()) in low), None)
    if mark is None:
        return pkg
    at = low.index("/textures" + mark.lower()) + len("/textures")
    cand = (pkg[:at] + NPC_TEXTURE_MARK + pkg[at + len(mark):]).lower()
    known = getattr(tex, "npc_texture", None)
    if known is None:   # обычный dict не из build_index — ищем среди значений (⚠ атлас под тем же именем-ключом
                        #   сюда не попадает, поэтому для plain dict защита практически не срабатывает; нужен TexIndex)
        known = {str(v).lower(): str(v) for v in tex.values() if NPC_TEXTURE_MARK in str(v)}
    return known.get(cand, pkg)

# ★ Детерминированное правило дублей (15.09.2026, см. шапку). Ранг не зависит от порядка реестра.
DUP_RANK = "rank"

# ★ Пресеты индекса (имя пресета → параметры tex_index). Имена прежние (их зовут потребители), правило дублей
#   во всех — DUP_RANK; пропуски и корень остались как были (это охват индекса, а не правило дублей):
#   all_last          — 13 потребителей: корень Textures, без пропусков;
#   noportrait_first  — asda_apply_blend_groups, asda_reimport_npc_mob_split (без UI-портретов /Portrait/);
#   noportrait_last   — asda_fix_npc_materials, asda_import_mob_skel (без /Portrait/);
#   chars_last        — asda_fix_weapon_missing_tex (корень только Textures/Characters).
#   ⚠ Суффиксы _last/_first в именах исторические: порядок реестра больше не влияет на выбор.
INDEX_PRESETS = {
    "all_last":         dict(root=_U.G_TEXTURES,       skip=(),              dup=DUP_RANK),
    "noportrait_first": dict(root=_U.G_TEXTURES,       skip=(PORTRAIT_MARK,), dup=DUP_RANK),
    "noportrait_last":  dict(root=_U.G_TEXTURES,       skip=(PORTRAIT_MARK,), dup=DUP_RANK),
    "chars_last":       dict(root=_U.G_TEX_CHARACTERS, skip=(),              dup=DUP_RANK),
}

# ★ Папки текстур по ТИПУ модели — как в оригинале (архив модели ↔ свой Texture.btz, эталон в шапке).
#   Ключ kind → подстроки package path (без учёта регистра). "map:<карта>" строится в kind_dirs.
KIND_DIRS = {
    "npc":  ("/Textures/NPC/Texture/",),          # NpcModel.bnz ↔ Data\NPC\Texture\Texture.btz
    "mob":  ("/Textures/Mobs/Texture/",),         # MobModel.bnz ↔ Data\Mob\Texture\Texture.btz
    "char": ("/Textures/Characters/Texture/",),   # CharModel.bnz ↔ Data\Character\Texture\Texture.btz
    "item": ("/Textures/Items/Texture/",),        # предмет на земле ↔ Data\item\texture
}

# ★ Ранг копии при отсутствии своей папки (или без kind): чем раньше, тем сильнее; внутри ранга — алфавит пути.
#   Модельные архивы первыми (их копии — текстуры моделей; во всех текущих пересечениях архивов пиксели равны),
#   затем эффекты, задник логина, копии папок карт (верны только в контексте своей карты), прочее,
#   UI-портреты NPC — последними (не модельные текстуры, `npc-skin-notes`).
RANK_DIRS = (
    "/Textures/NPC/Texture/",
    "/Textures/Mobs/Texture/",
    "/Textures/Characters/Texture/",
    "/Textures/Items/Texture/",
    "/Textures/Effects/",
    "/Textures/Backdrop/",
    "/Textures/Maps/",
)
RANK_LAST_DIRS = (NPC_PORTRAIT_MARK, NPC_IMAGE_MARK)   # UI-портреты — после всех остальных


def _rank(pkg):
    """Ранг копии: (группа, путь.lower()) — меньше = сильнее. Группа: позиция в RANK_DIRS; прочие папки —
    len(RANK_DIRS); `/NPC/Portrait/`, `/NPC/Image/` — после них. Не зависит от порядка реестра."""
    low = str(pkg).lower()
    for i, mark in enumerate(RANK_DIRS):
        if mark.lower() in low:
            return (i, low)
    for j, mark in enumerate(RANK_LAST_DIRS):
        if ("/textures" + mark.lower()) in low:
            return (len(RANK_DIRS) + 1 + j, low)
    return (len(RANK_DIRS), low)


def kind_dirs(kind):
    """Подстроки папок своего типа для kind ("npc"/"mob"/"char"/"item"/"map:<карта>") или () если kind не задан/неизвестен."""
    if not kind:
        return ()
    k = str(kind).lower()
    if k.startswith("map:"):
        return ("/Textures/Maps/%s/" % k[4:],)
    return KIND_DIRS.get(k, ())


def kind_for_package(pkg):
    """Тип модели по пути её пакета (MIC/меша/папки) — какой архив оригинала она представляет, или None.
    /Models/NPC → npc; /Models/Mob, MobSkel, MobTex → mob; /Models/Items → item;
    /Models/Char, /Models/Skeletal, /Models/Backdrop → char; /Game/Map/<карта>/… → map:<карта>."""
    if not pkg:
        return None
    parts = str(pkg).strip("/").split("/")
    low = [p.lower() for p in parts]
    if len(low) >= 3 and low[0] == "game" and low[1] == "map":
        return "map:" + low[2]
    if "models" in low:
        i = low.index("models")
        grp = low[i + 1] if i + 1 < len(low) else ""
        return {"npc": "npc", "mob": "mob", "mobskel": "mob", "mobtex": "mob", "items": "item",
                "char": "char", "skeletal": "char", "backdrop": "char"}.get(grp)
    return None


# ★★★ ТЕКСТУРА ПО ДАННЫМ — ЯВНОЕ ИСКЛЮЧЕНИЕ ИЗ ПОИСКА ПО ИМЕНИ СЛОТА (15.09.2026, отчёт
#   `Reports/dedup_audit/tex_rebind_2026-09-15.md`).
#   Моб `mon_obj_0_minewar-a_00` («Кристалл духа», `Data\Mob\Mob list.BIN` id 846, поле +0x04 — модель). В matTable его `.jbm`
#   авторское имя `crystaltower.png`, но для мобов оригинал matTable не читает: текстура сабмеша берётся из `<map>.MLK/.MBB`
#   (эталон modelBuildMeshFromCache@0x11A45E0, разбор `Tools/asda_mob_texmap.py`) — у id 846 все 12 сабмешей несут
#   `\Data\Mob\texture\mon_obj_0_minewar-a_00.TEX`. Имени `CRYSTALTOWER` в `Data\Mob\Texture\Texture.btz` нет, поэтому поиск
#   по имени слота находит только копию карты `Textures/Maps/mine/texture/CRYSTALTOWER` (копия одна — правило дублей и `kind`
#   тут бессильны). Слоты мешей `Mob/` и `MobSkel/` этой модели держат MIC на копии по данным (ставит `asda_fix_mob_materials.py`).
#   Правило для перевязчиков по имени: `data_pinned_tex(pkg, текущая)` — заданная текущая `Tex` НЕ меняется, пустая заполняется
#   копией по данным; `asda_fix_model_materials.py` модель пропускает целиком (он переименовал бы MIC по имени текстуры).
#   Ключ — папка модели (package path, без учёта регистра), значение — текстура по данным.
DATA_PINNED_TEX = {
    "/Game/AsdaAssets/Models/Mob/mon_obj_0_minewar-a_00": "/Game/AsdaAssets/Textures/Mobs/Texture/mon_obj_0_minewar-a_00",
    "/Game/AsdaAssets/Models/MobSkel/mon_obj_0_minewar-a_00": "/Game/AsdaAssets/Textures/Mobs/Texture/mon_obj_0_minewar-a_00",
}


def data_pinned_dir(pkg):
    """Папка модели из DATA_PINNED_TEX, которой принадлежит путь пакета/объекта/папки pkg (без учёта регистра), или None."""
    if not pkg:
        return None
    low = str(pkg).replace("\\", "/").rstrip("/").lower()
    last = low.rsplit("/", 1)[-1]
    if "." in last:                                   # путь объекта «…/Имя.Имя» → путь пакета
        low = low[:len(low) - len(last)] + last.split(".", 1)[0]
    for d in DATA_PINNED_TEX:
        dl = d.lower()
        if low == dl or low.startswith(dl + "/"):
            return d
    return None


def data_pinned_tex(pkg, current=None):
    """None — пакет не закреплён данными (обычный поиск по имени). Иначе путь текстуры, которую MIC должен носить:
    `current` (путь текущей Tex, объектный или пакетный), если он задан, — не менять; иначе текстура по данным."""
    d = data_pinned_dir(pkg)
    if d is None:
        return None
    if current:
        return str(current).split(".", 1)[0]
    return DATA_PINNED_TEX[d]


def _mic_tex_path(mic):
    """Путь СОБСТВЕННОГО значения параметра `Tex` у MIC (переопределение в texture_parameter_values) или None.
    Значение по умолчанию у мастера не считается: пустой MIC должен получить текстуру по данным."""
    try:
        for tp in (mic.get_editor_property("texture_parameter_values") or []):
            if str(tp.get_editor_property("parameter_info").get_editor_property("name")) == "Tex":
                v = tp.get_editor_property("parameter_value")
                return v.get_path_name() if v else None
    except Exception:
        return None
    return None


# Приоритет папок для DUP_PREFER (прежняя опция, сохранена для сверок; пресеты её не используют).
PREFER_DIRS = RANK_DIRS[:4]


def _prefer_rank(pkg):
    """Ранг пакета для DUP_PREFER: (позиция в PREFER_DIRS или len, путь) — меньше = сильнее."""
    for i, mark in enumerate(PREFER_DIRS):
        if mark.lower() in pkg.lower():
            return (i, pkg.lower())
    return (len(PREFER_DIRS), pkg.lower())


def build_index(entries, *, skip=(), dup=DUP_RANK):
    """ЧИСТОЕ ядро индекса (без UE): entries — итерируемое (имя_ассета, package_name) в любом порядке.

    Возвращает TexIndex {имя.lower(): package_name}; `copies` — все копии имени по `_rank`.
    skip — подстроки пути, которые пропускаются.
    dup: DUP_RANK (умолчание) — копия с наименьшим `_rank`, результат не зависит от порядка entries;
         DUP_LAST / DUP_FIRST — последняя / первая в порядке entries (прежние правила, только для сверок);
         DUP_PREFER — по PREFER_DIRS.
    """
    idx = TexIndex()
    idx.npc_texture = {}
    copies = {}
    for name, pkg in entries:
        pkg = str(pkg)
        if NPC_TEXTURE_MARK in pkg:          # ★ реестр атласов NPC для npc_image_to_atlas (выбор индекса не трогает)
            idx.npc_texture[pkg.lower()] = pkg
        if any(s in pkg for s in skip):
            continue
        key = str(name).lower()
        copies.setdefault(key, []).append(pkg)
        if dup == DUP_FIRST:
            idx.setdefault(key, pkg)
        elif dup == DUP_PREFER:
            cur = idx.get(key)
            if cur is None or _prefer_rank(pkg) < _prefer_rank(cur):
                idx[key] = pkg
        elif dup == DUP_LAST:
            idx[key] = pkg
    for key, lst in copies.items():
        lst.sort(key=_rank)
        if dup == DUP_RANK:
            idx[key] = lst[0]
    idx.copies = copies
    return idx


def registry_textures(root):
    """(имя, package) всех Texture2D под root в ПОРЯДКЕ РЕЕСТРА UE (тот же вызов, что у прежних копий)."""
    import unreal
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    out = []
    for ad in ar.get_assets_by_path(unreal.Name(root), recursive=True):
        if str(ad.asset_class_path.asset_name) == "Texture2D":
            out.append((str(ad.asset_name), str(ad.package_name)))
    return out


def tex_index(preset=None, *, root=None, skip=None, dup=None):
    """Индекс Texture2D проекта {имя.lower(): package_name} (нужен UE).

    preset — имя из INDEX_PRESETS; явные root/skip/dup перекрывают пресет. Без пресета: all_last.
    """
    p = dict(INDEX_PRESETS[preset or "all_last"])
    if root is not None: p["root"] = root
    if skip is not None: p["skip"] = tuple(skip)
    if dup is not None: p["dup"] = dup
    return build_index(registry_textures(p["root"]), skip=p["skip"], dup=p["dup"])


# ═══════════════════════════════════════════════════════════════════════════════════════════════
# 2. ПОИСК ТЕКСТУРЫ ДЛЯ СЛОТА / MIC
# ═══════════════════════════════════════════════════════════════════════════════════════════════

# Формула имени текстуры причёски (эталон entity_char.c:11535, CCharacter_LoadHairModel@0x9E2700):
#   sprintf("ch1_%c_00_hair00%d_00_0%d_0.TEX", style+1, color) — «hair00» + десятичное без нулей.
#   Строка материала внутри .jbm причёсок 011..025 указывает на несуществующее имя — поэтому фолбэк.
import re as _re

_HAIR_RE = _re.compile(r'^(ch1_[mf]_\d+_)hair(\d+)_(\d+)_(\d+)_(\d+)$')

# Хвостовой фолбэк: модель без суффикса слота → имя атласа «<имя>_00_00_0».
TAIL_SUFFIX = "_00_00_0"


def hair_slot_to_tex(k):
    """Имя слота причёски (lower) → каноническое имя текстуры по формуле эталона или None."""
    m = _HAIR_RE.match(k)
    if not m:
        return None
    pref, style, mid, color, tail = m.groups()
    return "%shair00%d_%s_%02d_%s" % (pref, int(style), mid, int(color), tail)


def normalize_slot(name, mode):
    """Имя слота/MIC → ключ поиска текстуры (до lower()).

    mode None     — как есть;
    mode "blend"  — снять метку `_blend` (asda_material_policy.strip_blend);
    mode "class"  — снять метки класса `_blend`/`_msk` (asda_mesh_render_class.strip_class).
    """
    if mode == "blend":
        return POL.strip_blend(name)
    if mode == "class":
        import asda_mesh_render_class as _MC   # лениво: тянет asda_jbm
        return _MC.strip_class(name)
    return name


# ★ Пресеты поиска ровно по прежним копиям find_tex (имя пресета → параметры find_tex):
#   normalize  — см. normalize_slot;
#   hair       — после точного совпадения пробовать формулу причёски;
#   dunder     — хранители: «Sb_ Onehand» → конвертер даёт «sb__onehand», пробовать «__»→«_»;
#   digits     — "loop": срезать хвостовые цифры по одной (хвост Interchange / мультислот),
#                "all": одно обращение с полностью срезанными цифрами (asda_build_map_anim),
#                None: не срезать;
#   tail       — пробовать «<ключ>_00_00_0» в конце (True) или вернуть None (False).
FIND_PRESETS = {
    # asda_diag_materials, asda_import_pet_models, import_char_cremmy, import_equip_parts
    "plain":        dict(normalize=None,    hair=False, dunder=False, digits="loop", tail=True),
    # asda_import_mob_skel
    "plain_notail": dict(normalize=None,    hair=False, dunder=False, digits="loop", tail=False),
    # asda_apply_blend_groups, asda_import_npc, asda_import_skinned_models
    "blend":        dict(normalize="blend", hair=False, dunder=False, digits="loop", tail=True),
    # asda_fix_model_materials
    "blend_hair":   dict(normalize="blend", hair=True,  dunder=False, digits="loop", tail=True),
    # asda_assign_materials
    "plain_hair":   dict(normalize=None,    hair=True,  dunder=False, digits="loop", tail=True),
    # asda_fix_npc_materials
    "class":        dict(normalize="class", hair=False, dunder=False, digits="loop", tail=True),
    # asda_reimport_npc_mob_split
    "class_notail": dict(normalize="class", hair=False, dunder=False, digits="loop", tail=False),
    # asda_import_soulguard_models
    "soulguard":    dict(normalize=None,    hair=False, dunder=True,  digits="loop", tail=True),
    # asda_fix_hair_materials (resolve_tex): точное → формула причёски → None
    "hair_only":    dict(normalize=None,    hair=True,  dunder=False, digits=None,   tail=False),
    # asda_build_map_anim: точное → без хвостовых цифр (разом)
    "map_anim":     dict(normalize=None,    hair=False, dunder=False, digits="all",  tail=False),
}


# ★★★ СНЯТИЕ МЕТОК КЛАССА ВО ВСЕХ ПРЕСЕТАХ (решение координатора волны 2, 14.09.2026).
#   Слоты/MIC с метками `_blend`/`_msk` носят ТУ ЖЕ текстуру, что и соседи (метка только разводит мастера).
#   Пресеты без `normalize="class"` таких имён не находили → MIC оставался без Tex при переподвязке.
#   Правило ДОБАВОЧНОЕ: сначала поиск ровно по пресету (как прежняя копия), и ТОЛЬКО если он дал None и
#   в имени есть метка — повтор с `normalize="class"`. Поэтому там, где текстура находилась, результат
#   прежний по построению (доказано стендом, `Reports/dedup_audit/wave2_import.md` §8).
#   Выключить для конкретного вызова: find_tex(..., strip_marks=False).
MARKS = ("_blend", "_msk")


def tex_lookup(tex, key, kind=None):
    """Package path копии имени `key` (lower) с учётом типа модели или None (★ 15.09.2026, правило в шапке).
    kind задан и среди `tex.copies[key]` есть копия из папки своего типа (`kind_dirs`) — она (при нескольких —
    с наименьшим `_rank`); иначе значение индекса `tex[key]` (копия с наименьшим рангом при DUP_RANK).
    Обычный dict без `copies` — просто `tex.get(key)`."""
    if key not in tex:
        return None
    dirs = kind_dirs(kind)
    copies = getattr(tex, "copies", None)
    if dirs and copies:
        for pkg in copies.get(key, ()):           # уже отсортированы по _rank
            low = pkg.lower()
            if any(d.lower() in low for d in dirs):
                return pkg
    return tex[key]


def _find_once(name, tex, p, kind=None):
    """Один проход поиска по параметрам p (без добавочного снятия меток); копия имени — через tex_lookup(kind)."""
    k = normalize_slot(name, p["normalize"]).lower()
    if k in tex:
        return tex_lookup(tex, k, kind)
    if p["dunder"]:
        d = k.replace("__", "_")
        if d in tex:
            return tex_lookup(tex, d, kind)
    if p["hair"]:
        hk = hair_slot_to_tex(k)
        if hk and hk in tex:
            return tex_lookup(tex, hk, kind)
    if p["digits"] == "loop":
        s = k
        while s and s[-1].isdigit():
            s = s[:-1]
            if s in tex:
                return tex_lookup(tex, s, kind)
            if p["dunder"] and s.replace("__", "_") in tex:
                return tex_lookup(tex, s.replace("__", "_"), kind)
    elif p["digits"] == "all":
        r = tex_lookup(tex, k.rstrip("0123456789"), kind)
        if r:
            return r
    if p["tail"]:
        return tex_lookup(tex, k + TAIL_SUFFIX, kind)
    return None


def has_mark(name):
    """True, если в имени слота/MIC есть метка класса (`_blend`/`_msk`, в т.ч. с хвостом Interchange)."""
    low = str(name).lower()
    return any(m in low for m in MARKS)


def find_tex(name, tex, preset="blend", strip_marks=True, npc_atlas=True, kind=None, **over):
    """Package path текстуры для слота/MIC `name` по индексу `tex` или None.

    preset — имя из FIND_PRESETS (умолчание "blend"); явные параметры (normalize/hair/dunder/digits/tail)
    перекрывают пресет. strip_marks=True — если поиск по пресету ничего не нашёл, а в имени есть метка
    `_blend`/`_msk`, повторить со снятыми метками (добавочное правило, см. MARKS выше).
    npc_atlas=True — выбранный UI-портрет `/NPC/Image/<имя>` заменить атласом `/NPC/Texture/<имя>`, если он есть
    (см. NPC_IMAGE_MARK выше); остальные результаты не меняются.
    kind — тип модели ("npc"/"mob"/"char"/"item"/"map:<карта>", см. `kind_for_package`): при нескольких копиях
    имени побеждает копия из папки своего типа, как в оригинале (★ 15.09.2026, шапка). None — копия с наименьшим рангом.
    """
    p = dict(FIND_PRESETS[preset]); p.update(over)
    r = _find_once(name, tex, p, kind)
    if r is None and strip_marks and p["normalize"] != "class" and has_mark(name):
        q = dict(p); q["normalize"] = "class"
        r = _find_once(name, tex, q, kind)
    if npc_atlas:
        r = npc_image_to_atlas(r, tex)
    return r


def find_tex_candidates(cands, tex, kind=None):
    """Первый найденный среди кандидатов (каждый: точное → срез цифр по одной); иначе None.
    kind — тип модели, как у find_tex. Потребитель: asda_fix_weapon_missing_tex (имя меша и его «n»-вариант)."""
    for cand in cands:
        if cand in tex:
            return npc_image_to_atlas(tex_lookup(tex, cand, kind), tex)     # ★ портрет NPC → атлас (см. NPC_IMAGE_MARK)
        s = cand
        while s and s[-1].isdigit():
            s = s[:-1]
            if s in tex:
                return npc_image_to_atlas(tex_lookup(tex, s, kind), tex)
    return None


# ═══════════════════════════════════════════════════════════════════════════════════════════════
# 3. МАСТЕР МАТЕРИАЛА
# ═══════════════════════════════════════════════════════════════════════════════════════════════

def master_path_for(mic_name):
    """Путь мастера по ИМЕНИ MIC через единую политику (asda_material_policy.master_for).
    Так выбирают asda_fix_npc_materials / asda_import_npc / asda_import_skinned_models."""
    return POL.master_for(mic_name, mic_name)


def policy_master_loader(fallback):
    """Функция «имя MIC → объект мастера» по политике, с запасным мастером при неудачной загрузке
    (ровно `unreal.load_asset(POL.master_for(name, name)) or master` прежних копий)."""
    def pick(name):
        import unreal
        return unreal.load_asset(master_path_for(name)) or fallback
    return pick


# ═══════════════════════════════════════════════════════════════════════════════════════════════
# 4. ОПЕРАЦИИ UE: подвязка MIC, импорт пачкой, Nanite
# ═══════════════════════════════════════════════════════════════════════════════════════════════

def wire_materials(root, *, tex_for, master=None, pick_master=None, ambient=True, on_miss=None):
    """Подвязка всех MIC под root: родитель → Tex → (Ambient=1.0).

    tex_for(name) → package path текстуры или None;
    pick_master(name) → объект мастера (если задан), иначе `master` для всех;
    ambient — писать скаляр Ambient=1.0 (прежние копии для M_AsdaObjectLit/M_AsdaCharLit);
    on_miss(name) — вызывается, когда текстура не найдена.
    Возвращает dict(n_all, n_tex, n_miss, per_master) — обёртки потребителей берут нужные поля.
    Порядок записей (родитель, затем Tex, затем Ambient) — как у всех прежних копий.
    """
    import unreal
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    mel = unreal.MaterialEditingLibrary
    st = dict(n_all=0, n_tex=0, n_miss=0, per_master={})
    st["n_login_skip"] = 0   # ★ 15.09.2026: пропущено MIC сцены логина (asda_login_guard.login_package_blocked)
    import asda_login_guard as _LG
    for ad in ar.get_assets_by_path(unreal.Name(root), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        if _LG.login_package_blocked(str(ad.package_name), "wire_materials", quiet=True):
            st["n_login_skip"] += 1   # ★ сцена логина: MIC не трогаем без ASDA_ALLOW_LOGIN_WRITE=1
            continue
        a = unreal.load_asset(str(ad.package_name))
        name = str(ad.asset_name)
        tp = tex_for(name)
        if data_pinned_dir(str(ad.package_name)):   # ★ 15.09: текстура по данным (DATA_PINNED_TEX) — текущую не менять
            tp = data_pinned_tex(str(ad.package_name), _mic_tex_path(a))
        m = pick_master(name) if pick_master else master
        mel.set_material_instance_parent(a, m)
        mn = m.get_name()
        st["per_master"][mn] = st["per_master"].get(mn, 0) + 1
        if tp:
            mel.set_material_instance_texture_parameter_value(a, "Tex", unreal.load_asset(tp))
            st["n_tex"] += 1
        else:
            st["n_miss"] += 1
            if on_miss:
                on_miss(name)
        if ambient:
            mel.set_material_instance_scalar_parameter_value(a, "Ambient", 1.0)
        st["n_all"] += 1
    if st["n_login_skip"]:
        unreal.log_warning("[LOGIN-GUARD] wire_materials: под %s пропущено MIC сцены логина: %d (запись — только ASDA_ALLOW_LOGIN_WRITE=1)" % (root, st["n_login_skip"]))
    return st


def per_master_line(per_master):
    """Строка лога «мастера по MIC: A=n, B=m» (как печатали прежние копии)."""
    return "  мастера по MIC: %s" % ", ".join("%s=%d" % kv for kv in sorted(per_master.items()))


def static_meshes_in_packages(pkgs):
    """AssetData StaticMesh из списка пакетов (реестр UE; отбор по классу — как у C++-обхода по папкам).
    Одна реализация (дедуп №2 В1 Т19, 11.10.2026): было 2 копии `_loose_static_meshes` —
    asda_clear_fastbuild_flag.py и asda_refresh_mesh_ddc_key.py."""
    import unreal
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    out = []
    for pkg in pkgs:
        for ad in ar.get_assets_by_package_name(unreal.Name(pkg)):
            if str(ad.asset_class_path.asset_name) == "StaticMesh":
                out.append(ad)
    return out


def find_texture_in_dir(root, stem):
    """Текстура по имени `stem` (без учёта регистра) среди ассетов папки `<root>/Texture` (не рекурсивно), загруженная;
    папки нет или имени нет — None. Первое совпадение в порядке `list_assets` побеждает.
    Одна реализация поиска текстуры купола неба и теней облаков (дедуп №2 В1 Т13, 11.10.2026): была ×3 —
    `asda_build_sky_dome.find_texture`, `asda_add_cloud_shadows.find_texture`, `asda_place_cloud_shadow_actors.find_texture`.
    ★ Корень — `/Game/Map/<карта>`, и он ВЕРНЫЙ (проверено в В2 дедупа №2, 11.10.2026, офлайн по Content): текстуры
      неба SKY_DOME / SKY_DOME_CLOUDE1 / SKY_DOME_CLOUDE2 у всех 18 карт с куполом лежат в `/Game/Map/<карта>/Texture`
      НАСТОЯЩИМИ ассетами (не редиректорами), а в `asda_ue_paths.map_tex_dir(<карта>)/Texture` их нет ни у одной.
      Перенос 17.08 (`asda_build_map_level.py:192-200`) увёл туда текстуры террейна/моделей (1 297 из 4 326 файлов старых
      папок), но не небо. Поэтому предложенный аудитом перевод корня на `map_tex_dir` НЕ делается — он сломал бы поиск на
      всех картах. «Fix Up Redirectors» небу не опасен (редиректоров неба нет)."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    d = root + "/Texture"
    if not EAL.does_directory_exist(d): return None
    for p in EAL.list_assets(d, recursive=False):
        nm = p.split("/")[-1].split(".")[0]
        if nm.lower() == stem.lower(): return EAL.load_asset(p.split(".")[0])
    return None


def import_batch(paths, dest, replace=True, skip_read_only=None, keep_normal_detect=False):
    """Импорт файлов (.glb/.png) пачкой через AutomatedAssetImportData; возвращает список ассетов.
    skip_read_only — None: поле не задаётся (умолчание движка); True/False — задаётся последним, как у прежних копий
    asda_import_textures / asda_build_map_level (дедуп №2 В1 Т17, 11.10.2026: блок жил ~15 копиями).
    ★ ПРОВЕРЕНО В UE 5.8 (11.10.2026, `Reports/dedup_audit2/wave2_T_followup.md` §П.1, песочница + переимпорт в
      свежем процессе): картинки этот путь ведёт через Interchange, и при повторном импорте поверх СУЩЕСТВУЮЩЕЙ текстуры
      ВСЕ её настройки сохраняются (0 изменённых свойств; ручные лод-группа/сжатие/мипы/фильтр/адресация/стриминг/sRGB —
      8 из 8). Создание новой — свойства и пиксели те же, что у `import_textures_keep_settings` (19 файлов: PNG RGB/RGBA,
      TGA, BMP, NPOT, 16×16…2048²). Прежнее предупреждение «перетирает настройки» относилось к legacy-фабрике и здесь
      неверно. Единственный провал — JPEG `000_basic.jpg` (InterchangeJPGTranslator «не удалось декодировать»).
    ★ 05.11.2026 (bunny-hood) [ИСПРАВЛЕНИЕ]: НОВАЯ текстура этим путём проходит автоопределение карты нормалей UE —
      синеватую/сиреневую картинку движок помечает TC_NORMALMAP + sRGB выкл. + WORLD_NORMAL_MAP, BC5 хранит лишь R/G,
      и цвет в игре искажается (сиреневый капюшон 328 стал жёлтым; найдено 89 текстур персонажей/предметов/NPC).
      В данных оригинала карт нормалей нет (все .TEX цветные), поэтому вновь созданной текстуре с TC_NORMALMAP
      возвращаются TC_DEFAULT, sRGB и группа TEXTUREGROUP_WORLD (_fix_new_normal_autodetect). Существующие ассеты не
      трогаются. Настоящие нормали (NormalStudio, asda_nb_build_mats) выставляют сжатие сами после импорта —
      им передавать keep_normal_detect=True не требуется, но этот ключ возвращает прежнее поведение."""
    import unreal
    # Имена ассетов, которые уже были в папке до импорта (их настройки не меняем).
    before = set(str(p) for p in (unreal.EditorAssetLibrary.list_assets(dest, recursive=False) or [])) \
        if not keep_normal_detect else set()
    data = unreal.AutomatedAssetImportData()
    data.set_editor_property("destination_path", dest)
    data.set_editor_property("filenames", paths)
    data.set_editor_property("replace_existing", replace)
    if skip_read_only is not None:
        data.set_editor_property("skip_read_only", skip_read_only)
    result = unreal.AssetToolsHelpers.get_asset_tools().import_assets_automated(data) or []
    if not keep_normal_detect:
        _fix_new_normal_autodetect(result, before)
    return result


def _fix_new_normal_autodetect(assets, existed_before):
    """★ 05.11.2026 (bunny-hood) [ИСПРАВЛЕНИЕ]: снять с ВНОВЬ созданных текстур ошибочное автоопределение карты нормалей
    (см. import_batch). assets — результат импорта; existed_before — пути ассетов папки до импорта. Возвращает число
    исправленных текстур; исправленные сохраняются сразу (вызывающий мог сохранить их раньше этой правки)."""
    import unreal
    fixed = 0   # число исправленных текстур
    for a in assets:
        if not isinstance(a, unreal.Texture2D):
            continue
        path = a.get_path_name()   # полный путь объекта «/Game/…/Имя.Имя»
        if any(path.startswith(p.split(".")[0] + ".") or path == p for p in existed_before):
            continue   # ассет существовал — его настройки не трогаем
        if a.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_NORMALMAP:
            continue
        a.modify()
        a.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        a.set_editor_property("srgb", True)
        a.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)   # set_editor_property сам вызывает PostEditChange
        unreal.EditorAssetLibrary.save_loaded_asset(a, False)
        unreal.log_warning("[import] автоопределение нормали снято: %s (цветная текстура оригинала)" % path)
        fixed += 1
    return fixed


def import_textures_keep_settings(paths, dest, save=False):
    """★ БЕЗОПАСНЫЙ импорт картинок (PNG/TGA/…) → Texture2D: настройки СУЩЕСТВУЮЩИХ ассетов сохраняются.

    Дедуп №2 В2 Т17. Тело — дословно путь `import_jui_textures.py` (06.10.2026, `Reports/owner_1006/
    texture_importers_fix.md`), проверенный там живыми прогонами на 2 201 текстуре:
      unreal.AssetImportTask на каждый файл: automated, replace_existing=True, ★ replace_existing_settings=False,
      save=`save`, явная фабрика TextureFactory (legacy-путь без Interchange, AssetTools.cpp:3566-3569).
    Почему безопасно: при replace_existing_settings=False движок у СУЩЕСТВУЮЩЕЙ текстуры переносит её настройки
      (EditorFactories.cpp:3880, 4125-4151 — группа, сжатие, sRGB, мипы, never_stream и др.) и не запускает
      автоопределение карты нормалей (EditorFactories.cpp:4162). Прежний `import_batch` (AutomatedAssetImportData +
      replace_existing=True) отвечает «YesAll» и перезаписывает их (Factory.cpp:389-391, EditorFactories.cpp:3894-3905).
    Новым ассетам настройки выставляет вызывающий (как import_jui_textures). Возврат — список задач (у каждой
      `imported_object_paths`), порядок = порядок `paths`.
    ⚠ ПРОВЕРЕНО 11.10.2026 (`Reports/dedup_audit2/wave2_T_followup.md` §П.1): при повторном импорте поверх
      существующей текстуры этот путь сохраняет ручные настройки (8 из 8), НО меняет два свойства, которых никто не
      просил: `composite_texture_mode` CTM_DISABLED → CTM_NORMAL_ROUGHNESS_TO_ALPHA и `normalize_normals` True → False
      (так в песочнице и в свежем процессе с диска). `import_batch` (Interchange) не меняет ничего. Поэтому прочие
      импортёры на эту функцию НЕ переведены; её плюс — читает JPEG, который Interchange не декодирует."""
    import unreal
    tasks = []
    for f in paths:
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", f)
        t.set_editor_property("destination_path", dest)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("replace_existing_settings", False)   # ★ настройки существующих сохраняются
        t.set_editor_property("save", save)
        t.set_editor_property("factory", unreal.TextureFactory())    # ★ legacy-путь (EditorFactories.cpp), без Interchange
        tasks.append(t)
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    return tasks


def ensure_png_texture(dest, src_dir, stem):
    """Текстура `dest`/`stem` есть — True; нет — импорт `src_dir`/`stem`.png (import_batch) с сохранением ассетов;
    PNG нет — предупреждение «Нет PNG текстуры» и False. Итог — есть ли ассет после импорта.
    Одна реализация ensure_texture сборщиков эффектов выбора персонажа (дедуп №2 В1 Т17: точные копии в
    asda_place_charselect_effect.py и asda_place_charselect_glyph.py)."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    tp = "%s/%s" % (dest, stem)
    if EAL.does_asset_exist(tp): return True
    src = _os.path.join(src_dir, stem + ".png")
    if not _os.path.isfile(src):
        unreal.log_warning("Нет PNG текстуры: %s" % src); return False
    for a in import_batch([src], dest):
        try: EAL.save_loaded_asset(a)
        except Exception: pass
    return EAL.does_asset_exist(tp)


# Классы мешей, с которых по умолчанию снимается Nanite (у import_equip_parts — только SkeletalMesh).
NANITE_CLASSES = ("StaticMesh", "SkeletalMesh")


def disable_nanite(root, classes=NANITE_CLASSES):
    """Снять Nanite со всех мешей классов `classes` под root (низкополигональный контент 2008)."""
    import unreal
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    import asda_login_guard as _LG   # ★ 15.09.2026: меши сцены логина (Models/Backdrop, Char/cre_creback-a_00, cre_creation01*) — только с разрешением
    n_login = 0
    for ad in ar.get_assets_by_path(unreal.Name(root), recursive=True):
        if str(ad.asset_class_path.asset_name) in classes:
            if _LG.login_package_blocked(str(ad.package_name), "disable_nanite", quiet=True):   # ★ 15.09.2026: сцена логина
                n_login += 1
                continue
            m = unreal.load_asset(str(ad.package_name))
            try:
                ns = m.get_editor_property("nanite_settings")
                if ns and ns.get_editor_property("enabled"):
                    ns.set_editor_property("enabled", False)
                    m.set_editor_property("nanite_settings", ns)
            except Exception:
                pass
    if n_login:   # ★ 15.09.2026
        unreal.log_warning("[LOGIN-GUARD] disable_nanite: пропущено мешей сцены логина под %s: %d (ASDA_ALLOW_LOGIN_WRITE=1)" % (root, n_login))
