// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ 2-Г (06.10.2026): `asda.UI.SelfTestWave2D [delay=N] [quit]` — окно города 79300 (6743/6744/6745)
//   и окно ввода имени 1552 свитков перемещения/призыва (5458/6134, 5459/6135, 6136/6137). Отчёт
//   `Reports/owner_1006/wave2D_gift_townwar.md` §5.
//   Входы ТОЛЬКО штатные:
//     • окно города — кнопкой NPC 30076 (`UAsdaNpcDialogSubsystem::RunCommandForCheck` = боевой `ExecuteCommand`);
//     • свитки — БОЕВЫМ использованием предмета (`UAsdaInventorySubsystem::UseByDoubleClick`, ветка премиум-предмета);
//     • клики — НАСТОЯЩИЕ события Slate по живой геометрии (`UAsdaWindowSubsystem::SimulateClickAt` / `SimulateDrag`);
//     • набор имени и Enter — события клавиатуры Slate в поле 1558 (`ProcessKeyCharEvent` / `ProcessKeyDownEvent`);
//     • пакеты — `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` (тот же разбор, что у сервера);
//     • отправки 6744/6134/5458/6137 — БОЕВЫЕ отправители под перехватом (`ArmSendCapture`, в сеть не уходит ничего);
//     • переход по свитку (5459 код 1) — перехват исхода (`bDebugCaptureTransfer`), сокет не трогается.
//   Контрольные пары у каждой ветки: глава ↔ не глава, «Да» ↔ «Отмена», ключ [ПОРТ] 1 ↔ 0, код ↔ код без ветки,
//   повторный запрос 6136 при висящем ↔ после ответа. Сервер не нужен: команда отказывает при живом соединении.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Net/NetOpcodes.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaTownWarSubsystem.h"
#include "UI/AsdaScrollCallSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Text/AsdaLang.h"                // слова текста налога по языку сессии (RU/EN)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPossessGate.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWave2DTest, Log, All);

// Доступ к перехвату исходящих (`friend struct FAsdaWave2DSelfTest` в `UAsdaNetSubsystem`) — только поставить/снять.
struct FAsdaWave2DSelfTest
{
	// Поставить перехватчик ВСЕГО исходящего трафика (в сеть не уходит ничего). Возвращает поколение.
	static uint32 Arm(UAsdaNetSubsystem* Net, TFunction<void(uint16, const uint8*, int32, bool, TCHAR)> Fn) { return AsdaSelfTestKit::ArmCapture(Net, MoveTemp(Fn), 0, TEXT("asda.UI.SelfTestWave2D")); }
	// Снять перехватчик своего поколения.
	static void Disarm(UAsdaNetSubsystem* Net, uint32 Gen) { AsdaSelfTestKit::DisarmCapture(Net, Gen); }
};

