# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_weapon_glow_fx.py — BP-заготовки СВЕЧЕНИЯ ЗАТОЧЕННОГО ОРУЖИЯ И ЩИТА.
#
# ЭТАЛОН (Reports/weapon_glow_re.md):
#   ОРУЖИЕ  — CCharacter_UpdateHeldItemEffect@0xA4C070 (entity_char.c:28277): экип-слот 9 (itemType 9),
#             инстанс 57 б, заточка = байт +0x18. Имя .jeb строит CCharacter_BuildWeaponJebFileName@0xA4CB10:
#               "%s_%s_%s_%s_%s.jeb" = <база по itemSubType>_<буква imgSubType>_<грейд>_<скин>_<состояние>
#             база: subType<=1→"ohs", 2→"spr", 3→"ths", 5→"stf", 8|10→"bow", 9→"xbow", 11→"bali"
#             (строки в EXE @0x13AEA40/3C/38/2C/20 = "ohs"/"spr"/"ths"/"bow"/"stf", 0x13AEA30="xbow",
#              0x13AEA24="bali"; проверено чтением из asda.dmp).
#   ЩИТ     — CCharacter_UpdateEquipAuraEffect@0xA4DD60 (entity_char.c:28693): экип-слот 8 (itemType 8),
#             имя строит itemBuildJebEffectName@0xA4E790 (item_inventory.c:3577) с ФИКСИРОВАННОЙ базой
#             "shd" (строка @0x13AEAC8) — от типа оружия НЕ зависит.
#   буква   — imgSubType (ImgRecord+212): 1..12 → a..l, 0 → пусто (файла нет ⇒ свечения нет).
#   грейд   — ItemRecord.itemGrade (редкость): 0→"co", 1→"uc", 2→"ra", 3→"he", 4→"le" (ассетов "le" НЕТ).
#   скин    — оба сайта зовут билдер с a4=0 ⇒ ВСЕГДА "ma" ("s1"/"s2" в этой сборке недостижимы).
#   состояние — по уровню ЗАТОЧКИ: 5→"01", 6→"02", >=7→"03" (ниже 5 свечения нет вообще).
#   спец    — itemId 41956/56048 при a4==0 → "test-000.jeb" (жёстко зашито в билдере имени).
#
# Пайплайн — копия asda_build_char_fx.py (parse_jeb + PE.build_emitter + BP на AsdaEffectActor).
# BP-имя = BP_Fx_<база jeb>.  Запуск: Tools/run_headless.bat Tools\asda_build_weapon_glow_fx.py
# ★ Аудит №4 (волна 3): форма сборки BP переехала в `asda_fx_common`; здесь остались ТОЛЬКО
#   отличия канала заточки (подмена типов 0x13/0x14/0x19, атлас, ring_track_pos=True).
import sys, os, re, struct
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fx_common as FXC
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

SRC_DIR = _P.EFFECT_MODEL_ORIG_DIR
DATA_DIR = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "Character")
OUT_PKG = _U.G_EFFECTS

# База имени по itemSubType оружия (эталон CCharacter_BuildWeaponJebFileName).
# ★ Волна 3 п.16 (15.09.2026): таблица баз — ОДНА на Python, в Tools/asda_gen_fx_light_ini.py (там же --check сверяет её
#   с регэкспом семейства weapon_glow в JSON и с C++ AsdaWeaponGlow). Модуль генератора при импорте ничего не делает.
from asda_gen_fx_light_ini import WEAPON_GLOW_BASE_BY_SUBTYPE as WEAPON_BASE, SHIELD_GLOW_BASE as SHIELD_BASE, weapon_glow_bases as _glow_bases
# ★ Хвосты волны 3 (15.09.2026): таблица грейдов — тоже одна на Python (генератор, сверка с C++ AsdaWeaponGlow::ComposeJeb в --check)
from asda_gen_fx_light_ini import WEAPON_GLOW_GRADE_CODE as GRADE_CODE
STATE_CODE = {5: "01", 6: "02"}          # >=7 → "03"
# Спец-предметы: билдер имени подменяет имя на test-000.jeb ДО всех таблиц.
EXTRA = ["test-000"]

# Имя свечения: база (оружие + щит, из общей таблицы) _ буква _ грейд _ma_ ступень
#   (★ хвосты волны 3: альтернативы грейда — из той же таблицы, в порядке кода грейда: co|uc|ra|he|le)
_NAME_RE = re.compile(r"^(%s)_[a-l]_(%s)_ma_(01|02|03)$" % ("|".join(_glow_bases()), "|".join(GRADE_CODE[k] for k in sorted(GRADE_CODE))))


def log(m): unreal.log_warning("[glowfx] %s" % m)


