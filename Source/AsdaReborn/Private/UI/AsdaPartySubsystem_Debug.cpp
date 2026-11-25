// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPartySubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaPartySubsystem.cpp, дословный перенос): щелчок по узлу, дампы, отладочное состояние, дев-команды GParty*.
//   Общее для частей — в AsdaPartySubsystem_Private.h; шапка и реверс — в AsdaPartySubsystem.cpp.
#include "UI/AsdaPartySubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"          // DecodeCp1251 — имя из тела пакета
#include "Resources/AsdaSoundSubsystem.h" // звук 215 при приходе заявки
#include "UI/AsdaHotbarSubsystem.h"       // иконка уведомления (statusBarAddIcon)
#include "UI/AsdaMiniStateSubsystem.h"  // ★ 30.10.2026: общая плашка подсказки (ShowSharedTip/HideSharedTip)
#include "UI/AsdaTipPanel.h"            // ★ 30.10.2026: EAsdaTipOwner::PartyHud
#include "UI/AsdaPlayerMenuSubsystem.h"  // ★ ОБЩАЯ точка следования StartFollow
#include "GameFramework/PlayerController.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"      // ★ волна 1-Б: отметки соотрядников (EAsdaMapMark::Party, §10 заголовка)
#include "World/AsdaLoadManagerSubsystem.h" // ★ волна 1-Б: моя карта (g_nCurMapId) — признак +272 «на моей карте»
#include "World/AsdaWorldKind.h"         // ★ 22.10.2026: экран загрузки/логин — панель там не восстанавливаем

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaCloseGear.h"   // ★ 08.11.2026: шестерёнка у крестика — общая точка (asda.UI.CloseGearHover)
#include "AsdaPartySubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPartySubsystem*.cpp

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════
void UAsdaPartySubsystem::ClickByNodeName(const TCHAR* Node)
{
	// ★ ЗАКОН: диагностика жмёт САМУ кнопку через `OnClicked`, а не зовёт обработчик.
	auto Hit = [](UUserWidget* Host, const TCHAR* N) -> UButton*
	{
		if (!Host || !Host->WidgetTree) { return nullptr; }
		TArray<UWidget*> All;
		Host->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			if (UButton* B = Cast<UButton>(W)) { if (B->GetName() == N) { return B; } }
		}
		return nullptr;
	};
	UButton* B = Hit(CreateWidget, Node);
	if (!B) { B = Hit(JoinWidget, Node); }
	if (!B) { B = Hit(RosterWidget, Node); }      // кнопки панели 2200 (2260/2262/свернуть)
	if (!B) { B = Hit(DisbandWidget, Node); }     // кнопки окна подтверждения 2256
	if (!B) { B = Hit(RowMenuWidget, Node); }     // пункты меню строки 8100
	if (!B)
	{
		UE_LOG(LogAsdaParty, Warning, TEXT("[отряд] кнопки «%s» в открытых окнах нет"), Node);
		return;
	}
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] жму кнопку «%s» её собственным OnClicked"), Node);
	B->OnClicked.Broadcast();
}

