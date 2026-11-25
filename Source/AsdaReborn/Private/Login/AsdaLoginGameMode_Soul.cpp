// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginGameMode_Soul.cpp — ★ 23.10.2026 часть AAsdaLoginGameMode, вынесенная из AsdaLoginGameMode.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaLoginGameMode.plan.json).
//   «Одержимость» на выборе персонажа (персонаж соулмейта в слоте, силуэт и аура души, рамка сведений) и предзагрузка
//   экрана выбора: состояния клипов, офлайн-режим и самотесты выбора (консоль), аура души в Tick.
//   #include — те же, что у AsdaLoginGameMode.cpp, в том же порядке; общее — AsdaLoginGameModeInternal.h.
#include "Login/AsdaLoginGameMode.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "World/AsdaFadeComponent.h"         // ★ дедуп №2 В2-C (F9): AsdaTranslucentSlots::MakeSwapMid — общая MID-подмена слота
#include "Resources/AsdaSoundSubsystem.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaAdminSubsystem.h"   // сброс прав GM при возврате в меню логина
#include "Login/AsdaLoginData.h"
#include "Login/AsdaServerAddress.h"   // ★ 16.10.2026: домен сервера
#include "AsdaConnection.h"           // ★ 16.10.2026: FAsdaConnection::PrefetchHost — фоновый резолв домена заранее
#include "Login/AsdaText.h"          // FAsdaText::Strip (срез JUI-разметки)
// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FormatPrintf — %d в строках DT_GameText   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaMarkup.h"           // UAsdaRichText::ApplyFormatted — единая точка форматированного текста
#include "UI/AsdaTipPanel.h"         // UAsdaTipPanel::CountWrappedLines — единственный счётчик строк с переносом
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "Engine/DataTable.h"
#include "Components/ComboBoxString.h"  // созвездие CARAMAKE_cb
#include "Blueprint/WidgetTree.h"    // GetAllWidgets — поиск план-виджетов по префиксу (имена с idx-суффиксом)
#include "Login/AsdaLoginConfig.h"
#include "LevelSequence.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Misc/ScopeExit.h"                  // ON_SCOPE_EXIT — укладка окна 114 у курсора после переходов наведения   // ★ масштаб вьюпорта для раскладки стартовой заставки
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"          // ICONBOX-иконки сетки создания
#include "Blueprint/WidgetTree.h"
#include "Engine/Font.h"
#include "Components/EditableText.h"
#include "Components/RichTextBlock.h"   // [САМОТЕСТ] 08.10.2026: чтение пузыря ANI_TXT в asda.Login.CreateNameTest
#include "AsdaOutgoingGate.h"           // [САМОТЕСТ] 08.10.2026: офлайн-гейт asda.Login.CreateNameTest
#include "Test/AsdaSelfTestKit.h"       // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы с `quit`, итог, выход)
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"       // FWidgetPath::ContainsWidget — маппинг focus-события на наш контрол
#include "Framework/Application/IInputProcessor.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "Components/CheckBox.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"
#include "AsdaNetSubsystem.h"
#include "Net/ChannelCipher.h"   // ★ профиль ключа канального шифра (наш / XyT)
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "UnrealClient.h"                 // FScreenshotRequest (движковый захват вместо RDP CopyFromScreen)
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"       // FSlateBrush — рантайм-фикс рамки пузыря
#include "GameFramework/PlayerController.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"   // ★ ActiveBoneIndices LOD0 — кости с весами для коробки пика «одержимости»
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaHudSubsystem.h"            // CharexpCumExp — int64-порог опыта (общая точка с HUD)
#include "Characters/AsdaCharAssembly.h"
#include "UI/AsdaIconCellGrid.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "AsdaEffectActor.h"
#include "EngineUtils.h"
#include "Login/AsdaPossessScene.h"   // ★ «одержимость»: правила парных клипов + ловец кликов по сцене
#include "World/AsdaPlayerCharacter.h" // ★ общая точка kAsdaCharPreviewBaseM/F (база пути превью-скелета)
#include "World/AsdaLoadManagerSubsystem.h" // ★ charsel_preload: фоновая предзагрузка превью экрана выбора
#include "PipelineStateCache.h"             // ★ charsel_preload: IsPSOPrecachingEnabled — прогрев PSO только где он есть
#include "AsdaLoginGameModeInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaLoginGameMode*.cpp (LogAsdaLogin, чистые хелперы)


// ══════════════ ★★★ «ОДЕРЖИМОСТЬ»: персонаж соулмейта в сцене выбора персонажа ═════════════════
//
// Полный разбор тракта — `Reports/audit_2026-09-02/fix_possess_scene.md`. Коротко:
//   логин-сервер в ответ на 1005 шлёт 1024 (экип чужих) → 1023 (статы+внешность) → 1025 (статус),
//   всё это разбирает `UAsdaNetSubsystem`; сцена берёт готовый набор `GetSoulmateSharedChars()`.
//   Здесь — ТОЛЬКО показ и выбор: раздача по слотам, модели, панели, вход в мир.

const FAsdaSoulmateSharedChar* AAsdaLoginGameMode::FindPartnerRecord(uint8 Charidx) const
{
	// Эталон `playerFindSubRecordByType@0xF0A210`: вне 10..12 — сразу nullptr, дальше сверка rec[1076].
	if (Charidx < 10 || Charidx > 12) { return nullptr; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return nullptr; }
	for (const FAsdaSoulmateSharedChar& R : Net->GetSoulmateSharedChars())
	{
		if (!R.IsEmpty() && R.Charidx == Charidx) { return &R; }
	}
	return nullptr;
}

void AAsdaLoginGameMode::ShowCharStatePanel(bool bVisible)
{
	// Эталон `sub_929C7A` → `0xF083B0`: включает/выключает ОКНО 17 `CARASTATE_WIN` целиком.
	//   В порту статы живут внутри монолита `WBP_CharSelect` вместе с кнопками, поэтому гасим
	//   группу узлов по префиксу `CARASTATE` (закон `jui-uneven-node-layout`: иерархия расплющена,
	//   дети сами по себе не гаснут).
	if (!CharStateWidget || !CharStateWidget->WidgetTree) { return; }
	const ESlateVisibility Vis = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	TArray<UWidget*> AllW;
	CharStateWidget->WidgetTree->GetAllWidgets(AllW);
	for (UWidget* W : AllW)
	{
		if (W && W->GetName().StartsWith(TEXT("CARASTATE"))) { W->SetVisibility(Vis); }
	}
}

// ★★★ ОБЩАЯ ТОЧКА: форматированный текст в ГОТОВОМ контроле JUI со СНИМКОМ ЕГО СТИЛЯ.
//   Контракт `UI/AsdaMarkup.h` прямым текстом: «кто зовёт `ApplyFormatted` для ГОТОВОГО контрола
//   из JUI — ОБЯЗАН передать сюда его собственный стиль», иначе новый `URichTextBlock` берёт строку
//   `Default` таблицы `DT_AsdaRichStyle` (белый, 10 px, autowrap) и теряет всё, что импортёр взял
//   из JUI: кегль `FONT_PX`, цвет из блока `EXS TextColor`, выравнивание из `EXS TextAlign` и
//   чёрную обводку скина. Ровно на этом классе дефектов проект уже горел («нечитаемо, не те цвета
//   и размер» в инфо-панели инвентаря).
//   ⚠ Снимок МОЖНО снять только ДО первой подмены: после неё `Cast<UTextBlock>` не найдёт ничего.
//   Поэтому снимаем ЛЕНИВО — при первом же показе этого узла, когда он гарантированно ещё
//   `UTextBlock`, и запоминаем в `JuiTextStyles` по его имени (`ApplyFormatted` имя сохраняет).
//   ★ 02.09.2026: заведено вместо трёх двухаргументных вызовов `ApplyFormatted` (пузырь помощника
//     в `SetLoginStatus` и `ApplyHelper`, строка `MESSAGE_TXT01`) и вместо разового пакетного
//     снимка стиля окна 114 — чтобы рецепт был ОДИН на весь логин (закон `port-shared-points`).
UWidget* AAsdaLoginGameMode::ApplyRichWithJuiStyle(UWidget* Node, const FString& Raw)
{
	if (!Node) { return nullptr; }
	const FName Key = Node->GetFName();
	FAsdaJuiTextStyle* S = JuiTextStyles.Find(Key);
	if (!S)
	{
		FAsdaJuiTextStyle New;
		if (UTextBlock* Tb = Cast<UTextBlock>(Node))
		{
			New.Style = FTextBlockStyle()
				.SetFont(Tb->GetFont())                                // кегль JUI + обводка скина
				.SetColorAndOpacity(Tb->GetColorAndOpacity())          // цвет из блока `EXS TextColor`
				.SetShadowOffset(Tb->GetShadowOffset())
				.SetShadowColorAndOpacity(Tb->GetShadowColorAndOpacity());
			New.Justify = AsdaUi::ReadJustification(Tb);               // общая точка: свойство protected
			// ★ Закон `ui-autowrap-canvas-gotcha`: перенос включаем только там, где авторский слот
			//   рассчитан на несколько строк (высота >= 28 = две строки по 14), ширину берём из слота.
			float W_ = 0.f, H_ = 0.f;
			if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Tb))
			{
				const FVector2D Sz = CS->GetSize();
				W_ = (float)Sz.X; H_ = (float)Sz.Y;
			}
			New.bWrap  = (H_ >= 28.f);
			New.WrapAt = New.bWrap ? W_ : 0.f;
			New.bCaptured = true;
		}
		else
		{
			// Узел уже подменён кем-то другим (или это не текстовый контрол) — снимать нечего.
			//   Запись всё равно кладём, чтобы не пытаться снимать снова каждый кадр.
			UE_LOG(LogAsdaLogin, Warning,
				TEXT("[текст] стиль узла '%s' снять не удалось: это уже не UTextBlock — покажем стилем Default"),
				*Key.ToString());
		}
		S = &JuiTextStyles.Add(Key, New);
	}

	FAsdaRichApplyOpts Opts;
	if (S->bCaptured)
	{
		Opts.BaseStyle   = &S->Style;
		Opts.bSetJustify = true;
		Opts.Justify     = S->Justify;
		Opts.bWrap       = S->bWrap;
		Opts.WrapAt      = S->WrapAt;
	}
	return UAsdaRichText::ApplyFormatted(Node, Raw, S->bCaptured ? &Opts : nullptr);
}

// ── ★★★ АВТО-ВЫСОТА РАМКИ ОКНА 114 ПОД ФАКТИЧЕСКИЙ ТЕКСТ ─────────────────────────────────────
//
// ЗАЧЕМ. Тултип «одержимости» показывает ДВА РАЗНЫХ текста: режим 1 — восемь записей типа 0
//   («здесь встанет персонаж твоего Друга», ~10 строк), режим 2 — семнадцать записей типа 2
//   (~30 строк). Рамка в ассете одна, поэтому высоту приходилось брать по худшему случаю: после
//   правки 03.09.2026 (`fix_soulinfo_tooltip_frame.md`) она равна 485 px, и в режиме 1 под текстом
//   остаётся ≈300 px пустоты (62 % окна — замер ревизии). Владелец просил рамку ПО ТЕКСТУ.
//
// ПОЧЕМУ ЭТО НЕ НАРУШЕНИЕ ЗАКОНА `no-runtime-widget-fixes`. Закон запрещает чинить КОДОМ дефект
//   РАЗМЕТКИ: если контрол не там, не того размера или его нет — это правится в ассете. Здесь
//   случай принципиально другой: нужная высота ЗАВИСИТ ОТ РАНТАЙМ-СОДЕРЖИМОГО (какой режим навели
//   и сколько строк дала таблица текста после переноса по ширине 227 px), статикой её задать
//   НЕВОЗМОЖНО — одна цифра в ассете обязана обслужить оба режима. Ровно тот же случай уже решён в
//   порту трижды (`AsdaBoosterShopSubsystem::ApplyPanelHeight`, `AsdaHairShopSubsystem::
//   ApplyGridLayout`, `AsdaPurchaseBoxSubsystem`), и общая точка растяжки — `AsdaUi::
//   StretchFramePlanesHeight` — заведена именно под это. Второй такой хелпер не заводим (закон
//   `check-existing-before-creating`). Базовая разметка окна (ширина 270, позиции узлов, состав
//   дерева) кодом НЕ трогается — она остаётся в ассете; здесь меняется ровно одно число — высота.
//
// ЧТО ИМЕННО ДЕЛАЕМ (порядок обязателен):
//   1) меряем фактическую высоту текста режима: строк с переносом × высота строки;
//   2) ставим эту высоту текстовому боксу;
//   3) растягиваем средний ряд девятипатча и двигаем нижний — общей точкой `StretchFramePlanesHeight`
//      (низ среднего ряда у неё ВСЕГДА совпадает с верхом нижнего, стык 9-slice не рвётся);
//   4) верх рамки и вся горизонталь не трогаются — рост строго ВНИЗ.
//   Низ бокса кладём ровно на верх нижнего бордюра. В ассете бокс заходит на 4 px ПОД бордюр
//   (265.5 против 261.5) — там это безвредно (текст короче бокса), а при счёте ПО ФАКТУ такой
//   нахлёст срезал бы низ последней строки, поэтому в авто-режиме его нет.
//
// ОТКАТ: `asda.SoulInfo.AutoHeight 0` — возвращаются АССЕТНЫЕ значения (рамка 485, боксы 445/200/150),
//   то есть поведение, сделанное правкой ассета; сама та правка остаётся значением по умолчанию и
//   фолбэком на случай, когда померить текст нечем (Slate не поднят, стиль контрола не снят).
static ASDA_DEV_CVAR(int32, CVarAsdaSoulInfoAutoHeight,
	TEXT("asda.SoulInfo.AutoHeight"), 1,
	TEXT("Высота рамки окна 114 (тултип «одержимости»): 1 = по фактическому тексту показанного режима ")
	TEXT("(наша правка, по умолчанию), 0 = фиксированная высота из ассета (485 px) — путь отката."),
	ECVF_Default);

