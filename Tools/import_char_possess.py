# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Headless-импорт ассетов фичи «ОДЕРЖИМОСТЬ». Источник — `export_char_possess.py`
#   (RawAssets/Models/Skeletal/Characters/possess/*.glb). Делает ДВЕ вещи:
#
#   1) КЛИПЫ НА СУЩЕСТВУЮЩИЙ СКЕЛЕТ `ch1_{m|f}_cremmy_preview_Skeleton`. Меши и скелет НЕ трогаются
#      (переимпорт скелета запрещён — он снёс бы 200+ уже лежащих клипов). Механика 1:1 с
#      `import_char_emotions.py`: Interchange-pipeline override
#      `common_skeletal_meshes_and_animations_properties.skeleton` = существующий +
#      `import_only_animations=True`, импорт во ВРЕМЕННУЮ папку → перенос каждого «…anim_NN» на
#      место `ch1_{g}_cremmy_previewanim_NN`.
#        пол m : 205..209 (пара М+М)  ·  215..219 (пара Ж+М)
#        пол f : 00..04 (★ ЗАМЕНА: женский набор `cre_crewmy` вместо ошибочного мужского `cre_cremmy`)
#                210..214 (пара М+Ж)  ·  220..224 (пара Ж+Ж)
#
#   2) СКИННУТЫЕ МОДЕЛИ «ДУШИ» (полупрозрачный светящийся силуэт) `cre_cresoul{man|woman}_preview` —
#      самостоятельные SkeletalMesh + свой Skeleton + anim_00..04.
#      ★★★ ИСПРАВЛЕНО 02.09.2026 (см. `Reports/audit_2026-09-02/fix_soul_silhouette_look.md`):
#      РАНЬШЕ материал брал мастера ПЕРСОНАЖА `M_AsdaCharLit` (OPAQUE, освещаемый) и СЕРУЮ дымку
#      `silhoutte_tex_blue` в BaseColor тела ⇒ силуэт выходил серым и унылым. Это был ДЕФЕКТ ПОРТА:
#      по реверсу (`Reports/soulmate_soul_silhouette_render.md` §5-6) «синева» силуэта лежит в
#      СОБСТВЕННОЙ текстуре меша `cre_cresoulman-a_00` (синяя) / `cre_cresoulwoman-a_00` (бирюза),
#      обе DXT2 с мягкой альфой ⇒ рисуются АЛЬФА-СМЕШЕНИЕМ, а `silhoutte_tex_blue` в оригинале шла на
#      ОТДЕЛЬНЫЙ оверлей-объект, а не на тело.
#      ТЕПЕРЬ (★ 03.09.2026 пересобрано ПО ЭТАЛОНУ, `fix_soul_ghost_charselect.md`): мастер
#      `M_AsdaSoulSilhouette` = Translucent + DefaultLit + односторонний, BaseColor = НАТИВНАЯ текстура
#      по полу СИЛУЭТА (см. `NATIVE_SOUL_TEX`), без тинта/emissive-буста; [ПОРТ] Opacity = tex.a ×
#      `OpacityScale` (0.3 — α «души» в мире). Прежние Unlit/TintColor/EmissiveBoost были выдуманы и
#      удалены. Крутится параметром `OpacityScale` в MIC без пересборки.
#      ⚠ Уже существующий СТАТИЧЕСКИЙ импорт `/Game/AsdaAssets/Models/Char/cre_cresoul*-a_00`
#      (общий батч моделей) НЕ трогается: он в другой папке и под другим именем.
#
# Запуск:
#   python Tools/run_headless_safe.py Tools/import_char_possess.py --marker "ИТОГ:"
#   (или run_headless.bat import_char_possess.py [m|f])
import unreal, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RAW  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "possess"))
TEXSRC = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Textures", "Data", "Character", "Texture"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # ★ волна 2 п. 7: Tools в пути — для asda_ue_paths
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_char_clips as _CC   # ★ дедуп №2 В1 Т11: общий импорт клипов персонажа (Interchange «только анимации»)
from asda_login_guard import login_package_blocked   # ★ 15.09.2026: защита сцены логина (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13)
DEST      = _U.G_MODELS_CHARACTERS
DEST_BASE = DEST + "/ch1_%s_cremmy_preview/SkeletalMeshes"
SKEL = {"m": DEST + "/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview_Skeleton",
        "f": DEST + "/ch1_f_cremmy_preview/SkeletalMeshes/ch1_f_cremmy_preview_Skeleton"}
