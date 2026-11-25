// Copyright (c) 2026 @sitxovski (Discord)
// ★ Реализация единых UI-хелперов порта (см. AsdaUiHelpers.h; аудит Reports/code_dup_audit_ui.md).
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaUiSlotDrift.h"   // ★ 25.10.2026: учёт внешнего сдвига слота (перетаскивание/постановка окна)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"           // ★ состояния подписи кнопки: FButtonStyle как хранилище
#include "Components/Image.h"            // B4: сам «призрак» перетаскивания — UImage
#include "UI/AsdaAnimatedPlane.h"         // ★ В2-B (U19): перезапуск кадров рамки выбора в `PlaceFrameOver`
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Styling/SlateBrush.h"
#include "Components/TextBlock.h"        // D4b: единая точка обводки текста (SetTextOutline)
#include "Components/TextWidgetTypes.h"   // UTextLayoutWidget — чтение Justification рефлексией
#include "Components/Widget.h"
#include "Components/PanelWidget.h"    // ★ клон поддерева окна: приёмник и обход детей
#include "Components/PanelSlot.h"      // ★ перенос свойств слота в клон
#include "InputCoreTypes.h"              // B3: EKeys::LeftMouseButton / RightMouseButton
#include "Engine/GameInstance.h"
#include "Login/AsdaButtonRelay.h"     // ★ мостик hover/press для состояний подписи кнопок
#include "UObject/UnrealType.h"
#include <initializer_list>    // ★ FBoolProperty: снятие `IsFocusable` у кнопок (синее кольцо фокуса)
#include "AsdaDataTableCache.h"       // таблица исходных размеров атласов
#include "Engine/Texture2D.h"
#include "Resources/AsdaTextureSubsystem.h"   // Д2: плоская JUI-иконка берётся ТОЛЬКО через менеджер текстур
#include "Text/AsdaTextSubsystem.h"      // D10: GetTextByIdRaw — единственный источник loc-строк
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"   // фолбэк угла камеры: мировой yaw стрелы
#include "World/AsdaPlayerCharacter.h"          // ★ общая точка `AsdaUi::CameraConeYaw`
#include "World/AsdaPlayerController.h"         //   (её же зовут мини-карта, карта мира и трекер 1800)
#include "AsdaNetSubsystem.h"                    // ★ общая точка «война за города»: владелец и ставка
#include "World/AsdaWorldSubsystem.h"             //   (фракция игрока)
#include "World/AsdaLoadManagerSubsystem.h"        //   (текущая карта)
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Fonts/CompositeFont.h"
#include "Engine/Font.h"
#include "HAL/IConsoleManager.h"            // ★ волна 1: ключи asda.UI.InteractiveHitTest / ItemIconWrap
#include "Containers/Ticker.h"              // ★ волна В6 ремесла: покадровый пересчёт флага `+0x24F` (SetTopRender)
#include "Items/AsdaItemTableSubsystem.h"   // ★ волна 1: FAsdaItemIcon — общая точка иконки предмета
#include "UI/AsdaWindowSubsystem.h"         // ★ П8-9: модальный гейт покадрового опроса (IsAnyModalOpen)
#include "UI/AsdaHighRefineSubsystem.h"     // ★ волна 9 заточки: `CheckRecentActivity` (условия окна 78200)
#include "UI/AsdaDisassemblySubsystem.h"     // ★ волна 2-А: `CheckRecentActivity` (условия 2 и 4 — окно разборки 32000)
#include "Resources/AsdaSoundSubsystem.h"   // ★ волна 9 заточки: `PlayInvenButtonSfx` (звук id 5)
#include "Components/CheckBox.h"            // ★ IsInteractiveWidget (переезд из вкладки друзей)
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/ComboBoxKey.h"
#include "Components/Slider.h"
#include "Components/SpinBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBar.h"
#include "Components/ListViewBase.h"

// Категория лога общих точек UI. Заведена 24.09.2026 вместе с `ApplySceneMaterialBrush`:
//   без неё сообщение о пересборке кисти сцены было бы некуда писать.
DEFINE_LOG_CATEGORY_STATIC(LogAsdaUiHelpers, Log, All);

namespace AsdaUi
{
	// ── D1 ───────────────────────────────────────────────────────────────────────────────────────
	// Точная sRGB→linear кривая. Для байтовых значений FColor::FromHex даёт то же (его 256-строчная
	//   таблица — тот же pow, посчитанный по байтам), но здесь она ЯВНАЯ и общая для всех потребителей.
	//   ★ Экспортирована (объявление в .h): обе стороны кривой теперь ОДНА точка — раньше обратную
	//     (linear→sRGB) выписывали копией в `AsdaWindowSubsystem` и ниже в этом же файле.
	float SrgbToLinearChannel(float C)
	{
		return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
	}

	// Обратная кривая linear→sRGB (IEC 61966-2-1). Нужна там, где линейный компонент надо вернуть
	//   в sRGB для сравнения ЯРКОСТИ подписи (эвристика семейства вкладок, `ApplyTabCaptionState`).
	float LinearToSrgbChannel(float L)
	{
		return (L <= 0.0031308f) ? (L * 12.92f) : (1.055f * FMath::Pow(L, 1.f / 2.4f) - 0.055f);
	}

	FLinearColor SrgbHexToLinear(const FString& Hex)
	{
		// Собрать только hex-цифры (терпит разделители/пробелы в данных скина).
		auto Nib = [](TCHAR Ch) -> uint32
		{
			if (Ch >= TEXT('0') && Ch <= TEXT('9')) { return (uint32)(Ch - TEXT('0')); }
			if (Ch >= TEXT('a') && Ch <= TEXT('f')) { return (uint32)(Ch - TEXT('a') + 10); }
			if (Ch >= TEXT('A') && Ch <= TEXT('F')) { return (uint32)(Ch - TEXT('A') + 10); }
			return 0u;
		};
		FString S;
		for (TCHAR Ch : Hex) { if (FChar::IsHexDigit(Ch)) { S.AppendChar(Ch); } }
		const int32 L = S.Len();
		auto Byte = [&](int32 i) { return (float)((Nib(S[i]) << 4) | Nib(S[i + 1])) / 255.f; };
		if (L >= 8)
		{
			// AARRGGBB: альфа линейна, RGB через кривую.
			return FLinearColor(SrgbToLinearChannel(Byte(2)), SrgbToLinearChannel(Byte(4)),
			                    SrgbToLinearChannel(Byte(6)), Byte(0));
		}
		if (L >= 6)
		{
			// RRGGBB: альфа = 1 (эталон маскирует альфу у разметки/декоратора).
			return FLinearColor(SrgbToLinearChannel(Byte(0)), SrgbToLinearChannel(Byte(2)),
			                    SrgbToLinearChannel(Byte(4)), 1.f);
		}
		return FLinearColor::White;   // пусто/битьё
	}

	FLinearColor SrgbU32ToLinear(uint32 Argb)
	{
		return FLinearColor(SrgbToLinearChannel(((Argb >> 16) & 0xFF) / 255.f),
		                    SrgbToLinearChannel(((Argb >> 8) & 0xFF) / 255.f),
		                    SrgbToLinearChannel((Argb & 0xFF) / 255.f),
		                    ((Argb >> 24) & 0xFF) / 255.f);   // альфа линейна
	}

	// ── D2 ───────────────────────────────────────────────────────────────────────────────────────
	FVector2D ScreenToCanvasLocal(const UUserWidget* Host, const FVector2D& ScreenPos, const UWorld* FallbackWorld)
	{
		if (Host && Host->WidgetTree)
		{
			if (UCanvasPanel* Root = Cast<UCanvasPanel>(Host->WidgetTree->RootWidget))
			{
				const FGeometry& G = Root->GetCachedGeometry();
				const float S = FMath::Max(0.01f, (float)G.Scale);
				if (G.GetLocalSize().X > 1.f && G.GetLocalSize().Y > 1.f)
				{
					return (ScreenPos - G.GetAbsolutePosition()) / (double)S;
				}
			}
		}
		// Канвас ещё не отрисован (нет геометрии) — падаем на прежний путь, чтобы не давать (0,0).
		const float Scale = FMath::Max(0.01f, FallbackWorld ? UWidgetLayoutLibrary::GetViewportScale(const_cast<UWorld*>(FallbackWorld)) : 1.f);
		return ScreenPos / (double)Scale;
	}

	// ── D3 ───────────────────────────────────────────────────────────────────────────────────────
	bool WorldToViewportFollow(APlayerController* PC, const UWorld* W, const FVector& WorldPt, float Margin, FVector2D& OutScreen)
	{
		OutScreen = FVector2D::ZeroVector;
		if (!PC || !W) { return false; }
		// bPlayerViewportRelative=true → координаты вьюпорта игрока (учитывает сплит/окно).
		if (!PC->ProjectWorldLocationToScreen(WorldPt, OutScreen, /*bPlayerViewportRelative=*/true))
		{
			return false;   // за камерой (эталон planeAccumulateDotTest)
		}
		// Фрустум-отсечка: проекция за краем экрана клампится, поэтому проверяем границы с запасом.
		const FVector2D VpSize = UWidgetLayoutLibrary::GetViewportSize(const_cast<UWorld*>(W));
		if (OutScreen.X < -Margin || OutScreen.Y < -Margin || OutScreen.X > VpSize.X + Margin || OutScreen.Y > VpSize.Y + Margin)
		{
			return false;
		}
		return true;
	}

	// ── D4 ───────────────────────────────────────────────────────────────────────────────────────
	float MeasureLineHeight(const FSlateFontInfo& Font, float Fallback)
	{
		if (FSlateApplication::IsInitialized())
		{
			const TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			return (float)FM->GetMaxCharacterHeight(Font, 1.f);
		}
		return Fallback;
	}

	float MeasureTextWidth(const FString& Text, const FSlateFontInfo& Font, float Fallback)
	{
		if (Text.IsEmpty()) { return 0.f; }
		if (FSlateApplication::IsInitialized())
		{
			const TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			return (float)FM->Measure(Text, Font, 1.f).X;
		}
		return Fallback;
	}

	// ── D4b ──────────────────────────────────────────────────────────────────────────────────────
	// Обводка текста: единая точка снятия/постановки (обоснование — в заголовке).
	// Пишем шрифт обратно ТОЛЬКО при фактическом отличии: SetFont() у TextBlock инвалидирует
	//   раскладку, а зовут эту функцию на каждый показ строки.
	// Единственная реализация чтения `Justification` (свойство protected, геттера нет).
	ETextJustify::Type ReadJustification(const UTextBlock* Text)
	{
		if (!Text) { return ETextJustify::Left; }
		static const FByteProperty* Prop = CastField<FByteProperty>(
			UTextLayoutWidget::StaticClass()->FindPropertyByName(FName(TEXT("Justification"))));
		if (!Prop) { return ETextJustify::Left; }
		return (ETextJustify::Type)Prop->GetPropertyValue_InContainer(Text);
	}

	bool SetTextOutline(UTextBlock* Text, int32 Px, const FLinearColor& Color)
	{
		if (!Text) { return false; }
		const int32 Want = FMath::Max(0, Px);
		FSlateFontInfo F = Text->GetFont();
		const bool bSizeDiff  = (F.OutlineSettings.OutlineSize != Want);
		const bool bColorDiff = (Want > 0 && F.OutlineSettings.OutlineColor != Color);
		if (!bSizeDiff && !bColorDiff) { return false; }
		F.OutlineSettings.OutlineSize = Want;
		if (Want > 0) { F.OutlineSettings.OutlineColor = Color; }
		Text->SetFont(F);
		return true;
	}

	// ── D4c ──────────────────────────────────────────────────────────────────────────────────────
	// Двусторонний обход дерева окна (обоснование — в заголовке). Дерево берём у `WidgetTree`:
	//   он видит и узлы, созданные импортёром, и добавленные кодом в рантайме.
	int32 ApplyTextOutlineTree(UUserWidget* Host, int32 DefaultPx,
		const TSet<FName>& Except, int32 ExceptPx, int32* OutExceptCount)
	{
		if (OutExceptCount) { *OutExceptCount = 0; }
		if (!Host || !Host->WidgetTree) { return 0; }
		TArray<UWidget*> All;
		Host->WidgetTree->GetAllWidgets(All);
		int32 Touched = 0;
		for (UWidget* W : All)
		{
			UTextBlock* T = Cast<UTextBlock>(W);
			if (!T) { continue; }
			const bool bExcept = Except.Contains(T->GetFName());
			SetTextOutline(T, bExcept ? ExceptPx : DefaultPx);
			++Touched;
			if (bExcept && OutExceptCount) { ++(*OutExceptCount); }
		}
		return Touched;
	}

	// ── D6 ───────────────────────────────────────────────────────────────────────────────────────
	float ViewportDpiScale(const UWorld* World)
	{
		return FMath::Max(0.01f, World ? UWidgetLayoutLibrary::GetViewportScale(const_cast<UWorld*>(World)) : 1.f);
	}

	FVector2D ViewportSizeUnits(const UWorld* World)
	{
		if (!World) { return FVector2D(1920.f, 1080.f); }
		const FVector2D Px = UWidgetLayoutLibrary::GetViewportSize(const_cast<UWorld*>(World));
		return Px / (double)ViewportDpiScale(World);
	}

	// ★ Дедуп №2, В1-B (U16): кламп попапа к экрану — формула копий меню 28000 / панели 43058 (см. объявление).
	FVector2D ClampPopupToViewport(const FVector2D& Pos, const FVector2D& Size, const FVector2D& View)
	{
		if (!(View.X > 1.0 && View.Y > 1.0)) { return Pos; }
		return FVector2D(FMath::Clamp(Pos.X, 0.0, FMath::Max(0.0, View.X - Size.X)),
		                 FMath::Clamp(Pos.Y, 0.0, FMath::Max(0.0, View.Y - Size.Y)));
	}

	// ★ Дедуп №2, В1-B (U16): угол окна по центру экрана (эталон `MiniMap_BuildUI@0xD63220`, копии 26500/бустера).
	FVector2D CenteredOrigin(const FVector2D& View, const FVector2D& Size)
	{
		return FVector2D(FMath::RoundToFloat(View.X * 0.5 - Size.X * 0.5), FMath::RoundToFloat(View.Y * 0.5 - Size.Y * 0.5));
	}