FString UAsdaPartySubsystem::DumpRoster() const
{
	// ★★★ ПОЧЕМУ ЭТОТ ДАМП ПЕРЕПИСАН (2026-08-29). Прежняя версия печатала МОИ СОБСТВЕННЫЕ поля
	//   (`Row.HpFullW`, `Row.HpBar.Shown`, свои bool-ы) — то есть «что я СОБИРАЛСЯ нарисовать».
	//   Поэтому три правки, не доехавшие до экрана, выглядели в прогоне выполненными.
	//   Теперь читаем ФАКТИЧЕСКОЕ состояние виджета: реальную ширину слота заливки и реальную
	//   `Visibility` каждого узла. Это единственное, что видит игрок.
	auto SlotW = [](const UWidget* W) -> float
	{
		const UCanvasPanelSlot* S = W ? AsdaUi::SlotOf(W) : nullptr;
		return S ? S->GetOffsets().Right : -1.f;
	};
	auto VisOf = [](const UWidget* W) -> const TCHAR*
	{
		if (!W) { return TEXT("НЕТ УЗЛА"); }
		return (W->GetVisibility() == ESlateVisibility::Collapsed) ? TEXT("нет") : TEXT("да");
	};

	FString S = TEXT("=== ПАНЕЛЬ ОТРЯДА 2200: ФАКТИЧЕСКОЕ состояние виджета ===\n");
	S += FString::Printf(TEXT("  общая цель (g_nPartySelIdx) = слот %d; выбран слот %d; распределение подсветок = %s\n"),
		SharedTargetSlot, SelectedSlot,
		CVarSwapRowHighlights.GetValueOnGameThread() ? TEXT("обменено") : TEXT("эталонное"));
	S += FString::Printf(TEXT("  открыта=%s готова=%s свёрнута=%s; в отряде=%s глава=%s участников=%d\n"),
		bRosterOpen ? TEXT("да") : TEXT("нет"), bRosterReady ? TEXT("да") : TEXT("нет"),
		bRosterCollapsed ? TEXT("да") : TEXT("нет"),
		bInParty ? TEXT("да") : TEXT("нет"), bIsLeader ? TEXT("да") : TEXT("нет"), Members.Num());
	S += FString::Printf(TEXT("  ФОН: Party_WinN_grp=%s Party_WinH_grp=%s\n"),
		VisOf(RosterNormal), VisOf(RosterHeader));

	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FAsdaPartyRowUi& R = Rows[i];
		FString Who = TEXT("—");
		if (Members.IsValidIndex(R.MemberIdx))
		{
			const FAsdaPartyMember& M = Members[R.MemberIdx];
			Who = FString::Printf(TEXT("«%s» charId=%d userNum=%d ур %d класс %d HP %d/%d"),
				*M.Name, M.CharId, M.UserNum, M.Level, M.ClassCode, M.Hp, M.MaxHp);
		}
		// ★ ШИРИНА ЗАЛИВКИ — ИЗ СЛОТА ВИДЖЕТА, а не из моего `HpFullW`.
		S += FString::Printf(TEXT("  слот %d: строка=%s | HP заливка %.0f из %.0f (доля %.2f) | MP %.0f из %.0f\n"),
			i, VisOf(R.Root), SlotW(R.HpFill), R.HpFullW, R.HpBar.Shown, SlotW(R.MpFill), R.MpFullW);
		S += FString::Printf(TEXT("           рамка(Select 183x28)=%s кинжал(TargetShare 29x14)=%s глава=%s | %s\n"),
			VisOf(R.Select), VisOf(R.TargetShare), VisOf(R.LeaderIcon), *Who);
	}
	return S;
}

