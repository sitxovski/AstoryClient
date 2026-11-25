# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_gacha_ui_material.py — материал вывода 3D-сцены премиум-гачи в окно 68000.
#
# ★ ЗАЧЕМ ОН НУЖЕН (доказано замером, `Reports/gacha_3d_impl.md` §6).
#   Оригинал чистит render-target цветом **0x00000000** (`PetWindow_RenderToTarget@0xB6D5B0`,
#   npc_mob_pet.c:159) — то есть фон кадра ПРОЗРАЧНЫЙ, и 3D ложится поверх подложки `capback`
#   (контрол 68059) и под полосы автоматов. Голый render-target в кисти даёт чёрный прямоугольник,
#   поэтому кадр обязан склеиваться материалом.
#
# ★★★ ОТКУДА БЕРЁТСЯ НЕПРОЗРАЧНОСТЬ (переделано 2026-08-18, `Reports/gacha_alpha_composite.md`).
#   Раньше кадр снимался ДВУМЯ капчерами: цвет из `SCS_FinalColorLDR` + отдельная маска из
#   `SCS_SceneColorHDR`, потому что при выключенном `r.PostProcessing.PropagateAlpha` альфа не
#   доживала до тонмаппера (была 255 везде). После включения `r.PostProcessing.PropagateAlpha=1`
#   (`Config/DefaultEngine.ini`) альфа ДВУХ капчеров совпала БИТ В БИТ (кадры `sb_m1_color.png` /
#   `sb_m1_mask.png`: A==0 у 47 194 пикс., A==255 у 849 968, 0<A<255 у 24 438 — одинаково).
#   ⇒ второй капчер убран, и цвет, и альфа берутся из ОДНОГО RT.
#   Соглашение UE: `A` = ОБРАТНАЯ непрозрачность ⇒ `op = 1 − SceneColor.A`.
#
# ★★★ ЦВЕТ В КАДРЕ ПРЕДУМНОЖЕН НА НЕПРОЗРАЧНОСТЬ — и это надо ОТМЕНИТЬ.
#   Калибровка (`Tools/asda_gacha_shadow_check.py`, шаг `calib`): три плашки на своём мастере
#   `M_AsdaAlphaCalibProbe` (Unlit+Translucent) с ЛИНЕЙНЫМ цветом 0.21586 (= sRGB-байт 128) и
#   заданной непрозрачностью 0.25 / 0.50 / 0.80. Кадр вернул:
#
#     задано op | A в кадре | op = 1−A/255 | RGB кадра | «прямая» ждёт | «предумнож.» ждёт
#        0.25   |    191    |    0.251     |   66.0    |      128      |      65.7
#        0.50   |    127    |    0.502     |   92.0    |      128      |      92.3
#        0.80   |     51    |    0.800     |  115.0    |      128      |     115.4
#
#   Совпадение с предумноженной моделью — до 0.4 байта на всех трёх точках; прямая ошибается на
#   62 байта. ⇒ в RT лежит `sRGB(lin(C)·op)`, а не `C`.
#
#   Slate для UI-материала с `BLEND_TRANSLUCENT` ставит `BF_SourceAlpha / BF_InverseSourceAlpha`
#   (`SlateRHIRenderingPolicy.cpp:566`), то есть УМНОЖАЕТ цвет на непрозрачность ЕЩЁ РАЗ:
#   получалось `C·op²` — прозрачное темнело вдвое против оригинала. Оригинал же (D3D9,
#   `CRenderer_ApplyBlendRenderState@0x9764F0`, SRCALPHA/INVSRCALPHA по НЕЛИНЕЙНЫМ байтам) даёт
#   ровно `C·op + F·(1−op)`.
#
#   ⇒ Материал ВОССТАНАВЛИВАЕТ прямой цвет: `C = SceneColor.rgb / op` (деление в ЛИНЕЙНОМ
#   пространстве — сэмплер отдаёт линейные значения), а множитель `op` обратно вносит уже сам
#   бленд Slate. Проверка по тем же трём плашкам: `sRGB(lin(66)/0.25) = 128.5`,
#   `sRGB(lin(92)/0.50) = 127.2`, `sRGB(lin(115)/0.80) = 127.3` — исходные 128 восстановлены
#   с точностью ≤ 1 байта.
#   На непрозрачных пикселях (`op = 1`) деление ничего не меняет — правка трогает ТОЛЬКО
#   полупрозрачное: тень и купол.
#
# ★★★ ГЛАВНАЯ ПРИЧИНА «БЛЕДНОЙ КАРТИНКИ» В ЖИВОЙ СЦЕНЕ — ДВОЙНАЯ ГАММА
#   (найдено 2026-08-18, подробно в `Reports/gacha_composite_final.md`).
#   Зонд снимал RT и считал композит ОФФЛАЙН — там всё было верно. Ломался ПОСЛЕДНИЙ шаг —
#   вывод через Slate:
#
#     1. RT создаётся как `RTF_RGBA8` (`AsdaGachaSceneActor.cpp::EnsureRenderTarget`), а `OverrideFormat`
#        остаётся `PF_Unknown` (`InitAutoFormat` его НЕ трогает, TextureRenderTarget2D.cpp:143-155).
#        ⇒ `UTextureRenderTarget2D::IsSRGB()` = (`RenderTargetFormat == RTF_RGBA8_SRGB`) = **false**
#        (TextureRenderTarget2D.cpp:72-87), и RHI-текстура создаётся **БЕЗ** флага `SRGB`
#        (там же :495 — `TexCreateFlags = Owner->IsSRGB() ? SRGB : None`).
#     2. При этом `bForceLinearGamma = false` ⇒ `GetDisplayGamma()` = 2.2 (там же :704-722), то есть
#        тонмаппер КЛАДЁТ в RT УЖЕ ГАММА-КОДИРОВАННЫЕ байты. Это подтверждено
#        калибровкой выше: линейный 0.21586 вернулся байтом 128 = sRGB(0.21586).
#     3. Сэмплер без флага `SRGB` НЕ декодирует ⇒ материал получает 0.502 и считает его линейным.
#     4. Slate перед выводом БЕЗУСЛОВНО делает `OutColor.rgb = GammaCorrect(OutColor.rgb)`
#        (`SlateElementPixelShader.usf:653`; `SOURCE_IN_LINEAR_SPACE` жёстко = 1 в `SlateShaderCommon.ush:33`),
#        а `GammaCorrect` = `pow(c, 2.2/DisplayGamma)` + `LinearToSrgb` (`SlateElementPixelShader.usf:87-98`,
#        `GammaCorrectionCommon.ush:16-53`). При штатной `DisplayGamma = 2.2` показатель = 1.0
#        (`SlateShaders.h:130-135`), остаётся чистый `LinearToSrgb`.
#
#   Итог: байт 128 из кадра выходил на экран как **188**; ядро тени (байт 27.7 при `op = 0.8`)
#   вместо эталонных ~63 давало ~120 — те самые «светло-серые тени и бледная картинка».
#
#   ⇒ Материал ОБЯЗАН отдать в Emissive ЛИНЕЙНОЕ значение, чтобы после `GammaCorrect` на экран
#   лёг РОВНО тот байт, что считал эталонный композит. Для этого добавлен узел `sRGBToLinear`
#   — ТОЧНАЯ обратная к `LinearToSrgbBranching` функция из `GammaCorrectionCommon.ush:55-59`.
#
# ★★★ ДЕЛЕНИЕ ИДЁТ В ТОМ ЖЕ ПРОСТРАНСТВЕ, ЧТО И У ЭТАЛОНА.
#   Эталон владельца (`Reports/gacha_composite_preview.png`) считается по БАЙТАМ кадра:
#       op = 1 − A/255;  direct = clip(RGB / max(op, 1/255), 0, 255);  out = direct·op + фон·(1−op)
#   то есть деление И смешивание — в ГАММА-ПРОСТРАНСТВЕ. Смешивание Slate тоже идёт по
#   ГАММА-БАЙТАМ (бленд стоит ПОСЛЕ `GammaCorrect`) — ровно как D3D9-бленд оригинала
#   (`CRenderer_ApplyBlendRenderState@0x9764F0`, SRCALPHA/INVSRCALPHA по НЕЛИНЕЙНЫМ байтам).
#   ⇒ чтобы живая сцена совпала с эталоном, деление тоже делается ПО СЫРЫМ ЗНАЧЕНИЯМ
#   сэмпла (они и есть байты кадра/255, см. п. 3 выше), а `sRGBToLinear` стоит ПОСЛЕ деления
#   и обрезки — только как компенсация шага Slate, ни для чего иного.
#
# ⚠ ГРАБЛЯ НА БУДУЩЕЕ: если RT переведут в `RTF_RGBA8_SRGB` или выставят `OverrideFormat`,
#   сэмплер начнёт декодировать сам, и узел `sRGBToLinear` станет ЛИШНИМ (картинка уйдёт
#   в темноту). Формат RT и этот узел — ОДНО целое.
#
# Домен материала — **User Interface** (иначе UMG-кисть его не примет), блендинг Translucent.
# Запуск: run_headless.bat asda_build_gacha_ui_material.py
import io
import os