	// ★ Дедуп №2, В1-B (U16): перенос построчно одинаковых тел окна пакета 26500 и бустера (сдвиг всех детей корня).
	void CenterRootOnViewport(UCanvasPanel* Root, const UCanvasPanelSlot* RefSlot, const FVector2D& Size, const UWorld* World)
	{
		if (!Root || !RefSlot) { return; }
		const FVector2D Want = CenteredOrigin(ViewportSizeUnits(World), Size);
		const FVector2D Delta(Want.X - RefSlot->GetOffsets().Left, Want.Y - RefSlot->GetOffsets().Top);
		if (Delta.IsNearlyZero()) { return; }
		// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): сдвиг детей корня — SPAN-безопасный (был только `Left/Top += d`).
		ShiftRootChildren(Root, Delta);
	}

	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): единое правило сдвига на дельту (см. объявление). Тело — бывший
	//   `ShiftCanvasSlot` оконной подсистемы ≡ `ShiftSlot` миникарты (там теперь форварды).
	FMargin ShiftedOffsets(const FMargin& O, const FAnchors& A, const FVector2D& D)
	{
		FMargin M = O;
		M.Left += D.X;
		if (!FMath::IsNearlyEqual(A.Minimum.X, A.Maximum.X)) { M.Right -= D.X; }
		M.Top += D.Y;
		if (!FMath::IsNearlyEqual(A.Minimum.Y, A.Maximum.Y)) { M.Bottom -= D.Y; }
		return M;
	}

	void ShiftCanvasSlot(UWidget* W, const FVector2D& D)
	{
		UCanvasPanelSlot* S = W ? SlotOf(W) : nullptr;
		if (!S) { return; }
		S->SetOffsets(ShiftedOffsets(S->GetOffsets(), S->GetAnchors(), D));
	}

	int32 ShiftRootChildren(UPanelWidget* Root, const FVector2D& D)
	{
		if (!Root) { return 0; }
		int32 N = 0;
		for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
		{
			UWidget* C = Root->GetChildAt(i);
			if (!SlotOf(C)) { continue; }
			ShiftCanvasSlot(C, D);
			++N;
		}
		return N;
	}

	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U19): рамка выбора над ячейкой — одна функция (см. объявление).
	void PlaceFrameOver(UWidget* Frame, const UWidget* Over, bool bRestartAnim)
	{
		if (!Frame || !Over) { return; }
		if (UCanvasPanelSlot* Dst = SlotOf(Frame))
		{
			// Позиция родителя рамки в координатах корня = позиция рамки − её собственное смещение (≡ CanvasRootPos(родитель)).
			const FVector2D Own(Dst->GetOffsets().Left, Dst->GetOffsets().Top);
			const FVector2D ParentRoot = CanvasRootPos(Frame) - Own;
			Dst->SetPosition(CanvasRootPos(Over) - ParentRoot);
		}
		Frame->SetVisibility(ESlateVisibility::HitTestInvisible);   // NoSelect=1: рамка мышь не ловит
		if (bRestartAnim)
		{
			if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(Frame)) { P->RestartAnim(); }
		}
	}

	bool CursorInViewportUnits(const UWorld* World, FVector2D& OutPos)
	{
		const float S = ViewportDpiScale(World);
		// Основной путь: координаты вьюпорта у контроллера (положение окна на десктопе уже снято).
		if (World)
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				float X = 0.f, Y = 0.f;
				if (PC->GetMousePosition(X, Y))
				{
					OutPos = FVector2D(X / S, Y / S);
					return true;
				}
			}
		}
		// Фолбэк: абсолютный курсор Slate (верно при окне в (0,0) / полном экране).
		const FVector2D Abs = FSlateApplication::IsInitialized()
			? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
		OutPos = Abs / (double)S;
		return false;
	}

	// ── D7 ───────────────────────────────────────────────────────────────────────────────────────
	bool FontHasTypeface(const FSlateFontInfo& Font, FName Typeface)
	{
		const UFont* F = Cast<UFont>(Font.FontObject);
		const FCompositeFont* CF = F ? F->GetCompositeFont() : nullptr;
		if (!CF) { return false; }
		for (const FTypefaceEntry& E : CF->DefaultTypeface.Fonts)
		{
			if (E.Name == Typeface) { return true; }
		}
		return false;
	}

	// ── D7b: псевдо-жирный (разбор и отступление — у объявления в `.h`) ──────────────────────────
	void PseudoBoldShadow(const FLinearColor& FillColor, FVector2D& OutOffset, FLinearColor& OutColor)
	{
		// ВТОРОЙ ПРОХОД ТОЛЬКО ПО ГОРИЗОНТАЛИ. Эталон: `penX += 1` → квад → `penX -= 1` → квад,
		//   то есть {x+1, x} — тень Slate со сдвигом (1,0) даёт РОВНО ЭТИ ЖЕ две позиции.
		OutOffset = FVector2D(1.0, 0.0);
		OutColor  = FillColor;             // тем же цветом, что основной проход
	}

	void ApplyPseudoBold(UTextBlock* Text)
	{
		if (!Text) { return; }
		FVector2D Off; FLinearColor Col;
		// Цвет берём У САМОГО КОНТРОЛА — второй проход обязан совпадать с основным.
		PseudoBoldShadow(Text->GetColorAndOpacity().GetSpecifiedColor(), Off, Col);
		Text->SetShadowOffset(Off);
		Text->SetShadowColorAndOpacity(Col);
	}

	// ── D5 ───────────────────────────────────────────────────────────────────────────────────────
	UWidget* FindNodeByName(const UUserWidget* Host, const TCHAR* Exact, const TCHAR* Needle)
	{
		if (!Host) { return nullptr; }
		if (UWidget* W = Host->GetWidgetFromName(FName(Exact))) { return W; }
		TArray<UWidget*> Ws;
		if (Host->WidgetTree) { Host->WidgetTree->GetAllWidgets(Ws); }
		for (UWidget* W : Ws)
		{
			if (W && W->GetName().Contains(Needle, ESearchCase::IgnoreCase)) { return W; }
		}
		return nullptr;
	}

	// ── Д1 ───────────────────────────────────────────────────────────────────────────────────────
	// Только точное имя. `GetWidgetFromName` внутри — `WidgetTree->FindWidget(FName)`, то есть
	//   сравнение по `FName`; никакого подстрочного фолбэка здесь быть не должно (см. .h).
	UWidget* FindNodeExact(const UUserWidget* Host, const TCHAR* Exact)
	{
		return (Host && Exact) ? Host->GetWidgetFromName(FName(Exact)) : nullptr;
	}

	// ── D11 ──────────────────────────────────────────────────────────────────────────────────────
	UWidget* FindNodeByPrefix(const UUserWidget* Host, const TCHAR* Prefix)
	{
		if (!Host || !Host->WidgetTree || !Prefix) { return nullptr; }
		UWidget* Found = nullptr;
		const FString P(Prefix);
		Host->WidgetTree->ForEachWidget([&Found, &P](UWidget* W)
		{
			if (Found || !W) { return; }
			if (W->GetName().StartsWith(P)) { Found = W; }
		});
		return Found;
	}

	// ══════ ВТОРАЯ ВОЛНА СВЕДЕНИЯ (аудит `Reports/code_dedup_audit_2.md`) ══════════════════════

	// ── D1b ──────────────────────────────────────────────────────────────────────────────────────
	bool WidgetAbsRect(const UWidget* W, FVector2D& OutMin, FVector2D& OutMax)
	{
		if (!W) { return false; }
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Sz = G.GetLocalSize() * G.Scale;
		if (Sz.X <= 0.f || Sz.Y <= 0.f) { return false; }   // ни разу не отрисован
		OutMin = G.GetAbsolutePosition();
		OutMax = OutMin + Sz;
		return true;
	}

	// ── D1c: корпус окна по планам его рамки (обоснование — в шапке объявления) ──────────────────
	bool FramePlanesAbsRect(const UUserWidget* Host, const TCHAR* PlaneFmt, int32 Count,
		FVector2D& OutMin, FVector2D& OutMax)
	{
		if (!Host || !PlaneFmt || Count <= 0) { return false; }
		bool bAny = false;
		FVector2D Mn(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D Mx(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
		for (int32 i = 0; i < Count; ++i)
		{
			// ⚠ Не `FString::Printf`: с UE 5.4 формат проверяется на этапе компиляции и обязан быть
			//   литералом, а он приходит параметром. Подстановка вручную — тот же результат.
			const FString Name = FString(PlaneFmt).Replace(TEXT("%d"), *FString::FromInt(i));
			const UWidget* W = FindNodeExact(Host, *Name);
			if (!W || W->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
			FVector2D P, Q;
			if (!WidgetAbsRect(W, P, Q)) { continue; }
			Mn = FVector2D(FMath::Min(Mn.X, P.X), FMath::Min(Mn.Y, P.Y));
			Mx = FVector2D(FMath::Max(Mx.X, Q.X), FMath::Max(Mx.Y, Q.Y));
			bAny = true;
		}
		if (!bAny) { return false; }
		OutMin = Mn; OutMax = Mx;
		return true;
	}

	// ── D1 ───────────────────────────────────────────────────────────────────────────────────────
	bool GUiInputHidden = false;

	bool IsWidgetInHiddenUi(const UWidget* W)
	{
		if (!GUiInputHidden || !W) { return false; }
		// Корневое окно: самый внешний UUserWidget по цепочке Outer (вложенные виджеты живут в дереве окна).
		const UUserWidget* Top = nullptr;
		for (const UObject* O = W; O; O = O->GetOuter()) { if (const UUserWidget* U = Cast<UUserWidget>(O)) { Top = U; } }
		if (!Top || !Top->IsInViewport()) { return false; }
		const TSharedPtr<SWidget> Root = Top->GetCachedWidget();
		const TSharedPtr<SWidget> Host = Root.IsValid() ? Root->GetParentWidget() : nullptr;
		return Host.IsValid() && Host->GetRenderOpacity() <= 0.f;   // так прячет `UAsdaUiVisibilitySubsystem`
	}

	bool IsCursorOverWidget(const UWidget* W)
	{
		if (!W || !FSlateApplication::IsInitialized()) { return false; }
		if (IsWidgetInHiddenUi(W)) { return false; }   // ★ 21.10.2026 (Alt+Z): спрятанное окно курсор не ловит
		const ESlateVisibility V = W->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		FVector2D Mn, Mx;
		if (!WidgetAbsRect(W, Mn, Mx)) { return false; }
		const FVector2D C = FSlateApplication::Get().GetCursorPos();
		return C.X >= Mn.X && C.X <= Mx.X && C.Y >= Mn.Y && C.Y <= Mx.Y && !IsCoveredByHigherWindow(W, C);
	}

	bool IsCoveredByHigherWindow(const UWidget* W, const FVector2D& Pt)
	{
		const UWorld* World = W ? W->GetWorld() : nullptr;
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		return WM && WM->IsWidgetBelowCatcher(W, Pt);
	}

	// ── D5 (имя плана импортёра) ─────────────────────────────────────────────────────────────────
	bool ParsePlaneName(const FString& Name, FString& OutIdc, int32& OutJuiIdx, int32& OutPlane)
	{
		int32 P2 = INDEX_NONE, P1 = INDEX_NONE;
		if (!Name.FindLastChar(TEXT('_'), P2) || P2 <= 0) { return false; }
		const FString TailPlane = Name.Mid(P2 + 1);
		if (TailPlane.IsEmpty() || !TailPlane.IsNumeric()) { return false; }
		const FString Head = Name.Left(P2);
		if (!Head.FindLastChar(TEXT('_'), P1) || P1 <= 0) { return false; }
		const FString TailIdx = Head.Mid(P1 + 1);
		if (TailIdx.IsEmpty() || !TailIdx.IsNumeric()) { return false; }
		OutIdc    = Head.Left(P1);
		OutJuiIdx = FCString::Atoi(*TailIdx);
		OutPlane  = FCString::Atoi(*TailPlane);
		return true;
	}

	// ══════ ЧЕТВЁРТАЯ ВОЛНА (аудит `Reports/system_dedup_audit_4.md`, B1) ══════════════════════

	// ── ★ 25.10.2026 [ИСПРАВЛЕНИЕ]: учёт внешнего сдвига слота (разбор — `AsdaUiSlotDrift.h`) ──────────────
	//   Что МЫ поставили узлу в прошлый раз (Left/Top). Ключ слабый: пересозданный виджет — новый узел, начнёт с базы.
	//   Только игровой поток (вся раскладка UMG живёт в нём).
	static TMap<TWeakObjectPtr<UWidget>, FVector2D> GSlotDriftLast;
	static ASDA_DEV_CVAR(int32, CVarBaseShiftKeepsDrag,
		TEXT("asda.UI.BaseShiftKeepsDrag"), 1,
		TEXT("1 = узлы, которые код окна ставит «от авторской базы» (сжатие полос, скрытие строк), сохраняют сдвиг окна "
		     "перетаскиванием и постановкой (исправление 25.10.2026); 0 = прежнее «база − сдвиг» (сдвиг окна теряется)."),
		ECVF_Default);

	bool BaseShiftKeepsDrag() { return CVarBaseShiftKeepsDrag.GetValueOnGameThread() != 0; }

	// ★ 25.10.2026 (ревизия `Reports/wave_1025b_review/REVIEW.md`, 5.1) [ИСПРАВЛЕНИЕ]: чистка мёртвых узлов таблицы — РЕДКАЯ.
	//   Было: `SlotExternalDrift` проходил всю таблицу на КАЖДЫЙ узел (O(N²) на перестройку склада), а `ShiftSlotY/XFromBase` записи
	//   только добавляли — после пересоздания окон таблица росла до ближайшей перестройки склада. Теперь мёртвые ключи (виджет уничтожен —
	//   слабая ссылка погасла) снимаются одним проходом, когда таблица дорастает до порога; порог после чистки — вдвое больше живых
	//   (не меньше `kSlotDriftSweepMin`), то есть в среднем O(1) на запись. Результат раскладки не меняется: мёртвый ключ ни с одним живым
	//   виджетом не совпадает (номер и серия объекта), поэтому поведение чистки не ключуется.
	static constexpr int32 kSlotDriftSweepMin = 256;   // нижний порог чистки (десятки узлов на окно × несколько окон)
	static int32 GSlotDriftSweepAt = kSlotDriftSweepMin; // размер таблицы, при котором новая запись сначала снимет мёртвые ключи

	// Запись «что мы поставили» узлу `W` (новая — со значением `Init`). Единственная точка добавления в `GSlotDriftLast`.
	static FVector2D& SlotDriftRecord(UWidget* W, const FVector2D& Init)
	{
		if (GSlotDriftLast.Num() >= GSlotDriftSweepAt && !GSlotDriftLast.Contains(W))
		{
			for (auto It = GSlotDriftLast.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
			GSlotDriftLast.Compact();
			GSlotDriftSweepAt = FMath::Max(kSlotDriftSweepMin, GSlotDriftLast.Num() * 2);
		}
		return GSlotDriftLast.FindOrAdd(W, Init);
	}

	// Внешний перенос с прошлого нашего вызова: текущее − наше прошлое (первый вызов — относительно базы).
	static FVector2D SlotExternalDrift(UWidget* W, const FMargin& Cur, const FMargin& Base)
	{
		const FVector2D* Last = GSlotDriftLast.Find(W);   // ★ ревизия 5.1: мёртвые узлы снимает `SlotDriftRecord`, не каждый вызов
		// Ось, которую мы ещё не ставили (метка Lowest — её ведёт только Y- или X-близнец), считается от базы.
		const double Lo = TNumericLimits<double>::Lowest();
		const FVector2D Prev((Last && Last->X != Lo) ? Last->X : Base.Left, (Last && Last->Y != Lo) ? Last->Y : Base.Top);
		return FVector2D(Cur.Left - Prev.X, Cur.Top - Prev.Y);
	}

	bool SetOffsetsKeepingDrift(UWidget* W, const FMargin& Desired, const FMargin& Base)
	{
		UCanvasPanelSlot* S = W ? SlotOf(W) : nullptr;
		if (!S) { return false; }
		FMargin O = Desired;
		if (BaseShiftKeepsDrag())
		{
			const FVector2D D = SlotExternalDrift(W, S->GetOffsets(), Base);
			O.Left += (float)D.X; O.Top += (float)D.Y;
		}
		S->SetOffsets(O);
		SlotDriftRecord(W, FVector2D()) = FVector2D(Desired.Left, Desired.Top);
		return true;
	}

	// ── B1: слот канваса ─────────────────────────────────────────────────────────────────────────
	// Реализация — подробный комментарий в `AsdaUiHelpers.h` (ловушка склада D19: считать
	//   сдвиг от ИСХОДНОГО смещения, иначе повторный вызов даёт ноль и окно не перестраивается).
	// ⚠⚠ ОГРАНИЧЕНИЕ (25.10.2026, ревизия `wave_1025b_review` 5.2; у объявления в горячем `AsdaUiHelpers.h` не написано, чтобы не
	//   пересобирать ~200 файлов): узел, который ведут `ShiftSlotYFromBase` / `ShiftSlotXFromBase`, НЕЛЬЗЯ ставить напрямую
	//   (`SetOffsets`, `SetPosition`, возврат к базе) между их вызовами. Разница «текущее − что мы ставили в прошлый раз» считается
	//   внешним сдвигом (перетаскивание, постановка окна) и прибавляется: прямой возврат слота к базе сложит сдвиг ДВАЖДЫ.
	//   Нужна новая позиция — передайте её через тот же помощник (другое dY/dX) или через `SetOffsetsKeepingDrift`.
	bool ShiftSlotYFromBase(TMap<TWeakObjectPtr<UWidget>, FMargin>& Base, UWidget* W, float dY)
	{
		if (!W) { return false; }
		UCanvasPanelSlot* S = SlotOf(W);
		if (!S) { return false; }
		const TWeakObjectPtr<UWidget> Key(W);
		if (!Base.Contains(Key)) { Base.Add(Key, S->GetOffsets()); }
		const FMargin B = Base[Key];
		// ⚠ Меняем ТОЛЬКО Y — X может быть уже сдвинут уплотнением полосы (см. `ShiftSlotXFromBase`).
		FMargin O = S->GetOffsets();
		// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`AsdaUiSlotDrift.h`): внешний перенос по Y (перетаскивание/постановка окна) сохраняем.
		//   Учёт по оси свой: X этого узла ставит `ShiftSlotXFromBase`, его «прошлое» здесь не трогаем.
		const FVector2D* Last = GSlotDriftLast.Find(W);
		const float PrevTop = (Last && Last->Y != TNumericLimits<double>::Lowest()) ? (float)Last->Y : B.Top;
		const float Drift = BaseShiftKeepsDrag() ? (O.Top - PrevTop) : 0.f;
		O.Top = B.Top - dY + Drift;
		S->SetOffsets(O);
		FVector2D& Rec = SlotDriftRecord(W, FVector2D(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest()));
		Rec.Y = B.Top - dY;
		return true;
	}

	bool BaseOffsetsOf(TMap<TWeakObjectPtr<UWidget>, FMargin>& Base, UWidget* W, FMargin& Out)
	{
		if (!W) { return false; }
		UCanvasPanelSlot* S = SlotOf(W);
		if (!S) { return false; }
		const TWeakObjectPtr<UWidget> Key(W);
		if (!Base.Contains(Key)) { Base.Add(Key, S->GetOffsets()); }
		Out = Base[Key];
		return true;
	}

	// Горизонтальный близнец `ShiftSlotYFromBase` — уплотнение полосы вкладок без дыр.
	// ⚠⚠ То же ограничение, что у `ShiftSlotYFromBase` (выше): узел между вызовами напрямую не ставить — сдвиг сложится дважды.
	bool ShiftSlotXFromBase(TMap<TWeakObjectPtr<UWidget>, FMargin>& Base, UWidget* W, float dX)
	{
		FMargin B;
		if (!BaseOffsetsOf(Base, W, B)) { return false; }
		UCanvasPanelSlot* S = SlotOf(W);
		if (!S) { return false; }
		// ⚠ БЕРЁМ ТЕКУЩИЙ офсет и меняем ТОЛЬКО X: иначе горизонтальный сдвиг затирал бы
		//   вертикальный (найдено 2026-08-28 — уплотнение полосы категорий откатывалось назад,
		//   потому что следом шёл `ShiftSlotYFromBase` и восстанавливал Left из базы).
		FMargin O = S->GetOffsets();
		// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`AsdaUiSlotDrift.h`): внешний перенос по X сохраняем (учёт по оси — как у Y-близнеца).
		const FVector2D* Last = GSlotDriftLast.Find(W);
		const float PrevLeft = (Last && Last->X != TNumericLimits<double>::Lowest()) ? (float)Last->X : B.Left;
		const float Drift = BaseShiftKeepsDrag() ? (O.Left - PrevLeft) : 0.f;
		O.Left = B.Left - dX + Drift;
		S->SetOffsets(O);
		FVector2D& Rec = SlotDriftRecord(W, FVector2D(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest()));
		Rec.X = B.Left - dX;
		return true;
	}

	UCanvasPanelSlot* SlotOf(const UWidget* W)
	{
		return W ? Cast<UCanvasPanelSlot>(W->Slot) : nullptr;
	}

	// Строка ассета той же геометрии, что `Ref` (разбор — у объявления в `.h`).
	UTextBlock* FindOwnLabelLike(const UWidget* Ref, float Tol)
	{
		const UCanvasPanelSlot* RefSlot = SlotOf(Ref);
		const UPanelWidget* Parent = Ref ? Ref->GetParent() : nullptr;
		if (!RefSlot || !Parent) { return nullptr; }
		for (int32 i = 0; i < Parent->GetChildrenCount(); ++i)
		{
			UTextBlock* Own = Cast<UTextBlock>(Parent->GetChildAt(i));   // кандидат — текстовый сосед хит-зоны
			const UCanvasPanelSlot* S = SlotOf(Own);                      // его слот в том же канвасе
			if (S && S->GetPosition().Equals(RefSlot->GetPosition(), Tol) && S->GetSize().Equals(RefSlot->GetSize(), Tol))
			{
				return Own;
			}
		}
		return nullptr;
	}

	// ── ★★★ ЭТАЛОННАЯ РАМКА ТЕКСТА (24.09.2026) ──────────────────────────────────────────────────
	//   Разбор, доказательства из `ui_core.c`/`font_text.c` и причина появления — у объявления
	//   в `AsdaUiHelpers.h`. Здесь только тело.
	// ── ★★★ МАТЕРИАЛ СЦЕНЫ В КИСТЬ: разбор и список пострадавших — у объявления в `.h` ──────────
	UMaterialInstanceDynamic* ApplySceneMaterialBrush(UImage* Img, UMaterialInterface* Mat,
		const FVector2D& ImageSize, const TCHAR* WhoForLog)
	{
		if (!Img || !Mat) { return nullptr; }
		// Печатаем ПРЕЖНИЙ регион: это единственное место, где его вообще видно (поле закрыто
		//   для Blueprint и Python). Именно по этой строке грабля и была опознана в двух окнах.
		const FSlateBrush& Was = Img->GetBrush();
		const FBox2f WasUv = Was.GetUVRegion();
		UE_LOG(LogAsdaUiHelpers, Log,
			TEXT("[кисть сцены] %s: было uv валиден=%d [%.4f %.4f]-[%.4f %.4f] draw_as=%d ⇒ собираем кисть заново"),
			WhoForLog, WasUv.bIsValid ? 1 : 0, WasUv.Min.X, WasUv.Min.Y, WasUv.Max.X, WasUv.Max.Y,
			(int32)Was.DrawAs);

		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Image;              // квад целиком, как в оригинале
		Brush.TintColor = FSlateColor(FLinearColor::White);     // Diffuse скина `NoImage_icon`
		Brush.SetResourceObject(Mat);
		// ⚠ Размер кисти ставится БЕЗУСЛОВНО (поправка ревизии, МЕЛКОЕ): прежнее тело гачи делало
		//   именно так, а «только если > 0» оставляло бы виджету старое значение из ассета.
		Brush.ImageSize = FVector2f((float)ImageSize.X, (float)ImageSize.Y);
		// ★★★ РАДИ ЭТОЙ СТРОКИ ВСЁ И ЗАТЕВАЛОСЬ: регион не задан ⇒ Slate берёт ВЕСЬ ресурс.
		Brush.SetUVRegion(FBox2f(ForceInit));
		Img->SetBrush(Brush);
		// Кадр кликов не ловит — как и все планы импортёра.
		Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		// ★ Экземпляр берём ЗДЕСЬ, после пересборки — порядок вшит в общую точку (СРЕДН-8).
		return Img->GetDynamicMaterial();
	}

	int32 ApplyJuiTextBoundsToTree(UUserWidget* Win, bool bSkinClipsText)
	{
		// Разбор, значения `TextOutLine` и почему признак приходит извне — у объявления в `.h`.
		if (!Win || !Win->WidgetTree) { return 0; }
		if (!bSkinClipsText) { return 0; }   // у скина ножниц нет — эталон тоже не режет
		int32 N = 0;
		Win->WidgetTree->ForEachWidget([&N](UWidget* W)
		{
			if (UTextBlock* T = Cast<UTextBlock>(W)) { N += ApplyJuiTextBounds(T) ? 1 : 0; }
		});
		return N;
	}

	bool ApplyJuiTextBounds(UTextBlock* T)
	{
		if (!T) { return false; }
		FVector2D Pos, Size;
		if (!SlotRect(T, Pos, Size)) { return false; }   // общая точка B1, второго чтения слота нет
		if (Size.X <= 1.f) { return false; }             // авторазмер: эталонной ширины взять неоткуда
		T->SetAutoWrapText(false);                       // закон `ui-autowrap-canvas-gotcha`

		// *** ПЕРЕНОС ПО ШИРИНЕ СЛОТА У ВСЕХ УЗЛОВ — как у эталона (★ временная ветка СНЯТА 2026-09-24).
		//
		// ЧТО ЗДЕСЬ СТОЯЛО И ПОЧЕМУ СНЯТО. С правки КРИТ-1 у контрола высотой В ОДНУ СТРОКУ перенос
		//   ВЫКЛЮЧАЛСЯ (оставалась только обрезка): при переносе четыре подписи-метки, читавшиеся
		//   целиком, теряли по слову («Уровень роста» → «Уровень»; «Материал 1» и «Материал 2» →
		//   одинаковое «Материал»). Замер живого окна (`asda.UI.DumpTextMetrics`) давал у них
		//   82/80, 72/70, 62/60, 62/60 — превышение слота на 2 px. Причиной было НЕ правило переноса,
		//   а ШРИФТ: весь JUI рисовался начертанием `Bold`, которое на 7 % шире эталона. В самой
		//   ветке и было записано: «отступление временное, снять, когда шрифт сойдётся по ширине».
		//
		// ШРИФТ СОШЁЛСЯ (24.09.2026). Подписи переведены на `Regular` — тот самый вес 400, которым
		//   оригинал печёт свой растровый атлас `JUIData/font/FONT0.BIN` (`CreateFontA(..., cHeight 14,
		//   вес `boldFlag ? 700 : 400`, "Arial")`, `fontBuildGlyphAtlasForLang` font_text.c:6515-6600;
		//   `boldFlag` = 0, ui_core.c:106690; побайтовая сверка с живым файлом: 222 глифа из 223).
		//   Те же четыре метки на `Regular`: 75/80, ~66/70, 59/60, 59/60 — ВЛЕЗАЮТ ЦЕЛИКОМ, ради чего
		//   ветка и держалась. По корпусу 1124 однострочных подписей превышение слота осталось у 27
		//   (было 61); у этих 27 строка не влезала и в ОРИГИНАЛЕ — они внесены в реестр отложенного
		//   `Docs/Port/PORT_PENDING_ON_SYSTEMS.md` как дефекты разметки автора.
		//
		// ЧТО ТЕПЕРЬ. Правило ровно эталонное: перенос по ширине прямоугольника плюс обрезка.
		//   `TextFont_DrawText` прекращает вывод, как только новая строка уходит ниже `rect.bottom`
		//   (`font_text.c:8225`) ⇒ у контрола в одну строку вторая строка просто не показывается —
		//   ровно то, что видел игрок оригинала. Ширина переноса = ширина слота плюс обводка с двух
		//   сторон (обводка рисуется ЗА пределами продвижения пера и своей ширины не добавляет).
		const int32 OutlinePx = FMath::Max(0, T->GetFont().OutlineSettings.OutlineSize);
		T->SetWrapTextAt((float)Size.X + 2.f * (float)OutlinePx);
		T->SetClipping(EWidgetClipping::ClipToBounds);   // `if (rect.bottom < curY) break;` (`font_text.c:8225`)
		return true;
	}

	// ── B1: прямоугольник слота (POINT-анкор импортёра: R/B = РАЗМЕР) ────────────────────────────
	bool SlotRect(const UWidget* W, FVector2D& OutPos, FVector2D& OutSize)
	{
		const UCanvasPanelSlot* S = SlotOf(W);
		if (!S) { return false; }
		const FMargin O = S->GetOffsets();
		OutPos  = FVector2D(O.Left, O.Top);
		OutSize = FVector2D(O.Right, O.Bottom);
		return true;
	}

	// ── B1: сдвиг без изменения размера ──────────────────────────────────────────────────────────
	void SetSlotPos(UWidget* W, const FVector2D& Pos)
	{
		UCanvasPanelSlot* S = SlotOf(W);
		if (!S) { return; }
		FMargin O = S->GetOffsets();
		O.Left = Pos.X; O.Top = Pos.Y;
		S->SetOffsets(O);
	}

	// ── B1: позиция + размер разом ───────────────────────────────────────────────────────────────
	void SetSlotRect(UWidget* W, const FVector2D& Pos, const FVector2D& Size)
	{
		UCanvasPanelSlot* S = SlotOf(W);
		if (!S) { return; }
		S->SetOffsets(FMargin(Pos.X, Pos.Y, Size.X, Size.Y));
	}

	// ── ★★★ РАСТЯЖКА РАМКИ 9-slice ПО ВЫСОТЕ (см. подробное «зачем» в заголовке) ─────────────────
	//   Ряды 9-slice: 0,1,2 — верх; 3,4,5 — середина (тянется); 6,7,8 — низ (едет).
	//   Высоты верхнего и нижнего рядов — из скина, их не трогаем: это углы и бордюры, растяни их
	//   и рамка «поплывёт». Горизонталь не трогаем вовсе — по ширине рамка уже верна.
	static TAutoConsoleVariable<int32> CVarFrameKeepOverlap(
		TEXT("asda.UI.FrameKeepOverlap"), 1,
		TEXT("1 (по умолчанию) — растяжки рамок кодом сохраняют перекрытие кусков из ассета (нет просвета 1 px на дробной позиции); ")
		TEXT("0 — прежняя перекладка встык."),
		ECVF_Default);

	bool FrameKeepOverlapOn() { return CVarFrameKeepOverlap.GetValueOnAnyThread() != 0; }

	void Relayout9KeepOverlap(UCanvasPanelSlot* const S[9], const float Xs[3], const float Ws[3], const float Ys[3], const float Hs[3])
	{
		if (!S[0] || !S[2] || !S[6]) { return; }
		const bool bKeep = FrameKeepOverlapOn();
		// Текущая сетка встык — по углам (у углов перекрытия нет: швы заводят под соседей только середину).
		const FMargin C0 = S[0]->GetOffsets(), C2 = S[2]->GetOffsets(), C6 = S[6]->GetOffsets();
		const float CurL[3] = { C0.Left, C0.Left + C0.Right, C2.Left };
		const float CurR[3] = { C0.Left + C0.Right, C2.Left, C2.Left + C2.Right };
		const float CurT[3] = { C0.Top, C0.Top + C0.Bottom, C6.Top };
		const float CurB[3] = { C0.Top + C0.Bottom, C6.Top, C6.Top + C6.Bottom };
		for (int32 k = 0; k < 9; ++k)
		{
			if (!S[k]) { continue; }
			const int32 r = k / 3, c = k % 3;
			const FMargin O = S[k]->GetOffsets();
			const float L = bKeep ? FMath::Max(0.f, CurL[c] - O.Left) : 0.f;
			const float R = bKeep ? FMath::Max(0.f, (O.Left + O.Right) - CurR[c]) : 0.f;
			const float T = bKeep ? FMath::Max(0.f, CurT[r] - O.Top) : 0.f;
			const float B = bKeep ? FMath::Max(0.f, (O.Top + O.Bottom) - CurB[r]) : 0.f;
			S[k]->SetOffsets(FMargin(Xs[c] - L, Ys[r] - T, Ws[c] + L + R, Hs[r] + T + B));
		}
	}

	bool StretchFramePlanesHeight(const TArray<TObjectPtr<UWidget>>& Planes, float NewHeight)
	{
		if (Planes.Num() != 9) { return false; }
		UCanvasPanelSlot* Sl[9] = { nullptr };
		for (int32 i = 0; i < 9; ++i)
		{
			Sl[i] = SlotOf(Planes[i].Get());
			if (!Sl[i]) { return false; }
		}
		const FMargin Top = Sl[0]->GetOffsets();          // верх-лево: даёт и Y верха, и высоту ряда
		const FMargin Bot = Sl[6]->GetOffsets();          // низ-лево: даёт высоту нижнего ряда
		const float MidH = FMath::Max(1.f, NewHeight - Top.Bottom - Bot.Bottom);
		const float MidY = Top.Top + Top.Bottom;
		const float BotY = MidY + MidH;
		const bool bKeep = FrameKeepOverlapOn();
		const float OldBotY = Bot.Top;   // прежний верх нижнего ряда (до перекладки)
		for (int32 i = 3; i <= 5; ++i)
		{
			FMargin O = Sl[i]->GetOffsets();
			// ★ 09.11.2026: насколько кусок среднего ряда заходил вверх под верхний ряд и вниз под нижний — то же после растяжки.
			const float Up   = bKeep ? FMath::Max(0.f, MidY - O.Top) : 0.f;
			const float Down = bKeep ? FMath::Max(0.f, (O.Top + O.Bottom) - OldBotY) : 0.f;
			O.Top = MidY - Up; O.Bottom = MidH + Up + Down;
			Sl[i]->SetOffsets(O);
		}
		for (int32 i = 6; i <= 8; ++i)
		{
			FMargin O = Sl[i]->GetOffsets();
			O.Top = bKeep ? BotY + (O.Top - OldBotY) : BotY;
			Sl[i]->SetOffsets(O);
		}
		return true;
	}

	// ── Д6 ───────────────────────────────────────────────────────────────────────────────────────
	// Четыре свойства и ровно в этом порядке: анкоры задают систему отсчёта для позиции и размера,
	//   поэтому ставятся первыми (иначе `SetPosition` посчитается в старых анкорах).
	void CopyCanvasGeometry(const UCanvasPanelSlot* From, UCanvasPanelSlot* To)
	{
		if (!From || !To) { return; }
		To->SetAnchors(From->GetAnchors());
		To->SetAlignment(From->GetAlignment());
		To->SetPosition(From->GetPosition());
		To->SetSize(From->GetSize());
	}

	// ── ★★★ КЛОН ПОДДЕРЕВА ВИДЖЕТОВ (см. подробное «зачем» в заголовке) ──────────────────────────
	//   Копия делается ПОИМЁННЫМ переносом свойств, а НЕ `NewObject(..., Template=Src)`. Причина
	//   конкретная: у `UPanelWidget` массив `Slots` помечен `Instanced`, и копия-по-шаблону либо
	//   продублировала бы слоты ЧУЖИХ детей, либо утащила бы указатели на них в клон — в обоих
	//   случаях дерево-источник портится. Поимённый перенос со списком исключений таких сюрпризов
	//   не даёт и заодно не тащит делегаты (клон не должен звать обработчики оригинала).
	static void CopyUObjectProps(const UObject* From, UObject* To, std::initializer_list<const TCHAR*> Skip)
	{
		if (!From || !To || From->GetClass() != To->GetClass()) { return; }
		for (TFieldIterator<FProperty> It(From->GetClass()); It; ++It)
		{
			FProperty* P = *It;
			// Делегаты пропускаем: подписка принадлежит ОРИГИНАЛУ (см. шапку).
			if (P->IsA<FMulticastDelegateProperty>() || P->IsA<FDelegateProperty>()) { continue; }
			const FString N = P->GetName();
			bool bSkip = false;
			for (const TCHAR* S : Skip) { if (N == S) { bSkip = true; break; } }
			if (bSkip) { continue; }
			P->CopyCompleteValue_InContainer(To, From);
		}
	}

	UWidget* CloneWidgetSubtree(UUserWidget* Host, UWidget* Src, UPanelWidget* DstParent,
		TFunctionRef<FString(const UWidget*)> NameFn)
	{
		if (!Src || !DstParent) { return nullptr; }
		// ★★★ ДЕРЕВО КЛОНА — ЭТО ДЕРЕВО ПРИЁМНИКА, А НЕ ИСТОЧНИКА (поправка 25.09.2026).
		//   ЧТО БЫЛО: `Tree` брался у ИСТОЧНИКА. Пока клонируют ВНУТРИ одного окна (единственный
		//   случай до сегодня — трекер заданий 1800), источник и приёмник лежат в одном дереве,
		//   и разницы нет. Но при клоне МЕЖДУ ОКНАМИ (крестик 43002 окна 43000 → окно 43300)
		//   узел создавался бы с `Outer` = дерево ЧУЖОГО окна, а в иерархию вставлялся в СВОЁ:
		//   ссылка живёт, пока жив донор, и при пересоздании донора клон уносит с собой в GC
		//   половину чужого окна. Это ровно тот класс «висящего владения», который в порту уже
		//   ловили на `ImgPreview3d` (`AsdaPetCageSubsystem::ResetCageWidget`).
		//   ЧТО СТАЛО: сначала дерево ПРИЁМНИКА (`DstParent`), и только если его нет — прежние
		//   запасные варианты. Для клонов внутри одного окна поведение НЕ МЕНЯЕТСЯ (оба дерева
		//   совпадают), поэтому существующие вызывающие не затронуты.
		UWidgetTree* Tree = DstParent->GetTypedOuter<UWidgetTree>();
		if (!Tree) { Tree = Src->GetTypedOuter<UWidgetTree>(); }
		if (!Tree && Host) { Tree = Host->WidgetTree; }
		if (!Tree) { return nullptr; }

		// Имя: просят у вызывающего, занятое — делаем производным уникальным (дерево не ломаем).
		FName Want(*NameFn(Src));
		if (Want.IsNone() || StaticFindObjectFast(nullptr, Tree, Want) != nullptr)
		{
			Want = MakeUniqueObjectName(Tree, Src->GetClass(), Want.IsNone() ? Src->GetFName() : Want);
		}
		UWidget* Dst = NewObject<UWidget>(Tree, Src->GetClass(), Want, RF_Transactional);
		if (!Dst) { return nullptr; }
		// `Slot`/`Slots`/`Parent` — структура дерева, её задаёт AddChild ниже, а не копия свойств.
		CopyUObjectProps(Src, Dst, { TEXT("Slot"), TEXT("Slots"), TEXT("Parent") });

		UPanelSlot* NewSlot = DstParent->AddChild(Dst);
		if (NewSlot && Src->Slot && NewSlot->GetClass() == Src->Slot->GetClass())
		{
			// `Content` = сам виджет слота, `Parent` = панель: их ставит AddChild, копировать нельзя.
			CopyUObjectProps(Src->Slot, NewSlot, { TEXT("Content"), TEXT("Parent") });
			NewSlot->SynchronizeProperties();
		}
		Dst->SynchronizeProperties();

		if (UPanelWidget* SrcPanel = Cast<UPanelWidget>(Src))
		{
			UPanelWidget* DstPanel = Cast<UPanelWidget>(Dst);
			for (int32 i = 0; DstPanel && i < SrcPanel->GetChildrenCount(); ++i)
			{
				CloneWidgetSubtree(Host, SrcPanel->GetChildAt(i), DstPanel, NameFn);
			}
		}
		return Dst;
	}

	// ── ★★★ ВЕРТИКАЛЬ ТЕКСТА ПО МАСКЕ .JUI ───────────────────────────────────────────────────────
	//   Подробности и вывод формулы — в заголовке. Здесь только две вещи:
	//     1) `JuiBakedTextVOffset` — ТОЧНАЯ копия того, что кладёт импортёр
	//        (`Tools/asda_jui_generic_umg.py`: `vy = (h-14)/2 | (h-14) | 0`, затем `max(vy, 0)`);
	//        ★ КОНСТАНТА 14 ЖИВЁТ В ДВУХ МЕСТАХ и обязана совпадать — в питон-импортёре и здесь.
	//        Замер живого кадра (`asda.UI.DumpTextMetrics`, отчёт `ministate_name_align.md`):
	//        реальная высота строки игрового шрифта на размере 8 = 13-14 px, то есть допущение
	//        импортёра верное — снимать его надо не «потому что оно врёт», а чтобы не сложить
	//        один и тот же сдвиг дважды.
	//     2) `ApplyJuiTextAlign` — формула эталона поверх СЫРОГО верха контрола.
	static constexpr float kJuiGenAssumedLineH = 14.f;   // допущение импортёра о высоте строки

	float JuiBakedTextVOffset(int32 AlignMask, float JuiH)
	{
		const float Vy = (AlignMask & 0x4) ? (JuiH - kJuiGenAssumedLineH) * 0.5f
		               : (AlignMask & 0x8) ? (JuiH - kJuiGenAssumedLineH)
		                                   : 0.f;
		return FMath::Max(Vy, 0.f);
	}

	void ApplyJuiTextAlign(UTextBlock* Text, int32 AlignMask, float JuiH, float TextPosY)
	{
		if (!Text) { return; }
		// Горизонталь — Justification самого TextBlock: его слот занимает всю ширину контрола.
		Text->SetJustification((AlignMask & 0x2) ? ETextJustify::Right
		                     : (AlignMask & 0x1) ? ETextJustify::Center
		                                         : ETextJustify::Left);
		UCanvasPanelSlot* S = SlotOf(Text);
		if (!S) { return; }
		FMargin O = S->GetOffsets();
		const float SlotH = O.Bottom;                        // высота, которую поставил генератор
		const float LineH = MeasureLineHeight(Text->GetFont(), SlotH);
		// Верх строки БЕЗ запечённой генератором центровки = `ВерхКонтроля + TextPos.y` (эталон
		//   к этому месту уже прибавил TextPos.y и НЕ трогал низ прямоугольника).
		const float RawTop  = O.Top - JuiBakedTextVOffset(AlignMask, JuiH);
		const float CtrlTop = RawTop - TextPosY;             // собственно верх контроля
		const float RectBot = CtrlTop + JuiH;                // низ прямоугольника (TextPos его НЕ двигает)
		float NewTop = RawTop;
		if (AlignMask & 0x8)      { NewTop = RectBot - LineH; }                       // к низу
		else if (AlignMask & 0x4) { NewTop = (RawTop + RectBot) * 0.5f - LineH * 0.5f; }   // по центру
		O.Top    = NewTop;
		// ★ 07.10.2026 (отчёт `Reports/owner_1007/label_band_and_8500.md`): низ узла — по низу прямоугольника
		//   контрола, не ниже (как `asda_jui_parse.label_slot` импортёра). БЫЛО `Max(LineH, JuiH)`: узел, сдвинутый
		//   центровкой вниз, вылезал за контрол на (JuiH − LineH)/2 + TextPos.y, и хит-тест окна (`HitTestWidgetTree`)
		//   считал эту полосу окном. Верх строки не меняется — текст на экране стоит там же.
		O.Bottom = FMath::Max(LineH, RectBot - NewTop);
		S->SetOffsets(O);
	}

	// ── D6 ───────────────────────────────────────────────────────────────────────────────────────
	FVector2D CanvasRootPos(const UWidget* W)
	{
		FVector2D P = FVector2D::ZeroVector;
		const UWidget* Cur = W;
		while (Cur)
		{
			const UCanvasPanelSlot* CS = SlotOf(Cur);   // ★ B1: через общую точку
			if (!CS) { break; }
			const FMargin O = CS->GetOffsets();
			P.X += O.Left; P.Y += O.Top;
			Cur = Cur->GetParent();
		}
		return P;
	}

	// ── ★★★ ПОЗИЦИЯ + АНКОРЫ ДЛЯ НАКЛАДКИ В КОРНЕ (см. подробное «зачем» в заголовке) ────────────
	bool CanvasRootPlacement(const UWidget* W, FAnchors& OutAnchors, FVector2D& OutPos)
	{
		FVector2D P = FVector2D::ZeroVector;
		FAnchors A(0.f, 0.f, 0.f, 0.f);
		const UWidget* Cur = W;
		bool bAny = false;
		while (Cur)
		{
			const UCanvasPanelSlot* CS = SlotOf(Cur);
			if (!CS) { break; }
			const FMargin O = CS->GetOffsets();
			const FVector2D Al = CS->GetAlignment();
			// Конвенция POINT-анкора импортёра: Right/Bottom — это РАЗМЕР, а не врезки.
			P.X += O.Left - Al.X * O.Right;
			P.Y += O.Top  - Al.Y * O.Bottom;
			A = CS->GetAnchors();      // анкоры САМОГО ВЕРХНЕГО пройденного слота
			bAny = true;
			Cur = Cur->GetParent();
		}
		OutAnchors = A;
		OutPos = P;
		return bAny;
	}
	// ── D7 ───────────────────────────────────────────────────────────────────────────────────────
	FString FormatThousands(int32 Value)
	{
		const bool bNeg = (Value < 0);
		const FString Digits = FString::FromInt(bNeg ? -Value : Value);
		FString Out;
		const int32 N = Digits.Len();
		for (int32 i = 0; i < N; ++i)
		{
			Out.AppendChar(Digits[i]);
			const int32 Left = N - 1 - i;
			if (Left > 0 && (Left % 3) == 0) { Out.AppendChar(TEXT(',')); }
		}
		return bNeg ? (TEXT("-") + Out) : Out;
	}

	// ── D8 ───────────────────────────────────────────────────────────────────────────────────────
	FString FormatPrintfArgs(const FString& Fmt, const TArray<FString>& Args)
	{
		FString Out; Out.Reserve(Fmt.Len() + 32);
		int32 ArgI = 0;
		for (int32 i = 0; i < Fmt.Len(); ++i)
		{
			if (Fmt[i] == TEXT('%') && i + 1 < Fmt.Len())
			{
				const TCHAR C = Fmt[i + 1];
				if (C == TEXT('%')) { Out.AppendChar(TEXT('%')); ++i; continue; }   // C-sprintf: «%%» → «%»
				if (C == TEXT('s') || C == TEXT('d'))
				{
					Out += Args.IsValidIndex(ArgI) ? Args[ArgI] : FString();
					++ArgI; ++i; continue;
				}
			}
			Out.AppendChar(Fmt[i]);
		}
		return Out;
	}

	// ── D10 ──────────────────────────────────────────────────────────────────────────────────────
	FString LocRaw(const UGameInstance* GI, int32 Id, bool bDebugFallback)
	{
		if (UAsdaTextSubsystem* T = GI ? const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
		{
			// ★ Через общую точку подстановки (порт `UI_FormatLocalizedNotice`): у статических
			//   подписей аргументов нет, но маркеры в строке всё равно обязаны исчезнуть, а не
			//   попасть на экран (правка 2026-08-28, жалоба владельца про «[]»/«[sd1]»).
			const FString S = T->FormatNotice(Id);
			if (!S.IsEmpty()) { return S; }
		}
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N9): было «[loc N]» — второй формат заглушки рядом с «[#id]»
		//   мультиязычности (09.10); стало единая `FAsdaLang::MissingTag` → «[#N]». Эталона у заглушки нет (у оригинала
		//   строка есть всегда) — это служебная метка порта; поведение «пусто без bDebugFallback» не тронуто.
		return bDebugFallback ? FAsdaLang::MissingTag(Id) : FString();
	}
}

// ══════════════════════════════════════════════════════════════════════════════════════════════════
// ★ Аудит №3 (B3): опрос мыши за кадр (шапка — в AsdaUiHelpers.h)
// ══════════════════════════════════════════════════════════════════════════════════════════════════

void FAsdaMouseFrame::Update()
{
	// Slate не поднят — считаем, что кнопки отпущены, курсор оставляем прежним. Все прежние копии
	//   до опроса выходили по `FSlateApplication::IsInitialized()`, кроме `TickBuffPointer`
	//   мини-статуса: там `bLmbNow` собирался как `IsInitialized() && Contains(...)`, то есть при
	//   неподнятом Slate давал ровно «отпущено» и латч тоже обнулялся. Повторяем это.
	const bool bReady = FSlateApplication::IsInitialized();
	const bool bLmbNow = bReady && FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	const bool bRmbNow = bReady && FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::RightMouseButton);
	// ★ СРЕДНЯЯ КНОПКА (24.09.2026). Заведена не «про запас»: эталонное событие 0xE
	//   (`CControl_OnRButtonDblClick@0x12DC410`, `ui_core.c:107524-107527`) ТРЕБУЕТ, чтобы средняя
	//   была ОТПУЩЕНА — `CObject_GetStateByteAt(pManager, 2) == 1` отменяет событие. Без этого
	//   поля условие эталона было невыразимо, и окно 43000 его молча не проверяло.
	const bool bMmbNow = bReady && FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::MiddleMouseButton);
	if (bReady) { Cursor = FSlateApplication::Get().GetCursorPos(); }

	// Фронты — по латчу ПРЕДЫДУЩЕГО кадра, ровно как раньше делали `bDown && !bPrev` / `!bDown && bPrev`.
	bLmbDown = bLmbNow && !bPrevLmb;
	bLmbUp   = !bLmbNow && bPrevLmb;
	bRmbDown = bRmbNow && !bPrevRmb;
	bRmbUp   = !bRmbNow && bPrevRmb;

	bLmb = bLmbNow;
	bRmb = bRmbNow;
	bMmb = bMmbNow;   // фронты средней не нужны никому: эталон читает только «зажата ли сейчас»
	bPrevLmb = bLmbNow;
	bPrevRmb = bRmbNow;
	// ★ 21.10.2026 (Alt+Z) [ИСПРАВЛЕНИЕ]: при скрытом интерфейсе окна не видят кнопок мыши — клик идёт в мир, а не в невидимое
	//   окно. Латч всё равно ведётся по живым кнопкам: показ интерфейса посреди удержания не даст ложного фронта.
	if (AsdaUi::GUiInputHidden && !bIgnoreUiHidden)
	{
		bLmb = bRmb = bMmb = false;
		bLmbDown = bLmbUp = bRmbDown = bRmbUp = false;
	}
}

void FAsdaMouseFrame::Reset()
{
	// Латч в «кнопка была отпущена»: удержанная кнопка после возврата в разбор даст ЧЕСТНЫЙ фронт.
	bPrevLmb = false;
	bPrevRmb = false;
	bLmb = bLmbDown = bLmbUp = false;
	bRmb = bRmbDown = bRmbUp = false;
	// Курсор НЕ трогаем: он не состояние латча, а последняя известная точка.
}

// ══════════════════════════════════════════════════════════════════════════════════════════════════
// ★ Аудит №3 (B4): «призрак» перетаскиваемой иконки (шапка — в AsdaUiHelpers.h)
// ══════════════════════════════════════════════════════════════════════════════════════════════════

// ── Реестр ЖИВЫХ призраков — единственный источник ответа «предмет сейчас в руке?». ──
//   Обоснование выбора (слабые ссылки вместо счётчика) — в шапке `FAsdaDragGhost::IsAnyActive`.
//   Список короткий: владельцев призрака семь, а одновременно тащат максимум один предмет,
//   поэтому линейный проход дешевле любой хэш-таблицы.
static TArray<TWeakObjectPtr<UImage>> GAsdaActiveDragGhosts;

bool FAsdaDragGhost::IsAnyActive()
{
	bool bAny = false;
	for (int32 i = GAsdaActiveDragGhosts.Num() - 1; i >= 0; --i)
	{
		const UImage* Img = GAsdaActiveDragGhosts[i].Get();
		if (!Img)
		{
			// Окно-владелец умерло вместе с призраком — запись больше ничего не значит.
			GAsdaActiveDragGhosts.RemoveAt(i);
			continue;
		}
		// `Cancel` не удаляет виджет, а прячет его, поэтому видимость проверяем отдельно.
		const ESlateVisibility V = Img->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { continue; }
		// ★ Страховка от «залипания». Призрак живёт В ДЕРЕВЕ окна-владельца, и если окно спрятали,
		//   не дождавшись отпускания ЛКМ (в порту горячие клавиши работают и с зажатой кнопкой),
		//   `Cancel` никто не позовёт — сам `UImage` останется видимым, и признак «предмет в руке»
		//   завис бы навсегда, заперев ESC-меню. Спрятанное окно = перетаскивания нет.
		const UUserWidget* Owner = Img->GetTypedOuter<UUserWidget>();
		if (Owner)
		{
			const ESlateVisibility OV = Owner->GetVisibility();
			if (OV == ESlateVisibility::Collapsed || OV == ESlateVisibility::Hidden) { continue; }
		}
		bAny = true;
	}
	return bAny;
}

bool FAsdaDragGhost::Begin(UUserWidget* Owner, UCanvasPanel* HostCanvas, const FSlateBrush& Brush,
                           const FVector2D& Size, float Opacity, const FVector2D& InCursorOffset,
                           const TCHAR* NodeName, const UWorld* FallbackWorld)
{
	if (!Owner || !Owner->WidgetTree || !HostCanvas) { return false; }
	Host = Owner;
	CursorOffset = InCursorOffset;

	if (!Ghost)
	{
		// Имя уникальное (см. п.3 шапки `Begin`): фиксированное заставило бы движок переименовывать
		//   живой объект, если структуру забыли сбросить при пересоздании окна.
		Ghost = Owner->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
			MakeUniqueObjectName(Owner->WidgetTree, UImage::StaticClass(), FName(NodeName)));
		if (!Ghost) { return false; }
		if (UCanvasPanelSlot* GS = HostCanvas->AddChildToCanvas(Ghost))
		{
			GS->SetAnchors(FAnchors(0.f, 0.f));
			GS->SetAlignment(FVector2D(0.f, 0.f));
			GS->SetAutoSize(false);
			GS->SetZOrder(10000);   // поверх всего содержимого окна
		}
	}

	Ghost->SetBrush(Brush);
	Ghost->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, Opacity));
	if (UCanvasPanelSlot* GS = AsdaUi::SlotOf(Ghost))   // ★ B1: через общую точку
	{
		// ★ Размер ставим на КАЖДЫЙ захват. Хотбар и инвентарь так и делали (размер иконки ячейки
		//   меняется вместе с раскладкой), а книга навыков ставила его ОДИН РАЗ при создании — и
		//   правка `IconDrawSize` в редакторе до неё не доезжала до перезапуска. Это исправление
		//   дефекта, а не смена поведения: значение то же самое, просто применяется каждый раз.
		GS->SetSize(Size);
	}
	// `SelfHitTestInvisible` — у `UImage` детей нет, поэтому это ровно то же, что `HitTestInvisible`
	//   книги навыков: призрак не отбирает мышь ни у окна, ни у мира (закон `ui-window-drag-hittest`).
	Ghost->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	bActive = true;
	// Учёт в общем реестре — по нему отвечает `IsAnyActive` (порт записи `CUIManager+0x04 = 1`,
	//   эталон `set_o1_5C90@0xC55C90` на JUI-событии 6 «нажатие ЛКМ на ячейке»).
	GAsdaActiveDragGhosts.AddUnique(Ghost);

	// Первый кадр — сразу под курсор, иначе призрак мигнул бы в левом-верхнем углу окна.
	//   (Все три прежних тела заканчивались тем же вызовом `UpdateDragGhost()`.)
	Tick(FallbackWorld);
	return true;
}

