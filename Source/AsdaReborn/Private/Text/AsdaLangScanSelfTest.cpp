// Copyright (c) 2026 @sitxovski (Discord)
// ★ [ПОРТ] 09.10.2026 (мультиязычность, этап 8 плана `Reports/owner_1009/multilang_plan.md`): СКАНЕР «КИРИЛЛИЦА НА ЭКРАНЕ»
//   и ПЕРЕПОЛНЕНИЯ ПОДПИСЕЙ. Путь игрока (-game): каждое окно `/Game/AsdaAssets/UI/**/WBP_*` (кроме Unused/Temp и окон
//   логина — их показывает своя сцена) открывается так же, как его открывает игра (`UAsdaWindowSubsystem::OpenWindowByPath`),
//   после отрисовки читаются ВСЕ видимые TextBlock:
//     • в нерусской сессии текст с кириллицей = непереведённая подпись (окно, узел, текст);
//     • желаемая ширина текста больше отведённой узлу (`GetDesiredSize` против `GetCachedGeometry`) = переполнение.
//   Это СТАТИКА окон (то, что лежит в ассете или ставит код при открытии без данных); динамические строки (имена, числа,
//   ответы сервера) появляются только в живой игре.
//   ★ 09.10.2026 (ревизия партии владельца, СРЕДН-2, `Reports/owner_1009/multilang_owner_fixes.md`): кроме «поле узла
//   меньше строки» сканер ищет НАЕЗДЫ — видимую часть строки (по выравниванию узла: лево/центр/право, высота — по числу
//   строк) сверяет с соседями того же окна:
//     • ПЕРЕП-КНОПКА — подпись вылезает за кнопку, в которой стоит (у JUI поле подписи бывает шире самой кнопки);
//     • НАЕЗД-ЗНАЧОК — подпись заходит под значок ≤ 24×24 той же кнопки (прежний скан этого не видел: поле подписи
//       начинается у края кнопки, значок рисуется поверх);
//     • НАЕЗД-КНОПКА — строка (в т.ч. перенесённая) заходит на чужую кнопку;
//     • НАЕЗД-ТЕКСТ — видимые строки двух узлов пересекаются.
//   ★ 10.10.2026 (волна «наезды текста», `Reports/owner_1010/overlaps.md`; рецепт ревизии
//   `owner_1009/multilang_owner_fixes_REVIEW.md` СРЕДН-1, МЕЛК-4): без ложных — значок только свой (внутри кнопки), флажок
//   UCheckBox ≤ 24×24 = значок (НАЕЗД-ФЛАЖОК), допуск 3 px у маленьких чужих кнопок, ПЕРЕКРЫТО (закрыто непрозрачной
//   картинкой, нарисованной позже — стопки страниц) и ВАРИАНТ (одно поле в одном родителе) — служебные, не наезды; дубли
//   по ключу (окно, вид, узел, сосед); ПЕРЕП-ПОЛЕ — видимая часть строки выходит за свою подложку. Итог дополнен
//   «служебных S». ЗАГЛУШКА — находка строки-образца, которую код всегда заменяет данными (список
//   `Tools/data/ui_scan_placeholders.tsv`), тоже служебная; там же ВЗАИМОИСКЛ (пары узлов разных панелей окна) и
//   ОКНО-ВНЕ-ИГРЫ (окно, которое игра не открывает) — служебные ВАРИАНТ и ВНЕ-ИГРЫ. НАЕЗД-ПЛАШКА — строка частично уходит
//   под непрозрачную картинку, нарисованную позже. «Закрывает»/«подложка» решаются по АЛЬФЕ ПИКСЕЛЕЙ исходника текстуры
//   (прозрачные куски атласа не закрывают). Строка с обрезкой `ClipToBounds` — не шире своего поля.
//   Команда: `asda.Lang.ScanScreen [quit] [filter=<подстрока пути>] [measure]`; итог — строка «СКАН-ЯЗЫК ИТОГ: окон N,
//   кириллица K (в узлах M), переполнений P, наездов Q, служебных S»; построчно — «СКАН-ЯЗЫК КИР|ПЕРЕП|ПЕРЕП-КНОПКА|
//   ПЕРЕП-ПОЛЕ|НАЕЗД-*|ПЕРЕКРЫТО|ВАРИАНТ|ЗАГЛУШКА|ВНЕ-ИГРЫ <окно> <узел> …»; `measure` — «СКАН-ЯЗЫК МЕРА» с желаемым
//   размером каждой видимой строки (данные для правок разметки).
//   ★ 10.10.2026 (ревизия волны, СРЕДН-3, `Reports/owner_1010/overlaps_fixes.md`): скан мерил ОБРАЗЕЦ ассета там, где
//   строку ставит код (в EN — русский образец «Текущий лимит членов клана» вместо кодовой «■ Currently available clan
//   member openings»), и пометка ЗАГЛУШКА у соседа гасила находку. Теперь:
//     • у строк-образцов, которые код заменяет loc-строкой (столбец `loc` таблицы заглушек), скан ПОДСТАВЛЯЕТ эту строку
//       текущего языка (`AsdaNotice::Loc`) сразу после открытия окна и мерит её как обычную — такие узлы уже не заглушки;
//     • ЗАГЛУШКА остаётся только у строк ДАННЫХ (имена, звания, описания — замерить нечего); её находка служебная, только
//       если в ней участвует сама строка-образец. Находка «строка × поле соседа-заглушки» служебная, только если строка
//       НЕ заходит на поле соседа (данные в нём лягут в это поле);
//     • `live` — проверка окон, уже открытых игрой (через свои подсистемы: `asda.Clan.IncreaseSelfTest show`,
//       `asda.EquipShop.Open` и т. п.), со строками, которые поставил код; ничего не открывает и не закрывает, таблица
//       заглушек не применяется (на экране — то, что видит игрок). Метка прогона — `tag=<имя>`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"            // ★ 10.10.2026: loc-строка текущего языка для подстановки в образцы (СРЕДН-3)
#include "UObject/UObjectIterator.h"  // ★ 10.10.2026: режим `live` — окна, открытые игрой
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AssetRegistry/AssetRegistryModule.h"
#include "UI/AsdaUiWbpCatalog.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход): общий разбор ассета окна (класс WBP_<id>_C → окно WBP_<id>)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLangScan, Log, All);

namespace AsdaLangScan
{
	// Текст содержит кириллицу (U+0400…U+04FF).
	static bool HasCyrillic(const FString& S)
	{
		for (TCHAR C : S) { if (C >= 0x0400 && C <= 0x04FF) { return true; } }
		return false;
	}

	// Состояние прохода: список окон, позиция, счётчики (живёт до конца прохода).
	struct FState
	{
		TArray<FString> Paths;          // объектные пути классов окон
		int32 Pos = 0;                  // текущее окно
		int32 Windows = 0;              // открыто окон
		int32 CyrWindows = 0;           // окон с кириллицей
		int32 CyrNodes = 0;             // узлов с кириллицей
		int32 Overflow = 0;             // переполнений
		int32 Overlap = 0;              // наездов на соседние контролы (кнопка/значок/флажок/чужая кнопка/чужой текст/подложка)
		int32 Service = 0;              // ★ 10.10.2026: служебных находок (ПЕРЕКРЫТО, ВАРИАНТ) — не дефекты
		bool bQuit = false;             // выйти по окончании
		bool bCheckCyr = true;          // искать кириллицу (не в русской сессии)
		bool bMeasure = false;          // ★ 10.10.2026: печатать желаемый размер КАЖДОЙ видимой строки (МЕРА) — данные для правок
		bool bLive = false;             // ★ 10.10.2026 (СРЕДН-3): проверить окна, открытые игрой (без открытия/закрытия и без таблицы заглушек)
		FString Tag;                    // ★ 10.10.2026: метка прогона `live` (в итоговой строке)
		TArray<FString> Expect;         // ★ 10.10.2026 (ревизия overlaps_fixes СРЕДН-1): `expect=WBP_a,WBP_b` — окна, которые состояние `live` обязано показать
		TArray<FString> Seen;           // ★ 10.10.2026: имена окон, проверенных в режиме `live`
		int32 Substituted = 0;          // ★ 10.10.2026: строк-образцов, заменённых loc-строкой кода (СРЕДН-3)
	};

