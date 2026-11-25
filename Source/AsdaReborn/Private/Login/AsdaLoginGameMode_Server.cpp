// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginGameMode_Server.cpp — ★ 23.10.2026 часть AAsdaLoginGameMode, вынесенная из AsdaLoginGameMode.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaLoginGameMode.plan.json).
//   Выбор сервера и канала, Tick сцены, карусель и слоты персонажей, экипировка слота, стартовая заставка Start_logo.
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
#include "Login/AsdaButtonRelay.h"
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
#if WITH_EDITOR
#include "TextureCompiler.h"   // ★ 08.11.2026 (баг 12): FTextureCompilingManager — дождаться сборки текстуры заставки в редакторе
#endif   // ★ масштаб вьюпорта для раскладки стартовой заставки
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
#include "UI/AsdaEnterConfirmSubsystem.h"   // ★ 25.10.2026: роспись окна выбора сервера/канала для Enter
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

// Idle слота — клип 0 набора char-select (эталонный дефолтный трек, `worldModelsUpdateAndRender`
//   крутит трек = индекс слота, дефолт 0). Путь собираем из базы, чтобы литерал остался один.
static FString CharPreviewIdleAnim(const TCHAR* Base) { return FString::Printf(TEXT("%sanim_00"), Base); }
// ★ Дедуп №2, В2-C (F11, 11.10.2026): фейд глифов пустых слотов вместе с превью. ★ [ОТСТУПЛЕНИЕ] умолчание 1 — решение
//   владельца 11.10.2026 («плавное»): глиф проявляется вместе с превью. 0 — как эталон: глиф без фейда
//   (`channelSelectRender3D@0xF07BE0` кладёт эффект глифа в очередь без множителя альфы); 1 — проявление через `FxAlphaScale`.
//   Разбор — в `CollectPreviewFadeComps`.
static ASDA_DEV_CVAR(int32, CVarAsdaLoginGlyphFade,
	TEXT("asda.Login.GlyphFade"), 1,
	TEXT("Фейд глифов пустых слотов выбора персонажа вместе с превью: 0 = как в оригинале (глиф без фейда), 1 = проявление ~1 с через множитель альфы эффекта (FxAlphaScale)."),
	ECVF_Default);

void AAsdaLoginGameMode::ShowServerSelect()
{
	// ★ ШТАТНОЕ игровое JUI-окно SERVER_SELECT_WIN (одно на оба этапа: серверы↔каналы; server_select_re.md).
	//   5 слотов серверов SERVER_BTN00-04, 15 каналов CHANNEL_BTN00-14, подтвердить SERVER/CHANNEL_SELECT_BTN1.
	//   Подписи — из ассета DT_GameText по loc-ID (never-invent). Замена кастомного WBP_ServerSelect + Slate.
	if (!GetWorld()) return;
	CurrentScreen = EAsdaScreen::ServerSelect;
	if (!ServerSelectWidget)
	{
		UClass* WbpClass = LoadClass<UUserWidget>(nullptr,
			TEXT("/Game/AsdaAssets/UI/Login/WBP_SERVER_SELECT_WIN.WBP_SERVER_SELECT_WIN_C"));
		if (!WbpClass) { UE_LOG(LogAsdaLogin, Warning, TEXT("WBP_SERVER_SELECT_WIN не загружен")); return; }
		ServerSelectWidget = CreateWidget<UUserWidget>(GetWorld(), WbpClass);
		if (!ServerSelectWidget) return;

		auto SetTxt = [&](const FString& N, const FString& S)
		{
			if (UTextBlock* T = Cast<UTextBlock>(ServerSelectWidget->GetWidgetFromName(FName(*N))))
				T->SetText(FText::FromString(S));
		};
		auto BindBtn = [&](const FString& N, int32 Idx, TFunction<void(int32)> Cb)
		{
			if (UButton* B = Cast<UButton>(ServerSelectWidget->GetWidgetFromName(FName(*N))))
			{
				AsdaUi::BindButton(ServerSelectWidget, B, Idx, MoveTemp(Cb), ButtonRelays);   // ★ U20: реле клика — общая точка
			}
		};
		// ★ Состояния кнопки = ОТДЕЛЬНЫЕ контролы JUI: <база>_O (Over) и <база>_C (Press). Сами по себе они
		//   скрыты (ShowServerOrChannelView прячет их как «не-Nom»), показывать их должен hover/нажатие —
		//   без этой привязки у каналов не было ни подсветки, ни отклика на клик.
		//   Индекс в имени бывает 2- и 3-значным (CHANNEL_BTN012 против CHANNEL_BTN12_O), поэтому состояния
		//   ищем по обоим написаниям.
		auto BindStateVisuals = [&](const FString& BasePrefix, int32 Idx)
		{
			// ★ Имя виджета НЕЛЬЗЯ брать точным: генератор UMG (asda_jui_generic_umg.py) для НЕинтерактивных
			//   типов JUI даёт плану имя "<idc>_<индекс плана>" (base = "%s_%d"), т.е. в дереве лежит
			//   CHANNEL_BTN00_0, а не CHANNEL_BTN00. Плюс сам индекс канала бывает 2- и 3-значным
			//   (CHANNEL_BTN01 против CHANNEL_BTN012). Поэтому ищем по ПРЕФИКСУ и проверяем, что дальше
			//   идёт либо конец имени, либо '_' — иначе "CHANNEL_BTN01" поймал бы "CHANNEL_BTN012".
			UWidget* Btn = nullptr;
			{
				const FString P2 = FString::Printf(TEXT("%s%02d"), *BasePrefix, Idx);
				const FString P3 = FString::Printf(TEXT("%s%03d"), *BasePrefix, Idx);
				TArray<UWidget*> AllW;
				ServerSelectWidget->WidgetTree->GetAllWidgets(AllW);
				for (UWidget* W : AllW)
				{
					if (!W) continue;
					const FString N = W->GetName();
					for (const FString& P : { P2, P3 })
					{
						if (!N.StartsWith(P)) continue;
						const FString Rest = N.Mid(P.Len());
						// сам план базы: "" или "_<цифра>"; состояния (_O/_C/_D) сюда не берём
						const bool bPlane = Rest.IsEmpty() ||
							(Rest.StartsWith(TEXT("_")) && Rest.Len() > 1 && FChar::IsDigit(Rest[1]));
						if (bPlane) { Btn = W; break; }
					}
					if (Btn) break;
				}
			}
			// ★ В JUI база кнопки — StaticWindow, импортируется КАРТИНКОЙ, а не UButton: клики и наведение
			//   она не принимает. Кладём поверх прозрачную кнопку по слоту картинки (приём из
			//   UAsdaIconCellGrid::BuildClickOverlay) — она и даёт клик, и переключает состояния _O/_C.
			UButton* B = Cast<UButton>(Btn);
			if (!B && Btn)
			{
				UCanvasPanelSlot* BS = AsdaUi::SlotOf(Btn);
				UPanelWidget* ParentCanvas = Btn->GetParent();
				if (BS && ParentCanvas && ServerSelectWidget->WidgetTree)
				{
					B = ServerSelectWidget->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
						FName(*FString::Printf(TEXT("%s_HIT%02d"), *BasePrefix, Idx)));
					FButtonStyle St = B->GetStyle();
					St.Normal.TintColor = St.Hovered.TintColor = St.Pressed.TintColor =
						FSlateColor(FLinearColor(1, 1, 1, 0));
					B->SetStyle(St);
					if (UCanvasPanelSlot* NS = Cast<UCanvasPanelSlot>(ParentCanvas->AddChild(B)))
					{
						NS->SetAnchors(BS->GetAnchors());
						NS->SetPosition(BS->GetPosition());
						NS->SetSize(BS->GetSize());
						NS->SetAlignment(BS->GetAlignment());
						NS->SetZOrder(50);
					}
					// Клик по прозрачной кнопке = выбор канала/сервера (у картинки обработчика нет).
					UAsdaButtonRelay* CR = NewObject<UAsdaButtonRelay>(ServerSelectWidget);
					CR->Index = Idx;
					const bool bChannelPrefix = BasePrefix.StartsWith(TEXT("CHANNEL"));
					CR->Callback = [this, bChannelPrefix](int32 i)
					{
						if (bChannelPrefix) { OnChannelClicked(i); } else { OnServerChosen(i); }
					};
					B->OnClicked.AddDynamic(CR, &UAsdaButtonRelay::OnClicked);
					AsdaUi::AnchorRelay(ButtonRelays, CR, B);
				}
			}
			if (!B) return;
			// Состояния лежат так же — с суффиксом плана ("CHANNEL_BTN00_O_0"), поэтому ищем по префиксу.
			auto Find = [&](const TCHAR* Suffix) -> UWidget*
			{
				const FString S2 = FString::Printf(TEXT("%s%02d%s"), *BasePrefix, Idx, Suffix);
				const FString S3 = FString::Printf(TEXT("%s%03d%s"), *BasePrefix, Idx, Suffix);
				TArray<UWidget*> AllW;
				ServerSelectWidget->WidgetTree->GetAllWidgets(AllW);
				for (UWidget* W : AllW)
				{
					if (!W) continue;
					const FString N = W->GetName();
					if (N.StartsWith(S2) || N.StartsWith(S3)) return W;
				}
				return nullptr;
			};
			TWeakObjectPtr<UWidget> Over  = Find(TEXT("_O"));
			TWeakObjectPtr<UWidget> Press = Find(TEXT("_C"));
			if (!Over.IsValid() && !Press.IsValid()) return;
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(ServerSelectWidget);
			R->Index = Idx;
			auto Show = [](TWeakObjectPtr<UWidget> W, bool bOn)
			{
				if (W.IsValid()) W->SetVisibility(bOn ? ESlateVisibility::HitTestInvisible
				                                     : ESlateVisibility::Collapsed);
			};
			R->HoverCallback   = [Show, Over]()  { Show(Over, true); };
			R->UnhoverCallback  = [Show, Over, Press]() { Show(Over, false); Show(Press, false); };
			R->PressCallback   = [Show, Press](int32, bool bDown) { Show(Press, bDown); };
			B->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
			B->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
			B->OnPressed.AddDynamic(R, &UAsdaButtonRelay::OnPressed);
			B->OnReleased.AddDynamic(R, &UAsdaButtonRelay::OnReleased);
			AsdaUi::AnchorRelay(ButtonRelays, R, B);
		};

		// Серверы: слот 0 = наш сервер (OPSet). Подпись — на Nom-базе (SERVER_BTN00, видима) И на Sell (SERVER_SELECT00_1).
		//   Слоты 1-4 существуют в JUI, но серверов нет → скроет ShowServerOrChannelView. Клик → OnServerChosen.
		const FString SrvName = Server.Name.IsEmpty() ? FAsdaLoginData::Loc(11000) : Server.Name;
		SetTxt(TEXT("SERVER_BTN00_Text"), SrvName);
		SetTxt(TEXT("SERVER_SELECT00_1_Text"), SrvName);
		for (int32 s = 0; s < 5; ++s)
		{
			BindBtn(FString::Printf(TEXT("SERVER_BTN%02d"), s), s, [this](int32 i){ OnServerChosen(i); });
			BindStateVisuals(TEXT("SERVER_BTN"), s);          // подсветка/нажатие слота сервера
		}
		BindBtn(TEXT("SERVER_SELECT_BTN1"), 0, [this](int32){ OnServerChosen(0); });   // подтвердить сервер → 1012

		// Каналы 0..14: имя канала loc 11051+i на Nom-базе (CHANNEL_BTN0i, видима) И на Sel (CHANNEL_SELECT0i).
		// НЕ сбрасываем в 1: при возврате из char-select виджет пересоздаётся, а счётчики каналов
		// уже известны из 1013 — иначе второй канал пропадал после «Назад».
		NumRealChannels = ComputeRealChannels();
		for (int32 c = 0; c < 15; ++c)
		{
			const FString ChName = FAsdaLoginData::Loc(11051 + c);
			SetTxt(FString::Printf(TEXT("CHANNEL_BTN%02d_Text"), c), ChName);
			SetTxt(FString::Printf(TEXT("CHANNEL_SELECT%02d_Text"), c), ChName);
			BindBtn(FString::Printf(TEXT("CHANNEL_BTN%02d"), c), c, [this](int32 i){ OnChannelClicked(i); });   // клик = выделить
			BindStateVisuals(TEXT("CHANNEL_BTN"), c);          // подсветка/нажатие канала
		}
		BindBtn(TEXT("CHANNEL_SELECT_BTN1"), 0, [this](int32){ OnChannelSelected(SelectedChannel); }); // подтвердить канал → 1005

		// ★ ТИТУЛЫ этапов (loc 114="Выбор сервера" / 115="Выбор канала"): в JUI бары
		//   SERVER_LIST/CHANNEL_LIST — StaticWindow БЕЗ текст-виджета (игра рисует текст рантаймом).
		//   ★★★ САМИ УЗЛЫ ПЕРЕЕХАЛИ В АССЕТ (2026-08-27).
		//   ЧТО БЫЛО ЗДЕСЬ. Лямбда `AddTitle`: загрузка шрифта `AsdaUIFontRT`, обход дерева в
		//   поисках РЕАЛЬНОГО бар-Image (не группы `*_grp` — первым по префиксу шла группа с чужим
		//   слотом, и титул уезжал), конструирование `UTextBlock` и копирование слота бара 1:1.
		//   ПОЧЕМУ УБРАНО. Геометрия — копия чужого слота, т.е. константа: оба бара стоят на
		//   off(5, 0, 210, 29) в своих группах `SERVER_LIST_grp` / `CHANNEL_LIST_grp`.
		//   ГДЕ ЖИВЁТ ТЕПЕРЬ. Узлы `SERVER_TitleTxt` / `CHANNEL_TitleTxt` лежат в
		//   `WBP_SERVER_SELECT_WIN` (шрифт `AsdaUIFontRT` 14 Regular, центр, z=50); создаёт их шаг
		//   `Tools/asda_ui_geometry_bake.py` (блок G3), он в `STEPS`, имена — в `HANDMADE`.
		//   ⚠ В АССЕТ УШЛА ТОЛЬКО ГЕОМЕТРИЯ. Текст — ДАННЫЕ (таблица локализации), поэтому его
		//   по-прежнему кладёт код; строка меняется вместе с языком клиента.
		SetTxt(TEXT("SERVER_TitleTxt"), FAsdaLoginData::Loc(114));    // «Выбор сервера»
		SetTxt(TEXT("CHANNEL_TitleTxt"), FAsdaLoginData::Loc(115));   // «Выбор канала»

		ServerSelectWidget->AddToViewport(10);
		// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (Enter → кнопка согласия): окно показано мимо реестра окон — роспись для Enter, иначе он
		//   не нашёл бы «Выбрать»/«Войти» (`UAsdaEnterConfirmSubsystem`, z — тот же порядок вьюпорта 10).
		UAsdaEnterConfirmSubsystem::RegisterExternalWindow(this, FName(TEXT("SERVER_SELECT_WIN")), ServerSelectWidget, 10);
	}
	FocusUI(ServerSelectWidget);
	ServerSelectWidget->SetVisibility(ESlateVisibility::Visible);
	ShowServerOrChannelView(false);   // старт — этап серверов
}