void AAsdaLoginGameMode::ApplySoulInfoFrameHeight(int32 Mode)
{
	if (!SoulInfoWidget) { return; }

	// Девять планов рамки. Имена дал генерик-импортёр JUI (`<idc>_<номер плана>`), порядок
	//   ПОСТРОЧНЫЙ (0-2 верх, 3-5 середина, 6-8 низ) — ровно тот, которого ждёт общая точка.
	TArray<TObjectPtr<UWidget>> Planes;
	Planes.Reserve(9);
	for (int32 i = 0; i < 9; ++i)
	{
		UWidget* P = SoulInfoWidget->GetWidgetFromName(FName(*FString::Printf(TEXT("SOULINFO_WIN_0_%d"), i)));
		if (!P)
		{
			UE_LOG(LogAsdaLogin, Warning,
				TEXT("[одержимость] рамка 114: план SOULINFO_WIN_0_%d не найден — высоту не меняем"), i);
			return;
		}
		Planes.Add(P);
	}
	UCanvasPanelSlot* SlTop = AsdaUi::SlotOf(Planes[0].Get());
	UCanvasPanelSlot* SlBot = AsdaUi::SlotOf(Planes[6].Get());
	if (!SlTop || !SlBot) { return; }
	const FMargin OffTop = SlTop->GetOffsets();
	const FMargin OffBot = SlBot->GetOffsets();
	const float TopRowH  = OffTop.Bottom;   // высота верхнего бордюра — из скина (21), не меняется
	const float BotRowH  = OffBot.Bottom;   // высота нижнего бордюра — из скина (21), не меняется
	const float FrameTop = OffTop.Top;      // верх рамки — не меняется (рост только вниз)

	// Снимок АССЕТНОЙ раскладки — один раз, до первой растяжки (иначе «база» уедет вслед за правкой).
	if (SoulInfoBaseFrameH <= 0.f)
	{
		SoulInfoBaseFrameH = (OffBot.Top + OffBot.Bottom) - OffTop.Top;
		for (const TCHAR* N : { TEXT("SOULINFO_TXT00"), TEXT("SOULINFO_TXT01"), TEXT("SOULINFO_TXT02") })
		{
			if (UCanvasPanelSlot* S = AsdaUi::SlotOf(SoulInfoWidget->GetWidgetFromName(FName(N))))
			{
				SoulInfoBaseBoxH.Add(FName(N), S->GetOffsets().Bottom);
			}
		}
	}

	float LineH = 0.f;              // высота строки шрифта контрола (нужна и для нижней границы рамки)
	float ContentBottom = 0.f;      // низ САМОГО НИЖНЕГО занятого текстом бокса
	bool  bMeasured = false;

	// Высота ОДНОГО текстового бокса по его фактическому тексту. Обе метрики — ОБЩИЕ ТОЧКИ порта:
	//   `UAsdaTipPanel::CountWrappedLines` (единственная реализация счётчика строк с переносом, порт
	//   эталонного `textFontCountWrappedLines_Active`) и `AsdaUi::MeasureLineHeight` (замер Slate).
	//   Своих копий не заводим (закон `check-existing-before-creating`).
	auto MeasureBox = [&](const TCHAR* NodeName, int32 TextType) -> bool
	{
		UCanvasPanelSlot* S = AsdaUi::SlotOf(SoulInfoWidget->GetWidgetFromName(FName(NodeName)));
		if (!S) { return false; }
		// Стиль контрола снят при первом показе (`ApplyRichWithJuiStyle`). Без него нечем мерить:
		//   шрифт и ширина переноса живут именно в этом снимке.
		const FAsdaJuiTextStyle* St = JuiTextStyles.Find(FName(NodeName));
		if (!St || !St->bCaptured) { return false; }
		const FString Raw = FAsdaPossessData::SoulInfoText(GetGameInstance(), TextType);
		// Текст таблицы несёт ЛИТЕРАЛЬНЫЙ «\n» (два символа) и теги <FC=…>/<FB>/</F>. Снимаем и то и
		//   другое ровно так, как это делает показ (`FAsdaMarkup::ToRichMarkup`), иначе счёт строк
		//   разойдётся с рендером: теги ширины не занимают, а «\n» даёт перевод строки.
		const FString Plain = FAsdaMarkup::StripTags(Raw).Replace(TEXT("\\n"), TEXT("\n"));
		if (Plain.TrimStartAndEnd().IsEmpty()) { return false; }   // пустой контрол места не занимает
		const float WrapW = (St->WrapAt > 1.f) ? St->WrapAt : (float)S->GetSize().X;
		const int32 Lines = UAsdaTipPanel::CountWrappedLines(Plain, St->Style.Font, WrapW);
		const float H = AsdaUi::MeasureLineHeight(St->Style.Font, 0.f);
		if (H <= 0.f || Lines <= 0) { return false; }              // шрифт не померился — уходим в фолбэк
		LineH = FMath::Max(LineH, H);
		FMargin O = S->GetOffsets();
		// ★ ЗАПАС В ОДНУ СТРОКУ (по ревизии `review_soulinfo_autoheight.md`). Без него бокс равен
		//   ровно `Lines * H`, клипа у контрола нет, и недосчёт всего одной строки даёт МОЛЧАЛИВЫЙ
		//   перелив текста за рамку — тот самый баг, который здесь и чинили. Счётчик меряет живым
		//   `FSlateFontMeasure`, то есть той же метрикой, что и рендер, и на сегодняшних данных
		//   сходится со Slate; разойтись он может лишь на дрейфе данных (двойные пробелы, новые
		//   записи, токен шире ширины переноса — тогда Slate добавит свой перенос внутри токена).
		//   Цена запаса — одна пустая строка снизу, цена его отсутствия — обрезанный текст.
		O.Bottom = (float)(Lines + 1) * H;
		S->SetOffsets(O);
		ContentBottom = bMeasured ? FMath::Max(ContentBottom, O.Top + O.Bottom) : (O.Top + O.Bottom);
		bMeasured = true;
		return true;
	};

	const bool bAuto = (CVarAsdaSoulInfoAutoHeight.GetValueOnGameThread() != 0);
	if (bAuto && FSlateApplication::IsInitialized())
	{
		if (Mode == 2)
		{
			MeasureBox(TEXT("SOULINFO_TXT02"), 2);
		}
		else if (Mode == 1)
		{
			MeasureBox(TEXT("SOULINFO_TXT00"), 0);
			// Второй абзац меряем ТОЛЬКО если он реально показан — гейт тот же, что у наполнения
			//   (эталонный флаг `+3075045` из пакета 1025). Иначе рамка росла бы под скрытый текст.
			UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (!Net || !Net->bSoulmateInfoSecondParagraph)
			{
				MeasureBox(TEXT("SOULINFO_TXT01"), 1);
			}
		}
	}

	float WantH = SoulInfoBaseFrameH;
	if (bMeasured)
	{
		WantH = (ContentBottom + BotRowH) - FrameTop;
		// Нижняя граница: рамку не схлопывать — минимум это оба бордюра и одна строка текста.
		const float MinH = TopRowH + BotRowH + LineH;
		WantH = FMath::Max(WantH, MinH);
		// Верхняя граница — ЭКРАН: ниже нижней кромки вьюпорта рамке расти некуда. Высота корневой
		//   канвы окна = высота вьюпорта в юнитах раскладки (пиксели / DPI-масштаб проекта), верх
		//   рамки = якорная доля × эта высота + собственный офсет плана (Alignment у импортёра 0,0).
		const FVector2D VpSize   = UWidgetLayoutLibrary::GetViewportSize(this);
		const float     DpiScale = UWidgetLayoutLibrary::GetViewportScale(this);
		if (VpSize.Y > 1.f && DpiScale > 0.f)
		{
			const float CanvasH = (float)VpSize.Y / DpiScale;
			const float TopY    = (float)SlTop->GetAnchors().Minimum.Y * CanvasH + FrameTop;
			const float MaxH    = CanvasH - TopY;
			if (MaxH > MinH && WantH > MaxH)
			{
				UE_LOG(LogAsdaLogin, Warning,
					TEXT("[одержимость] рамка 114: тексту режима %d нужно %.0f px, на экране помещается %.0f — обрезаем"),
					Mode, WantH, MaxH);
				WantH = MaxH;
			}
		}
	}
	else
	{
		// Откат по ключу либо «мерить нечем»: возвращаем АССЕТНЫЕ высоты боксов, иначе после
		//   авто-режима остались бы урезанные.
		for (const TPair<FName, float>& It : SoulInfoBaseBoxH)
		{
			if (UCanvasPanelSlot* S = AsdaUi::SlotOf(SoulInfoWidget->GetWidgetFromName(It.Key)))
			{
				FMargin O = S->GetOffsets();
				O.Bottom = It.Value;
				S->SetOffsets(O);
			}
		}
	}

	if (!AsdaUi::StretchFramePlanesHeight(Planes, WantH))
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("[одержимость] рамка 114: растянуть девятипатч не удалось"));
		return;
	}
	UE_LOG(LogAsdaLogin, Log,
		TEXT("[одержимость] рамка 114: режим %d, %s — высота %.0f px (база из ассета %.0f, строка %.1f px)"),
		Mode, bMeasured ? TEXT("по тексту") : TEXT("фикс из ассета"), WantH, SoulInfoBaseFrameH, LineH);
}

// ── ★ [ИСПРАВЛЕНИЕ] 08.11.2026 (пачка 16, баг 9, просьба владельца): ОКНО 114 ПРИ НАВЕДЕНИИ — У КУРСОРА ──
//
// ЭТАЛОН. Окно 114 не двигается никогда: `uiStatusPanel_ShowByMode@0xF083F0` (`RE/etalon/ui_core.c:
//   94102-94136`) только показывает/прячет его и меняет текст, а все вызовы из наведения
//   (`scene_handleLeftClickPick@0xF0A680`, `render_core.c:5919-6042`) позиции не трогают. Окно стоит в
//   прямоугольнике панели 17 (статы персонажа) — справа, ближе к центру экрана. Это и увидел владелец.
// ПОЧЕМУ ОТСТУПАЕМ. В режиме 0 окно 114 показывается ТОЛЬКО пока курсор над призраком («душой») или
//   над персонажем Друга, то есть по смыслу это всплывающая подсказка — а стоит далеко от предмета
//   подсказки. Владелец: «окно должно быть под курсором и следовать за ним». Делаем ровно по закону
//   тултипов порта (эталон `Tooltip_LayoutNearCursor@0xEA26F0` + `uiClampPopupToScreen@0xEA6770`,
//   та же формула, что `UAsdaTipPanel::LayoutAtCursor`): левый-верх рамки = курсор +32/+32, прижатие
//   к краям экрана с отступом 5 px, при переливе вниз — над курсором и X от курсора +5.
//   В режимах 1/2 окно — ПОСТОЯННАЯ панель на месте 17 (не подсказка), там оно остаётся на эталонном
//   месте. Раскладка в ассете НЕ меняется: двигаем весь виджет `RenderTranslation` (как перетаскивание),
//   сдвиг 0 = эталонное место.
// ОТКАТ: `asda.SoulInfo.FollowCursor 0` — окно всегда на эталонном месте.
static ASDA_DEV_CVAR(int32, CVarAsdaSoulInfoFollowCursor,
	TEXT("asda.SoulInfo.FollowCursor"), 1,
	TEXT("Окно 114 (подсказка о Друге на выборе персонажа) при наведении на призрак/персонажа Друга в режиме 0: ")
	TEXT("1 = у курсора и следует за ним, как подсказка (просьба владельца 08.11), 0 = эталонное место панели 17."),
	ECVF_Default);

// Прямоугольник рамки окна 114 БЕЗ сдвига (эталонное место) в единицах вьюпорта: по левому-верхнему (план 0)
//   и правому-нижнему (план 8) планам девятипатча. Высота уже учитывает авто-высоту `ApplySoulInfoFrameHeight`.
static bool AsdaSoulInfoFrameRect(UUserWidget* W, const FVector2D& View, FVector2D& OutPos, FVector2D& OutSize)
{
	if (!W) { return false; }
	UCanvasPanelSlot* S0 = AsdaUi::SlotOf(W->GetWidgetFromName(FName(TEXT("SOULINFO_WIN_0_0"))));
	UCanvasPanelSlot* S8 = AsdaUi::SlotOf(W->GetWidgetFromName(FName(TEXT("SOULINFO_WIN_0_8"))));
	if (!S0 || !S8) { return false; }
	auto Corner = [&](const UCanvasPanelSlot* S, bool bFar) -> FVector2D
	{
		const FAnchors A = S->GetAnchors();
		const FMargin  O = S->GetOffsets();
		const FVector2D Al = S->GetAlignment();
		// Точечный анкор (так кладёт импортёр JUI): Right/Bottom — это ширина/высота плана.
		const FVector2D Sz(O.Right, O.Bottom);
		const FVector2D TopLeft(A.Minimum.X * View.X + O.Left - Al.X * Sz.X, A.Minimum.Y * View.Y + O.Top - Al.Y * Sz.Y);
		return bFar ? TopLeft + Sz : TopLeft;
	};
	OutPos  = Corner(S0, false);
	OutSize = Corner(S8, true) - OutPos;
	return OutSize.X > 1.f && OutSize.Y > 1.f;
}

// Свободная функция (не член): заголовок режима логина общий, его не трогаем ради одной точки.
static void AsdaPlaceSoulInfoPanel(UUserWidget* SoulInfoWidget, UWorld* World, bool bAtCursor)
{
	if (!SoulInfoWidget) { return; }
	FVector2D Delta = FVector2D::ZeroVector;
	if (bAtCursor && CVarAsdaSoulInfoFollowCursor.GetValueOnGameThread() != 0)
	{
		const FVector2D View = AsdaUi::ViewportSizeUnits(World);
		FVector2D Base, Size, Cursor;
		if (View.X > 1.f && View.Y > 1.f && AsdaSoulInfoFrameRect(SoulInfoWidget, View, Base, Size))
		{
			AsdaUi::CursorInViewportUnits(World, Cursor);
			// Эталон `Tooltip_LayoutNearCursor` + `uiClampPopupToScreen` (как `UAsdaTipPanel::LayoutAtCursor`).
			float X = (float)Cursor.X + 32.f;
			float Y = (float)Cursor.Y + 32.f;
			if (X + Size.X > View.X) { X = View.X - Size.X - 5.f; }
			if (X < 0.f)             { X = 5.f; }
			if (Y < 0.f)             { Y = 5.f; }
			if (Y + Size.Y + 5.f > View.Y)
			{
				Y = View.Y - Size.Y - 5.f;
				X = (float)Cursor.X + 5.f;
				if (X + Size.X > View.X) { X = View.X - Size.X - 5.f; }
			}
			Delta = FVector2D(X, Y) - Base;
		}
	}
	if (!SoulInfoWidget->GetRenderTransform().Translation.Equals(Delta, 0.25f))
	{
		SoulInfoWidget->SetRenderTranslation(Delta);
	}
}

void AAsdaLoginGameMode::ShowSoulInfoPanel(bool bVisible, int32 Mode)
{
	// Эталон `uiStatusPanel_ShowByMode@0xF083F0` (`RE/etalon/ui_core.c:94101-94136`):
	//   режим 1 → показать 115 и 116, скрыть 117; текст 115 = склейка записей типа 0, текст 116 =
	//              склейка типа 1, причём 116 наполняется ТОЛЬКО при сброшенном флаге `+3075045`
	//              (он же `bSoulmateInfoSecondParagraph` из 1025);
	//   режим 2 → скрыть 115/116, показать 117 (склейка типа 2);
	//   в конце — видимость самого окна 114 = `bVisible`.
	UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WinMgr) { return; }
	// Гейт «не дёргать каждый кадр»: наведение опрашивается в Tick, а перестроение rich-текста
	//   недёшево. Состояние меняем только на ПЕРЕХОДАХ.
	const int32 Want = bVisible ? Mode : -1;
	if (Want == SoulPanelMode) { return; }

	if (!SoulInfoWidget)
	{
		if (!bVisible) { SoulPanelMode = -1; return; }   // прятать нечего — окно ещё не создавалось
		// ★ Окно 114 УЖЕ ЕСТЬ в проекте: `WBP_SOULINFO_WIN` собран генерик-импортёром JUI вместе с
		//   остальными 25 окнами логина (закон `check-existing-before-creating` — второго не заводим).
		//   ZOrder 16 = ровно над монолитом char-select (15): панели 17 и 114 в оригинале занимают
		//   ОДИН И ТОТ ЖЕ прямоугольник (обе 270×405, анкоры `16 86 34` / `32 86 34`).
		SoulInfoWidget = WinMgr->OpenWindowByPath(FName(TEXT("SOULINFO_WIN")),
			TEXT("/Game/AsdaAssets/UI/Login/WBP_SOULINFO_WIN"), 16, ESlateVisibility::SelfHitTestInvisible);
		if (!SoulInfoWidget)
		{
			UE_LOG(LogAsdaLogin, Warning, TEXT("WBP_SOULINFO_WIN не загружен — панель «одержимости» не показать"));
			return;
		}
	}

	if (bVisible)
	{
		UGameInstance* GI = GetGameInstance();
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		// Текст — ТОЛЬКО из таблицы DT_soulsoulmatetext по полю Type (в коде игровых строк нет).
		//   Стиль контрола снимает и передаёт общая точка `ApplyRichWithJuiStyle` (см. её комментарий).
		auto SetRich = [&](const TCHAR* Node, const FString& Raw)
		{
			ApplyRichWithJuiStyle(SoulInfoWidget->GetWidgetFromName(FName(Node)), Raw);
		};
		if (Mode == 1)
		{
			SetRich(TEXT("SOULINFO_TXT00"), FAsdaPossessData::SoulInfoText(GI, 0));
			// Второй абзац наполняется только при сброшенном флаге ветки соулмейта (эталон).
			if (!Net || !Net->bSoulmateInfoSecondParagraph)
			{
				SetRich(TEXT("SOULINFO_TXT01"), FAsdaPossessData::SoulInfoText(GI, 1));
			}
			AsdaCommunityUi::SetVisByPrefix(SoulInfoWidget, TEXT("SOULINFO_TXT00"), true);
			AsdaCommunityUi::SetVisByPrefix(SoulInfoWidget, TEXT("SOULINFO_TXT01"), true);
			AsdaCommunityUi::SetVisByPrefix(SoulInfoWidget, TEXT("SOULINFO_TXT02"), false);
		}
		else if (Mode == 2)
		{
			SetRich(TEXT("SOULINFO_TXT02"), FAsdaPossessData::SoulInfoText(GI, 2));
			AsdaCommunityUi::SetVisByPrefix(SoulInfoWidget, TEXT("SOULINFO_TXT00"), false);
			AsdaCommunityUi::SetVisByPrefix(SoulInfoWidget, TEXT("SOULINFO_TXT01"), false);
			AsdaCommunityUi::SetVisByPrefix(SoulInfoWidget, TEXT("SOULINFO_TXT02"), true);
		}
		// ★ Высота рамки — ПОСЛЕ наполнения текстом: считать её можно только по тому, что уже
		//   показано (обоснование рантайм-правки — в блок-комментарии над функцией).
		ApplySoulInfoFrameHeight(Mode);
	}
	// ★ 08.11.2026: при гашении — сдвиг «у курсора» снять, чтобы следующий показ НЕ из наведения (режим 1
	//   на месте панели 17) не унаследовал позицию подсказки.
	if (!bVisible) { AsdaPlaceSoulInfoPanel(SoulInfoWidget, GetWorld(), false); }
	WinMgr->SetWindowVisible(FName(TEXT("SOULINFO_WIN")), bVisible);
	SoulPanelMode = Want;
}

void AAsdaLoginGameMode::AssignPartnersToSlots()
{
	// Эталон `charSlotUpdateStateFlags@0xF06600` → `assignSortedTopValuesToSlots@0xF0D000`:
	//   персонажи соулмейта сортируются ПО УРОВНЮ ПО УБЫВАНИЮ и раздаются по ЗАНЯТЫМ моим слотам
	//   сверху вниз. Слот без моего персонажа партнёра не получает (там стоит глиф создания).
	for (int32 i = 0; i < 3; ++i) { SlotPartnerCharidx[i] = 0; }

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	TArray<const FAsdaSoulmateSharedChar*> Sorted;
	for (const FAsdaSoulmateSharedChar& R : Net->GetSoulmateSharedChars())
	{
		if (!R.IsEmpty()) { Sorted.Add(&R); }
	}
	if (Sorted.Num() == 0) { return; }
	// StableSort: при равных уровнях порядок остаётся исходным (Charidx 10→11→12), как у эталона,
	//   который тоже перебирает массив по порядку и лишь вытесняет меньшие значения.
	Sorted.StableSort([](const FAsdaSoulmateSharedChar& A, const FAsdaSoulmateSharedChar& B)
	{
		return A.Level > B.Level;
	});

	int32 Next = 0;
	for (int32 i = 0; i < 3 && Next < Sorted.Num(); ++i)
	{
		const bool bMine = Net->CharList.IsValidIndex(i) && !Net->CharList[i].IsEmpty();
		if (!bMine) { continue; }
		SlotPartnerCharidx[i] = Sorted[Next++]->Charidx;
	}
	UE_LOG(LogAsdaLogin, Log, TEXT("[одержимость] раздача партнёров по слотам: %u / %u / %u (доступно записей %d)"),
		(uint32)SlotPartnerCharidx[0], (uint32)SlotPartnerCharidx[1], (uint32)SlotPartnerCharidx[2], Sorted.Num());
}

