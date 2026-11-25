# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_nan_bounds.py — УСТАРЕЛ, оставлен как псевдоним рабочего инструмента.
#
# Что здесь было и почему это НЕ работало (разобрано 2026-08-12):
#   • диагностика шла по `sm.get_bounds()` — это ExtendedBounds, а их UStaticMesh::CalculateExtendedBounds
#     (StaticMesh.cpp:7285) берёт из CachedMeshDescriptionBounds, а НЕ из FStaticMeshRenderData::Bounds.
#     Именно поэтому редактор всегда показывал корректные габариты, скрипт «не находил битых»,
#     а кук продолжал ругаться «FStaticMeshRenderData::Serialize found NaN in Bounds»;
#   • «лечение» вызывало `build_from_static_mesh_descriptions(descs)` с дефолтным fast_build=True —
#     то есть ровно то, что и создаёт дефект: взводит UStaticMesh::bDoFastBuild, после чего
#     FStaticMeshRenderData::Cache навсегда уходит в ветку без заполнения Bounds;
#   • голый `main()` в теле модуля нарушал закон «Tools/*.py без сайд-эффектов при импорте».
#
# Реальная причина и лечение — в asda_fix_fastbuild_bounds.py (там же полный разбор по исходникам
# движка). Этот файл просто передаёт управление туда, чтобы старые команды запуска не ломались.
# Переменные окружения — те же: ASDA_LIST / ASDA_ROOTS / ASDA_LIMIT / ASDA_VERBOSE.
import unreal
import asda_fix_fastbuild_bounds as _impl


def main():
    unreal.log("[fix_nan_bounds] скрипт устарел → выполняю asda_fix_fastbuild_bounds")
    _impl.main()


if __name__ == "__main__":
    main()