	// Прямоугольник в абсолютных пикселях экрана (L, T, R, B).
	struct FRectPx
	{
		float L = 0.f, T = 0.f, R = 0.f, B = 0.f;   // края
		// Пересечение с `O` больше допуска по обеим осям (касание краями и сглаживание — не наезд).
		bool Overlaps(const FRectPx& O, float TolX, float TolY) const
		{
			return FMath::Min(R, O.R) - FMath::Max(L, O.L) > TolX && FMath::Min(B, O.B) - FMath::Max(T, O.T) > TolY;
		}
		// Содержит точку.
		bool Contains(float X, float Y) const { return X >= L && X <= R && Y >= T && Y <= B; }
		// Площадь (для выбора самой тесной кнопки-контейнера).
		float Area() const { return (R - L) * (B - T); }
	};

	// Прямоугольник отрисованного виджета по кэшированной геометрии; false — не отрисован (размер ≈ 0).
	static bool AbsRect(const UWidget* W, FRectPx& Out)
	{
		const FGeometry G = W->GetCachedGeometry();
		const FVector2D P = G.GetAbsolutePosition();
		const FVector2D Sz = G.GetAbsoluteSize();
		if (Sz.X <= 1.f || Sz.Y <= 1.f) { return false; }
		Out.L = float(P.X); Out.T = float(P.Y); Out.R = float(P.X + Sz.X); Out.B = float(P.Y + Sz.Y);
		return true;
	}

	// ★ 10.10.2026 (живой скан): видимая область узла с учётом предков с обрезкой (`ClipToBounds`, прокрутки, списки
	//   привязок настроек). false — узел целиком вне области предка (на экране его нет); иначе `Out` — область обрезки
	//   (или весь экран, если обрезающих предков нет). Без этого строка ряда, прокрученного за край списка, давала наезд на
	//   кнопку под списком («Down» × «По умолчанию» в настройках клавиш).
	static bool AncestorClip(const UWidget* W, const FRectPx& R, FRectPx& Out)
	{
		Out.L = -1.e6f; Out.T = -1.e6f; Out.R = 1.e6f; Out.B = 1.e6f;
		for (const UWidget* P = W ? W->GetParent() : nullptr; P; P = P->GetParent())
		{
			const EWidgetClipping C = P->GetClipping();
			if (C != EWidgetClipping::ClipToBounds && C != EWidgetClipping::ClipToBoundsAlways) { continue; }
			FRectPx PR;
			if (!AbsRect(P, PR)) { return false; }
			Out.L = FMath::Max(Out.L, PR.L); Out.T = FMath::Max(Out.T, PR.T);
			Out.R = FMath::Min(Out.R, PR.R); Out.B = FMath::Min(Out.B, PR.B);
		}
		return FMath::Min(R.R, Out.R) - FMath::Max(R.L, Out.L) > 1.f && FMath::Min(R.B, Out.B) - FMath::Max(R.T, Out.T) > 1.f;
	}

	// ★ 10.10.2026 (живой скан): рисует ли кнопка что-нибудь сама (кисть стиля «обычное»). Кнопки JUI прозрачны и видны
	//   своими планами; кнопка без планов и без кисти — невидимая зона клика (`BoosterShop_HIT20` над подписью «Сейчас GXP»).
	static bool ButtonDrawsItself(const UWidget* W)
	{
		const UButton* B = Cast<UButton>(W);
		if (!B) { return true; }   // флажок-кнопка — считаем видимой, как раньше
		const FSlateBrush& N = B->GetStyle().Normal;
		return N.DrawAs != ESlateBrushDrawType::NoDrawType && N.TintColor.GetSpecifiedColor().A > 0.01f && N.GetResourceObject() != nullptr;
	}

	// Выравнивание строки узла (`Justification` у UTextLayoutWidget защищённое — читаем свойство отражением).
	static ETextJustify::Type Justify(const UTextBlock* T)
	{
		static const FProperty* P = UTextLayoutWidget::StaticClass()->FindPropertyByName(TEXT("Justification"));
		return P ? (ETextJustify::Type)*P->ContainerPtrToValuePtr<uint8>(T) : ETextJustify::Left;
	}

	// Видимая часть строки узла в абсолютных пикселях: ширина — желаемая (с переносом — не шире поля), положение — по
	//   выравниванию узла; верх — верх поля, высота — желаемая (все строки).
	static FRectPx InkRect(const UTextBlock* T, const FRectPx& Box, const FVector2D& Have, const FVector2D& Want)
	{
		const float Px = (Box.R - Box.L) / float(FMath::Max(Have.X, 1.0));   // пикселей экрана на единицу узла
		const bool bWrap = T->GetAutoWrapText() || T->GetWrapTextAt() > 0.f;
		const float W = float(bWrap ? FMath::Min(Want.X, Have.X) : Want.X) * Px;   // ширина видимой части
		FRectPx I;
		switch (Justify(T))
		{
		case ETextJustify::Center: I.L = (Box.L + Box.R - W) * 0.5f; break;
		case ETextJustify::Right:  I.L = Box.R - W; break;
		default:                   I.L = Box.L; break;
		}
		I.R = I.L + W;
		// ★ 10.10.2026: обрезка по краю поля (`ClipToBounds` у самой строки) — видимая часть не шире поля
		if (T->GetClipping() == EWidgetClipping::ClipToBounds)
		{
			I.L = FMath::Max(I.L, Box.L);
			I.R = FMath::Min(I.R, Box.R);
		}
		I.T = Box.T;
		I.B = Box.T + float(Want.Y) * Px;
		return I;
	}

	// Картинка видна глазу: есть ресурс кисти, не прозрачна (служебные `*_HIT`/`*_DRAG` — прозрачные зоны клика и ручки
	//   перетаскивания — не в счёт).
	static bool IsVisibleImage(const UImage* Im)
	{
		if (!Im || !Im->IsVisible() || Im->GetRenderOpacity() <= 0.01f || Im->GetColorAndOpacity().A <= 0.01f) { return false; }
		if (Im->GetName().EndsWith(TEXT("_HIT")) || Im->GetName().EndsWith(TEXT("_DRAG"))) { return false; }
		const FSlateBrush& Br = Im->GetBrush();
		return Br.GetResourceObject() != nullptr && Br.DrawAs != ESlateBrushDrawType::NoDrawType
			&& Br.TintColor.GetSpecifiedColor().A > 0.01f;
	}

	// ★ 10.10.2026 (волна «наезды текста», рецепт ревизии `owner_1009/multilang_owner_fixes_REVIEW.md` СРЕДН-1/МЕЛК-4):
	//   картинка НЕПРОЗРАЧНА целиком (может закрыть то, что нарисовано раньше).
	static bool IsOpaqueImage(const UImage* Im)
	{
		return IsVisibleImage(Im) && Im->GetRenderOpacity() >= 0.9f && Im->GetColorAndOpacity().A >= 0.9f
			&& Im->GetBrush().TintColor.GetSpecifiedColor().A >= 0.9f;
	}

	// ★ 10.10.2026: виден весь путь до корня (свёрнутый/скрытый предок прячет узел, хотя у самого узла вид «виден»).
	static bool ChainVisible(const UWidget* W)
	{
		for (const UWidget* P = W; P; P = P->GetParent())
		{
			if (!P->IsVisible()) { return false; }
		}
		return true;
	}

	// ★ 10.10.2026: порядок ОТРИСОВКИ узлов окна (обход в глубину; у канваса дети по ZOrder слота, при равном — по
	//   списку, память umg-canvas-draw-order). Больше номер — рисуется позже (поверх).
	static void PaintOrder(UWidget* W, int32& Idx, TMap<const UWidget*, int32>& Out)
	{
		if (!W) { return; }
		Out.Add(W, Idx++);
		UPanelWidget* P = Cast<UPanelWidget>(W);
		if (!P) { return; }
		TArray<UWidget*> Ch;   // дети в порядке отрисовки
		for (int32 i = 0; i < P->GetChildrenCount(); ++i) { Ch.Add(P->GetChildAt(i)); }
		if (Cast<UCanvasPanel>(P))
		{
			Ch.StableSort([](const UWidget& A, const UWidget& B)
			{
				const UCanvasPanelSlot* SA = Cast<UCanvasPanelSlot>(A.Slot);
				const UCanvasPanelSlot* SB = Cast<UCanvasPanelSlot>(B.Slot);
				return (SA ? SA->GetZOrder() : 0) < (SB ? SB->GetZOrder() : 0);
			});
		}
		for (UWidget* C : Ch) { PaintOrder(C, Idx, Out); }
	}