void FAsdaDragGhost::Tick(const UWorld* FallbackWorld)
{
	if (!Ghost || !bActive || !Host || !FSlateApplication::IsInitialized()) { return; }
	const FVector2D Cur = FSlateApplication::Get().GetCursorPos();
	// D2: единый перевод «экран → канвас окна» (учитывает DPI и положение окна игры на десктопе).
	const FVector2D Local = AsdaUi::ScreenToCanvasLocal(Host, Cur, FallbackWorld);
	if (UCanvasPanelSlot* GS = AsdaUi::SlotOf(Ghost))   // ★ B1: через общую точку
	{
		GS->SetPosition(Local + CursorOffset);
	}
}

void FAsdaDragGhost::Cancel()
{
	if (Ghost) { Ghost->SetVisibility(ESlateVisibility::Collapsed); }
	bActive = false;
	// Снятие с учёта (порт записи `CUIManager+0x04 = 0`, эталон JUI-событие 7 «отпускание ЛКМ»).
	GAsdaActiveDragGhosts.Remove(Ghost);
}

// ══════════════════════════════════════════════════════════════════════════════════════════════════
//  ЛОГИЧЕСКИЙ размер атласа — см. объявление в заголовке.
//  Таблица `DT_AtlasSourceSize` перечисляет ТОЛЬКО те атласы, чей файл увеличивали: у остальных
//  логический размер и есть фактический, и строки для них не нужны.
// ══════════════════════════════════════════════════════════════════════════════════════════════════
// ★ ОБЩАЯ ТОЧКА: иконка-ячейка квадратного атласа (см. объявление в .h).
//   Эталон: `IconAtlas_IndexToUV` = `gridIndexToXY_10col@0xEBFF70` (X = CellPx*(i%Cols),
//   Y = CellPx*(i/Cols)) + `CControl_DrawTextureRect(ctrl, tex, X, Y, CellPx, CellPx)`.
bool AsdaUi::ApplyAtlasCellIcon(UImage* Img, const FString& AtlasName, int32 Cell, int32 CellPx, int32 Cols)
{
	if (!Img || AtlasName.IsEmpty()) { return false; }
	UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	UTexture2D* Atlas = TexMgr ? TexMgr->LoadUITexture(FName(*AtlasName)) : nullptr;
	if (!Atlas) { return false; }
	const FIntPoint Imp = LogicalAtlasSize(Atlas);
	const float TexW = (float)FMath::Max(1, Imp.X);
	const float TexH = (float)FMath::Max(1, Imp.Y);
	const float Cs   = (float)FMath::Max(1, CellPx);
	const int32 C    = FMath::Max(1, Cols);
	const float X = Cs * (float)(Cell % C);
	const float Y = Cs * (float)(Cell / C);
	FSlateBrush B;
	B.SetResourceObject(Atlas);
	B.ImageSize = FVector2D(Cs, Cs);
	B.SetUVRegion(FBox2D(FVector2D(X / TexW, Y / TexH), FVector2D((X + Cs) / TexW, (Y + Cs) / TexH)));
	Img->SetBrush(B);
	return true;
}

