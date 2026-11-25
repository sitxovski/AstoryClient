# Copyright (c) 2026 @sitxovski (Discord)
# Headless-импорт мировых КЛИПОВ ПРИМЕНЕНИЯ НАВЫКОВ (anim_100..) на СУЩЕСТВУЮЩИЙ скелет
# ch1_{m|f}_cremmy_preview_Skeleton. Меши/скелет/anim_00..85 НЕ трогаются (переимпорт скелета запрещён).
#
# Источник: RawAssets/Models/Skeletal/Characters/skillclips/ch1_{m|f}_cremmy_preview.glb
#   (готовит `export_char_skillclips.py` по таблице, снятой с бинаря `asda_dump_skill_dispatch.py`).
#   Ожидаемые индексы берутся из skillclips_map.json — «изобретения» списка нет.
#
# Механика 1:1 с `import_char_weaponsets.py`: Interchange-pipeline override
#   common_skeletal_meshes_and_animations_properties.skeleton = существующий + import_only_animations,
#   импорт во ВРЕМЕННУЮ папку → перенос каждого "…anim_NN" на место ch1_{g}_cremmy_previewanim_NN.
#
# Запуск (редактор ДОЛЖЕН быть закрыт — иначе save не пройдёт):
#   "D:\Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" AsdaReborn.uproject
#     -ExecutePythonScript="Tools\import_char_skillclips.py" -unattended -nopause -nosplash -nosound
#     -NullRHI -stdout -abslog=...
import json
import os
import re
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "skillclips"))
MAP_JSON = os.path.join(RAW, "skillclips_map.json")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # ★ волна 2 п. 7: Tools в пути — для asda_ue_paths
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_char_clips as _CC   # ★ дедуп №2 В1 Т11: общий импорт клипов персонажа (Interchange «только анимации»)
import asda_skillclip_guard as GUARD   # ★ 09.10.2026 (п. 6): защита anim_205 «одержимости» и прочих чужих/правленных клипов
import shutil   # бэкап .uasset перед явным переимпортом
DEST_BASE = _U.G_MODELS_CHARACTERS + "/ch1_%s_cremmy_preview/SkeletalMeshes"
SKEL = {g: _U.skel_mesh(_U.G_MODELS_CHARACTERS, _U.char_preview_name(g == "m"), "_Skeleton") for g in ("m", "f")}
TMP = "/Game/Tmp_SkillClips"

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m):
    unreal.log("[import_skillclips] " + str(m))


def guard_targets(g, expect):
    """★ 09.10.2026 (п. 6 владельца): какие номера из карты можно писать. Чужие номера (anim_205 одержимости, парные 205..224,
    рыбалка 225..232, всё < 100) и ассеты Content вне карты — запрещены всегда (импорт останавливается). Уже существующий СВОЙ
    ассет по умолчанию не перезаписывается (его могли править в редакторе) — только по явному списку
    ASDA_SKILLCLIPS_REIMPORT="m:102,105;f:110", с бэкапом .uasset. Возвращает (писать, пропущены, запрещены)."""
    bad = [(n, GUARD.why(g, n, expect)) for n in expect if n in GUARD.forbidden(g, expect)]
    force = GUARD.parse_reimport(os.environ.get("ASDA_SKILLCLIPS_REIMPORT", "")).get(g, set())
    disk = GUARD.on_disk(g)
    write, skipped = [], []
    for n in expect:
        if n in disk and n not in force:
            skipped.append(n)
        else:
            write.append(n)
    return write, skipped, bad


def backup_uasset(g, n):
    """Копия .uasset клипа до перезаписи — `asda_headless_tool.backup_file` (★ Т3, 11.10.2026): набор
    @@ASTORY_DEV_ROOT_ESC@@\\_backup\\<дата>_skillclips\\<путь от @@ASTORY_DEV_ROOT_ESC@@> (корень и раскладка те же), сверка размера. Было: повторный
    импорт в тот же день ПЕРЕЗАПИСЫВАЛ копию; теперь первая копия дня — исходник до правки — не трогается.
    Возвращает True — копия есть и сверена (или исходника нет, перезаписывать нечего); False — сверка не прошла."""
    import asda_headless_tool as HT
    src = GUARD.asset_file(g, n)
    if not os.path.exists(src):
        return True
    try:
        dst = HT.backup_file(src, "skillclips", log=lambda _m: None)
    except RuntimeError as e:
        log("бэкап %s: !! %s" % (os.path.basename(src), e))
        return False
    log("бэкап %s → %s (размер совпал)" % (os.path.basename(src), dst))
    return True


