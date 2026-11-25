// Copyright (c) 2026 @sitxovski (Discord)
// AsdaIconsAfterTravelSelfTest.cpp — САМОТЕСТ «ИКОНКИ ПОСЛЕ СМЕНЫ КАРТЫ» (04.11.2026, баг владельца: «иконки в просмотре
//   набора на месте, пока не сменишь карту; после смены карты — пусто; у наград квеста то же»; Reports/levelup_potion).
//
//   `asda.SelfTest.IconsAfterTravel [travels=N] [out=<папка>] [quit]` — офлайн (`-AsdaNoNet`), НАСТОЯЩИЙ травел UE
//   (`OpenLevel` — движок сносит виджеты мира, `PostLoadMap` рассылает «смену карты»), N переходов (по умолчанию 2).
//   На КАЖДОМ круге (0 — до первого перехода, 1…N — после каждого) путём игрока:
//     • панель состава 26500: `Populate` коробок 58966 «Additional Upgrades(2)» (кадр владельца), 76405 и 79935 —
//       у каждой занятой ячейки проверяется ВИДЖЕТ, который подсистема красит: он ВНУТРИ ТЕКУЩЕГО окна панели (а не
//       окна прошлой карты), кисть = текстура атласа, цепочка видима, на живом кадре размер ≥ 8 px;
//     • журнал квестов 700: квест 130 стадия 0 (`asda.Quest.FakeState`), ячейка 0 выбрана — те же проверки у иконок наград;
//     • ОБХОД ВСЕХ ПОДСИСТЕМ GameInstance рефлексией: ссылка на виджет СНЕСЁННОГО окна, когда та же подсистема уже держит
//       НОВОЕ окно того же класса, = «окно пересоздано, а ячейка осталась на старом» (FAIL с путём свойства). Ловит
//       дефект у любого окна, не только у двух из жалобы.
//   Кадры `asda_iconstravel_r<круг>.png` (копия в `out=`). Итог `ИКОНКИ-ТРАВЕЛ ИТОГ: ok=N fail=M`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaPackagePanelSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"
#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"
#include "UI/AsdaWarShopSubsystem.h"
#include "UI/AsdaExtractShopSubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaPremiumPostSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaCashGiftSubsystem.h"
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/PropertyIterator.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaIconsTravelTest, Log, All);

// Друг журнала квестов (объявлен в `AsdaQuestLogSubsystem.h`): читает массивы иконок наград и окно.
struct FAsdaIconsAfterTravelSelfTest
{
	static const TArray<TObjectPtr<UImage>>& RewardIcons(const UAsdaQuestLogSubsystem* Q) { return Q->RewardIcons; }
	static const TArray<TObjectPtr<UImage>>& ChoiceIcons(const UAsdaQuestLogSubsystem* Q) { return Q->ChoiceIcons; }
	static const TArray<int32>& RewardCodes(const UAsdaQuestLogSubsystem* Q) { return Q->RewardCodes; }
	static const TArray<int32>& ChoiceCodes(const UAsdaQuestLogSubsystem* Q) { return Q->ChoiceCodes; }
	static const UUserWidget* Win(const UAsdaQuestLogSubsystem* Q) { return Q->Win.Get(); }
};

