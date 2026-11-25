# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_soulbase_model.py — ДУХ ДРУГА (слот Дружбы 0): модель soul_base_idle01.jbm + три клипа → .glb.
#
# Эталон: `soulBase_LoadIdleMesh@0xFB66E0` (модель с тремя дорожками), `SoulBase_LoadStep@0xFB6790`:
#   дорожка 0 = soul_base_idle01.jbb, 1 = soul_base_idle02.jbb, 2 = soul_base_move01.jbb.
# Клипы в .glb называются anim_00/01/02 = НОМЕР ДОРОЖКИ — рантайм (AAsdaSoulBaseActor) грузит "<model>anim_%02d".
# Выход: RawAssets/Models/SoulBase/soul_base_idle01.glb
# Запуск (вне UE): python asda_build_soulbase_model.py
import os, sys, zipfile, importlib.util
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P
import asda_skin_build as SB

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "RawAssets", "Models", "SoulBase")
MODEL = "soul_base_idle01"
TRACKS = ("soul_base_idle01", "soul_base_idle02", "soul_base_move01")   # порядок = дорожка эталона


def load_mod(name, fn):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fn))
    m = importlib.util.module_from_spec(spec); sys.modules[name] = m; spec.loader.exec_module(m); return m


def main():
    conv = load_mod("conv", "asda_skinned_to_gltf.py")
    T = load_mod("T", "asda_test_export_one.py")
    zmodel = zipfile.ZipFile(_P.DATA + r"\Character\Model\CharModel.bnz")
    zmot = zipfile.ZipFile(_P.DATA + r"\Character\Motion\CharMotion.baz")
    jbm = T.parse_jbm_ext(zmodel.read(MODEL + ".jbm"))
    if not jbm:
        print("FAIL: parse_jbm_ext вернул пусто"); return 1
    clips = []
    for t, base in enumerate(TRACKS):
        jbb = conv.parse_jbb(zmot.read(base + ".jbb"))
        if not jbb:
            print("FAIL: не разобран %s.jbb" % base); return 1
        clips.append(("anim_%02d" % t, jbb))
    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.join(OUT_DIR, MODEL + ".glb")
    rep = SB.build_fixed_glb(jbm, clips, out, recover_bones=True, fix_degenerate=True,
                            extra_anchors=("bip01 neck",))   # кость привязки эффектов `soulEffAttachToNeckBone@0xFB6B80`
    print("%s: кости %d→%d клипов=%d bind_err=%.4f вырожденных=%d → %s"
          % (MODEL, rep["bones_before"], rep["bones_after"], len(rep["clips"]), rep["bind_err"], rep["degenerate"],
             os.path.abspath(out)))
    names = [str(b.get("name", b)) if isinstance(b, dict) else str(b) for b in clips[0][1].get("bones", [])]
    print("кости клипа (есть ли Bip01 Neck):", [n for n in names if "neck" in n.lower()], "всего", len(names))
    return 0


if __name__ == "__main__":
    sys.exit(main())
