# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_charlit_soul_mat.py — TRANSLUCENT-ДВОЙНИК мастера персонажа для режима «душа» (SoulState==1).
#
# ЭТАЛОН (`Reports/soul_silhouette_visual_re.md` §2.2-§2.3, §8): при SoulState==1 персонаж
#   (тело + все надетые части) рисуется ОБЫЧНОЙ моделью через штатный translucent-проход:
#     ALPHABLENDENABLE=1, SRCBLEND=SRCALPHA, DESTBLEND=INVSRCALPHA, CULLMODE=CW (односторонний),
#     стадия 0 MODULATE(tex, diffuse), diffuse = (1,1,1, 0.3) ⇒ пиксель = tex.rgb, α = tex.a × 0.3.
#     Тинта/свечения/пульса НЕТ; освещение/туман — как у обычной модели; тень ПОЛНАЯ
#     (shadow-pass форсирует ALPHABLENDENABLE=0 — СРЕДНЯЯ уверенность).
#
# ПОЧЕМУ ОТДЕЛЬНЫЙ АССЕТ. Blend-mode у UE в рантайме не переключается: у Opaque-мастера
#   `M_AsdaCharLit` нет translucent-прохода вовсе. Поэтому мастер ДУБЛИРУЕТСЯ (со всем графом:
#   BaseColor=Tex·VertexColor, Roughness/Metallic, Specular=SpecBase+SpecAlphaBoost·Tex.A,
#   Emissive=BaseColor·AmbientFloor, тун-рим из asda_add_toon_rim.py — всё, что есть в живом
#   мастере на момент прогона), и ТОЛЬКО ПОВЕРХ него ставится translucent-режим и вход Opacity.
#   Так светотень «души» тождественна обычному персонажу (§8: «тот же мастер, только с Opacity»).
#   ⚠ Это КОПИЯ, а не инстанс: правка M_AsdaCharLit сюда сама не перетечёт — перегнать скрипт.
#
# ЧТО ДОБАВЛЯЕТСЯ ПОВЕРХ КОПИИ:
#   • blend_mode = TRANSLUCENT; translucency_lighting_mode = Surface ForwardShading (per-pixel) —
#     единственный режим, где translucent-поверхность освещается как Lit-меш, а не «объёмом»;
#   • two_sided = False — эталонный CULLMODE=CW (у Opaque-мастера two_sided=True, см. wire_char;
#     на translucent двусторонность рисовала бы внутренние стенки модели «рентгеном»).
#     Откат: env ASDA_SOUL_TWO_SIDED=1, если у части мешей вылезут дыры от развёрнутых граней;
#   • cast_dynamic_shadow_as_masked = True + Opacity через ShadowReplace(Default = Tex.A·SoulOpacity,
#     Shadow = 1.0): в shadow-depth translucent у UE НЕ ПИШЕТСЯ вовсе, а с этим флагом пишется как
#     masked по OpacityMaskClipValue; ветка Shadow=1.0 гарантированно выше порога ⇒ ТЕНЬ ПОЛНАЯ,
#     как у эталона (ALPHABLENDENABLE=0 на shadow-pass);
#   • параметр SoulOpacity (дефолт 0.3 = эталонная константа 0x3E99999A) — рантайм ставит его
#     в MID вместе с Tex (AAsdaPlayerCharacter::ApplySoulTranslucency).
#   Альфа берётся с СУЩЕСТВУЮЩЕГО сэмплера "Tex" скопированного графа (ищется по parameter_name);
#   второй сэмплер с тем же именем не заводим — ревизия (review_soul_translucent_world.md §1.2):
#   дубль имени параметра = битый по стандарту проекта ассет, контроль дублей его не пропускает.
#
# ★★★ КОНТРОЛЬ ГРАФА (asda_mat_graph.py; баг UE 5.8 DeleteAllMaterialExpressions, из-за которого
#   03.09.2026 копия унаследовала 9 фантомов мастера): ДО дублирования мастер обязан быть чистым
#   (ровно WIRE_CHAR_EXPRESSIONS узлов, дублей нет — иначе сперва asda_fix_charlit_lighting.py),
#   ПОСЛЕ сборки — ровно EXPECTED_EXPRESSIONS узлов и ни одного дубля, иначе RuntimeError и ассет
#   не сохраняется. Итог печатается ИЗ АССЕТА (кэш параметров, флаги, вход Opacity).
#
# ДОПУЩЕНИЕ (в отчёте): Z-запись. Эталон пишет глубину в translucent-проходе (ZWRITEENABLE=1);
#   у UE translucent глубину не пишет — перекрывающиеся части одной модели (перчатка поверх кисти)
#   смешиваются дважды. Ближайшее честное приближение — translucent с сортировкой по расстоянию
#   (эталон тоже QSort по dist²); «Masked+дизер» отвергнут как выдумка.
#
# Запуск (СКРЫТО, редактор закрыт, ПОСЛЕ asda_fix_charlit_lighting.py):
#   python Tools/run_headless_safe.py Tools/asda_build_charlit_soul_mat.py --marker SOUL-MAT-FINISHED
import os, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_mat_graph as MG                                        # контроль числа узлов/дублей
from asda_fix_charlit_lighting import WIRE_CHAR_EXPRESSIONS       # 11 узлов чистого мастера (единый источник)

mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
PKG = "/Game/Materials"
SRC = "%s/M_AsdaCharLit" % PKG
NAME = "M_AsdaCharLitSoul"
DST = "%s/%s" % (PKG, NAME)
SOUL_ALPHA = 0.3   # эталонная константа 1050253722 = 0x3E99999A = 0.3f (charEntityUpdateTick :80705)
# Узлы, добавляемые поверх копии мастера: ScalarParameter SoulOpacity, Multiply(Tex.A·SoulOpacity),
#   Constant 1.0, ShadowReplace = 4. Сэмплер Tex — существующий. При изменении main() — обновить.
SOUL_EXTRA_EXPRESSIONS = 4
EXPECTED_EXPRESSIONS = WIRE_CHAR_EXPRESSIONS + SOUL_EXTRA_EXPRESSIONS   # 64 + 4 = 68 (сет D; число живёт в asda_fix_charlit_lighting/asda_build_materials_lit, не хардкодить)


def log(msg): unreal.log_warning("[soul-mat] %s" % msg)


def E(m, cls, x, y):
    return mel.create_material_expression(m, cls, x, y)


def C(a, ao, b, bi):
    """Соединение с ПРОВЕРКОЙ: connect_material_expressions при неверном имени входа молча
    возвращает False (грабля [[ue-material-script-gotchas]] №2)."""
    ok = mel.connect_material_expressions(a, ao, b, bi)
    if not ok:
        raise RuntimeError("не соединилось: %s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))
    return ok


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    two_sided = os.environ.get("ASDA_SOUL_TWO_SIDED", "0") == "1"
    if not EAL.does_asset_exist(SRC):
        raise RuntimeError("нет мастера %s — нечего дублировать" % SRC)
    # ★ Мастер обязан быть чистым ДО дублирования (иначе копия унаследует фантомы) — проверяем
    #   раньше, чем удалять старую копию: при провале на диске ничего не меняется.
    src = EAL.load_asset(SRC)
    log("мастер %s: узлов %d, дубли %s" % (SRC, MG.expression_count(src), MG.parameter_duplicates(src) or "нет"))
    MG.check_graph(src, expected=WIRE_CHAR_EXPRESSIONS, log=log, tag="[soul-mat] мастер")
    if EAL.does_asset_exist(DST):
        if not EAL.delete_asset(DST):
            raise RuntimeError("не удалось удалить старый %s" % DST)
    dup = EAL.duplicate_asset(SRC, DST)
    if not dup:
        raise RuntimeError("duplicate_asset %s -> %s вернул None" % (SRC, DST))
    m = EAL.load_asset(DST)
    if not isinstance(m, unreal.Material):
        raise RuntimeError("%s не Material: %r" % (DST, m))
    # Существующий сэмплер "Tex" копии — ровно один (дубль = битый мастер, см. шапку).
    texs = [e for e in mel.get_material_expressions(m)
            if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D)
            and str(e.get_editor_property("parameter_name")) == "Tex"]
    if len(texs) != 1:
        raise RuntimeError("в копии мастера сэмплеров Tex: %d, ожидался 1" % len(texs))
    tex = texs[0]

    # ── translucent-режим поверх скопированного графа ──
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("translucency_lighting_mode",
                          unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    m.set_editor_property("two_sided", two_sided)                 # эталон: CULLMODE=CW
    m.set_editor_property("cast_dynamic_shadow_as_masked", True)  # тень полная (см. шапку)
    m.set_editor_property("used_with_skeletal_mesh", True)        # тело/броня = скелетал (наследуется, но явно)

    # ── Opacity = ShadowReplace(Default = Tex.A × SoulOpacity, Shadow = 1.0); Tex — существующий узел ──
    alpha = E(m, unreal.MaterialExpressionScalarParameter, -900, 1150)
    alpha.set_editor_property("parameter_name", "SoulOpacity")
    alpha.set_editor_property("default_value", SOUL_ALPHA)
    mul = E(m, unreal.MaterialExpressionMultiply, -600, 1000)
    C(tex, "A", mul, "A")
    C(alpha, "", mul, "B")
    one = E(m, unreal.MaterialExpressionConstant, -600, 1200)
    one.set_editor_property("r", 1.0)
    sw = E(m, unreal.MaterialExpressionShadowReplace, -350, 1050)
    C(mul, "", sw, "Default")
    C(one, "", sw, "Shadow")
    if not mel.connect_material_property(sw, "", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("не подключился вход Opacity")

    # ★ Контроль ДО сохранения: ровно EXPECTED_EXPRESSIONS узлов, дублей имён параметров нет.
    MG.check_graph(m, expected=EXPECTED_EXPRESSIONS, log=log, tag="[soul-mat]")
    mel.recompile_material(m)
    m.modify()
    ok = EAL.save_loaded_asset(m, False)
    if not ok:
        raise RuntimeError("save_loaded_asset вернул False")
    # ── Итог ИЗ АССЕТА (не константы скрипта) ──
    op = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_OPACITY)
    log("%s: узлов %d, дубли %s, save=%s" % (DST, MG.expression_count(m), MG.parameter_duplicates(m) or "нет", ok))
    log("свойства: blend=%s shading=%s tlm=%s two_sided=%s cast_dynamic_shadow_as_masked=%s used_with_skeletal_mesh=%s" % (
        m.get_editor_property("blend_mode"), m.get_editor_property("shading_model"),
        m.get_editor_property("translucency_lighting_mode"), m.get_editor_property("two_sided"),
        m.get_editor_property("cast_dynamic_shadow_as_masked"), m.get_editor_property("used_with_skeletal_mesh")))
    log("кэш: %s; текстуры=%s" % (
        ", ".join("%s=%r" % (k, mel.get_material_default_scalar_parameter_value(m, k))
                  for k in ("SoulOpacity", "AmbientFloor", "SpecBase", "Roughness", "Metallic", "SpecAlphaBoost")),
        [str(n) for n in mel.get_texture_parameter_names(m)]))
    log("Opacity <- %s" % (op.get_name() if op else "<не подключён>"))
    log("ЗАВЕРШЕНО (SOUL-MAT-FINISHED)")   # ASCII-маркер для run_headless_safe.py --marker SOUL-MAT-FINISHED


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
