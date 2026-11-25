// Copyright (c) 2026 @sitxovski (Discord)
#include "Editor/AsdaUmgBuilder.h"

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "UI/AsdaAnimatedPlane.h"     // анимир. JUI-plane (система кадровой анимации)
#include "UI/AsdaJuiWindowMeta.h"     // ассет меты окон (ручки перетаскивания + контролы NoSelect)
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Sound/SoundBase.h"
#include "Sound/SlateSound.h"
#include "Engine/Texture2D.h"
#include "WidgetBlueprintFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprintOperationUtils.h"   // RemoveWidget / RenameWidget — штатные операции над узлами ассета
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"

// Строит/загружает Runtime-UFont-обёртку над FontFace'ами UI. Транзиентный шрифт НЕ сериализуется
// в WBP (падает в дефолт-«тофу» на загрузке), поэтому создаём и СОХРАНЯЕМ ассет-обёртку, на который
// ссылается FSlateFontInfo.
// ★★★ ЧТО ЗА ШРИФТ НА САМОМ ДЕЛЕ (03.09.2026, находка ревизии `review_soulinfo_tooltip_frame.md` Н2).
//   ЗДЕСЬ И В ЧЕТЫРЁХ ДРУГИХ ФАЙЛАХ комментарии писали «пиксель-шрифт из FONT0.BIN+DATA0.TEX» и
//   «Epilepsy Sans» — ЭТО НЕВЕРНО и заражало все замеры текста. По ФАКТУ в ассетах лежит:
//     `Content/AsdaAssets/Fonts/AsdaUIFont.uasset`     -> C:/Windows/Fonts/arial.ttf   (Arial Regular)
//     `Content/AsdaAssets/Fonts/AsdaUIFontBold.uasset` -> C:/Windows/Fonts/arialbd.ttf (Arial Bold)
//   (доказано именами исходников и name-таблицей внутри самих .uasset). Файлы
//   `RawAssets/Fonts/EpilepsySans*.ttf` и `RawAssets/Fonts/AsdaUIFont.ttf` в эти ассеты НЕ
//   импортированы — они лежат рядом как сырьё. На поведение миснейм не влиял, но врал при любом
//   расчёте ширины/высоты строки, поэтому исправлен во всех вхождениях (закон
//   `net-getcurrentmaptype-is-misname`: неверный вывод заражает комментарии, чистить все).
static UFont* GetOrCreateAsdaUIFont()
{
	const TCHAR* RTPath = TEXT("/Game/AsdaAssets/Fonts/AsdaUIFontRT");
	if (UFont* Existing = LoadObject<UFont>(nullptr, RTPath)) { return Existing; }
	UFontFace* Face = LoadObject<UFontFace>(nullptr, TEXT("/Game/AsdaAssets/Fonts/AsdaUIFont.AsdaUIFont"));
	if (!Face) { return nullptr; }
	UPackage* Pkg = CreatePackage(RTPath);
	if (!Pkg) { return nullptr; }
	UFont* Font = NewObject<UFont>(Pkg, TEXT("AsdaUIFontRT"), RF_Public | RF_Standalone);
	Font->FontCacheType = EFontCacheType::Runtime;
	FTypefaceEntry Entry;
	Entry.Name = FName(TEXT("Regular"));
	Entry.Font = FFontData(Face);
	Font->CompositeFont.DefaultTypeface.Fonts.Add(Entry);
	// ★ Bold-начертание (`AsdaUIFontBold` = arialbd.ttf, см. шапку функции) — для тега <FB> в markup.
	if (UFontFace* BoldFace = LoadObject<UFontFace>(nullptr, TEXT("/Game/AsdaAssets/Fonts/AsdaUIFontBold.AsdaUIFontBold")))
	{
		FTypefaceEntry BoldEntry;
		BoldEntry.Name = FName(TEXT("Bold"));
		BoldEntry.Font = FFontData(BoldFace);
		Font->CompositeFont.DefaultTypeface.Fonts.Add(BoldEntry);
	}
	Font->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Font);
	const FString FileName = FPackageName::LongPackageNameToFilename(RTPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Pkg, Font, *FileName, SaveArgs);
	return Font;
}

// ★ Базовый слот = rect в координатах базы 1024×768 (top-left анкор 0,0; offsets = Pos + Size).
//   ФАКТИЧЕСКИЕ анкоры (пер-контрольные, доля экрана — jui_align_layout.md) ставит ПОСТ-ПАСС питон-импортёра
//   (asda_jui_generic_umg._apply_anchor), читая этот базовый rect из слота + node.anchor{xspan,fx,yspan,fy}.
//   DPI проекта = 1.0 (UserInterfaceSettings) → 1 slate-юнит = 1 пиксель базы (фикс-контролы держат px-размер,
//   как оригинал; %-элементы тянутся span-анкором). Для не-JUI вызовов это разумный дефолт (top-left px).
static void ApplyResponsiveSlot(UPanelSlot* PSlotIn, FVector2D Pos, FVector2D Size)
{
	UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(PSlotIn);
	if (!S) { return; }
	S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
	S->SetAlignment(FVector2D(0.f, 0.f));
	S->SetOffsets(FMargin(Pos.X, Pos.Y, Size.X, Size.Y));   // L,T = позиция; R,B = размер (при point-анкоре)
}

UWidgetBlueprint* UAsdaUmgBuilder::CreateWidgetBP(const FString& PackagePath, const FString& AssetName)
{
	// (существующий ассет удаляет Python перед вызовом — unreal.EditorAssetLibrary.delete_asset)
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = UUserWidget::StaticClass();
	UObject* Created = AssetTools.CreateAsset(AssetName, PackagePath, UWidgetBlueprint::StaticClass(), Factory);
	UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(Created);
	if (!BP) { return nullptr; }
	// ★ Корень = полноэкранный CanvasPanel (заполняет viewport). Дети позиционируются RESPONSIVE-анкорами
	//   (ApplyResponsiveSlot: доля экрана как оригинал, doc jui_align_layout.md) — НЕ единый ScaleBox-stretch.
	//   Пиксельные размеры фикс-элементов масштабирует DPI-скейл проекта (UIScaleRule=ShortestSide, база 768).
	if (BP->WidgetTree)
	{
		UCanvasPanel* Canvas = BP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		BP->WidgetTree->RootWidget = Canvas;
	}
	return BP;
}

