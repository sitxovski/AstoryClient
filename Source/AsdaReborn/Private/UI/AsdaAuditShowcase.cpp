// Copyright (c) 2026 @sitxovski (Discord)
// ★ ВИТРИНА ДЛЯ КАДРОВ РЕВИЗИИ 07.10.2026: `asda.UI.AuditShowcase <случай>`. [ПОРТ, ТЕСТОВАЯ ДИАГНОСТИКА]
//   Отчёт: `Reports/owner_1007/audit_fixes.md`.
//
// ⚠ ЭТО НЕ РАНТАЙМ-КОД. Файл целиком под `#if !UE_BUILD_SHIPPING`, игра его не зовёт; команда только офлайн
//   (`FAsdaOutgoingGate::RefuseDebugWhenOnline`). Назначение — поднять окно в ТИПОВОМ состоянии ШТАТНЫМ путём
//   и ОСТАВИТЬ его на экране, чтобы следующие шаги `asda.UI.Seq` (wait / asda.UI.Shot / asda.UI.TreeGeom) сняли
//   кадр и геометрию. Ничего не закрывается и не восстанавливается: прогон всё равно завершается `quit`.
//
// ВХОДЫ — ровно как в самотестах, откуда взята установка состояния (без своей логики разбора):
//   • пакеты — `UAsdaNetSubsystem::DebugFeedPacket` → штатный `HandlePacket`;
//   • вызовы подсистем — те же, что в самотестах (`OpenForNpcId`/`OnDialogButton`/`RunCommandForCheck`,
//     `HandleNoticeIconClick`, `AnswerMsgBox`, `SimulateClick` доп-меню, `SetBigMapVisible`);
//   • исходящие — под перехватом `UAsdaPetSubsystem::BeginSendCapture` (весь трафик, в сеть не уходит ничего).
//     ⚠ Самотесты 2-В и О1 ставят перехват `UAsdaNetSubsystem::ArmSendCapture`, но он закрыт (`friend` только
//     для самих самотестов); существующие файлы не меняем, поэтому — публичный `BeginSendCapture` (как 1-А/1-Б),
//     он шире (весь трафик), т. е. строже.
//
// СЛУЧАИ (источник установки состояния — в скобках):
//   find8200      — окно поиска Друга 8200 с двумя кандидатами (`AsdaSoulWave1ASelfTest.cpp`, этап F);
//   find8200_signs — то же, но свой знак 1 и знаки кандидатов 3/7: колонки созвездия/влечения 8275/8276 не пустые;
//   npc30024_one  — NPC 40, кнопка 581 при слоте −1 → ONEBUT (0, 24) loc 6035 (там же, этап D1);
//   npc30024_two  — то же при слоте 5 → TWOBUT (1, 8036) (этап D2), окно без ответа;
//   npc30025      — `RunCommandForCheck(30025, −1)` при слоте 5 → TWOBUT (1, 29) loc 8030 (этап E1), без ответа;
//   duel2245      — вызов на дуэль 4302 → клик по значку 14 → окно 2245 (`AsdaWave2CSelfTest.cpp`, блок 1);
//   duel1200      — отсчёт дуэли 4303 код 1 про меня при РЕАЛЬНЫХ часах (там же, блок 2);
//   tran2008      — окно обмена 2008 после 5285 код 1 (`AsdaExchangeP2PSelfTest.cpp`, шаг I1);
//   bigmap_party  — отметки отряда на БОЛЬШОЙ карте (`AsdaWave1BSelfTest.cpp`, Run/Run2/SParty/SMarks).
//   Строки лога — «[витрина] <случай>: …» (категория LogAsdaShowcase).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"                  // RefuseDebugWhenOnline — офлайн-гейт отладочных команд
#include "AsdaSessionReset.h"                  // EAsdaSessionResetReason — чистая карточка Друга / модель обмена
#include "AsdaBytes.h"                         // WrI16 / WrI32 — тела 5093/5104/5109/5283/5285
#include "AsdaSelf.h"                          // GetUserIdxOr — свой userNum для 4302/4303
#include "Login/AsdaLoginData.h"               // EncodeCp1251 — имена в телах пакетов
#include "AsdaTestPacketsP2P.h"                // ★ R5-fix-b 13.11.2026: тела 5283/5285 — общие фикстуры самотестов обмена
#include "Npc/AsdaNpcDialogSubsystem.h"        // окно разговора 570: OpenForNpcId / OnDialogButton / RunCommandForCheck
#include "Pet/AsdaPetSubsystem.h"              // BeginSendCapture — перехват всех исходящих
#include "Data/AsdaExtraMenuRow.h"             // FAsdaExtraMenuRow::Cmd — строка «Меню Друга» (команда 1)
#include "UI/AsdaSoulmateSubsystem.h"          // окно поиска 8200, карточка Друга
#include "UI/AsdaExtraMenuSubsystem.h"         // доп-меню 74600 — вход в поиск 8200
#include "UI/AsdaWindowSubsystem.h"            // очередь окон-сообщений
#include "UI/AsdaNotice.h"                     // AsdaNotice::Loc — ожидаемые тексты
#include "UI/AsdaDuelSubsystem.h"              // CDuelMgr: вызов 4302, отсчёт 4303, окно 1200
#include "UI/AsdaHotbarSubsystem.h"            // значки уведомлений 4 / 14
#include "UI/AsdaPartySubsystem.h"             // окно 2245 (общее с заявкой в отряд), отряд 5093/5107
#include "UI/AsdaExchangeP2PSubsystem.h"       // CExchange: окно 2008
#include "UI/AsdaTradeStateSubsystem.h"        // IsExchangeWindowOpen — признак «окно обмена открыто»
#include "UI/AsdaMinimapSubsystem.h"           // отметки отряда, большая карта 1710
#include "UI/AsdaUiHelpers.h"                  // WidgetAbsRect / SlotOf — живая геометрия и раскладка
#include "World/AsdaWorldSubsystem.h"          // свой павн (DebugSpawnSelfAt), фейковый сосед
#include "World/AsdaPlayerCharacter.h"         // GetGamePosXZ — тайл игрока
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

