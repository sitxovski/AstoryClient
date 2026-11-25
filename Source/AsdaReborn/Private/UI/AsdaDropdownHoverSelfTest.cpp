// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПАЧКИ 20, БАГ 1 (09.11.2026): подсветка пункта выпадающего списка при наведении — `asda.UI.SelfTestDropdownHover [delay=N] [quit]`.
//
// ЭТАЛОН: подсветка строки — квад на ВСЮ клиентскую ширину списка × высоту пункта (`uiListGrid_layoutRows@0x12EBFC0`,
//   ui_core.c:112605: SetRect(clientLeft, rowTop, clientRight, rowTop + высота), client = Rect плана 0 самого ListBox);
//   цвет — под курсором 0x96F0F0F0, у выбранного 0xFFFFFFFF (рендер ui_core.c:112969).
// ПУТЬ ИГРОКА: окно открывается штатно (карта мира — клавиша M, команда 37), список раскрывается щелчком `SimulateClickAt`
//   по его коробке, курсор ставится настоящим движением Slate на пункты. В журнал — структурный дамп узлов списка
//   (класс, видимость, слот, живая геометрия, кисть), проверка — подсветка закрывает строку целиком.
//   Кадры — `Reports/batch20/dropdown_hover/<язык>_<список>_<шаг>.png`. Строки журнала — «САМОТЕСТ-СПИСОК», итог «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaPlayerController.h"
#include "UI/AsdaExchangeSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDropdownTest, Log, All);

namespace AsdaDropdownTestLocal
{
	static const TCHAR* kWho = TEXT("asda.UI.SelfTestDropdownHover");   // имя самотеста (сторож ключей)

	// Настоящее движение курсора Slate.
	static void MoveCursor(const FVector2D& P)
	{
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(P);
		FPointerEvent Mv(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
		App.ProcessMouseMoveEvent(Mv);
	}
	// Живая геометрия узла.
	static bool RectOf(const UWidget* W, FVector2D& Mn, FVector2D& Mx)
	{
		return W && AsdaUi::WidgetAbsRect(W, Mn, Mx) && !(Mx - Mn).IsNearlyZero();
	}
	// Строка структурного дампа узла.
	static FString Dump(const UWidget* W)
	{
		if (!W) { return TEXT("НЕТ"); }
		FString S = FString::Printf(TEXT("%s [%s] вид=%d"), *W->GetName(), *W->GetClass()->GetName(), (int32)W->GetVisibility());
		if (const UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(W->Slot))
		{
			const FMargin O = CS->GetOffsets(); const FAnchors A = CS->GetAnchors();
			S += FString::Printf(TEXT(" слот(L%.1f T%.1f R%.1f B%.1f якорь %.2f,%.2f-%.2f,%.2f авто=%d z=%d)"),
				O.Left, O.Top, O.Right, O.Bottom, A.Minimum.X, A.Minimum.Y, A.Maximum.X, A.Maximum.Y, CS->GetAutoSize() ? 1 : 0, CS->GetZOrder());
		}
		else if (W->Slot) { S += FString::Printf(TEXT(" слот %s"), *W->Slot->GetClass()->GetName()); }
		FVector2D Mn, Mx;
		if (RectOf(W, Mn, Mx)) { S += FString::Printf(TEXT(" экран(%.1f,%.1f %.1f×%.1f)"), Mn.X, Mn.Y, Mx.X - Mn.X, Mx.Y - Mn.Y); }
		else { S += TEXT(" экран(нет)"); }
		if (const UImage* I = Cast<UImage>(W))
		{
			const FSlateBrush& B = I->GetBrush();
			const FLinearColor C = B.TintColor.GetSpecifiedColor();
			const FLinearColor Ct = I->GetColorAndOpacity();
			S += FString::Printf(TEXT(" кисть(%s рисуется=%d размер %.0f×%.0f тинт %.3f,%.3f,%.3f,%.3f цвет %.3f,%.3f,%.3f,%.3f)"),
				B.GetResourceObject() ? *B.GetResourceObject()->GetName() : TEXT("-"), (int32)B.DrawAs, B.ImageSize.X, B.ImageSize.Y,
				C.R, C.G, C.B, C.A, Ct.R, Ct.G, Ct.B, Ct.A);
		}
		return S;
	}
}

struct FAsdaDropdownHoverSelfTest
{
	int32 Ok = 0, Fail = 0;                    // счётчики
	TArray<TFunction<float(UWorld*)>> Steps;   // шаг возвращает паузу (с)
	FString Lang, ShotDir;                     // язык и папка кадров

	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		b ? ++Ok : ++Fail;
		UE_LOG(LogAsdaDropdownTest, Log, TEXT("САМОТЕСТ-СПИСОК %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	void Shot(const FString& Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *Lang, *Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaDropdownTest, Log, TEXT("САМОТЕСТ-СПИСОК кадр: %s"), *File);
	}
	template <class T> static T* Sub(UWorld* W) { UGameInstance* G = W ? W->GetGameInstance() : nullptr; return G ? G->GetSubsystem<T>() : nullptr; }
	static UWidget* Node(UWorld* W, FName Win, const TCHAR* Name)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		UUserWidget* U = WM ? WM->FindWindow(Win) : nullptr;
		return U ? U->GetWidgetFromName(Name) : nullptr;
	}
	void DumpNodes(UWorld* W, FName Win, const TArray<FString>& Names, const FString& Tag)
	{
		for (const FString& N : Names)
		{
			UE_LOG(LogAsdaDropdownTest, Log, TEXT("САМОТЕСТ-СПИСОК дамп %s: %s"), *Tag, *AsdaDropdownTestLocal::Dump(Node(W, Win, *N)));
		}
	}

