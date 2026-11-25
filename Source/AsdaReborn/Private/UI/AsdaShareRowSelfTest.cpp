// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ БЛОКА «ДЕЛИТЬСЯ / НЕ ДЕЛИТЬСЯ» ОКНА СООБЩЕСТВА 6400 (вкладка «Друг») — `asda.UI.SelfTestShareRow [delay=N] [shot] [quit]`
//   (баг владельца 08.10.2026 п.2, отчёт `Reports/owner_1008/ui_bugs.md`). ПУТЬ ИГРОКА: окно открывается штатным входом
//   (`UAsdaCommunitySubsystem::OpenOnTab(0)`), строки блока наполняет `UAsdaSoulmateSubsystem::RefreshShareBlock` из списка
//   персонажей (`UAsdaNetSubsystem::CharList`, сюда — два имени, как на снимке владельца). После отрисовки — ЖИВАЯ геометрия:
//     • квадрат галки SOUL_CHECK0N целиком левее подписи SHARE_0N (правый край галки < левого края строки), размер 18×19;
//     • подпись целиком в строке блока (не за вертикальной линией 211 и не обрезана);
//     • три строки одинаковы (сдвиг галки и подписи от левого края строки — равный);
//     • «Разорвать Дружбу» (Soul_CutoffBtn_Text) — кегль 7 (решение владельца), строка в пределах кнопки.
//   Строки — «ДЕЛИТЬСЯ», итог — «ДЕЛИТЬСЯ ИТОГ: ok=… fail=…». Только офлайн.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Pet/AsdaPetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaShareRowTest, Log, All);