// Категория лога витрины: строки «[витрина] <случай>: …».
DEFINE_LOG_CATEGORY_STATIC(LogAsdaShowcase, Log, All);

// ⚠ Пространство имён ИМЕНОВАННОЕ (не анонимное): в unity-сборке анонимные пространства соседних .cpp сливаются
//   в одно, и одноимённые помощники (Feed/TextOf/Make4302 из самотестов) дали бы повторное определение.
namespace AsdaAuditShowcaseLocal
{
	// ── Константы, взятые из самотестов-источников ──
	static constexpr uint16 kSoulMe = 500;          // свой userNum самотеста 1-А
	static constexpr int32 kDuelSelfCharId = 910001; // свой charId самотеста 2-В
	static constexpr int32 kDuelOtherCharId = 910002; // charId вызвавшего (2-В; в мире его нет)
	static constexpr int32 kDuelOtherUser = 4321;   // userNum вызвавшего (2-В; в мире его нет)
	static constexpr uint16 kTranMe = 500;          // свой userNum самотеста О1
	static constexpr int32 kTranPeerB = 902;        // инициатор входящей заявки (О1)
	static constexpr int32 kMapMe = 1001;           // свой charId самотеста 1-Б
	static constexpr int32 kMapMate = 2002;         // соотрядник на моей карте (1-Б)
	static constexpr int32 kMapFar = 3003;          // соотрядник на чужой карте (1-Б)
	static constexpr uint16 kMapMyUserNum = 321;    // свой userNum самотеста 1-Б
	static constexpr int32 kMapParty = 42;          // свой partyIndex (1-Б)

	// «да»/«нет» для строк итога.
	static const TCHAR* YesNo(bool b) { return b ? TEXT("да") : TEXT("нет"); }

