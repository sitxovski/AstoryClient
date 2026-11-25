# Copyright (c) 2026 @sitxovski (Discord)
# asda_rollback_blend.py — ПРИВЕДЕНИЕ МАСТЕРОВ ПРОЗРАЧНОСТИ К ДЕЙСТВУЮЩЕЙ ПОЛИТИКЕ.
#
# ЗАЧЕМ. 2026-08-18 владелец, осмотрев проект в редакторе (PIE), решил:
#   «Оставь прозрачность пока только в gacha. На других объектах это ужасно выглядит.»
# Раскатка прозрачности (класс BLEND → `M_AsdaObjectBlendLit`) была сделана двумя потоками
# (`Reports/blend_rollout_maps_items.md`, `Reports/blend_rollout_npc_mobs.md`) для декора
# карт `mdel_*`/`natu_*`, сцены создания/выбора персонажа `cre_*` (включая 667 акторов задника
# логина), моделей предметов и 6 моделей мобов + 11 BLEND-слотов декора в `Models/NPC`.
#
# ★★★ ЭТО НЕ УДАЛЕНИЕ НАРАБОТКИ, А ПЕРЕКЛЮЧАТЕЛЬ. Сохранены полностью:
#   • мастер `/Game/Materials/M_AsdaObjectBlendLit` (ассет остаётся в проекте, не трогается);
#   • критерий прозрачности `asda_jbm.is_alpha_blend` + `asda_mesh_render_class.mesh_class`;
#   • метки `_blend` в ИМЕНАХ СЛОТОВ, которые ставят экспортёры `.glb`;
#   • отдельные MIC (`MI_<tex>_blend`) — они просто временно сидят на masked-мастере.
# Единственная точка переключения — `asda_material_policy.BLEND_ROLLOUT_ENABLED`. Этот скрипт
# работает В ОБЕ СТОРОНЫ: он приводит родителей существующих MIC к тому, что говорит политика.
#   False → все `_blend`-MIC вне гачи уходят на `M_AsdaObjectLit` (masked, как до раскатки);
#   True  → те же MIC возвращаются на `M_AsdaObjectBlendLit`.
# Переимпорт моделей НЕ требуется: имена слотов не меняются ни на символ.
#
# ★ ГАЧА — ИСКЛЮЧЕНИЕ, ЗАТРЕБОВАННОЕ ВЛАДЕЛЬЦЕМ. Папка `/Game/AsdaAssets/Models/Skeletal/Gachya`
#   исключена ЯВНО. Её прозрачность держат СВОИ мастера — `M_AsdaGachaGlass` (купол `Bowl`) и
#   `M_AsdaGachaShadow` (`G_shadow`, `Ball_shadow`), которые назначает `asda_import_gachya_models.py`
#   собственным правилом и без обращения к общей политике. Их этот скрипт не касается вовсе, но
#   ПЕРЕСЧИТЫВАЕТ и печатает — чтобы «у гачи на месте» было доказано числом, а не обещанием.
#
# ★ ЧЕГО СКРИПТ НЕ ДЕЛАЕТ. Разделение CHAR/MASK по технике подсетки (`M_AsdaCharLit` против
#   `M_AsdaObjectLit`, фикс «masked резал маску блика» у NPC и мобов) НЕ откатывается: владелец
#   жаловался на прозрачность, а не на светотень. Скрипт трогает ТОЛЬКО тех, кто сидит на
#   прозрачном мастере (или должен на него вернуться) — родителей CHAR/MASK он не перебирает.
#
# ★ ГРАБЛЯ (из `weapon_texture_alpha_fix.md` §4, повторно подтверждена 2026-08-18):
#   `MaterialEditingLibrary.set_material_instance_parent()` НЕ помечает пакет грязным ⇒ без
#   `mic.modify()` + `save_loaded_asset(mic, False)` правка теряется при выходе из редактора.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_rollback_blend.py
#   env ASDA_BLEND_DRY=1 — только посчитать и напечатать, ничего не сохранять.

import os
import unreal
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_material_policy as POL
import asda_login_guard as _LG   # ★ 15.09.2026: MIC сцены логина (Models/Char/cre_creback-a_00, cre_creation01*) — только ASDA_ALLOW_LOGIN_WRITE=1

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

# Корень, в котором вообще может встретиться наш MIC. `/Game/Materials` тут не нужен — там лежат
# сами мастера; их родителей мы не меняем.
SCAN_ROOTS = ["/Game/AsdaAssets", "/Game/Map", "/Game/Materials"]