AActor* AAsdaLoginGameMode::SpawnPartnerInSlot(int32 SlotIdx, const FAsdaSoulmateSharedChar& Rec, uint8 MySex)
{
	UWorld* World = GetWorld();
	if (!World || SlotIdx < 0 || SlotIdx > 2) { return nullptr; }

	const bool bMyMale  = FAsdaPossessData::IsMale(MySex);
	const bool bHisMale = FAsdaPossessData::IsMale(Rec.Gender);
	const int32 ClipIdx = FMath::Clamp(PartnerClipState[SlotIdx], 0, 4);

	// ★★★ ПАРНЫЙ КЛИП — УСЛОВИЕ СПАВНА, А НЕ УКРАШЕНИЕ. Смещение «встал рядом» в оригинале живёт
	//   в КОРНЕВОЙ КОСТИ клипа (доказано измерением: `cre_cremmy-a_00_0` корень (-9.17, 82.65, 3.97),
	//   `cre_cremmman-a_00_0` — (15.82, 82.56, -35.19) на той же базе), а собственной матрицы у
	//   объекта партнёра НЕТ: `channelSelectRender3D@0xF07BE0` позиционирует только моего персонажа.
	//   Без клипа партнёр встанет ВНУТРЬ моего персонажа — это не «чуть хуже», это сломанная сцена.
	//   Поэтому: нет ассета клипа → не спавним и честно пишем, чего не хватает.
	const FString ClipPath = FAsdaPossessData::PairClipAssetPath(bMyMale, bHisMale, ClipIdx);
	UAnimSequence* PairClip = LoadClip(ClipPath);
	if (!PairClip)
	{
		UE_LOG(LogAsdaLogin, Warning,
			TEXT("[одержимость] слот %d: парный клип не найден (%s ← исходник %s) — персонаж соулмейта '%s' НЕ показан. ")
			TEXT("Нужен экспорт/импорт парных клипов cre_cre<мой><его><man|woman> из CharMotion.baz."),
			SlotIdx, *ClipPath, *FAsdaPossessData::PairClipSourceName(bMyMale, bHisMale, ClipIdx), *Rec.CharName);
		return nullptr;
	}

	// База слота — ТА ЖЕ кость пьедестала, на которой стоит мой персонаж (эталон: партнёр —
	//   дочерний узел моего объекта, `posSetWithChildOffset` двигает обоих одной матрицей).
	USkeletalMeshComponent* CaskComp = CasketActor ? CasketActor->GetSkeletalMeshComponent() : nullptr;
	const FName SlotBone = CarouselPedestalBone(SlotIdx, SelectedSlot);
	FTransform Base = AsdaLoginPedestalBase(CasketActor, CaskComp, SlotBone);   // ★ R5: общая база слота шкатулки
	const FVector SpawnLoc = Base.TransformPosition(SlotLocalOffset);

	FActorSpawnParameters SP;
	AActor* Partner = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnLoc, Base.Rotator(), SP);
	if (!Partner) { return nullptr; }

	USceneComponent* Root = NewObject<USceneComponent>(Partner, TEXT("PartnerRoot"));
	Root->RegisterComponent();
	Partner->SetRootComponent(Root);
	Partner->SetActorLocationAndRotation(SpawnLoc, Base.Rotator());
	// Тот же масштаб и то же зеркало по Y, что у моего персонажа (иначе партнёр «зеркальный»).
	Root->SetWorldScale3D(FVector(CharacterScale) * FVector(-1.f, 1.f, 1.f));

	// Меш — по ЕГО полу (пол приходит в записи 1023, rec+22).
	const TCHAR* MeshPath = bHisMale ? GCharPreviewMesh : GCharPreviewMeshF;
	USkeletalMesh* SkMesh = LoadMesh(MeshPath);
	if (!SkMesh)
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("[одержимость] слот %d: превью-меш %s не загружен"), SlotIdx, MeshPath);
		Partner->Destroy();
		return nullptr;
	}
	USkeletalMeshComponent* SkC = NewObject<USkeletalMeshComponent>(Partner, TEXT("CharSkel"));
	SkC->SetupAttachment(Root);
	SkC->SetSkeletalMesh(SkMesh);
	SkC->SetRelativeTransform(FTransform::Identity);
	SkC->SetCastShadow(true);
	SkC->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	SkC->RegisterComponent();
	// Клипы 0 и 2 — петлевые idle, 1 и 3 — one-shot переходы (эталон: по концу 1→2, 3→0).
	SkC->PlayAnimation(PairClip, /*bLooping=*/(ClipIdx == 0 || ClipIdx == 2));

	// Внешность и надетое — ТОЙ ЖЕ системой сборки, что и у своего персонажа (закон
	//   `check-existing-before-creating`: второй сборки не заводим). Эталон делает так же:
	//   `playerCharObjectLoadColored@0xF0A2B0` зовётся для объекта соулмейта тем же кодом.
	UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (Items)
	{
		const FAsdaWornParts Parts = AsdaLoginPossess::WornPartsFrom(Rec.Equip, Rec.Hair, Rec.HairColor, Rec.Face);
		const int32 nParts = UAsdaCharAssembly::BuildWornParts(Partner, Root, SkC, bHisMale, Parts, Items);
		UE_LOG(LogAsdaLogin, Log, TEXT("[одержимость] слот %d: '%s' (пол=%u, ур.%u) надето частей=%d"),
			SlotIdx, *Rec.CharName, (uint32)Rec.Gender, (uint32)Rec.Level, nParts);
	}

	ApplyPreviewStableQuality(Partner);   // ★ charsel_preload: LOD0 + полные мипы

	if (CaskComp && CaskComp->DoesSocketExist(SlotBone))
	{
		Partner->AttachToComponent(CaskComp, FAttachmentTransformRules::KeepWorldTransform, SlotBone);
	}
	else if (CasketActor && CasketActor->GetRootComponent())
	{
		Partner->AttachToActor(CasketActor, FAttachmentTransformRules::KeepWorldTransform);
	}
	return Partner;
}

// ★★★ КЛЮЧИ призрака («душа», объект `+946128`). ИСТОРИЯ РЕШЕНИЙ ВЛАДЕЛЬЦА:
//   • 03.09.2026 (утро): «призраков (силуэты) не отображать» — это касалось НАШЕГО СЕРОГО силуэта
//     (Unlit + серый тинт + emissive-буст — выдуманный вид); дефолт был 0.
//   • 03.09.2026 (позже): «режим „душа“ ОЖИВЛЯЕМ; там другие силуэты» — реверс
//     (`Reports/soul_silhouette_visual_re.md` §5.2, §8) подтвердил: призрак — ОСОБЫЙ меш
//     `cre_cresoulman/woman-a_00` со СВОЕЙ сине-облачной текстурой, освещённый, односторонний, без
//     тинта. Материал `M_AsdaSoulSilhouette` пересобран по эталону (`Tools/asda_build_soul_silhouette_mat.py`,
//     отчёт `audit_2026-09-02/fix_soul_ghost_charselect.md`).
//   • 03.09.2026 (вечер, по скриншоту): «пустой персонаж друга (силуэт) выбираешь — игра начинает
//     загрузку. Бред!» ⇒ фигура — ЗАГЛУШКА МЕСТА ДРУГА: клик по ней даёт только модалку loc 990136
//     (`OnScenePickClicked`), а режим 1 выбирается модалкой на «Играть» (`ShowEnterModeChoice`,
//     ключ `asda.Login.AskEnterMode`). Наведение (панель 114 режим 1, подписи 22/23) оставлено как было.
//   ⇒ ДЕФОЛТ = 1: заглушка показана. 0 = поведение живого оригинала (загрузчик `@0xF05420` мёртв,
//   объект пуст): `SpawnSoulInSlot` выходит первой строкой — нет ни меша, ни ауры, ни хит-зоны
//   (`SoulActors[i]` = null ⇒ луч по «душе» не бьют), ни клипа, ни тика ауры (`TickSoulAura` выходит,
//   пока все `SoulAuraMIDs` пусты). Вход в режим 1 от ключа НЕ зависит (он на «Играть»).
static ASDA_DEV_CVAR(int32, CVarAsdaSoulSilhouette,
	TEXT("asda.Login.SoulSilhouette"), 1,
	TEXT("Призрак («душа») рядом со своим персонажем на выборе персонажа — вход в режим 1: 1 = показывать ")
	TEXT("(по умолчанию, решение владельца 03.09.2026 «режим душа оживляем», вид по эталону); ")
	TEXT("0 = как в живом оригинале с мёртвым загрузчиком — не показывать, режим 1 недостижим."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaSoulSilhouetteOppositeSex,
	TEXT("asda.Login.SoulSilhouetteOppositeSex"), 0,
	TEXT("Пол призрака («душа»): 0 = МОЙ (по умолчанию — эталон `0xF054C6` и решение владельца 03.09.2026 ")
	TEXT("«для ghost-режима — свой пол»); 1 = противоположный (прежнее решение от 02.09, когда призрак был серой заглушкой)."),
	ECVF_Default);

// ★★★ АУРА силуэта (glow-shell). НАШ визуал по ЗАМЫСЛУ оригинала: в эталоне pbb-проход `+0xE6FE4`
//   рисует свечение-оболочку поверх тела силуэта (аддитив/unlit/two-sided, дымка `silhoutte_tex_blue`,
//   дыхание 2000 мс), но загрузчик силуэта в образе мёртв ⇒ на экране этого нет. Разбор и дизасм —
//   `Reports/soulmate_soul_aura_render.md` + `audit_2026-09-02/fix_soul_aura_impl.md`.
//   Реализуем оверлей-материалом на ТОМ ЖЕ меше силуэта (SetOverlayMaterial) — «второй проход по
//   тому же мешу», а не отдельный объект. Числа дыхания 1:1 с эталоном; перевод толщины в UE-масштаб
//   и знак V-скролла — ДОПУЩЕНИЯ (помечены). Крутилки — ниже, за ключами.
static const TCHAR* kSoulAuraMaster = TEXT("/Game/Materials/M_AsdaSoulAura.M_AsdaSoulAura");
// ★ 03.09.2026 — вместе с оживлением режима «душа» дефолт 1: дымка `silhoutte_tex_blue` — pbb-проход,
//   заложенный в загрузчик призрака (`0xF0571B` ставит узел с этой текстурой, `0xF05819`
//   `pbbBuildStripBatch`), т.е. часть задуманного вида, а не наша выдумка. Без силуэта ауре не на
//   чем висеть (MID создаётся внутри `SpawnSoulInSlot`). 0 = выключить.
static ASDA_DEV_CVAR(int32, CVarAsdaSoulAura,
	TEXT("asda.Login.SoulAura"), 1,
	TEXT("Дымка (pbb-оболочка silhoutte_tex_blue) призрака «души»: 1 = вкл (по умолчанию, замысел загрузчика ")
	TEXT("0xF05420; работает только вместе с asda.Login.SoulSilhouette 1), 0 = выкл."),
	ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaSoulAuraPeriodMs,
	TEXT("asda.Login.SoulAuraPeriodMs"), 2000.f,
	TEXT("Период «дыхания» ауры, мс (эталон 2000). Толщина и альфа пульсируют треугольником/ping-pong за период."),
	ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaSoulAuraThickness,
	TEXT("asda.Login.SoulAuraThickness"), 1.f,
	TEXT("Множитель амплитуды толщины ауры (НАШ дизайн; 1.0 = как в эталоне относительно высоты меша). "
	     "Цвет ауры — параметр TintColor мастера M_AsdaSoulAura (дефолт 0.5,1,1)."),
	ECVF_Default);

// ★★★ «ДУША»: третий объект слота (эталонный `+946128`).
//
// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА №1 — ПОМЕЧЕНО (решение владельца: «силуэт остаётся»).
//   ЧТО ДЕЛАЕТ ОРИГИНАЛ: НЕ ПОКАЗЫВАЕТ силуэт НИКОГДА. Доказано по БИНАРЮ, а не по декомпилу
//   (закон `false-negatives-method`). Модели силуэта грузит ровно одна функция —
//   `CEntity_LoadCreatureMorphModels_Cremmman@0xF05930` (`RE/etalon/file_vfs.c:1447`,
//   `cre_cremmsoul` / `cre_crewwsoul`); её единственный вызов идёт из обёртки `0xF05420`
//   (в срез декомпила обёртка не попала — лежит внутри диапазона, помеченного как
//   `CharCreate_LoadCreationEffect`; начало функции — пролог `55 8B EC` после `CC`-паддинга).
//   ПОЛНЫЙ ПОИСК ССЫЛОК ПО ОБРАЗУ `AsdaStory.exe` (13 344 768 б, эталонная сборка IDB):
//     • ни одного `E8`/`E9 rel32` на `0xF05420` во всей `.text` (просканированы все 0xA73000 байт);
//     • ни одного абсолютного DWORD `20 54 F0 00` НИГДЕ в файле ⇒ и в vtable его нет, то есть
//       виртуальным вызовом обёртка тоже недостижима.
//   ⇒ У ОРИГИНАЛА ОБЪЕКТ `+946128` НИКОГДА НЕ ПОЛУЧАЕТ МЕША. `charDealWndSetSlotEnable@0xF08560`
//   (`entity_char.c:51653-51663`) действительно ВКЛЮЧАЕТ его при `playerFindSubRecordByType == null`,
//   но включает ПУСТОЙ объект — на экране рядом с персонажем не появляется ничего.
//   ⚠ Тем же способом проверена обёртка парных клипов `0xF05AE0` (единственный вызов
//   `CEntity_LoadCreatureMorphMotions@0xF05BF0`) — ссылок на неё в образе тоже НЕТ.
//   ЧТО ДЕЛАЕМ МЫ (03.09.2026, решение владельца «режим „душа“ оживляем»): ПОКАЗЫВАЕМ призрака
//   ровно таким, каким его задумал мёртвый загрузчик (`Reports/soul_silhouette_visual_re.md` §5.2):
//   меш `cre_cresoulman/woman-a_00` со своей сине-облачной текстурой, освещённый/односторонний/без
//   тинта (мастер `M_AsdaSoulSilhouette`, [ПОРТ] Opacity = tex.a × 0.3 по α «души» в мире), OBB
//   (25,75,25) → хит-бокс по позе (`ComputePickBox`), дочерний узел к моему персонажу (та же кость
//   пьедестала), idle-«дыхание» — клип со стартовым состоянием 2, pbb-дымка `silhoutte_tex_blue`
//   overlay-проходом. Ключ `asda.Login.SoulSilhouette` по умолчанию 1; 0 = живой оригинал (пусто).
//
// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА №2 — ПОМЕЧЕНО (решение владельца: «пол силуэта — противоположный»).
//   ЧТО ДЕЛАЕТ ОРИГИНАЛ: у него есть ТОЛЬКО ОДНОПОЛЫЕ наборы — `cre_cremmman`+`cre_cremmsoul`
//   (мужчина) и `cre_crewwwoman`+`cre_crewwsoul` (женщина), выбор по `a3 == 1`.
//   ЧТО ДЕЛАЕМ МЫ: мужчине показываем ЖЕНСКИЙ силуэт, женщине — МУЖСКОЙ.
//   ★ ДАННЫЕ НЕ ВЫДУМАНЫ: смешанные пары лежат в том же `CharMotion.baz` авторскими файлами —
//     `cre_cremwwoman-a_00_{0..3}` + `cre_cremwsoul-a_00_4` и `cre_crewmman-a_00_{0..3}` +
//     `cre_crewmsoul-a_00_4`; собраны в ассеты `cre_cresoulmwwoman_preview` / `cre_cresoulwmman_preview`
//     (`Tools/export_char_possess.py`, план `SOUL_PLAN`).
//   ★ ЗАЗОР «ВСТАЛ РЯДОМ» ПРИ ЭТОМ АВТОРСКИЙ, ПРОВЕРЕНО ЗАМЕРОМ: корень кадра 0 клипа
//     `cre_cresoulmwwoman_previewanim_02` = (313.20, 2076.02, 8352.24) — БАЙТ В БАЙТ равен корню
//     парного клипа настоящего партнёра `ch1_f_cremmy_previewanim_212`; `cre_cresoulwmman_previewanim_02`
//     = (5000.43, 2847.18, 8461.32) равен `ch1_m_cremmy_previewanim_217`. То есть силуэт встаёт
//     ровно туда, куда встал бы живой персонаж соулмейта той же пары.
//   ★ ПЕРЕСМОТРЕНО 03.09.2026: пол призрака по умолчанию СНОВА МОЙ (эталон `0xF054C6`; владелец:
//     «для ghost-режима — свой пол»). Противоположный пол остался за ключом
//     `asda.Login.SoulSilhouetteOppositeSex 1` — это было решение от 02.09 для серой заглушки.
//   ★ УСТАРЕЛО с 03.09.2026: «пакет 1016 в режиме 1 не отправляется ни у нас, ни у эталона» — у
//     эталона по-прежнему не отправляется (`entity_char.c:51784-51790`), а у НАС теперь
//     отправляется (`SendSelectChar(Mode=1)`, ключ `asda.Soul.AllowEnter`, решение владельца
//     «режим душа оживляем»). Пол призрака на сервер не влияет в любом случае.
//
//   Механика: смещение «встал рядом» приходит из КОРНЯ клипа, поэтому без клипа силуэт не спавним —
//   он встал бы внутрь моего персонажа.
AActor* AAsdaLoginGameMode::SpawnSoulInSlot(int32 SlotIdx, uint8 MySex)
{
	UWorld* World = GetWorld();
	if (!World || SlotIdx < 0 || SlotIdx > 2) { return nullptr; }
	if (CVarAsdaSoulSilhouette.GetValueOnGameThread() == 0) { return nullptr; }   // откат к эталону

	const bool bMyMale = FAsdaPossessData::IsMale(MySex);
	const bool bOpposite = (CVarAsdaSoulSilhouetteOppositeSex.GetValueOnGameThread() != 0);
	const int32 ClipIdx = FMath::Clamp(SoulClipState[SlotIdx], 0, 4);

	const FString ClipPath = FAsdaPossessData::SoulClipAssetPath(bMyMale, bOpposite, ClipIdx);
	const FString MeshPath = FAsdaPossessData::SoulMeshAssetPath(bMyMale, bOpposite);
	UAnimSequence* SoulClip = LoadClip(ClipPath);
	USkeletalMesh* SkMesh = LoadMesh(MeshPath);
	if (!SoulClip || !SkMesh)
	{
		UE_LOG(LogAsdaLogin, Warning,
			TEXT("[одержимость] слот %d: ассетов «души» нет (меш %s %s, клип %s %s) — силуэт не показан. ")
			TEXT("Нужен прогон Tools/export_char_possess.py + Tools/import_char_possess.py."),
			SlotIdx, *MeshPath, SkMesh ? TEXT("есть") : TEXT("НЕТ"),
			*ClipPath, SoulClip ? TEXT("есть") : TEXT("НЕТ"));
		return nullptr;
	}

	// База слота — ТА ЖЕ кость пьедестала, что у моего персонажа (эталон двигает всю тройку
	//   объектов слота одной матрицей `posSetWithChildOffset`).
	USkeletalMeshComponent* CaskComp = CasketActor ? CasketActor->GetSkeletalMeshComponent() : nullptr;
	const FName SlotBone = CarouselPedestalBone(SlotIdx, SelectedSlot);
	FTransform Base = AsdaLoginPedestalBase(CasketActor, CaskComp, SlotBone);   // ★ R5: общая база слота шкатулки
	const FVector SpawnLoc = Base.TransformPosition(SlotLocalOffset);

	FActorSpawnParameters SP;
	AActor* Soul = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnLoc, Base.Rotator(), SP);
	if (!Soul) { return nullptr; }

	USceneComponent* Root = NewObject<USceneComponent>(Soul, TEXT("SoulRoot"));
	Root->RegisterComponent();
	Soul->SetRootComponent(Root);
	Soul->SetActorLocationAndRotation(SpawnLoc, Base.Rotator());
	// Тот же масштаб и то же зеркало по Y, что у персонажей слота.
	Root->SetWorldScale3D(FVector(CharacterScale) * FVector(-1.f, 1.f, 1.f));

	USkeletalMeshComponent* SkC = NewObject<USkeletalMeshComponent>(Soul, TEXT("CharSkel"));
	SkC->SetupAttachment(Root);
	SkC->SetSkeletalMesh(SkMesh);
	SkC->SetRelativeTransform(FTransform::Identity);
	SkC->SetCastShadow(true);   // тень у оригинала полная, непрозрачная (shadow-pass гасит ALPHABLENDENABLE, :422630)
	SkC->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// [ПОРТ] У оригинала translucent-проход персонажа ПИШЕТ глубину (ZWRITEENABLE=1 из
	//   `CRenderer_ApplyAlphaFogRenderState :18578`) и сортируется по dist² (`CRenderQueue_AddObject :857200`).
	//   В UE translucent глубину не пишет — ближайший эквивалент по §8 разбора: приоритет сортировки
	//   выше окружения (сцена логина = 0), чтобы призрак не проваливался под прозрачные объекты сцены.
	SkC->SetTranslucentSortPriority(1);
	SkC->RegisterComponent();
	// Idle-«дыхание» призрака: стартовое состояние клипа 2 (`AsdaStory_game.c:528328`, `SoulClipState`
	//   инициализируется 2 в `DestroyPossessScene`), клип зациклен; при смене персонажа/повороте карусели
	//   актор НЕ пересоздаётся — его перепривязывает `AttachSlotCompanionsToBone` (нет «моргания»).
	SkC->PlayAnimation(SoulClip, /*bLooping=*/(ClipIdx == 0 || ClipIdx == 2));

	// ★★★ АУРА (glow-shell) — overlay-проход по ТОМУ ЖЕ мешу силуэта (эталонный pbb `+0xE6FE4`).
	//   Это НЕ отдельный billboard/квад: SetOverlayMaterial перерисовывает тот же скиннутый меш
	//   аддитивным unlit-материалом с выдавливанием по нормали — ровно «второй проход» оригинала.
	//   Дыхание (Thickness/AuraAlpha) гонит TickSoulAura; TintColor/ScrollV — статические крутилки.
	SoulAuraMIDs[SlotIdx] = nullptr;
	SoulAuraHeightScale[SlotIdx] = 1.f;
	if (CVarAsdaSoulAura.GetValueOnGameThread() != 0)
	{
		if (UMaterialInterface* AuraMaster = LoadObject<UMaterialInterface>(nullptr, kSoulAuraMaster))
		{
			UMaterialInstanceDynamic* AuraMID = UMaterialInstanceDynamic::Create(AuraMaster, Soul);
			if (AuraMID)
			{
				AuraMID->SetVectorParameterValue(TEXT("TintColor"), FLinearColor(0.5f, 1.f, 1.f, 1.f)); // c91.rgb эталона
				AuraMID->SetScalarParameterValue(TEXT("ScrollV"), 0.5f);                                 // +0.5 UV/с эталона
				SkC->SetOverlayMaterial(AuraMID);
				// Толщина эталона 0.5..1.0 ед. модели при высоте ≈150 ед. Переводим в см UE
				//   пропорционально фактической высоте меша силуэта (габариты ассета × масштаб слота).
				//   ДОПУЩЕНИЕ (зависит от импортёра). WPO = normal(единичный) × Thickness_см.
				const float HalfZ = (float)SkMesh->GetBounds().BoxExtent.Z;
				const float HeightUE = FMath::Max(1.f, HalfZ * 2.f * FMath::Abs(CharacterScale));
				SoulAuraHeightScale[SlotIdx] = HeightUE / 150.f;
				SoulAuraMIDs[SlotIdx] = AuraMID;
			}
		}
		else
		{
			UE_LOG(LogAsdaLogin, Warning,
				TEXT("[одержимость] слот %d: мастер ауры %s не найден — аура не показана "
				     "(нужен прогон Tools/asda_build_soul_aura_mat.py)."), SlotIdx, kSoulAuraMaster);
		}
	}

	ApplyPreviewStableQuality(Soul);   // ★ charsel_preload: LOD0 + полные мипы

	if (CaskComp && CaskComp->DoesSocketExist(SlotBone))
	{
		Soul->AttachToComponent(CaskComp, FAttachmentTransformRules::KeepWorldTransform, SlotBone);
	}
	else if (CasketActor && CasketActor->GetRootComponent())
	{
		Soul->AttachToActor(CasketActor, FAttachmentTransformRules::KeepWorldTransform);
	}
	UE_LOG(LogAsdaLogin, Log, TEXT("[одержимость] слот %d: «душа» показана — мой пол %s, силуэт %s (набор %s)"),
		SlotIdx, bMyMale ? TEXT("М") : TEXT("Ж"),
		(bMyMale != bOpposite) ? TEXT("М") : TEXT("Ж"), *FPaths::GetCleanFilename(MeshPath));
	return Soul;
}

