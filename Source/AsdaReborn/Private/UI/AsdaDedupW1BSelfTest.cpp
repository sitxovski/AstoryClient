// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ В1 (часть B, интерфейс) АУДИТА ДУБЛЕЙ №2 — СВЕРКА ВЫНЕСЕННЫХ ТОЧЕК С ПРЕЖНИМИ КОПИЯМИ
//   (11.10.2026; план `Reports/dedup_audit2/00_PLAN.md` §5 В1, отчёт `wave1_B.md`).
//
// ★ ЗАЧЕМ. Волна В1 — ЧИСТЫЙ ВЫНОС: кисти иконок, геометрия попапов, ответы «это я?», шапка трекера подсказки и шаги
//   каркаса окна меняться не должны. Здесь каждая новая общая точка сравнивается с КОПИЕЙ ПРЕЖНЕГО КОДА (функции `Old*`
//   ниже — дословно тела до правки, из бэкапа `_backup/2026-10-11_dedup2_w1B`) на одних и тех же входах:
//     • U4a — `AsdaUi::ApplyAtlasCellIcon` против портрета и иконок состояний панели цели (все атласы портретов из
//       DT_MobStats, «char_emblems», все атласы stateinfotip; ячейки 0…399; несуществующий атлас и пустое имя);
//     • U4b — `AsdaUi::ApplyJuiIconRegionTex` против ветки «клетка атласа» хотбара (все атласы иконок предметов; клетки
//       из данных и случайные — с вылетом за край для заворота) и прежнее тело `ApplyJuiIconCell/Rect` против нового;
//     • U4c — `AsdaUi::ApplyItemIcon(…, 64×64, заворот)` против `GetItemIcon + ApplyJuiIconCell(…, 64)` подтверждения
//       военного магазина — по ВСЕМ кодам item_list, special_itemlist, sowell_list и кодам 0 / −1 / несуществующему;
//     • U16 — `ClampPopupToViewport` против клампа меню 28000 и панели 43058 (бит в бит) и меню 2000 (та же математика, другая
//       запись: разница — только последний бит округления, печатается максимум); `CenteredOrigin` против трёх копий
//       формулы центра (бит в бит); `CenterRootOnViewport` против прежнего сдвига детей корня (на настоящих канвах);
//     • U5a — `AsdaSelf::IsSelfUserIdx` против метода `UAsdaNetSubsystem::IsSelfUserIdx` (номер известен/нет × индексы)
//       при ключе `asda.Self.StrictUnknownNum 1`; при ключе 0 печатается число расхождений (смысл ключа, не дефект);
//     • U18 — `UAsdaItemTooltipSubsystem::BeginHoverTip` против прежней шапки (ключ и ответ «показывать») на случайных
//       последовательностях наведения;
//     • U22 — `FAsdaJuiWindowHandle::EnsureLazy`: порядок шагов (сброс → открыть → привязка), «кэш годен» без шагов,
//       путь «не загрузился» (сброс + лог, привязки нет).
//   Входы случайных групп — детерминированный xorshift64 (зерно постоянное).
//
// Консоль: `asda.Dedup.W1BSelfTest [случаев=2000] [quit]`. Строки — «САМОТЕСТ-В1Б ok|FAIL …», итог — «САМОТЕСТ-В1Б ИТОГ: ok=… fail=…».
//   Только офлайн-отладка (`-AsdaNoNet`); пакеты не шлются. Побочные следы: временное окно-проба U22 (`W1B_LAZY_PROBE`)
//   закрывается в конце; поля номера своего игрока возвращаются как были.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaSelf.h"
#include "AsdaSelfTestBatch.h"
#include "AsdaDataTableCache.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Data/AsdaMobStatsRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Npc/AsdaMobActor.h"
#include "Resources/AsdaAssetPaths.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDedupW1B, Log, All);

namespace AsdaDedupW1B
{
	// Детерминированный генератор (то же устройство, что у самотеста В1-A).
	struct FRng
	{
		uint64 S;
		explicit FRng(uint64 Seed) : S(Seed ? Seed : 0x9E3779B97F4A7C15ull) {}
		uint32 Next() { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return (uint32)(S >> 32); }
		int32 Range(int32 N) { return N > 0 ? (int32)(Next() % (uint32)N) : 0; }
		double Real(double Lo, double Hi) { return Lo + (Hi - Lo) * ((double)Next() / 4294967295.0); }
	};

	// Учёт групп: группа ok, если все её сравнения совпали; первая разница печатается.
	struct FTally
	{
		int32 Ok = 0, Fail = 0;
		void Group(const TCHAR* What, int32 N, int32 Bad, const FString& FirstBad, const FString& Extra = FString())
		{
			if (Bad == 0 && N > 0)
			{
				++Ok;
				UE_LOG(LogAsdaDedupW1B, Display, TEXT("САМОТЕСТ-В1Б ok %s: совпало %d из %d%s"), What, N, N, *Extra);
			}
			else
			{
				++Fail;
				UE_LOG(LogAsdaDedupW1B, Error, TEXT("САМОТЕСТ-В1Б FAIL %s: расхождений %d из %d%s%s%s"), What, Bad, N,
					FirstBad.IsEmpty() ? TEXT("") : TEXT("; первое: "), *FirstBad, *Extra);
			}
		}
	};

	// Кисть строкой (для первой разницы).
	FString BrushStr(const FSlateBrush& B)
	{
		const FBox2f UV = B.GetUVRegion();
		const UObject* R = B.GetResourceObject();
		return FString::Printf(TEXT("[%s uv=(%.9g,%.9g)-(%.9g,%.9g)%s размер=%.9gx%.9g рисовать=%d]"), R ? *R->GetName() : TEXT("нет"),
			UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y, UV.bIsValid ? TEXT("") : TEXT("(нет)"), B.ImageSize.X, B.ImageSize.Y, (int32)B.DrawAs);
	}