UPanelWidget* UAsdaUmgBuilder::GetRootCanvas(UWidgetBlueprint* BP)
{
	// Вернуть внутренний CanvasPanel (не корневой ScaleBox).
	if (BP && BP->WidgetTree)
	{
		if (UWidget* W = BP->WidgetTree->FindWidget(TEXT("RootCanvas")))
		{
			return Cast<UPanelWidget>(W);
		}
	}
	return nullptr;
}

UPanelWidget* UAsdaUmgBuilder::AddCanvas(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UCanvasPanel* Canvas = BP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), FName(*Name));
	// Контейнер сам НЕ ловит клики (прозрачная группа) — их обрабатывают дочерние интерактивы.
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UPanelSlot* PSlot = Parent->AddChild(Canvas);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Canvas;
}

UWidget* UAsdaUmgBuilder::AddImage(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size, UTexture2D* Texture, FBox2D UV, FLinearColor Tint, bool bBoxBrush, FMargin BoxMarginPx)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UImage* Img = BP->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*Name));
	FSlateBrush Brush;
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = Size;
	Brush.TintColor = FSlateColor(Tint);
	if (bBoxBrush)
	{
		Brush.DrawAs = ESlateBrushDrawType::Box;
		// маргин в долях от размера бокс-шаблона (для 9-slice)
		Brush.Margin = FMargin(BoxMarginPx.Left, BoxMarginPx.Top, BoxMarginPx.Right, BoxMarginPx.Bottom);
	}
	else
	{
		Brush.DrawAs = ESlateBrushDrawType::Image;
	}
	if (Texture) { Brush.SetImageSize(Size); }
	Img->SetBrush(Brush);
	// UV-регион
	if (!(UV.Min == FVector2D::ZeroVector && UV.Max == FVector2D::ZeroVector))
	{
		FSlateBrush B2 = Img->GetBrush();
		B2.SetUVRegion(FBox2f(FVector2f((float)UV.Min.X, (float)UV.Min.Y), FVector2f((float)UV.Max.X, (float)UV.Max.Y)));
		Img->SetBrush(B2);
	}
	// Картинка НЕ ловит клики (иначе перекрывала бы кнопки под/над ней) — только визуал.
	Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UPanelSlot* PSlot = Parent->AddChild(Img);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Img;
}

UWidget* UAsdaUmgBuilder::AddStrokeBox(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size, UTexture2D* Texture, FBox2D UV, FLinearColor Tint, float ThicknessPx)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UImage* Img = BP->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*Name));
	FSlateBrush Brush;
	// ★ БАГ-ФИКС (плашка RINA, «коричневый квадратик + оранжевая дуга» у камп-марки): Slate ИГНОРИРУЕТ
	//   UVRegion для DrawAs Box/Border — прежний Border-браш с текстурой атласа (n_01) резал рамку из
	//   УГЛОВ ЦЕЛОГО атласа (Margin×512 ≈ 21px: справа-сверху оранжевая дуга, справа-снизу коричневая
	//   панель — подтверждено дампом WBP + кропами атласа). Скин Box_White1Stroke — сплошной белый
	//   3×3-тайл (n_01 УВ 0.43..0.436 = чисто-белые пиксели), т.е. эталонный вид = ТОНКАЯ ОДНОТОННАЯ
	//   РАМКА цвета Diffuse. Рисуем её RoundedBox-АУТЛАЙНОМ без текстуры: заливка прозрачная,
	//   контур = Tint толщиной ThicknessPx (радиус 0 → прямоугольник). Texture/UV оставлены в API
	//   (совместимость с генератором), но больше не используются.
	Brush.SetResourceObject(nullptr);
	Brush.ImageSize = Size;
	Brush.TintColor = FSlateColor(FLinearColor::Transparent);          // центр пустой (ES4 прозрачен)
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.CornerRadii = FVector4(0.f, 0.f, 0.f, 0.f);  // прямые углы
	Brush.OutlineSettings.Color = FSlateColor(Tint);                    // цвет рамки = Diffuse скина
	Brush.OutlineSettings.Width = FMath::Max(ThicknessPx, 1.f);
	(void)Texture; (void)UV;   // параметры сохранены для совместимости API генератора
	Img->SetBrush(Brush);
	Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UPanelSlot* PSlot = Parent->AddChild(Img);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Img;
}

UWidget* UAsdaUmgBuilder::AddAnimatedPlane(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size, UTexture2D* Texture, FBox2D BaseUV, FLinearColor Tint,
	FVector2D FrameSizePx, int32 TotalFrame, float FrameIntervalMs, bool bLoop)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	// UAsdaAnimatedPlane = UImage-подкласс с кадровой анимацией (система JUI plane-анимации).
	UAsdaAnimatedPlane* Plane = BP->WidgetTree->ConstructWidget<UAsdaAnimatedPlane>(
		UAsdaAnimatedPlane::StaticClass(), FName(*Name));
	// Начальный браш кадра0 (текстура + UV) — как у статичного image; далее виджет сам циклирует UV.
	FSlateBrush Brush;
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = Size;
	Brush.TintColor = FSlateColor(Tint);
	Brush.DrawAs = ESlateBrushDrawType::Image;
	if (Texture) { Brush.SetImageSize(Size); }
	if (!(BaseUV.Min == FVector2D::ZeroVector && BaseUV.Max == FVector2D::ZeroVector))
	{
		Brush.SetUVRegion(FBox2f(FVector2f((float)BaseUV.Min.X, (float)BaseUV.Min.Y),
			FVector2f((float)BaseUV.Max.X, (float)BaseUV.Max.Y)));
	}
	Plane->SetBrush(Brush);
	// Параметры анимации (из JuiSkin: FrameTextureSize/TotalFrame/FrameInterval/UV кадра0).
	Plane->FrameTexture     = Texture;
	Plane->FrameSizePx      = FrameSizePx;
	Plane->TotalFrame       = TotalFrame;
	Plane->FrameIntervalMs  = FrameIntervalMs;
	Plane->BaseUV           = BaseUV;
	Plane->bLoop            = bLoop;
	Plane->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UPanelSlot* PSlot = Parent->AddChild(Plane);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Plane;
}

UWidget* UAsdaUmgBuilder::AddHitZone(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UImage* Img = BP->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*Name));
	FSlateBrush Brush;
	Brush.SetResourceObject(nullptr);                       // без текстуры — зона ничего не рисует
	Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
	Brush.ImageSize = Size;
	Brush.TintColor = FSlateColor(FLinearColor::Transparent);
	Img->SetBrush(Brush);
	// ★ SelfHitTestInvisible — зона НЕ должна перехватывать мышь у настоящих кнопок под ней
	//   (её прямоугольник читает подсистема окон напрямую, через GetCachedGeometry).
	Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UPanelSlot* PSlot = Parent->AddChild(Img);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Img;
}