void AAsdaLoginGameMode::DestroyPossessScene()
{
	for (AActor* A : PartnerActors) { if (A) { A->Destroy(); } }
	PartnerActors.Reset();
	PartnerActors.SetNum(3);
	for (AActor* A : SoulActors) { if (A) { A->Destroy(); } }
	SoulActors.Reset();
	SoulActors.SetNum(3);
	// Ауру гасим вместе с силуэтами: MID приаутерены к актору «души», снос актора их соберёт GC;
	//   здесь только обнуляем указатели, чтобы TickSoulAura не трогал висячие.
	for (int32 i = 0; i < 3; ++i) { SoulAuraMIDs[i] = nullptr; SoulAuraHeightScale[i] = 1.f; }
	for (int32 i = 0; i < 3; ++i)
	{
		// Стартовые значения — эталонные (`AsdaStory_game.c:528327-528329`): мой клип 0, «души» 2,
		//   партнёра 2, режим слота 0 («выбран мой персонаж»).
		SlotMode[i] = 0;
		MyClipState[i] = 0;
		SoulClipState[i] = 2;
		PartnerClipState[i] = 2;
		ClipOneShotLeft[i] = 0.f;
	}
	bPossessHoverMyChar = false;
	bPossessHoverPartner = false;
	bPossessHoverSoul = false;
	ShowSoulInfoPanel(false, -1);
}

// ═══ ★★★ ПРЕДЗАГРУЗКА ЭКРАНА ВЫБОРА ПЕРСОНАЖА (задача владельца 13.09.2026: «на выборе персонажа видна
//   прогрузка деталей персонажей»; замеры и кадры — `Reports/charsel_preload/`) ═══════════════════════
//   БЫЛО (-game, ASDA_TEST_FLOW): кадр появления превью 223 мс и 63 пакета, загруженных в нём синхронно;
//   текстуры появлялись в 64×64 и дотягивались кадрами позже, часть так и оставалась в половинном размере.
//   СТАЛО: пути собираются ТЕМИ ЖЕ функциями, что строят превью (`UAsdaCharAssembly::CollectWornPartPaths` —
//   зеркало `BuildWornParts`; `FAsdaPossessData::*AssetPath`; база и клипы превью), и отдаются менеджеру
//   загрузки: фон, полные мипы/LOD, сильная ссылка до выхода с экрана. Точки вызова: успешный вход (база),
//   список своих персонажей (1006/1007), персонажи соулмейта (1023/1024). Перелёт и открытие шкатулки идут
//   ≈5.3 с — к появлению превью всё в памяти и в полном размере.
void AAsdaLoginGameMode::RequestCharSelectPreload()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	if (!LM) { return; }
	if (!UAsdaLoadManagerSubsystem::IsCharSelectPreloadEnabled()) { ReleaseCharSelectPreload(); return; }
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	UAsdaItemTableSubsystem* Items = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	const bool bTestFlow = (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_FLOW")) == TEXT("1"));

	TArray<FString> P;
	// Пакетный путь → объектный («пакет.имя») — так его достраивает и менеджер моделей.
	auto Obj = [](const FString& Pkg) { return Pkg + TEXT(".") + FPaths::GetBaseFilename(Pkg); };

	// 1. Не зависит от данных: база превью ОБОИХ полов (свой пол и пол соулмейта заранее неизвестны), клипы моего
	//    персонажа anim_00..04 (`ApplyClipStates`), стоковые кисти, материал фейда появления, окно выбора.
	for (const bool bMale : { true, false })
	{
		P.AddUnique(Obj(bMale ? GCharPreviewMesh : GCharPreviewMeshF));
		for (int32 c = 0; c <= 4; ++c)
		{
			P.AddUnique(Obj(FString::Printf(TEXT("%sanim_%02d"), bMale ? GCharPreviewAnimBase : GCharPreviewAnimBaseF, c)));
		}
		const FString Hands = UAsdaCharAssembly::DefaultHandsMeshName(bMale);
		P.AddUnique(UAsdaCharAssembly::EquipPartMeshPath(Hands) + TEXT(".") + Hands);
	}
	P.AddUnique(kPreviewFadeMaterial);
	P.AddUnique(Obj(kCharSelectWidgetPath) + TEXT("_C"));

	// 2. Мои персонажи — та же раскладка, что у `SpawnOneCharSlot` → `ApplyEquipToSlot` (меш по Sex != 2).
	TArray<bool> MyPossessMale;   // полы моих занятых слотов по правилу «одержимости» (парные клипы, «душа»)
	for (int32 i = 0; i < 3; ++i)
	{
		if (Net && Net->CharList.IsValidIndex(i) && !Net->CharList[i].IsEmpty())
		{
			const FAsdaCharRecord& R = Net->CharList[i];
			UAsdaCharAssembly::CollectWornPartPaths(R.Sex != 2,
				AsdaLoginPossess::WornPartsFrom(R.Equip, R.Hair, R.Color, R.Face), Items, P);
			MyPossessMale.AddUnique(FAsdaPossessData::IsMale(R.Sex));
		}
		else if (bTestFlow && i == 0)
		{
			// Регресс-прогон без сервера: синтетический мужской персонаж слота 0 (как `SpawnCharacterInSlot`).
			UAsdaCharAssembly::CollectWornPartPaths(true, AsdaLoginPossess::TestFlowWornParts(), Items, P);
			const uint8 MySex = (Net && Net->CharList.IsValidIndex(0)) ? Net->CharList[0].Sex : 1;   // как SetupPossessScene
			MyPossessMale.AddUnique(FAsdaPossessData::IsMale(MySex));
		}
	}

	// 3. Персонажи соулмейта — их внешность и парные клипы (`SpawnPartnerInSlot`). Какой слот кому достанется,
	//    решает раздача по уровню при спавне, поэтому парные клипы — на каждый пол моих персонажей.
	TArray<FAsdaSoulmateSharedChar> Partners;
	if (Net)
	{
		for (const FAsdaSoulmateSharedChar& R : Net->GetSoulmateSharedChars()) { if (!R.IsEmpty()) { Partners.Add(R); } }
	}
	if (bTestFlow && MyPossessMale.Num() > 0)
	{
		FAsdaSoulmateSharedChar Fake;   // тот же синтетический партнёр, что ставит SetupPossessScene
		Fake.Gender = MyPossessMale[0] ? 2 : 1;
		Partners.Add(Fake);
	}
	for (const FAsdaSoulmateSharedChar& R : Partners)
	{
		const bool bHisMale = FAsdaPossessData::IsMale(R.Gender);
		UAsdaCharAssembly::CollectWornPartPaths(bHisMale,
			AsdaLoginPossess::WornPartsFrom(R.Equip, R.Hair, R.HairColor, R.Face), Items, P);
		for (const bool bMyMale : MyPossessMale)
		{
			for (int32 c = 0; c <= 4; ++c)
			{
				const FString Clip = FAsdaPossessData::PairClipAssetPath(bMyMale, bHisMale, c);
				if (!Clip.IsEmpty()) { P.AddUnique(Obj(Clip)); }
			}
		}
	}

	// 4. «Душа» — заглушка места друга (`SpawnSoulInSlot`), если её показ включён.
	if (CVarAsdaSoulSilhouette.GetValueOnGameThread() != 0)
	{
		const bool bOpposite = (CVarAsdaSoulSilhouetteOppositeSex.GetValueOnGameThread() != 0);
		for (const bool bMyMale : MyPossessMale)
		{
			P.AddUnique(Obj(FAsdaPossessData::SoulMeshAssetPath(bMyMale, bOpposite)));
			for (int32 c = 0; c <= 4; ++c) { P.AddUnique(Obj(FAsdaPossessData::SoulClipAssetPath(bMyMale, bOpposite, c))); }
		}
		if (MyPossessMale.Num() > 0 && CVarAsdaSoulAura.GetValueOnGameThread() != 0) { P.AddUnique(kSoulAuraMaster); }
	}

	LM->StartCharSelectPreload(P, FSimpleDelegate::CreateWeakLambda(this, [this]() { OnCharSelectPreloadLoaded(); }));
}

void AAsdaLoginGameMode::ReleaseCharSelectPreload()
{
	if (CharSelPsoWarmActor) { CharSelPsoWarmActor->Destroy(); CharSelPsoWarmActor = nullptr; }
	UAsdaTextureSubsystem::ReleaseAllFullMips(CharSelPreviewMips);   // доля превью во флаге полных мипов
	UGameInstance* GI = GetGameInstance();
	if (UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr)
	{
		LM->ReleaseCharSelectPreload();
	}
}

void AAsdaLoginGameMode::OnCharSelectPreloadLoaded()
{
	// ★ ПРОГРЕВ PSO. В упакованной игре компонент, чьи PSO ещё компилируются, по умолчанию НЕ рисуется, пока они
	//   не готовы (r.PSOPrecache.ProxyCreationStrategy) — превью появилось бы с опозданием или по частям. Прекэш
	//   запускает РЕГИСТРАЦИЯ компонента, поэтому, как только ассеты в памяти, регистрируем скрытые компоненты с
	//   теми же мешами и материалами: обычными, фейда появления (он надевается на всё превью, включая «душу») и
	//   оверлеем ауры «души». Компиляция идёт фоном, пока шкатулка ещё закрыта. В сборке редактора прекэш PSO
	//   выключен движком (IsPSOPrecachingEnabled = false) — шаг пропускается.
	if (!PipelineStateCache::IsPSOPrecachingEnabled()) { return; }
	UWorld* World = GetWorld();
	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	if (!World || !LM) { return; }
	if (CharSelPsoWarmActor) { CharSelPsoWarmActor->Destroy(); CharSelPsoWarmActor = nullptr; }

	TArray<UObject*> Assets;
	LM->GetCharSelectPreloadedAssets(Assets);
	UMaterialInterface* Fade = FindObject<UMaterialInterface>(nullptr, kPreviewFadeMaterial);   // в наборе → уже в памяти
	UMaterialInterface* Aura = FindObject<UMaterialInterface>(nullptr, kSoulAuraMaster);
	TSet<FString> SoulMeshes;
	for (const bool bA : { true, false })
	{
		for (const bool bB : { true, false })
		{
			const FString S = FAsdaPossessData::SoulMeshAssetPath(bA, bB);
			SoulMeshes.Add(S + TEXT(".") + FPaths::GetBaseFilename(S));
		}
	}

	FActorSpawnParameters SP;
	SP.ObjectFlags |= RF_Transient;
	AActor* Warm = World->SpawnActor<AActor>(AActor::StaticClass(),
		CasketActor ? CasketActor->GetActorLocation() : FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (!Warm) { return; }
	USceneComponent* Root = NewObject<USceneComponent>(Warm, TEXT("PsoWarmRoot"));
	Root->RegisterComponent();
	Warm->SetRootComponent(Root);
	Warm->SetActorHiddenInGame(true);

	int32 nComps = 0;
	// Зарегистрировать скрытый компонент (OverrideAll — материал на все слоты, Overlay — оверлей-проход).
	auto AddComp = [&](UMeshComponent* C, int32 NumSlots, UMaterialInterface* OverrideAll, UMaterialInterface* Overlay)
	{
		C->SetupAttachment(Root);
		C->SetHiddenInGame(true);
		// Без коллизии: иначе при регистрации скелетным с физ-ассетом создаются тела в точке шкатулки.
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(true);   // превью отбрасывает тень — PSO теневого прохода тоже нужны
		if (OverrideAll) { for (int32 s = 0; s < NumSlots; ++s) { C->SetMaterial(s, OverrideAll); } }
		if (Overlay) { C->SetOverlayMaterial(Overlay); }
		C->RegisterComponent();   // ← здесь движок ставит запросы прекэша PSO
		C->SetComponentTickEnabled(false);
		++nComps;
	};
	for (UObject* O : Assets)
	{
		if (USkeletalMesh* Sk = Cast<USkeletalMesh>(O))
		{
			const bool bSoul = SoulMeshes.Contains(Sk->GetPathName());
			const int32 NumSlots = Sk->GetMaterials().Num();
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				if (Pass == 1 && !Fade) { break; }
				USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(Warm);
				C->SetSkeletalMesh(Sk);
				C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
				AddComp(C, NumSlots, (Pass == 1) ? Fade : nullptr, (Pass == 0 && bSoul) ? Aura : nullptr);
			}
		}
		else if (UStaticMesh* Sm = Cast<UStaticMesh>(O))
		{
			const int32 NumSlots = Sm->GetStaticMaterials().Num();
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				if (Pass == 1 && !Fade) { break; }
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Warm);
				C->SetStaticMesh(Sm);
				AddComp(C, NumSlots, (Pass == 1) ? Fade : nullptr, nullptr);
			}
		}
	}
	CharSelPsoWarmActor = Warm;
	UE_LOG(LogAsdaLogin, Log, TEXT("[charsel_preload] прогрев PSO: скрытых компонентов %d, запросов прекэша в работе %u"),
		nComps, PipelineStateCache::NumActivePrecacheRequests());
}

