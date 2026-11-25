// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ «ЧИСЛО КОЛИЧЕСТВА В ЯЧЕЙКАХ ПРЕДМЕТОВ» ПУТЁМ ИГРОКА (26.10.2026, баг игроков «число стопки плохо видно»). [ПОРТ]
//
// ЧТО ДЕЛАЕТ. Кладёт в сумку/склад набор стопок (светлые и тёмные иконки, колчаны, зелья, материалы, знаки; 1, 2, 99, 999, 9999),
//   по очереди открывает окна боевыми путями — сумка (обе вкладки), панель быстрого доступа, склад, магазин NPC, отчёт охоты,
//   передача параметров, улучшение, коробка, обмен игрок-игрок (5283/5285 штатным приёмом), лавка продавца (6301) и витрина
//   хозяина (6303) — снимает кадр каждого окна со слоем UI и проходит ЖИВЫЕ виджеты: у каждого видимого непустого узла-числа
//   (перечень — как в шаге `Tools/asda_ui_item_count_outline.py` + код-окна сумки/хотбара) пишет текст, обводку, цвет, тень и
//   экранный прямоугольник. Проверки: число стоит там, где ждём (сумка, хотбар, склад), и у всех видимых чисел обводка 1 px чёрная.
//   Режим `legacy` — ключ `asda.UI.ItemCountOutline 0` (как было в порте) — для кадров «до»: проверка обводки не делается.
//
// Консоль: `asda.SelfTest.ItemCount [res=1024x768] [tag=after] [legacy] [delay=N] [quit]`. Только офлайн (-AsdaNoNet).
//   Кадры: `Reports/item_count/<tag>_<res>_<окно>.png`. Строки — «САМОТЕСТ-ЧИСЛО», итог — «САМОТЕСТ-ЧИСЛО ИТОГ».
//   Сумка/склад, свой номер/charId/уровень и ключи возвращаются в конце; слоты хотбара, занятые тестом, снимаются.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaDevConsole.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Login/AsdaLoginData.h"
#include "UI/AsdaExchangeP2PSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaSelfStoreSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Regex.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemCountTest, Log, All);

namespace AsdaItemCountTestLocal
{
	// Узлы-числа количества: окно (класс WBP) и regex имени. Совпадает с `Tools/asda_ui_item_count_outline.py` (ассет)
	//   плюс окна, где шрифт числа ставит код (сумка, хотбар).
	struct FNodeRule { const TCHAR* Wbp; const TCHAR* Rx; };
	static const FNodeRule kRules[] = {
		{ TEXT("WBP_INVEN_WIN"),            TEXT("^INVEN_SET\\d+$") },
		{ TEXT("WBP_QUICK_NEW_WIN"),        TEXT("^QUICK_NEW_SLOT(\\d+|_WEAPON|_T\\d+)$") },
		{ TEXT("WBP_WARE_WIN"),             TEXT("^(WAREKEEP_TRAY\\d\\d|WAREBAG\\d_TRAY\\d\\d)$") },
		{ TEXT("WBP_TRAN_WIN"),             TEXT("^TRAN_TRAY\\d\\d$") },
		{ TEXT("WBP_StreetStall_Open_Win"), TEXT("^(Stall_Open_Inven\\d\\d|Stall_Open_SellIcon\\d\\d)$") },
		{ TEXT("WBP_StreetStall_Win"),      TEXT("^(Stall_SellIcon\\d\\d|Stall_PurItem\\d\\d)$") },
		{ TEXT("WBP_NPCSHOP_WIN"),          TEXT("^(NPCSHOP_TRAY\\d\\d|NPCSHOP_BTRAY\\d\\d)$") },
		{ TEXT("WBP_EXTRACT_WIN"),          TEXT("^EXTRACT_Scroll\\d\\d$") },
		{ TEXT("WBP_Item_Upgrade_Win"),     TEXT("^(Item_Ugrade_Sub[01]_\\d+|Item_Ugrade_Scroll\\d\\d)$") },
		{ TEXT("WBP_Hunt_report_Win"),      TEXT("^Hunt_report_Mark_\\d+$") },
		{ TEXT("WBP_FISH_BOOK_WIN"),        TEXT("^(FISH_BOOK_JOINBOX_M\\d+|FISH_BOOK_BIG_BOX_3_MARK_.*)$") },
		{ TEXT("WBP_QUEST_WIN"),            TEXT("^QUEST_TRAY\\d$") },
		{ TEXT("WBP_EPISODEPOP_WIN"),       TEXT("^EPISODEPOP_ITEMICON\\d\\d$") },
		{ TEXT("WBP_QUEST_Confirm_Win"),    TEXT("^QUEST_Confirm_Text_T[12]_MARK\\d+$") },
		{ TEXT("WBP_Pet_LimitBreak_Win"),   TEXT("^Pet_LimitBreak_SETMARK[23]$") },
		{ TEXT("WBP_OTHERCHARA_DATA"),      TEXT("^OTHERCHARA_EQUIP\\d\\d$") },
		{ TEXT("WBP_Package_Contents_Win"), TEXT("^Package_(BagIcon\\d|BagItemIcon\\d_\\d|ItemIcon\\d_+\\d(_\\d)?)$") },
	};

