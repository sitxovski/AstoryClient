// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerCharacter_Move_Private.h — ★ R3-c 12.11.2026 (разрез AsdaPlayerCharacter_Move.cpp): ЗАКРЫТЫЙ заголовок частей AsdaPlayerCharacter_Move*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaPlayerCharacter_Move.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "World/AsdaPlayerCharacter.h"
#include "Misc/App.h"   // FApp::GetDeltaTime — оценка кадра (точка отправки за кадр до дедлайна)
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaWorldSubsystem.h"   // LocalPlayerActor — «свой» персонаж (звук 2D против 3D)
#include "Engine/StaticMesh.h"                 // ★ 10.10.2026 (копание): меш инструмента на `8_a1`
#include "Engine/World.h"
#include "Npc/AsdaMobActor.h"              // ★ 18.10.2026: журнал боя — имя и rid цели разворота в замахе
#include "AsdaPlayerCharacterInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerCharacter*.cpp (LogAsdaPlayer, чистые хелперы)
#include "AsdaNetSubsystem.h"              // ★ 23.10.2026 (прыжок донора): 4024 код 131
#include "World/AsdaCrouchStand.h"         // ★ 05.11.2026: присед стоя — поза и 4024 132/133
#include "Framework/Application/SlateApplication.h"   // ★ … фокус в любом поле ввода
#include "Resources/AsdaGameUserSettings.h"           // ★ 25.10.2026 (присед): режим Ctrl «Нажатие/Зажатие» из окна настроек

// ★ 31.10.2026: свечение заточки как состояние кадра (тела — AsdaWorldSubsystem_Visuals.cpp, у ключа `asda.WeaponGlow.Persist`).
bool AsdaWeaponGlowPersistOn();                            // ключ постоянной модели свечения включён
void AsdaWeaponGlowReconcile(AAsdaPlayerCharacter* Ch);    // сверка свечения носителя с последним заказом
void AsdaPartFxTick(AAsdaPlayerCharacter* Ch);   // ★ 07.11.2026: сверка эффектов частей .PEB (AsdaPartFx.cpp)
void AsdaSowelGlowTick(AAsdaPlayerCharacter* Ch, float DeltaSeconds);   // ★ 05.11.2026: обводка самоцвета (AsdaSowelGlow.cpp)

// GAsdaCrouchResendPending: определение — в домашней части (R2).
extern TWeakObjectPtr<AAsdaPlayerCharacter> GAsdaCrouchResendPending;

// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (режим «Зажатие» и дребезг Ctrl): что сервер уже знает о статусе хода своего персонажа — статус и
//   время ПОСЛЕДНЕГО отправленного 4006 (любого: клик, клавиатура, досылка приседа). Пишет `AsdaCrouchNote4006Sent` из
//   `UAsdaNetSubsystem::SendMove4006`. По нему досылка приседа (а) не шлёт статус, который сервер уже получил (вкл→выкл быстрее
//   ответа — 0 пакетов), (б) идёт не чаще `kCrouchNetGapS`. Признак только своего персонажа — одна запись на файл.
struct FAsdaCrouchNetSent
{
	TWeakObjectPtr<const AAsdaPlayerCharacter> Who;   // чей 4006 (другой персонаж — после смены карты: статус неизвестен)
	int32 Status = -1;                                // статус @+3 последнего 4006 (−1 — не отправлялся)
	double TimeS = -1.0e9;                            // FPlatformTime::Seconds() отправки
};
// GAsdaCrouchNetSent: определение — в домашней части (R2).
extern FAsdaCrouchNetSent GAsdaCrouchNetSent;
// Минимальный промежуток между 4006, которые шлёт смена приседа (досылка по `asda.Crouch.NetCoalesce`), с.
static constexpr double kCrouchNetGapS = 0.25;

// Аксессоры ключей приседа (тела — у определений ключей ниже).
bool AsdaCrouchHoldModeOn();
bool AsdaCrouchNetCoalesceOn();
bool AsdaCrouchStrayGuardOn();        // ★ 08.11.2026 (пачка 15, баг 3): `asda.Crouch.StrayGuard` — защита от ложного переключения Ctrl
bool AsdaCrouchSlateEventsSince();    // ★ 08.11.2026: было ли с прошлого вызова нажатие (кроме Ctrl), увиденное Slate до виджетов

// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] Сообщить серверу текущий статус приседа своего персонажа, идущего «в точку», БЕЗ лишних пакетов:
//   статус совпал с последним отправленным 4006 — ничего (сервер уже знает; быстрое вкл→выкл не шлёт ничего); прежний 4006 в пути
//   или с прошлой отправки меньше `kCrouchNetGapS` — откладываем (досылка в `TickSwimAndJump`), иначе — 4006 из текущей точки к той
//   же цели (цель хода `LastSent4006TargetRid` сохраняется). Так при любом дребезге Ctrl — не больше 4 пакетов в секунду.
static void AsdaSyncCrouchStatusToServer(AAsdaPlayerCharacter* Me, UAsdaNetSubsystem* Net)
{
	if (!Me || !Net) { return; }
	const int32 Want = Me->GetMoveStatusFor4006();
	const bool bKnown = GAsdaCrouchNetSent.Who.Get() == Me && GAsdaCrouchNetSent.Status >= 0;
	if (bKnown && GAsdaCrouchNetSent.Status == Want) { GAsdaCrouchResendPending.Reset(); return; }
	const bool bGap = !bKnown || (FPlatformTime::Seconds() - GAsdaCrouchNetSent.TimeS) >= kCrouchNetGapS;
	if (Net->IsMoveRequestInFlight() || !bGap) { GAsdaCrouchResendPending = Me; return; }
	GAsdaCrouchResendPending.Reset();
	const FVector2D Cur = Me->GetGamePosXZ();
	const FVector2D Dst = Me->GetMoveDstGame();
	if (FVector2D::Distance(Cur, Dst) <= 1.f) { return; }
	Net->SendMove4006((float)Cur.X, (float)Cur.Y, (float)Dst.X, (float)Dst.Y, Me->LastSent4006TargetRid);
	UE_LOG(LogAsdaPlayer, Log, TEXT("[присед] %s: статус хода %d сообщён серверу (цель rid %d)"), *Me->GetName(), Want, Me->LastSent4006TargetRid);
}

// CVarAsdaCrouchStandRelay: определение — в домашней части (R2).
extern TAutoConsoleVariable<int32> CVarAsdaCrouchStandRelay;

namespace AsdaCrouchStand {
	// Отправка кодов приседа стоя своего персонажа (одна запись — свой персонаж один).
	struct FRelayState
	{
		TWeakObjectPtr<AAsdaPlayerCharacter> Who;   // чей код
		int32 SentCode = -1;                         // последний ушедший код (−1 — после хода ещё не слали)
		double SentS = -1.0;                         // время последней отправки, с
		int32 PendingCode = -1;                      // отложенный код (−1 — нет)
		double PendingS = -1.0;                      // когда код поставлен в очередь, с
		int32 SentCount = 0;                         // самотест: сколько ушло всего
	};
// GRelay: определение — в домашней части (R2).
extern FRelayState GRelay;

// Shown: определение — в домашней части (R2).
bool Shown(const AAsdaPlayerCharacter* Ch);
}
