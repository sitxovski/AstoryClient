# Copyright (c) 2026 @sitxovski (Discord)
# asda_ue_paths.py — ЕДИНЫЙ реестр путей UE-проекта: каталоги на диске + /Game-префиксы ассетов.
#
# ЗАЧЕМ: ~30 объявлений одних и тех же префиксов по тулам. Звук раздвоен ПО НАЗНАЧЕНИЮ (не случайно):
#   /Game/AsdaAssets/Sound/...        — SFX + DT_SoundTable (дефолт ResolveSound)
#   /Game/AsdaAssets/Sounds/Music|UI  — музыка (PlayMusic) и служебный клик UMG
# Дубли (Sound/BGM, Sounds/SFX) убраны 2026-08-02 — см. блок «ЗВУК — КАНОН РАСКЛАДКИ» ниже.
#
# ★ Модуль БЕЗ побочных эффектов при импорте (правило tools-no-import-side-effects).

import os
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asda_paths as _P
import asda_maps as _MAPS      # ★ map_asset_name живёт ТАМ (здесь — реэкспорт, не копия)

# ---------------------------------------------------------------- каталоги проекта на диске
PROJECT     = os.path.join(_P.ROOT, "AstoryClient")
UPROJECT    = os.path.join(PROJECT, "AsdaReborn.uproject")
CONTENT     = os.path.join(PROJECT, "Content")
RAW_ASSETS  = os.path.join(PROJECT, "RawAssets")     # входные png/fbx/csv для импорта
TOOLS       = os.path.join(PROJECT, "Tools")
TOOLS_OUT   = os.path.join(TOOLS, "_out")
SOURCE      = os.path.join(PROJECT, "Source")
SAVED_LOGS  = os.path.join(PROJECT, "Saved", "Logs")

RAW_TEXTURES = os.path.join(RAW_ASSETS, "Textures")
RAW_MODELS   = os.path.join(RAW_ASSETS, "Models")
RAW_TEXT     = os.path.join(RAW_ASSETS, "Text")
RAW_FONTS    = os.path.join(RAW_ASSETS, "Fonts")
RAW_SOUNDS   = os.path.join(RAW_ASSETS, "Sounds")

CONTENT_ASSETS = os.path.join(CONTENT, "AsdaAssets")
# ★★★ 26.08.2026: В `Content/AsdaAssets/Data` ЛЕЖАТ ТОЛЬКО `.uasset`. Раньше туда клали сырые
#     клиентские `.bin/.BIN/.sif/.txt` — и исходники импортёров, и три файла, которые рантайм
#     читал с диска напрямую. Всё это переехало в `RawAssets/Data/` (см. `DATA_SRC` ниже),
#     а данные живут в DataTable. Отчёт: `Reports/raw_data_to_datatables.md`.
#   ПОЧЕМУ ТАК: сырой файл в `Content` — не ассет. Его не видит браузер редактора, его не
#     забирает упаковщик (в собранной игре его просто НЕТ), и никакой референс на него не ведёт.
CONTENT_DATA   = os.path.join(CONTENT_ASSETS, "Data")   # ← только DT_*.uasset и прочие ассеты

