// Copyright (c) 2026 @sitxovski (Discord)
// ★★ ОСТАНОВКА ХОДА НА ВРЕМЯ ДЕЙСТВИЯ UI С ОЖИДАНИЕМ (08.11.2026, пачка 19, баг 6: «нажатие „Создать" в окне крафта не останавливает
//   персонажа полностью, он продолжает двигаться»).
//
// РАЗБОР ЭТАЛОНА. «Стоп» из UI у оригинала — пара `CharFx_AutoSabzil_State(свой, …, 0)` (= `CCharacter_SetAction(…, 0)`: выход из
//   состояния хода 1, персонаж встаёт на месте) + `Net_SendActionPacket` (4008), плюс `followTargetCancelReset(&g_bMapTransferActive)`
//   (снятие автопути/следования). Из окон её зовут: вход в ремесло `tileInteractGateAndOpen@0xFDD1A0` (ui_core.c:100576-100584),
//   вход в сцену премиум-гачи `premiumGacha_BeginRoll` (world_map.c:4714), открытие элитного почтового ящика 69000
//   `uiBagWindow_OpenAndSync@0xEF1B30` (ui_core.c:92008-92019), подтверждения обмена и продажи. Кнопка «Создать» 77533
//   (`netSendItemCombineReq@0xFE5E30`, network.c:50079) ход не трогает; заточка, разборка, переплавка, извлечение, бустер — тоже.
//   Пока видны 76000/77500, оригинал гасит только клик по миру (`Input_WorldClickHandler`, input.c:144-157), клавиши хода
//   (`updatePlayerMovement`, верхний гейт math_geom.c:417) — нет: персонаж, зашагавший клавишами, шёл и во время крафта.
//
// ДЕФЕКТ ПОРТА [ОРИГИНАЛ]. Порт «стопа» в ремесле и гаче ставил только клип покоя и слал 4008, а сам ход павна (`bMoving`) не гасил
//   и автопуть не снимал: персонаж, шедший к точке, ехал дальше в позе покоя (это и видел владелец). Окно 69000 стопа не делало вовсе.
//   Теперь все три входа зовут `AsdaFullStopSelfForUiAction` — то же, что делает эталон.
//
// [ИСПРАВЛЕНИЕ] (ожидание владельца — полная остановка в момент начала крафта): на время действия с ожиданием ход своего персонажа
//   держится общей точкой остановки `AAsdaPlayerController::IsStunHardStopped` (клавиши, клик по земле и мини-карте, автопуть,
//   опоздавший грант 4007, верхом — то же; на фронте — стоп + 4008, автопуть снимается). Действия:
//     · создание снаряжения 77500 — от «Создать» (6602 ушёл) до конца анимации последней штуки (`IsAwaitingReply`/`IsAnimating`);
//     · заточка 20000 — от «Да»/клика по иконке до ответа 6505 и конца анимации исхода (`IsAttemptInProgress`);
//     · разборка 32000 — идёт разборка или 6502 в полёте (`IsDisassembling`/`IsInFlight`);
//     · переплавка (высшая очистка) — полоса идёт / окно занято (`DebugIsInProgress`/`DebugIsBusy`);
//     · извлечение — фаза машины, 5433 в полёте, применение ответа (`IsExtractInProgress`);
//     · бустер (открытие коробок 10050) — идёт процесс (`IsProcessRunning`);
//     · сцена премиум-гачи — открыта (`IsSceneOpen`; клавиши оригинал держит и так — верхний гейт).
//   Каждое состояние снимается само по ответу/концу анимации/закрытию окна (у всех систем свои страховки молчания сервера),
//   бессрочного замка нет. Ключ `asda.Move.UiActionStop` (1; 0 — прежнее поведение порта: ни полного стопа, ни удержания).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "UI/AsdaCraftMakeWindowSubsystem.h"
#include "UI/AsdaItemUpgradeSubsystem.h"
#include "UI/AsdaDisassemblySubsystem.h"
#include "UI/AsdaHighRefineSubsystem.h"
#include "UI/AsdaExtractShopSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaUiMoveHold, Log, All);