UAsdaJuiWindowMetaAsset* UAsdaUmgBuilder::CreateWindowMetaAsset(const FString& PackagePath, const FString& AssetName)
{
	const FString Full = PackagePath + TEXT("/") + AssetName + TEXT(".") + AssetName;
	// ★ Существующий ассет НЕ чистим: Python сам сливает старые записи с новыми по WindowId.
	//   Иначе прогон одного класса .JUI (напр. только login) стёр бы мету всех остальных окон.
	if (UAsdaJuiWindowMetaAsset* Existing = LoadObject<UAsdaJuiWindowMetaAsset>(nullptr, *Full))
	{
		return Existing;
	}
	UPackage* Pkg = CreatePackage(*(PackagePath + TEXT("/") + AssetName));
	if (!Pkg) { return nullptr; }
	UAsdaJuiWindowMetaAsset* Asset = NewObject<UAsdaJuiWindowMetaAsset>(
		Pkg, UAsdaJuiWindowMetaAsset::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);
	if (!Asset) { return nullptr; }
	Asset->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);
	return Asset;
}

UWidget* UAsdaUmgBuilder::AddComboBox(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UComboBoxString* Cb = BP->WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), FName(*Name));
	// Опции наполняет рантайм (напр. созвездия). Шрифт — дефолтный стиль комбобокса.
	UPanelSlot* PSlot = Parent->AddChild(Cb);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Cb;
}

// ★★ КНОПКИ JUI НЕ ФОКУСИРУЕМЫ. `UButton::IsFocusable` по умолчанию `true` (Button.cpp:48), а
//   `SWidget::Paint` (SWidget.cpp:1744) дорисовывает фокусируемому виджету кисть `FocusRectangle` —
//   в Starship-стиле это СИНЯЯ СКРУГЛЁННАЯ РАМКА (StarshipCoreStyle.cpp:217). В оригинале её нет,
//   а фокус — понятие ТОЛЬКО поля ввода (события GOT/LOST_FOCUS у `EditBox`), так что кнопка,
//   забирающая фокус у строки чата, — ещё и поведенческая ошибка.
//   Свойство ставим рефлексией: в UE 5.8 у него только геттер, само поле protected (Button.h:70).
//   Рантайм-близнец для уже собранных окон — `AsdaUi::DisableButtonFocusRings`.
static void AsdaMakeButtonUnfocusable(UButton* Btn)
{
	static const FBoolProperty* FocusProp =
		CastField<FBoolProperty>(UButton::StaticClass()->FindPropertyByName(TEXT("IsFocusable")));
	if (Btn && FocusProp) { FocusProp->SetPropertyValue_InContainer(Btn, false); }
}

UPanelWidget* UAsdaUmgBuilder::AddButton(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UButton* Btn = BP->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
	// ПРОЗРАЧНЫЙ стиль: фон рисует отдельный _Bg-image атласа, кнопка только ловит клики (иначе серый дефолт перекрыл бы).
	FButtonStyle Style = Btn->GetStyle();
	FSlateBrush Empty;
	Empty.DrawAs = ESlateBrushDrawType::NoDrawType;
	Empty.TintColor = FSlateColor(FLinearColor(1, 1, 1, 0));
	Style.SetNormal(Empty);
	Style.SetHovered(Empty);
	Style.SetPressed(Empty);
	Style.NormalPadding = FMargin(0);
	Style.PressedPadding = FMargin(0);
	Btn->SetStyle(Style);
	AsdaMakeButtonUnfocusable(Btn);
	UPanelSlot* PSlot = Parent->AddChild(Btn);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Btn;
}

UPanelWidget* UAsdaUmgBuilder::AddStyledButton(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size, UTexture2D* Texture,
	FBox2D NormalUV, FLinearColor NormalTint,
	FBox2D HoverUV, FLinearColor HoverTint,
	FBox2D PressUV, FLinearColor PressTint,
	const FString& ClickSoundPath)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UButton* Btn = BP->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
	auto MakeBr = [&](FBox2D UV, FLinearColor Tint)
	{
		FSlateBrush Br;
		Br.SetResourceObject(Texture);
		Br.DrawAs = ESlateBrushDrawType::Image;
		Br.ImageSize = Size;
		Br.SetImageSize(Size);
		Br.TintColor = FSlateColor(Tint);
		Br.SetUVRegion(FBox2f(FVector2f((float)UV.Min.X, (float)UV.Min.Y), FVector2f((float)UV.Max.X, (float)UV.Max.Y)));
		return Br;
	};
	FButtonStyle Style;
	Style.SetNormal(MakeBr(NormalUV, NormalTint));
	Style.SetHovered(MakeBr(HoverUV, HoverTint));
	Style.SetPressed(MakeBr(PressUV, PressTint));
	// ★ «Выключено» = кадр 0. Отдельного disable-кадра у JUI-кнопок НЕТ (все 130 скинов PushButton
	//   в JuiSkin.ini имеют ровно 3 кадра SS0..SS2), а эталон при `disabled` (+610) просто ИГНОРИРУЕТ
	//   ввод, не трогая индекс кадра `+1976`. Без этой строки Slate рисовал бы свой серый дефолт.
	Style.SetDisabled(MakeBr(NormalUV, NormalTint));
	Style.NormalPadding = FMargin(0);
	Style.PressedPadding = FMargin(0);
	// Звук клика (эталон S_e_button09) — UMG-нативно через PressedSlateSound.
	if (!ClickSoundPath.IsEmpty())
	{
		if (USoundBase* Snd = LoadObject<USoundBase>(nullptr, *ClickSoundPath))
		{
			FSlateSound Sound;
			Sound.SetResourceObject(Snd);
			Style.PressedSlateSound = Sound;
		}
	}
	Btn->SetStyle(Style);
	AsdaMakeButtonUnfocusable(Btn);
	UPanelSlot* PSlot = Parent->AddChild(Btn);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Btn;
}

