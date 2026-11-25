// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ИКОНКА ПРЕДМЕТА В ПАНЕЛИ БЫСТРОГО ДОСТУПА = ИКОНКА В СУМКЕ» (05.11.2026): `asda.SelfTest.QuickSlotItemIcon [quit]`.
//   Жалоба игроков: «маунт, перетащенный на панель быстрого доступа, показывает чужую иконку (в части слотов — правильную)».
//   Причина — дефект порта: `UAsdaHotbarSubsystem::RebuildOneSlot` не сбрасывал признак `bIconWholeTexture` (его ставят только
//   ветки навыка/эмоции), и предмет в слоте, где раньше лежал навык/эмоция, рисовался клеткой 0 своего атласа. Ключ
//   `asda.Quick.IconFlagReset` (1 = исправлено, 0 = прежнее поведение).
//   Путь игрока, без сети (-AsdaNoNet), карта мира. Сумка и расходники заполняются штатным разбором батча (`SimulateBatch` =
//   4045/4048 + маркер конца), предмет кладётся в слот `AssignToHotbarSlot` (та же точка, что бросок мышью), страница с сервера
//   подаётся пакетом 5059 через `DebugFeedPacket` (вход в мир / переход). Иконка слота сверяется в МОДЕЛИ (атлас, клетка, размер,
//   признак «цельная текстура») и в КИСТИ картинки слота окна QUICK_NEW_WIN — против кисти, которую рисует сумка
//   (`AsdaUi::ApplyItemIcon`, общая точка окна инвентаря).
//     A. Все маунты (коды `special_itemlist`, которые мир разрешает в транспорт `LookupMount`, в т.ч. 79 кодов Fable 559–637)
//        × все 78 слотов панели, бросок из сумки: перед броском слот ЗАНЯТ навыком (или эмоцией 204, если навык не встал) —
//        именно это и ломало иконку. Колонка оружия: бросок спецпредмета обязан отбиваться (эталон), слот не меняется.
//     B. Те же маунты, восстановление с сервера: на все 6 страниц приходит 5059 с навыками, затем 5059 с маунтами.
//     C. Прочие предметы (`item_list`, по одному на каждый атлас иконок, контейнер 2 «Расходники») × все слоты, бросок.
//     D. Ключ 0 — дефект воспроизводится: тот же бросок в бывший слот навыка даёт клетку 0 атласа, а не клетку предмета.
//   Кадры PNG (`Reports/mount_qslot_icon/`): страница 0 с маунтами в бывших слотах навыков — ключ 0 и ключ 1.
//   Итог — «ИКОНКА-ПАНЕЛЬ ИТОГ: ok=… fail=…». Слоты панели и ячейки сумки/расходников в конце возвращаются как были.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaAssetPaths.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"                       // FScreenshotRequest — кадры PNG
#include "UObject/StrongObjectPtr.h"            // TStrongObjectPtr — временная картинка сумки

DEFINE_LOG_CATEGORY_STATIC(LogAsdaQuickIconTest, Log, All);

namespace AsdaQuickIconTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.QuickSlotItemIcon");   // имя команды самотеста
	static constexpr int32 kBagCells = 60;                                 // ёмкость сумки/расходников (P96, 12×5)
	static constexpr int32 kSlots = UAsdaHotbarSubsystem::kSlotCount;      // 78 слотов панели
	static constexpr int32 kPages = 6;                                     // страниц в пакете 5059 (5 основных + T-ряд)
	static constexpr int32 kEmotionPrime = 204;                            // эмоция «бой» (её кладёт UPCreateChar) — запасная «прошлая» начинка слота
	static constexpr int32 kSkillPrime = 3;                                // тип записи слота «навык»
	static constexpr int32 kEmotionType = 2;                               // тип записи слота «эмоция»
}

struct FAsdaQuickSlotItemIconSelfTest
{
	// Один проверяемый предмет: код, контейнер (1 сумка / 2 расходники), ячейка, количество в ячейке.
	struct FCase { int32 Code = 0; int32 Cont = 1; int32 Cell = 0; int32 Amount = 1; };

