# Copyright (c) 2026 @sitxovski (Discord)
# asda_apply_blend_groups.py — РАСКАТКА ПРОЗРАЧНОСТИ на три группы: декор карт, сцена
# создания/выбора персонажа, модели предметов.  Отчёт: `Reports/blend_rollout_maps_items.md`.
#
# ЧТО ДЕЛАЕТ (по шагам, каждый печатает числа):
#   0. собирает мастер `M_AsdaObjectBlendLit` (asda_build_blend_material);
#   1. ДЕКОР КАРТ  — переимпорт 10 скелетных .glb в /Game/AsdaAssets/Models/NPC + перепривязка
#      материалов ТОЛЬКО этих моделей (папку NPC целиком не трогаем: там 125 моделей NPC, их ведёт
#      отдельная задача);
#   2. ПРЕДМЕТЫ    — переимпорт 39 статик-.glb в /Game/AsdaAssets/Models/Items + перепривязка;
#   3. СЦЕНА       — шкатулка `cre_creation01-a_00` и скелетный фолбэк задника `cre_creback-a_00`
#      (переимпорт + перепривязка) и ВИДИМЫЙ задник = 697 статик-акторов уровня Login: им
#      переназначаются MIC по флагу `blend` из `RawAssets/backdrop/backdrop_elements.json`;
#   4. СВОДКА      — сколько MIC на каждом мастере в каждой группе.
#
# ★ Метка `_blend` в имени слота ставится ЭКСПОРТЁРАМИ по данным `.jbm`
#   (`asda_jbm.is_alpha_blend`: рендер-бит `mr+0x004 & 0x1|0x2` И техника tNoSkin), поэтому
#   результат воспроизводится любым штатным переимпортом, а не только этим скриптом.
#
# Предварительно (вне UE):
#   python asda_build_npc_fixed.py mdel_   &&  python asda_build_npc_fixed.py natu_
#   set ASDA_MODEL_CATS=Items && python asda_build_models.py
#   python export_casket.py  &&  python export_creback.py  &&  python asda_export_backdrop_elements.py
# Запуск: UnrealEditor-Cmd.exe <proj> -ExecutePythonScript=<этот файл> -unattended -nopause -nosplash -nosound -NullRHI
import os, json
import unreal
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ue_paths as _U
import asda_material_policy as POL
import asda_build_blend_material as BLENDMAT
import asda_build_backdrop_elements as BD

RAW = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "RawAssets"))
NPC_GLB   = os.path.join(RAW, "Models", "Skeletal", "NPC")
ITEM_GLB  = os.path.join(RAW, "Models", "Items")
CHAR_GLB  = os.path.join(RAW, "Models", "Skeletal", "Characters")
BD_JSON   = os.path.join(RAW, "backdrop", "backdrop_elements.json")

# 10 моделей декора карт с помеченными подсетками (jbm_blend_flag_study.md §3.5)
DECOR = ["mdel_buryd-a_00", "mdel_csomachine-a_00", "mdel_dmirror-a_00", "mdel_gmirror-a_00",
         "mdel_irontree-a_00", "mdel_mactree-a_00", "mdel_oldtree-a_00", "mdel_recall00-a_00",
         "mdel_warningsign-a_00", "natu_poisonpt-a_00"]
SCENE_SKEL = ["cre_creation01-a_00", "cre_creback-a_00"]

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m):
    unreal.log("[blend-раскатка] " + str(m))


# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «noportrait_first» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("noportrait_first")


def find_tex(name, tex, kind=None):
    """Текстура для слота/MIC (пресет «blend» библиотеки = прежняя копия этого файла).
    ★ 15.09.2026: kind — тип модели (MI.kind_for_package папки NPC/Items/Char): при дублях имени копия из папки
    своего типа, как в оригинале (`Reports/dedup_audit/tex_dup_rule.md`)."""
    return MI.find_tex(name, tex, "blend", kind=kind)


