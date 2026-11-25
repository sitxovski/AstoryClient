# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_gachya_models.py — импорт СКЕЛЕТАЛЬНЫХ моделей АВТОМАТОВ ПРЕМИУМ-ГАЧИ.
#
# Источник: RawAssets/Models/Gachya/md_gachya_{a..e}_00.glb — по 1 SkeletalMesh (20 мешей, 22 узла,
#   кости 33→20 после чистки неиспользуемых) и 5 клипов anim_00…anim_04.
# Назначение: /Game/AsdaAssets/Models/Skeletal/Gachya/<модель>/
#
# Эталон (gacha_fx_re.md, gachaScene_UpdateChildAnims@0xB6B0B0):
#   слоты клипов 1=покой (2.000 с) → 2=прокрутка (2.000 с) → 3=выдача (2.333 с) →
#   4=вскрытие шара по клику (2.333 с). Слот 0 (anim_00) в оригинале НЕ загружается никогда —
#   дефект оригинала; клип импортируем, но в рантайме не используем.
#   Кроссфейд между клипами в оригинале недостижим (флаг +464 нигде не ставится в 1) ⇒ жёсткая склейка.
#
# ★ Статик-версии тех же моделей УЖЕ лежат в /Game/AsdaAssets/Models/Char/md_gachya_*/StaticMeshes —
#   их не трогаем: они пригодны для превью, а сцене нужны скелеталки с клипами.
# ★ Текстуры НЕ импортируем: все 11 уже в проекте, в /Game/AsdaAssets/Textures/Characters/Texture
#   (md_gachya_{a..e}_00 уже апскейлены до 1024). Единый источник — закон проекта.
#
# Запуск: run_headless.bat asda_import_gachya_models.py   (env GACHYA_REIMPORT=1 — снести и заново)
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Gachya"))
DEST = _U.G_MODELS_GACHYA   # ★ волна 2 п. 7: константа реестра путей (значение то же)
TEX_ROOT = _U.G_TEXTURES
# ★★★ МАСТЕР КОРПУСА И ШАРА — СВОЙ, с эталонной формулой освещения.
#   Раньше здесь стоял общий `M_AsdaObjectLit` (DefaultLit PBR), а сцену подсвечивали два
#   собственных источника актора — это было приближение и давало пересвет («лутбоксы ярковатые»).
#   `M_AsdaGachaMachine` (asda_build_gacha_machine_material.py) воспроизводит `VShade`/`PShade`
#   клиента: `RGB = Tex.rgb · (MaterialAmbient 0.8 + max(0, N·lhtDir) · MaterialDiffuse 1.0)`,
#   поэтому свет сцене не нужен вовсе — как и в оригинале.
MASTER = "/Game/Materials/M_AsdaGachaMachine"
# ★★★ ПОЛУПРОЗРАЧНЫЕ ПОДСЕТКИ. В `.jbm` рендер-флаг `mr+0x004` бит 0x1 помечает меши, которые
#   оригинал рисует с альфа-блендингом (`CRenderer_ApplyBlendRenderState@0x9764F0`:
#   SRCBLEND=SRCALPHA, DESTBLEND=INVSRCALPHA). У автоматов помечены ровно три подсетки:
#   купол `Bowl` и два квада тени `G_shadow`/`Ball_shadow`. Сборщик .glb (asda_build_skinned_models,
#   BLEND_MAT_SUFFIX) дописывает таким материалам суффикс `_blend`, поэтому UE заводит им
#   ОТДЕЛЬНЫЙ слот меша — без этого купол делил бы слот с корпусом (та же текстура) и прозрачным
#   стать не мог. Здесь по имени слота выбираем мастер.
MASTER_GLASS = "/Game/Materials/M_AsdaGachaGlass"    # купол: Unlit+Translucent (asda_build_gacha_glass_material.py)
MASTER_SHADOW = "/Game/Materials/M_AsdaGachaShadow"  # тень:  Unlit+Translucent (asda_build_gacha_shadow_material.py)
MODELS = ["md_gachya_%s_00" % c for c in "abcde"]
# Имя материала в .glb (= базовое имя текстуры из .jbm) → имя текстуры проекта. Совпадают, но
# сопоставление держим явным: имя MIC UE получает числовой суффикс (md_Gachya_a_0019 и т.п.).
MAT_KEYS = ["md_gachya_a_00", "md_gachya_b_00", "md_gachya_c_00", "md_gachya_d_00",
            "md_gachya_e_00", "ball_ani_01", "ball_ani_02", "ball_ani_03", "ball_ani_04",
            "ball_ani_05", "gachya_shadow"]