void AAsdaLoginGameMode::ApplyPreviewStableQuality(AActor* Preview)
{
	// Правило качества персонажа (char-stable-quality) — и на превью: LOD0 + постоянные полные мипы. Работает
	//   только вместе с предзагрузкой: `asda.Login.CharSelPreload 0` возвращает экран выбора в прежний вид целиком.
	if (!Preview || !UAsdaLoadManagerSubsystem::IsCharSelectPreloadEnabled() || !AAsdaPlayerCharacter::IsStableQualityEnabled())
	{
		return;
	}
	AAsdaPlayerCharacter::ApplyStableLodToActor(Preview, true);
	// Половина текстур — ключ `asda.Char.StableQuality.Textures` ≥ 1; флаг через единый реестр владения:
	//   доля превью живёт до выхода с экрана выбора (пересборки превью на экране её только дополняют).
	if (AAsdaPlayerCharacter::GetStableQualityTexturesMode() >= 1)
	{
		TSet<UStreamableRenderAsset*> Tex;
		AAsdaPlayerCharacter::CollectActorTextures(Preview, Tex);
		for (UStreamableRenderAsset* A : Tex) { UAsdaTextureSubsystem::AcquireFullMips(A, CharSelPreviewMips); }
	}
}

void AAsdaLoginGameMode::SetupPossessScene()
{
	// Пересборка идёт вместе с пересборкой слотов (SpawnCharacterInSlot), поэтому сначала — снос.
	DestroyPossessScene();

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	AssignPartnersToSlots();

	for (int32 i = 0; i < 3; ++i)
	{
		const uint8 MySex = Net->CharList.IsValidIndex(i) ? Net->CharList[i].Sex : 1;
		if (SlotPartnerCharidx[i] != 0)
		{
			const FAsdaSoulmateSharedChar* Rec = FindPartnerRecord(SlotPartnerCharidx[i]);
			if (Rec)
			{
				AActor* A = SpawnPartnerInSlot(i, *Rec, MySex);
				if (A) { PartnerActors[i] = A; continue; }
			}
			SlotPartnerCharidx[i] = 0;   // не заспавнился/записи нет — режимы слота про него знать не должны
		}
		// ★★★ ПАРТНЁРА В СЛОТЕ НЕТ → эталон включает третий объект слота `+946128` («душу»)
		//   (`charDealWndSetSlotEnable`: `playerFindSubRecordByType` == null ⇒ `RenderCtx_SetEnable`).
		//   ⚠ У ЖИВОГО ОРИГИНАЛА ЭТОТ ОБЪЕКТ ПУСТОЙ: загрузчик его моделей в образе никем не вызывается
		//   (полное доказательство по бинарю — в комментарии к `SpawnSoulInSlot`). ★ 03.09.2026 —
		//   решение владельца «режим „душа“ оживляем»: показываем призрака по замыслу загрузчика
		//   (вид по эталону, ключ `asda.Login.SoulSilhouette` по умолчанию 1; 0 = пусто, как живьём).
		//   ★ ГЕЙТ ПОКАЗА 1:1 С ЭТАЛОНОМ: призрак стоит ТОЛЬКО когда справа НЕТ фигуры соулмейта —
		//   `continue` после успешного `SpawnPartnerInSlot` выше; и в пике (`TickPossessScene`/
		//   `OnScenePickClicked`) «второй объект» = партнёр ЛИБО призрак (`login_charselect.c:711-767`).
		//   Условие ветки — 1:1 с эталоном: партнёра нет И мой персонаж в слоте ЕСТЬ (пустому слоту
		//   с глифом создания «душа» не положена — эталон входит в ветку только при
		//   `findSlotByType != null`).
		//   ★ Регресс-прогон БЕЗ сервера (ASDA_TEST_FLOW=1): слот 0 занят синтетическим персонажем —
		//     то же правило, что в `SpawnCharacterInSlot`; рядом с ним ставится СИНТЕТИЧЕСКИЙ партнёр
		//     (ниже), чтобы самопроверка хит-зон `asda.Login.PossessPickTest` проверяла модель ДРУГА
		//     без сервера. Это ТОЛЬКО тестовые леса (env-переменная), игровых данных не подменяет.
		const bool bTestFlowSlot0 = (i == 0) && (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_FLOW")) == TEXT("1"));
		const bool bMineHere = (Net->CharList.IsValidIndex(i) && !Net->CharList[i].IsEmpty()) || bTestFlowSlot0;
		if (!bMineHere) { continue; }
		if (bTestFlowSlot0 && !PartnerActors[i])
		{
			// Синтетическая запись партнёра: пол противоположный, без экипировки (внешность 0).
			//   `SlotPartnerCharidx` НЕ трогаем: записи в сети нет, режим слота/вход остаются как без
			//   партнёра; актор нужен только для наведения и самопроверки коробки пика.
			FAsdaSoulmateSharedChar Fake;
			Fake.Charidx = 10; Fake.CharName = TEXT("TEST_PARTNER"); Fake.Gender = FAsdaPossessData::IsMale(MySex) ? 2 : 1;
			Fake.Level = 1;
			if (AActor* P = SpawnPartnerInSlot(i, Fake, MySex))
			{
				PartnerActors[i] = P;
				UE_LOG(LogAsdaLogin, Warning, TEXT("[ТЕСТ] слот %d: синтетический партнёр (пол=%u) для самопроверки хит-зон"), i, (uint32)Fake.Gender);
				continue;
			}
		}
		if (AActor* S = SpawnSoulInSlot(i, MySex)) { SoulActors[i] = S; }
	}

	// ★ Ловец кликов по 3D-сцене (см. AsdaPossessScene.h — почему не GameAndUI). Создаётся один раз
	//   и живёт под всеми окнами char-select; кнопки панели лежат выше и клики забирают первыми.
	if (!ScenePick)
	{
		if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			ScenePick = CreateWidget<UAsdaScenePickCatcher>(PC, UAsdaScenePickCatcher::StaticClass());
			if (ScenePick)
			{
				ScenePick->OnSceneClicked.BindUObject(this, &AAsdaLoginGameMode::OnScenePickClicked);
				// ⚠ Именно `Visible`, а не `SelfHitTestInvisible`: последний мышь НЕ ловит
				//   (закон `selfhittest-vs-hittest-invisible`).
				ScenePick->SetVisibility(ESlateVisibility::Visible);
				ScenePick->AddToViewport(/*ZOrder=*/1);
				if (UAsdaWindowSubsystem* WinMgr = GI->GetSubsystem<UAsdaWindowSubsystem>())
				{
					WinMgr->RegisterWindow(FName(TEXT("CHARSELECT_PICK")), ScenePick.Get(), 1);
				}
			}
		}
	}
}

void AAsdaLoginGameMode::ApplyClipStates(int32 SlotIdx)
{
	if (SlotIdx < 0 || SlotIdx > 2) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	// ⚠ У собранного персонажа НЕСКОЛЬКО скелетных мешей: базовое тело + части экипировки
	//   (`UAsdaCharAssembly::BuildWornParts` вешает их рядом с LeaderPose=база). `FindComponentByClass`
	//   вернул бы ПЕРВЫЙ попавшийся — то есть, скорее всего, штаны. Клип ставится только на БАЗУ:
	//   она названа `CharSkel` и здесь, и в `SpawnOneCharSlot`; части следуют за ней через LeaderPose.
	auto SkelOf = [](AActor* A) -> USkeletalMeshComponent*
	{
		if (!A) { return nullptr; }
		TArray<USkeletalMeshComponent*> Skels;
		A->GetComponents<USkeletalMeshComponent>(Skels);
		for (USkeletalMeshComponent* S : Skels)
		{
			if (S && S->GetFName() == FName(TEXT("CharSkel"))) { return S; }
		}
		return nullptr;
	};
	float LongestClip = 0.f;

	// ── МОЙ персонаж слота: клипы `cre_cremmy` (М) / `cre_crewmy` (Ж) = ассеты previewanim_00..04 ──
	//   ★ До 02.09.2026 женское превью анимировалось МУЖСКИМ набором (экспортёр был «унисекс»),
	//     хотя эталон `CEntity_LoadCreatureMorphModels_Cremmy@0xF05150` грузит женщине `cre_crewmy`.
	//     Правильный набор положен в те же ассеты `ch1_f_cremmy_previewanim_00..04`
	//     (`Tools/export_char_possess.py` + `import_char_possess.py`), поэтому код тут не менялся —
	//     менялось СОДЕРЖИМОЕ клипов. Это важно не для красоты: зазор пары «стоят рядом» живёт в
	//     корне клипа, и с мужским набором он у игрока-женщины уезжал в 1.4 раза.
	AActor* Mine = SlotContent.IsValidIndex(SlotIdx) ? SlotContent[SlotIdx].Get() : nullptr;
	if (Mine && CharacterActors.Contains(Mine))
	{
		const uint8 MySex = Net->CharList.IsValidIndex(SlotIdx) ? Net->CharList[SlotIdx].Sex : 1;
		const TCHAR* AnimBase = FAsdaPossessData::IsMale(MySex) ? GCharPreviewAnimBase : GCharPreviewAnimBaseF;
		const int32 Idx = FMath::Clamp(MyClipState[SlotIdx], 0, 4);
		if (UAnimSequence* Clip = LoadClip(FString::Printf(TEXT("%sanim_%02d"), AnimBase, Idx)))
		{
			if (USkeletalMeshComponent* S = SkelOf(Mine))
			{
				S->PlayAnimation(Clip, /*bLooping=*/(Idx == 0 || Idx == 2));
			}
			LongestClip = FMath::Max(LongestClip, Clip->GetPlayLength());
		}
	}

	// ── Персонаж СОУЛМЕЙТА: парный набор по паре полов ──
	AActor* Partner = PartnerActors.IsValidIndex(SlotIdx) ? PartnerActors[SlotIdx].Get() : nullptr;
	if (Partner)
	{
		if (const FAsdaSoulmateSharedChar* Rec = FindPartnerRecord(SlotPartnerCharidx[SlotIdx]))
		{
			const uint8 MySex = Net->CharList.IsValidIndex(SlotIdx) ? Net->CharList[SlotIdx].Sex : 1;
			const int32 Idx = FMath::Clamp(PartnerClipState[SlotIdx], 0, 4);
			const FString Path = FAsdaPossessData::PairClipAssetPath(
				FAsdaPossessData::IsMale(MySex), FAsdaPossessData::IsMale(Rec->Gender), Idx);
			if (UAnimSequence* Clip = LoadClip(Path))
			{
				if (USkeletalMeshComponent* S = SkelOf(Partner))
				{
					S->PlayAnimation(Clip, /*bLooping=*/(Idx == 0 || Idx == 2));
				}
				LongestClip = FMath::Max(LongestClip, Clip->GetPlayLength());
			}
		}
	}

	// ── «ДУША» слота: набор пары «мой пол × пол призрака». Пол призрака по умолчанию МОЙ
	//   (эталон `CEntity_LoadCreatureMorphModels_Cremmman@0xF05930`; решение владельца 03.09.2026
	//   «для ghost-режима — свой пол»); ключ `asda.Login.SoulSilhouetteOppositeSex 1` даёт
	//   противоположный (прежнее решение от 02.09 для серой заглушки, см. `SpawnSoulInSlot`).
	AActor* Soul = SoulActors.IsValidIndex(SlotIdx) ? SoulActors[SlotIdx].Get() : nullptr;
	if (Soul)
	{
		const uint8 MySex = Net->CharList.IsValidIndex(SlotIdx) ? Net->CharList[SlotIdx].Sex : 1;
		const bool bOpposite = (CVarAsdaSoulSilhouetteOppositeSex.GetValueOnGameThread() != 0);
		const int32 Idx = FMath::Clamp(SoulClipState[SlotIdx], 0, 4);
		if (UAnimSequence* Clip = LoadClip(FAsdaPossessData::SoulClipAssetPath(FAsdaPossessData::IsMale(MySex), bOpposite, Idx)))
		{
			if (USkeletalMeshComponent* S = SkelOf(Soul))
			{
				S->PlayAnimation(Clip, /*bLooping=*/(Idx == 0 || Idx == 2));
			}
			LongestClip = FMath::Max(LongestClip, Clip->GetPlayLength());
		}
	}

	// One-shot переход (клипы 1 и 3) сам себя завершает.
	// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА — ПОМЕЧЕНО (закон `fix-original-defects`, находка R6 ревизии).
	//   ЧТО ДЕЛАЕТ ОРИГИНАЛ. `worldModelsUpdateAndRender@0xF07890` (`RE/etalon/ui_core.c:93849-93862`)
	//   доводит one-shot до конца ТОЛЬКО у МОЕГО персонажа: по концу клипа он зовёт `sub_935FCA`
	//   (сеттер массива `+768742`) и переводит 1→2 / 3→0. У партнёра (`+768748`) и у «души»
	//   (`+768745`) автоперехода НЕТ вовсе — проверено двумя методами: (а) в декомпиле `+768748`
	//   встречается только в конструкторе, в сеттере `charContainer_SetSlotArrayC@0xF0A620` и в
	//   `charSelect_OnClickPick`; (б) сканирование `.text` на непосредственный операнд `3074992`
	//   (= 768748·4) даёт пять вхождений, и ни одного внутри `worldModelsUpdateAndRender`.
	//   ЧТО ДЕЛАЕМ МЫ. Доводим до конца ВСЕ ТРИ объекта. ПОЧЕМУ: у оригинала клип партнёра/«души»
	//   после перехода остаётся на последнем кадре анимации ухода/прихода и второй раз уже не
	//   играет корректно; при следующем клике состояние 1/3 не сбрасывается и гейт
	//   `if (state && state != 3)` начинает пропускать/глотать переключения. Это дефект оригинала,
	//   а не проводная кривизна: на сервер это не влияет ни байтом.
	//   КЛЮЧ ОТКАТА: нет — поведение не конфигурируется; чтобы вернуть эталон, надо убрать перевод
	//   `PartnerClipState`/`SoulClipState` в `TickPossessScene` (там же помечено).
	// ⚠ Длительность берём МАКСИМАЛЬНУЮ из фактически проигранных клипов (`GetPlayLength`), а не
	//   константу: наборы разной длины — переходные клипы 1 и 3 везде 46 ключей / 1.5 с, а idle 0 и 2
	//   у мужских наборов (`cremmy`, `cremmman`, `cremwwoman`) 61 ключ / 2.0 с, у женских
	//   (`crewmy`, `crewmman`, `crewwwoman`) — 46 / 1.5 с. (Прежний комментарий «61 ключ в обоих
	//   наборах» был неверен; сам код всегда считал по `GetPlayLength` и работал правильно.)
	const bool bOneShot = (MyClipState[SlotIdx] == 1 || MyClipState[SlotIdx] == 3
	                    || PartnerClipState[SlotIdx] == 1 || PartnerClipState[SlotIdx] == 3
	                    || SoulClipState[SlotIdx] == 1 || SoulClipState[SlotIdx] == 3);
	ClipOneShotLeft[SlotIdx] = bOneShot ? FMath::Max(LongestClip, 0.05f) : 0.f;
}

// Подсказка помощника при наведении/клике по МОЕМУ персонажу в режиме ≠ 0 — порт эталонного
//   `chatInput_InsertTargetName@0xF0C1B0` (`RE/etalon/chat.c:6649-6701`). ★ ИМЯ ФУНКЦИИ ВРЁТ:
//   к чату она отношения не имеет (закон `false-negatives-method` — миснеймы декомпила).
//   Тело: `if (rec[1082] >= 9 && rec[1080])` → запись 21 с ИМЕНЕМ персонажа, вставленным сразу за
//   первой '['; иначе → запись 19. По раскладке записи слота `rec[1082]` = уровень, `rec[1080]` =
//   класс. Смысл: персонажу 9-го уровня и выше, у которого класс уже выбран, помощник предлагает
//   войти им в игру; «безклассовому» новичку — запись 19 про смену профессии.
void AAsdaLoginGameMode::ApplyHelperForMyChar(int32 SlotIdx)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->CharList.IsValidIndex(SlotIdx)) { return; }
	const FAsdaCharRecord& R = Net->CharList[SlotIdx];
	if (R.Level >= 9 && R.Job != 0) { ApplyHelper(21, R.Name); }
	else                            { ApplyHelper(19); }
}

bool AAsdaLoginGameMode::ComputePickBox(const AActor* A, FBox& OutBox) const
{
	// Коробка пика по ТЕКУЩЕЙ позе. Правила (обоснование — в комментарии к `RayHitsActor`):
	//   • скелетал: мировые позиции костей из `ActiveBoneIndices` LOD0 (кости с весами вершин +
	//     родители); кости-пустышки `*_End` в них не входят, поэтому хулл = деформируемое тело;
	//   • части экипировки (LeaderPose = база) пропускаем: кости у них те же, а для кости вне
	//     `LeaderBoneMap` `GetBoneTransform` вернул бы Identity = НАЧАЛО МИРА и раздул бы коробку;
	//   • статик-меш (оружие на кости) — `Bounds`, позы у него нет.
	// ⚠ ОТСТУПЛЕНИЕ (наше инженерное, с прошлой волны): хулл по костям уже видимого тела на
	//   толщину плоти, поэтому по горизонтали расширяем на `kPickBoxPadFrac` от ВЫСОТЫ коробки
	//   (полуширина плеч ≈ 0.1 роста). Размера habit box в данных оригинала нет; ключ отката — 0.f.
	OutBox = FBox(ForceInit);
	if (!A) { return false; }
	constexpr float kPickBoxPadFrac = 0.10f;
	TArray<UMeshComponent*> Comps;
	const_cast<AActor*>(A)->GetComponents<UMeshComponent>(Comps);
	for (UMeshComponent* C : Comps)
	{
		if (!C || !C->IsRegistered() || C->bHiddenInGame) { continue; }
		if (const USkeletalMeshComponent* SkC = Cast<USkeletalMeshComponent>(C))
		{
			if (SkC->LeaderPoseComponent.IsValid()) { continue; }   // часть экипировки: поза = база
			const int32 NumBones = SkC->GetNumBones();
			const FSkeletalMeshRenderData* RD = SkC->GetSkeletalMeshRenderData();
			const TArray<FBoneIndexType>* Active =
				(RD && RD->LODRenderData.Num() > 0 && RD->LODRenderData[0].ActiveBoneIndices.Num() > 0)
				? &RD->LODRenderData[0].ActiveBoneIndices : nullptr;
			if (Active)
			{
				for (FBoneIndexType B : *Active)
				{
					if ((int32)B < NumBones) { OutBox += SkC->GetBoneTransform((int32)B).GetLocation(); }
				}
			}
			else if (NumBones > 0)
			{
				// Рендер-данных нет (NullRHI-прогон) — по всем костям, как в прошлой волне.
				for (int32 B = 0; B < NumBones; ++B) { OutBox += SkC->GetBoneTransform(B).GetLocation(); }
			}
			else
			{
				OutBox += C->Bounds.GetBox();
			}
			continue;
		}
		OutBox += C->Bounds.GetBox();   // статик-меш и прочее: своей позы у него нет
	}
	if (!OutBox.IsValid) { return false; }
	if (kPickBoxPadFrac > 0.f)
	{
		const float Pad = (float)OutBox.GetSize().Z * kPickBoxPadFrac;
		OutBox = OutBox.ExpandBy(FVector(Pad, Pad, 0.0));
	}
	return true;
}