	// Стопка набора: код, количество, зачем.
	struct FStack { int32 Code; int32 Amount; const TCHAR* Why; };
	// Расходники (контейнер 2, ячейки по порядку): светлые флаконы/зелья, тёмная руда/камни, колчаны, знаки, детали; 1/2/99/999/9999.
	static const FStack kSpend[] = {
		{ 20571, 2,    TEXT("простое зелье здоровья (красное)") },
		{ 20577, 99,   TEXT("малое зелье маны (синее)") },
		{ 20929, 999,  TEXT("стеклянный флакон (СВЕТЛЫЙ)") },
		{ 20930, 9999, TEXT("хрустальный флакон (СВЕТЛЫЙ)") },
		{ 20923, 2,    TEXT("драгоценный камень D") },
		{ 20956, 99,   TEXT("сияющая руда") },
		{ 20957, 999,  TEXT("сияющий кристалл") },
		{ 20927, 9999, TEXT("смазка") },
		{ 20562, 2,    TEXT("колчан (FlagWord176 = 300)") },
		{ 20565, 99,   TEXT("колчан для арбалета") },
		{ 22647, 2,    TEXT("знак волка (отчёт охоты)") },
		{ 22648, 99,   TEXT("знак гриба (отчёт охоты)") },
		{ 22649, 999,  TEXT("знак кота (отчёт охоты)") },
		{ 20850, 2,    TEXT("рукоять меча (деталь)") },
		{ 20994, 99,   TEXT("куски железной пластины") },
		{ 20912, 999,  TEXT("стопка карточек") },
		{ 20633, 9999, TEXT("особая монета (событие)") },
		{ 20796, 99,   TEXT("частичка ночи (неторгуемая)") },
		{ 20572, 1,    TEXT("зелье здоровья ×1 (контроль «1»)") },
		{ 20589, 2,    TEXT("сильное зелье здоровья") },
		{ 20590, 99,   TEXT("сильное зелье маны") },
		{ 20578, 999,  TEXT("зелье маны") },
	};

	// Ячейки набора, которые панель быстрого доступа принимает (зелья): 20571×2, 20577×99, 20572×1, 20589×2, 20590×99, 20578×999.
	static const int32 kHotbarCells[] = { 0, 1, 18, 19, 20, 21 };

	// Подать пакет штатным приёмом.
	static void Feed(UAsdaNetSubsystem* Net, int32 Op, const TArray<uint8>& B) { if (Net) { Net->DebugFeedPacket(Op, B); } }
	// Тело 5283 (24 б): [0] код, [1..2] userNum инициатора, [3..22] имя cp1251, [23] тип (как самотест О4).
	static TArray<uint8> Body5283(int32 From, const FString& Name)
	{
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = 1; AsdaBytes::WrI16(B.GetData() + 1, From);
		FAsdaLoginData::EncodeCp1251(Name, B.GetData() + 3, 19);
		B[23] = 0;
		return B;
	}
	// Тело 5285 (50 б): [0] код, [1] тип, [2..5] windowIndex, [6..7]/[8..27] сторона 1, [28..29]/[30..49] сторона 2.
	static TArray<uint8> Body5285(int32 Idx, int32 N1, const FString& S1, int32 N2, const FString& S2)
	{
		TArray<uint8> B; B.SetNumZeroed(50);
		B[0] = 1; B[1] = 0;
		AsdaBytes::WrI32(B.GetData() + 2, Idx);
		AsdaBytes::WrI16(B.GetData() + 6, N1);
		FAsdaLoginData::EncodeCp1251(S1, B.GetData() + 8, 19);
		AsdaBytes::WrI16(B.GetData() + 28, N2);
		FAsdaLoginData::EncodeCp1251(S2, B.GetData() + 30, 19);
		return B;
	}

	// Итог обхода живых узлов одного окна.
	struct FScan
	{
		int32 Found = 0;      // узлов-чисел в живых окнах
		int32 Shown = 0;      // из них видимых по цепочке и с непустым текстом
		int32 Outlined = 0;   // из видимых — с обводкой 1 px чёрной
		int32 AllOutlined = 0;   // из всех найденных (и пустых) — с обводкой 1 px чёрной (свойство ассета/кода)
		int32 AllComp = 0;       // из всех найденных — со сдвигом отрисовки по обводке (dy = −обводка; Н3 ревизии wave_1026)
		TMap<FString, FString> Text;   // имя узла → текст (видимые непустые)
		FString Bad;          // видимые непустые без обводки
	};