BLEND_SUFFIX = "_blend"
REIMPORT = os.environ.get("GACHYA_REIMPORT") == "1"

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[gachya] " + str(m))


# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)


def import_batch(paths, dest):
    """Импорт пачкой (replace_existing=True) — asda_model_import_ue.import_batch."""
    return MI.import_batch(paths, dest)


def check_skeletal_usage(masters):
    """★★★ ПРОВЕРКА, БЕЗ КОТОРОЙ В ИГРЕ ПОЯВЛЯЮТСЯ РОВНО-СЕРЫЕ КВАДЫ.

    UE применяет материал к СКЕЛЕТАЛЬНОМУ мешу только при `bUsedWithSkeletalMesh`. В редакторе
    `UMaterial::CheckMaterialUsage` дописывает флаг на лету, а в `-game`/PIE/куке возвращает false
    и подставляет `DefaultMaterial` — ровный непрозрачный серый. Именно так у владельца появились
    «большая ровно-серая плита под автоматами» (квад `G_shadow`, 270×210 ед.) и «такой же серый
    квадрат под шаром» (квад `Ball_shadow`, 55×43 ед.): у мастера `M_AsdaGachaShadow` флага не было,
    а headless-кадры дефект НЕ показывали, потому что снимались в редакторе.
    Поэтому флаг проверяется здесь при каждом импорте, а не только в сборщике мастера.
    """
    bad = 0
    for key, m in sorted(masters.items()):
        if not m:
            continue
        ok = bool(m.get_editor_property("used_with_skeletal_mesh"))
        log("  мастер %-9s %-24s used_with_skeletal_mesh=%s" % (key, m.get_name(), ok))
        if not ok:
            bad += 1
            log("  !!! %s БЕЗ ФЛАГА: в игре его подсетки станут РОВНО-СЕРЫМИ (DefaultMaterial). "
                "Почините сборщик мастера." % m.get_name())
    return bad


def pick_master(name, masters):
    """Мастер по имени материала: тень → свой, полупрозрачная подсетка → стекло, прочее → объект.

    ★ ВАЖНО ДЛЯ ПОВТОРЯЕМОСТИ: раньше здесь всем без разбора ставился `M_AsdaObjectLit`, поэтому
      каждый переимпорт МОЛЧА откатывал правку тени (тот же цикл, что описан в памяти про задник:
      пересборка мешей отменяла ручную переподвязку MIC). Теперь маршрут вычисляется из имени,
      и переимпорт воспроизводит результат целиком.
    """
    if name.startswith("gachya_shadow"):
        return masters["shadow"] or masters["lit"]
    if BLEND_SUFFIX in name:
        return masters["glass"] or masters["lit"]
    return masters["lit"]