namespace AsdaIconsTravelTest
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.IconsAfterTravel");   // имя команды
	const TCHAR* const kPrefix = TEXT("ИКОНКИ-ТРАВЕЛ");                 // префикс строк и итога
	const int32 kBoxes[] = { 58966, 76405, 79935 };                      // коробки: кадр владельца + волна levelup_potion

	// Внешнее окно виджета: самый наружный `UUserWidget` по цепочке владельцев (WBP-узел → WidgetTree → окно).
	const UUserWidget* RootUserWidget(const UObject* O)
	{
		const UUserWidget* Top = nullptr;
		for (const UObject* Up = O; Up; Up = Up->GetOuter())
		{
			if (const UUserWidget* U = Cast<UUserWidget>(Up)) { Top = U; }
		}
		return Top;
	}

	// Окно «живо для игрока»: само во вьюпорте либо вложено в живое окно.
	bool RootLive(const UUserWidget* U)
	{
		return U && (U->IsInViewport() || U->GetParent() != nullptr);
	}

	// Причина «иконку не видно» ("" — видно). `Host` — ТЕКУЩЕЕ окно подсистемы.
	FString IconProblem(const UWidget* W, const UUserWidget* Host, bool bGeom)
	{
		const UImage* Img = Cast<UImage>(W);
		if (!Img) { return TEXT("нет виджета иконки"); }
		if (!Host) { return TEXT("нет текущего окна"); }
		const UUserWidget* Root = RootUserWidget(Img);
		if (Root != Host)
		{
			return FString::Printf(TEXT("виджет %s из ДРУГОГО окна %s (текущее %s, во вьюпорте: %s)"), *Img->GetName(),
				Root ? *Root->GetName() : TEXT("нет"), *Host->GetName(), RootLive(Root) ? TEXT("да") : TEXT("НЕТ"));
		}
		if (!RootLive(Host)) { return TEXT("текущее окно не во вьюпорте"); }
		const FSlateBrush& B = Img->GetBrush();
		if (!Cast<UTexture2D>(B.GetResourceObject())) { return TEXT("кисть без текстуры атласа"); }
		if (B.DrawAs == ESlateBrushDrawType::NoDrawType) { return TEXT("кисть NoDraw"); }
		for (const UWidget* Up = Img; Up; Up = Up->GetParent())
		{
			const ESlateVisibility V = Up->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return FString::Printf(TEXT("скрыт узел %s"), *Up->GetName()); }
		}
		if (bGeom)
		{
			const FVector2D Sz = Img->GetCachedGeometry().GetLocalSize();
			if (Sz.X < 8.f || Sz.Y < 8.f) { return FString::Printf(TEXT("на кадре %.0f×%.0f px"), Sz.X, Sz.Y); }
		}
		return FString();
	}

	struct FRun : public TSharedFromThis<FRun>
	{
		TWeakObjectPtr<UGameInstance> GI;
		bool bQuit = false;
		int32 Travels = 2;
		FString OutDir;
		int32 Ok = 0, Fail = 0;
		TArray<TPair<float, TFunction<void(FRun&)>>> Steps;
		int32 Next = 0;
		double DueAt = 0.0;
		FString PendingShot;   // имя кадра, запрошенного на прошлом шаге (копируется в out=)

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaIconsTravelTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaIconsTravelTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Add(float Delay, TFunction<void(FRun&)> Fn) { Steps.Emplace(Delay, MoveTemp(Fn)); }
		void Exec(const TCHAR* C) { if (UWorld* Wd = W()) { if (GEngine) { GEngine->Exec(Wd, C); } } }
		void CopyShot()
		{
			if (PendingShot.IsEmpty() || OutDir.IsEmpty()) { return; }
			const FString Src = FPaths::ScreenShotDir() / (PendingShot + TEXT(".png"));
			IFileManager::Get().MakeDirectory(*OutDir, true);
			const bool bCopied = IFileManager::Get().Copy(*(OutDir / (PendingShot + TEXT(".png"))), *Src) == COPY_OK;
			UE_LOG(LogAsdaIconsTravelTest, Log, TEXT("%s кадр %s → %s: %s"), kPrefix, *Src, *OutDir, bCopied ? TEXT("скопирован") : TEXT("НЕ скопирован"));
			PendingShot.Reset();
		}
		void Start()
		{
			DueAt = FPlatformTime::Seconds() + (Steps.Num() ? Steps[0].Key : 0.f);
			TSharedRef<FRun> Self = AsShared();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self](float) { return Self->TickOnce(); }));
		}
		bool TickOnce()
		{
			if (FPlatformTime::Seconds() < DueAt) { return true; }
			if (Next >= Steps.Num()) { return false; }
			TFunction<void(FRun&)> Fn = Steps[Next].Value;
			++Next;
			Fn(*this);
			if (Next >= Steps.Num())
			{
				CopyShot();
				AsdaSelfTestKit::Finish(W(), kCmd, bQuit, LogAsdaIconsTravelTest, kPrefix, Ok, Fail);
				return false;
			}
			DueAt = FPlatformTime::Seconds() + Steps[Next].Key;
			return true;
		}
	};

	// Открыть проверяемое: журнал с квестом 130 (ячейка 0) и панель состава коробки `Box`.
	void OpenAll(FRun& R, int32 Box)
	{
		R.Exec(TEXT("asda.Quest.FakeSelf 20 1 0"));
		R.Exec(TEXT("asda.Quest.FakeState 130 0"));
		if (UAsdaQuestLogSubsystem* Q = R.Sub<UAsdaQuestLogSubsystem>())
		{
			if (!Q->IsOpen()) { Q->Open(); }
			Q->SelectSlot(0);
		}
		if (UAsdaPackagePanelSubsystem* Pk = R.Sub<UAsdaPackagePanelSubsystem>())
		{
			Pk->ClosePanel();
			const bool bOpen = Pk->Populate(Box, 0, -1, -1);
			R.Check(bOpen, FString::Printf(TEXT("коробка %d: панель 26500 открыта"), Box));
		}
	}

	// Проверки одного круга (после того как кадр отрисован — геометрия живая).
	void CheckAll(FRun& R, int32 Round, int32 Box)
	{
		const FString Tag = FString::Printf(TEXT("круг %d (переходов %d), коробка %d"), Round, Round, Box);
		// ── панель 26500 ──
		if (UAsdaPackagePanelSubsystem* Pk = R.Sub<UAsdaPackagePanelSubsystem>())
		{
			const UUserWidget* Host = Cast<UUserWidget>(Pk->DebugRootWidget());
			int32 Cells = 0, Bad = 0;
			FString First;
			for (int32 b = 0; b < UAsdaPackagePanelSubsystem::kBlocks; ++b)
			{
				for (int32 Pass = 0; Pass < 2; ++Pass)
				{
					const bool bBig = (Pass == 0);
					const int32 N = bBig ? UAsdaPackagePanelSubsystem::kBigCells : UAsdaPackagePanelSubsystem::kSmallCells;
					for (int32 i = 0; i < N; ++i)
					{
						const int32 Code = Pk->DebugCellCode(b, bBig, i);
						if (Code <= 0) { continue; }
						++Cells;
						const FString Why = IconProblem(Pk->DebugCellIcon(b, bBig, i), Host, /*bGeom*/ true);
						if (!Why.IsEmpty())
						{
							++Bad;
							if (First.IsEmpty()) { First = FString::Printf(TEXT("блок %d %s %d (предмет %d): %s"), b, bBig ? TEXT("верх") : TEXT("низ"), i, Code, *Why); }
						}
					}
				}
			}
			R.Check(Cells > 0 && Bad == 0, FString::Printf(TEXT("%s: панель состава — ячеек %d, без видимой иконки %d%s%s"),
				*Tag, Cells, Bad, First.IsEmpty() ? TEXT("") : TEXT("; первая: "), *First));
		}
		// ── журнал 700: иконки наград ──
		if (UAsdaQuestLogSubsystem* Q = R.Sub<UAsdaQuestLogSubsystem>())
		{
			using F = FAsdaIconsAfterTravelSelfTest;
			const UUserWidget* Host = F::Win(Q);
			int32 Cells = 0, Bad = 0;
			FString First;
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const TArray<TObjectPtr<UImage>>& Icons = Pass == 0 ? F::RewardIcons(Q) : F::ChoiceIcons(Q);
				const TArray<int32>& Codes = Pass == 0 ? F::RewardCodes(Q) : F::ChoiceCodes(Q);
				for (int32 j = 0; j < Codes.Num(); ++j)
				{
					if (Codes[j] <= 0) { continue; }
					++Cells;
					const FString Why = IconProblem(Icons.IsValidIndex(j) ? Icons[j].Get() : nullptr, Host, /*bGeom*/ true);
					if (!Why.IsEmpty())
					{
						++Bad;
						if (First.IsEmpty()) { First = FString::Printf(TEXT("%s %d (предмет %d): %s"), Pass == 0 ? TEXT("награда") : TEXT("на выбор"), j, Codes[j], *Why); }
					}
				}
			}
			R.Check(Cells > 0 && Bad == 0, FString::Printf(TEXT("%s: журнал — иконок наград-предметов %d, без видимой %d%s%s"),
				*Tag, Cells, Bad, First.IsEmpty() ? TEXT("") : TEXT("; первая: "), *First));
		}
	}

	// ★ Обход всех подсистем GameInstance: ссылка на виджет снесённого окна при живом окне ТОГО ЖЕ класса у той же подсистемы.
	void SweepStale(FRun& R, int32 Round)
	{
		if (!R.GI.IsValid()) { return; }
		int32 Subs = 0, Refs = 0, Stale = 0;
		for (UGameInstanceSubsystem* S : R.GI->GetSubsystemArrayCopy<UGameInstanceSubsystem>())
		{
			if (!S) { continue; }
			++Subs;
			TSet<const UClass*> LiveClasses;
			struct FHit { FString Path; const UUserWidget* Root; FString Widget; };
			TArray<FHit> Dead;
			for (TPropertyValueIterator<FObjectPropertyBase> It(S->GetClass(), S, EPropertyValueIteratorFlags::FullRecursion); It; ++It)
			{
				const UObject* O = It.Key()->GetObjectPropertyValue(It.Value());
				const UWidget* Wd = Cast<UWidget>(O);
				const UUserWidget* Root = Wd ? RootUserWidget(Wd) : Cast<UUserWidget>(O);
				if (!Root) { continue; }
				++Refs;
				if (RootLive(Root)) { LiveClasses.Add(Root->GetClass()); continue; }
				TArray<const FProperty*> Chain;
				It.GetPropertyChain(Chain);
				FString Path;
				for (int32 c = Chain.Num() - 1; c >= 0; --c) { Path += (Path.IsEmpty() ? TEXT("") : TEXT(".")) + Chain[c]->GetName(); }
				Dead.Add({ Path, Root, O->GetName() });
			}
			TMap<FString, int32> PerPath;
			for (const FHit& H : Dead)
			{
				if (LiveClasses.Contains(H.Root->GetClass())) { PerPath.FindOrAdd(H.Path)++; }
			}
			for (const TPair<FString, int32>& P : PerPath)
			{
				++Stale;
				R.Check(false, FString::Printf(TEXT("круг %d: %s.%s — %d ссылок на виджеты СНЕСЁННОГО окна, хотя окно этого класса уже пересоздано"),
					Round, *S->GetClass()->GetName(), *P.Key, P.Value));
			}
		}
		R.Check(Stale == 0, FString::Printf(TEXT("круг %d: обход подсистем — %d подсистем, %d ссылок на окна, «старое окно рядом с новым» у %d свойств"),
			Round, Subs, Refs, Stale));
	}

	// Открыть остальные окна с иконками предметов (магазины NPC/бустеров/премиум/военный/снаряжения/извлечения, почта,
	//   окно приёма задания 750, подсказка предмета) — чтобы после перехода они ПЕРЕСОЗДАЛИСЬ и обход подсистем увидел,
	//   не осталась ли у кого ячейка на снесённом окне. Отправки офлайн перекрыты вторым барьером самотеста.
	void OpenOthers(FRun& R)
	{
		if (UAsdaNpcShopSubsystem* S = R.Sub<UAsdaNpcShopSubsystem>())         { S->OpenShop(5, 1); }
		if (UAsdaBoosterShopSubsystem* S = R.Sub<UAsdaBoosterShopSubsystem>()) { S->OpenWindow(); }
		if (UAsdaPremiumShopSubsystem* S = R.Sub<UAsdaPremiumShopSubsystem>()) { S->OpenWindow(true); }
		if (UAsdaEquipShopSubsystem* S = R.Sub<UAsdaEquipShopSubsystem>())     { S->OpenWindow(); }
		if (UAsdaWarShopSubsystem* S = R.Sub<UAsdaWarShopSubsystem>())         { S->OpenWindow(); }
		if (UAsdaExtractShopSubsystem* S = R.Sub<UAsdaExtractShopSubsystem>()) { S->OpenWindow(); }
		if (UAsdaPremiumPostSubsystem* S = R.Sub<UAsdaPremiumPostSubsystem>()) { S->OpenAndSync(false); }
		R.Exec(TEXT("asda.Npc.AcceptShow 20011 20011 0"));
		if (UAsdaItemTooltipSubsystem* S = R.Sub<UAsdaItemTooltipSubsystem>()) { S->ShowItemTooltipByCode(79935); }   // коробка 5.1.4 — запись в таблице предметов есть (558 — нет, тултип не показывался)
	}

	// Шаги одного круга: открыть → кадр → проверки по всем коробкам → снимок → закрыть.
	void AddRound(const TSharedRef<FRun>& Run, int32 Round)
	{
		for (int32 k = 0; k < UE_ARRAY_COUNT(kBoxes); ++k)
		{
			const int32 Box = kBoxes[k];
			Run->Add(k == 0 ? 0.5f : 0.3f, [Box](FRun& R) { R.CopyShot(); OpenAll(R, Box); });
			Run->Add(1.0f, [Round, Box, k](FRun& R)
			{
				CheckAll(R, Round, Box);
				if (k == 0)
				{
					R.PendingShot = FString::Printf(TEXT("asda_iconstravel_r%d"), Round);
					FScreenshotRequest::RequestScreenshot(R.PendingShot, /*bShowUI*/ true, /*bAddUniqueSuffix*/ false);
				}
			});
		}
		Run->Add(0.3f, [](FRun& R) { OpenOthers(R); });
		Run->Add(1.0f, [Round](FRun& R)
		{
			SweepStale(R, Round);
			// `CloseAllPlayerWindows` (порт `uiCloseAllWindows`) ещё и блокирует хотбар и трей — без парного
			//   `RestoreAfterSceneExit` следующий самотест пакета видел «персонаж занят» (CashGift 49/20 после нас).
			if (UAsdaWindowSubsystem* WM = R.Sub<UAsdaWindowSubsystem>()) { WM->CloseAllPlayerWindows(); WM->RestoreAfterSceneExit(); }
			if (UAsdaItemTooltipSubsystem* Tt = R.Sub<UAsdaItemTooltipSubsystem>()) { Tt->HideTooltip(); }
			// Закрыватель в росписи `CloseAllPlayerWindows` есть не у всех: открытый магазин снаряжения держит
			//   `AsdaShopCanInteract` = «нельзя» для всех следующих самотестов. Закрываем ВСЁ, что открыли, явно.
			if (UAsdaNpcShopSubsystem* S = R.Sub<UAsdaNpcShopSubsystem>())         { S->CloseShop(); }
			if (UAsdaBoosterShopSubsystem* S = R.Sub<UAsdaBoosterShopSubsystem>()) { S->CloseWindow(); }
			if (UAsdaPremiumShopSubsystem* S = R.Sub<UAsdaPremiumShopSubsystem>()) { S->CloseWindow(); }
			if (UAsdaEquipShopSubsystem* S = R.Sub<UAsdaEquipShopSubsystem>())     { S->CloseWindow(); }
			if (UAsdaWarShopSubsystem* S = R.Sub<UAsdaWarShopSubsystem>())         { S->CloseWindow(); }
			if (UAsdaExtractShopSubsystem* S = R.Sub<UAsdaExtractShopSubsystem>()) { S->CloseWindow(); }
			if (UAsdaPremiumPostSubsystem* S = R.Sub<UAsdaPremiumPostSubsystem>()) { S->CloseWindow(); }
			if (UAsdaPackagePanelSubsystem* Pk = R.Sub<UAsdaPackagePanelSubsystem>()) { Pk->ClosePanel(); }
			if (UAsdaQuestLogSubsystem* Q = R.Sub<UAsdaQuestLogSubsystem>()) { if (Q->IsOpen()) { Q->Close(); } }
		});
		// Уборка за собой: самотест не должен оставлять «персонаж занят» следующим в пакете (CashGift и др.).
		Run->Add(0.5f, [Round](FRun& R)
		{
			const UWorld* W = R.W();
			const UAsdaEquipShopSubsystem* Es = R.Sub<UAsdaEquipShopSubsystem>();
			const UAsdaSystemOptionSubsystem* So = R.Sub<UAsdaSystemOptionSubsystem>();
			const UAsdaInventorySubsystem* Inv = R.Sub<UAsdaInventorySubsystem>();
			const bool bEquip = Es && Es->IsOpen();
			const bool bTrade = UAsdaTradeStateSubsystem::IsExchangeWindowOpen(W);
			const bool bBusy  = So && So->IsLocalPlayerBusy();
			const bool bInv   = Inv && Inv->IsWindowOpen();
			R.Check(AsdaShopCanInteract(W), FString::Printf(TEXT("круг %d: уборка — `AsdaShopCanInteract` открыт (магазин снаряжения %d, обмен %d, занят %d, рюкзак %d)"),
				Round, bEquip ? 1 : 0, bTrade ? 1 : 0, bBusy ? 1 : 0, bInv ? 1 : 0));
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestIconsAfterTravel(
	TEXT("asda.SelfTest.IconsAfterTravel"),
	TEXT("Самотест иконок после НАСТОЯЩЕЙ смены карты: панель состава 26500 и награды журнала 700 на каждом круге, обход всех "
	     "подсистем на ссылки на виджеты снесённых окон. `travels=N` (2), `out=<папка>` для кадров, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaIconsTravelTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		int32 Travels = 2;
		FString Out;
		for (const FString& A : KA.Pos)
		{
			if (A.StartsWith(TEXT("travels="))) { Travels = FMath::Clamp(FCString::Atoi(*A.Mid(8)), 1, 5); }
			if (A.StartsWith(TEXT("out=")))     { Out = A.Mid(4); }
		}
		// Мир без своего персонажа — поднимаем его тем же путём, что соседние самотесты травела, и перезапускаемся.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			FString Again = FString::Printf(TEXT("%s selfready travels=%d"), kCmd, Travels);
			if (!Out.IsEmpty()) { Again += TEXT(" out=") + Out; }
			if (KA.bQuit) { Again += TEXT(" quit"); }
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle Hd;
			W->GetTimerManager().SetTimer(Hd, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }

		TSharedRef<FRun> R = MakeShared<FRun>();
		R->GI = GI;
		R->bQuit = KA.bQuit;
		R->Travels = Travels;
		R->OutDir = Out;
		AddRound(R, 0);
		for (int32 t = 1; t <= Travels; ++t)
		{
			// Настоящий травел UE (карта чередуется: Альпия ↔ она же — движок всё равно сносит мир и виджеты).
			R->Add(0.3f, [](FRun& Rn)
			{
				Rn.CopyShot();
				if (UWorld* Wd = Rn.W()) { UGameplayStatics::OpenLevel(Wd, FName(TEXT("/Game/Map/alpen/alpen")), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode")); }
			});
			R->Add(6.f, [](FRun& Rn) { Rn.Exec(TEXT("Asda.Load.CurrentMap 3")); Rn.Exec(TEXT("Asda.World.FakeSelfSpawn 256 256")); });
			R->Add(2.0f, [](FRun&) {});
			AddRound(R, t);
		}
		UE_LOG(LogAsdaIconsTravelTest, Log, TEXT("%s старт: переходов %d, коробки 58966/76405/79935, журнал — квест 130"), kPrefix, Travels);
		R->Start();
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
