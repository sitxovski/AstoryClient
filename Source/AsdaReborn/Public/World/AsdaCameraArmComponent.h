// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCameraArmComponent — стрела орбит-камеры своего персонажа (25.10.2026, волна camera). [ОРИГИНАЛ]
//
// ★ КАК ДЕЛАЕТ ОРИГИНАЛ (реверс сборки IDB 13 344 768 б, `RE/etalon/render_core.c:510`, дизасм 0x959880):
//   `cameraUpdate@0x959880` (режим 0, каждый кадр):
//     1) цель = позиция игрока + 60 (`a3 += 60`) + `g_fViewHeightOffset` (= 60.0, .data 0x15B5B08) → +120 над ногами;
//     2) глаз = цель − направление камеры × `curDist` (+0x3AC), без какой-либо проверки препятствий;
//     3) `terrainGetCellByPos` → `terrainGetCellByPos_A840@0x95A840` → `terrainInterpHeightAtXY@0x1130F90` — высота
//        ЛОГИЧЕСКОЙ сетки .TER под глазом (та же, что у ног персонажа), + `g_fViewHeightOffset` (60);
//     4) если `земля + 60 > глаз.y` — глаз поднимается на `земля + 60` и смотрит на цель (`cameraSetViewLookAtTarget`);
//        иначе кадр как есть. Подъём мгновенный, без сглаживания. Точка подъёма — пробная точка ПОСЛЕ зажима
//        `terrainGetCellByPos_A840` (X/Z вне 0..51100 → 100/51100): внутри карты это та же точка XZ, а за краем сетки
//        глаз у оригинала скачком переносится на край карты.
//        [ИСПРАВЛЕНИЕ] Порт этот скачок не повторяет: за краем сетки глаз поднимается в своей (незажатой) точке XZ —
//        высота берётся у края (зажим `UAsdaWalkGrid::GetGroundY`), положение глаза не прыгает.
//   Правило применяется только к стреле СВОЕГО (локально управляемого) персонажа: у соседей стрела не используется,
//   высоту для неё не считаем.
//   Столкновения камеры с объектами у оригинала НЕТ: `curDist` пишут только `camera_setupProjection` (800),
//   `Camera_MouseWheelZoom` (зум), `cameraUpdate` (рост к цели 0.3·dt, только вверх) и функция 0x95B700 (шаговый
//   луч по ячейкам карты с укорочением `curDist`), на которую НЕТ ни одной ссылки: ни call/jmp в .text (кроме
//   переходника 0x92F46D, у которого вызовов нет), ни указателя на неё или переходник во всём файле — в эталоне она
//   не исполняется. Игроки, мобы, NPC, деревья, здания и ящики камеру не двигают — она проходит сквозь них.
//
// ★ ЧТО БЫЛО В ПОРТЕ: штатный `USpringArmComponent` с `bDoCollisionTest = true` — сфера R=12 по каналу `ECC_Camera`
//   от точки +120 до глаза; первое блокирующее попадание укорачивало стрелу (вплоть до «камера в затылке»).
//   Блокируют `ECC_Camera` все статик-меши с профилем по умолчанию `BlockAllDynamic`: объекты карты (деревья, дома,
//   ящики) и статик-части сущностей (причёска `HairMesh` и оружие `WeaponMesh` чужих персонажей, статик-фолбэк мобов).
//
// ★ КЛЮЧ `asda.Camera.OriginalNoCollide` (дев-ключ, по умолчанию 1):
//   1 — [ОРИГИНАЛ] проверка препятствий выключена, глаз не опускается ниже `.TER + 60` (п. 3–4 выше);
//   0 — прежнее поведение порта (сфера по `ECC_Camera`, `bDoCollisionTest` компонента).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "AsdaCameraArmComponent.generated.h"

// Включён ли режим оригинала (ключ `asda.Camera.OriginalNoCollide` ≠ 0). Аксессор для чужих .cpp (правило Shipping:
//   имя дев-ключа в рабочем коде других файлов не ищем).
ASDAREBORN_API bool AsdaCameraOriginalNoCollideOn();

// Стрела орбит-камеры: штатная `USpringArmComponent` + правило камеры оригинала (см. шапку).
UCLASS(ClassGroup=Camera, meta=(BlueprintSpawnableComponent))
class ASDAREBORN_API UAsdaCameraArmComponent : public USpringArmComponent
{
	GENERATED_BODY()

public:
	// Конструктор: поведение штатной стрелы не меняется (длина/наклон/проверка задаёт владелец).
	UAsdaCameraArmComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Запас над землёй под глазом камеры (`g_fViewHeightOffset` = 60.0, сборка IDB .data 0x15B5B08).
	static constexpr float kGroundClearance = 60.f;

	// Поднят ли глаз над землёй в последнем кадре (диагностика и самотест `asda.Camera.SelfTestOcclusion`).
	bool WasGroundLiftedLastUpdate() const { return bGroundLifted; }
	// На сколько юнитов UE глаз поднят в последнем кадре (0 — подъёма не было).
	float GetLastGroundLift() const { return LastGroundLift; }
	// Высота земли (.TER) под глазом в последнем кадре режима оригинала; NaN — не считалась.
	float GetLastGroundZ() const { return LastGroundZ; }
	// Точка, вокруг которой вращается стрела (цель взгляда), в последнем кадре.
	FVector GetLastArmOrigin() const { return PreviousArmOrigin; }

protected:
	// Расчёт положения глаза: при ключе 1 — без проверки препятствий и с подъёмом над `.TER + 60` (оригинал),
	//   при ключе 0 — штатный расчёт `USpringArmComponent` без изменений.
	virtual void UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime) override;

private:
	bool bGroundLifted = false;   // глаз поднят над землёй в последнем кадре
	float LastGroundLift = 0.f;   // величина подъёма в последнем кадре (юниты UE)
	float LastGroundZ = NAN;      // высота .TER под глазом в последнем кадре (NaN — не считалась)
};