	struct FRun
	{
		TWeakObjectPtr<UWorld> W;                // мир прогона
		bool bQuit = false;                      // выйти после прогона
		int32 Ok = 0, Fail = 0;                  // счётчики проверок
		int32 Waits = 0;                         // попыток дождаться панели/сумки
		int32 PrimeSkill = 0;                    // навык, которым «занимаем» слот перед броском (0 — только эмоция)
		TArray<int32> Mounts;                    // коды маунтов (special_itemlist + LookupMount)
		TArray<int32> Others;                    // прочие предметы item_list (по одному на атлас)
		TArray<FCase> Batch;                     // текущая партия (до 60 предметов в одном контейнере)
		int32 MountPos = 0;                      // следующий маунт для партии
		int32 OtherPos = 0;                      // следующий прочий предмет для партии
		FAsdaHotbarSlot Saved[AsdaQuickIconTestLocal::kSlots];   // начинка панели до прогона
		int32 SavedPage = 0;                     // страница панели до прогона
		int32 NBagMismatchModel = 0, NBagMismatchBrush = 0, NChecked = 0, NWeaponRefused = 0, NWirePath = 0, NPrimeEmotion = 0;   // сводка
		TUniquePtr<FAsdaCVarScope> Keys;         // ключи консоли на время прогона
		TStrongObjectPtr<UImage> Ref;              // временная картинка: кисть, которую рисует сумка (сильная ссылка — GC её не соберёт)
		uint32 CapGen = 0;                       // перехват отправок (5067/5000 не уходят в сеть)
		TMap<int32, bool> Accepts;               // код → принят ли он панелью в первом слоте (дальше ответ обязан совпадать)
		TSet<int32> GateRefused;                 // коды, которые эталонный гейт `hotbarAssignInventoryItem` в панель не пускает
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; }
			else   { ++Fail; UE_LOG(LogAsdaQuickIconTest, Error, TEXT("ИКОНКА-ПАНЕЛЬ FAIL %s"), *What); }
		}
		void ExpectLoud(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaQuickIconTest, Log, TEXT("ИКОНКА-ПАНЕЛЬ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaQuickIconTest, Error, TEXT("ИКОНКА-ПАНЕЛЬ FAIL %s"), *What); }
		}
	};
	using FRef = TSharedRef<FRun>;

	template<class T> static T* Sub(const FRef& R)
	{
		UWorld* W = R->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<T>() : nullptr;
	}
	static void After(const FRef& R, float Sec, TFunction<void(const FRef&)> Fn)
	{
		UWorld* W = R->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, Fn = MoveTemp(Fn)]() { Fn(R); }), Sec, false);
	}
	// Картинка слота `Abs` в окне QUICK_NEW_WIN: основной ряд — узел `QUICK_NEW_SLOT<col+1>` (колонка 12 = `QUICK_NEW_SLOT_WEAPON`),
	//   T-ряд — `QUICK_NEW_SLOT_T<col+1>`. Видна только текущая страница основного ряда; слот 77 контрола не имеет.
	static UImage* SlotImage(const FRef& R, int32 Abs)
	{
		using namespace AsdaQuickIconTestLocal;
		const int32 MainCount = UAsdaHotbarSubsystem::kColsPerPage * UAsdaHotbarSubsystem::kMainPages;
		FString Want;
		if (Abs < MainCount)
		{
			const int32 Col = Abs % UAsdaHotbarSubsystem::kColsPerPage;
			Want = (Col == UAsdaHotbarSubsystem::kWeaponCol) ? FString(TEXT("QUICK_NEW_SLOT_WEAPON")) : FString::Printf(TEXT("QUICK_NEW_SLOT%d"), Col + 1);
		}
		else
		{
			const int32 Col = Abs - MainCount;
			if (Col >= 12) { return nullptr; }
			Want = FString::Printf(TEXT("QUICK_NEW_SLOT_T%d"), Col + 1);
		}
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R);
		UUserWidget* Win = WM ? WM->FindWindow(FName(TEXT("QUICK_NEW_WIN"))) : nullptr;
		if (!Win || !Win->WidgetTree) { return nullptr; }
		TArray<UWidget*> All;
		Win->WidgetTree->GetAllWidgets(All);
		UImage* Found = nullptr;
		for (UWidget* Wd : All)
		{
			FString Idc; int32 J = 0, P = 0;
			if (Wd && AsdaUi::ParsePlaneName(Wd->GetName(), Idc, J, P) && Idc == Want)
			{
				if (UImage* Img = Cast<UImage>(Wd)) { Found = Img; }
			}
		}
		return Found;
	}
	// Две кисти рисуют одно и то же: тот же ресурс и та же UV-область (допуск полпикселя атласа 2048).
	static bool SameBrush(const FSlateBrush& A, const FSlateBrush& B)
	{
		if (A.GetResourceObject() != B.GetResourceObject() || !A.GetResourceObject()) { return false; }
		const FBox2D UA = A.GetUVRegion(), UB = B.GetUVRegion();
		const float Eps = 0.25f / 2048.f;
		return UA.Min.Equals(UB.Min, Eps) && UA.Max.Equals(UB.Max, Eps);
	}
	static FString BrushText(const FSlateBrush& B)
	{
		const UObject* Res = B.GetResourceObject();
		const FBox2D U = B.GetUVRegion();
		return FString::Printf(TEXT("%s UV(%.4f,%.4f)-(%.4f,%.4f)"), Res ? *Res->GetName() : TEXT("<нет>"), U.Min.X, U.Min.Y, U.Max.X, U.Max.Y);
	}

	// Иконка слота `Abs` против иконки сумки для `C.Code`: модель (атлас/клетка/размер/признак) и, если слот виден, кисть.
	static void CheckSlot(const FRef& R, int32 Abs, const FCase& C, const TCHAR* Path, bool bVisible)
	{
		UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
		UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
		UWorld* W = R->W.Get();
		if (!Hb || !IT || !W) { return; }
		const FAsdaHotbarSlot& S = Hb->GetSlot(Abs);
		const FAsdaItemIcon Ic = IT->GetItemIcon(C.Code, true);
		++R->NChecked;
		const bool bModel = S.Type == 1 && S.IconAtlas == Ic.Atlas && S.IconX == Ic.CellX && S.IconY == Ic.CellY
			&& S.IconSize == Ic.CellSize && !S.bIconWholeTexture && S.Count == C.Amount;
		if (!bModel) { ++R->NBagMismatchModel; }
		R->Expect(bModel, FString::Printf(TEXT("%s: слот %d предмет %d — модель %s (%d,%d) %d цельная=%d кол-во=%d, сумка %s (%d,%d) %d кол-во=%d"),
			Path, Abs, C.Code, *S.IconAtlas, S.IconX, S.IconY, S.IconSize, S.bIconWholeTexture ? 1 : 0, S.Count,
			*Ic.Atlas, Ic.CellX, Ic.CellY, Ic.CellSize, C.Amount));
		if (!bVisible) { return; }
		UImage* Img = SlotImage(R, Abs);
		if (!Img) { return; }
		if (!R->Ref.IsValid()) { R->Ref.Reset(NewObject<UImage>(GetTransientPackage())); }
		const bool bRef = AsdaUi::ApplyItemIcon(W->GetGameInstance(), R->Ref.Get(), C.Code, FVector2D(32.f, 32.f), /*bMale*/ true, /*bWrap*/ true);
		const bool bBrush = bRef && SameBrush(Img->GetBrush(), R->Ref->GetBrush());
		if (!bBrush) { ++R->NBagMismatchBrush; }
		R->Expect(bBrush, FString::Printf(TEXT("%s: слот %d предмет %d — кисть слота %s, кисть сумки %s"),
			Path, Abs, C.Code, *BrushText(Img->GetBrush()), bRef ? *BrushText(R->Ref->GetBrush()) : TEXT("<сумка не нарисовала>")));
	}

	// Занять слот «прошлой» начинкой — навыком (бросок из книги) или, если не встал, эмоцией 204. Колонка оружия не принимает ни то,
	//   ни другое — там слот просто чистится. Возвращает, стоит ли после этого признак «цельная текстура».
	static bool PrimeSlot(const FRef& R, int32 Abs)
	{
		using namespace AsdaQuickIconTestLocal;
		UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
		if (!Hb) { return false; }
		if (Hb->ColumnOfAbsSlot(Abs) == UAsdaHotbarSubsystem::kWeaponCol && Abs < UAsdaHotbarSubsystem::kColsPerPage * UAsdaHotbarSubsystem::kMainPages)
		{
			Hb->ClearSlot(Abs);
			return false;
		}
		if (R->PrimeSkill > 0 && Hb->AssignSkillToSlot(Abs, R->PrimeSkill) && Hb->GetSlot(Abs).bIconWholeTexture) { return true; }
		++R->NPrimeEmotion;
		Hb->SetSlotContent(Abs, (uint8)kEmotionType, 0, kEmotionPrime, 0, 0);
		return Hb->GetSlot(Abs).bIconWholeTexture;
	}

	// Тело 5059: страница + 13 записей по 10 б (`u8 тип, u8 подтип, u16 ячейка, u32 кол-во, u16 код`).
	static TArray<uint8> Page5059(int32 Page, TFunctionRef<void(int32 Col, uint8* Rec)> Fill)
	{
		TArray<uint8> B;
		B.SetNumZeroed(1 + UAsdaHotbarSubsystem::kColsPerPage * 10);
		B[0] = (uint8)Page;
		for (int32 Col = 0; Col < UAsdaHotbarSubsystem::kColsPerPage; ++Col) { Fill(Col, B.GetData() + 1 + Col * 10); }
		return B;
	}

	static void Start(UWorld* W, bool bQuit);
	static void WaitReady(const FRef& R);
	static void Prepare(const FRef& R);
	static void NextBatch(const FRef& R);
	static void WaitBatch(const FRef& R, int32 Cont);
	static void RunDrag(const FRef& R, int32 Page);
	static void RunWire(const FRef& R, int32 Step);
	static void RunKeyZero(const FRef& R);
	static void Shots(const FRef& R, int32 Step);
	static void Finish(const FRef& R);
};

