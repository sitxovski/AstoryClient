// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ «ИКОНКА БРОНИ ПО ПОЛУ СВОЕГО ПЕРСОНАЖА» ПУТЁМ ИГРОКА (08.11.2026, пачка 18, баг 1 владельца). [ОРИГИНАЛ]
//
// ЧТО ПРОВЕРЯЕТ. Эталон рисует иконку обычного предмета через `CItemManager_GetImgOptionPtr/Value(imgIndex, g_ItemImgOptionVariant)`
//   (@0x9C3DF0/@0x9C3F40): `g_ItemImgOptionVariant` = пол своего персонажа (байт `CCharacter`+0x437 из блока 1017); у строки
//   `item_list[imgIndex]` с полем пола +0x58 == 0 — всегда мужской атлас, иначе 1 → мужской, 2 → женский. Значка пола в эталоне
//   нет: «женский знак» на броне — это женская картинка той же вещи (`item_img_table` +0xAC/+0xCC).
//   Тест для пола 1 и 2 кладёт в сумку три вещи — броню с парой картинок (поле пола 1), «общую» броню (поле 0, но картинки
//   в таблице разные) и коробку с полем 2 — надевает парную броню, открывает сумку, наводит НАСТОЯЩИЙ курсор на ячейку
//   (подсказка + правая инфо-панель 423) и сверяет ЖИВЫЕ кисти ячейки сумки, надетого слота и иконки панели с эталонной кистью
//   нужного пола (та же общая точка `AsdaUi::ApplyItemIcon` с явным полом в пробный `UImage`). Кадры — на каждую ячейку.
//
// Консоль: `asda.SelfTest.GenderIcon [legacy] [out=<папка>] [quit]`. Только офлайн (-AsdaNoNet).
//   `legacy` — ключи `asda.Item.IconSelfGender 0` и `asda.Item.IconUnisexMale 0` (прежний порт): ждём мужскую картинку всегда.
//   Кадры: `Reports/batch18/gender_icon/<язык>_<режим>_sex<пол>_<код>.png`. Итог — «ПОЛ-ИКОНКИ ИТОГ».
//   Сумка, надетое, пол своего персонажа и ключи возвращаются в конце.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGenderIconTest, Log, All);

