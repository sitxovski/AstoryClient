# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_registry_io.py — ОБЩИЙ ввод-вывод генераторов реестров (дедуп №2, В1 Т16, 11.10.2026).
#
# Было: дословные клоны `_read_bytes`, `_write_if_changed`, `generate` (и двухстрочный `load_registry`) в
#   `asda_asset_paths.py` и `asda_map_registry.py` — тела совпадали до символа, разнились только имя набора
#   бэкапа (`_asset_paths` / `_map_registry`) и суффикс временного файла (`.tmp_asset_paths` / `.tmp_map_registry`).
# Стало: одна реализация здесь; в модулях-генераторах остались тонкие обёртки с ПРЕЖНИМИ именами и сигнатурами
#   (их зовут `--check`/генерация и стенды), поведение и вывод не изменились.
#
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает (только определения).
import datetime
import difflib
import json
import os
import shutil


def load_json(path):
    """Прочитать JSON реестра (UTF-8)."""
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def read_bytes(path):
    """Байты файла или None (файла нет / не читается)."""
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def write_if_changed(path, data, backup_path, tmp_suffix):
    """Записать файл, только если содержимое другое; прежнюю версию — в бэкап (один раз за день, со сверкой размера);
    запись атомарная (временный файл рядом + os.replace). Возвращает True, если файл записан.

    backup_path — функция path → путь копии дня (у каждого генератора свой набор в @@ASTORY_DEV_ROOT_ESC@@\\_backup).
    tmp_suffix  — суффикс временного файла рядом с целевым (у каждого генератора свой).
    ★ Ревизия 15.09: бэкап дня уже есть и хранит ДРУГУЮ версию — промежуточную кладём отдельной копией со временем.
    ★ Ревизия 14.09/15.09: обрыв не оставляет половину файла; при сбое временный файл удаляется."""
    cur = read_bytes(path)
    if cur == data:
        return False
    if cur is not None:
        bk = backup_path(path)
        if os.path.exists(bk) and read_bytes(bk) != cur:
            bk = bk + "." + datetime.datetime.now().strftime("%H%M%S")
        if not os.path.exists(bk):
            os.makedirs(os.path.dirname(bk), exist_ok=True)
            shutil.copy2(path, bk)
            if os.path.getsize(bk) != len(cur):
                raise RuntimeError("бэкап %s не совпал по размеру — запись отменена" % bk)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + tmp_suffix
    try:
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise
    return True


def generate(reg, check_registry, rendered_outputs, write_if_changed_fn, write=True, show_diff=False):
    """Сгенерировать файлы реестра. Возвращает список путей, которые изменились (или изменились бы при write=False).

    check_registry(reg)   → (ошибки, предупреждения, сведения); есть ошибки — SystemExit, ничего не пишется.
    rendered_outputs(reg) → [(путь, ожидаемые байты)].
    write_if_changed_fn(path, data) — запись с бэкапом (обёртка генератора: свой набор бэкапа и временный файл).
    Печать — прежняя построчно («без изменений» / «ЗАПИСАНО» / «изменился бы», дифф по --diff)."""
    errs, _w, _i = check_registry(reg)
    if errs:
        for e in errs:
            print("  ОШИБКА реестра: " + e)
        raise SystemExit("реестр с ошибками — генерация отменена")
    changed = []
    for path, data in rendered_outputs(reg):
        cur = read_bytes(path)
        if cur == data:
            print("  без изменений: %s" % path)
            continue
        changed.append(path)
        if show_diff:
            a = (cur or b"").decode("utf-8", "replace").splitlines()
            b = data.decode("utf-8", "replace").splitlines()
            for ln in difflib.unified_diff(a, b, path, "сгенерировано", lineterm="", n=1):
                print("    " + ln)
        if write:
            write_if_changed_fn(path, data)
            print("  ЗАПИСАНО: %s" % path)
        else:
            print("  изменился бы: %s" % path)
    return changed
