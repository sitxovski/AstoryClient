// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПОДСКАЗКА ЗОЛОТА В ОКНЕ ХВАСТОВСТВА» (05.11.2026, баг игрока, сессия gold-boast): `asda.SelfTest.GoldBoastTip [quit]`.
//   Баг: игрок похвастался золотом (ПКМ по ячейке золота 20551 → меню 28000 → 6129), у адресата строка окна 28100 «Gold» по
//   наведению показывала подсказку снаряжения («Эффект ЖК 0~0, Профессия, Тип, Уровень, Живые камни 0/0, Улучшения 0/0, Вес 0»)
//   без суммы и последней строкой русское «Нет» в EN-клиенте. Лечение:
//     • `asda.ItemTip.GoldAmountOnly` (AsdaItemTooltipSubsystem.cpp) — золото полным путём → золотая ветка простого билдера
//       эталона «<N> Золото» (item_inventory.c:60016-60027);
//     • `asda.Lang.DropRuPlaceholder` (AsdaLangRows.cpp) — в не-русской сессии RU-заглушка описания «Нет» без перевода не печатается.
//   Путь игрока (закон `ui-check-game-path-geometry`): НАСТОЯЩЕЕ тело 6130 (81 б: i32 · char[20] cp1251 · запись 57 б) подаётся
//   в штатный `UAsdaNetSubsystem::HandlePacket` (`DebugFeedPacket`), окно 28100 открывается само, курсор ставится настоящими
//   событиями мыши Slate в центр живой геометрии строки, текст снимается с показанной плашки, кадр — снимком экрана. Офлайн (-AsdaNoNet).
//   Разделы:
//     G. золото 1 234 567 — подсказка «1,234,567 <2304>», заголовок «<имя 20551>», НЕТ «Эффект ЖК/Живые камни/Улучшения/Вес», нет «Нет»;
//     L. золото, ключ GoldAmountOnly 0 (как эталон) — снова поля снаряжения (печать «что было»);
//     P. вещь 20552 с RU-описанием-заглушкой «Нет» — RU: «Нет» есть (данные RU); EN: «Нет» нет;
//     Q. та же вещь, ключ DropRuPlaceholder 0 — EN: «Нет» снова есть (прежнее поведение); в RU ключ ничего не меняет;
//     W. оружие (тип 9) — контроль: полная подсказка снаряжения, как была.
//   Кадры — `Reports/gold_boast/frames/<язык>_<раздел>.png`. Ключи и опция окна возвращаются в конце.
//   Итог: «ЗОЛОТО-ХВАСТОВСТВО ИТОГ: ok=N fail=M». Карта мира (`--map world`), язык — `--lang ru|en`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaLoginData.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaShowOffSubsystem.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGoldBoastTipTest, Log, All);

namespace AsdaGoldBoastTipTest
{
	static const TCHAR* kCmd     = TEXT("asda.SelfTest.GoldBoastTip");     // имя команды
	static const TCHAR* kPfx     = TEXT("ЗОЛОТО-ХВАСТОВСТВО");              // префикс строк и итога
	static const TCHAR* kKeyGold = TEXT("asda.ItemTip.GoldAmountOnly");     // ключ исправления «золото только суммой»
	static const TCHAR* kKeyPh   = TEXT("asda.Lang.DropRuPlaceholder");     // ключ исправления «RU-заглушка в EN»
	static const TCHAR* kKeyWin  = TEXT("asda.ShowOff.Enabled");            // опция 875 «показывать окно хвастовства»

	static constexpr int32 kOpShowOff   = 6130;      // S→C `OP_ITEM_LIST_WND_ON_FILL_PACKET`, тело 81 б
	static constexpr int32 kGoldCode    = 20551;     // предмет «Золото»
	static constexpr int32 kGoldAmount  = 1234567;   // сумма в записи (+9 i32) — проверяет разделители тысяч
	static constexpr int32 kPhItemCode  = 20552;     // вещь с RU-описанием «Нет» (item_tooltip строка 1)
	// Нотисы строк, по которым видна ветка подсказки (те же id, что в `AsdaItemTooltipSubsystem.cpp`).
	static constexpr int32 kLocGold         = 2304;    // «Золото» — хвост строки суммы
	static constexpr int32 kLocWeight       = 2295;    // «Вес» — только у поля снаряжения/простых не-золотых
	static constexpr int32 kLocStatEffect   = 2360;    // «Эффект ЖК» — только полный билдер
	static constexpr int32 kLocSowelCount   = 2298;    // «Количество живых камней» — только полный
	static constexpr int32 kLocUpgradesLeft = 50024;   // «Доступно улучшений:» — только полный