namespace AsdaGenderIconTest
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.GenderIcon");   // имя команды
	const TCHAR* const kPrefix = TEXT("ПОЛ-ИКОНКИ");               // префикс строк журнала

	// Случай: код предмета, контейнер/ячейка сумки, вкладка, где он виден, пояснение.
	struct FCase { int32 Code; int32 Cont; int32 Cell; int32 Tab; const TCHAR* Why; };
	// Коды подобраны по таблицам (item_list + item_img_table, 08.11.2026): у всех трёх мужская и женская картинки РАЗНЫЕ.
	static const FCase kCases[] = {
		{ 20724, 1, 0, 1, TEXT("кираса с парой картинок (поле пола 1)") },
		{ 61741, 1, 1, 1, TEXT("«общая» кираса (поле пола 0, картинки разные)") },
		{ 64789, 2, 0, 0, TEXT("коробка с полем пола 2") },
	};
	constexpr int32 kWornCode = 20724;   // парная кираса — ещё и в надетый слот 1 (контейнер 3)
	constexpr int32 kWornSlot = 1;

	// Курсор в точку + событие движения (как `asda.SelfTest.MountTipFrame`).
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Щелчок ЛКМ в точке штатным путём окон (`SimulateClickAt`, как `asda.SelfTest.InvArrow`): эталон заполняет правую
	//   инфо-панель по ВЫБОРУ ячейки (`ItemTooltip_PopulateInfoPanel(g_pInventoryWnd, 3, слот)`), не по наведению.
	static void Click(UWorld* W, const FVector2D& P)
	{
		UGameInstance* G = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = G ? G->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { return; }
		Hover(P);
		WM->SimulateClickAt(P, /*HoldFrames*/ 2);
	}

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		bool bLegacy = false;
		int32 Ok = 0, Fail = 0;
		FString Out;
		TUniquePtr<FAsdaCVarScope> CVars;          // ключи — вернуть
		TArray<TFunction<float()>> Steps;          // шаг → пауза до следующего, с
		int32 Index = 0;
		FTimerHandle H;
		double NextTime = 0.0;
		uint8 SavedGender = 0;                     // пол своего персонажа до теста (блок 1017)
		TMap<FIntPoint, TArray<uint8>> Saved;      // (контейнер, ячейка) → запись до теста; пусто — ячейка была пуста
		TStrongObjectPtr<UImage> Probe;            // пробный `UImage` — эталонная кисть нужного пола

		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template<class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		void Expect(bool b, const FString& What, const FString& Det)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaGenderIconTest, Log, TEXT("%s %s: %s — %s"), kPrefix, b ? TEXT("OK  ") : TEXT("FAIL"), *What, *Det);
		}
		// Запомнить ячейку до теста (один раз).
		void Remember(int32 Cont, int32 Cell)
		{
			const FIntPoint K(Cont, Cell);
			if (Saved.Contains(K)) { return; }
			const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>();
			const FAsdaItemInstance* It = Inv ? Inv->GetCell(Cont, Cell) : nullptr;
			Saved.Add(K, (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>());
		}
		// Ожидаемый пол картинки — независимый оракул по таблице (строка item_list с номером imgIndex, поле +0x58).
		bool WantMale(int32 Code, uint8 Sex) const
		{
			if (bLegacy) { return true; }
			const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>();
			const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
			if (!Rec) { return true; }
			const TArray<FAsdaItemRecord>& All = IT->GetAllItems();
			const int32 Gate = All.IsValidIndex(Rec->ImgIndex) ? All[Rec->ImgIndex].ClassRestriction : Rec->ClassRestriction;
			return Gate == 0 || Sex != 2;
		}
		// Эталонная кисть: та же общая точка с ЯВНЫМ полом в пробный `UImage`. Правило «поле пола 0 → мужская» на время пробы
		//   выключено — проба отдаёт СЫРУЮ картинку запрошенного пола, и оракул не зависит от проверяемого кода.
		FSlateBrush WantBrush(int32 Code, bool bMale)
		{
			if (!Probe.IsValid()) { Probe.Reset(NewObject<UImage>(GetTransientPackage())); }
			FSlateBrush Empty; Probe->SetBrush(Empty);
			if (CVars) { CVars->Set(TEXT("asda.Item.IconUnisexMale"), 0); }
			AsdaUi::ApplyItemIcon(GI(), Probe.Get(), Code, FVector2D(64.f, 64.f), bMale ? 1 : 0, /*bWrap*/ true);
			if (CVars) { CVars->Set(TEXT("asda.Item.IconUnisexMale"), bLegacy ? 0 : 1); }
			return Probe->GetBrush();
		}
		static FString BrushStr(const FSlateBrush& B)
		{
			const UObject* R = B.GetResourceObject();
			const FBox2f UV = B.GetUVRegion();   // UE 5.8: FDeprecateBox2D → FBox2f
			return FString::Printf(TEXT("%s uv(%.4f,%.4f)-(%.4f,%.4f)"), R ? *R->GetName() : TEXT("нет"), UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
		}
		static bool SameBrush(const FSlateBrush& A, const FSlateBrush& B)
		{
			return A.GetResourceObject() != nullptr && A.GetResourceObject() == B.GetResourceObject()
				&& FBox2f(A.GetUVRegion()).Min.Equals(FBox2f(B.GetUVRegion()).Min, 1e-4f)
				&& FBox2f(A.GetUVRegion()).Max.Equals(FBox2f(B.GetUVRegion()).Max, 1e-4f);
		}
		// Сверить живую кисть с эталоном нужного пола; заодно доказать, что у случая картинки полов РАЗНЫЕ (иначе проверка пустая).
		void CheckBrush(const FString& Where, const UWidget* Live, int32 Code, uint8 Sex)
		{
			const bool bMale = WantMale(Code, Sex);
			const FSlateBrush Want = WantBrush(Code, bMale);
			const FSlateBrush Other = WantBrush(Code, !bMale);
			const UImage* Img = Cast<UImage>(Live);
			const FSlateBrush Got = Img ? Img->GetBrush() : FSlateBrush();
			Expect(Img && SameBrush(Got, Want) && !SameBrush(Got, Other),
				FString::Printf(TEXT("[пол %u] %s, код %d: картинка %s"), Sex, *Where, Code, bMale ? TEXT("мужская") : TEXT("женская")),
				FString::Printf(TEXT("живая %s | ждём %s | другой пол %s"), *BrushStr(Got), *BrushStr(Want), *BrushStr(Other)));
		}
		// Правая инфо-панель сумки: план иконки 423 (`INVEN_EQUIICON_<n>_0`, середина — число).
		const UImage* InfoIcon() const
		{
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* UW = *It;
				if (!UW || UW->GetWorld() != W.Get() || !UW->WidgetTree || !UW->IsInViewport()
					|| UW->GetClass()->GetName() != TEXT("WBP_INVEN_WIN_C")) { continue; }
				const UImage* Found = nullptr;
				UW->WidgetTree->ForEachWidget([&](UWidget* Wd)
				{
					const UImage* I = Cast<UImage>(Wd);
					if (Found || !I) { return; }
					const FString N = I->GetName();
					if (!N.StartsWith(TEXT("INVEN_EQUIICON_")) || !N.EndsWith(TEXT("_0"))) { return; }
					const FString Mid = N.Mid(15, N.Len() - 15 - 2);
					if (!Mid.IsEmpty() && Mid.IsNumeric()) { Found = I; }
				});
				if (Found) { return Found; }
			}
			return nullptr;
		}
		void Shot(const FString& Name) const
		{
			const FString File = Out / FString::Printf(TEXT("%s_%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()),
				bLegacy ? TEXT("legacy") : TEXT("after"), *Name);
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogAsdaGenderIconTest, Log, TEXT("%s кадр → %s"), kPrefix, *File);
		}
	};

	static void Finish(TSharedRef<FRun> R)
	{
		UWorld* W = R->W.Get();
		if (W) { W->GetTimerManager().ClearTimer(R->H); }
		if (UAsdaItemTooltipSubsystem* T = R->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		if (UAsdaInventorySubsystem* Inv = R->Sub<UAsdaInventorySubsystem>())
		{
			for (const TPair<FIntPoint, TArray<uint8>>& S : R->Saved)
			{
				Inv->ApplyExternalRecord(S.Value.Num() == FAsdaItemRecordWriter::kSize ? S.Value : FAsdaItemRecordWriter::ClearCell(S.Key.X, S.Key.Y),
					UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
			Inv->CloseWindow();
		}
		if (UAsdaNetSubsystem* Net = R->Sub<UAsdaNetSubsystem>()) { Net->SelfGender = R->SavedGender; }
		R->CVars.Reset();
		R->Probe.Reset();
		AsdaSelfTestKit::Finish(W, kCmd, R->bQuit, LogAsdaGenderIconTest, kPrefix, R->Ok, R->Fail,
			*FString::Printf(TEXT(" (режим %s, язык %s)"), R->bLegacy ? TEXT("legacy") : TEXT("эталон"), FAsdaLang::Code(FAsdaLang::Current())));
	}

	static void Build(TSharedRef<FRun> R)
	{
		FRun* P = &R.Get();
		// ── 0. ключи, запомнить сумку и надетое ──
		P->Steps.Add([P]() -> float
		{
			P->CVars = MakeUnique<FAsdaCVarScope>(P->W.Get(), kCmd);
			P->CVars->Set(TEXT("asda.Item.IconSelfGender"), P->bLegacy ? 0 : 1);
			P->CVars->Set(TEXT("asda.Item.IconUnisexMale"), P->bLegacy ? 0 : 1);
			for (const FCase& C : kCases) { P->Remember(C.Cont, C.Cell); }
			P->Remember(3, kWornSlot);
			if (UAsdaNetSubsystem* Net = P->Sub<UAsdaNetSubsystem>()) { P->SavedGender = Net->SelfGender; }
			return 0.2f;
		});
		for (const uint8 Sex : { (uint8)1, (uint8)2 })
		{
			// ── 1. пол своего персонажа, вещи в сумку и на тело, сумка открыта ──
			P->Steps.Add([P, Sex]() -> float
			{
				UAsdaNetSubsystem* Net = P->Sub<UAsdaNetSubsystem>();
				UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
				if (!Net || !Inv) { P->Expect(false, TEXT("подсистемы сети/сумки"), TEXT("нет")); return 0.f; }
				Net->SelfGender = Sex;   // эталон: байт блока 1017 (+27) = `g_ItemImgOptionVariant`
				Inv->CloseWindow();
				for (const FCase& C : kCases)
				{
					Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(C.Code, C.Cont, C.Cell, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
				}
				Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(kWornCode, 3, kWornSlot, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
				Inv->OpenWindow();   // окно открывается заново — иконки строятся с нуля при текущем поле
				return 1.0f;
			});
			for (const FCase& C : kCases)
			{
				// ── 2. вкладка, курсор мимо, затем на ячейку ──
				P->Steps.Add([P, C]() -> float
				{
					if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->SetTab(C.Tab); }
					Hover(FVector2D(5.f, 5.f));
					return 0.5f;
				});
				P->Steps.Add([P, C]() -> float
				{
					const UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
					FVector2D Mn, Mx;
					const UWidget* Icon = Inv ? Inv->DebugGetGridIcon(C.Cell) : nullptr;
					if (Icon && AsdaUi::WidgetAbsRect(Icon, Mn, Mx)) { Hover((Mn + Mx) * 0.5f); }
					return 0.8f;
				});
				// ── 3. сверка: ячейка сумки, подсказка, инфо-панель, надетый слот; кадр ──
				P->Steps.Add([P, C, Sex]() -> float
				{
					const UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
					const UAsdaItemTooltipSubsystem* T = P->Sub<UAsdaItemTooltipSubsystem>();
					P->CheckBrush(FString::Printf(TEXT("ячейка сумки (вкладка %d, ячейка %d) — %s"), C.Tab, C.Cell, C.Why),
						Inv ? Inv->DebugGetGridIcon(C.Cell) : nullptr, C.Code, Sex);
					const bool bTip = T && T->IsTooltipShown();
					P->Expect(bTip, FString::Printf(TEXT("[пол %u] подсказка по наведению, код %d"), Sex, C.Code), bTip ? TEXT("показана") : TEXT("НЕ показана"));
					if (C.Code == kWornCode)
					{
						P->CheckBrush(TEXT("надетый слот 1 (кираса)"), Inv ? Inv->DebugGetEquipSlotIcon(kWornSlot) : nullptr, kWornCode, Sex);
					}
					P->Shot(FString::Printf(TEXT("sex%u_%d"), Sex, C.Code));
					return 1.0f;
				});
				// ── 4. щелчок по ячейке → правая инфо-панель 423; сверка её иконки; кадр ──
				//   Только вкладка «Снаряжение»: на «Расходниках» эталон панель по выбору не раскрывает (`AutoExpandOnSelect`).
				if (C.Tab != 1) { continue; }
				P->Steps.Add([P, C]() -> float
				{
					const UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
					FVector2D Mn, Mx;
					const UWidget* Icon = Inv ? Inv->DebugGetGridIcon(C.Cell) : nullptr;
					if (Icon && AsdaUi::WidgetAbsRect(Icon, Mn, Mx)) { Click(P->W.Get(), (Mn + Mx) * 0.5f); }
					return 0.8f;
				});
				P->Steps.Add([P, C, Sex]() -> float
				{
					const UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
					P->Expect(Inv && Inv->IsPanelExpanded(), FString::Printf(TEXT("[пол %u] правая панель раскрыта щелчком, код %d"), Sex, C.Code),
						(Inv && Inv->IsPanelExpanded()) ? TEXT("да") : TEXT("НЕТ"));
					P->CheckBrush(FString::Printf(TEXT("иконка инфо-панели 423 после щелчка — %s"), C.Why), P->InfoIcon(), C.Code, Sex);
					P->Shot(FString::Printf(TEXT("sex%u_%d_panel"), Sex, C.Code));
					Hover(FVector2D(5.f, 5.f));
					return 0.5f;
				});
			}
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestGenderIcon(
	TEXT("asda.SelfTest.GenderIcon"),
	TEXT("Иконка брони по полу своего персонажа (сумка, подсказка, инфо-панель, надетый слот) путём игрока для пола 1 и 2; "
	     "кадры в Reports/batch18/gender_icon. `legacy` `out=<папка>` `quit`. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGenderIconTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		FString Out = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/batch18/gender_icon"));
		bool bReady = false, bLegacy = false;
		for (const FString& A : KA.Pos)
		{
			if (A.StartsWith(TEXT("out=")))                     { Out = A.Mid(4); }
			if (A == TEXT("selfready"))                         { bReady = true; }
			if (A.Equals(TEXT("legacy"), ESearchCase::IgnoreCase)) { bLegacy = true; }
		}
		// Мир без своего персонажа — поднять и перезапуститься (как соседние самотесты).
		if (!bReady && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			FString Again = FString::Printf(TEXT("%s selfready out=%s"), kCmd, *Out);
			if (bLegacy) { Again += TEXT(" legacy"); }
			if (KA.bQuit) { Again += TEXT(" quit"); }
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle Hd;
			W->GetTimerManager().SetTimer(Hd, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->W = W; R->bQuit = KA.bQuit; R->bLegacy = bLegacy; R->Out = Out;
		IFileManager::Get().MakeDirectory(*Out, /*Tree*/ true);
		Build(R);
		W->GetTimerManager().SetTimer(R->H, FTimerDelegate::CreateLambda([R]()
		{
			if (FPlatformTime::Seconds() < R->NextTime) { return; }
			if (FScreenshotRequest::IsScreenshotRequested()) { return; }
			if (!R->Steps.IsValidIndex(R->Index)) { Finish(R); return; }
			const float Wait = R->Steps[R->Index++]();
			R->NextTime = FPlatformTime::Seconds() + Wait;
		}), 0.1f, true);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