// Извлечь индексный номер (2 цифры) из имени вида PREFIX_BTNnn[...] ; -1 если нет.
static int32 ExtractBtnIndex(const FString& Name, const TCHAR* Prefix)
{
	int32 P = Name.Find(Prefix);
	if (P == INDEX_NONE) return -1;
	const int32 D = P + FCString::Strlen(Prefix);
	// ★ Читаем ВСЕ цифры после префикса, а не ровно две. В JUI индекс бывает трёхзначным:
	//   база канала 12 названа "CHANNEL_BTN012", тогда как её состояния — "CHANNEL_BTN12_O"/"_C".
	//   При жёстком Mid(D,2) из "CHANNEL_BTN012" читалось "01" -> канал 1, поэтому подписи каналов
	//   12..15 показывались как второй канал (чужой скин, без своих Over/Press => нет hover/клика).
	int32 End = D;
	while (End < Name.Len() && FChar::IsDigit(Name[End])) ++End;
	if (End == D) return -1;
	const FString Num = Name.Mid(D, End - D);
	return FCString::Atoi(*Num);
}

void AAsdaLoginGameMode::ShowServerOrChannelView(bool bChannels)
{
	if (!ServerSelectWidget || !ServerSelectWidget->WidgetTree) return;
	// ★ Видимость по РЕАЛЬНОЙ структуре (full-structural-analysis, server_win_full.txt). Индекс слота/канала —
	//   СТРОГО после известного префикса idc (ExtractBtnIndex), НЕ первый 2-знак в имени (иначе гло-индекс
	//   виджета "CHANNEL_LIST_15_0" ловится как «канал 15» → рамка исчезает).
	//   Состояния = отдельные контролы: Nom(база,всегда)/Over/Press/Sell(выбран,оранж)/Off. Бары: BASE=контур(всегда),
	//   CONNECTBAR0i_seg=заливка по онлайну, Connect_Full=индикатор.
	// ★★★ ДЕДУПЛИКАЦИЯ 2026-09-02. Здесь жила лямбда-копия правила видимости
	//   («`W->IsA<UButton>() ? Visible : SelfHitTestInvisible`»). Правило одно на весь порт —
	//   `AsdaCommunityUi::ShownVisibilityFor` (`UI/AsdaCommunityFriendSubsystem.h`), откат
	//   `asda.UI.InteractiveHitTest 0`.
	//   ⚠ В ЭТОМ окне поведение НЕ меняется: в разметке `SERVER_SELECT_WIN` (199, `login_jui.ini`,
	//   349 узлов) нет НИ ОДНОГО контрола со скином `EditBox`/`CheckButton`/`ComboBox` —
	//   только `StaticWindow` 316, `Text` 30, `PushButton` 2 и `ResizeWindow` 1. Слоты серверов
	//   и каналов порт достраивает `UButton`-ами, а они получали `Visible` и раньше.
	auto ShownVis = [](UWidget* W){ return AsdaCommunityUi::ShownVisibilityFor(W); };
	auto OnlineLit = [this](int32 Ch) -> int32 {
		int32 Cnt = 0;
		if (UGameInstance* GI = GetGameInstance()) if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
			if (Net->ChannelOnline.IsValidIndex(Ch)) Cnt = Net->ChannelOnline[Ch];
		return (Cnt < 0) ? 0 : FMath::Clamp(Cnt / 20 + 1, 1, 5);
	};
	const int32 NumServers = 1;
	TArray<UWidget*> All;
	ServerSelectWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) continue;
		const FString N = W->GetName();
		if (N == TEXT("RootCanvas")) continue;
		if (N.StartsWith(TEXT("SERVER_SELECT_WIN"))) continue;   // рамка/тело окна — всегда

		const bool bServer  = N.StartsWith(TEXT("SERVER"));
		const bool bChannel = N.StartsWith(TEXT("CHANNEL")) || N.StartsWith(TEXT("CONNECTBAR")) || N.StartsWith(TEXT("Connect_Full"));
		if (!bServer && !bChannel) continue;
		if (bServer ? bChannels : !bChannels) { W->SetVisibility(ESlateVisibility::Collapsed); continue; }

		bool bVis = true;
		if (bServer)
		{
			int32 slot = ExtractBtnIndex(N, TEXT("SERVER_BTN"));
			bool isSell = false;
			if (slot < 0) { slot = ExtractBtnIndex(N, TEXT("SERVER_SELECT")); isSell = (slot >= 0); }
			if (slot < 0) bVis = true;                                              // титул/декор/кнопка «Войти»
			else if (slot >= NumServers) bVis = false;                             // нет такого сервера
			else if (N.Contains(FString::Printf(TEXT("SERVER_BTN%02d_O"), slot)) ||
			         N.Contains(FString::Printf(TEXT("SERVER_BTN%02d_C"), slot)) ||
			         N.Contains(FString::Printf(TEXT("SERVER_BTN%02d_D"), slot))) bVis = false;   // Over/Press/Off
			else if (isSell) bVis = (slot == SelectedServer);                      // Sell/выбран (оранж)
			else bVis = true;                                                      // Nom-база + подпись
		}
		else if (N.StartsWith(TEXT("CONNECTBAR")) || N.StartsWith(TEXT("Connect_Full")))
		{
			const bool bFull = N.StartsWith(TEXT("Connect_Full"));
			int32 ch = ExtractBtnIndex(N, TEXT("CONNECTBAR_BASE"));
			const bool isBase = (ch >= 0);
			if (ch < 0) ch = ExtractBtnIndex(N, bFull ? TEXT("Connect_Full") : TEXT("CONNECTBAR"));
			if (ch < 0 || ch >= NumRealChannels) bVis = false;
			else if (bFull) bVis = false;                                          // Connect_Full — мёртвая ветка (эталон её не показывает НИКОГДА)
			// ★ У НЕДОСТУПНОГО канала (count<0) индикатора нет ВОВСЕ — эталон скрывает и подложку CONNECTBAR_BASE.
			else if (IsChannelDisabled(ch)) bVis = false;
			else if (isBase) bVis = true;                                          // контур ромбов — у доступных
			else
			{
				const FString Pref = FString::Printf(TEXT("CONNECTBAR%02d_"), ch);
				int32 Seg = -1; const int32 pp = N.Find(Pref);
				if (pp >= 0 && pp + Pref.Len() < N.Len() && FChar::IsDigit(N[pp + Pref.Len()])) Seg = N[pp + Pref.Len()] - TCHAR('0');
				bVis = (Seg >= 0 && Seg < OnlineLit(ch));                          // заливка по онлайну
			}
		}
		else
		{
			int32 tab = ExtractBtnIndex(N, TEXT("CHANNEL_BTN"));
			bool isSel = false;
			if (tab < 0) { tab = ExtractBtnIndex(N, TEXT("CHANNEL_SELECT")); isSel = (tab >= 0); }
			const bool bDisabled = (tab >= 0) && IsChannelDisabled(tab);
			const bool bIsOffPlate = (tab >= 0) && N.Contains(FString::Printf(TEXT("CHANNEL_BTN%02d_D"), tab));
			const bool bIsDisText  = (tab >= 0) && N.Contains(FString::Printf(TEXT("CHANNEL_DIS%02d"), tab));
			if (tab < 0) bVis = true;                                              // титул/декор/кнопка «Войти»
			else if (tab >= NumRealChannels) bVis = false;
			// ★ НЕДОСТУПНЫЙ канал (эталон uiPanelShowVariantFull@0xF1BFA0): показываем ТОЛЬКО серую плашку
			//   CHANNEL_BTN{NN}_D (скин Log_Channel_Base_Off) и текст CHANNEL_DIS{NN} (нотис 11013);
			//   Nom-база, Sel и hover/press скрыты. Канал остаётся на экране, но не выбирается.
			else if (bDisabled) bVis = (bIsOffPlate || bIsDisText);
			else if (bIsOffPlate || bIsDisText) bVis = false;                      // у доступного «серых» нет
			else if (N.Contains(FString::Printf(TEXT("CHANNEL_BTN%02d_O"), tab)) ||
			         N.Contains(FString::Printf(TEXT("CHANNEL_BTN%02d_C"), tab))) bVis = false;   // Over/Press
			else if (isSel) bVis = (tab == SelectedChannel);                       // Sel/выбран (оранж)
			else bVis = true;                                                      // Nom-база + подпись
		}
		W->SetVisibility(bVis ? ShownVis(W) : ESlateVisibility::Collapsed);
	}
}