void FAsdaQuickSlotItemIconSelfTest::Start(UWorld* W, bool bQuit)
{
	using namespace AsdaQuickIconTestLocal;
	FRef R = MakeShared<FRun>();
	R->W = W; R->bQuit = bQuit;
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("нет сетевой подсистемы")); return; }
	if (Net->IsConnected()) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("живое соединение")); return; }
	if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
	{
		GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
		GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
	}
	WaitReady(R);
}

// Панель создаёт её тик (нужны номер карты и свой павн), тем же тиком встают мосты к книге навыков и к инвентарю — ждём до 20 с.
void FAsdaQuickSlotItemIconSelfTest::WaitReady(const FRef& R)
{
	using namespace AsdaQuickIconTestLocal;
	UWorld* W = R->W.Get();
	if (!W) { return; }
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	if (!Hb || !Hb->bSkillBridgeBound || !SlotImage(R, 0) || !Sub<UAsdaInventorySubsystem>(R))
	{
		if (++R->Waits > 40) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("нет панели QUICK_NEW_WIN / мостов за 20 с")); return; }
		After(R, 0.5f, [](const FRef& R) { WaitReady(R); });
		return;
	}
	Prepare(R);
}

void FAsdaQuickSlotItemIconSelfTest::Prepare(const FRef& R)
{
	using namespace AsdaQuickIconTestLocal;
	UWorld* W = R->W.Get();
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
	UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(R);
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	const UAsdaSkillWindowSubsystem* SW = Sub<UAsdaSkillWindowSubsystem>(R);
	const UAsdaSkillDatabase* Db = SW ? SW->GetDatabase() : nullptr;
	UAsdaTextureSubsystem* Tex = AsdaTexMgr();
	if (!Hb || !IT || !WS || !Net || !Tex) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("нет панели/таблиц/мира/сети/текстур")); return; }

	// Маунты: все коды второй таблицы, которые мир разрешает в транспорт (та же точка, что посадка по 4021/5382).
	for (int32 Code = 1; Code <= 100000; ++Code)
	{
		if (!IT->IsSpecialItemCode(Code)) { continue; }
		FAsdaSpecialItemlistRow Sp;
		if (!IT->GetSpecialItem(Code, Sp)) { continue; }
		FString Model, Anim; float H = 0.f;
		// ★ Маунт = спецпредмет подтипа 15 (транспорт; так же их считает `asda.W3.MountDump` и импорт Fable), который мир сажает.
		if (Sp.SubType == 15 && WS->LookupMount(Code, Model, Anim, H) && IT->GetItemIcon(Code, true).IsValid()) { R->Mounts.Add(Code); }
	}
	// Прочие предметы: по одному коду item_list на каждый атлас иконок, только те, что панель показывает (эталонные гейты
	//   `hotbarRebuildSlots`: не золото, не самоцвет, не тип 11 подтип 6..12, flagWord176 == 0) и ячейка не 0 (клетка 0 = та
	//   самая «чужая» картинка, на ней дефект не виден).
	TSet<FString> Atlases;
	for (int32 Code = 1; Code <= 100000 && R->Others.Num() < kBagCells; ++Code)
	{
		const FAsdaItemRecord* Rec = IT->GetItem(Code);
		if (!Rec || IT->IsSowelCode(Code) || IT->IsSpecialItemCode(Code) || Code == 20551 || Rec->FlagWord176 != 0
			|| (Rec->ItemType == 11 && Rec->ItemSubType >= 6 && Rec->ItemSubType <= 12)) { continue; }
		const FAsdaItemIcon Ic = IT->GetItemIcon(Code, true);
		if (!Ic.IsValid() || Ic.Cell == 0 || Atlases.Contains(Ic.Atlas)) { continue; }
		if (!Tex->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(Ic.Atlas))) { continue; }
		Atlases.Add(Ic.Atlas);
		R->Others.Add(Code);
	}
	// Навык для «прошлой» начинки слота — первый из книги с иконкой.
	if (Db)
	{
		TArray<int32> Cand;
		for (const TPair<int32, FAsdaSkillDef>& It : Db->GetAll())
		{
			if (It.Key >= 501 && It.Key <= 1201 && !It.Value.IconName.IsEmpty()
				&& Tex->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(It.Value.IconName))) { Cand.Add(It.Key); }
		}
		Cand.Sort();
		if (Cand.Num() > 0) { R->PrimeSkill = Cand[0]; }
	}
	int32 NFable = 0;
	for (int32 C : R->Mounts) { NFable += (C >= 559 && C <= 637) ? 1 : 0; }
	UE_LOG(LogAsdaQuickIconTest, Log, TEXT("ИКОНКА-ПАНЕЛЬ: маунтов %d (Fable 559–637: %d), прочих предметов %d (атласов %d), навык-начинка %d"),
		R->Mounts.Num(), NFable, R->Others.Num(), Atlases.Num(), R->PrimeSkill);
	R->ExpectLoud(R->Mounts.Num() > 0 && NFable == 79, FString::Printf(TEXT("найдены маунты: всего %d, Fable %d (ожидание 79)"), R->Mounts.Num(), NFable));
	if (R->Mounts.Num() == 0) { Finish(R); return; }

	for (int32 I = 0; I < kSlots; ++I) { R->Saved[I] = Hb->GetSlot(I); }
	R->SavedPage = Hb->GetCurrentPage();
	R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
	R->Keys->Set(TEXT("asda.Quick.IconFlagReset"), 1);
	R->Keys->Set(TEXT("asda.Inven.LegacyHotbarSilentReject"), 1);   // отказ колонки оружия — без нотиса в очередь окон
	R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [](uint16, const uint8*, int32, bool, TCHAR) {}, 0, kCmd);
	if (R->CapGen == 0) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("перехват отправок занят")); return; }
	NextBatch(R);
}