FIntPoint AsdaUi::LogicalAtlasSize(const UTexture2D* Tex)
{
	if (!Tex)
	{
		return FIntPoint(1, 1);
	}
	// ★ GetImportedSize, а не GetSizeX/Y: рендер-ресурс на первом показе ещё не готов (общая грабля
	//   порта — из-за неё иконки были «кривые до перелистывания»).
	const FIntPoint Actual = Tex->GetImportedSize();
	const int32 Wf = FMath::Max(1, Actual.X);
	const int32 Hf = FMath::Max(1, Actual.Y);

	// ★★★ КЛЮЧ — ПУТЬ ПАКЕТА, А НЕ ИМЯ. Имена текстур в проекте НЕ уникальны: одни и те же имена
	//   носят портреты NPC и текстуры их моделей (113 таких пар), а `TILE0` живёт в десятке карт.
	//   По имени размер брался бы от однофамильца — той же природы ошибка, что и при реимпорте.
	static TMap<FName, FIntPoint> Sizes;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		if (const UDataTable* Dt = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/DT_AtlasSourceSize.DT_AtlasSourceSize")))
		{
			for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
			{
				if (const FAsdaAtlasSizeRow* Row = reinterpret_cast<const FAsdaAtlasSizeRow*>(It.Value))
				{
					if (Row->Width > 0 && Row->Height > 0)
					{
						Sizes.Add(It.Key, FIntPoint(Row->Width, Row->Height));
					}
				}
			}
		}
	}
	if (Sizes.Num() == 0)
	{
		return FIntPoint(Wf, Hf);          // таблицы нет — значит ничего не увеличивали
	}

	// Ключ строки: путь пакета со слэшами, заменёнными подчёркиванием (CSV не любит слэши).
	FString Key = Tex->GetOutermost()->GetName();
	Key.ReplaceInline(TEXT("/"), TEXT("_"));
	if (const FIntPoint* Found = Sizes.Find(FName(*Key)))
	{
		return FIntPoint(FMath::Max(1, Found->X), FMath::Max(1, Found->Y));
	}
	return FIntPoint(Wf, Hf);
}