void AAsdaLoginGameMode::OnServerChosen(int32 ServerIndex)
{
	SelectedServer = ServerIndex;   // подсветить выбранный слот (_O оранжевый)
	// Клик по серверу (эталон event 0xD ID 210+10s) → 1012 (server-select, sub_40D540 → 4083 echo).
	// UI сам переключается на список каналов. char-list тут НЕ запрашивается (только по клику канала).
	UE_LOG(LogAsdaLogin, Log, TEXT("Выбран сервер #%d → 1012 (server-select)"), ServerIndex);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			// ★ Индекс сервера нужен и В МИРЕ: подпись 1717 мини-карты = нотис 15200 «[ss1] - [sd1] канал»,
			//   где ss1 = имя сервера loc(11000 + index) (эталон ChannelSelectWnd_UpdateChannelLabels: dword_340F328).
			Net->SelectedServerIndex = ServerIndex;
			Net->SendChannelSelect((uint8)ServerIndex);
		}
	}
	ShowServerOrChannelView(true);   // переключить на список каналов (в оригинале CHANNEL_LIST Vis→1)
}

// ★ Канал недоступен, если счётчик онлайна из 1013 отрицательный (0xFFFF): отдельного флага в протоколе нет.
bool AAsdaLoginGameMode::IsChannelDisabled(int32 Channel) const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			if (Net->ChannelOnline.IsValidIndex(Channel)) { return Net->ChannelOnline[Channel] < 0; }
			return Net->ChannelOnline.Num() > 0;   // счётчики пришли, а этого канала в них нет → недоступен
		}
	}
	return false;
}

int32 AAsdaLoginGameMode::ComputeRealChannels() const
{
	// ★ ЭТАЛОН (channel_select_re.md §4): число кнопок каналов = m_nChannelCount (+0x3BC) — КОНСТАНТА
	//   клиента, а НЕ «сколько доступных». В RU/арабском клиенте она = 3, в мастер-сборке 5.
	//   Видимость: SetVisible(CHANNEL_BTN{i}, i < m_nChannelCount) — НЕДОСТУПНЫЕ каналы остаются
	//   на экране СЕРОЙ плашкой, а не исчезают (прежний подсчёт «валидных» их прятал).
	return ChannelSlotCount;
}

void AAsdaLoginGameMode::OnChannelClicked(int32 Channel)
{
	// ★ Эталонный флоу: клик по каналу ТОЛЬКО выделяет его (контрол CHANNEL_SELECT<idx>, оранжевый),
	//   вход в игру — отдельным подтверждением «Войти» (CHANNEL_SELECT_BTN1 → 1012+1005).
	//   Раньше клик сразу отправлял пакеты, поэтому выбор проскакивал мимо выделения.
	if (Channel < 0 || Channel >= NumRealChannels) return;
	// ★ Недоступный канал НЕ кликабелен (эталон: гейт всех событий мыши — видимость серой плашки _D).
	if (IsChannelDisabled(Channel))
	{
		UE_LOG(LogAsdaLogin, Log, TEXT("Канал #%d недоступен — клик игнорируется (как в оригинале)"), Channel);
		return;
	}
	SelectedChannel = (uint8)Channel;
	UE_LOG(LogAsdaLogin, Log, TEXT("Канал #%d выделен (вход — по кнопке «Войти»)"), Channel);
	ShowServerOrChannelView(true);   // перерисовать: Sel у выбранного, Nom у остальных
}

void AAsdaLoginGameMode::OnChannelSelected(int32 Channel)
{
	// ★ Эталонный флоу (post_login_flow.md §8b): клик КАНАЛА шлёт ТОЛЬКО 1005 (Send_CharacterPacket,
	//   char-list). НЕ ждём 1013 как гейт — в одиночном сервере он не приходит (косметика населённости).
	//   Ответ на 1005: 1007(equip)+1006(char-list)+1025 → OnCharListReady. Секрет пакета сервер терпит
	//   (кик только после >15 рассинхронов); MD5-целостность тела теперь корректна (§8c) → пакет принят.
	SelectedChannel = (uint8)Channel;
	// ★ ДЕФИНИТИВНО из захвата оригинала (asda_login.pcapng, поток 51634→15001): последовательность отправок
	//   клиента = 1003(логин) → 1012(server-select) → 1005(char-list) → 1016(enter-world). server-select (1012)
	//   ОБЯЗАТЕЛЕН перед char-list: сервер на 1012 отвечает 1013 (список каналов), и лишь ПОТОМ принимает 1005.
	//   Оригинал шлёт 1012 и 1005 последовательно (payload 1012 = UserIndex+channel, payload 1005 = userNo+seq).
	//   Клик канала = одно действие пользователя → шлём оба пакета по порядку.
	UE_LOG(LogAsdaLogin, Log, TEXT("Выбран канал #%d → server-select(1012)+char-list(1005)"), Channel);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			// ★ Канал и число слотов — В МИР (эталон g_nCurrentChannel / g_nChannelCount). Панель каналов
			//   мини-карты (17501) рисует ровно ChannelCount строк и красит текущую своим цветом.
			Net->CurrentChannel = Channel;
			Net->ChannelCount   = NumRealChannels;
			Net->SendChannelSelect((uint8)Net->SelectedServerIndex);   // 1012 — сервер ответит 1013
			Net->SendCharListRequest((uint16)Net->SelectedServerIndex);   // 1005 — сервер ответит 1007+1006+1025
		}
	}
}

void AAsdaLoginGameMode::OnChannelOnlineUpdated()
{
	// Пришёл 1013 → число реальных каналов = валидные счётчики (!=0xFFFF); обновить видимость + бары.
	if (!ServerSelectWidget) return;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			NumRealChannels = ComputeRealChannels();   // единый источник: счётчики 1013
			Net->ChannelCount = NumRealChannels;       // то же число строк — панели каналов мини-карты в мире
			// ★ Ромбики загруженности (channel_load_fix.md, эталон uiShowControlRow @0xF1BC30): АБСОЛЮТНЫЕ пороги
			//   по 20 юзеров — Lit = clamp(count/20 + 1, 1, 5). Пустой канал (0 онлайн) = 1 ромбик (НЕ 0/полный);
			//   полный (5) только при ≥80 онлайн. count<0 (0xFFFF) = недоступен → 0 ромбиков. НЕ процент curr/max.
// Бары JUI: CONNECTBAR{cc}_{seg} — картинки-виджеты с idx-суффиксом → матч по ПРЕФИКСУ.
				//   Lit ромбов = clamp(count/20+1,1,5) (пороги по 20 юзеров). count<0 → 0.
				TArray<UWidget*> AllBars;
				if (ServerSelectWidget->WidgetTree) ServerSelectWidget->WidgetTree->GetAllWidgets(AllBars);
				for (UWidget* W : AllBars)
				{
					if (!W) continue;
					const FString BN = W->GetName();
					if (!BN.StartsWith(TEXT("CONNECTBAR"))) continue;
					const int32 Ch = ExtractBtnIndex(BN, TEXT("CONNECTBAR"));
					if (Ch < 0 || Ch >= NumRealChannels) { W->SetVisibility(ESlateVisibility::Collapsed); continue; }
					const FString Pref = FString::Printf(TEXT("CONNECTBAR%02d_"), Ch);
					int32 Seg = -1;
					if (BN.StartsWith(Pref) && BN.Len() > Pref.Len() && FChar::IsDigit(BN[Pref.Len()])) Seg = BN[Pref.Len()] - TCHAR(0x30);
					const int32 Cnt = Net->ChannelOnline.IsValidIndex(Ch) ? Net->ChannelOnline[Ch] : 0;
					// ★ Недоступный канал (count<0): индикатора НЕТ вовсе (эталон скрывает и подложку
					//   CONNECTBAR_BASE, и сегменты). Доступный: Lit = clamp(count/20+1,1,5), 0 онлайн = 1.
					const int32 Lit = (Cnt < 0) ? 0 : FMath::Clamp(Cnt / 20 + 1, 1, 5);
					W->SetVisibility((Seg >= 0 && Seg < Lit) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
				}
			// перерисовать видимость каналов, если сейчас показан channel-view.
			ShowServerOrChannelView(true);
		}
	}
}

void AAsdaLoginGameMode::OnCharListReady()
{
	// ★ charsel_preload: данные персонажей пришли — фоном грузим их превью ДО открытия шкатулки (перелёт ≈3.3 с
	//   + открытие 2 с). Повторный вызов с тем же набором менеджер отсеивает.
	RequestCharSelectPreload();
	// ★ Во время секвенции шкатулки (форджинг/back) свежий char-list только запоминается (Net->CharList уже
	//   обновлён) — спавн/UI произойдут по завершении секвенции (эталон: запись перса кладётся сразу,
	//   презентация ждёт открытия шкатулки).
	if (CasketSeq != ECasketSeqPhase::None)
	{
		UpdateCharSelectInfo();
		return;
	}
	// ★ Данные char-list приходят ДВУМЯ пакетами: 0x3EE (слоты+созвездие) и 0x3FF (имя/класс/уровень/статы) —
	//   оба вещают это событие. Перелёт запускаем ТОЛЬКО ОДИН раз; повторный вызов (второй пакет) НЕ рестартует
	//   анимацию, а лишь обновляет инфо-панель/персонажа, если char-select уже показан (иначе перелёт бы сбрасывался).
	if (bFlightActive || bCasketOpening || bCasketOpened)
	{
		UpdateCharSelectInfo();
		if (bCasketOpened)
		{
			SpawnCharacterInSlot();   // догрузились пол/данные из 0x3FF → пересобрать превью
			// ★ Пересборка во время активного фейда: новые компоненты продолжают с ТЕКУЩЕГО прогресса
			//   (иначе резкий скачок в полную видимость — дефолт FadeIn=1).
			if (PreviewFadeTime >= 0.f && PreviewFadeTime < PreviewFadeDur)
			{
				CollectPreviewFadeComps(FMath::Clamp(PreviewFadeTime / PreviewFadeDur, 0.f, 1.f));
			}
		}
		return;
	}
	// ★ Первый вход в char-select (state 5): скрыть server-select, перелёт камеры к шкатулке + открытие (клип _0 вперёд).
	UE_LOG(LogAsdaLogin, Log, TEXT("Char-list получен → char-select: перелёт + открытие шкатулки"));
	if (ServerSelectWidget) { ServerSelectWidget->SetVisibility(ESlateVisibility::Collapsed); }
	HideLoginScene();   // ★ уход в char-select → скрыть весь login-оверлей (лого/аним/версия/quit)
	// ★ Робот-помощник (WBP_ANI_WIN) на время ПЕРЕЛЁТА камеры скрыт (HideLoginScene его пропускает — god-robot
	//   виден на всех логин-экранах); снова показывает ShowCharSelectUI (с Z поверх градиента char-select).
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(TEXT("WBP_ANI_WIN")), false);
	}
	bFlightActive = true;
	FlightTime = 0.f;
	bCasketOpened = false;
	if (bFlightSeqMode && FlightPlayer)   // перелёт = секвенция (иначе массив в Tick)
	{
		FlightPlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(FFrameTime(FFrameNumber(0)), EUpdatePositionMethod::Jump));
		FlightPlayer->Play();
	}
}

void AAsdaLoginGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickStartLogo(DeltaSeconds);

	// ★★★ РАННИЙ КОННЕКТ к логин-серверу — ПЕРВЫМ ТИКОМ, а не из StartPlay (аудит 2026-09-02, К-4).
	//   К этому моменту первый кадр уже отрисован: заставка на экране, и пауза на установление
	//   соединения (её предел — asda.Net.ConnectTimeoutSec) не выглядит как «клиент завис до старта».
	//   Заявку снимаем ДО вызова — повторно не пытаемся, реконнект делает DoLogin по кнопке входа.
	if (bEarlyConnectPending)
	{
		bEarlyConnectPending = false;
		if (UGameInstance* GI = GetGameInstance())
			if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
			{
				// ★ Р-2 (ревизия shopdbl2, 10.10.2026): автопрогон без сети (`-AsdaNoNet`) или в процессе уже допущен
				//   офлайн-самотест (`OfflineSelfTestLatch`) — раннее автоподключение не поднимаем. Ручной вход (DoLogin) — как был.
				if (UAsdaNetSubsystem::IsNoNetRun())
				{
					UE_LOG(LogAsdaLogin, Log, TEXT("Ранний коннект пропущен: ключ -AsdaNoNet (прогон без сети)"));
				}
				else if (!Net->OfflineSelfTestLatch.IsEmpty())
				{
					UE_LOG(LogAsdaLogin, Warning, TEXT("Ранний коннект пропущен: в процессе идёт офлайн-самотест %s"), *Net->OfflineSelfTestLatch);
				}
				else if (!Net->IsConnected()) { Net->ConnectToLogin(Server.IP, Server.Port); }
			}
	}

	// ★ Секвенции шкатулки: форджинг create-успеха / закрытие+обратный перелёт на back (фазовая машина).
	TickCasketSequence(DeltaSeconds);

	// ★★★ «Одержимость»: наведение мышью на персонажа соулмейта (эталон scene_handleLeftClickPick@0xF0A680
	//   зовётся тиком сцены выбора персонажа) + докрутка one-shot переходов парных поз.
	TickPossessScene(DeltaSeconds);
	// ★ Дыхание ауры силуэтов — отдельным тиком (не внутри TickPossessScene: у того ранние
	//   выходы по наведению/секвенции, а аура должна пульсировать каждый кадр, пока силуэт виден).
	TickSoulAura(DeltaSeconds);

	// ★ ВРЕМЕННО (self-test форджинга БЕЗ сервера и БЕЗ ручного прогона): ASDA_TEST_FORGE=1 (+ASDA_TEST_FLOW=1) —
	//   после открытия шкатулки авто-запуск форджинг-секвенции + серия скриншотов фаз в Saved/Screenshots.
	{
		static const bool bForgeTest = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_FORGE")) == TEXT("1");
		if (bForgeTest && bCasketOpened && CasketSeq == ECasketSeqPhase::None && !bForgeTestDone)
		{
			bForgeTestDone = true;
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, [this]()
			{
				CreateSlot = SelectedSlot;
				StartForgeSequence();
				auto Shot = [this](float T, const FString& Name)
				{
					FTimerHandle S;
					GetWorldTimerManager().SetTimer(S, [Name]()
					{
						FScreenshotRequest::RequestScreenshot(Name, false, false);
					}, T, false);
				};
				Shot(1.5f, TEXT("forge_1_close"));   // закрытие (камера отлетает)
				Shot(3.0f, TEXT("forge_2_flash"));   // вспышка (глоу t=1.0 после снапа 0.5)
				Shot(4.5f, TEXT("forge_3_glow"));    // позднее свечение
				Shot(6.5f, TEXT("forge_4_open"));    // открытие
			}, 2.0f, false);
		}
	}

	// ★ Фейд-ин превью-персонажей/глифов (~1с после открытия шкатулки, эталон).
	if (PreviewFadeTime >= 0.f)
	{
		PreviewFadeTime += DeltaSeconds;
		const float A = FMath::Clamp(PreviewFadeTime / PreviewFadeDur, 0.f, 1.f);
		for (FPreviewFadeEntry& E : PreviewFadeEntries)
		{
			for (UMaterialInstanceDynamic* Mid : E.Mids)
			{
				if (Mid) { Mid->SetScalarParameterValue(FName(AsdaMat::kParamFadeIn), A); }
			}
		}
		for (const TWeakObjectPtr<AAsdaEffectActor>& Wp : PreviewFadeGlyphFx)   // ★ В2-C F11: только при asda.Login.GlyphFade 1
		{
			if (AAsdaEffectActor* G = Wp.Get()) { G->SetFxAlphaScale(A); }
		}
		if (A >= 1.f) { PreviewFadeTime = -1.f; FinishPreviewFade(); }   // один раз; вернуть исходные материалы
	}

	// ★ B3: вращение create-превью при удержании стрелки (эталон objPitchStepBy6Deg: ±6°/кадр вокруг Y).
	if (CreateRotDir != 0 && CreatePreviewActor)
	{
		CreateYaw += 6.f * CreateRotDir;   // 6° за тик-кадр
		CreatePreviewActor->SetActorRotation(CreateBaseRot + FRotator(0.f, CreateYaw, 0.f));
	}

	// Вотчдоги по эталону: задник ВСЕГДА крутится; шкатулка до открытия ЗАКРЫТА (клип _0, t=0).
	if (BackdropActor)
	{
		if (USkeletalMeshComponent* B = BackdropActor->GetSkeletalMeshComponent())
		{
			UAnimSingleNodeInstance* SN = B->GetSingleNodeInstance();
			if (SN && !SN->IsPlaying()) { SN->SetLooping(true); SN->SetPlaying(true); }
		}
	}
	static const bool bCasketPlayTest = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_CASKET_PLAY")) == TEXT("1");
	// Вотчдог держит шкатулку ЗАКРЫТОЙ только В IDLE (до перелёта); НЕ во время открытия/после.
	if (!bCasketPlayTest && !bFlightActive && !bCasketOpening && !bCasketOpened && CasketActor)
	{
		if (USkeletalMeshComponent* C = CasketActor->GetSkeletalMeshComponent())
		{
			UAnimSingleNodeInstance* SN = C->GetSingleNodeInstance();
			if (!SN) { FreezeCasketClosed(); }
			else if (SN->IsPlaying() || SN->GetCurrentTime() != 0.f)
			{
				SN->SetPlaying(false);
				SN->SetPosition(0.f, false);
			}
		}
	}

	// ★ Карусель char-select: проигрывается РОДНОЙ клип поворота шкатулки (lturn/rturn) — подставка+ручка+
	//   шестерёнки крутятся анимацией, персонажи на костях CH_00N едут с ней. По завершении — сменить слот + idle.
	if (bTurnPlaying)
	{
		TurnTime += DeltaSeconds;
		// ★ Звук по кадрам поворота (конфиг TurnSoundCues; фолбэк = эталон case2/3: f0→09 f10→08 f51→10).
		{
			const UAsdaLoginConfig* Cfg = ResolveConfig();
			static const TArray<FAsdaCasketSoundCue> GDefTurnCues = { {0,9},{10,8},{51,10} };
			FireCasketCues((Cfg && Cfg->TurnSoundCues.Num()) ? Cfg->TurnSoundCues : GDefTurnCues, TurnTime, TurnSndMask);
		}
		if (TurnTime >= TurnDur)
		{
			FinishCasketTurn();
		}
	}

	// ФАЗА 1 — перелёт камеры к шкатулке.
	if (bFlightActive)
	{
		bool bFlightDone;
		if (bFlightSeqMode && FlightPlayer)
		{
			bFlightDone = !FlightPlayer->IsPlaying();      // секвенция сама ведёт камеру (★ 13.09; в откате — якорь сцены), Keep State держит финал
		}
		else
		{
			FlightTime += DeltaSeconds;                    // фолбэк: запечённый массив-трек
			ApplyFlightPose(FlightTime);
			bFlightDone = FlightTime >= GLoginFlightTimes[GLoginFlightNum - 1];
		}
		if (bFlightDone)
		{
			bFlightActive = false;
			// Прилетели → ЗАПУСК открытия шкатулки (клип _0 вперёд, один раз, холд на последнем кадре).
			bCasketOpening = true;
			CasketOpenTime = 0.f;
			OpenSndMask = 0;                                     // сброс латча звука открытия
			if (CasketActor)
			{
				if (USkeletalMeshComponent* C = CasketActor->GetSkeletalMeshComponent())
				{
					C->SetPosition(0.f, false);
					if (UAnimSingleNodeInstance* SN = C->GetSingleNodeInstance())
					{
						SN->SetLooping(false);
						if (UAnimSequence* Seq = Cast<UAnimSequence>(SN->GetAnimationAsset()))
						{
							CasketOpenDur = Seq->GetPlayLength();   // точная длительность клипа открытия
						}
					}
					C->Play(false);
				}
			}
			UE_LOG(LogAsdaLogin, Log, TEXT("Прилетели → шкатулка ОТКРЫВАЕТСЯ (%.2fс)"), CasketOpenDur);
		}
		return;
	}

	// ФАЗА 2 — шкатулка открывается; ждём ПОЛНОГО открытия, ТОЛЬКО потом персонаж (как оригинал).
	if (bCasketOpening)
	{
		CasketOpenTime += DeltaSeconds;
		// ★ Звук по кадрам открытия (конфиг OpenSoundCues; фолбэк = эталон case0: f0→01+03 f19→04 f33→05 f55→02).
		{
			const UAsdaLoginConfig* Cfg = ResolveConfig();
			static const TArray<FAsdaCasketSoundCue> GDefOpenCues = { {0,1},{0,3},{19,4},{33,5},{55,2} };
			FireCasketCues((Cfg && Cfg->OpenSoundCues.Num()) ? Cfg->OpenSoundCues : GDefOpenCues, CasketOpenTime, OpenSndMask);
		}
		if (CasketOpenTime >= CasketOpenDur)
		{
			bCasketOpening = false;
			bCasketOpened = true;
			UE_LOG(LogAsdaLogin, Log, TEXT("Шкатулка ОТКРЫТА → появление персонажа"));
			SpawnCharacterInSlot();   // ★ ТОЛЬКО ПОСЛЕ полного открытия шкатулки
			ShowCharSelectUI();       // панель выбора персонажа (кнопки Играть/Назад/... + инфо)
			StartPreviewFadeIn();     // ★ фейд-ин превью+глифов ~1с (после ShowCharSelectUI: глифы уже видимы)
			// ★ ВРЕМЕННО (ASDA_TEST_FLOW): запрос скриншота из движка через 1.5с (RDP CopyFromScreen падает —
			//   render-target захват надёжнее). Сохраняется в Saved/Screenshots/.
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_FLOW")) == TEXT("1"))
			{
				FTimerHandle SH;
				GetWorldTimerManager().SetTimer(SH, [this]()
				{
					if (GEngine && GEngine->GameViewport)
					{
						FScreenshotRequest::RequestScreenshot(TEXT("cremmy_idle_check"), true, false);   // +UI (панель char-select)
						UE_LOG(LogAsdaLogin, Warning, TEXT("[TEST] запрошен скриншот cremmy_idle_check"));
					}
				}, 1.5f, false);
			}
		}
		return;
	}
}

// Перепривязать контент всех слотов (персонаж/глиф) к костям пьедестала по T0[curSlot=SelectedSlot] — вызывается
// при СМЕНЕ выбранного слота (FinishCasketTurn). Так выбранный слот едет во фронт, остальные — по бокам, никто не
// «уезжает назад» за активный (иначе глиф/перс оказывался за активным слотом). Спавн — один раз, только перепривязка.
void AAsdaLoginGameMode::ReattachCarousel()
{
	USkeletalMeshComponent* CaskComp = CasketActor ? CasketActor->GetSkeletalMeshComponent() : nullptr;
	if (!CaskComp) return;
	for (int32 i = 0; i < SlotContent.Num() && i < 3; ++i)
	{
		AActor* A = SlotContent[i];
		if (!A) continue;
		const FName Bone = CarouselPedestalBone(i, SelectedSlot);
		if (CaskComp->DoesSocketExist(Bone))
		{
			// SnapToTarget = переставить на НОВУЮ кость (loc+rot), relative-scale держим (перс CharacterScale,
			//   глиф — относительные 85 к шкатулке, что даёт мировые 0.85 игровой единицы; ★ 12.09: раньше мировые 85).
			A->AttachToComponent(CaskComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bone);
		}
		// ★ Персонаж СОУЛМЕЙТА и «душа» едут с каруселью вместе со «своим» слотом: в эталоне они
		//   дочерние узлы моего объекта и наследуют ту же матрицу кости пьедестала. Кость — ТА ЖЕ.
		//   Общая точка с `StartCasketTurn` (там та же перепривязка на кость поворота T2/T3).
		AttachSlotCompanionsToBone(i, CaskComp, Bone);
	}
}

