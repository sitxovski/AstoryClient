#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
gen_itemdb_structs.py — КОДОГЕНЕРАТОР USTRUCT-строк DataTable для item-БД.

Источник истины = REGISTRY из @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB\decode_item_db.py (точные типы полей,
1:1 с реверсом форматов Bin.bbz). Для КАЖДОЙ таблицы генерит:
  * USTRUCT(BlueprintType) FAsda<Table>Row : public FTableRowBase — по одному UPROPERTY
    BlueprintReadOnly на колонку (типы из registry), + DataHex (если таблица с _data_hex).
  * манифест (таблица -> имя struct, /Script-путь, DT-asset-путь, ключевое поле).

Вывод:
  Source/AsdaReborn/Public/Items/AsdaItemDbRows.h        (UHT-парсимый, все USTRUCT)
  Source/AsdaReborn/Public/Items/AsdaItemDbManifest.h    (plain C++ таблица метаданных)
  Tools/_out/itemdb_manifest.json                        (для gen_itemdb_datatables.py)

Запуск:  python gen_itemdb_structs.py
"""
import os, sys, json, re

TOOLS_DIR   = os.path.dirname(os.path.abspath(__file__))
DECODE_DIR  = r'@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB'
PROJECT     = r'@@ASTORY_DEV_ROOT@@\AstoryClient'
PUBLIC_ITEMS= os.path.join(PROJECT, 'Source', 'AsdaReborn', 'Public', 'Items')
OUT_DIR     = os.path.join(TOOLS_DIR, '_out')
MODULE      = 'AsdaReborn'   # структуры компилируются в этот рантайм-модуль

sys.path.insert(0, DECODE_DIR)
import decode_item_db as D   # даёт REGISTRY, gen_ints, C

# явные имена struct для «крупных» таблиц (по требованию заказчика)
STRUCT_OVERRIDE = {
    'item_list':      'FAsdaItemRow',
    'item_img_table': 'FAsdaImgRow',
    'sowell_list':    'FAsdaSowellRow',
    'title_list':     'FAsdaTitleRow',
    'pettable':       'FAsdaPetRow',
    'jobtype':        'FAsdaJobtypeRow',
    'pricetable':     'FAsdaPriceRow',
    'item_tooltip':   'FAsdaItemTooltipRow',
}
# ключевое (id) поле таблицы — для типизированных геттеров подсистемы
KEY_FIELD = {
    'item_list': 'ItemCode', 'item_img_table': '',    # img индексируется позицией
    'sowell_list': 'Id', 'sowell_tooltip': 'Id',
    'title_list': 'Id', 'pettable': 'Id', 'pricetable': 'ItemCode',
    'item_tooltip': 'ItemCode', 'jobtype': '',
}

def base_name(entry):
    return os.path.splitext(entry)[0].replace(' ', '_')

def pascal(s):
    parts = re.split(r'[^A-Za-z0-9]+', s)
    return ''.join(p[:1].upper() + p[1:] for p in parts if p)

def struct_name(base):
    return STRUCT_OVERRIDE.get(base, 'FAsda' + pascal(base) + 'Row')

# тип registry -> (C++ тип, дефолт-инициализатор)
def ue_type(typ):
    if typ.startswith('s'):        return 'FString', ''
    if typ == 'f32':               return 'float', ' = 0.f'
    if typ == 'u32':               return 'int64', ' = 0'   # без потери (unsigned 32)
    # i32/i16/u16/i8/u8 — влезают в int32
    return 'int32', ' = 0'

def sanitize_prop(name):
    # UPROPERTY-имена: без ведущего '_' (напр. _data_hex -> DataHex через отд. ветку)
    return name

def gen():
    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(PUBLIC_ITEMS, exist_ok=True)

    manifest = []   # {table, struct, script_path, dt_asset, dt_path, key_field, has_hex, cols[]}
    structs_src = []

    for entry, spec in D.REGISTRY.items():
        base   = base_name(entry)
        sname  = struct_name(base)
        cols   = spec['cols']
        hashex = spec.get('has_hex', spec.get('dump_hex', False))
        key    = KEY_FIELD.get(base, '')

        used_names = set()
        lines = []
        lines.append(f'// {base}  (stride={spec.get("stride")}, entry={entry})')
        note = spec.get('note')
        if note:
            lines.append(f'//   note: {note}')
        lines.append('USTRUCT(BlueprintType)')
        lines.append(f'struct {sname} : public FTableRowBase')
        lines.append('{')
        lines.append('\tGENERATED_BODY()')
        for (off, typ, nm) in cols:
            if nm in used_names:      # защита от коллизий имён колонок
                nm = f'{nm}_{off}'
            used_names.add(nm)
            cpp, dflt = ue_type(typ)
            cat = base
            lines.append(
                f'\tUPROPERTY(EditAnywhere, BlueprintReadOnly, Category="{cat}") '
                f'{cpp} {nm}{dflt};   // +0x{off:X} {typ}')
        if hashex:
            lines.append(
                f'\tUPROPERTY(EditAnywhere, BlueprintReadOnly, Category="{base}") '
                f'FString DataHex;   // сырой остаток записи (1:1, hex)')
        lines.append('};')
        lines.append('')
        structs_src.append('\n'.join(lines))

        manifest.append({
            'table':       base,
            'struct':      sname,
            'struct_noF':  sname[1:],                       # для /Script-пути
            'script_path': f'/Script/{MODULE}.{sname[1:]}',
            'dt_asset':    f'DT_{base}',
            'dt_path':     f'/Game/AsdaAssets/Data/ItemDB/DT_{base}',
            'csv':         f'{base}.csv',
            'key_field':   key,
            'has_hex':     bool(hashex),
            'columns':     [nm for (_o, _t, nm) in cols] + (['DataHex'] if hashex else []),
        })

    # ── AsdaItemDbRows.h ──────────────────────────────────────────────────
    hdr = []
    hdr.append('// AsdaItemDbRows.h — АВТО-СГЕНЕРИРОВАНО gen_itemdb_structs.py. НЕ РЕДАКТИРОВАТЬ ВРУЧНУЮ.')
    hdr.append('// USTRUCT(BlueprintType) FTableRow-строка на КАЖДУЮ таблицу item-БД (Bin.bbz).')
    hdr.append('// Типы полей 1:1 из Tools/decode_item_db.py REGISTRY (реверс форматов подтверждён).')
    hdr.append('#pragma once')
    hdr.append('')
    hdr.append('#include "CoreMinimal.h"')
    hdr.append('#include "Engine/DataTable.h"')
    hdr.append('#include "AsdaItemDbRows.generated.h"')
    hdr.append('')
    hdr.append('\n'.join(structs_src))
    with open(os.path.join(PUBLIC_ITEMS, 'AsdaItemDbRows.h'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(hdr))

    # ── AsdaItemDbManifest.h (plain, для подсистемы) ─────────────────────
    man = []
    man.append('// AsdaItemDbManifest.h — АВТО-СГЕНЕРИРОВАНО gen_itemdb_structs.py.')
    man.append('// Список DataTable item-БД: {имя таблицы, /Game-путь ассета, ключевое поле}.')
    man.append('// ⚠ В массив иногда дописывают строки ВРУЧНУЮ (таблицы, которых нет в реестре')
    man.append('//   декодера). Перегенерация их сотрёт — сверяйтесь с git/бэкапом перед запуском.')
    man.append('#pragma once')
    man.append('#include "CoreMinimal.h"')
    man.append('')
    man.append('namespace AsdaItemDb')
    man.append('{')
    man.append('\tstruct FTableInfo { const TCHAR* Table; const TCHAR* DataTablePath; const TCHAR* KeyField; };')
    man.append('\tstatic const FTableInfo GTables[] = {')
    for m in manifest:
        man.append(f'\t\t{{ TEXT("{m["table"]}"), TEXT("{m["dt_path"]}.{m["dt_asset"]}"), TEXT("{m["key_field"]}") }},')
    man.append('\t};')
    # ★★★ Счётчик — ТОЛЬКО из массива. Числом писать нельзя: при ручной вставке строки
    #     (см. предупреждение выше) счётчик отставал, и ПОСЛЕДНЯЯ таблица массива молча
    #     переставала грузиться (так пропала `skill_info` → пустые слоты навыков в панели).
    man.append(f'\tstatic constexpr int32 GTableCount = UE_ARRAY_COUNT(GTables);   // записей в реестре: {len(manifest)}')
    man.append('}')
    with open(os.path.join(PUBLIC_ITEMS, 'AsdaItemDbManifest.h'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(man))

    # ── JSON манифест для генератора DataTable ────────────────────────────
    with open(os.path.join(OUT_DIR, 'itemdb_manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)

    print(f'[gen] структур: {len(manifest)}')
    print(f'[gen] -> {os.path.join(PUBLIC_ITEMS, "AsdaItemDbRows.h")}')
    print(f'[gen] -> {os.path.join(PUBLIC_ITEMS, "AsdaItemDbManifest.h")}')
    print(f'[gen] -> {os.path.join(OUT_DIR, "itemdb_manifest.json")}')

if __name__ == '__main__':
    gen()
