# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_zone_data.py — ДОКАЗАТЕЛЬСТВО системы «город / не город»:
#   по каждой карте DT_MapTable печатает городской/полевой BGM и текстуры названия локации,
#   и проверяет НАЛИЧИЕ ассетов (SoundWave + Texture2D + looping) в Content.
# Отчёт пишется в Reports/zone_bgm_table.md.
# Запуск: run_headless.bat asda_verify_zone_data.py
import unreal, os

EAL = unreal.EditorAssetLibrary
DT = "/Game/AsdaAssets/Data/DT_MapTable"
MUSIC_DIRS = ["/Game/AsdaAssets/Sounds/Music"]   # канон музыки (Sound/BGM удалён 2026-08-02 как дубль)
TEX_DIRS = ["/Game/AsdaAssets/Textures/NPC/Image", "/Game/AsdaAssets/Textures/NPC/Portrait"]
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\zone_bgm_table.md"


def find_sound(name):
    if not name: return None
    for d in MUSIC_DIRS:
        p = "%s/%s" % (d, name)
        if EAL.does_asset_exist(p): return p
    return None


def find_tex(name):
    if not name: return None
    n = name.replace(" ", "_")
    for d in TEX_DIRS:
        p = "%s/%s" % (d, n)
        if EAL.does_asset_exist(p): return p
    return None


def looping(path):
    a = EAL.load_asset(path) if path else None
    if not a: return None
    for prop in ("looping", "b_looping"):
        try: return bool(a.get_editor_property(prop))
        except Exception: pass
    return None




# Читаем DataTable ПОКОЛОНОЧНО (get_data_table_row_from_name в UE5.8 из Python не доступен).
DTF = unreal.DataTableFunctionLibrary
def col(prop):
    try: return list(DTF.get_data_table_column_as_string(dt, prop))
    except Exception as e:
        unreal.log_warning("[verify-zone] колонка %s: %s" % (prop, e)); return []



