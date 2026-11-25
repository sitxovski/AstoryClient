# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_apply_fx_boost.py — ГЕНЕРАТОР MIC волны Ф1: разносит таблицу «семейство → яркость эмиссии»
#   (Tools/asda_fx_boost.json) по материалам эффектов.
#
# ПОЧЕМУ НЕ «просто прописать буст в существующий MIC». MIC эффектов зовётся `MI_<текстура>_<режим>`
#   и ключуется ТЕКСТУРОЙ, а не эффектом (asda_place_effects.get_fx_material). Одна и та же текстура
#   (напр. `eff_0_mon_99_smoke03-a_00`) встречается у эффектов РАЗНЫХ семейств — у дыма карты и у
#   радуги. Прописать буст прямо в общий MIC значит подвинуть яркость и тем, кого правило не выбирало.
#   Поэтому генератор:
#     • на каждый (исходный MIC + значение буста/затухания) создаёт ОТДЕЛЬНЫЙ MIC-вариант
#       `<имя>_fxb<NNN>` дублированием исходного (родитель, текстура и все переопределения сохраняются);
#     • перенацеливает на него ТОЛЬКО те эмиттеры, чей .jeb подошёл под правило;
#     • исходные MIC не изменяются НИКОГДА — откат = вернуть ссылку эмиттера (и удалить варианты).
#
# РЕЖИМЫ (переменная окружения ASDA_FXB_MODE):
#   report (ДЕФОЛТ) — ничего не пишет: считает план (сколько эффектов, каких семейств, какие MIC,
#                     сколько вариантов появится) и кладёт его в JSON. Это же режим «сухого прогона»
#                     перед раскатом.
#   assets          — реально создаёт варианты MIC и перенацеливает эмиттеры, сохраняет BP и уровни.
#
# ОГРАНИЧЕНИЕ ОХВАТА (для прототипа, чтобы не катить на 1283 BP):
#   ASDA_FXB_ONLY="jeb1,jeb2"  — только эти .jeb (пусто = все);
#   ASDA_FXB_PROTO=1           — только список `prototype.jebs` из таблицы;
#   ASDA_FXB_MAPS="alpen,Login" — какие уровни обходить (дефолт = ВСЕ ключи MAPS, "none" = ни одного);
#   ASDA_FXB_BP=0              — не обходить BP_Fx_* (по умолчанию обходятся).
#
# Запуск (сухой прогон): python Tools/run_headless_safe.py Tools/asda_apply_fx_boost.py --marker "FX-BOOST ЗАВЕРШЕНО"
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_fx_boost as FXB   # таблица и матчер; при импорте ничего не делает
import unreal
import os as _fxg_os, sys as _fxg_sys
_fxg_sys.path.insert(0, _fxg_os.path.dirname(_fxg_os.path.abspath(__file__)))
import asda_fx_common as _FXC_GATE   # ★ Ф16: гейт записи эмиттеров (импорт без побочных эффектов)
import asda_fx_modadda_unboost as _UB  # ★ 11.10.2026: «прежняя половина давала 0» (импорт без побочных эффектов)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()

BP_DIR = "/Game/AsdaAssets/Effects"
# ★ ОХВАТ КАРТ (расширен 09.09.2026 «раскатом на все карты»). Было две карты — alpen и nightvalley,
#   поэтому у остальных 18 карт и у сцены логина ни одного варианта не появилось. Ключ = имя карты,
#   значение = путь уровня. Логин лежит НЕ в подпапке (`/Game/Map/Login`), поэтому пишется отдельно.
#   Карты battlefield / mine / L_Loading / _nltest эффект-акторов не несут (проверено по .umap) и
#   в списке НЕ значатся — грузить их незачем.
# ★ дедуп №2 В1 Т14 (11.10.2026): словарь строится из реестра карт — `asda_fx_boost.effect_maps()` (одна таблица
#   с asda_revert_fx_boost.py; исключения battlefield/mine — там же, FX_MAPS_WITHOUT_ACTORS). Ключи и порядок прежние.
MAPS = FXB.effect_maps()
# ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): уровень логина без ASDA_ALLOW_LOGIN_WRITE=1 не трогаем и не сохраняем
#   ★ 16.09.2026 (раздел 17): с разрешением save_map уровня Login в автоматике пройдёт, только если задана ещё
#   ASDA_ALLOW_LOGIN_MAP_WRITE=1 (C++-сторож AsdaLoginSaveGuard; login_save_guard.md п. 6.3). Сообщения здесь нет — пропуск молча.
from asda_login_guard import login_write_allowed   # единая проверка разрешения (без побочных эффектов)
if not login_write_allowed():
    MAPS.pop("Login", None)
