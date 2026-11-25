// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginGameMode_Create.cpp — ★ 23.10.2026 часть AAsdaLoginGameMode, вынесенная из AsdaLoginGameMode.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaLoginGameMode.plan.json).
//   Удаление персонажа (BUT01 → MESSAGE2 + 1018), создание персонажа (CARAMAKE_WIN): окно, превью, витрина.
//   #include — те же, что у AsdaLoginGameMode.cpp, в том же порядке; общее — AsdaLoginGameModeInternal.h.
#include "Login/AsdaLoginGameMode.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "World/AsdaFadeComponent.h"         // ★ дедуп №2 В2-C (F9): AsdaTranslucentSlots::MakeSwapMid — общая MID-подмена слота
#include "Resources/AsdaSoundSubsystem.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaAdminSubsystem.h"   // сброс прав GM при возврате в меню логина
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"
#include "Login/AsdaServerAddress.h"   // ★ 16.10.2026: домен сервера
#include "AsdaConnection.h"           // ★ 16.10.2026: FAsdaConnection::PrefetchHost — фоновый резолв домена заранее
#include "Login/AsdaText.h"          // FAsdaText::Strip (срез JUI-разметки)
// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FormatPrintf — %d в строках DT_GameText   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted — единая точка форматированного текста   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
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
#include "Blueprint/WidgetLayoutLibrary.h"   // ★ масштаб вьюпорта для раскладки стартовой заставки
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
// [САМОТЕСТ] 08.10.2026: офлайн-гейт asda.Login.CreateNameTest   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
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

// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] Ключ отката: 1 = клик-зоны сетки создания только у 4 видимых рядов; 0 = как раньше, 25 зон (разбор у `Setup`).
static ASDA_DEV_CVAR(int32, CVarAsdaCreateGridVisibleRowsOnly,
	TEXT("asda.Login.CreateGridVisibleRowsOnly"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = в окне создания персонажа клик-зоны только у видимых рядов сетки; 0 = как раньше, зоны скрытого ряда перекрывают «Создать»/«Отмена»."),
	ECVF_Default);

// ★ 08.11.2026 (пачка 16, баг 10) [ИСПРАВЛЕНИЕ] Расстановка квада затемнения blackscreen за создаваемым персонажем.
//   ПРИЧИНА. Квад создаётся как голый `AActor` БЕЗ корня: `SpawnActor` такому актору позицию и поворот не ставит, а
//   `SetRootComponent` + `RegisterComponent` оставляют компонент в начале координат с нулевым поворотом. Геометрия меша
//   запечена в старых единицах ×100 (плоскость y = −29000, центр z = 50000). Пока сцена логина была ×100, это случайно
//   давало ровно эталонную картину (`BlackScreen_LoadModel@0xF0F380` ставит модель в 0,0,0 мира — плоскость между камерой
//   и шкатулкой). 12.09.2026 сцену перевели в ×1, камера встала у (1219, −1849), и плоскость оказалась в ~27 000 ед.
//   ПОЗАДИ камеры: затемнение исчезло (кадры с квадом и без совпадали до 0.1 яркости).
//   0 = как было (актор без расстановки — затемнения нет); 1 = по умолчанию: квад ставится туда, куда его задумывал код,
//   центром в точку сразу за персонажем на оси камеры, лицом к камере (закрывает весь кадр за персонажем при любой камере);
//   2 = [ОРИГИНАЛ] как в эталоне: модель в начале координат мира, масштаб ×(GLoginScale/100) — переводит запечённую ×100
//   геометрию в единицы сцены.
static ASDA_DEV_CVAR(int32, CVarAsdaCreateDimPlace,
	TEXT("asda.Login.CreateDimPlace"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Затемнение за создаваемым персонажем (квад blackscreen): 1 (по умолчанию) = квад за персонажем лицом к камере; ")
	TEXT("2 = как в эталоне, модель в начале координат в единицах сцены; 0 = как раньше (квад вне кадра, затемнения нет)."),
	ECVF_Default);
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
// ★ charsel_preload: фоновая предзагрузка превью экрана выбора   ★ R3: комментарий снятого включения "World/AsdaLoadManagerSubsystem.h" (файл им не пользовался) — сохранён
#include "PipelineStateCache.h"             // ★ charsel_preload: IsPSOPrecachingEnabled — прогрев PSO только где он есть
#include "AsdaLoginGameModeInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaLoginGameMode*.cpp (LogAsdaLogin, чистые хелперы)


// ═══════════════════════ УДАЛЕНИЕ персонажа (BUT01 → MESSAGE2 + 1018) ═══════════════════════

void AAsdaLoginGameMode::ShowDeleteConfirm()
{
	UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WinMgr) return;
	// Только для занятого слота.
	TArray<FAsdaCharRecord> CharList;
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>()) { CharList = Net->CharList; }
	if (!(CharList.IsValidIndex(SelectedSlot) && !CharList[SelectedSlot].IsEmpty())) return;
	ApplyHelper(28);   // §2.3: открыт диалог удаления → «Действительно удалить? Щёлкни Ok в течение 10 секунд…»

	// Последний ли персонаж (эталон countHighTypeSlots==1 → loc132, иначе loc129).
	int32 Occupied = 0;
	for (const FAsdaCharRecord& R : CharList) { if (!R.IsEmpty()) { ++Occupied; } }
	const int32 LocId = (Occupied == 1) ? 132 : 129;

	// Единая СИСТЕМА подтверждения (MESSAGE2). Кнопки: «Удалить» (loc 200000) / «Отменить» (loc 200001) — из таблицы.
	//   ★ gate-режим: «Удалить» НЕАКТИВНА (и визуально) пока гадж не заполнится за 10с (анти-мисклик; текст 6529
	//   «через ** сек будет удалён»). Yes → пакет 1018 удаления (charId сервер знает по слоту+аккаунту, slotNum=слот).
	AsdaEnterGuard::NoEnter(WinMgr)->ShowConfirm(FAsdaText::Loc(LocId), [this](bool bConfirmed)
	{
		if (!bConfirmed) { return; }
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			// СЫРОЙ slotId (10/11/12) из записи, НЕ индекс 0..2 (эталон netSendReq1018 payload[1].b0=rec[1076]).
			const uint8 RawSlot = Net->CharList.IsValidIndex(SelectedSlot) ? Net->CharList[SelectedSlot].RawSlotId : (uint8)(SelectedSlot + 10);
			DeleteSlot = SelectedSlot;   // слот чистится ЛОКАЛЬНО по ответу 1019 code 1 (OnCharDeleteResult)
			Net->SendDeleteChar(RawSlot, (uint16)Net->SelectedServerIndex);
			// ★ БЕЗ немедленного SendCharListRequest: запрос уходил ДО обработки удаления сервером — 1006 не
			//   приходил, слот «чистился со второго раза». Эталон (1019 case 1) чистит слот локально, без запроса.
			UE_LOG(LogAsdaLogin, Log, TEXT("Удаление персонажа слот %d rawSlot=%u → ждём 1019"), SelectedSlot, RawSlot);
		}
	}, 10.0f, /*bTimeoutConfirms*/false, /*YesLoc*/200000, /*NoLoc*/200001, /*bGateYesOnGauge*/true);
}

void AAsdaLoginGameMode::OnCharDeleteResult(int32 Code)
{
	// Эталон charCreate_OnCreateResultPacket@0xF03E90 (op 1019 = результат УДАЛЕНИЯ): code 1 → слот чистится
	//   ЛОКАЛЬНО (cleanup слот-объектов + глиф создания CharCreate_LoadCreationEffect + refresh инфо-панели),
	//   БЕЗ повторного запроса списка; codes 2/3/4 → notice loc 12434/12459/12820.
	switch (Code)
	{
	case 1:
	{
		const int32 Slot = (DeleteSlot >= 0 && DeleteSlot < 3) ? DeleteSlot : SelectedSlot;
		DeleteSlot = -1;
		if (UGameInstance* GI = GetGameInstance())
			if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
				if (Net->CharList.IsValidIndex(Slot)) { Net->CharList[Slot] = FAsdaCharRecord(); }   // Slot=0xFF = пусто
		SpawnCharacterInSlot();     // пересборка слотов: на опустевшем появится глиф создания (эталон-эквивалент)
		UpdateCharSelectInfo();
		UE_LOG(LogAsdaLogin, Log, TEXT("Удаление подтверждено (1019 code 1): слот %d очищен локально"), Slot);
		break;
	}
	case 2: ShowMessageWin(FAsdaText::Loc(12434)); break;
	case 3: ShowMessageWin(FAsdaText::Loc(12459)); break;
	case 4: ShowMessageWin(FAsdaText::Loc(12820)); break;
	default:
		UE_LOG(LogAsdaLogin, Warning, TEXT("1019 code=%d — отказ/неизвестный результат удаления"), Code);
		break;
	}
}

// ═══════════════════════ СОЗДАНИЕ персонажа (state 6, CARAMAKE_WIN) ═══════════════════════

TArray<int32> AAsdaLoginGameMode::GetDefaultParts(uint8 Gender) const
{
	// Первый ItemCode каждого partType (SlotType 0..4) для пола → в порядке отправки slotOrder {0,1,4,2,3}.
	int32 ByType[5] = { 0, 0, 0, 0, 0 };
	if (UGameInstance* GI = GetGameInstance())
		if (UAsdaItemTableSubsystem* Items = GI->GetSubsystem<UAsdaItemTableSubsystem>())
			if (UDataTable* DT = Items->GetDataTable(FName(TEXT("charequiptable"))))
			{
				TArray<FAsdaCharequiptableRow*> Rows;
				DT->GetAllRows<FAsdaCharequiptableRow>(TEXT("GetDefaultParts"), Rows);
				for (const FAsdaCharequiptableRow* R : Rows)
				{
					if (R && R->Gender == (int32)Gender && R->SlotType >= 0 && R->SlotType < 5 && ByType[R->SlotType] == 0)
						ByType[R->SlotType] = R->ItemCode;
				}
			}
	// Возвращаем по partType 0..4 (slotOrder применит SendCreateChar при упаковке пакета).
	TArray<int32> Out; Out.SetNum(5);
	for (int32 i = 0; i < 5; ++i) { Out[i] = ByType[i]; }
	return Out;
}