namespace AsdaWave2DTestLocal
{
	// Счётчики прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // пройдено / провалено
		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaWave2DTest, Log, TEXT("[волна2Г-самотест] ОК — %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaWave2DTest, Error, TEXT("[волна2Г-самотест] ПРОВАЛ — %s"), *What); }
		}
	};

	// Перехваченная отправка: опкод и тело (payload без конверта).
	using FSent = TArray<TPair<int32, TArray<uint8>>>;

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		FRun R;                                   // счётчики
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		float Delay = 0.f;                        // сдвиг старта
		bool bQuit = false;                       // выйти после итога
		TSharedPtr<FSent> Sent;                   // перехваченные исходящие
		uint32 CapGen = 0;                        // поколение перехвата
		int32 SavedCharId = 0;                    // свой charId до прогона
		uint8 SavedRawSlot = 0;                   // свой индекс персонажа до прогона
		UAsdaNetSubsystem::FAsdaCityOwner SavedCity[3];   // записи 6743 до прогона
		bool bSavedCityReceived = false;          // 6743 приходил до прогона
		TArray<uint8> SavedCell[2];               // ячейки сумки 5/6 до прогона (пусто — пустая)
		int32 DragExpect = -1;                    // ожидаемая позиция полосы после среднего перетаскивания
		int32 Pos6744 = -1;                       // позиция полосы, с которой «Нажми» ставил подтверждение
		TArray<uint8> Last6136;                   // последнее поданное тело 6136
		int32 HbAbs = -1;                         // слот хотбара проверки С-1
		FAsdaHotbarSlot HbSaved;                  // его содержимое до прогона
		int32 Sent5450Before = 0;                 // 5450 до клика по слоту
		int32 Sent5458Before = 0;                 // 5458 до клика по слоту
	};

	static constexpr int32 kSelfCharId = 910101;   // свой charId на время прогона
	static constexpr uint8 kSelfIdx    = 11;       // свой индекс персонажа (Charidx 10..12)
	static constexpr int32 kMeetCode   = 507;      // «Teleport Scroll(10+5)» — SubType 38
	static constexpr int32 kSummonCode = 509;      // «Recall Scroll(20+10)» — SubType 64
	static constexpr int32 kMeetCell   = 5;        // ячейка сумки свитка перемещения
	static constexpr int32 kSummonCell = 6;        // ячейка сумки свитка призыва

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Запустить шаг через Sec секунд от старта прогона.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Центр живой геометрии узла (false — геометрии нет).
	static bool Center(UWidget* Node, FVector2D& Out)
	{
		FVector2D Mn, Mx;
		if (!Node || !AsdaUi::WidgetAbsRect(Node, Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	// Настоящий клик Slate по центру живой геометрии узла.
	static bool ClickNode(const TSharedRef<FCtx>& C, UWidget* Node, const FString& What)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FVector2D P = FVector2D::ZeroVector;
		const bool bGeo = WM && Center(Node, P);
		C->R.Expect(bGeo, FString::Printf(TEXT("клик «%s»: живая геометрия есть"), *What));
		if (bGeo) { WM->SimulateClickAt(P, /*HoldFrames*/ 3); }
		return bGeo;
	}
	// Узел окна-сообщения по имени (TWOBUT_BUT00 и т.п.).
	static UWidget* MsgNode(const TSharedRef<FCtx>& C, const TCHAR* Win, const TCHAR* Node)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* W = WM ? WM->FindWindow(FName(Win)) : nullptr;
		return W ? W->GetWidgetFromName(FName(Node)) : nullptr;
	}
	// Тело 6743 (274 б): слот → (фракция, налог, клан, глава charId/индекс/имя).
	static TArray<uint8> Make6743(int32 Slot, int32 Faction, int32 Tax, int32 MasterCharId, int32 MasterIdx)
	{
		TArray<uint8> B; B.SetNumZeroed(274);
		B[0] = 1;
		for (int32 I = 0; I < 3; ++I)
		{
			uint8* R = B.GetData() + 1 + 91 * I;
			AsdaBytes::WrI16(R, (I == Slot) ? Faction : 3);
			R[2] = (uint8)((I == Slot) ? Tax : 0);
			if (I != Slot) { continue; }
			AsdaBytes::WrI16(R + 6, 77);
			const char* Clan = "Kimtrul"; FMemory::Memcpy(R + 8, Clan, 7);
			AsdaBytes::WrI32(R + 25, MasterCharId);
			R[29] = (uint8)MasterIdx;
			const char* Master = "Boss"; FMemory::Memcpy(R + 30, Master, 4);
		}
		return B;
	}
	// Положить запись предмета в сумку боевой точкой (`ApplyExternalRecord`, как приёмники пакетов).
	static void PutRec(const TSharedRef<FCtx>& C, int32 Cell, int32 Code, int32 Amount)
	{
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
		if (!Inv) { return; }
		if (Code <= 0) { Inv->ClearCell(1, Cell); return; }
		const TArray<uint8> R = FAsdaItemRecordWriter::Cell(Code, 1, Cell, Amount);   // ★ U17
		Inv->ApplyExternalRecord(R, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}
	// Сколько перехвачено опкода Op; OutLast — тело последнего.
	using AsdaSelfTestCtx::CountOp;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Строка cp1251 (ASCII в тесте) из тела.
	static FString Str(const TArray<uint8>& B, int32 At, int32 Max)
	{
		FString S;
		for (int32 I = 0; I < Max && At + I < B.Num() && B[At + I]; ++I) { S.AppendChar((TCHAR)B[At + I]); }
		return S;
	}
	// Набрать строку в поле, у которого клавиатурный фокус (события Slate), и (по желанию) Enter.
	static void TypeText(const FString& S, bool bEnter)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication& App = FSlateApplication::Get();
		for (TCHAR Ch : S) { App.ProcessKeyCharEvent(FCharacterEvent(Ch, FModifierKeysState(), 0, false)); }
		if (bEnter)
		{
			App.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 13));
			App.ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 13));
		}
	}
	// Тело 6136 (62 б): поля +0…+19 и имя зовущего +24.
	static TArray<uint8> Make6136(const char* Name, int32 Seed)
	{
		TArray<uint8> B; B.SetNumZeroed(62);
		for (int32 I = 0; I < 20; ++I) { B[I] = (uint8)(Seed + I); }
		FMemory::Memcpy(B.GetData() + 24, Name, FCStringAnsi::Strlen(Name));
		return B;
	}
	// Тело 6745 (2 б): код, ставка.
	static TArray<uint8> B2(int32 A, int32 B) { TArray<uint8> R; R.Add((uint8)A); R.Add((uint8)B); return R; }
	static void SetCVar(const TCHAR* Name, int32 V) { AsdaCVarSave::SetForTest(Name, V); }

	static void Finish(TSharedRef<FCtx> C);
	static void Stage2(TSharedRef<FCtx> C);

	// ═══════════════════════════ ПРОГОН ═══════════════════════════
	static void Run(TSharedRef<FCtx> C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaTownWarSubsystem* Town = Sub<UAsdaTownWarSubsystem>(C);
		UAsdaScrollCallSubsystem* Sc = Sub<UAsdaScrollCallSubsystem>(C);
		UAsdaNpcDialogSubsystem* Npc = Sub<UAsdaNpcDialogSubsystem>(C);
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FRun& R = C->R;
		if (!Net || !Town || !Sc || !Npc || !Inv || !WM)
		{
			R.Expect(false, TEXT("подсистемы сети/города/свитков/NPC/сумки/окон есть"));
			Finish(C);
			return;
		}
		// Сохранить своё состояние.
		C->SavedCharId = Net->SelectedCharId;
		C->SavedRawSlot = Net->LastSelectedRawSlot;
		for (int32 I = 0; I < 3; ++I) { C->SavedCity[I] = Net->CityOwner[I]; }
		C->bSavedCityReceived = Net->bCityOwnerReceived;
		for (int32 K = 0; K < 2; ++K)
		{
			const FAsdaItemInstance* X = Inv->GetCell(1, K == 0 ? kMeetCell : kSummonCell);
			C->SavedCell[K] = (X && !X->IsEmpty()) ? X->Raw : TArray<uint8>();
		}
		Net->SelectedCharId = kSelfCharId;
		Net->LastSelectedRawSlot = kSelfIdx;
		Net->bDebugCaptureTransfer = true;
		WM->ClearMsgBoxQueue(TEXT("самотест 2-Г: старт"));
		Town->DebugResetThrottle();
		// Перехват исходящих на весь прогон (в сеть не уходит ничего).
		C->Sent = MakeShared<FSent>();
		TSharedPtr<FSent> SentRef = C->Sent;
		C->CapGen = FAsdaWave2DSelfTest::Arm(Net, [SentRef](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			SentRef->Emplace((int32)Op, TArray<uint8>(P, FMath::Max(0, Len)));
		});

		// ═══ 1. ГОРОД: не занят → 111913, окно не открыто ═══
		const int32 Slot = AsdaUi::WarCitySlot(GIOf(C));
		R.Expect(Slot == 0, FString::Printf(TEXT("карта прогона — Альпия: слот города %d (ждём 0)"), Slot));
		Net->DebugFeedPacket(6743, Make6743(0, 3, 0, kSelfCharId, kSelfIdx));
		Town->ClearTrace();
		Npc->RunCommandForCheck(30076, -1);
		R.Expect(!Town->IsOpen() && Town->GetNoticeTrace().Num() == 1 && Town->GetNoticeTrace()[0] == 111913 && WM->GetMsgBoxQueueNum() == 1,
			FString::Printf(TEXT("30076 при ничьём городе: окно закрыто, ONEBUT 111913 «%s»"), *Town->GetLastNoticeText()));
		WM->ClearMsgBoxQueue(TEXT("самотест 2-Г"));
		// ═══ 2. ГОРОД: занят «Тьмой», налог 10 %, глава — я ═══
		Net->DebugFeedPacket(6743, Make6743(0, 1, 10, kSelfCharId, kSelfIdx));
		Npc->RunCommandForCheck(30076, -1);
		R.Expect(Town->IsOpen() && Town->GetCitySlot() == 0, TEXT("30076 при занятом городе: окно 79300 открыто (слот 0)"));
		R.Expect(Town->GetText(79311) == TEXT("+ 10%") && Town->GetText(79313) == TEXT("- 10%") && Town->GetText(79315) == TEXT("10%"),
			FString::Printf(TEXT("79311 «%s», 79313 «%s», 79315 «%s» (ждём «+ 10%%», «- 10%%», «10%%»)"),
				*Town->GetText(79311), *Town->GetText(79313), *Town->GetText(79315)));
		R.Expect(Town->GetScrollPos() == 10 && Town->GetThumbLocalX() == 11 + 185 * 10 / 25,
			FString::Printf(TEXT("полоса 79314: позиция %d, бегунок X %d (ждём 10 и %d)"), Town->GetScrollPos(), Town->GetThumbLocalX(), 11 + 185 * 10 / 25));
		const FString Body = Town->GetText(79303);
		// ★ 11.11.2026 (D4): слова города, фракции и часа — по языку сессии (на EN: «Alpen», «Darkness», «24:00»); раньше ждали
		//   только русские, и на EN верный текст давал провал.
		R.Expect(Body.Contains(TEXT("10%")) && Body.Contains(FAsdaLang::IsRu() ? TEXT("Альпия") : TEXT("Alpen")) && Body.Contains(TEXT("Kimtrul"))
			&& Body.Contains(FAsdaLang::IsRu() ? TEXT("Тьма") : TEXT("Darkness"))
			&& Body.Contains(TEXT("Boss")) && Body.Contains(FAsdaLang::IsRu() ? TEXT("00:00") : TEXT("24:00")) && !Body.Contains(TEXT("%s")) && !Body.Contains(TEXT("%d")),
			FString::Printf(TEXT("тело 79303 (Д-ТВ-1 исправлен): «%s»"), *Body));
		R.Expect(Town->GetText(79301) == AsdaNotice::Loc(GIOf(C), 111907) && Town->GetText(79321) == AsdaNotice::Loc(GIOf(C), 111904)
			&& Town->GetText(79322) == AsdaNotice::Loc(GIOf(C), 5035) && Town->GetText(79310) == AsdaNotice::Loc(GIOf(C), 111908)
			&& Town->GetText(79312) == AsdaNotice::Loc(GIOf(C), 111903),
			FString::Printf(TEXT("подписи: заголовок «%s», «%s», «%s», кнопки «%s»/«%s»"), *Town->GetText(79301), *Town->GetText(79310),
				*Town->GetText(79312), *Town->GetText(79321), *Town->GetText(79322)));
		R.Expect(Town->IsMarkShown(), TEXT("знак лагеря-хозяина 79302 нарисован (CampWar_MarkImage)"));
		R.Expect(Town->GetFoundControls() == Town->GetTotalControls(), FString::Printf(TEXT("контролы окна разобраны: %d/%d"), Town->GetFoundControls(), Town->GetTotalControls()));

		// ═══ 3. ГЕОМЕТРИЯ ПОСЛЕ ОТРИСОВКИ + КЛИКИ ПО СТРЕЛКАМ ═══
		At(C, 0.8f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			FVector2D TMn, TMx, HMn, HMx, WMn, WMx;
			const bool bT = AsdaUi::WidgetAbsRect(T->GetHitWidget(0), TMn, TMx);
			const bool bH = AsdaUi::WidgetAbsRect(T->GetHitWidget(3), HMn, HMx);
			const bool bW = AsdaUi::WidgetAbsRect(T->GetHitWidget(4), WMn, WMx);
			const float K = bT ? (TMx.X - TMn.X) / 196.f : 1.f;
			C->R.Expect(bT && bH && bW && FMath::IsNearlyEqual((HMn.X - TMn.X) / K, 74.f, 1.5f) && FMath::IsNearlyEqual((HMx.X - HMn.X) / K, 11.f, 1.f),
				FString::Printf(TEXT("живая геометрия: жёлоб %.0f×%.0f, бегунок от жёлоба %.1f (ждём 74), ширина %.1f (ждём 11), масштаб %.2f"),
					TMx.X - TMn.X, TMx.Y - TMn.Y, (HMn.X - TMn.X) / K, (HMx.X - HMn.X) / K, K));
			ClickNode(C, T->GetHitWidget(2), TEXT("→ полосы 79314"));
		});
		At(C, 1.3f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(T->GetScrollPos() == 16 && T->GetText(79315) == TEXT("16%"),
				FString::Printf(TEXT("→: позиция %d, 79315 «%s» (ждём 16 — шаг 6)"), T->GetScrollPos(), *T->GetText(79315)));
			ClickNode(C, T->GetHitWidget(1), TEXT("← полосы 79314"));
		});
		At(C, 1.8f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(T->GetScrollPos() == 10 && T->GetText(79315) == TEXT("10%"), FString::Printf(TEXT("←: позиция %d (ждём 10)"), T->GetScrollPos()));
			// Перетаскивание бегунка за правый край жёлоба → 25.
			FVector2D From, TMn, TMx;
			if (Center(T->GetHitWidget(3), From) && AsdaUi::WidgetAbsRect(T->GetHitWidget(0), TMn, TMx))
			{
				Sub<UAsdaWindowSubsystem>(C)->SimulateDrag(From, FVector2D(TMx.X + 40.f, From.Y), /*HoldFrames*/ 12);
			}
			else { C->R.Expect(false, TEXT("геометрия бегунка/жёлоба для перетаскивания")); }
		});
		At(C, 2.8f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(T->GetScrollPos() == 25 && T->GetText(79315) == TEXT("25%") && T->GetThumbLocalX() == 196,
				FString::Printf(TEXT("перетаскивание за край: позиция %d, 79315 «%s», бегунок X %d (ждём 25, 196)"), T->GetScrollPos(), *T->GetText(79315), T->GetThumbLocalX()));
			// Клик по жёлобу ЛЕВЕЕ бегунка: [ПОРТ] Д-ТВ-2 — «страница» 50 → 0.
			FVector2D TMn, TMx;
			if (AsdaUi::WidgetAbsRect(T->GetHitWidget(0), TMn, TMx)) { Sub<UAsdaWindowSubsystem>(C)->SimulateClickAt(FVector2D(TMn.X + 20.f, (TMn.Y + TMx.Y) * 0.5f), 3); }
		});
		At(C, 3.3f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(T->GetScrollPos() == 0 && T->GetText(79315) == TEXT("0%"), FString::Printf(TEXT("жёлоб левее бегунка (ключ 1): позиция %d (ждём 0)"), T->GetScrollPos()));
			SetCVar(TEXT("asda.TownWar.FixTrackPage"), 0);
			FVector2D TMn, TMx;
			if (AsdaUi::WidgetAbsRect(T->GetHitWidget(0), TMn, TMx)) { Sub<UAsdaWindowSubsystem>(C)->SimulateClickAt(FVector2D(TMx.X - 20.f, (TMn.Y + TMx.Y) * 0.5f), 3); }
		});
		At(C, 3.8f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(T->GetScrollPos() == 0, FString::Printf(TEXT("жёлоб правее бегунка (ключ 0 — как оригинал): позиция %d не изменилась (пара)"), T->GetScrollPos()));
			SetCVar(TEXT("asda.TownWar.FixTrackPage"), 1);
			// Перетаскивание в середину жёлоба: x = 98 от жёлоба → (98 − 5) / 185 × 25 = 12.
			FVector2D From, TMn, TMx;
			if (Center(T->GetHitWidget(3), From) && AsdaUi::WidgetAbsRect(T->GetHitWidget(0), TMn, TMx))
			{
				const float K = (TMx.X - TMn.X) / 196.f;
				C->DragExpect = FMath::Clamp((int32)(25.0 * ((98.0 - 5.0) / 185.0)), 0, 25);
				Sub<UAsdaWindowSubsystem>(C)->SimulateDrag(From, FVector2D(TMn.X + 98.f * K, From.Y), /*HoldFrames*/ 12);
			}
		});
		At(C, 4.8f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(FMath::Abs(T->GetScrollPos() - C->DragExpect) <= 1 && T->GetText(79315) == FString::Printf(TEXT("%d%%"), T->GetScrollPos()),
				FString::Printf(TEXT("перетаскивание в середину: позиция %d (ждём %d±1), 79315 «%s»"), T->GetScrollPos(), C->DragExpect, *T->GetText(79315)));
			C->Pos6744 = T->GetScrollPos();
			ClickNode(C, T->GetHitWidget(4), TEXT("«Нажми» 79321 (глава)"));
		});
		// ═══ 4. «НАЖМИ» ГЛАВОЙ → TWOBUT (1, 86) → «Да» → 6744 ═══
		At(C, 5.3f, [C]()
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(WM->IsMsgBoxQueued(1, 86) && T->GetLastNoticeText().Contains(FString::FromInt(C->Pos6744)) && !T->GetLastNoticeText().Contains(TEXT("%d")),
				FString::Printf(TEXT("TWOBUT (1, 86): «%s»"), *T->GetLastNoticeText()));
			UWidget* No = MsgNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT01_Text"));
			UWidget* Yes = MsgNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00_Text"));
			const FString YesT = Yes ? Cast<UTextBlock>(Yes)->GetText().ToString() : FString();
			const FString NoT = No ? Cast<UTextBlock>(No)->GetText().ToString() : FString();
			C->R.Expect(YesT == AsdaNotice::Loc(GIOf(C), 108510) && NoT == AsdaNotice::Loc(GIOf(C), 108511),
				FString::Printf(TEXT("кнопки TWOBUT 86: «%s» / «%s» (108510/108511)"), *YesT, *NoT));
			ClickNode(C, MsgNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00")), TEXT("«OK» TWOBUT 86"));
		});
		At(C, 5.8f, [C]()
		{
			TArray<uint8> B;
			const int32 N = CountOp(C, 6744, &B);
			C->R.Expect(N == 1 && B.Num() == 5 && AsdaBytes::RdI32(B.GetData()) == kSelfCharId && B[4] == C->Pos6744,
				FString::Printf(TEXT("6744: %d пакет(ов), %d б, charId %d, ставка %d (ждём %d)"), N, B.Num(), B.Num() >= 4 ? AsdaBytes::RdI32(B.GetData()) : -1,
					B.Num() >= 5 ? B[4] : -1, C->Pos6744));
			C->R.Expect(Sub<UAsdaWindowSubsystem>(C)->GetMsgBoxQueueNum() == 0, TEXT("после «Да» очередь окон пуста"));
			ClickNode(C, Sub<UAsdaTownWarSubsystem>(C)->GetHitWidget(4), TEXT("«Нажми» повторно < 2 с"));
		});
		At(C, 6.3f, [C]()
		{
			C->R.Expect(!Sub<UAsdaWindowSubsystem>(C)->IsMsgBoxQueued(1, 86), TEXT("«Нажми» в пределах 2000 мс отбит молча (пара замка +2256)"));
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			T->DebugResetThrottle();
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6743, Make6743(0, 1, 10, 4242, kSelfIdx));   // глава — другой
			T->ClearTrace();
			ClickNode(C, T->GetHitWidget(4), TEXT("«Нажми» не главой"));
		});
		At(C, 6.8f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			C->R.Expect(T->GetNoticeTrace().Num() == 1 && T->GetNoticeTrace()[0] == 111900 && !WM->IsMsgBoxQueued(1, 86) && CountOp(C, 6744) == 1
				&& T->GetLastNoticeText().Contains(FAsdaLang::IsRu() ? TEXT("Тьма") : TEXT("Darkness")) && T->GetLastNoticeText().Contains(TEXT("Boss")),
				FString::Printf(TEXT("не глава: ONEBUT 111900 «%s», подтверждения и 6744 нет (пара)"), *T->GetLastNoticeText()));
			WM->ClearMsgBoxQueue(TEXT("самотест 2-Г"));
			// ═══ 5. ОТВЕТЫ 6745 ═══
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			T->ClearTrace();
			Net->DebugFeedPacket(6745, B2(1, 12));
		});
		At(C, 7.1f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(T->GetNoticeTrace().Num() == 1 && T->GetNoticeTrace()[0] == 111912 && T->GetLastNoticeText().Contains(TEXT("12")),
				FString::Printf(TEXT("6745 код 1: ONEBUT 111912 «%s»"), *T->GetLastNoticeText()));
			T->ClearTrace();
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6745, B2(3, 7));
		});
		At(C, 7.4f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(T->GetNoticeTrace().Num() == 1 && T->GetNoticeTrace()[0] == 111919 && T->GetLastNoticeText().Contains(TEXT("7")),
				FString::Printf(TEXT("6745 код 3: ONEBUT 111919 «%s»"), *T->GetLastNoticeText()));
			T->ClearTrace();
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6745, B2(2, 0));
		});
		At(C, 7.7f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(T->GetNoticeTrace().Num() == 1 && T->GetNoticeTrace()[0] == 111900,
				FString::Printf(TEXT("6745 код 2 (ключ Д-ТВ-3 = 1): ONEBUT 111900 «%s»"), *T->GetLastNoticeText()));
			SetCVar(TEXT("asda.TownWar.FixRawCode"), 0);
			T->ClearTrace();
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6745, B2(4, 0));
		});
		At(C, 8.0f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(T->GetNoticeTrace().Num() == 1 && T->GetNoticeTrace()[0] == -1 && T->GetLastNoticeText() == TEXT("4"),
				FString::Printf(TEXT("6745 код 4 (ключ 0 — как оригинал): голая цифра «%s» (пара)"), *T->GetLastNoticeText()));
			SetCVar(TEXT("asda.TownWar.FixRawCode"), 1);
			Sub<UAsdaWindowSubsystem>(C)->ClearMsgBoxQueue(TEXT("самотест 2-Г"));
			// Д-ТВ-1, пара: ключ 0 — порядок кода оригинала (первым полем — город).
			SetCVar(TEXT("asda.TownWar.FixTextArgs"), 0);
			Sub<UAsdaNpcDialogSubsystem>(C)->RunCommandForCheck(30076, -1);
			const FString B0 = T->GetText(79303);
			SetCVar(TEXT("asda.TownWar.FixTextArgs"), 1);
			Sub<UAsdaNpcDialogSubsystem>(C)->RunCommandForCheck(30076, -1);
			const FString B1 = T->GetText(79303);
			C->R.Expect(B0 != B1 && B0.StartsWith(AsdaNotice::Loc(GIOf(C), 111909).Left(14)) && B0.Contains(FAsdaLang::IsRu() ? TEXT("Альпия") : TEXT("Alpen")),
				FString::Printf(TEXT("ключ Д-ТВ-1 = 0: «%s» ≠ исправленное (пара)"), *B0));
			ClickNode(C, T->GetHitWidget(5), TEXT("X 79329"));
		});
		At(C, 8.5f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(!T->IsOpen() && !Sub<UAsdaWindowSubsystem>(C)->IsWindowVisible(FName(TEXT("Town_as_WIN"))), TEXT("X 79329: окно закрыто"));
			Sub<UAsdaNpcDialogSubsystem>(C)->RunCommandForCheck(30076, -1);
		});
		At(C, 9.0f, [C]()
		{
			ClickNode(C, Sub<UAsdaTownWarSubsystem>(C)->GetHitWidget(6), TEXT("«Отмена» 79322"));
		});
		At(C, 9.5f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(!T->IsOpen(), TEXT("«Отмена» 79322: окно закрыто"));
			// ═══ 6. СВИТОК ПЕРЕМЕЩЕНИЯ (SubType 38) → окно 1552 вид 60 → 5458 ═══
			PutRec(C, kMeetCell, kMeetCode, 3);
			PutRec(C, kSummonCell, kSummonCode, 1);
			UAsdaScrollCallSubsystem* S = Sub<UAsdaScrollCallSubsystem>(C);
			const bool bUse = Sub<UAsdaInventorySubsystem>(C)->UseByDoubleClick(1, kMeetCell);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			UWidget* Txt = MsgNode(C, TEXT("GIFT_MSGBOX"), TEXT("GIFT_TEXT00"));
			const FString TxtS = UAsdaWindowSubsystem::ReadMsgBoxShownText(Txt);   // ★ 09.10.2026: узел может быть URichTextBlock
			C->R.Expect(bUse && WM->IsMsgBoxKindQueued(60) && S->GetMeetCell() == kMeetCell && TxtS == AsdaNotice::Loc(GIOf(C), 12832)
				&& WM->GetGiftMsgBoxEditText().IsEmpty() && CountOp(C, 5458) == 0,
				FString::Printf(TEXT("свиток %d из ячейки %d: окно 1552 вид 60, текст «%s», поле пусто, пакета ещё нет"), kMeetCode, kMeetCell, *TxtS));
			TypeText(TEXT("Target"), /*bEnter*/false);
		});
		At(C, 10.0f, [C]()
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			FVector2D Mn, Mx;
			const bool bGeo = AsdaUi::WidgetAbsRect(MsgNode(C, TEXT("GIFT_MSGBOX"), TEXT("GIFT_EDIT")), Mn, Mx);
			C->R.Expect(bGeo && WM->IsWindowVisible(FName(TEXT("GIFT_MSGBOX"))) && WM->GetGiftMsgBoxEditText() == TEXT("Target"),
				FString::Printf(TEXT("окно 1552 на экране, поле 1558 %.0f×%.0f, набрано «%s» (клавиатура Slate, фокус при показе)"),
					Mx.X - Mn.X, Mx.Y - Mn.Y, *WM->GetGiftMsgBoxEditText()));
			const FString OkT = Cast<UTextBlock>(MsgNode(C, TEXT("GIFT_MSGBOX"), TEXT("GIFT_BUT00_Text"))) ? Cast<UTextBlock>(MsgNode(C, TEXT("GIFT_MSGBOX"), TEXT("GIFT_BUT00_Text")))->GetText().ToString() : FString();
			C->R.Expect(OkT == AsdaNotice::Loc(GIOf(C), 2288), FString::Printf(TEXT("кнопка 1554 «%s» (loc 2288)"), *OkT));
			ClickNode(C, MsgNode(C, TEXT("GIFT_MSGBOX"), TEXT("GIFT_BUT00")), TEXT("1554 «ОК»"));
		});
		At(C, 10.5f, [C]()
		{
			TArray<uint8> B;
			const int32 N = CountOp(C, 5458, &B);
			C->R.Expect(N == 1 && B.Num() == 22 && AsdaBytes::RdI16(B.GetData()) == kMeetCell && Str(B, 2, 20) == TEXT("Target"),
				FString::Printf(TEXT("5458: %d пакет(ов), %d б, ячейка %d, имя «%s»"), N, B.Num(), B.Num() >= 2 ? AsdaBytes::RdI16(B.GetData()) : -1, *Str(B, 2, 20)));
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			C->R.Expect(!WM->IsWindowVisible(FName(TEXT("GIFT_MSGBOX"))) && !WM->IsMsgBoxKindQueued(60), TEXT("после 1554 окно 1552 закрыто, записи вида 60 нет"));
			// ═══ 7. СВИТОК ПРИЗЫВА (SubType 64) → вид 59: «Отмена» (пара) ═══
			Sub<UAsdaInventorySubsystem>(C)->UseByDoubleClick(1, kSummonCell);
			C->R.Expect(WM->IsMsgBoxKindQueued(59) && Sub<UAsdaScrollCallSubsystem>(C)->GetSummonCell() == kSummonCell, TEXT("свиток призыва: окно 1552 вид 59"));
		});
		At(C, 11.0f, [C]()
		{
			UWidget* T = MsgNode(C, TEXT("GIFT_MSGBOX"), TEXT("GIFT_TEXT00"));
			const FString TxtS = UAsdaWindowSubsystem::ReadMsgBoxShownText(T);     // ★ 09.10.2026: узел может быть URichTextBlock
			C->R.Expect(TxtS == AsdaNotice::Loc(GIOf(C), 104000), FString::Printf(TEXT("вид 59: текст «%s» (loc 104000)"), *TxtS));
			ClickNode(C, MsgNode(C, TEXT("GIFT_MSGBOX"), TEXT("GIFT_BUT01")), TEXT("1555 «Отмена»"));
		});
		At(C, 11.5f, [C]()
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			C->R.Expect(!WM->IsWindowVisible(FName(TEXT("GIFT_MSGBOX"))) && CountOp(C, 6134) == 0, TEXT("1555: окно закрыто, 6134 не ушёл (пара)"));
			Sub<UAsdaInventorySubsystem>(C)->UseByDoubleClick(1, kSummonCell);
			TypeText(TEXT("Friend"), /*bEnter*/true);   // Enter (67587) в 1558 = 1554
		});
		At(C, 12.0f, [C]()
		{
			TArray<uint8> B;
			const int32 N = CountOp(C, 6134, &B);
			C->R.Expect(N == 1 && B.Num() == 26 && AsdaBytes::RdI32(B.GetData()) == kSelfCharId && AsdaBytes::RdI16(B.GetData() + 4) == kSummonCell && Str(B, 6, 20) == TEXT("Friend"),
				FString::Printf(TEXT("Enter в 1558 → 6134: %d пакет(ов), %d б, charId %d, ячейка %d, имя «%s»"), N, B.Num(),
					B.Num() >= 4 ? AsdaBytes::RdI32(B.GetData()) : -1, B.Num() >= 6 ? AsdaBytes::RdI16(B.GetData() + 4) : -1, *Str(B, 6, 20)));
			C->R.Expect(!Sub<UAsdaWindowSubsystem>(C)->IsWindowVisible(FName(TEXT("GIFT_MSGBOX"))), TEXT("после Enter окно 1552 закрыто"));
			// ═══ 8. ОТВЕТЫ 6135 ═══
			UAsdaScrollCallSubsystem* S = Sub<UAsdaScrollCallSubsystem>(C);
			S->ClearTrace();
			TArray<uint8> P; P.SetNumZeroed(62);
			P[4] = 1; AsdaBytes::WrI32(P.GetData() + 5, kSummonCode); P[9] = 1; AsdaBytes::WrI16(P.GetData() + 10, kSummonCell); AsdaBytes::WrI32(P.GetData() + 14, 0);
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6135, P);
		});
		At(C, 12.3f, [C]()
		{
			UAsdaScrollCallSubsystem* S = Sub<UAsdaScrollCallSubsystem>(C);
			const FAsdaItemInstance* X = Sub<UAsdaInventorySubsystem>(C)->GetCell(1, kSummonCell);
			C->R.Expect((!X || X->IsEmpty()) && S->GetNoticeTrace().Num() == 1 && S->GetNoticeTrace()[0] == 104004,
				FString::Printf(TEXT("6135 код 1, остаток 0: ячейка %d очищена, ONEBUT 104004 «%s»"), kSummonCell, *S->GetLastNoticeText()));
			Sub<UAsdaWindowSubsystem>(C)->ClearMsgBoxQueue(TEXT("самотест 2-Г"));
			S->ClearTrace();
			for (int32 Code : { 2, 3, 11, 12 })
			{
				TArray<uint8> P; P.SetNumZeroed(62); P[4] = (uint8)Code;
				Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6135, P);
			}
		});
		At(C, 12.6f, [C]()
		{
			UAsdaScrollCallSubsystem* S = Sub<UAsdaScrollCallSubsystem>(C);
			const TArray<int32>& Tr = S->GetNoticeTrace();
			C->R.Expect(Tr.Num() == 3 && Tr[0] == 104007 && Tr[1] == 104008 && Tr[2] == 104016,
				FString::Printf(TEXT("6135 коды 2/3/11 → 104007/104008/104016, код 12 — молча (след %d)"), Tr.Num()));
			Sub<UAsdaWindowSubsystem>(C)->ClearMsgBoxQueue(TEXT("самотест 2-Г"));
			S->ClearTrace();
			// ═══ 9. ОТВЕТЫ 5459 ═══
			for (int32 Code : { 7, 0, 26, 10 })
			{
				TArray<uint8> P; P.SetNumZeroed(84); P[0] = (uint8)Code;
				Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(5459, P);
			}
		});
		At(C, 12.9f, [C]()
		{
			UAsdaScrollCallSubsystem* S = Sub<UAsdaScrollCallSubsystem>(C);
			const TArray<int32>& Tr = S->GetNoticeTrace();
			C->R.Expect(Tr.Num() == 3 && Tr[0] == 12165 && Tr[1] == 12837 && Tr[2] == 12857 && Sub<UAsdaWindowSubsystem>(C)->GetMsgBoxQueueNum() == 0,
				FString::Printf(TEXT("5459 коды 7/0/26 → плашка 12165/12837/12857 (не окно), код 10 — молча (след %d)"), Tr.Num()));
			S->ClearTrace();
			TArray<uint8> P; P.SetNumZeroed(84);
			P[0] = 1;
			const char* Ip = "127.0.0.1"; FMemory::Memcpy(P.GetData() + 1, Ip, 9);
			AsdaBytes::WrI16(P.GetData() + 17, 15100); AsdaBytes::WrI16(P.GetData() + 19, 100); AsdaBytes::WrI16(P.GetData() + 21, 200); AsdaBytes::WrI16(P.GetData() + 23, 7);
			AsdaBytes::WrI32(P.GetData() + 25, kMeetCode); P[29] = 1; AsdaBytes::WrI16(P.GetData() + 30, kMeetCell);
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(5459, P);
		});
		At(C, 13.2f, [C]()
		{
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(C);
			const float Cd = Hb ? Hb->GetItemCooldownRemainSec(kMeetCode, 1, kMeetCell) : 0.f;
			C->R.Expect(Net->DebugTransferMap == 7 && Net->DebugTransferX == 100 && Net->DebugTransferZ == 200 && Net->DebugTransferPort == 15100
				&& Net->DebugTransferIp == TEXT("127.0.0.1"),
				FString::Printf(TEXT("5459 код 1: переход перехвачен — %s:%d карта %d (%d,%d)"), *Net->DebugTransferIp, Net->DebugTransferPort,
					Net->DebugTransferMap, Net->DebugTransferX, Net->DebugTransferZ));
			// У всех свитков `special_itemlist.CooldownBase` = 0 (коды 1…545 подтипов 38/64) ⇒ `sub_9339C8(…, 0)` — отката нет,
			//   как у оригинала. Ветку отката проверяет контрольная пара ниже — запись с кодом 6 (CooldownBase 30000, группа 5).
			C->R.Expect(Cd <= 0.f, FString::Printf(TEXT("5459 код 1: у свитка %d CooldownBase = 0 — отката нет (%.1f с), как оригинал"), kMeetCode, Cd));
			{
				TArray<uint8> P; P.SetNumZeroed(84);
				P[0] = 1; AsdaBytes::WrI32(P.GetData() + 25, 6); P[29] = 1; AsdaBytes::WrI16(P.GetData() + 30, 9);
				Net->DebugFeedPacket(5459, P);
			}
			At(C, 0.25f, [C]()
			{
				UAsdaHotbarSubsystem* H = Sub<UAsdaHotbarSubsystem>(C);
				const float Cd6 = H ? H->GetItemCooldownRemainSec(6, 1, 9) : 0.f;
				// ★ 11.11.2026 (D4, разбор красных эталона 7.6.0): у записи 6 «Усиление атаки (1)» в special_itemlist CooldownBase = 2000 мс,
				//   группа 6 (сырые байты записи; 30000 мс и группа 5 — у записей 21…25). Игра взводит ровно табличные 2 с
				//   («откат предмета 6 …: 2000 мс, группа 6»), а самотест ждал 25–30 с. Ветка отката проверяется по данным записи.
				C->R.Expect(Cd6 > 0.f && Cd6 <= 2.f, FString::Printf(TEXT("5459 код 1 с предметом 6 (CooldownBase 2000): откат взведён %.1f с (пара)"), Cd6));
			});
			// ═══ 10. МЕНЯ ЗОВУТ 6136 → TWOBUT (1, 61) → 6137 ═══
			UAsdaScrollCallSubsystem* S = Sub<UAsdaScrollCallSubsystem>(C);
			S->ClearTrace();
			C->Last6136 = Make6136("Caller", 40);
			Net->DebugFeedPacket(6136, C->Last6136);
		});
		At(C, 13.6f, [C]()
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			UAsdaScrollCallSubsystem* S = Sub<UAsdaScrollCallSubsystem>(C);
			const int32 Before = WM->GetMsgBoxQueueNum();
			C->R.Expect(WM->IsMsgBoxQueued(1, 61) && S->IsCallPending() && S->GetLastNoticeText().Contains(TEXT("Caller")) && !S->GetLastNoticeText().Contains(TEXT("%s")),
				FString::Printf(TEXT("6136: TWOBUT (1, 61) «%s»"), *S->GetLastNoticeText()));
			UWidget* Yes = MsgNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00_Text"));
			UWidget* No  = MsgNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT01_Text"));
			const FString YesT = Yes ? Cast<UTextBlock>(Yes)->GetText().ToString() : FString();
			const FString NoT = No ? Cast<UTextBlock>(No)->GetText().ToString() : FString();
			C->R.Expect(YesT == AsdaNotice::Loc(GIOf(C), 104002) && NoT == AsdaNotice::Loc(GIOf(C), 104003),
				FString::Printf(TEXT("кнопки TWOBUT 61: «%s» / «%s» (104002/104003)"), *YesT, *NoT));
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6136, Make6136("Other", 90));
			C->Last6136.Reset();   // повтор не должен перезаписать буфер
			At(C, 0.25f, [C, Before]()
			{
				C->R.Expect(Sub<UAsdaWindowSubsystem>(C)->GetMsgBoxQueueNum() == Before, TEXT("второй 6136 при висящем запросе — молча отброшен (пара +62)"));
				ClickNode(C, MsgNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00")), TEXT("«OK» TWOBUT 61"));
			});
		});
		At(C, 14.3f, [C]()
		{
			TArray<uint8> B;
			const int32 N = CountOp(C, 6137, &B);
			bool bSame = (B.Num() == 21);
			for (int32 I = 0; I < 20 && bSame; ++I) { bSame = (B[I] == (uint8)(40 + I)); }
			C->R.Expect(N == 1 && bSame && B[20] == 1 && !Sub<UAsdaScrollCallSubsystem>(C)->IsCallPending(),
				FString::Printf(TEXT("«OK» → 6137: %d пакет(ов), %d б, поля 6136 перенесены %d, ответ %d (ждём 1), признак снят"), N, B.Num(), bSame ? 1 : 0, B.Num() >= 21 ? B[20] : -1));
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6136, Make6136("Caller2", 60));
			At(C, 0.35f, [C]() { ClickNode(C, MsgNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT01")), TEXT("«Отказать» TWOBUT 61")); });
		});
		At(C, 15.1f, [C]()
		{
			TArray<uint8> B;
			const int32 N = CountOp(C, 6137, &B);
			C->R.Expect(N == 2 && B.Num() == 21 && B[0] == 60 && B[20] == 10, FString::Printf(TEXT("«Отказать» → 6137 с ответом %d (ждём 10)"), B.Num() >= 21 ? B[20] : -1));
			// ═══ 11. ГЕЙТЫ ОДЕРЖИМОСТИ / «ДУШИ» (таблицы `AsdaPossessGate`, отправка без перехвата их проходит) ═══
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			WM->ClearMsgBoxQueue(TEXT("самотест 2-Г"));
			const uint8 SoulWas = Net->SelfSoulState;
			Net->SetSelfSoulState(2);
			const bool bBlk = AsdaPossess::BlockOutgoing(Net, 6134);
			const FString Top = WM->GetMsgBoxQueueTopText();
			const bool bMeet = AsdaPossess::IsGatedOpcode(5458) && AsdaPossess::IsGatedOpcode(6134) && !AsdaPossess::IsGatedOpcode(6744);
			Net->SetSelfSoulState(SoulWas);
			WM->ClearMsgBoxQueue(TEXT("самотест 2-Г"));
			C->R.Expect(bBlk && Top == AsdaNotice::Loc(GIOf(C), 104008) && bMeet,
				FString::Printf(TEXT("одержимость: 6134 отрезан, ONEBUT «%s» (104008); 5458 в таблице, 6744 — нет (сервер не гейтит)"), *Top));
			C->R.Expect(AsdaSoul::IsGatedOpcode(5458) && !AsdaSoul::IsGatedOpcode(6134) && !AsdaSoul::IsGatedOpcode(6744),
				TEXT("«душа»: 5458 молча (диспетчер `case 0x1552`), 6134 и 6744 — без гейта (пара)"));
			Stage2(C);
		});
	}

	// Кнопка меню NPC с кодом Code в дампе окна диалога (видимая) — имя узла `NPC_SHOP_BUTnn`.
	static FString FindMenuButton(const FString& Dump, int32 Code)
	{
		TArray<FString> Lines;
		Dump.ParseIntoArrayLines(Lines);
		const FString Want = FString::Printf(TEXT("код=%d"), Code);
		for (const FString& L : Lines)
		{
			const FString T = L.TrimStart();
			if (T.StartsWith(TEXT("NPC_SHOP_BUT")) && T.Contains(Want) && T.Contains(TEXT("видим=1"))) { return T.Left(14); }
		}
		return FString();
	}

	// ═══════════════════════════ ВТОРАЯ ЧАСТЬ: исправления по ревизии 2-Г ═══════════════════════════
	static void Stage2(TSharedRef<FCtx> C)
	{
		// ── М-4: окно города открывается НАСТОЯЩИМ кликом по кнопке 30076 в диалоге NPC (133/179/180) ──
		UAsdaNpcDialogSubsystem* Dlg = Sub<UAsdaNpcDialogSubsystem>(C);
		Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6743, Make6743(0, 1, 10, kSelfCharId, kSelfIdx));
		FString Btn; int32 NpcUsed = 0;
		for (int32 Npc : { 180, 133, 179 })
		{
			if (Dlg->IsDialogOpen()) { Dlg->Close(); }
			if (!Dlg->OpenForNpcId(Npc)) { continue; }
			Btn = FindMenuButton(Dlg->DumpWindow(), 30076);
			if (!Btn.IsEmpty()) { NpcUsed = Npc; break; }
		}
		C->R.Expect(!Btn.IsEmpty(), FString::Printf(TEXT("у NPC %d видна кнопка меню 30076 (%s)"), NpcUsed, *Btn));
		At(C, 0.6f, [C, Btn]()
		{
			if (!Btn.IsEmpty()) { ClickNode(C, MsgNode(C, TEXT("NPC_LINE_NPC"), *Btn), TEXT("кнопка NPC 30076")); }
		});
		At(C, 1.2f, [C]()
		{
			UAsdaTownWarSubsystem* T = Sub<UAsdaTownWarSubsystem>(C);
			C->R.Expect(T->IsOpen() && !Sub<UAsdaNpcDialogSubsystem>(C)->IsDialogOpen(),
				TEXT("клик по кнопке NPC 30076 → окно 79300 открыто, разговор закрыт (UI_resetAllWindowsAndLeave_5045)"));
			T->CloseWindow();
			// ── М-5: пустые имена клана/главы → «.» (как `uniconWar_UpdateFactionStateUI`) ──
			TArray<uint8> B = Make6743(0, 1, 10, kSelfCharId, kSelfIdx);
			FMemory::Memzero(B.GetData() + 1 + 8, 17);
			FMemory::Memzero(B.GetData() + 1 + 30, 20);
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			Net->DebugFeedPacket(6743, B);
			C->R.Expect(Net->CityOwner[0].ClanName == TEXT(".") && Net->CityOwner[0].MasterName == TEXT("."),
				FString::Printf(TEXT("6743 с пустыми именами: клан «%s», глава «%s» (ждём «.»)"), *Net->CityOwner[0].ClanName, *Net->CityOwner[0].MasterName));
			// ── С-1: свиток с ПАНЕЛИ БЫСТРЫХ СЛОТОВ → окно 1552 (не 5450) ──
			PutRec(C, kMeetCell, kMeetCode, 3);
			UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(C);
			const int32 Abs = Hb->AbsSlot(3, false);
			C->HbAbs = Abs;
			C->HbSaved = Hb->GetSlot(Abs);
			Hb->SetSlotContent(Abs, (uint8)EAsdaHotbarSlotType::Item, (uint8)EAsdaHotbarContainer::Bag, kMeetCell, 0, kMeetCode);
			C->Sent5450Before = CountOp(C, 5450);
			C->Sent5458Before = CountOp(C, 5458);
		});
		At(C, 1.8f, [C]()
		{
			UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(C);
			FVector2D Mn, Mx;
			const bool bGeo = Hb->GetSlotIconAbsRect(C->HbAbs, Mn, Mx);
			C->R.Expect(bGeo, FString::Printf(TEXT("слот хотбара %d со свитком %d: живая геометрия есть"), C->HbAbs, kMeetCode));
			if (bGeo)
			{
				FSlateApplication::Get().SetCursorPos((Mn + Mx) * 0.5f);
				FString Why;
				const bool bBlk = Hb->IsActivationBlockedForCheck(Why);
				UE_LOG(LogAsdaWave2DTest, Log, TEXT("[волна2Г-самотест] диагностика слота: под курсором %d, слот пуст=%d, блок=%d «%s», модалка=%d"),
					Hb->HitTestSlotUnderCursor(), Hb->GetSlot(C->HbAbs).IsEmpty() ? 1 : 0, bBlk ? 1 : 0, *Why,
					AsdaUi::IsModalDispatchBlocked(GIOf(C)) ? 1 : 0);
				Sub<UAsdaWindowSubsystem>(C)->SimulateClickAt((Mn + Mx) * 0.5f, /*HoldFrames*/ 3);
			}
		});
		At(C, 2.4f, [C]()
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			C->R.Expect(WM->IsMsgBoxKindQueued(60) && WM->IsWindowVisible(FName(TEXT("GIFT_MSGBOX"))) && CountOp(C, 5450) == C->Sent5450Before,
				FString::Printf(TEXT("клик по слоту хотбара → окно 1552 вид 60, 5450 не ушёл (было %d, стало %d)"), C->Sent5450Before, CountOp(C, 5450)));
			TypeText(TEXT("Panel"), /*bEnter*/true);
		});
		At(C, 3.0f, [C]()
		{
			TArray<uint8> B;
			const int32 N = CountOp(C, 5458, &B);
			C->R.Expect(N == C->Sent5458Before + 1 && Str(B, 2, 20) == TEXT("Panel") && AsdaBytes::RdI16(B.GetData()) == kMeetCell,
				FString::Printf(TEXT("с панели: Enter → 5458, ячейка %d, имя «%s»"), B.Num() >= 2 ? AsdaBytes::RdI16(B.GetData()) : -1, *Str(B, 2, 20)));
			UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(C);
			const FAsdaHotbarSlot& S0 = C->HbSaved;
			if (S0.IsEmpty()) { Hb->ClearSlot(C->HbAbs); } else { Hb->SetSlotContent(C->HbAbs, S0.Type, S0.SubType, S0.Index, S0.Count, S0.Id); }
			// ── М-2: окно подтверждения (1, 61) снято молча → признак снимается ([ПОРТ], ключ) ──
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6136, Make6136("Latch", 70));
		});
		At(C, 3.4f, [C]()
		{
			UAsdaScrollCallSubsystem* S = Sub<UAsdaScrollCallSubsystem>(C);
			C->R.Expect(S->IsCallPending(), TEXT("6136: признак запроса взведён"));
			Sub<UAsdaWindowSubsystem>(C)->ClearMsgBoxQueue(TEXT("самотест 2-Г: молчаливое снятие (1, 61)"));
		});
		At(C, 3.7f, [C]()
		{
			UAsdaScrollCallSubsystem* S = Sub<UAsdaScrollCallSubsystem>(C);
			C->R.Expect(!S->IsCallPending() && CountOp(C, 6137) == 2, TEXT("запись (1, 61) снята молча → признак снят, 6137 не ушёл [ПОРТ М-2]"));
			SetCVar(TEXT("asda.Scroll.ReleaseCallLatch"), 0);
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6136, Make6136("Latch2", 80));
		});
		At(C, 4.0f, [C]()
		{
			Sub<UAsdaWindowSubsystem>(C)->ClearMsgBoxQueue(TEXT("самотест 2-Г: молчаливое снятие (1, 61), ключ 0"));
		});
		At(C, 4.3f, [C]()
		{
			UAsdaScrollCallSubsystem* S = Sub<UAsdaScrollCallSubsystem>(C);
			const bool bLatched = S->IsCallPending();
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6136, Make6136("Lost", 90));
			C->R.Expect(bLatched, TEXT("ключ 0: признак остался (как оригинал, пара)"));
			At(C, 0.3f, [C]()
			{
				C->R.Expect(!Sub<UAsdaWindowSubsystem>(C)->IsMsgBoxQueued(1, 61), TEXT("ключ 0: следующий призыв отброшен без окна (дефект оригинала, пара)"));
				SetCVar(TEXT("asda.Scroll.ReleaseCallLatch"), 1);
				At(C, 0.2f, [C]() { Finish(C); });
			});
		});
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaTownWarSubsystem* Town = Sub<UAsdaTownWarSubsystem>(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
		if (Town && Town->IsOpen()) { Town->CloseWindow(); }
		if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест 2-Г: конец")); }
		if (Net)
		{
			if (C->CapGen) { FAsdaWave2DSelfTest::Disarm(Net, C->CapGen); }
			Net->bDebugCaptureTransfer = false;
			Net->SelectedCharId = C->SavedCharId;
			Net->LastSelectedRawSlot = C->SavedRawSlot;
			for (int32 I = 0; I < 3; ++I) { Net->CityOwner[I] = C->SavedCity[I]; }
			Net->bCityOwnerReceived = C->bSavedCityReceived;
		}
		if (Inv)
		{
			const int32 Cells[2] = { kMeetCell, kSummonCell };
			for (int32 K = 0; K < 2; ++K)
			{
				if (C->SavedCell[K].Num() >= 57) { Inv->ApplyExternalRecord(C->SavedCell[K], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ClearCell(1, Cells[K]); }
			}
		}
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (TownWar.FixTrackPage/FixRawCode/FixTextArgs, Scroll.ReleaseCallLatch) — к исходному
		//   значению И приоритету SetBy (прежде — константа 1 с SetByConsole).
		AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestWave2D"));
		if (UAsdaNpcDialogSubsystem* D = Sub<UAsdaNpcDialogSubsystem>(C)) { if (D->IsDialogOpen()) { D->Close(); } }
		AsdaSelfTestKit::Summary(LogAsdaWave2DTest, TEXT("[волна2Г-самотест]"), C->R.Ok, C->R.Fail, TEXT(""), false);   // ★ 11.10.2026 (В3 S1): единый итог (было «проверок N, провалов M»)
		if (C->bQuit && GEngine) { if (UWorld* W = C->W.Get()) { GEngine->Exec(W, TEXT("quit")); } }
	}

	// asda.UI.SelfTestWave2D [delay=N] [quit] — офлайн: спавн своего павна (если его нет), через 4 с — прогон.
	static FAutoConsoleCommandWithWorldAndArgs GAsdaWave2DSelfTest(
		TEXT("asda.UI.SelfTestWave2D"),
		TEXT("asda.UI.SelfTestWave2D [delay=N] [quit] — ОФЛАЙН-самотест волны 2-Г: окно города 79300 (NPC 30076, полоса, «Нажми», 6744, 6745) "
		     "и окно ввода имени 1552 свитков (5458/6134, 5459/6135, 6136/6137) — клики Slate по живой геометрии, пакеты HandlePacket"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.UI.SelfTestWave2D"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
			if (!W) { return; }
			TSharedRef<FCtx> C = MakeShared<FCtx>();
			C->W = W;
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { C->bQuit = true; }
			}
			C->Delay = AsdaSelfTestKit::DelayArgOr(Args, C->Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
			// ТОЛЬКО ОФЛАЙН: прогон шлёт 6744/6134/5458/6137 боевыми отправителями.
			static const uint16 kGuardOps[] = { 6744, 6134, 5458, 6137 };
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.SelfTestWave2D"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
			auto Start = [C]()
			{
				UWorld* Wd = C->W.Get();
				if (!Wd || !GEngine) { return; }
				if (AsdaSelfTestKit::RefuseDeferred(Wd, TEXT("asda.UI.SelfTestWave2D"), C->bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
				APlayerController* P = Wd->GetFirstPlayerController();
				if (!P || !P->GetPawn())
				{
					GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
					GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 Wave2D"));
				}
				FTimerHandle H;
				Wd->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]() { Run(C); }), 4.f, false);
			};
			if (C->Delay <= 0.f) { Start(); return; }
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Start), C->Delay, false);
		}));
}

#endif // !UE_BUILD_SHIPPING