	// Узел и все родители видимы.
	static bool ShownChain(const UWidget* W)
	{
		for (const UWidget* P = W; P; P = P->GetParent())
		{
			if (!P->IsVisible()) { return false; }
		}
		return true;
	}

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		bool bLegacy = false;
		float Delay = 0.f;
		FString Res;          // «1024x768» и т.п. (пусто — не менять)
		FString Tag = TEXT("after");
		FString Dir;
		int32 Ok = 0, Fail = 0;
		TArray<TFunction<float()>> Steps;   // шаг возвращает паузу до следующего, с; < 0 — снимок (ждать кадра)
		int32 Index = 0;
		double NextTime = 0.0;
		bool bAwaitShot = false;
		int32 FramesAfterShot = 0;
		TArray<TArray<uint8>> Saved1, Saved2, Saved5;   // сумка/склад до теста (сырые записи 57 б; пусто — ячейка была пуста)
		TArray<int32> HotbarSlots;   // слоты хотбара, занятые тестом
		TUniquePtr<FAsdaNetSelfScope> Self;   // свой номер/charId/уровень — вернуть
		TUniquePtr<FAsdaCVarScope> CVars;     // ключи — вернуть
		int32 Special97 = 0, Special98 = 0, SpecialA = 0, SpecialB = 0;   // коды второй таблицы для вкладки «Снаряжение»/улучшения

