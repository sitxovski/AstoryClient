# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_models.py — ФАЗА 1: распаковка архивов моделей (ZIP) + конвертация всех .jbm → .glb,
# каталогизированно в RawAssets/Models/<Char|Mob|Items>/. Анимации (.jbb/.ani) — отдельные фазы.
# Запуск: python asda_build_models.py
import os, sys, zipfile
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asda_jbm_to_gltf import parse_jbm_bytes, to_glb

import asda_paths as _P
import asda_ue_paths as _U
import asda_material_policy as _POL

# ★ 2026-08-02, РАЗОБРАНО И ОСТАВЛЕНО КАК БЫЛО. Этот тул читает ORIG-сборку клиента, а почти все
#   остальные — MINE. Это НЕ опечатка:
#     • CharModel.bnz: 1650 записей в обеих сборках, имена совпадают, но 105 моделей РАЗНЫЕ по
#       содержимому (другая геометрия и/или другое имя текстуры в matTable). MobModel.bnz: 1 из 132.
#       Item/Model: по 243 .jbm, различий нет.
#     • Что лежит в RawAssets/Models СЕЙЧАС — сделано из ORIG. Проверка: у ch1_f_00_chst308_00_00_0.glb
#       имя материала "ch1_f_09_chst200_00_00_0" (текстура ORIG); в MINE у той же модели текстура
#       "»óÀÇ3.TEX". То же различие в обратную сторону есть у других моделей.
#     • Текстуры извлекались из ORIG (asda_extract_textures.py, GAME_ROOT по умолчанию = CLIENT_ORIG),
#       поэтому связка «модель → имя текстуры» согласована ИМЕННО на ORIG.
#   Переключение на MINE переименует материалы у 105 моделей и порвёт их привязку к импортированным
#   текстурам ⇒ это отдельная задача с переэкспортом+переимпортом, а не правка константы.
#   Здесь корень лишь НАЗВАН явно, чтобы расхождение сборок было видно грепом по asda_paths.
GAME = _P.DATA_ORIG
OUT  = _U.RAW_MODELS

# (категория, источник).  Источник: ('zip', путь) или ('dir', папка с .jbm).
SOURCES = [
    ("Char", ("zip", os.path.join(GAME, "Character", "Model", "CharModel.bnz"))),
    ("Mob",  ("zip", os.path.join(GAME, "Mob", "Model", "MobModel.bnz"))),
    ("Items", ("dir", os.path.join(GAME, "Item", "Model"))),
]

# ★★★ МЕТКА ПОЛУПРОЗРАЧНЫХ ПОДСЕТОК (2026-08-18, `Reports/jbm_blend_flag_study.md` §5).
#   Подсетки с рендер-флагом `mr+0x004 & (0x1|0x2)` И техникой tNoSkin получают отдельное имя
#   материала → отдельный слот UE → прозрачный мастер `M_AsdaObjectBlendLit` (выбор мастера —
#   `asda_material_policy.master_for`, поэтому переимпорт воспроизводит результат).
#   ★ ОХВАТ НАМЕРЕННО УЗКИЙ — только группы, чей визуал проверен и чьи импортёры к метке готовы:
#     • Items  — 3D-модели предметов на земле (37 моделей / 48 подсеток: блики, ленты, свитки,
#                ореол `itm1_n_common_00` с альфой 62…206);
#     • Char   — ТОЛЬКО сцена создания/выбора персонажа `cre_*` (задник, комната, шкатулка).
#   НЕ включено (осознанно, отдельные задачи):
#     • экип `ch1_n_06_bbow103_00_00_0` — единственный tNoSkin среди экипа, нужен живой кадр
#       (jbm_blend_flag_study.md §5, строка «⚠ НУЖЕН ЖИВОЙ КАДР»);
#     • статик-модели мобов `Mob/` — их ведёт отдельная задача (реальная текстура моба берётся не
#       из matTable, а из пер-карточных `.MBB`, поэтому альфа-статистика по matTable недостоверна).
BLEND_CATEGORIES = {"Items": lambda base: True,
                    "Char":  lambda base: base.lower().startswith("cre_")}


def blend_suffix_for(cat, base):
    """Суффикс метки для данной модели или None (None = прежнее поведение, имена не меняются)."""
    pred = BLEND_CATEGORIES.get(cat)
    return _POL.BLEND_SUFFIX if (pred and pred(base)) else None


