# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_weapon_missing_tex.py — доназначение текстуры мешам семейства ch1_*, у которых MIC БЕЗ "Tex".
#
# ПРИЧИНА: у 8 моделей оружия строка материала внутри .jbm — МУСОР (корейская кодировка: «È°», «¼®±Ã»,
#   «ÇÑ¼Õ°Ë»...), у части — имя ЧУЖОЙ текстуры (ch1_n_90_bbow209 → bbow203, ch1_n_00_swrd003 → swrd002).
#   Оригинал эту строку НЕ читает вообще: текстура предмета берётся из item_img_table (ImgRecord+36 муж /
#   +100 жен, эталон CCharacter_SetParts_Armor@0x9EB3C0 — String_Append(texPath, imgRecord+36) + ".TEX").
#   В рантайме порт так и делает (AsdaCharAssembly::ApplyItemTexture), но в РЕДАКТОРЕ такие меши белые.
# ФИКС (только для редактора/превью): текстура по ИМЕНИ САМОГО МЕША ch1_{b|n|p}_NN_xxxNNN_.. →
#   ch1_n_NN_xxxNNN_.. (канонический «n»-вариант), с откатом на срез хвостовых цифр.
# Запуск: run_headless.bat asda_fix_weapon_missing_tex.py [-dry]
import os, re, sys
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U
import unreal

import asda_material_policy as POL

ROOTS = [_U.G_MODELS_CHAR]
TEX_ROOT = _U.G_TEX_CHARACTERS
DRY = any(a.lower() in ("-dry", "--dry") for a in sys.argv)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m): unreal.log_warning("[weaptex] %s" % m)


# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «chars_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("chars_last", root=TEX_ROOT)


_RE = re.compile(r'^(ch1_)[bnp](_.+)$')


def guess_tex(meshname, tex):
    """Кандидаты: имя меша и его «n»-вариант; у каждого точное → срез хвостовых цифр (библиотека)."""
    k = meshname.lower()
    return MI.find_tex_candidates([k] + ([_RE.sub(r'\1n\2', k)] if _RE.match(k) else []), tex)


def main():
    tex = tex_index()
    log("текстур персонажа в индексе: %d" % len(tex))
    n_no = n_fix = 0
    for root in ROOTS:
        for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
            if str(ad.asset_class_path.asset_name) != "StaticMesh":
                continue
            name = str(ad.asset_name)
            if not POL.is_char_family(name):
                continue
            m = unreal.load_asset(str(ad.package_name))
            if not m:
                continue
            for s in m.get_editor_property("static_materials"):
                mi = s.material_interface
                if not isinstance(mi, unreal.MaterialInstanceConstant):
                    continue
                has = False
                for tp in (mi.get_editor_property("texture_parameter_values") or []):
                    if str(tp.get_editor_property("parameter_info").get_editor_property("name")) == "Tex":
                        has = tp.get_editor_property("parameter_value") is not None
                if has:
                    continue
                n_no += 1
                tp = guess_tex(name, tex)
                if not tp:
                    log("нет текстуры-кандидата: %s" % name)
                    continue
                if not DRY:
                    MEL.set_material_instance_texture_parameter_value(mi, "Tex", unreal.load_asset(tp))
                    mi.modify()
                    EAL.save_loaded_asset(mi, False)
                n_fix += 1
                log("%s → %s%s" % (name, tp.rsplit("/", 1)[-1], " (DRY)" if DRY else ""))
    log("ИТОГ: без Tex было %d, назначено %d" % (n_no, n_fix))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