bool UAsdaUmgBuilder::ApplyButtonCaptionStates(UButton* Button,
	FLinearColor NormalColor, FLinearColor HoverColor, FLinearColor PressColor,
	FVector2D NormalOffset, FVector2D PressOffset)
{
	if (!Button) { return false; }
	FButtonStyle Style = Button->GetStyle();
	Style.SetNormalForeground(FSlateColor(NormalColor));
	Style.SetHoveredForeground(FSlateColor(HoverColor));
	Style.SetPressedForeground(FSlateColor(PressColor));
	// ★ Отдельного disable-кадра у JUI-кнопок нет (все 130 скинов `PushButton` — ровно 3 кадра),
	//   эталон при `disabled` (+610) просто игнорирует ввод, не трогая индекс кадра ⇒ цвет покоя.
	Style.SetDisabledForeground(FSlateColor(NormalColor));
	// Смещение подписи: `CaptionPoint` кадра. Right/Bottom НЕ используем (в JUI смещение
	//   двумерное), но обнуляем явно — иначе останется дефолт Slate (2,2,2,2)/(2,3,2,1).
	Style.NormalPadding  = FMargin((float)NormalOffset.X, (float)NormalOffset.Y, 0.f, 0.f);
	Style.PressedPadding = FMargin((float)PressOffset.X,  (float)PressOffset.Y,  0.f, 0.f);
	Button->SetStyle(Style);
	return true;
}

bool UAsdaUmgBuilder::ApplyButtonSkin(UButton* Button, UTexture2D* Texture,
	FBox2D NormalUV, FLinearColor NormalTint,
	FBox2D HoverUV, FLinearColor HoverTint,
	FBox2D PressUV, FLinearColor PressTint)
{
	if (!Button || !Texture) { return false; }
	// Размер кисти = размер слота кнопки (как в AddStyledButton, где он равен Size).
	FVector2D Size(0.f, 0.f);
	if (const UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Button->Slot))
	{
		const FMargin Off = CS->GetOffsets();
		Size = FVector2D(Off.Right, Off.Bottom);
	}
	auto MakeBr = [&](FBox2D UV, FLinearColor Tint)
	{
		FSlateBrush Br;
		Br.SetResourceObject(Texture);
		Br.DrawAs = ESlateBrushDrawType::Image;
		Br.ImageSize = Size;
		Br.SetImageSize(Size);
		Br.TintColor = FSlateColor(Tint);
		Br.SetUVRegion(FBox2f(FVector2f((float)UV.Min.X, (float)UV.Min.Y), FVector2f((float)UV.Max.X, (float)UV.Max.Y)));
		return Br;
	};
	FButtonStyle Style = Button->GetStyle();
	Style.SetNormal(MakeBr(NormalUV, NormalTint));
	Style.SetHovered(MakeBr(HoverUV, HoverTint));
	Style.SetPressed(MakeBr(PressUV, PressTint));
	Style.SetDisabled(MakeBr(NormalUV, NormalTint));   // disable-кадра у JUI нет — см. AddStyledButton
	Style.NormalPadding = FMargin(0);
	Style.PressedPadding = FMargin(0);
	Button->SetStyle(Style);
	return true;
}

UWidget* UAsdaUmgBuilder::AddText(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size, const FString& Text, int32 FontSize, FLinearColor Color, int32 HAlign,
	bool bWrap, bool bBold)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UTextBlock* Txt = BP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*Name));
	Txt->SetText(FText::FromString(Text));
	// ★ Шрифт UI порта — `AsdaUIFontRT` (обёртка над arial/arialbd, см. `GetOrCreateAsdaUIFont`).
	//   Fallback — Roboto (кириллица есть).
	FSlateFontInfo Font;
	if (UFont* UIFont = GetOrCreateAsdaUIFont())
	{
		// ★ Начертание: ВСЕГДА Regular, если вызывающий явно не попросил Bold (разбор — у объявления
		//   в заголовке: атлас оригинала собран весом 400, а «жирный» там — двойная отрисовка ±1 px).
		Font = FSlateFontInfo(UIFont, FontSize, FName(bBold ? TEXT("Bold") : TEXT("Regular")));
	}
	else
	{
		Font = FCoreStyle::GetDefaultFontStyle("Regular", FontSize);
		if (UObject* RobotoFont = StaticLoadObject(UFont::StaticClass(), nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")))
		{ Font.FontObject = RobotoFont; Font.TypefaceFontName = FName(TEXT("Regular")); Font.Size = FontSize; }
	}
	Txt->SetFont(Font);
	Txt->SetColorAndOpacity(FSlateColor(Color));
	Txt->SetJustification(HAlign == 1 ? ETextJustify::Center : HAlign == 2 ? ETextJustify::Right : ETextJustify::Left);
	// ★ Автоперенос ТОЛЬКО когда явно нужен (bWrap): в CanvasPanel autowrap НЕ получает ограничение ширины на
	//   measure-pass → текст рендерится полной шириной без переноса И ломает right-justify (значение выравнивается
	//   по полной ширине, а не по борту). Для однострочного UI (лейбл/значение) bWrap=false → корректный aX=2.
	Txt->SetAutoWrapText(bWrap);
	Txt->SetVisibility(ESlateVisibility::SelfHitTestInvisible);   // текст не ловит клики
	// ★ Текст рисуется НА СВОЁМ rect из JUI (Pos/Size), без нашей вертикальной центровки — как оригинал.
	UPanelSlot* PSlot = Parent->AddChild(Txt);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Txt;
}

UWidget* UAsdaUmgBuilder::AddEditableText(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size, const FString& HintText, int32 FontSize, FLinearColor Color, bool bPassword)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UEditableText* Ed = BP->WidgetTree->ConstructWidget<UEditableText>(UEditableText::StaticClass(), FName(*Name));
	FSlateFontInfo Font;
	if (UFont* UIFont = GetOrCreateAsdaUIFont())
	{ Font = FSlateFontInfo(UIFont, FontSize, FName(TEXT("Regular"))); }
	else
	{
		Font = FCoreStyle::GetDefaultFontStyle("Regular", FontSize);
		if (UObject* RobotoFont = StaticLoadObject(UFont::StaticClass(), nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")))
		{ Font.FontObject = RobotoFont; Font.TypefaceFontName = FName(TEXT("Regular")); Font.Size = FontSize; }
	}
	FEditableTextStyle St = FCoreStyle::Get().GetWidgetStyle<FEditableTextStyle>("NormalEditableText");
	St.Font = Font;
	St.ColorAndOpacity = FSlateColor(Color);
	Ed->SetWidgetStyle(St);
	Ed->SetHintText(FText::FromString(HintText));
	Ed->SetIsPassword(bPassword);
	UPanelSlot* PSlot = Parent->AddChild(Ed);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Ed;
}

// ── Кисть Slate из кадра-состояния JUI-скина (текстура + UV-регион + Diffuse-тинт кадра) ──────────
//   Кадр без текстуры = скин его не задал → зовущий подставляет кадр 0 (см. AddCheckBox/AddOptionButton).
static FSlateBrush MakeJuiFrameBrush(const FAsdaJuiFrame& Frame, FVector2D Size)
{
	FSlateBrush Br;
	Br.SetResourceObject(Frame.Texture);
	Br.DrawAs = ESlateBrushDrawType::Image;
	Br.ImageSize = Size;
	Br.SetImageSize(Size);
	Br.TintColor = FSlateColor(Frame.Tint);
	Br.SetUVRegion(FBox2f(FVector2f((float)Frame.UV.Min.X, (float)Frame.UV.Min.Y),
	                      FVector2f((float)Frame.UV.Max.X, (float)Frame.UV.Max.Y)));
	return Br;
}

UWidget* UAsdaUmgBuilder::AddCheckBox(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size,
	const FAsdaJuiFrame& Normal, const FAsdaJuiFrame& Hover,
	const FAsdaJuiFrame& Checked, const FAsdaJuiFrame& CheckedHover)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UCheckBox* Cb = BP->WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(*Name));
	if (Normal.Texture)
	{
		// Кадр без своей текстуры = состояние в скине не задано → рисуем кадром 0 (эталон: слот кадра
		//   остаётся копией имени кадра 0, `slotRec_SetNameAll4 @0x12B4400`).
		auto Br = [&](const FAsdaJuiFrame& F)
		{
			return MakeJuiFrameBrush(F.Texture ? F : Normal, Size);
		};
		FCheckBoxStyle St;
		const FSlateBrush F0 = Br(Normal);
		const FSlateBrush F1 = Br(Hover);
		const FSlateBrush F2 = Br(Checked);
		const FSlateBrush F3 = Br(CheckedHover);
		// СНЯТО: кадр0 покой, кадр1 наведение, кадр2 нажатие (оно же превью «отмечено» — OnPressed ставит +1976=2).
		St.UncheckedImage = F0; St.UncheckedHoveredImage = F1; St.UncheckedPressedImage = F2;
		// ОТМЕЧЕНО: кадр2 покой, кадр3 наведение и нажатие (checked+hover, `CControl_SetVisibleState`).
		St.CheckedImage   = F2; St.CheckedHoveredImage   = F3; St.CheckedPressedImage   = F3;
		// «Неопределённого» состояния у JUI-чекбокса НЕТ (только флаг +608) — рисуем как снятое.
		St.UndeterminedImage = F0; St.UndeterminedHoveredImage = F1; St.UndeterminedPressedImage = F2;
		// Кадр занимает ВЕСЬ прямоугольник контрола: отступа между «боксом» и содержимым в JUI нет,
		//   а дефолтный FCheckBoxStyle::Padding сдвинул бы картинку вправо.
		St.Padding = FMargin(0.f);
		St.CheckBoxType = ESlateCheckBoxType::CheckBox;
		Cb->SetWidgetStyle(St);
	}
	UPanelSlot* PSlot = Parent->AddChild(Cb);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Cb;
}