// Партия: до 60 маунтов в сумку (контейнер 1), затем до 60 прочих в «Расходники» (контейнер 2); штатный батч + маркер конца.
void FAsdaQuickSlotItemIconSelfTest::NextBatch(const FRef& R)
{
	using namespace AsdaQuickIconTestLocal;
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	if (!Inv) { Finish(R); return; }
	R->Batch.Reset();
	int32 Cont = 0;
	if (R->MountPos < R->Mounts.Num())
	{
		Cont = 1;
		for (int32 Cell = 0; Cell < kBagCells && R->MountPos < R->Mounts.Num(); ++Cell)
		{
			FCase C; C.Code = R->Mounts[R->MountPos++]; C.Cont = 1; C.Cell = Cell; C.Amount = 1 + (Cell % 5);
			R->Batch.Add(C);
		}
	}
	else if (R->OtherPos < R->Others.Num())
	{
		Cont = 2;
		for (int32 Cell = 0; Cell < kBagCells && R->OtherPos < R->Others.Num(); ++Cell)
		{
			FCase C; C.Code = R->Others[R->OtherPos++]; C.Cont = 2; C.Cell = Cell; C.Amount = 2 + (Cell % 7);
			R->Batch.Add(C);
		}
	}
	else
	{
		RunWire(R, 0);
		return;
	}
	TArray<uint8> Raw;
	for (const FCase& C : R->Batch)
	{
		const int32 Base = Raw.Num();
		Raw.AddZeroed(57);
		uint8* P = Raw.GetData() + Base;
		FMemory::Memcpy(P, &C.Code, 4);
		P[4] = (uint8)C.Cont;
		P[5] = (uint8)(C.Cell & 0xFF); P[6] = (uint8)((C.Cell >> 8) & 0xFF);
		FMemory::Memcpy(P + 9, &C.Amount, 4);
	}
	Inv->SimulateBatch(Cont, Raw);
	R->Waits = 0;
	After(R, 0.2f, [Cont](const FRef& R) { WaitBatch(R, Cont); });
}