// ══════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ СОСТОЯНИЯ ПОДПИСИ КНОПКИ (наведение / нажатие) — полное обоснование в заголовке.
// ══════════════════════════════════════════════════════════════════════════════════════════════════
int32 AsdaUi::BindButtonCaptionStates(UUserWidget* Window, TArray<TObjectPtr<UObject>>& OutRelays)
{
	if (!Window || !Window->WidgetTree) { return 0; }
	int32 Bound = 0;
	Window->WidgetTree->ForEachWidget([&](UWidget* W)
	{
		UButton* Btn = Cast<UButton>(W);
		if (!Btn) { return; }
		const FButtonStyle& St = Btn->GetStyle();
		// Импортёр состояния подписи не клал (окно ещё не переимпортировано либо у скина один кадр) —
		//   кнопку не трогаем вовсе: дефолт `FSlateColor::UseForeground()` не «цвет», а «наследуй».
		if (!St.NormalForeground.IsColorSpecified() || !St.HoveredForeground.IsColorSpecified()
			|| !St.PressedForeground.IsColorSpecified())
		{
			return;
		}
		// Подпись кнопки — сосед `<Кнопка>_Text` (закон `jui-importer-widget-naming`).
		UTextBlock* Cap = Cast<UTextBlock>(Window->GetWidgetFromName(FName(*(Btn->GetName() + TEXT("_Text")))));
		if (!Cap) { return; }

		const FLinearColor ColNormal = St.NormalForeground.GetSpecifiedColor();
		const FLinearColor ColHover  = St.HoveredForeground.GetSpecifiedColor();
		const FLinearColor ColPress  = St.PressedForeground.GetSpecifiedColor();
		// Сдвиг подписи при нажатии = `PressedPadding − NormalPadding`: импортёр положил туда
		//   `CaptionPoint` кадров 2 и 0, а смещение покоя уже вписано в позицию слота подписи.
		const FVector2D PressDelta(St.PressedPadding.Left - St.NormalPadding.Left,
		                           St.PressedPadding.Top  - St.NormalPadding.Top);

		// Применённый сдвиг живёт в разделяемой ячейке: и добавление, и снятие идут через разницу,
		//   поэтому повторные события состояний не накапливают смещение.
		TSharedRef<FVector2D> Applied = MakeShared<FVector2D>(FVector2D::ZeroVector);
		TWeakObjectPtr<UTextBlock> WeakCap(Cap);
		TWeakObjectPtr<UButton>    WeakBtn(Btn);
		auto SetFrame = [WeakCap, ColNormal, ColHover, ColPress, PressDelta, Applied](int32 Frame)
		{
			UTextBlock* T = WeakCap.Get();
			if (!T) { return; }
			T->SetColorAndOpacity(FSlateColor(Frame == 2 ? ColPress : (Frame == 1 ? ColHover : ColNormal)));
			if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(T->Slot))
			{
				const FVector2D Want  = (Frame == 2) ? PressDelta : FVector2D::ZeroVector;
				const FVector2D Delta = Want - *Applied;
				if (!Delta.IsNearlyZero())
				{
					const FMargin Off = S->GetOffsets();
					S->SetOffsets(FMargin((float)(Off.Left + Delta.X), (float)(Off.Top + Delta.Y),
					                      Off.Right, Off.Bottom));
					*Applied = Want;
				}
			}
		};

		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(Window);
		R->HoverCallback   = [SetFrame]() { SetFrame(1); };
		R->UnhoverCallback = [SetFrame]() { SetFrame(0); };
		// Нажатие → кадр 2; отпускание → кадр 1, а НЕ 0 (`CPushButton_OnReleased @0x12F7600`
		//   ставит `+1976 = 1`: курсор ещё над кнопкой). Если курсор уже ушёл — сразу кадр 0.
		R->PressCallback   = [SetFrame, WeakBtn](int32, bool bPressed)
		{
			SetFrame(bPressed ? 2 : ((WeakBtn.IsValid() && WeakBtn->IsHovered()) ? 1 : 0));
		};
		Btn->OnHovered.AddDynamic(R,   &UAsdaButtonRelay::OnHovered);
		Btn->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
		Btn->OnPressed.AddDynamic(R,   &UAsdaButtonRelay::OnPressed);
		Btn->OnReleased.AddDynamic(R,  &UAsdaButtonRelay::OnReleased);
		AsdaUi::AnchorRelay(OutRelays, R, Btn);   // ★ 31.10.2026: якорь выбрасывает мёртвые реле (`asda.UI.RelayAnchorPrune`)
		SetFrame(0);   // стартовое состояние — покой (цвет кадра 0 из скина)
		++Bound;
	});
	return Bound;
}


// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ СОСТОЯНИЕ ПЕРЕКЛЮЧАТЕЛЯ-ВКЛАДОК — см. разбор в заголовке.
// ════════════════════════════════════════════════════════════════════════════════════════════════
namespace
{
	// Порог «подпись тёмная» в ЛИНЕЙНОМ пространстве. Подписи вкладок в данных бывают двух семей:
	//   тёмные (`ff4c4c4c` → яркость 0.068, `ff555555` → 0.086) и светлые (`ffb5b5ab` → 0.44,
	//   `ffe2e2c7` → 0.75, `ffaedff4` → 0.68). Порог 0.25 (≈ `ff888888` в sRGB) разводит их с
	//   запасом больше чем втрое в обе стороны, поэтому попасть «между» ни одна строка `TABBARS`
	//   не может. Это НЕ палитра: цвета отсюда не берутся, здесь только выбор СЕМЕЙСТВА — светлый
	//   скин (тёмная подпись) или тёмный (светлая подпись).
	constexpr float kTabIdleDarkLimit = 0.25f;

	// ★ МИНИМАЛЬНЫЙ РАЗРЫВ «активная ↔ неактивная» в ВОСПРИНИМАЕМОЙ (sRGB-кодированной) шкале.
	//   Значение не выдумано: это разрыв уже утверждённой светлой пары — чёрная активная (0.0)
	//   против базового `ff4c4c4c` (0x4C/255 = 0.298). Округлено ВНИЗ до 0.25, чтобы ни одна строка
	//   светлого семейства под порог не попала (`ff4c4c4c` 0.298, `ff555555` 0.333, `ff646464`
	//   0.392) и светлые полосы остались в точности такими же, как до зеркальной правки.
	constexpr float kTabActiveGapMin = 0.25f;

	// Передаточная кривая sRGB нужна потому, что ЛИНЕЙНАЯ яркость плохо описывает ВИДИМЫЙ контраст:
	//   `ff4c4c4c` — это 0.072 линейных, но глазу это 30 % серого. Разрыв меряем в закодированной
	//   шкале. ★ Кривую больше НЕ дублируем: обе стороны берём у экспортированных `LinearToSrgbChannel`
	//   / `SrgbToLinearChannel` (клэмп входа [0..1] делаем на месте — luminance и так в диапазоне).

	// Обе публичные перегрузки различаются только типом массива, поэтому тело — одно, а доступ к
	//   элементу отдаётся вызывающему (`TFunctionRef`): копировать `TObjectPtr`-массив в сырой
	//   ради одного прохода незачем.
	void TabCaptureIdleImpl(int32 Num, const TFunctionRef<UTextBlock*(int32)>& At,
		TArray<FLinearColor>& InOutIdle)
	{
		// ★ СНИМАЕМ РОВНО ОДИН РАЗ (образец — `bCatIdleColorTaken` склада): после первой покраски
		//   «базовым» цветом активной подписи стал бы чёрный, и вкладка навсегда осталась бы чёрной.
		if (InOutIdle.Num() == Num) { return; }
		InOutIdle.Reset(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			const UTextBlock* T = At(i);
			// Пустой слот подписи допустим (окно не переимпортировано) — держим место в массиве,
			//   чтобы индексы подписей и цветов не разъехались.
			InOutIdle.Add(T ? T->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::White);
		}
	}

	void TabApplyStateImpl(int32 Num, const TFunctionRef<UTextBlock*(int32)>& At, int32 ActiveIndex,
		TArray<FLinearColor>& InOutIdle)
	{
		TabCaptureIdleImpl(Num, At, InOutIdle);
		for (int32 i = 0; i < Num; ++i)
		{
			UTextBlock* T = At(i);
			if (!T || !InOutIdle.IsValidIndex(i)) { continue; }
			const FLinearColor Idle = InOutIdle[i];
			T->SetColorAndOpacity(FSlateColor(i == ActiveIndex ? AsdaUi::TabCaptionActiveColor(Idle)
			                                                   : AsdaUi::TabCaptionInactiveColor(Idle)));
		}
	}
}

FLinearColor AsdaUi::TabCaptionActiveColor(const FLinearColor& IdleColor)
{
	// Решение владельца 2026-08-27, ОБЕ половины: активная вкладка контрастнее фона.
	//   Скин вкладки коду не виден, но данные клали подпись ПОД скин, поэтому её базовый цвет и
	//   есть признак семейства: тёмная подпись ⇒ скин светлый ⇒ активная ЧЁРНАЯ; светлая подпись ⇒
	//   скин тёмный ⇒ активная БЕЛАЯ (зеркальный случай, 2026-08-27). Альфу подписи сохраняем.
	return (IdleColor.GetLuminance() <= kTabIdleDarkLimit) ? FLinearColor(0.f, 0.f, 0.f, IdleColor.A)
	                                                       : FLinearColor(1.f, 1.f, 1.f, IdleColor.A);
}

FLinearColor AsdaUi::TabCaptionInactiveColor(const FLinearColor& IdleColor)
{
	// ★ СВЕТЛОЕ СЕМЕЙСТВО (тёмная подпись на светлом скине) НЕ ТРОГАЕМ ВООБЩЕ — возвращаем цвет
	//   ассета байт в байт. Это гарантия «не сломать работающее»: все шесть подключённых окон,
	//   кроме под-полосы 12006, живут именно здесь.
	if (IdleColor.GetLuminance() <= kTabIdleDarkLimit) { return IdleColor; }

	// ── ЗЕРКАЛЬНЫЙ СЛУЧАЙ: активная БЕЛАЯ, значит её воспринимаемая яркость = 1.0 ──
	//   Обычно базовый цвет и так достаточно ниже белого (`ffb5b5ab` → 0.706, разрыв 0.294) и
	//   приглушать нечего — оформление остаётся тем, что положил ассет.
	const float IdleLum = IdleColor.GetLuminance();
	if ((1.f - LinearToSrgbChannel(FMath::Clamp(IdleLum, 0.f, 1.f))) >= kTabActiveGapMin) { return IdleColor; }

	// А вот если базовый сам почти белый (`ffe2e2c7` → 0.879, `ffaedff4` → 0.832, `ffd8d8d0`,
	//   `ffe1cfa9`, и особенно ЧИСТО БЕЛЫЕ рантайм-подписи `MakeLabelLike` военного магазина 67200
	//   и магазина снаряжения 6800 → 1.000), активную от неактивных было бы не отличить. Тогда
	//   приглушаем — но не подставляем свой цвет, а гасим ЯРКОСТЬ базового ровно до порога:
	//   все три канала множим на ОДИН коэффициент, поэтому тон подписи из ассета сохраняется
	//   (голубой `ffaedff4` остаётся голубым, тёплый `ffe1cfa9` — тёплым). Палитры здесь нет.
	const float WantLum = SrgbToLinearChannel(FMath::Clamp(1.f - kTabActiveGapMin, 0.f, 1.f));
	const float K = (IdleLum > KINDA_SMALL_NUMBER) ? (WantLum / IdleLum) : 1.f;
	return FLinearColor(IdleColor.R * K, IdleColor.G * K, IdleColor.B * K, IdleColor.A);
}

void AsdaUi::CaptureTabCaptionIdle(const TArray<TObjectPtr<UTextBlock>>& Captions,
	TArray<FLinearColor>& InOutIdle)
{
	TabCaptureIdleImpl(Captions.Num(), [&Captions](int32 i) { return Captions[i].Get(); }, InOutIdle);
}

void AsdaUi::ApplyTabCaptionState(const TArray<UTextBlock*>& Captions, int32 ActiveIndex,
	TArray<FLinearColor>& InOutIdle)
{
	TabApplyStateImpl(Captions.Num(), [&Captions](int32 i) { return Captions[i]; }, ActiveIndex, InOutIdle);
}

void AsdaUi::ApplyTabCaptionState(const TArray<TObjectPtr<UTextBlock>>& Captions, int32 ActiveIndex,
	TArray<FLinearColor>& InOutIdle)
{
	TabApplyStateImpl(Captions.Num(), [&Captions](int32 i) { return Captions[i].Get(); }, ActiveIndex, InOutIdle);
}