bool AAsdaLoginGameMode::RayHitsActor(const FVector& Origin, const FVector& Dir, const AActor* A) const
{
	// Порт эталонного `entityRayVsObbHit`: там тоже проверяется КОРОБКА объекта, а не его геометрия.
	//
	// ★★★ ИСПРАВЛЕНО 02.09.2026 ПО ЖАЛОБЕ ВЛАДЕЛЬЦА: «на мужском силуэте наведение ничего не даёт,
	//   на женском тултип вылезает». В самом тракте наведения (`TickPossessScene`) НЕТ НИ ОДНОЙ
	//   зависящей от пола строки — перечитано целиком. Значит, по полу различалась только КОРОБКА.
	//   ЧТО БЫЛО: коробка бралась как `UMeshComponent::Bounds`, а у скелетала это границы,
	//   посчитанные по РЕФЕРЕНСНОЙ ПОЗЕ (T-поза, руки в стороны). Замер импортных границ
	//   (`Tools/_probe_soul_hitbox.py`, прогон 02.09):
	//     ch1_m_cremmy_preview   extent (4061.7, 2012.2, 7796.3)
	//     ch1_f_cremmy_preview   extent (4061.7, 2235.5, 7813.2)
	//     cre_cresoulman_preview extent (6967.7, 1378.1, 7703.2)
	//     cre_cresoulwoman…      extent (6758.1, 1322.5, 7650.8)
	//   То есть коробка ШИРЕ ЧЕЛОВЕКА В РАЗЫ (полуширина 4000-7000 при полувысоте 7700-7800) и у
	//   каждого пола СВОЯ. Персонаж и силуэт стоят в 1200-5000 единиц друг от друга — их T-позные
	//   коробки перекрываются, а `bOnMine` в эталонном порядке разбора имеет БЕЗУСЛОВНЫЙ приоритет
	//   (`scene_handleLeftClickPick`: ветка «попал по своему» обрабатывается целиком, второй объект
	//   разбирается только в `else`). Итог: у одних пар коробка моего персонажа накрывала силуэт и
	//   съедала наведение, у других нет — ровно та «асимметрия по полу», которую видел владелец.
	//   ЧТО ДЕЛАЕМ: коробку скелетала строим по ТЕКУЩЕЙ ПОЗЕ — по мировым позициям костей, а не по
	//   T-позным границам ассета. Это и ближе к эталону: там `entityRayVsObbHit` бьёт по OBB самого
	//   объекта (`CObject+8`), который оригинал пересчитывает по позе, а не по ref-позе.
	//   ⚠ ОТСТУПЛЕНИЕ (наше инженерное): хулл по костям уже видимого силуэта на толщину тела,
	//   поэтому расширяем его на `kPickBoxPadFrac` от собственной ВЫСОТЫ коробки по горизонтали
	//   (полуширина плеч человека ≈ 0.1 роста). Значение наше — в данных оригинала размера habit box
	//   нет; ключ отката — поставить 0.f, тогда попадать надо будет ровно по костям.
	//
	// ★★★ ИСПРАВЛЕНО 02.09.2026 ВТОРОЙ РАЗ (жалоба владельца: «клик и наведение срабатывают не на
	//   силуэте, а сильно правее»). Хулл «по всем костям» был неверен: экспортёр GLB кладёт в скелет
	//   КОСТИ-ПУСТЫШКИ `*_End` (концевые эффекторы, родитель `Armature_ProxyTrueRootJoint`), которые
	//   не имеют ни одного вершинного веса, клипами не анимируются и стоят ДАЛЕКО от тела. Замер
	//   ref-позы (`Tools/_probe_soul_bones.py`, лог `Reports/headless__probe_soul_bones.log`):
	//     cre_cresoulmwwoman_preview: bounds Y −1712..933, а кость `_8_a_End` в Y = −11145,
	//       `8_b_End` в Y = −9612, `8_a_End` в Z = 18193 (голова на 15301);
	//     ch1_m_cremmy_preview: плюс `2_a_End` в X = −7199 (bounds X ±4062).
	//   Итог: хулл был шире тела на ~10 000 ед. в −Y (≈ 2/3 роста) и его центр уезжал на 0.25 роста —
	//   ровно «хит-зона правее силуэта». Теперь в хулл идут ТОЛЬКО кости, на которые ссылаются веса
	//   вершин LOD0, плюс их родители по цепочке (`ActiveBoneIndices` — по `MeshUtilities.cpp:814-819`
	//   это BoneMap секций + `EnsureParentsExistAndSort`); концевые `*_End` ни в одной цепочке не
	//   родители, поэтому выпадают. Сборка коробки вынесена в `ComputePickBox`,
	//   чтобы самопроверка `asda.Login.PossessPickTest` печатала ту же коробку, что бьёт луч.
	FBox Box(ForceInit);
	if (!ComputePickBox(A, Box)) { return false; }

	// Slab-тест луча против AABB. Направление нормализовано вызывающим; нулевые компоненты
	//   обрабатываются через бесконечности (луч параллелен плоскости плиты).
	double TMin = 0.0, TMax = TNumericLimits<double>::Max();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double D = Dir[Axis];
		const double Lo = Box.Min[Axis], Hi = Box.Max[Axis], O = Origin[Axis];
		if (FMath::IsNearlyZero(D, UE_DOUBLE_SMALL_NUMBER))
		{
			if (O < Lo || O > Hi) { return false; }   // параллельно плите и вне её
			continue;
		}
		double T1 = (Lo - O) / D;
		double T2 = (Hi - O) / D;
		if (T1 > T2) { Swap(T1, T2); }
		TMin = FMath::Max(TMin, T1);
		TMax = FMath::Min(TMax, T2);
		if (TMin > TMax) { return false; }
	}
	return true;
}

// ★★★ Дыхание «ауры» силуэтов. Формулы 1:1 с эталоном (uiTweenUpdatePhase@0x1250ED0, разбор
//   `soulmate_soul_aura_render.md` §3/§7): период 2000 мс, толщина — треугольник 0.5↔1.0 ед.
//   модели, альфа — 4-фазный ping-pong с клампом [0,1] (эмуляция saturate oD0 vs.1.1). Толщина
//   переводится в см UE множителем SoulAuraHeightScale (высота_меша/150 — ДОПУЩЕНИЕ). Панорама UV
//   (скролл дымки по V) идёт от Time самого материала — здесь не трогаем. Гоним только пока
//   силуэты видны; период/амплитуда/вкл-выкл — за ключами asda.Login.SoulAura*.
// ★★★ САМОПРОВЕРКА ХИТ-ЗОН «ОДЕРЖИМОСТИ» ПУТЁМ ИГРОКА (закон `ui-check-game-path-geometry`: PIE-дамп
//   врёт, проверять `-game` и фактической экранной геометрией). Печатает для моего персонажа,
//   партнёра и «души» текущего слота:
//     • коробку пика (`ComputePickBox` — ровно та, что бьёт луч) в мире и её экранный прямоугольник;
//     • хулл СКИННУТЫХ вершин LOD0 (`ComputeSkinnedPositions`, то есть то, что игрок ВИДИТ) в мире и
//       на экране — эталон сравнения; доступен, пока у буферов есть CPU-копия (редакторный бинарь);
//     • две карты по сетке экрана: «ЛУЧ» — кого выберет тракт наведения в этой точке (та же цепочка
//       и приоритет, что в `TickPossessScene`), «ВИДНО» — чей хулл вершин накрывает точку; и число
//       клеток, где они расходятся. Совпали — хит-зона там, где видимое тело.
//   Прогон: `Tools/run_game_possess_pick.bat` (ASDA_TEST_FLOW=1, `-game -RenderOffscreen`, скрыто).
void AAsdaLoginGameMode::RunPossessPickSelfTest(bool bQuit)
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	FVector2D VP(0, 0);
	if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->GetViewportSize(VP); }
	UE_LOG(LogAsdaLogin, Warning, TEXT("[PossessPickTest] экран=%s screen=%d шкатулка=%d слот=%d режим=%d"),
		CurrentScreen == EAsdaScreen::CharSelect ? TEXT("CharSelect") : TEXT("другой"),
		(int32)CurrentScreen, bCasketOpened ? 1 : 0, SelectedSlot,
		(SelectedSlot >= 0 && SelectedSlot < 3) ? SlotMode[SelectedSlot] : -1);
	UE_LOG(LogAsdaLogin, Warning, TEXT("[PossessPickTest] вьюпорт %.0f×%.0f, PC=%d"), VP.X, VP.Y, PC ? 1 : 0);
	if (!PC || VP.X < 8 || VP.Y < 8 || SelectedSlot < 0 || SelectedSlot > 2)
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("[PossessPickTest] нет контроллера/вьюпорта/слота — проверка невозможна"));
		if (bQuit) { AsdaSelfTestBatch::Quit(GetWorld()); }   // ★ 11.10.2026: было FPlatformMisc::RequestExit — тот же выход, но виден пакету
		return;
	}

	AActor* Mine = SlotContent.IsValidIndex(SelectedSlot) ? SlotContent[SelectedSlot].Get() : nullptr;
	if (Mine && !CharacterActors.Contains(Mine)) { Mine = nullptr; }
	AActor* Partner = PartnerActors.IsValidIndex(SelectedSlot) ? PartnerActors[SelectedSlot].Get() : nullptr;
	AActor* Soul    = Partner ? nullptr : (SoulActors.IsValidIndex(SelectedSlot) ? SoulActors[SelectedSlot].Get() : nullptr);

	// Экранный прямоугольник мировой коробки: проекция 8 углов.
	auto ScreenRectOf = [&](const FBox& B, FBox2D& Out) -> bool
	{
		Out = FBox2D(ForceInit);
		FVector Corners[8];
		B.GetVertices(Corners);
		int32 Ok = 0;
		for (const FVector& Cn : Corners)
		{
			FVector2D S;
			if (PC->ProjectWorldLocationToScreen(Cn, S, false)) { Out += S; ++Ok; }
		}
		return Ok > 0;
	};
	// Хулл скиннутых вершин LOD0 в мире — «что видно». Только ведущие скелеталы (части = та же поза).
	auto VertexHullOf = [&](AActor* A, FBox& Out, int32& NumVerts) -> bool
	{
		Out = FBox(ForceInit); NumVerts = 0;
		if (!A) { return false; }
		TArray<USkeletalMeshComponent*> Skels;
		A->GetComponents<USkeletalMeshComponent>(Skels);
		for (USkeletalMeshComponent* S : Skels)
		{
			if (!S || !S->IsRegistered() || S->bHiddenInGame || !S->GetSkinnedAsset()) { continue; }
			FSkeletalMeshRenderData* RD = S->GetSkeletalMeshRenderData();
			if (!RD || RD->LODRenderData.Num() == 0) { continue; }
			const FSkeletalMeshLODRenderData& LOD = RD->LODRenderData[0];
			const FSkinWeightVertexBuffer* SW = S->GetSkinWeightBuffer(0);
			if (!SW || !SW->GetNeedsCPUAccess() || !LOD.StaticVertexBuffers.PositionVertexBuffer.GetAllowCPUAccess()
				|| LOD.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() == 0)
			{
				UE_LOG(LogAsdaLogin, Warning, TEXT("[PossessPickTest] %s/%s: у вершин нет CPU-копии — хулл вершин недоступен"),
					*A->GetName(), *S->GetName());
				continue;
			}
			TArray<FMatrix44f> RefToLocal;
			S->GetCurrentRefToLocalMatrices(RefToLocal, 0);
			TArray<FVector3f> Pos;
			USkinnedMeshComponent::ComputeSkinnedPositions(S, Pos, RefToLocal, LOD, *SW);
			const FTransform& L2W = S->GetComponentTransform();
			for (const FVector3f& P : Pos) { Out += L2W.TransformPosition(FVector(P)); }
			NumVerts += Pos.Num();
		}
		return Out.IsValid != 0;   // FBox::IsValid — uint8; явное сравнение против C4800 (warning-as-error)
	};
	struct FObj { const TCHAR* Name; AActor* A; FBox Pick; FBox2D PickR; FBox Vis; FBox2D VisR; bool bPick; bool bVis; };
	FObj Objs[3] = { { TEXT("МОЙ"), Mine, FBox(ForceInit), FBox2D(ForceInit), FBox(ForceInit), FBox2D(ForceInit), false, false },
	                 { TEXT("ПАРТНЁР"), Partner, FBox(ForceInit), FBox2D(ForceInit), FBox(ForceInit), FBox2D(ForceInit), false, false },
	                 { TEXT("ДУША"), Soul, FBox(ForceInit), FBox2D(ForceInit), FBox(ForceInit), FBox2D(ForceInit), false, false } };
	for (FObj& O : Objs)
	{
		if (!O.A) { UE_LOG(LogAsdaLogin, Warning, TEXT("[PossessPickTest] %s: объекта в слоте нет"), O.Name); continue; }
		O.bPick = ComputePickBox(O.A, O.Pick) && ScreenRectOf(O.Pick, O.PickR);
		int32 NV = 0;
		O.bVis = VertexHullOf(O.A, O.Vis, NV) && ScreenRectOf(O.Vis, O.VisR);
		UE_LOG(LogAsdaLogin, Warning, TEXT("[PossessPickTest] %s (%s) актор=%s"), O.Name, *O.A->GetName(), *O.A->GetActorLocation().ToString());
		if (O.bPick)
		{
			UE_LOG(LogAsdaLogin, Warning, TEXT("   коробка ПИКА:   мир min=%s max=%s | экран (%.0f,%.0f)-(%.0f,%.0f)"),
				*O.Pick.Min.ToString(), *O.Pick.Max.ToString(), O.PickR.Min.X, O.PickR.Min.Y, O.PickR.Max.X, O.PickR.Max.Y);
		}
		else { UE_LOG(LogAsdaLogin, Warning, TEXT("   коробка ПИКА: НЕТ")); }
		if (O.bVis)
		{
			UE_LOG(LogAsdaLogin, Warning, TEXT("   хулл ВЕРШИН (%d): мир min=%s max=%s | экран (%.0f,%.0f)-(%.0f,%.0f)"),
				NV, *O.Vis.Min.ToString(), *O.Vis.Max.ToString(), O.VisR.Min.X, O.VisR.Min.Y, O.VisR.Max.X, O.VisR.Max.Y);
			if (O.bPick)
			{
				const FVector2D dMin = O.PickR.Min - O.VisR.Min, dMax = O.PickR.Max - O.VisR.Max;
				UE_LOG(LogAsdaLogin, Warning, TEXT("   сдвиг экрана (пик − видно): левый край %+.0f, правый %+.0f, верх %+.0f, низ %+.0f px"),
					dMin.X, dMax.X, dMin.Y, dMax.Y);
			}
		}
		else { UE_LOG(LogAsdaLogin, Warning, TEXT("   хулл ВЕРШИН: недоступен")); }
	}

	// Карты по сетке экрана. Клетка ≈ 20 px; приоритет — как в тракте наведения: мой → партнёр → душа.
	const int32 Cols = FMath::Clamp((int32)(VP.X / 20.f), 16, 120);
	const int32 Rows = FMath::Clamp((int32)(VP.Y / 20.f), 8, 60);
	FString RayMap, VisMap;
	int32 Mismatch = 0, Cells = 0;
	for (int32 r = 0; r < Rows; ++r)
	{
		FString LineRay, LineVis;
		for (int32 c = 0; c < Cols; ++c)
		{
			const FVector2D S((c + 0.5f) * VP.X / Cols, (r + 0.5f) * VP.Y / Rows);
			TCHAR Ray = TEXT('.');
			FVector O, D;
			if (PC->DeprojectScreenPositionToWorld(S.X, S.Y, O, D))
			{
				D.Normalize();
				if (Mine && RayHitsActor(O, D, Mine))            { Ray = TEXT('M'); }
				else if (Partner && RayHitsActor(O, D, Partner)) { Ray = TEXT('P'); }
				else if (Soul && RayHitsActor(O, D, Soul))       { Ray = TEXT('S'); }
			}
			TCHAR Vis = TEXT('.');
			if (Objs[0].bVis && Objs[0].VisR.IsInside(S))      { Vis = TEXT('M'); }
			else if (Objs[1].bVis && Objs[1].VisR.IsInside(S)) { Vis = TEXT('P'); }
			else if (Objs[2].bVis && Objs[2].VisR.IsInside(S)) { Vis = TEXT('S'); }
			LineRay.AppendChar(Ray); LineVis.AppendChar(Vis);
			++Cells;
			if (Ray != Vis) { ++Mismatch; }
		}
		UE_LOG(LogAsdaLogin, Warning, TEXT("[PossessPickTest] ЛУЧ  |%s|   ВИДНО |%s|"), *LineRay, *LineVis);
	}
	UE_LOG(LogAsdaLogin, Warning, TEXT("[PossessPickTest] ИТОГ: клеток %d, расхождений луч/видно %d (%.1f%%). M=мой P=партнёр S=душа .=мимо"),
		Cells, Mismatch, Cells ? 100.f * Mismatch / Cells : 0.f);
	if (bQuit) { AsdaSelfTestBatch::Quit(GetWorld()); }   // ★ 11.10.2026: было FPlatformMisc::RequestExit — тот же выход, но виден пакету
}