TMP  = "/Game/Tmp_Possess"
TEX_ROOT = "/Game/AsdaAssets/Textures"
# ★★★ Мастер призрака «души» (Translucent + DefaultLit, вид по эталону) — собирается
#   `Tools/asda_build_soul_silhouette_mat.py`. Прогонять ДО этого импортёра; если мастера нет,
#   импортёр падает предупреждением, а не тихо ставит серый opaque.
SOUL_MASTER = "/Game/Materials/M_AsdaSoulSilhouette"
# ★ НАТИВНЫЕ текстуры тела силуэта по полу ПОКАЗЫВАЕМОГО силуэта (= пол меша): синяя (муж.),
#   бирюзовая (жен.). Обе уже импортированы общим батчем текстур персонажей.
#   (РАНЬШЕ сюда ошибочно ставилась СЕРАЯ дымка `silhoutte_tex_blue` — корень «серого» бага.)
NATIVE_SOUL_TEX = {
    "man":   "/Game/AsdaAssets/Textures/Characters/Texture/cre_cresoulman-a_00",
    "woman": "/Game/AsdaAssets/Textures/Characters/Texture/cre_cresoulwoman-a_00",
}


def _soul_tex_path(name):
    """Нативная текстура по имени ассета силуэта. Пол СИЛУЭТА = пол его меша:
       `...woman_preview` (в т.ч. смешанный `mwwoman`) → бирюза; иначе (`man`, `wmman`) → синяя.
       ⚠ 'man' — подстрока 'woman', поэтому проверяем именно 'woman'."""
    return NATIVE_SOUL_TEX["woman"] if "woman" in name else NATIVE_SOUL_TEX["man"]

# Какие anim_NN ждём в мини-glb каждого пола (должно совпасть с PAIR_PLAN/MINE_PLAN экспортёра
#   и с `FAsdaPossessData::PairClipAnimIndex` в коде).
EXPECT = {
    "m": list(range(205, 210)) + list(range(215, 220)),
    "f": list(range(0, 5)) + list(range(210, 215)) + list(range(220, 225)),
}
# Наборы «души» по полу МОЕГО персонажа: однополый (эталонный) + смешанный (решение владельца
#   02.09.2026 — силуэт-заглушка показывается ПРОТИВОПОЛОЖНЫМ полом). Смешанные наборы собраны из
#   авторских клипов `cre_cremwwoman`+`cre_cremwsoul` / `cre_crewmman`+`cre_crewmsoul`, которые
#   лежат в `CharMotion.baz`, но эталонным загрузчиком не используются — см. export_char_possess.py.
SOUL = {"m": ["cre_cresoulman_preview", "cre_cresoulmwwoman_preview"],
        "f": ["cre_cresoulwoman_preview", "cre_cresoulwmman_preview"]}

ONLY_SOULS = []          # непустой список = импортировать ТОЛЬКО эти наборы «души»
LOGIN_SKIPPED = []       # ★ 15.09.2026 (раздел 14): наборы «души», пропущенные защитой логина в import_soul — число идёт в ИТОГ

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[import_possess] " + str(m))
def warn(m): unreal.log_warning("[import_possess] " + str(m))


# Interchange-override «ТОЛЬКО анимации на уже существующий скелет» — общий `asda_char_clips.anim_only_pipeline`
#   (дедуп №2 В1 Т11).
_anim_only_pipeline = _CC.anim_only_pipeline