	// Кисти равны бит в бит по всему, что видно на экране: ресурс, UV, размер, способ рисования, плюс общий `operator==`.
	bool SameBrush(const FSlateBrush& A, const FSlateBrush& B)
	{
		const FBox2f UA = A.GetUVRegion(), UB = B.GetUVRegion();
		return A == B && A.GetResourceObject() == B.GetResourceObject() && UA.bIsValid == UB.bIsValid && UA.Min == UB.Min && UA.Max == UB.Max
			&& A.ImageSize == B.ImageSize && A.DrawAs == B.DrawAs;
	}

	// Опорная кисть «до вызова»: одинаковая у обеих картинок, чтобы «кисть не тронута» тоже сравнивалась.
	void Reset2(UImage* A, UImage* B)
	{
		FSlateBrush S;
		S.ImageSize = FVector2D(7.f, 9.f);
		S.SetUVRegion(FBox2f(FVector2f(0.125f, 0.25f), FVector2f(0.5f, 0.75f)));
		A->SetBrush(S);
		B->SetBrush(S);
	}

	// Сверка пары (кисти + ответы); первая разница — в FirstBad.
	void Cmp(UImage* A, UImage* B, bool RA, bool RB, const FString& Ctx, int32& Bad, FString& FirstBad)
	{
		if (RA == RB && SameBrush(A->GetBrush(), B->GetBrush())) { return; }
		if (Bad++ == 0)
		{
			FirstBad = FString::Printf(TEXT("%s: было %d %s, стало %d %s"), *Ctx, RA ? 1 : 0, *BrushStr(A->GetBrush()), RB ? 1 : 0,
				*BrushStr(B->GetBrush()));
		}
	}

	// ══════════════ ПРЕЖНИЕ КОПИИ (дословно из бэкапа 2026-10-11_dedup2_w1B) ══════════════

	// U4a: `UAsdaTargetFrameSubsystem::SetPortraitFromAtlas` (кисть; ответ false = «атлас не найден»).
	bool OldTargetPortrait(UImage* Portrait, const FString& AtlasName, int32 Cell, int32 CellPx, int32 Cols)
	{
		if (!Portrait || AtlasName.IsEmpty()) { return false; }
		UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
		UTexture2D* Atlas = TexMgr ? TexMgr->LoadUITexture(FName(*AtlasName)) : nullptr;
		if (!Atlas) { return false; }
		const FIntPoint Imp = AsdaUi::LogicalAtlasSize(Atlas);
		const float TexW = (float)FMath::Max(1, Imp.X);
		const float TexH = (float)FMath::Max(1, Imp.Y);
		const float Cs = (float)FMath::Max(1, CellPx);
		const int32 C = FMath::Max(1, Cols);
		const float X = Cs * (float)(Cell % C);
		const float Y = Cs * (float)(Cell / C);
		FSlateBrush B;
		B.SetResourceObject(Atlas);
		B.ImageSize = FVector2D(Cs, Cs);
		B.SetUVRegion(FBox2D(FVector2D(X / TexW, Y / TexH), FVector2D((X + Cs) / TexW, (Y + Cs) / TexH)));
		Portrait->SetBrush(B);
		return true;
	}

	// U4a: тело цикла `UAsdaTargetFrameSubsystem::RefreshEnemyStateIcons` (kStateIconPx = 24, kStateIconCols = 10).
	bool OldTargetStateIcon(UImage* Icon, const FString& Atlas, int32 Cell)
	{
		UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
		UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(FName(*Atlas)) : nullptr;
		if (!Tex || !Icon) { return false; }
		const FIntPoint Imp = AsdaUi::LogicalAtlasSize(Tex);
		const float TexW = (float)FMath::Max(1, Imp.X);
		const float TexH = (float)FMath::Max(1, Imp.Y);
		const float Cs = (float)24;
		const float X = Cs * (float)(Cell % 10);
		const float Y = Cs * (float)(Cell / 10);
		FSlateBrush B;
		B.SetResourceObject(Tex);
		B.ImageSize = FVector2D(Cs, Cs);
		B.SetUVRegion(FBox2D(FVector2D(X / TexW, Y / TexH), FVector2D((X + Cs) / TexW, (Y + Cs) / TexH)));
		Icon->SetBrush(B);
		return true;
	}

	// U4b: ветка «клетка атласа» `UAsdaHotbarSubsystem::ApplyIconToImage` (текстура уже загружена тем же путём).
	bool OldHotbarCell(UImage* Img, UTexture2D* Tex, int32 IconX, int32 IconY, int32 IconSize)
	{
		if (!Img || !Tex) { return false; }
		const FIntPoint Imp = AsdaUi::LogicalAtlasSize(Tex);
		const float TexW = (float)FMath::Max(1, Imp.X);
		const float TexH = (float)FMath::Max(1, Imp.Y);
		const int32 CX = ((IconX % (int32)TexW) + (int32)TexW) % (int32)TexW;
		const int32 CY = ((IconY % (int32)TexH) + (int32)TexH) % (int32)TexH;
		const int32 Cell = FMath::Max(1, IconSize);
		FSlateBrush B;
		B.SetResourceObject(Tex);
		B.ImageSize = FVector2D(32.f, 32.f);
		B.SetUVRegion(FBox2D(
			FVector2D((float)CX / TexW, (float)CY / TexH),
			FVector2D((float)(CX + Cell) / TexW, (float)(CY + Cell) / TexH)));
		Img->SetBrush(B);
		return true;
	}