// ★ Спутники слота (персонаж соулмейта + «душа») — на ту же кость пьедестала, что и мой персонаж.
//   Единая точка для idle-раскладки (`ReattachCarousel`, T0) и раскладки поворота (`StartCasketTurn`,
//   T2/T3): пока эти два места жили порознь, поворот перепривязывал только моего персонажа, и
//   спутники ехали на чужой кости (см. пометку исправления в `StartCasketTurn`).
//   SnapToTargetNotIncludingScale — как у контента слота: позиция/поворот = кость, масштаб свой
//   (у спутников он выставлен при спавне: CharacterScale × зеркало по X).
void AAsdaLoginGameMode::AttachSlotCompanionsToBone(int32 SlotIdx, USkeletalMeshComponent* CaskComp, FName Bone)
{
	if (!CaskComp || SlotIdx < 0 || SlotIdx > 2 || !CaskComp->DoesSocketExist(Bone)) { return; }
	if (AActor* P = PartnerActors.IsValidIndex(SlotIdx) ? PartnerActors[SlotIdx].Get() : nullptr)
	{
		P->AttachToComponent(CaskComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bone);
	}
	if (AActor* S = SoulActors.IsValidIndex(SlotIdx) ? SoulActors[SlotIdx].Get() : nullptr)
	{
		S->AttachToComponent(CaskComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bone);
	}
}

void AAsdaLoginGameMode::CollectPreviewFadeComps(float ApplyAlpha)
{
	// Вернуть исходники прежним записям (пересборка превью посреди фейда) и собрать заново.
	FinishPreviewFade();
	UMaterialInterface* FadeBase = LoadObject<UMaterialInterface>(nullptr, kPreviewFadeMaterial);
	// ПЕРСОНАЖИ: свап каждого слота на MID фейд-материала (Tex переносим из исходного), FadeIn=ApplyAlpha.
	//   ★ Персонажи СОУЛМЕЙТА появляются тем же фейдом, что и мои: в эталоне они живут в той же сцене
	//     и включаются тем же `charDealWndSetSlotEnable`, отдельного появления у них нет.
	TArray<AActor*> FadeChars;
	FadeChars.Reserve(CharacterActors.Num() + PartnerActors.Num() + SoulActors.Num());
	for (AActor* A : CharacterActors) { FadeChars.Add(A); }
	for (AActor* A : PartnerActors)   { FadeChars.Add(A); }
	for (AActor* A : SoulActors)      { FadeChars.Add(A); }   // «душа» появляется тем же фейдом
	for (AActor* A : FadeChars)
	{
		if (!A || A->IsHidden() || !FadeBase) { continue; }
		TArray<UMeshComponent*> Comps;
		A->GetComponents<UMeshComponent>(Comps);
		for (UMeshComponent* Cm : Comps)
		{
			FPreviewFadeEntry E; E.Comp = Cm;
			const int32 N = Cm->GetNumMaterials();
			for (int32 s = 0; s < N; ++s)
			{
				UMaterialInterface* Orig = Cm->GetMaterial(s);
				E.Orig.Add(Orig);
				// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F9): MID-подмена — общая точка `AsdaTranslucentSlots::MakeSwapMid`
				//   (та же, что у фейда мира и «души»). Отличие от прежнего тела — имя `MIDFade_<исходник>` вместо безымянного:
				//   пересборка превью (догрузка 0x3FF посреди фейда) находит секции `face/hair/chst…` по имени материала.
				//   null — слот без Tex (не подменяется, как и прежде).
				UMaterialInstanceDynamic* Mid = AsdaTranslucentSlots::MakeSwapMid(Orig, FadeBase, this, TEXT("MIDFade"),
					FName(AsdaMat::kParamFadeIn), ApplyAlpha);
				if (Mid) { Cm->SetMaterial(s, Mid); }
				E.Mids.Add(Mid);   // null = слот без Tex-парамa — не свапаем (останется как есть)
				// ★ GC-защита: Orig/Mid — в UPROPERTY-зеркало (иначе экип-MID без ссылок собирается GC за время
				//   фейда/хитча → dangling TObjectPtr → краш FinishPreviewFade при возврате материалов).
				if (Orig) { PreviewFadeKeepAlive.Add(Orig); }
				if (Mid)  { PreviewFadeKeepAlive.Add(Mid); }
			}
			PreviewFadeEntries.Add(MoveTemp(E));
		}
	}
	// ★★★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F11): ГЛИФЫ ПУСТЫХ СЛОТОВ.
	//   БЫЛО: скаляр FadeIn через `SetScalarParameterValueOnMaterials` на меш-компонентах глифа — авто-MID на ЭФФЕКТ-актор.
	//   Эффект ставит секции материалом из данных эмиттера при пересборке (`RebuildSection`, «MID на эффекте не живёт»):
	//   авто-MID снимался, фейда глифа не было, а каждый показ превью плодил брошенные MID.
	//   ЭТАЛОН: `channelSelectRender3D@0xF07BE0` (ui_core.c:94044-94067), ветка пустого слота — глиф `eft_creation0X`
	//   тикается (`objAnimRotateUpdate`), получает матрицу кости подставки и кладётся в очередь `CRenderQueue_AddNode` БЕЗ
	//   множителя альфы (последний аргумент — смещение глубины сортировки, 0). Фейд-ин в оригинале — у МОДЕЛИ персонажа
	//   (альфа модели после загрузки), у эффекта глифа его нет. ★ [ОТСТУПЛЕНИЕ] С 11.10.2026 умолчание `asda.Login.GlyphFade 1`
	//   (решение владельца «плавное»): глиф проявляется вместе с превью через рантайм-множитель эффекта `FxAlphaScale`
	//   (вершинная альфа всех построителей; MID не вводится). Ключ 0 — как эталон: глиф без фейда.
	for (const TWeakObjectPtr<AAsdaEffectActor>& Wp : PreviewFadeGlyphFx)
	{
		if (AAsdaEffectActor* G = Wp.Get()) { G->SetFxAlphaScale(1.f); }   // пересборка посреди фейда: прежний множитель снять
	}
	PreviewFadeGlyphFx.Reset();
	if (CVarAsdaLoginGlyphFade.GetValueOnGameThread() != 0)
	{
		for (AActor* G : GlyphActors)
		{
			AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(G);
			if (!Fx || Fx->IsHidden()) { continue; }
			Fx->SetFxAlphaScale(ApplyAlpha);
			PreviewFadeGlyphFx.Add(Fx);
		}
	}
}

void AAsdaLoginGameMode::FinishPreviewFade()
{
	// Вернуть исходные материалы персонажам; глифам — FadeIn=1.
	for (FPreviewFadeEntry& E : PreviewFadeEntries)
	{
		if (UMeshComponent* Cm = E.Comp.Get())
		{
			for (int32 s = 0; s < E.Orig.Num(); ++s)
			{
				// IsValid-гейт: даже с KeepAlive не возвращаем компоненту умерший материал.
				if (E.Mids.IsValidIndex(s) && E.Mids[s] && IsValid(E.Orig[s])) { Cm->SetMaterial(s, E.Orig[s]); }
			}
		}
	}
	PreviewFadeEntries.Reset();
	PreviewFadeKeepAlive.Reset();   // strong-ref зеркало больше не нужно
	for (const TWeakObjectPtr<AAsdaEffectActor>& Wp : PreviewFadeGlyphFx)   // ★ В2-C F11: множитель эффекта глифа — в 1
	{
		if (AAsdaEffectActor* G = Wp.Get()) { G->SetFxAlphaScale(1.f); }
	}
	PreviewFadeGlyphFx.Reset();
}

void AAsdaLoginGameMode::StartPreviewFadeIn()
{
	// ★ Эталон: появление превью после открытия шкатулки = фейд-ин ~1с, ВСЕ слоты одновременно, один раз.
	PreviewFadeTime = 0.f;
	CollectPreviewFadeComps(0.f);
}

void AAsdaLoginGameMode::SpawnCharacterInSlot()
{
	if (!GetWorld()) return;

	// ★ Все 3 слота стоят на ПОДСТАВКЕ (кости CH_001/002/003 шкатулки): занятый → персонаж, пустой → «облако
	//   создания» eft_creation0X. Персонажи ОСТАЮТСЯ на своих слотах; переключение = поворот самой подставки
	//   (шкатулки) — см. стрелки/Tick. Идемпотентно: снести старых + спрятать глифы, заспавнить по текущим данным.
	for (AActor* A : CharacterActors) { if (A) { A->Destroy(); } }
	CharacterActors.Empty();
	for (AActor* G : GlyphActors) { if (G) { G->SetActorHiddenInGame(true); } }
	SlotContent.Reset(); SlotContent.SetNum(3);   // контент по слотам для карусель-перепривязки

	TArray<FAsdaCharRecord> CharList;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>()) { CharList = Net->CharList; }
	}
	const bool bTestFlow = (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_FLOW")) == TEXT("1"));

	for (int32 i = 0; i < 3; ++i)
	{
		// ★ ПОЛ слота = байт записи char-list rec+22 (эталон charSelect_FillSlotRecords → char+1079).
		//   Значение вне {1,2} = сервер не заполнил Sex Type (LoginManager пишет об этом в свой лог) —
		//   тогда превью уходит в мужскую модель, и это должно быть ВИДНО в логе, а не молча.
		bool bOccupied = false; uint8 Sex = 1;
		if (CharList.IsValidIndex(i) && !CharList[i].IsEmpty())
		{
			bOccupied = true; Sex = CharList[i].Sex;
			if (Sex != 1 && Sex != 2)
			{
				UE_LOG(LogAsdaLogin, Warning, TEXT("[пол] слот %d '%s': байт пола = %u (не 1/2) — рисуем мужскую модель"),
					i, *CharList[i].Name, (uint32)Sex);
			}
		}
		else if (bTestFlow && i == 0) { bOccupied = true; Sex = 1; }

		if (bOccupied)
		{
			SpawnOneCharSlot(i, Sex);                                                   // слот i → кость CH_00{i+1}
			// ★ Экип персонажа (0x3EF) распарсен в CharList[i].Equip. Рендер надетых предметов на модели
			//   (замена chst/pant/shoe + крепление оружия) требует item-таблицы (itemId→имя .jbm из Bin.bbz)
			//   и скелетных мешей частей на cre_cremmy — см. charselect_equip.md §B (план). Пока логируем.
			if (CharList.IsValidIndex(i) && CharList[i].Equip.Num() > 0)
			{
				const FAsdaCharRecord& R = CharList[i];
				UE_LOG(LogAsdaLogin, Log, TEXT("Слот %d экип: шлем=%d тело=%d штаны=%d обувь=%d перчатки=%d"),
					i,
					R.Equip.IsValidIndex(0) ? R.Equip[0].ItemId : 0,
					R.Equip.IsValidIndex(1) ? R.Equip[1].ItemId : 0,
					R.Equip.IsValidIndex(2) ? R.Equip[2].ItemId : 0,
					R.Equip.IsValidIndex(3) ? R.Equip[3].ItemId : 0,
					R.Equip.IsValidIndex(4) ? R.Equip[4].ItemId : 0);
			}
		}
		else if (GlyphActors.IsValidIndex(i) && GlyphActors[i])
		{
			// ★ Глиф пустого слота (charselect_present_fix.md §D): эталон каждый кадр ставит эффекту матрицу
			//   кости шкатулки CH_00N (как персонажу) → глиф «стоит на подставке» и едет с каруселью. Крепим
			//   актор эффекта к сокету CH_00{i+1} шкатулки (SnapToTarget = на кость), затем показываем.
			AActor* G = GlyphActors[i];
			USkeletalMeshComponent* CaskComp = CasketActor ? CasketActor->GetSkeletalMeshComponent() : nullptr;
			const FName SlotBone = CarouselPedestalBone(i, SelectedSlot);   // кость пьедестала по роли (не фикс CH_00{i+1})
			const bool bBone = (CaskComp && CaskComp->DoesSocketExist(SlotBone));
			if (bBone)
			{
				G->AttachToComponent(CaskComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SlotBone);
			}
			// ★ 12.09.2026: сцена char-select ПЕРЕВЕДЕНА В ×1 (была ×100 — геометрия запечена ×100 в кости и
			//   вершины). Эффект-геометрия AsdaEffectActor — в СЫРЫХ игровых единицах, и в ×100-сцене глиф
			//   выходил ≈1 юнит при сцене шириной в тысячи = СУБПИКСЕЛЬ = «не видно» (glyph_render_deep.md);
			//   компенсировали мировым ×100. В ×1 масштаб сцены равен игровой единице, и тул ставит глифу
			//   мировые 0.85 (арт-поправка −15 %), а кость шкатулки (−0.01, 0.01, 0.01) даёт относительные
			//   (−85, 85, 85) — зеркало согласовано. + снять cull.
			// ★ Editor-Friendly: арт-параметры глифа (scale, EffectEmitterScale, спин) — РЕДАКТИРУЕМЫЕ дефолты
			//   на самом акторе уровня (asda_place_charselect_glyph.py). GameMode их НЕ перезаписывает — только
			//   гарантирует техфлаг базы=кость (bBaseAlreadyUE) на случай старого ассета. Масштаб держит attach.
			if (AAsdaEffectActor* GE = Cast<AAsdaEffectActor>(G)) { GE->bBaseAlreadyUE = true; }
			G->SetActorHiddenInGame(false);
			if (SlotContent.IsValidIndex(i)) { SlotContent[i] = G; }   // глиф — контент слота i
			UE_LOG(LogAsdaLogin, Log, TEXT("Глиф слот %d показан: кость=%s(%s) loc=%s hidden=%d"),
				i, *SlotBone.ToString(), bBone ? TEXT("есть") : TEXT("НЕТ"),
				*G->GetActorLocation().ToString(), G->IsHidden() ? 1 : 0);
		}
	}
	// ★ Эффект шкатулки (eft_creation01) НЕ показывается постоянно: эталонный тик рендерит +769360 ТОЛЬКО
	//   в pageState 2 (форджинг создания/удаления) — показ/скрытие ведёт TickCasketSequence (глоу-фаза).
	//   (Раньше unhide стоял тут, но эффект был субпиксельным (scale 1) и незаметным; с починенным ×100
	//   постоянный показ давал бы нон-стоп свечение, которого в оригинале нет.)
	bCharacterSpawned = true;
	// ★★★ «ОДЕРЖИМОСТЬ»: рядом с КАЖДЫМ моим персонажем встаёт персонаж соулмейта, если тот им
	//   поделился. Пересобирается ВМЕСТЕ со слотами: акторы партнёров привязаны к тем же костям
	//   пьедестала, что и мои, а мои только что пересозданы.
	SetupPossessScene();
}

