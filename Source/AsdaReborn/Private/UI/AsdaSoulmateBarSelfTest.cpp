// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ПАНЕЛИ ДРУГА 6500 «SM bar» (23.10.2026): `asda.Soulmate.BarSelfTest [quit]`. [ПОРТ]
//   Всё — штатным путём: приём через `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` → делегаты; наведение —
//   `FSlateApplication::SetCursorPos` по живой геометрии; клик — `UAsdaWindowSubsystem::SimulateClickAt` (настоящие события Slate);
//   Друг — фейковый сосед `DebugSpawnFakePeer` (путь снапшота 4005/4065). Офлайн (-AsdaNoNet), на карте мира.
//   Этапы:
//     0  без Друга панели нет;
//     1  5110 (запись) без 5220 — панели нет (хвост unitFrame_Refresh требует серверный признак 5220 @33, как оригинал);
//     2  5220 СВОЯ: галки 6546/6543..6545 ← @33/@36..38, панель показана, нотис 110103, имя/уровни/HP/MP/значок класса/эффект;
//        контроль — 5220 чужого charId ничего не меняет;
//     3  5111: уровень и HP/MP обновлены;   4  5112: три эффекта, порядок слотов (33000, 33014, 33001);
//     5  наведение: 6501 → подсказка (владелец SoulPop), значок 33014 → подсказка, уход — погашена;
//     6  ЛКМ по панели → Друг целью; контроль — ключ PopClickTarget 0 цель не ставит;
//     7  5226 «Друг уходит» → панель скрыта; 5220 @25=0 — не показывается; @25=1 — снова (с нотисом);
//     8  смена карты (сброс MapChanged + снос окон) → панель вернулась с теми же данными;
//     9  Alt+Z — панель скрыта вместе с интерфейсом, подсказки нет; показ интерфейса — видна;
//     10 ключ PopFrom5220 0 — прежний порт: 5220 не даёт панели (доказывает, что ключ откатывает).
//   Итог: «ПАНЕЛЬ-ДРУГА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Login/AsdaLoginData.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSmBarTest, Log, All);