# Куда класть план прогона. Переопределяется ASDA_FXB_OUT, чтобы новый прогон не затирал улики прошлого.
OUT = os.environ.get("ASDA_FXB_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1/fx_boost_plan.json")

MODE = os.environ.get("ASDA_FXB_MODE", "report")
ONLY = {FXB.jeb_key(x) for x in os.environ.get("ASDA_FXB_ONLY", "").split(",") if x.strip()}
# ★ 04.11.2026 (яркость эффектов навыков): ASDA_FXB_ONLY_FILE=<файл со списком через запятую> — тот же охват, когда список длиннее
#   предела переменной окружения (32767 символов; раскат 1235 эффектов). Добавляется к ASDA_FXB_ONLY.
if os.environ.get("ASDA_FXB_ONLY_FILE"):
    ONLY |= {FXB.jeb_key(x) for x in open(os.environ["ASDA_FXB_ONLY_FILE"], encoding="utf-8").read().split(",") if x.strip()}
PROTO = os.environ.get("ASDA_FXB_PROTO", "") == "1"
MAP_KEYS = [x.strip() for x in os.environ.get("ASDA_FXB_MAPS", ",".join(MAPS)).split(",") if x.strip()]
DO_BP = os.environ.get("ASDA_FXB_BP", "1") != "0"


def variant_name(mic_name, boost, fade, dist):
    """Имя MIC-варианта: `<исходный>_fxb<буст×100>` (+`s<дистанция>` при включённом затухании).

    Числа в имени, а не порядковый номер, — чтобы вариант читался глазами и повторный прогон
    с той же таблицей давал ТОТ ЖЕ ассет (идемпотентность раската)."""
    s = "%s_fxb%03d" % (mic_name, int(round(boost * 100)))
    if fade:
        s += "s%d" % int(round(dist))
    return s


def ensure_variant(src_mic, boost, fade, dist, made, dry):
    """MIC-вариант с нужными параметрами (создаётся дублированием исходного). В dry — только имя."""
    src_path = src_mic.get_path_name().split(".")[0]
    folder, name = src_path.rsplit("/", 1)
    dst_name = variant_name(name, boost, fade, dist)
    dst_path = "%s/%s" % (folder, dst_name)
    if dry:
        made.add(dst_path)
        return None, dst_path
    if not EAL.does_asset_exist(dst_path):
        # Дубликат, а не новый MIC: сохраняются родитель, текстура и любые прежние переопределения.
        if not EAL.duplicate_asset(src_path, dst_path):
            raise RuntimeError("не удалось создать вариант %s" % dst_path)
    mic = EAL.load_asset(dst_path)
    MEL.set_material_instance_scalar_parameter_value(mic, "EmissiveBoost", float(boost))
    if fade:
        MEL.set_material_instance_static_switch_parameter_value(mic, "SoftFadeEnabled", True)
        MEL.set_material_instance_scalar_parameter_value(mic, "SoftFadeDistance", float(dist))
    MEL.update_material_instance(mic)
    mic.modify()
    if not EAL.save_loaded_asset(mic, False):
        raise RuntimeError("вариант не сохранился: %s" % dst_path)
    made.add(dst_path)
    return mic, dst_path


