# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_await_maps.py — две таблицы подсказки робота «Минопио» (окно 599,
#   «Тебя кое-кто разыскивает. Нажми 'M', и увидишь их на карте»).
#   Полный реверс системы — `Reports\minopio_window.md`.
#
# ══════════════════════════════════ ЧТО И ЗАЧЕМ ═════════════════════════════════════════════════
#  1) `Data\NPC\TIM_List.BIN` → **DT_TimList** (тег "VIL", count в шапке `+4` = 8, шаг 204).
#     Загрузчик оригинала `loadNpcTimList@0xD98F80` (`npc_mob_pet.c:2388`).
#     Запись: `+0 i32 mapId`, `+4 char name[200]`.
#     Роль: (а) БЕЛЫЙ СПИСОК карт, которые вообще можно назвать в подсказке; (б) их имена.
#     Контроль размером: 20 + 8×204 = 1652 = размер файла.
#
#  2) `Data\NPC\NMS_list.BIN` → **DT_NmsList** (тег "NSL", count `+4` = 12, шаг 44).
#     Загрузчик оригинала `npcLoadNameList@0xD99130` (`npc_mob_pet.c:2423`).
#     Запись: `+0 i32 mapId`, `+4 … +40` — 10× i32 связанных карт (−1 = пусто).
#     Роль: находясь на карте M, оригинал берёт ИМЕННО строку `44*M` и проверяет только эти 10 карт.
#     Контроль размером: 20 + 12×44 = 548 = размер файла.
#
# ★ КОДИРОВКА. Единственные строки здесь — 8 названий местностей. Правило проекта: НЕ «сначала
#   cp949, при ошибке cp1251» (русский текст молча превращается в иероглифы, потому что байты
#   cp1251 образуют валидные пары cp949), а определитель по СОДЕРЖИМОМУ — `AstoryClient\Tools\Translate\tools\langdet`.
#   Второго определителя не заводим.
#
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_await_maps.py
#   без UE:  python asda_import_await_maps.py --csv-only
import json
import os
import os as _os
import struct
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_bintable as _BT     # ★ дедуп №2 В1 Т5: общий выбор кодировки и C-строки (было своё тело)

OUT_DIR = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC")
PKG = "/Game/AsdaAssets/Data/NPC"

RS_TIM = "/Script/AsdaReborn.AsdaTimListRow"
RS_NMS = "/Script/AsdaReborn.AsdaNmsListRow"

HDR = 0x14   # общая шапка `.BIN`: +0 версия, +4 count, +8 полей, +0xC тег, +0x10 ноль

I = lambda b, o: struct.unpack_from("<i", b, o)[0]


def log(msg):
    try:
        import unreal
        unreal.log_warning("[await-maps] %s" % msg)
    except Exception:
        print("[await-maps] %s" % msg)


_sys.path.insert(0, _P.TRANSLATE_TOOLS)
try:
    from langdet import lang as _lang
except Exception:                     # langdet недоступен — честно говорим и не молчим
    _lang = None


def pick_enc(raw):
    """'cp949' только для реально корейских строк, иначе 'cp1251' (см. блок в шапке).
    Тело — общий `asda_bintable.pick_enc` (тот же langdet; без него — эвристика «декодируется ли cp1251»)."""
    return _BT.pick_enc(raw, on_missing="heuristic")


def cstr(buf, off, maxlen):
    """C-строка поля maxlen байт (до первого NUL) в кодировке по содержимому, битые байты — замена."""
    raw = _BT.cstr_bytes(buf, off, maxlen)
    return raw.decode(pick_enc(raw), "replace")


def check(name, blob, tag, stride):
    """Проверка «тег + count + шаг» арифметикой РАЗМЕРА ФАЙЛА, а не на глаз."""
    got = blob[0x0C:0x0F].decode("ascii", "replace")
    count = I(blob, 4)
    need = HDR + stride * count
    if got != tag or need != len(blob):
        raise ValueError("%s: тег «%s» (ждали «%s»), count %d, шаг %d ⇒ ожидалось %d б, в файле %d"
                         % (name, got, tag, count, stride, need, len(blob)))
    return HDR, count


def read_tim():
    b = open(os.path.join(_P.DATA, r"NPC\TIM_List.BIN"), "rb").read()
    base, n = check("TIM_List.BIN", b, "VIL", 204)
    return [dict(MapId=I(b, base + 204 * i), VillageName=cstr(b, base + 204 * i + 4, 200))
            for i in range(n)]


def read_nms():
    b = open(os.path.join(_P.DATA, r"NPC\NMS_list.BIN"), "rb").read()
    base, n = check("NMS_list.BIN", b, "NSL", 44)
    return [dict(MapId=I(b, base + 44 * i),
                 Related=[I(b, base + 44 * i + 4 + 4 * k) for k in range(10)])
            for i in range(n)]


# * DEDUP D10: telo pereyehalo v obshchuyu tochku `asda_dt_common.json_rows`
#   (ono zhilo v chetyryoh importyorah slovo v slovo). Zdes tonkiy psevdonim.
from asda_dt_common import json_rows  # noqa: E402


