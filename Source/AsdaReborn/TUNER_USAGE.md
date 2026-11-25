# AsdaSkinTuneActor — живой тюнер скелетной анимации

Цель: вручную перебрать конвенцию скиннинга на двух тест-моделях и найти верную.
CPU-скиннинг каждый кадр в ProceduralMesh; **анимация идёт прямо в редакторе** (без Play), все контроллеры применяются мгновенно.

## Запуск
1. Если редактор был открыт во время сборки — **перезапусти редактор** (чтобы появился новый класс актора).
2. В пустом уровне: **Place Actors** → найди **"Asda Skin Tune Actor"** → перетащи в сцену.
3. По умолчанию грузится `scorpion.asdaraw`, модель сразу анимируется. Камеру наведи на актор (он в районе origin, масштаб ~метры).
4. Контроллеры — в **Details**, категории **Asda|...**. Меняй — результат виден сразу.

Данные: `RawAssets/Models/_AnimTest/raw/{opara,scorpion}.asdaraw` (экспорт `Tools/asda_export_raw.py`).

## Контроллеры
**Asda|Model**: `Model` (Opara/Scorpion), `RawDir` (путь к .asdaraw).

**Asda|Skel**:
- `SkelMode` — Flat (каждая кость = свой кейфрейм) / Hierarchical (композит с родителем).
- `QuatMat` — EngineExact (как `mathQuaternionToMatrix4x4`) / Standard (транспонированная).
- `ScaleMode` — ScaleRows S·R (эталон) / ScaleCols R·S.
- `bApplyConvert` — масштаб CONVERT на ротблок.
- `bConjugateQuat` — инверт xyz; `bNegateQuatW` — инверт w.
- `bHierParentFirst` — порядок композита (при Hierarchical).

**Asda|Bind**: `BindMode` — Identity (вершины bone-local) / InvRest (inverse-bind=inv(кадр0)). `bTwoBoneBlend`.

**Asda|Axes** (game LH Y-up → UE LH Z-up): `AxisPerm` (6 перестановок) + `bNegX/Y/Z` (= 48 базисов), `bReverseWinding`, `ExtraRotate`, `GlobalScale`.

**Asda|Anim**: `bPlay`, `bSlerp`, `TimeScale`, `bManualScrub`+`ManualTimeMs` (заморозить кадр).

**Asda|Debug**: `bDrawBones` (скелет линиями), readonly NumBones/NumVerts/DurationMs.

## Порядок перебора (рекомендация)
1. **Ориентация** (Asda|Axes): начни `AxisPerm=XZY` (Y↔Z), потом перебирай `bNegX/Y/Z`, при необходимости `bReverseWinding`. Цель — модель стоит правильно, не вывернута.
2. **Скелет**: `SkelMode` Flat↔Hierarchical — какой даёт связное тело без разлёта.
3. **Bind**: `BindMode` InvRest↔Identity.
4. **Quat**: `QuatMat` EngineExact↔Standard, `bConjugateQuat`, `bNegateQuatW` — устраняет «зеркальное» вращение костей.
5. **Масштаб блока**: `ScaleMode`, `bApplyConvert`.
6. Заморозь кадр (`bManualScrub`) для сравнения позы; включи `bPlay` для движения.

Найдём рабочую комбинацию → зашью её константами в `asda_skinned_to_gltf.py` (батч-экспорт всех моделей).