def jeb_name(base, img_sub_type, item_grade, enhance):
    """1:1 с эталоном: <base>_<letter>_<grade>_ma_<state>. Пустая строка = имени нет (свечения не будет)."""
    if enhance < 5: return ""
    letter = chr(ord("a") + img_sub_type - 1) if 1 <= img_sub_type <= 12 else ""
    if not letter: return ""
    grade = GRADE_CODE.get(item_grade)
    if grade is None: return ""
    state = STATE_CODE.get(enhance, "03")
    return "%s_%s_%s_ma_%s" % (base, letter, grade, state)


def db_reachable():
    """Имена, ДОСТИЖИМЫЕ по нашей item-БД: перебор всех надетых оружий (itemType 9) и щитов (itemType 8)
    × три состояния заточки. Возвращает (set имён, статистика)."""
    il = open(os.path.join(DATA_DIR, "item_list.dec.bin"), "rb").read()
    ig = open(os.path.join(DATA_DIR, "item_img_table.bin"), "rb").read()
    n = struct.unpack_from("<i", il, 4)[0]
    names, stat = set(), {"weapon": 0, "shield": 0, "no_letter": 0, "no_asset": set()}
    for i in range(n):
        o, io = 20 + 248 * i, 20 + 300 * i
        if io + 300 > len(ig) or o + 248 > len(il): break
        itype = struct.unpack_from("<i", il, o + 0x80)[0]
        if itype not in (8, 9): continue
        sub = struct.unpack_from("<i", il, o + 0x84)[0]
        grade = struct.unpack_from("<i", il, o + 0xEC)[0]
        isub = struct.unpack_from("<i", ig, io + 212)[0]
        base = SHIELD_BASE if itype == 8 else WEAPON_BASE.get(sub)
        if not base: continue
        stat["shield" if itype == 8 else "weapon"] += 1
        for enh in (5, 6, 7):
            nm = jeb_name(base, isub, grade, enh)
            if not nm: stat["no_letter"] += 1; continue
            if os.path.isfile(os.path.join(SRC_DIR, nm + ".jeb")): names.add(nm)
            else: stat["no_asset"].add(nm)
    return names, stat


def asset_reachable():
    """Все .jeb в ресурсах, попадающие под достижимый шаблон (скин ma, состояния 01/02/03)."""
    out = set()
    for f in os.listdir(SRC_DIR):
        if not f.lower().endswith(".jeb"): continue
        b = f[:-4].lower()
        if _NAME_RE.match(b): out.add(b)
    return out


