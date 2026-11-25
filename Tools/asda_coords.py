# Copyright (c) 2026 @sitxovski (Discord)
# asda_coords.py — ЕДИНАЯ конверсия координат game ↔ UE для offline-инструментов.
#
# ЗАКОН (тот же, что у AsdaMapActor::ToUE в C++ и у всех расстановщиков):
#   game (x, y, z)  →  UE (-z, x, y)
#   обратно: gameX = UE.y, gameY = UE.z, gameZ = -UE.x
# Масштаб: игровая единица = 1 см UE. SCALE=100.0 применяют только тулы, работающие в ×100-сцене
#   (свет карты, анимация объектов, спавн NPC) — у расстановщиков эффектов/мобов/звуков масштаб 1.
#
# ЗАЧЕМ модуль: формула была записана инлайном в 10+ тулах (`def to_ue`, `def to_ue_v`, голые
#   `unreal.Vector(-z, x, y)`), а ОБРАТНАЯ — ровно в одном (asda_place_effects.from_ue_v). Правка знака
#   в одной копии ломала хиральность в остальных (закон effects-conversion-model).
#
# ★ ИСКЛЮЧЕНИЕ, которое НЕЛЬЗЯ сюда сводить: asda_build_backdrop_elements.py использует ДРУГУЮ
#   перестановку (x, -z, y) — она выведена эмпирически из сверки glb↔_OLD для задника логина
#   (★ 12.09.2026: множитель ×100 у неё СНЯТ — сцена логина переведена в ×1, как все карты; масштаб
#    там задаётся переменной ASDA_LOGIN_SCALE, умолч. 1.0)
#   (экспортёр негатит Z, а glTF-импорт UE меняет местами Y/Z). Это не копия и не ошибка.
#
# ★ Модуль БЕЗ побочных эффектов при импорте, но требует `unreal` (используется только из UE-скриптов).

import unreal

SCALE = 100.0            # игровая единица → см UE в ×100-сценах


def to_ue(x, y, z, s=1.0):
    """game (x, y, z) → unreal.Vector. s — масштаб сцены (1.0 или SCALE)."""
    return unreal.Vector(-z * s, x * s, y * s)


def to_ue_v(v, s=1.0):
    """То же от последовательности v=(x, y, z) (форма из asda_place_effects/npcs/charselect_glyph)."""
    return unreal.Vector(-v[2] * s, v[0] * s, v[1] * s)


def from_ue_v(u, s=1.0):
    """unreal.Vector → game [x, y, z]. Обратная к to_ue_v (была только в asda_place_effects.py)."""
    return [u.y / s, u.z / s, -u.x / s]


def matmul3(a, b):
    """Произведение 3×3 матриц, плоские списки по строкам (row-major, 9 чисел): o = a·b.
    Одна реализация для расстановщиков и сборщиков карт (дедуп №2 В1 Т19, 11.10.2026: было 6 дословных копий
    `matmul`/`mat_mul` — asda_add_uvscroll, asda_build_map_anim, asda_build_npc_spawn, asda_place_effects,
    asda_place_npcs, asda_build_map_level). Порядок сложения прежний — результат побитово тот же."""
    o = [0.0] * 9
    for i in range(3):
        for j in range(3):
            o[i*3+j] = a[i*3]*b[j] + a[i*3+1]*b[3+j] + a[i*3+2]*b[6+j]
    return o


def to_ue_mat_translation(m, s=1.0):
    """Трансляция из 4×4 игровой матрицы (row-major, m[12..14]) → unreal.Vector.

    Форма из asda_build_map_anim / asda_build_npc_spawn / asda_add_uvscroll.inst_loc.
    """
    return to_ue(m[12], m[13], m[14], s)
