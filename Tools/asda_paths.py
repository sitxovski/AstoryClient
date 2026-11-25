# Copyright (c) 2026 @sitxovski (Discord)
# asda_paths.py — ЕДИНЫЙ реестр корней ОРИГИНАЛЬНОГО клиента и общих каталогов данных.
#
# ЗАЧЕМ: до 2026-08-01 тулы держали абсолютные пути инлайном и ссылались на ЧЕТЫРЕ РАЗНЫЕ сборки
# клиента. Это не опечатка — сборки реально разные, и каждая нужна для своего:
#   CLIENT_MINE  — основная рабочая сборка (RU), из неё берётся почти всё: Data, map, juidata, system
#   CLIENT_ORIG  — «чистая» сборка (сверка OPset.bin, сравнение Data/map при расхождениях)
#   CLIENT_WOS   — сборка WoS: ТОЛЬКО таблицы эффектов мобов (asda_effect_mob_tables.py)
#   MODELS_ORIG  — Donors\TRANSLATE_unpacked\models_orig: модели/эффекты ДО перевода (asda_build_*_fx.py, click-marker)
# Опасность была в том, что выбор сборки нигде не назван — тул молча читал «свою». Теперь имя корня
# видно в коде, а несовпадение сборок ловится грепом по этому файлу.
#
# ★ Модуль БЕЗ побочных эффектов при импорте (правило tools-no-import-side-effects): только константы.
# ★ Переопределение через переменные окружения (ASDA_ROOT, ASDA_CLIENT_MINE, ...) — для другой машины.

import os

def _env(name, default):
    v = os.environ.get(name)
    return v if v else default


# ---------------------------------------------------------------- корень всего проекта
ROOT = _env("ASDA_ROOT", r"@@ASTORY_DEV_ROOT@@")

# ---------------------------------------------------------------- сборки клиента (НЕ взаимозаменяемы)
# ★ 17.10.2026 реорганизация: клиенты-доноры живут в ROOT\Donors, порт — в ROOT\port (см. Docs\Port\PROJECT_LAYOUT.md)
DONORS      = _env("ASDA_DONORS", os.path.join(ROOT, "Donors"))
PORT_DIR    = _env("ASDA_PORT", ROOT)                    # repository root
CLIENT_MINE = _env("ASDA_CLIENT_MINE", os.path.join(DONORS, "AsdaChroniclesInterlude_MINE"))
CLIENT_ORIG = _env("ASDA_CLIENT_ORIG", os.path.join(DONORS, "AsdaChroniclesInterlude"))
CLIENT_WOS  = _env("ASDA_CLIENT_WOS",  os.path.join(DONORS, "AsdaChroniclesInterlude_MINE_WoS"))
MODELS_ORIG = _env("ASDA_MODELS_ORIG", os.path.join(DONORS, "TRANSLATE_unpacked", "models_orig"))

# ---------------------------------------------------------------- каталоги основной сборки
DATA        = os.path.join(CLIENT_MINE, "Data")        # Data основной сборки
MAP_DIR     = os.path.join(CLIENT_MINE, "map")         # каталоги карт (.TER/.BIN/Pos.POS/…)
JUIDATA     = os.path.join(CLIENT_MINE, "juidata")     # .jui разметка окон
SYSTEM      = os.path.join(CLIENT_MINE, "system")      # OPset.bin и пр.

DATA_ORIG    = os.path.join(CLIENT_ORIG, "Data")
MAP_ORIG     = os.path.join(CLIENT_ORIG, "map")
SYSTEM_ORIG  = os.path.join(CLIENT_ORIG, "system")
JUIDATA_ORIG = os.path.join(CLIENT_ORIG, "juidata")
# Ещё одна копия ORIG-сборки: у неё дорисованы отдельные атласы иконок (asda_merge_icon_atlases.py).
CLIENT_ORIG_COPY  = _env("ASDA_CLIENT_ORIG_COPY", os.path.join(DONORS, "AsdaChroniclesInterlude_orig_copy"))
JUIDATA_ORIG_COPY = os.path.join(CLIENT_ORIG_COPY, "juidata")

DATA_WOS    = os.path.join(CLIENT_WOS, "Data")
DATA_MODELS_ORIG = os.path.join(MODELS_ORIG, "Data")