FString UAsdaPartySubsystem::DumpAll() const
{
	FString S = TEXT("=== ОТРЯД: окна 2250 / 2245 и состояние ===\n");
	auto L = [&S](const TCHAR* Id, const UObject* W)
	{ S += FString::Printf(TEXT("  %-46s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	S += TEXT("-- PARTYTAKE_WIN 2250 --\n");
	L(TEXT("2250 окно"),                        CreateWidget);
	L(TEXT("2251 PARTYTAKE_TXT"),               CreateText);
	L(TEXT("2254 PARTYTAKE_CHOICE (список)"),   Combo);
	L(TEXT("2255 PARTYTAKE_DIS (правило)"),     CreateRule);
	L(TEXT("2252 PARTYTAKE_YES"),               CreateOk);
	L(TEXT("2252 подпись"),                     CreateOkText);
	L(TEXT("2253 PARTYTAKE_NO"),                CreateNo);
	L(TEXT("2253 подпись"),                     CreateNoText);
	L(TEXT("PartyTake_ChoiceBase_grp"),         ChoiceGroup);
	S += TEXT("-- PARTYBOX_WIN 2245 --\n");
	L(TEXT("2245 окно"),                        JoinWidget);
	L(TEXT("2246 PARTYBOX_TXT"),                JoinText);
	L(TEXT("2247 PARTYBOX_TIME_13_1 (заливка)"),JoinBarFill);
	L(TEXT("2248 PARTYBOX_YES"),                JoinOk);
	L(TEXT("2249 PARTYBOX_NO"),                 JoinNo);
	S += TEXT("-- PARTYDISPERSION 2256 (волна 1-Б) --\n");
	L(TEXT("2256 окно"),                        DisbandWidget);
	L(TEXT("2257 PARTYDISPERSION_MESSAGE"),     DisbandText);
	L(TEXT("2258 PARTYDISPERSION_OK"),          DisbandOk);
	L(TEXT("2258 подпись"),                     DisbandOkText);
	L(TEXT("2259 PARTYDISPERSION_CANCEL"),      DisbandCancel);
	L(TEXT("2259 подпись"),                     DisbandCancelText);
	S += FString::Printf(TEXT("-- состояние --\n  2250 открыто=%s готово=%s · 2245 открыто=%s готово=%s · 2256 открыто=%s готово=%s\n"),
		bCreateOpen ? TEXT("да") : TEXT("нет"), bCreateReady ? TEXT("да") : TEXT("нет"),
		bJoinOpen ? TEXT("да") : TEXT("нет"), bJoinReady ? TEXT("да") : TEXT("нет"),
		bDisbandOpen ? TEXT("да") : TEXT("нет"), bDisbandReady ? TEXT("да") : TEXT("нет"));
	S += FString::Printf(TEXT("  в отряде=%s глава=%s partyIndex=%d правило=%d (сырое %d) участников=%d\n"),
		bInParty ? TEXT("да") : TEXT("нет"), bIsLeader ? TEXT("да") : TEXT("нет"),
		PartyIndex, GetPartyLootRule(), LootRule, Members.Num());
	for (const FAsdaPartyMember& M : Members)
	{
		S += FString::Printf(TEXT("    userNum=%-6d charId=%-10d %s«%s» тайл=(%d,%d)%s карта=%d%s\n"),
			M.UserNum, M.CharId, M.bLeader ? TEXT("[глава] ") : TEXT(""), *M.Name,
			M.TileX, M.TileZ, M.bPosKnown ? TEXT("") : TEXT("[нет данных]"), M.MapId, M.bOnMyMap ? TEXT(" (моя)") : TEXT(""));
	}
	S += FString::Printf(TEXT("  отметок отряда отдано мини-карте: %d\n"), PushedMarkCount);
	S += FString::Printf(TEXT("  моя заявка в полёте=%s (%.1f с из %.0f) · цель окна userNum=%d «%s»\n"),
		bMyRequestActive ? TEXT("да") : TEXT("нет"),
		bMyRequestActive ? (FPlatformTime::Seconds() - MyRequestStart) : 0.0, kMyRequestTimeoutSec,
		InviteTargetUserNum, *InviteTargetName);
	S += FString::Printf(TEXT("  входящая=%s тип=%s reqId=%d от «%s» (userNum=%d charId=%d) правило=%d, идёт %.1f с из %.0f\n"),
		bIncomingPending ? TEXT("да") : TEXT("нет"),
		bIncomingIsNew ? TEXT("новый отряд 5091") : (bIncomingIsJoin ? TEXT("вступление 5095") : TEXT("—")),
		IncomingReqId, *IncomingName, IncomingUserNum, IncomingCharId, IncomingLootRule,
		bIncomingPending ? (FPlatformTime::Seconds() - IncomingStart) : 0.0, kIncomingTimeoutSec);
	S += FString::Printf(TEXT("  выбор списка 2254 = %d (%s)\n"), GetLootRuleChoice(),
		GetLootRuleChoice() == 1 ? TEXT("автораспределение") : TEXT("кто успел"));
	S += FString::Printf(TEXT("  счётчики: открытий=%d заявок отправлено=%d ответов отправлено=%d "
	                          "заявок принято=%d таймаутов=%d\n"),
		CntOpened, CntSentReq, CntSentAns, CntIncoming, CntTimeout);
	S += FString::Printf(TEXT("  ключи отката: FixContextMenuInvite=%d ShowLootRuleForLeader=%d "
	                          "TrackLootRule=%d AutoOpenJoinBox=%d FixDisbandConfirm=%d MapMarkNeedPos=%d\n"),
		CVarFixContextMenuInvite.GetValueOnGameThread(),
		CVarShowLootRuleForLeader.GetValueOnGameThread(),
		CVarTrackLootRule.GetValueOnGameThread(),
		CVarAutoOpenJoinBox.GetValueOnGameThread(),
		CVarFixDisbandConfirm.GetValueOnGameThread(),
		CVarMapMarkNeedPos.GetValueOnGameThread());
	return S;
}

namespace {
// ═════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (живая проверка) ════════════════════════════
	UAsdaPartySubsystem* PartySub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyDump(
	TEXT("asda.Party.Dump"),
	TEXT("asda.Party.Dump — полный структурный дамп окон 2250/2245 и всего состояния отряда"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { UE_LOG(LogAsdaParty, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaParty, Log, TEXT("\n%s"), *S->DumpAll());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyInvite(
	TEXT("asda.Party.Invite"),
	TEXT("asda.Party.Invite <userNum> [имя] [1=пропустить гейт «цель в реестре»] — пройти ветку 0x7D1"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { UE_LOG(LogAsdaParty, Warning, TEXT("подсистема недоступна")); return; }
		const int32 U = A.Num() > 0 ? FCString::Atoi(*A[0]) : -1;
		const FString N = A.Num() > 1 ? A[1] : FString(TEXT("?"));
		// Третий аргумент = 1: пропустить гейт «цель есть в реестре соседей». Нужен для прогона
		//   БЕЗ живого сервера — соседей там нет вовсе, и проверка честно всё отбивает.
		const bool bSkip = (A.Num() > 2) && (FCString::Atoi(*A[2]) != 0);
		UE_LOG(LogAsdaParty, Log, TEXT("окно 2250 открыто: %s"),
			S->OpenInviteWindow(U, N, bSkip) ? TEXT("да") : TEXT("НЕТ (см. нотис/лог выше)"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyRule(
	TEXT("asda.Party.Rule"),
	TEXT("asda.Party.Rule <0|1> — выбрать пункт списка 2254 (0 «кто успел», 1 «автораспределение»)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { return; }
		S->SetLootRuleChoice(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0);
		UE_LOG(LogAsdaParty, Log, TEXT("правило дележа в списке = %d"), S->GetLootRuleChoice());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyClick(
	TEXT("asda.Party.Click"),
	TEXT("asda.Party.Click <PARTYTAKE_YES|PARTYTAKE_NO|PARTYBOX_YES|PARTYBOX_NO|PARTYDISPERSION_OK|PARTYDISPERSION_CANCEL> — нажать САМУ кнопку"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { return; }
		S->ClickByNodeName(A.Num() > 0 ? *A[0] : TEXT("PARTYTAKE_YES"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyFakeRequest(
	TEXT("asda.Party.FakeRequest"),
	TEXT("asda.Party.FakeRequest <5091|5095> <код> <правило 0|1> <reqId> <userNum> <charId> <имя>\n")
	TEXT("  Собирает тело 30 б РОВНО по раскладке сервера (@0 код · @1 i16 userNum · @3 i32 charId ·\n")
	TEXT("  @7 char[20] имя · @27 u8 правило · @28 i16 reqId) и кладёт его в очередь приёма.\n")
	TEXT("  Разбирает его тот же код, что и живой пакет, — проверяется НАСТОЯЩИЙ приёмник."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 5091/5095 — боевой приёмник сразу или автоотказом через 30 с шлёт 5092/5096 с ПОДДЕЛЬНЫМ reqId.
		static const uint16 kGuardOps[] = { 5092, 5096 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Party.FakeRequest"), 35.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { UE_LOG(LogAsdaParty, Warning, TEXT("подсистема недоступна")); return; }
		const int32 Op   = A.Num() > 0 ? FCString::Atoi(*A[0]) : 5091;
		const int32 Code = A.Num() > 1 ? FCString::Atoi(*A[1]) : 0;
		const int32 Rule = A.Num() > 2 ? FCString::Atoi(*A[2]) : 0;
		const int32 Rid  = A.Num() > 3 ? FCString::Atoi(*A[3]) : 42;
		const int32 Unum = A.Num() > 4 ? FCString::Atoi(*A[4]) : 7;
		const int32 Cid  = A.Num() > 5 ? FCString::Atoi(*A[5]) : 1234;
		const FString Nm = A.Num() > 6 ? A[6] : FString(TEXT("TestLeader"));
		TArray<uint8> B; B.SetNumZeroed(30);
		B[0] = (uint8)Code;
		AsdaBytes::WrI16(&B[1], Unum);
		AsdaBytes::WrI32(&B[3], Cid);
		FAsdaLoginData::EncodeCp1251(Nm, &B[7], 19);
		B[27] = (uint8)(Rule != 0 ? 1 : 0);
		AsdaBytes::WrI16(&B[28], Rid);
		S->InjectPacket(Op, B);
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] в очередь положено тело %d: код=%d userNum=%d charId=%d «%s» правило=%d reqId=%d"),
			Op, Code, Unum, Cid, *Nm, Rule, Rid);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyFakeResult(
	TEXT("asda.Party.FakeResult"),
	TEXT("asda.Party.FakeResult 5093 <код> <reqUserNum> <reqCharId> <resUserNum> <resCharId> <partyIndex>\n")
	TEXT("  Тело 15 б по раскладке `recvResponseUserNewPartyCreate` — проверка ветки «отряд создан»."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный результат ставит bInParty/PartyIndex — 5098/5094/5100 ушли бы с поддельным индексом.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Party.FakeResult"))) { return; }
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { return; }
		const int32 Op = A.Num() > 0 ? FCString::Atoi(*A[0]) : 5093;
		TArray<uint8> B; B.SetNumZeroed(15);
		B[0] = (uint8)(A.Num() > 1 ? FCString::Atoi(*A[1]) : 1);
		AsdaBytes::WrI16(&B[1],  A.Num() > 2 ? FCString::Atoi(*A[2]) : 5);
		AsdaBytes::WrI32(&B[3],  A.Num() > 3 ? FCString::Atoi(*A[3]) : 1000);
		AsdaBytes::WrI16(&B[7],  A.Num() > 4 ? FCString::Atoi(*A[4]) : 7);
		AsdaBytes::WrI32(&B[9],  A.Num() > 5 ? FCString::Atoi(*A[5]) : 2000);
		AsdaBytes::WrI16(&B[13], A.Num() > 6 ? FCString::Atoi(*A[6]) : 42);
		S->InjectPacket(Op, B);
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] в очередь положено тело %d (15 б)"), Op);
	}));
#endif // ASDA_DEV_CONSOLE

void UAsdaPartySubsystem::DebugSetPartyState(bool bIn, bool bLeader, int32 Index, int32 Rule)
{
	bInParty = bIn;
	bIsLeader = bLeader;
	PartyIndex = Index;
	LootRule = Rule;
	UE_LOG(LogAsdaParty, Warning,
		TEXT("[отряд] ДИАГНОСТИКА: состояние выставлено напрямую — в отряде=%s глава=%s "
		     "partyIndex=%d правило=%d (в игре так не бывает, только для прогона гейтов)"),
		bIn ? TEXT("да") : TEXT("нет"), bLeader ? TEXT("да") : TEXT("нет"), Index, Rule);
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyRoster(
	TEXT("asda.Party.Roster"),
	TEXT("asda.Party.Roster <0|1> — показать/спрятать панель отряда 2200"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { return; }
		S->SetRosterVisible(A.Num() == 0 || FCString::Atoi(*A[0]) != 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyFakeRoster(
	TEXT("asda.Party.FakeRoster"),
	TEXT("asda.Party.FakeRoster <мойCharId> <charId1> <charId2> … — тело 5107 (144 б) в очередь.\n")
	TEXT("  Слот 0 = ГЛАВА. Имена подставляются как Member1…N. Пустые слоты = −1 (ловушка проекта)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный состав — бан/выход ушли бы с поддельными charId.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Party.FakeRoster"))) { return; }
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { return; }
		TArray<uint8> B; B.SetNumZeroed(144);
		for (int32 i = 0; i < 6; ++i) { AsdaBytes::WrI32(&B[4 * i], -1); }   // пустой слот = −1
		const int32 N = FMath::Min(A.Num(), 6);
		for (int32 i = 0; i < N; ++i)
		{
			AsdaBytes::WrI32(&B[4 * i], FCString::Atoi(*A[i]));
			FAsdaLoginData::EncodeCp1251(FString::Printf(TEXT("Member%d"), i), &B[24 + 20 * i], 19);
		}
		S->InjectPacket(5107, B);
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] в очередь положено тело 5107 (144 б), заполнено слотов %d"), N);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyFakeParam(
	TEXT("asda.Party.FakeParam"),
	TEXT("asda.Party.FakeParam <charId> <уровень> <curHP> <maxHP> <curMP> <maxMP> [класс] — тело 5109"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельные HP/MP членов отряда в живой сессии.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Party.FakeParam"))) { return; }
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { return; }
		TArray<uint8> B; B.SetNumZeroed(24);
		AsdaBytes::WrI32(&B[0],  A.Num() > 0 ? FCString::Atoi(*A[0]) : 0);
		B[4] = (uint8)(A.Num() > 1 ? FCString::Atoi(*A[1]) : 1);
		B[5] = (uint8)(A.Num() > 6 ? FCString::Atoi(*A[6]) : 1);   // класс: 1..4 меч, 12..15 лук, 23..26 посох
		AsdaBytes::WrI32(&B[7],  A.Num() > 3 ? FCString::Atoi(*A[3]) : 100);   // maxHP @7
		AsdaBytes::WrI32(&B[11], A.Num() > 2 ? FCString::Atoi(*A[2]) : 50);    // curHP @11
		AsdaBytes::WrI16(&B[15], A.Num() > 5 ? FCString::Atoi(*A[5]) : 60);    // maxMP @15
		AsdaBytes::WrI16(&B[17], A.Num() > 4 ? FCString::Atoi(*A[4]) : 30);    // curMP @17
		S->InjectPacket(5109, B);
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] в очередь положено тело 5109 (19 б)"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyClickRow(
	TEXT("asda.Party.ClickRow"),
	TEXT("asda.Party.ClickRow <слот 0..5> — ЛКМ по строке: выбрать участника целью + обводка"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaPartySubsystem* S = PartySub(W)) { S->ClickRow(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyRowMenu(
	TEXT("asda.Party.RowMenu"),
	TEXT("asda.Party.RowMenu <слот 0..5> — ПКМ по строке: меню 8100 у курсора"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaPartySubsystem* S = PartySub(W)) { S->OpenRowMenu(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyDumpRoster(
	TEXT("asda.Party.DumpRoster"),
	TEXT("asda.Party.DumpRoster — состояние строк панели 2200 (что реально показано)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { return; }
		UE_LOG(LogAsdaParty, Log, TEXT("\n%s"), *S->DumpRoster());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyFakeState(
	TEXT("asda.Party.FakeState"),
	TEXT("asda.Party.FakeState <вОтряде 0|1> <глава 0|1> <partyIndex> <правило 0|1> — выставить\n")
	TEXT("  состояние отряда напрямую. ТОЛЬКО для прогона гейтов без второго живого игрока."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: прямая подмена bInParty/лидера/PartyIndex — ветки 5094/5098/5100 с поддельным индексом.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Party.FakeState"))) { return; }
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { return; }
		S->DebugSetPartyState(A.Num() > 0 && FCString::Atoi(*A[0]) != 0,
		                      A.Num() > 1 && FCString::Atoi(*A[1]) != 0,
		                      A.Num() > 2 ? FCString::Atoi(*A[2]) : -1,
		                      A.Num() > 3 ? FCString::Atoi(*A[3]) : 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyFakeMaster(
	TEXT("asda.Party.FakeMaster"),
	TEXT("asda.Party.FakeMaster <charId нового главы> — тело 4 б пакета 5102 в очередь приёма.\n")
	TEXT("  Нужно, чтобы прогнать ветку «я глава отряда» окна 2250 без второго живого игрока."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельная смена главы открывает ветку лидера (5094).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Party.FakeMaster"))) { return; }
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { return; }
		TArray<uint8> B; B.SetNumZeroed(4);
		AsdaBytes::WrI32(&B[0], A.Num() > 0 ? FCString::Atoi(*A[0]) : 0);
		S->InjectPacket(5102, B);
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] в очередь положено тело 5102 (4 б)"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPartyShowJoin(
	TEXT("asda.Party.ShowJoinBox"),
	TEXT("asda.Party.ShowJoinBox — показать окно 2245 по ВИСЯЩЕЙ заявке (эквивалент клика по иконке уведомления)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaPartySubsystem* S = PartySub(W);
		if (!S) { return; }
		UE_LOG(LogAsdaParty, Log, TEXT("2245 показано: %s"), S->ShowJoinBox() ? TEXT("да") : TEXT("нет"));
	}));
#endif // ASDA_DEV_CONSOLE