def import_gender(g, expect):
    write, skipped, bad = guard_targets(g, expect)
    if bad:
        log("!! [%s] СТОП: карта навыков указывает на защищённые номера — импорт не выполняется: %s" % (g, bad))
        return False
    if skipped:
        log("[%s] уже есть в Content, НЕ перезаписываются (п. 6; переимпорт — ASDA_SKILLCLIPS_REIMPORT): %d шт. %s"
            % (g, len(skipped), skipped))
    if not write:
        log("[%s] новых клипов нет — импорт не нужен" % g)
        return True
    expect = write
    glb = os.path.join(RAW, "ch1_%s_cremmy_preview.glb" % g)
    if not os.path.exists(glb):
        log("НЕТ glb: %s — пропуск" % glb)
        return False
    skel = unreal.load_asset(SKEL[g])
    if not skel:
        log("!! скелет не найден: %s" % SKEL[g])
        return False
    tmp = TMP + "_" + g
    _CC.import_anims_to_tmp(glb, skel, tmp)   # ★ дедуп №2 Т11: Interchange «только анимации» во временную папку (общий)

    found = {}
    for ad in AR.get_assets_by_path(unreal.Name(tmp), recursive=True):
        if str(ad.asset_class_path.asset_name) != "AnimSequence":
            continue
        m = re.search(r"anim_(\d+)$", str(ad.asset_name))
        if m:
            found[int(m.group(1))] = str(ad.package_name)
    miss = [n for n in expect if n not in found]
    if miss:
        log("!! [%s] не импортировались клипы: %s" % (g, miss))
        return False

    ok = True
    for n in expect:
        dst = (DEST_BASE % g) + "/ch1_%s_cremmy_previewanim_%d" % (g, n)
        if EAL.does_asset_exist(dst):
            # ★ п. 6: сюда доходит только явный переимпорт (guard_targets) — сначала бэкап .uasset.
            if not backup_uasset(g, n):
                log("!! [%s] anim_%d: бэкап не прошёл проверку размера — клип НЕ перезаписан" % (g, n))
                ok = False
                continue
            EAL.delete_asset(dst)
        if not EAL.rename_asset(found[n], dst):
            log("!! [%s] rename %s -> %s не удался" % (g, found[n], dst))
            ok = False
            continue
        EAL.save_asset(dst, only_if_is_dirty=False)
    EAL.delete_directory(tmp)     # выбрасывает "__ref_ignore" и остатки

    bad = []
    for n in expect:
        dst = (DEST_BASE % g) + "/ch1_%s_cremmy_previewanim_%d" % (g, n)
        a = unreal.load_asset(dst)
        if not a:
            bad.append((n, "нет ассета"))
            continue
        ln = float(a.get_editor_property("sequence_length"))
        sk = a.get_editor_property("skeleton")
        if ln <= 0 or sk is None or sk.get_name() != os.path.basename(SKEL[g]):
            bad.append((n, "len=%.3f skel=%s" % (ln, sk.get_name() if sk else "NONE")))
    if bad:
        log("!! [%s] проблемные клипы: %s" % (g, bad))
        ok = False
    else:
        log("[%s] OK: %d клипов навыков на скелете %s" % (g, len(expect), os.path.basename(SKEL[g])))
    return ok


def main():
    if not os.path.exists(MAP_JSON):
        log("НЕТ %s — сначала python Tools\\export_char_skillclips.py" % MAP_JSON)
        return
    cmap = json.load(open(MAP_JSON, encoding="utf-8"))
    genders = [a.lower() for a in sys.argv if a.lower() in ("m", "f")] or ["m", "f"]
    ok = True
    for g in genders:
        expect = sorted(set(cmap.get(g, {}).values()))
        if not expect:
            log("[%s] карта пуста — пропуск" % g)
            continue
        ok = import_gender(g, expect) and ok
    log("ИТОГ: %s" % ("ВСЕ OK" if ok else "ЕСТЬ ПРОБЛЕМЫ"))


# ★ ЗАКОН tools-no-import-side-effects: `__main__` тут не годится (при -ExecutePythonScript имя модуля
#   не гарантировано), поэтому проверяем «имя НЕ равно имени модуля» — при обычном импорте оно им и будет.
# ★★★ Закон tools-no-import-side-effects: main() только при ПРЯМОМ запуске.
#   Прежняя проверка `__name__ != "import_char_skillclips"` ловила лишь импорт ровно под этим именем:
#   `import Tools.import_char_skillclips` или `importlib.util.spec_from_file_location("X", ...)` дают другое
#   имя — и main() выстреливал бы. Надёжный признак импорта — `__spec__`: его выставляет ЛЮБАЯ
#   машинерия импорта (import / importlib / пакет / runpy.run_module) и НЕ выставляет прямое
#   исполнение файла (python file.py, UE -ExecutePythonScript). `python -m` даёт "__main__".
def _is_direct_run():
    """True — файл запущен НАПРЯМУЮ, а не импортирован."""
    return __name__ == "__main__" or globals().get("__spec__") is None


if _is_direct_run():
    main()