	// Подсистема GameInstance мира (nullptr — нет мира/подсистемы).
	template <class T> static T* Sub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<T>() : nullptr;
	}

	// Запустить Fn через Sec секунд мирового таймера (кадры идут, геометрия успевает отрисоваться).
	static void After(UWorld* W, float Sec, TFunction<void(UWorld*)> Fn)
	{
		if (!W) { return; }
		TWeakObjectPtr<UWorld> WW = W;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Fn]()
		{
			if (UWorld* W2 = WW.Get()) { Fn(W2); }
		}), Sec, false);
	}

	// Взвести перехват ВСЕХ исходящих (`UAsdaPetSubsystem::BeginSendCapture`, charId не подменяется) — в сеть
	//   не уходит ничего, каждый пойманный пакет — строкой лога. Повторный вызов (второй случай в том же
	//   процессе) перехват не трогает: он уже стоит. false — перехват не взведён (причина в логе).
	static bool ArmCapture(UWorld* W, const TCHAR* Case)
	{
		UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(W);
		if (!Pet)
		{
			UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — нет подсистемы питомцев, перехват исходящих не взведён"), Case);
			return false;
		}
		if (Pet->IsSendCaptureArmed()) { return true; }
		const bool bOk = Pet->BeginSendCapture([](int32 Op, const TArray<uint8>& Body, bool, TCHAR)
		{
			UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] перехвачен исходящий %d (%d б) — в сеть не ушёл"), Op, Body.Num());
		}, /*TestCharId*/ 0);
		if (!bOk)
		{
			UE_LOG(LogAsdaShowcase, Warning, TEXT("[витрина] %s: перехват не взведён — перехватчик сети занят другим инструментом (исходящие ловит он)"), Case);
		}
		return bOk;
	}

	// Свой павн: офлайн его нет — как самотест 1-Б (`Asda.Load.CurrentMap 3` + штатный `DebugSpawnSelfAt`).
	//   true — павн поднят сейчас (случаю нужно дать кадры), false — уже был.
	static bool EnsureSelfPawn(UWorld* W)
	{
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(W);
		if (!WS || ::IsValid(WS->LocalPlayerActor.Get()) || !GEngine) { return false; }
		GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
		WS->DebugSpawnSelfAt(256, 256, TEXT("Витрина"));
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] свой павн поднят офлайн (карта 3, тайл 256,256): %s"),
			YesNo(::IsValid(WS->LocalPlayerActor.Get())));
		return true;
	}

	// ════════════════ Помощники самотеста 1-А (`AsdaSoulWave1ASelfTest.cpp`) ════════════════

	// Запись соулмейта 5110 (251 б): слот [0..3], имя партнёра с [63]. Подаётся ШТАТНЫМ приёмом.
	static void FeedSlot(UAsdaNetSubsystem* Net, int32 Slot)
	{
		TArray<uint8> B; B.SetNumZeroed(251);
		B[0] = (uint8)(Slot & 0xFF); B[1] = (uint8)((Slot >> 8) & 0xFF); B[2] = (uint8)((Slot >> 16) & 0xFF); B[3] = (uint8)((Slot >> 24) & 0xFF);
		FAsdaLoginData::EncodeCp1251(TEXT("Партнёр"), B.GetData() + 63, 19);
		Net->DebugFeedPacket(5110, B);
	}

	// Запись кандидата 5484 (33 б + «о себе»): уровень, профессия, имя, пол, userNum, charId, хвост.
	static void AppendCandidate(TArray<uint8>& B, const TCHAR* Name, int32 UserNum, int32 CharId, const char* Intro, uint8 Sign = 0)
	{
		const int32 At = B.Num();                                       // начало записи
		const int32 IntroLen = (int32)FCStringAnsi::Strlen(Intro);      // длина «о себе»
		B.AddZeroed(33 + IntroLen);
		uint8* R = B.GetData() + At;                                    // запись
		R[0] = 20;                                                     // уровень
		R[1] = 1;                                                      // профессия
		FAsdaLoginData::EncodeCp1251(Name, R + 3, 19);                 // имя
		R[23] = 1;                                                     // пол
		R[24] = (uint8)(UserNum & 0xFF); R[25] = (uint8)(UserNum >> 8); // userNum
		R[26] = (uint8)(CharId & 0xFF); R[27] = (uint8)(CharId >> 8); R[28] = (uint8)(CharId >> 16); R[29] = (uint8)(CharId >> 24);
		R[30] = 1;                                                     // участников
		R[31] = (uint8)IntroLen;                                       // длина «о себе»
		R[32] = Sign;                                                  // знак созвездия (0 — не выбран)
		FMemory::Memcpy(R + 33, Intro, IntroLen);
	}

	// Текст живого TextBlock окна по имени узла («<нет узла>» — узла нет).
	static FString TextOf(const UUserWidget* Wnd, const TCHAR* Name)
	{
		const UTextBlock* T = Wnd ? Cast<UTextBlock>(Wnd->GetWidgetFromName(FName(Name))) : nullptr;
		return T ? T->GetText().ToString() : FString(TEXT("<нет узла>"));
	}

	// Подготовка соулмейт-случаев — как начало `Run` самотеста 1-А: очередь сообщений пуста, карточка Друга
	//   чистая (слот −1), свой userNum 500, «души» нет, перехват исходящих. false — нет подсистем.
	static bool PrepSoul(UWorld* W, const TCHAR* Case)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
		UAsdaSoulmateSubsystem* Soul = Sub<UAsdaSoulmateSubsystem>(W);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		if (!Net || !Soul || !WM)
		{
			UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — нет подсистем (сеть/соулмейт/окна)"), Case);
			return false;
		}
		WM->ClearMsgBoxQueue(TEXT("витрина: старт"));
		Soul->ResetSessionState(EAsdaSessionResetReason::LeftWorld);   // чистая карточка: слот −1, окна скрыты
		Net->SelfUserNum = kSoulMe; Net->bSelfUserNumKnown = true;
		Net->SetSelfSoulState(0);
		ArmCapture(W, Case);
		return true;
	}

	// ════════════════ 1. find8200 — этап F самотеста 1-А ════════════════
	// bSigns (случай `find8200_signs`, 07.10.2026, С-2 ревизии `widen_fixes2_REVIEW.md`): свой знак 1, у кандидатов знаки 3 и 7 —
	//   колонки 8275/8276 (созвездие/влечение) НЕ пустые, их видно на кадре и в строке лога после пересоздания окна.
	static void CaseFind8200(UWorld* W, bool bSigns = false)
	{
		const TCHAR* Case = bSigns ? TEXT("find8200_signs") : TEXT("find8200");   // имя случая в логе
		if (!PrepSoul(W, Case)) { return; }
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
		if (bSigns && Net) { Net->SelfConstellation = 1; }   // свой знак — иначе таблица влечения не даёт строки
		UAsdaSoulmateSubsystem* Soul = Sub<UAsdaSoulmateSubsystem>(W);
		UAsdaExtraMenuSubsystem* Etc = Sub<UAsdaExtraMenuSubsystem>(W);
		if (!Etc) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — нет подсистемы доп-меню"), Case); return; }
		int32 EtcRow = -1;   // строка «Меню Друга» (команда 1)
		for (int32 i = 0; i < Etc->GetRecordCount(); ++i)
		{
			if (const FAsdaExtraMenuRow* R = Etc->FindByIndex(i)) { if (R->Cmd == 1) { EtcRow = i; break; } }
		}
		Soul->CloseSearchWindow();
		if (EtcRow >= 0) { Etc->SimulateClick(EtcRow); }   // → 5483 (под перехватом), окно ждёт 5485
		else { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — в таблице доп-меню нет строки команды 1 («Меню Друга»), 5483 не отправлен"), Case); }
		{
			TArray<uint8> A; A.Add(2); A.Add(1); AppendCandidate(A, TEXT("Кандидат1"), 701, 7001, "abc", bSigns ? 3 : 0);
			TArray<uint8> B; B.Add(2); B.Add(1); AppendCandidate(B, TEXT("Кандидат2"), 702, 7002, "de", bSigns ? 7 : 0);
			Net->DebugFeedPacket(5484, A);
			Net->DebugFeedPacket(5484, B);
		}
		Net->DebugFeedPacket(5485, TArray<uint8>());
		UUserWidget* Find = Soul->GetSearchWindowForCheck();   // живое окно 8200
		const bool bShown = Soul->IsSearchWindowShown();
		const FString N1 = TextOf(Find, TEXT("Find_SMate_ListName01")), N2 = TextOf(Find, TEXT("Find_SMate_ListName02"));
		const FString Cur = TextOf(Find, TEXT("Find_SMate_CurrPage")), Last = TextOf(Find, TEXT("Find_SMate_LastPage"));
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: окно 8200 видно %s, строка доп-меню %d, записей %d (ждём 2), строки «%s» / «%s», страницы %s / %s (ждём 1 / 2)"),
			Case, YesNo(bShown), EtcRow, Soul->GetSearchCandidateNum(), *N1, *N2, *Cur, *Last);
		if (bSigns)
		{
			// Колонки 8275/8276 ЖИВОГО дерева: «<нет узла>» — подписи не достроены в этом экземпляре окна (С-2).
			UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: созвездие «%s» / «%s», влечение «%s» / «%s»"), Case,
				*TextOf(Find, TEXT("Find_SMate_ListConst01")), *TextOf(Find, TEXT("Find_SMate_ListConst02")),
				*TextOf(Find, TEXT("Find_SMate_ListMatch01")), *TextOf(Find, TEXT("Find_SMate_ListMatch02")));
		}
		if (!bShown || N1 != TEXT("Кандидат1") || N2 != TEXT("Кандидат2"))
		{
			UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — %s"), Case,
				!Find ? TEXT("виджета окна 8200 нет") : (!bShown ? TEXT("после 5485 окно не показано") : TEXT("строки кандидатов не те")));
		}
	}

	// Итог случая с окном-сообщением: стоит ли в очереди (Kind, Id), текст верха против loc WantLoc.
	static void ReportMsgBox(UWorld* W, const TCHAR* Case, int32 Kind, int32 Id, int32 WantLoc)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		UAsdaNpcDialogSubsystem* Dlg = Sub<UAsdaNpcDialogSubsystem>(W);
		UAsdaSoulmateSubsystem* Soul = Sub<UAsdaSoulmateSubsystem>(W);
		const bool bQueued = WM && WM->IsMsgBoxQueued(Kind, Id);
		const FString Top = WM ? WM->GetMsgBoxQueueTopText() : FString();
		const FString Want = AsdaNotice::Loc(W->GetGameInstance(), WantLoc);
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: окно (%d, %d) в очереди %s, сообщений %d, текст «%s» = loc %d %s, разговор открыт %s, слот Друга %d"),
			Case, Kind, Id, YesNo(bQueued), WM ? WM->GetMsgBoxQueueNum() : -1, *Top, WantLoc, YesNo(!Want.IsEmpty() && Top == Want),
			YesNo(Dlg && Dlg->IsDialogOpen()), Soul ? Soul->GetFriendSlot() : -999);
		if (!bQueued) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — окна (%d, %d) в очереди нет (верх «%s»)"), Case, Kind, Id, *Top); }
	}

	// ════════════════ 2/3. npc30024_one / npc30024_two — этап D (D1/D2) самотеста 1-А ════════════════
	static void CaseNpc30024(UWorld* W, bool bTwo)
	{
		const TCHAR* Case = bTwo ? TEXT("npc30024_two") : TEXT("npc30024_one");   // имя случая в логе
		if (!PrepSoul(W, Case)) { return; }
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
		UAsdaNpcDialogSubsystem* Dlg = Sub<UAsdaNpcDialogSubsystem>(W);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		if (!Dlg) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — нет подсистемы разговора"), Case); return; }
		FeedSlot(Net, bTwo ? 5 : -1);
		WM->ClearMsgBoxQueue(TEXT("витрина: D"));
		const bool bOpen = Dlg->OpenForNpcId(40);
		if (!bOpen) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — окно разговора с NPC 40 не открылось"), Case); }
		Dlg->OnDialogButton(581);   // кнопка 0 → команда 30024
		if (bTwo) { ReportMsgBox(W, Case, 1, 8036, 8036); }
		else      { ReportMsgBox(W, Case, 0, 24, 6035); }
	}

	// ════════════════ 4. npc30025 — этап E1 самотеста 1-А ════════════════
	static void CaseNpc30025(UWorld* W)
	{
		const TCHAR* Case = TEXT("npc30025");   // имя случая в логе
		if (!PrepSoul(W, Case)) { return; }
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
		UAsdaNpcDialogSubsystem* Dlg = Sub<UAsdaNpcDialogSubsystem>(W);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		if (!Dlg) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — нет подсистемы разговора"), Case); return; }
		FeedSlot(Net, 5);
		WM->ClearMsgBoxQueue(TEXT("витрина: E"));
		Dlg->RunCommandForCheck(30025, -1);   // кнопки в данных нет — штатный диспетчер команд
		ReportMsgBox(W, Case, 1, 29, 8030);
	}

	// ════════════════ Помощники самотеста 2-В (`AsdaWave2CSelfTest.cpp`) ════════════════

	// Запись little-endian i16 в тело.
	static void PutI16(TArray<uint8>& B, int32 At, int32 V) { B[At] = (uint8)(V & 0xFF); B[At + 1] = (uint8)((V >> 8) & 0xFF); }
	// Запись little-endian i32 в тело.
	static void PutI32(TArray<uint8>& B, int32 At, int32 V) { for (int32 i = 0; i < 4; ++i) { B[At + i] = (uint8)(((uint32)V >> (8 * i)) & 0xFF); } }
	// Запись имени (ASCII, до 19 б) в тело.
	static void PutStr(TArray<uint8>& B, int32 At, const char* S) { for (int32 i = 0; S[i] && i < 19; ++i) { B[At + i] = (uint8)S[i]; } }

	// 4302 (34 б): вызвавший @0/@2/@6, цель @26, группа @32.
	static TArray<uint8> Make4302(int32 CalleeUser, int32 Pvp)
	{
		TArray<uint8> B; B.SetNumZeroed(34);
		PutI16(B, 0, kDuelOtherUser); PutI32(B, 2, kDuelOtherCharId); PutStr(B, 6, "Challenger");
		PutI16(B, 26, CalleeUser); PutI32(B, 28, kDuelSelfCharId); PutI16(B, 32, Pvp);
		return B;
	}
	// 4303 (33 б): код @0, userNum @1, charId @3, имя @7, группа @27, центр @29/@31.
	static TArray<uint8> Make4303(int32 Code, int32 User, int32 Pvp)
	{
		TArray<uint8> B; B.SetNumZeroed(33);
		B[0] = (uint8)Code; PutI16(B, 1, User); PutI32(B, 3, kDuelSelfCharId); PutStr(B, 7, "Rival");
		PutI16(B, 27, Pvp); PutI16(B, 29, 256); PutI16(B, 31, 256);
		return B;
	}

	// ════════════════ 5. duel2245 — блок 1 самотеста 2-В (без ручных часов, без ответа) ════════════════
	static void CaseDuel2245(UWorld* W)
	{
		const TCHAR* Case = TEXT("duel2245");   // имя случая в логе
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
		UAsdaDuelSubsystem* Duel = Sub<UAsdaDuelSubsystem>(W);
		UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(W);
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(W);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		if (!Net || !Duel || !Hb || !Party || !WM) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — нет подсистем (сеть/дуэль/хотбар/отряд/окна)"), Case); return; }
		ArmCapture(W, Case);
		Net->SelectedCharId = kDuelSelfCharId;
		const int32 Self = AsdaSelf::GetUserIdxOr(W->GetGameInstance(), 0);   // свой userNum
		Duel->DebugSetManualClock(false);   // реальные часы: таймер 30 с идёт сам
		Duel->DebugResetAll();
		WM->ClearMsgBoxQueue(TEXT("витрина: дуэль"));
		Net->DebugFeedPacket(4302, Make4302(Self, 7));
		const bool bIcon = Hb->HasNoticeIcon(14);
		const bool bClick = Hb->HandleNoticeIconClick(14);
		const FString Txt = Party->GetJoinBoxText();
		const bool bOpen = Party->IsJoinBoxOpen() && Duel->IsPromptShown();
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: свой userNum %d (это я: %s), значок 14 был %s, клик обработан %s, окно 2245 видно %s (дуэлью %s), вызвавший «%s», текст «%s»"),
			Case, Self, YesNo(Net->IsSelfUserIdx(Self)), YesNo(bIcon), YesNo(bClick), YesNo(Party->IsJoinBoxOpen()), YesNo(Duel->IsPromptShown()),
			*Duel->GetTarget().Name, *Txt);
		if (!bOpen)
		{
			UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — %s"), Case,
				!Net->IsSelfUserIdx(Self) ? TEXT("свой userNum не известен (4302 не про меня)") : (!bIcon ? TEXT("4302 не поставил значок 14") : TEXT("клик по значку 14 окно 2245 не открыл")));
		}
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: ⚠ реальные часы — через 30 с от значка вызов сбрасывается (WM_TIMER 31), кадр снимать раньше"), Case);
	}

	// ════════════════ 6. duel1200 — блок 2 самотеста 2-В (код 1 про меня), реальные часы ════════════════
	static void CaseDuel1200(UWorld* W)
	{
		const TCHAR* Case = TEXT("duel1200");   // имя случая в логе
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
		UAsdaDuelSubsystem* Duel = Sub<UAsdaDuelSubsystem>(W);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(W);
		if (!Net || !Duel || !WM || !WS) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — нет подсистем (сеть/дуэль/окна/мир)"), Case); return; }
		ArmCapture(W, Case);
		Net->SelectedCharId = kDuelSelfCharId;
		const int32 Self = AsdaSelf::GetUserIdxOr(W->GetGameInstance(), 0);   // свой userNum
		Duel->DebugSetManualClock(false);   // реальные часы: цифры идут сами
		Duel->DebugResetAll();
		WM->ClearMsgBoxQueue(TEXT("витрина: дуэль"));
		Net->DebugFeedPacket(4303, Make4303(1, Self, 5));
		const bool bRun = Duel->IsCountdownRunning();
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: свой userNum %d (павн по номеру найден: %s), отсчёт идёт %s, группа [156] %d (ждём 5), 108101 «%s», окно 1200 видно %s"),
			Case, Self, YesNo(WS->GetPlayerActorByUserNum(Self) != nullptr), YesNo(bRun), Duel->GetDuelIndex(Self), *Duel->DebugLastText(108101),
			YesNo(Duel->DebugOverlayShown()));
		if (!bRun)
		{
			UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — %s"), Case,
				!WS->GetPlayerActorByUserNum(Self) ? TEXT("своего павна по userNum нет — эталон молчит про персонажа вне мира") : TEXT("4303 код 1 отсчёт не запустил"));
		}
		// Через 2,5 с — какие цифры на экране (кадр Seq снимает в окне 2–4 с).
		After(W, 2.5f, [Case](UWorld* W2)
		{
			UAsdaDuelSubsystem* D = Sub<UAsdaDuelSubsystem>(W2);
			if (!D) { return; }
			UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: через 2,5 с — окно 1200 видно %s, десятки %d, единицы %d, последняя цифра %d, отсчёт идёт %s"),
				Case, YesNo(D->DebugOverlayShown()), D->DebugShownTens(), D->DebugShownOnes(), D->GetLastCountdownShown(), YesNo(D->IsCountdownRunning()));
		});
	}

	// ════════════════ Помощники самотеста О1 (`AsdaExchangeP2PSelfTest.cpp`) ════════════════

	// Тело 5283 (24 б): [0] код, [1..2] userNum инициатора, [3..22] имя cp1251, [23] тип.
	using AsdaTestPacketsP2P::Body5283;   // ★ R5-fix-b 13.11.2026: была дословная копия фикстуры R5-c (`AsdaTestPacketsP2P.h`)
	// Тело 5285 (50 б): [0] код, [1] тип, [2..5] windowIndex, [6..7] userNum 1, [8..27] имя 1, [28..29] userNum 2, [30..49] имя 2.
	using AsdaTestPacketsP2P::Body5285;   // ★ R5-fix-b 13.11.2026: была дословная копия фикстуры R5-c (`AsdaTestPacketsP2P.h`)

	// ════════════════ 7. tran2008 — шаг I1 самотеста О1 ════════════════
	static void CaseTran2008(UWorld* W)
	{
		const TCHAR* Case = TEXT("tran2008");   // имя случая в логе
		UGameInstance* GI = W->GetGameInstance();
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
		UAsdaExchangeP2PSubsystem* X = Sub<UAsdaExchangeP2PSubsystem>(W);
		UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(W);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(W);
		if (!Net || !X || !Hb || !WM || !WS) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — нет подсистем (сеть/обмен/хотбар/окна/мир)"), Case); return; }
		// Подготовка О1: свой userNum 500, сосед-инициатор, панель значков, ключ исправлений 1, перехват.
		Net->SelfUserNum = kTranMe; Net->bSelfUserNumKnown = true;
		WS->DebugSpawnFakePeer(kTranPeerB, TEXT("Инициатор"), FVector2D(25710.f, 25700.f));
		Hb->DumpNoticeIcons();   // поднимает панель значков уведомлений (в мире она поднимается лениво)
		if (IConsoleVariable* CvFix = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.P2PFixDefects"))) { CvFix->Set(1, ECVF_SetByConsole); }
		ArmCapture(W, Case);
		// `Clean` самотеста: модель обмена и очередь сообщений — с нуля.
		X->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		WM->ClearMsgBoxQueue(TEXT("витрина: обмен"));
		// I1: входящая заявка → значок 4 → вопрос 1520 «Принять» → 5285 код 1.
		Net->DebugFeedPacket(5283, Body5283(1, kTranPeerB, TEXT("Инициатор"), 0)); X->Tick(0.f);
		Hb->OnNoticeIconClicked.Broadcast(4); Hb->RemoveNoticeIcon(4);
		WM->AnswerMsgBox(true, true);
		Net->DebugFeedPacket(5285, Body5285(1, 0, 42, kTranPeerB, TEXT("Инициатор"), kTranMe, TEXT("Я"))); X->Tick(0.f);
		X->Tick(0.f);   // как I2 самотеста: окно держится после тика
		const bool bOpen = UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI);
		const bool bShown = X->IsWindowShown();
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: признак «окно обмена открыто» %s, окно 2008 видно %s, #%d (ждём 42), тип %d, модель: %s"),
			Case, YesNo(bOpen), YesNo(bShown), X->GetWindowIndex(), X->GetExchangeType(), *X->DebugString());
		if (!bOpen || !bShown)
		{
			UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — окно 2008 не открыто (последний нотис %d, «%s»)"), Case, X->GetLastNoticeLoc(), *X->GetLastNoticeSlot());
		}
	}

	// ════════════════ Помощники самотеста 1-Б (`AsdaWave1BSelfTest.cpp`) ════════════════

	// 5109: уровень/HP/MP + тайл + карта одного участника (раскладка `Recv_GM_PARTY_UPDATE_PARAM`).
	static TArray<uint8> Body5109(int32 Cid, int32 Tx, int32 Tz, int32 Map)
	{
		TArray<uint8> B; B.SetNumZeroed(24);
		AsdaBytes::WrI32(&B[0], Cid); B[4] = 10; B[5] = 1; B[6] = 0;
		AsdaBytes::WrI32(&B[7], 100); AsdaBytes::WrI32(&B[11], 80);
		AsdaBytes::WrI16(&B[15], 60); AsdaBytes::WrI16(&B[17], 30);
		AsdaBytes::WrI16(&B[19], Tx); AsdaBytes::WrI16(&B[21], Tz); B[23] = (uint8)Map;
		return B;
	}
	// 5104: позиция соотрядника (раскладка сервера `sub_451A90`).
	static TArray<uint8> Body5104(int32 PartyIdx, int32 Cid, int32 Tx, int32 Tz)
	{
		TArray<uint8> B; B.SetNumZeroed(12);
		AsdaBytes::WrI16(&B[0], PartyIdx); AsdaBytes::WrI32(&B[2], Cid); AsdaBytes::WrI16(&B[6], 77);
		AsdaBytes::WrI16(&B[8], Tx); AsdaBytes::WrI16(&B[10], Tz);
		return B;
	}

	// Контекст случая большой карты (живёт между таймерами).
	struct FMapCtx
	{
		int32 MapId = -1;                         // карта мини-карты на момент прогона
		FIntPoint Tile = FIntPoint::ZeroValue;    // тайл игрока
	};

	// Строка прямоугольника виджета «(x0,y0)-(x1,y1)» («—» — не отрисован).
	static FString RectStr(const UWidget* Wd, FVector2D* OutMin = nullptr, FVector2D* OutMax = nullptr)
	{
		FVector2D Mn, Mx;
		if (!Wd || !AsdaUi::WidgetAbsRect(Wd, Mn, Mx)) { return FString(TEXT("—")); }
		if (OutMin) { *OutMin = Mn; }
		if (OutMax) { *OutMax = Mx; }
		return FString::Printf(TEXT("(%.1f,%.1f)-(%.1f,%.1f)"), Mn.X, Mn.Y, Mx.X, Mx.Y);
	}

	// Итог bigmap_party: списки, нарисованные квады, прямоугольники, смещение по слотам.
	static void ReportBigMap(UWorld* W, TSharedRef<FMapCtx> C)
	{
		const TCHAR* Case = TEXT("bigmap_party");   // имя случая в логе
		UAsdaMinimapSubsystem* MM = Sub<UAsdaMinimapSubsystem>(W);
		if (!MM) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — нет подсистемы мини-карты"), Case); return; }
		// Список отметок отряда (то, что читает отрисовка).
		const TArray<FAsdaMapMark>* M = MM->FindMarks(EAsdaMapMark::Party);
		FString Tiles;   // «(x,y карта m)» через пробел
		if (M) { for (const FAsdaMapMark& K : *M) { Tiles += FString::Printf(TEXT(" (%d,%d карта %d)"), K.TileX, K.TileY, K.MapId); } }
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: большая карта открыта %s; FindMarks(Party): %d —%s (игрок в тайле (%d,%d) карты %d; ждём 2002 в (%d,%d), 3003 на карте %d)"),
			Case, YesNo(MM->IsWorldMapOpen()), M ? M->Num() : -1, *Tiles, C->Tile.X, C->Tile.Y, C->MapId, C->Tile.X + 5, C->Tile.Y + 4, (C->MapId + 1) % 23);
		// Нарисованные квады на большой карте.
		TArray<UImage*> PartyImgs, SelfImgs;
		const int32 NParty = MM->CountDrawnMarks(EAsdaMapMark::Party, /*bBig*/ true, &PartyImgs);
		const int32 NSelf = MM->CountDrawnMarks(EAsdaMapMark::Player, /*bBig*/ true, &SelfImgs);
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: CountDrawnMarks большой карты — отряд %d, игрок %d"), Case, NParty, NSelf);
		// Слой маркеров большой карты.
		FVector2D Lmn(0.0, 0.0), Lmx(0.0, 0.0);
		const UCanvasPanel* Layer = MM->GetMarkLayer(/*bBig*/ true);
		const FString LayerRect = RectStr(Layer, &Lmn, &Lmx);
		const bool bLayer = LayerRect != TEXT("—");
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: слой маркеров большой карты %s: %s"), Case, Layer ? *Layer->GetName() : TEXT("<нет>"), *LayerRect);
		// Каждая отметка отряда: прямоугольник и «внутри слоя».
		bool bAllInside = PartyImgs.Num() > 0;   // все отметки отряда внутри слоя
		for (int32 i = 0; i < PartyImgs.Num(); ++i)
		{
			FVector2D Mn(0.0, 0.0), Mx(0.0, 0.0);
			const FString R = RectStr(PartyImgs[i], &Mn, &Mx);
			const FVector2D Ctr = (Mn + Mx) * 0.5;
			const bool bIn = bLayer && R != TEXT("—") && Ctr.X >= Lmn.X && Ctr.Y >= Lmn.Y && Ctr.X <= Lmx.X && Ctr.Y <= Lmx.Y;
			bAllInside &= bIn;
			UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: отметка отряда #%d %s: %s, центр (%.1f,%.1f), внутри слоя %s"),
				Case, i, PartyImgs[i] ? *PartyImgs[i]->GetName() : TEXT("<нет>"), *R, Ctr.X, Ctr.Y, YesNo(bIn));
		}
		for (int32 i = 0; i < SelfImgs.Num(); ++i)
		{
			UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: значок игрока #%d %s: %s (⚠ повёрнут на курс — рамка повёрнутая, смещение считается по слотам)"),
				Case, i, SelfImgs[i] ? *SelfImgs[i]->GetName() : TEXT("<нет>"), *RectStr(SelfImgs[i]));
		}
		// Смещение отметки 2002 от значка игрока — по РАСКЛАДКЕ (слоты одной канвы), как SMarks самотеста 1-Б.
		const UCanvasPanelSlot* SlP = PartyImgs.Num() == 1 ? AsdaUi::SlotOf(PartyImgs[0]) : nullptr;
		const UCanvasPanelSlot* SlS = SelfImgs.Num() == 1 ? AsdaUi::SlotOf(SelfImgs[0]) : nullptr;
		const bool bOff = SlP && SlS;
		const float Dx = bOff ? SlP->GetOffsets().Left - SlS->GetOffsets().Left : 0.f;   // смещение по X, px раскладки
		const float Dy = bOff ? SlP->GetOffsets().Top - SlS->GetOffsets().Top : 0.f;     // смещение по Y, px раскладки
		if (bOff)
		{
			UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: слот игрока (%.1f,%.1f %.0fx%.0f), слот отметки (%.1f,%.1f %.0fx%.0f)"), Case,
				SlS->GetOffsets().Left, SlS->GetOffsets().Top, SlS->GetOffsets().Right, SlS->GetOffsets().Bottom,
				SlP->GetOffsets().Left, SlP->GetOffsets().Top, SlP->GetOffsets().Right, SlP->GetOffsets().Bottom);
		}
		UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] bigmap_party: нарисовано отметок отряда %d (ждём 1: 2002 на моей карте; 3003 с чужой — нет), отметка внутри слоя %s, смещение (%+.1f,%+.1f)%s (ждём +X,+Z)"),
			NParty, YesNo(bAllInside), Dx, Dy, bOff ? TEXT("") : TEXT(" [слотов нет — смещение не посчитано]"));
		if (NParty != 1 || !bAllInside || !bOff || Dx <= 0.f || Dy <= 0.f)
		{
			UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: расхождение с ожиданием — отметок %d, внутри слоя %s, слоты %s, смещение (%+.1f,%+.1f); карта мини-карты %d"),
				Case, NParty, YesNo(bAllInside), YesNo(bOff), Dx, Dy, C->MapId);
		}
	}

	// ════════════════ 8. bigmap_party — Run2/SParty самотеста 1-Б, затем большая карта ════════════════
	static void CaseBigMapParty(UWorld* W)
	{
		const TCHAR* Case = TEXT("bigmap_party");   // имя случая в логе
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(W);
		UAsdaMinimapSubsystem* MM = Sub<UAsdaMinimapSubsystem>(W);
		const UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(W);
		if (!Net || !Party || !MM || !WS) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — нет подсистем (сеть/отряд/мини-карта/мир)"), Case); return; }
		TSharedRef<FMapCtx> C = MakeShared<FMapCtx>();
		// Run2: свои идентификаторы, карта и тайл игрока, перехват.
		Net->SelectedCharId = kMapMe; Net->SelfUserNum = kMapMyUserNum; Net->bSelfUserNumKnown = true;
		C->MapId = MM->GetDrawnMapId();
		if (const AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor))
		{
			const FVector2D Pos = P->GetGamePosXZ();
			C->Tile = FIntPoint(FMath::FloorToInt(Pos.X / 100.f), FMath::FloorToInt(Pos.Y / 100.f));
		}
		else
		{
			UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — своего павна (AAsdaPlayerCharacter) нет, тайл (0,0)"), Case);
		}
		if (C->MapId < 0) { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — мини-карта ещё не рисует карту (id %d)"), Case, C->MapId); }
		ArmCapture(W, Case);
		// 5093 код 1: отряд 42 создан, запросил я (charId 1001).
		{
			TArray<uint8> B; B.SetNumZeroed(15);
			B[0] = 1; AsdaBytes::WrI16(&B[1], kMapMyUserNum); AsdaBytes::WrI32(&B[3], kMapMe);
			AsdaBytes::WrI16(&B[7], 322); AsdaBytes::WrI32(&B[9], kMapMate); AsdaBytes::WrI16(&B[13], kMapParty);
			Net->DebugFeedPacket(5093, B);
		}
		// 5107: полный состав, слот 0 — глава (я).
		{
			TArray<uint8> R; R.SetNumZeroed(144);
			const int32 Ids[6] = { kMapMe, kMapMate, kMapFar, -1, -1, -1 };
			for (int32 i = 0; i < 6; ++i)
			{
				AsdaBytes::WrI32(&R[4 * i], Ids[i]);
				if (Ids[i] != -1) { FAsdaLoginData::EncodeCp1251(FString::Printf(TEXT("Member%d"), i), &R[24 + 20 * i], 19); }
			}
			Net->DebugFeedPacket(5107, R);
		}
		// SParty (через 0,4 с, как самотест): 5109 ×3 и 5104 ×2 с теми же тайлами, затем большая карта.
		After(W, 0.4f, [C, Case](UWorld* W2)
		{
			UAsdaNetSubsystem* N = Sub<UAsdaNetSubsystem>(W2);
			UAsdaPartySubsystem* Pa = Sub<UAsdaPartySubsystem>(W2);
			UAsdaMinimapSubsystem* Mm = Sub<UAsdaMinimapSubsystem>(W2);
			if (!N || !Pa || !Mm) { return; }
			UE_LOG(LogAsdaShowcase, Log, TEXT("[витрина] %s: отряд — в отряде %s, глава %s, partyIndex %d (ждём %d), состав %d"),
				Case, YesNo(Pa->IsInParty()), YesNo(Pa->IsPartyLeader()), Pa->GetPartyIndex(), kMapParty, Pa->GetMembers().Num());
			N->DebugFeedPacket(5109, Body5109(kMapMe,   C->Tile.X, C->Tile.Y, C->MapId));
			N->DebugFeedPacket(5109, Body5109(kMapMate, C->Tile.X + 3, C->Tile.Y + 2, C->MapId));
			N->DebugFeedPacket(5109, Body5109(kMapFar,  C->Tile.X + 1, C->Tile.Y + 1, (C->MapId + 1) % 23));
			N->DebugFeedPacket(5104, Body5104(43, kMapMate, C->Tile.X - 9, C->Tile.Y - 9));        // чужой отряд — пропуск
			N->DebugFeedPacket(5104, Body5104(kMapParty, kMapMate, C->Tile.X + 5, C->Tile.Y + 4)); // свой — новый тайл 2002
			Mm->SetBigMapVisible(true);
			After(W2, 0.6f, [C](UWorld* W3) { ReportBigMap(W3, C); });
		});
	}

	// Список случаев для подсказки.
	static const TCHAR* kCaseList = TEXT("find8200 | find8200_signs | npc30024_one | npc30024_two | npc30025 | duel2245 | duel1200 | tran2008 | bigmap_party");

	// Выполнить случай по имени (павн уже есть).
	static void RunCase(UWorld* W, const FString& Name)
	{
		if      (Name == TEXT("find8200"))     { CaseFind8200(W); }
		else if (Name == TEXT("find8200_signs")) { CaseFind8200(W, /*bSigns=*/true); }
		else if (Name == TEXT("npc30024_one")) { CaseNpc30024(W, false); }
		else if (Name == TEXT("npc30024_two")) { CaseNpc30024(W, true); }
		else if (Name == TEXT("npc30025"))     { CaseNpc30025(W); }
		else if (Name == TEXT("duel2245"))     { CaseDuel2245(W); }
		else if (Name == TEXT("duel1200"))     { CaseDuel1200(W); }
		else if (Name == TEXT("tran2008"))     { CaseTran2008(W); }
		else if (Name == TEXT("bigmap_party")) { CaseBigMapParty(W); }
		else { UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] %s: ошибка — неизвестный случай; есть: %s"), *Name, kCaseList); }
	}
}

