// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ТРЕТИЙ СТАТ ПОСЛЕ ЗАТОЧКИ +6» (08.11.2026, баг 8 пачки 17): `asda.SelfTest.FortOption [quit]`.
//   Баг тестеров: у сета Ruby Celestine (79216…79230) «Fort» (заточка) не добавляет третий стат.
//   Причина — СЕРВЕРНЫЕ данные: SowelCombine col49/col50 = −1 (лог «Recv_User_ShellItemUpgrade(10) ItemCode=79216
//   AddIndex=-1» на живом 01.11), поэтому `CharShellItemUpgrade_0` на ступени 6 не кладёт опцию в +41/+43 записи.
//   Разбор и ведомость — `Reports/batch17/set_fort/`. Клиент НЕ правится: тест доказывает, что клиент на пути
//   игрока покажет третий стат, как только сервер его выдаст (пары опций +29…+45, `item_buildStatTooltip@0xEC5300`
//   и инфо-панель 10000…10004).
//   Разделы (вещь в ячейке сумки, наведение и щелчок — настоящими событиями мыши Slate по живой геометрии, офлайн):
//     A. 79219 «Набедренник рубиновой небожительницы» +6 БЕЗ опции +41 (как выдаёт сервер сейчас): две строки
//        базовых опций (303, 325), строки 86 нет — так игрок и видит баг;
//     B. та же вещь +6 С опцией +41 = 86/5 (группа 6092 предложенной правки): три строки в подсказке,
//        третья — в инфо-панели (`INVEN_ArmoryOp4`) после щелчка по ячейке;
//     C. 79216 «Венец рубиновой небожительницы» +6 с опцией +41 = 323/670 (группа 6094): строка «+6%».
//   Кадры — `Reports/batch17/set_fort/frames/<язык>_<раздел>_{tip,info}.png`. Итог: «FORT-ОПЦИЯ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMarkup.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFortOptionTest, Log, All);