FString UAsdaUmgBuilder::DescribeCheckBoxStyle(UCheckBox* Box)
{
	if (!Box) { return TEXT("нет виджета"); }
	const FCheckBoxStyle& St = Box->GetWidgetStyle();
	auto One = [](const TCHAR* Label, const FSlateBrush& Br)
	{
		const UObject* Res = Br.GetResourceObject();
		const FBox2f& UV = Br.GetUVRegion();
		const FLinearColor C = Br.TintColor.GetSpecifiedColor();
		return FString::Printf(TEXT("%s: тек=%s uv=%.4f %.4f %.4f %.4f тинт=%.3f %.3f %.3f %.3f"),
			Label, Res ? *Res->GetName() : TEXT("нет"),
			UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y, C.R, C.G, C.B, C.A);
	};
	TArray<FString> Lines;
	Lines.Add(One(TEXT("снято      "), St.UncheckedImage));
	Lines.Add(One(TEXT("снято+наве "), St.UncheckedHoveredImage));
	Lines.Add(One(TEXT("снято+нажат"), St.UncheckedPressedImage));
	Lines.Add(One(TEXT("отмечено   "), St.CheckedImage));
	Lines.Add(One(TEXT("отмеч+наве "), St.CheckedHoveredImage));
	Lines.Add(One(TEXT("отмеч+нажат"), St.CheckedPressedImage));
	return FString::Join(Lines, TEXT("\n"));
}

UPanelWidget* UAsdaUmgBuilder::AddOptionButton(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size, const FAsdaJuiFrame& Hover, const FAsdaJuiFrame& Pressed)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UButton* Btn = BP->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
	FButtonStyle Style = Btn->GetStyle();
	// Покой и «выключено» — НИЧЕГО не рисуем: под кнопкой лежит план-картинка кадра 0, и её же красит
	//   рантайм, когда вариант выбран (эталонный кадр 3 SetSelectedIndex). Перекрыть её нельзя.
	FSlateBrush Empty;
	Empty.DrawAs = ESlateBrushDrawType::NoDrawType;
	Empty.TintColor = FSlateColor(FLinearColor(1, 1, 1, 0));
	Style.SetNormal(Empty);
	Style.SetDisabled(Empty);
	Style.SetHovered(Hover.Texture   ? MakeJuiFrameBrush(Hover, Size)   : Empty);
	Style.SetPressed(Pressed.Texture ? MakeJuiFrameBrush(Pressed, Size) : Empty);
	Style.NormalPadding = FMargin(0);
	Style.PressedPadding = FMargin(0);
	Btn->SetStyle(Style);
	AsdaMakeButtonUnfocusable(Btn);
	UPanelSlot* PSlot = Parent->AddChild(Btn);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return Btn;
}