# ★ ЯВНОЕ ИСКЛЮЧЕНИЕ: автоматы премиум-гачи. Сравнение по префиксу пути пакета в нижнем регистре.
GACHA_ROOT = "/game/asdaassets/models/skeletal/gachya"
GACHA_MASTERS = ("M_AsdaGachaGlass", "M_AsdaGachaShadow", "M_AsdaGachaMachine")

BLEND_NAME = POL.MASTER_BLEND.rsplit("/", 1)[-1]        # M_AsdaObjectBlendLit
DRY = os.environ.get("ASDA_BLEND_DRY", "") not in ("", "0")


def log(m):
    unreal.log("[откат-прозрачности] " + str(m))


def is_gacha(package_name):
    """True, если пакет принадлежит автоматам премиум-гачи (их трогать НЕЛЬЗЯ)."""
    return str(package_name).lower().startswith(GACHA_ROOT)


def all_mic():
    """[(package_name, asset_name, MaterialInstanceConstant)] по всем корням проекта, без дублей."""
    seen = {}
    for root in SCAN_ROOTS:
        for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
            if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
                continue
            seen[str(ad.package_name)] = str(ad.asset_name)
    out = []
    for pkg, name in sorted(seen.items()):
        a = unreal.load_asset(pkg)
        if isinstance(a, unreal.MaterialInstanceConstant):
            out.append((pkg, name, a))
    return out


def parent_name(mic):
    p = mic.get_editor_property("parent")
    return p.get_name() if p else "—"


# ★ Мастера, из которых/в которые ВООБЩЕ допустимо переключать по этой политике. Список закрытый
#   НАМЕРЕННО: метка `_blend` — просто подстрока имени, и она встречается у ассетов, к раскатке
#   отношения не имеющих (например, MIC эффектов с режимом смешивания в имени). Трогать MIC,
#   сидящий на постороннем мастере (эффекты, террейн, вода, небо, UI), нельзя ни при каком флаге.
SWITCHABLE = (BLEND_NAME, POL.MASTER_MASKED.rsplit("/", 1)[-1])


def want_master(pkg, name, cur_parent):
    """Какой мастер положен этому MIC при ТЕКУЩЕЙ политике, или None — если трогать не надо.

    Правило узкое НАМЕРЕННО. Кандидат — только MIC, который относится к прозрачной раскатке:
      • УЖЕ сидит на прозрачном мастере `M_AsdaObjectBlendLit` (прямая раскатка → откат), ЛИБО
      • носит метку `_blend` в имени И сидит на masked-мастере (откат → возврат раскатки).
    Всё остальное — класс CHAR, мастера гачи, эффекты, террейн, вода, небо, UI — не перебирается
    вовсе: см. SWITCHABLE.
    """
    if is_gacha(pkg) or cur_parent in GACHA_MASTERS:
        return None
    if cur_parent not in SWITCHABLE:
        return None
    marked = POL.is_blend_slot(name)
    if cur_parent != BLEND_NAME and not marked:
        return None                        # обычный masked-слот без метки — не наше дело
    # ★ Мастер берём из ОБЩЕЙ ТОЧКИ политики — она и есть выключатель.
    #   Помеченному имени `master_for` вернёт `blend_master()`; MIC на прозрачном мастере БЕЗ метки
    #   (остатки прежних импортов) отправляем туда же — политика решает про них одинаково.
    tgt = POL.master_for(name, name) if marked else POL.blend_master()
    # ★ Сравнивать надо ИМЕНА, а не «полный путь против имени»: `parent.get_name()` отдаёт
    #   `M_AsdaObjectLit`, а политика — `/Game/Materials/M_AsdaObjectLit`. Прямое сравнение всегда
    #   давало «не равно», и повторный прогон вхолостую пересохранял 194 уже верных MIC.
    return tgt if tgt.rsplit("/", 1)[-1] != cur_parent else None