namespace AsdaSmBarTest
{
	static const TCHAR* kCmd = TEXT("asda.Soulmate.BarSelfTest");   // имя команды
	static constexpr int32  kMe = 1001;          // свой charId на прогон
	static constexpr uint16 kMyUserNum = 321;    // свой userNum на прогон
	static constexpr int32  kMateU = 777;        // userNum Друга (фейковый сосед)
	static constexpr int32  kMateCid = 900000 + kMateU;   // charId фейкового соседа (правило DebugSpawnFakePeer)
	static constexpr int32  kSlot = 5;           // слот пары

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                  // счёт
		TWeakObjectPtr<UWorld> W;                // мир прогона
		bool bQuit = false;                      // выйти после итога
		TUniquePtr<FAsdaNetSelfScope> NetScope;  // свои charId/userNum — вернуть в конце
		int32 BuffA = -1, BuffB = -1, SoulC = -1;   // реальные id таблиц stateinfotip/soulstateinfotip
		int32 NoticeSeq = 0;                     // след нотисов до шага
		TWeakObjectPtr<AActor> Peer;             // актор Друга
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSmBarTest, Log, TEXT("ПАНЕЛЬ-ДРУГА ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSmBarTest, Error, TEXT("ПАНЕЛЬ-ДРУГА FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Feed;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только тем, что подсистема сети бралась через `Sub<UAsdaNetSubsystem>` — то же, что `AsdaTestNet::Get`.
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Запись 5110 (251 б): слот, Друг, HP/MP, уровень, класс, уровень Дружбы, имя, эффект A в rec+83.
	static TArray<uint8> Body5110(const FCtxRef& C)
	{
		TArray<uint8> B; B.SetNumZeroed(251);
		AsdaBytes::WrI32(&B[0], kSlot); AsdaBytes::WrI32(&B[4], kMateCid); AsdaBytes::WrI16(&B[8], kMateU);
		AsdaBytes::WrI32(&B[13], 1000); AsdaBytes::WrI32(&B[17], 500);   // HP макс/тек
		AsdaBytes::WrI16(&B[21], 200);  AsdaBytes::WrI16(&B[23], 50);    // MP макс/тек
		B[29] = 42; B[38] = 3; B[40] = 2; B[50] = 3;
		FAsdaLoginData::EncodeCp1251(TEXT("Mate"), &B[63], 19);
		B[83] = 1; AsdaBytes::WrI32(&B[85], C->BuffA);
		return B;
	}
	// 5111 (50 б): HP 250/1000, MP 100/200, уровень 43, класс 2, уровень Дружбы 3.
	static TArray<uint8> Body5111()
	{
		TArray<uint8> B; B.SetNumZeroed(50);
		AsdaBytes::WrI32(&B[0], 1000); AsdaBytes::WrI32(&B[4], 250);
		AsdaBytes::WrI16(&B[8], 200);  AsdaBytes::WrI16(&B[10], 100);
		B[16] = 43; B[27] = 2; B[37] = 3;
		return B;
	}
	// 5220 (одна запись 101 б).
	static TArray<uint8> Body5220(int32 CharId, uint8 Login, uint8 Opt)
	{
		TArray<uint8> B; B.SetNumZeroed(101);
		AsdaBytes::WrI32(&B[0], CharId); AsdaBytes::WrI16(&B[4], (CharId == kMe) ? kMyUserNum : 999);
		AsdaBytes::WrI32(&B[6], kSlot); AsdaBytes::WrI32(&B[19], kMateCid); AsdaBytes::WrI16(&B[23], kMateU);
		B[25] = Login; B[33] = Opt; B[36] = 1; B[37] = 0; B[38] = 1;
		return B;
	}

	static UUserWidget* Pop(const FCtxRef& C) { UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C); return Sm ? Sm->GetPopWindow() : nullptr; }
	static bool PopShown(const FCtxRef& C)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* P = Pop(C);
		return WM && P && WM->IsWindowLive(P) && WM->IsWindowVisible(FName(TEXT("SOULMATE_POP_WIN")));
	}
	static FString Txt(const FCtxRef& C, const TCHAR* Name)
	{
		UUserWidget* P = Pop(C);
		const UTextBlock* T = P ? Cast<UTextBlock>(P->GetWidgetFromName(FName(Name))) : nullptr;
		return T ? T->GetText().ToString() : FString(TEXT("<нет узла>"));
	}
	// Ширина заливки полосы (Right оффсета плана `<idc>_<обход>_1`).
	static float FillW(const FCtxRef& C, const TCHAR* Prefix)
	{
		UUserWidget* P = Pop(C);
		UWidget* W = P ? AsdaUi::FindNodeByPrefix(P, Prefix) : nullptr;
		UCanvasPanelSlot* S = W ? AsdaUi::SlotOf(W) : nullptr;
		return S ? S->GetOffsets().Right : -1.f;
	}
	static bool Center(UWidget* W, FVector2D& Out)
	{
		FVector2D Mn, Mx;
		if (!W || !AsdaUi::WidgetAbsRect(W, Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	static void Cursor(const FCtxRef& C, const TCHAR* Prefix)
	{
		UUserWidget* P = Pop(C);
		FVector2D Pt;
		if (P && Center(AsdaUi::FindNodeByPrefix(P, Prefix), Pt)) { FSlateApplication::Get().SetCursorPos(Pt); }
		else { C->Expect(false, FString::Printf(TEXT("у «%s» нет живой геометрии"), Prefix)); }
	}
	static AActor* TargetNow(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		return PC ? PC->TargetActor.Get() : nullptr;
	}
	static void ClearTarget(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr) { PC->ClearTarget(); }
	}

	static void S1(FCtxRef C); static void S2(FCtxRef C); static void S3(FCtxRef C); static void S4(FCtxRef C);
	static void S5(FCtxRef C); static void S5b(FCtxRef C); static void S5c(FCtxRef C); static void S6(FCtxRef C);
	static void S6b(FCtxRef C); static void S6c(FCtxRef C); static void S7(FCtxRef C); static void S7b(FCtxRef C);
	static void S7c(FCtxRef C); static void S8(FCtxRef C); static void S9(FCtxRef C); static void S9b(FCtxRef C);
	static void S10(FCtxRef C); static void Finish(FCtxRef C);

	// ═══ 0 → 1 ═══
	static void S0(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && !PopShown(C) && !Sm->IsPopWanted(), TEXT("0: без Друга панели 6500 нет"));
		Feed(C, 5110, Body5110(C));
		After(C, 0.3f, &S1);
	}
	static void S1(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && Sm->HasSoulmatePair() && Sm->GetMateUserIdx() == kMateU && Sm->GetMateCharId() == kMateCid && !PopShown(C),
			FString::Printf(TEXT("1: 5110 без 5220 — пара есть, Друг %d/%d, панели НЕТ (гейт серверного признака, как оригинал)"),
				Sm ? Sm->GetMateUserIdx() : -2, Sm ? Sm->GetMateCharId() : -2));
		C->NoticeSeq = Sm ? Sm->GetPopNoticeSeq() : 0;
		Feed(C, 5220, Body5220(kMe, 1, 1));
		After(C, 0.4f, &S2);
	}
	static void S2(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		if (!Sm) { C->Expect(false, TEXT("2: нет подсистемы")); Finish(C); return; }
		C->Expect(Sm->GetMode(3) == 1 && Sm->GetMode(0) == 1 && Sm->GetMode(1) == 0 && Sm->GetMode(2) == 1 && Sm->GetPopServerOption()
			&& Sm->GetSelfSoulInfo5220().Num() == 101,
			FString::Printf(TEXT("2: 5220 своя — галки 6546/6543/6544/6545 = %u/%u/%u/%u (ждём 1/1/0/1), признак панели, запись 101 б"),
				Sm->GetMode(3), Sm->GetMode(0), Sm->GetMode(1), Sm->GetMode(2)));
		C->Expect(PopShown(C) && Sm->IsPopWanted(), TEXT("2: панель 6500 ПОКАЗАНА (жалоба владельца «SM bar is not showing up»)"));
		C->Expect(Sm->GetPopNoticeSeq() == C->NoticeSeq + 1 && Sm->GetPopLastNoticeLoc() == 110103,
			FString::Printf(TEXT("2: нотис 110103 «Появляется Друг» (было %d, стало %d, loc %d)"), C->NoticeSeq, Sm->GetPopNoticeSeq(), Sm->GetPopLastNoticeLoc()));
		const FString Name = Txt(C, TEXT("SOULMATE_TXT00_Text")), L0 = Txt(C, TEXT("SOULMATE_POP_LV00")), L1 = Txt(C, TEXT("SOULMATE_POP_LV01"));
		C->Expect(Name == TEXT("Mate") && L0 == TEXT("3") && L1 == TEXT("42"),
			FString::Printf(TEXT("2: имя «%s», уровень Дружбы «%s», уровень Друга «%s» (ждём Mate/3/42)"), *Name, *L0, *L1));
		const float Hp = FillW(C, TEXT("SOULMATE_GAUGE02_15_1")), Mp = FillW(C, TEXT("SOULMATE_GAUGE03_16_1"));
		C->Expect(FMath::IsNearlyEqual(Hp, 83.f * 0.5f, 1.f) && FMath::IsNearlyEqual(Mp, 83.f * 0.25f, 1.f),
			FString::Printf(TEXT("2: HP %.1f (ждём 41.5 = 500/1000), MP %.1f (ждём 20.8 = 50/200)"), Hp, Mp));
		C->Expect(Sm->GetPopBuffShownCount() == 1 && Sm->GetPopBuffSlotId(0) == C->BuffA && Sm->GetPopClassIconIndex() == 1,
			FString::Printf(TEXT("2: эффект из 5110 (значков %d, слот 0 = %d, ждём %d), значок класса %d (ждём 1 — меч, класс 2)"),
				Sm->GetPopBuffShownCount(), Sm->GetPopBuffSlotId(0), C->BuffA, Sm->GetPopClassIconIndex()));
		// Контроль: чужая запись 5220 своих галок не трогает.
		Feed(C, 5220, Body5220(4242, 1, 0));
		C->Expect(Sm->GetMode(3) == 1 && PopShown(C), TEXT("2 контроль: 5220 чужого charId — галки и панель прежние"));
		Feed(C, 5111, Body5111());
		After(C, 0.3f, &S3);
	}
	static void S3(FCtxRef C)
	{
		const FString L1 = Txt(C, TEXT("SOULMATE_POP_LV01"));
		const float Hp = FillW(C, TEXT("SOULMATE_GAUGE02_15_1")), Mp = FillW(C, TEXT("SOULMATE_GAUGE03_16_1"));
		C->Expect(L1 == TEXT("43") && FMath::IsNearlyEqual(Hp, 83.f * 0.25f, 1.f) && FMath::IsNearlyEqual(Mp, 83.f * 0.5f, 1.f),
			FString::Printf(TEXT("3: 5111 — уровень «%s» (43), HP %.1f (20.8), MP %.1f (41.5)"), *L1, Hp, Mp));
		TArray<uint8> B; B.SetNumZeroed(168);
		B[0] = 1;  AsdaBytes::WrI32(&B[2], C->BuffA);
		B[6] = 3;  AsdaBytes::WrI32(&B[8], C->SoulC);
		B[12] = 1; AsdaBytes::WrI32(&B[14], C->BuffB);
		B[18] = 7; AsdaBytes::WrI32(&B[20], 12345);   // неизвестный вид — пропускается
		// Ещё 9 эффектов A (записи 4..12) — всего 12 значков: слот 10 ляжет в 33005 (левее плашки занятости).
		for (int32 i = 4; i <= 12; ++i) { B[6 * i] = 1; AsdaBytes::WrI32(&B[6 * i + 2], C->BuffA); }
		Feed(C, 5112, B);
		After(C, 0.3f, &S4);
	}
	static void S4(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		UUserWidget* P = Pop(C);
		const UWidget* I14 = P ? AsdaUi::FindNodeByPrefix(P, TEXT("SOULMATE_SICON14_")) : nullptr;
		const UWidget* I01 = P ? AsdaUi::FindNodeByPrefix(P, TEXT("SOULMATE_SICON01_")) : nullptr;
		const UWidget* I15 = P ? AsdaUi::FindNodeByPrefix(P, TEXT("SOULMATE_SICON06_")) : nullptr;
		C->Expect(Sm && Sm->GetPopBuffShownCount() == 12 && Sm->GetPopBuffSlotId(0) == C->BuffA && Sm->GetPopBuffSlotId(1) == C->SoulC
			&& Sm->GetPopBuffSlotId(2) == C->BuffB && I14 && I14->GetVisibility() != ESlateVisibility::Collapsed
			&& I01 && I01->GetVisibility() != ESlateVisibility::Collapsed && I15 && I15->GetVisibility() == ESlateVisibility::Collapsed,
			FString::Printf(TEXT("4: 5112 — значков %d (12, вид 7 пропущен), слоты %d/%d/%d; порядок эталона: слот 1 = 33014, слот 2 = 33001, 33006 (слот 12) пуст"),
				Sm ? Sm->GetPopBuffShownCount() : -1, Sm ? Sm->GetPopBuffSlotId(0) : -1, Sm ? Sm->GetPopBuffSlotId(1) : -1, Sm ? Sm->GetPopBuffSlotId(2) : -1));
		// Панель занятости городов 79900 — ПОД панелью Друга (нижний ряд значков 33014… кончается на её низу).
		{
			UAsdaWindowSubsystem* WMo = Sub<UAsdaWindowSubsystem>(C);
			UUserWidget* Occ = WMo ? WMo->FindWindow(FName(TEXT("OCCUPATION_BOX"))) : nullptr;
			FVector2D PMn = FVector2D::ZeroVector, PMx = FVector2D::ZeroVector, OMn = FVector2D::ZeroVector, OMx = FVector2D::ZeroVector;   // ★ R5-fix-c (13.11.2026): начальные значения — C4701 (при отказе геометрии числа уходили в строку проверки неинициализированными)
			const bool bP = P && AsdaUi::WidgetAbsRect(AsdaUi::FindNodeByPrefix(P, TEXT("SOULMATE_SICON14_")), PMn, PMx);
			const bool bO = Occ && AsdaUi::WidgetAbsRect(AsdaUi::FindNodeByPrefix(Occ, TEXT("NEW_OCC_BACK_")), OMn, OMx);
			C->Expect(!bO || (bP && OMn.Y >= PMx.Y - 0.5f),
				FString::Printf(TEXT("4: занятость 79900 под панелью Друга: верх занятости %.0f ≥ низ значков Друга %.0f (окно занятости %s)"),
					bO ? OMn.Y : -1.f, bP ? PMx.Y : -1.f, bO ? TEXT("есть") : TEXT("нет — проверять нечего")));
		}
		Cursor(C, TEXT("SOULMATE_POP_LV00_"));
		After(C, 0.3f, &S5);
	}
	static void S5(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		UAsdaItemTooltipSubsystem* Tip = Sub<UAsdaItemTooltipSubsystem>(C);
		C->Expect(Sm && Sm->GetPopTipControl() == 6501 && Tip && Tip->IsTooltipShown() && Tip->GetTipOwner() == EAsdaTipOwner::SoulPop,
			FString::Printf(TEXT("5: наведение на 6501 — подсказка темы 16 (контрол %d, владелец SoulPop)"), Sm ? Sm->GetPopTipControl() : -1));
		// Значок слота 2 (33001, первый ряд). Второй ряд (33014…) на экране 1024×768 заходит на значки занятости городов
		//   79900 (их включил владелец; у оригинала они скрыты) — там мышь ловит окно занятости; это пишется в журнал ниже.
		Cursor(C, TEXT("SOULMATE_SICON05_"));
		After(C, 0.3f, &S5b);
	}
	static void S5b(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		UAsdaItemTooltipSubsystem* Tip = Sub<UAsdaItemTooltipSubsystem>(C);
		UAsdaWindowSubsystem* WMh = Sub<UAsdaWindowSubsystem>(C);
		const FVector2D Cur = FSlateApplication::Get().GetCursorPos();
		const FName Catch = WMh ? WMh->FindWindowCatchingAt(Cur) : NAME_None;
		C->Expect(Sm && Sm->GetPopTipControl() == 33005 && Tip && Tip->IsTooltipShown(),
			FString::Printf(TEXT("5: наведение на значок 33005 (слот 10, эффект Друга) — подсказка (контрол %d; курсор %.0f,%.0f ловит окно %s)"),
				Sm ? Sm->GetPopTipControl() : -1, Cur.X, Cur.Y, *Catch.ToString()));
		{
			UUserWidget* P = Pop(C);
			FVector2D Pt;
			if (WMh && P && Center(AsdaUi::FindNodeByPrefix(P, TEXT("SOULMATE_SICON14_")), Pt))
			{
				UE_LOG(LogAsdaSmBarTest, Warning, TEXT("ПАНЕЛЬ-ДРУГА справка: центр значка 33014 (%.0f,%.0f) ловит окно %s"),
					Pt.X, Pt.Y, *WMh->FindWindowCatchingAt(Pt).ToString());
			}
			// Прямоугольники: значки эффектов (первый/второй ряд) и плашка занятости 79904 — для доклада о наложении.
			FVector2D Mn, Mx;
			if (P && AsdaUi::WidgetAbsRect(AsdaUi::FindNodeByPrefix(P, TEXT("SOULMATE_SICON13_")), Mn, Mx))
			{
				UE_LOG(LogAsdaSmBarTest, Warning, TEXT("ПАНЕЛЬ-ДРУГА справка: 33013 [%.0f,%.0f]-[%.0f,%.0f]"), Mn.X, Mn.Y, Mx.X, Mx.Y);
			}
			if (P && AsdaUi::WidgetAbsRect(AsdaUi::FindNodeByPrefix(P, TEXT("SOULMATE_SICON14_")), Mn, Mx))
			{
				UE_LOG(LogAsdaSmBarTest, Warning, TEXT("ПАНЕЛЬ-ДРУГА справка: 33014 [%.0f,%.0f]-[%.0f,%.0f]"), Mn.X, Mn.Y, Mx.X, Mx.Y);
			}
			UUserWidget* Occ = WMh ? WMh->FindWindow(FName(TEXT("OCCUPATION_BOX"))) : nullptr;
			TArray<UWidget*> All;
			if (Occ && Occ->WidgetTree) { Occ->WidgetTree->GetAllWidgets(All); }
			for (UWidget* Wd : All)
			{
				if (Wd && Wd->IsA<UImage>() && Wd->GetVisibility() != ESlateVisibility::Collapsed && AsdaUi::WidgetAbsRect(Wd, Mn, Mx))
				{
					UE_LOG(LogAsdaSmBarTest, Warning, TEXT("ПАНЕЛЬ-ДРУГА справка: занятость %s [%.0f,%.0f]-[%.0f,%.0f]"),
						*Wd->GetName(), Mn.X, Mn.Y, Mx.X, Mx.Y);
				}
			}
			{
			}
		}
		FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 400.0));
		After(C, 0.3f, &S5c);
	}
	static void S5c(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		UAsdaItemTooltipSubsystem* Tip = Sub<UAsdaItemTooltipSubsystem>(C);
		C->Expect(Sm && Sm->GetPopTipControl() == -1 && Tip && Tip->GetTipOwner() != EAsdaTipOwner::SoulPop,
			TEXT("5: курсор ушёл — подсказка панели погашена"));
		ClearTarget(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* P = Pop(C);
		FVector2D Pt;
		if (WM && P && Center(AsdaUi::FindNodeByPrefix(P, TEXT("SOULMATE_TXT00_Text")), Pt)) { WM->SimulateClickAt(Pt, 3); }
		else { C->Expect(false, TEXT("6: у имени 6502 нет геометрии")); }
		After(C, 0.5f, &S6);
	}
	static void S6(FCtxRef C)
	{
		AActor* T = TargetNow(C);
		C->Expect(T && T == C->Peer.Get(), FString::Printf(TEXT("6 [ВЛАДЕЛЕЦ]: ЛКМ по панели — Друг (userNum %d) стал целью (%s)"),
			kMateU, T ? *T->GetName() : TEXT("цели нет")));
		ClearTarget(C);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.PopClickTarget"), 0);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* P = Pop(C);
		FVector2D Pt;
		if (WM && P && Center(AsdaUi::FindNodeByPrefix(P, TEXT("SOULMATE_TXT00_Text")), Pt)) { WM->SimulateClickAt(Pt, 3); }
		After(C, 0.5f, &S6b);
	}
	static void S6b(FCtxRef C)
	{
		C->Expect(TargetNow(C) == nullptr, TEXT("6 контроль: ключ PopClickTarget 0 — клик цель не ставит (оригинал)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.PopClickTarget"), 1);
		FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 400.0));
		Feed(C, 5226, TArray<uint8>());
		After(C, 0.3f, &S6c);
	}
	static void S6c(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && !PopShown(C) && !Sm->IsPopWanted() && !Sm->HasSoulmatePair(), TEXT("7: 5226 «Друг уходит» — панель скрыта, Друга в игре нет"));
		Feed(C, 5220, Body5220(kMe, 0, 1));
		After(C, 0.3f, &S7);
	}
	static void S7(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && !PopShown(C) && !Sm->HasSoulmatePair(), TEXT("7: 5220 «Друг не в игре» (@25 = 0) — панель не показывается"));
		C->NoticeSeq = Sm ? Sm->GetPopNoticeSeq() : 0;
		Feed(C, 5220, Body5220(kMe, 1, 1));
		After(C, 0.3f, &S7b);
	}
	static void S7b(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && PopShown(C) && Sm->HasSoulmatePair() && Sm->GetPopNoticeSeq() == C->NoticeSeq + 1,
			TEXT("7: 5220 «Друг в игре» — панель снова видна, нотис 110103"));
		// Смена карты: сброс MapChanged + снос окон (как травел).
		if (Sm) { Sm->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->CloseAll(); }
		After(C, 0.8f, &S7c);
	}
	static void S7c(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		const FString Name = Txt(C, TEXT("SOULMATE_TXT00_Text"));
		C->Expect(Sm && PopShown(C) && Name == TEXT("Mate") && Sm->GetPopBuffShownCount() == 12 && Txt(C, TEXT("SOULMATE_POP_LV01")) == TEXT("43"),
			FString::Printf(TEXT("8: смена карты — панель вернулась на новом виджете: имя «%s», значков %d (12)"), *Name, Sm ? Sm->GetPopBuffShownCount() : -1));
		if (UAsdaUiVisibilitySubsystem* Vis = Sub<UAsdaUiVisibilitySubsystem>(C)) { Vis->SetUiShown(false, TEXT("самотест панели Друга")); }
		Cursor(C, TEXT("SOULMATE_POP_LV00_"));
		After(C, 0.4f, &S8);
	}
	static void S8(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		UUserWidget* P = Pop(C);
		C->Expect(P && !UAsdaUiVisibilitySubsystem::IsViewportHostVisible(P) && Sm && Sm->GetPopTipControl() == -1,
			TEXT("9: Alt+Z — панель скрыта вместе с интерфейсом, подсказки при наведении нет"));
		if (UAsdaUiVisibilitySubsystem* Vis = Sub<UAsdaUiVisibilitySubsystem>(C)) { Vis->SetUiShown(true, TEXT("самотест панели Друга")); }
		FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 400.0));
		After(C, 0.4f, &S9);
	}
	static void S9(FCtxRef C)
	{
		UUserWidget* P = Pop(C);
		C->Expect(P && PopShown(C) && UAsdaUiVisibilitySubsystem::IsViewportHostVisible(P), TEXT("9: интерфейс показан — панель видна"));
		// Ключ отката: прежний порт.
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.PopFrom5220"), 0);
		if (UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C)) { Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		Feed(C, 5110, Body5110(C));
		Feed(C, 5220, Body5220(kMe, 1, 1));
		After(C, 0.4f, &S9b);
	}
	static void S9b(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && !PopShown(C) && Sm->GetMode(3) == 0,
			TEXT("10: ключ asda.Soulmate.PopFrom5220 0 — 5220 не разбирается, панели нет (прежнее поведение порта)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.PopFrom5220"), 1);
		After(C, 0.1f, &S10);
	}
	static void S10(FCtxRef C) { Finish(C); }

	static void Finish(FCtxRef C)
	{
		UWorld* W = C->W.Get();
		// Своя запись «слота нет» (−1) — штатным приёмом: так снимается и признак «соулмейт есть» у подсистемы навыков
		//   (`UAsdaSkillUseSubsystem::SelfSoulIdx`, канал OnSoulmateState), иначе следующие самотесты пакета видели бы Друга.
		{
			TArray<uint8> B = Body5220(kMe, 0, 0);
			AsdaBytes::WrI32(&B[6], -1);
			Feed(C, 5220, B);
		}
		if (UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C)) { Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		ClearTarget(C);
		{
			TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(B.GetData(), kMateU);
			Feed(C, 4011, B);   // уход фейкового соседа из мира
		}
		C->NetScope.Reset();
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaSmBarTest, TEXT("ПАНЕЛЬ-ДРУГА"), C->Ok, C->Fail);
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		// Свой персонаж нужен клику «цель» (контроллер мира) — офлайн его нет: штатный фейковый спавн и повтор через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		if (!Net || !WS || !Fx || !Sm) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W; C->bQuit = KA.bQuit;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelectedCharId = kMe; Net->SelfUserNum = kMyUserNum; Net->bSelfUserNumKnown = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.PopFrom5220"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.PopClickTarget"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.PopBuffTipFix"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.PopClassIcon"), 1);
		// Реальные id таблиц эффектов (не выдумываем): первые две записи stateinfotip и первая soulstateinfotip.
		for (int32 Id = 1; Id < 5000 && (C->BuffA < 0 || C->BuffB < 0); ++Id)
		{
			FString N, Ic, T; int32 Cell = 0;
			if (Fx->GetStateInfo(Id, N, Ic, Cell, T)) { if (C->BuffA < 0) { C->BuffA = Id; } else { C->BuffB = Id; } }
		}
		for (int32 Id = 0; Id < 5000 && C->SoulC < 0; ++Id)
		{
			FString N, Ic, T; int32 Cell = 0;
			if (Fx->GetSoulStateInfo(Id, N, Ic, Cell, T)) { C->SoulC = Id; }
		}
		C->Expect(C->BuffA >= 0 && C->BuffB >= 0 && C->SoulC >= 0,
			FString::Printf(TEXT("подготовка: id эффектов из таблиц %d/%d/%d"), C->BuffA, C->BuffB, C->SoulC));
		Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		WS->DebugSpawnFakePeer(kMateU, TEXT("Mate"), FVector2D(25700.f, 25700.f));
		C->Peer = WS->GetPlayerActorByUserNum(kMateU);
		C->Expect(C->Peer.IsValid(), TEXT("подготовка: Друг (фейковый сосед 777) в мире"));
		FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 400.0));
		After(C, 0.3f, &S0);
	}
}

// ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★ 26.10.2026 САМОТЕСТ «ЭХО СЕБЯ» — баг Player17 (панель Друга с СВОИМ именем/ур. 70): `asda.Soulmate.SelfEchoSelfTest [quit]`.
//   Пакеты — НАСТОЯЩИЕ байты пары Player17 (Userindex 75214, userNum 0) ↔ Player52 (75230, userNum 2) из журналов LogDrop:
//   Player17 26.10 22:17:57 (5220 @19 = 75230 «в игре», 5111 со своими HP 86706, 5110 rec[4] = 75214 «Player17» ур. 70,
//   5220 @19 = 75214) и Player52 25.10 21:08 (своя 5220 @19 = 75214). Хвосты длиннее, чем печатает журнал, — нули (в разбор
//   панели не входят). 5110 Player52 для Player17 в журналах нет — собрана из БД/1023 (Player52, ур. 24, класс 12, HP 6641/7437).
//   Этапы:  E1 5220 «Друг в игре» — панель есть, Друг 75230;  E2 5112/5111 эха до 5110 — отброшены;
//           E3 5110 = я — не применена, панель скрыта, Друг вне игры;  E4 5220 @19 = я — панель не вернулась, Друг 75230 цел;
//           E5 ключ SelfEchoGuard 0 — прежнее (как оригинал): панель «Player17»/70 — баг воспроизведён;
//           E6 5110 НАСТОЯЩЕГО Друга Player52 — эхо снято, панель «Player52»/24, 5111 Друга принимается;
//           E7 сторона Player52: его 5220 + ТЕ ЖЕ байты 5110 Player17 — панель «Player17»/70 (у каждой стороны — свой Друг).
//   Итог: «ЭХО-СЕБЯ ИТОГ: ok=N fail=M».
namespace AsdaSmEchoTest
{
	using namespace AsdaSmBarTest;
	static const TCHAR* kEchoCmd = TEXT("asda.Soulmate.SelfEchoSelfTest");   // имя команды
	static constexpr int32  kImonCid = 75214;   // Player17 — Userindex (charId пакетов)
	static constexpr uint16 kImonU   = 0;       // его userNum на поле (журнал: «uidx=0»)
	static constexpr int32  kXfCid   = 75230;   // Player52
	static constexpr uint16 kXfU     = 2;       // его userNum (журнал Player52 25.10: «uidx=2»)