void AAsdaLoginGameMode::ShowCharCreateUI(int32 Slot)
{
	UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WinMgr) return;
	CurrentScreen = EAsdaScreen::CharCreate;
	CreateSlot = Slot;
	// Дефолты создания (Фаза 2 — выбор внешности стрелками/сеткой; Фаза 3 — 3D-превью).
	CreateGender = 1; CreateHair = 0; CreateColor = 0; CreateFace = 0; CreateConstellation = 0;   // 0 = созвездие НЕ выбрано (эталон init -1, обязателен)
	bCreateInFlight = false;   // свежий вход в создание → снять возможную блокировку
	// Префил дефолт-облика от сервера (0x3F7): причёска/цвет/лицо (имя — как подсказка ниже).
	FString DefaultName;
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		if (Net->CharCreateDefaults.bValid)
		{
			CreateHair  = Net->CharCreateDefaults.Hair;
			CreateColor = Net->CharCreateDefaults.Color;
			CreateFace  = Net->CharCreateDefaults.Face;
			DefaultName = Net->CharCreateDefaults.Name;
		}
	}

	// Скрыть char-select (монолит CHARSELECT_WIN), показать экран создания (эталон Game_SetState(6)).
	WinMgr->SetWindowVisible(FName(TEXT("CHARSELECT_WIN")), false);
	// ★ Вместе с ним — панель «одержимости» 114 и ловец кликов по 3D (на экране создания клики по
	//   сцене свои: там крутят превью стрелками, а не выбирают персонажа лучом).
	ShowSoulInfoPanel(false, -1);
	WinMgr->SetWindowVisible(FName(TEXT("CHARSELECT_PICK")), false);

	// Подписка на результат создания (1019) — один раз.
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		if (!bCharCreateResultBound)
		{
			Net->OnCharCreateResult.AddUObject(this, &AAsdaLoginGameMode::OnCharCreateResult);
			bCharCreateResultBound = true;
		}
	}

	if (!CharCreateWidget)
	{
		CharCreateWidget = WinMgr->OpenWindowByPath(FName(TEXT("CARAMAKE_WIN")),
			TEXT("/Game/AsdaAssets/UI/Login/WBP_CARAMAKE_WIN"), 16, ESlateVisibility::Visible);
		if (!CharCreateWidget) { UE_LOG(LogAsdaLogin, Warning, TEXT("WBP_CARAMAKE_WIN не загружен")); return; }

		// ★ ДИАГНОСТИКА: перечислить ВСЕ виджеты WBP_CARAMAKE_WIN (найти реальные имена стрелок вращения).
		if (CharCreateWidget->WidgetTree)
		{
			TArray<UWidget*> AllW; CharCreateWidget->WidgetTree->GetAllWidgets(AllW);
			for (UWidget* W : AllW)
			{
				if (W && (W->GetName().Contains(TEXT("BUT")) || W->GetName().Contains(TEXT("LEFT")) ||
					W->GetName().Contains(TEXT("RIGHT")) || W->GetName().Contains(TEXT("ARROW")) || W->IsA<UButton>()))
				{
					UE_LOG(LogAsdaLogin, Warning, TEXT("[CHARCREATE-WIDGET] %s (%s)"), *W->GetName(), *W->GetClass()->GetName());
				}
			}
		}

		auto BindBtn = [&](const FString& N, TFunction<void(int32)> Cb)
		{
			if (UButton* B = Cast<UButton>(CharCreateWidget->GetWidgetFromName(FName(*N))))
			{
				AsdaUi::BindButton(CharCreateWidget, B, 0, MoveTemp(Cb), ButtonRelays);   // ★ U20: реле клика — общая точка
			}
		};
		// Пол (эталон 51 SexBtn): две ОТДЕЛЬНЫЕ иконки-кнопки — клик по ♂ (OPT0) выбирает МУЖ, по ♀ (OPT1) ЖЕН
		//   (НЕ toggle). Смена пола сбрасывает внешность (charCreateScene_Reset) + пересбор превью/вариантов.
		auto SetGender = [this](int32 G)
		{
			if (CreateGender == G) { return; }
			CreateGender = G;
			CreateHair = 0; CreateColor = 0; CreateFace = 0;   // сброс внешности при смене пола (иначе лицо out-of-range)
			// Сброс showcase (клип-набор другого пола) — новый превью встаёт в idle.
			bCreateShowcase = false; CreateAnimState = 0;
			if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(CreateShowcaseTimer); }
			BuildCreateCategories(CreateGender);               // варианты частей зависят от пола
			PopulateIconGrid();
			ArrangeCreateSelMarkers();                         // рамки на дефолт-выбор нового пола
			SpawnCreatePreview();
			UpdateGenderHighlight();                           // выбранный пол → жёлтый (эталон FFFFFF00)
		};
		BindBtn(TEXT("CARAMAKE_SEXBUT_OPT0"), [SetGender](int32) { SetGender(1); });   // ♂ мужской
		BindBtn(TEXT("CARAMAKE_SEXBUT_OPT1"), [SetGender](int32) { SetGender(2); });   // ♀ женский
		// 6 стрелок внешности (эталон 53-58): причёска/цвет/лицо prev/next.
		BindBtn(TEXT("CARAMAKE_ARROWBUT00"), [this](int32) { CycleCreateAppearance(0, -1); });  // причёска ←
		BindBtn(TEXT("CARAMAKE_ARROWBUT01"), [this](int32) { CycleCreateAppearance(0, +1); });  // причёска →
		BindBtn(TEXT("CARAMAKE_ARROWBUT02"), [this](int32) { CycleCreateAppearance(1, -1); });  // цвет ←
		BindBtn(TEXT("CARAMAKE_ARROWBUT03"), [this](int32) { CycleCreateAppearance(1, +1); });  // цвет →
		BindBtn(TEXT("CARAMAKE_ARROWBUT04"), [this](int32) { CycleCreateAppearance(2, -1); });  // лицо ←
		BindBtn(TEXT("CARAMAKE_ARROWBUT05"), [this](int32) { CycleCreateAppearance(2, +1); });  // лицо →
		// OK (93) → создать; Cancel (94) → назад в char-select.
		BindBtn(TEXT("CARAMAKE_OKBUT"),     [this](int32) { SubmitCharCreate(); });
		BindBtn(TEXT("CARAMAKE_CANCELBUT"), [this](int32) { HideCharCreate(true); });
		// Сетка частей: BACK/NEXT сдвиг вариантов (эталон 89/90).
		BindBtn(TEXT("CARAMAKE_BACKBUT"), [this](int32) { ScrollIconGrid(-1); });
		BindBtn(TEXT("CARAMAKE_NEXTBUT"), [this](int32) { ScrollIconGrid(+1); });
		// ★ B3 ВРАЩЕНИЕ превью: кнопки — ОТДЕЛЬНЫЕ WBP-окна WBP_CARAMAKE_LEFTBUT/RIGHTBUT (конвертер вынес их
		//   отдельными виджет-блюпринтами, НЕ детьми CARAMAKE_WIN). Открываем их как окна, находим внутри UButton,
		//   бинжим УДЕРЖАНИЕ → ±6°/кадр (эталон objPitchStepBy6Deg@0xF12310). z=17 поверх сцены.
		auto OpenRotBtn = [&](const TCHAR* Path, const FName Id, int32 Dir)
		{
			UUserWidget* RW = WinMgr->OpenWindowByPath(Id, Path, 17, ESlateVisibility::Visible);
			if (!RW || !RW->WidgetTree) { UE_LOG(LogAsdaLogin, Warning, TEXT("[CHARCREATE] %s не открыт"), Path); return; }
			UButton* B = nullptr;
			TArray<UWidget*> All; RW->WidgetTree->GetAllWidgets(All);
			for (UWidget* x : All) { if ((B = Cast<UButton>(x))) break; }
			if (B)
			{
				UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(RW);
				R->Index = Dir;
				R->PressCallback = [this](int32 D, bool bDown) { CreateRotDir = bDown ? D : 0; };
				R->HoverCallback   = [this]() { ApplyHelper(36); };   // §3.2 наведение на стрелку поворота → «Можно поворачивать…»
				R->UnhoverCallback = [this]() { ApplyHelper(35); };
				B->OnPressed.AddDynamic(R, &UAsdaButtonRelay::OnPressed);
				B->OnReleased.AddDynamic(R, &UAsdaButtonRelay::OnReleased);
				B->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
				B->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
				AsdaUi::AnchorRelay(ButtonRelays, R, B);
			}
			UE_LOG(LogAsdaLogin, Warning, TEXT("[CHARCREATE] %s открыт, кнопка=%d"), Path, B ? 1 : 0);
		};
		OpenRotBtn(TEXT("/Game/AsdaAssets/UI/Login/WBP_CARAMAKE_LEFTBUT"),  FName(TEXT("CARAMAKE_LEFTBUT")),  -1);
		OpenRotBtn(TEXT("/Game/AsdaAssets/UI/Login/WBP_CARAMAKE_RIGHTBUT"), FName(TEXT("CARAMAKE_RIGHTBUT")), +1);
		// Сетка иконок частей (25 ячеек CARAMAKE_ICONBOX00-24) — через переиспользуемую систему UAsdaIconCellGrid.
		//   Она кладёт прозрачные клик-кнопки поверх JUI-ячеек и рендерит иконки; модель данных (что в ячейке
		//   и что по клику) — наши провайдеры CreateCellCode/OnIconCellClicked.
		if (!IconGrid)
		{
			IconGrid = NewObject<UAsdaIconCellGrid>(this);
			// ★★★ 06.11.2026 [ИСПРАВЛЕНИЕ] Клик-зоны только у ВИДИМЫХ рядов. Пятый ряд (BLANK20-24) в JUI `Visible="0"`, в оригинале
			//   скрытый контрол клика не получает. Зоны сетки живут в корне с Z=50 и строились для всех 25 ячеек, поэтому зоны
			//   HIT20/21 ложились поверх «Создать»/«Отмена» (своя площадь 16 и 94 из 328 точек). Зон в ассете нет, правится их число.
			const int32 GridCells = CVarAsdaCreateGridVisibleRowsOnly.GetValueOnGameThread() != 0 ? 20 : 25;
			IconGrid->Setup(CharCreateWidget, TEXT("CARAMAKE_ICONBOX"), GridCells, (CreateGender != 2),
				[this](int32 Cell) { return CreateCellCode(Cell); },
				[this](int32 Cell) { OnIconCellClicked(Cell); });
		}
		// ★ 08.11.2026: пол иконок — ЯВНЫЙ пол создаваемого персонажа (у сетки `Setup(true)` теперь значит «свой
		//   персонаж», а на экране создания своего ещё нет). Тот же вызов делает смена пола (ниже по файлу).
		if (IconGrid) { IconGrid->SetMale(CreateGender != 2); }

		// Поле имени: дефолт-имя от сервера (0x3F7) в текст + подсказка. CARAMAKE_NAMEINPUT = UEditableText.
		if (UEditableText* E = Cast<UEditableText>(CharCreateWidget->GetWidgetFromName(TEXT("CARAMAKE_NAMEINPUT"))))
		{
			E->SetHintText(FText::FromString(FAsdaText::Loc(1198).IsEmpty() ? TEXT("[#1198]") : FAsdaText::Loc(1198)));   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
			if (!DefaultName.IsEmpty()) { E->SetText(FText::FromString(DefaultName)); }
		}
		// Комбобокс СОЗВЕЗДИЯ (эталон 99, обязателен): ИГРОВОЙ комбо (скин Bk_ComboBox + список DarkGrey_List),
		//   а НЕ дефолтный UComboBoxString. Строится из атласа n_02; 0 = не выбрано (гейт OK до выбора).
		CreateConstellation = 0;
		BuildGameConstellationCombo();

		// ★ Подсказки помощника char-create (HELPER_HINT_CASES_RE.md §3.2). Наведение на контрол → запись; поле
		//   имени → фокус (rec39); уход → rec35 «Создай персонажа…». Пол М/Ж→37/38, причёска→40, цвет→41, лицо→42,
		//   «все наборы»→49, создать→50, отмена→51. Поворот (LEFTBUT/RIGHTBUT — отдельные окна)→36 (в OpenRotBtn).
		HelperFocusFields.Reset();
		CurrentHelperDefault = 35;
		AddHelperFocusField(CharCreateWidget, TEXT("CARAMAKE_NAMEINPUT"), 39);
		RegisterHelperFocus();
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_SEXBUT_OPT0"), 37, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_SEXBUT_OPT1"), 38, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_ARROWBUT00"),  40, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_ARROWBUT01"),  40, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_ARROWBUT02"),  41, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_ARROWBUT03"),  41, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_ARROWBUT04"),  42, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_ARROWBUT05"),  42, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_OKBUT"),       50, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_CANCELBUT"),   51, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_BACKBUT"),     49, 35);
		BindHelperHover(CharCreateWidget, TEXT("CARAMAKE_NEXTBUT"),     49, 35);
	}
	else
	{
		WinMgr->SetWindowVisible(FName(TEXT("CARAMAKE_WIN")), true);
		HelperFocusFields.Reset();
		CurrentHelperDefault = 35;
		AddHelperFocusField(CharCreateWidget, TEXT("CARAMAKE_NAMEINPUT"), 39);
	}
	// ★ Подписи полей char-create — рантайм по loc-ID (эталон ui_core.c init @95740; в JUI CaptionText пусты →
	//   без этого текст не виден). never-invent: строки из DT_GameText (error list.bin), как char-select.
	if (CharCreateWidget)
	{
		auto SetTxt = [&](const TCHAR* N, const FString& S)
		{
			if (UTextBlock* T = Cast<UTextBlock>(CharCreateWidget->GetWidgetFromName(FName(N))))
				T->SetText(FText::FromString(S));
		};
		SetTxt(TEXT("CARAMAKE_TXT00"), FAsdaLoginData::Loc(11016));   // Пол
		SetTxt(TEXT("CARAMAKE_TXT01"), FAsdaLoginData::Loc(11017));   // Имя
		SetTxt(TEXT("CARAMAKE_TXT02"), FAsdaLoginData::Loc(11018));   // Волосы
		SetTxt(TEXT("CARAMAKE_TXT03"), FAsdaLoginData::Loc(11019));   // Цвет
		SetTxt(TEXT("CARAMAKE_TXT04"), FAsdaLoginData::Loc(11020));   // Лицо
		SetTxt(TEXT("CARAMAKE_tt2"),   FAsdaLoginData::Loc(110631));  // Знак зодиака
		// ★ Зодиак-метка tt2 в JUI Visible="0" (дизайн) → конвертер поставил Collapsed; но в char-create она ПОКАЗЫВАЕТСЯ
		//   (игра включает рантаймом). Раскрываем её + фрейм/дропдаун комбо (на случай коллапса предком).
		auto Show = [&](const TCHAR* N)
		{
			if (UWidget* W = CharCreateWidget->GetWidgetFromName(FName(N)))
				W->SetVisibility(ESlateVisibility::Visible);
		};
		Show(TEXT("CARAMAKE_tt2"));   // метка «Знак зодиака»
		Show(TEXT("CARAMAKE_st"));    // рамка бокса комбо (tt/cb управляет игровой комбо: скрывает UE-дефолт)
	}
	// ★ Вход в создание: сброс showcase-состояния (эталон login_charselect.c:1037/1066 — animState(118382)=0).
	bCreateShowcase = false;
	CreateAnimState = 0;
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(CreateShowcaseTimer); }
	BuildCreateCategories(CreateGender);   // варианты частей из CharEquipTable
	SpawnCreatePreview();          // 3D-превью создаваемого персонажа = ОТДЕЛЬНЫЙ объект (B2)
	PopulateIconGrid();            // наполнить сетку иконок
	ArrangeCreateSelMarkers();     // сияющие рамки выбранных вариантов (эталон uiArrangeSkillSlots5)
	UpdateGenderHighlight();       // подсветить дефолт-пол (♂) жёлтым (эталон Log_CaracterSex FFFFFF00)
	// ★ B1 ЗАТЕМНЕНИЕ: отдельное топ-окно WBP_GRAD_BG_FULL (полноэкранный градиент-виньетка, JUI depth 0) —
	//   z=13 под панелью CARAMAKE(16), поверх 3D-сцены. Не блокирует ввод. Открывается как часть сцены создания.
	WinMgr->OpenWindowByPath(FName(TEXT("GRAD_BG_FULL")), TEXT("/Game/AsdaAssets/UI/Login/WBP_GRAD_BG_FULL"), 13, ESlateVisibility::SelfHitTestInvisible);
	WinMgr->SetWindowVisible(FName(TEXT("GRAD_BG_FULL")), true, ESlateVisibility::SelfHitTestInvisible);
	WinMgr->SetWindowVisible(FName(TEXT("CARAMAKE_LEFTBUT")),  true);   // показать кнопки вращения (топ-окна)
	WinMgr->SetWindowVisible(FName(TEXT("CARAMAKE_RIGHTBUT")), true);
	WinMgr->SetWindowVisible(FName(TEXT("WBP_ANI_WIN")), true);   // ★ B4: показать окно робота-помощника (в char-select скрыто)
	ApplyHelper(35);               // ★ ВХОД в создание = запись 35 «Хорошо. Создай персонажа. Подбери одежду…»
	                               //   (эталон entity_char.c:51549 uiSelectListItem(35)). Запись 33 «войти призраком,
	                               //   подожди минуту» — это ПОСЛЕ клика «Создать» (dealMode==1), переносится в SubmitCharCreate.
	FocusUI(CharCreateWidget);
}