class R:  # лёгкая обёртка «строка таблицы»
    def __init__(self, i):
        self.map_id = int(C["MapId"][i] or -1)
        self.dir_name      = C["DirName"][i]
        self.bgm_town      = C["BgmTown"][i]
        self.bgm_field     = C["BgmField"][i]
        self.name_tex      = C["NameTex"][i]
        self.name_tex_town = C["NameTexTown"][i]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global C, dt, n
    dt = EAL.load_asset(DT)
    if not dt:
        unreal.log_warning("[verify-zone] нет DT_MapTable"); raise SystemExit
    lines = ["# Зона «город / не город»: карта → BGM → текстура названия", "",
             "Источник BGM — заголовок `<map>.TER` (ФАЙЛ 0x2F30 слот 0 «город», 0x2F50 слот 1 «поле`,",
             "флаги наличия 0x2F70/0x2F74). Текстуры — свитч `Map_SetNameTextureByID@0xDA3700`.", "",
             "| id | папка | BGM ГОРОД (0x80) | BGM ПОЛЕ | ассет/loop | название | название(город) | ассет |",
             "|---|---|---|---|---|---|---|---|"]
    C = {p: col(p) for p in ("MapId", "DirName", "BgmTown", "BgmField", "NameTex", "NameTexTown")}
    n = len(C["MapId"])
    rows = sorted(((R(i).map_id, R(i)) for i in range(n)), key=lambda x: x[0])
    bad = 0
    for mid, r in rows:
        st, sf = find_sound(r.bgm_town), find_sound(r.bgm_field)
        tn, tt = find_tex(r.name_tex), find_tex(r.name_tex_town)
        lt, lf = looping(st), looping(sf)
        snd = "%s/%s" % ("да" if st else ("нет" if r.bgm_town else "—"),
                         "да" if sf else ("нет" if r.bgm_field else "—"))
        loop = "loop %s/%s" % ("+" if lt else ("-" if st else "—"), "+" if lf else ("-" if sf else "—"))
        tex = "%s/%s" % ("да" if tn else ("НЕТ" if r.name_tex else "—"),
                         "да" if tt else ("НЕТ" if r.name_tex_town else "—"))
        if (r.bgm_town and not st) or (r.bgm_field and not sf) or (r.name_tex and not tn): bad += 1
        lines.append("| %d | %s | %s | %s | %s %s | %s | %s | %s |" % (
            mid, r.dir_name, r.bgm_town or "—", r.bgm_field or "—", snd, loop,
            r.name_tex or "—", r.name_tex_town or "—", tex))
        unreal.log("[verify-zone] %2d %-18s город=%-28s поле=%-28s | %s %s | %s / %s | %s" % (
            mid, r.dir_name, r.bgm_town or "—", r.bgm_field or "—", snd, loop,
            r.name_tex or "—", r.name_tex_town or "—", tex))
    lines += ["", "Карт: %d. Строк с отсутствующими ассетами: %d." % (len(rows), bad),
              "",
              "## Покрытие музыки (актуально 2026-08-02)",
              "",
              "Просканированы Data/Sound ВСЕХ сборок: AsdaChroniclesInterlude (+MINE/+WoS/+копия),",
              "AsdaGlobal, Arabic ASDA Story, Фантазиум, Donors\\TRANSLATE_unpacked\\backup_assets,",
              "а также ВТОРАЯ ЧАСТЬ NEW\\AsdaFableClassic и NEW\\WorldOfDreams (39 mp3).",
              "",
              "* `S_BGM_Inferion` (карта Inferion, слот ГОРОД) — **ДОБРАН 2026-08-02**: файл",
              "  `S_BGM_inferion.mp3` есть в AsdaGlobal, NEW\\AsdaFableClassic и NEW\\WorldOfDreams,",
              "  все три байт-в-байт идентичны (md5 7c26ab19d37a1f712190401e6efcef8a). Импортирован",
              "  под именем, которое просит заголовок карты — `S_BGM_Inferion`.",
              "* `S_BGM_NEW_Field` (карта StagnantDesert, слот ГОРОД) — трека НЕТ ни в одной сборке;",
              "  решение юзера 2026-08-01: пустыне отдан её же `S_BGM_stagnantdesert` под этим именем.",
              "* `S_BGM_flabisTown` (flabis, слот ГОРОД) — **ДОБРАН 2026-08-02**: mp3 с таким именем нет",
              "  НИ В ОДНОЙ сборке. Решение владельца: город флабиса получает его же полевую тему.",
              "  Источник — `Donors\\AsdaChroniclesInterlude_MINE_WoS\\Data\\Sound\\S_BGM_flabisfield.mp3`.",
              "  ★ ФАКТ (сверено побайтово): аудио-payload (без ID3) у нашего `S_BGM_flabisfield.mp3`,",
              "  у `S_BGM_flabisfield.mp3` из NEW\\AsdaFableClassic и у `S_BGM_stagnantdesert.mp3` —",
              "  ОДИН И ТОТ ЖЕ (md5 c4248a5a62b9b2bbaebaff012fa7b00a, 2304316 б). Различие md5 файлов",
              "  целиком (c8696ab3 / 24c7e005) даёт ТОЛЬКО ID3-тег. «Уникального флабисового трека» нет.",
              "",
              "### Отступления от .TER — РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-02 (данными, не кодом)",
              "",
              "В оригинале слот с флагом наличия 0 = ТИШИНА в этой зоне. Владелец потребовал музыку",
              "в ОБЕИХ зонах на Inferion и в пустыне. Реализовано таблицей `OWNER_BGM_OVERRIDES` в",
              "`Tools/asda_export_map_zone_data.py` → CSV → DT_MapTable (строка правится в редакторе UE):",
              "",
              "| карта | слот | .TER | стало |",
              "|---|---|---|---|",
              "| Inferion (18) | ПОЛЕ | `S_BGM_AlpeonField`, флаг=0 → тишина | `S_BGM_Inferion` (как в городе) |",
              "| StagnantDesert (14) | ПОЛЕ | `S_BGM_Alpeondgn_NightValley`, флаг=0 → тишина | `S_BGM_NEW_Field` (как в городе) |",
              "",
              "Имя взято РОВНО то же, что в городском слоте той же карты → новых ассетов не создано.",
              "",
              "* `S_BGM_battlefield` (battlefield, ПОЛЕ) и `S_BGM_fieldofhonor` (quizstage, ПОЛЕ) —",
              "  трека нет НИ В ОДНОЙ сборке, включая вторую часть; владелец подтвердил 2026-08-02:",
              "  эти карты остаются БЕЗ музыки. Подмен не делаем.",
              "",
              "Треки второй части, которых у нас нет и которые НИ ОДНА из 22 карт не просит",
              "(доступны «про запас», НЕ импортированы): S_BGM_Rein, S_BGM_SilalisTown,",
              "S_BGM_SilalisField, S_BGM_AQUATONTOWN, S_BGM_AQUATONFIELD, S_BGM_mine, S_BGM_minebill,",
              "S_BGM_castle_of_chaos, S_BGM_Coldswamp, S_BGM_DesolatedMarsh, S_BGM_NODFIELD,",
              "S_BGM_windcanyon, S_BGM_neverfall, S_BGM_astarica, S_BGM_fantagle, S_BGM_Acanpolis,",
              "S_BGM_Elysion, S_BGM_Labyrinthos, S_BGM_CreateCharacter."]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    unreal.log("[verify-zone] отчёт: %s (карт=%d, проблемных=%d)" % (OUT, len(rows), bad))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