	// Один раздел: какой предмет «прислал» сервер и при каких ключах.
	struct FCase
	{
		FString Tag;            // раздел (G/L/P/Q/W) — в имени кадра и в строках
		int32 Code = 0;         // код предмета записи 6130
		int32 Amount = 1;       // +9 записи (у золота — сумма)
		int32 Container = 1;    // +4 записи (золото живёт в контейнере 2)
		int32 KeyGold = 1;      // значение asda.ItemTip.GoldAmountOnly на время раздела
		int32 KeyPh = 1;        // значение asda.Lang.DropRuPlaceholder на время раздела
	};

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                          // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                        // мир прогона
		bool bQuit = false;                              // выйти после итога
		TArray<FCase> Cases;                             // разделы по порядку
		int32 SavedGold = 1, SavedPh = 1, SavedWin = 1;  // значения ключей до прогона
		FVector2D WorldPt = FVector2D::ZeroVector;       // точка мира без интерфейса (сброс наведения)
		FString FrameDir;                                // куда класть кадры
		int32 Weapon = 0;                                // код оружия для контроля W

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaGoldBoastTipTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaGoldBoastTipTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Центр живого прямоугольника узла (false — не отрисован / скрыт).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Курсор в точку + событие движения (наведение без щелчка).
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Весь текст показанной плашки (заголовок + тело; у RichText — с тегами разметки).
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
			if (!X || !AsdaUi::IsShown(X)) { continue; }
			if (const UTextBlock* TB = Cast<UTextBlock>(X))              { Out += TB->GetText().ToString() + TEXT("\n"); }
			else if (const URichTextBlock* RB = Cast<URichTextBlock>(X)) { Out += RB->GetText().ToString() + TEXT("\n"); }
		}
		return Out;
	}
	// Есть ли в тексте подпись нотиса (пустая подпись — «нет данных», считаем отсутствием).
	static bool Has(const FCtxRef& C, const FString& Text, int32 Loc)
	{
		const FString L = AsdaNotice::LocRaw(C->GI(), Loc, /*bDebugFallback*/false).TrimStartAndEnd();
		return !L.IsEmpty() && Text.Contains(L);
	}
	// Есть ли в тексте RU-заглушка «Нет» отдельной строкой (как её печатает билдер описанием).
	static bool HasRuPlaceholder(const FString& Text)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ true);
		for (const FString& L : Lines)
		{
			// Регистр — явными формами: IgnoreCase UE не сворачивает кириллицу.
			const FString T = L.TrimStartAndEnd();
			if (T.Equals(TEXT("нет")) || T.Equals(TEXT("Нет")) || T.Equals(TEXT("НЕТ"))) { return true; }
		}
		return false;
	}
	// Снимок экрана с плашкой.
	static void Shot(const FCtxRef& C, const FString& Tag)
	{
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), *Tag);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaGoldBoastTipTest, Display, TEXT("%s кадр заказан → %s"), kPfx, *File);
	}
	// Настоящее тело 6130 так, как его кладёт сервер (`Recv_ItemInfoDisplay@0x55BF80`): @0 i32 · @4 char[20] cp1251 · @24 запись 57 б.
	static TArray<uint8> Body6130(const FString& Sender, const FCase& K)
	{
		TArray<uint8> Body;
		Body.SetNumZeroed(81);
		FAsdaLoginData::EncodeCp1251(Sender, Body.GetData() + 4, 19);
		const TArray<uint8> Rec = FAsdaItemRecordWriter::Cell(K.Code, K.Container, /*CellSlot*/ 0, K.Amount);
		FMemory::Memcpy(Body.GetData() + 24, Rec.GetData(), FMath::Min(Rec.Num(), 57));
		return Body;
	}

	static void Finish(const FCtxRef& C)
	{
		AsdaCVarSave::SetForTest(kKeyGold, C->SavedGold);
		AsdaCVarSave::SetForTest(kKeyPh, C->SavedPh);
		AsdaCVarSave::SetForTest(kKeyWin, C->SavedWin);
		Hover(C->WorldPt);
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		if (UAsdaShowOffSubsystem* S = C->Sub<UAsdaShowOffSubsystem>()) { S->CloseListWindow(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaGoldBoastTipTest, kPfx, C->Ok, C->Fail);
	}

	// Один раздел: ключи → пакет 6130 штатным путём → наведение «мир → строка» → разбор текста и кадр.
	static void RunCase(const FCtxRef& C, int32 Idx, float T0)
	{
		At(C, T0, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			AsdaCVarSave::SetForTest(kKeyGold, K.KeyGold);
			AsdaCVarSave::SetForTest(kKeyPh, K.KeyPh);
			Hover(C->WorldPt);
			UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
			if (Net) { Net->DebugFeedPacket(kOpShowOff, Body6130(FString::Printf(TEXT("Boaster%s"), *K.Tag), K)); }
			C->Expect(Net != nullptr, FString::Printf(TEXT("%s0 6130 (код %d, кол-во %d, контейнер %d) подан в HandlePacket"),
				*K.Tag, K.Code, K.Amount, K.Container));
		});
		At(C, T0 + 0.35f, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			UAsdaShowOffSubsystem* S = C->Sub<UAsdaShowOffSubsystem>();
			const int32 Want = S ? S->GetEntryCount() - 1 : -1;
			int32 Row = -1;
			for (int32 j = 0; S && j < UAsdaShowOffSubsystem::kVisibleRows; ++j) { if (S->DebugGetRowEntry(j) == Want) { Row = j; } }
			FVector2D P = FVector2D::ZeroVector;
			const bool bGeo = S && S->IsListWindowOpen() && Row >= 0 && Center(S->DebugGetRowHit(Row), P);
			C->Expect(bGeo, FString::Printf(TEXT("%s1 окно 28100 открыто, запись %d в строке %d, строка на экране (%.0f,%.0f)"),
				*K.Tag, Want, Row, P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, T0 + 0.85f, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			const FString Txt = TipText(C);
			const bool bFullLines = Has(C, Txt, kLocStatEffect) || Has(C, Txt, kLocSowelCount) || Has(C, Txt, kLocUpgradesLeft);
			const bool bPh = HasRuPlaceholder(Txt);
			const bool bEn = !FAsdaLang::IsRu();
			UE_LOG(LogAsdaGoldBoastTipTest, Display, TEXT("%s %s код %d «%s» ×%d (GoldAmountOnly=%d, DropRuPlaceholder=%d) — текст плашки:\n%s"),
				kPfx, *K.Tag, K.Code, IT ? *IT->GetItemName(K.Code) : TEXT("?"), K.Amount, K.KeyGold, K.KeyPh, *Txt);
			if (K.Tag == TEXT("G"))
			{
				const FString Sum = AsdaUi::FormatThousands(K.Amount);
				const FString Name = IT ? IT->GetItemName(kGoldCode) : FString();
				C->Expect(!Txt.IsEmpty() && Txt.Contains(Sum) && Has(C, Txt, kLocGold) && !Name.IsEmpty() && Txt.Contains(Name),
					FString::Printf(TEXT("G2 золото — заголовок «%s» и сумма «%s %s» %s"), *Name, *Sum,
						*AsdaNotice::LocRaw(C->GI(), kLocGold, false), Txt.Contains(Sum) ? TEXT("есть") : TEXT("НЕТ")));
				C->Expect(!bFullLines && !Has(C, Txt, kLocWeight) && !bPh,
					FString::Printf(TEXT("G3 золото — строк снаряжения %s, «Вес» %s, «Нет» %s"),
						bFullLines ? TEXT("ЕСТЬ") : TEXT("нет"), Has(C, Txt, kLocWeight) ? TEXT("ЕСТЬ") : TEXT("нет"), bPh ? TEXT("ЕСТЬ") : TEXT("нет")));
			}
			else if (K.Tag == TEXT("L"))
			{
				C->Expect(!Txt.IsEmpty() && bFullLines && !Txt.Contains(AsdaUi::FormatThousands(K.Amount)),
					FString::Printf(TEXT("L2 ключ 0 (как эталон) — золото снова полным билдером без суммы (строки снаряжения %s)"),
						bFullLines ? TEXT("есть") : TEXT("НЕТ")));
			}
			else if (K.Tag == TEXT("P"))
			{
				// RU: «Нет» — данные RU-оригинала, остаётся. EN: перевода нет → строка не печатается.
				C->Expect(!Txt.IsEmpty() && bFullLines && (bEn ? !bPh : bPh),
					FString::Printf(TEXT("P2 вещь %d с описанием-заглушкой (%s): «Нет» %s, ждём %s"), K.Code, bEn ? TEXT("EN") : TEXT("RU"),
						bPh ? TEXT("есть") : TEXT("нет"), bEn ? TEXT("нет") : TEXT("есть")));
			}
			else if (K.Tag == TEXT("Q"))
			{
				C->Expect(!Txt.IsEmpty() && bPh,
					FString::Printf(TEXT("Q2 ключ DropRuPlaceholder 0 — «Нет» %s (прежнее поведение, %s)"), bPh ? TEXT("есть") : TEXT("НЕТ"),
						bEn ? TEXT("EN") : TEXT("RU")));
			}
			else if (K.Tag == TEXT("W"))
			{
				C->Expect(!Txt.IsEmpty() && bFullLines && !bPh,
					FString::Printf(TEXT("W2 оружие %d — полная подсказка снаряжения (строки %s), «Нет» %s"), K.Code,
						bFullLines ? TEXT("есть") : TEXT("НЕТ"), bPh ? TEXT("ЕСТЬ") : TEXT("нет")));
			}
			Shot(C, K.Tag);
		});
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaShowOffSubsystem* S = C->Sub<UAsdaShowOffSubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		if (!S || !IT || !WM || !C->Sub<UAsdaNetSubsystem>()) { C->Expect(false, TEXT("подсистемы хвастовства/таблиц/окон/сети есть")); Finish(C); return; }

		// Данные: золото и вещь-заглушка — по таблицам игры; оружие — первый тип 9.
		for (int32 Code = 20000; Code < 80000 && !C->Weapon; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (R && R->ItemType == 9) { C->Weapon = Code; }
		}
		const bool bData = IT->GetItem(kGoldCode) && IT->GetItem(kPhItemCode) && C->Weapon;
		C->Expect(bData, FString::Printf(TEXT("данные: золото %d «%s», заглушка %d «%s», оружие %d"), kGoldCode,
			*IT->GetItemName(kGoldCode), kPhItemCode, *IT->GetItemName(kPhItemCode), C->Weapon));
		if (!bData) { Finish(C); return; }

		auto Mk = [](const TCHAR* Tag, int32 Code, int32 Amount, int32 Cont, int32 KG, int32 KP)
		{
			FCase K; K.Tag = Tag; K.Code = Code; K.Amount = Amount; K.Container = Cont; K.KeyGold = KG; K.KeyPh = KP; return K;
		};
		C->Cases.Add(Mk(TEXT("G"), kGoldCode, kGoldAmount, 2, 1, 1));
		C->Cases.Add(Mk(TEXT("L"), kGoldCode, kGoldAmount, 2, 0, 1));
		C->Cases.Add(Mk(TEXT("P"), kPhItemCode, 1, 1, 1, 1));
		C->Cases.Add(Mk(TEXT("Q"), kPhItemCode, 1, 1, 1, 0));
		C->Cases.Add(Mk(TEXT("W"), C->Weapon, 1, 1, 1, 1));

		if (const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKeyGold)) { C->SavedGold = V->GetInt(); }
		if (const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKeyPh))   { C->SavedPh = V->GetInt(); }
		if (const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKeyWin))  { C->SavedWin = V->GetInt(); }
		AsdaCVarSave::SetForTest(kKeyWin, 1);   // опция 875 включена — иначе 6130 окна не покажет (эталон)

		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/gold_boast/frames"));
		IFileManager::Get().MakeDirectory(*C->FrameDir, /*Tree*/ true);
		// Точка мира без интерфейса (сброс наведения между разделами).
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

		float T = 0.6f;
		for (int32 i = 0; i < C->Cases.Num(); ++i)
		{
			RunCase(C, i, T);
			T += 1.6f;
		}
		At(C, T + 0.3f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestGoldBoastTip(
	TEXT("asda.SelfTest.GoldBoastTip"),
	TEXT("Самотест «золото в окне хвастовства»: настоящий 6130 с записью золота → строка окна 28100 → подсказка только суммой "
	     "«N Золото»; ключ 0 — как эталон (поля снаряжения); RU-заглушка «Нет» в EN не печатается; оружие — контроль. "
	     "Кадры в Reports/gold_boast/frames. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGoldBoastTipTest;
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
