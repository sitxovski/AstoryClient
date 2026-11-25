# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_gacha_shadow_material.py — «тень» автомата гачи должна быть ПРОЗРАЧНОЙ.
#
# ★ ЧТО НЕ ТАК. У модели автомата есть отдельный слот `gachya_shadow` — плоский квад с мягкой
#   тенью под корпусом. Текстура `gachya_shadow.png` 256×256 имеет АЛЬФУ (0…204, альфа < 255 у
#   100 % пикселей), но импортёр повесил слот на непрозрачный мастер `M_AsdaObjectLit`, и в кадре
#   (замер `Reports/gacha_3d/gacha3d_idle_alpha_over_magenta.png`) тень выходит СПЛОШНЫМ
#   ЧЁРНЫМ ЭЛЛИПСОМ — поверх подложки окна 68000 это была бы чёрная клякса, а не тень.
#
# ★ ЧТО ДЕЛАЕМ. Отдельный мастер `M_AsdaGachaShadow` (Unlit + Translucent: цвет = Tex.rgb,
#   прозрачность = Tex.a) и переподвязка на него ТОЛЬКО слотов `gachya_shadow*` пяти моделей
#   автоматов. Ничего другого не трогаем — MIC-и корпуса и шара остаются на `M_AsdaObjectLit`.
#
# Запуск: run_headless.bat asda_build_gacha_shadow_material.py
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))

import unreal
import asda_gacha_light_node as LN
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

MASTER_PKG = _U.G_MATERIALS
MASTER_NAME = "M_AsdaGachaShadow"
GACHYA_ROOT = _U.G_MODELS_GACHYA

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m):
    unreal.log("[гача-тень] " + str(m))


def connect(frm, frm_out, to, to_in):
    """★ Закон ue-material-script-gotchas: connect МОЛЧА возвращает False при неверном входе."""
    if not MEL.connect_material_expressions(frm, frm_out, to, to_in):
        raise RuntimeError("не соединилось: %s[%s] -> %s[%s]" % (frm, frm_out, to, to_in))


def build_master(save=True):
    """`save=False` — собрать граф ТОЛЬКО В ПАМЯТИ (для замера при занятом .uasset)."""
    log(LN.assert_all_unlit())          # ★ проверка по данным: освещённых подсеток у гачи нет
    path = "%s/%s" % (MASTER_PKG, MASTER_NAME)
    # ★ Пересборка НА МЕСТЕ: удалять ассет нельзя — при открытом редакторе `.uasset` занят
    #   (Error Code 32), и `create_asset` вернул бы None; заодно сохраняются родители MIC.
    mat = LN.make_or_reset_master(MASTER_PKG, MASTER_NAME)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # ★★★ ОДНОСТОРОННИЙ — как в оригинале. `d3dApplyMaterialRenderState@0x111DA90` даёт
    #   `CULLMODE = NONE` только при `meshType & 1`; у квадов `G_shadow`/`Ball_shadow`
    #   meshType = 0x200, бита нет ⇒ `CULLMODE = CW`. С `two_sided = True` плоский
    #   ПОЛУПРОЗРАЧНЫЙ квад смешивался сам с собой (обе грани на одной глубине) и тень выходила
    #   вдвое плотнее оригинала.
    mat.set_editor_property("two_sided", False)
    # ★ Флаг применимости к скелеталке. Тень висит на подсетках `G_shadow` и `Ball_shadow`
    #   СКЕЛЕТАЛЬНОГО меша, а UE применяет материал к скелеталке только при `bUsedWithSkeletalMesh`;
    #   в КУКЕ (`-game`) без флага подставляется `DefaultMaterial` — ровный непрозрачный серый.
    #   ⚠ ЭТО НЕ БЫЛО ПРИЧИНОЙ ЖАЛОБЫ «тень серая»: PIE исполняется в редакторе, где
    #   `UMaterial::CheckMaterialUsage` дописывает флаг на лету, поэтому в PIE серого от флага
    #   быть не могло. Настоящая причина — неверное направление света (см. ниже и
    #   `Reports/gacha_grey_fix2.md`). Флаг всё равно нужен для упакованного билда.
    LN.apply_skeletal_usage(mat)

    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    tex.set_editor_property("parameter_name", "Tex")     # имя параметра — общее для проекта
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    gain = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -220, 200)
    gain.set_editor_property("parameter_name", "ShadowOpacity")
    gain.set_editor_property("default_value", 1.0)
    mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -80, 160)
    connect(tex, "A", mul, "A")
    connect(gain, "", mul, "B")

    # ★★★ ЦВЕТ = РОВНО ТЕКСТУРА. Квады тени, как и весь автомат, идут НЕосвещённым шейдером
    #   (`mr+0x004 & 0x8 == 0` у всех подсеток — проверяется `LN.assert_all_unlit()`):
    #   `mov oD0.xyz, c95.w` = 1.0, стадия `COLOROP = MODULATE(TEXTURE, DIFFUSE)` ⇒ `out = Tex`.
    #   Тень и без множителя тёмная сама по себе: ядро текстуры `gachya_shadow` — RGB 34 из 255.
    #   Прежний множитель (0.48, затем 0.6) был взят из `.fx`-пути, который у сцены гачи выключен
    #   (`g_bUseEffectPath = 0`), — см. `Reports/gacha_color_fix.md`.
    node, out = LN.build_unlit_color(mat, tex, "RGB")

    # ★★★ ТОТ ЖЕ ПОРЯДОК, ЧТО У КУПОЛА: квады `G_shadow`/`Ball_shadow` несут в `.jbm` тот же бит
    #   `mr+0x004 & 1`, значит идут ПЕРВЫМ проходом без записи Z, и любая непрозрачная геометрия
    #   рисуется поверх них. Иначе тень БЛИЖНЕГО автомата затемняла бы корпус ДАЛЬНЕГО — в
    #   оригинале этого не бывает. Полный вывод — `LN.apply_behind_opaque`.
    gate = LN.apply_behind_opaque(mat, log)
    op_final = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -40, 260)
    connect(mul, "", op_final, "A")
    connect(gate, "", op_final, "B")

    MEL.connect_material_property(node, out, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(op_final, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    if save:
        LN.save_master(mat)      # ★ падаем, если .uasset занят: молчаливый пропуск уже стоил часа
    log("мастер готов: %s" % path)
    return mat


def main():
    master = build_master()
    fixed = 0
    for ad in AR.get_assets_by_path(unreal.Name(GACHYA_ROOT), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        if not str(ad.asset_name).lower().startswith("gachya_shadow"):
            continue
        mic = EAL.load_asset(str(ad.package_name))
        if not mic:
            continue
        # перевешиваем родителя, значение `Tex` уже стоит и сохраняется
        tex = MEL.get_material_instance_texture_parameter_value(mic, "Tex")
        mic.set_editor_property("parent", master)
        if tex:
            MEL.set_material_instance_texture_parameter_value(mic, "Tex", tex)
        EAL.save_loaded_asset(mic)
        fixed += 1
        log("переподвязан %s (Tex=%s)" % (str(ad.asset_name), tex.get_name() if tex else "НЕТ"))
    log("готово: MIC переподвязано %d" % fixed)


if __name__ == "__main__":
    main()