// asda.UI.AuditShowcase <случай> — офлайн: павн (если его нет) → через 1,5 с случай; окно остаётся на экране.
static FAutoConsoleCommandWithWorldAndArgs GAsdaAuditShowcase(
	TEXT("asda.UI.AuditShowcase"),
	TEXT("asda.UI.AuditShowcase <случай> — ОФЛАЙН-витрина для кадров ревизии 07.10.2026: поднимает окно в типовом состоянии штатным путём ")
	TEXT("и оставляет его на экране. Случаи: find8200 | find8200_signs | npc30024_one | npc30024_two | npc30025 | duel2245 | duel1200 | tran2008 | bigmap_party"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.UI.AuditShowcase"))) { return; }
		if (!W) { return; }
		if (A.Num() == 0)
		{
			UE_LOG(LogAsdaShowcase, Error, TEXT("[витрина] ошибка — не указан случай; есть: %s"), AsdaAuditShowcaseLocal::kCaseList);
			return;
		}
		const FString Name = A[0].ToLower();   // имя случая
		const bool bSpawned = AsdaAuditShowcaseLocal::EnsureSelfPawn(W);   // павн поднят сейчас — дать кадры
		if (!bSpawned) { AsdaAuditShowcaseLocal::RunCase(W, Name); return; }
		AsdaAuditShowcaseLocal::After(W, 1.5f, [Name](UWorld* W2)
		{
			if (FAsdaOutgoingGate::RefuseDeferredStartWhenOnline(W2, TEXT("asda.UI.AuditShowcase"))) { return; }   // ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте
			AsdaAuditShowcaseLocal::RunCase(W2, Name);
		});
	}));

#endif // !UE_BUILD_SHIPPING