	// Байты из журнала в массив нужной длины (хвост — нули).
	static TArray<uint8> Hex(const TCHAR* S, int32 Len)
	{
		TArray<uint8> B; B.SetNumZeroed(Len);
		TArray<FString> Parts; FString(S).ParseIntoArrayWS(Parts);
		for (int32 i = 0; i < Parts.Num() && i < Len; ++i) { B[i] = (uint8)FParse::HexNumber(*Parts[i]); }
		return B;
	}
	// Player17 22:17:57.846 — своя 5220: Друг 75230, @23 = 0, @25 = 1 «в игре», @33 = 1, галки 1/1/0, имена share.
	static TArray<uint8> Imon5220First()
	{
		return Hex(TEXT("CE 25 01 00 00 00 08 00 00 00 03 00 00 00 00 00 00 00 01 DE 25 01 00 00 00 01 00 00 00 00 00 00 00 01 00 00 01 01 00 ")
			TEXT("49 4D 4F 4E 53 53 54 45 52 49 00 00 00 00 00 00 00 00 00 00 49 4D 4F 4E 53 53 53 54 45 52 49 00 00"), 101);
	}
	// Player17 22:17:58.063 — своя 5220 после эха: Друг @19 = 75214 (Я), карта 1 канал 10.
	static TArray<uint8> Imon5220Echo()
	{
		return Hex(TEXT("CE 25 01 00 00 00 08 00 00 00 03 00 00 00 00 00 00 00 01 CE 25 01 00 00 00 01 00 00 00 00 01 0A 00 01 00 00 01 01 00 ")
			TEXT("49 4D 4F 4E 53 53 54 45 52 49 00 00 00 00 00 00 00 00 00 00 49 4D 4F 4E 53 53 53 54 45 52 49 00 00"), 101);
	}
	// Player17 22:17:58.063 — 5111 (50 б): HP 86706/86706, MP 583/583, тайл (387,396), уровень 70 — его СОБСТВЕННЫЕ.
	static TArray<uint8> Imon5111()
	{
		return Hex(TEXT("B2 52 01 00 B2 52 01 00 47 02 47 02 83 01 8C 01 46 34 54 D5 10 02 00 00 00 00 00 02 00 03 00 00 00 00 00 00 00 01 ")
			TEXT("00 00 00 00 00 00 00 00 00 00 00 00"), 50);
	}
	// Запись Player17 5110 (251 б): rec[4] = 75214, HP 86706, ур. 70, имя с rec[63] (журнал печатает до «…STER», дописано «I»).
	static TArray<uint8> Imon5110()
	{
		TArray<uint8> B = Hex(TEXT("08 00 00 00 CE 25 01 00 00 00 00 01 0A B2 52 01 00 B2 52 01 00 47 02 47 02 83 01 8C 01 46 34 54 D5 10 02 00 00 00 ")
			TEXT("00 00 02 00 03 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00"), 251);
		FAsdaLoginData::EncodeCp1251(TEXT("Player17"), &B[63], 19);
		return B;
	}
	// 5110 Player52 для Player17 — раскладка `recv_1` (слот 8, charId 75230, userNum 2, ур. 24, класс 12, HP/MP из 1023).
	static TArray<uint8> Xf5110()
	{
		TArray<uint8> B; B.SetNumZeroed(251);
		AsdaBytes::WrI32(&B[0], 8); AsdaBytes::WrI32(&B[4], kXfCid); AsdaBytes::WrI16(&B[8], kXfU);
		AsdaBytes::WrI32(&B[13], 7437); AsdaBytes::WrI32(&B[17], 6641);
		AsdaBytes::WrI16(&B[21], 204);  AsdaBytes::WrI16(&B[23], 143);
		B[29] = 24; B[40] = 12; B[50] = 1;
		FAsdaLoginData::EncodeCp1251(TEXT("Player52"), &B[63], 19);
		return B;
	}
	// 5111 Player52: HP 3000/7437, ур. 24.
	static TArray<uint8> Xf5111()
	{
		TArray<uint8> B; B.SetNumZeroed(50);
		AsdaBytes::WrI32(&B[0], 7437); AsdaBytes::WrI32(&B[4], 3000);
		AsdaBytes::WrI16(&B[8], 204);  AsdaBytes::WrI16(&B[10], 100);
		B[16] = 24; B[27] = 12; B[37] = 1;
		return B;
	}
	// Player52 25.10 21:08:56 — своя 5220: Друг @19 = 75214, @23 = 0, «в игре».
	static TArray<uint8> Xf5220()
	{
		return Hex(TEXT("DE 25 01 00 02 00 08 00 00 00 03 00 00 00 00 00 00 00 01 CE 25 01 00 00 00 01 00 01 01 00 00 00 00 01 00 00 00 00 00 ")
			TEXT("78 46 69 72 53 54"), 101);
	}
	static void SetSelf(const FCtxRef& C, int32 Cid, uint16 U)
	{
		if (UAsdaNetSubsystem* N = Sub<UAsdaNetSubsystem>(C)) { N->SelectedCharId = Cid; N->SelfUserNum = U; N->bSelfUserNumKnown = true; }
	}
	static int32 Drops(const FCtxRef& C) { UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C); return Sm ? Sm->GetSelfEchoDropCount() : -1; }

	static void E2(FCtxRef C); static void E3(FCtxRef C); static void E4(FCtxRef C); static void E5(FCtxRef C);
	static void E5b(FCtxRef C); static void E6(FCtxRef C); static void E6b(FCtxRef C); static void E7(FCtxRef C); static void EFinish(FCtxRef C);

	// ═══ E1: своя 5220 «Друг 75230 в игре» ═══
	static void E1(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && PopShown(C) && Sm->HasSoulmatePair() && Sm->GetMateCharId() == kXfCid && !Sm->IsSelfEchoSeen(),
			FString::Printf(TEXT("E1: 5220 @19=75230 @25=1 — панель показана, Друг charId %d (ждём 75230), эха нет"), Sm ? Sm->GetMateCharId() : -2));
		C->NoticeSeq = Drops(C);
		TArray<uint8> Fx; Fx.SetNumZeroed(168); Fx[0] = 1; AsdaBytes::WrI32(&Fx[2], C->BuffA);
		Feed(C, 5112, Fx);
		Feed(C, 5111, Imon5111());
		After(C, 0.3f, &E2);
	}
	// ═══ E2: 5112/5111 эха до 5110 ═══
	static void E2(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && Drops(C) == C->NoticeSeq + 2 && Sm->GetPopBuffShownCount() == 0 && Txt(C, TEXT("SOULMATE_POP_LV01")) != TEXT("70"),
			FString::Printf(TEXT("E2: 5112 и 5111 (свои HP 86706, ур. 70) до 5110 Друга — отброшены (%d из 2), значков %d, уровень «%s»"),
				Drops(C) - C->NoticeSeq, Sm ? Sm->GetPopBuffShownCount() : -1, *Txt(C, TEXT("SOULMATE_POP_LV01"))));
		C->NoticeSeq = Drops(C);
		Feed(C, 5110, Imon5110());
		After(C, 0.3f, &E3);
	}
	// ═══ E3: 5110 = я ═══
	static void E3(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && Sm->IsSelfEchoSeen() && Drops(C) == C->NoticeSeq + 1 && !PopShown(C) && !Sm->HasSoulmatePair()
			&& Sm->GetPartnerName().IsEmpty() && Sm->GetMateCharId() == kXfCid && Sm->GetMateUserIdx() == -1,
			FString::Printf(TEXT("E3: 5110 rec[4]=75214 (я) — НЕ применена: эхо=%d, панель %s, пара(в игре)=%d, имя «%s», Друг %d/%d"),
				Sm ? (int32)Sm->IsSelfEchoSeen() : -1, PopShown(C) ? TEXT("ВИДНА") : TEXT("скрыта"), Sm ? (int32)Sm->HasSoulmatePair() : -1,
				Sm ? *Sm->GetPartnerName() : TEXT("?"), Sm ? Sm->GetMateCharId() : -2, Sm ? Sm->GetMateUserIdx() : -2));
		C->Expect(Sm && Sm->GetFriendSlot() == 8 && Sm->GetMode(3) == 1, TEXT("E3: своё из 5220 цело — слот пары 8, галка 6546"));
		C->NoticeSeq = Drops(C);
		Feed(C, 5220, Imon5220Echo());
		Feed(C, 5111, Imon5111());
		After(C, 0.3f, &E4);
	}
	// ═══ E4: 5220 @19 = я ═══
	static void E4(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && !PopShown(C) && !Sm->HasSoulmatePair() && Sm->GetMateCharId() == kXfCid && Drops(C) == C->NoticeSeq + 2,
			FString::Printf(TEXT("E4: 5220 @19=75214 (я) и 5111 — панель скрыта, Друг %d (75230 — не я), отброшено %d (ждём 2)"),
				Sm ? Sm->GetMateCharId() : -2, Drops(C) - C->NoticeSeq));
		// Контроль ключом: прежнее поведение = оригинал (`soulmateUpdateInfo` копирует без проверки) — баг виден.
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.SelfEchoGuard"), 0);
		if (Sm) { Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		Feed(C, 5220, Imon5220First());
		Feed(C, 5111, Imon5111());
		Feed(C, 5110, Imon5110());
		Feed(C, 5220, Imon5220Echo());
		After(C, 0.4f, &E5);
	}
	static void E5(FCtxRef C)
	{
		const FString Name = Txt(C, TEXT("SOULMATE_TXT00_Text")), L1 = Txt(C, TEXT("SOULMATE_POP_LV01"));
		C->Expect(PopShown(C) && Name == TEXT("Player17") && L1 == TEXT("70"),
			FString::Printf(TEXT("E5 контроль: ключ SelfEchoGuard 0 (как оригинал) — панель «%s»/«%s» = скрин игрока (своё имя и ур. 70)"), *Name, *L1));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.SelfEchoGuard"), 1);
		if (UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C)) { Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		// Снова эхо, затем — настоящий Друг входит: `recv_1` шлёт его 5110, потом `Send_SoulMateInfo` — 5220 с @19 = 75230.
		Feed(C, 5220, Imon5220First());
		Feed(C, 5110, Imon5110());
		Feed(C, 5220, Imon5220Echo());
		After(C, 0.3f, &E5b);
	}
	static void E5b(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && Sm->IsSelfEchoSeen() && !PopShown(C), TEXT("E6 подготовка: эхо замечено, панель скрыта"));
		Feed(C, 5110, Xf5110());
		TArray<uint8> B = Imon5220First(); B[23] = (uint8)kXfU;   // сервер переписал номер Друга (+99018 ← номер Player52)
		Feed(C, 5220, B);
		After(C, 0.4f, &E6);
	}
	// ═══ E6: настоящий Друг ═══
	static void E6(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		const FString Name = Txt(C, TEXT("SOULMATE_TXT00_Text")), L1 = Txt(C, TEXT("SOULMATE_POP_LV01"));
		C->Expect(Sm && !Sm->IsSelfEchoSeen() && PopShown(C) && Name == TEXT("Player52") && L1 == TEXT("24") && Sm->HasSoulmatePair()
			&& Sm->GetMateCharId() == kXfCid && Sm->GetMateUserIdx() == kXfU,
			FString::Printf(TEXT("E6: 5110 Player52 — эхо снято, панель «%s»/«%s» (ждём Player52/24), Друг %d/%d"),
				*Name, *L1, Sm ? Sm->GetMateCharId() : -2, Sm ? Sm->GetMateUserIdx() : -2));
		C->NoticeSeq = Drops(C);
		Feed(C, 5111, Xf5111());
		After(C, 0.3f, &E6b);
	}
	static void E6b(FCtxRef C)
	{
		const float Hp = FillW(C, TEXT("SOULMATE_GAUGE02_15_1"));
		C->Expect(Drops(C) == C->NoticeSeq && FMath::IsNearlyEqual(Hp, 83.f * 3000.f / 7437.f, 1.f),
			FString::Printf(TEXT("E6: 5111 Друга принята — HP %.1f (ждём %.1f = 3000/7437)"), Hp, 83.f * 3000.f / 7437.f));
		// Сторона Player52: его вход и ТЕ ЖЕ байты 5110 Player17 — для него это настоящий Друг.
		SetSelf(C, kXfCid, kXfU);
		if (UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C)) { Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		Feed(C, 5220, Xf5220());
		Feed(C, 5110, Imon5110());
		Feed(C, 5111, Imon5111());
		After(C, 0.4f, &E7);
	}
	// ═══ E7: сторона Player52 ═══
	static void E7(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		const FString Name = Txt(C, TEXT("SOULMATE_TXT00_Text")), L1 = Txt(C, TEXT("SOULMATE_POP_LV01"));
		C->Expect(Sm && !Sm->IsSelfEchoSeen() && PopShown(C) && Name == TEXT("Player17") && L1 == TEXT("70") && Sm->GetMateCharId() == kImonCid,
			FString::Printf(TEXT("E7: сторона Player52 — панель «%s»/«%s» (ждём Player17/70), Друг %d (75214)"), *Name, *L1, Sm ? Sm->GetMateCharId() : -2));
		After(C, 0.1f, &EFinish);
	}

	static void EFinish(FCtxRef C)
	{
		UWorld* W = C->W.Get();
		{
			TArray<uint8> B = Xf5220();
			AsdaBytes::WrI32(&B[6], -1); B[25] = 0; B[33] = 0;
			Feed(C, 5220, B);   // «слота нет» — снять признак соулмейта у подсистемы навыков (как Finish самотеста панели)
		}
		if (UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C)) { Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		C->NetScope.Reset();
		AsdaSelfTestKit::Finish(W, kEchoCmd, C->bQuit, LogAsdaSmBarTest, TEXT("ЭХО-СЕБЯ"), C->Ok, C->Fail);
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kEchoCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kEchoCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		if (!Net || !Fx || !Sm) { AsdaSelfTestKit::Refused(W, kEchoCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W; C->bQuit = KA.bQuit;
		AsdaCVarSave::ArmWorldGuard(W, kEchoCmd);
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kEchoCmd);
		SetSelf(C, kImonCid, kImonU);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.PopFrom5220"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.SelfEchoGuard"), 1);
		for (int32 Id = 1; Id < 5000 && C->BuffA < 0; ++Id)
		{
			FString N, Ic, T; int32 Cell = 0;
			if (Fx->GetStateInfo(Id, N, Ic, Cell, T)) { C->BuffA = Id; }
		}
		C->Expect(C->BuffA >= 0, FString::Printf(TEXT("подготовка: id эффекта из stateinfotip %d"), C->BuffA));
		Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 400.0));
		Feed(C, 5220, Imon5220First());
		After(C, 0.4f, &E1);
	}
}

// Консольная команда самотеста «эха себя».
static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulmateSelfEchoSelfTest(
	TEXT("asda.Soulmate.SelfEchoSelfTest"),
	TEXT("Самотест «эха себя» (баг Player17: панель Друга со своим именем): настоящие пакеты пары Player17↔Player52, "
	     "эхо отброшено, настоящий Друг принят, сторона Player52, ключ отката. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaSmEchoTest::Run(A, W); }));

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulmateBarSelfTest(
	TEXT("asda.Soulmate.BarSelfTest"),
	TEXT("Самотест панели Друга 6500 (SM bar): 5220/5110/5111/5112, показ/скрытие, подсказки, ЛКМ = цель, 5226, смена карты, Alt+Z, "
	     "ключ отката. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaSmBarTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
