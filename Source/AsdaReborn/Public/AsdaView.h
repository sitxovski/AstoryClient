// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ТОЧКА НАБЛЮДАТЕЛЯ — ЕДИНЫЙ ОТВЕТ «ОТКУДА СМОТРИТ ЗРИТЕЛЬ ЭТОГО МИРА» (аудит дублей 14.09.2026: E7 / K5 / С3). [ПОРТ]
//
//   ЗАЧЕМ. До 15.09.2026 точку наблюдателя считали шесть копий: эффект (`AAsdaEffectActor::GetViewLocation` — дистанц-кулл
//   и ориентация частиц), внутри того же эффекта ещё две (биллборд-квады 0xC и поворот камеры для частиц), купол неба
//   (`AAsdaSkyDomeActor::GetViewLocation`), свет эффектов (`UAsdaFxLightSubsystem::UpdateViewLocation`, гейт дальности)
//   и бюджет теней (`UAsdaLightBudgetSubsystem::PlayerPos`). Они расходились (дефект E7):
//     • эффект и купол в окне редактора брали ПЕРВЫЙ перспективный вьюпорт уровня БЕЗ проверки мира — для превью-сцены
//       это вьюпорт чужого мира; свет эффектов мир проверял. Комментарий света «ровно тот же выбор, что у эффекта»
//       был ложным;
//     • в игровом мире без контроллера (первые кадры PIE) эффект и купол падали на вьюпорт РЕДАКТОРА (другой мир,
//       другие координаты), свет — «наблюдателя нет»;
//     • биллборд-квады 0xC в редакторе смотрели на последний отрисованный вид, прочие частицы того же эффекта — на
//       вьюпорт уровня; поворот камеры в редакторе не брался вовсе (частицы «цилиндром на точку», в игре — плоскость экрана).
//
//   ОРИГИНАЛА ЗДЕСЬ НЕТ: у клиента 2008 года одна камера, редактора и превью-миров нет. Правило — порт.
//
//   ПРАВИЛО (один выбор для всех потребителей «мира»: кулл и частицы эффекта, следование купола, гейт света эффектов,
//   бюджет теней, значимость):
//     игровой мир (Game, PIE): камера первого локального игрока → его павн → последний отрисованный вид ЭТОГО мира;
//     прочие миры (окно редактора, превью-сцены): перспективный вьюпорт уровня, показывающий ЭТОТ мир →
//       последний отрисованный вид ЭТОГО мира.
//   «Последний отрисованный вид» — `UWorld::ViewLocationsRenderedLastFrame[0]`. Движок кладёт туда только виды, рисовавшие
//   именно этот мир: игровой вьюпорт (`UnrealClient.cpp:1780` из `GameViewportClient.cpp:1913`) и ПЕРСПЕКТИВНЫЕ вьюпорты
//   редактора, в том числе превью-сцен (`EditorEngine.cpp:2632`, мир = `FEditorViewportClient::GetWorld`). Сбрасывается в
//   конце `UWorld::Tick` (`LevelTick.cpp:2039`) ⇒ во время тика акторов там вид прошлого кадра. Без рендера (headless,
//   -nullrhi) массив пуст — «наблюдателя нет», как было у всех копий.
//   Поворот есть у камеры и у вьюпорта уровня; у павна и у отрисованного вида его нет (bHasRotation = false).
//   «Первый локальный игрок» — `UGameplayStatics::GetPlayerController(W, 0)` (прежний выбор эффекта, купола, бюджета теней
//   и значимости). Свет эффектов брал `UWorld::GetFirstPlayerController()` — в мире с ОДНИМ контроллером (клиент порта
//   сети UE не использует) это тот же объект.
//
//   ДРУГИЕ ТОЧКИ — НАМЕРЕННО НЕ ЗДЕСЬ (у них другое назначение, сводить нельзя):
//     • `APlayerController::GetPlayerViewPoint` — точка обзора КОНТРОЛЛЕРА: до первого обновления камеры это цель обзора,
//       а не кэш камеры. Нужна подкачке текстур перед снятием загрузочного экрана (`AsdaLoadManagerSubsystem`), старту
//       свободной камеры (`AsdaAdminSubsystem`), точке прилёта монет (`AsdaSoulGuardSubsystem`);
//     • слушатель звука (`AsdaSoundSubsystem::IsBeyondHearing`) — ухо, может стоять на игроке (asda.Sound.ListenerAtPlayer);
//     • проекция мир→экран (`ULocalPlayer::GetProjectionData`) — плашки, кадровый кэш `UAsdaVisibilitySubsystem`;
//     • камера переопределения превью-панели эффекта (`AAsdaEffectActor::bUseViewOverride`) — приоритетнее этого правила,
//       её ставит владелец панели; проверяется у эффекта ДО вызова сюда;
//     • камера логина (`AsdaLoginGameMode`) — сцена логина живёт своей камерой, код логина не переводился.
//   Полный инвентарь и последствия — `Reports/dedup_audit/wave3_asdaview.md`. Живая сверка — `asda.View.Dump`.
#pragma once
#include "CoreMinimal.h"