void AsdaUi::ApplyTabCaptionState(const TArray<TObjectPtr<UTextBlock>>& Captions,
	const TArray<TObjectPtr<UButton>>& Buttons, int32 ActiveIndex, TArray<FLinearColor>& InOutIdle)
{
	// Базовые цвета снимаем ТЕМ ЖЕ проходом, что и обычная перегрузка (ровно один раз).
	TabCaptureIdleImpl(Captions.Num(), [&Captions](int32 i) { return Captions[i].Get(); }, InOutIdle);

	// ★★★ КАДРОВЫЕ ЦВЕТА — НА ВСЮ ПОЛОСУ, А НЕ ПОКНОПОЧНО (2026-08-29).
	//   ПРИЧИНА. Шаг `asda_ui_tabbar_captions.py` кладёт цвета кадров скина в `FButtonStyle`
	//   КАЖДОЙ кнопки `{idc}_OPT<i>`. Но у контрола, объявленного в данных ДВАЖДЫ, в дереве
	//   лежат ДВА виджета С ОДНИМ ИМЕНЕМ (проба `Tools/_probe_exch_tabs.py`: таких полос в порте
	//   ДВЕ — `EXCHANGE_WIN / EXCHANGER_RADIOBTN2` (5 двойников) и
	//   `CHARAINFO_WIN / CHARAINFO_STMENU` (2)). Шаг и рантайм выбирают из двойников НЕЗАВИСИМО
	//   (обход стеком против `GetAllWidgets`), поэтому часть кнопок приезжает БЕЗ кадровых
	//   цветов: `bFromSkin == false` ⇒ включается эвристика «по яркости базовой подписи», и на
	//   вкладке с ЧЁРНОЙ плашкой кадра 2 она возвращает ЧЁРНЫЙ — подпись пропадает (жалоба
	//   владельца: у первой вкладки нет надписи).
	//   ЛЕЧЕНИЕ В ОБЩЕЙ ТОЧКЕ: у одного скина `CaptionColor` кадров ОДИНАКОВ для всех ES
	//   (проверено по `JuiSkin.ini` для всех четырёх скинов аукциона), поэтому достаточно найти
	//   ЛЮБУЮ кнопку полосы, которой цвета достались, и применить её пару ко ВСЕЙ полосе.
	FLinearColor BarIdle = FLinearColor::White, BarActive = FLinearColor::White;
	bool bBarFromSkin = false;
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		const UButton* B0 = Buttons[i].Get();
		if (!B0) { continue; }
		const FButtonStyle& St0 = B0->GetStyle();
		const FLinearColor N0 = St0.NormalForeground.GetSpecifiedColor();
		const FLinearColor P0 = St0.PressedForeground.GetSpecifiedColor();
		if (!N0.Equals(P0, 1.f / 512.f)) { BarIdle = N0; BarActive = P0; bBarFromSkin = true; break; }
	}

	for (int32 i = 0; i < Captions.Num(); ++i)
	{
		UTextBlock* T = Captions[i].Get();
		if (!T || !InOutIdle.IsValidIndex(i)) { continue; }

		// ── Есть ли у парной кнопки НАСТОЯЩИЕ кадровые цвета скина? ──
		//   Признак — «покой ≠ нажатие»: шаг подписей кладёт кадр 0 и кадр 2, а у кнопки, которой
		//   шаг не касался, оба поля остаются одинаковым значением по умолчанию.
		const UButton* B = Buttons.IsValidIndex(i) ? Buttons[i].Get() : nullptr;
		bool bFromSkin = false;
		FLinearColor SkinIdle = FLinearColor::White, SkinActive = FLinearColor::White;
		if (B)
		{
			const FButtonStyle& St = B->GetStyle();
			SkinIdle   = St.NormalForeground.GetSpecifiedColor();
			SkinActive = St.PressedForeground.GetSpecifiedColor();
			bFromSkin  = !SkinIdle.Equals(SkinActive, 1.f / 512.f);
		}
		// ★ У этой кнопки цветов нет (двойник имени) — берём пару, найденную по ВСЕЙ полосе.
		if (!bFromSkin && bBarFromSkin) { SkinIdle = BarIdle; SkinActive = BarActive; bFromSkin = true; }

		const FLinearColor Idle = InOutIdle[i];
		const FLinearColor Want = bFromSkin
			? (i == ActiveIndex ? SkinActive : SkinIdle)
			: (i == ActiveIndex ? AsdaUi::TabCaptionActiveColor(Idle)
			                    : AsdaUi::TabCaptionInactiveColor(Idle));
		T->SetColorAndOpacity(FSlateColor(Want));
	}
}


void AsdaUi::ApplyTabState(const TArray<TObjectPtr<UImage>>& Planes,
	const TArray<TObjectPtr<UTextBlock>>& Captions, const TArray<TObjectPtr<UButton>>& Buttons,
	int32 ActiveIndex, TArray<FSlateBrush>& InOutIdle, TArray<FButtonStyle>& InOutIdleStyles,
	TArray<FLinearColor>& InOutIdleCaption)
{
	// 1) ★ ИСХОДНЫЕ СТИЛИ КНОПОК СНИМАЕМ ПЕРВЫМИ И РОВНО ОДИН РАЗ.
	//    ⚠ ПОРЯДОК ЗДЕСЬ КРИТИЧЕН, и я на нём уже обжёгся (живой PIE 2026-08-29): сперва в этой
	//    функции стояло применение плашки, которое читало кисть кадра 2 ИЗ ЖИВОГО стиля кнопки,
	//    а следом стиль активной кнопки гасился в NoDraw. На СЛЕДУЮЩЕМ вызове плашка читала уже
	//    ПОГАШЕННУЮ кисть, не находила кадр 2 и оставляла кадр 0 — активная вкладка становилась
	//    СВЕТЛОЙ, а подпись на ней белой, то есть «надписи нет». Поэтому кадр 2 берётся ТОЛЬКО из
	//    снятой копии `InOutIdleStyles`, которая никогда не гасится.
	if (InOutIdleStyles.Num() != Buttons.Num())
	{
		InOutIdleStyles.Reset(Buttons.Num());
		for (int32 i = 0; i < Buttons.Num(); ++i)
		{
			InOutIdleStyles.Add(Buttons[i] ? Buttons[i]->GetStyle() : FButtonStyle());
		}
	}

	// 2) Плашка: активной — кадр 2 из СНЯТОГО стиля, остальным — исходная кисть плана.
	if (InOutIdle.Num() != Planes.Num())
	{
		InOutIdle.Reset(Planes.Num());
		for (int32 i = 0; i < Planes.Num(); ++i)
		{
			InOutIdle.Add(Planes[i] ? Planes[i]->GetBrush() : FSlateBrush());
		}
	}
	for (int32 i = 0; i < Planes.Num(); ++i)
	{
		UImage* Img = Planes[i].Get();
		if (!Img || !InOutIdle.IsValidIndex(i)) { continue; }
		if (i != ActiveIndex || !InOutIdleStyles.IsValidIndex(i))
		{
			Img->SetBrush(InOutIdle[i]);
			continue;
		}
		const FSlateBrush& Sel = InOutIdleStyles[i].Pressed;
		if (Sel.DrawAs == ESlateBrushDrawType::NoDrawType || Sel.GetResourceObject() == nullptr)
		{
			Img->SetBrush(InOutIdle[i]);   // кадра 2 у скина нет — вид не выдумываем
			continue;
		}
		FSlateBrush Want = Sel;
		Want.ImageSize = InOutIdle[i].ImageSize;
		Img->SetBrush(Want);
	}

	// 3) ★ КНОПКА АКТИВНОЙ ВКЛАДКИ НИЧЕГО НЕ РИСУЕТ (разбор — в объявлении).
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		UButton* B = Buttons[i].Get();
		if (!B || !InOutIdleStyles.IsValidIndex(i)) { continue; }
		if (i == ActiveIndex)
		{
			FButtonStyle St = InOutIdleStyles[i];       // цвета подписи (Foreground) сохраняем
			FSlateBrush Empty;
			Empty.DrawAs = ESlateBrushDrawType::NoDrawType;
			Empty.TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.f));
			St.SetNormal(Empty); St.SetHovered(Empty); St.SetPressed(Empty); St.SetDisabled(Empty);
			B->SetStyle(St);
		}
		else
		{
			B->SetStyle(InOutIdleStyles[i]);            // вернуть ровно то, что лежало в ассете
		}
	}

	// 4) Цвет подписи — тем же проходом.
	ApplyTabCaptionState(Captions, Buttons, ActiveIndex, InOutIdleCaption);

	// 5) ★★★ У АКТИВНОЙ ВКЛАДКИ ЦВЕТ ПОДПИСИ ОБЯЗАН БЫТЬ ЦВЕТОМ КАДРА 2 (2026-08-29).
	//    `ApplyTabCaptionState` берёт его из `PressedForeground` кнопки, НО только если у неё
	//    цвета кадров вообще есть (их кладёт шаг `asda_ui_tabbar_captions.py`). Если шаг до
	//    кнопки не добрался — включается эвристика «по яркости базового цвета», и на вкладке,
	//    которой мы ТОЛЬКО ЧТО поставили ЧЁРНУЮ плашку кадра 2, она может вернуть ЧЁРНЫЙ
	//    (`TabCaptionActiveColor` для тёмной базовой подписи) — подпись пропадает.
	//    Мы знаем, что плашка сменилась, поэтому здесь это чинится однозначно: раз активный план
	//    взял кисть кадра 2, подпись берёт цвет кадра 2 той же кнопки; а если кнопка цветов не
	//    несёт — БЕЛЫЙ, потому что кадр 2 у скинов-вкладок тёмный (закон `jui-caption-states`).
	if (Captions.IsValidIndex(ActiveIndex) && Planes.IsValidIndex(ActiveIndex))
	{
		UTextBlock* T = Captions[ActiveIndex].Get();
		const UButton* B = Buttons.IsValidIndex(ActiveIndex) ? Buttons[ActiveIndex].Get() : nullptr;
		bool bPlaneSwitched = false;
		if (InOutIdle.IsValidIndex(ActiveIndex) && Planes[ActiveIndex])
		{
			const FBox2f UvNow  = Planes[ActiveIndex]->GetBrush().GetUVRegion();
			const FBox2f UvIdle = InOutIdle[ActiveIndex].GetUVRegion();
			bPlaneSwitched = !UvNow.Min.Equals(UvIdle.Min, 1e-4f) || !UvNow.Max.Equals(UvIdle.Max, 1e-4f);
			// ★ fishfix 09.10 (книга рыбака, МЕЛК-4): кадры скина могут лежать в ОТДЕЛЬНЫХ текстурах-вырезках без UV-региона
			//   (девятисрез BOX шага `asda_ui_widen_windows` у вкладок 74105) — тогда кадр 2 отличается ТЕКСТУРОЙ, а не регионом.
			bPlaneSwitched |= Planes[ActiveIndex]->GetBrush().GetResourceObject() != InOutIdle[ActiveIndex].GetResourceObject();
		}
		if (T && bPlaneSwitched)
		{
			FLinearColor Want = FLinearColor::White;
			if (InOutIdleStyles.IsValidIndex(ActiveIndex))
			{
				(void)B;
				const FSlateColor N = InOutIdleStyles[ActiveIndex].NormalForeground;
				const FSlateColor P = InOutIdleStyles[ActiveIndex].PressedForeground;
				if (!N.GetSpecifiedColor().Equals(P.GetSpecifiedColor(), 1.f / 512.f))
				{
					Want = P.GetSpecifiedColor();       // настоящий цвет кадра 2 из скина
				}
			}
			T->SetColorAndOpacity(FSlateColor(Want));
		}
	}
}

void AsdaUi::ApplyTabPlaneState(const TArray<TObjectPtr<UImage>>& Planes,
	const TArray<TObjectPtr<UButton>>& Buttons, int32 ActiveIndex, TArray<FSlateBrush>& InOutIdle)
{
	// ★ Исходные кисти (кадр 0) снимаем РОВНО ОДИН РАЗ — иначе после первой подсветки «исходной»
	//   стала бы кисть кадра 2, и вкладка навсегда осталась бы чёрной (та же ловушка, что у
	//   базовых цветов подписей в `TabCaptureIdleImpl`).
	if (InOutIdle.Num() != Planes.Num())
	{
		InOutIdle.Reset(Planes.Num());
		for (int32 i = 0; i < Planes.Num(); ++i)
		{
			InOutIdle.Add(Planes[i] ? Planes[i]->GetBrush() : FSlateBrush());
		}
	}

	for (int32 i = 0; i < Planes.Num(); ++i)
	{
		UImage* Img = Planes[i].Get();
		if (!Img || !InOutIdle.IsValidIndex(i)) { continue; }
		if (i != ActiveIndex) { Img->SetBrush(InOutIdle[i]); continue; }

		const UButton* B = Buttons.IsValidIndex(i) ? Buttons[i].Get() : nullptr;
		if (!B) { Img->SetBrush(InOutIdle[i]); continue; }
		const FSlateBrush& Sel = B->GetStyle().Pressed;
		if (Sel.DrawAs == ESlateBrushDrawType::NoDrawType || Sel.GetResourceObject() == nullptr)
		{
			Img->SetBrush(InOutIdle[i]);   // кадра 2 у скина нет — вид не выдумываем
			continue;
		}
		// Размер держим от ПЛАНА (он растянут по данным контрола), картинку и UV — от кадра 2.
		FSlateBrush Want = Sel;
		Want.ImageSize = InOutIdle[i].ImageSize;
		Img->SetBrush(Want);
	}
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★ СНЯТЬ СИНЕЕ КОЛЬЦО ФОКУСА У КНОПОК — см. обоснование в заголовке.
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★ U23 (В1 дедупа №2): тело — построчно прежних `UAsdaChatWindowSubsystem::MakeSwatch` / `MakeTabQuad`.
UImage* AsdaUi::MakeAtlasQuad(UUserWidget* Host, UPanelWidget* Parent, const TCHAR* Name, FName AtlasTex,
	const FBox2f& UV, const FVector2D& Size, int32 Z, bool bHandCursor)
{
	if (!Host || !Host->WidgetTree) { return nullptr; }
	UPanelWidget* Root = Parent ? Parent : Cast<UPanelWidget>(Host->WidgetTree->RootWidget);
	if (!Root) { return nullptr; }
	UImage* Img = Host->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(Name));
	if (!Img) { return nullptr; }
	FSlateBrush Br;
	// ★ Аудит №3 (A8/B8): атлас JUI — у менеджера текстур (кэш + refCount), а не своим `LoadObject`.
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
	if (UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(AtlasTex) : nullptr)
	{
		Br.SetResourceObject(Tex);
		Br.DrawAs = ESlateBrushDrawType::Image;
	}
	Br.SetUVRegion(UV);
	Br.ImageSize = Size;
	Img->SetBrush(Br);
	Img->SetVisibility(ESlateVisibility::Visible);
	if (bHandCursor) { Img->SetCursor(EMouseCursor::Hand); }
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Root->AddChild(Img))) { CS->SetZOrder(Z); }
	return Img;
}

// ★ U20 (В1 дедупа №2): тело — построчно прежних копий «NewObject → Index → Callback → AddDynamic(OnClicked)».
UAsdaButtonRelay* AsdaUi::MakeClickRelay(UObject* Owner, UButton* Btn, int32 Tag, TFunction<void(int32)> Fn)
{
	if (!Btn) { return nullptr; }
	UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(Owner);
	R->Index = Tag;
	R->Callback = MoveTemp(Fn);
	R->SetBoundWidget(Btn);   // ★ 31.10.2026: по нему якорь узнаёт мёртвое реле (`asda.UI.RelayAnchorPrune`)
	Btn->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
	return R;
}

bool AsdaUi::DisableButtonFocusRing(UButton* Btn)
{
	// ★ Свойство ставим ЧЕРЕЗ РЕФЛЕКСИЮ: в UE 5.8 у `UButton::IsFocusable` есть только геттер,
	//   а само поле и `InitIsFocusable` — protected (Button.h:70/206). Создавать ради одного бита
	//   подкласс `UButton` и переводить на него все окна дороже, чем один поиск свойства.
	static const FBoolProperty* FocusProp =
		CastField<FBoolProperty>(UButton::StaticClass()->FindPropertyByName(TEXT("IsFocusable")));
	if (!FocusProp || !Btn || !Btn->GetIsFocusable()) { return false; }
	FocusProp->SetPropertyValue_InContainer(Btn, false);
	return true;
}

int32 AsdaUi::DisableButtonFocusRings(UUserWidget* Window)
{
	if (!Window || !Window->WidgetTree) { return 0; }
	int32 N = 0;
	Window->WidgetTree->ForEachWidget([&N](UWidget* W)
	{
		if (DisableButtonFocusRing(Cast<UButton>(W))) { ++N; }
	});
	return N;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  Д2/Д8 (аудит `Reports/quest_npc_dedup_audit.md`) — ИКОНКА И «ПУСТАЯ» КИСТЬ.
//  Обе операции жили двумя копиями: в попапе наград эпизода (540) и в журнале заданий (700),
//  хотя это порт ОДНОЙ эталонной функции `QuestReward_SetSlotDisplay@0xD80230`.
// ════════════════════════════════════════════════════════════════════════════════════════════════
bool AsdaUi::ApplyFlatJuiIcon(UImage* Img, const FString& IconName, float Size)
{
	if (!Img || IconName.IsEmpty()) { return false; }
	// Путь к пакету знает менеджер ресурсов, а не мы (закон `resource-subsystems`):
	//   `LoadUITexture` внутри и есть склейка `/Game/AsdaAssets/Textures/JUI/<имя>.<имя>`.
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
	UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(FName(*IconName)) : nullptr;
	if (!Tex) { return false; }
	FSlateBrush B;
	B.SetResourceObject(Tex);
	B.ImageSize = FVector2D(Size, Size);
	Img->SetBrush(B);
	return true;
}

// ★ Ревизия В2 МЕЛК-6 (волна В4 рыбалки, 08.10.2026): ОДНО тело вырезки клетки атласа для `ApplyJuiIconCell` (квадрат)
//   и `ApplyJuiIconRect` (прямоугольник) — прежде второе построчно повторяло первое (закон check-existing, аудит дублей).
//   X/Y — левый верхний угол в пикселях ЛОГИЧЕСКОГО атласа, W×H — размер клетки, DispW×DispH — размер кисти.
static bool AsdaApplyJuiIconRegion(UImage* Img, const FString& IconName, int32 X, int32 Y, int32 W, int32 H, float DispW, float DispH)
{
	if (!Img || IconName.IsEmpty() || W <= 0 || H <= 0) { return false; }
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
	UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(FName(*IconName)) : nullptr;
	// ★ В1-B (U4b): тело вырезки — `ApplyJuiIconRegionTex` (им же пользуется хотбар со своим загрузчиком).
	return AsdaUi::ApplyJuiIconRegionTex(Img, Tex, X, Y, W, H, DispW, DispH);
}

bool AsdaUi::ApplyJuiIconRegionTex(UImage* Img, UTexture2D* Tex, int32 X, int32 Y, int32 W, int32 H, float DispW, float DispH)
{
	if (!Img || !Tex || W <= 0 || H <= 0) { return false; }
	const FIntPoint Size = AsdaUi::LogicalAtlasSize(Tex);
	const int32 TexW = FMath::Max(1, Size.X);
	const int32 TexH = FMath::Max(1, Size.Y);
	// Вылет за край заворачивается по модулю — так же, как D3DTADDRESS_WRAP оригинала.
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

bool AsdaUi::ApplyJuiIconCell(UImage* Img, const FString& IconName,
	int32 CellX, int32 CellY, int32 CellSize, float DisplaySize)
{
	return AsdaApplyJuiIconRegion(Img, IconName, CellX, CellY, CellSize, CellSize, DisplaySize, DisplaySize);
}

bool AsdaUi::ApplyJuiIconRect(UImage* Img, const FString& IconName, int32 X, int32 Y, int32 W, int32 H)
{
	return AsdaApplyJuiIconRegion(Img, IconName, X, Y, W, H, (float)W, (float)H);
}

void AsdaUi::ClearImageBrush(UImage* Img)
{
	if (!Img) { return; }
	FSlateBrush Empty;
	Empty.SetResourceObject(nullptr);
	Empty.TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.f));
	Img->SetBrush(Empty);
}