	// Карта мира 1710: список областей 1723 (10 строк) и континентов 1721 (2 строки).
	void AddWorldMap(const TCHAR* Base, int32 HoverRows, const TCHAR* Tag, bool bKeyOff = false)
	{
		using namespace AsdaDropdownTestLocal;
		const FName Win(TEXT("MAXMAP_WIN"));
		const FString B(Base), T(Tag);
		Steps.Add([this, Win, B, T, bKeyOff](UWorld* W) -> float
		{
			AsdaCVarSave::SetForTest(TEXT("asda.WorldMap.ListPlateEtalon"), bKeyOff ? 0 : 1);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			if (WM && !WM->IsWindowVisible(Win))
			{
				if (AAsdaPlayerController* P = Cast<AAsdaPlayerController>(W->GetFirstPlayerController())) { P->RunCommandForCheck(37); }
			}
			MoveCursor(FVector2D(4.f, 4.f));
			return 1.0f;
		});
		Steps.Add([this, Win, B, T](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			Expect(WM && WM->IsWindowVisible(Win), FString::Printf(TEXT("%s 0 карта мира открыта"), *T));
			FVector2D Mn, Mx;
			UWidget* Box = Node(W, Win, *(B + TEXT("_Click")));
			if (WM && RectOf(Box, Mn, Mx)) { WM->SimulateClickAt((Mn + Mx) * 0.5f, 2, false); }
			return 0.5f;
		});
		for (int32 r = 0; r < HoverRows; ++r)
		{
			Steps.Add([this, Win, B, T, r](UWorld* W) -> float
			{
				UWidget* List = Node(W, Win, *(B + TEXT("_List")));
				Expect(List && AsdaUi::IsShown(List), FString::Printf(TEXT("%s %d список раскрыт"), *T, r));
				FVector2D Mn, Mx;
				UWidget* Row = Node(W, Win, *FString::Printf(TEXT("%s_Row%d_Click"), *B, r));
				if (RectOf(Row, Mn, Mx)) { MoveCursor(FVector2D(Mn.X + 6.f, (Mn.Y + Mx.Y) * 0.5f)); }
				return 0.25f;
			});
			Steps.Add([this, Win, B, T, r, bKeyOff](UWorld* W) -> float
			{
				using namespace AsdaDropdownTestLocal;
				TArray<FString> Names = { B + TEXT("_Click"), B + TEXT("_List"), B + TEXT("_ListBg"), B + TEXT("_Sel"),
					FString::Printf(TEXT("%s_Row%d"), *B, r), FString::Printf(TEXT("%s_Row%d_Click"), *B, r) };
				DumpNodes(W, Win, Names, FString::Printf(TEXT("%s строка %d"), *T, r));
				UWidget* Sel = Node(W, Win, *(B + TEXT("_Sel")));
				UWidget* Row = Node(W, Win, *FString::Printf(TEXT("%s_Row%d_Click"), *B, r));
				UWidget* List = Node(W, Win, *(B + TEXT("_List")));
				FVector2D SMn = FVector2D::ZeroVector, SMx = FVector2D::ZeroVector, RMn = FVector2D::ZeroVector, RMx = FVector2D::ZeroVector, LMn = FVector2D::ZeroVector, LMx = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
				// Наведённый пункт = выбранный: эталон рисует один квад выбранного (белый) — тогда меряем `_Cur`.
				UWidget* Plate = (Sel && AsdaUi::IsShown(Sel)) ? Sel : Node(W, Win, *(B + TEXT("_Cur")));
				const bool bS = Plate && AsdaUi::IsShown(Plate) && RectOf(Plate, SMn, SMx);
				const bool bR = RectOf(Row, RMn, RMx), bL = RectOf(List, LMn, LMx);
				// Эталон: квад = вся ширина списка × строка пункта.
				Expect(bS && bR && bL && FMath::IsNearlyEqual(SMn.X, LMn.X, 1.f) && FMath::IsNearlyEqual(SMx.X, LMx.X, 1.f)
					&& FMath::IsNearlyEqual(SMn.Y, RMn.Y, 1.f) && FMath::IsNearlyEqual(SMx.Y, RMx.Y, 1.f),
					FString::Printf(TEXT("%s %d подсветка закрывает строку на всю ширину списка"), *T, r),
					FString::Printf(TEXT("подсветка (%.1f,%.1f)-(%.1f,%.1f), строка (%.1f,%.1f)-(%.1f,%.1f), список x %.1f..%.1f"),
						SMn.X, SMn.Y, SMx.X, SMx.Y, RMn.X, RMn.Y, RMx.X, RMx.Y, LMn.X, LMx.X));
				// Кисть плиты — сплошной белый квад (дефект: UV-свотч n_01 ≈2 текселя закрашивал ~2/5 строки), цвет — эталон.
				// Кисть `_Sel` проверяем всегда (ассет), цвет — у той плиты, что рисует наведённую строку.
				const UImage* SI = Cast<UImage>(Sel);
				const UObject* Res = SI ? SI->GetBrush().GetResourceObject() : nullptr;
				Expect(Res && Res->GetName() == TEXT("WhiteSquareTexture"), FString::Printf(TEXT("%s %d кисть подсветки — сплошной белый квад"), *T, r),
					FString::Printf(TEXT("ресурс %s"), Res ? *Res->GetName() : TEXT("нет")));
				UWidget* Cur = Node(W, Win, *(B + TEXT("_Cur")));
				FVector2D CMn = FVector2D::ZeroVector, CMx = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
				const bool bCurShown = Cur && AsdaUi::IsShown(Cur) && RectOf(Cur, CMn, CMx);
				// Выбранная строка — по `_Cur` (поле выбора подсистемы закрыто): офлайн область может быть не выбрана (SelArea −1).
				const bool bHoverIsSel = bCurShown && bR && FMath::IsNearlyEqual(CMn.Y, RMn.Y, 1.f);
				const UImage* TI = Cast<UImage>(bHoverIsSel ? Cur : Sel);
				const FLinearColor Tint = TI ? TI->GetBrush().TintColor.GetSpecifiedColor() : FLinearColor::Transparent;
				const uint32 WantU = bHoverIsSel ? 0xFFFFFFFFu : (bKeyOff ? 0xFF225777u : 0x96F0F0F0u);
				const FLinearColor Want = AsdaUi::SrgbU32ToLinear(WantU);
				Expect(Tint.Equals(Want, 0.01f), FString::Printf(TEXT("%s %d цвет плиты наведённой строки (эталон 0x%08X%s)"), *T, r, WantU,
					bHoverIsSel ? TEXT(", строка выбрана — белая плита выбранного") : (bKeyOff ? TEXT(", ключ 0") : TEXT(""))),
					FString::Printf(TEXT("тинт %.3f,%.3f,%.3f,%.3f"), Tint.R, Tint.G, Tint.B, Tint.A));
				// Наведена выбранная строка — плита наведения спрятана (эталон: выбранный квад перекрывает наведённый).
				Expect(bHoverIsSel == !(Sel && AsdaUi::IsShown(Sel)), FString::Printf(TEXT("%s %d плита наведения спрятана ровно на выбранной строке"), *T, r), Dump(Sel));
				if (bKeyOff)
				{
					Expect(Cur && !AsdaUi::IsShown(Cur), FString::Printf(TEXT("%s %d ключ 0: плиты выбранного нет"), *T, r));
				}
				else if (bCurShown)
				{
					// ★ R5-fix-b 13.11.2026 (второй заход): геометрия списка и строки обязана быть СЧИТАНА и ненулевой (`bL`, `bR`, ширина и высота
					//   > 1 px) — раньше на несчитанной геометрии (нули) сравнение «0 ≈ 0» могло пройти.
					const bool bGeo = bL && bR && (LMx.X - LMn.X) > 1.f && (RMx.Y - RMn.Y) > 1.f && (CMx.X - CMn.X) > 1.f;
					Expect(bGeo && FMath::IsNearlyEqual(CMn.X, LMn.X, 1.f) && FMath::IsNearlyEqual(CMx.X, LMx.X, 1.f) && FMath::IsNearlyEqual(CMx.Y - CMn.Y, RMx.Y - RMn.Y, 1.f),
						FString::Printf(TEXT("%s %d плита выбранного пункта — вся ширина списка × строка"), *T, r),
						FString::Printf(TEXT("%s; геометрия считана %d (список x %.1f..%.1f, строка h %.1f)"), *Dump(Cur), bGeo ? 1 : 0, LMn.X, LMx.X, RMx.Y - RMn.Y));
				}
				else if (B.Contains(TEXT("Continent")))
				{
					// Континент выбран всегда (SelContinent ≥ 0) — плита выбранного обязана быть.
					Expect(false, FString::Printf(TEXT("%s %d плита выбранного континента видна"), *T, r), Dump(Cur));
				}
				else
				{
					UE_LOG(LogAsdaDropdownTest, Log, TEXT("САМОТЕСТ-СПИСОК %s %d: выбранного пункта нет (офлайн область не выбрана) — плита выбранного свёрнута"), *T, r);
				}
				Shot(FString::Printf(TEXT("%s_hover%d"), *T, r));
				return 0.3f;
			});
		}
		Steps.Add([this, Win, B, T](UWorld* W) -> float
		{
			MoveCursor(FVector2D(4.f, 4.f));
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			FVector2D Mn, Mx;
			UWidget* Box = Node(W, Win, *(B + TEXT("_Click")));
			if (WM && RectOf(Box, Mn, Mx)) { WM->SimulateClickAt((Mn + Mx) * 0.5f, 2, false); }   // свернуть
			return 0.5f;
		});
	}

