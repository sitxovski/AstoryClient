// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaWalkGrid — сетка проходимости карты (порт клиентской walkable-сетки из <map>.TER, реверс
//   walkable_grid_re.md / CMap_LoadTER@0x1133580). Один ассет на карту: /Game/Map/<map>/DA_WalkGrid.
//   Editor-Friendly DataAsset — правится/просматривается в редакторе, игра ссылается по имени карты.
// Формат: сетка Width×Height тайлов (обычно 512×512), проходимость бит-паком (1 бит/тайл, 1=проходимо).
//   Источник — БАЙТ1 4-байтного флага тайла .TER; блок при флаге в {1,2,129,130,65,193} (эталон-загрузчик).
//   Мир↔тайл: tileX=worldX/100, tileZ=worldZ/100 (эталон worldX=tile*100+50).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AsdaWalkGrid.generated.h"

// ★ 28.10.2026 [ПОРТ] (плавание): ключ `asda.Swim.OpenWater` — открытая вода из `SwimBits` проходима (1, по умолчанию);
//   0 — проходимость как у эталона (вода — блок). Тело — AsdaPlayerCharacter_Move.cpp, рядом с ключами плавания.
ASDAREBORN_API bool AsdaSwimOpenWaterOn();
// ★ 04.11.2026 [ИСПРАВЛЕНИЕ] (портал Fireway Forest → Flabis): ключ `asda.Walk.ClosedTiles` — тайлы `ClosedBits` непроходимы
//   (1, по умолчанию); 0 — проходимость как у эталона (.TER). Тело — AsdaPlayerCharacter_Move.cpp, рядом с ключами плавания.
ASDAREBORN_API bool AsdaWalkClosedTilesOn();
// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 18, баг 9 «невидимые стены»): ключ `asda.Swim.WallGuard` — тайлы `SwimWallBits` непроходимы, хотя
//   маска `SwimBits` их открывает (1, по умолчанию); 0 — как до правки. Тело — AsdaPlayerCharacter_Move.cpp, рядом с ключами плавания.
ASDAREBORN_API bool AsdaSwimWallGuardOn();