	// U4b/U4c: прежнее тело `AsdaApplyJuiIconRegion` (до выделения `ApplyJuiIconRegionTex`).
	bool OldJuiIconRegion(UImage* Img, const FString& IconName, int32 X, int32 Y, int32 W, int32 H, float DispW, float DispH)
	{
		if (!Img || IconName.IsEmpty() || W <= 0 || H <= 0) { return false; }
		UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
		UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(FName(*IconName)) : nullptr;
		if (!Tex) { return false; }
		const FIntPoint Size = AsdaUi::LogicalAtlasSize(Tex);
		const int32 TexW = FMath::Max(1, Size.X);
		const int32 TexH = FMath::Max(1, Size.Y);
		const int32 CX = ((X % TexW) + TexW) % TexW;
		const int32 CY = ((Y % TexH) + TexH) % TexH;
		FSlateBrush B;
		B.SetResourceObject(Tex);
		B.ImageSize = FVector2D(DispW, DispH);
		B.SetUVRegion(FBox2D(
			FVector2D((float)CX / (float)TexW, (float)CY / (float)TexH),
			FVector2D((float)(CX + W) / (float)TexW, (float)(CY + H) / (float)TexH)));
		Img->SetBrush(B);
		return true;
	}

	// U4c: подтверждение военного магазина — `GetItemIcon` + прежний `ApplyJuiIconCell(…, 64)`; ответа у копии не было.
	void OldWarShopIcon(const UAsdaItemTableSubsystem* IT, UImage* ConfirmIcon, int32 ItemCode)
	{
		if (ConfirmIcon && IT)
		{
			const FAsdaItemIcon Ic = IT->GetItemIcon(ItemCode, /*bMale*/true);
			if (Ic.IsValid())
			{
				OldJuiIconRegion(ConfirmIcon, Ic.Atlas, Ic.CellX, Ic.CellY, Ic.CellSize, Ic.CellSize, 64.f, 64.f);
			}
		}
	}

	// U16: меню игрока 2000 (`OpenMenuAtCursor`, kMenuW = 158, kMenuH = 276).
	FVector2D OldPlayerMenuClamp(const FVector2D& Cursor, const FVector2D& VP)
	{
		constexpr float kMenuW = 158.f, kMenuH = 276.f;
		FVector2D Pos = Cursor;
		if (VP.X > 1.f && VP.Y > 1.f)
		{
			if (Pos.X + kMenuW > VP.X) { Pos.X -= (Pos.X + kMenuW) - VP.X; }
			if (Pos.Y + kMenuH > VP.Y) { Pos.Y -= (Pos.Y + kMenuH) - VP.Y; }
			Pos.X = FMath::Max(Pos.X, 0.f);
			Pos.Y = FMath::Max(Pos.Y, 0.f);
		}
		return Pos;
	}

	// U16: меню «Хвастовство» 28000 (kMenuW = 158, kMenuH = 100; ключ клампа включён).
	FVector2D OldShowOffClamp(FVector2D Pos, const FVector2D& VP)
	{
		constexpr float kMenuW = 158.f, kMenuH = 100.f;
		if (VP.X > 1.f && VP.Y > 1.f)
		{
			Pos.X = FMath::Clamp(Pos.X, 0.f, FMath::Max(0.f, VP.X - kMenuW));
			Pos.Y = FMath::Clamp(Pos.Y, 0.f, FMath::Max(0.f, VP.Y - kMenuH));
		}
		return Pos;
	}

	// U16: панель питомца 43058 (ключ клампа включён).
	FVector2D OldPetCageClamp(FVector2D Cur, const FVector2D& Size, const FVector2D& View)
	{
		if (View.X > 1.0 && View.Y > 1.0)
		{
			Cur.X = FMath::Clamp(Cur.X, 0.0, FMath::Max(0.0, View.X - Size.X));
			Cur.Y = FMath::Clamp(Cur.Y, 0.0, FMath::Max(0.0, View.Y - Size.Y));
		}
		return Cur;
	}

	// U16: формулы центра трёх копий (окно пакета 26500 — ширина float, бустер 266, карта мира 690×519).
	FVector2D OldPackageWant(const FVector2D& Vp, float TotalW)
	{
		constexpr float kWinHeight = 320.f;
		return FVector2D(FMath::RoundToFloat(Vp.X * 0.5f - TotalW * 0.5f), FMath::RoundToFloat(Vp.Y * 0.5f - kWinHeight * 0.5f));
	}
	FVector2D OldBoosterWant(const FVector2D& Vp)
	{
		return FVector2D(FMath::RoundToFloat(Vp.X * 0.5f - 266.f * 0.5f), FMath::RoundToFloat(Vp.Y * 0.5f - 266.f * 0.5f));
	}
	FVector2D OldMinimapWant(const FVector2D& VP)
	{
		constexpr float kBigWinW = 690.f, kBigWinH = 519.f;
		return FVector2D(FMath::RoundToFloat(VP.X * 0.5f - kBigWinW * 0.5f), FMath::RoundToFloat(VP.Y * 0.5f - kBigWinH * 0.5f));
	}

