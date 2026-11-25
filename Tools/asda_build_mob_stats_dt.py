# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_mob_stats_dt.py — DT_MobStats: ЭТАЛОННЫЕ ЧИСЛОВЫЕ ПОЛЯ ТИПА МОБА из Mob list.BIN.
#
# ★ РЕВЕРС ПОЛЕЙ (2026-08-03, дизасм аксессоров CMobMgr — все вида
#   `rec = *(mgr+0xC) + typeIdx*0x110; return *(rec + OFF);`):
#     sub_930AC5 → 0xF31080 : OFF 0x84, три float — ПОЛУ-РАЗМЕР AABB (halfX, halfY, halfZ)
#     sub_932483 → 0xF31040 : OFF 0x94, int       — MaxHp
#     sub_91F789 → 0xF83400 : OFF 0x98, int       — УРОВЕНЬ (CMob_Init кладёт в сущность +0x1DC)
#     sub_933400 → 0xF83440 : OFF 0x9C, int       — опыт/награда
#     sub_93DA7C → 0xF833C0 : OFF 0x104, int      — радиус звука
#   Байты, переносимые Recv_MobSpawn@0xF305E0 (network.c:37178-37181):
#     +0xF8 → +6978 ранг/иконка · +0xFC → +6979 АГРЕССИЯ (ветка цвета имени)
#     +0x100 → +6977 «бьёт первым» · +0x10C → +18700 «жизни» полосы HP
#
# ★ ВЫСОТА ПЛАШКИ = 2·HalfY + 50 (закон [[nameplate-height-from-data]]): спавн строит AABB
#   half=(+0x84), центр=позиция, центр.Y += half.Y; плашка = верх AABB (+50.0).
#
# Заполняет /Game/AsdaAssets/Data/Mob/DT_MobStats (RowStruct FAsdaMobStatsRow, RowName = MobId−1).
# Запуск (headless): Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_build_mob_stats_dt.py
import os as _os
import struct
import sys as _sys
import unreal
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_bintable as _BT     # noqa: E402  ★ дедуп №2 В1 Т5: общий обход записей и C-строки
import asda_ue_paths as _U      # noqa: E402  ★ дедуп №2 В1 Т20: каталоги проекта и /Game — реестр путей

BIN = _os.path.join(_U.DATA_SRC, "Mob", "Mob_list.bin")
OUT_PKG, OUT_NAME = _U.G_DATA + "/Mob", "DT_MobStats"
HDR, STRIDE = 0x14, 0x110


def log(m):
    unreal.log_warning("[mobstats] %s" % m)