namespace AsdaFortOptionTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.FortOption");   // имя команды
	static const TCHAR* kPfx = TEXT("FORT-ОПЦИЯ");                 // префикс строк и итога
	static const TCHAR* kInfoOpt4 = TEXT("INVEN_ArmoryOp4");       // контрол инфо-панели 4-й пары (+41/+43)

	// Один раздел.
	struct FCase
	{
		FString Tag;                 // A/B/C — в имени кадра и строках
		int32 Code = 0;              // код предмета
		int32 Cell = 0;              // ячейка сумки (контейнер 1, страница 0)
		int32 Base0Type = 0, Base0Val = 0;   // пара +29/+31 (базовая опция 1)
		int32 Base1Type = 0, Base1Val = 0;   // пара +33/+35 (базовая опция 2)
		int32 FortType = 0, FortVal = 0;     // пара +41/+43 (опция ступени 6); 0 — нет
	};

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		TArray<FCase> Cases;                       // разделы
		TMap<int32, TArray<uint8>> SavedBag;       // ячейки сумки до прогона (пусто — была пустой)
		int32 SavedTab = 1;                        // вкладка до прогона
		FVector2D WorldPt = FVector2D::ZeroVector; // точка мира без интерфейса
		FString FrameDir;                          // куда класть кадры

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaFortOptionTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaFortOptionTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Центр живого прямоугольника узла.
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Наведение без щелчка.
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Нажатие / отпускание левой кнопки в точке. Инвентарь опрашивает кнопку по кадрам (`Mouse.bLmbDown/bLmbUp`
	//   в Tick окна), поэтому нажатие и отпускание шлются на РАЗНЫХ кадрах (см. RunCase).
	static void Press(const FVector2D& P, bool bDown)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication& App = FSlateApplication::Get();
		Hover(P);
		TSet<FKey> Held;
		if (bDown) { Held.Add(EKeys::LeftMouseButton); }
		const FPointerEvent Ev(0, P, P, Held, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		if (bDown) { App.ProcessMouseButtonDownEvent(nullptr, Ev); }
		else       { App.ProcessMouseButtonUpEvent(Ev); }
	}
	// Текст узла (TextBlock/RichTextBlock) без разметки.
	static FString NodeText(const UWidget* X)
	{
		if (const UTextBlock* TB = Cast<UTextBlock>(X))            { return TB->GetText().ToString(); }
		if (const URichTextBlock* RB = Cast<URichTextBlock>(X))    { return FAsdaMarkup::StripTags(RB->GetText().ToString()); }
		return FString();
	}
	// Весь видимый текст показанной плашки.
	static FString TipText(const FCtxRef& C)
	{
		UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		UAsdaTipPanel* P = T ? T->GetMainPanel() : nullptr;
		UUserWidget* Wd = P ? P->GetWidget() : nullptr;
		if (!T || !T->IsTooltipShown() || !Wd || !Wd->WidgetTree) { return FString(); }
		TArray<UWidget*> All;
		Wd->WidgetTree->GetAllWidgets(All);
		FString Out;
		for (UWidget* X : All)
		{
			if (X && AsdaUi::IsShown(X)) { const FString S = NodeText(X); if (!S.IsEmpty()) { Out += S + TEXT("\n"); } }
		}
		return Out;
	}
	// Текст контрола инфо-панели окна инвентаря ("" — скрыт/нет).
	static FString InfoText(const FCtxRef& C, const TCHAR* Name)
	{
		UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		UUserWidget* Wd = T ? T->GetInfoPanelWindow() : nullptr;
		UWidget* X = Wd ? Wd->GetWidgetFromName(FName(Name)) : nullptr;
		return (X && AsdaUi::IsShown(X)) ? NodeText(X) : FString();
	}
	// Строка опции так, как её собирает клиент (таблица itemoptiontable + оверлей языка), без разметки.
	static FString OptLine(const FCtxRef& C, int32 Type, int32 Val)
	{
		UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		return T ? FAsdaMarkup::StripTags(T->FormatOptionValue(Type, Val)).TrimStartAndEnd() : FString();
	}
	// Положить запись в ячейку (пусто — очистить).
	static void Put(UAsdaInventorySubsystem* Inv, int32 Cell, const TArray<uint8>& Rec)
	{
		Inv->ApplyExternalRecord(Rec.Num() == FAsdaItemRecordWriter::kSize ? Rec : FAsdaItemRecordWriter::ClearCell(1, Cell),
			UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}
	// Кадр.
	static void Shot(const FCtxRef& C, const FString& Tag)
	{
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), *Tag);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaFortOptionTest, Display, TEXT("%s кадр заказан → %s"), kPfx, *File);
	}

	static void Finish(const FCtxRef& C)
	{
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>())
		{
			for (const TPair<int32, TArray<uint8>>& P : C->SavedBag) { Put(I, P.Key, P.Value); }
			I->SetTab(C->SavedTab);
		}
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaFortOptionTest, kPfx, C->Ok, C->Fail);
	}

	static void RunCase(const FCtxRef& C, int32 Idx, float T0)
	{
		At(C, T0, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (I)
			{
				// Запись вещи после заточки до +6 — поля, которые кладёт сервер (`CharShellItemUpgrade_0`).
				FAsdaItemRecordWriter Rw;
				Rw.Code(K.Code).Container(1).Slot(K.Cell).Second(-1).Amount(1).Durability(100)
				  .GemGrade(6).Byte25(6).Byte26(8)
				  .EnchantPair(0, K.Base0Type, K.Base0Val).EnchantPair(1, K.Base1Type, K.Base1Val);
				if (K.FortType > 0) { Rw.EnchantPair(3, K.FortType, K.FortVal); }
				Put(I, K.Cell, Rw.ToArray());
				if (!I->IsWindowOpen()) { I->OpenWindow(); }
				I->SetTab(1);
			}
			Hover(C->WorldPt);
		});
		At(C, T0 + 0.25f, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			const bool bGeo = I && Center(I->DebugGetGridIcon(K.Cell), P);
			C->Expect(bGeo, FString::Printf(TEXT("%s0 ячейка %d сумки на экране (%.0f,%.0f)"), *K.Tag, K.Cell, P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, T0 + 0.75f, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			const FString Txt = TipText(C);
			const FString L0 = OptLine(C, K.Base0Type, K.Base0Val), L1 = OptLine(C, K.Base1Type, K.Base1Val);
			UE_LOG(LogAsdaFortOptionTest, Display, TEXT("%s %s код %d «%s» — текст подсказки:\n%s"), kPfx, *K.Tag, K.Code,
				IT ? *IT->GetItemName(K.Code) : TEXT("?"), *Txt);
			C->Expect(!L0.IsEmpty() && !L1.IsEmpty() && Txt.Contains(L0) && Txt.Contains(L1),
				FString::Printf(TEXT("%s1 подсказка: базовые опции «%s» и «%s» есть"), *K.Tag, *L0, *L1));
			if (K.FortType > 0)
			{
				const FString LF = OptLine(C, K.FortType, K.FortVal);
				C->Expect(!LF.IsEmpty() && Txt.Contains(LF),
					FString::Printf(TEXT("%s2 подсказка: третий стат (+41) «%s» есть"), *K.Tag, *LF));
			}
			else
			{
				const FString LF = OptLine(C, 86, 5);
				C->Expect(!Txt.Contains(LF), FString::Printf(TEXT("%s2 подсказка: третьего стата нет (как выдаёт сервер сейчас)"), *K.Tag));
			}
			Shot(C, K.Tag + TEXT("_tip"));
		});
		// Щелчок по ячейке — эталонный путь наполнения правой инфо-панели (item_inventory.c:36278).
		At(C, T0 + 1.15f, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			if (I && Center(I->DebugGetGridIcon(K.Cell), P)) { Press(P, true); }
		});
		At(C, T0 + 1.3f, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			if (I && Center(I->DebugGetGridIcon(K.Cell), P)) { Press(P, false); }
		});
		At(C, T0 + 1.8f, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			const FString Op4 = InfoText(C, kInfoOpt4).TrimStartAndEnd();
			if (K.FortType > 0)
			{
				const FString LF = OptLine(C, K.FortType, K.FortVal);
				C->Expect(!LF.IsEmpty() && Op4 == LF,
					FString::Printf(TEXT("%s3 инфо-панель: %s = «%s» (ждём «%s»)"), *K.Tag, kInfoOpt4, *Op4, *LF));
			}
			else
			{
				C->Expect(Op4.IsEmpty(), FString::Printf(TEXT("%s3 инфо-панель: %s пуст («%s»)"), *K.Tag, kInfoOpt4, *Op4));
			}
			Shot(C, K.Tag + TEXT("_info"));
		});
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		if (!Inv || !IT || !WM) { C->Expect(false, TEXT("подсистемы инвентаря/таблиц/окон есть")); Finish(C); return; }
		C->Expect(IT->GetItem(79219) && IT->GetItem(79216), TEXT("коды 79219 и 79216 есть в таблице предметов клиента"));

		// Значения — из живых серверных групп: 12946 (303/1330), 12947 (325/1476), 6092 (86/5), 12950 (300/670), 6094 (323/670).
		auto Mk = [](const TCHAR* Tag, int32 Code, int32 Cell, int32 T0, int32 V0, int32 T1, int32 V1, int32 TF, int32 VF)
		{
			FCase K; K.Tag = Tag; K.Code = Code; K.Cell = Cell;
			K.Base0Type = T0; K.Base0Val = V0; K.Base1Type = T1; K.Base1Val = V1; K.FortType = TF; K.FortVal = VF; return K;
		};
		C->Cases.Add(Mk(TEXT("A"), 79219, 0, 303, 1330, 325, 1476, 0, 0));
		C->Cases.Add(Mk(TEXT("B"), 79219, 1, 303, 1330, 325, 1476, 86, 5));
		C->Cases.Add(Mk(TEXT("C"), 79216, 2, 300, 670, 325, 1476, 323, 670));

		C->SavedTab = Inv->GetTab();
		for (int32 S = 0; S < 3; ++S) { const FAsdaItemInstance* It = Inv->GetCell(1, S); C->SavedBag.Add(S, (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>()); }

		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/batch17/set_fort/frames"));
		IFileManager::Get().MakeDirectory(*C->FrameDir, /*Tree*/ true);
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid())
		{
			const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
			for (const FVector2D F : { FVector2D(0.5f, 0.22f), FVector2D(0.3f, 0.3f), FVector2D(0.7f, 0.3f), FVector2D(0.5f, 0.4f) })
			{
				const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * F);
				FName Zone;
				if (!WM->IsScreenPointOverAnyUiWindow(P, Zone)) { C->WorldPt = P; break; }
			}
		}
		Inv->OpenWindow();

		float T = 0.6f;
		for (int32 i = 0; i < C->Cases.Num(); ++i) { RunCase(C, i, T); T += 2.5f; }
		At(C, T + 0.3f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestFortOption(
	TEXT("asda.SelfTest.FortOption"),
	TEXT("Самотест бага 8 пачки 17: третий стат после заточки +6 (пара +41/+43) у Ruby Celestine — без опции (как сейчас на сервере) "
	     "две строки, с опцией — третья строка в подсказке и в инфо-панели (INVEN_ArmoryOp4). Кадры в Reports/batch17/set_fort/frames. "
	     "Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaFortOptionTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
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
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
