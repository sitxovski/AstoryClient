# Copyright (c) 2026 @sitxovski (Discord)
# Экспорт шкатулки логина/создания-персонажа cre_creation01-a_00 (skinned .jbm + клипы .jbb → .glb).
# Модель = ШКАТУЛКА (41 узел, тексты cre_creation01-a_00..03 = оранж/золото с шестерёнками).
# Грузится charCreationBuildPreview@0xF9ED20 в позицию (0,0,0); клипы _0(idle/закрыта)/_1/_2(открытие).
# Запуск: python export_casket.py
import os, importlib.util, zipfile
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = _P.DATA
MODEL_ARC  = GAME + r"\Character\Model\CharModel.bnz"
MOTION_ARC = GAME + r"\Character\Motion\CharMotion.baz"
OUT_DIR = os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters")
MODEL = "cre_creation01-a_00"

# ★ ВОЛНА 2 (14.09.2026, дефект E10): сборка идёт через ЕДИНЫЙ вход asda_skin_build.build_skinned_glb.
#   Режим по умолчанию для шкатулки — СТАРЫЙ путь (asda_skin_build.MODE_LEGACY), это решение, а не забытая
#   ветка: модель стоит в Login.umap, fixed-путь меняет геометрию (отклонение от игры 8.31 → 0.33 ед,
#   `Reports/dedup_audit/wave2_import.md` §4), переимпорт логина запрещён. Текущий .glb старым путём
#   воспроизводится байт в байт. Fixed-путь: ASDA_LOGIN_SKIN_FIXED=1 (только решением владельца).
import asda_skin_build as SB

# ★ РАСКЛАДКА ТРЕКОВ 1:1 с эталоном CCharCreate_BuildMotionPath@0xF9F870 (12 мотий, индексы 0-11):
#   0-2  = _creation01-a_00_{0,1,2}  (idle/появление шкатулки; трек 0 = charSelect_PlaySlotAnim mode 0)
#   3-5  = _credel01-a_00_{0,1,2}    (закрытие/del; mode 1 = curSlot+3)
#   6-8  = _lturn01-a_00_{0,1,2}     (поворот ВЛЕВО; mode 2 = 8-curSlot)
#   9-11 = _rturn01-a_00_{0,1,2}     (поворот ВПРАВО; mode 3 = curSlot+9)
# Все клипы на ОДНОМ скелете (45 костей, тот же .jbm). anim_00..11 = эти индексы.
# Общий префикс модели в игре = "cre" (off_1455044), файлы <pfx>_<suffix>-a_00_<n>.jbb.
PFX = "cre"
CLIP_SUFFIXES = ["_creation01", "_credel01", "_lturn01", "_rturn01"]

def main():
    zmodel = zipfile.ZipFile(MODEL_ARC)
    zmot   = zipfile.ZipFile(MOTION_ARC)
    jbm_bytes = zmodel.read(MODEL + ".jbm")
    names = set(zmot.namelist())
    def find(fname):
        for n in names:
            if os.path.basename(n) == fname: return n
        return None
    clips = []
    idx = 0
    for suf in CLIP_SUFFIXES:
        for slot in range(3):
            fname = "%s%s-a_00_%d.jbb" % (PFX, suf, slot)
            n = find(fname)
            if n is None:
                print("ПРОПУСК (нет в архиве):", fname); idx += 1; continue
            jbb = SB.parse_jbb(zmot.read(n))
            if jbb: clips.append(("anim_%02d" % idx, jbb)); print("  anim_%02d <- %s (B=%d)" % (idx, fname, jbb['B']))
            idx += 1
    if not clips:
        print("FAIL нет клипов"); return
    # ★★ ЕДИНЫЙ bind = ПОСЛЕДНИЙ кадр клипа _0 для ВСЕХ костей (подтверждено дампом login_casket.dmp +
    #   casket_dump_analysis.md). RAW-верты ВСЕЙ шкатулки (крышка b-*, шестерни c-*, корпус a-*) авторены
    #   в ПОЛНОСТЬЮ ОТКРЫТОМ (последнем) кадре: raw b-02 Y≈269≈lastY 266, raw c-05 Y≈60≈lastY 61.
    #   → node-rest = кадр0 = ЗАКРЫТО (движковый механизм: крышка ВНИЗ Y63-111 + шестерни ВВЕРХ Y185-247);
    #   проигрывание _0 вперёд = открывание (крышка ВВЕРХ, шестерни ВНИЗ). auto_bind (пер-кость) НЕВЕРЕН —
    #   ломал когерентность (шестерни всплывали не туда). Единый last-frame bind = 1:1 движок.
    #   ★ Используется только СТАРЫМ путём; fixed-путь восстанавливает истинный bind из POSITION0/POSITION1.
    B = clips[0][1]['B']
    bind_frames = {i: 9999 for i in range(B)}   # build() клампит к keyCount-1 = последний кадр
    mode = SB.mode_for(MODEL)
    print("режим сборки скина:", mode)
    if mode == SB.MODE_LEGACY:
        print("uniform bind = последний кадр для", B, "костей")
    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.abspath(os.path.join(OUT_DIR, MODEL + ".glb"))
    # ★ ПОЛУПРОЗРАЧНАЯ ПОДСЕТКА (2026-08-18, `Reports/jbm_blend_flag_study.md` §5).
    #   У шкатулки помечена ОДНА подсетка — узел `c-04`, техника tNoSkin(0x200), рендер-флаг 0x1,
    #   текстура `cre_creation01-a_03.TEX`, у которой альфа РОВНО 51 (=0.2) у 100 % пикселей.
    #   На masked-мастере с порогом 0.5 такой меш не рисуется ВООБЩЕ (в оригинале это ореол
    #   свечения на 20 % непрозрачности). Метка `_blend` заводит ему отдельный слот — без неё
    #   он делит слот с корпусом (build() дедуплицирует материалы по имени текстуры).
    import asda_material_policy as _POL
    try:
        # fix_tex=False — как было: правило «чужой текстуры» касается только автоматов гачи
        SB.build_skinned_glb(MODEL, jbm_bytes, clips, out, mode=mode, bind_frames=bind_frames,
                             blend_mat_suffix=_POL.BLEND_SUFFIX, fix_tex=False)
    except RuntimeError as e:
        print("FAIL", MODEL, e); return
    print("OK ->", out, "| клипов:", len(clips), "| размер:", os.path.getsize(out))

if __name__ == "__main__":
    main()
