# Copyright (c) 2026 @sitxovski (Discord)
# Декомпозиция login-фона cre_creback-a_00: 697 ригидных под-мешей (0 костей, статика) → отдельные
# редактируемые элементы. Правило [[ue-editor-friendly]]: каждый деревце/травинка = свой актор.
#
# Пишет (для UE-билдера asda_build_backdrop_elements.py):
#   RawAssets/backdrop/backdrop_meshes.npz  — уникальная геометрия (дедуп): pos/uv/idx на meshId
#   RawAssets/backdrop/backdrop_elements.json — размещения: [{name, meshId, center[gx,gy,gz], tex}]
# Позиции — СЫРЫЕ игровые (рецентрированы к центру элемента = пивот); to_ue применит билдер (линейно,
# actor_loc=to_ue(center) + mesh=to_ue(pos-center) = to_ue(pos) → верная раскладка + пивот на элементе).
import importlib.util, zipfile, os, sys, json, hashlib
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_jbm as _JBM        # ★ критерий прозрачности подсетки (техника tNoSkin + рендер-флаг)
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = _P.DATA
OUT = os.path.join(HERE, "..", "RawAssets", "backdrop")

spec = importlib.util.spec_from_file_location("conv", os.path.join(HERE, "asda_skinned_to_gltf.py"))
conv = importlib.util.module_from_spec(spec); sys.modules["conv"] = conv; spec.loader.exec_module(conv)


uniq = {}            # hash → meshId
umesh = {}           # meshId → dict(pos_flat, uv_flat, idx)
placements = []

# запись — ВСЁ в JSON (UE-python без numpy читает нативно). Геометрия округлена (компактнее).
meshes_json = {}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    os.makedirs(OUT, exist_ok=True)
    z = zipfile.ZipFile(GAME + r"\Character\Model\CharModel.bnz")
    jbm = conv.parse_jbm(z.read("cre_creback-a_00.jbm"))
    meshes = jbm["meshes"]
    print("под-мешей:", len(meshes))
    for i, m in enumerate(meshes):
        pos = np.asarray(m["pos"], np.float32)     # (V,3) мировые
        uv = np.asarray(m["uv"], np.float32)       # (V,2)
        idx = np.asarray(m["idx"], np.uint32)      # тройки
        if len(pos) == 0 or len(idx) < 3:
            continue
        center = ((pos.min(0) + pos.max(0)) * 0.5).astype(np.float32)
        local = (pos - center).astype(np.float32)  # рецентр к пивоту элемента
        # хэш геометрии (рецентр, округл.) для дедупа одинаковых элементов
        h = hashlib.md5(np.round(local, 2).tobytes() + np.round(uv, 4).tobytes() + idx.tobytes()).hexdigest()[:16]
        if h not in uniq:
            mid = "m%03d" % len(uniq)
            uniq[h] = mid
            umesh[mid] = dict(pos=local.ravel(), uv=uv.ravel(), idx=idx)
        mid = uniq[h]
        tex = str(m.get("tex", "")).replace(".TEX", "").replace(".tex", "")
        # ★★★ ПРОЗРАЧНОСТЬ ЭЛЕМЕНТА (2026-08-18, `Reports/jbm_blend_flag_study.md` §5):
        #   рендер-флаг `mr+0x004 & (0x1|0x2)` И техника tNoSkin. У задника это 667 подсеток из 697
        #   (остальные 30 — непрозрачные). Билдер по этому полю выбирает мастер, поэтому пересборка
        #   задника воспроизводит прозрачность, а не откатывает её (грабля «билдер пере-привязывает
        #   MIC к MAT_MASTER на каждом прогоне»).
        placements.append(dict(name="bg_%03d_%s" % (i, m.get("node", "")), meshId=mid,
                               center=[float(center[0]), float(center[1]), float(center[2])], tex=tex,
                               blend=bool(_JBM.is_alpha_blend(m))))
    for k, m in umesh.items():
        meshes_json[k] = dict(
            pos=[round(float(x), 3) for x in m["pos"]],
            uv=[round(float(x), 5) for x in m["uv"]],
            idx=[int(x) for x in m["idx"]])
    with open(os.path.join(OUT, "backdrop_elements.json"), "w", encoding="utf-8") as f:
        json.dump(dict(elements=placements, meshIds=list(umesh.keys()), meshes=meshes_json), f)
    texset = sorted(set(p["tex"] for p in placements))
    nblend = sum(1 for p in placements if p["blend"])
    print("уникальных мешей (после дедупа): %d / %d под-мешей" % (len(umesh), len(placements)))
    print("полупрозрачных (tNoSkin + рендер-флаг): %d из %d" % (nblend, len(placements)))
    print("текстур: %d → %s" % (len(texset), texset))
    print("записано:", OUT)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