# ★★★ МИНА: любой .csv/.json ВНУТРИ Content редактор авто-импортирует и заводит из него
#     АССЕТ-ДВОЙНИК с той же базой имени, но другим составом полей (если файл не «занят»
#     чьим-то AssetImportData). Двойник живёт рядом с настоящим DT_* и рано или поздно
#     расходится с ним по данным — 20.08.2026 два таких двойника держали 0 строк вместо
#     38 917 и 25 220. Разбор: Reports/content_intermediate_cleanup.md.
# ⇒ ПРОМЕЖУТОЧНЫЕ .csv/.json генераторов таблиц пишем СЮДА, вне Content:
DATA_SRC        = os.path.join(RAW_ASSETS, "Data")          # RawAssets/Data — корневые таблицы
DATA_SRC_ITEMDB = os.path.join(DATA_SRC, "ItemDB")          # 53 таблицы item-БД (decode_item_db)
DATA_SRC_NPC    = os.path.join(DATA_SRC, "NPC")             # таблицы диалогов/квестов NPC
# ★ ИСКЛЮЧЕНИЙ БОЛЬШЕ НЕТ. До 26.08.2026 таким исключением был
# `Content/AsdaAssets/Data/MessageRoutes.csv` — его читал фолбэк рантайма. Фолбэк убран
# (AsdaMessageSubsystem читает только DT_MessageRoutes), а сам CSV переехал в RawAssets/Data/.
DATA_SRC_CHAR   = os.path.join(DATA_SRC, "Character")       # сырые .bin клиента (Data/Character оригинала)
DATA_SRC_MOB    = os.path.join(DATA_SRC, "Mob")             # Mob_list.bin

# ---------------------------------------------------------------- /Game-префиксы (пакеты UE)
# ★ 14.09.2026 (волна 2 п. 7): константы G_*/M_* и функции правил пути ГЕНЕРИРУЮТСЯ — блок «★ GENERATED asda_asset_paths»
#   ниже (источник Tools/data/asda_asset_paths.json, генератор Tools/asda_asset_paths.py, двойник C++ —
#   Source/AsdaReborn/Public/Resources/AsdaAssetPaths.h). Имена и значения прежние. Комментарии-каноны — здесь, руками.

# ЗВУК — КАНОН РАСКЛАДКИ (утверждён 2026-08-02, отчёт Reports/sound_layout_cleanup.md).
#   G_SOUND        — SFX (1462 wav клиента 1:1) + DT_SoundTable (SFT.BIN, 216 id).
#                    ResolveSound(Name, Folder=TEXT("Sound")) — дефолт подсистемы (SoundSubsystem.h:101).
#   G_SOUNDS_MUSIC — МУЗЫКА (стрим, 20 треков). PlayMusic → ResolveSound(Track, TEXT("Sounds/Music"))
#                    (AsdaSoundSubsystem.cpp:172) — ЕДИНСТВЕННЫЙ путь музыки в рантайме.
#   G_SOUNDS_UI    — служебный клик генератора UMG (S_e_button09), жёсткая ссылка из WBP.
# Разделение повторяет ОРИГИНАЛ: у него один каталог Data/Sound, но ДВА устройства FMOD —
#   sfxDevice (1462 .wav, id-таблица SFT.BIN) и musicDevice (19 .mp3, CSoundStream).
# ★ Удалено 2026-08-02 (дубли, ссылок не было): G_SOUND+"/BGM" (19 треков ⊂ Music, FileMD5 совпал 19/19)
#   и G_SOUNDS_ALT+"/SFX" (1462 ассета, FileMD5 совпал 1462/1462 с G_SOUND). Возврата не предполагается.
#
# ★ ХРАНИТЕЛЬ (Soul Guardian / «ангел»): 9 скиннутых моделей sb_md_{oh,cb,am}_0{1,2,3} — G_MODELS_SOULGUARD.
#   ОТДЕЛЬНАЯ ветка от G_MODELS_CHAR/sb_md_* — там лежат СТАТИК-меши с теми же именами (импорт
#   до реверса компаньона). Разные каталоги, чтобы импорт скелеталов не наткнулся на дубль имени.
# Материалы/эффекты: мастера в /Game/Materials и /Game/Effects, вне AsdaAssets.

# ── ★ GENERATED asda_asset_paths BEGIN: генерирует Tools/asda_asset_paths.py из Tools/data/asda_asset_paths.json — НЕ ПРАВИТЬ РУКАМИ ──
# Корни /Game и одиночные ассеты (имена и значения — прежние) + функции правил пути (двойники C++
#   Source/AsdaReborn/Public/Resources/AsdaAssetPaths.h). Правка — Tools/data/asda_asset_paths.json.
import json as _json
import asda_map_registry as _MAPREG   # ★ приведение имени карты/звука — реестр карт (cpp_asset_name), не копия

