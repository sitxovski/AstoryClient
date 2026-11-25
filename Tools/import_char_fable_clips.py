# Copyright (c) 2026 @sitxovski (Discord)
# import_char_fable_clips.py — headless-импорт клипов персонажа из донора AsdaFableClassic на СУЩЕСТВУЮЩИЙ скелет
# ch1_{m|f}_cremmy_preview_Skeleton. Меши/скелет/прочие anim_NN не трогаются.
# Наборы (переменная окружения ASDA_FABLE_CLIPSET, по умолчанию «jump» — поведение до 24.10.2026):
#   jump   — ПРЫЖОК и ПЛАВАНИЕ anim_235..238, источник RawAssets/Models/Skeletal/Characters/fable_clips/
#   crouch — ★ 24.10.2026 ХОДЬБА ВПРИСЯДКУ anim_239 (донорский hidewalk_01), источник .../fable_crouch/
#   (оба glb — export_char_fable_clips.py <набор>). Импорт набора трогает ТОЛЬКО свои номера anim_NN.
# Механика — общая `asda_char_clips.import_gender_clips` (как у эмоций и копания).
# Запуск: set ASDA_FABLE_CLIPSET=crouch && python run_headless_safe.py import_char_fable_clips.py --marker "ИТОГ: ВСЕ OK"
import os, sys, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CHAR_RAW = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters"))
sys.path.insert(0, HERE)
import asda_ue_paths as _U
import asda_char_clips as _CC
DEST_BASE = _U.G_MODELS_CHARACTERS + "/ch1_%s_cremmy_preview/SkeletalMeshes"
SKEL = {g: _U.skel_mesh(_U.G_MODELS_CHARACTERS, _U.char_preview_name(g == "m"), "_Skeleton") for g in ("m", "f")}
# Набор → (папка glb, временная папка UE, ожидаемые номера anim_NN).
CLIPSETS = {
    "jump":   ("fable_clips",  "/Game/Tmp_FableClips",  [235, 236, 237, 238]),   # прыжок с места/с бега, плавание в движении/на месте
    "crouch": ("fable_crouch", "/Game/Tmp_FableCrouch", [239]),                  # ходьба вприсядку (hidewalk_01)
}


def log(m): unreal.log("[import_fable_clips] " + str(m))


def main():
    s = os.environ.get("ASDA_FABLE_CLIPSET", "jump").strip().lower() or "jump"
    if s not in CLIPSETS:
        log("!! неизвестный набор ASDA_FABLE_CLIPSET=%s (есть: %s)" % (s, ", ".join(CLIPSETS)))
        log("ИТОГ: ЕСТЬ ПРОБЛЕМЫ"); return
    folder, tmp, expect = CLIPSETS[s]
    raw = os.path.join(CHAR_RAW, folder)
    log("набор %s: %s -> anim_%s" % (s, raw, ",".join(str(n) for n in expect)))
    ok = True
    for g in ("m", "f"):
        ok = _CC.import_gender_clips(g, raw, SKEL[g], tmp, DEST_BASE, expect, log) and ok
    log("ИТОГ: %s" % ("ВСЕ OK" if ok else "ЕСТЬ ПРОБЛЕМЫ"))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
