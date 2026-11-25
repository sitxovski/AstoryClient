// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaProjectileActor — СНАРЯД дальнобойного оружия (стрела лука / болт арбалета / снаряд баллисты).
//
// ЭТАЛОН (реверс Reports/projectile_re.md):
//   Пул из 6 слотов живёт В САМОМ ПЕРСОНАЖЕ (charEffectSlots_Ctor@0xA583B0, слот 0xAA4 = 2724 б).
//   Модели грузятся один раз при создании персонажа (Projectile_LoadArrowModel@0xA58910):
//     ch1_n_00_arrs001_00_00_0.jbm — стрела НА ТЕТИВЕ (видна во время натяга, флаг slot[124]),
//     ch1_n_00_arrs001_00_00_1.jbm — стрела В ПОЛЁТЕ            (флаг slot[255]).
//   Выстрел (skillFireArrowProjectile@0xA52C00 / charFx_spawnFlyArrowProjectile@0xA52FB0):
//     старт   = трансляция матрицы (кость «8_b1» × мировая матрица персонажа);
//     цель    = центр цели (soundPos), ФИКСИРУЕТСЯ на момент выстрела — самонаведения НЕТ;
//     ось     = cBillboardInitFromAxis: строка0=norm(cross(up,dir)), строка1=norm(cross(dir,строка0)),
//               строка2=dir ⇒ меш авторен вдоль своего ИГРОВОГО +Z (bbox .jbm 0…73.73 по Z);
//               в нашем конвейере .jbm→glTF→UE эта ось лежит в UE-ЛОКАЛЬНОМ +Y (замер ассета) ⇒
//               доворот MeshAlignRot yaw −90°, масштаб MeshScale 0.01 (шкала ch1_*-ассетов);
//     полёт   = collisionTrace_Init(..., mode 2, speed 2.5f, dur = dist/2.5, ...) →
//               effectNodeMotionUpdate@0x11D05F0: disp = dir·speed·t, прилёт при t>=dur.
//               deltaTime движка = МИЛЛИСЕКУНДЫ (timerUpdateHighRes: qpc·1000/freq) ⇒
//               СКОРОСТЬ = 2.5 юнита/мс = 2500 uu/с. Ускорение (-3e-5) в mode 2 не используется.
//     шлейф   = eff_0_char_03_flyarr01-a_00.jeb, живёт в трансформе снаряда (self+411).
//   Прилёт (Combat_ProcessHitResultAndShowDamage@0xA53370): цифры урона, импакт
//     eff_0_char_99_battk03-a_00.jeb на позиции снаряда, звук по типу оружия — ВСЁ здесь,
//     а не на кадре замаха. Урон едет В СНАРЯДЕ (slot[337] = playerArrayPopFront на выстреле).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaProjectileActor.generated.h"

class UStaticMeshComponent;

UCLASS()
class ASDAREBORN_API AAsdaProjectileActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaProjectileActor();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ★ СКОРОСТЬ ЭТАЛОНА: 2.5 юнита/мс (collisionTrace_Init a14 = 0x40200000). Редактируется в UE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Снаряд")
	float SpeedUnitsPerMs = 2.5f;

	// ★ Доворот меша под ось полёта. Меш ch1_n_00_arrs001_00_00_1 авторен вдоль ИГРОВОГО +Z (bbox .jbm
	//   0…73.73 по Z), а конвейер .jbm→glTF→UE кладёт эту ось в UE-ЛОКАЛЬНЫЙ +Y (замер габаритов
	//   ассета: полу-габарит (177, 3755, 160) — длинная сторона по Y). «Вперёд» актора в UE — +X,
	//   поэтому доворот yaw −90° (+Y → +X). Редактируется в UE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Снаряд")
	FRotator MeshAlignRot = FRotator(0.f, -90.f, 0.f);

	// ★ Масштаб меша. Скелетал-конвейер персонажей ставит MeshRoot 0.01 (glTF-метры → см), и статик-меши
	//   ch1_* экспортированы в ТОЙ ЖЕ шкале (стрела 73.7 игровых юнита = 7510 в ассете). Снаряд —
	//   САМОСТОЯТЕЛЬНЫЙ актор (носителя со скейлом над ним нет) ⇒ 0.01 задаётся здесь.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Снаряд")
	float MeshScale = 0.01f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Снаряд")
	TObjectPtr<USceneComponent> RootScene = nullptr;

	// Меш стрелы в полёте (ch1_n_00_arrs001_00_00_1).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Снаряд")
	TObjectPtr<UStaticMeshComponent> ArrowMesh = nullptr;

	// Шлейф-эффект (BP_Fx_eff_0_char_03_flyarr01_a_00), прицеплен к снаряду; гасится в EndPlay.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Снаряд")
	TObjectPtr<AActor> TrailFx = nullptr;

	// Вызывается РОВНО ОДИН РАЗ в момент прилёта (порт: цифры урона + импакт + звук попадания).
	DECLARE_DELEGATE(FAsdaProjectileArrive);
	FAsdaProjectileArrive OnArrive;

	// Запуск по эталону: направление и длительность считаются один раз, цель не отслеживается.
	UFUNCTION(BlueprintCallable, Category="Asda|Снаряд")
	void Launch(const FVector& From, const FVector& To, float InSpeedUnitsPerMs = 2.5f);

	// ★ 05.11.2026 [ОРИГИНАЛ] ПОЛЁТ ДУГОЙ — узел `skillProjectile_createNode@0xA60590` → `effectBeamUpdate@0xA5F3F0` (граната 604/607):
	//   кривая Безье по трём точкам (`skillBuildBezierCurve@0xA5FF90`): старт (`From`), вершина = середина старт↔конец по плоскости и
	//   высота конец + `ApexUp` (эталон +750), конец — ТЕКУЩАЯ позиция цели (каждый кадр — самонаведение), если цель жива, иначе
	//   последняя известная. Доля пути = прошедшее / `DurationS` (эталон 1500 мс). Поворот модели не меняется (эталон ставит только
	//   трансляцию — `sub_91E032`). Прилёт — `OnArrive`, как у прямого полёта.
	void LaunchArc(const FVector& From, AActor* HomingTarget, const FVector& EndFallback, float DurationS, float ApexUp);

	// Длительность полёта по эталонной формуле (для синхронизации смерти цели и ритма замахов).
	static float FlightTimeSeconds(float DistanceUu, float InSpeedUnitsPerMs)
	{
		const float SpeedUuPerSec = FMath::Max(1.f, InSpeedUnitsPerMs * 1000.f);
		return DistanceUu / SpeedUuPerSec;
	}

	// ── Диагностика (asda.VerifyProjectile) ──
	UPROPERTY(BlueprintReadOnly, Category="Asda|Снаряд") FVector  StartPos = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Снаряд") FVector  EndPos   = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Снаряд") FVector  FlightDir = FVector::ForwardVector;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Снаряд") float    FlightTimeS = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Снаряд") float    ElapsedS = 0.f;

private:
	bool bArrived = false;
	bool bLaunched = false;
	bool bArc = false;                        // ★ 05.11: полёт дугой (`LaunchArc`)
	float ArcApexUp = 0.f;                    // подъём вершины над концом, юн
	TWeakObjectPtr<AActor> ArcTarget;         // цель самонаведения дуги
};