void AAsdaLoginGameMode::HideCharCreate(bool bBackToSelect)
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		WM->SetWindowVisible(FName(TEXT("CARAMAKE_WIN")), false);
	// Остановить one-shot showcase и сбросить состояние.
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(CreateShowcaseTimer); }
	bCreateShowcase = false;
	CreateAnimState = 0;
	TeardownCreateScene();   // снести отдельный превью-объект + вернуть слот-персонажей char-select
	if (bBackToSelect) { ShowCharSelectUI(); }
}

void AAsdaLoginGameMode::SubmitCharCreate()
{
	if (!CharCreateWidget) return;
	FString Name;
	if (UEditableText* E = Cast<UEditableText>(CharCreateWidget->GetWidgetFromName(TEXT("CARAMAKE_NAMEINPUT"))))
		Name = E->GetText().ToString().TrimStartAndEnd();
	UE_LOG(LogAsdaLogin, Warning, TEXT("[SUBMIT] нажато Создать: имя='%s' созвездие=%d пол=%d причёска=%d цвет=%d лицо=%d слот=%d канал=%d"),
		*Name, CreateConstellation, CreateGender, CreateHair, CreateColor, CreateFace, CreateSlot, SelectedChannel);
	// Клиентская валидация имени (эталон: пустое → loc 9582; недопустимое/апостроф → loc 9583; НЕ серверный код 12459).
	if (Name.IsEmpty())
	{
		SetLoginStatus(FAsdaText::Loc(9582));   // пустое имя
		ShowMessageWin(FAsdaText::Loc(9582));   // ★ 08.10.2026 [ОРИГИНАЛ]: окно сообщения (ui_core.c:95062-95066, см. ниже)
		ApplyHelper(52);                        // §3.4: «У твоего персонажа всё ещё нет имени…»
		return;
	}
	// ★ Созвездие ОБЯЗАТЕЛЬНО (эталон ui_core.c:94882: if(constellation==-1) notice 110635; порядок — ПОСЛЕ
	//   пустого имени, ДО charset-валидации). CreateConstellation<1 = «не выбрано».
	if (CreateConstellation < 1)
	{
		SetLoginStatus(FAsdaText::Loc(110635));   // «выберите созвездие»
		ShowMessageWin(FAsdaText::Loc(110635));   // ★ 08.10.2026 [ОРИГИНАЛ]: окно сообщения (ui_core.c:94900-94903)
		return;
	}
	// ★ [ПОРТ] 08.10.2026: длина — ≤ 18 СИМВОЛОВ (эталон) И ≤ 19 БАЙТ провода (поле char[20] пакета 1014 и столбец
	//   Charinfo.Charname varchar(20)). Русское/латинское имя — 18 байт cp1251 как раньше, арабское — 18 букв + маркер 0x98 = 19.
	if (Name.Len() > 18 || FAsdaLoginData::WireTextByteLen(Name) > 19 || Name.Contains(TEXT("'")))   // + запрет апострофа (эталон)
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("CharCreate: имя недопустимо '%s'"), *Name);
		SetLoginStatus(FAsdaText::Loc(9583));   // недопустимое имя
		// ★ 08.10.2026 [ОРИГИНАЛ] (batch_fixes Блок 3): эталон `ui_core.c:94918-94922` кладёт текст 9583 в ОКНО СООБЩЕНИЯ
		//   (`uiControl_setTextAndFlag(g_pGameApp, …, 9583)` пишет его в контрол +118274, `wndShowHideOrQuit(g_pGameApp, 1)`
		//   показывает окно) и только затем ставит помощника 53. В порту окна не было, а пузырь `SetLoginStatus` тут же
		//   перезаписывал `ApplyHelper(53)` — строка 9583 на экране не появлялась вовсе. Те же два шага у пустого имени (9582)
		//   и невыбранного созвездия (110635).
		ShowMessageWin(FAsdaText::Loc(9583));
		ApplyHelper(53);                        // §3.4: «В имени введён неверный символ…»
		return;
	}
	// ★ validateStringByLocale (эталон isValidNameCharAscii@0x9570A0, локаль ASCII case2/6): разрешены ТОЛЬКО
	//   латиница A-Z/a-z и цифры 0-9. КИРИЛЛИЦА/пробелы/спецсимволы ЗАПРЕЩЕНЫ (русское имя недопустимо). Забаненные
	//   слова проверяет СЕРВЕР (Send_CreateChar CharName Curse → 1026 code 0/2/3 → notice в OnCharCreateResult).
	// ★ [ПОРТ] 08.10.2026: правило символов — локальные валидаторы ОРИГИНАЛА (кириллическая case 7 ИЛИ арабская case 8),
	//   см. `FAsdaLoginData::IsValidCharNameByLocale`; `asda.Text.NameLocale 0` — прежнее правило (только A-Z a-z 0-9).
	if (!FAsdaLoginData::IsValidCharNameByLocale(Name))
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("CharCreate: имя '%s' не проходит правило локали (asda.Text.NameLocale)"), *Name);
		SetLoginStatus(FAsdaText::Loc(9583));   // недопустимое имя (спецсимвол/смешение алфавитов)
		ShowMessageWin(FAsdaText::Loc(9583));   // ★ 08.10.2026 [ОРИГИНАЛ]: окно сообщения — разбор в ветке выше
		ApplyHelper(53);                        // §3.4: «В имени введён неверный символ…»
		return;
	}
	// ★ Один запрос за раз: повторные клики до ответа 1026 десинкают anti-replay секрет → сервер кикает (4101 code=3).
	if (bCreateInFlight)
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("[SUBMIT] запрос уже в полёте — игнор повторного клика"));
		return;
	}
	bCreateInFlight = true;
	UAsdaNetSubsystem* NetChk = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UE_LOG(LogAsdaLogin, Warning, TEXT("[SUBMIT] гейты пройдены → отправка SendCreateChar (Net=%s)"), NetChk ? TEXT("есть") : TEXT("НЕТ!"));
	bool bSent = false;
	if (UGameInstance* GI = GetGameInstance())
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
			// ★ СЫРОЙ slotId = позиция+10 (10/11/12), НЕ индекс 0..2. Доказано клиент-эталоном:
			//   charDealSendReq create-ветка → uiInputFieldSetTextFocus(wnd, pos+10) → pDlg+473524 → payload+6.
			//   (create всегда на ПУСТОЙ слот → сырой slotId = позиция+10, как fallback у select/delete.)
			bSent = Net->SendCreateChar((uint8)(CreateSlot + 10), Name, CreateGender, CreateHair, CreateColor, CreateFace,
				CreateConstellation, GetDefaultParts(CreateGender), (uint16)Net->SelectedServerIndex);
	if (!bSent)   // отправка не удалась (нет соединения) → снять блок, чтобы можно было повторить
	{
		bCreateInFlight = false;
		UE_LOG(LogAsdaLogin, Warning, TEXT("[SUBMIT] SendCreateChar FAIL — блок снят"));
	}
	else
	{
		ApplyHelper(33);   // ★ после клика «Создать» — запись 33 «…войти призраком. Подожди минуту» (эталон dealMode==1)
	}
}