def build_one(J, PE, eff_name):
    out_name = FXC.bp_name(eff_name)
    jb = FXC.read_jeb(J, SRC_DIR, eff_name, log)
    if jb is None: return None
    emitters = jb.get("emitters", [])
    blend_add = FXC.blend_mode(jb)
    bp, cdo = FXC.ensure_effect_bp(out_name, OUT_PKG, log)
    if bp is None: return None
    cdo.set_editor_property("effect_name", eff_name)
    ems, skipped = [], 0
    for em in emitters:
        try:
            # ★ КВАДЫ-ФЛИПБУКИ 0x13/0x14/0x19 (реверс weapon_glow_re.md §D, дизасм лоадеров и vtable):
            #   0x13 (ctor 0x123DD70, лоадер 0x11795B0) — плоский квад + атлас: РЕНДЕР ТОТ ЖЕ, что у 0xB/0x18;
            #   0x14 (ctor 0x123EB70, лоадер 0x1179A10) и 0x19 (ctor 0x1246DC0, лоадер 0x117BA00) — квад
            #   + атлас + billboardMode: РЕНДЕР ТОТ ЖЕ, что у 0xC (spriteBuildTransformAndDraw@0x120CF10).
            #   Рантайм новых типов не знает, поэтому подставляем эквивалентный jeb_type; атлас-раскадровку
            #   докладываем ниже (в квад-ветке общего генератора её нет).
            orig_type = em.get("type")
            atlas = (em.get("params", {}).get("atlasCols"), em.get("params", {}).get("atlasRows"))
            if orig_type == 0x13:            em = dict(em, type=0x18)
            elif orig_type in (0x14, 0x19):  em = dict(em, type=0xC)
            # ★ ring_track_pos=True — ТОЛЬКО здесь (свечение оружия/щита). Возвращает офсет колец 0xE
            #   с трек-узлами, который общий импортёр терял (см. шапку PE.build_emitter). Карты и
            #   прочие генераторы вызывают build_emitter БЕЗ флага ⇒ их BP не меняются.
            # ★★★ kf_seed_nodes (этап 3 раскатки, `Reports/item_fx_ring_motion.md`): эталонный
            #   лоадер достраивает трек СТАРТ-узлом (t=0) и КОНЕЦ-узлом (t=lifetime) из базовых сидов
            #   записи, в файле лежат только промежуточные ключи (`animSampleCurveAtTime@0x11FCEC0`,
            #   `effectUpdateKeyframeTrack@0x11F7B10`). Замер по каналу заточки: 388 эффектов с
            #   ключами, в них 32 кольца — ВСЕ 32 с ЕДИНСТВЕННЫМ ключом (были статичны) — и 924 квада.
            #   Откат: `ASDA_FX_NO_KF_SEED=1` + пересборка канала.
            # ★★★ etalon_end (2026-08-14, `Reports/fx_scale_timing_audit.md` §4): канал
            #   заточки переопределения `end_mode` не имел ⇒ 160 квадов с keyframe-треком и
            #   `endMode = 3` (ФРИЗ на последнем узле) перезапускались каждые life секунд, а
            #   24 квада с `endMode = 0` (СТОП) зацикливались. Откат: `ASDA_FX_NO_ETALON_ENDMODE=1`.
            fx = PE.build_emitter(em, blend_add, ring_track_pos=True,
                                  kf_seed_nodes=FXC.kf_seed_enabled(),
                                  etalon_end=FXC.etalon_end_enabled())
            if fx is None:
                skipped += 1
                continue
            if orig_type in (0x13, 0x14, 0x19) and atlas[0] and atlas[1]:
                try:
                    fx.set_editor_property("atlas_cols", int(atlas[0]))
                    fx.set_editor_property("atlas_rows", int(atlas[1]))
                except Exception: pass
            # ★ Флаги ЭФФЕКТОВ ПЕРСОНАЖА (как у крыльев, asda_build_char_fx): эффект живёт в ПОЛНОМ
            #   трансформе живой кости — строгий знак канальных вращений, pos-канал через полную W,
            #   орбита квадов, basePos кольца. Разбор — в шапке `asda_fx_common.apply_bone_fx_flags`.
            #   Набор квадов у заточки БЕЗ типа 2 (у 0x13/0x14/0x19 тип уже подменён выше).
            FXC.apply_bone_fx_flags(fx, em.get("type"))
            ems.append(fx)
        except Exception as ex:
            skipped += 1
            log("  %s: эмиттер %s (тип 0x%X) пропущен: %s" % (eff_name, em.get("name", "?"), em.get("type", 0), ex))
    # ★ ЧЕСТНО ПРО СОХРАНЕНИЕ (2026-08-21, см. шапку `asda_fx_common.finish_effect_bp`):
    #   занятый другим процессом `.uasset` НЕ сохраняется, а строка «создан…» печаталась всё равно.
    if not FXC.finish_effect_bp(bp, cdo, ems):
        log("НЕ СОХРАНЁН %s: файл занят другим процессом (закрой редактор UE и повтори)" % out_name)
        return (0, skipped)
    return (len(ems), skipped)


def main():
    mods = FXC.import_fx_modules(log)
    if mods is None: return
    J, PE = mods
    FXC.point_pe_to(PE, OUT_PKG)

    assets = asset_reachable()
    try:
        db, stat = db_reachable()
        log("item-БД: оружий(тип9)=%d, щитов(тип8)=%d; комбинаций с ассетом=%d; без буквы imgSubType=%d; "
            "без ассета=%d" % (stat["weapon"], stat["shield"], len(db), stat["no_letter"], len(stat["no_asset"])))
        only_db = sorted(db - assets)
        if only_db: log("В БД есть, а ассета нет (первые 10): %s" % ", ".join(only_db[:10]))
    except Exception as e:
        db = set(); log("item-БД не прочитана (%s) — строю по ассетам" % e)

    jebs = sorted(assets)
    for e in EXTRA:
        if e not in jebs and os.path.isfile(os.path.join(SRC_DIR, e + ".jeb")): jebs.append(e)
    log("к сборке %d BP (ассеты %d + спец %d); из них подтверждено item-БД: %d"
        % (len(jebs), len(assets), len(jebs) - len(assets), len(db & assets)))

    ok = miss = empty = 0
    skipped_total = 0
    for eff in jebs:
        r = build_one(J, PE, eff)
        if r is None: miss += 1; continue
        ok += 1
        if r[0] == 0: empty += 1
        skipped_total += r[1]
    log("ГОТОВО: BP создано %d/%d (пустых %d, не создано %d); эмиттеров пропущено (неподдержанный тип) %d"
        % (ok, len(jebs), empty, miss, skipped_total))


# ★ Закон [[tools-no-import-side-effects]]. Было `__name__ == "__main__"`: от импорта защищает,
#   но НЕ срабатывает, когда UE запускает файл через -ExecutePythonScript (там `__name__` не
#   обещан) — сборщик молча ничего не делал. `is_direct_run` покрывает оба пути; разбор признака
#   — в шапке `asda_fx_common.is_direct_run`.
if FXC.is_direct_run(globals()):
    main()