		void Expect(bool b, const FString& What, const FString& Det)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaItemCountTest, Log, TEXT("САМОТЕСТ-ЧИСЛО %s: %s — %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *What, *Det);
		}
		template<class T> T* Sub() const
		{
			UWorld* Wd = W.Get();
			UGameInstance* GI = Wd ? Wd->GetGameInstance() : nullptr;
			return GI ? GI->GetSubsystem<T>() : nullptr;
		}
		void Exec(const FString& Cmd) const
		{
			UE_LOG(LogAsdaItemCountTest, Log, TEXT("САМОТЕСТ-ЧИСЛО exec: %s"), *Cmd);
			if (GEngine && W.IsValid()) { GEngine->Exec(W.Get(), *Cmd); }
		}
		// Обход живых узлов-чисел окна `Wbp` (пусто — всех окон набора).
		FScan Scan(const TCHAR* Stage, const TCHAR* Wbp) const
		{
			FScan R;
			UWorld* Wd = W.Get();
			const int32 WantPx = bLegacy ? 0 : 1;
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* UW = *It;
				if (!UW || UW->GetWorld() != Wd || !UW->WidgetTree || !UW->IsInViewport() || !UW->IsVisible()) { continue; }
				const FString Cls = UW->GetClass()->GetName();
				for (const FNodeRule& Rule : kRules)
				{
					if (Wbp && FCString::Strcmp(Rule.Wbp, Wbp) != 0) { continue; }
					if (Cls != FString(Rule.Wbp) + TEXT("_C")) { continue; }
					const FRegexPattern Pat(Rule.Rx);
					UW->WidgetTree->ForEachWidget([&](UWidget* Wg)
					{
						UTextBlock* Tb = Cast<UTextBlock>(Wg);
						if (!Tb) { return; }
						FRegexMatcher M(Pat, Tb->GetName());
						if (!M.FindNext()) { return; }
						++R.Found;
						{
							const FFontOutlineSettings& O = Tb->GetFont().OutlineSettings;
							if (O.OutlineSize == 1 && O.OutlineColor.R < 0.01f && O.OutlineColor.G < 0.01f && O.OutlineColor.B < 0.01f) { ++R.AllOutlined; }
							if (FMath::IsNearlyEqual(Tb->GetRenderTransform().Translation.Y, -(float)O.OutlineSize)) { ++R.AllComp; }
						}
						const FString Txt = Tb->GetText().ToString();
						if (Txt.IsEmpty() || !ShownChain(Tb)) { return; }
						++R.Shown;
						R.Text.Add(Tb->GetName(), Txt);
						const FSlateFontInfo F = Tb->GetFont();
						const FLinearColor OC = F.OutlineSettings.OutlineColor;
						const bool bBlack = OC.R < 0.01f && OC.G < 0.01f && OC.B < 0.01f && OC.A > 0.99f;
						const bool bGood = F.OutlineSettings.OutlineSize == WantPx && (WantPx == 0 || bBlack);
						if (F.OutlineSettings.OutlineSize >= 1 && bBlack) { ++R.Outlined; }
						if (!bGood) { R.Bad += FString::Printf(TEXT("%s(обв %d) "), *Tb->GetName(), F.OutlineSettings.OutlineSize); }
						FVector2D Mn(0, 0), Mx(0, 0);
						AsdaUi::WidgetAbsRect(Tb, Mn, Mx);
						const FLinearColor Sh = Tb->GetShadowColorAndOpacity();
						UE_LOG(LogAsdaItemCountTest, Log,
							TEXT("САМОТЕСТ-ЧИСЛО узел [%s] %s/%s «%s» обводка=%d цвет=(%.2f,%.2f,%.2f,%.2f) тень-альфа=%.2f кегль=%d сдвиг=(%.0f,%.0f) прямоугольник=(%.1f,%.1f)-(%.1f,%.1f)"),
							Stage, Rule.Wbp, *Tb->GetName(), *Txt, F.OutlineSettings.OutlineSize, OC.R, OC.G, OC.B, OC.A, Sh.A,
							(int32)F.Size, Tb->GetRenderTransform().Translation.X, Tb->GetRenderTransform().Translation.Y, Mn.X, Mn.Y, Mx.X, Mx.Y);
					});
				}
			}
			UE_LOG(LogAsdaItemCountTest, Log, TEXT("САМОТЕСТ-ЧИСЛО окно [%s] %s: узлов %d (с обводкой %d), видимых чисел %d, с обводкой %d"),
				Stage, Wbp ? Wbp : TEXT("*"), R.Found, R.AllOutlined, R.Shown, R.Outlined);
			return R;
		}
		// Общая проверка окна: окно живо (узлы найдены), есть видимые числа, и у всех обводка (кроме режима legacy).
		void CheckWindow(const TCHAR* Stage, const TCHAR* Wbp, bool bExpectNumbers)
		{
			const FScan R = Scan(Stage, Wbp);
			Expect(R.Found > 0, FString::Printf(TEXT("[%s] окно %s открыто, узлы-числа найдены"), Stage, Wbp),
				FString::Printf(TEXT("узлов %d"), R.Found));
			if (bExpectNumbers)
			{
				Expect(R.Shown > 0, FString::Printf(TEXT("[%s] в окне видны числа количества"), Stage),
					FString::Printf(TEXT("видимых %d"), R.Shown));
			}
			if (!bLegacy && R.Found > 0)
			{
				Expect(R.AllOutlined == R.Found, FString::Printf(TEXT("[%s] у всех узлов-чисел окна (и пустых) обводка 1 px чёрная"), Stage),
					FString::Printf(TEXT("узлов %d, с обводкой %d"), R.Found, R.AllOutlined));
				Expect(R.AllComp == R.Found, FString::Printf(TEXT("[%s] у всех узлов-чисел сдвиг отрисовки −1 по Y (заливка цифры на месте эталона)"), Stage),
					FString::Printf(TEXT("узлов %d, со сдвигом %d"), R.Found, R.AllComp));
			}
			if (!bLegacy && R.Shown > 0)
			{
				Expect(R.Bad.IsEmpty() && R.Outlined == R.Shown,
					FString::Printf(TEXT("[%s] у всех видимых чисел обводка 1 px чёрная"), Stage),
					FString::Printf(TEXT("видимых %d, с обводкой %d; без: %s"), R.Shown, R.Outlined, R.Bad.IsEmpty() ? TEXT("—") : *R.Bad));
			}
		}
		// Шаг-снимок.
		void AddShot(const TCHAR* Name)
		{
			const FString N = Name;
			Steps.Add([this, N]() -> float
			{
				const FString File = Dir / FString::Printf(TEXT("%s_%s_%s.png"), *Tag, Res.IsEmpty() ? TEXT("cur") : *Res, *N);
				FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
				UE_LOG(LogAsdaItemCountTest, Log, TEXT("САМОТЕСТ-ЧИСЛО кадр → %s"), *File);
				return -1.f;
			});
		}
	};

	// Сохранить контейнер `C` (Num ячеек) сырыми записями.
	static void SaveCont(UAsdaInventorySubsystem* Inv, int32 C, TArray<TArray<uint8>>& Out)
	{
		Out.Reset();
		const int32 N = Inv->GetContainerSize(C);
		for (int32 i = 0; i < N; ++i)
		{
			const FAsdaItemInstance* I = Inv->GetCell(C, i);
			Out.Add((I && !I->IsEmpty()) ? I->Raw : TArray<uint8>());
		}
	}
	// Вернуть контейнер.
	static void RestoreCont(UAsdaInventorySubsystem* Inv, int32 C, const TArray<TArray<uint8>>& In)
	{
		for (int32 i = 0; i < In.Num(); ++i)
		{
			if (In[i].Num() >= 57) { Inv->ApplyExternalRecord(In[i], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
			else { Inv->ClearCell(C, i); }
		}
	}

	static void Build(TSharedRef<FCtx> C);

	static void Finish(TSharedRef<FCtx> C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		if (UAsdaExchangeP2PSubsystem* X = C->Sub<UAsdaExchangeP2PSubsystem>()) { X->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaSelfStoreSubsystem* S = C->Sub<UAsdaSelfStoreSubsystem>()) { S->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(TEXT("самотест числа")); WM->CloseAll(); }
		for (const int32 Sl : C->HotbarSlots) { C->Exec(FString::Printf(TEXT("asda.Quick.Clear %d"), Sl)); }
		if (Inv)
		{
			RestoreCont(Inv, 1, C->Saved1);
			RestoreCont(Inv, 2, C->Saved2);
			RestoreCont(Inv, 5, C->Saved5);
		}
		C->Self.Reset();
		C->CVars.Reset();
		AsdaSelfTestKit::Finish(C->W.Get(), TEXT("asda.SelfTest.ItemCount"), C->bQuit, LogAsdaItemCountTest, TEXT("САМОТЕСТ-ЧИСЛО"),
			C->Ok, C->Fail, *FString::Printf(TEXT(" (режим %s, %s)"), C->bLegacy ? TEXT("legacy") : TEXT("обводка"), *C->Res));
	}

	static void Build(TSharedRef<FCtx> C)
	{
		FCtx* P = &C.Get();
		// ── 0. разрешение ──
		P->Steps.Add([P]() -> float
		{
			// Свой персонаж офлайн: панель быстрого доступа живёт только в мире с персонажем (тик хотбара ждёт `LocalPlayerActor`).
			if (AsdaSelfTestKit::NeedSelfSpawn(P->W.Get()))
			{
				P->Exec(TEXT("Asda.Load.CurrentMap 3"));
				P->Exec(TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
			if (!P->Res.IsEmpty()) { P->Exec(FString::Printf(TEXT("r.SetRes %sw"), *P->Res)); }
			return 3.0f;
		});
		// ── 1. подготовка: ключ, свой персонаж, набор стопок, пересборка окон сумки/хотбара под ключ ──
		P->Steps.Add([P]() -> float
		{
			UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
			UAsdaNetSubsystem* Net = P->Sub<UAsdaNetSubsystem>();
			UAsdaItemTableSubsystem* IT = P->Sub<UAsdaItemTableSubsystem>();
			UAsdaHotbarSubsystem* Hb = P->Sub<UAsdaHotbarSubsystem>();
			if (!Inv || !Net || !IT || !Hb) { P->Expect(false, TEXT("подсистемы сумки/сети/таблиц/хотбара"), TEXT("нет")); return 0.f; }
			P->CVars = MakeUnique<FAsdaCVarScope>(P->W.Get(), TEXT("asda.SelfTest.ItemCount"));
			P->CVars->Set(TEXT("asda.UI.ItemCountOutline"), P->bLegacy ? 0 : 1);
			P->Self = MakeUnique<FAsdaNetSelfScope>(Net, TEXT("asda.SelfTest.ItemCount"));
			Net->SelectedCharId = 424242;
			Net->SetSelfUserNumForDebug(500);
			Net->SelfLevel = 30;
			SaveCont(Inv, 1, P->Saved1); SaveCont(Inv, 2, P->Saved2); SaveCont(Inv, 5, P->Saved5);
			for (int32 i = 0; i < Inv->GetContainerSize(2); ++i) { Inv->DebugPutItem(2, i, 0, 0); }
			for (int32 i = 0; i < Inv->GetContainerSize(1); ++i) { Inv->DebugPutItem(1, i, 0, 0); }
			for (int32 i = 0; i < Inv->GetContainerSize(5); ++i) { Inv->DebugPutItem(5, i, 0, 0); }
			for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(kSpend); ++i)
			{
				Inv->DebugPutItem(2, i, kSpend[i].Code, kSpend[i].Amount);
				if (i < Inv->GetContainerSize(5)) { Inv->DebugPutItem(5, i, kSpend[i].Code, kSpend[i].Amount); }
			}
			// Вторая таблица (premium/бустеры): вкладка «Снаряжение» печатает их количество; 97/98 — помощники окна улучшения.
			TArray<int32> Any;
			for (int32 Code = 1; Code < 200000 && (Any.Num() < 4 || !P->Special97 || !P->Special98); ++Code)
			{
				if (!IT->IsSpecialItemCode(Code)) { continue; }
				FAsdaSpecialItemlistRow Sp;
				if (!IT->GetSpecialItem(Code, Sp)) { continue; }
				if (Sp.SubType == 97 && !P->Special97) { P->Special97 = Code; continue; }
				if (Sp.SubType == 98 && !P->Special98) { P->Special98 = Code; continue; }
				if (Any.Num() < 4) { Any.Add(Code); }
			}
			const int32 kEqAmt[] = { 2, 99, 999, 9999 };
			int32 Cell = 0;
			for (int32 k = 0; k < Any.Num(); ++k) { Inv->DebugPutItem(1, Cell++, Any[k], kEqAmt[k]); }
			if (P->Special97) { Inv->DebugPutItem(1, Cell++, P->Special97, 99); }
			if (P->Special98) { Inv->DebugPutItem(1, Cell++, P->Special98, 2); }
			Inv->DebugPutItem(1, Cell++, 20552, 1);   // обычная одежда — числа нет (эталон)
			P->SpecialA = Any.Num() > 0 ? Any[0] : 0;
			UE_LOG(LogAsdaItemCountTest, Log, TEXT("САМОТЕСТ-ЧИСЛО набор: расходников %d, вторая таблица %d (97=%d, 98=%d)"),
				(int32)UE_ARRAY_COUNT(kSpend), Any.Num(), P->Special97, P->Special98);
			// Ключ читают раскладки окон: сумку пересобрать (окно уходит из вьюпорта → `EnsureWidget` соберёт заново), хотбар — перестилить.
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* UW = *It;
				if (UW && UW->GetWorld() == P->W.Get() && UW->IsInViewport() && UW->GetClass()->GetName() == TEXT("WBP_INVEN_WIN_C"))
				{
					UW->RemoveFromParent();
				}
			}
			Hb->RefreshStackTextStyle();
			Inv->OpenWindow();
			Inv->SetTab(0);
			return 1.5f;
		});
		// ── 2. сумка, «Расходники» ──
		P->AddShot(TEXT("inv_spend"));
		P->Steps.Add([P]() -> float
		{
			const FScan R = P->Scan(TEXT("inv_spend"), TEXT("WBP_INVEN_WIN"));
			FString Det; bool bAll = true;
			for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(kSpend) && i < UAsdaInventorySubsystem::kGridCells; ++i)   // ★ 29.10.2026: 60 ячеек
			{
				const FString Want = FString::FromInt(kSpend[i].Amount);
				const FString* Got = R.Text.Find(FString::Printf(TEXT("INVEN_SET%d"), i));
				const bool b = Got && *Got == Want;
				bAll &= b;
				if (!b) { Det += FString::Printf(TEXT("%d:%d≠«%s» "), i, kSpend[i].Code, Got ? **Got : TEXT("")); }
			}
			P->Expect(bAll, TEXT("[inv_spend] у каждой стопки сумки число = количеству (2/99/999/9999, «1» — как эталон)"), Det.IsEmpty() ? TEXT("все") : Det);
			P->CheckWindow(TEXT("inv_spend"), TEXT("WBP_INVEN_WIN"), true);
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->SetTab(1); }
			return 1.0f;
		});
		// ── 3. сумка, «Снаряжение» ──
		P->AddShot(TEXT("inv_equip"));
		P->Steps.Add([P]() -> float
		{
			P->CheckWindow(TEXT("inv_equip"), TEXT("WBP_INVEN_WIN"), P->SpecialA != 0);
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->SetTab(0); }
			// Хотбар: применяемые стопки (зелья; материалы панель не принимает — нотис 2004) в слоты 0…5 путём перетаскивания.
			for (int32 k = 0; k < (int32)UE_ARRAY_COUNT(kHotbarCells); ++k)
			{
				P->Exec(FString::Printf(TEXT("asda.Inven.ToHotbar 2 %d %d"), kHotbarCells[k], k));
				P->HotbarSlots.Add(k);
			}
			return 1.0f;
		});
		// ── 4. панель быстрого доступа ──
		P->AddShot(TEXT("hotbar"));
		P->Steps.Add([P]() -> float
		{
			const FScan R = P->Scan(TEXT("hotbar"), TEXT("WBP_QUICK_NEW_WIN"));
			FString Det; bool bAll = true;
			for (int32 k = 0; k < (int32)UE_ARRAY_COUNT(kHotbarCells); ++k)
			{
				const FString Want = FString::FromInt(kSpend[kHotbarCells[k]].Amount);
				const FString* Got = R.Text.Find(FString::Printf(TEXT("QUICK_NEW_SLOT%d"), k + 1));
				const bool b = Got && *Got == Want;
				bAll &= b;
				if (!b) { Det += FString::Printf(TEXT("слот%d:%s≠«%s» "), k, *Want, Got ? **Got : TEXT("")); }
			}
			P->Expect(bAll, TEXT("[hotbar] число стопки в слотах 1…6 = количеству (2/99/1/2/99/999)"), Det.IsEmpty() ? TEXT("все") : Det);
			P->CheckWindow(TEXT("hotbar"), TEXT("WBP_QUICK_NEW_WIN"), true);
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			P->Exec(TEXT("asda.Warehouse.Open 0"));
			return 1.5f;
		});
		// ── 5. склад ──
		P->AddShot(TEXT("ware"));
		P->Steps.Add([P]() -> float
		{
			const FScan R = P->Scan(TEXT("ware"), TEXT("WBP_WARE_WIN"));
			int32 Hit = 0;
			for (int32 i = 0; i < 20; ++i)
			{
				const FString* Got = R.Text.Find(FString::Printf(TEXT("WAREKEEP_TRAY%02d"), i));
				if (Got && *Got == FString::FromInt(kSpend[i].Amount)) { ++Hit; }
			}
			P->Expect(Hit >= 15, TEXT("[ware] числа стопок склада на месте (ячейки 0…19)"), FString::Printf(TEXT("совпало %d из 20"), Hit));
			P->CheckWindow(TEXT("ware"), TEXT("WBP_WARE_WIN"), true);
			P->Exec(TEXT("asda.Warehouse.Close"));
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			P->Exec(TEXT("asda.Shop.Open 6 1"));
			return 1.5f;
		});
		// ── 6. магазин NPC: продажа (список сумки с числами) и покупка ──
		P->AddShot(TEXT("npcshop_sell"));
		P->Steps.Add([P]() -> float
		{
			P->CheckWindow(TEXT("npcshop_sell"), TEXT("WBP_NPCSHOP_WIN"), true);
			P->Exec(TEXT("asda.Shop.Close"));
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			P->Exec(TEXT("asda.Shop.Open 5 3"));
			return 1.5f;
		});
		P->AddShot(TEXT("npcshop_buy"));
		P->Steps.Add([P]() -> float
		{
			P->CheckWindow(TEXT("npcshop_buy"), TEXT("WBP_NPCSHOP_WIN"), false);
			P->Exec(TEXT("asda.Shop.Close"));
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			P->Exec(TEXT("asda.HuntReport.Open"));
			return 1.5f;
		});
		// ── 7. отчёт охоты (знаки 22647…22649) ──
		P->AddShot(TEXT("huntreport"));
		P->Steps.Add([P]() -> float
		{
			P->CheckWindow(TEXT("huntreport"), TEXT("WBP_Hunt_report_Win"), true);
			P->Exec(TEXT("asda.HuntReport.Close"));
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			P->Exec(TEXT("asda.Extract.Open"));
			return 1.5f;
		});
		// ── 8. передача параметров ──
		P->AddShot(TEXT("extract"));
		P->Steps.Add([P]() -> float
		{
			P->CheckWindow(TEXT("extract"), TEXT("WBP_EXTRACT_WIN"), false);
			P->Exec(TEXT("asda.Extract.Close"));
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			P->Exec(TEXT("asda.Upgrade.Window"));
			return 1.5f;
		});
		// ── 9. улучшение (помощники 97/98) ──
		P->AddShot(TEXT("upgrade"));
		P->Steps.Add([P]() -> float
		{
			P->CheckWindow(TEXT("upgrade"), TEXT("WBP_Item_Upgrade_Win"), false);
			P->Exec(TEXT("asda.Upgrade.Window"));
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			P->Exec(TEXT("asda.Package.Show 20816 0"));
			return 1.5f;
		});
		// ── 10. коробка ──
		P->AddShot(TEXT("package"));
		P->Steps.Add([P]() -> float
		{
			P->CheckWindow(TEXT("package"), TEXT("WBP_Package_Contents_Win"), false);
			UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>();
			if (WM) { WM->CloseAll(); }
			// Обмен: заявка партнёра 5283 → значок → «Принять» → окно 5285 (как самотест О4).
			UAsdaNetSubsystem* Net = P->Sub<UAsdaNetSubsystem>();
			UAsdaExchangeP2PSubsystem* X = P->Sub<UAsdaExchangeP2PSubsystem>();
			UAsdaHotbarSubsystem* Hb = P->Sub<UAsdaHotbarSubsystem>();
			UAsdaWorldSubsystem* WS = P->Sub<UAsdaWorldSubsystem>();
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->CloseWindow(); }   // гейт 5032 «открыт рюкзак»
			if (Net && X && Hb && WS && WM)
			{
				WS->DebugSpawnFakePeer(902, TEXT("Партнёр"), FVector2D(25710.f, 25700.f));
				Feed(Net, 5283, Body5283(902, TEXT("Партнёр"))); X->Tick(0.f);
				Hb->HandleNoticeIconClick(4);
				WM->AnswerMsgBox(true, true);
				Feed(Net, 5285, Body5285(101, 902, TEXT("Партнёр"), 500, TEXT("Я"))); X->Tick(0.f);
			}
			return 1.5f;
		});
		// ── 11. обмен ──
		P->AddShot(TEXT("trade"));
		P->Steps.Add([P]() -> float
		{
			P->CheckWindow(TEXT("trade"), TEXT("WBP_TRAN_WIN"), true);
			if (UAsdaExchangeP2PSubsystem* X = P->Sub<UAsdaExchangeP2PSubsystem>()) { X->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(TEXT("самотест числа")); WM->CloseAll(); }
			// Лавка продавца: 6301 код 1 → окно 22000 (сетка сумки).
			UAsdaSelfStoreSubsystem* S = P->Sub<UAsdaSelfStoreSubsystem>();
			UAsdaNetSubsystem* Net = P->Sub<UAsdaNetSubsystem>();
			if (S && Net)
			{
				Net->SetSelfUserNumForDebug(17);
				S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
				TArray<uint8> B; B.Add(1);
				Feed(Net, 6301, B); S->PumpPending();
			}
			return 1.5f;
		});
		// ── 12. лавка продавца ──
		P->AddShot(TEXT("stall_sell"));
		P->Steps.Add([P]() -> float
		{
			P->CheckWindow(TEXT("stall_sell"), TEXT("WBP_StreetStall_Open_Win"), true);
			// 6303 код 1 → витрина хозяина (лоты: 20577 остаток 99, 20929 остаток 999, 20571 остаток 2).
			UAsdaSelfStoreSubsystem* S = P->Sub<UAsdaSelfStoreSubsystem>();
			UAsdaNetSubsystem* Net = P->Sub<UAsdaNetSubsystem>();
			if (S && Net)
			{
				TArray<uint8> R; R.SetNumZeroed(452); R[0] = 1; R[1] = 3;
				const int32 Lots[3][3] = { { 20577, 1, 99 }, { 20929, 2, 999 }, { 20571, 0, 2 } };   // код, слот, количество
				for (int32 k = 0; k < 3; ++k)
				{
					uint8* L = R.GetData() + 2 + 45 * k;
					AsdaBytes::WrI32(L + 0, Lots[k][0]); L[4] = 2; AsdaBytes::WrI16(L + 5, Lots[k][1]);
					AsdaBytes::WrI32(L + 7, Lots[k][2]); AsdaBytes::WrI32(L + 13, 1000);
				}
				Feed(Net, 6303, R); S->PumpPending();
			}
			return 1.5f;
		});
		// ── 13. витрина хозяина ──
		P->AddShot(TEXT("stall_show"));
		P->Steps.Add([P]() -> float
		{
			P->CheckWindow(TEXT("stall_show"), TEXT("WBP_StreetStall_Win"), true);
			return 0.2f;
		});
	}

	static FAutoConsoleCommandWithWorldAndArgs GCmdItemCount(
		TEXT("asda.SelfTest.ItemCount"),
		TEXT("Число количества в ячейках предметов путём игрока: сумка ×2, хотбар, склад, магазин, отчёт охоты, передача параметров, "
		     "улучшение, коробка, обмен, лавка ×2; кадры в Reports/item_count; проверка обводки 1 px. "
		     "`res=1024x768` `tag=after` `legacy` `delay=N` `quit`. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			if (!W) { return; }
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			static const uint16 kGuardOps[] = { 5067, 5282, 5284, 5286, 6300, 6302, 6305 };
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.SelfTest.ItemCount"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
			TSharedRef<FCtx> C = MakeShared<FCtx>();
			C->W = W;
			C->bQuit = KitArgs.bQuit;
			C->Delay = KitArgs.Delay;
			C->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/item_count"));
			for (const FString& A : KitArgs.Pos)
			{
				if (A.StartsWith(TEXT("res="))) { C->Res = A.Mid(4); }
				else if (A.StartsWith(TEXT("tag="))) { C->Tag = A.Mid(4); }
				else if (A.Equals(TEXT("legacy"), ESearchCase::IgnoreCase)) { C->bLegacy = true; }
			}
			Build(C);
			C->NextTime = FPlatformTime::Seconds() + FMath::Max(0.f, C->Delay);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) -> bool
			{
				if (!C->W.IsValid()) { return false; }
				if (C->bAwaitShot)
				{
					if (FScreenshotRequest::IsScreenshotRequested()) { return true; }
					if (C->FramesAfterShot++ < 2) { return true; }
					C->bAwaitShot = false;
				}
				if (FPlatformTime::Seconds() < C->NextTime) { return true; }
				if (!C->Steps.IsValidIndex(C->Index))
				{
					Finish(C);
					return false;
				}
				const float Wait = C->Steps[C->Index++]();
				if (Wait < 0.f) { C->bAwaitShot = true; C->FramesAfterShot = 0; C->NextTime = 0.0; }
				else { C->NextTime = FPlatformTime::Seconds() + Wait; }
				return true;
			}));
		}));
}

#endif // !UE_BUILD_SHIPPING