G_ASSETS = "/Game/AsdaAssets"             # корень ассетов порта
G_DATA = G_ASSETS + "/Data"
G_TEXTURES = G_ASSETS + "/Textures"
G_MODELS = G_ASSETS + "/Models"
G_EFFECTS = G_ASSETS + "/Effects"         # эффект-BP BP_Fx_<jeb>
G_UI = G_ASSETS + "/UI"
G_TEXT = G_ASSETS + "/Text"
G_SOUND = G_ASSETS + "/Sound"             # SFX + DT_SoundTable (дефолтная папка ResolveSound)
G_SOUNDS_ALT = G_ASSETS + "/Sounds"
G_SOUNDS_MUSIC = G_SOUNDS_ALT + "/Music"  # МУЗЫКА (канон): PlayMusic читает отсюда
G_SOUNDS_UI = G_SOUNDS_ALT + "/UI"        # S_e_button09, используется генератором UMG
G_MODELS_CHAR = G_MODELS + "/Char"        # жёсткие меши персонажа <m>/StaticMeshes/<m>
G_MODELS_ITEMS = G_MODELS + "/Items"      # модели лута <m>/StaticMeshes/<m>
G_MODELS_NPC = G_MODELS + "/NPC"          # скелеталы NPC
G_MODELS_SKELETAL = G_MODELS + "/Skeletal"
G_MODELS_EQUIPPARTS = G_MODELS_SKELETAL + "/EquipParts"  # части экипировки <m>/<m> (без SkeletalMeshes/)
G_MODELS_VEHICLE = G_MODELS_SKELETAL + "/Vehicle"  # транспорт
G_MODELS_MOBSKEL = G_MODELS + "/MobSkel"  # скелеталы мобов
G_MODELS_MOB = G_MODELS + "/Mob"          # статик-модели мобов (фолбэк к MobSkel)
G_MODELS_PET = G_MODELS_SKELETAL + "/Pet"  # питомцы
G_MODELS_SOULGUARD = G_MODELS_SKELETAL + "/SoulGuard"  # хранитель: скиннутые sb_md_* (статик-двойники — в Models/Char)
G_MODELS_CLANFLAG = G_MODELS_SKELETAL + "/ClanFlag"  # флаг клана
G_MODELS_FISHING = G_MODELS_SKELETAL + "/Fishing"  # рыбалка: зона fs_zone, удочки fird_*, рыбы fs_fsm_*/fs_stuff_00 (волна В5а 08.10.2026)
G_MODELS_CHARACTERS = G_MODELS_SKELETAL + "/Characters"  # база персонажа ch1_<g>_cremmy_preview + клипы
G_MODELS_GACHYA = G_MODELS_SKELETAL + "/Gachya"  # автоматы премиум-гачи (новая константа, 14.09)
G_TEX_JUI = G_TEXTURES + "/JUI"           # UI-атласы JUI
G_TEX_NPC = G_TEXTURES + "/NPC"
G_TEX_NPC_TEX = G_TEX_NPC + "/Texture"
G_TEX_NPC_IMAGE = G_TEX_NPC + "/Image"    # текстуры названий локаций (эталон Data/NPC/Image)
G_TEX_NPC_PORTRAIT = G_TEX_NPC + "/Portrait"  # портреты NPC и текстуры названий
G_TEX_CHARACTERS = G_TEXTURES + "/Characters"
G_TEX_CHAR_TEXTURE = G_TEX_CHARACTERS + "/Texture"  # текстуры моделей персонажа
G_TEX_ITEMS = G_TEXTURES + "/Items"
G_TEX_ITEMS_TEXTURE = G_TEX_ITEMS + "/Texture"  # текстуры моделей предметов (лут)
G_TEX_MAPS = G_TEXTURES + "/Maps"         # текстуры карт, метки мини-карты, картинки загрузки
G_TEX_MAPIMAGE = G_TEXTURES + "/MapImage"  # подложки мини-/большой карты
# ★ УСТАРЕЛО (D5, волна 2 п. 7): папка есть, ассетов 0 (сверка 14.09); пишет туда только asda_assign_materials.py
G_MATERIALS_MODELS = G_ASSETS + "/Materials/Models"  # MIC-и моделей (asda_assign_materials)
G_MATERIALS = "/Game/Materials"           # мастера материалов (вне AsdaAssets)
M_OBJECT_LIT = G_MATERIALS + "/M_AsdaObjectLit"
M_OBJECT = G_MATERIALS + "/M_AsdaObject"
M_CHAR_LIT = G_MATERIALS + "/M_AsdaCharLit"
# ★ УСТАРЕЛО (D5, волна 2 п. 7): ассета нет в Content, потребителей константы нет (asda_build_questmark_glow.py:99 помечает OBSOLETE)
M_GLOW_TINT = G_MATERIALS + "/M_AsdaMeshGlowTint"
G_FX = "/Game/Effects"                    # мастера эффектов
M_FX_ADD = G_FX + "/M_AsdaEffectAdd"
M_FX_ALPHA = G_FX + "/M_AsdaEffectAlpha"
G_MAP = "/Game/Map"                       # уровни карт
G_MAP_LOGIN = G_MAP + "/Login"            # ★ логин: уровень Login.umap — только константа, свет владельца не трогать