UCLASS(BlueprintType)
class ASDAREBORN_API UAsdaWalkGrid : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// Размерность сетки в тайлах (512×512 у всех карт AsdaStory).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Walk") int32 Width = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Walk") int32 Height = 0;
	// Бит-пак проходимости: бит (tz*Width+tx) = 1 → тайл проходим. Размер = ceil(Width*Height/8).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Walk") TArray<uint8> Bits;
	// ★ БЕЗОПАСНАЯ (ГОРОДСКАЯ) ЗОНА — бит 0x80 ТОГО ЖЕ байта1 флага тайла .TER.
	//   Эталон: controlBar_SlotIsActive(g_pLocalPlayer, cellIdx) & 0x80 — по нему клиент (а) включает
	//   миникарту (item_inventory.c:50222), (б) играет городской BGM-поток 0 вместо полевого 1
	//   (game_main.c:3406/3821), (в) берёт «городской» вариант текстуры названия карты (npc_mob_pet.c:3723)
	//   и (г) ПРИНУДИТЕЛЬНО ставит БЕЗОРУЖНЫЙ набор анимаций ch4_{m|f}_00 для покоя и движения
	//   (CCharacter_LoadMotionAnims@0x9B84D0, ветки category 0 и 2).
	//   Проверка данных: alpen 21.8% тайлов с 0x80 (город), rein 0.3%, flabis 0% (чистое поле).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Walk") TArray<uint8> PeaceBits;

	// ── ★ МАТЕРИАЛ ПОВЕРХНОСТИ (байт0 того же флага .TER) ───────────────────────────────────────
	// От него зависят следы и звук шагов. Долго считался «мёртвым полем» — читателя не находили,
	// потому что он спрятан за тремя обёртками (`sub_A1A620` → `sub_A1A5E0` → thunk `sub_922687`).
	// На деле его дважды зовёт `CCharacter_ApplyMoveState@0xA199E0` и по значению выбирает:
	//     0 — облачко пыли `eff_1_char_99_move01-a_00` + звук `S_c_run1`
	//     1 — всплеск `eff_1_char_99_move02-a_00` + `S_c_run0`, но ТОЛЬКО если нога над водой
	//     2 — `S_c_run2`      3 — `S_c_run3`      4 — `S_c_run0`      прочее — `S_c_run1`
	// Один байт на тайл: значений всего пять, но храним как есть — так проще читать и сверять.
	// Пусто = данных нет, тогда шаги звучат как по земле (материал 0).
	// Распределение (ненулевых): alpen 8696, sunnycoast 4849, sillaris 3033, flabis 2026, flammio 6,
	// mine 3895 (значение 2). Разбор: `Reports/alpen_dancefloor_trigger.md` §5.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Walk") TArray<uint8> StepMaterials;

	// ── ★ ВОДА (геометрия `.WAT`, растеризованная по тайлам) ────────────────────────────────────
	// Нужна шагам: по водному тайлу персонаж роняет всплеск, а по мостику над той же водой — нет.
	// Эталон спрашивает саму геометрию: `sub_926A07` → `sub_B5E9B0` → `pickElems_testAnyHit`
	// перебирает элементы воды и зовёт `pickSpriteQuadRayTest`, который отсекает точку по
	// прямоугольнику X/Z элемента (сравнения высот там нет — важен горизонтальный охват).
	// Здесь тот же ответ заранее посчитан по тайлам: бит = хотя бы один водный треугольник
	// накрывает этот тайл. Заполняет `asda_export_walkgrid.py`, разбирая `.WAT` карты.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Walk") TArray<uint8> WaterBits;

	// ── ★ ОТКРЫТАЯ ВОДА (плавание, 28.10.2026, [ПОРТ]) ─────────────────────────────────────────────────────────────
	// В эталоне вода — блок (байт1 .TER = 1, как стены); у сервера — бит 0 `.sim`. Решение владельца: в воду можно войти и
	// плыть. Бит = тайл воды (или берега-спуска к ней), который открыт и на сервере (тот же список снят с бита 0 `.sim`).
	// Единый источник маски — `Tools/asda_water_swim.py` (`Tools/data/water_swim/<карта>.bits`), пишет её в ассет
	// `Tools/asda_export_swimbits.py`. Лава, объекты в воде, обрывы и дальняя вода не открыты (правила — в шапке генератора).
	// Пусто — открытой воды на карте нет. Проходимость читает `IsTileWalkable` при ключе `asda.Swim.OpenWater` = 1.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Walk") TArray<uint8> SwimBits;

	// ── ★ СТЕНА МАСКИ ПЛАВАНИЯ (08.11.2026, [ИСПРАВЛЕНИЕ], пачка 18, баг 9: «игроки проходят сквозь невидимые стены») ───────────
	// Бит = тайл маски `SwimBits`, до которого от проходимой суши `.TER` не дойти без стены: обрыв круче 230 ед. между соседними
	// тайлами (провал под башней в центре Альпена — земля .TER −1000, мостовая 864, вода 446; котловины под обрывами Санни-Кост)
	// или только через «берег» безопасной зоны (ровные блок-тайлы с битом 0x80 на уровне мостовой — кромка города Альпена,
	// Силалиса). Правило — `Tools/asda_water_swim.py::wall_tiles`, файл `Tools/data/water_swim/<карта>.walls.bits`, запись в ассет —
	// `Tools/asda_export_swimbits.py`. `SwimBits` не меняется: это список, снятый с бита 0 `.sim` живого сервера (сервер пускает
	// шире — расхождение безопасно: клиент сюда ход не шлёт). Пусто — стены нет. Читает `IsTileWalkable` при `asda.Swim.WallGuard` = 1.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Walk") TArray<uint8> SwimWallBits;

	// ── ★ ЗАКРЫТЫЕ ТАЙЛЫ (04.11.2026, [ИСПРАВЛЕНИЕ] дефектов данных оригинала) ─────────────────────────────────────────
	// Бит = 1 → тайл непроходим у клиента, хотя `.TER` его открывает. Два источника (генератор `Tools/asda_portal_pockets.py`,
	// данные `Tools/data/walk_closed/<карта>.json`, запись в ассет — `Tools/asda_export_closedbits.py`):
	//   • карман за зоной портала под геометрией объектов (Fireway Forest → Flabis: внутренность домика-портала, земля .TER на
	//     ~1000 ниже пола — персонаж уходил «под карту», если переход не сработал);
	//   • паритет с сервером: тайл открыт в `.TER`, но закрыт битом 0 `.sim` GameServer — сервер в своём шаге хода
	//     (`MoveSyncProcess@0x5427D0`) там встаёт, клиент шёл дальше, позиции расходились («CharStop Revision Pos»), и проверка
	//     зоны портала (её зовёт только шаг сервера) не срабатывала.
	// Пусто — закрытий на карте нет. Проходимость читает `IsTileWalkable` при ключе `asda.Walk.ClosedTiles` = 1.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Walk") TArray<uint8> ClosedBits;

	// ── ★ ВЫСОТА ЗЕМЛИ (логическая сетка .TER) ──────────────────────────────────────────────────
	// Оригинал берёт высоту ноги НЕ трейсом по рендер-мешу, а из ЛОГИЧЕСКОЙ сетки .TER:
	//   terrainGetCellByPos_A840 @0x95A840 → terrainInterpHeightAtXY @0x1130F90.
	// В .TER после массива флагов идёт массив 4 угловых высот на тайл (fread 16·N, memcpy в тайл+20 =
	//   dword[5..8]); порядок углов подтверждён совпадением общих рёбер соседних тайлов:
	//   [0]=(minX,maxZ) [1]=(minX,minZ) [2]=(maxX,minZ) [3]=(maxX,maxZ).
	// Углы соседних тайлов совпадают БИТ-В-БИТ на всех 22 картах (шов max = 0), поэтому храним
	//   компактную вершинную сетку (Width+1)×(Height+1): VertHeights[vz*(Width+1)+vx] = Y в (vx·Cell, vz·Cell).
	// Размер ячейки — this+0x08 в шапке .TER (у всех карт 100.0 игровых единиц = 100 см UE).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Height") float CellSize = 100.f;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Height") TArray<float> VertHeights;
	// Сводка (для глаз в редакторе; массив на 263169 чисел в Details не листают).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Height") float HeightMin = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Height") float HeightMax = 0.f;

	// Высота земли в МИРОВОЙ точке карты (game XZ) — 1:1 эталон terrainInterpHeightAtXY.
	// Квад тайла разбит диагональю (minX,minZ)—(maxX,maxZ); выбор треугольника — знак
	//   (−0.7071·x + 0.7071·z) − (−0.7071·cx + 0.7071·cz) ≤ 0  ⇔  (z−cz) ≤ (x−cx)  ⇔  v ≤ u.
	// Плоскость строится по 3 углам выбранного треугольника (Plane_FromThreePoints + sub_937852),
	//   что для осевого квада тождественно барицентрической форме ниже.
	UFUNCTION(BlueprintCallable, Category="Asda|Height")
	bool GetGroundY(float GameX, float GameZ, float& OutY) const
	{
		if (Width <= 0 || Height <= 0 || CellSize <= 0.f) { return false; }
		const int32 VW = Width + 1;
		if (VertHeights.Num() != VW * (Height + 1)) { return false; }
		// Зажим ДО индексации — ровно как эталон (x<0 → CellSize, x>(W−1)·Cell → (W−1)·Cell).
		const float MinC = CellSize;
		const float MaxC = (Width - 1) * CellSize;
		float X = GameX, Z = GameZ;
		if (X < 0.f) { X = MinC; }  if (X > MaxC) { X = MaxC; }
		if (Z < 0.f) { Z = MinC; }  if (Z > MaxC) { Z = MaxC; }
		const int32 Tx = FMath::Clamp((int32)(X / CellSize), 0, Width  - 1);
		const int32 Tz = FMath::Clamp((int32)(Z / CellSize), 0, Height - 1);
		const float H00 = VertHeights[Tz       * VW + Tx    ];   // (minX, minZ) — dword[6] тайла
		const float H10 = VertHeights[Tz       * VW + Tx + 1];   // (maxX, minZ) — dword[7]
		const float H01 = VertHeights[(Tz + 1) * VW + Tx    ];   // (minX, maxZ) — dword[5]
		const float H11 = VertHeights[(Tz + 1) * VW + Tx + 1];   // (maxX, maxZ) — dword[8]
		const float U = (X - Tx * CellSize) / CellSize;
		const float V = (Z - Tz * CellSize) / CellSize;
		OutY = (V <= U) ? (H00 + (H10 - H00) * U + (H11 - H10) * V)    // нижне-правый треугольник
		                : (H00 + (H01 - H00) * V + (H11 - H01) * U);   // верхне-левый треугольник
		return true;
	}

	// Проходим ли тайл (tx,tz). Вне сетки / границ 1..W-2 — НЕ проходим (эталон: валидны 1..510).
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	bool IsTileWalkable(int32 TileX, int32 TileZ) const
	{
		if (Width <= 0 || Height <= 0) { return true; }   // нет данных → не блокируем (fail-open)
		if (TileX < 1 || TileZ < 1 || TileX >= Width - 1 || TileZ >= Height - 1) { return false; }
		const int32 Idx = TileZ * Width + TileX;
		// ★ 04.11.2026 [ИСПРАВЛЕНИЕ]: закрытый тайл (карман за порталом / паритет с .sim сервера) — блок раньше всего прочего.
		if (IsTileClosed(TileX, TileZ) && AsdaWalkClosedTilesOn()) { return false; }
		if (!Bits.IsValidIndex(Idx >> 3)) { return true; }
		if ((Bits[Idx >> 3] >> (Idx & 7)) & 1) { return true; }
		// ★ 28.10.2026 [ПОРТ]: открытая вода (тот же список открыт на сервере).
		if (!(SwimBits.IsValidIndex(Idx >> 3) && ((SwimBits[Idx >> 3] >> (Idx & 7)) & 1) != 0 && AsdaSwimOpenWaterOn())) { return false; }
		// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 18, баг 9): тайл маски за стеной (обрыв / кромка города) — блок, как у эталона.
		return !(IsTileSwimWall(TileX, TileZ) && AsdaSwimWallGuardOn());
	}

	// Тайл закрыт (`ClosedBits`), независимо от ключа. Нет данных → false.
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	bool IsTileClosed(int32 TileX, int32 TileZ) const
	{
		if (Width <= 0 || Height <= 0 || ClosedBits.Num() == 0) { return false; }
		if (TileX < 0 || TileZ < 0 || TileX >= Width || TileZ >= Height) { return false; }
		const int32 Idx = TileZ * Width + TileX;
		return ClosedBits.IsValidIndex(Idx >> 3) && ((ClosedBits[Idx >> 3] >> (Idx & 7)) & 1) != 0;
	}

	// Тайл маски плавания за стеной (`SwimWallBits`), независимо от ключа. Нет данных → false.
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	bool IsTileSwimWall(int32 TileX, int32 TileZ) const
	{
		if (Width <= 0 || Height <= 0 || SwimWallBits.Num() == 0) { return false; }
		if (TileX < 0 || TileZ < 0 || TileX >= Width || TileZ >= Height) { return false; }
		const int32 Idx = TileZ * Width + TileX;
		return SwimWallBits.IsValidIndex(Idx >> 3) && ((SwimWallBits[Idx >> 3] >> (Idx & 7)) & 1) != 0;
	}

	// Тайл — открытая вода/берег (`SwimBits`), независимо от ключа. Нет данных → false.
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	bool IsTileSwimOpen(int32 TileX, int32 TileZ) const
	{
		if (Width <= 0 || Height <= 0 || SwimBits.Num() == 0) { return false; }
		if (TileX < 0 || TileZ < 0 || TileX >= Width || TileZ >= Height) { return false; }
		const int32 Idx = TileZ * Width + TileX;
		return SwimBits.IsValidIndex(Idx >> 3) && ((SwimBits[Idx >> 3] >> (Idx & 7)) & 1) != 0;
	}

	// Проходима ли МИРОВАЯ точка (game XZ): tile = floor(worldX/100).
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	bool IsWorldWalkable(float GameX, float GameZ) const
	{
		return IsTileWalkable((int32)FMath::FloorToInt(GameX / 100.f), (int32)FMath::FloorToInt(GameZ / 100.f));
	}

	// Тайл в безопасной (городской) зоне — бит 0x80 флага .TER. Нет данных → false (поле).
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	bool IsTilePeaceZone(int32 TileX, int32 TileZ) const
	{
		if (Width <= 0 || Height <= 0 || PeaceBits.Num() == 0) { return false; }
		if (TileX < 0 || TileZ < 0 || TileX >= Width || TileZ >= Height) { return false; }
		const int32 Idx = TileZ * Width + TileX;
		if (!PeaceBits.IsValidIndex(Idx >> 3)) { return false; }
		return ((PeaceBits[Idx >> 3] >> (Idx & 7)) & 1) != 0;
	}

	// Мировая точка (game XZ) в безопасной зоне.
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	bool IsWorldPeaceZone(float GameX, float GameZ) const
	{
		return IsTilePeaceZone((int32)FMath::FloorToInt(GameX / 100.f), (int32)FMath::FloorToInt(GameZ / 100.f));
	}

	// Материал поверхности тайла (0..4). Нет данных / вне сетки → 0 (земля).
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	int32 GetTileStepMaterial(int32 TileX, int32 TileZ) const
	{
		if (Width <= 0 || Height <= 0 || StepMaterials.Num() == 0) { return 0; }
		if (TileX < 0 || TileZ < 0 || TileX >= Width || TileZ >= Height) { return 0; }
		const int32 Idx = TileZ * Width + TileX;
		return StepMaterials.IsValidIndex(Idx) ? (int32)StepMaterials[Idx] : 0;
	}

	// Материал поверхности в МИРОВОЙ точке (game XZ).
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	int32 GetWorldStepMaterial(float GameX, float GameZ) const
	{
		return GetTileStepMaterial((int32)FMath::FloorToInt(GameX / 100.f), (int32)FMath::FloorToInt(GameZ / 100.f));
	}

	// Накрыт ли тайл водной геометрией. Нет данных → false (суша).
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	bool IsTileOverWater(int32 TileX, int32 TileZ) const
	{
		if (Width <= 0 || Height <= 0 || WaterBits.Num() == 0) { return false; }
		if (TileX < 0 || TileZ < 0 || TileX >= Width || TileZ >= Height) { return false; }
		const int32 Idx = TileZ * Width + TileX;
		if (!WaterBits.IsValidIndex(Idx >> 3)) { return false; }
		return ((WaterBits[Idx >> 3] >> (Idx & 7)) & 1) != 0;
	}

	// Накрыта ли водой МИРОВАЯ точка (game XZ).
	UFUNCTION(BlueprintCallable, Category="Asda|Walk")
	bool IsWorldOverWater(float GameX, float GameZ) const
	{
		return IsTileOverWater((int32)FMath::FloorToInt(GameX / 100.f), (int32)FMath::FloorToInt(GameZ / 100.f));
	}
};