void AAsdaLoginGameMode::SpawnOneCharSlot(int32 SlotIdx, uint8 Sex)
{
	UWorld* World = GetWorld();
	if (!World) return;
	const TCHAR* MeshPath = (Sex == 2) ? GCharPreviewMeshF : GCharPreviewMesh;   // 1=M, 2=G(жен)
	const FString AnimPath = CharPreviewIdleAnim((Sex == 2) ? GCharPreviewAnimBaseF : GCharPreviewAnimBase);

	// ★ Editor-Friendly: базовые оффсет/масштаб слота — из конфига DA_LoginConfig (правится в редакторе).
	//   ENV ASDA_CHAR_OFFSET/SCALE — только для быстрой тестовой подстройки, перебивает конфиг.
	if (const UAsdaLoginConfig* Cfg = ResolveConfig())
	{
		SlotLocalOffset = Cfg->SlotLocalOffset;
		CharacterScale  = Cfg->CharacterScale;
	}
	// Тюнингуемые оффсет/масштаб слота (точные XYZ узла CH_00N рантайм; в jbm литералов нет).
	const FString OffEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_CHAR_OFFSET"));
	if (!OffEnv.IsEmpty())
	{
		FVector V;
		if (V.InitFromString(FString::Printf(TEXT("X=%s"), *OffEnv)) || V.InitFromString(OffEnv))
		{
			SlotLocalOffset = V;
		}
		else
		{
			TArray<FString> P; OffEnv.ParseIntoArray(P, TEXT(","));
			if (P.Num() == 3) { SlotLocalOffset = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])); }
		}
	}
	const FString ScEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_CHAR_SCALE"));
	if (!ScEnv.IsEmpty()) { CharacterScale = FCString::Atof(*ScEnv); }

	// ★ Слот персонажа = КОСТЬ CH_001 скелета шкатулки cre_creation01 (дамп: CH_001/002/003 = idx 42-44,
	//   frame0 Y≈36 внизу, поднимаются до Y≈138 при открытии → персонаж «выезжает» из шкатулки).
	//   Базовая позиция = мировой трансформ этой кости; персонаж крепится к ней (едет с анимацией слота).
	USkeletalMeshComponent* CaskComp = CasketActor ? CasketActor->GetSkeletalMeshComponent() : nullptr;
	const FName SlotBone = CarouselPedestalBone(SlotIdx, SelectedSlot);   // кость пьедестала по роли (не фикс CH_00{SlotIdx+1})
	FTransform Base = AsdaLoginPedestalBase(CasketActor, CaskComp, SlotBone);   // ★ R5: общая база слота шкатулки
	const FVector SpawnLoc = Base.TransformPosition(SlotLocalOffset);

	FActorSpawnParameters SP;
	AActor* SlotChar = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnLoc, Base.Rotator(), SP);
	if (!SlotChar) { UE_LOG(LogAsdaLogin, Warning, TEXT("Спавн персонажа слот %d: не создан актёр"), SlotIdx); return; }
	CharacterActors.Add(SlotChar);
	if (SlotContent.IsValidIndex(SlotIdx)) { SlotContent[SlotIdx] = SlotChar; }   // персонаж — контент слота
	if (!CharacterActor) { CharacterActor = SlotChar; }   // legacy-указатель на первого

	// Корень (части крепим к нему; масштаб общий).
	USceneComponent* Root = NewObject<USceneComponent>(SlotChar, TEXT("CharRoot"));
	Root->RegisterComponent();
	SlotChar->SetRootComponent(Root);
	// ★ SetRootComponent на новый (identity) компонент сбрасывает актёра в origin — восстановить мировой
	//   трансформ слота ЯВНО (иначе персонаж стоит в (0,0,0), вне кадра char-select).
	SlotChar->SetActorLocationAndRotation(SpawnLoc, Base.Rotator());
	// ★ ЗЕРКАЛО (как в мире): негейт Y — модель праворукая, конвертер дал лишний флип.
	Root->SetWorldScale3D(FVector(CharacterScale) * FVector(-1.f, 1.f, 1.f));

	// ★ ПЕРСОНАЖ = ОДИН SkeletalMeshComponent на скелете cre_cremmy (18 скиннутых костей), проигрывающий
	//   idle-клип cre_cremmy-a_00_0 (2.0с). Подтверждено RE (charselect_render.md §4): части ch1_m_*
	//   (chst/pant/shoe/face/hair) СКИННУТЫ на этот скелет (не статичная T-поза). Все 5 частей — суб-меши
	//   одного SkeletalMesh с 5 материалами. Idle = anim_00 (луп, «дышит»).
	//   ★ bind восстановлен фитом pos0→POSITION (export_char_cremmy.py): движок оригинала inverse-bind НЕ
	//     хранит (палитра = boneWorld(t)·meshWorld), сырые верты .jbm лежат в bone-local — восстановление
	//     bind опускает руки из авторской T-позы в idle-позу скелета (иначе руки торчали горизонтально).
	int32 Loaded = 0;
	USkeletalMesh* SkMesh = LoadMesh(MeshPath);
	if (SkMesh)
	{
		USkeletalMeshComponent* SkC = NewObject<USkeletalMeshComponent>(SlotChar, TEXT("CharSkel"));
		SkC->SetupAttachment(Root);
		SkC->SetSkeletalMesh(SkMesh);
		SkC->SetRelativeTransform(FTransform::Identity);
		SkC->SetCastShadow(true);
		SkC->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		SkC->RegisterComponent();
		// ★ РАНТАЙМ idle-анимация (запекание убрано, export_char_cremmy.py bake_pose=False): bind восстановлен
		//   фитом pos0→POSITION, тождество pos0·IBM_j·Bone_j(t) = POSITION_j·Bone_j(t) → линейный blend-skinning UE
		//   воспроизводит 2-позиционную формулу Asda точно (bind_err≈0, проверено численно). Проигрываем idle-клип
		//   anim_00 (луп) — персонаж стоит естественно и «дышит», как в оригинале. Экип-части следуют через
		//   LeaderPose=база (тот же скелет, тоже без запекания).
		if (UAnimSequence* Idle = LoadClip(AnimPath))
		{
			SkC->PlayAnimation(Idle, /*bLooping=*/true);
		}
		else
		{
			UE_LOG(LogAsdaLogin, Warning, TEXT("Idle-анима персонажа не загружена: %s"), *AnimPath);
		}
		Loaded = 1;
		// ★ Рендер НАДЕТОЙ снаряги (0x3EF Equip): броня chst/pant/shoe = отдельные скелетные части на ОБЩЕМ
		//   скелете с LeaderPose=база (деформируются idle-анимой) + скрытие базовой секции; оружие ch1_b_* =
		//   StaticMesh на кость M_DUMMY (эталон: якорь оружия). Мост itemId→меш = item-подсистема.
		ApplyEquipToSlot(SlotChar, Root, SkC, Sex, SlotIdx);
	}
	else
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("Превью-меш персонажа не загружен: %s"), MeshPath);
	}

	ApplyPreviewStableQuality(SlotChar);   // ★ charsel_preload: LOD0 + полные мипы (как у персонажа в мире)

	// Привязка к КОСТИ CH_00N шкатулки: персонаж стоит в слоте и «выезжает» при открытии крышки.
	if (CaskComp && CaskComp->DoesSocketExist(SlotBone))
	{
		SlotChar->AttachToComponent(CaskComp, FAttachmentTransformRules::KeepWorldTransform, SlotBone);
	}
	else if (CasketActor && CasketActor->GetRootComponent())
	{
		SlotChar->AttachToActor(CasketActor, FAttachmentTransformRules::KeepWorldTransform);
	}

	UE_LOG(LogAsdaLogin, Log, TEXT("Персонаж слот %d (пол=%u, cremmy idle) заспавнен: loaded=%d loc=%s"),
		SlotIdx, Sex, Loaded, *SpawnLoc.ToString());
}