def fill_dt(name, row_struct_path, json_text):
    # * DEDUP D10: sam shag "RowStruct -> DataTable -> zalivka -> sohranenie" na port ODIN
    #   (`asda_dt_common.fill_dt`). Zdes ostayotsya tolko svoyo: paket, logger, podskazka pro
    #   zagolovok C++ i priznak uspeha.
    import unreal
    from asda_dt_common import fill_dt as _fill
    dt = _fill(unreal, PKG, name, row_struct_path, json_text, log,
               fmt="json", missing_hint="Public/Quest/AsdaQuestAwaitTypes.h")
    return dt is not None


def main(csv_only=False):
    os.makedirs(OUT_DIR, exist_ok=True)
    tim, nms = read_tim(), read_nms()

    jobs = [
        ("DT_TimList", RS_TIM, tim, lambda r: str(r["MapId"]), "TimList.json"),
        ("DT_NmsList", RS_NMS, nms, lambda r: str(r["MapId"]), "NmsList.json"),
    ]
    texts = {}
    for name, rs, rows, key, fn in jobs:
        t = json_rows(rows, key)
        texts[name] = t
        with open(os.path.join(OUT_DIR, fn), "w", encoding="utf-8", newline="") as f:
            f.write(t)
        log("%s: %d строк" % (name, len(rows)))

    # ── контрольные значения (взяты из РЕВЕРСА и байтов файла, а не из этого же кода) ──
    bad = 0

    # TIM: id уникальны (иначе строка DataTable перетрёт соседнюю), их ровно 8, и это 0…7.
    ids = sorted(r["MapId"] for r in tim)
    if ids != list(range(8)):
        log("!! контроль TIM_List: ожидали id 0…7, получили %s" % ids)
        bad += 1
    else:
        log("контроль TIM_List: 8 карт с id 0…7 — OK")
    empty = [r["MapId"] for r in tim if not r["VillageName"].strip()]
    if empty:
        log("!! контроль TIM_List: пустые названия у карт %s" % empty)
        bad += 1

    # ★ Кодировка: названия местностей русского клиента переведены целиком — иероглиф = ошибка разбора.
    def _cjk(s):
        return sum(1 for c in s if "\uac00" <= c <= "\ud7a3" or "\u4e00" <= c <= "\u9fff")
    cjk = [r["VillageName"] for r in tim if _cjk(r["VillageName"])]
    if cjk:
        log("!! ИЕРОГЛИФЫ В НАЗВАНИЯХ МЕСТНОСТЕЙ: %s — разбор кодировки сломан" % cjk)
        bad += 1
    lat = [r["VillageName"] for r in tim
           if all(ord(c) < 128 for c in r["VillageName"]) and r["VillageName"].strip()]
    if lat:
        # Не ошибка разбора, а ДЕФЕКТ ДАННЫХ (§7.7 отчёта): в чистой RU-сборке таблица не переведена.
        log("остаток непереведённого в TIM_List: %d из %d — %s"
            % (len(lat), len(tim), ", ".join("«%s»" % s for s in lat)))
    else:
        log("контроль перевода TIM_List: все 8 названий на кириллице — OK")

    # NMS: ключ = ИНДЕКС записи (эталон адресуется `44*mapId` без поиска) ⇒ MapId обязан совпасть с i.
    misplaced = [(i, r["MapId"]) for i, r in enumerate(nms) if r["MapId"] != i]
    if misplaced:
        log("!! контроль NMS_list: запись i ≠ MapId %s — эталонная адресация `44*mapId` СЛОМАНА" % misplaced)
        bad += 1
    else:
        log("контроль NMS_list: 12 записей, MapId == индекс — OK (эталон адресуется `44*mapId`)")
    ref0 = [1, 2, 3, 0, 7, 6, -1, -1, -1, -1]
    if not nms or nms[0]["Related"] != ref0:
        log("!! контроль NMS_list запись 0 НЕ СОШЁЛСЯ: %s (ждали %s)" % (nms and nms[0]["Related"], ref0))
        bad += 1
    else:
        log("контроль NMS_list запись 0 (Силарис): связанные %s — OK" % ref0)

    # Дефект данных §7.8: карты 4 и 5 не перечислены НИГДЕ ⇒ подсказка о них невозможна.
    named = set()
    for r in nms:
        named.update(x for x in r["Related"] if x >= 0)
    unreachable = sorted(set(r["MapId"] for r in tim) - named)
    log("карты из TIM_List, которых нет ни в одной строке NMS_list (о них не расскажут НИКОГДА): %s"
        % (unreachable or "нет"))

    if _lang is None:
        log("!! langdet недоступен (%s) — кодировка выбрана грубой эвристикой, проверьте названия"
            % _P.TRANSLATE_TOOLS)
        bad += 1
    log("СВЕРКА: расхождений %d" % bad)

    if csv_only:
        return
    try:
        import unreal  # noqa: F401
    except Exception:
        log("UE недоступен — записаны только JSON в %s" % OUT_DIR)
        return
    for name, rs, rows, key, fn in jobs:
        fill_dt(name, rs, texts[name])


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main(csv_only=("--csv-only" in _sys.argv))