namespace AsdaShareRowLocal
{
	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false, bShot = false;
		FString ShotTag = TEXT("run");   // метка снимка
		bool bCap = false;               // перехват исходящих взведён
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaShareRowTest, Log, TEXT("ДЕЛИТЬСЯ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaShareRowTest, Error, TEXT("ДЕЛИТЬСЯ FAIL %s"), *What); }
		}
	};

	// Таймер мира прогона.
	using AsdaSelfTestCtx::ByFn::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Живой прямоугольник узла окна (абсолютные единицы). false — нет/не виден.
	static bool Rect(UUserWidget* Wnd, const TCHAR* Name, FVector2D& Mn, FVector2D& Mx)
	{
		UWidget* W = Wnd ? Wnd->GetWidgetFromName(FName(Name)) : nullptr;
		return W && W->IsVisible() && AsdaUi::WidgetAbsRect(W, Mn, Mx) && Mx.X > Mn.X;
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = C->W.Get() ? C->W.Get()->GetGameInstance() : nullptr;
		if (C->bCap && GI) { if (UAsdaPetSubsystem* Pet = GI->GetSubsystem<UAsdaPetSubsystem>()) { Pet->EndSendCapture(); } }
		UE_LOG(LogAsdaShareRowTest, Log, TEXT("ДЕЛИТЬСЯ ИТОГ: ok=%d fail=%d"), C->Ok, C->Fail);
		if (C->bQuit && GEngine && C->W.Get()) { GEngine->Exec(C->W.Get(), TEXT("quit")); }
	}

	// Замер после отрисовки.
	static void Measure(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = C->W.Get() ? C->W.Get()->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UUserWidget* Wnd = WM ? WM->FindWindow(FName(TEXT("COMMUNITY_WIN"))) : nullptr;
		if (!Wnd) { C->Expect(false, TEXT("окно COMMUNITY_WIN не открылось")); Finish(C); return; }
		FVector2D LMn, LMx;
		const bool bLine = Rect(Wnd, TEXT("Soul_Hline1_95_0"), LMn, LMx);   // вертикальная линия блока (правая граница строк)
		float Sc = 1.f;
		{
			FVector2D Mn, Mx;
			if (Rect(Wnd, TEXT("SOUL_CHECK00"), Mn, Mx)) { Sc = FMath::Max((float)((Mx.Y - Mn.Y) / 19.0), 0.01f); }
		}
		TArray<float> DxCheck, DxText;
		for (int32 i = 0; i < 2; ++i)   // две строки с персонажами (третья пуста и скрыта)
		{
			const FString Chk = FString::Printf(TEXT("SOUL_CHECK0%d"), i), Lbl = FString::Printf(TEXT("SHARE_0%d"), i);
			const FString Row = FString::Printf(TEXT("SOUL_TXT0%d_grp"), 7 + i);
			FVector2D CMn, CMx, TMn, TMx, RMn, RMx;
			const bool bC = Rect(Wnd, *Chk, CMn, CMx), bT = Rect(Wnd, *Lbl, TMn, TMx), bR = Rect(Wnd, *Row, RMn, RMx);
			UTextBlock* Tb = Cast<UTextBlock>(Wnd->GetWidgetFromName(FName(*Lbl)));
			const float TextW = Tb ? (float)Tb->GetDesiredSize().X : 0.f;   // ширина строки подписи (вся надпись)
			const float TextRight = (float)TMn.X / Sc + TextW;
			UE_LOG(LogAsdaShareRowTest, Log, TEXT("ДЕЛИТЬСЯ замер строка %d: галка x %.1f…%.1f (%.1f×%.1f) | подпись «%s» x %.1f, ширина строки %.1f → правый край %.1f | строка x %.1f | линия x %.1f"),
				i, CMn.X / Sc, CMx.X / Sc, (CMx.X - CMn.X) / Sc, (CMx.Y - CMn.Y) / Sc, Tb ? *Tb->GetText().ToString() : TEXT("?"),
				TMn.X / Sc, TextW, TextRight, RMn.X / Sc, bLine ? LMn.X / Sc : -1.0);
			C->Expect(bC && bT && CMx.X <= TMn.X + 0.01, FString::Printf(TEXT("строка %d: квадрат галки целиком левее подписи (правый край %.1f ≤ начало подписи %.1f)"),
				i, CMx.X / Sc, TMn.X / Sc));
			C->Expect(bC && FMath::IsNearlyEqual((float)((CMx.X - CMn.X) / Sc), 18.f, 0.6f) && FMath::IsNearlyEqual((float)((CMx.Y - CMn.Y) / Sc), 19.f, 0.6f),
				FString::Printf(TEXT("строка %d: квадрат галки не растянут — 18×19"), i));
			C->Expect(bT && TextW > 1.f && (!bLine || TextRight <= LMn.X / Sc - 1.f),
				FString::Printf(TEXT("строка %d: подпись целиком в строке, до вертикальной линии блока (правый край %.1f, линия %.1f)"),
					i, TextRight, bLine ? LMn.X / Sc : -1.0));
			if (bR) { DxCheck.Add((float)((CMn.X - RMn.X) / Sc)); DxText.Add((float)((TMn.X - RMn.X) / Sc)); }
		}
		C->Expect(DxCheck.Num() == 2 && FMath::IsNearlyEqual(DxCheck[0], DxCheck[1], 0.01f) && FMath::IsNearlyEqual(DxText[0], DxText[1], 0.01f),
			FString::Printf(TEXT("строки одинаковы: галка от края строки %.1f / %.1f, подпись %.1f / %.1f"),
				DxCheck.Num() > 0 ? DxCheck[0] : -1.f, DxCheck.Num() > 1 ? DxCheck[1] : -1.f, DxText.Num() > 0 ? DxText[0] : -1.f, DxText.Num() > 1 ? DxText[1] : -1.f));
		// «Разорвать Дружбу».
		UTextBlock* Cut = Cast<UTextBlock>(Wnd->GetWidgetFromName(TEXT("Soul_CutoffBtn_Text")));
		FVector2D BMn, BMx;
		const bool bBtn = Rect(Wnd, TEXT("Soul_CutoffBtn"), BMn, BMx);
		const float CutW = Cut ? (float)Cut->GetDesiredSize().X : 0.f;
		const float CutSize = Cut ? (float)Cut->GetFont().Size : 0.f;
		UE_LOG(LogAsdaShareRowTest, Log, TEXT("ДЕЛИТЬСЯ замер кнопка: «%s» кегль %.1f ширина строки %.1f, кнопка %.1f×%.1f"),
			Cut ? *Cut->GetText().ToString() : TEXT("?"), CutSize, CutW, bBtn ? (BMx.X - BMn.X) / Sc : -1.0, bBtn ? (BMx.Y - BMn.Y) / Sc : -1.0);
		C->Expect(Cut && FMath::IsNearlyEqual(CutSize, 7.f, 0.01f) && bBtn && CutW <= (BMx.X - BMn.X) / Sc,
			FString::Printf(TEXT("«Разорвать Дружбу»: кегль 7 (решение владельца), строка %.1f в кнопке %.1f"), CutW, bBtn ? (BMx.X - BMn.X) / Sc : -1.0));
		if (C->bShot && GEngine && C->W.Get()) { GEngine->Exec(C->W.Get(), *FString::Printf(TEXT("asda.UI.Shot sharerow_%s"), *C->ShotTag)); }
		After(C, 0.6f, &Finish);
	}

	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaCommunitySubsystem* Comm = GI ? GI->GetSubsystem<UAsdaCommunitySubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!Net || !Comm || !WS) { C->Expect(false, TEXT("нет подсистем")); Finish(C); return; }
		if (UAsdaPetSubsystem* Pet = GI->GetSubsystem<UAsdaPetSubsystem>())
		{
			C->bCap = Pet->BeginSendCapture([](int32, const TArray<uint8>&, bool, TCHAR) {}, 0);   // в сеть не уходит ничего
		}
		if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("GM_sitxovski"));
		}
		// Список персонажей аккаунта — два, как на снимке владельца (третий слот пуст: 0xFF).
		Net->CharList.SetNum(3);
		const TCHAR* Names[3] = { TEXT("GM_sitxovski"), TEXT("sitx"), TEXT("") };
		for (int32 i = 0; i < 3; ++i)
		{
			Net->CharList[i] = FAsdaCharRecord();
			Net->CharList[i].Name = Names[i];
			Net->CharList[i].Slot = (i < 2) ? (uint8)i : (uint8)0xFF;
		}
		After(C, 1.0f, [](TSharedRef<FCtx> C2)
		{
			UGameInstance* GI2 = C2->W.Get() ? C2->W.Get()->GetGameInstance() : nullptr;
			if (UAsdaCommunitySubsystem* Comm2 = GI2 ? GI2->GetSubsystem<UAsdaCommunitySubsystem>() : nullptr) { Comm2->OpenOnTab(0); }   // вкладка «Друг»
			After(C2, 1.0f, &Measure);
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSelfTestShareRow(
	TEXT("asda.UI.SelfTestShareRow"),
	TEXT("asda.UI.SelfTestShareRow [delay=N] [shot] [tag=метка] [quit] — ОФЛАЙН: окно сообщества 6400, вкладка «Друг», блок «Делиться»: галка левее ")
	TEXT("подписи (квадрат 18×19), подпись целиком, строки одинаковы; «Разорвать Дружбу» кеглем 7"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.SelfTestShareRow"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;
		TSharedRef<AsdaShareRowLocal::FCtx> C = MakeShared<AsdaShareRowLocal::FCtx>();
		C->W = W;
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { C->bQuit = true; }
			else if (Arg.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { C->bShot = true; }
			else if (Arg.StartsWith(TEXT("tag="))) { C->ShotTag = Arg.Mid(4); }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		if (Delay <= 0.f) { AsdaShareRowLocal::Run(C); return; }
		AsdaShareRowLocal::After(C, Delay, [](TSharedRef<AsdaShareRowLocal::FCtx> C2)
		{
			if (AsdaSelfTestKit::RefuseDeferred(C2->W.Get(), TEXT("asda.UI.SelfTestShareRow"), C2->bQuit)) { return; }   // ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте
			AsdaShareRowLocal::Run(C2);
		});
	}));

#endif // !UE_BUILD_SHIPPING