// Ключ исправления (разбор — шапка файла).
static ASDA_DEV_CVAR(int32, CVarAsdaMoveUiActionStop,
	TEXT("asda.Move.UiActionStop"), 1,
	TEXT("1 = вход в ремесло/гачу/почтовый ящик 69000 останавливает ход полностью (павн + автопуть + 4008, как оригинал), а на время ")
	TEXT("действия с ожиданием (создание снаряжения, заточка, разборка, переплавка, извлечение, бустер, сцена гачи) ход держится ")
	TEXT("общей точкой остановки [ИСПРАВЛЕНИЕ]; 0 = прежнее поведение порта."),
	ECVF_Default);

// Включено ли исправление (для вызывающих: вход в окна).
bool AsdaUiActionStopEnabled()
{
	return CVarAsdaMoveUiActionStop.GetValueOnGameThread() != 0;
}

// Какое действие UI с ожиданием сейчас идёт у своего персонажа (nullptr — никакое). Объявление у вызывающих — через extern.
const TCHAR* AsdaSelfUiActionInProgress(const UGameInstance* GI)
{
	if (!GI) { return nullptr; }
	if (const UAsdaCraftMakeWindowSubsystem* C = GI->GetSubsystem<UAsdaCraftMakeWindowSubsystem>())
	{
		if (C->IsAwaitingReply() || C->IsAnimating()) { return TEXT("создание снаряжения"); }
	}
	if (const UAsdaItemUpgradeSubsystem* U = GI->GetSubsystem<UAsdaItemUpgradeSubsystem>())
	{
		if (U->IsAttemptInProgress()) { return TEXT("заточка"); }
	}
	if (const UAsdaDisassemblySubsystem* D = GI->GetSubsystem<UAsdaDisassemblySubsystem>())
	{
		if (D->IsOpen() && (D->IsDisassembling() || D->IsInFlight())) { return TEXT("разборка"); }
	}
	if (const UAsdaHighRefineSubsystem* H = GI->GetSubsystem<UAsdaHighRefineSubsystem>())
	{
		if (H->IsOpen() && (H->DebugIsInProgress() || H->DebugIsBusy())) { return TEXT("переплавка"); }
	}
	if (const UAsdaExtractShopSubsystem* E = GI->GetSubsystem<UAsdaExtractShopSubsystem>())
	{
		if (E->IsExtractInProgress()) { return TEXT("извлечение"); }
	}
	if (const UAsdaBoosterPanelSubsystem* B = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>())
	{
		if (B->IsProcessRunning()) { return TEXT("бустер"); }
	}
	if (const UAsdaPremiumGachaSubsystem* G = GI->GetSubsystem<UAsdaPremiumGachaSubsystem>())
	{
		if (G->IsSceneOpen()) { return TEXT("сцена гачи"); }
	}
	return nullptr;
}

// Ход держится действием UI (общая точка — `AAsdaPlayerController::IsStunHardStopped`).
bool AsdaSelfMoveHeldForUiAction(const UGameInstance* GI)
{
	return AsdaUiActionStopEnabled() && AsdaSelfUiActionInProgress(GI) != nullptr;
}

// Полная остановка своего персонажа из клика UI (эталон: `followTargetCancelReset` + `CharFx_AutoSabzil_State(…, 0)` + 4008):
//   автопуть снят, маршрут/подход к NPC/навыку сняты, павн встал (локальное предсказание погашено), 4008 — если ход не закрыт.
//   Вызывать ТОЛЬКО из клика UI (не из сетевого колбэка). Ключ 0 — ничего не делает (вызывающие сохраняют прежнее поведение).
void AsdaFullStopSelfForUiAction(UGameInstance* GI, const TCHAR* Why)
{
	if (!GI || !AsdaUiActionStopEnabled()) { return; }
	bool bTravel = false;
	if (UAsdaMinimapSubsystem* Mini = GI->GetSubsystem<UAsdaMinimapSubsystem>())
	{
		bTravel = Mini->IsTravelRequested();
		if (bTravel) { Mini->CancelTravelRequest(); }
	}
	UWorld* W = GI->GetWorld();
	AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	const bool bWalking = Ch && Ch->IsMoving();
	if (PC)
	{
		PC->CancelPendingSkillApproach(Why);
		PC->ClearPendingNpcTalk(Why);
		PC->StopMoveAndNotify(Why);   // маршрут снят, павн встал, 4008 — если ход не закрыт (гейт `bActionPending`)
	}
	UE_LOG(LogAsdaUiMoveHold, Log, TEXT("[ход] полная остановка (%s): шёл %d, автопуть %s"),
		Why ? Why : TEXT(""), bWalking ? 1 : 0, bTravel ? TEXT("снят") : TEXT("не было"));
}