def import_clips(g):
    glb = os.path.join(RAW, "ch1_%s_cremmy_preview.glb" % g)
    if not os.path.exists(glb):
        warn("НЕТ glb: %s — пропуск клипов пола %s" % (glb, g)); return False
    skel = unreal.load_asset(SKEL[g])
    if not skel:
        warn("!! скелет не найден: %s" % SKEL[g]); return False
    tmp = TMP + "_clips_" + g
    if EAL.does_directory_exist(tmp):
        EAL.delete_directory(tmp)

    t = unreal.AssetImportTask()
    t.set_editor_property("filename", glb)
    t.set_editor_property("destination_path", tmp)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", False)
    t.set_editor_property("options", _anim_only_pipeline(skel))
    ATOOLS.import_asset_tasks([t])

    found = {}
    for ad in AR.get_assets_by_path(unreal.Name(tmp), recursive=True):
        if str(ad.asset_class_path.asset_name) != "AnimSequence": continue
        m = re.search(r'anim_(\d+)$', str(ad.asset_name))
        if m: found[int(m.group(1))] = str(ad.package_name)
    miss = [n for n in EXPECT[g] if n not in found]
    if miss:
        warn("!! [%s] не импортировались клипы: %s" % (g, miss)); return False

    ok = True
    for n in EXPECT[g]:
        dst = (DEST_BASE % g) + "/ch1_%s_cremmy_previewanim_%02d" % (g, n)
        # ★ Для пола f номера 00..04 УЖЕ СУЩЕСТВУЮТ (там лежал ошибочный мужской набор) — сносим и
        #   кладём на то же имя: путь ассета в коде остаётся прежним.
        if EAL.does_asset_exist(dst):
            EAL.delete_asset(dst)
        if not EAL.rename_asset(found[n], dst):
            warn("!! [%s] rename %s -> %s не удался" % (g, found[n], dst)); ok = False; continue
        EAL.save_asset(dst, only_if_is_dirty=False)
    EAL.delete_directory(tmp)   # выбрасывает "__ref_ignore" и остатки

    bad = []
    for n in EXPECT[g]:
        dst = (DEST_BASE % g) + "/ch1_%s_cremmy_previewanim_%02d" % (g, n)
        a = unreal.load_asset(dst)
        if not a: bad.append((n, "нет ассета")); continue
        ln = float(a.get_editor_property("sequence_length"))
        sk = a.get_editor_property("skeleton")
        if ln <= 0 or sk is None or sk.get_name() != os.path.basename(SKEL[g]):
            bad.append((n, "len=%.3f skel=%s" % (ln, sk.get_name() if sk else "NONE")))
    if bad:
        warn("!! [%s] проблемные клипы: %s" % (g, bad)); ok = False
    else:
        log("[%s] OK: %d клипов %s на скелете %s"
            % (g, len(EXPECT[g]), EXPECT[g], os.path.basename(SKEL[g])))
    return ok