def read_rows():
    b = open(BIN, "rb").read()
    rows = []
    # ★ дедуп №2 В1 Т5: обход — общий `asda_bintable.iter_records` (счётчик u32@4, база 0x14; запись, не
    #   влезающая в файл, обрывает обход — как прежнее `if r + STRIDE > len(b): break`). Смещения ниже — от начала записи.
    for i, rec in _BT.iter_records(b, STRIDE, count=_BT.record_count(b, signed=False), base=HDR):
        model = _BT.cstr(rec, 0x04, 0x20, "cp1251", "ignore")
        name = _BT.cstr(rec, 0x64, 0x1E, "cp1251", "ignore")
        hx, hy, hz = struct.unpack_from("<3f", rec, 0x84)
        max_hp, level, exp = struct.unpack_from("<3i", rec, 0x94)
        snd_range = struct.unpack_from("<i", rec, 0x104)[0]
        # ★★★ +0x90 → сущность +7356 (РАЗМЕРНЫЙ КЛАСС), +0x108 → сущность +7360 (ПРОЦЕНТ РАЗМЕРА).
        #   Писатели доказаны по бинарю (2026-08-11): mobEffect_LoadArchiveStep@0xF81B90+0xEEF/+0xF0C
        #   зовут sub_F82F10 (rec+144=0x90) и sub_F82F50 (rec+264=0x108) и кладут результат в
        #   [entity+0x1CBC] / [entity+0x1CC0]. Читатели: ветка Hexa и CEffectMgr_scaleAll(percent/100).
        size_class = struct.unpack_from("<i", rec, 0x90)[0]
        size_percent = struct.unpack_from("<i", rec, 0x108)[0]
        # ★ ПОРТРЕТ ТИПА для панели цели PVP_IMAGE (эталон hudShowMobTargetInfo, ui_core.c:75762-75769):
        #   +0xD4 — имя атласа (ASCIIZ), +0xF4 — номер ячейки 46×46 в сетке 11 колонок
        #   (sub_935651 → gridIndexToXY_11col@0xEBFFE0: X = 46*(i%11), Y = 46*(i/11)).
        #   В данных три атласа: mon_icon_01/02/03, ячейки 0..120.
        portrait_tex = _BT.cstr(rec, 0xD4, 0x20, "cp1251", "ignore")
        portrait_cell = struct.unpack_from("<i", rec, 0xF4)[0]
        rows.append(dict(row=i, model=model, name=name, hx=hx, hy=hy, hz=hz,
                         max_hp=max_hp, level=level, exp=exp, snd=snd_range,
                         size_class=size_class, size_percent=size_percent,
                         rank=rec[0xF8], aggr=rec[0xFC], first=rec[0x100], lives=rec[0x10C],
                         ptex=portrait_tex, pcell=portrait_cell))
    # ★ 14.10.2026 (names11, этап 4): имена пяти событийных монстров в Mob_list.bin русской сборки английские; русский перевод -
    #   из ведомости Tools/data/ru_dt_overrides.json (поле Name строки DT_MobStats); без неё пересборка вернула бы английский.
    import asda_ru_dt_overrides as _RO
    view = [{"Row": str(r["row"]), "Name": r["name"], "_r": r} for r in rows]
    _RO.apply_rows("DT_MobStats", view, lambda v: v["Row"], log)
    for v in view:
        v["_r"]["name"] = v["Name"]
    # ★ 18.10.2026 (решение владельца: эталон HP мобов — СЕРВЕР): MaxHp из серверного NpcServer\Monster\Monster.ai +0x2D
    #   вместо клиентского Mob_list.bin +0x94 (190 типов расходились — целый моб показывался неполным; дефект оригинала).
    #   Ведомость Tools/data/mob_maxhp_server_overrides.json (was — клиент, now — сервер); без неё пересборка вернула бы клиентские.
    hp_view = [{"Row": str(r["row"]), "MaxHp": r["max_hp"], "_r": r} for r in rows]
    _RO.apply_rows("DT_MobStats", hp_view, lambda v: v["Row"], log,
                   path=_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "data", "mob_maxhp_server_overrides.json"))
    for v in hp_view:
        v["_r"]["max_hp"] = v["MaxHp"]
    return rows


def main():
    rows = read_rows()
    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    dt = unreal.EditorAssetLibrary.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else None
    if not dt:
        factory = unreal.DataTableFactory()
        factory.set_editor_property("struct", unreal.load_object(None, "/Script/AsdaReborn.AsdaMobStatsRow"))
        dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(OUT_NAME, OUT_PKG, unreal.DataTable, factory)
    if not dt:
        log("DT не создан")
        return
    head = ("Row,Model,Name,HalfX,HalfY,HalfZ,MaxHp,Level,Exp,SoundRange,"
            "SizeClass,SizePercent,Rank,Aggression,AttackFirst,Lives,PortraitTex,PortraitCell")
    body = "\n".join(
        '%d,"%s","%s",%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"%s",%d' % (
            r["row"], r["model"], r["name"].replace('"', "'"), r["hx"], r["hy"], r["hz"],
            r["max_hp"], r["level"], r["exp"], r["snd"],
            r["size_class"], r["size_percent"],
            r["rank"], r["aggr"], r["first"], r["lives"],
            r["ptex"], r["pcell"]) for r in rows)
    ok = _DTC.fill_and_save(unreal, dt, head + "\n" + body, log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("DT_MobStats: строк %d, заполнение=%s" % (len(rows), ok))
    # Контрольная выборка (доказательство): мелкий / средний / босс.
    for probe in (1, 13, 240, 593):
        for r in rows:
            if r["row"] == probe - 1:
                log("  id=%-4d %-24s halfY=%7.1f ⇒ H=%7.1f | lvl=%-3d aggr(РАНГ/кольцо)=%d "
                    "rank(СТИХИЯ 0..5)=%d lives=%d | класс=%d процент=%d ⇒ scaleAll=%.2f | "
                    "портрет=%s ячейка=%d" % (
                        probe, r["name"][:24], r["hy"], 2 * r["hy"] + 50.0, r["level"], r["aggr"],
                        r["rank"], r["lives"], r["size_class"], r["size_percent"],
                        r["size_percent"] / 100.0, r["ptex"], r["pcell"]))


if __name__ == "__main__":   # ★ без побочных эффектов при импорте (правило проекта)
    main()