def glb_material_names(path):
    """Имена материалов .glb — БЕЗ pygltflib (в питоне UE его нет; замер: ImportError).

    .glb = [12б хедер][u32 len][u32 'JSON'][json]... Читаем только JSON-чанк и отдаём его текстом:
    вызывающему достаточно проверить наличие подстроки-метки.
    """
    import struct as _st
    with open(path, "rb") as f:
        head = f.read(20)
        if len(head) < 20 or head[:4] != b"glTF":
            return ""
        clen, ctype = _st.unpack_from("<II", head, 12)
        if ctype != 0x4E4F534A:            # 'JSON'
            return ""
        return f.read(clen).decode("utf-8", "replace")


def import_batch(paths, dest):
    """Импорт пачкой (replace_existing=True) — asda_model_import_ue.import_batch."""
    return MI.import_batch(paths, dest)


def wire_scope(root, tex, stat):
    """Перепривязка MIC ВНУТРИ одной папки модели. Возвращает (всего, на прозрачном мастере)."""
    n = nb = 0
    for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        a = unreal.load_asset(str(ad.package_name))
        name = str(ad.asset_name)
        mpath = POL.master_for(name, name)
        m = unreal.load_asset(mpath)
        if not m:
            unreal.log_warning("НЕТ мастера %s" % mpath); continue
        MEL.set_material_instance_parent(a, m)
        tp = find_tex(name, tex, MI.kind_for_package(root))   # ★ 15.09: тип модели по папке (NPC/Items/Char)
        if tp:
            MEL.set_material_instance_texture_parameter_value(a, "Tex", unreal.load_asset(tp))
        a.modify(); EAL.save_loaded_asset(a, False)
        n += 1
        stat[m.get_name()] = stat.get(m.get_name(), 0) + 1
        # ★ 2026-08-18 (откат раскатки): при POL.BLEND_ROLLOUT_ENABLED=False `master_for()` выше
        #   отдаёт masked даже помеченным слотам, поэтому здесь честно выйдет 0. Это не поломка
        #   скрипта, а действующая политика — счётчик оставлен, чтобы при возврате раскатки
        #   (один флаг в asda_material_policy.py) он снова показал реальное число.
        if mpath == POL.MASTER_BLEND:
            nb += 1
    return n, nb


def slot_report(mesh_pkg):
    """Список (имя слота, имя материала, мастер) — для проверки числами."""
    m = unreal.load_asset(mesh_pkg)
    if not m: return []
    out = []
    if isinstance(m, unreal.StaticMesh):
        slots = m.get_editor_property("static_materials")
        for s in slots:
            mi = s.get_editor_property("material_interface")
            par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
            out.append((str(s.get_editor_property("material_slot_name")),
                        mi.get_name() if mi else "НЕТ", par.get_name() if par else "—"))
    else:
        for s in m.get_editor_property("materials"):
            mi = s.get_editor_property("material_interface")
            par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
            out.append((str(s.get_editor_property("material_slot_name")),
                        mi.get_name() if mi else "НЕТ", par.get_name() if par else "—"))
    return out