def equip_part_mesh(mesh):
    """Пакетный путь скелетной части экипировки: EquipParts/<m>/<m> (ключ менеджера моделей).
    Двойник C++ AsdaAssetPaths::EquipPartMesh."""
    return "/Game/AsdaAssets/Models/Skeletal/EquipParts/%s/%s" % (mesh, mesh,)


def char_static_mesh_object(mesh):
    """Объектный путь жёсткого меша персонажа (причёска/оружие): Char/<m>/StaticMeshes/<m>.<m>.
    Двойник C++ AsdaAssetPaths::CharStaticMeshObject."""
    return "/Game/AsdaAssets/Models/Char/%s/StaticMeshes/%s.%s" % (mesh, mesh, mesh,)


def char_texture_object(tex):
    """Объектный путь текстуры модели персонажа: Textures/Characters/Texture/<t>.<t>.
    Двойник C++ AsdaAssetPaths::CharTextureObject."""
    return "/Game/AsdaAssets/Textures/Characters/Texture/%s.%s" % (tex, tex,)


def face_texture_name(male, face_index):
    """Имя текстуры лица (эталон SetParts_FaceTexture): ch1_<m|f>_00_face001_NN_00_0, NN — 2 знака.
    Двойник C++ AsdaAssetPaths::FaceTextureName."""
    return "ch1_%s_00_face001_%02d_00_0" % (("m" if male else "f"), face_index,)


def skel_mesh(root, name, suffix=""):
    """Пакетный путь скелетала/клипа: <корень>/<имя>/SkeletalMeshes/<имя><суффикс> (корень — k*SkelRoot*).
    Двойник C++ AsdaAssetPaths::SkelMesh."""
    return "%s/%s/SkeletalMeshes/%s%s" % (root, name, name, suffix,)


def skel_anim_suffix(anim_idx):
    """Суффикс клипа: anim_NN (NN — не меньше 2 знаков).
    Двойник C++ AsdaAssetPaths::SkelAnimSuffix."""
    return "anim_%02d" % (anim_idx,)


def skel_anim(root, name, anim_idx):
    """Пакетный путь клипа: <корень>/<имя>/SkeletalMeshes/<имя>anim_NN.
    Двойник C++ AsdaAssetPaths::SkelAnim."""
    suffix = skel_anim_suffix(anim_idx)
    return "%s/%s/SkeletalMeshes/%s%s" % (root, name, name, suffix,)