def wire_materials(tex, masters):
    """MIC → нужный мастер + текстура по имени материала.

    ★ Имя MIC в UE = имя материала glb + числовой суффикс при повторах (md_Gachya_a_00,
      md_Gachya_a_001 … md_Gachya_a_0019). Поэтому ключ ищем как САМОЕ ДЛИННОЕ совпадение
      префикса: иначе `ball_ani_01` ошибочно подойдёт и к `ball_ani_012`, а `md_gachya_a_00`
      совпадёт с любым из 20 своих клонов — что как раз и нужно.
    ★ Суффикс `_blend` в имени слота на текстуру НЕ влияет (подсетка носит ту же текстуру, что и
      корпус) — он только выбирает мастер, поэтому при поиске ключа его снимаем.
    ★ Скаляры пишем ТОЛЬКО те, что реально есть у выбранного мастера. Прежний код писал в каждый
      MIC `Ambient` и `MetallicFactor`, которых у `M_AsdaObjectLit` нет вовсе (его скаляры —
      Roughness/Metallic/AmbientFloor/FadeIn): значения оседали мёртвым грузом в оверрайдах.
    Реализация подвязки — asda_model_import_ue.wire_materials (ambient=False).
    """
    def key_of(name):
        lookup = name.lower().replace(BLEND_SUFFIX, "")
        key = None
        for k in MAT_KEYS:
            if lookup.startswith(k) and (key is None or len(k) > len(key)):
                key = k
        return key
    def tex_for(name):
        key = key_of(name)
        return tex.get(key) if key else None
    def on_miss(name):
        log("  !! MIC %s: текстура не определена (key=%s)" % (name.lower(), key_of(name)))
    st = MI.wire_materials(DEST, tex_for=tex_for, pick_master=lambda n: pick_master(n.lower(), masters),
                           ambient=False, on_miss=on_miss)
    log(MI.per_master_line(st["per_master"]))
    return st["n_tex"], st["n_miss"]


def used_mic_set():
    """Пакеты MIC, реально назначенные слотам скелеталок под DEST."""
    used = set()
    for ad in AR.get_assets_by_path(unreal.Name(DEST), recursive=True):
        if str(ad.asset_class_path.asset_name) != "SkeletalMesh":
            continue
        sm = unreal.load_asset(str(ad.package_name))
        for sl in (sm.get_editor_property("materials") or []):
            mi = sl.get_editor_property("material_interface")
            if mi:
                used.add(mi.get_path_name().split(".")[0])
    return used