	// U16: прежний сдвиг детей корня (окно пакета 26500 / бустер).
	void OldCenterRoot(UCanvasPanel* RootCanvas, const UCanvasPanelSlot* SRef, const FVector2D& Vp, float W, float H)
	{
		if (!SRef || !RootCanvas) { return; }
		const FVector2D Want(FMath::RoundToFloat(Vp.X * 0.5f - W * 0.5f), FMath::RoundToFloat(Vp.Y * 0.5f - H * 0.5f));
		const FVector2D Delta(Want.X - SRef->GetOffsets().Left, Want.Y - SRef->GetOffsets().Top);
		if (Delta.IsNearlyZero()) { return; }
		for (int32 i = 0; i < RootCanvas->GetChildrenCount(); ++i)
		{
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(RootCanvas->GetChildAt(i)))
			{
				FMargin O = CS->GetOffsets();
				O.Left += Delta.X; O.Top += Delta.Y;
				CS->SetOffsets(O);
			}
		}
	}

	// Канва с детьми-картинками на заданных оффсетах (для U16 CenterRoot).
	UCanvasPanel* MakeCanvas(const TArray<FMargin>& Offs)
	{
		UCanvasPanel* C = NewObject<UCanvasPanel>(GetTransientPackage());
		for (const FMargin& M : Offs)
		{
			UImage* I = NewObject<UImage>(GetTransientPackage());
			if (UCanvasPanelSlot* S = C->AddChildToCanvas(I)) { S->SetOffsets(M); }
		}
		return C;
	}

	FString VecStr(const FVector2D& V) { return FString::Printf(TEXT("(%.17g, %.17g)"), V.X, V.Y); }
}