def char_preview_name(male):
    """Имя базы персонажа: ch1_<m|f>_cremmy_preview.
    Двойник C++ AsdaAssetPaths::CharPreviewName."""
    return "ch1_%s_cremmy_preview" % (("m" if male else "f"),)


def mob_static_mesh_object(model):
    """Объектный путь статик-модели моба (фолбэк к скелеталу): Mob/<m>/StaticMeshes/<m>.<m>.
    Двойник C++ AsdaAssetPaths::MobStaticMeshObject."""
    return "/Game/AsdaAssets/Models/Mob/%s/StaticMeshes/%s.%s" % (model, model, model,)


def loot_mesh_object(name):
    """Объектный путь модели лута: Items/<m>/StaticMeshes/<m>.<m>.
    Двойник C++ AsdaAssetPaths::LootMeshObject."""
    return "/Game/AsdaAssets/Models/Items/%s/StaticMeshes/%s.%s" % (name, name, name,)


def loot_texture_object(tex):
    """Объектный путь текстуры лута: Textures/Items/Texture/<t>.<t>.
    Двойник C++ AsdaAssetPaths::LootTextureObject."""
    return "/Game/AsdaAssets/Textures/Items/Texture/%s.%s" % (tex, tex,)


def fx_bp_name(jeb):
    """Имя эффект-BP по базе .jeb: BP_Fx_<jeb с «-»→«_»> (дефис в имени .jeb на диске сохраняется).
    Двойник C++ AsdaAssetPaths::FxBpName."""
    jeb_us = jeb.replace("-", "_")
    return "BP_Fx_%s" % (jeb_us,)


def fx_class_path_from_bp(bp):
    """Путь класса эффект-BP по готовому имени ассета: Effects/<bp>.<bp>_C.
    Двойник C++ AsdaAssetPaths::FxClassPathFromBp."""
    return "/Game/AsdaAssets/Effects/%s.%s_C" % (bp, bp,)


def fx_package_from_bp(bp):
    """Пакет эффект-BP по имени ассета: Effects/<bp>.
    Двойник C++ AsdaAssetPaths::FxPackageFromBp."""
    return "/Game/AsdaAssets/Effects/%s" % (bp,)


def fx_class_path_from_jeb(jeb):
    """Путь класса эффект-BP по базе .jeb: Effects/BP_Fx_<jeb _>.BP_Fx_<jeb _>_C.
    Двойник C++ AsdaAssetPaths::FxClassPathFromJeb."""
    bp = fx_bp_name(jeb)
    return "/Game/AsdaAssets/Effects/%s.%s_C" % (bp, bp,)


_FX_JEB_DASH = None


def _fx_jeb_dash_table():
    """Обратная таблица D4 (лениво, из Tools/data/asda_asset_paths.json): имя без дефиса (нижний регистр) → имя .jeb."""
    global _FX_JEB_DASH
    if _FX_JEB_DASH is None:
        p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "asda_asset_paths.json")
        with open(p, encoding="utf-8") as f:
            names = _json.load(f).get("fx_jeb_dash_names", [])
        _FX_JEB_DASH = {n.replace("-", "_").lower(): n for n in names}
    return _FX_JEB_DASH


def jeb_from_fx_class_name(class_name):
    """Имя класса/ассета «BP_Fx_<jeb с «_»>[_C]» → имя .jeb. ★ ИСПРАВЛЕНИЕ D4: дефис восстанавливается по таблице
    реальных имён (правилом нельзя: 71 имя .jeb без дефиса тоже кончается на «_a_NN»). Регистр префикса/суффикса
    не важен — как FString::RemoveFromStart/RemoveFromEnd (ESearchCase::IgnoreCase)."""
    jeb = class_name
    if jeb.lower().endswith("_c"):
        jeb = jeb[:-2]
    if jeb.lower().startswith("bp_fx_"):
        jeb = jeb[6:]
    return _fx_jeb_dash_table().get(jeb.lower(), jeb)