void FAsdaQuickSlotItemIconSelfTest::WaitBatch(const FRef& R, int32 Cont)
{
	using namespace AsdaQuickIconTestLocal;
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	const FAsdaItemInstance* Last = (Inv && R->Batch.Num() > 0) ? Inv->GetCell(Cont, R->Batch.Last().Cell) : nullptr;
	if (!Inv || !Inv->IsContainerReceived(Cont) || !Last || Last->ItemCode != R->Batch.Last().Code)
	{
		if (++R->Waits > 50) { R->ExpectLoud(false, FString::Printf(TEXT("батч контейнера %d не применился за 10 с"), Cont)); Finish(R); return; }
		After(R, 0.2f, [Cont](const FRef& R) { WaitBatch(R, Cont); });
		return;
	}
	R->Waits = 0;
	RunDrag(R, 0);
}

// A/C: каждый предмет партии × каждый слот панели — бросок из сумки в слот, занятый навыком/эмоцией. Видимость кисти: страница
//   переключается штатным `SetPage` (вне боя он ОТЛОЖЕН — действие персонажа 104 применяет страницу на следующем тике), поэтому
//   страница обрабатывается только после того, как реально стала текущей; T-ряд виден всегда (проверяется со страницей 0).
//   Отказ гейта эталона (`hotbarAssignInventoryItem`: спецпредмет с флагом +100 == 2, предмет с полем +0xB4 == 1) — не ошибка,
//   но он обязан быть ОДИНАКОВЫМ во всех слотах: первый ответ по коду запоминается, остальные с ним сверяются.
void FAsdaQuickSlotItemIconSelfTest::RunDrag(const FRef& R, int32 Page)
{
	using namespace AsdaQuickIconTestLocal;
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	if (!Hb || !Inv) { Finish(R); return; }
	if (Page >= UAsdaHotbarSubsystem::kMainPages) { NextBatch(R); return; }
	if (Hb->GetCurrentPage() != Page)
	{
		if (R->Waits == 0) { Hb->SetPage(Page); }
		if (++R->Waits > 50) { R->ExpectLoud(false, FString::Printf(TEXT("страница %d панели не стала текущей за 5 с"), Page + 1)); Finish(R); return; }
		After(R, 0.1f, [Page](const FRef& R) { RunDrag(R, Page); });
		return;
	}
	R->Waits = 0;
	const int32 MainCount = UAsdaHotbarSubsystem::kColsPerPage * UAsdaHotbarSubsystem::kMainPages;
	const TCHAR* Path = (R->Batch.Num() > 0 && R->Batch[0].Cont == 1) ? TEXT("бросок маунта") : TEXT("бросок предмета (расходники)");
	for (int32 Abs = 0; Abs < kSlots; ++Abs)
	{
		const bool bOnPage = (Abs >= Page * UAsdaHotbarSubsystem::kColsPerPage && Abs < (Page + 1) * UAsdaHotbarSubsystem::kColsPerPage);
		if (Abs < MainCount && !bOnPage) { continue; }       // слот основного ряда — когда его страница на экране
		if (Abs >= MainCount && Page != 0) { continue; }      // T-ряд — один раз
		const bool bWeapon = Abs < MainCount && (Abs % UAsdaHotbarSubsystem::kColsPerPage) == UAsdaHotbarSubsystem::kWeaponCol;
		for (const FCase& C : R->Batch)
		{
			PrimeSlot(R, Abs);
			const FAsdaHotbarSlot Before = Hb->GetSlot(Abs);
			const bool bOk = Inv->AssignToHotbarSlot(C.Cont, C.Cell, Abs);
			if (bWeapon)
			{
				// Колонка оружия: эталон `hotbarAssignInventoryItem` отбивает спецпредмет и не-оружие; из контейнера 2 — всегда.
				const bool bRefused = !bOk && Hb->GetSlot(Abs).Type == Before.Type;
				if (bRefused) { ++R->NWeaponRefused; }
				R->Expect(bRefused, FString::Printf(TEXT("%s: колонка оружия %d отбивает предмет %d"), Path, Abs, C.Code));
				continue;
			}
			if (const bool* First = R->Accepts.Find(C.Code))
			{
				R->Expect(*First == bOk, FString::Printf(TEXT("%s: слот %d — ответ на предмет %d (%s) тот же, что в первом слоте (%s)"),
					Path, Abs, C.Code, bOk ? TEXT("принят") : TEXT("отбит"), *First ? TEXT("принят") : TEXT("отбит")));
			}
			else
			{
				R->Accepts.Add(C.Code, bOk);
				if (!bOk)
				{
					R->GateRefused.Add(C.Code);
					UE_LOG(LogAsdaQuickIconTest, Log, TEXT("ИКОНКА-ПАНЕЛЬ: предмет %d в панель не кладётся (гейт эталона hotbarAssignInventoryItem) — сверяется только одинаковость отказа"), C.Code);
				}
			}
			if (bOk) { CheckSlot(R, Abs, C, Path, /*bVisible*/ true); }
			else { R->Expect(Hb->GetSlot(Abs).Type == Before.Type, FString::Printf(TEXT("%s: отказ по предмету %d не тронул слот %d"), Path, C.Code, Abs)); }
		}
	}
	After(R, 0.05f, [Page](const FRef& R) { RunDrag(R, Page + 1); });
}