// СИСТЕМА прогресс-баров: скин типа "ProgressBar" (Log_WhiteBar и др.) → нативный UProgressBar.
//   Заливка (FillImage) = текстура/UV/тинт скина; фон трека прозрачный (видимый контур даёт отдельный *_BASE).
//   Значение ставит рантайм: UProgressBar::SetPercent(0..1) — заливка растёт слева-направо БЕЗ мыла (не scale).
UWidget* UAsdaUmgBuilder::AddProgressBar(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
	FVector2D Pos, FVector2D Size, UTexture2D* Texture, FBox2D FillUV, FLinearColor FillTint)
{
	if (!BP || !BP->WidgetTree || !Parent) { return nullptr; }
	UProgressBar* PB = BP->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), FName(*Name));
	FProgressBarStyle St;
	FSlateBrush Bg; Bg.DrawAs = ESlateBrushDrawType::NoDrawType; Bg.TintColor = FSlateColor(FLinearColor(0, 0, 0, 0));
	St.BackgroundImage = Bg;
	// Заливка = СПЛОШНОЙ цвет из diffuse скина (БЕЗ текстуры — крошечный UV-свотч атласа сэмплился в серое).
	//   Log_WhiteBar diffuse=FFFFFFFF → белый; HP-бары возьмут свой цвет. Чистый бар, растёт по Percent.
	FSlateBrush Fill;
	Fill.DrawAs = ESlateBrushDrawType::Image;   // без ResourceObject = белый 1×1 → тинтуется в цвет diffuse
	Fill.ImageSize = Size;
	Fill.TintColor = FSlateColor(FillTint);
	St.FillImage = Fill;
	PB->SetWidgetStyle(St);
	PB->SetFillColorAndOpacity(FillTint);
	PB->SetBarFillType(EProgressBarFillType::LeftToRight);
	PB->SetPercent(0.f);
	UPanelSlot* PSlot = Parent->AddChild(PB);
	ApplyResponsiveSlot(PSlot, Pos, Size);
	return PB;
}

// ★ Порядок детей = z-порядок в CanvasPanel. Квад карты миникарты должен быть ПОД контролами окна
//   (оригинал: uiMiniMapRender рисует карту до WindowMgr_RenderControls) → ставим его на index 0.
bool UAsdaUmgBuilder::MoveChildToIndex(UPanelWidget* Parent, UWidget* Child, int32 Index)
{
	if (!Parent || !Child) { return false; }
	const int32 Cur = Parent->GetChildIndex(Child);
	if (Cur < 0) { return false; }
	Parent->ShiftChild(FMath::Clamp(Index, 0, Parent->GetChildrenCount() - 1), Child);
	return true;
}

// ★ Точечное удаление узла ассета (обоснование — в .h). Штатный путь редактора, без поддерева.
bool UAsdaUmgBuilder::RemoveWidgetByName(UWidgetBlueprint* BP, const FString& Name, const FString& ExpectClass)
{
	if (!BP || !BP->WidgetTree || Name.IsEmpty()) { return false; }
	UWidget* W = BP->WidgetTree->FindWidget(FName(*Name));
	if (!W) { return false; }
	if (!ExpectClass.IsEmpty() && W->GetClass()->GetName() != ExpectClass) { return false; }
	if (const UPanelWidget* P = Cast<UPanelWidget>(W))
	{
		if (P->GetChildrenCount() > 0) { return false; }   // поддерево — только явным разбором вызывающего
	}
	FText Err;
	const bool bOk = FWidgetBlueprintOperationUtils::RemoveWidget(BP, W, Err);
	if (!bOk) { UE_LOG(LogTemp, Warning, TEXT("[UmgBuilder] RemoveWidgetByName(%s): %s"), *Name, *Err.ToString()); }
	return bOk && BP->WidgetTree->FindWidget(FName(*Name)) == nullptr;
}

// ★ Точечное переименование узла ассета (обоснование — в .h).
bool UAsdaUmgBuilder::RenameWidgetByName(UWidgetBlueprint* BP, const FString& OldName, const FString& NewName)
{
	if (!BP || !BP->WidgetTree || OldName.IsEmpty() || NewName.IsEmpty()) { return false; }
	UWidget* W = BP->WidgetTree->FindWidget(FName(*OldName));
	if (!W || BP->WidgetTree->FindWidget(FName(*NewName))) { return false; }
	if (!FWidgetBlueprintOperationUtils::RenameWidget(BP, W, NewName)) { return false; }
	W->SetDisplayLabel(FString());   // импортёр меток не ставит — держим ассет таким же
	return W->GetName() == NewName;
}

// ★ 30.10.2026: копия поддерева (см. объявление). Слот канваса копируется полностью (анкоры, смещения, выравнивание, авторазмер, z).
static UWidget* AsdaDupWidgetRec(UWidgetBlueprint* BP, UWidget* Src, UPanelWidget* Parent, const FString& Suffix, const FVector2D* RootPos, int32& Count)
{
	const FName NewName(*(Src->GetName() + Suffix));
	if (BP->WidgetTree->FindWidget(NewName)) { return nullptr; }
	UWidget* NewW = nullptr;
	if (UPanelWidget* SrcPanel = Cast<UPanelWidget>(Src))
	{
		NewW = BP->WidgetTree->ConstructWidget<UWidget>(SrcPanel->GetClass(), NewName);
		NewW->SetVisibility(Src->GetVisibility());
		NewW->SetRenderOpacity(Src->GetRenderOpacity());
	}
	else
	{
		NewW = DuplicateObject<UWidget>(Src, BP->WidgetTree, NewName);
		// ★ `Slot` у виджета — ссылка на слот РОДИТЕЛЯ оригинала (внешний объект, `DuplicateObject` его не копирует): только
		//   отвязать копию. Трогать сам слот нельзя — он принадлежит оригиналу (перенос в transient отрывал оригинал от родителя).
		if (NewW) { NewW->Slot = nullptr; }
	}
	if (!NewW) { return nullptr; }
	UPanelSlot* NewSlot = Parent->AddChild(NewW);
	if (UCanvasPanelSlot* DS = Cast<UCanvasPanelSlot>(NewSlot))
	{
		if (const UCanvasPanelSlot* SS = Cast<UCanvasPanelSlot>(Src->Slot))
		{
			DS->SetLayout(SS->GetLayout());
			DS->SetAutoSize(SS->GetAutoSize());
			DS->SetZOrder(SS->GetZOrder());
		}
		if (RootPos) { DS->SetPosition(*RootPos); }
	}
	BP->OnVariableAdded(NewName);
	++Count;
	if (UPanelWidget* SrcPanel = Cast<UPanelWidget>(Src))
	{
		UPanelWidget* NewPanel = Cast<UPanelWidget>(NewW);
		for (int32 I = 0; I < SrcPanel->GetChildrenCount(); ++I)
		{
			if (UWidget* C = SrcPanel->GetChildAt(I)) { AsdaDupWidgetRec(BP, C, NewPanel, Suffix, nullptr, Count); }
		}
	}
	return NewW;
}