def gacha_report():
    """★ ДОКАЗАТЕЛЬСТВО «у гачи прозрачность на месте» — по СЛОТАМ мешей, а не по числу MIC.

    Печатает для каждой из 5 моделей автомата: имя слота → мастер его MIC, и отдельно — режим
    смешивания мастеров купола и тени. Ожидание (`asda_import_gachya_models.py`):
        `Bowl*_blend`        → M_AsdaGachaGlass  (Unlit + Translucent)
        `gachya_shadow*`     → M_AsdaGachaShadow (Unlit + Translucent)  — квады `G_shadow`/`Ball_shadow`
        остальные            → M_AsdaGachaMachine
    """
    log("-" * 96)
    log("ГАЧА — контроль, что прозрачность НЕ снята (исключение, затребованное владельцем):")
    for mp in ("/Game/Materials/M_AsdaGachaGlass", "/Game/Materials/M_AsdaGachaShadow"):
        m = unreal.load_asset(mp)
        if not m:
            log("    !! НЕТ мастера %s" % mp); continue
        log("    %-34s blend_mode=%s shading=%s" % (mp.rsplit("/", 1)[-1],
            m.get_editor_property("blend_mode"), m.get_editor_property("shading_model")))
    total = {}
    for ad in AR.get_assets_by_path(unreal.Name("/Game/AsdaAssets/Models/Skeletal/Gachya"),
                                    recursive=True):
        if str(ad.asset_class_path.asset_name) != "SkeletalMesh":
            continue
        mesh = unreal.load_asset(str(ad.package_name))
        if not mesh:
            continue
        per = {}
        for s in mesh.get_editor_property("materials"):
            mi = s.get_editor_property("material_interface")
            par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
            pn = par.get_name() if par else "—"
            per[pn] = per.get(pn, 0) + 1
            total[pn] = total.get(pn, 0) + 1
        log("    %-22s слотов=%2d  %s" % (str(ad.asset_name), sum(per.values()), per))
    log("    ИТОГО слотов гачи по мастерам: %s" % total)
    glass = total.get("M_AsdaGachaGlass", 0)
    shadow = total.get("M_AsdaGachaShadow", 0)
    # ★ Ждём по 5 слотов: у каждой из 5 моделей ОДИН слот купола (`Bowl…_blend`) и ОДИН слот тени.
    #   MIC-ассетов на мастере тени вдвое больше (10): квады `G_shadow` и `Ball_shadow` носят ОДНУ
    #   текстуру `gachya_shadow`, поэтому Interchange сливает их в ОДИН слот меша (два MIC — один
    #   слот). Путать эти два числа нельзя: «10 MIC» и «5 слотов» описывают одно и то же.
    log("    ★ прозрачных слотов у гачи: купол(Glass)=%d, тени(Shadow)=%d — ждали 5 и 5 (по одному "
        "на каждую из 5 моделей)" % (glass, shadow))