	// Видимая картинка окна: узел, прямоугольник экрана, размер в единицах узла, номер отрисовки.
	struct FImg { UImage* Im; FRectPx R; FVector2D Local; int32 Paint; };

	// ★ 10.10.2026: альфа ПИКСЕЛЯ картинки в точке экрана (X, Y) — 0…1. Кусок атласа JUI бывает прозрачным целиком или
	//   местами (ручка-шапка CAMPWAR_Time_Move 136×40 нарисована поверх строк, а строки видны), поэтому «закрывает» и
	//   «подложка» решаются по пикселям исходника текстуры (прогоны идут редактором в режиме -game — исходник доступен).
	//   Регион UV кисти учитывается; поля девятисреза — нет (точность до куска). Нет исходника/формат не BGRA8 — 1 (как
	//   прежде: «картинка непрозрачна»). Кэш исходников — на проход окна.
	static float AlphaAtBrush(const FSlateBrush& Br, float Tint, const FRectPx& Rect, float X, float Y, TMap<const UTexture2D*, TArray64<uint8>>& Cache);
	static float AlphaAt(const FImg& I, float X, float Y, TMap<const UTexture2D*, TArray64<uint8>>& Cache)
	{
		const FSlateBrush& Br = I.Im->GetBrush();
		return AlphaAtBrush(Br, Br.TintColor.GetSpecifiedColor().A * I.Im->GetColorAndOpacity().A, I.R, X, Y, Cache);
	}

	// ★ 10.10.2026 (ревизия overlaps_fixes СРЕДН-1): то же для любой кисти (`Br`, общая прозрачность `Tint`), растянутой
	//   на прямоугольник экрана `Rect` — нужно и картинкам, и собственной кисти кнопки (флажок `EXCHANGER_ALLCHK`).
	static float AlphaAtBrush(const FSlateBrush& Br, float Tint, const FRectPx& Rect, float X, float Y, TMap<const UTexture2D*, TArray64<uint8>>& Cache)
	{
		const FImg I{ nullptr, Rect, FVector2D::ZeroVector, 0 };   // прямоугольник кисти на экране
#if WITH_EDITORONLY_DATA
		const UTexture2D* Tex = Cast<UTexture2D>(Br.GetResourceObject());
		if (!Tex || !Tex->Source.IsValid() || Tex->Source.GetFormat() != TSF_BGRA8) { return Tint; }
		TArray64<uint8>* Data = Cache.Find(Tex);
		if (!Data)
		{
			Data = &Cache.Add(Tex);
			const_cast<UTexture2D*>(Tex)->Source.GetMipData(*Data, 0);
		}
		const int64 Sx = Tex->Source.GetSizeX(), Sy = Tex->Source.GetSizeY();
		if (Data->Num() < Sx * Sy * 4 || I.R.R - I.R.L <= 0.f || I.R.B - I.R.T <= 0.f) { return Tint; }
		FBox2f UV = Br.GetUVRegion();
		if (!UV.bIsValid) { UV = FBox2f(FVector2f(0.f, 0.f), FVector2f(1.f, 1.f)); }
		const float U = UV.Min.X + (X - I.R.L) / (I.R.R - I.R.L) * (UV.Max.X - UV.Min.X);
		const float V = UV.Min.Y + (Y - I.R.T) / (I.R.B - I.R.T) * (UV.Max.Y - UV.Min.Y);
		const int64 Px = FMath::Clamp<int64>(int64(U * Sx), 0, Sx - 1), Py = FMath::Clamp<int64>(int64(V * Sy), 0, Sy - 1);
		return Tint * float((*Data)[(Py * Sx + Px) * 4 + 3]) / 255.f;
#else
		return Tint;
#endif
	}

	// ★ 10.10.2026 (ревизия overlaps_fixes СРЕДН-1): НЕПРОЗРАЧНАЯ часть кисти `Br` на прямоугольнике `Rect` — рамка точек с
	//   альфой ≥ 0.5 (выборка с шагом 1 px экрана). Кусок атласа маленькой кнопки-флажка (`BigBox_Check` 31×37) прозрачен
	//   почти целиком — видимый квадрат 14×14, а прямоугольник клика выше и шире; наезд меряется по видимому. Только кисть
	//   «картинкой» (не девятисрез: у него поля тянутся иначе). false — непрозрачных точек нет или текстуры не прочесть
	//   (тогда видимым остаётся весь прямоугольник, как раньше).
	static bool OpaqueBox(const FSlateBrush& Br, float Tint, const FRectPx& Rect, FRectPx& Out, TMap<const UTexture2D*, TArray64<uint8>>& Cache)
	{
		if (Br.DrawAs != ESlateBrushDrawType::Image || !Cast<UTexture2D>(Br.GetResourceObject())) { return false; }
		bool bAny = false;   // нашлась хоть одна непрозрачная точка
		for (float Y = Rect.T + 0.5f; Y < Rect.B; Y += 1.f)
		{
			for (float X = Rect.L + 0.5f; X < Rect.R; X += 1.f)
			{
				if (AlphaAtBrush(Br, Tint, Rect, X, Y, Cache) < 0.5f) { continue; }
				if (!bAny) { Out.L = X - 0.5f; Out.R = X + 0.5f; Out.T = Y - 0.5f; Out.B = Y + 0.5f; bAny = true; }
				Out.L = FMath::Min(Out.L, X - 0.5f); Out.R = FMath::Max(Out.R, X + 0.5f);
				Out.T = FMath::Min(Out.T, Y - 0.5f); Out.B = FMath::Max(Out.B, Y + 0.5f);
			}
		}
		return bAny;
	}

	// ★ 10.10.2026: прямоугольник ПОДЛОЖКИ строки — видимой картинки (не значка), на которой стоит начало строки
	//   (точка привязки по выравниванию; высота ≥ 10 px и ширина ≥ половины поля строки — узкие планки и столбики
	//   оформления подложкой не считаются), расширенной по своей полосе: по X — соседними кусками с тем же верхом и низом
	//   (девятисрез, шапка из трёх кусков), по Y — соседними с теми же левым и правым краями. Нет подложки — false.
	static bool PlateRect(const TArray<FImg>& Imgs, float Px, float Py, int32 TextPaint, float FieldW, FRectPx& Out,
		TMap<const UTexture2D*, TArray64<uint8>>& Cache)
	{
		int32 Best = INDEX_NONE;
		for (int32 k = 0; k < Imgs.Num(); ++k)
		{
			const FImg& I = Imgs[k];
			if (I.Paint > TextPaint || (I.Local.X <= 24.f && I.Local.Y <= 24.f)) { continue; }   // подложка — под строкой, не значок
			if (!I.R.Contains(Px, Py) || I.R.B - I.R.T < 10.f || I.R.R - I.R.L < 0.5f * FieldW) { continue; }
			if (AlphaAt(I, Px, Py, Cache) < 0.5f) { continue; }   // в точке привязки картинка прозрачна — не подложка
			if (Best == INDEX_NONE || I.R.Area() < Imgs[Best].R.Area()) { Best = k; }
		}
		if (Best == INDEX_NONE) { return false; }
		FRectPx H = Imgs[Best].R, V = Imgs[Best].R;   // полоса по X и по Y
		for (bool bGrew = true; bGrew; )
		{
			bGrew = false;
			for (const FImg& I : Imgs)
			{
				if (FMath::Abs(I.R.T - H.T) <= 1.f && FMath::Abs(I.R.B - H.B) <= 1.f)
				{
					if (I.R.L < H.L - 0.5f && I.R.R >= H.L - 1.f) { H.L = I.R.L; bGrew = true; }
					if (I.R.R > H.R + 0.5f && I.R.L <= H.R + 1.f) { H.R = I.R.R; bGrew = true; }
				}
				if (FMath::Abs(I.R.L - V.L) <= 1.f && FMath::Abs(I.R.R - V.R) <= 1.f)
				{
					if (I.R.T < V.T - 0.5f && I.R.B >= V.T - 1.f) { V.T = I.R.T; bGrew = true; }
					if (I.R.B > V.B + 0.5f && I.R.T <= V.B + 1.f) { V.B = I.R.B; bGrew = true; }
				}
			}
		}
		Out.L = H.L; Out.R = H.R; Out.T = V.T; Out.B = V.B;
		return true;
	}