	// Экземпляры общей точки `UAsdaJuiComboBox` в окне: первые MaxCombos списков (узлы `<idc>_RowBg0`).
	void AddCommonCombos(FName Win, const TCHAR* Tag, TFunction<void(UWorld*)> Open, TFunction<void(UWorld*)> Close, int32 MaxCombos,
		TFunction<void(UWorld*, UWidget*)> Reveal = nullptr)
	{
		using namespace AsdaDropdownTestLocal;
		const FString T(Tag);
		Steps.Add([Open](UWorld* W) -> float { AsdaDropdownTestLocal::MoveCursor(FVector2D(4.f, 4.f)); Open(W); return 1.0f; });
		TSharedRef<TArray<FString>> Idcs = MakeShared<TArray<FString>>();
		Steps.Add([this, Win, T, Idcs, MaxCombos, Reveal](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			UUserWidget* U = WM ? WM->FindWindow(Win) : nullptr;
			Expect(U && WM->IsWindowVisible(Win), FString::Printf(TEXT("%s 0 окно %s открыто"), *T, *Win.ToString()));
			if (U && U->WidgetTree)
			{
				U->WidgetTree->ForEachWidget([&](UWidget* X)
				{
					const FString N = X->GetName();
					if (!N.EndsWith(TEXT("_RowBg0")) || Idcs->Num() >= MaxCombos) { return; }
					// Список, чьё окно-место (родители `_List`) не показано, раскрыть путём игрока нельзя — пропуск.
					const UWidget* L = X->GetParent();
					bool bVis = L != nullptr;
					for (const UWidget* P = L ? L->GetParent() : nullptr; P && bVis; P = P->GetParent()) { bVis = AsdaUi::IsShown(P); }
					if (bVis || Reveal) { Idcs->Add(N.LeftChop(7)); }
				});
			}
			Expect(Idcs->Num() > 0, FString::Printf(TEXT("%s 0 списков общей точки найдено: %d"), *T, Idcs->Num()), FString::Join(*Idcs, TEXT(", ")));
			return 0.f;
		});
		for (int32 c = 0; c < MaxCombos; ++c)
		{
			// Список может лежать на скрытой вкладке/под-странице окна — показать её путём окна (Reveal).
			Steps.Add([this, Win, Idcs, c, Reveal](UWorld* W) -> float
			{
				if (!Idcs->IsValidIndex(c) || !Reveal) { return 0.f; }
				Reveal(W, Node(W, Win, *((*Idcs)[c] + TEXT("_RowBg0"))));
				return 0.4f;
			});
			// Раскрыть щелчком по телу (план 0 контрола, иначе подпись `<idc>_Text`).
			Steps.Add([this, Win, Idcs, c](UWorld* W) -> float
			{
				if (!Idcs->IsValidIndex(c)) { return 0.f; }
				UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
				UUserWidget* U = WM ? WM->FindWindow(Win) : nullptr;
				UWidget* Body = nullptr;
				if (U && U->WidgetTree)
				{
					U->WidgetTree->ForEachWidget([&](UWidget* X)
					{
						FString I; int32 J = 0, P = 0;
						if (!Body && AsdaUi::ParsePlaneName(X->GetName(), I, J, P) && I == (*Idcs)[c] && P == 0 && AsdaUi::IsShown(X)) { Body = X; }
					});
					if (!Body) { Body = U->GetWidgetFromName(*((*Idcs)[c] + TEXT("_Text"))); }
				}
				FVector2D Mn, Mx;
				if (WM && AsdaDropdownTestLocal::RectOf(Body, Mn, Mx)) { WM->SimulateClickAt((Mn + Mx) * 0.5f, 2, false); return 0.4f; }
				// Тело общей точки — над списком в той же панели (`ApplyListGeometry`: список = тело.низ + 1).
				UWidget* List = Node(W, Win, *((*Idcs)[c] + TEXT("_List")));
				const UCanvasPanelSlot* LS = List ? Cast<UCanvasPanelSlot>(List->Slot) : nullptr;
				UWidget* Par = List ? List->GetParent() : nullptr;
				FVector2D PMn, PMx;
				if (WM && LS && AsdaDropdownTestLocal::RectOf(Par, PMn, PMx))
				{
					const FVector2D LocalSize = Par->GetCachedGeometry().GetLocalSize();
					const FVector2D K((PMx.X - PMn.X) / FMath::Max(LocalSize.X, 1.f), (PMx.Y - PMn.Y) / FMath::Max(LocalSize.Y, 1.f));
					const FMargin O = LS->GetOffsets();
					const FVector2D P = PMn + FVector2D((O.Left + O.Right * 0.5f) * K.X, (O.Top - 11.f) * K.Y);
					UE_LOG(LogAsdaDropdownTest, Log, TEXT("САМОТЕСТ-СПИСОК %s: тело по месту над списком — щелчок (%.0f,%.0f)"), *(*Idcs)[c], P.X, P.Y);
					WM->SimulateClickAt(P, 2, false);
				}
				return 0.4f;
			});
			for (int32 r = 0; r < 2; ++r)
			{
				Steps.Add([this, Win, Idcs, c, r](UWorld* W) -> float
				{
					if (!Idcs->IsValidIndex(c)) { return 0.f; }
					FVector2D Mn, Mx;
					UWidget* Row = Node(W, Win, *FString::Printf(TEXT("%s_Row%d"), *(*Idcs)[c], r));
					if (AsdaDropdownTestLocal::RectOf(Row, Mn, Mx)) { AsdaDropdownTestLocal::MoveCursor(FVector2D(Mn.X + 4.f, (Mn.Y + Mx.Y) * 0.5f)); }
					return 0.25f;
				});
				Steps.Add([this, Win, T, Idcs, c, r](UWorld* W) -> float
				{
					using namespace AsdaDropdownTestLocal;
					if (!Idcs->IsValidIndex(c)) { return 0.f; }
					const FString I = (*Idcs)[c];
					DumpNodes(W, Win, { I + TEXT("_List"), I + TEXT("_ListBg"), FString::Printf(TEXT("%s_RowBg%d"), *I, r), FString::Printf(TEXT("%s_Row%d"), *I, r) },
						FString::Printf(TEXT("%s %s строка %d"), *T, *I, r));
					UWidget* Bg = Node(W, Win, *FString::Printf(TEXT("%s_RowBg%d"), *I, r));
					UWidget* Row = Node(W, Win, *FString::Printf(TEXT("%s_Row%d"), *I, r));
					UWidget* List = Node(W, Win, *(I + TEXT("_List")));
					FVector2D BMn = FVector2D::ZeroVector, BMx = FVector2D::ZeroVector, RMn = FVector2D::ZeroVector, RMx = FVector2D::ZeroVector, LMn = FVector2D::ZeroVector, LMx = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
					const bool bB = Bg && AsdaUi::IsShown(Bg) && RectOf(Bg, BMn, BMx);
					const bool bOk = bB && RectOf(Row, RMn, RMx) && RectOf(List, LMn, LMx)
						&& FMath::IsNearlyEqual(BMn.X, LMn.X, 1.f) && FMath::IsNearlyEqual(BMx.X, LMx.X, 1.f)
						&& BMn.Y <= RMn.Y + 1.f && BMx.Y >= RMx.Y - 2.f;
					Expect(bOk, FString::Printf(TEXT("%s %s %d подсветка закрывает строку на всю ширину списка"), *T, *I, r),
						FString::Printf(TEXT("плита (%.1f,%.1f)-(%.1f,%.1f), строка (%.1f,%.1f)-(%.1f,%.1f), список x %.1f..%.1f"),
							BMn.X, BMn.Y, BMx.X, BMx.Y, RMn.X, RMn.Y, RMx.X, RMx.Y, LMn.X, LMx.X));
					Shot(FString::Printf(TEXT("%s_%s_hover%d"), *T, *I, r));
					return 0.2f;
				});
			}
			Steps.Add([this](UWorld* W) -> float
			{
				AsdaDropdownTestLocal::MoveCursor(FVector2D(4.f, 4.f));
				if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W)) { WM->SimulateClickAt(FVector2D(4.f, 4.f), 2, false); }   // щелчок мимо — свернуть
				return 0.3f;
			});
		}
		Steps.Add([Close](UWorld* W) -> float { Close(W); return 0.5f; });
	}

	void BuildSteps()
	{
		AddWorldMap(TEXT("MaxMap_Area"), 3, TEXT("worldmap_area"));
		AddWorldMap(TEXT("MaxMap_Continent"), 2, TEXT("worldmap_continent"));
		AddWorldMap(TEXT("MaxMap_Area"), 1, TEXT("worldmap_area_key0"), /*bKeyOff*/ true);
		Steps.Add([](UWorld* W) -> float
		{
			if (AAsdaPlayerController* P = Cast<AAsdaPlayerController>(W->GetFirstPlayerController())) { P->RunCommandForCheck(37); }   // закрыть карту
			return 0.5f;
		});
		// Общая точка выпадающих списков: посредник (панель поиска) и настройки.
		AddCommonCombos(FName(TEXT("EXCHANGE_WIN")), TEXT("exchange"),
			// Режим 4 «просмотр»: списки критериев поиска видны всегда, порога уровня нет (режим 3 окна не показывает —
			//   панель «мои лоты» открывает приёмник 9908; сроки EXCH_DUE режима 1 видны только с выставленной вещью).
			[](UWorld* W) { if (auto* S = Sub<UAsdaExchangeSubsystem>(W)) { S->OpenMode(4); } },
			[](UWorld* W) { if (auto* S = Sub<UAsdaExchangeSubsystem>(W)) { S->CloseAll(); } }, 2);
		AddCommonCombos(FName(TEXT("SYSTEM_Option_NEW")), TEXT("options"),
			[](UWorld* W) { if (auto* S = Sub<UAsdaSystemOptionSubsystem>(W)) { S->ShowOptions(); } },
			[](UWorld* W) { if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W)) { WM->CloseWindow(FName(TEXT("SYSTEM_Option_NEW"))); } }, 2,
			// Списки настроек лежат на под-страницах вкладок — открыть ту, в чьей группе список (таблица групп SetupControls).
			[](UWorld* W, UWidget* N)
			{
				static const struct { const TCHAR* Grp; int32 Tab, Page; } Pages[] = {
					{ TEXT("SYSTEM_Op_INT_1_grp"), 0, 0 }, { TEXT("SYSTEM_Op_INT_2_grp"), 0, 1 }, { TEXT("SYSTEM_Op_INT_3_grp"), 0, 2 },
					{ TEXT("SYSTEM_Op_SYSTEM_souund_grp"), 1, 0 }, { TEXT("SYSTEM_Op_SYSTEM_screen_grp"), 1, 1 }, { TEXT("SYSTEM_Op_SYSTEM_3d_OP_grp"), 1, 2 },
					{ TEXT("SYSTEM_Op_SYSTEM_quality_grp"), 1, 3 }, { TEXT("SYSTEM_Op_SYSTEM_effects_grp"), 1, 4 } };
				auto* S = Sub<UAsdaSystemOptionSubsystem>(W);
				for (UWidget* P = N ? N->GetParent() : nullptr; S && P; P = P->GetParent())
				{
					for (const auto& E : Pages)
					{
						if (P->GetName() == E.Grp)
						{
							S->SelectTab(E.Tab); S->SelectSubPage(E.Tab, E.Page);
							UE_LOG(LogAsdaDropdownTest, Log, TEXT("САМОТЕСТ-СПИСОК options: список в %s — вкладка %d страница %d"), E.Grp, E.Tab, E.Page);
							return;
						}
					}
				}
			});
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaDropdownHoverSelfTest(
		TEXT("asda.UI.SelfTestDropdownHover"),
		TEXT("asda.UI.SelfTestDropdownHover [delay=N] [quit] — ОФЛАЙН-самотест пачки 20 (баг 1): подсветка пункта выпадающего списка ")
		TEXT("при наведении закрывает строку на всю ширину списка (эталон uiListGrid_layoutRows), кадры RU/EN в Reports/batch20/dropdown_hover."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaDropdownTestLocal::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			float Delay = 0.f;
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix-b 13.11.2026 (третий заход): `delay=N` — общая точка (был цикл из одной этой ветки)
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaDropdownHoverSelfTest> T = MakeShared<FAsdaDropdownHoverSelfTest>();
				T->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
				T->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch20/dropdown_hover")));
				T->BuildSteps();
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				TSharedRef<double> Next = MakeShared<double>(0.0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, Next, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(Who); return false; }
					const double Now = FPlatformTime::Seconds();
					if (Now < *Next) { return true; }
					if (*Idx < T->Steps.Num()) { *Next = Now + T->Steps[(*Idx)++](W3); return true; }
					AsdaCVarSave::RestoreAll(Who);
					UE_LOG(LogAsdaDropdownTest, Log, TEXT("САМОТЕСТ-СПИСОК ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaDropdownTest, Log, TEXT("САМОТЕСТ-СПИСОК: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