// Консоль: `asda.Login.PossessPickTest [задержкаСек] [quit]` — снять геометрию хит-зон после задержки
//   (`-ExecCmds` исполняется до отрисовки, поэтому задержка обязательна — грабля из памяти проекта).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaPossessPickTestCmd(
	TEXT("asda.Login.PossessPickTest"),
	TEXT("Хит-зоны сцены «одержимости» на выборе персонажа: [задержкаСек] [quit] — коробка пика vs хулл вершин + карта луча (для -game)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* W)
		{
			AAsdaLoginGameMode* GM = W ? Cast<AAsdaLoginGameMode>(W->GetAuthGameMode()) : nullptr;
			if (!GM) { UE_LOG(LogAsdaLogin, Warning, TEXT("[PossessPickTest] AAsdaLoginGameMode не найден в мире")); return; }
			const float Delay = Args.Num() > 0 ? FMath::Max(0.f, FCString::Atof(*Args[0])) : 0.f;
			bool bQuit = false;
			for (const FString& A : Args) { if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; } }
			TWeakObjectPtr<AAsdaLoginGameMode> Weak(GM);
			if (Delay <= 0.f) { GM->RunPossessPickSelfTest(bQuit); return; }
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, [Weak, bQuit]()
			{
				if (Weak.IsValid()) { Weak->RunPossessPickSelfTest(bQuit); }
				else if (bQuit)    { AsdaSelfTestBatch::Quit(nullptr); }   // ★ 11.10.2026: было FPlatformMisc::RequestExit
			}, Delay, false);
			UE_LOG(LogAsdaLogin, Warning, TEXT("[PossessPickTest] запланирован через %.1f с (quit=%d)"), Delay, bQuit ? 1 : 0);
		}));
#endif // ASDA_DEV_CONSOLE

// [САМОТЕСТ] 10.10.2026 (en_completeness) — разбор у объявления.
void AAsdaLoginGameMode::RunCharSelectOfflineDemo()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	bool bAny = false;   // в списке уже есть персонаж (тогда данные не трогаем)
	for (const FAsdaCharRecord& C : Net->CharList) { if (!C.IsEmpty()) { bAny = true; break; } }
	if (!bAny)
	{
		// Условный персонаж для снимка (как на снимке владельца: лучник 17-го уровня, знак Лев).
		Net->CharList.SetNum(3);
		for (int32 i = 0; i < 3; ++i) { Net->CharList[i] = FAsdaCharRecord(); }
		FAsdaCharRecord& R = Net->CharList[0];
		R.Slot = 0; R.RawSlotId = 10; R.Name = TEXT("GM_sitxovski"); R.Job = 3; R.Level = 17; R.Exp = 0;
		R.HpMax = R.HpCur = 3270; R.MpMax = R.MpCur = 169; R.Stats = { 134, 206, 119, 146, 86, 206 };
		R.Constellation = 4;
	}
	SelectedSlot = 0;
	HideLoginScene();
	if (LoginWidget) { LoginWidget->SetVisibility(ESlateVisibility::Collapsed); }             // окно ввода логина — под снимком не нужно
	if (ServerSelectWidget) { ServerSelectWidget->SetVisibility(ESlateVisibility::Collapsed); }   // как в OnCharListReady
	ShowCharSelectUI();
	UE_LOG(LogAsdaLogin, Warning, TEXT("[CharSelectOffline] экран выбора персонажа показан офлайн (персонажей в списке: %d)"), Net->CharList.Num());
}

// Консоль: `asda.Login.CharSelectOffline [задержкаСек]` — экран выбора персонажа без сервера (только офлайн).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaCharSelectOfflineCmd(
	TEXT("asda.Login.CharSelectOffline"),
	TEXT("[САМОТЕСТ] Экран выбора персонажа без сервера (условный персонаж, только офлайн): [задержкаСек]."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* W)
		{
			if (!W || FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Login.CharSelectOffline"))) { return; }
			AAsdaLoginGameMode* GM = Cast<AAsdaLoginGameMode>(W->GetAuthGameMode());
			if (!GM) { UE_LOG(LogAsdaLogin, Warning, TEXT("[CharSelectOffline] AAsdaLoginGameMode не найден в мире")); return; }
			const float Delay = Args.Num() > 0 ? FMath::Max(0.f, FCString::Atof(*Args[0])) : 0.f;
			TWeakObjectPtr<AAsdaLoginGameMode> Weak(GM);
			if (Delay <= 0.f) { GM->RunCharSelectOfflineDemo(); return; }
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, [Weak]() { if (Weak.IsValid()) { Weak->RunCharSelectOfflineDemo(); } }, Delay, false);
		}));
#endif // ASDA_DEV_CONSOLE

// [САМОТЕСТ] 08.10.2026 — разбор у объявления. Пишет в лог текст пузыря помощника ANI_TXT после «Создать».
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
void AAsdaLoginGameMode::RunCreateNameSelfTest(const FString& Name)
{
	ShowCharCreateUI(0);
	CreateConstellation = 1;   // созвездие выбрано — проверка ниже доходит до правила символов имени
	if (UEditableText* E = CharCreateWidget ? Cast<UEditableText>(CharCreateWidget->GetWidgetFromName(TEXT("CARAMAKE_NAMEINPUT"))) : nullptr)
	{
		E->SetText(FText::FromString(Name));
	}
	SubmitCharCreate();
	FString Shown;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (UUserWidget* Ani = WM->FindWindow(FName(TEXT("WBP_ANI_WIN"))))
		{
			if (UWidget* T = Ani->GetWidgetFromName(TEXT("ANI_TXT")))
			{
				if (UTextBlock* TB = Cast<UTextBlock>(T)) { Shown = TB->GetText().ToString(); }
				else if (URichTextBlock* RB = Cast<URichTextBlock>(T)) { Shown = RB->GetText().ToString(); }
			}
		}
	}
	// Окно сообщения MESSAGE_WIN (эталон показывает текст ошибки именно в нём).
	FString MsgShown = TEXT("(окна нет)");
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->IsOpen(FName(TEXT("MESSAGE_WIN"))))
		{
			if (UUserWidget* Mw = WM->FindWindow(FName(TEXT("MESSAGE_WIN"))))
			{
				if (UWidget* T = Mw->GetWidgetFromName(TEXT("MESSAGE_TXT01")))
				{
					if (UTextBlock* TB = Cast<UTextBlock>(T)) { MsgShown = TB->GetText().ToString(); }
					else if (URichTextBlock* RB = Cast<URichTextBlock>(T)) { MsgShown = RB->GetText().ToString(); }
				}
			}
		}
	}
	UE_LOG(LogAsdaLogin, Warning, TEXT("[CreateNameTest] имя «%s» → окно сообщения: «%s» | пузырь помощника: «%s» | loc 9583 = «%s»"),
		*Name, *MsgShown.Replace(TEXT("\n"), TEXT(" ")), *Shown.Replace(TEXT("\n"), TEXT(" ")), *FAsdaText::Loc(9583));
	// ★ 11.10.2026 (В3 S1): единый итог для пакетного прогона. Проверка — та, что прежде читалась глазами из строки выше:
	//   недопустимое имя отсечено клиентом — показано окно сообщения (эталон выводит ошибку в MESSAGE_WIN), пакет 1014 не ушёл.
	const bool bMsg = MsgShown != TEXT("(окна нет)") && !MsgShown.IsEmpty();
	UE_LOG(LogAsdaLogin, Warning, TEXT("[CreateNameTest] %s окно сообщения об ошибке имени показано"), bMsg ? TEXT("ok") : TEXT("FAIL"));
	AsdaSelfTestKit::Summary(LogAsdaLogin, TEXT("[CreateNameTest]"), bMsg ? 1 : 0, bMsg ? 0 : 1, TEXT(""), true);
}
#endif // !UE_BUILD_SHIPPING

// Консоль: `asda.Login.CreateNameTest <имя> [задержкаСек]` — экран создания персонажа, «Создать» с этим именем (офлайн).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaCreateNameTestCmd(
	TEXT("asda.Login.CreateNameTest"),
	TEXT("asda.Login.CreateNameTest <имя> [задержкаСек] — открыть создание персонажа, вписать имя, нажать «Создать» (офлайн; лог [CreateNameTest])."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* W)
		{
			// ★ 11.10.2026 (В3 S1): `quit` в любой позиции — флаг выхода, а не имя (в пакете `asda.Login.CreateNameTest quit` прежде
			//   проверял «quit» как имя, оно допустимо — отказ без итога и без выхода, пакет стоял до таймаута). Без имени — недопустимое
			//   имя по умолчанию «a'b» (апостроф отсекает и проверка ниже, и клиентская `SubmitCharCreate`).
			const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
			AAsdaLoginGameMode* GM = W ? Cast<AAsdaLoginGameMode>(W->GetAuthGameMode()) : nullptr;
			if (!GM) { AsdaSelfTestKit::Refused(W, TEXT("asda.Login.CreateNameTest"), KA.bQuit, TEXT("нет AAsdaLoginGameMode")); return; }
			const FString Name = KA.Str(0, TEXT("a'b"));
			// ★ Только НЕДОПУСТИМЫЕ имена: их отсекает клиентская проверка `SubmitCharCreate`, пакет 1014 не уходит никогда —
			//   поэтому команда безопасна и при живом соединении экрана логина. Допустимое имя (создало бы персонажа) — отказ.
			if (FAsdaLoginData::IsValidCharNameByLocale(Name) && !Name.Contains(TEXT("'")) && Name.Len() <= 18)
			{
				UE_LOG(LogAsdaLogin, Warning, TEXT("[CreateNameTest] «%s» — допустимое имя: команда только для недопустимых (ничего не отправлено)"), *Name);
				AsdaSelfTestKit::Refused(W, TEXT("asda.Login.CreateNameTest"), KA.bQuit, TEXT("допустимое имя"));
				return;
			}
			const float Delay = FMath::Max(0.f, KA.Float(1, 0.f));
			const bool bQuit = KA.bQuit;
			TWeakObjectPtr<AAsdaLoginGameMode> Weak(GM);
			TWeakObjectPtr<UWorld> WW(W);
			auto Go = [Weak, WW, Name, bQuit]()
			{
				if (Weak.IsValid()) { Weak->RunCreateNameSelfTest(Name); }
				else { AsdaSelfTestKit::Refused(WW.Get(), TEXT("asda.Login.CreateNameTest"), false, TEXT("режим логина ушёл")); }
				AsdaSelfTestKit::Quit(WW.Get(), bQuit);
			};
			if (Delay <= 0.f) { Go(); return; }
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, Go, Delay, false);
		}));
#endif // ASDA_DEV_CONSOLE

void AAsdaLoginGameMode::TickSoulAura(float DeltaSeconds)
{
	if (CVarAsdaSoulAura.GetValueOnGameThread() == 0) { return; }
	if (CurrentScreen != EAsdaScreen::CharSelect || !bCasketOpened) { return; }
	if (!SoulAuraMIDs[0] && !SoulAuraMIDs[1] && !SoulAuraMIDs[2]) { return; }

	const float PeriodMs  = FMath::Max(1.f, CVarAsdaSoulAuraPeriodMs.GetValueOnGameThread());
	const float ThickMul  = CVarAsdaSoulAuraThickness.GetValueOnGameThread();
	SoulAuraElapsedMs = FMath::Fmod(SoulAuraElapsedMs + DeltaSeconds * 1000.f, PeriodMs);

	const float T    = SoulAuraElapsedMs;
	const float Half = PeriodMs * 0.5f;               // при 2000 → 1000
	const float Q    = Half * 0.5f;                   // «средний ключ» альфы, при 2000 → 500
	// Толщина: треугольник 0.5 → 1.0 (T=Half) → 0.5 (ед. модели).
	const float ThickModel = (T < Half) ? (0.5f + 0.5f * (T / Half))
	                                     : (1.0f - 0.5f * ((T - Half) / Half));
	// Альфа (до клампа): фаза 0 — T≤Q: 1.5−T/Q (1.5→0.5), иначе 0.5; фаза 2 — t≤Q: 0.5, иначе 0.5+(t−Q)/Q.
	float A;
	if (T < Half) { A = (T <= Q) ? (1.5f - T / Q) : 0.5f; }
	else          { const float t = T - Half; A = (t <= Q) ? 0.5f : (0.5f + (t - Q) / Q); }
	const float Alpha = FMath::Clamp(A, 0.f, 1.f);

	for (int32 i = 0; i < 3; ++i)
	{
		UMaterialInstanceDynamic* MID = SoulAuraMIDs[i];
		if (!MID) { continue; }
		if (!SoulActors.IsValidIndex(i) || !SoulActors[i]) { continue; }
		MID->SetScalarParameterValue(TEXT("Thickness"), ThickModel * SoulAuraHeightScale[i] * ThickMul);
		MID->SetScalarParameterValue(TEXT("AuraAlpha"), Alpha);
	}
}