void AAsdaLoginGameMode::OnCharCreateResult(int32 Code)
{
	// ★ ПОЛНАЯ семантика op 1026 (эталон netHandleNameEntryDialogPacket): все коды обрабатываются.
	bCreateInFlight = false;   // ответ получен → разблокировать кнопку «Создать»
	switch (Code)
	{
	case 1:   // УСПЕХ (эталон case 1): закрыть модалку → ФОРДЖИНГ (шкатулка закрывается, свечение eft_creation01,
		//   открытие, камера отлетает/подлетает) → перс в слоте с фейдом. UI char-select вернёт конец секвенции.
		UE_LOG(LogAsdaLogin, Log, TEXT("CharCreate УСПЕХ слот %d → форджинг-анимация"), CreateSlot);
		HideCharCreate(false);   // БЕЗ мгновенного показа char-select UI (его вернёт ForgeOpen-фаза)
		if (UGameInstance* GI = GetGameInstance())
			if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
				Net->SendCharListRequest((uint16)Net->SelectedServerIndex);   // свежий 1006/1007 с созданным персом придёт за время анимации
		StartForgeSequence();
		break;
	case 2:   // ИМЯ ЗАНЯТО (эталон case 2: notice loc 112 + робот uiSelectListItem(55)) — фокус в поле имени.
		SetLoginStatus(FAsdaText::Loc(112));   // «Такое имя персонажа уже есть.»
		ApplyHelper(55);                        // робот: «Имя уже используется. Постарайся придумать другое имя.»
		if (CharCreateWidget)
			if (UEditableText* E = Cast<UEditableText>(CharCreateWidget->GetWidgetFromName(TEXT("CARAMAKE_NAMEINPUT"))))
				E->SetKeyboardFocus();
		UE_LOG(LogAsdaLogin, Warning, TEXT("CharCreate: имя занято (code 2, робот 55)"));
		break;
	case 3:   // НЕВЕРНЫЙ СИМВОЛ/ЦЕНЗУРА (эталон case 3: notice loc 113 + робот uiSelectListItem(53)).
		SetLoginStatus(FAsdaText::Loc(113));
		ApplyHelper(53);                        // робот: «В имени введён неверный символ… нецензурное слово…»
		if (CharCreateWidget)
			if (UEditableText* E = Cast<UEditableText>(CharCreateWidget->GetWidgetFromName(TEXT("CARAMAKE_NAMEINPUT"))))
				E->SetKeyboardFocus();
		UE_LOG(LogAsdaLogin, Warning, TEXT("CharCreate: недопустимое/цензура имя (code 3, робот 53)"));
		break;
	case 0:   // ошибка общая (эталон case 0: notice loc 111 + робот uiSelectListItem(54)).
	default:
		SetLoginStatus(FAsdaText::Loc(111));
		ApplyHelper(54);                        // робот: «Снова создать персонажа.»
		UE_LOG(LogAsdaLogin, Warning, TEXT("CharCreate ошибка code=%d (робот 54)"), Code);
		break;
	}
}

void AAsdaLoginGameMode::OnConstellationSelected(FString SelectedItem, ESelectInfo::Type)
{
	if (UComboBoxString* Cb = CharCreateWidget ? Cast<UComboBoxString>(CharCreateWidget->GetWidgetFromName(TEXT("CARAMAKE_cb"))) : nullptr)
	{
		// Эталон шлёт индекс+1 (1-based, 1..12): *(this+119885)=selIdx+1. 0 = «не выбрано» (недопустимо).
		CreateConstellation = (uint8)(FMath::Max(0, Cb->GetSelectedIndex()) + 1);
	}
}