// ── ★★★ ЕДИНАЯ ТОЧКА: УГОЛ КАМЕРЫ ДЛЯ ЭКРАННЫХ УКАЗАТЕЛЕЙ (эталон `g_fCameraYaw`) ────────────────
//   Разбор — в шапке объявления (AsdaUiHelpers.h). Переехала сюда из `AsdaMinimapSubsystem.cpp`,
//   когда за тем же углом пришёл экранный трекер заданий 1800: копии не заводим.
//
//   ★ ПОЧЕМУ УГОЛ БЕРЁТСЯ БЕЗ ПОПРАВОК. Экран карты: +X = gameX, +Y = gameZ. Поворот квада на f
//   уводит «верх» (0,−1) в (sin f, −cos f); приравняв к игровому направлению, получаем
//   f = atan2(dirGameX, −dirGameZ) — ровно `AsdaCoord::FaceYawFromGameDir` (этим же выражением
//   рисуется значок игрока `miniMyPos`). Для камеры: UE-форвард камеры = (cos Yaw, sin Yaw),
//   в игровых осях это (gameX, gameZ) = (sin Yaw, −cos Yaw), откуда f = Yaw. Т.е. угол UMG-поворота
//   равен `CamYawDeg` БЕЗ смещений и без смены знака.
float AsdaUi::CameraConeYaw(const AAsdaPlayerCharacter* P)
{
	if (!P) { return 0.f; }
	if (const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(P->GetController()))
	{
		return PC->GetCameraYawUE();      // эталонный g_fCameraYaw
	}
	// Павном рулит не наш контроллер (редакторский просмотр/отладочный владелец) — берём мировой yaw
	//   САМОЙ стрелы камеры: это тот же угол, только уже применённый к компоненту.
	if (P->CameraArm) { return P->CameraArm->GetComponentRotation().Yaw; }
	return 0.f;
}

// ══════════ ВОЙНА ФРАКЦИЙ ЗА ГОРОДА — одна точка на порт (см. объявления в .h) ═════════════════
int32 AsdaUi::WarCitySlot(const UGameInstance* GI)
{
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const int32 MapId = LM ? LM->CurrentMapId : -1;
	if (MapId == 3) { return 0; }   // Альпия
	if (MapId == 0) { return 1; }   // Силарис
	if (MapId == 7) { return 2; }   // Пламения
	return -1;
}