import unreal

PKG = "/Game/Materials"
NAME = "M_AsdaGachaScene3D"
# Выгрузка графа текстом — единственный способ прочитать узлы из Python.
T3D_DUMP = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d/M_AsdaGachaScene3D.T3D"

# ★ Нижняя граница делителя: 1/255 — шаг самого 8-битного кадра. Меньше op в RT не бывает
#   (там уже 0), а деление на ноль дало бы NaN и чёрные точки по краю тени.
MIN_OPACITY = 1.0 / 255.0

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary


def log(m):
    unreal.log("[гача-материал] " + str(m))


def connect(frm, frm_out, to, to_in):
    """★ Закон ue-material-script-gotchas: connect МОЛЧА возвращает False при неверном имени входа."""
    ok = MEL.connect_material_expressions(frm, frm_out, to, to_in)
    if not ok:
        raise RuntimeError("не соединилось: %s[%s] -> %s[%s]" % (frm, frm_out, to, to_in))
    return ok


def connect_prop(frm, out, prop):
    ok = MEL.connect_material_property(frm, out, prop)
    if not ok:
        raise RuntimeError("не соединилось со свойством %s (выход '%s')" % (prop, out))
    return ok


def export_t3d(mat, out_path=T3D_DUMP):
    """Выгрузить граф материала в `.T3D` и вернуть его текст (None — если не вышло).

    ★ Зачем: свойства-списка выражений у `UMaterial` в Python нет, а граф проверять надо:
      сегодня дважды ловили расхождение «сборщик сказал OK / на диске старое»."""
    try:
        task = unreal.AssetExportTask()
        task.set_editor_property("object", mat)
        task.set_editor_property("filename", out_path)
        task.set_editor_property("automated", True)
        task.set_editor_property("prompt", False)
        task.set_editor_property("replace_identical", True)
        # ★ Экспортёр НЕ задаём: `UExporter::FindExporter` сам подберёт его по расширению
        #   `.T3D`. Класса `MaterialExporterT3D` в Python-обёртке НЕТ (проверено запуском:
        #   `module 'unreal' has no attribute 'MaterialExporterT3D'`).
        if not unreal.Exporter.run_asset_export_task(task):
            log("⚠ AssetExportTask вернул False")
            return None
    except Exception as e:
        log("⚠ экспорт T3D не удался: %s" % e)
        return None
    try:
        with io.open(out_path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except Exception as e:
        log("⚠ T3D не прочитался: %s" % e)
        return None


def main():
    path = "%s/%s" % (PKG, NAME)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)

    mat = ATOOLS.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    # Translucent = бленд оригинала (SRCALPHA/INVSRCALPHA). AlphaComposite здесь НЕЛЬЗЯ: он
    # ожидает предумноженный цвет в ГАММЕ, а RT предумножен в ЛИНЕЙНОМ — купол выходил бы
    # на 31 байт светлее оригинала (расчёт в `Reports/gacha_alpha_composite.md` §2).
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    # ЕДИНСТВЕННЫЙ вход: кадр `SCS_FinalColorLDR` — RGB (предумноженный) и A (обратная непрозр.).
    color = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, -120)
    color.set_editor_property("parameter_name", "SceneColor")
    color.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    # op = 1 − A  (соглашение UE: альфа = обратная непрозрачность)
    one_minus = MEL.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -460, 120)
    connect(color, "A", one_minus, "")

    # Делитель = max(op, 1/255): отмена предумножения без деления на ноль.
    min_op = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -460, 220)
    min_op.set_editor_property("r", MIN_OPACITY)
    safe_op = MEL.create_material_expression(mat, unreal.MaterialExpressionMax, -300, 160)
    connect(one_minus, "", safe_op, "A")
    connect(min_op, "", safe_op, "B")

    # C = RGB / op — ПРЯМОЙ (не предумноженный) цвет, как в текстуре оригинала.
    unpremul = MEL.create_material_expression(mat, unreal.MaterialExpressionDivide, -140, -60)
    connect(color, "RGB", unpremul, "A")
    connect(safe_op, "", unpremul, "B")
    # Ограничение 0..1: у оригинала цвет — байт текстуры, больше 1.0 он быть не мог, а
    # деление на маленький op из-за округления 8-битного кадра может выскочить за единицу.
    clamp = MEL.create_material_expression(mat, unreal.MaterialExpressionSaturate, 20, -60)
    connect(unpremul, "", clamp, "")

    # Дополнительная прозрачность всей сцены — на случай затуханий; по умолчанию 1 (без влияния).
    opacity_gain = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -300, 320)
    opacity_gain.set_editor_property("parameter_name", "OpacityGain")
    opacity_gain.set_editor_property("default_value", 1.0)

    mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, 20, 260)
    connect(one_minus, "", mul, "A")
    connect(opacity_gain, "", mul, "B")

    # ★★★ КОМПЕНСАЦИЯ `GammaCorrect` В SLATE (разбор — в шапке файла).
    #   Slate перед выводом применяет к Emissive `LinearToSrgb`. Сэмпл же вернул УЖЕ
    #   гамма-кодированные байты (RT без флага SRGB) ⇒ без этого узла гамма ложится ДВАЖДЫ.
    #   Формула — дословно `sRGBToLinear` из `GammaCorrectionCommon.ush:55-59`, то есть ТОЧНАЯ
    #   обратная к `LinearToSrgbBranching` (:23-33), включая линейный участок у нуля.
    #   Обычными узлами её точно не собрать: `If` в материалах СКАЛЯРНЫЙ, а выбор
    #   ветви нужен ПОКАНАЛЬНЫЙ — поэтому Custom-узел.
    to_lin = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, 200, -60)
    to_lin.set_editor_property("description", "sRGBToLinear")
    to_lin.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    to_lin.set_editor_property(
        "code",
        "float3 c = saturate(C);\n"
        "return lerp(c * (1.0 / 12.92),"
        " pow(max(c * (1.0 / 1.055) + 0.0521327, 0.0), 2.4),"
        " step(0.04045, c));")
    cin = unreal.CustomInput()
    cin.set_editor_property("input_name", "C")
    to_lin.set_editor_property("inputs", [cin])
    connect(clamp, "", to_lin, "C")

    connect_prop(to_lin, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    connect_prop(mul, "", unreal.MaterialProperty.MP_OPACITY)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    # ★ Закон normal-from-albedo-studio: ps = 0 ⇒ материал не скомпилировался (сэмплер/домен).
    st = MEL.get_statistics(mat)
    fields = []
    for k in ("num_pixel_shader_instructions", "num_vertex_shader_instructions",
              "num_samplers", "num_texture_samples", "num_virtual_texture_samples"):
        try:
            fields.append("%s=%s" % (k, st.get_editor_property(k)))
        except Exception:
            pass
    log("готов %s — %s" % (path, ", ".join(fields) if fields else "статистика недоступна"))

    # ★ Проверка ПО ДИСКУ, а не по тому, что собрано в памяти: перечитываем ассет заново.
    EAL.save_loaded_asset(mat)
    reloaded = EAL.load_asset(path)
    if reloaded is None:
        raise RuntimeError("материал не перечитался с диска")
    names = [str(n) for n in MEL.get_scalar_parameter_names(reloaded)]
    tex_names = [str(n) for n in MEL.get_texture_parameter_names(reloaded)]
    log("с диска: домен=%s бленд=%s скаляры=%s текстуры=%s"
        % (reloaded.get_editor_property("material_domain"),
           reloaded.get_editor_property("blend_mode"), names, tex_names))
    if "SceneMask" in tex_names:
        raise RuntimeError("на диске остался параметр SceneMask — старый материал не перезаписался")

    # ★ ГРАФ С ДИСКА, А НЕ ИЗ ПАМЯТИ: уже дважды ловили, что сборщик рапортует успех,
    #   а `.uasset` на диске старый (запись при открытом редакторе молча падает, `Error Code 32`).
    #   Считаем УЗЛЫ перечитанного ассета и требуем наличие Custom-узла sRGBToLinear.
    #   Граф не читается из Python напрямую (у `UMaterial` нет свойства-списка выражений,
    #   проверено: `expression_collection` / `expressions` НЕ отдаются), поэтому выгружаем
    #   его экспортом `.T3D` и читаем ТЕКСТОМ — вторым методом (закон false-negatives-method).
    t3d = export_t3d(reloaded)
    if t3d is None:
        raise RuntimeError("граф не выгрузился в T3D — проверить граф нечем")
    need_nodes = ("MaterialExpressionTextureSampleParameter2D", "MaterialExpressionOneMinus",
                  "MaterialExpressionMax", "MaterialExpressionDivide",
                  "MaterialExpressionSaturate", "MaterialExpressionCustom",
                  "MaterialExpressionMultiply", "MaterialExpressionScalarParameter")
    missing = [n for n in need_nodes if n not in t3d]
    if missing:
        raise RuntimeError("в T3D нет узлов: %s" % missing)
    if "0.0521327" not in t3d:
        raise RuntimeError("в T3D нет узла sRGBToLinear — компенсация гаммы Slate не записалась")
    log("T3D: %d байт, все узлы на месте, sRGBToLinear найден" % len(t3d))


if __name__ == "__main__":
    main()