	// ★ 10.10.2026: ЗАГЛУШКИ — строки-образцы разметки JUI («Название …», «999,999~», «Имя персонажа до 10 символов»), которые
	//   код окна ВСЕГДА заменяет данными при показе. Скан открывает окно без данных, поэтому их наезды — не то, что видит
	//   игрок (служебный вид ЗАГЛУШКА). Там же ВЗАИМОИСКЛ — пары узлов разных панелей/режимов окна, которые код никогда не
	//   показывает вместе (панели аукциона, объявление в 1/2 строки): их наезд — служебный ВАРИАНТ. Список —
	//   `Tools/data/ui_scan_placeholders.tsv` (окно, узел, вид, сосед, где это делает код), каждая строка подтверждена
	//   кодом; ключи «WBP_окно|узел» и «WBP_окно|узел|сосед». ОКНО-ВНЕ-ИГРЫ — окно, которое игра не открывает (ссылок в
	//   срезе нет, его место заняло другое окно): все его находки — служебный вид ВНЕ-ИГРЫ (ключ «WBP_окно|*»).
	static TSet<FString> GPlaceholders;
	// ★ 10.10.2026 (СРЕДН-3): «WBP_окно|узел» → loc-id строки, которую код ставит в этот образец (столбец 6 `loc` таблицы).
	//   Такие узлы — не заглушки: скан подставляет строку текущего языка и мерит её.
	static TMap<FString, int32> GStubLoc;
	// ★ 10.10.2026 (СРЕДН-3): режим `live` — таблица заглушек не применяется (на экране строки, поставленные кодом).
	static bool GLive = false;
	static void LoadPlaceholders()
	{
		GPlaceholders.Reset();
		GStubLoc.Reset();
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/data/ui_scan_placeholders.tsv")));
		for (const FString& L : Lines)
		{
			TArray<FString> C;
			L.ParseIntoArray(C, TEXT("\t"), false);
			if (C.Num() < 3 || !C[0].StartsWith(TEXT("WBP_"))) { continue; }
			if (C[2] == TEXT("ЗАГЛУШКА"))
			{
				const int32 LocId = C.Num() >= 6 ? FCString::Atoi(*C[5]) : 0;   // столбец `loc`: «-» / пусто — строка данных
				if (LocId > 0) { GStubLoc.Add(C[0] + TEXT("|") + C[1], LocId); }   // строка кода — подставляется и мерится
				else { GPlaceholders.Add(C[0] + TEXT("|") + C[1]); }               // строка данных — служебная ЗАГЛУШКА
			}
			else if (C[2] == TEXT("ОКНО-ВНЕ-ИГРЫ")) { GPlaceholders.Add(C[0] + TEXT("|*")); }   // окно игра не открывает
			else if (C[2] == TEXT("ВЗАИМОИСКЛ") && C.Num() >= 4)   // пара узлов, которые код никогда не показывает вместе
			{
				GPlaceholders.Add(C[0] + TEXT("|") + C[1] + TEXT("|") + C[3]);
				GPlaceholders.Add(C[0] + TEXT("|") + C[3] + TEXT("|") + C[1]);
			}
		}
		UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК: заглушек в списке %d, строк кода к подстановке %d"), GPlaceholders.Num(), GStubLoc.Num());
	}

	// ★ 10.10.2026 (СРЕДН-3): заменить в окне строки-образцы, которые код заменяет loc-строкой, на строку текущего языка.
	static int32 SubstituteCodeStrings(UUserWidget* Win, const FString& WinName)
	{
		if (!Win || !Win->WidgetTree || GStubLoc.Num() == 0) { return 0; }
		int32 Done = 0;                        // заменено строк
		TArray<UWidget*> All;                  // все узлы окна
		Win->WidgetTree->GetAllWidgets(All);
		for (UWidget* Wd : All)
		{
			UTextBlock* T = Cast<UTextBlock>(Wd);
			const int32* Id = T ? GStubLoc.Find(WinName + TEXT("|") + T->GetName()) : nullptr;
			if (!Id) { continue; }
			const FString S = AsdaNotice::Loc(Win->GetGameInstance(), *Id);   // строка текущего языка, теги срезаны
			if (S.IsEmpty()) { continue; }
			T->SetText(FText::FromString(S));
			++Done;
			UE_LOG(LogAsdaLangScan, Log, TEXT("СКАН-ЯЗЫК ПОДСТАНОВКА %s %s loc %d «%s»"), *WinName, *T->GetName(), *Id, *S.Left(60));
		}
		return Done;
	}

