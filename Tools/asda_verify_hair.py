# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_hair.py — ИНВЕНТАРИЗАЦИЯ ПРИЧЁСОК в проекте UE (доказательство «0 причёсок без текстуры»).
#
# Что проверяет для КАЖДОЙ строки hairtable (38 стилей × 3 цвета × 2 пола = 228):
#   1) МЕШ         — /Game/AsdaAssets/Models/Char/<Mesh>/StaticMeshes/<Mesh>   (Mesh = колонка hairtable)
#   2) ТЕКСТУРА    — /Game/AsdaAssets/Textures/Characters/Texture/<tex>, где tex построен ФОРМУЛОЙ ЭТАЛОНА
#                    (entity_char.c:11535, CCharacter_LoadHairModel@0x9E2700):
#                        sprintf("ch1_%c_00_hair00%d_00_0%d_0.TEX", hairStyle + 1, hairColor)
#                    «hair00» + ДЕСЯТИЧНОЕ (style+1) без ведущих нулей: 1→hair001, 11→hair0011, 38→hair0038.
#   3) МАТЕРИАЛ    — у каждого слота меша материал должен быть MaterialInstance с параметром Tex,
#                    в котором лежит реальная Texture2D (иначе меш рендерится без текстуры).
#
# Стили 5..9 (0-база) в оригинале ОТКЛЮЧЕНЫ (hairtable Enabled=0) и ассетов не имеют ни в одном клиенте —
# они выводятся отдельным блоком «отключено в оригинале» и в ошибки НЕ считаются.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_verify_hair.py
# Отчёт:  @@ASTORY_DEV_ROOT@@\Reports\hair_inventory.txt
import unreal, csv, os, io
import os as _au_os, sys as _au_sys   # ★ волна 2 п. 7 (14.09.2026): реестр путей ассетов (Tools/asda_ue_paths.py)
_au_sys.path.insert(0, _au_os.path.dirname(_au_os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
CSV_PATH  = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "ItemDB", "hairtable.csv")
OUT_PATH  = os.path.join(_P.RE_REPORTS, "hair_inventory.txt")
MESH_FMT  = _U.G_MODELS_CHAR + "/%s/StaticMeshes/%s"
TEX_FMT   = _U.G_TEX_CHAR_TEXTURE + "/%s"

EAL = unreal.EditorAssetLibrary


def hair_tex_name(gender, style, color):
    """Формула эталона: ch1_{m|f}_00_hair00{style+1}_00_0{color}_0 (без ведущих нулей у style+1)."""
    return "ch1_%s_00_hair00%d_00_0%d_0" % ("m" if gender == 1 else "f", style + 1, color)




def canon_slot_tex(slot_name):
    """Имя слота → каноническое имя текстуры по формуле эталона (hairNNN → hair00N); None если не причёска."""
    m = _HAIR_RE.match(slot_name.lower())
    if not m: return None
    pref, style, mid, color, tail = m.groups()
    return "%shair00%d_%s_%02d_%s" % (pref, int(style), mid, int(color), tail)


def slot_tex_ok(mesh_asset):
    """(всего слотов, слотов с ПРАВИЛЬНОЙ текстурой в Tex, [имена проблемных слотов]).
    Правильная = ровно та, что даёт формула эталона по имени слота (иначе — DefaultTexture/чужая)."""
    mats = mesh_asset.get_editor_property("static_materials")
    total = ok = 0
    bad = []
    for slot in mats:
        total += 1
        name = str(slot.get_editor_property("material_slot_name"))
        mi = slot.get_editor_property("material_interface")
        tex = None
        if mi:
            try:
                tex = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mi, unreal.Name("Tex"))
            except Exception:
                tex = None
        tname = tex.get_name().lower() if tex else ""
        want = canon_slot_tex(name) or name.lower()
        if tname and tname == want:
            ok += 1
        else:
            bad.append("%s (mat=%s, tex=%s, ожидалась=%s)" % (name, mi.get_name() if mi else "НЕТ", tname or "НЕТ", want))
    return total, ok, bad


def main():
    rows = list(csv.DictReader(io.open(CSV_PATH, encoding="utf-8")))
    L = []
    n_ok = n_err = n_disabled = 0
    err_lines = []
    dis_lines = []
    seen_mesh = {}
    for r in rows:
        gender = int(r["Gender"]); style = int(r["HairStyle"]); color = int(r["ColorIdx"])
        enabled = int(r["Enabled"]); mesh = r["Mesh"]
        tex = hair_tex_name(gender, style, color)
        mesh_path = MESH_FMT % (mesh, mesh)
        tex_path  = TEX_FMT % tex
        has_mesh = EAL.does_asset_exist(mesh_path)
        has_tex  = EAL.does_asset_exist(tex_path)
        mat_note = "-"
        if has_mesh:
            if mesh not in seen_mesh:
                a = unreal.load_asset(mesh_path)
                seen_mesh[mesh] = slot_tex_ok(a) if a else (0, 0, ["меш не загрузился"])
            total, okn, bad = seen_mesh[mesh]
            mat_note = "%d/%d слот(ов) с Tex" % (okn, total)
            mat_bad = bad
        else:
            mat_bad = []
        line = "пол=%d стиль=%2d цвет=%d | меш=%-28s %s | тех=%-28s %s | мат: %s" % (
            gender, style, color, mesh, "OK " if has_mesh else "НЕТ", tex, "OK " if has_tex else "НЕТ", mat_note)
        if not enabled:
            n_disabled += 1
            dis_lines.append(line)
            continue
        if has_mesh and has_tex and not mat_bad:
            n_ok += 1
        else:
            n_err += 1
            err_lines.append(line + ("  ПРОБЛЕМНЫЕ СЛОТЫ: %s" % "; ".join(mat_bad) if mat_bad else ""))
        L.append(line)

    out = []
    out.append("ИНВЕНТАРИЗАЦИЯ ПРИЧЁСОК (hairtable × ассеты проекта)")
    out.append("строк всего: %d | OK: %d | ПРОБЛЕМ: %d | отключено в оригинале (Enabled=0): %d"
               % (len(rows), n_ok, n_err, n_disabled))
    out.append("")
    out.append("== ПРОБЛЕМЫ ==" if err_lines else "== ПРОБЛЕМ НЕТ ==")
    out += err_lines
    out.append("")
    out.append("== ОТКЛЮЧЕНО В ОРИГИНАЛЕ (Enabled=0, ассетов нет ни в одном клиенте) ==")
    out += dis_lines
    out.append("")
    out.append("== ПОЛНАЯ ТАБЛИЦА (включённые стили) ==")
    out += L
    txt = "\n".join(out)
    try:
        os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
        io.open(OUT_PATH, "w", encoding="utf-8").write(txt)
    except Exception as e:
        unreal.log_warning("Не записан отчёт: %s" % e)
    unreal.log("ИТОГ: OK=%d ПРОБЛЕМ=%d ОТКЛЮЧЕНО=%d → %s" % (n_ok, n_err, n_disabled, OUT_PATH))
    for l in err_lines[:60]:
        unreal.log_warning(l)


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global _HAIR_RE
    _HAIR_RE = __import__("re").compile(r'^(ch1_[mf]_\d+_)hair(\d+)_(\d+)_(\d+)_(\d+)$')
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
