# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_probe_master.py — почему автоматы серые: параметры мастера и значения в MIC.
# ★ Закон normal-from-albedo-studio: у сломанного материала num_pixel_shader_instructions = 0,
#   и движок МОЛЧА подставляет серый дефолт — ровно наш симптом. Проверять только с настоящим RHI.
# Запуск (с RHI): UnrealEditor-Cmd.exe <проект> -ExecCmds="py <этот файл>" -stdout ...
import unreal

MASTER = "/Game/Materials/M_AsdaObjectLit"
MICS = ["/Game/AsdaAssets/Models/Skeletal/Gachya/md_gachya_a_00/Materials/md_Gachya_a_0014",
        "/Game/AsdaAssets/Models/Skeletal/Gachya/md_gachya_a_00/Materials/gachya_shadow1"]
# для сравнения — MIC модели, которая в проекте заведомо показывается с текстурой
REF_MIC_DIR = "/Game/AsdaAssets/Models/NPC"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary


def log(m):
    unreal.log("[гача-мастер] " + str(m))


def stats(mat):
    st = MEL.get_statistics(mat)
    out = []
    for k in ("num_pixel_shader_instructions", "num_vertex_shader_instructions", "num_samplers"):
        try:
            out.append("%s=%s" % (k, st.get_editor_property(k)))
        except Exception:
            pass
    return ", ".join(out)


def main():
    master = EAL.load_asset(MASTER)
    if not master:
        log("НЕТ мастера %s" % MASTER)
        return
    MEL.recompile_material(master)
    log("мастер %s :: %s" % (MASTER, stats(master)))
    names = MEL.get_texture_parameter_names(master)
    log("текстурные параметры мастера: %s" % ", ".join(str(n) for n in names))
    log("скалярные: %s" % ", ".join(str(n) for n in MEL.get_scalar_parameter_names(master)))
    log("векторные: %s" % ", ".join(str(n) for n in MEL.get_vector_parameter_names(master)))

    for p in MICS:
        mic = EAL.load_asset(p)
        if not mic:
            log("НЕТ MIC %s" % p)
            continue
        vals = []
        for n in names:
            try:
                t = MEL.get_material_instance_texture_parameter_value(mic, n)
                vals.append("%s=%s" % (n, t.get_name() if t else "НЕТ"))
            except Exception as e:
                vals.append("%s=<%s>" % (n, e))
        log("MIC %s :: %s" % (mic.get_name(), "; ".join(vals)))
        # тот же список, но как ХРАНИТСЯ в оверрайдах
        stored = [(str(v.get_editor_property("parameter_info").get_editor_property("name")),
                   v.get_editor_property("parameter_value").get_name()
                   if v.get_editor_property("parameter_value") else "НЕТ")
                  for v in mic.get_editor_property("texture_parameter_values")]
        log("    оверрайды в MIC: %s" % stored)
        # текстура: размер и сжатие
        for _n, tv in stored:
            tex = None
            for v in mic.get_editor_property("texture_parameter_values"):
                pv = v.get_editor_property("parameter_value")
                if pv and pv.get_name() == tv:
                    tex = pv
            if tex:
                log("    текстура %s: %dx%d, сжатие=%s, sRGB=%s" % (
                    tex.get_name(), tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
                    tex.get_editor_property("compression_settings"),
                    tex.get_editor_property("srgb")))

    unreal.SystemLibrary.quit_editor()


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        unreal.log_error("[гача-мастер] СБОЙ:\n" + traceback.format_exc())
        unreal.SystemLibrary.quit_editor()
