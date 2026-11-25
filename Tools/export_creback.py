# Copyright (c) 2026 @sitxovski (Discord)
# Точечный экспорт login-фона cre_creback-a_00 (skinned .jbm + анимация .jbb → .glb).
# Тот же conv-модуль/логика, что и asda_build_skinned_models.py, но ТОЛЬКО эта модель.
# Запуск (вне UE): python export_creback.py
import os, importlib.util, zipfile
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = _P.DATA
MODEL_ARC  = GAME + r"\Character\Model\CharModel.bnz"
MOTION_ARC = GAME + r"\Character\Motion\CharMotion.baz"
OUT_DIR = os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters")
MODEL = "cre_creback-a_00"

# ★ ВОЛНА 2 (14.09.2026, дефект E10): сборка идёт через ЕДИНЫЙ вход asda_skin_build.build_skinned_glb.
#   Режим по умолчанию для задника — СТАРЫЙ путь (asda_skin_build.MODE_LEGACY), это решение, а не забытая
#   ветка: модель логина, fixed-путь меняет геометрию (отклонение от игры 412.18 → 0.0005 ед,
#   `Reports/dedup_audit/wave2_import.md` §4), переимпорт логина запрещён. Текущий .glb старым путём
#   воспроизводится байт в байт. Fixed-путь: ASDA_LOGIN_SKIN_FIXED=1 (только решением владельца).
import asda_skin_build as SB

def main():
    zmodel = zipfile.ZipFile(MODEL_ARC)
    zmot   = zipfile.ZipFile(MOTION_ARC)

    jbm_name = MODEL + ".jbm"
    jbm_bytes = zmodel.read(jbm_name)

    # клипы модели: cre_creback-a_00_N.jbb (N — индекс анимации). Главный сцена-клип = _0.
    clip_names = sorted(n for n in zmot.namelist()
                        if os.path.splitext(os.path.basename(n))[0].startswith(MODEL + "_"))
    print("клипы:", clip_names)
    clips = []
    for cn in clip_names:
        base = os.path.splitext(os.path.basename(cn))[0]
        try: N = int(base[len(MODEL) + 1:])
        except ValueError: continue
        jbb = SB.parse_jbb(zmot.read(cn))
        if jbb: clips.append(("anim_%02d" % N, jbb))
    if not clips:
        print("FAIL: нет клипов для", MODEL); return

    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.abspath(os.path.join(OUT_DIR, MODEL + ".glb"))
    # ★ ПОЛУПРОЗРАЧНЫЕ ПОДСЕТКИ (2026-08-18, `Reports/jbm_blend_flag_study.md` §5): у задника
    #   667 подсеток из 697 имеют рендер-флаг блендинга при технике tNoSkin. Метка `_blend` даёт им
    #   отдельные слоты (build() дедуплицирует материалы по имени текстуры, а текстуры
    #   `cre_creback-l/m/v/x_00` делят и прозрачные, и непрозрачные подсетки).
    #   ★ Этот .glb — СКРЫТЫЙ ФОЛБЭК `LoginBackdrop_OLD`; сцену рисуют 697 статик-акторов
    #     (asda_build_backdrop_elements.py). Держим оба пути согласованными.
    import asda_material_policy as _POL
    mode = SB.mode_for(MODEL)
    print("режим сборки скина:", mode)
    try:
        # линковка скина по референс-клипу — внутри единого входа; fix_tex=False — как было
        SB.build_skinned_glb(MODEL, jbm_bytes, clips, out, mode=mode,
                             blend_mat_suffix=_POL.BLEND_SUFFIX, fix_tex=False)
    except RuntimeError as e:
        print("FAIL: не собран", jbm_name, e); return
    print("OK →", out, "| клипов:", len(clips), "| размер:", os.path.getsize(out) if os.path.exists(out) else "нет")

if __name__ == "__main__":
    main()