def want(jeb):
    """Попадает ли эффект в текущий охват (ASDA_FXB_ONLY / ASDA_FXB_PROTO)."""
    k = FXB.jeb_key(jeb)
    if ONLY:
        return k in ONLY
    if PROTO:
        return k in {FXB.jeb_key(x) for x in FXB.load()["prototype"]["jebs"]}
    return True


def _etalon_half_stem(mic_name):
    """Текстура (stem) эталонной половины `MI_<stem>_{modadda,modaddsq,modaddinv}` или '' для прочих имён."""
    for suf in ("_modadda", "_modaddsq", "_modaddinv"):
        if mic_name.startswith("MI_") and mic_name.endswith(suf):
            return mic_name[len("MI_"):-len(suf)]
    return ""


def _disk_by_index(jeb, ems):
    """{индекс эмиттера носителя: запись .jeb} — сопоставление как у asda_fx_blend_fix.align; {} без .jeb."""
    import asda_fx_blend_fix as BF
    jb = BF.read_jeb(jeb) if jeb else None
    return {i: d for i, d in BF.align(ems, jb["emitters"])} if jb else {}


def process_owner(owner, jeb, plan, made, dry):
    """Обработать носителя эмиттеров (CDO блюпринта или актор уровня). Возвращает True, если менялся.

    ★ ГРАБЛЯ Python-UE (поймана контрольным прогоном 09.09): перебор `unreal.Array` структур отдаёт
      КОПИИ, и `e.set_editor_property(...)` меняет копию, а не элемент массива. Первый прогон
      «assets» отработал без единой ошибки, сохранил BP — и эмиттеры остались на исходных MIC.
      Поэтому массив читается в ОБЫЧНЫЙ список, элемент кладётся обратно по индексу, и целиком
      записывается назад."""
    ems = list(owner.get_editor_property("emitters"))
    changed = False
    disk = None                               # {индекс эмиттера: запись .jeb} — лениво, только при эталонной половине
    for idx in range(len(ems)):
        e = ems[idx]
        touched = False
        for field in ("material", "material_b"):
            mic = e.get_editor_property(field)
            if not mic:
                continue
            mname = mic.get_name()
            if "_fxb" in mname:
                continue                      # уже вариант — повторный прогон не наслаивает
            mode = FXB.mode_of_mic(mname)
            if mode is None:
                plan["unknown_mode"].append("%s / %s" % (jeb, mname))
                continue
            st = FXB.settings(jeb, mode)
            if st is None:
                plan["skipped_no_rule"].append("%s [%s]" % (jeb, mode))
                continue
            boost, fade, dist, fam = st
            if field == "material_b" and _etalon_half_stem(mname):
                # ★ 11.10.2026 (решение владельца, `Reports/owner_1011/fx_unboost_meshes.md`): на эталонной
                #   аддитивной половине пары dst = 3 (src 4/2/9) буст не ставится, если прежняя половина srcC×(1−srcA)
                #   давала 0 — там буст Ф1 был невидим, а на новой половине дал бы ×N к эталону.
                if disk is None:
                    disk = _disk_by_index(jeb, ems)
                if _UB.prior_half_zero(disk.get(idx), _etalon_half_stem(mname)):
                    plan.setdefault("prior_zero", []).append("%s / %s" % (jeb, mname))
                    continue
            if FXB.is_noop(st):
                plan["noop"].append("%s [%s] правило %s" % (jeb, mode, fam))
                continue
            _, dst = ensure_variant(mic, boost, fade, dist, made, dry)
            plan["families"].setdefault(fam, {"jebs": set(), "mics": set(), "variants": set()})
            plan["families"][fam]["jebs"].add(FXB.jeb_key(jeb))
            plan["families"][fam]["mics"].add(mname)
            plan["families"][fam]["variants"].add(dst.rsplit("/", 1)[-1])
            if not dry:
                e.set_editor_property(field, EAL.load_asset(dst))
                touched = True
        if touched:
            ems[idx] = e                      # копию — обратно в список (см. шапку функции)
            changed = True
    if changed:
        # ★★★ Ф16 (К-1, 20.09.2026): запись массива эмиттеров — под явным гейтом
        #   `asda.Fx.Glow.EmittersWriter` (обоснование — asda_fx_common.emitters_writer_gate):
        #   признак «пишет скрипт» не должен зависеть от формы события правки.
        with _FXC_GATE.emitters_writer_gate():
            owner.set_editor_property("emitters", ems)
    return changed