int32 UAsdaUmgBuilder::DuplicateSubtree(UWidgetBlueprint* BP, const FString& SrcName, const FString& ParentName, const FString& Suffix, FVector2D Pos)
{
	if (!BP || !BP->WidgetTree || Suffix.IsEmpty()) { return 0; }
	UWidget* Src = BP->WidgetTree->FindWidget(FName(*SrcName));
	UPanelWidget* Parent = Cast<UPanelWidget>(BP->WidgetTree->FindWidget(FName(*ParentName)));
	if (!Src || !Parent) { return 0; }
	BP->Modify();
	int32 Count = 0;
	AsdaDupWidgetRec(BP, Src, Parent, Suffix, &Pos, Count);
	return Count;
}

bool UAsdaUmgBuilder::CompileAndSave(UWidgetBlueprint* BP)
{
	if (!BP) { return false; }
	FKismetEditorUtilities::CompileBlueprint(BP);
	BP->MarkPackageDirty();
	return true;   // сохранение делает Python (unreal.EditorAssetLibrary.save_loaded_asset)
}

// ★ Массовая правка шрифта в существующем окне (ручные правки раскладки не трогаем).
int32 UAsdaUmgBuilder::RestyleTextFonts(UWidgetBlueprint* BP, int32 FontSize, FName Typeface)
{
	if (!BP || !BP->WidgetTree || FontSize <= 0) { return 0; }
	TArray<UWidget*> AllW;
	BP->WidgetTree->GetAllWidgets(AllW);
	int32 Changed = 0;
	auto Fix = [&](FSlateFontInfo& F) -> bool
	{
		if (F.Size == FontSize && F.TypefaceFontName == Typeface) { return false; }
		F.Size = FontSize;
		F.TypefaceFontName = Typeface;
		return true;
	};
	for (UWidget* W : AllW)
	{
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			FSlateFontInfo F = T->GetFont();
			if (Fix(F)) { T->SetFont(F); ++Changed; }
		}
		else if (UEditableText* E = Cast<UEditableText>(W))
		{
			// У UEditableText шрифт живёт в стиле виджета (WidgetStyle.Font).
			FEditableTextStyle St = E->WidgetStyle;
			if (Fix(St.Font)) { E->WidgetStyle = St; E->SynchronizeProperties(); ++Changed; }
		}
	}
	if (Changed > 0)
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
		BP->MarkPackageDirty();
	}
	return Changed;
}

// ★ Чёрная обводка текста (как в оригинале у над-головных плашек).
int32 UAsdaUmgBuilder::ApplyTextOutline(UWidgetBlueprint* BP, int32 OutlineSize, FLinearColor OutlineColor)
{
	if (!BP || !BP->WidgetTree) { return 0; }
	TArray<UWidget*> AllW;
	BP->WidgetTree->GetAllWidgets(AllW);
	int32 Changed = 0;
	auto FixOutline = [&](FSlateFontInfo& F) -> bool
	{
		if (F.OutlineSettings.OutlineSize == OutlineSize &&
			F.OutlineSettings.OutlineColor == OutlineColor) { return false; }
		F.OutlineSettings.OutlineSize  = OutlineSize;
		F.OutlineSettings.OutlineColor = OutlineColor;
		// Обводка рисуется и под полупрозрачным текстом — как контур в оригинале.
		F.OutlineSettings.bSeparateFillAlpha = false;
		return true;
	};
	for (UWidget* W : AllW)
	{
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			FSlateFontInfo F = T->GetFont();
			if (FixOutline(F)) { T->SetFont(F); ++Changed; }
		}
		else if (UEditableText* E = Cast<UEditableText>(W))
		{
			FEditableTextStyle St = E->WidgetStyle;
			if (FixOutline(St.Font)) { E->WidgetStyle = St; E->SynchronizeProperties(); ++Changed; }
		}
	}
	if (Changed > 0)
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
		BP->MarkPackageDirty();
	}
	return Changed;
}

// ★★★ ЦВЕТ ТЕКСТА ИЗ `EXS TextColor` (см. объявление в заголовке — там же разбор грабли).
int32 UAsdaUmgBuilder::ApplyJuiTextColors(UWidgetBlueprint* BP, const TArray<FString>& Names,
	const TArray<FLinearColor>& Colors, bool bOnlyIfPlaceholder, FLinearColor Placeholder)
{
	if (!BP || !BP->WidgetTree || Names.Num() == 0 || Names.Num() != Colors.Num()) { return 0; }
	TMap<FString, FLinearColor> Want;
	Want.Reserve(Names.Num());
	for (int32 i = 0; i < Names.Num(); ++i) { Want.Add(Names[i], Colors[i]); }

	TArray<UWidget*> AllW;
	BP->WidgetTree->GetAllWidgets(AllW);
	int32 Changed = 0;
	for (UWidget* W : AllW)
	{
		UTextBlock* T = Cast<UTextBlock>(W);
		if (!T) { continue; }
		// Имя виджета может нести суффикс уникализации импортёра (`_0`, `_1`), поэтому сверяем
		//   и точное имя, и имя без хвостового `_<цифры>`.
		FString N = T->GetName();
		const FLinearColor* Found = Want.Find(N);
		if (!Found)
		{
			int32 Us = INDEX_NONE;
			if (N.FindLastChar(TEXT('_'), Us) && Us > 0 && Us + 1 < N.Len())
			{
				const FString Tail = N.Mid(Us + 1);
				if (Tail.IsNumeric()) { Found = Want.Find(N.Left(Us)); }
			}
		}
		if (!Found) { continue; }
		const FLinearColor Cur = T->GetColorAndOpacity().GetSpecifiedColor();
		if (bOnlyIfPlaceholder && !Cur.Equals(Placeholder, 1.f / 512.f)) { continue; }
		if (Cur.Equals(*Found, 1.f / 512.f)) { continue; }
		T->SetColorAndOpacity(FSlateColor(*Found));
		++Changed;
	}
	if (Changed > 0)
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
		BP->MarkPackageDirty();
	}
	return Changed;
}