class UWorld;
class APlayerController;
class APlayerCameraManager;

// Откуда взята точка наблюдателя (для дампа и автотеста)
enum class EAsdaViewSource : uint8
{
	None,               // наблюдателя нет
	PlayerCamera,       // менеджер камеры первого локального игрока (игровой мир)
	PlayerPawn,         // павн того же игрока (менеджера камеры нет)
	LevelViewport,      // перспективный вьюпорт уровня, показывающий ЭТОТ мир (окно редактора)
	RenderedLastFrame,  // последний отрисованный вид ЭТОГО мира (UWorld::ViewLocationsRenderedLastFrame[0])
};

// Результат выбора точки наблюдателя
struct FAsdaViewPoint
{
	// Точка наблюдателя в мировых координатах UE
	FVector Location = FVector::ZeroVector;
	// Поворот взгляда (осмыслен только при bHasRotation)
	FRotator Rotation = FRotator::ZeroRotator;
	// Есть ли у источника поворот (камера, вьюпорт уровня — да; павн, отрисованный вид — нет)
	bool bHasRotation = false;
	// Источник точки
	EAsdaViewSource Source = EAsdaViewSource::None;
};

// Снимок состояния мира — вход ЧИСТОГО решения AsdaView::Resolve (его же собирает GetViewPoint из живого мира,
//   а автотест `AsdaReborn.View.Resolve` — из синтетических состояний)
struct FAsdaViewInputs
{
	// Вьюпорт уровня редактора, как его видит правило
	struct FLevelViewport
	{
		// Перспективный (ортогональные не годятся: их «позиция» — не глаз)
		bool bPerspective = false;
		// Показывает ли вьюпорт тот же мир, для которого ищем точку
		bool bSameWorld = false;
		// Позиция камеры вьюпорта
		FVector Location = FVector::ZeroVector;
		// Поворот камеры вьюпорта
		FRotator Rotation = FRotator::ZeroRotator;
	};

	// Мир есть вообще
	bool bWorld = false;
	// Игровой мир (Game/PIE) — UWorld::IsGameWorld
	bool bGameWorld = false;
	// Есть менеджер камеры первого локального игрока
	bool bHasCamera = false;
	// Позиция камеры (кэш кадра менеджера камеры)
	FVector CameraLocation = FVector::ZeroVector;
	// Поворот камеры (кэш кадра менеджера камеры)
	FRotator CameraRotation = FRotator::ZeroRotator;
	// Есть павн первого локального игрока
	bool bHasPawn = false;
	// Позиция павна
	FVector PawnLocation = FVector::ZeroVector;
	// Вьюпорты уровня редактора в порядке GEditor->GetLevelViewportClients() (для игрового мира не читаются)
	TArray<FLevelViewport, TInlineAllocator<4>> LevelViewports;
	// Отрисованные в прошлом кадре виды этого мира (UWorld::ViewLocationsRenderedLastFrame)
	TArrayView<const FVector> RenderedLastFrame;
};

namespace AsdaView
{
	// Контроллер первого локального игрока мира (`UGameplayStatics::GetPlayerController(W, 0)`); nullptr — нет.
	ASDAREBORN_API APlayerController* GetLocalPlayerController(const UWorld* W);

	// Менеджер камеры первого локального игрока (`UGameplayStatics::GetPlayerCameraManager(W, 0)`); nullptr — нет.
	//   Для потребителей, которым нужен сам объект камеры (значимость: владелец камеры, поворот, проекция).
	ASDAREBORN_API APlayerCameraManager* GetPlayerCameraManager(const UWorld* W);

	// ЧИСТОЕ решение правила из шапки над снимком состояния. Out сбрасывается всегда; false — наблюдателя нет.
	ASDAREBORN_API bool Resolve(const FAsdaViewInputs& In, FAsdaViewPoint& Out);

	// Точка наблюдателя мира W с поворотом и источником. Out сбрасывается всегда; false — наблюдателя нет.
	ASDAREBORN_API bool GetViewPoint(const UWorld* W, FAsdaViewPoint& Out);

	// Только позиция. При false Out НЕ трогается (прежнее поведение всех копий: вызывающий держит свой запасной вариант).
	ASDAREBORN_API bool GetViewLocation(const UWorld* W, FVector& Out);

	// Имя источника для логов
	ASDAREBORN_API const TCHAR* SourceName(EAsdaViewSource Source);
}