void AAsdaLoginGameMode::TickPossessScene(float DeltaSeconds)
{
	if (CurrentScreen != EAsdaScreen::CharSelect || !bCasketOpened) { return; }

	// ── 1. Докрутка one-shot переходов (эталон: конец клипа 1 → 2, конец клипа 3 → 0) ──
	for (int32 i = 0; i < 3; ++i)
	{
		if (ClipOneShotLeft[i] <= 0.f) { continue; }
		ClipOneShotLeft[i] -= DeltaSeconds;
		if (ClipOneShotLeft[i] > 0.f) { continue; }
		ClipOneShotLeft[i] = 0.f;
		if (MyClipState[i] == 1)      { MyClipState[i] = 2; }
		else if (MyClipState[i] == 3) { MyClipState[i] = 0; }
		// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА — ПОМЕЧЕНО. В оригинале автопереход есть ТОЛЬКО у моего
		//   персонажа; у партнёра и «души» его нет (полное обоснование и оба метода проверки —
		//   в комментарии к `bOneShot` в `ApplyClipStates`). Чтобы вернуть эталон 1:1, убрать
		//   ровно эти четыре строки.
		if (PartnerClipState[i] == 1)      { PartnerClipState[i] = 2; }
		else if (PartnerClipState[i] == 3) { PartnerClipState[i] = 0; }
		if (SoulClipState[i] == 1)      { SoulClipState[i] = 2; }
		else if (SoulClipState[i] == 3) { SoulClipState[i] = 0; }
		ApplyClipStates(i);
	}

	// ── 2. Наведение мышью (эталон scene_handleLeftClickPick@0xF0A680) ──
	//   Предпросмотр панелей работает ТОЛЬКО в режиме 0: как только режим выбран кликом,
	//   наведение панели больше не переключает (эталон гейтит каждую ветку по `statusPanelMode`).
	//   Гейты эталона (`scene_handleLeftClickPick` проверяет `+3075037` и `+76`): пока идёт секвенция
	//   шкатулки или поворот карусели, пик не работает — иначе панель дёргается посреди анимации.
	if (CasketSeq != ECasketSeqPhase::None || bTurnPlaying) { return; }
	if (SelectedSlot < 0 || SelectedSlot > 2) { return; }

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	FVector Origin, Dir;
	if (!PC || !PC->DeprojectMousePositionToWorld(Origin, Dir)) { return; }
	Dir.Normalize();

	// Эталон входит в разбор ТОЛЬКО когда `findSlotByType` вернул мой объект: на пустом слоте
	//   (там глиф создания) наведение не обрабатывается вовсе.
	AActor* Mine = SlotContent.IsValidIndex(SelectedSlot) ? SlotContent[SelectedSlot].Get() : nullptr;
	if (Mine && !CharacterActors.Contains(Mine)) { Mine = nullptr; }   // в слоте глиф, а не персонаж
	if (!Mine) { return; }

	// ★★★ ВТОРОЙ ОБЪЕКТ СЛОТА — ЛИБО ПАРТНЁР, ЛИБО «ДУША», ровно как в эталоне
	//   (`scene_handleLeftClickPick@0xF0A680`, `RE/etalon/render_core.c:5975-6042`):
	//     `playerFindSubRecordByType` вернул запись → луч проверяется против ПАРТНЁРА;
	//     вернул null      → луч проверяется против объекта `+946128`, то есть «души».
	//   Обе ветки — взаимоисключающие плечи одного `if`, поэтому и здесь один «второй объект».
	AActor* Partner = PartnerActors.IsValidIndex(SelectedSlot) ? PartnerActors[SelectedSlot].Get() : nullptr;
	AActor* Soul    = Partner ? nullptr : (SoulActors.IsValidIndex(SelectedSlot) ? SoulActors[SelectedSlot].Get() : nullptr);
	// ★★★ ИСПРАВЛЕНО 02.09.2026 (находка Н1 ревизии). ЗДЕСЬ СТОЯЛ РАННИЙ ВЫХОД
	//   `if (!Partner && !Soul) { return; }` — ДО луча по МОЕМУ персонажу. Из-за него, как только
	//   в слоте не оказывалось второго объекта, наведение на СОБСТВЕННОГО персонажа переставало
	//   показывать панель 17 и подсказку помощника 21. Состояние достижимо всегда, когда силуэт
	//   не заспавнился (нет ассета) или отключён ключом `asda.Login.SoulSilhouette 0`.
	//   ЭТАЛОН так не делает: `scene_handleLeftClickPick@0xF0A680` (`RE/etalon/render_core.c:5919-6042`)
	//   сначала бьёт лучом по объекту слота (`entityRayVsObbHit(hoveredSlot + 8, …)`) и обрабатывает
	//   попадание ЦЕЛИКОМ, ни разу не спросив про партнёра/«душу»; поиск второго объекта живёт
	//   в `else`-плече промаха. Порядок восстановлен: сперва луч по своему, второй объект — потом.
	//   Ветка «мимо всего» ниже гасит панель 114 и возвращает 17 — ровно то же делает эталон
	//   в своей ветке промаха (`render_core.c:6034-6040`), так что отдельный выход не нужен.

	const bool bOnMine    = RayHitsActor(Origin, Dir, Mine);
	const bool bOnPartner = !bOnMine && Partner && RayHitsActor(Origin, Dir, Partner);
	const bool bOnSoul    = !bOnMine && Soul    && RayHitsActor(Origin, Dir, Soul);
	// ★ [ИСПРАВЛЕНИЕ] 08.11.2026: окно 114 как подсказка — у курсора, КАЖДЫЙ кадр (следует за ним), но только
	//   когда его показало наведение режима 0 на призрак/Друга; иначе — эталонное место (см. `AsdaPlaceSoulInfoPanel`).
	//   Ставим ПОСЛЕ переходов ниже, чтобы в кадр показа окно уже стояло у курсора.
	auto PlaceTip = [&]()
	{
		const bool bTip = SoulPanelMode > 0 && SlotMode[SelectedSlot] == 0 && (bOnPartner || bOnSoul);
		AsdaPlaceSoulInfoPanel(SoulInfoWidget, GetWorld(), bTip);
	};
	if (bOnMine == bPossessHoverMyChar && bOnPartner == bPossessHoverPartner
	    && bOnSoul == bPossessHoverSoul) { PlaceTip(); return; }   // переходов нет — только следование за курсором
	ON_SCOPE_EXIT { PlaceTip(); };
	bPossessHoverMyChar = bOnMine;
	bPossessHoverPartner = bOnPartner;
	bPossessHoverSoul = bOnSoul;

	const int32 Mode = SlotMode[SelectedSlot];

	if (bOnMine)
	{
		if (Mode == 0)
		{
			// Эталон (`render_core.c:5936-5940`): режим 0 → погасить 114, показать 17.
			ShowSoulInfoPanel(false, -1);
			ShowCharStatePanel(true);
			// И строка помощника 21 с ИМЕНЕМ персонажа, вставленным сразу за первой '['
			//   (`render_core.c:5947-5968`) — здесь БЕЗ проверки уровня/класса.
			UGameInstance* GI = GetGameInstance();
			UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			const FString MyName = (Net && Net->CharList.IsValidIndex(SelectedSlot)) ? Net->CharList[SelectedSlot].Name : FString();
			ApplyHelper(21, MyName);
		}
		else
		{
			if (Mode == 1)
			{
				ShowCharStatePanel(false);
				ShowSoulInfoPanel(true, 1);
			}
			// Режим 2 — панели эталон при наведении на своего персонажа не трогает.
			// ★ ПРОПУЩЕННАЯ ВЕТКА ПОДСКАЗКИ, ДОБАВЛЕНА 02.09.2026 (находка R4 ревизии). Эталон
			//   при `statusPanelMode != 0` зовёт `chatInput_InsertTargetName(gameUi, hoveredSlot)`
			//   (`render_core.c:5941-5944`) — то есть запись 21 с именем при уровне ≥ 9 и
			//   выбранном классе, иначе запись 19. Раньше порт в режиме 1 трогал только панели,
			//   а в режиме 2 не делал ничего, и запись 19 не использовалась нигде.
			ApplyHelperForMyChar(SelectedSlot);
		}
	}
	else if (bOnPartner)
	{
		if (Mode == 0)
		{
			ShowSoulInfoPanel(true, 2);
			ShowCharStatePanel(false);
		}
		// Подсказка помощника: 25 — если персонаж соулмейта уже ВЫБРАН, иначе 24 (эталон).
		ApplyHelper(Mode == 2 ? 25 : 24);
	}
	else if (bOnSoul)
	{
		// ★★★ РЕЖИМ 1. Эталон (`render_core.c:6013-6026`): при режиме 0 показать 114 в режиме 1
		//   («здесь будет стоять персонаж твоего Друга») и погасить 17; подсказка 23, если «душа»
		//   уже выбрана, иначе 22.
		if (Mode == 0)
		{
			ShowSoulInfoPanel(true, 1);
			ShowCharStatePanel(false);
		}
		// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА — ПОМЕЧЕНО (03.09.2026, по ревизии `review_charselect_enter_mode_modal`).
		//   Эталон (`render_core.c:6023`) даёт 22 «Хочешь играть призраком?», пока режим слота ≠ 1
		//   (и вдобавок читает режим БЕЗ индекса слота — `*((_DWORD *)gameUi + 768772)`, т.е. на слотах
		//   1 и 2 смотрит режим СЛОТА 0 — дефект оригинала, находка R11). У нас режим «душа» с 03.09
		//   выбирается ОКНОМ на «Играть» (`ShowEnterModeChoice`), а клик по фигуре его НЕ ставит —
		//   подпись про «призрака» на заглушке стала ложью. Показываем всегда 23 «Это место твоего
		//   Друга! Найди себе Друга в игре.» — ту же, что эталон даёт этой фигуре при клике
		//   (`login_charselect.c:747`). Подсказка чисто клиентская, на сервер не влияет.
		ApplyHelper(23);
	}
	else
	{
		if (Mode == 0)
		{
			// Эталон в ветке «луч не попал ни во что» (`render_core.c:6028-6040`) при режиме 0
			//   гасит 114 и показывает 17 — это здесь и делается.
			// ★ ПРИВЕДЕНО К ЭТАЛОНУ 02.09.2026 (находка R8 ревизии): отсюда убран вызов
			//   `ApplyHelper(CurrentHelperDefault)`. Ни в одной из трёх «промахов»-веток эталона
			//   `uiSelectListItem` не вызывается — текст помощника при уходе наведения остаётся
			//   прежним. Прежний комментарий ссылался на «эталон LOST_FOCUS», но LOST_FOCUS —
			//   это фокус КНОПОК UI, к 3D-пику отношения не имеющий.
			ShowSoulInfoPanel(false, -1);
			ShowCharStatePanel(true);
		}
	}
}

void AAsdaLoginGameMode::OnScenePickClicked()
{
	// Эталон `charSelect_OnClickPick@0xF0AF00` (`RE/etalon/login_charselect.c:641-798`).
	//   Гейты: char-select показан, шкатулка открыта, секвенция не идёт.
	//   Приоритет лучей: сначала МОЙ персонаж, затем ВТОРОЙ объект слота — персонаж соулмейта,
	//   а если его нет, «душа» (`&pCharSelect[59128*slot + 236534]` = `+946136`, то есть OBB
	//   объекта `+946128`).
	if (CurrentScreen != EAsdaScreen::CharSelect || !bCasketOpened) { return; }
	if (CasketSeq != ECasketSeqPhase::None || bTurnPlaying) { return; }
	if (SelectedSlot < 0 || SelectedSlot > 2) { return; }
	// ★ Пока открыта модалка (выбор режима входа / сообщение про место друга / отказ входа) клик по
	//   сцене не обрабатываем: ловец лежит под окнами, а модальность в оригинале — состояние менеджера
	//   окон (`pManager+104`), блокирующее ввод к нижним окнам (тот же гейт, что у `FocusUI`).
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->IsAnyModalOpen()) { return; }
	}

	// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА — ПОМЕЧЕНО (закон `fix-original-defects`, находка R5 ревизии).
	//   ЧТО ДЕЛАЕТ ОРИГИНАЛ. Перед любой обработкой клика он требует, чтобы ВСЕ ТРИ байта-гейта
	//   `+3075040`, `+3075041`, `+3075042` были ненулевыми (`login_charselect.c:692-699`), а при
	//   выборе гасит один из них: `+3075042` — выбран партнёр (`:722`), `+3075041` — выбрана
	//   «душа» (`:744`), `+3075040` — снова выбран мой персонаж (`:768`).
	//   ГДЕ ДЕФЕКТ. Конструктор ставит их ЦИКЛОМ ПО СЛОТАМ (`*(this + i + 3075040) = 1`, i = 0..2,
	//   `AsdaStory_game.c:528326`), то есть это МАССИВ по слотам. Но и чтение гейта, и его гашение,
	//   и восстановление по концу one-shot (`worldModelsUpdateAndRender`: `v14[3075040] = 1`,
	//   `RE/etalon/ui_core.c:93855` и `:93860`) идут БЕЗ индекса слота — везде элемент 0.
	//   Итог в оригинале: `+3075041`/`+3075042` после первого же выбора «души»/партнёра не
	//   восстанавливаются НИКОГДА, и клик по сцене умирает целиком до конца сеанса char-select.
	//   ЧТО ДЕЛАЕМ МЫ. Гейт не воспроизводим вовсе: переключаться между своим персонажем, партнёром
	//   и «душой» можно сколько угодно. Настоящее назначение гейта — не пустить клик, пока идёт
	//   переходный клип, — у нас закрыто теми же проверками, что и в эталоне
	//   (`if (state && state != 3)` ниже) плюс `CasketSeq`/`bTurnPlaying` выше.
	//   НА СЕРВЕР НЕ ВЛИЯЕТ: гейт целиком клиентский, ни одного байта в 1016 он не меняет.
	//   КЛЮЧ ОТКАТА: нет — воспроизводить дефект оригинала смысла не имеет.

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	FVector Origin, Dir;
	if (!PC || !PC->DeprojectMousePositionToWorld(Origin, Dir)) { return; }
	Dir.Normalize();

	// Эталон: `if (!pLeftCharObj) return;` — на пустом слоте клик не обрабатывается.
	AActor* Mine = SlotContent.IsValidIndex(SelectedSlot) ? SlotContent[SelectedSlot].Get() : nullptr;
	if (Mine && !CharacterActors.Contains(Mine)) { Mine = nullptr; }
	if (!Mine) { return; }

	AActor* Partner = PartnerActors.IsValidIndex(SelectedSlot) ? PartnerActors[SelectedSlot].Get() : nullptr;
	AActor* Soul    = Partner ? nullptr : (SoulActors.IsValidIndex(SelectedSlot) ? SoulActors[SelectedSlot].Get() : nullptr);

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	if (RayHitsActor(Origin, Dir, Mine))
	{
		// Эталон: клик по своему персонажу что-то делает ТОЛЬКО когда выбран не он (режим ≠ 0).
		if (SlotMode[SelectedSlot] == 0) { return; }
		// Гейт эталона `if (state && state != 3)`: не перезапускаем уже идущий приход и не трогаем
		//   объект, который и так «выбран» (state 0).
		if (MyClipState[SelectedSlot] != 0 && MyClipState[SelectedSlot] != 3)
		{
			MyClipState[SelectedSlot] = 3;        // приход: «выбирают меня»
			// Эталон (`login_charselect.c:763-766`): уходит тот объект, который БЫЛ выбран —
			//   партнёр, если запись есть, иначе «душа».
			if (Partner) { PartnerClipState[SelectedSlot] = 1; }
			else         { SoulClipState[SelectedSlot]    = 1; }
			SlotMode[SelectedSlot] = 0;
			ShowSoulInfoPanel(false, -1);
			ApplyClipStates(SelectedSlot);
		}
		// Здесь эталон вставляет запись 21 с именем НАПРЯМУЮ (`:771-794`), без гейта уровня/класса —
		//   в отличие от ветки НАВЕДЕНИЯ, где он идёт через `chatInput_InsertTargetName`.
		const FString MyName = (Net && Net->CharList.IsValidIndex(SelectedSlot)) ? Net->CharList[SelectedSlot].Name : FString();
		ApplyHelper(21, MyName);
		UpdateCharSelectInfo();
		return;
	}

	if (Partner && RayHitsActor(Origin, Dir, Partner))
	{
		if (SlotMode[SelectedSlot] == 2) { return; }
		if (PartnerClipState[SelectedSlot] != 0 && PartnerClipState[SelectedSlot] != 3)
		{
			MyClipState[SelectedSlot] = 1;        // уход моего персонажа
			PartnerClipState[SelectedSlot] = 3;   // приход партнёра
			SlotMode[SelectedSlot] = 2;
			ShowSoulInfoPanel(false, -1);   // эталон гасит панель 114 в момент смены режима
			ApplyClipStates(SelectedSlot);
		}
		ApplyHelper(25);
		UpdateCharSelectInfo();   // панель 17 наполнится статами ЧУЖОГО персонажа
		return;
	}

	if (Soul && RayHitsActor(Origin, Dir, Soul))
	{
		// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА — ПОМЕЧЕНО (решение владельца 03.09.2026, закон `fix-original-defects`).
		//   ЧТО ДЕЛАЕТ ОРИГИНАЛ (`login_charselect.c:734-750`): клик по объекту `+946128` — мой клип 1
		//   (уход), его клип 3 (приход), режим слота 1, подпись 23 «Это место твоего Друга! Найди себе
		//   Друга в игре.»; затем «Играть» в режиме 1 заглушено подписью 33.
		//   ЧТО ДЕЛАЕМ МЫ. Эта фигура для владельца — ЗАГЛУШКА МЕСТА ДРУГА («здесь встанет друг»), и клик
		//   по ней НЕ выбирает режим и НЕ ведёт к 1016 (жалоба: «пустой персонаж друга выбираешь — игра
		//   начинает загрузку. Бред!»). Показываем ОДНОКНОПОЧНУЮ модалку MESSAGE_WIN (та же, что отказы
		//   1027/1011 — `ShowMessageWin`) с loc 990136 «Тут будет персонаж твоего друга, если он разрешит
		//   тебе играть за него!» (формулировка владельца дословно). Режим 1 выбирается только модалкой на
		//   «Играть» (`ShowEnterModeChoice`). Клипы/режим/панели не трогаем — сцена остаётся как была.
		//   НА СЕРВЕР не влияет: ни одного пакета здесь нет. КЛЮЧ ОТКАТА: нет — эталонный «клик = режим 1»
		//   владелец отверг явно.
		FString Msg = FAsdaText::Loc(990136);
		if (Msg.IsEmpty())
		{
			Msg = TEXT("[#990136]");   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): нейтральный фолбэк вместо RU-текста
			UE_LOG(LogAsdaLogin, Warning, TEXT("DT_GameText: нет строки 990136 — нужен переимпорт (Tools/import_gametext.py)"));
		}
		ShowMessageWin(Msg);
		UE_LOG(LogAsdaLogin, Log, TEXT("[одержимость] слот %d: клик по заглушке места друга — модалка loc 990136, режим слота не меняется (%d)"),
			SelectedSlot, SlotMode[SelectedSlot]);
		return;
	}

	// Луч не попал ни во что: эталон всё равно доходит до `charDealWndRefreshInfo` (LABEL_38),
	//   сбросив подсветку. Подсветку порт не воспроизводит (её семантика не доказана — см. отчёт),
	//   а обновление панели повторяем.
	UpdateCharSelectInfo();
}

void AAsdaLoginGameMode::OnSoulmateSharedCharsUpdated()
{
	// ★ charsel_preload: персонажи соулмейта (1023/1024) — в тот же набор предзагрузки.
	RequestCharSelectPreload();
	// 1025 закрывает транзакцию 1005 — набор персонажей соулмейта полон. Пересобираем сцену
	//   ТОЛЬКО если шкатулка уже открыта (иначе спавн сделает штатный SpawnCharacterInSlot,
	//   который зовётся по концу открытия и сам зовёт SetupPossessScene).
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	int32 Ready = 0;
	if (Net) { for (const FAsdaSoulmateSharedChar& R : Net->GetSoulmateSharedChars()) { if (!R.IsEmpty()) { ++Ready; } } }
	UE_LOG(LogAsdaLogin, Log, TEXT("[одержимость] набор персонажей соулмейта обновлён: записей %d, шкатулка %s"),
		Ready, bCasketOpened ? TEXT("открыта") : TEXT("ещё закрыта"));
	if (!bCasketOpened || CasketSeq != ECasketSeqPhase::None) { return; }
	SetupPossessScene();
	UpdateCharSelectInfo();
}

void AAsdaLoginGameMode::OnCharSelectRefused(int32 Code)
{
	// Эталон `netHandleAppearanceChangeResult@0xF03A50` (1027, байт[0] ≠ 1):
	//   код 2 → флаг «соулмейт онлайн» + нотис loc 131 + разблокировка кнопки;
	//   прочие коды → эталон ТОЛЬКО сбрасывает `+768757` и кнопку НЕ разблокирует.
	// ★ Дополнительно сюда приходят коды пакета 1011 (отказ входа, `Send_SelectChar@0x40AD30`):
	//   201 = Secret/MD5/GroupIndex/Channel/UserIndex, 204 = UPCharPos/UPChannelCount. Они не
	//   пересекаются с кодами 1027 (0/2/3) — различаем по значению.
	switch (Code)
	{
	case 201:
		// ★ [ОРИГИНАЛ] раньше отказ 1011 был НЕВИДИМ (обработчика пакета не было). Своей loc-строки
		//   под этот код нет — не выдумываем (таблицы текста ведёт другой агент); чётко логируем и
		//   оставляем панель рабочей, чтобы игрок мог выбрать другого персонажа/повторить.
		UE_LOG(LogAsdaLogin, Warning, TEXT("[вход] отказ 1011 код 201: пакет 1016 отвергнут сервером (Secret/MD5/GroupIndex/Channel/UserIndex). Панель разблокирована"));
		break;
	case 204:
		UE_LOG(LogAsdaLogin, Warning, TEXT("[вход] отказ 1011 код 204: серверная процедура позиции (UPCharPos/UPChannelCount) не отдала данные. Панель разблокирована"));
		break;
	case 2:
		// «Your Soulmate joined the game.» — текст берём из loc-таблицы, а не из кода.
		ShowMessageWin(FAsdaLoginData::Loc(131));
		UE_LOG(LogAsdaLogin, Log, TEXT("[одержимость] вход отклонён: соулмейт сейчас в игре (1027 код 2, loc 131)"));
		break;
	case 3:
		// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (дефект оригинала, закон `fix-original-defects`): «у аккаунта нет
		//   активной пары» оригинал не показывает НИКАК и вдобавок оставляет панель заблокированной
		//   (`uiPane_setEnabledFlag` в этой ветке не зовётся). Своей строки под этот код в loc-таблице
		//   нет, выдумывать её нельзя — поэтому мы молчим так же, как эталон, но панель остаётся
		//   рабочей (в порту она и не блокируется), и причина отказа попадает в лог.
		UE_LOG(LogAsdaLogin, Warning, TEXT("[одержимость] вход отклонён: у аккаунта нет активной пары (1027 код 3)"));
		break;
	default:
		UE_LOG(LogAsdaLogin, Warning, TEXT("[одержимость] вход отклонён логин-сервером, код 1027 = %d"), Code);
		break;
	}
}
