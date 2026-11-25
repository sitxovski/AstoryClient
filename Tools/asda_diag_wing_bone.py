# Copyright (c) 2026 @sitxovski (Discord)
# asda_diag_wing_bone.py — ДИАГНОСТИКА крепления крыльев: анимирована ли кость "M" (эталонная точка
#   крепления крыла, CCharacter_UpdateWingEffect@0xA4BAA0) и куда она уходит в клипах idle/бег.
#   Симптом: крылья на уровне пояса (bind-CS кости M = Z 8265 при росте 15010) вместо лопаток.
# Запуск: run_headless.bat asda_diag_wing_bone.py
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

BASE = "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview"
CLIPS = [12, 13, 17, 19]   # idle01, idle02(мирный покой), run01, back01
BONES = ["M", "G", "0", "1", "2", "3", "4", "7"]


def log(m): unreal.log_warning("[wing-diag] %s" % m)


def main():
    AL = unreal.AnimationLibrary
    for idx in CLIPS:
        path = "%sanim_%02d" % (BASE, idx)
        anim = unreal.load_asset(path)
        if not anim:
            log("НЕТ клипа %s" % path); continue
        try:
            names = [str(n) for n in AL.get_animation_track_names(anim)]
        except Exception as e:
            log("anim_%02d: треки недоступны: %s" % (idx, e)); names = []
        has = [b for b in BONES if b in names]
        log("anim_%02d: треков=%d, длина=%.2fс; из искомых есть: %s" %
            (idx, len(names), anim.get_editor_property("sequence_length"), has))
        # Позы искомых костей в начале и середине клипа (кость→родитель — локальная поза трека).
        for b in BONES:
            if b not in names: continue
            for t in (0.0, anim.get_editor_property("sequence_length") * 0.5):
                try:
                    tr = AL.get_bone_pose_for_time(anim, b, t, False)
                    loc = tr.translation; rot = tr.rotation.rotator()
                    log("   %s @%.2fс loc=(%.1f, %.1f, %.1f) rot=(P%.1f Y%.1f R%.1f)" %
                        (b, t, loc.x, loc.y, loc.z, rot.pitch, rot.yaw, rot.roll))
                except Exception as e:
                    log("   %s: поза недоступна: %s" % (b, e))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