	// Проверка наездов в окне `Win` (см. шапку файла); возвращает число находок-дефектов, пишет строки лога.
	//   ★ 10.10.2026 — рецепт ревизии СРЕДН-1/МЕЛК-4:
	//     1) значок — только СВОЙ: картинка ≤ 24×24 целиком внутри кнопки-владельца и заметно меньше её (собственный фон
	//        маленькой кнопки и куски рамки окна `<окно>_0_N` — не значки); флажок UCheckBox ≤ 24×24 — тоже значок
	//        (НАЕЗД-ФЛАЖОК: подпись заходит под флажок);
	//     2) кнопка — по ВИДИМОЙ части (с планами-кусками), а не по прямоугольнику клика; чужая кнопка ≤ 24×24 (флажок/радиокнопка с подписью сбоку) — допуск 3 px по X (прозрачный край кисти);
	//     3) ПЕРЕКРЫТО: строку (кнопку) закрывает непрозрачная картинка, нарисованная ПОЗЖЕ (стопки страниц) — служебный
	//        вид, в наезды не идёт;
	//     4) ВАРИАНТ: две строки с одним полем в одном родителе — код показывает одну (служебный вид);
	//     5) дубли — ключ (окно, узел, вид, сосед) — одна строка;
	//     6) ПЕРЕП-ПОЛЕ — видимая часть строки выходит за свою подложку (картинку, на которой стоит её начало, с соседними
	//        кусками девятисреза), т.е. «текст вылезает за своё поле» на экране; по вертикали — только у строк в
	//        несколько строк (одна строка выше тонкой планки-подложки — норма оформления).
	static int32 CheckOverlaps(UUserWidget* Win, const FString& Path, int32& OutService)
	{
		const FString WinName = FPackageName::ObjectPathToObjectName(Path);   // имя окна для лога
		TArray<UWidget*> All;                                                 // все узлы окна
		Win->WidgetTree->GetAllWidgets(All);
		TMap<const UWidget*, int32> Paint;                                    // порядок отрисовки
		int32 PaintIdx = 0;
		PaintOrder(Win->GetRootWidget(), PaintIdx, Paint);
		const UWidget* Root = Win->GetRootWidget();
		// Видимая строка: узел, поле, видимая часть, текст, номер отрисовки.
		struct FTx { UTextBlock* T; FRectPx Box; FRectPx Ink; FString Tx; int32 Paint; bool bCovered; };
		struct FBtn { UWidget* W; FRectPx R; FVector2D Local; int32 Paint; bool bCovered; FRectPx Vis; FRectPx VisFg; };   // Vis — видимая часть с планами; VisFg — она же для ЧУЖОЙ строки (у флажка — только непрозрачная часть, ★ 10.10.2026)
		TArray<FTx> Texts;                              // видимые непустые строки
		TArray<FBtn> Buttons;                           // видимые кнопки
		TArray<FBtn> Checks;                            // видимые флажки ≤ 24×24
		TArray<FImg> Imgs;                              // все видимые картинки
		for (UWidget* Wd : All)
		{
			if (!Wd || !ChainVisible(Wd)) { continue; }
			FRectPx R;
			if (!AbsRect(Wd, R)) { continue; }
			const int32 Pi = Paint.FindRef(Wd);
			const FVector2D Local = Wd->GetCachedGeometry().GetLocalSize();
			if (UTextBlock* T = Cast<UTextBlock>(Wd))
			{
				const FString Tx = T->GetText().ToString();
				if (Tx.TrimStartAndEnd().IsEmpty() || T->GetRenderOpacity() <= 0.01f || T->GetColorAndOpacity().GetSpecifiedColor().A <= 0.01f) { continue; }
				FRectPx Ink = InkRect(T, R, Local, T->GetDesiredSize());
				// ★ 10.10.2026: обрезка предками — строка вне области прокрутки/списка на экране не видна
				FRectPx Clip;
				if (!AncestorClip(T, Ink.Area() > 1.f ? Ink : R, Clip)) { continue; }
				Ink.L = FMath::Max(Ink.L, Clip.L); Ink.T = FMath::Max(Ink.T, Clip.T); Ink.R = FMath::Min(Ink.R, Clip.R); Ink.B = FMath::Min(Ink.B, Clip.B);
				Texts.Add({ T, R, Ink, Tx, Pi, false });
			}
			else if (Cast<UButton>(Wd))
			{
				Buttons.Add({ Wd, R, Local, Pi, false, R, R });
			}
			else if (Cast<UCheckBox>(Wd))
			{
				if (Local.X <= 24.f && Local.Y <= 24.f) { Checks.Add({ Wd, R, Local, Pi, false, R, R }); }
				else { Buttons.Add({ Wd, R, Local, Pi, false, R, R }); }   // флажок-кнопка с подписью внутри — как кнопка
			}
			else if (UImage* Im = Cast<UImage>(Wd))
			{
				if (IsVisibleImage(Im)) { Imgs.Add({ Im, R, Local, Pi }); }
			}
		}
		TMap<const UTexture2D*, TArray64<uint8>> TexCache;   // исходники текстур для альфы пикселей (на окно)
		// Перекрыт ли прямоугольник `X` (нарисованный под номером `P`) непрозрачной картинкой, нарисованной позже: картинка
		//   не значок, содержит центр `X`, закрывает ≥ 70 % его площади и НЕПРОЗРАЧНА ПО ПИКСЕЛЯМ в ≥ 4 из 5 точек средней
		//   линии `X` (10…90 % ширины). Возвращает имя закрывающей или пусто.
		auto CoveredBy = [&Imgs, &TexCache](const FRectPx& X, int32 P) -> FString
		{
			const float Cx = (X.L + X.R) * 0.5f, Cy = (X.T + X.B) * 0.5f;
			const float A = FMath::Max(X.Area(), 1.f);
			for (const FImg& I : Imgs)
			{
				if (I.Paint <= P || (I.Local.X <= 24.f && I.Local.Y <= 24.f) || !IsOpaqueImage(I.Im) || !I.R.Contains(Cx, Cy)) { continue; }
				const float Ow = FMath::Min(X.R, I.R.R) - FMath::Max(X.L, I.R.L), Oh = FMath::Min(X.B, I.R.B) - FMath::Max(X.T, I.R.T);
				if (Ow <= 0.f || Oh <= 0.f || Ow * Oh < 0.7f * A) { continue; }
				int32 Opaque = 0;   // непрозрачных точек из 5
				for (int32 k = 0; k < 5; ++k)
				{
					const float Sx = X.L + (X.R - X.L) * (0.1f + 0.2f * k);
					if (I.R.Contains(Sx, Cy) && AlphaAt(I, Sx, Cy, TexCache) >= 0.5f) { ++Opaque; }
				}
				if (Opaque >= 4) { return I.Im->GetName(); }
			}
			return FString();
		};
		TSet<FString> Seen;   // ключи (окно, узел, вид, сосед) — без дублей
		int32 N = 0;          // находок-дефектов в окне
		// `FieldHit` — только для пары строк: 1 — строка-не-заглушка заходит на ПОЛЕ соседа-заглушки (данные лягут туда),
		//   0 — не заходит, −1 — не пара строк (сосед — кнопка/значок/картинка).
		auto Report = [&Seen, &N, &OutService, &WinName](const TCHAR* Kind, bool bDefect, const FString& Node, const FString& Other, const FString& Msg, int32 FieldHit = -1)
		{
			const FString Key = FString::Printf(TEXT("%s|%s|%s|%s"), *WinName, Kind, *Node, *Other);
			if (Seen.Contains(Key)) { return; }
			Seen.Add(Key);
			// находка с участием строки-заглушки (сама строка или соседняя строка) — служебная ЗАГЛУШКА
			if (GLive)   // ★ live: на экране строки кода — служебных по таблице нет
			{
				if (bDefect) { ++N; } else { ++OutService; }
				UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК %s %s %s"), Kind, *WinName, *Msg);
				return;
			}
			if (bDefect && GPlaceholders.Contains(WinName + TEXT("|*")))
			{
				++OutService;
				UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК ВНЕ-ИГРЫ %s [%s] %s"), *WinName, Kind, *Msg);
				return;
			}
			// ★ 10.10.2026 (СРЕДН-3): служебная ЗАГЛУШКА — только если в находке участвует строка-образец ДАННЫХ: сама строка, или
			//   сосед-заглушка, на ПОЛЕ которого настоящая строка не заходит (`FieldHit` = 0; данные соседа лягут в его поле).
			const bool bNodeStub  = GPlaceholders.Contains(WinName + TEXT("|") + Node);    // сама строка — образец данных
			const bool bOtherStub = GPlaceholders.Contains(WinName + TEXT("|") + Other);   // сосед — образец данных
			const bool bStub = (bNodeStub && bOtherStub) || (bNodeStub != bOtherStub && FieldHit == 0) || (bNodeStub && FieldHit < 0);
			if (bDefect && GPlaceholders.Contains(WinName + TEXT("|") + Node + TEXT("|") + Other))
			{
				++OutService;
				UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК ВАРИАНТ %s [%s] %s"), *WinName, Kind, *Msg);
				return;
			}
			if (bDefect && bStub)
			{
				++OutService;
				UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК ЗАГЛУШКА %s [%s] %s"), *WinName, Kind, *Msg);
				return;
			}
			if (bDefect) { ++N; } else { ++OutService; }
			UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК %s %s %s"), Kind, *WinName, *Msg);
		};
		for (FTx& A : Texts)
		{
			const FString By = CoveredBy(A.Ink.Area() > 1.f ? A.Ink : A.Box, A.Paint);
			if (!By.IsEmpty())
			{
				A.bCovered = true;
				Report(TEXT("ПЕРЕКРЫТО"), false, A.T->GetName(), By, FString::Printf(TEXT("%s: строку закрывает %s «%s»"), *A.T->GetName(), *By, *A.Tx.Left(60)));
			}
		}
		for (FBtn& B : Buttons)
		{
			B.bCovered = !CoveredBy(B.R, B.Paint).IsEmpty();
			// видимая часть кнопки: прямоугольник UButton вместе с её планами (картинки не-значки, нарисованные раньше
			//   кнопки, по высоте — на всю кнопку, по ширине — в пределах кнопки ±8 px: края-куски девятисреза кнопки JUI
			//   шире прямоугольника клика на 3 px с каждой стороны)
			FRectPx V = B.R;
			bool bPlane = false;   // ★ 10.10.2026: у кнопки есть хоть один план (её видно на экране)
			for (const FImg& I : Imgs)
			{
				if (I.Paint >= B.Paint || (I.Local.X <= 24.f && I.Local.Y <= 24.f)) { continue; }
				if (I.R.L < B.R.L - 8.f || I.R.R > B.R.R + 8.f || I.R.T < B.R.T - 8.f || I.R.B > B.R.B + 8.f) { continue; }
				if (FMath::Min(I.R.B, B.R.B) - FMath::Max(I.R.T, B.R.T) < 0.8f * (B.R.B - B.R.T)) { continue; }
				V.L = FMath::Min(V.L, I.R.L); V.R = FMath::Max(V.R, I.R.R);
				bPlane = true;
			}
			B.Vis = V;
			B.VisFg = V;
			// ★ 10.10.2026: кнопка без планов и без своей кисти — невидимая зона клика: на экране её нет, как перекрытой
			if (!bPlane && !ButtonDrawsItself(B.W)) { B.bCovered = true; }
			// ★ 10.10.2026 (ревизия overlaps_fixes СРЕДН-1): маленькая кнопка-флажок (≤ 40×40) — видимой считается только
			//   НЕПРОЗРАЧНАЯ часть её собственной кисти и её планов (по пикселям текстуры). У `EXCHANGER_ALLCHK`
			//   (`BigBox_Check` 31×37, аукцион, режим 4) видимый квадрат 14×14, а прямоугольник клика выше него и
			//   задевал подпись «Уровень» сверху — ложный НАЕЗД-КНОПКА. Нет непрозрачных точек/текстуры — как раньше.
			if (!B.bCovered && B.Local.X <= 40.f && B.Local.Y <= 40.f)
			{
				FRectPx Ob;               // объединение непрозрачных частей
				bool bOb = false;         // что-то нашлось
				auto Grow = [&Ob, &bOb](const FRectPx& X)
				{
					if (!bOb) { Ob = X; bOb = true; return; }
					Ob.L = FMath::Min(Ob.L, X.L); Ob.T = FMath::Min(Ob.T, X.T); Ob.R = FMath::Max(Ob.R, X.R); Ob.B = FMath::Max(Ob.B, X.B);
				};
				bool bUnknown = false;    // есть кисть, прозрачность которой не прочесть (девятисрез, не текстура) — не сужаем
				if (const UButton* Bu = Cast<UButton>(B.W); Bu && ButtonDrawsItself(B.W))
				{
					const FSlateBrush& Nb = Bu->GetStyle().Normal;
					FRectPx X;
					if (OpaqueBox(Nb, Nb.TintColor.GetSpecifiedColor().A, B.R, X, TexCache)) { Grow(X); } else { bUnknown = true; }
				}
				// флажок шире 24×24 (`EXCHANGER_ALLCHK` — UCheckBox 31×37): его кисть по текущему состоянию
				else if (const UCheckBox* Cb = Cast<UCheckBox>(B.W))
				{
					const FCheckBoxStyle& Cs = Cb->GetWidgetStyle();
					const FSlateBrush& Nb = Cb->IsChecked() ? Cs.CheckedImage : Cs.UncheckedImage;   // кисть, видимая сейчас
					FRectPx X;
					if (OpaqueBox(Nb, Nb.TintColor.GetSpecifiedColor().A, B.R, X, TexCache)) { Grow(X); } else { bUnknown = true; }
				}
				for (const FImg& I : Imgs)
				{
					if (I.Paint >= B.Paint || (I.Local.X <= 24.f && I.Local.Y <= 24.f)) { continue; }
					if (I.R.L < B.R.L - 8.f || I.R.R > B.R.R + 8.f || I.R.T < B.R.T - 8.f || I.R.B > B.R.B + 8.f) { continue; }
					const FSlateBrush& Ib = I.Im->GetBrush();
					FRectPx X;
					if (OpaqueBox(Ib, Ib.TintColor.GetSpecifiedColor().A * I.Im->GetColorAndOpacity().A, I.R, X, TexCache)) { Grow(X); } else { bUnknown = true; }
				}
				if (bOb && !bUnknown)
				{
					UE_LOG(LogAsdaLangScan, Log, TEXT("СКАН-ЯЗЫК ВИДИМО-ФЛАЖОК %s %s: клик (%.0f,%.0f)…(%.0f,%.0f), видно (%.0f,%.0f)…(%.0f,%.0f)"),
						*WinName, *B.W->GetName(), B.R.L, B.R.T, B.R.R, B.R.B, Ob.L, Ob.T, Ob.R, Ob.B);
					B.VisFg = Ob;   // своя подпись внутри флажка меряется по-прежнему по Vis
				}
			}
		}
		for (FBtn& B : Checks) { B.bCovered = !CoveredBy(B.R, B.Paint).IsEmpty(); }
		for (int32 i = 0; i < Texts.Num(); ++i)
		{
			const FTx& A = Texts[i];
			if (A.bCovered) { continue; }
			const FString An = A.T->GetName();
			// центр ВИДИМОЙ части строки (не поля: поле бывает шире кнопки и захватывает соседний флажок — «Тип» у аукциона)
			const float Cx = (A.Ink.L + A.Ink.R) * 0.5f, Cy = (A.Ink.T + FMath::Min(A.Ink.B, A.Ink.T + 14.f)) * 0.5f;
			// кнопка-контейнер: самая тесная видимая (не перекрытая) кнопка, в которой центр видимой части строки
			int32 Own = INDEX_NONE;
			for (int32 b = 0; b < Buttons.Num(); ++b)
			{
				if (!Buttons[b].bCovered && Buttons[b].R.Contains(Cx, Cy) && (Own == INDEX_NONE || Buttons[b].R.Area() < Buttons[Own].R.Area())) { Own = b; }
			}
			if (Own != INDEX_NONE)
			{
				const FRectPx& Bt = Buttons[Own].R;
				const FRectPx& Bv = Buttons[Own].Vis;   // видимая часть (с планами)
				if (A.Ink.L < Bv.L - 1.f || A.Ink.R > Bv.R + 1.f)
				{
					Report(TEXT("ПЕРЕП-КНОПКА"), true, An, Buttons[Own].W->GetName(), FString::Printf(TEXT("%s: строка (%.0f,%.0f)…(%.0f,%.0f), кнопка %s (%.0f,%.0f)…(%.0f,%.0f) «%s»"),
						*An, A.Ink.L, A.Ink.T, A.Ink.R, A.Ink.B, *Buttons[Own].W->GetName(), Bv.L, Bt.T, Bv.R, Bt.B, *A.Tx.Left(60)));
				}
				// значок — только СВОЙ: ≤ 24×24, целиком внутри кнопки (±2 px), меньше половины её площади, не кусок рамки окна
				for (const FImg& Ic : Imgs)
				{
					if (Ic.Local.X > 24.f || Ic.Local.Y > 24.f) { continue; }
					const bool bInside = Ic.R.L >= Bt.L - 2.f && Ic.R.R <= Bt.R + 2.f && Ic.R.T >= Bt.T - 2.f && Ic.R.B <= Bt.B + 2.f;
					const bool bFrame = Ic.Im->GetParent() == Root && Ic.Im->GetName().Contains(TEXT("_0_"));
					if (!bInside || bFrame || Ic.R.Area() >= 0.5f * Bt.Area()) { continue; }
					if (!CoveredBy(Ic.R, Ic.Paint).IsEmpty()) { continue; }   // значок под планами (чужая страница) не виден
					if (A.Ink.Overlaps(Ic.R, 1.f, 1.f))
					{
						Report(TEXT("НАЕЗД-ЗНАЧОК"), true, An, Ic.Im->GetName(), FString::Printf(TEXT("%s: строка (%.0f,%.0f)…(%.0f,%.0f), значок %s (%.0f,%.0f)…(%.0f,%.0f) «%s»"),
							*An, A.Ink.L, A.Ink.T, A.Ink.R, A.Ink.B, *Ic.Im->GetName(), Ic.R.L, Ic.R.T, Ic.R.R, Ic.R.B, *A.Tx.Left(60)));
					}
				}
			}
			// флажки ≤ 24×24: подпись заходит под флажок (допуск 3 px — прозрачный край кисти)
			for (const FBtn& C : Checks)
			{
				if (C.bCovered) { continue; }
				if (A.Ink.Overlaps(C.R, 3.f, 2.f))
				{
					Report(TEXT("НАЕЗД-ФЛАЖОК"), true, An, C.W->GetName(), FString::Printf(TEXT("%s: строка (%.0f,%.0f)…(%.0f,%.0f) под флажком %s (%.0f,%.0f)…(%.0f,%.0f) «%s»"),
						*An, A.Ink.L, A.Ink.T, A.Ink.R, A.Ink.B, *C.W->GetName(), C.R.L, C.R.T, C.R.R, C.R.B, *A.Tx.Left(60)));
				}
			}
			// чужие кнопки: строка заходит на кнопку, в которой она не стоит (у маленьких кнопок ≤ 24×24 — допуск 3 px по X)
			for (int32 b = 0; b < Buttons.Num(); ++b)
			{
				const FBtn& Bb = Buttons[b];
				if (b == Own || Bb.bCovered || Bb.R.Contains(Cx, Cy)) { continue; }
				const float TolX = (Bb.Local.X <= 24.f && Bb.Local.Y <= 24.f) ? 3.f : 1.f;
				if (A.Ink.Overlaps(Bb.VisFg, TolX, 2.f))
				{
					Report(TEXT("НАЕЗД-КНОПКА"), true, An, Bb.W->GetName(), FString::Printf(TEXT("%s: строка (%.0f,%.0f)…(%.0f,%.0f) на кнопке %s (%.0f,%.0f)…(%.0f,%.0f) «%s»"),
						*An, A.Ink.L, A.Ink.T, A.Ink.R, A.Ink.B, *Bb.W->GetName(), Bb.VisFg.L, Bb.VisFg.T, Bb.VisFg.R, Bb.VisFg.B, *A.Tx.Left(60)));
				}
			}
			// чужие строки (одно поле в одном родителе — ВАРИАНТЫ: код показывает одну)
			for (int32 j = i + 1; j < Texts.Num(); ++j)
			{
				const FTx& Bx = Texts[j];
				if (Bx.bCovered || !A.Ink.Overlaps(Bx.Ink, 1.f, 2.f)) { continue; }
				const bool bVariant = A.T->GetParent() == Bx.T->GetParent()
					&& FMath::Abs(A.Box.L - Bx.Box.L) <= 1.f && FMath::Abs(A.Box.T - Bx.Box.T) <= 1.f
					&& FMath::Abs(A.Box.R - Bx.Box.R) <= 1.f && FMath::Abs(A.Box.B - Bx.Box.B) <= 1.f;
				// ★ 10.10.2026 (СРЕДН-3): заходит ли настоящая строка на ПОЛЕ соседа-заглушки (когда заглушка — одна из двух)
				const bool bAStub = GPlaceholders.Contains(WinName + TEXT("|") + An);
				const bool bBStub = GPlaceholders.Contains(WinName + TEXT("|") + Bx.T->GetName());
				const int32 FieldHit = bAStub == bBStub ? 0
					: (bAStub ? (Bx.Ink.Overlaps(A.Box, 1.f, 2.f) ? 1 : 0) : (A.Ink.Overlaps(Bx.Box, 1.f, 2.f) ? 1 : 0));
				Report(bVariant ? TEXT("ВАРИАНТ") : TEXT("НАЕЗД-ТЕКСТ"), !bVariant, An, Bx.T->GetName(),
					FString::Printf(TEXT("%s × %s «%s» × «%s»"), *An, *Bx.T->GetName(), *A.Tx.Left(40), *Bx.Tx.Left(40)), FieldHit);
			}
			// ★ 10.10.2026: НАЕЗД-ПЛАШКА — конец (или начало) строки уходит ПОД непрозрачную картинку, нарисованную позже
			//   (поле клавиши в настройках поверх подписи «Открыть/закрыть окно снаряжения»): картинка не значок, закрывает
			//   строку частично (целиком — это ПЕРЕКРЫТО), в полосе пересечения непрозрачна по пикселям в ≥ 3 из 5 точек.
			for (const FImg& I : Imgs)
			{
				if (I.Paint <= A.Paint || (I.Local.X <= 24.f && I.Local.Y <= 24.f) || !IsOpaqueImage(I.Im)) { continue; }
				const float L = FMath::Max(A.Ink.L, I.R.L), R = FMath::Min(A.Ink.R, I.R.R);
				const float T = FMath::Max(A.Ink.T, I.R.T), B = FMath::Min(A.Ink.B, I.R.B);
				if (R - L <= 2.f || B - T <= 2.f) { continue; }
				int32 Opaque = 0;   // непрозрачных точек из 5 в полосе пересечения
				for (int32 k = 0; k < 5; ++k)
				{
					if (AlphaAt(I, L + (R - L) * (0.1f + 0.2f * k), (T + B) * 0.5f, TexCache) >= 0.5f) { ++Opaque; }
				}
				if (Opaque >= 3)
				{
					Report(TEXT("НАЕЗД-ПЛАШКА"), true, An, I.Im->GetName(), FString::Printf(TEXT("%s: строка (%.0f,%.0f)…(%.0f,%.0f) под картинкой %s (%.0f,%.0f)…(%.0f,%.0f) «%s»"),
						*An, A.Ink.L, A.Ink.T, A.Ink.R, A.Ink.B, *I.Im->GetName(), I.R.L, I.R.T, I.R.R, I.R.B, *A.Tx.Left(60)));
				}
			}
			// строка выходит за свою подложку (не внутри кнопки — там ПЕРЕП-КНОПКА)
			if (Own == INDEX_NONE)
			{
				float Px;
				switch (Justify(A.T))
				{
				case ETextJustify::Center: Px = (A.Ink.L + A.Ink.R) * 0.5f; break;
				case ETextJustify::Right:  Px = A.Ink.R - 2.f; break;
				default:                   Px = A.Ink.L + 2.f; break;
				}
				const float Py = A.Ink.T + FMath::Min(7.f, (A.Ink.B - A.Ink.T) * 0.5f);   // середина первой строки
				FRectPx Pl;
				if (PlateRect(Imgs, Px, Py, A.Paint, A.Box.R - A.Box.L, Pl, TexCache)
					&& (A.Ink.L < Pl.L - 1.f || A.Ink.R > Pl.R + 1.f || (A.Ink.B - A.Ink.T >= 24.f && A.Ink.B > Pl.B + 2.f)))
				{
					Report(TEXT("ПЕРЕП-ПОЛЕ"), true, An, TEXT("-"), FString::Printf(TEXT("%s: строка (%.0f,%.0f)…(%.0f,%.0f), подложка (%.0f,%.0f)…(%.0f,%.0f) «%s»"),
						*An, A.Ink.L, A.Ink.T, A.Ink.R, A.Ink.B, Pl.L, Pl.T, Pl.R, Pl.B, *A.Tx.Left(60)));
				}
			}
		}
		return N;
	}