def run_blueprints(plan, made, dry):
    """Обход запечённых BP_Fx_* (их CDO несёт EffectName и массив эмиттеров)."""
    names = [n for n in EAL.list_assets(BP_DIR, False, False) if "/BP_Fx_" in n]
    touched = 0
    for n in names:
        path = n.split(".")[0]
        jeb = path.rsplit("/BP_Fx_", 1)[-1]
        if not want(jeb):
            continue
        bp = EAL.load_asset(path)
        if not bp:
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        real_jeb = str(cdo.get_editor_property("effect_name")) or jeb
        if process_owner(cdo, real_jeb, plan, made, dry) and not dry:
            bp.modify()
            if not EAL.save_loaded_asset(bp, False):
                raise RuntimeError("BP не сохранился: %s" % path)
            touched += 1
    unreal.log("[FX-BOOST] BP просмотрено %d, изменено %d" % (len(names), touched))


def run_levels(plan, made, dry):
    """Обход эффект-акторов, размещённых на уровнях карт."""
    for key in MAP_KEYS:
        if key == "none" or key not in MAPS:
            continue
        unreal.EditorLoadingAndSavingUtils.load_map(MAPS[key])
        world = unreal.EditorLevelLibrary.get_editor_world()
        acts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AsdaEffectActor)
        touched = 0
        for a in acts:
            jeb = str(a.get_editor_property("effect_name"))
            if not want(jeb):
                continue
            if process_owner(a, jeb, plan, made, dry):
                touched += 1
        unreal.log("[FX-BOOST] карта %s: акторов %d, изменено %d" % (key, len(acts), touched))
        if touched and not dry:
            unreal.EditorLoadingAndSavingUtils.save_map(world, MAPS[key])


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон tools-no-import-side-effects)."""
    dry = (MODE != "assets")
    unreal.log("[FX-BOOST] режим=%s (dry=%s) охват: only=%s proto=%s карты=%s BP=%s"
               % (MODE, dry, sorted(ONLY) or "—", PROTO, MAP_KEYS, DO_BP))
    plan = {"families": {}, "noop": [], "skipped_no_rule": [], "unknown_mode": []}
    made = set()
    if DO_BP:
        run_blueprints(plan, made, dry)
    run_levels(plan, made, dry)
    out = {"mode": MODE, "dry": dry,
           "families": {k: {"jebs": sorted(v["jebs"]), "mics": sorted(v["mics"]),
                            "variants": sorted(v["variants"])} for k, v in plan["families"].items()},
           "variants_total": sorted(made),
           "noop": sorted(set(plan["noop"])),
           "skipped_no_rule": sorted(set(plan["skipped_no_rule"])),
           "unknown_mode": sorted(set(plan["unknown_mode"])),
           "prior_zero": sorted(set(plan.get("prior_zero", [])))}   # буст не ставится: прежняя половина давала 0
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    for fam, v in sorted(out["families"].items()):
        unreal.log("[FX-BOOST] семейство %s: эффектов=%d, исходных MIC=%d, вариантов=%d"
                   % (fam, len(v["jebs"]), len(v["mics"]), len(v["variants"])))
    unreal.log("[FX-BOOST] вариантов всего=%d, без правила=%d, множитель 1.0 (не трогаем)=%d, чужой суффикс MIC=%d"
               % (len(made), len(out["skipped_no_rule"]), len(out["noop"]), len(out["unknown_mode"])))
    unreal.log("[FX-BOOST] план записан: %s" % OUT)
    unreal.log("FX-BOOST ЗАВЕРШЕНО")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
