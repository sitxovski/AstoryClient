// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ЕДИНЫЙ ПЕРЕВОД КООРДИНАТ ИГРА ↔ UE + ТАЙЛОВАЯ СЕТКА.
//   Аудит дублей №3 (`Reports/system_dedup_audit_3.md`, B1/B2/B9): формула свопа осей лежала
//   ОТКРЫТЫМ КОДОМ в 47 местах (`FVector2D(L.Y, -L.X)` / `FVector2D(-G.Y, G.X)`), центр тайла
//   `T * 100 + 50` — ещё в 15. Формула везде была верна, но одного промаха знака достаточно, чтобы
//   «моб пошёл не туда», а число 100 (размер тайла оригинала) жило без имени в 19 местах.
//
// ★ СИСТЕМА КООРДИНАТ ОРИГИНАЛА. Игра оперирует парой (gameX, gameZ) — это горизонталь; вертикаль
//   у неё gameY. UE: X — вперёд, Y — вправо, Z — вверх. Соответствие 1:1 с эталоном:
//        игра (x, y, z)  →  UE (−z, x, y)
//        UE   (X, Y, Z)  →  игра (Y, Z, −X)   [для горизонтали: gameX = UE.Y, gameZ = −UE.X]
//   Единица длины у игры и у UE одна и та же (1 юнит), масштаб НЕ применяется.
//
// Заголовок header-only (всё `FORCEINLINE`), ни .cpp, ни экспорта не требует.
#pragma once
#include "CoreMinimal.h"

namespace AsdaCoord
{
	// ── Константы сетки ──────────────────────────────────────────────────────────────────────────
	// Размер тайла оригинала в юнитах. Сетка карт .TER/.SIM и все серверные позиции считаются в них.
	inline constexpr float kTileUnits = 100.f;

	// Порог рассинхрона «серверная позиция против нашей», после которого актор снапится в серверную.
	//   Меньший разрыв догоняется движением: снап на каждый AoI-пакет давал «телепорты» (см. NetMove
	//   у моба и у игрока — оба используют ровно это число).
	inline constexpr float kDesyncSnapUnits = 150.f;

	// ── Горизонталь: UE → игра ───────────────────────────────────────────────────────────────────
	// gameX = UE.Y, gameZ = −UE.X. Принимает полную UE-локацию (высота отбрасывается).
	FORCEINLINE FVector2D UeToGame2D(const FVector& UeLoc)
	{
		return FVector2D(UeLoc.Y, -UeLoc.X);
	}

	// Тот же своп для уже плоской UE-пары (X, Y).
	FORCEINLINE FVector2D UeToGame2D(const FVector2D& UeXY)
	{
		return FVector2D(UeXY.Y, -UeXY.X);
	}

	// ── Горизонталь: игра → UE ───────────────────────────────────────────────────────────────────
	// UE.X = −gameZ, UE.Y = gameX. Высоту сюда НЕ подмешиваем: её даёт `AsdaGround::GroundZ`.
	FORCEINLINE FVector2D GameToUe2D(const FVector2D& Game)
	{
		return FVector2D(-Game.Y, Game.X);
	}

	// ── Полный трёхмерный своп (позиции эффектов, точки привязки из .JEB) ────────────────────────
	// игра (x, y, z) → UE (−z, x, y). Это ровно прежний `ToUeV` из `AsdaEffectActor.cpp`.
	FORCEINLINE FVector GameToUe3D(const FVector& Game)
	{
		return FVector(-Game.Z, Game.X, Game.Y);
	}

	// UE (X, Y, Z) → игра (Y, Z, −X) — обратное к `GameToUe3D`.
	FORCEINLINE FVector UeToGame3D(const FVector& Ue)
	{
		return FVector(Ue.Y, Ue.Z, -Ue.X);
	}

	// ── Направления ──────────────────────────────────────────────────────────────────────────────
	// Для направления своп тот же, что и для точки (преобразование линейное, без переноса), но имя
	//   отдельное: «повернуть вектор скорости» и «перенести позицию» путать нельзя.
	FORCEINLINE FVector2D GameDirToUe(const FVector2D& DirGame) { return GameToUe2D(DirGame); }
	FORCEINLINE FVector2D UeDirToGame(const FVector2D& DirUe)   { return UeToGame2D(DirUe);   }

	// Курс (yaw, градусы UE) для игрового направления. БЕЗ доворота модели: базовый `MeshYawOffset`
	//   у каждого носителя свой и прибавляется вызывающим.
	FORCEINLINE float FaceYawFromGameDir(const FVector2D& DirGame)
	{
		const FVector2D UeDir = GameDirToUe(DirGame);
		return FMath::RadiansToDegrees(FMath::Atan2(UeDir.Y, UeDir.X));
	}

	// ── Тайлы ────────────────────────────────────────────────────────────────────────────────────
	// Центр тайла в ИГРОВЫХ координатах: сервер адресует клетку, а идём мы в её середину.
	FORCEINLINE FVector2D TileCenterGame(int32 TileX, int32 TileZ)
	{
		return FVector2D(TileX * kTileUnits + kTileUnits * 0.5f, TileZ * kTileUnits + kTileUnits * 0.5f);
	}

	FORCEINLINE FVector2D TileCenterGame(const FIntPoint& Tile)
	{
		return TileCenterGame(Tile.X, Tile.Y);
	}

	// Игровая позиция → номер тайла (округление ВНИЗ, как в оригинале: деление целочисленное).
	FORCEINLINE FIntPoint GameToTile(const FVector2D& Game)
	{
		return FIntPoint(FMath::FloorToInt(Game.X / kTileUnits), FMath::FloorToInt(Game.Y / kTileUnits));
	}
}