def sweep_orphan_mics():
    """★ Снос MIC, которые больше НЕ назначены ни одному слоту.

    Нужен после смены ИМЁН слотов в .glb: старые MIC остаются в папке как ничьи и путают
    ревизию («в папке `md_gachya_d_00` лежат `md_Gachya_e_00*`»). Например, исправление дефекта
    оригинала «четвёртый автомат носит текстуру пятого» (`Reports/gacha_model_d_texture.md`)
    переименовало 6 слотов модели `d` с `md_Gachya_e_00*` на `md_Gachya_d_00*` — без этой уборки
    прежние шесть MIC остались бы лежать рядом.
    """
    used = used_mic_set()
    dead = []
    for ad in AR.get_assets_by_path(unreal.Name(DEST), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        pkg = str(ad.package_name)
        if pkg not in used:
            dead.append(pkg)
    for pkg in dead:
        EAL.delete_asset(pkg)
    if dead:
        log("уборка ничьих MIC: %d (%s)" % (len(dead), ", ".join(sorted(p.rsplit("/", 1)[-1] for p in dead)[:8])))
    return len(dead)


def verify_texture_binding():
    """★★★ СТОРОЖ: у каждого автомата корпус обязан носить СВОЮ текстуру.

    Зачем. У этой системы уже был цикл, когда ручная переподвязка материала МОЛЧА откатывалась
    следующим переимпортом (см. `pick_master`). Проверка сравнивает не имя слота, а РЕАЛЬНО
    назначенную текстуру параметра `Tex`, поэтому ловит откат с любой стороны — и из сборщика
    .glb, и из ручной правки ассета.
    Правило (`Reports/gacha_model_d_texture.md`): MIC внутри папки `md_gachya_<буква>_00`,
    чьё имя начинается с `md_gachya_`, обязан ссылаться на текстуру `md_gachya_<та же буква>_00`.
    Текстуры шаров `ball_ani_NN` и тени `gachya_shadow` под правило не подпадают.
    """
    bad = 0
    rows = []
    for ad in AR.get_assets_by_path(unreal.Name(DEST), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        pkg = str(ad.package_name)
        parts = pkg.split("/")
        owner = next((p for p in parts if p.lower().startswith("md_gachya_")), "")
        name = str(ad.asset_name).lower()
        if not owner or not name.startswith("md_gachya_"):
            continue
        want = owner.lower()                                    # md_gachya_<буква>_00
        a = unreal.load_asset(pkg)
        t = MEL.get_material_instance_texture_parameter_value(a, "Tex")
        got = t.get_name().lower() if t else "(нет)"
        rows.append((owner, str(ad.asset_name), got))
        if got != want:
            bad += 1
            log("  !!! %s/%s: текстура %s, а должна быть %s" % (owner, ad.asset_name, got, want))
    per = {}
    for owner, _mic, got in rows:
        per.setdefault(owner, {}).setdefault(got, 0)
        per[owner][got] += 1
    for owner in sorted(per):
        log("  корпус %-16s → %s" % (owner, ", ".join("%s×%d" % (k, v) for k, v in sorted(per[owner].items()))))
    return bad


def disable_nanite():
    """Nanite у проекта выключен (низкополигональный контент 2008) — снимаем и с новых мешей."""
    MI.disable_nanite(DEST)


def main():
    glbs = [os.path.join(RAW, m + ".glb") for m in MODELS]
    have = [g for g in glbs if os.path.isfile(g)]
    if len(have) != len(glbs):
        log("НЕТ файлов (%d из %d) в %s — сначала: python asda_build_skinned_models.py gachya"
            % (len(have), len(glbs), RAW))
        if not have:
            return
    if REIMPORT and EAL.does_directory_exist(DEST):
        log("ПЕРЕИМПОРТ: удаляю %s (смена состава костей ⇒ старый Skeleton несовместим)" % DEST)
        EAL.delete_directory(DEST)

    masters = {"lit": unreal.load_asset(MASTER),
               "glass": unreal.load_asset(MASTER_GLASS),
               "shadow": unreal.load_asset(MASTER_SHADOW)}
    if not masters["lit"]:
        log("НЕТ мастер-материала %s — соберите его: asda_build_gacha_machine_material.py" % MASTER)
        return
    for k, p in (("glass", MASTER_GLASS), ("shadow", MASTER_SHADOW)):
        if not masters[k]:
            log("!! НЕТ мастера %s — соберите его (asda_build_gacha_%s_material.py); "
                "слоты уйдут на непрозрачный %s" % (p, k, MASTER))
    if check_skeletal_usage(masters):
        log("★ ОСТАНОВ: есть мастер без used_with_skeletal_mesh — импорт дал бы серые подсетки.")
        return
    tex = tex_index()
    log("текстур в индексе: %d, моделей к импорту: %d" % (len(tex), len(have)))

    import_batch(have, DEST)
    EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    sweep_orphan_mics()                 # ★ до подвязки: ничьи MIC не должны попасть в отчёт
    mats, miss = wire_materials(tex, masters)
    disable_nanite()
    EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    # ★ сторож «корпус носит свою текстуру» — см. verify_texture_binding
    if verify_texture_binding():
        log("★ ДЕФЕКТ ПРИВЯЗКИ ТЕКСТУР: см. строки !!! выше. Причина обычно в .glb — "
            "пересоберите: python asda_build_skinned_models.py gachya")

    kinds = {}
    anims = []
    for ad in AR.get_assets_by_path(unreal.Name(DEST), recursive=True):
        k = str(ad.asset_class_path.asset_name)
        kinds[k] = kinds.get(k, 0) + 1
        if k == "AnimSequence":
            anims.append(str(ad.asset_name))
    log("ГОТОВО: материалов настроено=%d (без текстуры=%d), ассеты=%s" % (mats, miss, kinds))
    log("клипы: %s" % ", ".join(sorted(anims)[:12]))
    # ★ Проверка ожидаемого состава: 5 скелеталок × (1 меш + 1 скелет + 5 клипов)
    want_anim = len(have) * 5
    got_anim = kinds.get("AnimSequence", 0)
    if got_anim != want_anim or kinds.get("SkeletalMesh", 0) != len(have):
        log("!! СОСТАВ НЕ СОШЁЛСЯ: скелеталок %d (ждали %d), клипов %d (ждали %d)"
            % (kinds.get("SkeletalMesh", 0), len(have), got_anim, want_anim))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
