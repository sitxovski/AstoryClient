// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaVehicleRideSubsystem — ПАСХАЛКА «ТРАНСПОРТ»: пункты админ-меню спавнят машину на месте
//   персонажа и отдают ей управление. Чистая КОСМЕТИКА: сети нет, серверу ничего не уходит,
//   другие игроки транспорт не видят, персонаж на время поездки просто стоит на месте.
//
// ═══════════════════ ПОЧЕМУ ИМЕННО ТАК ═════════════════════════════════════════════════════════
//   • ФИЗИКА — ШТАТНАЯ система машин UE (Chaos Vehicles, плагин `ChaosVehiclesPlugin`). Своей
//     физики не пишем: берём готовый `Blueprint` из набора Vehicle Variety Pack, где подвеска,
//     колёса, двигатель и коробка уже настроены авторами набора.
//   • ВВОД — СВОЙ. Набор Epic рассчитан на Enhanced Input, а этот проект живёт на КЛАССИЧЕСКОМ
//     вводе (`AAsdaPlayerController::SetupInputComponent`, `BindAxisKey`), и включать ради
//     пасхалки вторую систему ввода — лишний риск для боевого управления персонажем. Поэтому
//     клавиши читаем сами (`APlayerController::IsInputKeyDown`) и кормим ими штатный
//     `UChaosVehicleMovementComponent`. Физика при этом остаётся полностью движковой.
//   • ПУНКТЫ МЕНЮ — через общую точку `UAsdaAdminSubsystem::RegisterFunction` (закон
//     `port-shared-points`): своего UI не заводим (закон `no-invented-ui`), «выбор тачек» — это
//     строки того же списка, по одной на машину набора.
//
// ═══════════════════ УПРАВЛЕНИЕ И ЖИЗНЕННЫЙ ЦИКЛ ═══════════════════════════════════════════════
//   `W`/`S` — газ вперёд/назад (на ходу `S` сначала ТОРМОЗИТ, как в жизни), `A`/`D` — руль,
//   `Пробел` — ручной тормоз, `Shift` — ускорение, **`F12` — вернуть управление персонажу**.
//   ★ ВАЖНО: возврат управления НЕ убирает транспорт — машина остаётся стоять в сцене
//   (требование владельца). Убирает её отдельный пункт меню «Деспаун транспорта», который
//   удаляет ВЕСЬ заспавненный транспорт разом.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/SubclassOf.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaVehicleRideSubsystem.generated.h"

class APawn;
class APlayerController;
class UChaosVehicleMovementComponent;

UCLASS()
class ASDAREBORN_API UAsdaVehicleRideSubsystem : public UGameInstanceSubsystem,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Фронты клавиш «слезть» и «фара» — состояние прошлой поездки.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Сесть в машину: заспавнить её на месте персонажа и передать управление.
	//   `ClassPath` — путь к Blueprint-классу машины набора.
	void EnterVehicle(APlayerController* PC, TSubclassOf<class AAsdaRideVehicle> VehicleClass);
	// Вернуть управление персонажу. ★ Транспорт при этом ОСТАЁТСЯ в сцене.
	void ReleaseControl();
	// Убрать ВЕСЬ заспавненный транспорт (отдельный пункт меню).
	void DespawnAll();

	// Сидим ли сейчас за рулём (управление у машины).
	bool IsRiding() const { return Vehicle != nullptr; }

	// Зовётся из тика игрока: читает клавиши и кормит ими штатный компонент движения машины.
	//   Здесь же ловится `F12` — возврат управления.
	void TickRide(APlayerController* PC, float DeltaSeconds);

private:
	// Машина, которой сейчас управляем, и её штатный компонент движения.
	UPROPERTY(Transient) TObjectPtr<APawn> Vehicle = nullptr;
	UPROPERTY(Transient) TObjectPtr<UChaosVehicleMovementComponent> Movement = nullptr;
	// Пешка игрока, в которую возвращаемся.
	UPROPERTY(Transient) TObjectPtr<APawn> PrevPawn = nullptr;
	// ★ ВЕСЬ заспавненный транспорт — он переживает возврат управления и убирается только
	//   пунктом «Деспаун транспорта».
	UPROPERTY(Transient) TArray<TObjectPtr<APawn>> Spawned;

	// F12 гасим по фронту, иначе одно нажатие сработало бы много кадров подряд.
	bool bReleaseKeyWasDown = false;
	// `L` — фары, тоже по фронту нажатия.
	bool bLightKeyWasDown = false;
	// Накопитель для диагностики раз в полсекунды.
	float DebugAccum = 0.f;
};