# TRANSLATE — рабочее дерево перевода (models_orig = модели/эффекты ДО перевода имён узлов).
TRANSLATE       = os.path.join(PORT_DIR, "AstoryClient", "Tools", "Translate")          # скрипты/отчёты перевода (распаковки — Donors\TRANSLATE_unpacked)
TRANSLATE_TOOLS = os.path.join(TRANSLATE, "tools")
TRANSLATE_UNPACKED = os.path.join(DONORS, "TRANSLATE_unpacked")

# ---------------------------------------------------------------- частые файлы/архивы клиента
NPC_LIST_BIN   = os.path.join(DATA, "NPC", "npc_list.BIN")
CHAR_MODEL_BNZ = os.path.join(DATA, "Character", "Model", "CharModel.bnz")
MOB_MODEL_BNZ  = os.path.join(DATA, "Mob", "Model", "MobModel.bnz")
NPC_MODEL_BNZ  = os.path.join(DATA, "NPC", "Model", "NpcModel.bnz")
NPC_MOTION_BAZ = os.path.join(DATA, "NPC", "Motion", "NpcMotion.baz")
MOB_MOTION_BAZ = os.path.join(DATA, "Mob", "Motion", "MobMotion.baz")
EFFECT_MODEL_BNZ = os.path.join(DATA, "Effect", "Model", "EffectModel.bnz")
EFFECT_TEX_BTZ   = os.path.join(DATA, "Effect", "Texture", "Texture.btz")
SOUND_DIR        = os.path.join(DATA, "Sound")
JUI_IMAGE_BTZ    = os.path.join(JUIDATA, "Image", "Texture.btz")
JUI_FONT_DIR     = os.path.join(JUIDATA, "font")
NPC_IMAGE_BTZ    = os.path.join(DATA, "NPC", "Image", "Texture.btz")
CHAR_BIN_BBZ     = os.path.join(DATA, "Character", "Bin.bbz")
SFT_BIN          = os.path.join(DATA, "Sound", "SFT.BIN")
OPSET_BIN        = os.path.join(SYSTEM, "OPset.bin")
OPSET_BIN_ORIG   = os.path.join(SYSTEM_ORIG, "OPset.bin")
# Модели эффектов ДО перевода (нужны там, где перевод испортил имена узлов).
EFFECT_MODEL_ORIG_DIR = os.path.join(DATA_MODELS_ORIG, "Effect", "Model")
CHAR_MODEL_ORIG_DIR   = os.path.join(DATA_MODELS_ORIG, "Character", "Model")

# ---------------------------------------------------------------- серверные данные (GameServer)
GAMESERVER      = os.path.join(DONORS, "GameServer_stock")
NPC_DATA_INFO   = os.path.join(GAMESERVER, "NpcData_Info")        # .npc — позиции NPC
MONSTER_DATA_INFO = os.path.join(GAMESERVER, "MonsterData_Info")  # .svr — спавны мобов

# ---------------------------------------------------------------- промежуточный экспорт инструментов
EXPORT       = os.path.join(PORT_DIR, "Data", "Export")
EXPORT_MAPS  = os.path.join(EXPORT, "maps")        # <map>.mapraw + <map>/ (манифесты, эффекты, анимации)
EXPORT_FX    = os.path.join(EXPORT, "effects")

# ---------------------------------------------------------------- отчёты/документы реверса
RE_DIR       = os.path.join(PORT_DIR, "RE")                 # знания реверса оригинала (ida, etalon, docs, capture, dumps)
RE_REPORTS   = os.path.join(PORT_DIR, "Reports")            # отчёты волн порта (бывший _re\reports)
RE_DOCS      = os.path.join(RE_DIR, "docs")                 # документы реверса (бывший _re\docs без доков порта)
PORT_DOCS    = os.path.join(PORT_DIR, "Docs")               # документы порта: Port\HANDOFF, Changelog, UI, Process
DECODED_JUI  = os.path.join(PORT_DIR, "Data", "Decoded", "jui")   # расшифрованные JUI оригинала (бывший _re\decomp)


def mapraw(map_name):
    """Путь к <map>.mapraw промежуточного экспорта."""
    return os.path.join(EXPORT_MAPS, map_name + ".mapraw")


def map_export_dir(map_name):
    """Каталог промежуточного экспорта конкретной карты (манифесты/эффекты/анимации)."""
    return os.path.join(EXPORT_MAPS, map_name)