def sound_object(folder, name):
    """Объектный путь звука: /Game/AsdaAssets/<Folder>/<N>.<N>, N = ToAssetName(имя) (пробел → «_»).
    Двойник C++ AsdaAssetPaths::SoundObject."""
    n = _MAPREG.cpp_asset_name(name)
    return "/Game/AsdaAssets/%s/%s.%s" % (folder, n, n,)


def jui_texture_object(name):
    """Объектный путь UI-атласа JUI: Textures/JUI/<n>.<n> (имя без расширения — срезает вызывающий).
    Двойник C++ AsdaAssetPaths::JuiTextureObject."""
    return "/Game/AsdaAssets/Textures/JUI/%s.%s" % (name, name,)


def map_tex_dir(map_name):
    """Каталог текстур карты: Textures/Maps/<ToAssetName(имя папки карты)> (★ ИСПРАВЛЕНИЕ D2: с приведением имени).
    Двойник C++ AsdaAssetPaths::MapTexDir."""
    a = _MAPREG.cpp_asset_name(map_name)
    return "/Game/AsdaAssets/Textures/Maps/%s" % (a,)


def map_load_dir(dir_name):
    """Каталог картинок загрузки карты: Textures/Maps/<ToAssetName(DirName)>/load (★ ИСПРАВЛЕНИЕ D1: с приведением имени).
    Двойник C++ AsdaAssetPaths::MapLoadDir."""
    a = _MAPREG.cpp_asset_name(dir_name)
    return "/Game/AsdaAssets/Textures/Maps/%s/load" % (a,)


def npc_portrait_object(model):
    """Объектный путь портрета NPC: Textures/NPC/Portrait/<ModelName>.<ModelName>.
    Двойник C++ AsdaAssetPaths::NpcPortraitObject."""
    return "/Game/AsdaAssets/Textures/NPC/Portrait/%s.%s" % (model, model,)


def object_path_from_package(key):
    """Пакетный путь → объектный: «/A/B/N» → «/A/B/N.N»; если у листа уже есть «.» или это не путь — как есть.
    Двойник C++ AsdaAssetPaths::ObjectPathFromPackage (кэши текстур и моделей)."""
    if key.startswith("/") and "/" in key:
        leaf = key[key.rfind("/") + 1:]
        if "." not in leaf:
            return key + "." + leaf
    return key


def package_object_path(pkg):
    """Пакет → «<пакет>.<короткое имя>» без условий (короткое имя — после последнего «/», как FPackageName::GetShortName)."""
    return pkg + "." + pkg[pkg.rfind("/") + 1:]


def widget_class_path(path):
    """Путь WBP → путь класса: «/A/WBP_X» → «/A/WBP_X.WBP_X_C», «/A/WBP_X.WBP_X» → «…_C»; с «_C» — как есть.
    Двойник C++ AsdaAssetPaths::WidgetClassPath (UAsdaWindowSubsystem::OpenWindowByPath)."""
    if not path.lower().endswith("_c"):
        if "." not in path and "/" in path:
            path = path + "." + path[path.rfind("/") + 1:]
        path += "_C"
    return path
# ── ★ GENERATED asda_asset_paths END ──


# ★ 2026-08-02 (верификация рефакторинга): санитайзер имени карты жил ДВАЖДЫ — здесь и в
#   asda_maps.py (тела совпадали дословно). Точка правды — asda_maps.map_asset_name; здесь
#   оставлен реэкспорт, чтобы потребители `asda_ue_paths.map_asset_name` не ломались.
map_asset_name = _MAPS.map_asset_name


def level_path(map_name):
    """/Game/Map/<asset>/<asset> — путь уровня карты."""
    a = map_asset_name(map_name)
    return "%s/%s/%s" % (G_MAP, a, a)


def asset_ref(package_path):
    """'/Game/X/Y' -> '/Game/X/Y.Y' (ссылка на объект внутри пакета)."""
    return "%s.%s" % (package_path, package_path.rsplit("/", 1)[-1])