# ★★★ КАТЕГОРИЯ Mob — ИМЕНА МАТЕРИАЛОВ ПО ТЕХНИКЕ (2026-08-18, `Reports/blend_rollout_npc_mobs.md`).
#   Статик-модели мобов (фолбэк для 33 моделей без клипов + превью) ведёт задача NPC/мобов. Здесь
#   одного суффикса `_blend` мало: у 37 слотов мобов ОДНУ текстуру делят подсетки tSkin/tRigid
#   (альфа = маска блика ⇒ непрозрачный мастер) и tNoSkin (альфа = вырезка ⇒ masked), а UE сливает
#   их в ОДИН слот по имени материала. Полный список имён (с `_msk` и `_blend`) считает
#   `asda_mesh_render_class.material_names()` из `meshType`/`renderFlags` самой модели.
MATNAME_CATEGORIES = {"Mob"}


def mat_names_for(cat, mdl):
    """Список имён материалов по подсеткам или None (None = прежнее поведение)."""
    if cat not in MATNAME_CATEGORIES:
        return None
    import asda_mesh_render_class as _MC
    return _MC.material_names(mdl["meshes"], split=True, enable_blend=True)


def convert_one(name, data, outdir, cat):
    mdl = parse_jbm_bytes(data)
    if not mdl or not mdl["meshes"]:
        return False, "parse"
    base = os.path.splitext(os.path.basename(name))[0]
    # ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ИСХОДНЫХ ДАННЫХ «ссылка на текстуру чужой модели» (правило и весь
    #   разбор — `asda_material_policy.fix_cross_model_tex`, отчёт `gacha_model_d_texture.md`).
    #   Тот же `.jbm` конвертирует ещё и `asda_build_skinned_models.py` (скелеталки сцены гачи);
    #   чинить обе ветви надо ОДИНАКОВО, иначе статик-версия автомата и скелеталка разъедутся
    #   по раскраске. Печатаем поимённо — правка не должна быть молчаливой.
    for node, was, now in _POL.fix_cross_model_tex(base, mdl["meshes"]):
        print("     ★ ИСПРАВЛЕН ДЕФЕКТ ОРИГИНАЛА: %s / подсетка %-10s текстура %s → %s"
              % (base, node, was, now))
    dst = os.path.join(outdir, base + ".glb")
    os.makedirs(outdir, exist_ok=True)
    try:
        to_glb(mdl, dst, blend_mat_suffix=blend_suffix_for(cat, base),
               mat_names=mat_names_for(cat, mdl))
        return True, mdl["tex"]
    except Exception as e:
        return False, str(e)[:40]

def iter_jbm(src):
    kind, path = src
    if kind == "zip":
        z = zipfile.ZipFile(path)
        for n in z.namelist():
            if n.lower().endswith(".jbm"):
                yield n, z.read(n)
    else:
        for root, _d, files in os.walk(path):
            for f in files:
                if f.lower().endswith(".jbm"):
                    yield f, open(os.path.join(root, f), "rb").read()

def main():
    # ★ ОХВАТ: ASDA_MODEL_CATS="Items,Char" — конвертировать только эти категории (по умолчанию все).
    #   Нужен для точечной пересборки после правки метки, чтобы не гонять 1650 моделей Char/Mob.
    only = set(s.strip() for s in os.environ.get("ASDA_MODEL_CATS", "").split(",") if s.strip())
    # ★ ТОЧЕЧНАЯ ПЕРЕСБОРКА: ASDA_MODEL_NAMES="^md_gachya_[a-e]_00$" — регексп по имени модели.
    #   Нужен, когда правка касается нескольких моделей: гонять все 1650 записей CharModel.bnz
    #   ради пяти автоматов бессмысленно, а перезапись всех .glb — лишняя разница в дереве.
    #   Пусто (по умолчанию) — фильтра нет, поведение прежнее.
    name_pat = os.environ.get("ASDA_MODEL_NAMES", "").strip()
    name_re = __import__("re").compile(name_pat, __import__("re").I) if name_pat else None
    if name_re:
        print("ФИЛЬТР ИМЁН: %s" % name_pat)
    grand_ok = grand_fail = 0
    for cat, src in SOURCES:
        if only and cat not in only:
            print("[%s] пропуск (ASDA_MODEL_CATS)" % cat); continue
        outdir = os.path.join(OUT, cat)
        ok = fail = 0; fails = []
        for name, data in iter_jbm(src):
            if name_re and not name_re.match(os.path.splitext(os.path.basename(name))[0]):
                continue
            good, info = convert_one(name, data, outdir, cat)
            if good: ok += 1
            else:
                fail += 1
                if len(fails) < 8: fails.append("%s(%s)" % (os.path.basename(name), info))
        print("[%s] конвертировано=%d, не вышло=%d%s" % (cat, ok, fail, ("  напр: " + ", ".join(fails)) if fails else ""))
        grand_ok += ok; grand_fail += fail
    print("=" * 60)
    print("ИТОГО .jbm→.glb: ок=%d, не вышло=%d → %s" % (grand_ok, grand_fail, OUT))

if __name__ == "__main__":
    main()