// B: восстановление с сервера. Шаг 0 — на все 6 страниц приходит 5059 с навыками/эмоциями (прошлая раскладка), шаг 1 — 5059 с
//   маунтами партии (сумка заполняется заново первыми 60 маунтами), шаг 2 — проверка; дальше следующая порция маунтов.
void FAsdaQuickSlotItemIconSelfTest::RunWire(const FRef& R, int32 Step)
{
	using namespace AsdaQuickIconTestLocal;
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	if (!Hb || !Inv || !Net) { Finish(R); return; }
	const int32 NMounts = R->Mounts.Num();
	// Сумка: маунты по кругу в 60 ячейках; порция `Step/3` кладёт в слоты маунты начиная с `First`.
	const int32 Round = Step / 3, Phase = Step % 3;
	const int32 First = Round * kSlots;
	if (First >= NMounts)
	{
		// Дальше кисть сверяется на странице 0 — смена страницы вне боя отложена (действие 104), ждём её.
		if (Hb->GetCurrentPage() == 0) { R->Waits = 0; RunKeyZero(R); return; }
		if (R->Waits == 0) { Hb->SetPage(0); }
		if (++R->Waits > 50) { R->ExpectLoud(false, TEXT("страница 1 панели не стала текущей за 5 с")); Finish(R); return; }
		After(R, 0.1f, [Step](const FRef& R) { RunWire(R, Step); });
		return;
	}
	auto CaseFor = [&](int32 Abs) -> FCase
	{
		FCase C;
		const int32 Idx = (First + Abs) % NMounts;
		C.Code = R->Mounts[Idx]; C.Cont = 1; C.Cell = Abs % kBagCells; C.Amount = 1 + (Abs % 5);
		return C;
	};
	if (Phase == 0)
	{
		// Сумка под порцию: ячейка `Abs % 60` = маунт слота `Abs` (слоты 60..77 делят ячейки 0..17 с тем же кодом — берём код ячейки).
		TArray<uint8> Raw;
		for (int32 Cell = 0; Cell < kBagCells; ++Cell)
		{
			const FCase C = CaseFor(Cell);
			const int32 Base = Raw.Num();
			Raw.AddZeroed(57);
			uint8* P = Raw.GetData() + Base;
			FMemory::Memcpy(P, &C.Code, 4);
			P[4] = 1;
			P[5] = (uint8)(Cell & 0xFF); P[6] = (uint8)((Cell >> 8) & 0xFF);
			FMemory::Memcpy(P + 9, &C.Amount, 4);
		}
		Inv->SimulateBatch(1, Raw);
		for (int32 Page = 0; Page < kPages; ++Page)
		{
			const TArray<uint8> B = Page5059(Page, [&](int32 Col, uint8* Rec)
			{
				const bool bSkill = R->PrimeSkill > 0 && (Col % 2 == 0);
				Rec[0] = bSkill ? (uint8)kSkillPrime : (uint8)kEmotionType;
				const int32 Index = bSkill ? 0 : kEmotionPrime;
				Rec[2] = (uint8)(Index & 0xFF); Rec[3] = (uint8)((Index >> 8) & 0xFF);
				const int32 Id = bSkill ? R->PrimeSkill : 0;
				Rec[8] = (uint8)(Id & 0xFF); Rec[9] = (uint8)((Id >> 8) & 0xFF);
			});
			Net->DebugFeedPacket(5059, B);
		}
		After(R, 0.3f, [Step](const FRef& R) { RunWire(R, Step + 1); });
		return;
	}
	if (Phase == 1)
	{
		int32 NPrimed = 0;
		for (int32 Abs = 0; Abs < kSlots; ++Abs) { NPrimed += Hb->GetSlot(Abs).bIconWholeTexture ? 1 : 0; }
		R->ExpectLoud(NPrimed >= kSlots - 2, FString::Printf(TEXT("5059 порция %d: прошлая раскладка навыков/эмоций встала в %d слотов из %d"), Round, NPrimed, kSlots));
		for (int32 Page = 0; Page < kPages; ++Page)
		{
			const TArray<uint8> B = Page5059(Page, [&](int32 Col, uint8* Rec)
			{
				const int32 Abs = Page * UAsdaHotbarSubsystem::kColsPerPage + Col;
				const FCase C = CaseFor(Abs % kBagCells);
				Rec[0] = 1; Rec[1] = 1;
				Rec[2] = (uint8)(C.Cell & 0xFF); Rec[3] = (uint8)((C.Cell >> 8) & 0xFF);
				FMemory::Memcpy(Rec + 4, &C.Amount, 4);
				Rec[8] = (uint8)(C.Code & 0xFF); Rec[9] = (uint8)((C.Code >> 8) & 0xFF);
			});
			Net->DebugFeedPacket(5059, B);
		}
		After(R, 0.3f, [Step](const FRef& R) { RunWire(R, Step + 1); });
		return;
	}
	// Phase 2 — сверка всех 78 слотов (кисть — текущей страницы и T-ряда).
	const int32 MainCount = UAsdaHotbarSubsystem::kColsPerPage * UAsdaHotbarSubsystem::kMainPages;
	for (int32 Abs = 0; Abs < kSlots; ++Abs)
	{
		const bool bVisible = Abs >= MainCount || (Abs / UAsdaHotbarSubsystem::kColsPerPage) == Hb->GetCurrentPage();
		++R->NWirePath;
		CheckSlot(R, Abs, CaseFor(Abs % kBagCells), TEXT("5059 восстановление"), bVisible);
	}
	After(R, 0.05f, [Step](const FRef& R) { RunWire(R, Step + 1); });
}

