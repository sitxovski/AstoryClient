# Copyright (c) 2026 @sitxovski (Discord)
# DT_MobSounds: MobId-1 → звук-база моба (Mob_list.bin @+0x44, 0x20 байт) + модель (@+0x04).
# ★ 2026-08-02: колонка Model была из @+0x24 — это база КЛИПОВ .jbb (Effect_BuildJbbSuffixByType@0xF717C0),
#   а модель .jbm лежит в @+0x04 (эталон CMap_LoadMOB@0xF71A50). У 117 из 854 записей поля различаются.
# Эталон Sound_BuildEntryWavName@0xF71450: wav = "<база>_<фаза>.wav" (фаза 11 → _99);
# фазы: 3=?, 4=СМЕРТЬ, 5/6/7=замахи (5+вариант), 30/31=рык при замахе (обычн/крит), 10=?.
# RowStruct = FAsdaEntityNameRow (реюз: Name=звук-база, Model=модель).
import struct, unreal
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402

BIN = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\Mob\Mob_list.bin"
OUT_PKG, OUT_NAME = "/Game/AsdaAssets/Data/Mob", "DT_MobSounds"


def log(m): unreal.log_warning("[mobsnd] %s" % m)


def main():
    b = open(BIN, "rb").read()
    cnt = struct.unpack_from("<I", b, 4)[0]
    rows = []
    for i in range(cnt):
        r = 0x14 + i * 0x110
        if r + 0x110 > len(b): break
        model = b[r+0x04:r+0x24].split(b"\0")[0].decode("latin1", "ignore")   # ★ модель = +0x04
        snd   = b[r+0x44:r+0x64].split(b"\0")[0].decode("latin1", "ignore")
        rows.append((i, snd, model))
    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    dt = unreal.EditorAssetLibrary.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else None
    if not dt:
        factory = unreal.DataTableFactory()
        row_struct = unreal.load_object(None, "/Script/AsdaReborn.AsdaEntityNameRow")
        factory.set_editor_property("struct", row_struct)
        dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(OUT_NAME, OUT_PKG, unreal.DataTable, factory)
    if not dt:
        log("DT не создан"); return
    # Заполнение через CSV-строку (row_name,Name,Model)
    csv = "Row,Name,Model\n" + "\n".join('%d,"%s","%s"' % (i, s, m) for i, s, m in rows)
    ok = _DTC.fill_and_save(unreal, dt, csv, log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("DT_MobSounds: строк %d, заполнение=%s" % (len(rows), ok))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