def do_group(title, names, glb_dir, dest_root, tex, stat, wipe=False):
    """wipe=True — снести папку модели ПЕРЕД импортом.

    ★ ЗАЧЕМ (замер 2026-08-18). Переимпорт `replace_existing=True` ПОВЕРХ существующего
      SkeletalMesh НЕ переименовывает уже существующие слоты: у шкатулки `cre_creation01-a_00`
      четвёртый слот так и остался `cre_creation01-a_03` вместо `cre_creation01-a_03_blend`, и
      проверка честно показала «прозрачных слотов 0 при ожидаемом 1». Штатные точечные импортёры
      проекта (`import_casket.py`, `import_creback.py`) именно поэтому сносят папку модели перед
      импортом — повторяем их поведение для группы сцены.
    """
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, отчёт Reports/dedup_audit/tex_rebind_2026-09-15.md): шкатулка cre_creation01-a_00
    #   и задник cre_creback-a_00 (шаг 3) — снос папки, переимпорт и перевязка MIC только с ASDA_ALLOW_LOGIN_WRITE=1.
    # ★ раздел 22 (16.09.2026): save_dir_login_safe — dest_root группы «сцена» это /Game/AsdaAssets/Models/Char,
    #   где лежат пакеты сцены логина; сохранять папку целиком нельзя даже после отсева имён моделей.
    from asda_login_guard import login_package_blocked, save_dir_login_safe
    names = [n for n in names if not login_package_blocked("%s/%s" % (dest_root, n), "asda_apply_blend_groups.py")]
    if not names:
        log("%s: все модели группы — сцена логина, пропуск (ASDA_ALLOW_LOGIN_WRITE=1)" % title)
        return 0, 0
    files = [os.path.join(glb_dir, n + ".glb") for n in names]
    have = [f for f in files if os.path.isfile(f)]
    if len(have) != len(files):
        log("%s: НЕТ %d .glb из %d — сначала соберите их (см. шапку)" % (title, len(files) - len(have), len(files)))
    if not have:
        return 0, 0
    if wipe:
        for n in names:
            root = "%s/%s" % (dest_root, n)
            if EAL.does_directory_exist(root):
                log("%s: чистый импорт — сношу %s" % (title, root))
                EAL.delete_directory(root)
    log("%s: переимпорт %d моделей → %s" % (title, len(have), dest_root))
    for i in range(0, len(have), 4):                 # чанк 4 — меньше всплеск памяти/GPU
        import_batch(have[i:i + 4], dest_root)
        save_dir_login_safe(dest_root, "asda_apply_blend_groups.py")   # ★ раздел 22: не папкой целиком
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    tot = totb = 0
    for n in names:
        root = "%s/%s" % (dest_root, n)
        if not EAL.does_directory_exist(root):
            continue
        a, b = wire_scope(root, tex, stat)
        tot += a; totb += b
        log("   %-26s MIC=%-3d прозрачных=%-3d" % (n, a, b))
    save_dir_login_safe(dest_root, "asda_apply_blend_groups.py")   # ★ раздел 22: не папкой целиком
    log("%s: ИТОГО MIC=%d, из них на прозрачном мастере=%d" % (title, tot, totb))
    return tot, totb


def do_backdrop_actors():
    """ВИДИМЫЙ задник логина = 697 статик-акторов. Переназначаем им MIC по флагу `blend`.

    ★ Полную пересборку (asda_build_backdrop_elements.main) здесь НЕ гоняем: она пересоздаёт 491
      StaticMesh и все акторы. Материал — единственное, что меняется, и он назначается на компонент.
      Сам билдер уже исправлен, поэтому будущая полная пересборка даст тот же результат.
    """
    if not os.path.isfile(BD_JSON):
        log("НЕТ %s — задник пропущен" % BD_JSON); return 0, 0
    with open(BD_JSON, "r", encoding="utf-8") as f:
        elements = json.load(f)["elements"]
    by_label = {"BackdropElem_%s" % e["name"]: e for e in elements}
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): шаг сохраняет Login — только с ASDA_ALLOW_LOGIN_WRITE=1
    import sys as _sys
    _sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from asda_login_guard import login_write_allowed
    if not login_write_allowed():
        log("[LOGIN-GUARD] задник логина пропущен: уровень Login не сохраняется без ASDA_ALLOW_LOGIN_WRITE=1 (в автоматике для сохранения самого уровня нужна ещё ASDA_ALLOW_LOGIN_MAP_WRITE=1 — C++-сторож)")
        return 0, 0
    les.load_level(BD.LEVEL)
    n = nb = miss = 0
    for a in eas.get_all_level_actors():
        lbl = a.get_actor_label()
        e = by_label.get(lbl)
        if e is None:
            continue
        smc = a.get_component_by_class(unreal.StaticMeshComponent)
        if not smc:
            miss += 1; continue
        mic = BD.get_material(e["tex"], bool(e.get("blend")))
        if mic:
            smc.set_material(0, mic); n += 1
            if e.get("blend"): nb += 1
        else:
            miss += 1
    les.save_current_level()
    log("ЗАДНИК логина: акторов обновлено=%d (прозрачных=%d), без материала=%d, элементов в JSON=%d"
        % (n, nb, miss, len(elements)))
    return n, nb