// Рендер надетой снаряги на персонаже слота: броня (скелетные части + LeaderPose) + оружие (StaticMesh на M_DUMMY).
void AAsdaLoginGameMode::ApplyEquipToSlot(AActor* SlotChar, USceneComponent* Root, USkeletalMeshComponent* BaseSkel, uint8 Sex, int32 SlotIdx)
{
	if (!SlotChar || !Root || !BaseSkel) return;
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Items || !Net) return;
	// ★ РЕГРЕСС-ТЕСТ (ASDA_TEST_FLOW=1, без сервера): CharList пуст — собираем превью по синтетическим данным,
	//   чтобы прогонять ТОТ ЖЕ путь (лицо + причёска через DT_hairtable + части), на котором падал билд.
	if (!Net->CharList.IsValidIndex(SlotIdx))
	{
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_FLOW")) != TEXT("1")) { return; }
		const FAsdaWornParts TestParts = AsdaLoginPossess::TestFlowWornParts();
		const int32 nTest = UAsdaCharAssembly::BuildWornParts(SlotChar, Root, BaseSkel, (Sex != 2), TestParts, Items);
		UE_LOG(LogAsdaLogin, Warning, TEXT("[ТЕСТ] Equip слот %d по синтетическим данным: частей=%d"), SlotIdx, nTest);
		return;
	}

	// ★ Эталон playerCharObjectLoadColored@0xF0A2B0 (charselect_present_fix.md §B): грузятся РОВНО носимые
	//   части-типы по itemType, БЕЗ оружия/причёски. itemType (enum A): 0 шлем, 1 тело, 2 штаны, 3 обувь,
	//   4 перчатки. Оружие(9) в char-select НЕ рендерится. Источник — Equip[itemType] выбранного персонажа;
	//   маппим в канонические поля и отдаём единой системе сборки (та же, что и в char-create/мире).
	//   ★ Маппинг ячеек — ОБЩАЯ точка `AsdaLoginPossess::WornPartsFrom` (её же использует спавн
	//     персонажа соулмейта, чтобы раскладка у своего и чужего не разъехалась).
	const FAsdaCharRecord& Rec = Net->CharList[SlotIdx];
	const FAsdaWornParts Parts = AsdaLoginPossess::WornPartsFrom(Rec.Equip, Rec.Hair, Rec.Color, Rec.Face);
	const int32 nParts = UAsdaCharAssembly::BuildWornParts(SlotChar, Root, BaseSkel, (Sex != 2), Parts, Items);
	UE_LOG(LogAsdaLogin, Log, TEXT("Equip слот %d: надето частей=%d (оружие в char-select не рендерится)"), SlotIdx, nParts);
}

void AAsdaLoginGameMode::DoLogin(const FString& Login, const FString& Password)
{
	if (!Server.IsValid())
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("DoLogin: нет адреса сервера (OPSet)"));
		return;
	}
	// ★ УБРАНО: `LastLogin = Login; LastPassword = Password;` — поля заводились под АВТО-РЕТРАЙ при
	//   overlap (status 4), а его давно нет: OnLoginResult при любом status != 1 показывает нотис и
	//   ОСТАВЛЯЕТ игрока на логине (повторный вход — вручную). Потребителей у полей не осталось
	//   (проверено вторым методом: они не UPROPERTY, значит недоступны ни рефлексии, ни Blueprint,
	//   ни сериализации), а открытый пароль жил в акторе весь сеанс. Теперь не живёт.

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) return;

	// ★ Эталон (netConnectLoginServer коннектится на загрузке в StartPlay): если коннект уже жив, IP
	//   сессии на сервере зарегистрирован → 1003 шлём СИНХРОННО (audit_login_charselect P0-1).
	//   Если же коннект приходится поднимать ЗДЕСЬ (раннего не было или он оборвался) —
	//   отправка с барьером 0.5с (см. ниже), иначе гонка ProcConnect → 4101 code 5.
	// ★ Р-2 (ревизия shopdbl2, 10.10.2026): ручной вход важнее защёлки офлайн-самотеста — снимаем её с отметкой в журнале.
	if (!Net->OfflineSelfTestLatch.IsEmpty())
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("DoLogin: ручной вход снимает защёлку офлайн-самотеста %s"), *Net->OfflineSelfTestLatch);
		Net->OfflineSelfTestLatch.Reset();
	}
	if (!Net->IsConnected())
	{
		if (!Net->ConnectToLogin(Server.IP, Server.Port))
		{
			UE_LOG(LogAsdaLogin, Warning, TEXT("DoLogin: не удалось подключиться к %s:%d"), *Server.IP, Server.Port);
			// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Сервер недоступен.» = своя строка 990418; окно = нотис 1001
			//   «Не удается подключиться к серверу.» + адрес отдельной строкой (как было).
			SetLoginStatus(FAsdaLoginData::Loc(990418));
			ShowMessageWin(FAsdaLoginData::Loc(1001) + FString::Printf(TEXT("\n%s:%d"), *Server.IP, Server.Port));
			return;
		}
		// ★ Барьер connect→1003 (login_ipnull_fix.md §4): LoginManager пишет IP сессии асинхронно
		//   (ProcConnect по IOCP-событию 20000 в ДРУГОМ потоке). 1003, ушедший сразу после connect,
		//   обгоняет запись → GetUserListenArrayIP=NULL → kick 4101 code 5 «IP не определён». Эталон
		//   разделяет connect (StartPlay) и 1003 секундами загрузки; здесь коннект СВЕЖИЙ (раннего не было /
		//   оборвался) — отправку откладываем.
		GetWorldTimerManager().ClearTimer(LoginSendTimer);
		const FString L = Login, P = Password;
		GetWorldTimerManager().SetTimer(LoginSendTimer, FTimerDelegate::CreateWeakLambda(this, [this, L, P]()
		{
			UGameInstance* GI2 = GetGameInstance();
			UAsdaNetSubsystem* Net2 = GI2 ? GI2->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (Net2 && Net2->IsConnected()) { Net2->SendLogin(L, P); return; }
			UE_LOG(LogAsdaLogin, Warning, TEXT("DoLogin(отложенный): соединение потеряно до отправки 1003"));
			// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Сервер недоступен.» = своя строка 990418; окно = нотис 1001
			//   «Не удается подключиться к серверу.» + адрес отдельной строкой (как было).
			SetLoginStatus(FAsdaLoginData::Loc(990418));
			ShowMessageWin(FAsdaLoginData::Loc(1001) + FString::Printf(TEXT("\n%s:%d"), *Server.IP, Server.Port));
		}), 0.5f, false);
		return;
	}
	Net->SendLogin(Login, Password);
}

// ══════════════ ★★★ СТАРТОВАЯ ЗАСТАВКА Start_logo (задача владельца 2026-08-25) ═══════════════
//   «Сразу отображается start_logo, а за ним грузится login scene; через 3 секунды он растворяется».
//   Заставка — полноэкранный слой поверх всего (порядок 10000, выше любого окна логина). Логин
//   строится ПОД ней в том же кадре, поэтому к моменту растворения он уже готов: переход выходит
//   без паузы и без чёрного кадра.
void AAsdaLoginGameMode::PresentLoginScreen()
{
	// Прогрев UI-атласов через единый текстур-менеджер (кэш+GC-рут внутри подсистемы; браши виджета
	//   держат эти же объекты). Замена разбросанного рутинга UiTextures[] (правило: ресурсы — через систему).
	//   ★ 10.10.2026: ДО выбора языка не зовётся — иначе атласы с надписями оказались бы в кэше прежней культуры.
	if (UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr)
	{
		static const TCHAR* JuiNames[] = {
			TEXT("n_01"), TEXT("n_02"), TEXT("n_03"), TEXT("n_chapter"),
			TEXT("ne_skillbar3"), TEXT("ne_wheeltaget2"), TEXT("ne_booster_center100"),
			TEXT("ne_godrobot01_praise"), TEXT("ne_godrobot02_warning"), TEXT("ne_godrobot03_sation"),
			TEXT("tpl_yellowwin"), TEXT("tpl_talkwin"),
		};
		for (const TCHAR* N : JuiNames)
		{
			TexMgr->LoadUITexture(FName(N));
		}
	}

	// ★ Оригинальный Login.JUI-UI на UMG (WBP_LoginScreen, редактируется в UMG-дизайнере).
	ShowLoginScreenUMG();

	// ★ Музыка логина: с 12.10.2026 стартует в StartPlay (первым делом, под заставкой и окном языка) — здесь не трогаем.
}

static ASDA_DEV_CVAR(float, CVarAsdaStartLogoHoldSec,
	TEXT("asda.Login.StartLogoHoldSec"), 3.f,
	TEXT("Сколько секунд держать стартовую заставку до растворения (0 = не показывать вовсе)."),
	ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaStartLogoFadeSec,
	TEXT("asda.Login.StartLogoFadeSec"), 1.2f,
	TEXT("Длительность растворения стартовой заставки, секунды."),
	ECVF_Default);

// ★ 08.11.2026 (пачка 16, баг 12) [ИСПРАВЛЕНИЕ] Отсчёт выдержки заставки — от ПЕРВОГО ПОКАЗАННОГО КАДРА, а не от StartPlay.
//   ПРИЧИНА. Заставка добавляется во вьюпорт в StartPlay, а первый кадр окна рисуется только после всей остальной сборки
//   логина и первого тика (зонд кадров старта 08.11, -game: StartPlay 46.267 → первая отрисовка 48.522, т. е. 2.25 с; в
//   выпуске при холодном кэше шейдеров — дольше). Часы выдержки шли всё это время: на экране заставка держалась ~1 с,
//   растворение начиналось уже на 3-м кадре, а при задержке больше 3 с первый же кадр показывал сцену сквозь полупрозрачную
//   заставку (при задержке больше 4.2 с заставка снималась, не показавшись ни разу).
//   1 = по умолчанию: часы стартуют в первом тике (он идёт перед первой отрисовкой), а кадры-рывки дольше 0.1 с в выдержку
//   не засчитываются (подгрузка не съедает заставку); 0 = как раньше, от StartPlay по часам.
static ASDA_DEV_CVAR(int32, CVarAsdaStartLogoFromFirstFrame,
	TEXT("asda.Login.StartLogoFromFirstFrame"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Стартовая заставка: 1 (по умолчанию) = выдержка считается от первого показанного кадра, рывки дольше 0.1 с не засчитываются; ")
	TEXT("0 = как раньше, от StartPlay по часам (загрузка съедает выдержку)."),
	ECVF_Default);
static constexpr double kStartLogoMaxCountedDeltaS = 0.1;   // кадр дольше этого — рывок загрузки, в выдержку идёт только 0.1 с

// ★ 08.11.2026 (пачка 16, баг 12) [ИСПРАВЛЕНИЕ] Первая отрисовка окна — уже под заставкой.
//   ПРИЧИНА ВСПЫШКИ. Самая первая отрисовка окна (движок рисует вьюпорт сразу после BeginPlay, ещё до первого тика) шла с
//   картинкой заставки НУЛЕВОГО размера: LayoutStartLogo в StartPlay берёт размер вьюпорта, а он в этот момент ещё не
//   выставлен; правильный размер ставил только первый тик. Кадр зонда `boot_first.png` (08.11): сцена логина с окном входа
//   и без заставки, хотя виджет заставки уже во вьюпорте с непрозрачностью 1.
//   1 = по умолчанию: (а) пока размер вьюпорта неизвестен, раскладка берёт клиентскую область окна игры, а без неё — размер
//   основного дисплея; (б) под картинкой — чёрная подложка на весь экран (анкоры 0..1, без размеров), поэтому сцена не видна,
//   даже если раскладка опоздает на кадр. Растворяется вместе с заставкой. 0 = как раньше.
static ASDA_DEV_CVAR(int32, CVarAsdaStartLogoCoverFirstDraw,
	TEXT("asda.Login.StartLogoCoverFirstDraw"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Стартовая заставка с первой отрисовки окна: 1 (по умолчанию) = запасной размер из окна/дисплея и чёрная подложка на весь экран; ")
	TEXT("0 = как раньше (первая отрисовка до тика показывала сцену логина без заставки)."),
	ECVF_Default);

// Пустой concrete-виджет (тот же, что у загрузочного экрана: в дизайне только корневой канвас,
//   содержимое собирается кодом) и сама текстура заставки.
static const TCHAR* GStartLogoWidgetClass = TEXT("/Game/AsdaAssets/UI/Dev/WBP_LoadingScreen.WBP_LoadingScreen_C");
static const TCHAR* GStartLogoTexture     = AsdaLoadScreen::kStartLogoTex;   // ★ В1-C F16: одна строка с загрузчиком

void AAsdaLoginGameMode::ShowStartLogo()
{
	if (CVarAsdaStartLogoHoldSec.GetValueOnGameThread() <= 0.f) { return; }
	if (StartLogoWidget) { return; }
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	UClass* WC = LoadClass<UUserWidget>(nullptr, GStartLogoWidgetClass);
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, GStartLogoTexture);
	if (!WC || !Tex)
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("[заставка] не найден %s — показ пропущен"),
			WC ? GStartLogoTexture : GStartLogoWidgetClass);
		return;
	}
	StartLogoWidget = CreateWidget<UUserWidget>(GI, WC);
	if (!StartLogoWidget || !StartLogoWidget->WidgetTree) { StartLogoWidget = nullptr; return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(StartLogoWidget->WidgetTree->RootWidget);
	if (!Root) { StartLogoWidget = nullptr; return; }

	// ★ 08.11.2026 (баг 12, ключ asda.Login.StartLogoCoverFirstDraw): чёрная подложка на весь экран ПОД картинкой — растягивается
	//   анкорами 0..1, размеры ей не нужны, поэтому закрывает сцену уже в первой отрисовке окна.
	if (CVarAsdaStartLogoCoverFirstDraw.GetValueOnGameThread() != 0)
	{
		UImage* Backing = StartLogoWidget->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("AsdaStartLogoBacking"));
		Backing->SetColorAndOpacity(FLinearColor::Black);   // кисть без текстуры = сплошной прямоугольник цвета
		Backing->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* BS = Cast<UCanvasPanelSlot>(Root->AddChild(Backing)))
		{
			BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			BS->SetOffsets(FMargin(0.f));
			BS->SetZOrder(99);   // под картинкой (у неё 100)
		}
	}
	StartLogoImage = StartLogoWidget->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("AsdaStartLogo"));

	// ★★★ ЖДЁМ ПОЛНОЕ КАЧЕСТВО ДО ПОКАЗА. Заставка — первое, что видит игрок, и подкачке мипов
	//   тут просто нет времени: она бы догружалась уже поверх видимой картинки («мыло на пару
	//   секунд»). Просим все уровни детализации и ДОЖИДАЕМСЯ их — это одна из немногих точек, где
	//   блокировка уместна: экран всё равно статичен.
	Tex->bForceMiplevelsToBeResident = true;
	Tex->bGlobalForceMipLevelsToBeResident = true;
	Tex->SetForceMipLevelsToBeResident(60.f);