// ★ ПОЛНЫЙ СТРУКТУРНЫЙ РАЗБОР ОКНА ИЗ HEADLESS.
FString UAsdaUmgBuilder::DumpWidgetTree(UWidgetBlueprint* BP)
{
	if (!BP || !BP->WidgetTree) { return TEXT("(нет WidgetTree)"); }
	TArray<UWidget*> AllW;
	BP->WidgetTree->GetAllWidgets(AllW);
	FString S = FString::Printf(TEXT("=== %s: узлов %d ==="), *BP->GetName(), AllW.Num());
	for (const UWidget* W : AllW)
	{
		if (!W) { continue; }
		FString Slot = TEXT("слот: нет");
		if (const UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(W->Slot))
		{
			const FAnchors A = CS->GetAnchors();
			const FMargin  O = CS->GetOffsets();
			const bool bSpan = !FMath::IsNearlyEqual(A.Minimum.X, A.Maximum.X, 1e-4f)
			                || !FMath::IsNearlyEqual(A.Minimum.Y, A.Maximum.Y, 1e-4f);
			Slot = FString::Printf(
				TEXT("%s анкор [%.3f,%.3f]-[%.3f,%.3f] off(L%.1f T%.1f R%.1f B%.1f) align(%.2f,%.2f) auto=%d z=%d"),
				bSpan ? TEXT("SPAN ") : TEXT("POINT"), A.Minimum.X, A.Minimum.Y, A.Maximum.X, A.Maximum.Y,
				O.Left, O.Top, O.Right, O.Bottom, CS->GetAlignment().X, CS->GetAlignment().Y,
				CS->GetAutoSize() ? 1 : 0, CS->GetZOrder());
		}
		FString Extra;
		if (const UTextBlock* T = Cast<UTextBlock>(W))
		{
			const FLinearColor C = T->GetColorAndOpacity().GetSpecifiedColor();
			Extra = FString::Printf(TEXT(" цвет(%.4f,%.4f,%.4f,%.2f) текст«%s»"),
				C.R, C.G, C.B, C.A, *T->GetText().ToString().Left(24));
		}
		// ★★★ ВИДИМОСТЬ И UV — БЕЗ НИХ ДАМП ВРЁТ «ПРИСУТСТВИЕМ». Проверка «имя виджета есть в дампе»
		//   не отличает живой контрол от свёрнутого импортёром (`Visible = "0"` в `.JUI` даёт
		//   `Collapsed` всему поддереву). Ровно на этом прокололась проверка пяти автоматов гачи
		//   2026-08-18: имена нашлись, а полосы 3…5 на экране были пустыми плашками.
		//   UV-регион нужен по той же причине: у планов-цифр (`GOLD_roullete_FONT3_{1..5}`) сама
		//   цифра — это КУСОК АТЛАСА, и «браш есть» ещё не значит «нарисуется правильная цифра».
		const TCHAR* VisName = TEXT("?");
		switch (W->GetVisibility())
		{
			case ESlateVisibility::Visible:              VisName = TEXT("Visible");              break;
			case ESlateVisibility::Collapsed:            VisName = TEXT("Collapsed");            break;
			case ESlateVisibility::Hidden:               VisName = TEXT("Hidden");               break;
			case ESlateVisibility::HitTestInvisible:     VisName = TEXT("HitTestInvisible");     break;
			case ESlateVisibility::SelfHitTestInvisible: VisName = TEXT("SelfHitTestInvisible"); break;
			default: break;
		}
		if (const UImage* Img = Cast<UImage>(W))
		{
			const FSlateBrush& B = Img->GetBrush();
			const FBox2f Uv = B.GetUVRegion();
			const FLinearColor Tint = Img->GetColorAndOpacity();
			Extra += FString::Printf(TEXT(" браш=%s uv[%.4f %.4f]-[%.4f %.4f] тинт(%.3f,%.3f,%.3f,%.3f)"),
				*GetNameSafe(B.GetResourceObject()), Uv.Min.X, Uv.Min.Y, Uv.Max.X, Uv.Max.Y,
				Tint.R, Tint.G, Tint.B, Tint.A);
		}
		S += FString::Printf(TEXT("\n  %-46s %-24s род=%-30s вид=%-21s %s%s"),
			*W->GetName(), *W->GetClass()->GetName(),
			W->GetParent() ? *W->GetParent()->GetName() : TEXT("—"), VisName, *Slot, *Extra);
	}
	return S;
}

void UAsdaUmgBuilder::ApplyGroupAnchor(UWidgetBlueprint* BP, const FString& Prefix, FVector2D AnchorFrac, FVector2D BaseSize)
{
	if (!BP || !BP->WidgetTree || BaseSize.X <= 0.f || BaseSize.Y <= 0.f) { return; }
	TArray<UWidget*> AllW;
	BP->WidgetTree->GetAllWidgets(AllW);
	for (UWidget* W : AllW)
	{
		if (!W || !W->GetName().StartsWith(Prefix)) { continue; }
		UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(W->Slot);
		if (!S) { continue; }
		// Пропустить уже переанкоренные (guard от двойного применения: BUT02 матчит и «CARASTART_BUT02», и «CARASTART»
		//   — специфичный префикс вызываем ПЕРВЫМ, общий потом пропустит уже сдвинутый). Исходный слот = анкор (0,0).
		const FAnchors A = S->GetAnchors();
		if (!A.Minimum.IsNearlyZero() || !A.Maximum.IsNearlyZero()) { continue; }
		// Текущий слот стоит point-анкором (0,0): Offsets.Left/Top = дизайн-позиция (база 1024×768),
		//   Offsets.Right/Bottom = размер (px). Переносим анкор в долю-экрана AnchorFrac, СМЕЩЕНИЕ =
		//   дизайн-позиция − AnchorFrac·База (px). Так экран_поз = AnchorFrac·РеалРазмер + Смещение:
		//   при 1024×768 даёт исходную позицию, при др. разрешении блок целиком сдвигается к своей области.
		const FMargin Cur = S->GetOffsets();          // (Left=posX, Top=posY, Right=sizeX, Bottom=sizeY)
		const float NewL = Cur.Left - AnchorFrac.X * BaseSize.X;
		const float NewT = Cur.Top  - AnchorFrac.Y * BaseSize.Y;
		S->SetAnchors(FAnchors(AnchorFrac.X, AnchorFrac.Y, AnchorFrac.X, AnchorFrac.Y));   // point-анкор в доле экрана
		S->SetAlignment(FVector2D(0.f, 0.f));
		S->SetOffsets(FMargin(NewL, NewT, Cur.Right, Cur.Bottom));   // размер (Right/Bottom) сохраняем
	}
}
#endif // WITH_EDITOR