// ★ ИГРОВОЙ комбобокс созвездия (never-invent: воспроизводим скин Bk_ComboBox + список DarkGrey_List, не UE-дефолт).
//   Закрытый бокс = тёмная заливка FF153549 + стрелка ↓ (атлас n_02 UV). Клик → выпадающий список 12 знаков
//   (item-фон = n_02 UV DarkGrey_List) → клик по знаку выбирает и закрывает. Логика 1:1 (CreateConstellation=idx+1).
void AAsdaLoginGameMode::BuildGameConstellationCombo()
{
	UUserWidget* W = CharCreateWidget;
	if (!W || !W->WidgetTree) return;
	UWidget* CbW = W->GetWidgetFromName(TEXT("CARAMAKE_cb"));
	UCanvasPanelSlot* CbSlot = CbW ? AsdaUi::SlotOf(CbW) : nullptr;
	UCanvasPanel* CP = CbW ? Cast<UCanvasPanel>(CbW->GetParent()) : nullptr;
	if (!CbSlot || !CP) { UE_LOG(LogAsdaLogin, Warning, TEXT("[COMBO] CARAMAKE_cb/canvas не найдены")); return; }
	// ★ ФИКС ЗОДИАКА: JUI ставит CARAMAKE_tt2 (корень ветки tt2→st→cb) Visible="0" → конвертер схлопнул ВСЮ
	//   зодиак-группу (вместе с cb и его canvas CP). Игровой комбо строится под CP → под схлопнутым предком →
	//   невидим (метки/бокса нет в UI). Point-фикс Show(tt2)/Show(st) не спасает, если есть промежуточный
	//   схлопнутый предок. Раскрываем ВСЮ цепочку предков cb (сам cb НЕ трогаем — он прячет UE-дефолт-комбо).
	{
		int32 nFixed = 0; FString Chain;
		for (UWidget* Anc = CbW->GetParent(); Anc; Anc = Anc->GetParent())
		{
			const ESlateVisibility V = Anc->GetVisibility();
			const bool bHid = (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden);
			Chain += FString::Printf(TEXT("%s(%d)%s "), *Anc->GetName(), (int32)V, bHid ? TEXT("!") : TEXT(""));
			if (bHid) { Anc->SetVisibility(ESlateVisibility::SelfHitTestInvisible); ++nFixed; }   // раскрыть предка, не перехватывать клики (дети — да)
		}
		UE_LOG(LogAsdaLogin, Warning, TEXT("[COMBO] раскрыто предков cb: %d; цепочка: %s"), nFixed, *Chain);
	}
	// ★ Кладём элементы комбо ТЕМИ ЖЕ анкором+выравниванием, что у реального слота cb (НЕ хардкод «центр 0.5» —
	//   он давал сдвиг, если ApplyResponsiveSlot дал cb иной анкор/родитель). Тогда бокс точно накрывает cb.
	const FMargin Off = CbSlot->GetOffsets();
	const FAnchors CbAnch = CbSlot->GetAnchors();
	const FVector2D CbAlign = CbSlot->GetAlignment();
	const float BL = Off.Left, BT = Off.Top, BW = Off.Right, BH = Off.Bottom;   // Left/Top=позиция, Right/Bottom=размер (для точечного анкора)
	UE_LOG(LogAsdaLogin, Warning, TEXT("[COMBO] cb: parent=%s off=(%.0f,%.0f,%.0f,%.0f) anch=(%.2f,%.2f..%.2f,%.2f) align=(%.2f,%.2f)"),
		*CP->GetName(), BL, BT, BW, BH, CbAnch.Minimum.X, CbAnch.Minimum.Y, CbAnch.Maximum.X, CbAnch.Maximum.Y, CbAlign.X, CbAlign.Y);
	CbW->SetVisibility(ESlateVisibility::Collapsed);   // спрятать UE-дефолт-комбо
	if (UWidget* TtW = W->GetWidgetFromName(TEXT("CARAMAKE_tt"))) { TtW->SetVisibility(ESlateVisibility::Collapsed); }

	UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	UTexture2D* N02 = TexMgr ? TexMgr->LoadTextureCached(TEXT("/Game/AsdaAssets/Textures/JUI/n_02.n_02")) : nullptr;
	UWidgetTree* Tree = W->WidgetTree;
	UFont* GameFont = LoadObject<UFont>(nullptr, TEXT("/Game/AsdaAssets/Fonts/AsdaUIFontRT"));   // шрифт UI порта (arial/arialbd — см. AsdaUmgBuilder::GetOrCreateAsdaUIFont)

	// bResp=true → анкор+выравнивание КАК У cb (l,t=позиция,w,h=размер в его анкор-системе); false → SetPosition (внутри Drop).
	auto Place = [CbAnch, CbAlign](UPanelSlot* PS, float l, float t, float w, float h, int32 Z, bool bResp)
	{
		UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(PS); if (!S) return;
		if (bResp) { S->SetAnchors(CbAnch); S->SetAlignment(CbAlign); S->SetOffsets(FMargin(l, t, w, h)); }
		else { S->SetPosition(FVector2D(l, t)); S->SetSize(FVector2D(w, h)); }
		S->SetZOrder(Z);
	};
	auto MkImg = [&](UPanelWidget* P, float l, float t, float w, float h, UTexture2D* Tex, FBox2D UV, FLinearColor Tint, int32 Z, bool bR)
	{
		UImage* Img = Tree->ConstructWidget<UImage>(UImage::StaticClass(), NAME_None);
		FSlateBrush Br; if (Tex) { Br.SetResourceObject(Tex); Br.SetUVRegion(UV); }
		Br.ImageSize = FVector2D(w, h); Br.TintColor = FSlateColor(Tint); Img->SetBrush(Br);
		Place(P->AddChild(Img), l, t, w, h, Z, bR); return Img;
	};
	auto MkTxt = [&](UPanelWidget* P, float l, float t, float w, float h, const FString& Txt, int32 Z, bool bR)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), NAME_None);
		T->SetText(FText::FromString(Txt)); T->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.88f, 0.88f, 1.f)));
		if (GameFont) { T->SetFont(FSlateFontInfo(GameFont, 12, FName(TEXT("Regular")))); }   // игровой шрифт 12пт
		T->SetAutoWrapText(false);
		T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);   // текст поверх — не перехватывать мышь (hover/click к кнопке под ним)
		Place(P->AddChild(T), l + 5.f, t + 2.f, w, h, Z, bR); return T;
	};
	auto MkHit = [&](UPanelWidget* P, float l, float t, float w, float h, int32 Z, bool bR, int32 Idx, TFunction<void(int32)> Cb)
	{
		UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass(), NAME_None);
		FButtonStyle St = Btn->GetStyle();
		// hover/pressed из эталона Bk_ComboBox ES0: normal прозрачный, hover FF225777 (синий), pressed FF153549.
		auto Fill = [](FLinearColor C){ FSlateBrush B; B.DrawAs = ESlateBrushDrawType::Image; B.TintColor = FSlateColor(C); return B; };
		St.Normal  = Fill(FLinearColor(1.f, 1.f, 1.f, 0.f));
		St.Hovered = Fill(FLinearColor(0x22 / 255.f, 0x57 / 255.f, 0x77 / 255.f, 1.f));
		St.Pressed = Fill(FLinearColor(0x15 / 255.f, 0x35 / 255.f, 0x49 / 255.f, 1.f));
		Btn->SetStyle(St);
		AsdaUi::BindButton(this, Btn, Idx, MoveTemp(Cb), ButtonRelays);   // ★ U20: реле клика — общая точка
		Place(P->AddChild(Btn), l, t, w, h, Z, bR); return Btn;
	};

	const FLinearColor DarkFill(0x15 / 255.f, 0x35 / 255.f, 0x49 / 255.f, 1.f);   // Bk_ComboBox ES0 plane2 FF153549
	const FBox2D UVArrow(FVector2D(0.785f, 0.520f), FVector2D(0.824f, 0.559f));   // Bk_ComboBox ES1 стрелка ↓ (n_02)
	const FBox2D UVItem(FVector2D(0.285f, 0.816f), FVector2D(0.371f, 0.840f));    // DarkGrey_List item-фон (n_02)

	// Закрытый бокс (отзывчиво на CP): тёмная заливка + стрелка справа + дисплей выбранного знака + клик-переключатель.
	//   ★ Заливка(40) и стрелка(42) = SelfHitTestInvisible: они НАД переключателем(41) и иначе перехватывают клик
	//   (клик по иконке-стрелке ничего не делал). Теперь клики по ВСЕМУ боксу (вкл. стрелку) идут к переключателю.
	if (UImage* Bg = MkImg(CP, BL, BT, BW, BH, nullptr, FBox2D(), DarkFill, 40, true)) { Bg->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	if (UImage* Arr = MkImg(CP, BL + BW - 18.f, BT + 1.f, 18.f, 18.f, N02, UVArrow, FLinearColor::White, 42, true)) { Arr->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	ConstellationDisplay = MkTxt(CP, BL, BT, BW, BH, TEXT(""), 43, true);

	// Выпадающий список = canvas-контейнер под боксом (отзывчиво), collapsed; items внутри — обычным SetPosition.
	UCanvasPanel* Drop = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ConstellationDrop"));
	Place(CP->AddChild(Drop), BL, BT + BH, BW, 12.f * BH, 60, true);
	Drop->SetVisibility(ESlateVisibility::Collapsed);
	ConstellationDropdown = Drop;
	// ★ Знаки зодиака Id 1..12 (Овен..Рыбы) — БЕЗ «—» (пустышка Id0 = только дисплей до выбора). pick(Id)→Const=Id.
	for (int32 Id = 1; Id <= 12; ++Id)
	{
		const float IY = (float)(Id - 1) * BH;
		MkImg(Drop, 0.f, IY, BW, BH, N02, UVItem, FLinearColor::White, 0, false);                    // z0 фон item
		MkHit(Drop, 0.f, IY, BW, BH, 1, false, Id, [this](int32 PickId) { OnConstellationPick(PickId); });  // z1 кнопка+hover ПОД текстом
		MkTxt(Drop, 0.f, IY, BW, BH, FAsdaCharRecord::ConstellationName((uint8)Id), 2, false);        // z2 текст поверх (SelfHitTestInvisible)
	}
	MkHit(CP, BL, BT, BW, BH, 41, true, 0, [this](int32)   // z41: hover-подсветка ПОД стрелкой(42)/дисплеем(43), над заливкой(40)
	{
		if (ConstellationDropdown)
		{
			const bool bWasHidden = ConstellationDropdown->GetVisibility() == ESlateVisibility::Collapsed;
			ConstellationDropdown->SetVisibility(bWasHidden ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	});
}

void AAsdaLoginGameMode::UpdateGenderHighlight()
{
	if (!CharCreateWidget || !CharCreateWidget->WidgetTree) return;
	// Иконки-планы пола = UImage с именем-префиксом CARAMAKE_SEXBUT (converter: {idc}_{idx}_{plane}); их 2 —
	//   левая (X меньше) = ♂ ES0, правая = ♀ ES1. Сортируем по X, красим выбранную жёлтым, другую серой.
	TArray<UWidget*> All; CharCreateWidget->WidgetTree->GetAllWidgets(All);
	TArray<TPair<float, UImage*>> Imgs;
	for (UWidget* Wd : All)
	{
		UImage* Img = Cast<UImage>(Wd);
		if (Img && Img->GetName().StartsWith(TEXT("CARAMAKE_SEXBUT")))
		{
			float X = 0.f;
			if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Img)) { X = S->GetOffsets().Left; }
			Imgs.Add(TPair<float, UImage*>(X, Img));
		}
	}
	Imgs.Sort([](const TPair<float, UImage*>& A, const TPair<float, UImage*>& B) { return A.Key < B.Key; });
	const FLinearColor Sel(1.f, 1.f, 0.f, 1.f);      // FFFFFF00 — выбранный (жёлтый)
	const FLinearColor Dim(0.9f, 0.9f, 0.9f, 1.f);   // FFE6E6E6 — невыбранный (серый)
	for (int32 i = 0; i < Imgs.Num(); ++i)
	{
		const bool bMaleImg = (i == 0);   // левая иконка = ♂
		const bool bSelected = bMaleImg ? (CreateGender == 1) : (CreateGender == 2);
		Imgs[i].Value->SetBrushTintColor(FSlateColor(bSelected ? Sel : Dim));
	}
}

void AAsdaLoginGameMode::OnConstellationPick(int32 Index)
{
	CreateConstellation = (uint8)Index;   // Index = Id знака 1..12 (0 = не выбрано)
	UE_LOG(LogAsdaLogin, Warning, TEXT("[COMBO] выбран знак Id=%d → CreateConstellation=%d"), Index, CreateConstellation);
	if (ConstellationDisplay) { ConstellationDisplay->SetText(FText::FromString(FAsdaCharRecord::ConstellationName((uint8)Index))); }
	if (ConstellationDropdown) { ConstellationDropdown->SetVisibility(ESlateVisibility::Collapsed); }
}

// 3D-превью создаваемого перса — ★ ОТДЕЛЬНЫЙ объект (эталон uiBuildCharacterPreviewModel@0xF0FD30), НЕ слот
//   и НЕ кость шкатулки. Стоит ПЕРЕД шкатулкой; слот-персонажи char-select СКРЫТЫ (казна = задник). Слоты
//   char-select НЕ уничтожаются/не переиспользуются (баг B2). Позиция — трансформ шкатулки + оффсет (Editor-
//   Friendly, ENV ASDA_CREATE_OFFSET), крупным планом; вращается стрелками LEFTBUT/RIGHTBUT (B3).
void AAsdaLoginGameMode::SpawnCreatePreview()
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	UE_LOG(LogAsdaLogin, Warning, TEXT("[CHARCREATE] ===== SpawnCreatePreview v2 (отдельный объект+dim) ЗАПУЩЕН ====="));

	// Снести ПРОШЛЫЙ create-превью (пересбор при смене пола/части). Слот-персонажей НЕ трогаем — только прячем.
	if (CreatePreviewActor) { CreatePreviewActor->Destroy(); CreatePreviewActor = nullptr; }
	for (AActor* A : CharacterActors) { if (A) { A->SetActorHiddenInGame(true); } }
	for (AActor* G : GlyphActors)     { if (G) { G->SetActorHiddenInGame(true); } }

	// ── Якорь сцены = мировой трансформ фронт-кости шкатулки (там же стоят слот-персонажи). ──
	//    ★ 12.09.2026: «координаты ×100» здесь больше НЕ верно — сцена переведена в ×1 (GLoginScale).
	USkeletalMeshComponent* SceneCask = CasketActor ? CasketActor->GetSkeletalMeshComponent() : nullptr;
	const FName SceneBone = CarouselPedestalBone(CreateSlot, CreateSlot);
	FTransform SceneBase = AsdaLoginPedestalBase(CasketActor, SceneCask, SceneBone);   // ★ R5: общая база слота шкатулки
	// Камера (для выноса персонажа ВПЕРЁД к камере и ориентации dim к камере).
	FVector CamLoc = FVector::ZeroVector; FRotator CamRot = FRotator::ZeroRotator; bool bCam = false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager) { CamLoc = PC->PlayerCameraManager->GetCameraLocation(); CamRot = PC->PlayerCameraManager->GetCameraRotation(); bCam = true; }
	}
	UE_LOG(LogAsdaLogin, Warning, TEXT("[CHARCREATE] СЦЕНА: кость%s=%s | камера(есть=%d)=%s rot=%s | задник=%s шкатулка=%s"),
		*SceneBone.ToString(), *SceneBase.GetLocation().ToString(), bCam, *CamLoc.ToString(), *CamRot.ToString(),
		BackdropActor ? *BackdropActor->GetActorLocation().ToString() : TEXT("нет"),
		CasketActor ? *CasketActor->GetActorLocation().ToString() : TEXT("нет"));

	// ★ B1 ЗАТЕМНЕНИЕ — 3D-квад blackscreen в конце этой функции (расстановка — ключ asda.Login.CreateDimPlace, 08.11.2026)
	//   плюс виньетка-окно WBP_GRAD_BG_FULL из ShowCharCreateUI. UMG-оверлея CreateDimBorder больше нет (только снос в Teardown).

	if (CreatePartCodes.Num() != 5) { BuildCreateCategories(CreateGender); }

	const bool bMale = (CreateGender != 2);
	const TCHAR* MeshPath = bMale ? GCharPreviewMesh     : GCharPreviewMeshF;
	// ★ Набор анимаций CHAR-CREATE = cre_creman/crewoman (эталон CCreature_LoadCreManAnims @0xF10050),
	//   импортирован как previewanim_05..11 (трек эталона N → ассет 05+N; 0=idle создания). Текущий трек =
	//   CreateAnimState (эталон char+118382): idle 0 (луп) либо one-shot showcase 2..6 после клика одежды.
	const FString AnimPath = FString::Printf(TEXT("%sanim_%02d"),
		bMale ? GCharPreviewAnimBase : GCharPreviewAnimBaseF, 5 + CreateAnimState);

	// Позиция = кость сцены + вынос ВПЕРЁД к камере (правильный размер, как слот-персонажи) + сдвиг вбок/вниз.
	//   ENV ASDA_CREATE_FWD (доля к камере, деф 0.25) + ASDA_CREATE_SHIFT="right,up" (в осях камеры, экранные) —
	//   right>0 двигает вправо на экране, up>0 вверх. Так перс ставится ровно между стрелками (калибруется).
	const FTransform Base = SceneBase;
	FVector SpawnLoc = Base.GetLocation();
	float ScaleComp = 1.f;   // компенсация масштаба при выносе перса к камере (размер на экране тот же)
	if (bCam)
	{
		// ★ Камеро-относительное позиционирование: перс ВСЕГДА в центре кадра, не улетает.
		//   SpawnLoc = камера + Вперёд×Дист + мелкий экранный сдвиг (Право/Вверх в осях камеры).
		//   Дист задаёт видимую близость; размер держим постоянным (ScaleComp = Дист / дист-до-кости).
		//   ENV: ASDA_CREATE_DIST (деф 450 при GLoginScale=1; ★ 12.09 было 45000 в ×100-сцене),
		//   ASDA_CREATE_SHIFT="right,up" (деф правее/чуть-вверх; ★ 12.09 −60/−40, было −6000/−4000).
		const FRotationMatrix CamM(CamRot);
		const FVector CamFwd   = CamM.GetScaledAxis(EAxis::X);
		const FVector CamRight = CamM.GetScaledAxis(EAxis::Y);
		const FVector CamUp    = CamM.GetScaledAxis(EAxis::Z);
		float Dist = 450.f * GLoginScale;   // ★ 12.09: было 45000.f (450 game × 100)
		const FString DEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_CREATE_DIST"));
		if (!DEnv.IsEmpty()) { Dist = FCString::Atof(*DEnv); }
		float Right = -60.f * GLoginScale, Up = -40.f * GLoginScale;   // мелкий сдвиг: левее центра (между стрелок) + ниже (калибруется); ★ 12.09: было -6000/-4000 (×100)
		const FString ShEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_CREATE_SHIFT"));
		if (!ShEnv.IsEmpty()) { TArray<FString> P; ShEnv.ParseIntoArray(P, TEXT(",")); if (P.Num()==2){ Right=FCString::Atof(*P[0]); Up=FCString::Atof(*P[1]); } }
		const float BaseDist = (Base.GetLocation() - CamLoc).Size();
		SpawnLoc = CamLoc + CamFwd * Dist + CamRight * Right + CamUp * Up;
		ScaleComp = (BaseDist > 1.f) ? (Dist / BaseDist) : 1.f;   // видимый размер = как на кости
	}
	const FVector CreateOffset = SpawnLoc - Base.GetLocation();   // для лога
	const FRotator SpawnRot = Base.Rotator();   // ориентация кости (поворот игрок делает стрелками)

	AActor* Char = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnLoc, SpawnRot, FActorSpawnParameters());
	if (!Char) { UE_LOG(LogAsdaLogin, Warning, TEXT("SpawnCreatePreview: актёр не создан")); return; }
	CreatePreviewActor = Char;
	UE_LOG(LogAsdaLogin, Warning, TEXT("[CHARCREATE] ПЕРСОНАЖ спавн loc=%s (кость=%s toCam-offset=%s) слотов-скрыто=%d"),
		*SpawnLoc.ToString(), *SceneBone.ToString(), *CreateOffset.ToString(), CharacterActors.Num());

	USceneComponent* Root = NewObject<USceneComponent>(Char, TEXT("CreateRoot"));
	Root->RegisterComponent();
	Char->SetRootComponent(Root);
	Char->SetActorLocationAndRotation(SpawnLoc, SpawnRot);
	// ★ ЗЕРКАЛО (как в мире, AAsdaPlayerCharacter): игра ПРАВОРУКАЯ, конвертер моделей дал лишний
	//   флип. Зеркалим по X (право↔лево, лицом к камере); негейт Y разворачивал бы спиной.
	Root->SetWorldScale3D(FVector(CharacterScale * ScaleComp) * FVector(-1.f, 1.f, 1.f));
	CreateYaw = 0.f; CreateBaseRot = SpawnRot;   // сброс вращения; база — ориентация пьедестала (yaw добавляется поверх)

	USkeletalMesh* SkMesh = LoadMesh(MeshPath);
	if (!SkMesh) { UE_LOG(LogAsdaLogin, Warning, TEXT("SpawnCreatePreview: меш не загружен %s"), MeshPath); return; }
	USkeletalMeshComponent* SkC = NewObject<USkeletalMeshComponent>(Char, TEXT("CreateSkel"));
	SkC->SetupAttachment(Root);
	SkC->SetSkeletalMesh(SkMesh);
	SkC->SetRelativeTransform(FTransform::Identity);
	SkC->SetCastShadow(true);
	SkC->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	SkC->RegisterComponent();
	// ★ Эталонный тик char-create играет трек CreateAnimState: idle(0) — луп; showcase(2..6) — ОДИН раз,
	//   по концу клипа возврат к idle (effects_particles.c тик: IsFinished → animState=0, флаг 473507 сброшен).
	//   Пересборка превью (клик одежды) НЕ перезапускает клип — продолжаем с текущей позиции (эталон
	//   не пересоздаёт модель, анимация непрерывна).
	if (UAnimSequence* Clip = LoadClip(*AnimPath))
	{
		const bool bLoop = (CreateAnimState == 0);
		SkC->PlayAnimation(Clip, bLoop);
		if (!bLoop)
		{
			const float Len = Clip->GetPlayLength();
			const float Elapsed = FMath::Max(0.f, World->GetTimeSeconds() - CreateShowcaseStart);
			const float Pos = FMath::Min(Elapsed, FMath::Max(0.f, Len - KINDA_SMALL_NUMBER));
			SkC->SetPosition(Pos, /*bFireNotifies=*/false);
			// Конец клипа → idle (переустановка таймера при пересборке безопасна: остаток тот же).
			World->GetTimerManager().SetTimer(CreateShowcaseTimer, this,
				&AAsdaLoginGameMode::OnShowcaseAnimEnd, FMath::Max(0.05f, Len - Pos), false);
		}
	}
	// Надеть выбранные части + лицо (create-вариант единой системы сборки).
	ApplyCreateParts(Char, Root, SkC, CreateGender);

	// ★ B1 ЗАТЕМНЕНИЕ 1:1 (эталон blackscreen: SRCALPHA/INVSRCALPHA, alpha 0.85, ZWRITE off): translucent-чёрный
	//   квад ПОЗАДИ персонажа (между ним и шкатулкой) → затемняет ВСЮ шкатулку+фон; перс (впереди камеры) поверх
	//   рисуется ярким. Материал M_AsdaBlackScreen = точный бленд из RE. Лицом к камере (нормаль квада +Z → MakeFromZ).
	if (bCam)
	{
		if (CreateDimActor) { CreateDimActor->Destroy(); CreateDimActor = nullptr; }
		UStaticMesh* DimMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/AsdaAssets/Models/Char/blackscreen/StaticMeshes/blackscreen.blackscreen"));
		if (DimMesh)
		{
			// Blackscreen ЦЕНТРИРУЕМ по оси камеры (не по улетевшему SpawnLoc) → всегда в кадре, сразу ЗА персом.
			//   Дист = дист(камера→перс)+DimBack → перс ближе камеры (не затемняется depth-тестом), фон дальше → тускнеет.
			const FRotationMatrix CamMd(CamRot);
			const FVector CamFwdD = CamMd.GetScaledAxis(EAxis::X);
			// ★ 12.09: DimBack было 3000.f (30 game × 100). DimScale НЕ трогаем: меш blackscreen остался
			//   запечён в старых единицах (~200000×150000 юн) и в ×1-сцене тем более закрывает весь кадр.
			float DimBack = 30.f * GLoginScale, DimScale = 1.f;
			const FString DBEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_DIM_BACK"));  if (!DBEnv.IsEmpty()) DimBack = FCString::Atof(*DBEnv);
			const FString DSEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_DIM_SCALE"));  if (!DSEnv.IsEmpty()) DimScale = FCString::Atof(*DSEnv);
			const float CharDist = (SpawnLoc - CamLoc).Size();
			const FVector DimPos = CamLoc + CamFwdD * (CharDist + DimBack);   // центр кадра, сразу за персонажем
			// ★ Квад blackscreen ПЛОСКИЙ по локальной оси Y (bounds.extent.Y≈0) → нормаль=±Y. Ориентируем ИМЕННО Y
			//   на камеру (MakeFromZ давал квад РЕБРОМ → невидимая полоса). Центр меша смещён (bounds.Origin) —
			//   спавним так, чтобы центр меша попал ровно в DimPos.
			const FRotator DimRot = FRotationMatrix::MakeFromY(CamLoc - DimPos).Rotator();
			const FVector MeshOrigin = DimMesh->GetBounds().Origin;
			const FVector CenterOff = FRotationMatrix(DimRot).TransformVector(MeshOrigin * DimScale);
			const FVector DimSpawnAt = DimPos - CenterOff;   // центр меша ляжет в DimPos
			AActor* Dim = World->SpawnActor<AActor>(AActor::StaticClass(), DimSpawnAt, DimRot, FActorSpawnParameters());
			if (Dim)
			{
				UStaticMeshComponent* SMC = NewObject<UStaticMeshComponent>(Dim, TEXT("DimQuad"));
				SMC->SetMobility(EComponentMobility::Movable);
				Dim->SetRootComponent(SMC);
				SMC->RegisterComponent();
				SMC->SetStaticMesh(DimMesh);
				SMC->SetWorldScale3D(FVector(DimScale));
				// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (разбор у CVarAsdaCreateDimPlace): у актора без корня позиция из SpawnActor
				//   пропадает — ставим её явно, уже ПОСЛЕ установки корня (как у персонажа выше).
				const int32 DimPlace = CVarAsdaCreateDimPlace.GetValueOnGameThread();
				if (DimPlace == 1)
				{
					Dim->SetActorLocationAndRotation(DimSpawnAt, DimRot);   // центр меша — в DimPos, нормаль квада — на камеру
				}
				else if (DimPlace == 2)
				{
					Dim->SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);   // эталон: модель в 0,0,0 мира
					SMC->SetWorldScale3D(FVector(DimScale * GLoginScale / 100.f));                  // запечённые ×100 → единицы сцены
				}
				SMC->SetCastShadow(false);
				SMC->SetBoundsScale(100.f);   // огромный квад — не отсекать по bounds
				// Материал с ТОЧНЫМ блендом эталона (translucent unlit чёрный, opacity 0.85). На ВСЕ слоты.
				UMaterialInterface* DimMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Effects/M_AsdaBlackScreen.M_AsdaBlackScreen"));
				if (DimMat) { for (int32 mi = 0; mi < FMath::Max(1, SMC->GetNumMaterials()); ++mi) { SMC->SetMaterial(mi, DimMat); } }
				UE_LOG(LogAsdaLogin, Warning, TEXT("[CHARCREATE] DIM материал загружен=%d слотов=%d"), DimMat ? 1 : 0, SMC->GetNumMaterials());
				CreateDimActor = Dim;
			}
			UE_LOG(LogAsdaLogin, Warning, TEXT("[CHARCREATE] DIM blackscreen loc=%s (за персом %s) scale=%.0f расстановка=%d актор=%s"), *DimPos.ToString(), *SpawnLoc.ToString(), DimScale,
				CVarAsdaCreateDimPlace.GetValueOnGameThread(), CreateDimActor ? *CreateDimActor->GetActorLocation().ToString() : TEXT("нет"));
		}
	}
}