#if WITH_EDITOR
	// ★ 08.11.2026 (баг 12, ключ asda.Login.StartLogoCoverFirstDraw): в редакторской сборке текстура может ещё собираться
	//   асинхронно — первая отрисовка показала бы серую заглушку движка (кадр зонда boot_after_ru/boot_first.png до правки).
	//   В выпуске текстуры приготовлены заранее, ветки нет.
	if (CVarAsdaStartLogoCoverFirstDraw.GetValueOnGameThread() != 0) { FTextureCompilingManager::Get().FinishCompilation({ Tex }); }
#endif
	Tex->WaitForStreaming();

	// ★★★ БЕЗ МАТЕРИАЛА ЗАГРУЗОЧНОГО ЭКРАНА. Тот обрезает нижнюю четверть (у картинок загрузки там
	//   белый паддинг) — но у `Start_logo` паддинга НЕТ, изображение занимает все 2048×2048
	//   (проверено по пикселям). Обрезка съедала низ и «зумила» кадр — отсюда сбитый центр.
	FSlateBrush B;
	B.SetResourceObject(Tex);
	B.ImageSize = FVector2D((float)Tex->GetSizeX(), (float)Tex->GetSizeY());
	B.DrawAs = ESlateBrushDrawType::Image;
	StartLogoImage->SetBrush(B);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(StartLogoImage)))
	{
		// ★★★ ПО ЦЕНТРУ И БЕЗ ИСКАЖЕНИЙ. Картинка квадратная (2048×2048), экран широкий: растянуть
		//   её на весь слот значило бы расплющить эмблему в овал. Привязываем к центру экрана и
		//   задаём размер сами — «заполнить, сохранив пропорции»: масштаб по БОЛЬШЕЙ из сторон,
		//   лишнее уходит за края поровну сверху и снизу. Пересчёт — в тике (окно можно тянуть).
		S->SetAnchors(FAnchors(0.5f, 0.5f));
		S->SetAlignment(FVector2D(0.5f, 0.5f));
		S->SetPosition(FVector2D::ZeroVector);
		S->SetZOrder(100);
	}
	// ★ Обрезаем то, что вышло за экран: без этого заполнение по большей стороне вылезло бы
	//   за пределы окна и Slate нарисовал бы его поверх соседних слоёв.
	Root->SetClipping(EWidgetClipping::ClipToBounds);
	LayoutStartLogo();
	// ★ Заставка не должна перехватывать мышь: под ней уже живой экран логина, и клик по кнопке
	//   в последние миллисекунды растворения обязан дойти до него.
	StartLogoImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	StartLogoWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	StartLogoWidget->AddToViewport(10000);   // выше всех окон логина
	// ★ 08.11.2026 (баг 12): при ключе asda.Login.StartLogoFromFirstFrame часы стартуют в первом тике (метка −1 = «ещё не было тика»).
	StartLogoShownAtS = CVarAsdaStartLogoFromFirstFrame.GetValueOnGameThread() != 0 ? -1.0 : FPlatformTime::Seconds();
	UE_LOG(LogAsdaLogin, Log, TEXT("[заставка] Start_logo показан (выдержка %.1f с, растворение %.1f с)"),
		CVarAsdaStartLogoHoldSec.GetValueOnGameThread(), CVarAsdaStartLogoFadeSec.GetValueOnGameThread());
}

// Разложить заставку по текущему размеру окна: заполнить экран, сохранив пропорции, по центру.
void AAsdaLoginGameMode::LayoutStartLogo()
{
	if (!StartLogoImage) { return; }
	UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(StartLogoImage->Slot);
	if (!S) { return; }
	const FVector2D Src = StartLogoImage->GetBrush().ImageSize;
	if (Src.X <= 0.f || Src.Y <= 0.f) { return; }
	FVector2D View(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->GetViewportSize(View); }
	// ★ 08.11.2026 (баг 12, ключ asda.Login.StartLogoCoverFirstDraw): в StartPlay размер вьюпорта ещё не выставлен (0) — картинка
	//   получала нулевой размер, и первая отрисовка окна шла без заставки. Запасной размер: клиентская область окна, затем дисплей.
	if (CVarAsdaStartLogoCoverFirstDraw.GetValueOnGameThread() != 0 && (View.X < 2.f || View.Y < 2.f))
	{
		const FVector2D Was = View;
		TSharedPtr<SWindow> Win = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetWindow() : nullptr;
		if (Win.IsValid()) { View = Win->GetClientSizeInScreen(); }
		if ((View.X < 2.f || View.Y < 2.f) && FSlateApplication::IsInitialized())
		{
			FDisplayMetrics DM;
			FSlateApplication::Get().GetDisplayMetrics(DM);
			View = FVector2D((float)DM.PrimaryDisplayWidth, (float)DM.PrimaryDisplayHeight);
		}
		UE_LOG(LogAsdaLogin, Log, TEXT("[заставка] размер вьюпорта ещё не выставлен (%s) — раскладка по %s"), *Was.ToString(), *View.ToString());
	}
	// Масштаб виджета не равен пикселям окна: канвас живёт в единицах UMG, поэтому делим на DPI.
	float Dpi = 1.f;
	if (GEngine && GEngine->GameViewport)
	{
		Dpi = UWidgetLayoutLibrary::GetViewportScale(GEngine->GameViewport);
		if (Dpi <= 0.f) { Dpi = 1.f; }
	}
	const FVector2D ViewU = View / Dpi;
	const float Scale = FMath::Max(ViewU.X / Src.X, ViewU.Y / Src.Y);   // «заполнить», не «вписать»
	S->SetSize(Src * Scale);
}

void AAsdaLoginGameMode::TickStartLogo(float DeltaSeconds)
{
	if (!StartLogoWidget) { return; }
	LayoutStartLogo();   // окно могли растянуть — держим заполнение и центр
	// ★ 08.11.2026 (баг 12, ключ asda.Login.StartLogoFromFirstFrame): первый тик — точка отсчёта (кадр сразу за ним — первый
	//   показанный); рывок дольше kStartLogoMaxCountedDeltaS сдвигает отсчёт вперёд на лишнее — выдержку видно целиком.
	if (CVarAsdaStartLogoFromFirstFrame.GetValueOnGameThread() != 0)
	{
		const double Now = FPlatformTime::Seconds();
		if (StartLogoShownAtS < 0.0)
		{
			StartLogoShownAtS = Now;
			UE_LOG(LogAsdaLogin, Log, TEXT("[заставка] отсчёт выдержки от первого кадра"));
		}
		else if ((double)DeltaSeconds > kStartLogoMaxCountedDeltaS)
		{
			StartLogoShownAtS += (double)DeltaSeconds - kStartLogoMaxCountedDeltaS;
		}
	}
	const double Elapsed = FPlatformTime::Seconds() - StartLogoShownAtS;
	const float  Hold    = CVarAsdaStartLogoHoldSec.GetValueOnGameThread();
	const float  Fade    = FMath::Max(0.01f, CVarAsdaStartLogoFadeSec.GetValueOnGameThread());
	if (Elapsed < (double)Hold) { return; }                 // выдержка: держим полностью непрозрачной

	const float T = FMath::Clamp((float)((Elapsed - (double)Hold) / (double)Fade), 0.f, 1.f);
	if (T >= 1.f)
	{
		StartLogoWidget->RemoveFromParent();
		StartLogoWidget = nullptr;
		StartLogoImage  = nullptr;
		// ★ 12.10.2026 (владелец): окно первого выбора языка — ТОЛЬКО теперь, когда заставка снята целиком (раньше оно
		//   открывалось в StartPlay и просвечивало сквозь растворение). Экран логина был отложен в StartPlay; если окно
		//   не загрузилось — показываем его запасным путём, иначе игрок остался бы перед пустой сценой.
		if (bLangSelectAfterStartLogo)
		{
			bLangSelectAfterStartLogo = false;
			UAsdaLangSelectSubsystem* LangSel = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLangSelectSubsystem>() : nullptr;
			const bool bShown = LangSel && LangSel->ShowFirstRunIfNeeded();
			UE_LOG(LogAsdaLogin, Log, TEXT("[заставка] Start_logo снят, %s"),
				bShown ? TEXT("открыто окно первого выбора языка; экран логина — после выбора") : TEXT("окно выбора языка не открылось — экран логина показан запасным путём"));
			if (!bShown) { PresentLoginScreen(); }
			return;
		}
		// ★ 12.10.2026: замер графики первого запуска — после заставки (см. StartPlay). Не запустился — экран логина.
		if (bBenchAfterStartLogo)
		{
			bBenchAfterStartLogo = false;
			UAsdaBenchSubsystem* Bench = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaBenchSubsystem>() : nullptr;
			const bool bStarted = Bench && Bench->StartFirstRun(GetWorld());
			if (bStarted) { bEarlyConnectPending = false; }   // ревизия КРИТ-1: ранний коннект не поднимать (см. StartPlay)
			UE_LOG(LogAsdaLogin, Log, TEXT("[заставка] Start_logo снят, %s"),
				bStarted ? TEXT("запущен замер графики первого запуска (уход на карту замера)") : TEXT("замер графики не запустился — экран логина показан"));
			if (!bStarted) { PresentLoginScreen(); }
			return;
		}
		UE_LOG(LogAsdaLogin, Log, TEXT("[заставка] Start_logo снят, экран логина открыт"));
		return;
	}
	// ★ Мягкая кривая вместо прямой: у линейного растворения заметны начало и конец, а нужен
	//   переход «без резких мест» (просьба владельца). SmoothStep даёт нулевую скорость на обоих
	//   концах — заставка трогается с места и останавливается незаметно.
	StartLogoWidget->SetRenderOpacity(1.f - FMath::SmoothStep(0.f, 1.f, T));
}