// D: ключ 0 — дефект воспроизводится (клетка 0 атласа вместо клетки маунта), ключ 1 — снова верно.
void FAsdaQuickSlotItemIconSelfTest::RunKeyZero(const FRef& R)
{
	using namespace AsdaQuickIconTestLocal;
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
	if (!Hb || !Inv || !IT) { Finish(R); return; }
	Hb->SetPage(0);
	// Маунт с клеткой не 0 в ячейке 0 сумки (после шага B сумка заполнена маунтами по кругу).
	const FAsdaItemInstance* I0 = Inv->GetCell(1, 0);
	int32 Cell = -1;
	for (int32 C = 0; C < kBagCells; ++C)
	{
		const FAsdaItemInstance* I = Inv->GetCell(1, C);
		if (I && !I->IsEmpty() && IT->GetItemIcon(I->ItemCode, true).Cell != 0) { Cell = C; break; }
	}
	(void)I0;
	if (Cell < 0) { R->ExpectLoud(false, TEXT("ключ 0: в сумке нет маунта с клеткой иконки не 0")); Shots(R, 0); return; }
	const FAsdaItemInstance* I = Inv->GetCell(1, Cell);
	FCase C; C.Code = I->ItemCode; C.Cont = 1; C.Cell = Cell; C.Amount = I->Amount;
	R->Keys->Set(TEXT("asda.Quick.IconFlagReset"), 0);
	PrimeSlot(R, 0);
	Inv->AssignToHotbarSlot(1, Cell, 0);
	const FAsdaHotbarSlot& S = Hb->GetSlot(0);
	R->ExpectLoud(S.Type == 1 && S.bIconWholeTexture,
		FString::Printf(TEXT("ключ 0: маунт %d в бывшем слоте навыка — признак «цельная текстура» остался (дефект воспроизведён: рисуется клетка 0 атласа %s)"),
			C.Code, *S.IconAtlas));
	R->Keys->Set(TEXT("asda.Quick.IconFlagReset"), 1);
	PrimeSlot(R, 0);
	Inv->AssignToHotbarSlot(1, Cell, 0);
	const int32 FailBefore = R->Fail;
	CheckSlot(R, 0, C, TEXT("ключ 1 после ключа 0"), true);
	R->ExpectLoud(R->Fail == FailBefore, FString::Printf(TEXT("ключ 1: тот же маунт %d рисуется своей клеткой (%d)"), C.Code, IT->GetItemIcon(C.Code, true).Cell));
	Shots(R, 0);
}