static void AsdaDedupW1BRun(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaDedupW1B;
	int32 Cases = 2000;
	bool bQuit = false;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		else if (A.IsNumeric()) { Cases = FMath::Max(1, FCString::Atoi(*A)); }
	}
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
	if (AsdaSelfTestKit::RefuseStart(GI, TEXT("asda.Dedup.W1BSelfTest"), KitArgs)) { return; }
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
	FTally T;
	if (!IT || !Fx || !Net || !WM || !TexMgr)
	{
		UE_LOG(LogAsdaDedupW1B, Error, TEXT("САМОТЕСТ-В1Б FAIL нет подсистем (таблицы %d, эффекты %d, сеть %d, окна %d, текстуры %d)"),
			IT ? 1 : 0, Fx ? 1 : 0, Net ? 1 : 0, WM ? 1 : 0, TexMgr ? 1 : 0);
		++T.Fail;
		UE_LOG(LogAsdaDedupW1B, Display, TEXT("САМОТЕСТ-В1Б ИТОГ: ok=%d fail=%d"), T.Ok, T.Fail);
		if (bQuit) { AsdaSelfTestBatch::Quit(W); }
		return;
	}
	UImage* ImgA = NewObject<UImage>(GetTransientPackage());
	UImage* ImgB = NewObject<UImage>(GetTransientPackage());
	FRng R(0x5EEDB1B1ull);

	// ── Коды предметов: ВСЕ item_list, special_itemlist, sowell_list + особые ──
	TArray<int32> Codes = { 0, -1, 999999999 };
	for (const FAsdaItemRecord& Rc : IT->GetAllItems()) { Codes.Add(Rc.ItemCode); }
	if (const UDataTable* Dt = IT->GetDataTable(TEXT("special_itemlist")))
	{
		TArray<FAsdaSpecialItemlistRow*> Rows; Dt->GetAllRows<FAsdaSpecialItemlistRow>(TEXT("W1B"), Rows);
		for (const FAsdaSpecialItemlistRow* Rw : Rows) { if (Rw) { Codes.Add(Rw->Code); } }
	}
	if (const UDataTable* Dt = IT->GetDataTable(TEXT("sowell_list")))
	{
		TArray<FAsdaSowellRow*> Rows; Dt->GetAllRows<FAsdaSowellRow>(TEXT("W1B"), Rows);
		for (const FAsdaSowellRow* Rw : Rows) { if (Rw) { Codes.Add(Rw->Id); } }
	}

	// ── U4a: атласы портретов (DT_MobStats), эмблемы, атласы состояний ──
	{
		TSet<FString> Portraits;
		Portraits.Add(TEXT("char_emblems"));
		Portraits.Add(TEXT("w1b_нет_такого_атласа"));
		Portraits.Add(FString());
		if (const UDataTable* Dt = AsdaDt::Cached(AAsdaMobActor::kMobStatsPath))
		{
			TArray<FAsdaMobStatsRow*> Rows; Dt->GetAllRows<FAsdaMobStatsRow>(TEXT("W1B"), Rows);
			for (const FAsdaMobStatsRow* Rw : Rows) { if (Rw && !Rw->PortraitTex.IsEmpty()) { Portraits.Add(Rw->PortraitTex); } }
		}
		int32 N = 0, Bad = 0; FString First;
		for (const FString& A : Portraits)
		{
			for (int32 Cell = -3; Cell < 400; ++Cell)
			{
				Reset2(ImgA, ImgB);
				const bool RA = OldTargetPortrait(ImgA, A, Cell, 46, 11);   // kEmblemPx = 46, kEmblemCols = 11
				const bool RB = AsdaUi::ApplyAtlasCellIcon(ImgB, A, Cell, 46, 11);
				Cmp(ImgA, ImgB, RA, RB, FString::Printf(TEXT("портрет «%s» ячейка %d"), *A, Cell), Bad, First);
				++N;
			}
		}
		T.Group(TEXT("U4a портрет панели цели ≡ ApplyAtlasCellIcon (46 px, 11 колонок)"), N, Bad, First,
			FString::Printf(TEXT(" (атласов %d)"), Portraits.Num()));

		TSet<FString> States;
		States.Add(TEXT("w1b_нет_такого_атласа"));
		States.Add(FString());
		TArray<TPair<FString, int32>> StateCells;
		for (int32 Id = 0; Id < 4000; ++Id)
		{
			FString Nm, Icon, Txt; int32 Cell = 0;
			if (Fx->GetStateInfo(Id, Nm, Icon, Cell, Txt)) { States.Add(Icon); StateCells.Emplace(Icon, Cell); }
		}
		N = 0; Bad = 0; First.Reset();
		for (const TPair<FString, int32>& SC : StateCells)
		{
			Reset2(ImgA, ImgB);
			const bool RA = OldTargetStateIcon(ImgA, SC.Key, SC.Value);
			const bool RB = AsdaUi::ApplyAtlasCellIcon(ImgB, SC.Key, SC.Value, 24, 10);
			Cmp(ImgA, ImgB, RA, RB, FString::Printf(TEXT("состояние «%s» ячейка %d"), *SC.Key, SC.Value), Bad, First);
			++N;
		}
		for (const FString& A : States)
		{
			for (int32 Cell = -3; Cell < 200; ++Cell)
			{
				Reset2(ImgA, ImgB);
				const bool RA = OldTargetStateIcon(ImgA, A, Cell);
				const bool RB = AsdaUi::ApplyAtlasCellIcon(ImgB, A, Cell, 24, 10);
				Cmp(ImgA, ImgB, RA, RB, FString::Printf(TEXT("атлас состояний «%s» ячейка %d"), *A, Cell), Bad, First);
				++N;
			}
		}
		T.Group(TEXT("U4a иконки состояний цели ≡ ApplyAtlasCellIcon (24 px, 10 колонок)"), N, Bad, First,
			FString::Printf(TEXT(" (записей stateinfotip %d, атласов %d)"), StateCells.Num(), States.Num()));
	}

	// ── U4b/U4c: атласы иконок предметов ──
	{
		TMap<FString, TArray<FAsdaItemIcon>> ByAtlas;
		for (int32 C : Codes)
		{
			for (int32 Sex = 0; Sex < 2; ++Sex)
			{
				const FAsdaItemIcon Ic = IT->GetItemIcon(C, Sex == 0);
				if (Ic.IsValid()) { ByAtlas.FindOrAdd(Ic.Atlas).Add(Ic); }
			}
		}
		// U4b: клетка хотбара — тот же загрузчик (`LoadTextureCached(JuiTextureObject(имя))`), сверяется вырезка.
		int32 N = 0, Bad = 0; FString First;
		for (const TPair<FString, TArray<FAsdaItemIcon>>& Kv : ByAtlas)
		{
			UTexture2D* Tex = TexMgr->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(Kv.Key));
			for (const FAsdaItemIcon& Ic : Kv.Value)
			{
				Reset2(ImgA, ImgB);
				const bool RA = OldHotbarCell(ImgA, Tex, Ic.CellX, Ic.CellY, Ic.CellSize);
				const bool RB = Tex ? AsdaUi::ApplyJuiIconRegionTex(ImgB, Tex, Ic.CellX, Ic.CellY, FMath::Max(1, Ic.CellSize), FMath::Max(1, Ic.CellSize), 32.f, 32.f) : false;
				Cmp(ImgA, ImgB, RA, RB, FString::Printf(TEXT("хотбар «%s» (%d,%d) %d"), *Kv.Key, Ic.CellX, Ic.CellY, Ic.CellSize), Bad, First);
				++N;
			}
			for (int32 k = 0; k < 40; ++k)   // вылет за край (заворот), отрицательные, нулевой и крупный размер
			{
				const int32 X = R.Range(4096) - 1024, Y = R.Range(4096) - 1024;
				const int32 Sz = (k % 10 == 0) ? 0 : (k % 10 == 1) ? -5 : 1 + R.Range(128);
				Reset2(ImgA, ImgB);
				const bool RA = OldHotbarCell(ImgA, Tex, X, Y, Sz);
				const bool RB = Tex ? AsdaUi::ApplyJuiIconRegionTex(ImgB, Tex, X, Y, FMath::Max(1, Sz), FMath::Max(1, Sz), 32.f, 32.f) : false;
				Cmp(ImgA, ImgB, RA, RB, FString::Printf(TEXT("хотбар «%s» (%d,%d) %d"), *Kv.Key, X, Y, Sz), Bad, First);
				++N;
			}
		}
		T.Group(TEXT("U4b клетка хотбара ≡ ApplyJuiIconRegionTex"), N, Bad, First, FString::Printf(TEXT(" (атласов %d)"), ByAtlas.Num()));

		// Прежнее тело `ApplyJuiIconCell/Rect` против нового (тело вынесено в `ApplyJuiIconRegionTex`).
		N = 0; Bad = 0; First.Reset();
		TArray<FString> Names; ByAtlas.GetKeys(Names);
		Names.Add(TEXT("w1b_нет_такого_атласа")); Names.Add(FString());
		for (const FString& A : Names)
		{
			for (int32 k = 0; k < 40; ++k)
			{
				const int32 X = R.Range(4096) - 1024, Y = R.Range(4096) - 1024;
				const int32 Wd = (k % 8 == 0) ? 0 : 1 + R.Range(96), Hd = (k % 8 == 1) ? -2 : 1 + R.Range(96);
				const float Disp = (float)(8 + R.Range(80));
				Reset2(ImgA, ImgB);
				bool RA = OldJuiIconRegion(ImgA, A, X, Y, Wd, Wd, Disp, Disp);
				bool RB = AsdaUi::ApplyJuiIconCell(ImgB, A, X, Y, Wd, Disp);
				Cmp(ImgA, ImgB, RA, RB, FString::Printf(TEXT("Cell «%s» (%d,%d) %d/%.0f"), *A, X, Y, Wd, Disp), Bad, First);
				Reset2(ImgA, ImgB);
				RA = OldJuiIconRegion(ImgA, A, X, Y, Wd, Hd, (float)Wd, (float)Hd);
				RB = AsdaUi::ApplyJuiIconRect(ImgB, A, X, Y, Wd, Hd);
				Cmp(ImgA, ImgB, RA, RB, FString::Printf(TEXT("Rect «%s» (%d,%d) %dx%d"), *A, X, Y, Wd, Hd), Bad, First);
				N += 2;
			}
		}
		T.Group(TEXT("U4b ApplyJuiIconCell/Rect: прежнее тело ≡ новое"), N, Bad, First);

		// U4c: подтверждение военного магазина по всем кодам.
		N = 0; Bad = 0; First.Reset();
		for (int32 C : Codes)
		{
			Reset2(ImgA, ImgB);
			OldWarShopIcon(IT, ImgA, C);
			AsdaUi::ApplyItemIcon(GI, ImgB, C, FVector2D(64.f, 64.f), /*bMale*/true, /*bWrap*/true);
			Cmp(ImgA, ImgB, true, true, FString::Printf(TEXT("код %d"), C), Bad, First);
			++N;
		}
		T.Group(TEXT("U4c иконка подтверждения военного магазина ≡ ApplyItemIcon(64×64, заворот)"), N, Bad, First);
	}

	// ── U16: кламп попапа и центр окна ──
	{
		const FVector2D Views[] = { {0.0, 0.0}, {1.0, 1.0}, {1.0, 768.0}, {1024.0, 768.0}, {1280.0, 720.5}, {800.3333333, 600.6666667},
			{100.0, 90.0}, {2560.0, 1440.0}, AsdaUi::ViewportSizeUnits(W) };
		int32 NS = 0, BS = 0, NP = 0, BP = 0, NM = 0, BM = 0, NotBit = 0;
		double MaxDiff = 0.0;
		FString FS, FP, FM;
		for (const FVector2D& V : Views)
		{
			for (int32 k = 0; k < Cases; ++k)
			{
				const FVector2D Pos(R.Real(-300.0, V.X + 300.0), R.Real(-300.0, V.Y + 300.0));
				// 28000
				{
					const FVector2D A = OldShowOffClamp(Pos, V), B = AsdaUi::ClampPopupToViewport(Pos, FVector2D(158.f, 100.f), V);
					if (A != B && BS++ == 0) { FS = FString::Printf(TEXT("pos %s экран %s: было %s стало %s"), *VecStr(Pos), *VecStr(V), *VecStr(A), *VecStr(B)); }
					++NS;
				}
				// 43058 (корпус из .JUI 158×119 и случайный)
				{
					const FVector2D Size = (k & 1) ? FVector2D(158.0, 119.0) : FVector2D(R.Real(1.0, 1500.0), R.Real(1.0, 1000.0));
					const FVector2D A = OldPetCageClamp(Pos, Size, V), B = AsdaUi::ClampPopupToViewport(Pos, Size, V);
					if (A != B && BP++ == 0) { FP = FString::Printf(TEXT("pos %s размер %s экран %s: было %s стало %s"), *VecStr(Pos), *VecStr(Size), *VecStr(V), *VecStr(A), *VecStr(B)); }
					++NP;
				}
				// 2000: математически то же; допуск — последний бит округления
				{
					const FVector2D A = OldPlayerMenuClamp(Pos, V), B = AsdaUi::ClampPopupToViewport(Pos, FVector2D(158.f, 276.f), V);
					const double D = FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
					if (A != B) { ++NotBit; }
					MaxDiff = FMath::Max(MaxDiff, D);
					if (D > 1e-9 && BM++ == 0) { FM = FString::Printf(TEXT("pos %s экран %s: было %s стало %s"), *VecStr(Pos), *VecStr(V), *VecStr(A), *VecStr(B)); }
					++NM;
				}
			}
		}
		T.Group(TEXT("U16 кламп меню 28000 ≡ ClampPopupToViewport (бит в бит)"), NS, BS, FS);
		T.Group(TEXT("U16 кламп панели питомца 43058 ≡ ClampPopupToViewport (бит в бит)"), NP, BP, FP);
		T.Group(TEXT("U16 кламп меню 2000 ≡ ClampPopupToViewport (|Δ| ≤ 1e-9 ед.)"), NM, BM, FM,
			FString::Printf(TEXT(" (не бит в бит %d, макс. |Δ| = %.3g ед. — последний бит записи «сдвиг на перелёт»)"), NotBit, MaxDiff));

		int32 NC = 0, BC = 0; FString FC;
		for (const FVector2D& V : Views)
		{
			for (int32 k = 0; k < Cases / 4 + 1; ++k)
			{
				const FVector2D Vp = (k == 0) ? V : FVector2D(R.Real(0.0, 4000.0), R.Real(0.0, 3000.0));
				const float TotalW = 284.f + 284.f * (float)R.Range(6);   // kGridCellWidth·(блоков + 1)
				const FVector2D A1 = OldPackageWant(Vp, TotalW), B1 = AsdaUi::CenteredOrigin(Vp, FVector2D(TotalW, 320.f));
				const FVector2D A2 = OldBoosterWant(Vp), B2 = AsdaUi::CenteredOrigin(Vp, FVector2D(266.f, 266.f));
				const FVector2D A3 = OldMinimapWant(Vp), B3 = AsdaUi::CenteredOrigin(Vp, FVector2D(690.f, 519.f));
				if ((A1 != B1 || A2 != B2 || A3 != B3) && BC++ == 0)
				{
					FC = FString::Printf(TEXT("экран %s: пакет %s/%s бустер %s/%s карта %s/%s"), *VecStr(Vp), *VecStr(A1), *VecStr(B1),
						*VecStr(A2), *VecStr(B2), *VecStr(A3), *VecStr(B3));
				}
				NC += 3;
			}
		}
		T.Group(TEXT("U16 центр окна (26500/бустер/карта мира) ≡ CenteredOrigin (бит в бит)"), NC, BC, FC);

		int32 NR = 0, BR = 0; FString FR;
		for (int32 k = 0; k < 200; ++k)
		{
			TArray<FMargin> Offs;
			const int32 NCh = 1 + R.Range(8);
			for (int32 i = 0; i < NCh; ++i)
			{
				Offs.Add(FMargin((float)R.Real(-50.0, 900.0), (float)R.Real(-50.0, 700.0), (float)R.Real(0.0, 300.0), (float)R.Real(0.0, 300.0)));
			}
			UCanvasPanel* CA = MakeCanvas(Offs);
			UCanvasPanel* CB = MakeCanvas(Offs);
			const int32 Ref = R.Range(NCh);
			const float Wd = (k & 1) ? 266.f : 284.f * (float)(1 + R.Range(5)), Ht = (k & 1) ? 266.f : 320.f;
			OldCenterRoot(CA, AsdaUi::SlotOf(CA->GetChildAt(Ref)), AsdaUi::ViewportSizeUnits(W), Wd, Ht);
			AsdaUi::CenterRootOnViewport(CB, AsdaUi::SlotOf(CB->GetChildAt(Ref)), FVector2D(Wd, Ht), W);
			for (int32 i = 0; i < NCh; ++i)
			{
				const FMargin MA = AsdaUi::SlotOf(CA->GetChildAt(i))->GetOffsets(), MB = AsdaUi::SlotOf(CB->GetChildAt(i))->GetOffsets();
				if (!(MA == MB) && BR++ == 0)
				{
					FR = FString::Printf(TEXT("случай %d ребёнок %d: было (%.9g,%.9g,%.9g,%.9g) стало (%.9g,%.9g,%.9g,%.9g)"), k, i,
						MA.Left, MA.Top, MA.Right, MA.Bottom, MB.Left, MB.Top, MB.Right, MB.Bottom);
				}
				++NR;
			}
		}
		T.Group(TEXT("U16 центрирование корня окна (сдвиг детей) ≡ CenterRootOnViewport"), NR, BR, FR);
	}

	// ── U5a: «это я?» ──
	{
		const bool KnownWas = Net->bSelfUserNumKnown;
		const auto NumWas = Net->SelfUserNum;
		int32 N = 0, Bad = 0, Diff0 = 0; FString First;
		const int32 Self[] = { 0, 1, 7, 255, 1000, 32767 };
		const int32 Probe[] = { -32768, -1, 0, 1, 7, 8, 255, 256, 1000, 32767, 65535 };
		for (int32 Known = 0; Known < 2; ++Known)
		{
			for (int32 S : Self)
			{
				Net->bSelfUserNumKnown = (Known != 0);
				Net->SelfUserNum = (decltype(Net->SelfUserNum))S;
				for (int32 U : Probe)
				{
					AsdaCVarSave::SetForTest(TEXT("asda.Self.StrictUnknownNum"), 1);
					const bool A = Net->IsSelfUserIdx(U);
					const bool B = AsdaSelf::IsSelfUserIdx(Net, U);
					const bool BG = AsdaSelf::IsSelfUserIdx(GI, U);
					if ((A != B || A != BG) && Bad++ == 0)
					{
						First = FString::Printf(TEXT("известен=%d свой=%d проба=%d: метод %d, AsdaSelf(Net) %d, AsdaSelf(GI) %d"), Known, S, U, A ? 1 : 0, B ? 1 : 0, BG ? 1 : 0);
					}
					++N;
					AsdaCVarSave::SetForTest(TEXT("asda.Self.StrictUnknownNum"), 0);
					if (Net->IsSelfUserIdx(U) != AsdaSelf::IsSelfUserIdx(Net, U)) { ++Diff0; }
				}
			}
		}
		Net->bSelfUserNumKnown = KnownWas;
		Net->SelfUserNum = NumWas;
		AsdaCVarSave::RestoreAll(TEXT("asda.Dedup.W1BSelfTest"));   // значение и приоритет ключа — как до самотеста
		T.Group(TEXT("U5a AsdaSelf::IsSelfUserIdx ≡ метод сети при asda.Self.StrictUnknownNum 1"), N, Bad, First,
			FString::Printf(TEXT(" (при ключе 0 расходятся %d — смысл ключа отката: открытое сравнение до 4003)"), Diff0));
	}

	// ── U18: шапка трекера подсказки ──
	{
		int32 N = 0, Bad = 0; FString First;
		int32 OK = -1, OI = -1, NK = -1, NI = -1, OId = -1, NId = -1;
		UAsdaItemTooltipSubsystem* TipSub = GI->GetSubsystem<UAsdaItemTooltipSubsystem>();
		for (int32 k = 0; k < Cases; ++k)
		{
			// Ключи с повторами (повтор — главный случай «ничего не делать»).
			const int32 Kind = (R.Range(4) == 0) ? -1 : R.Range(4);
			const int32 Idx = (Kind < 0) ? -1 : R.Range(3);
			// Прежняя шапка (подсистема тултипа есть).
			bool bOldShow = false;
			if (!(Kind == OK && Idx == OI))
			{
				OK = Kind; OI = Idx;
				bOldShow = (Kind >= 0);
			}
			const bool bNewShow = UAsdaItemTooltipSubsystem::BeginHoverTip(GI, EAsdaTipOwner::Craft, NK, NI, Kind, Idx) != nullptr;
			if ((bOldShow != bNewShow || OK != NK || OI != NI) && Bad++ == 0)
			{
				First = FString::Printf(TEXT("шаг %d (%d,%d): было показать=%d ключ (%d,%d), стало %d (%d,%d)"), k, Kind, Idx, bOldShow ? 1 : 0, OK, OI, bNewShow ? 1 : 0, NK, NI);
			}
			// Окно с одним ключом.
			const int32 Id = (R.Range(4) == 0) ? -1 : 32000 + R.Range(3);
			bool bOld1 = false;
			if (Id != OId) { OId = Id; bOld1 = (Id >= 0); }
			const bool bNew1 = UAsdaItemTooltipSubsystem::BeginHoverTip(GI, EAsdaTipOwner::Disassembly, NId, Id) != nullptr;
			if ((bOld1 != bNew1 || OId != NId) && Bad++ == 0)
			{
				First = FString::Printf(TEXT("шаг %d id %d: было %d/%d, стало %d/%d"), k, Id, bOld1 ? 1 : 0, OId, bNew1 ? 1 : 0, NId);
			}
			N += 2;
		}
		// Нет подсистемы тултипа → ключ окна не трогается, ответ nullptr (как `if (!Tip) return;` копий).
		int32 KK = 5, KI = 6, KId = 7;
		const bool bNull = UAsdaItemTooltipSubsystem::BeginHoverTip(nullptr, EAsdaTipOwner::Craft, KK, KI, 1, 1) == nullptr
			&& UAsdaItemTooltipSubsystem::BeginHoverTip(nullptr, EAsdaTipOwner::Disassembly, KId, 1) == nullptr && KK == 5 && KI == 6 && KId == 7;
		if (!bNull && Bad++ == 0) { First = TEXT("без подсистемы тултипа ключ окна изменился"); }
		++N;
		if (TipSub) { TipSub->HideTooltipFor(EAsdaTipOwner::Craft); TipSub->HideTooltipFor(EAsdaTipOwner::Disassembly); }
		T.Group(TEXT("U18 шапка трекера подсказки ≡ BeginHoverTip (два ключа и один ключ)"), N, Bad, First);
	}

	// ── U22: каркас «открыть при первом показе» ──
	{
		int32 N = 0, Bad = 0; FString First;
		auto Check = [&N, &Bad, &First](bool b, const FString& What) { ++N; if (!b && Bad++ == 0) { First = What; } };
		static const FName kProbeId(TEXT("W1B_LAZY_PROBE"));
		TObjectPtr<UUserWidget> Wd = nullptr;
		FString Steps;
		// 1) не загрузился: сброс + лог, привязки нет
		{
			const EAsdaJuiEnsure E = FAsdaJuiWindowHandle(kProbeId, TEXT("/Game/AsdaAssets/UI/W1B_НЕТ_ТАКОГО_WBP"), 10).EnsureLazy(*WM, Wd, true,
				ESlateVisibility::Collapsed, [&Steps]() { Steps += TEXT("с"); }, [&Steps]() { Steps += TEXT("л"); },
				[&Steps](UAsdaWindowSubsystem&) { Steps += TEXT("п"); });
			Check(E == EAsdaJuiEnsure::LoadFailed && Steps == TEXT("сл") && Wd == nullptr,
				FString::Printf(TEXT("«не загрузился»: итог %d шаги «%s»"), (int32)E, *Steps));
		}
		// 2) открыть: сброс → открыть → привязка; корень свёрнут, окно живо
		Steps.Reset();
		{
			const EAsdaJuiEnsure E = FAsdaJuiWindowHandle(kProbeId, TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_OTHERCHARA_DATA"), 10).EnsureLazy(*WM, Wd, true,
				ESlateVisibility::Collapsed, [&Steps, &Wd]() { Steps += Wd ? TEXT("С") : TEXT("с"); }, [&Steps]() { Steps += TEXT("л"); },
				[&Steps, &Wd](UAsdaWindowSubsystem& M) { Steps += (Wd && M.IsWindowLive(Wd)) ? TEXT("П") : TEXT("п"); });
			Check(E == EAsdaJuiEnsure::Created && Steps == TEXT("сП") && Wd && WM->IsWindowLive(Wd) && Wd->GetVisibility() == ESlateVisibility::Collapsed,
				FString::Printf(TEXT("«открыть»: итог %d шаги «%s» живо %d видимость %d"), (int32)E, *Steps, (Wd && WM->IsWindowLive(Wd)) ? 1 : 0,
					Wd ? (int32)Wd->GetVisibility() : -1));
		}
		// 3) кэш годен: ни одного шага; при bReady = false — снова сброс → открыть → привязка
		Steps.Reset();
		{
			UUserWidget* Was = Wd;
			const EAsdaJuiEnsure E = FAsdaJuiWindowHandle(kProbeId, TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_OTHERCHARA_DATA"), 10).EnsureLazy(*WM, Wd, true,
				ESlateVisibility::Collapsed, [&Steps]() { Steps += TEXT("с"); }, [&Steps]() { Steps += TEXT("л"); },
				[&Steps](UAsdaWindowSubsystem&) { Steps += TEXT("п"); });
			Check(E == EAsdaJuiEnsure::AlreadyLive && Steps.IsEmpty() && Wd == Was, FString::Printf(TEXT("«кэш годен»: итог %d шаги «%s»"), (int32)E, *Steps));
		}
		WM->CloseWindow(kProbeId);
		WM->UnregisterWindowCloser(kProbeId);
		T.Group(TEXT("U22 EnsureLazy: порядок шагов, «кэш годен», «не загрузился»"), N, Bad, First);
	}

	UE_LOG(LogAsdaDedupW1B, Display, TEXT("САМОТЕСТ-В1Б ИТОГ: ok=%d fail=%d"), T.Ok, T.Fail);
	if (bQuit) { AsdaSelfTestBatch::Quit(W); }
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaDedupW1BSelfTest(
	TEXT("asda.Dedup.W1BSelfTest"),
	TEXT("Самотест В1 (часть B) дедупа №2: вынесенные точки интерфейса (U4a–c, U5a, U16, U18, U22) против прежних копий. ")
	TEXT("asda.Dedup.W1BSelfTest [случаев=2000] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		AsdaDedupW1BRun(Args, W);
	}));

#endif // !UE_BUILD_SHIPPING