def main():
    log("=" * 96)
    log("ПОЛИТИКА: BLEND_ROLLOUT_ENABLED=%s ⇒ класс BLEND вне гачи → %s"
        % (POL.blend_rollout_enabled(), POL.blend_master()))
    log("режим: %s" % ("СУХОЙ ПРОГОН (ничего не сохраняется)" if DRY else "ПРИМЕНЕНИЕ"))

    mics = all_mic()
    log("MaterialInstanceConstant в проекте: %d" % len(mics))

    # ---------- ДО ----------
    before = {}
    before_gacha = {}
    for pkg, name, a in mics:
        p = parent_name(a)
        (before_gacha if is_gacha(pkg) else before)[p] = \
            (before_gacha if is_gacha(pkg) else before).get(p, 0) + 1
    log("-" * 96)
    log("ДО — родители MIC ВНЕ гачи (топ):")
    for p, n in sorted(before.items(), key=lambda kv: -kv[1]):
        log("    %-28s %5d %s" % (p, n, "  ← ПРОЗРАЧНЫЙ" if p == BLEND_NAME else ""))
    log("ДО — родители MIC ГАЧИ:")
    for p, n in sorted(before_gacha.items(), key=lambda kv: -kv[1]):
        log("    %-28s %5d" % (p, n))

    # ---------- переподвязка ----------
    log("-" * 96)
    changed = 0
    per_target = {}
    per_move = {}
    masters = {}
    touched_pkgs = []
    n_login = 0   # ★ 15.09: пропущено MIC сцены логина
    for pkg, name, a in mics:
        cur = parent_name(a)
        tgt = want_master(pkg, name, cur)
        if not tgt:
            continue
        if _LG.login_package_blocked(pkg, "asda_rollback_blend.py", quiet=True):   # ★ 15.09: сцена логина
            n_login += 1
            continue
        per_move["%s → %s" % (cur, tgt.rsplit("/", 1)[-1])] = \
            per_move.get("%s → %s" % (cur, tgt.rsplit("/", 1)[-1]), 0) + 1
        if tgt not in masters:
            masters[tgt] = unreal.load_asset(tgt)
        m = masters[tgt]
        if not m:
            log("!! НЕТ мастера %s — MIC %s оставлен на %s" % (tgt, name, cur))
            continue
        if not DRY:
            MEL.set_material_instance_parent(a, m)
            try:
                a.modify()                      # ★ иначе пакет не грязный и правка не сохранится
            except Exception:
                pass
            EAL.save_loaded_asset(a, False)
        changed += 1
        per_target[tgt] = per_target.get(tgt, 0) + 1
        touched_pkgs.append(pkg)
    log("переподвязано MIC: %d  %s" % (changed, per_target))
    if n_login:
        log("[LOGIN-GUARD] пропущено MIC сцены логина (Models/Char/cre_creback-a_00, cre_creation01*): %d — ASDA_ALLOW_LOGIN_WRITE=1" % n_login)
    for k, v in sorted(per_move.items(), key=lambda kv: -kv[1]):
        log("    %-56s %5d" % (k, v))
    # ★ Разбивка по папкам — по ней в отчёт идут числа «сколько слотов вернулось в каждой группе».
    per_dir = {}
    for p in touched_pkgs:
        d = p.rsplit("/", 1)[0]
        # «/Game/AsdaAssets/Models/NPC/<модель>/Materials» → «/Game/AsdaAssets/Models/NPC»
        # ★ Порядок — от ДЛИННОГО к КОРОТКОМУ: иначе `/Models/MobSkel/...` схлопнется в `/Models/Mob`
        #   (префикс `Mob` — начало `MobSkel`) и статик-мобы съедят скелетных.
        for root in ("/Game/AsdaAssets/Models/MobSkel", "/Game/AsdaAssets/Models/Backdrop",
                     "/Game/AsdaAssets/Models/Items", "/Game/AsdaAssets/Models/Char",
                     "/Game/AsdaAssets/Models/NPC", "/Game/AsdaAssets/Models/Mob"):
            if d.startswith(root):
                d = root
                break
        per_dir[d] = per_dir.get(d, 0) + 1
    log("    по группам:")
    for d, n in sorted(per_dir.items(), key=lambda kv: -kv[1]):
        log("        %-52s %5d" % (d, n))

    # ---------- ПОСЛЕ ----------
    after = {}
    after_gacha = {}
    still = []
    for pkg, name, a in mics:
        p = parent_name(a)
        d = after_gacha if is_gacha(pkg) else after
        d[p] = d.get(p, 0) + 1
        if p == BLEND_NAME and not is_gacha(pkg):
            still.append(pkg)
    log("-" * 96)
    log("ПОСЛЕ — родители MIC ВНЕ гачи:")
    for p, n in sorted(after.items(), key=lambda kv: -kv[1]):
        log("    %-28s %5d" % (p, n))
    log("ПОСЛЕ — родители MIC ГАЧИ (должны остаться СВОИ прозрачные мастера):")
    for p, n in sorted(after_gacha.items(), key=lambda kv: -kv[1]):
        log("    %-28s %5d" % (p, n))
    log("★ MIC на %s ВНЕ гачи: %d" % (BLEND_NAME, len(still)))
    for p in still[:20]:
        log("      %s" % p)

    # ---------- сплошная проверка ссылок на прозрачный мастер ----------
    # ★ Второй метод (закон false-negatives-method): вместо перебора MIC спрашиваем у реестра
    #   ВСЕХ, кто ссылается на пакет мастера. Так ловятся и прямые оверрайды материала на
    #   компонентах акторов внутри `.umap` — их перебор MIC не увидел бы в принципе.
    log("-" * 96)
    try:
        refs = AR.get_referencers(unreal.Name(POL.MASTER_BLEND),
                                  unreal.AssetRegistryDependencyOptions(include_hard_package_references=True))
    except Exception as e:
        refs = None
        log("реестр ссылок недоступен (%s) — проверка только по MIC" % e)
    if refs is not None:
        rl = sorted(str(r) for r in refs)
        log("ссылок на %s в проекте: %d" % (POL.MASTER_BLEND, len(rl)))
        by_dir = {}
        for r in rl:
            by_dir[r.rsplit("/", 1)[0]] = by_dir.get(r.rsplit("/", 1)[0], 0) + 1
        for d, n in sorted(by_dir.items(), key=lambda kv: -kv[1])[:15]:
            log("      %-64s %5d" % (d, n))
        # ★ Ссылка ИЗ УРОВНЯ означала бы прямой оверрайд материала на компоненте актора — такой
        #   переподвязкой MIC не лечится. Проверяем отдельно и называем поимённо.
        lvl = [r for r in rl if "/Game/Map/" in r]
        log("      ссылок ИЗ УРОВНЕЙ (.umap, прямой оверрайд на акторе): %d %s" % (len(lvl), lvl[:10]))

    gacha_report()
    log("=" * 96)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