// Кадры: страница 0 — 12 маунтов в бывших слотах навыков; кадр с ключом 0 (дефект) и с ключом 1 (исправлено).
void FAsdaQuickSlotItemIconSelfTest::Shots(const FRef& R, int32 Step)
{
	using namespace AsdaQuickIconTestLocal;
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	if (!Hb || !Inv || Step >= 4) { Finish(R); return; }
	const FString Dir = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), TEXT("../Reports/mount_qslot_icon"));
	if (Step == 0 || Step == 2)
	{
		const int32 Key = (Step == 0) ? 0 : 1;
		R->Keys->Set(TEXT("asda.Quick.IconFlagReset"), Key);
		Hb->SetPage(0);
		for (int32 Col = 0; Col < UAsdaHotbarSubsystem::kWeaponCol; ++Col)
		{
			PrimeSlot(R, Col);
			Inv->AssignToHotbarSlot(1, Col, Col);
		}
		After(R, 0.5f, [Step](const FRef& R) { Shots(R, Step + 1); });
		return;
	}
	const FString Path = FPaths::Combine(Dir, (Step == 1) ? TEXT("hotbar_mounts_key0_defect.png") : TEXT("hotbar_mounts_key1_fixed.png"));
	FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/true, /*bAddUniqueSuffix=*/false);
	UE_LOG(LogAsdaQuickIconTest, Log, TEXT("ИКОНКА-ПАНЕЛЬ: кадр %s"), *Path);
	After(R, 0.5f, [Step](const FRef& R) { Shots(R, Step + 1); });
}

void FAsdaQuickSlotItemIconSelfTest::Finish(const FRef& R)
{
	using namespace AsdaQuickIconTestLocal;
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	if (Inv)
	{
		for (int32 Cont = 1; Cont <= 2; ++Cont)
		{
			for (int32 Cell = 0; Cell < kBagCells; ++Cell) { Inv->DebugPutItem(Cont, Cell, 0, 0); }
		}
	}
	if (Hb && R->Keys)
	{
		for (int32 I = 0; I < kSlots; ++I)
		{
			const FAsdaHotbarSlot& S = R->Saved[I];
			if (S.IsEmpty()) { Hb->ClearSlot(I); }
			else { Hb->SetSlotContent(I, S.Type, S.SubType, S.Index, S.Count, S.Id); }
		}
		Hb->SetPage(R->SavedPage);
	}
	if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R)) { if (R->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, R->CapGen); } }
	if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R)) { WM->ClearMsgBoxQueue(TEXT("самотест QuickSlotItemIcon")); }
	if (R->Keys) { R->Keys->Restore(); }
	const FString Tail = FString::Printf(TEXT(" (маунтов %d, прочих %d, сверок слотов %d: расхождений модели %d, кисти %d; путь 5059 %d; отбито колонкой оружия %d; гейт эталона %d кодов; начинка-эмоция %d)"),
		R->Mounts.Num(), R->Others.Num(), R->NChecked, R->NBagMismatchModel, R->NBagMismatchBrush, R->NWirePath, R->NWeaponRefused, R->GateRefused.Num(), R->NPrimeEmotion);
	AsdaSelfTestKit::Finish(R->W.Get(), kCmd, R->bQuit, LogAsdaQuickIconTest, TEXT("ИКОНКА-ПАНЕЛЬ"), R->Ok, R->Fail, *Tail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestQuickSlotItemIcon(
	TEXT("asda.SelfTest.QuickSlotItemIcon"),
	TEXT("Самотест: иконка предмета в панели быстрого доступа = иконка в сумке. Все маунты × все 78 слотов (бросок из сумки в слот, "
	     "где был навык/эмоция, и восстановление пакетом 5059), прочие предметы по одному на атлас; ключ asda.Quick.IconFlagReset 0 — "
	     "дефект; кадры PNG в Reports/mount_qslot_icon. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaQuickIconTestLocal::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { FAsdaQuickSlotItemIconSelfTest::Start(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaQuickIconTestLocal::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { FAsdaQuickSlotItemIconSelfTest::Start(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