// Снести create-сцену и вернуть char-select (выход из создания: Отмена/успех/back).
void AAsdaLoginGameMode::TeardownCreateScene()
{
	CreateRotDir = 0;
	if (CreatePreviewActor) { CreatePreviewActor->Destroy(); CreatePreviewActor = nullptr; }
	if (CreateDimActor)     { CreateDimActor->Destroy();     CreateDimActor = nullptr; }
	if (CreateDimBorder && GEngine && GEngine->GameViewport) { GEngine->GameViewport->RemoveViewportWidgetContent(CreateDimBorder->TakeWidget()); }
	// Спрятать топ-окна сцены создания (градиент + кнопки вращения) — они самостоятельные окна.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(TEXT("GRAD_BG_FULL")), false);
		WM->SetWindowVisible(FName(TEXT("CARAMAKE_LEFTBUT")), false);
		WM->SetWindowVisible(FName(TEXT("CARAMAKE_RIGHTBUT")), false);
	}
	// Показать обратно слот-персонажей char-select (глифы вернёт презентация слота).
	for (AActor* A : CharacterActors) { if (A) { A->SetActorHiddenInGame(false); } }
}

// Циклировать внешность: 0=причёска(0..4), 1=цвет(0..2), 2=лицо(муж0..10/жен0..9). Delta ±1 с wrap + пересбор превью.
void AAsdaLoginGameMode::CycleCreateAppearance(int32 Which, int32 Delta)
{
	auto Wrap = [](int32 V, int32 D, int32 N) { return (N > 0) ? ((V + D % N + N) % N) : 0; };
	switch (Which)
	{
		case 0: CreateHair  = (uint8)Wrap(CreateHair, Delta, 5); break;
		case 1: CreateColor = (uint8)Wrap(CreateColor, Delta, 3); break;
		case 2: CreateFace  = (uint8)Wrap(CreateFace, Delta, (CreateGender == 2) ? 10 : 11); break;
		default: break;
	}
	UE_LOG(LogAsdaLogin, Verbose, TEXT("CharCreate внешность: hair=%u color=%u face=%u"), CreateHair, CreateColor, CreateFace);
	SpawnCreatePreview();   // пересобрать превью (причёска/лицо → меш/текстура в ApplyCreateParts)
}

