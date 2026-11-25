# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_name_diff.py — СВЕРКА ТАБЛИЦ ИМЁН .uasset БЕЗ ЗАПУСКА РЕДАКТОРА.
#
#   ЗАЧЕМ. Перегенерация окна `asda_jui_generic_umg.py` собирает WBP С НУЛЯ из .JUI и молча
#   стирает всё, что доложено в ассет ПОСЛЕ импорта (дельта-скрипты, ручные узлы пользователя).
#   Чтобы такую потерю ловить ДО того, как её найдёт юзер, нужен дешёвый способ сравнить
#   «до/после» по всем окнам сразу. Загрузить бэкап в редактор нельзя (путь пакета зашит внутрь
#   .uasset), поэтому имена читаются прямо из таблицы FName файла.
#
#   ФОРМАТ. Заголовок UE5 .uasset: Tag(0x9E2A83C1) → LegacyFileVersion → ... → NameCount/NameOffset;
#   по NameOffset лежит NameCount записей FNameEntrySerialized = FString + 2×uint16 хешей.
#   Имена виджетов попадают в таблицу как есть (`MMap_View`, `RESIZE`, …), поэтому сверка имён
#   по таблице FName эквивалентна сверке набора узлов дерева виджетов.
#
#   ЗАПУСК (обычный python, редактор не нужен):
#       python Tools\asda_ui_name_diff.py <дерево_A> <дерево_B> [--json out.json]
#   например
#       python Tools\asda_ui_name_diff.py @@ASTORY_DEV_ROOT@@\_backup\UI_before_drag_2026-08-06\UI ^
#                                         @@ASTORY_DEV_ROOT@@\AstoryClient\Content\AsdaAssets\UI
#   Выводит по каждому окну имена, которые есть в A и пропали в B (потери), и наоборот (добавления).
import argparse
import json
import os
import struct
import sys

TAG = 0x9E2A83C1


class _R:
    """Курсор по байтам пакета."""

    def __init__(self, data):
        self.d = data
        self.p = 0

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def skip(self, n):
        self.p += n

    def fstring(self):
        n = self.i32()
        if n == 0:
            return ""
        if n > 0:
            s = self.d[self.p:self.p + n - 1].decode("latin-1", "replace")
            self.p += n
            return s
        n = -n
        s = self.d[self.p:self.p + (n - 1) * 2].decode("utf-16-le", "replace")
        self.p += n * 2
        return s


def _try_names(data, name_count, name_offset):
    """Попробовать прочитать таблицу FName по паре (count, offset). None — не подошло."""
    if not (0 < name_count < 500_000):
        return None
    if not (16 < name_offset < len(data)):
        return None
    r = _R(data)
    r.p = name_offset
    out = []
    for _ in range(name_count):
        if r.p + 4 > len(data):
            return None
        n = struct.unpack_from("<i", data, r.p)[0]
        if n == 0 or n > 4096 or n < -4096:
            return None
        s = r.fstring()
        if r.p + 4 > len(data):
            return None
        r.skip(4)                         # два uint16 хеша
        out.append(s)
    if not out:
        return None
    # Достоверность: имена UE — печатаемые, без управляющих символов.
    bad = sum(1 for s in out if any(ord(c) < 32 or ord(c) == 127 for c in s))
    if bad > len(out) * 0.02:
        return None
    return out


def read_names(path):
    """Вернуть список строк таблицы FName пакета .uasset.

    ★ Точная раскладка FPackageFileSummary менялась от версии к версии (в UE 5.8
    LegacyFileVersion = -9 и между лицензионной версией и списком custom-версий появились
    новые поля), поэтому пара (NameCount, NameOffset) ищется перебором по заголовку с
    ПРОВЕРКОЙ: таблица обязана прочитаться целиком и состоять из печатаемых строк.
    """
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 64:
        return []
    if struct.unpack_from("<I", data, 0)[0] != TAG:
        return []
    # ★ Шаг 1 байт: FString `PackageName` имеет ПРОИЗВОЛЬНУЮ длину, поэтому поля за ним
    #   в файле НЕ выровнены (в примере пара лежала по смещению 263).
    for cand in range(20, min(4096, len(data) - 8)):
        nc, no = struct.unpack_from("<ii", data, cand)
        got = _try_names(data, nc, no)
        if got is not None and len(got) > 8:
            # Настоящая таблица всегда начинается с путей движка/пакета.
            head = got[:12]
            if sum(1 for s in head if s.startswith("/Script/") or s.startswith("/Game/")) >= 3:
                return got
    return []


def scan_tree(root):
    """{относительный_путь_без_расширения: set(имён FName)} по всем .uasset дерева.

    ★ Ключ — ПУТЬ, а не имя файла: одно и то же имя может лежать в двух папках
    (`Temp/WBP_Job_Guide_Win` и `Player_UI/Tutorial/WBP_Job_Guide_Win` — два РАЗНЫХ окна,
    объявленных в разных классах .JUI). При ключе-имени один затирал другой и сверка врала.
    """
    res = {}
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.lower().endswith(".uasset"):
                continue
            full = os.path.join(dirpath, fn)
            try:
                names = read_names(full)
            except Exception as e:                       # noqa: BLE001
                print("  ! %s: %s" % (full, e), file=sys.stderr)
                continue
            rel = os.path.relpath(os.path.splitext(full)[0], root).replace("\\", "/")
            res[rel] = set(names)
    return res


# Имена, которые меняются от пересборки сами по себе и потерей НЕ являются.
NOISE_PREFIX = ("BP_", "Default__", "EXECUTE_UBERGRAPH", "K2Node_", "/Script/", "/Game/",
                "SKEL_", "REINST_", "TRASHCLASS_", "PLACEHOLDER-CLASS")
NOISE_EXACT = {"None"}


def _interesting(n):
    if not n or n in NOISE_EXACT:
        return False
    if n.startswith(NOISE_PREFIX):
        return False
    if n.startswith("/"):
        return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a", help="дерево ДО (эталон/бэкап)")
    ap.add_argument("b", help="дерево ПОСЛЕ (текущее)")
    ap.add_argument("--json", help="куда сложить машинный отчёт")
    ap.add_argument("--only-lost", action="store_true", help="печатать только потери")
    args = ap.parse_args()

    A = scan_tree(args.a)
    B = scan_tree(args.b)
    report = {"lost": {}, "added": {}, "missing_assets": sorted(set(A) - set(B)),
              "new_assets": sorted(set(B) - set(A))}
    for asset in sorted(set(A) & set(B)):
        lost = sorted(n for n in A[asset] - B[asset] if _interesting(n))
        added = sorted(n for n in B[asset] - A[asset] if _interesting(n))
        if lost:
            report["lost"][asset] = lost
        if added:
            report["added"][asset] = added

    print("Ассетов: A=%d B=%d; только в A: %d; только в B: %d"
          % (len(A), len(B), len(report["missing_assets"]), len(report["new_assets"])))
    for asset, lost in report["lost"].items():
        print("[ПОТЕРЯ] %s: %s" % (asset, ", ".join(lost)))
    if not args.only_lost:
        for asset, added in report["added"].items():
            print("[ДОБАВЛЕНО] %s: %s" % (asset, ", ".join(added)))
    print("ИТОГО окон с потерями: %d, с добавлениями: %d"
          % (len(report["lost"]), len(report["added"])))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(report, f, ensure_ascii=False, indent=1)
        print("отчёт → %s" % args.json)


# ★★★ ЗАКОН tools-no-import-side-effects.
if __name__ == "__main__":
    main()