	// Проверка одного окна на экране: кириллица (в нерусской сессии), переполнение поля, наезды (`CheckOverlaps`).
	//   ★ 10.10.2026: вынесено из `Step`, чтобы тем же телом проверять окна, открытые игрой (`live`, СРЕДН-3).
	static void ScanWindow(UUserWidget* Win, const FString& Path, FState& S)
	{
		if (!Win || !Win->WidgetTree) { return; }
		const FString WinName = FPackageName::ObjectPathToObjectName(Path);   // имя окна для лога
		int32 Cyr = 0;                                                        // узлов с кириллицей
		TArray<UWidget*> All;                                                 // все узлы окна
		Win->WidgetTree->GetAllWidgets(All);
		for (UWidget* Wd : All)
		{
			UTextBlock* T = Cast<UTextBlock>(Wd);
			if (!T || !T->IsVisible()) { continue; }
			const FString Tx = T->GetText().ToString();
			if (Tx.TrimStartAndEnd().IsEmpty()) { continue; }
			const FGeometry G = T->GetCachedGeometry();
			const FVector2D Have = G.GetLocalSize();
			if (Have.X <= 1.f) { continue; }   // не отрисован (свёрнут предок) — на экране его нет
			if (S.bCheckCyr && HasCyrillic(Tx))
			{
				++Cyr;
				UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК КИР %s %s «%s»"), *WinName, *T->GetName(), *Tx.Left(80));
			}
			const FVector2D Want = T->GetDesiredSize();
			if (S.bMeasure)
			{
				UE_LOG(LogAsdaLangScan, Log, TEXT("СКАН-ЯЗЫК МЕРА %s %s %.1f %.1f есть %.1f «%s»"),
					*WinName, *T->GetName(), Want.X, Want.Y, Have.X, *Tx.Left(80));
			}
			if (!T->GetAutoWrapText() && Want.X > Have.X + 2.f)
			{
				++S.Overflow;
				UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК ПЕРЕП %s %s: нужно %.0f, есть %.0f «%s»"),
					*WinName, *T->GetName(), Want.X, Have.X, *Tx.Left(60));
			}
		}
		if (Cyr) { ++S.CyrWindows; S.CyrNodes += Cyr; }
		S.Overlap += CheckOverlaps(Win, Path, S.Service);   // ★ 09.10.2026: наезды на соседние контролы (10.10 — без ложных)
	}

	// Открыть окно i, подставить строки кода (★ 10.10.2026), через 0.4 с проверить его и закрыть, затем следующее.
	static void Step(TWeakObjectPtr<UWorld> WW, TSharedRef<FState> S)
	{
		UWorld* W = WW.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { return; }
		if (S->Pos >= S->Paths.Num())
		{
			UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК ИТОГ: сессия «%s», окон %d, кириллица %d (в узлах %d), переполнений %d, наездов %d, служебных %d, строк кода подставлено %d"),
				FAsdaLang::Code(FAsdaLang::Current()), S->Windows, S->CyrWindows, S->CyrNodes, S->Overflow, S->Overlap, S->Service, S->Substituted);
			AsdaSelfTestKit::Quit(W, S->bQuit);
			return;
		}
		const FString Path = S->Paths[S->Pos++];
		const FName Id(*FString::Printf(TEXT("LANGSCAN_%d"), S->Pos));
		UUserWidget* Win = WM->OpenWindowByPath(Id, Path, 900, ESlateVisibility::SelfHitTestInvisible, false);
		if (!Win)
		{
			W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WW, S]() { Step(WW, S); }));
			return;
		}
		++S->Windows;
		// ★ 10.10.2026 (СРЕДН-3): строки-образцы, которые код заменяет loc-строкой, — строкой текущего языка (до раскладки)
		S->Substituted += SubstituteCodeStrings(Win, FPackageName::ObjectPathToObjectName(Path));
		TWeakObjectPtr<UUserWidget> WWin = Win;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, S, WWin, Id, Path]()
		{
			UWorld* W2 = WW.Get();
			UGameInstance* GI2 = W2 ? W2->GetGameInstance() : nullptr;
			UAsdaWindowSubsystem* WM2 = GI2 ? GI2->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			ScanWindow(WWin.Get(), Path, *S);
			if (WM2) { WM2->CloseWindow(Id); }
			Step(WW, S);
		}), 0.4f, false);
	}

	// ★ 10.10.2026 (СРЕДН-3): режим `live` — проверить окна, которые УЖЕ открыла игра (в вьюпорте, видимы, класс `WBP_*`),
	//   со строками, поставленными кодом. Ничего не открывает и не закрывает; таблица заглушек не применяется.
	static void ScanLive(UWorld* W, FState& S)
	{
		GLive = true;
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* Win = *It;
			if (!Win || Win->GetWorld() != W || !Win->IsInViewport() || !Win->IsVisible()) { continue; }
			const FString Path = Win->GetClass()->GetPathName();   // «/Game/…/WBP_x.WBP_x_C»
			FString Name = FPackageName::ObjectPathToObjectName(Path);
			Name.RemoveFromEnd(TEXT("_C"));
			if (!Name.StartsWith(TEXT("WBP_"))) { continue; }
			++S.Windows;
			S.Seen.AddUnique(Name);
			UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК ОКНО-ЖИВОЕ [%s] %s"), *S.Tag, *Name);
			ScanWindow(Win, FPackageName::ObjectPathToPackageName(Path) + TEXT(".") + Name, S);
		}
		GLive = false;
		UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК ИТОГ-ЖИВОЕ [%s]: сессия «%s», окон %d, кириллица %d (в узлах %d), переполнений %d, наездов %d, служебных %d"),
			*S.Tag, FAsdaLang::Code(FAsdaLang::Current()), S.Windows, S.CyrWindows, S.CyrNodes, S.Overflow, S.Overlap, S.Service);
		// ★ 10.10.2026 (ревизия overlaps_fixes СРЕДН-1): состояние, которое не показало ни одного окна (или не показало
		//   ожидаемое `expect=`), — ПРОВАЛ проверки, а не «0 наездов» (аукцион режим 4 и объявления так и прошли незамеченными).
		TArray<FString> Missing;   // ожидаемые окна, которых нет на экране
		for (const FString& E : S.Expect) { if (!S.Seen.Contains(E)) { Missing.Add(E); } }
		if ((S.Windows == 0 && !S.Tag.Equals(TEXT("hud"), ESearchCase::IgnoreCase)) || Missing.Num() > 0)
		{
			UE_LOG(LogAsdaLangScan, Error, TEXT("СКАН-ЯЗЫК ПРОВАЛ-ЖИВОЕ [%s]: окон %d, нет ожидаемых: %s — состояние не открылось, проверка не выполнена"),
				*S.Tag, S.Windows, Missing.Num() ? *FString::Join(Missing, TEXT(",")) : TEXT("-"));
		}
		if (S.bQuit) { GEngine->Exec(W, TEXT("quit")); }
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdLangScanScreen(
	TEXT("asda.Lang.ScanScreen"),
	TEXT("[ПОРТ] Этап 8 мультиязычности: открыть каждое окно UI (кроме логина/Unused/Temp), найти кириллицу на экране "
	     "(в нерусской сессии), переполнения подписей и наезды строк на кнопки/значки/соседний текст. "
	     "[quit] [filter=<подстрока пути>] [measure — размер каждой видимой строки] "
	     "[live tag=<метка> [expect=WBP_a,WBP_b] — только окна, уже открытые игрой, со строками кода; без окон/без ожидаемых — ПРОВАЛ (10.10.2026)]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (!W || AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Lang.ScanScreen"), KitArgs)) { return; }
		TSharedRef<AsdaLangScan::FState> S = MakeShared<AsdaLangScan::FState>();
		FString Filter;
		for (const FString& A : Args)
		{
			if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { S->bQuit = true; }
			if (A.StartsWith(TEXT("filter="))) { Filter = A.Mid(7); }
			if (A.Equals(TEXT("measure"), ESearchCase::IgnoreCase)) { S->bMeasure = true; }
			if (A.Equals(TEXT("live"), ESearchCase::IgnoreCase)) { S->bLive = true; }
			if (A.StartsWith(TEXT("tag="))) { S->Tag = A.Mid(4); }
			if (A.StartsWith(TEXT("expect="))) { A.Mid(7).ParseIntoArray(S->Expect, TEXT(","), true); }
		}
		S->bCheckCyr = !FAsdaLang::IsRu();
		if (S->bLive)   // ★ 10.10.2026 (СРЕДН-3): окна, открытые игрой через свои подсистемы
		{
			AsdaLangScan::LoadPlaceholders();
			AsdaLangScan::ScanLive(W, *S);
			return;
		}
		AsdaLangScan::LoadPlaceholders();   // ★ 10.10.2026: строки-заглушки (служебный вид)
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> A;
		AR.GetAssetsByPath(FName(TEXT("/Game/AsdaAssets/UI")), A, /*bRecursive*/ true);
		// ★ R5-fix-b 13.11.2026 (заключительный заход) (BRIEF4 п. 4): в СОБРАННОЙ игре реестр перечисляет и класс `WBP_<id>_C` (объект пакета `WBP_<id>`), а у части
		//   окон — только его (как у `UI.SelfTestOpenOnTop`, `AsdaUiWbpCatalog.h`). Скан брал его объектный путь как ещё одно окно:
		//   окно сканировалось дважды, а у окон с одним классом имя для заглушек было `WBP_<id>_C` — записи `WBP_<id>|…` из
		//   `ui_scan_placeholders.tsv` не находились. Класс теперь даёт путь ассета-окна `…/WBP_<id>.WBP_<id>`, повторы отброшены.
		//   В редакторе классов в реестре нет — список тот же.
		TSet<FString> Seen;   // уже взятые объектные пути
		for (const FAssetData& D : A)
		{
			FString Pkg, Id;
			if (!AsdaUiWbpCatalog::WindowAsset(D, Pkg, Id)) { continue; }
			const FString Short = FPackageName::GetShortName(Pkg);
			const FString N = D.AssetName.ToString();
			const FString P = (N == Short + TEXT("_C")) ? Pkg + TEXT(".") + Short : D.GetObjectPathString();
			bool bDup = false;
			Seen.Add(P, &bDup);
			if (bDup || P.Contains(TEXT("/Unused/")) || P.Contains(TEXT("/Temp/")) || P.Contains(TEXT("/Login/"))
				|| P.Contains(TEXT("/Dev/")) || (!Filter.IsEmpty() && !P.Contains(Filter)))
			{
				continue;
			}
			S->Paths.Add(P);
		}
		S->Paths.Sort();
		UE_LOG(LogAsdaLangScan, Warning, TEXT("СКАН-ЯЗЫК: окон к проверке %d, сессия «%s»"), S->Paths.Num(), FAsdaLang::Code(FAsdaLang::Current()));
		AsdaLangScan::Step(W, S);
	}));
#endif // ASDA_DEV_CONSOLE