// Заполнить 5 векторов вариантов ItemCode по fileSlot из CharEquipTable (для пола) + дефолт-выбор.
void AAsdaLoginGameMode::BuildCreateCategories(uint8 Gender)
{
	CategoryVars.SetNum(5);
	for (TArray<int32>& V : CategoryVars) { V.Reset(); }
	CategorySelIdx.Init(0, 5);
	CreatePartCodes.Init(0, 5);
	CategoryScroll.Init(0, 5);   // пер-строковый индекс скролла (по строке сетки 0..4)
	if (UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr)
		if (UDataTable* DT = Items->GetDataTable(FName(TEXT("charequiptable"))))
		{
			TArray<FAsdaCharequiptableRow*> Rows;
			DT->GetAllRows<FAsdaCharequiptableRow>(TEXT("BuildCreateCategories"), Rows);
			for (const FAsdaCharequiptableRow* R : Rows)
				if (R && R->Gender == (int32)Gender && R->SlotType >= 0 && R->SlotType < 5)
					CategoryVars[R->SlotType].Add(R->ItemCode);
		}
	for (int32 fs = 0; fs < 5; ++fs)
	{
		CreatePartCodes[fs] = CategoryVars[fs].Num() ? CategoryVars[fs][0] : 0;
	}
}

// Провайдер модели данных сетки: ячейка (row*5+col) → itemCode по карусели категорий.
//   Строка = категория (TabOrder→fileSlot), столбец = позиция в окне вариантов (scroll+col по модулю длины).
int32 AAsdaLoginGameMode::CreateCellCode(int32 Cell) const
{
	static const int32 TabOrder[5] = { 1, 4, 2, 3, 0 };   // строка → fileSlot (Chest,Gloves,Pants,Shoes,Helmet)
	if (Cell < 0 || Cell >= 25) { return 0; }
	const int32 Row = Cell / 5, Col = Cell % 5;
	const int32 Fs = TabOrder[Row];
	const int32 Len = CategoryVars.IsValidIndex(Fs) ? CategoryVars[Fs].Num() : 0;
	if (Len <= 0) { return 0; }
	const int32 RowScroll = CategoryScroll.IsValidIndex(Row) ? CategoryScroll[Row] : 0;   // индекс скролла СВОЕЙ строки
	return CategoryVars[Fs][(RowScroll + Col) % Len];
}