def import_soul(g, name):
    """Импорт ОДНОГО набора «души» (`name` — имя ассета из SOUL[g])."""
    glb = os.path.join(RAW, name + ".glb")
    if not os.path.exists(glb):
        warn("НЕТ glb: %s — пропуск «души» пола %s" % (glb, g)); return False
    sub = DEST + "/" + name
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): набор «души» `cre_cresoul*_preview` грузит только
    #   Login/AsdaPossessScene.cpp. Без ASDA_ALLOW_LOGIN_WRITE=1 набор пропускается ЦЕЛИКОМ: снос папки, импорт .glb, приведение
    #   его текстуры (её используют только MIC силуэтов и никем не используемые статичные Char/cre_cresoul*-a_00), MIC и
    #   сохранение. Клипы `ch1_*_cremmy_preview` (не логин) импортируются как раньше. Пропуск — не сбой: строка [LOGIN-GUARD] в логе.
    if login_package_blocked(sub, "import_char_possess.py"):
        LOGIN_SKIPPED.append(name)
        return True
    if EAL.does_directory_exist(sub):
        EAL.delete_directory(sub)

    data = unreal.AutomatedAssetImportData()
    data.set_editor_property("destination_path", DEST)
    data.set_editor_property("filenames", [glb])
    data.set_editor_property("replace_existing", True)
    ATOOLS.import_assets_automated(data)

    tex_path = _soul_tex_path(name)
    tex = unreal.load_asset(tex_path)
    if not tex:
        warn("!! нативная текстура силуэта не найдена: %s — материал останется без текстуры" % tex_path)
    else:
        # §6.2: цвет ⇒ sRGB=On, альфа сохранить, компрессия «default» (не Masks). Ставим только при
        #   расхождении, чтобы не дёргать общий ассет напрасно; при правке — сохраняем.
        changed = False
        if not tex.get_editor_property("srgb"):
            tex.set_editor_property("srgb", True); changed = True
        if tex.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_DEFAULT:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT); changed = True
        if changed:
            EAL.save_loaded_asset(tex)
            log("текстура %s приведена к sRGB=On / TC_Default" % tex_path)
    master = unreal.load_asset(SOUL_MASTER)
    if not master:
        warn("!! мастер силуэта не найден: %s — сначала прогони asda_build_soul_silhouette_mat.py" % SOUL_MASTER)
    mats = killed = anims = 0
    mesh_ok = False
    for ad in AR.get_assets_by_path(unreal.Name(sub), recursive=True):
        cls = str(ad.asset_class_path.asset_name); an = str(ad.asset_name)
        pkg = str(ad.package_name)
        if cls == "MaterialInstanceConstant":
            a = unreal.load_asset(pkg)
            # ★★★ Вид призрака: мастер Translucent/DefaultLit (по эталону) + НАТИВНАЯ синяя/бирюзовая
            #   текстура тела (НЕ серая дымка). Прозрачность задаёт мастер (`OpacityScale`) — здесь
            #   оставляем дефолты мастера, чтобы у владельца была ОДНА точка кручения.
            if master: MEL.set_material_instance_parent(a, master)
            if tex: MEL.set_material_instance_texture_parameter_value(a, "BaseTex", tex)
            mats += 1
        elif cls == "SkeletalMesh":
            mesh_ok = True
            m = unreal.load_asset(pkg)
            try:
                ns = m.get_editor_property("nanite_settings")
                if ns and ns.get_editor_property("enabled"):
                    ns.set_editor_property("enabled", False); m.set_editor_property("nanite_settings", ns)
            except Exception:
                pass
        elif cls == "AnimSequence":
            # Служебный референс-клип экспортёра (задаёт скелет вывода) в ассетах не нужен.
            if "__ref_ignore" in an:
                EAL.delete_asset(pkg); killed += 1
            else:
                anims += 1
    EAL.save_directory(sub, only_if_is_dirty=False, recursive=True)
    log("[%s] «душа» %s: меш=%s материалов=%d клипов=%d (служебных снесено %d)"
        % (g, name, "есть" if mesh_ok else "НЕТ", mats, anims, killed))
    return mesh_ok and anims >= 5


def main():
    genders = ["m", "f"]
    argv = [a for a in sys.argv if a.lower() in ("m", "f")]
    if argv: genders = [a.lower() for a in argv]
    # ★ Точечный режим: имена ассетов «души» в аргументах (или в ASDA_POSSESS_SOULS) — тогда клипы
    #   НЕ переимпортируются. Нужен, чтобы доложить новый набор, не трогая 25 уже лежащих клипов.
    global ONLY_SOULS
    ONLY_SOULS = [a for a in sys.argv if a.startswith("cre_cresoul")]
    env = os.environ.get("ASDA_POSSESS_SOULS", "").strip()
    if env: ONLY_SOULS = [x.strip() for x in env.split(",") if x.strip()]
    if ONLY_SOULS: log("точечный режим: только силуэты %s (клипы не трогаем)" % ONLY_SOULS)
    ok = True
    del LOGIN_SKIPPED[:]   # ★ 15.09.2026 (раздел 14): счёт пропусков — на один прогон
    for g in genders:
        if not ONLY_SOULS:
            ok = import_clips(g) and ok
        for nm in SOUL[g]:
            if ONLY_SOULS and nm not in ONLY_SOULS: continue
            ok = import_soul(g, nm) and ok
    # ★ 15.09.2026 (раздел 14): при пропуске наборов защитой логина итог называет их число — «ВСЕ OK» не должен скрывать пропуск.
    log("ИТОГ: %s%s" % ("ВСЕ OK" if ok else "ЕСТЬ ПРОБЛЕМЫ",
                        " (пропущено защитой логина: %d)" % len(LOGIN_SKIPPED) if LOGIN_SKIPPED else ""))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