def main():
    # ★ ОХВАТ ШАГОВ: ASDA_BLEND_STEPS="3" — выполнить только перечисленные шаги (1=декор,
    #   2=предметы, 3=сцена). Нужен, когда часть шагов уже прошла, а на другой части файлы были
    #   ЗАНЯТЫ вторым процессом редактора (`MoveFile ... Error Code 32`) — повторяем только их.
    steps = set(os.environ.get("ASDA_BLEND_STEPS", "123"))
    log("0) мастер прозрачности (шаги: %s)" % "".join(sorted(steps)))
    BLENDMAT.build()

    tex = tex_index()
    log("текстур в индексе: %d" % len(tex))
    stat = {}

    d_tot = d_blend = 0
    if "1" in steps:
        log("1) ДЕКОР КАРТ (mdel_*/natu_*)")
        d_tot, d_blend = do_group("декор", DECOR, NPC_GLB, _U.G_MODELS_NPC, tex, stat)

    i_tot = i_blend = 0
    if "2" in steps:
        log("2) ПРЕДМЕТЫ (Data/Item/Model)")
        import glob as _glob
        item_names = []
        for p in sorted(_glob.glob(os.path.join(ITEM_GLB, "*.glb"))):
            try:
                if POL.BLEND_SUFFIX in glb_material_names(p):
                    item_names.append(os.path.splitext(os.path.basename(p))[0])
            except Exception as e:
                unreal.log_warning("не прочитан %s: %s" % (p, e))
        log("   моделей предметов с меткой: %d" % len(item_names))
        i_tot, i_blend = do_group("предметы", item_names, ITEM_GLB, _U.G_MODELS_ITEMS, tex, stat)

    s_tot = s_blend = b_tot = b_blend = 0
    if "3" in steps:
        log("3) СЦЕНА СОЗДАНИЯ/ВЫБОРА ПЕРСОНАЖА")
        s_tot, s_blend = do_group("сцена (скелет)", SCENE_SKEL, CHAR_GLB, _U.G_MODELS_CHAR,
                                  tex, stat, wipe=True)
        b_tot, b_blend = do_backdrop_actors()

    log("=" * 78)
    log("СВОДКА MIC ПО МАСТЕРАМ: %s" % ", ".join("%s=%d" % kv for kv in sorted(stat.items())))
    log("декор: MIC=%d прозрачных=%d | предметы: MIC=%d прозрачных=%d | сцена(скелет): MIC=%d прозрачных=%d "
        "| задник-акторы: %d прозрачных=%d" % (d_tot, d_blend, i_tot, i_blend, s_tot, s_blend, b_tot, b_blend))
    # контрольные распечатки слотов (числа для отчёта)
    for pkg in ("%s/mdel_oldtree-a_00/SkeletalMeshes/mdel_oldtree-a_00" % _U.G_MODELS_NPC,
                "%s/itm1_n_00_neck009_00/StaticMeshes/itm1_n_00_neck009_00" % _U.G_MODELS_ITEMS,
                "%s/cre_creation01-a_00/SkeletalMeshes/cre_creation01-a_00" % _U.G_MODELS_CHAR):
        rows = slot_report(pkg)
        log("СЛОТЫ %s (%d):" % (pkg.rsplit("/", 1)[-1], len(rows)))
        for s, mi, par in rows:
            log("    %-34s %-36s %s" % (s, mi, par))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