bool AsdaUi::WarCityOwnedBySelf(const UGameInstance* GI, int32 Slot, int32& OutPercent, bool& bOutNeutral, bool* bOutNoFaction)
{
	OutPercent = 0;
	bOutNeutral = true;
	if (bOutNoFaction) { *bOutNoFaction = true; }
	const UAsdaNetSubsystem*   Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>()   : nullptr;
	const UAsdaWorldSubsystem* WS  = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (Slot < 0 || !Net || !Net->HasCityOwnerInfo()) { return false; }
	const int32 Owner = Net->GetCityOwnerFaction(Slot);
	OutPercent = Net->GetCityTaxPercent(Slot);
	const int32 Self = WS ? WS->GetSelfCampId() : -1;
	// Для ПОДПИСЕЙ эталон гасит блок в ДВУХ случаях: город не занят (состояние 3) ИЛИ «у игрока
	//   фракции нет» (−1) — оба печатают «-» (`uiItemListSlot_Populate@0xC3A200`).
	bOutNeutral = (Owner == 3) || (Self < 0);
	// ★★★ Для ДЕНЕГ доля обнуляется ТОЛЬКО когда у игрока нет фракции: эталон
	//   `uniconWar_ApplyOptionPercent@0xBCEEC0` (entity_char.c:44776) `if (фракция == −1) v4 = 0;`,
	//   ветки «город ничей» в формуле НЕТ. На ничьём городе (Owner == 3) доля ПРИМЕНЯЕТСЯ.
	if (bOutNoFaction) { *bOutNoFaction = (Self < 0); }
	return Owner == Self;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ВОЛНА 1 АУДИТА ДУБЛЕЙ (14.09.2026) — иконка предмета, видимость узла («это я?» — `AsdaSelf` ядра).
//   Разбор и доказательства — в шапках объявлений (`AsdaUiHelpers.h`).
// ════════════════════════════════════════════════════════════════════════════════════════════════
namespace
{
	// ★★★ ДЕФЕКТ ПОРТА (не оригинала), аудит 02.09.2026, находка Р2 — КЛЮЧ ОТКАТА.
	//   Общая точка показа поддерева (`AsdaCommunityUi::ShownVisibilityFor`) выдавала ПОЛНУЮ
	//   (ловящую мышь) видимость ТОЛЬКО `UButton`, а всем прочим узлам — `SelfHitTestInvisible`.
	//   Для поля ввода, галочки и комбобокса это значит «клик проходит насквозь»: щёлкнуть по
	//   полю и поставить каретку нельзя (закон `selfhittest-vs-hittest-invisible`).
	//   В оригинале такого дефекта НЕТ: хит-тест `uiControlHitTest@0x12DB470` пропускает клик
	//   насквозь только по признаку `NoSelect` из данных, а не по виду контрола.
	//   Ключ нужен для диагностики: главный риск правки — что какой-то узел начнёт ПЕРЕХВАТЫВАТЬ
	//   клики; с `0` поведение возвращается к прежнему за одну команду, без пересборки.
	ASDA_DEV_CVAR(int32, CVarInteractiveHitTest,
		TEXT("asda.UI.InteractiveHitTest"), 1,
		TEXT("1 (по умолчанию) = при показе поддерева мышь ловят ВСЕ интерактивные виджеты\n")
		TEXT("(кнопка, поле ввода, галочка, комбобокс, ползунок, прокрутка, список).\n")
		TEXT("0 = прежнее поведение порта: мышь ловит только UButton, поля ввода мертвы."),
		ECVF_Default);

	// ★ Ключ отката исправления E3 (гача и камень в тултипе рисовали клетку без заворота).
	ASDA_DEV_CVAR(int32, CVarItemIconWrap,
		TEXT("asda.UI.ItemIconWrap"), 1,
		TEXT("1 (по умолчанию) = иконки приза/варианта гачи и камня в тултипе заворачивают клетку атласа\n")
		TEXT("по модулю, как все остальные окна и как эталон (D3DTADDRESS_WRAP).\n")
		TEXT("0 = прежнее поведение этих трёх мест: UV без заворота (клетка за краем узкого атласа пустая)."),
		ECVF_Default);

	// ★★★ Ключ отката модального гейта покадрового опроса (П8-9, волна 9). Шапка — в `AsdaUiHelpers.h`.
	//   Ключ нужен по той же причине, что и `asda.UI.InteractiveHitTest`: главный риск правки —
	//   что какое-то окно окажется заглушено ШИРЕ, чем надо (например, модальным сочтётся то,
	//   что модальным быть не должно). С `0` поведение возвращается к прежнему за одну команду.
	ASDA_DEV_CVAR(int32, CVarModalGatePolling,
		TEXT("asda.UI.ModalGatePolling"), 1,
		TEXT("1 (по умолчанию) = под открытым модальным нотисом покадровый опрос мыши заглушён у ВСЕХ\n")
		TEXT("подсистем: ячейки, слоты и наведение мертвы так же, как кнопки, — как у эталона\n")
		TEXT("(ui_core.c:104149-104153, захваченный контрол забирает весь диспетч).\n")
		TEXT("0 = прежнее поведение порта: под нотисом кнопки мертвы, а ячейки и слоты ЖИВЫ."),
		ECVF_Default);
}

namespace AsdaUi
{
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ ПОКАДРОВОГО ОПРОСА (П8-9). Полное обоснование — в шапке `AsdaUiHelpers.h`.
	//   Коротко: у эталона захваченный модальный контрол забирает ВЕСЬ диспетч мыши
	//   (`ui_core.c:104149-104153`), а наш слейтовский блокер ест только клики, идущие через Slate,
	//   и покадровый опрос по ГЕОМЕТРИИ проходит мимо него. Эта функция — единственное место,
	//   где живёт условие «мышь сейчас отдана модалке».
	bool IsModalDispatchBlocked(const UGameInstance* GI)
	{
		// Ключ отката снимает гейт целиком, не трогая сами подсистемы.
		if (CVarModalGatePolling.GetValueOnAnyThread() == 0) { return false; }
		if (!GI) { return false; }
		// `GetSubsystem` у `UGameInstance` не константный, а сам вопрос — чтение; снимаем константность
		//   ровно здесь, чтобы все вызывающие могли держать `const UGameInstance*`.
		const UAsdaWindowSubsystem* WM = const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaWindowSubsystem>();
		// ★ 10.10.2026 (modal_lock3, МЕЛК-4): кадры с ПКМ больше НЕ пропускаются ни при каком значении ключа
		//   `asda.UI.ModalLockRightClick`. Эталон окнам под модалкой ПКМ, наведения и подсказок не отдаёт: нажатие (слот 3
		//   `uiWindowMgrDispatchMouse`) и перемещение (слот 5) при `+0x1A0` уходят только модалке; `uiControlDispatchMouseRToChildren`
		//   — миснейм слота 6 «кнопка отпущена» (разбор — у `UAsdaWindowSubsystem::ModalRightClickReachesWorld`). Ключ отката
		//   касается только мира (препроцессор контроллера).
		return WM != nullptr && WM->IsAnyModalOpen();
	}

	bool IsModalDispatchBlockedFor(const UGameInstance* GI, FName WindowId)
	{
		if (!IsModalDispatchBlocked(GI)) { return false; }
		// Модальный контрол эталона без `bModal` живёт в этом же окне — его собственный опрос не запираем.
		const UAsdaWindowSubsystem* WM = const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaWindowSubsystem>();
		return !(WM && !WindowId.IsNone() && WM->GetOriginalModalOwner() == WindowId);
	}

	bool CheckRecentActivity(const UGameInstance* GI)
	{
		// Порт `uiCheckRecentActivity@0xD326A0` — разбор условий и мест вызова в шапке (`AsdaUiHelpers.h`).
		if (!GI) { return false; }
		UGameInstance* G = const_cast<UGameInstance*>(GI);   // `GetSubsystem` не константный; вопрос — чтение
		// Условия 5 и 6 — окно высокой переплавки 78200 (+4 «идёт», [96] < 1000 мс после ответа 6501).
		const UAsdaHighRefineSubsystem* HR = G->GetSubsystem<UAsdaHighRefineSubsystem>();
		if (HR && HR->IsRecentActivity()) { return true; }
		// ★ Волна 2-А (06.10.2026): условия 2 и 4 — окно разборки 32000 ([89] == 1 «идёт», `now − [223] < 1000`).
		const UAsdaDisassemblySubsystem* DS = G->GetSubsystem<UAsdaDisassemblySubsystem>();
		if (DS && DS->IsRecentActivity()) { return true; }
		// Условия 1 и 3 — лавка заточки 17000: окна в порту нет (реестр отложенного).
		return false;
	}

	// Счётчик вызовов общей точки звука кнопок (самотест; игровой логики не несёт).
	static int32 GInvenButtonSfxCount = 0;

	int32 GetInvenButtonSfxCount() { return GInvenButtonSfxCount; }

	void PlayInvenButtonSfx(const UGameInstance* GI)
	{
		// `csoundPlayName2DStore(g_pSoundMgr, 5, 0)` — одноразовый 2D, доигрывает до конца.
		++GInvenButtonSfxCount;
		UGameInstance* G = const_cast<UGameInstance*>(GI);
		if (UAsdaSoundSubsystem* Snd = G ? G->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
		{
			Snd->PlaySound2DById(kInvenButtonSoundId);
		}
	}

	// ★★★ ИНТЕРАКТИВЕН ЛИ ВИДЖЕТ — то есть обязан ли он САМ ловить мышь.
	//   БЫЛО (дефект порта, находка Р2 аудита 02.09.2026): полную видимость получал ТОЛЬКО
	//     `UButton`, а поле ввода, галочка и комбобокс — `SelfHitTestInvisible`, то есть клик
	//     проходил сквозь них. По полю ввода нельзя было щёлкнуть и поставить каретку;
	//     подсистема клана лечила это точечно у двух своих полей (`Clan_MyIntroTxt` 14021 и
	//     `CMTY_CLAN_NOITICE_WRITE` 14029), а поле заметки соулмейта `Soul_Msg_Input` 6602
	//     оставалось мёртвым.
	//   СТАЛО: полную видимость получает ЛЮБОЙ интерактивный виджет, декорации — как были.
	//   ПОЧЕМУ ИМЕННО ЭТОТ СПИСОК (он снят с данных и с импортёра, а не подобран):
	//     · импортёр JUI `Tools/asda_jui_generic_umg.py` (строки 446-515) создаёт виджет ТОЛЬКО
	//       для типов скина `PushButton`/`RadioButton` → `UButton`, `CheckButton` → `UCheckBox`,
	//       `EditBox` → `UEditableText`, `ComboBox` → `UComboBoxString`, `Text` → `UTextBlock`;
	//       всё остальное (`StaticWindow`, `ResizeWindow`, `ProgressBar`, `ScrollBar`, `ListBox`,
	//       `Cursor`, `WheelWindow`) кладётся картинкой-планом `UImage`/`UAsdaAnimatedPlane`.
	//       Значит из ассетов сюда реально приходят четыре ПЕРВЫХ класса — и ровно у них
	//       интерактивность гарантирована самим типом скина. ★ ИСКЛЮЧЕНИЕ — `ComboBox`: его
	//       движковый виджет порт не показывает вовсе (разбор — в `ShownVisibilityFor`), поэтому
	//       в предикате его нет;
	//     · движковые близнецы (`UEditableTextBox`, обе многострочные правки)
	//       и `USlider`/`USpinBox`/`UScrollBox`/`UScrollBar`/`UListViewBase` перечислены потому,
	//       что порт строит такие узлы кодом (`ConstructWidget<UScrollBox>` в чате, диалоге NPC,
	//       журнале квестов). Все они ПОТРЕБЛЯЮТ ввод: с `SelfHitTestInvisible` прокрутка колесом
	//       и перетаскивание ползунка не работают.
	//   ⚠ ДЕКОРАЦИИ В СПИСОК НЕ ВХОДЯТ И НЕ ДОЛЖНЫ: `UTextBlock`, `UImage`, `UAsdaAnimatedPlane`,
	//     `UProgressBar`, `UCanvasPanel`, `UBorder` остаются `SelfHitTestInvisible`, иначе они
	//     начнут СЪЕДАТЬ клики по строкам списков (у строк клик ловит прозрачная подложка
	//     `EnsureRowHit` с `ZOrder = -1`, и любой видимый для мыши сосед сверху её убьёт).
	//     Это было бы хуже исходного дефекта (закон `selfhittest-vs-hittest-invisible`).
	bool IsInteractiveWidget(const UWidget* W)
	{
		if (!W) { return false; }
		return W->IsA<UButton>()                     // PushButton / RadioButton (`<idc>_OPT<i>`)
			|| W->IsA<UCheckBox>()                   // CheckButton (тоггл)
			|| W->IsA<UEditableText>()               // EditBox — ★ главный пострадавший
			|| W->IsA<UEditableTextBox>()            // движковый близнец поля ввода
			|| W->IsA<UMultiLineEditableText>()      // многострочная правка (объявление клана)
			|| W->IsA<UMultiLineEditableTextBox>()
			// ⚠ `UComboBoxString`/`UComboBoxKey` СЮДА НЕ ВХОДЯТ — см. `ShownVisibilityFor`:
			//   движковый комбобокс в порту не показывается ВООБЩЕ, мышь у поля ловит
			//   `UAsdaJuiComboBox` по геометрии плана.
			|| W->IsA<USlider>()                     // ползунок — тянется мышью
			|| W->IsA<USpinBox>()                    // числовое поле со стрелками
			|| W->IsA<UScrollBox>()                  // прокрутка колесом/перетаскиванием
			|| W->IsA<UScrollBar>()
			|| W->IsA<UListViewBase>();              // UListView/UTileView/UTreeView — выбор строки
	}

	// Видимость, с которой узел ПОКАЗЫВАЕТСЯ: интерактиву — полная (иначе он не ловит мышь),
	//   всем прочим — `SelfHitTestInvisible` (плашки и подписи не должны съедать клики по строке),
	//   движковому комбобоксу — `Collapsed` (разбор ниже).
	//   `asda.UI.InteractiveHitTest 0` возвращает прежнее поведение порта (только `UButton`).
	// ★★★ ЭТО ЕДИНСТВЕННАЯ РЕАЛИЗАЦИЯ ПРАВИЛА В ПОРТЕ (закон `check-existing-before-creating`):
	//   2026-09-02 сюда сведены восемь копий, список — у объявления в заголовке. Новую копию
	//   не заводить: показываешь поддерево окна — зови эту функцию.
	ESlateVisibility ShownVisibilityFor(const UWidget* W)
	{
		// ★★★ ДВИЖКОВЫЙ КОМБОБОКС НЕ ПОКАЗЫВАЕТСЯ НИКОГДА — и ключу отката это НЕ подчиняется.
		//   Импортёр JUI кладёт `UComboBoxString` ПОВЕРХ планов скина (`<idc>_<n>_0` тело,
		//   `<idc>_<n>_1` стрелка), а рисуется он движковым стилем — белая плашка с чёрным
		//   текстом. Порт рисует комбобокс САМ (`UAsdaJuiComboBox`), а движковый гасит:
		//     · `AsdaJuiComboBox.cpp:101` и `:392` — `Engine->SetVisibility(Collapsed)`;
		//     · `AsdaExchangeSubsystem.cpp:769-779` — отдельная ветка `Collapsed` в том же
		//       правиле показа поддерева (жалоба владельца 2026-08-28 «кнопка вызова
		//       выпадающего списка не игровая»: показ зажигал его обратно);
		//     · `AsdaChatWindowSubsystem.cpp:2054-2057` (2608) и `AsdaLoginGameMode.cpp:2542`
		//       (`CARAMAKE_cb`) — то же самое.
		//   Значит «показать движковый комбобокс» — дефект в ЛЮБОМ режиме, а не спорное решение,
		//   поэтому ветка стоит ДО `asda.UI.InteractiveHitTest`: ключ откатывает хит-тест
		//   интерактива, а не воскрешает белый дропдаун UE.
		if (W && (W->IsA<UComboBoxString>() || W->IsA<UComboBoxKey>()))
		{
			return ESlateVisibility::Collapsed;
		}
		if (CVarInteractiveHitTest.GetValueOnGameThread() == 0)
		{
			return (W && W->IsA<UButton>()) ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible;
		}
		return IsInteractiveWidget(W) ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible;
	}

}

bool AsdaUi::IsItemIconWrapFixEnabled()
{
	return CVarItemIconWrap.GetValueOnAnyThread() != 0;
}

bool AsdaUi::ApplyItemIcon(const UGameInstance* GI, UImage* Img, int32 ItemCode, FVector2D DrawSize,
	int32 IconSex, bool bWrap)
{
	if (!Img || ItemCode <= 0) { return false; }
	UGameInstance* MutGI = const_cast<UGameInstance*>(GI);
	UAsdaItemTableSubsystem* IT = MutGI ? MutGI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();   // ★ Аудит №3 (B8): короткий доступ к менеджеру текстур
	if (!IT || !TexMgr) { return false; }
	const FAsdaItemIcon Ic = IT->GetItemIcon(ItemCode, IT->ResolveIconMale(IconSex));   // ★ 08.11.2026: пол атласа как эталон
	if (!Ic.IsValid()) { return false; }
	UTexture2D* Tex = TexMgr->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(Ic.Atlas));   // ★ волна 2 п. 7
	if (!Tex) { return false; }   // атлас ещё не импортирован → узел остаётся как был
	// GetImportedSize (внутри LogicalAtlasSize), а не GetSizeX/Y: рендер-ресурс на первом показе ещё
	//   не готов («иконки кривые до перелистывания»); апскейл учтён таблицей исходных размеров.
	const FIntPoint Imp = LogicalAtlasSize(Tex);
	const int32 TexWi = FMath::Max(1, Imp.X);
	const int32 TexHi = FMath::Max(1, Imp.Y);
	// Заворот по модулю = D3DTADDRESS_WRAP эталона (разбор — в объявлении).
	const int32 CX = bWrap ? ((Ic.CellX % TexWi) + TexWi) % TexWi : Ic.CellX;
	const int32 CY = bWrap ? ((Ic.CellY % TexHi) + TexHi) % TexHi : Ic.CellY;
	FSlateBrush B;
	B.SetResourceObject(Tex);
	B.ImageSize = FVector2D(DrawSize.X > 0.f ? DrawSize.X : (float)Ic.CellSize,
	                        DrawSize.Y > 0.f ? DrawSize.Y : (float)Ic.CellSize);
	B.SetUVRegion(FBox2D(
		FVector2D((float)CX / (float)TexWi, (float)CY / (float)TexHi),
		FVector2D((float)(CX + Ic.CellSize) / (float)TexWi, (float)(CY + Ic.CellSize) / (float)TexHi)));
	Img->SetBrush(B);
	return true;
}

void AsdaUi::SetShown(UWidget* W, bool bShown)
{
	if (W) { W->SetVisibility(bShown ? ShownVisibilityFor(W) : ESlateVisibility::Collapsed); }
}

void AsdaUi::SetShownDecor(UWidget* W, bool bShown)
{
	if (W) { W->SetVisibility(bShown ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
}

void AsdaUi::SetShownNoHit(UWidget* W, bool bShown)
{
	if (W) { W->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
}

void AsdaUi::SetShownDecorAll(const TArray<TObjectPtr<UWidget>>& G, bool bShown)
{
	for (const TObjectPtr<UWidget>& W : G) { SetShownDecor(W, bShown); }
}

bool AsdaUi::AnyShown(const TArray<TObjectPtr<UWidget>>& G)
{
	for (const TObjectPtr<UWidget>& W : G)
	{
		if (IsShown(W)) { return true; }
	}
	return false;
}

bool AsdaUi::IsShown(const UWidget* W)
{
	return W && W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden;
}

namespace
{
	// Узлы с флагом `+0x24F` и их ИСХОДНЫЙ `ZOrder` (до первого подъёма). Слабые ключи: травел и снос окна их гасят сами.
	TMap<TWeakObjectPtr<UWidget>, int32>& TopRenderFlags()
	{
		static TMap<TWeakObjectPtr<UWidget>, int32> Map;
		return Map;
	}

	// Пересчёт одного родителя: `CControl_RenderChildren` — поверх рисуется ПОСЛЕДНИЙ видимый ребёнок с флагом.
	void RelayoutTopRender(UPanelWidget* Parent)
	{
		if (!Parent) { return; }
		TMap<TWeakObjectPtr<UWidget>, int32>& Flags = TopRenderFlags();
		int32 MaxZ = 0;
		UWidget* Top = nullptr;
		for (int32 I = 0; I < Parent->GetChildrenCount(); ++I)
		{
			UWidget* C = Parent->GetChildAt(I);
			UCanvasPanelSlot* CS = C ? Cast<UCanvasPanelSlot>(C->Slot) : nullptr;
			if (!CS) { continue; }
			const int32* Base = Flags.Find(C);
			const int32 Z = Base ? *Base : CS->GetZOrder();   // у поднятых — их исходный Z
			MaxZ = FMath::Max(MaxZ, Z);
			if (Base && AsdaUi::IsShown(C)) { Top = C; }     // перезапись = «последний по порядку»
		}
		for (int32 I = 0; I < Parent->GetChildrenCount(); ++I)
		{
			UWidget* C = Parent->GetChildAt(I);
			UCanvasPanelSlot* CS = C ? Cast<UCanvasPanelSlot>(C->Slot) : nullptr;
			const int32* Base = C ? Flags.Find(C) : nullptr;
			if (!CS || !Base) { continue; }
			const int32 Want = (C == Top) ? MaxZ + 1 : *Base;
			if (CS->GetZOrder() != Want) { CS->SetZOrder(Want); }
		}
	}

	// ★ Волна В6 ремесла (ревизия В5 МЕЛК-4): эталон выбирает поднимаемого КАЖДЫЙ кадр — в `CControl_RenderChildren`
	//   (ui_core.c:109155-109190) по ТЕКУЩЕЙ видимости детей. Порт делает то же: тикер ядра раз в кадр пересчитывает
	//   всех родителей, у которых есть дети с флагом, — поэтому показ/скрытие флагового узла без трогания флага тоже
	//   меняет, кто наверху. Z пишется только при изменении (`RelayoutTopRender`), пустая карта — ничего не делает.
	void EnsureTopRenderTicker()
	{
		static FTSTicker::FDelegateHandle Handle;
		if (Handle.IsValid()) { return; }
		Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) -> bool
		{
			TMap<TWeakObjectPtr<UWidget>, int32>& Flags = TopRenderFlags();
			if (Flags.Num() == 0) { return true; }
			TSet<UPanelWidget*> Parents;
			for (auto It = Flags.CreateIterator(); It; ++It)
			{
				UWidget* W = It.Key().Get();
				if (!W) { It.RemoveCurrent(); continue; }   // снесённый виджет — ключ протух
				if (UPanelWidget* P = W->GetParent()) { Parents.Add(P); }
			}
			for (UPanelWidget* P : Parents) { RelayoutTopRender(P); }
			return true;
		}));
	}
}

void AsdaUi::SetTopRender(UWidget* W, bool bOn)
{
	if (!W) { return; }
	UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(W->Slot);
	TMap<TWeakObjectPtr<UWidget>, int32>& Flags = TopRenderFlags();
	if (bOn)
	{
		if (!Flags.Contains(W)) { Flags.Add(W, CS ? CS->GetZOrder() : 0); }
	}
	else if (const int32* Base = Flags.Find(W))
	{
		const int32 Z = *Base;
		Flags.Remove(W);
		if (CS && CS->GetZOrder() != Z) { CS->SetZOrder(Z); }   // исходный порядок рисования
	}
	// Протухшие ключи (снесённые виджеты) выбрасываем попутно — карта не растёт между сессиями.
	for (auto It = Flags.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	RelayoutTopRender(W->GetParent());   // сразу — кадр вызова уже с верным порядком
	if (bOn) { EnsureTopRenderTicker(); }   // дальше — покадровый выбор, как `CControl_RenderChildren`
}

bool AsdaUi::IsTopRender(const UWidget* W)
{
	return W && TopRenderFlags().Contains(const_cast<UWidget*>(W));
}

void AsdaUi::SetTextIf(UTextBlock* T, const FString& S)
{
	if (T) { T->SetText(FText::FromString(S)); }
}

void AsdaUi::SetShownHit(UWidget* W, bool bShown, bool bOnlyIfChanged)
{
	if (!W) { return; }
	const ESlateVisibility V = bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (bOnlyIfChanged && W->GetVisibility() == V) { return; }
	W->SetVisibility(V);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] ОДИН GC-ЯКОРЬ — ОДНО ОКНО (`asda.UI.RelayAnchorSplit`). `UButton::OnClicked` держит реле СЛАБО (разбор у
//   `AsdaUi::BindButton`), поэтому якорь обязан жить ровно столько, сколько окно, чьи кнопки он держит. Полный обход 31.10 (96 якорей)
//   нашёл якоря, общие у двух самостоятельных окон: сброс при показе/пересоздании одного снимал якоря живого другого, и после
//   сборки мусора его кнопки молча не работали — `TWOBUT_MSGBOX` против логин-диалога `MESSAGE2_WIN` (UAsdaWindowSubsystem),
//   сетка 43400 против окна 43500 (UAsdaPetEvolutionSubsystem), попап 19100 против окна заточки (UAsdaItemUpgradeSubsystem); и
//   якорь, который рос при каждой разметке (карточки парикмахерской). 1 = у каждого окна свой якорь; 0 = прежнее поведение.
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════
static ASDA_DEV_CVAR(int32, CVarAsdaRelayAnchorSplit,
	TEXT("asda.UI.RelayAnchorSplit"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = у кнопок окон TWOBUT_MSGBOX, сетки питомца 43400, попапа заточки 19100 и карточек ")
	TEXT("парикмахерской свой GC-якорь реле (сброс соседнего окна их не снимает, повторная разметка не копит реле); 0 = прежнее ")
	TEXT("(общий якорь — после сборки мусора кнопки живого окна молча не работают)."),
	ECVF_Default);

// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] ЯКОРЬ НЕ КОПИТ МЁРТВЫЕ РЕЛЕ (`asda.UI.RelayAnchorPrune`, разбор у `AsdaUi::PruneDeadRelays`).
static ASDA_DEV_CVAR(int32, CVarAsdaRelayAnchorPrune,
	TEXT("asda.UI.RelayAnchorPrune"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = при каждой привязке кнопки якорь реле выбрасывает реле, чья кнопка уже уничтожена (окно ")
	TEXT("пересоздано), а подписи вкладок окна персонажа не множат подписки на одной кнопке; 0 = прежнее (реле копятся до конца подсистемы)."),
	ECVF_Default);

bool AsdaUi::RelayAnchorPruneOn()
{
	return CVarAsdaRelayAnchorPrune.GetValueOnGameThread() != 0;
}

bool AsdaUi::IsDeadRelay(const UObject* O)
{
	if (const UAsdaButtonRelay* R = Cast<UAsdaButtonRelay>(O)) { return R->HasDeadBinding(); }
	if (const UAsdaTextLimiter* L = Cast<UAsdaTextLimiter>(O)) { return L->HasDeadBinding(); }
	return false;
}

bool AsdaUi::IsDetachedRelay(const UObject* O, const UObject* W)
{
	if (!O || !W) { return false; }
	const UAsdaButtonRelay* R = Cast<UAsdaButtonRelay>(O);
	const UAsdaTextLimiter* L = Cast<UAsdaTextLimiter>(O);
	const UObject* Bound = R ? R->BoundWidget.Get() : (L ? L->BoundWidget.Get() : nullptr);
	if (Bound != W) { return false; }
	// Держит ли объект хоть один делегат виджета (`GetAllObjects` — живые приёмники записей).
	auto Holds = [O](const auto& Delegate) { return Delegate.GetAllObjects().Contains(O); };
	if (const UButton* B = Cast<UButton>(W))
	{
		return !(Holds(B->OnClicked) || Holds(B->OnPressed) || Holds(B->OnReleased) || Holds(B->OnHovered) || Holds(B->OnUnhovered));
	}
	if (const UCheckBox* C = Cast<UCheckBox>(W)) { return !Holds(C->OnCheckStateChanged); }
	if (const UEditableText* E = Cast<UEditableText>(W)) { return !(Holds(E->OnTextChanged) || Holds(E->OnTextCommitted)); }
	if (const UEditableTextBox* E = Cast<UEditableTextBox>(W)) { return !(Holds(E->OnTextChanged) || Holds(E->OnTextCommitted)); }
	if (const USlider* S = Cast<USlider>(W))
	{
		return !(Holds(S->OnValueChanged) || Holds(S->OnMouseCaptureBegin) || Holds(S->OnMouseCaptureEnd)
			|| Holds(S->OnControllerCaptureBegin) || Holds(S->OnControllerCaptureEnd));
	}
	return false;   // виджет неизвестного вида — не трогаем
}

void AsdaUi::MarkRelayBound(UObject* O, UObject* W)
{
	if (UAsdaButtonRelay* R = Cast<UAsdaButtonRelay>(O)) { R->SetBoundWidget(W); }
	else if (UAsdaTextLimiter* L = Cast<UAsdaTextLimiter>(O)) { L->SetBoundWidget(W); }
}

const UObject* AsdaUi::ButtonAsObject(const UButton* B)
{
	return B;
}

// Значение `asda.UI.RelayAnchorSplit` для подсистем окон (аксессор: имя ключа в exe вне журнала не попадает).
bool AsdaRelayAnchorSplitOn()
{
	return CVarAsdaRelayAnchorSplit.GetValueOnGameThread() != 0;
}

// ══ ★ R5-b 12.11.2026: общие точки волны R5 (разбор — в `AsdaUiHelpers.h`). Тела перенесены дословно из окон. ══════════════

void AsdaUi::CollectByIdc(const UUserWidget* Root, const FString& Idc, TArray<TObjectPtr<UWidget>>& Out)
{
	Out.Reset();
	if (!Root || !Root->WidgetTree) { return; }
	TArray<UWidget*> All;
	Root->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		// Сам контрол, его группа, капшн — точные имена.
		if (N == Idc || N == Idc + TEXT("_grp") || N == Idc + TEXT("_Text")) { Out.Add(W); continue; }
		// Планы и хит-зоны: `<idc>_<juiIdx>_<plane>` / `<idc>_<juiIdx>_HIT`.
		FString ParsedIdc; int32 JuiIdx = 0, Plane = 0;
		if (AsdaUi::ParsePlaneName(N, ParsedIdc, JuiIdx, Plane) && ParsedIdc == Idc) { Out.Add(W); }
	}
}

FVector2D AsdaUi::SlotPos(const UWidget* W)
{
	FVector2D P = FVector2D::ZeroVector, S = FVector2D::ZeroVector;   // без слота SlotRect их не трогает — ноль
	SlotRect(W, P, S);
	return P;
}

FVector2D AsdaUi::SlotSize(const UWidget* W)
{
	FVector2D P = FVector2D::ZeroVector, S = FVector2D::ZeroVector;   // без слота SlotRect их не трогает — ноль
	SlotRect(W, P, S);
	return S;
}

void AsdaUi::PlaySound2DByName(const UGameInstance* GI, const TCHAR* Name)
{
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2D(FName(Name));
	}
}

void AsdaUi::JournalAppend(TArray<FString>& Journal, double& Start, const FString& Event)
{
	const double Now = FPlatformTime::Seconds();
	if (Start < 0.0) { Start = Now; }
	if (Journal.Num() >= 256) { Journal.RemoveAt(0); }   // журнал диагностический — держим хвост
	Journal.Add(FString::Printf(TEXT("%7.0f мс  %s"), (Now - Start) * 1000.0, *Event));
}

void AsdaUi::PlaneShowRestart(UWidget* W)
{
	// Эталон `CControl_Show@0x12E1040`: перезапустить анимацию плана и показать его.
	if (!W) { return; }
	if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(W)) { P->RestartAnim(); }
	W->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void AsdaUi::PlaneHideStop(UWidget* W)
{
	// Эталон `CControl_CloseHide@0x12E1420`: остановить анимацию БЕЗ события «завершено» и спрятать.
	if (!W) { return; }
	if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(W)) { P->StopAnim(); }
	W->SetVisibility(ESlateVisibility::Collapsed);
}