// Обновить иконки сетки (текущие варианты/пол) — делегируем системе UAsdaIconCellGrid.
void AAsdaLoginGameMode::PopulateIconGrid()
{
	if (IconGrid) { IconGrid->SetMale(CreateGender != 2); IconGrid->Refresh(); }
}

// Клик по ячейке сетки → выбрать вариант части для категории её строки.
void AAsdaLoginGameMode::OnIconCellClicked(int32 Cell)
{
	static const int32 TabOrder[5] = { 1, 4, 2, 3, 0 };
	if (Cell < 0 || Cell >= 25) { return; }
	const int32 Row = Cell / 5, Col = Cell % 5;
	const int32 Fs = TabOrder[Row];
	if (!CategoryVars.IsValidIndex(Fs) || CategoryVars[Fs].Num() == 0) { return; }
	const int32 RowScroll = CategoryScroll.IsValidIndex(Row) ? CategoryScroll[Row] : 0;
	const int32 VarIdx = (RowScroll + Col) % CategoryVars[Fs].Num();
	CategorySelIdx[Fs] = VarIdx;
	CreatePartCodes[Fs] = CategoryVars[Fs][VarIdx];
	// ★ Showcase-анимация категории (эталон uiCharPartSelectCycle: if(!флаг473507){флаг=1; трек=animState}).
	//   Пока играет — клик НЕ перезапускает (одежда меняется, клип продолжается). Треки по fileSlot:
	//   0 Helmet→2, 1 Chest→4, 2 Pants→5, 3 Shoes→6, 4 Gloves→3.
	static const int32 AnimStateBySlot[5] = { 2, 4, 5, 6, 3 };
	if (!bCreateShowcase)
	{
		bCreateShowcase = true;
		CreateAnimState = AnimStateBySlot[Fs];
		CreateShowcaseStart = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	}
	SpawnCreatePreview();   // пересобрать превью с новой частью (клип продолжает позицию)
	ArrangeCreateSelMarkers();   // сияющая рамка на выбранный вариант (эталон uiArrangeSkillSlots5)
	ApplyHelper(FMath::RandRange(44, 48));   // §3.3: клик по одежде → случайная похвала (эталон rand()%5+44)
}

// One-shot showcase доиграл: сброс на idle (эталонный тик: IsFinished → флаг 473507=0, animState=0).
void AAsdaLoginGameMode::OnShowcaseAnimEnd()
{
	bCreateShowcase = false;
	CreateAnimState = 0;
	if (!CreatePreviewActor) { return; }
	// Не пересобираем превью — просто переключаем клип на лидер-скелете (части следуют LeaderPose).
	USkeletalMeshComponent* SkC = nullptr;
	for (UActorComponent* C : CreatePreviewActor->GetComponents())
	{
		if (USkeletalMeshComponent* S = Cast<USkeletalMeshComponent>(C))
		{
			if (S->GetFName() == FName(TEXT("CreateSkel"))) { SkC = S; break; }
			if (!SkC) { SkC = S; }
		}
	}
	if (!SkC) { return; }
	const FString IdlePath = FString::Printf(TEXT("%sanim_%02d"),
		(CreateGender != 2) ? GCharPreviewAnimBase : GCharPreviewAnimBaseF, 5);
	if (UAnimSequence* Idle = LoadClip(*IdlePath)) { SkC->PlayAnimation(Idle, /*bLooping=*/true); }
}

// BACK/NEXT — сдвиг вариантов. Эталон storageScrollTabFields@0xF11950: КАЖДАЯ строка независимо ±1,
//   врап по СВОЕЙ длине (up: ++idx→0 при len; down: --idx→len-1 при <0). НЕ общий scroll+модуль.
void AAsdaLoginGameMode::ScrollIconGrid(int32 Delta)
{
	static const int32 TabOrder[5] = { 1, 4, 2, 3, 0 };
	if (CategoryScroll.Num() < 5) { CategoryScroll.Init(0, 5); }
	const bool bUp = (Delta >= 0);
	for (int32 Row = 0; Row < 5; ++Row)
	{
		const int32 Fs = TabOrder[Row];
		const int32 Len = CategoryVars.IsValidIndex(Fs) ? CategoryVars[Fs].Num() : 0;
		if (Len <= 0) { CategoryScroll[Row] = 0; continue; }
		int32 Idx = CategoryScroll[Row] + (bUp ? 1 : -1);
		if (bUp) { if (Idx >= Len) { Idx = 0; } }
		else     { if (Idx < 0)    { Idx = Len - 1; } }
		CategoryScroll[Row] = Idx;
	}
	PopulateIconGrid();
	ArrangeCreateSelMarkers();   // выбранный вариант мог уйти из/прийти в видимое окно
}

// ── SEL-маркеры «сияющая рамка» выбранного варианта — 1:1 эталон uiArrangeSkillSlots5 @0xF132B0 ──
//   Для каждой категории (partSlot=fileSlot 0..4) ищем, в какой из 5 видимых ячеек её строки стоит
//   выбранный вариант, и ставим рамку CARAMAKE_ICONBOX_SEL<row> поверх этой ячейки; вне окна — прячем.
//   Рамка = UAsdaAnimatedPlane (конвертер JUI уже завёл 4-кадровое сияние Log_Ani_IconSelect, 300мс);
//   кадры крутит сам виджет — здесь ТОЛЬКО позиция+видимость. Структура WBP (дамп dump_caramake_tree.py):
//   SEL-рамки и обёртки ячеек CARAMAKE_BLANK{N}_grp — дети ОДНОЙ канвы CARAMAKE_BOTWIN_grp (одна система
//   координат: BLANK00=(11,13), шаг 67px); сами ICONBOX-виджеты лежат ВНУТРИ обёрток с pos=(0,0), поэтому
//   позицию ячейки даёт слот ОБЁРТКИ, не слот ICONBOX.
void AAsdaLoginGameMode::ArrangeCreateSelMarkers()
{
	if (!CharCreateWidget) { return; }
	static const int32 TabRemap[5] = { 4, 0, 2, 3, 1 };   // partSlot → строка сетки (эталон tabRemap)
	for (int32 Fs = 0; Fs < 5; ++Fs)
	{
		const int32 Row = TabRemap[Fs];
		UWidget* Sel = UAsdaIconCellGrid::FindCell(CharCreateWidget,
			FString::Printf(TEXT("CARAMAKE_ICONBOX_SEL%02d"), Row));
		if (!Sel) { UE_LOG(LogAsdaLogin, Warning, TEXT("[SEL] рамка SEL%02d не найдена"), Row); continue; }
		int32 Target = CategorySelIdx.IsValidIndex(Fs) ? CategorySelIdx[Fs] : -1;
		if (Target == -1 && Fs == 0) { continue; }   // эталон: -1 у slot0 → строку не трогаем
		if (Target == -1) { Target = 0; }
		const int32 Len = CategoryVars.IsValidIndex(Fs) ? CategoryVars[Fs].Num() : 0;
		if (Len <= 0) { Sel->SetVisibility(ESlateVisibility::Collapsed); continue; }
		// Видимое окно строки = 5 ячеек от scroll (wrap по длине вектора), ПЕРВОЕ совпадение (эталон break).
		int32 Scroll = CategoryScroll.IsValidIndex(Row) ? CategoryScroll[Row] : 0;
		int32 FoundCol = -1;
		for (int32 i = 0; i < 5 && FoundCol < 0; ++i)
		{
			if (Scroll >= Len) { Scroll = 0; }
			if (Target == Scroll) { FoundCol = i; }
			++Scroll;
		}
		if (FoundCol < 0) { Sel->SetVisibility(ESlateVisibility::Collapsed); continue; }
		// Позиция рамки = позиция ОБЁРТКИ ячейки (эталон CControl_SetField74_75 по Vec2 бокса).
		UWidget* CellGrp = UAsdaIconCellGrid::FindCell(CharCreateWidget,
			FString::Printf(TEXT("CARAMAKE_BLANK%02d_grp"), Row * 5 + FoundCol));
		UCanvasPanelSlot* CellSlot = CellGrp ? AsdaUi::SlotOf(CellGrp) : nullptr;
		UCanvasPanelSlot* SelSlot  = AsdaUi::SlotOf(Sel);
		if (CellSlot && SelSlot) { SelSlot->SetPosition(CellSlot->GetPosition()); }
		else { UE_LOG(LogAsdaLogin, Warning, TEXT("[SEL] обёртка BLANK%02d_grp не найдена (row=%d col=%d)"), Row * 5 + FoundCol, Row, FoundCol); }
		Sel->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

// Наложить выбранные части (CreatePartCodes) на превью-скелет (create-вариант ApplyEquipToSlot).
void AAsdaLoginGameMode::ApplyCreateParts(AActor* SlotChar, USceneComponent* Root, USkeletalMeshComponent* BaseSkel, uint8 Sex)
{
	if (!SlotChar || !Root || !BaseSkel) return;
	UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Items) return;

	// Источник частей — выбранные при создании коды CreatePartCodes[fileSlot]. Маппим в канонические поля
	// системы (fileSlot: 1=тело 2=штаны 3=обувь 4=перчатки 0=шлем) и отдаём единой системе сборки.
	auto Code = [this](int32 FileSlot) -> int32
	{
		return CreatePartCodes.IsValidIndex(FileSlot) ? CreatePartCodes[FileSlot] : 0;
	};
	FAsdaWornParts Parts;
	Parts.Chest  = Code(1);
	Parts.Pants  = Code(2);
	Parts.Shoes  = Code(3);
	Parts.Gloves = Code(4);
	Parts.Helmet = Code(0);
	Parts.FaceIndex = (int32)CreateFace;   // выбранное лицо (A1) — cycle 0..10
	Parts.HairStyle = (int32)CreateHair;   // причёска (стрелки CARAMAKE_ARROWBUT00/01) → меш волос
	Parts.HairColor = (int32)CreateColor;  // цвет волос (стрелки 02/03) → свап цвет-текстуры
	UAsdaCharAssembly::BuildWornParts(SlotChar, Root, BaseSkel, (Sex != 2), Parts, Items);
}
