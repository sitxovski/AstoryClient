// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginGameMode_Login.cpp — ★ R3-c 12.11.2026 (разрез AsdaLoginGameMode.cpp, дословный перенос): экран входа: вход в мир, результат логина, UMG, помощник, сообщения, кик, режим XyT.
//   Общее для частей — в AsdaLoginGameMode_Private.h; шапка и реверс — в AsdaLoginGameMode.cpp.
#include "Login/AsdaLoginGameMode.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "Resources/AsdaSoundSubsystem.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaAdminSubsystem.h"   // сброс прав GM при возврате в меню логина
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"
#include "Login/AsdaServerAddress.h"   // ★ 16.10.2026: домен сервера
#include "AsdaConnection.h"           // ★ 16.10.2026: FAsdaConnection::PrefetchHost — фоновый резолв домена заранее
#include "Login/AsdaText.h"          // FAsdaText::Strip (срез JUI-разметки)
#include "Text/AsdaTextSubsystem.h"  // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FormatPrintf — %d в строках DT_GameText
#include "UI/AsdaMarkup.h"           // UAsdaRichText::ApplyFormatted — единая точка форматированного текста
#include "UI/AsdaTipPanel.h"         // UAsdaTipPanel::CountWrappedLines — единственный счётчик строк с переносом
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
// ★ Аудит №4 (A8): `Login/AsdaGameText.h` больше не нужен — строка помощника теперь
//   `FAsdaLoginhelpertextRow` из `Items/AsdaItemDbRows.h` (тянется через AsdaItemTableSubsystem.h).
#include "Engine/DataTable.h"
#include "Components/ComboBoxString.h"  // созвездие CARAMAKE_cb
#include "Blueprint/WidgetTree.h"    // GetAllWidgets — поиск план-виджетов по префиксу (имена с idx-суффиксом)
#include "MovieSceneSequencePlaybackSettings.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"          // ICONBOX-иконки сетки создания
#include "Blueprint/WidgetTree.h"
#include "Components/EditableText.h"
#include "Components/RichTextBlock.h"   // [САМОТЕСТ] 08.10.2026: чтение пузыря ANI_TXT в asda.Login.CreateNameTest
#include "AsdaOutgoingGate.h"           // [САМОТЕСТ] 08.10.2026: офлайн-гейт asda.Login.CreateNameTest
// ── Ctrl+N-пикер разрешения/качества (dev) ──
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"       // FWidgetPath::ContainsWidget — маппинг focus-события на наш контрол
#include "Components/VerticalBoxSlot.h"
#include "Components/CheckBox.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "AsdaNetSubsystem.h"
#include "Net/ChannelCipher.h"   // ★ профиль ключа канального шифра (наш / XyT)
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Components/Image.h"
#include "UnrealClient.h"                 // FScreenshotRequest (движковый захват вместо RDP CopyFromScreen)
#include "Styling/SlateBrush.h"       // FSlateBrush — рантайм-фикс рамки пузыря
#include "GameFramework/PlayerController.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Engine/BlueprintGeneratedClass.h"    // ★ 31.10.2026: признак Blueprint-наследника света
#include "World/AsdaPlayerCharacter.h" // ★ общая точка kAsdaCharPreviewBaseM/F (база пути превью-скелета)
#include "World/AsdaLoadManagerSubsystem.h" // ★ charsel_preload: фоновая предзагрузка превью экрана выбора
#include "PipelineStateCache.h"             // ★ charsel_preload: IsPSOPrecachingEnabled — прогрев PSO только где он есть
#include "AsdaLoginGameModeInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaLoginGameMode*.cpp (LogAsdaLogin, чистые хелперы)
#include "AsdaLoginGameMode_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaLoginGameMode*.cpp

void AAsdaLoginGameMode::OnEnterWorld()
{
	// ★ ФАЗА A4: 1021 получен → уходим из логин-сцены в загрузку мира. Скрываем логин-UI/сцену; сетевой субсистем
	//   параллельно (след. тик) коннектится к game-серверу и шлёт enter-хендшейк 20000. Полноэкранный
	//   ПЕР-КАРТА загрузочный экран (эталон CMap_LoadLoadingScreen: map\<карта>\load\*.* рандом) — доработка ФАЗЫ B.
	int32 MapId = -1;
	if (UGameInstance* GI = GetGameInstance())
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>()) MapId = Net->EnterMapId;
	UE_LOG(LogAsdaLogin, Log, TEXT("Enter-world (A4): скрываю логин-сцену, целевая карта mapId=%d; жду world-state game-сервера (ФАЗА B/C)"), MapId);
	HideLoginScene();
	ReleaseCharSelectPreload();   // ★ charsel_preload: вход в мир — предзагрузку превью отпускаем
	// ★★★ ОБЯЗАТЕЛЬНО снять ловец кликов по 3D-сцене: он полноэкранный и ЛОВИТ МЫШЬ (`Visible`).
	//   Если он переживёт уход в мир (виджеты умирают вместе с уровнем, но между 1021 и травелом
	//   проходит загрузка), он съест все клики игрока. Панель 114 гасим по той же причине.
	ShowSoulInfoPanel(false, -1);
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(TEXT("CHARSELECT_PICK")), false);
	}
	// TODO(ФАЗА B): загрузочный экран целевой карты (mapId) + прогресс загрузки уровня + приём world-state (4055).
}

void AAsdaLoginGameMode::OnLoginResult(uint8 Status)
{
	// ★ ШТАТНЫЙ флоу (эталон Net_RecvLoginResponse, switch по статусу): успех=1 → server-select; ИНАЧЕ —
	//   показать НОТИС и ОСТАТЬСЯ на логине (пользователь входит СНОВА вручную). НИКАКОГО авто-ретрая:
	//   при overlap(4) повторный ручной вход по ТОМУ ЖЕ соединению проходит (сервер уже кикнул старую сессию).
	//   Авто-ретрай создавал дубль-сессию → сервер кикал 4101 code 02 после выбора сервера (симптом
	//   «канал не выбирается»: char-list уходил в мёртвый сокет). Тексты = login-нотисы клиента (100/101/102/117/118/119).
	if (Status != 1)
	{
		// ★ Если УЖЕ вошли (server/char-select) — запоздалый мусорный логин-ошибочный пакет (стрея сессия)
		//   НЕ должен всплывать логином поверх char-select. Игнорируем, только логируем.
		if (bLoggedIn)
		{
			UE_LOG(LogAsdaLogin, Warning, TEXT("Пост-логин отказ status=%u проигнорирован (уже в server/char-select)"), Status);
			return;
		}
		const FString Msg = BuildLoginErrorText(Status);
		UE_LOG(LogAsdaLogin, Warning, TEXT("Логин отклонён (status=%u) → подсказка Минопио + возврат на логин"), Status);
		// ★ Подсказка помощника по статусу ответа (HELPER_HINT_CASES_RE.md §1.3, эталон Net_RecvLoginResponse):
		//   status2 → rec8 (ID не существует), status3/0x66 → rec9 (неверный пароль), status4/6/7/8/9/10 → rec12
		//   («под этой учётной записью уже кто-то вошёл»). Эти записи = ГОТОВЫЙ текст ответа → показываем ИХ (не наш
		//   Msg). Для кодов без записи (kick 4101, Status=200+код) — наш Msg через SetLoginStatus.
		const int32 HelperRec =
			(Status == 2)                                ? 8  :
			(Status == 3 || Status == 0x66)              ? 9  :
			(Status == 4 || Status == 6 || Status == 7 ||
			 Status == 8 || Status == 9 || Status == 10) ? 12 : -1;
		if (LoginWidget) { LoginWidget->SetVisibility(ESlateVisibility::Visible); }
		FocusUI(LoginWidget);
		// ★ ВАЖНО: ставим подсказку ПОСЛЕ FocusUI — иначе смена фокуса (OnUIFocusChanging) перебила бы её.
		if (HelperRec >= 0) ApplyHelper(HelperRec);
		else               SetLoginStatus(Msg);
		// ★ Модальное игровое окно ошибки (эталон UI_ShowLocalizedNotice@0xF17280: любой отказ логина =
		//   MESSAGE_WIN с нотисом + OK). Показываем ПОСЛЕ подсказки помощника — модалка поверх (z=20, bModal).
		ShowMessageWin(Msg);
		return;
	}
	SetLoginStatus(FString());   // очистить статус при успехе
	bLoggedIn = true;            // ★ дальше логин-ошибки не всплывают поверх server/char-select
	// ★ charsel_preload: сразу после авторизации фоном грузим базу превью обоих полов (данные персонажей
	//   придут позже, со списком — тогда набор дополнится их внешностью).
	RequestCharSelectPreload();
	// ★ Эталон (Net_RecvLoginResponse status==1): скрыть окно логина + показать server/channel-select +
	//   запросить список каналов у сервера (netChannel_SendSelect1012 → ответ 1013). Шкатулка НЕ открывается
	//   тут — только после выбора канала → приёма char-list (OnCharListReady).
	UE_LOG(LogAsdaLogin, Log, TEXT("Логин OK → скрыть логин, показать СПИСОК КАНАЛОВ (сервер выбирать не нужно)"));
	if (LoginWidget) { LoginWidget->SetVisibility(ESlateVisibility::Collapsed); }
	// ★ Флоу (подтверждён пользователем): логин → СРАЗУ каналы → клик канала → персонажи. Сервер пользователь
	//   НЕ выбирает — сервер сам присылает server-инфо после авторизации. Экран/шаг «выбор сервера» убран:
	//   показываем канал-вид напрямую. server-select (1012) НЕ шлём (прежний авто/ручной 1012 провоцировал
	//   4101-кик и рвал сессию). Клик канала → char-list (1005) с корректным анти-реплей-секретом.
	ShowServerSelect();               // создаёт виджет (серверы+каналы в одном WBP)
	ShowServerOrChannelView(true);    // сразу канал-вид, а не список серверов
	// ★ СПИСОК КАНАЛОВ БЕРЁМ У СЕРВЕРА, а не из дефолта. Без этого запроса 1013 не приходит,
	//   ChannelOnline остаётся пустым и NumRealChannels висит на 1 — виден один канал, а второй
	//   мигал на кадр при клике (тогда 1012 уходил впервые и 1013 успевал перерисовать вид).
	//   1012 с индексом сервера = запрос счётчиков каналов (эталон netChannel_SendSelect1012).
	//   Показ не блокируем: вид уже на экране, а OnChannelOnlineUpdated обновит его по приходу 1013.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			const int32 Srv = FMath::Max(0, SelectedServer);   // наш сервер = слот 0
			Net->SendChannelSelect((uint8)Srv);
			UE_LOG(LogAsdaLogin, Log, TEXT("Запрошен список каналов (1012, сервер=%d) -> ждём 1013"), Srv);
		}
	}
	// ★ ВРЕМЕННО (проверка фикса канала без дисплея): ASDA_TEST_AUTOSELECT=1 → авто-клик канала через 2с
	//   (шлёт 1005). Цель — увидеть в логе, приходит ли char-list 1006 (MD5-фикс) вместо кика.
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_TEST_AUTOSELECT")) == TEXT("1"))
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this]() { OnChannelSelected(0); }, 2.0f, false);
	}
}

void AAsdaLoginGameMode::ShowLoginScreenUMG()
{
	if (!GetWorld()) return;
	CurrentScreen = EAsdaScreen::Login;
	if (LoginWidget) { LoginWidget->SetVisibility(ESlateVisibility::Visible); return; }
	// ★ Игровое JUI-окно LOGIN_WIN (генерится конвертером из login.JUI: игровой шрифт+раскладка+
	//   интерактив-виджеты по idc). Замена кастомного WBP_LoginScreen.
	UClass* WbpClass = LoadClass<UUserWidget>(nullptr,
		TEXT("/Game/AsdaAssets/UI/Login/WBP_LOGIN_WIN.WBP_LOGIN_WIN_C"));
	if (!WbpClass) { UE_LOG(LogAsdaLogin, Warning, TEXT("WBP_LOGIN_WIN не загружен")); return; }
	LoginWidget = CreateWidget<UUserWidget>(GetWorld(), WbpClass);
	if (!LoginWidget) return;

	auto SetTxt = [&](const TCHAR* N, const FString& S)
	{
		if (UTextBlock* T = Cast<UTextBlock>(LoginWidget->GetWidgetFromName(FName(N))))
			T->SetText(FText::FromString(S));
	};
	auto BindBtn = [&](const FString& N, TFunction<void(int32)> Cb)
	{
		if (UButton* B = Cast<UButton>(LoginWidget->GetWidgetFromName(FName(*N))))
		{
			AsdaUi::BindButton(LoginWidget, B, 0, MoveTemp(Cb), ButtonRelays);   // ★ U20: реле клика — общая точка
		}
	};

	// ★ ПОДПИСИ кнопок/чекбокса — из ОРИГИНАЛЬНОЙ loc-таблицы error list.bin по string-ID (login_flow_re.md,
	//   UI_FormatLocalizedNotice/GetLocalizedString): LOGIN_BUT00=loc1201, BUT01=loc1202, BUT02=loc1203,
	//   LOGIN_CHECK_TXT=loc116. НЕ выдумывать текст (правило never-invent) — грузим из таблицы.
	auto SetLoc = [&](const TCHAR* Widget, int32 LocId)
	{
		const FString T = FAsdaLoginData::Loc(LocId);   // из ассета DT_GameText (импорт error list.bin)
		if (!T.IsEmpty()) SetTxt(Widget, T);
	};
	SetLoc(TEXT("LOGIN_BUT00_Text"), 1201);
	SetLoc(TEXT("LOGIN_BUT01_Text"), 1202);
	SetLoc(TEXT("LOGIN_BUT02_Text"), 1203);
	SetLoc(TEXT("LOGIN_CHECK_TXT"), 116);

	// Сохранённый ID → поле LOGIN_EDIT00 + состояние чекбокса.
	FString SavedId; bool bSaved = false;
	FAsdaLoginData::LoadSavedId(bSaved, SavedId);
	if (!SavedId.IsEmpty())
	{
		if (UEditableText* E = Cast<UEditableText>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_EDIT00"))))
			E->SetText(FText::FromString(SavedId));
	}
	bSaveIdChecked = bSaved;

	// ★ 16.10.2026: поле SERVER_IP_INPUT (группа Server_IP) показывает домен сервера — только текст.
	FillServerAddrField();

	// ★ Игровой флоу (login_flow_re.md): ЛОГИН = ФАЗА 1 = LOGIN_CONFORM (id4, Log_OkBtn) → Send_LoginPacket_1
	//   (opcode 1003). LOGIN_BUT00 (id5) = ФАЗА 2 (вход на сервер, opcode 1004, гейт if(+405) — только ПОСЛЕ
	//   аутентификации), у нас закрывается флоу server-select → НЕ вешаем на него auth. LOGIN_BUT01/02 =
	//   URL регистрации/сброса пароля (case6/7). Реального «выхода» на login-экране нет (выход = закрытие окна).
	BindBtn(TEXT("LOGIN_CONFORM"), [this](int32){ DoLoginFromWidget(); });               // фаза 1: аутентификация
	// ★ 27.10.2026 (владелец): «Создать аккаунт» / «Забыли пароль» пока НИЧЕГО не открывают — ссылки на area-game.ru
	//   (оригинал: sign-up / password-reset-methods) сняты до появления своего сайта. Кнопки остаются привязанными (нажатие
	//   поглощается), чтобы не провалиться в обработку по умолчанию.
	BindBtn(TEXT("LOGIN_BUT01"),   [](int32){ });
	BindBtn(TEXT("LOGIN_BUT02"),   [](int32){ });
	BindBtn(TEXT("LOGIN_BUT00"),   [this](int32){ if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr) PC->ConsoleCommand(TEXT("quit")); });   // ВЫХОД (эталон loginWinOnEvent id5=Выход), логика как CARASTART_BUT03

	// Enter в поле пароля → логин; поле пароля = маскированное (конвертер создаёт немаскированным).
	if (UEditableText* Pw = Cast<UEditableText>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_EDIT01"))))
	{
		Pw->SetIsPassword(true);
		Pw->OnTextCommitted.AddDynamic(this, &AAsdaLoginGameMode::OnPasswordCommitted);
	}
	// Чекбокс «сохранить ID» (LOGIN_CHECK, скин Log_Box_Check: SS0 пустой бокс / SS2 галочка).
	//   ★ Клик по самому UCheckBox (18×19) не проходил — сверху рисуются LOGIN_CHECK_BASE/_TXT. Надёжно:
	//   сам чекбокс = ВИЗУАЛ (HitTestInvisible), а клики ловит прозрачная кнопка на ВСЮ строку LOGIN_CHECK_BASE
	//   (в оригинале кликается вся строка). Тоггл → SetIsChecked (свап кадра) + персист флага.
	if (UCheckBox* Cb = Cast<UCheckBox>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_CHECK"))))
	{
		Cb->SetIsChecked(bSaved);
		Cb->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	// ★ Клик по ВСЕЙ строке чекбокса → тоггл (как в оригинале кликается вся строка).
	//   ★★★ САМА КНОПКА ПЕРЕЕХАЛА В АССЕТ (2026-08-27, требование «править в ассете, а не кодом»).
	//   ЧТО БЫЛО ЗДЕСЬ. Обход дерева в поисках плана-строки `LOGIN_CHECK_BASE*` (имя в WBP несёт
	//   idx-суффикс `LOGIN_CHECK_BASE_18_0`, поэтому точный `GetWidgetFromName` возвращал null),
	//   конструирование `UButton` с прозрачным стилем и копирование его слота 1:1 у плана.
	//   ПОЧЕМУ УБРАНО. Копия чужого слота — статика: план `LOGIN_CHECK_BASE_18_0` стоит в группе
	//   `LOGIN_CHECK_BASE_grp` на off(0, 4, 100, 21) и никогда не двигается.
	//   ГДЕ ЖИВЁТ ТЕПЕРЬ. Узел `LOGIN_CHECK_HIT` лежит в `WBP_LOGIN_WIN` (z=100); создаёт и
	//   восстанавливает его шаг `Tools/asda_ui_geometry_bake.py` (блок G2), он в `STEPS`
	//   пост-импорта, а имя узла — в `HANDMADE`, поэтому потеря узла роняет прогон.
	if (UButton* Hit = Cast<UButton>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_CHECK_HIT"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32){ ToggleSaveId(); };
		Hit->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(ButtonRelays, R, Hit);
	}
	else
	{
		UE_LOG(LogAsdaLogin, Warning,
			TEXT("[логин] LOGIN_CHECK_HIT не найден в WBP_LOGIN_WIN — клик по строке «сохранить ID» "
			     "не работает; прогони Tools/asda_ui_geometry_bake.py (шаг G2)"));
	}

	// ★ Режим «XyT» (подключение к чужой сборке) — СКРЫТ и всегда ВЫКЛЮЧЕН (решение владельца
	//   2026-08-31). Переключатель на логин-скрине не строим, состояние из настроек не читаем;
	//   принудительно держим bXytMode=false и применяем «наш» профиль ключа/адрес.
	//   Логика (BuildXytToggle/ToggleXytMode/ApplyXytMode) оставлена в коде на случай возврата —
	//   вернуть режим = раскомментировать три строки ниже.
	bXytMode = false;
	// if (GConfig) { GConfig->GetBool(TEXT("AsdaLogin"), TEXT("XytMode"), bXytMode, GGameUserSettingsIni); }
	// BuildXytToggle();
	ApplyXytMode();

	LoginWidget->AddToViewport(10);
	// Регистрируем LOGIN_WIN в оконном менеджере (интерактив, z=10) — чтобы находить/скрывать через мгр, как оверлеи.
	if (UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WinMgr->RegisterWindow(FName(TEXT("LOGIN_WIN")), LoginWidget, 10);
	}
	AddLoginSceneOverlays();   // ★ ПОЛНАЯ сцена: лого/аним/версия/quit-рамка/слот-номер (не только LOGIN_WIN)

	// ★ Подсказки помощника «Минопио» (HELPER_HINT_CASES_RE.md §1.1). Поля ID/PW → фокус; кнопки → наведение.
	//   Записи по эталону loginWinOnEvent: ID→1, PW→2, чекбокс→6, OK→7, «нов.уч»(BUT01)→4, «смена пароля»(BUT02)→5,
	//   «Выход»(BUT00)→3. LOGIN_BUT00 = кнопка ВЫХОДА из игры (quit), забинжена выше рядом с CONFORM/BUT01/BUT02.

	HelperFocusFields.Reset();
	CurrentHelperDefault = 0;                                    // логин: уход фокуса → приветствие (rec0)
	AddHelperFocusField(LoginWidget, TEXT("LOGIN_EDIT00"), 1);   // поле ID
	AddHelperFocusField(LoginWidget, TEXT("LOGIN_EDIT01"), 2);   // поле пароля
	RegisterHelperFocus();
	BindHelperHover(LoginWidget, TEXT("LOGIN_CONFORM"), 7);      // кнопка OK/вход
	BindHelperHover(LoginWidget, TEXT("LOGIN_BUT01"),   4);      // новая учётная запись
	BindHelperHover(LoginWidget, TEXT("LOGIN_BUT02"),   5);      // смена пароля
	BindHelperHover(LoginWidget, TEXT("LOGIN_CHECK_HIT"), 6);    // наведение на строку «сохранить ID»
	BindHelperHover(LoginWidget, TEXT("LOGIN_BUT00"),     3);      // кнопка «Выход» (эталон §1.1 rec3)

	ApplyHelper(0);            // приветствие Минопио в ANI_TXT + god-robot praise (эталон game_main.c:12756, запись 0)
	FocusUI(LoginWidget);
}

// Дефолт-видимые окна login-сцены (login_flow_re.md). GRAD_BG_FULL пропущен — фон даёт 3D-задник.
// Порядок = z (задник-аним ниже, лого/версия/quit выше; LOGIN_WIN=10 сверху). Визуал, клики сквозь к LOGIN_WIN.
static const struct { const TCHAR* Wbp; int32 Z; } GLoginSceneWindows[] = {
	{ TEXT("WBP_ANI_WIN"), 3 },
	// ★ WBP_MM_LOGO УБРАН: в login_jui.ini MM_LOGO (118) имеет Visible="0" (скрыт в оригинале). Видимый логотип
	//   даёт CHATER_X (120, Visible=1, скин Log_MicMacLogo + дочерний текст «Chapter X»). Показ обоих = дубль
	//   (их Y-анкоры 30 vs 60 → ghost-смещение). CHATER_X открывается ниже как часть сцены.
	{ TEXT("WBP_VER_TEXT"), 6 },
	{ TEXT("WBP_CharacterSlot_Num"), 6 },
	{ TEXT("WBP_CHATER_X"), 6 },
	{ TEXT("WBP_QuitBtn_Base"), 7 },
};

void AAsdaLoginGameMode::AddLoginSceneOverlays()
{
	// Через единый оконный менеджер (реестр по id, z-order) — замена ручного LoadClass+CreateWidget+AddToViewport.
	UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WinMgr) return;
	if (WinMgr->IsOpen(FName(GLoginSceneWindows[0].Wbp))) return;   // уже собрано
	for (const auto& E : GLoginSceneWindows)
	{
		const FString Path = FString::Printf(TEXT("/Game/AsdaAssets/UI/Login/%s"), E.Wbp);
		WinMgr->OpenWindowByPath(FName(E.Wbp), Path, E.Z, ESlateVisibility::SelfHitTestInvisible);
	}
}

void AAsdaLoginGameMode::HideLoginScene()
{
	if (LoginWidget) LoginWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ★ Помощник «Минопио» (WBP_ANI_WIN) НЕ скрываем — god-robot виден на всех экранах логин-флоу
		//   (логин/выбор/создание, HELPER_HINT_CASES_RE.md). Прочие login-оверлеи (лого/версия/quit) — прячем.
		for (const auto& E : GLoginSceneWindows)
		{
			if (FCString::Strcmp(E.Wbp, TEXT("WBP_ANI_WIN")) == 0) continue;
			WinMgr->SetWindowVisible(FName(E.Wbp), false);
		}
	}
}

void AAsdaLoginGameMode::DoLoginFromWidget()
{
	if (!LoginWidget) return;
	FString Id, Pw;
	if (UEditableText* E = Cast<UEditableText>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_EDIT00"))))
		Id = E->GetText().ToString();
	if (UEditableText* E = Cast<UEditableText>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_EDIT01"))))
		Pw = E->GetText().ToString();
	// ★ [ОРИГИНАЛ] 12.10.2026 (ревизия античита А0/А2): эталон `Send_LoginPacket_1@0xF154F0` прогоняет ID и пароль через
	//   `TrimCredential@0xF153C0` — снимаются ТОЛЬКО ВЕДУЩИЕ пробелы (0x20), хвостовые остаются, — и лишь потом проверяет
	//   пустоту. Порт отправлял поле как есть: « sitxovski» уходил с пробелом, и серверная проверка набора символов P51
	//   (`UPUserLogin`) отвечала «ID Failed» там, где оригинал входил. Порядок как в эталоне: обрезка → проверка пустоты.
	auto TrimLeadingSpaces = [](FString& S)   // порт `TrimCredential`: пропустить ведущие ' ', остальное без изменений
	{
		int32 Skip = 0;
		while (Skip < S.Len() && S[Skip] == TEXT(' ')) { ++Skip; }
		if (Skip > 0) { S.RightChopInline(Skip); }
	};
	TrimLeadingSpaces(Id);
	TrimLeadingSpaces(Pw);
	// ★ Валидация перед отправкой (HELPER_HINT_CASES_RE.md §1.2, эталон Send_LoginPacket_1): пустой ID → rec10,
	//   пустой пароль → rec11 (mode 2 warning). Не отправляем пакет, показываем подсказку помощника.
	if (Id.IsEmpty())  { ApplyHelper(10); return; }
	if (Pw.IsEmpty())  { ApplyHelper(11); return; }
	// ★★ 16.10.2026 (решение владельца): адрес мира НЕ берётся из поля SERVER_IP_INPUT (поле выключено и показывает
	//   домен). Хост — домен сервера, порт — из DT_ServerList, ЗАНОВО из данных при каждом входе (не из рантайм-
	//   состояния: аудит 2026-09-02, «прилипание порта»). Ветка «поле пустое → блок логина + робот» снята: пустого
	//   адреса больше не бывает. В дев-режиме XyT (только дев-сборки) — адрес и порт прокси, заданные режимом.
	FAsdaServerEntry Target;
	if (bXytMode)
	{
		Target = Server;                          // режим XyT: адрес/порт прокси выставил ApplyXytMode
	}
	else if (!FAsdaLoginData::LoadLoginServer(Target))
	{
		// Таблица серверов не загрузилась — порта нет. Выдумывать число нельзя (закон never-invent): отказ с причиной.
		UE_LOG(LogAsdaLogin, Warning, TEXT("Порт сервера неизвестен: DT_ServerList не загружен"));
		SetLoginStatus(FAsdaLoginData::Loc(990402));   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Список серверов не загружен…»
		return;
	}
	if (Target.IP != Server.IP || Target.Port != Server.Port)
	{
		Server = Target;
		if (UGameInstance* GI = GetGameInstance())
			if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
				if (Net->IsConnected()) Net->Disconnect();   // сменился адрес/порт → реконнект в DoLogin
	}
	FillServerAddrField();                        // поле всегда показывает, куда идём
	// «Сохранить ID»: пишем/чистим check.bin (эталон loginSaveCredentials@0xF14FD0).
	FAsdaLoginData::SaveSavedId(bSaveIdChecked, bSaveIdChecked ? Id : FString());
	UE_LOG(LogAsdaLogin, Log, TEXT("OK нажат (UMG): id='%s' сервер='%s:%d' сохранить=%d"), *Id, *Server.IP, Server.Port, bSaveIdChecked ? 1 : 0);
	DoLogin(Id, Pw);
}

void AAsdaLoginGameMode::OnPasswordCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter) { DoLoginFromWidget(); }   // Enter в пароле → логин
}

void AAsdaLoginGameMode::SetLoginStatus(const FString& Msg)
{
	// Обратная связь входа в пузыре помощника (Минопио) = ANI_TXT в WBP_ANI_WIN (НЕ HELPER_TEXT — такого нет).
	//   Пусто → приветствие (helper-запись 0). Иначе → warning-помощник (mode 2) + модальное окно ошибки.
	if (Msg.IsEmpty())
	{
		ApplyHelper(0);          // приветствие Минопио (запись 0, mode=1 praise)
		return;
	}
	// Ошибка/статус: текст в пузырь помощника + god-robot warning; модальное окно сообщения (эталон UI_ShowLocalizedNotice).
	if (UUserWidget* Ani = GetGameInstance() && GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()->FindWindow(FName(TEXT("WBP_ANI_WIN"))) : nullptr)
	{
		// Форматированный рендер (сырой текст с тегами) через единую точку — не Strip.
		//   ★ 02.09.2026: со СНИМКОМ СТИЛЯ контрола, как требует контракт `AsdaMarkup.h`
		//     (раньше вызов был двухаргументным и пузырь помощника терял кегль/цвет/обводку JUI).
		ApplyRichWithJuiStyle(Ani->GetWidgetFromName(TEXT("ANI_TXT")), Msg);
	}
	SetGodRobotMode(2);          // warning — здесь ТОЛЬКО пузырь помощника (god-robot warning);
	// модалку MESSAGE_WIN для ошибок ЛОГИНА показывает OnLoginResult (ShowMessageWin) — не дублируем тут,
	// т.к. SetLoginStatus зовут и валидации char-create, где модалка не нужна.
}

// ── Помощник Минопио: ANI_TXT + состояние god-robot из записи LoginHelperText (DT_LoginHelper) ──
void AAsdaLoginGameMode::ApplyHelper(int32 RecordIdx, const FString& InsertName)
{
	UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UUserWidget* Ani = WinMgr ? WinMgr->FindWindow(FName(TEXT("WBP_ANI_WIN"))) : nullptr;
	if (!Ani) { UE_LOG(LogAsdaLogin, Warning, TEXT("[CHARCREATE] ApplyHelper(%d): WBP_ANI_WIN НЕ найден → робот/текст не показать"), RecordIdx); return; }

	// Запись помощника из импортированной таблицы (LoginHelperText.BIN). never-invent: текст из оригинала.
	// ★★★ Аудит №4 (A8): ДУБЛЬ ДАННЫХ УСТРАНЁН. `loginhelpertext.bin` была импортирована ДВАЖДЫ —
	//   в `/Game/AsdaAssets/Text/DT_LoginHelper` (21.07, структура-близнец `FAsdaLoginHelperRow`)
	//   и в `/Game/AsdaAssets/Data/ItemDB/DT_loginhelpertext` (09.08, `FAsdaLoginhelpertextRow`).
	//   Поля у структур одни и те же, а даты разошлись на 19 дней: живой потребитель (этот код)
	//   читал СТАРУЮ копию, а свежую — которую обновляет пайплайн импорта item-БД — не читал никто.
	//   ⇒ Оставлен ОДИН ассет, тот, что живёт в манифесте и переживает перегенерацию, и берётся он
	//   через менеджер таблиц (закон `resource-subsystems`), а не собственным `LoadObject`.
	//   Второй ассет и структура-близнец удалены.
	UAsdaItemTableSubsystem* ItemMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UDataTable* DT = ItemMgr ? ItemMgr->GetDataTable(FName(TEXT("loginhelpertext"))) : nullptr;
	int32 Mode = 1;   // деф. mode приветствия (запись 0 = praise)
	FString Text;
	if (DT)
	{
		if (const FAsdaLoginhelpertextRow* R = DT->FindRow<FAsdaLoginhelpertextRow>(FName(*FString::FromInt(RecordIdx)), TEXT("ApplyHelper"), false))
		{
			Mode = R->TabIndex;
			// СЫРОЙ текст с тегами (<FB>Минопио</F> и др.) — формат на рендере. ★ 09.10.2026 (мультиязычность): язык сессии —
			//   оверлей loginhelpertext, ключ `<_row>.Text` (`FAsdaLang::ResolveRow`); русская сессия — прежняя строка.
			Text = FAsdaLang::ResolveRow(DT, FName(*FString::FromInt(RecordIdx)), TEXT("Text"), R->Text);
		}
		else
		{
			// ★ 02.09.2026: РАНЬШЕ ПРОМАХ МОЛЧАЛ (`bWarnIfMissing = false`), и ровно поэтому вызов
			//   несуществующей записи 100 годами показывал ПУСТОЙ пузырь и никто этого не видел.
			//   Гасить `bWarnIfMissing` по-прежнему надо (иначе движок сыплет своим текстом), но
			//   своё предупреждение — обязательно.
			UE_LOG(LogAsdaLogin, Warning,
				TEXT("[помощник] записи %d в loginhelpertext НЕТ — пузырь останется пустым"), RecordIdx);
		}
	}
	// ★ Подстановка имени персонажа (эталон: запись 21 «…персонажем <FC=..>[]</F>?» — имя кладётся
	//   сразу ЗА первой '['). Ищем именно '[' и вставляем после неё: так работает и на строке
	//   с тегами (скобка внутри `<FC=…>[]</F>`), и на строке без тегов.
	if (!InsertName.IsEmpty())
	{
		int32 Br = INDEX_NONE;
		if (Text.FindChar(TEXT('['), Br))
		{
			Text.InsertAt(Br + 1, InsertName);
		}
		else
		{
			// Скобки в записи нет — вставлять некуда. Молчать нельзя: это расхождение данных
			//   с эталонным шаблоном, из-за него игрок не увидит имени.
			UE_LOG(LogAsdaLogin, Warning, TEXT("ApplyHelper(%d): в записи нет '[' — имя '%s' не подставлено"),
				RecordIdx, *InsertName);
		}
	}
	ShowHelperText(Text, Mode);
}

// ── Показ ГОТОВОЙ строки помощника: пузырь ANI_TXT + состояние god-robot ─────────────────────────
//   Хвост `ApplyHelper`, вынесенный отдельно (закон `port-shared-points`), потому что кроме записей
//   игровой таблицы через него шли и НАШИ строки из `DA_LoginConfig` (снято 16.10.2026 вместе с вводом адреса).
void AAsdaLoginGameMode::ShowHelperText(const FString& Raw, int32 Mode)
{
	UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UUserWidget* Ani = WinMgr ? WinMgr->FindWindow(FName(TEXT("WBP_ANI_WIN"))) : nullptr;
	if (!Ani) { return; }
	// Форматированный рендер через единую точку (URichTextBlock + декоратор).
	//   ★ 02.09.2026: со СНИМКОМ СТИЛЯ контрола (контракт `AsdaMarkup.h`). Ширину переноса теперь
	//     даёт canvas-слот самого `ANI_TXT`, а не «зашитые» 228 px внутри `ApplyFormatted`.
	ApplyRichWithJuiStyle(Ani->GetWidgetFromName(TEXT("ANI_TXT")), Raw);
	SetGodRobotMode(Mode);
}

// ★ 16.10.2026: «поле адреса мира пустое» (ApplyHelperServerAddrEmpty / ApplyHelperOurText) снято вместе с вводом
//   адреса — адрес сервера постоянный (домен), пустым не бывает. Поля DA_LoginConfig ServerAddrEmpty* оставлены, чтобы
//   не менять схему ассета (см. AsdaLoginConfig.h).
// ── Помощник «Минопио»: подсказки по наведению кнопки / фокусу поля (HELPER_HINT_CASES_RE.md §1.1) ──
// Кнопка (UButton) → hover: наведение = запись Rec, уход = RecLost (обычно 0 = приветствие, эталон LOST_FOCUS→rec0).
void AAsdaLoginGameMode::BindHelperHover(UUserWidget* W, const FString& BtnName, int32 Rec, int32 RecLost)
{
	if (!W) return;
	UButton* B = Cast<UButton>(W->GetWidgetFromName(FName(*BtnName)));
	if (!B) return;
	UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(W);
	R->HoverCallback   = [this, Rec]()     { ApplyHelper(Rec); };
	R->UnhoverCallback = [this, RecLost]() { ApplyHelper(RecLost); };
	B->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
	B->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
	AsdaUi::AnchorRelay(ButtonRelays, R, B);
}

// Поле ввода (EditableText) → клавиатурный фокус: клик/таб в поле = запись Rec (эталон GOT_FOCUS).
void AAsdaLoginGameMode::AddHelperFocusField(UUserWidget* W, const FString& FieldName, int32 Rec)
{
	if (!W) return;
	if (UWidget* F = W->GetWidgetFromName(FName(*FieldName)))
	{
		FHelperFocusField Ent; Ent.Widget = F; Ent.Rec = Rec;
		HelperFocusFields.Add(Ent);
	}
}

void AAsdaLoginGameMode::RegisterHelperFocus()
{
	if (HelperFocusHandle.IsValid() || !FSlateApplication::IsInitialized()) return;
	HelperFocusHandle = FSlateApplication::Get().OnFocusChanging().AddUObject(this, &AAsdaLoginGameMode::OnUIFocusChanging);
}

void AAsdaLoginGameMode::UnregisterHelperFocus()
{
	if (HelperFocusHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnFocusChanging().Remove(HelperFocusHandle);
	}
	HelperFocusHandle.Reset();
	HelperFocusFields.Reset();
}

// Смена клавиатурного фокуса Slate → если сфокусировано наше поле, показать его подсказку; иначе (ушёл со всех) rec0.
void AAsdaLoginGameMode::OnUIFocusChanging(const FFocusEvent& /*FocusEvent*/, const FWeakWidgetPath& /*OldPath*/,
	const TSharedPtr<SWidget>& /*OldWidget*/, const FWidgetPath& NewPath, const TSharedPtr<SWidget>& /*NewWidget*/)
{
	// Новый фокус на одном из наших полей? → его подсказка, запомнить как текущее сфокусированное поле.
	for (const FHelperFocusField& Ent : HelperFocusFields)
	{
		UWidget* Wg = Ent.Widget.Get();
		if (!Wg) continue;
		TSharedPtr<SWidget> S = Wg->GetCachedWidget();
		// Фокус уходит на ВНУТРЕННИЙ Slate поля (SEditableText), поэтому сверяем ВХОЖДЕНИЕ в путь, а не равенство.
		if (S.IsValid() && NewPath.IsValid() && NewPath.ContainsWidget(S.Get()))
		{
			CurrentFocusField = Wg;
			ApplyHelper(Ent.Rec);
			return;
		}
	}
	// Новый фокус НЕ на поле. Сброс на дефолт — ТОЛЬКО если до этого фокус был на нашем поле (поле его ПОТЕРЯЛО).
	//   Иначе (фокус гуляет между кнопками/root, напр. FocusUI после ответа сервера) НЕ трогаем — сохраняем
	//   текущую подсказку (ответ логина rec8/9/12 не должен сбрасываться приветствием).
	if (CurrentFocusField.IsValid())
	{
		CurrentFocusField = nullptr;
		ApplyHelper(CurrentHelperDefault);
	}
}

// Найти план-виджет по ПРЕФИКСУ имени (конвертер даёт image-планам idx-суффикс: "ANI_TALK_4_0").
// ★ СВЕДЕНО (`fix_helpers_dedup.md`): тонкий форвард к общей точке `AsdaUi::FindNodeByPrefix`
//   (был точный дубль обхода дерева с `StartsWith`, регистр не учитывается — как в каноне).
static UWidget* FindWidgetByPrefix(UUserWidget* Root, const FString& Prefix)
{
	return AsdaUi::FindNodeByPrefix(Root, *Prefix);
}

// mode → ровно один видимый god-robot (эталон ui3Tab_SetActiveByMode@0xF21E40).
void AAsdaLoginGameMode::SetGodRobotMode(int32 Mode)
{
	UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UUserWidget* Ani = WinMgr ? WinMgr->FindWindow(FName(TEXT("WBP_ANI_WIN"))) : nullptr;
	if (!Ani) return;
	// 0→ANI_TALK(Robot03 sation), 1→ANI_VICTORY(Robot01 praise), 2→ANI_WARNING(Robot02 warning).
	//   Префикс + "_" — чтобы "ANI_TALK_" не цеплял "ANI_TALKWIN"/"ANI_TALKTAIL".
	const TCHAR* Names[3] = { TEXT("ANI_TALK_"), TEXT("ANI_VICTORY_"), TEXT("ANI_WARNING_") };
	for (int32 i = 0; i < 3; ++i)
	{
		if (UWidget* W = FindWidgetByPrefix(Ani, Names[i]))
		{
			W->SetVisibility(i == Mode ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
}

// Модальное окно сообщения/ошибки MESSAGE_WIN (эталон UI_ShowLocalizedNotice@0xF17280): текст + OK, захват ввода.
void AAsdaLoginGameMode::ShowMessageWin(const FString& Text)
{
	UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WinMgr) return;
	// Показать как МОДАЛЬНОЕ (bModal=true) поверх сцены (z=20) — блок ввода к нижним окнам.
	UUserWidget* Msg = WinMgr->OpenWindowByPath(FName(TEXT("MESSAGE_WIN")),
		TEXT("/Game/AsdaAssets/UI/Login/WBP_MESSAGE_WIN"), 20, ESlateVisibility::Visible, /*bModal*/true);
	if (!Msg) return;
	// Единая точка форматирования (текст ошибки может нести <FC=/<FD>).
	//   ★ 02.09.2026: со СНИМКОМ СТИЛЯ контрола (контракт `AsdaMarkup.h`) — иначе строка окна
	//     сообщения рисовалась белым 10 px вместо кегля и цвета из JUI.
	ApplyRichWithJuiStyle(Msg->GetWidgetFromName(TEXT("MESSAGE_TXT01")), Text);
	// ★ [ОРИГИНАЛ] 08.10.2026 (пакет 2, решение владельца — исключение для одной кнопки экрана входа): подпись кнопки 113
	//   оригинал кладёт КОДОМ — `CErrorPopup_InitControls@0xF20A70` (ui_core.c:97091-97094): `Collection_AddString(113, loc 2288)`
	//   = «ОК». В разметке у кнопки корейская заготовка («내용~», login_jui.ini:3465), импортёр перевёл её как «Текст».
	//   Порт ставит подпись при каждом показе (окно пересоздаётся после закрытия). Подпись — сосед `<Кнопка>_Text`.
	if (UTextBlock* Cap = Cast<UTextBlock>(Msg->GetWidgetFromName(TEXT("MESSAGE_BUT_Text"))))
	{
		Cap->SetText(FText::FromString(FAsdaLoginData::Loc(2288)));
	}
	else
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("MESSAGE_WIN: подписи MESSAGE_BUT_Text нет — «ОК» (loc 2288) не поставлен"));
	}
	// OK-кнопка MESSAGE_BUT → закрыть окно. Биндим при КАЖДОМ показе (как `ShowConfirm`).
	//   ★★★ ДЕФЕКТ ПОРТА (не оригинала), найден ревизией 03.09.2026. БЫЛО: бинд ОДИН раз под флагом
	//   `bMessageWinBound` («DIV-9: не плодить relay-дубли»). Но OK зовёт `CloseWindow(MESSAGE_WIN)`,
	//   а тот удаляет запись реестра и снимает виджет (`AsdaWindowSubsystem.cpp`, CloseWindow), поэтому
	//   следующий `OpenWindowByPath` записи не находит и СОЗДАЁТ НОВЫЙ виджет — флаг же остался
	//   взведённым, и у нового окна кнопка OK была МЁРТВОЙ. Закрыть его нечем (Esc на логине не
	//   обрабатывается, `ModalBlocker` ест клики, `IsAnyModalOpen` глушит сцену) — клиент запирался
	//   до перезапуска. Раньше окно показывалось редко (кик 4101, отказы 1027/1019), теперь его даёт
	//   каждый клик по заглушке места друга (loc 990136) — второй показ стал обычным делом.
	//   Дублей реле нет и так: `Clear()` снимает прежний бинд (актуально, когда окно ещё открыто и
	//   `OpenWindow` вернул ТОТ ЖЕ виджет), реле создаётся с outer = виджет и держится только
	//   `MessageWinRelay` — старое уходит в GC вместе со своим окном.
	if (UButton* B = Cast<UButton>(Msg->GetWidgetFromName(TEXT("MESSAGE_BUT"))))
	{
		B->OnClicked.Clear();
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(Msg);
		R->Index = 0;
		R->Callback = [WinMgr](int32) { WinMgr->CloseWindow(FName(TEXT("MESSAGE_WIN"))); };
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		MessageWinRelay = R;
	}
	else
	{
		// Без кнопки окно нечем закрыть — это ошибка ассета, а не рантайма; кричим в лог, чтобы не искать вслепую.
		UE_LOG(LogAsdaLogin, Error, TEXT("MESSAGE_WIN: кнопка MESSAGE_BUT не найдена — окно сообщения нечем закрыть"));
	}
}

// Текст ошибки логина по статусу OnLoginResult (нормальный парс всех веток).
//   Статусы 1004 (Net_RecvLoginResponse) → нотисы клиента из error list.bin (те же id, что эталон
//   UI_ShowLocalizedNotice): 2→100, 3/0x66→101, 4→102, 6→117, 7→118, 8/9/10→119. Loc() пуст (таблица
//   не загрузилась) → RU-фолбэк. Kick-коды 4101 (Status=100+код) нотисов в эталоне НЕ имеют (оригинал
//   просто ставит g_nDisconnectReason) — тексты свои, по смыслу кода из LoginManager.c.
FString AAsdaLoginGameMode::BuildLoginErrorText(uint8 Status) const
{
	const int32 NoticeId =
		(Status == 2)                                ? 100 :
		(Status == 3 || Status == 0x66)              ? 101 :
		(Status == 4)                                ? 102 :
		(Status == 6)                                ? 117 :
		(Status == 7)                                ? 118 :
		(Status == 8 || Status == 9 || Status == 10) ? 119 : -1;
	if (NoticeId >= 0)
	{
		FString S = FAsdaLoginData::Loc(NoticeId);
		if (!S.IsEmpty()) return S;
	}
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэки нотисов 100/101/102/117/118/119 убраны — строки нет в
	//   таблице → нейтральная метка «[#id]» (английский игрок не должен видеть русский). Статус без нотиса — своя
	//   строка 990408 «Ошибка входа (статус %d).».
	if (NoticeId >= 0) { return FAsdaLang::MissingTag(NoticeId); }   // N9: единая заглушка
	return UAsdaTextSubsystem::FormatPrintf(FAsdaLoginData::Loc(990408), { FString::FromInt((int32)Status) });
}

// ★ Текст кика 4101 — ПО ЭТАЛОНУ (network.c, ветка opcode==4101 + game_main.c, разбор g_nDisconnectReason
//   в WM_USER 0x511/lParam=2). Раньше порт печатал самодельное «Соединение отклонено сервером (код N)» —
//   у оригинала для каждого кода есть свой нотис из loc-таблицы.
FString AAsdaLoginGameMode::BuildKickText(int32 Code, bool bClosed) const
{
	// 1) «Нотисные» коды (соединение живо) — эталонный switch по *pLoginStatusByte.
	int32 NoticeId = -1;
	if (!bClosed)
	{
		switch (Code)
		{
		case 0x0D: NoticeId = 134;    break;
		case 0x0F: NoticeId = 135;    break;
		case 0x10: NoticeId = 110690; break;
		case 0x14: NoticeId = 7010;   break;
		case 0x15: NoticeId = 7011;   break;
		case 0x16: case 0x69: NoticeId = 7012; break;
		case 0x18: NoticeId = 7013;   break;
		case 0x19: NoticeId = 7014;   break;
		case 0x1A: case 0x65: NoticeId = 7015; break;
		case 0x1B: case 0x66: NoticeId = 7016; break;
		case 0x1C: NoticeId = 7017;   break;
		case 0x1D: NoticeId = 7018;   break;
		case 0x1E: NoticeId = 7019;   break;
		case 0x1F: case 0x20: NoticeId = 7021; break;
		case 0x67: NoticeId = 7080;   break;
		case 0x68: NoticeId = 7081;   break;
		case 0x6A: NoticeId = 7082;   break;
		case 0x6B: NoticeId = 7083;   break;
		default: break;
		}
	}
	else
	{
		// 2) Коды разрыва — эталонная карта g_nDisconnectReason → Show_DisconnectPopup(loc).
		switch (Code)
		{
		case 0:    NoticeId = 1002;  break;
		case 1:    NoticeId = 1098;  break;   // ping timeout (Check_DisconnectUser)
		case 2:    NoticeId = 1099;  break;
		case 3:    NoticeId = 1100;  break;
		// ★ 25.10.2026 [ПОРТ] (ярлык исправлен 26.10.2026 по ревизии wave_1025d 4.3): причина 4 = неудача подключения к
		//   game-/канал-серверу. В эталонной таблице `MainWndProc` (game_main.c:5917-5955) случая 4 НЕТ: эталон показывает 1001 сам,
		//   ДО установки причины (network.c:3881-3886 `Show_DisconnectPopup(1001)`, затем `g_nDisconnectReason = 4`; вход из выбора
		//   персонажа — ошибка 1001, game_main.c:3337-3340), а по самой причине 4 ничего не выводит. У порта на сцене входа отдельного
		//   показа нет — 1001 даёт эта строка (ставит причину `UAsdaNetSubsystem::HandleGameConnectFailed`; прежде такой отказ молчал).
		//   Побочное следствие: 4101 с кодом 4 от сервера (если такой бывает) тоже покажет 1001.
		case 4:    NoticeId = 1001;  break;
		case 5:    NoticeId = 8509;  break;
		case 6:    NoticeId = 1106;  break;
		case 7:    NoticeId = 1107;  break;
		case 8:    NoticeId = 9566;  break;
		case 9:    NoticeId = 9567;  break;
		case 10:   NoticeId = 12940; break;
		case 11:   NoticeId = 12941; break;
		case 12:   NoticeId = 14000; break;
		case 13:   NoticeId = 14103; break;
		case 14:   NoticeId = 14150; break;
		case 15:   NoticeId = 7034;  break;
		case 0xFF: NoticeId = 1108;  break;   // обрыв без 4101 (эталонный reason 255)
		default: break;
		}
	}
	if (NoticeId >= 0)
	{
		const FString S = FAsdaLoginData::Loc(NoticeId);
		if (!S.IsEmpty()) return S;
	}
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэки при пропуске строки нотиса убраны → нейтральная метка
	//   «[#id]» (английский игрок не должен видеть русский). Код без нотиса — свои строки: 990409 (отказ при живом
	//   соединении), 990416 «Соединение закрыто сервером (код %d).\nВойдите ещё раз.».
	if (NoticeId >= 0) { return FAsdaLang::MissingTag(NoticeId); }   // N9: единая заглушка
	if (!bClosed) return FAsdaLoginData::Loc(990409);
	return UAsdaTextSubsystem::FormatPrintf(FAsdaLoginData::Loc(990416), { FString::FromInt(Code) });
}

// ★ Кик login-сервера / обрыв. Эталон закрывает сокет и показывает попап (кнопка OK там завершает клиент —
//   PostQuitMessage в CommonWin_CommonEventFunc). Порт закрывает сокет ТАК ЖЕ, но вместо выхода возвращает
//   на экран логина: соединение уже сброшено, поэтому DoLogin поднимет НОВЫЙ коннект и вход сработает.
// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] Ключ: разрыв на выборе персонажа. 1 — ОК окна разрыва перезагружает сцену логина (сцена выбора
//   снесена, войти можно только через канал → свежие 1005/1007/1006); 0 — прежнее: сцена выбора живёт под окном входа.
//   Оригинал здесь закрывает клиент (`Show_DisconnectPopup@0xEC9A80` → `PostQuitMessage`), порт оставляет его живым.
static ASDA_DEV_CVAR(int32, CVarLoginKickReloadsScene,
	TEXT("asda.Login.KickReloadsScene"), 1,
	TEXT("1 = разрыв на выборе персонажа: ОК окна сообщения перезагружает сцену логина; "
	     "0 = прежнее: «Играть» оставалась под окном входа и входила без свежего списка персонажей (голая модель)."),
	ECVF_Default);

void AAsdaLoginGameMode::OnLoginKick(int32 Code, bool bClosed)
{
	// ★ 16.10.2026 (EAC, тексты владельца): разрыв по событию античита (KICK / нарушение / отказ входа EAC) несёт свой текст
	//   (заголовок + причина 991xxx на языке сессии, UAsdaNetSubsystem::GetEacKickText) — он вместо нотиса оригинала по коду.
	//   Пусто (строк 991xxx нет / разрыв не от EAC) — нотис оригинала, как раньше. Окно и статус — те же (MESSAGE_WIN).
	const UAsdaNetSubsystem* EacNet = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FString Msg = (EacNet && !EacNet->GetEacKickText().IsEmpty()) ? EacNet->GetEacKickText() : BuildKickText(Code, bClosed);
	UE_LOG(LogAsdaLogin, Warning, TEXT("Кик 4101 code=%d (соединение %s) → %s"),
		Code, bClosed ? TEXT("закрыто") : TEXT("живо"), *Msg);
	// Сцена выбора персонажа уже начата (перелёт, шкатулка, экран выбора, создание, возврат).
	const bool bCharSelectScene = bFlightActive || bCasketOpening || bCasketOpened || CasketSeq != ECasketSeqPhase::None;

	if (bClosed)
	{
		// ★ Сессия мертва — снести открытый диалог MESSAGE2 (выбор режима входа / подтверждение
		//   удаления), иначе он висит сиротой над экраном логина поверх окна кика (03.09.2026, по
		//   ревизии `review_charselect_enter_mode_modal` п.4). `CloseConfirm(false)` для обоих
		//   логин-диалогов безвреден: колбэк удаления на `false` выходит, колбэк выбора режима лишь
		//   запоминает кадр, а 1016 шлёт только реле реального клика по BUT01 — его тут нет.
		//   При живом соединении (нотисный кик, bClosed=false) диалог не трогаем: MESSAGE_WIN встаёт
		//   поверх него в полосе модалок, после OK игрок отвечает на вопрос как ни в чём не бывало.
		if (UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			if (WinMgr->IsOpen(FName(TEXT("MESSAGE2_WIN")))) { WinMgr->CloseConfirm(false); }
		}
		// полный откат на экран логина (кик мог прийти и на server/char-select)
		bLoggedIn = false;
		ReleaseCharSelectPreload();   // ★ charsel_preload: сессии нет — предзагрузку превью отпускаем
		if (ServerSelectWidget) { ServerSelectWidget->SetVisibility(ESlateVisibility::Collapsed); }
		if (LoginWidget)        { LoginWidget->SetVisibility(ESlateVisibility::Visible); }
		FocusUI(LoginWidget);
	}
	SetLoginStatus(Msg);
	ShowMessageWin(Msg);
	// ★ 06.11.2026 (`asda.Login.KickReloadsScene`): сессии нет, а сцена выбора жива — после нового входа «Играть» слала бы 1016
	//   без канала и 1005 (журнал Player5 27.10 00:42:46–54), с пустым снимком экипа и устаревшим списком. ОК окна — чистый логин.
	if (bClosed && bCharSelectScene && CVarLoginKickReloadsScene.GetValueOnGameThread() != 0)
	{
		TWeakObjectPtr<UWorld> W(GetWorld());
		auto Reload = [W]()
		{
			if (W.IsValid())
			{
				UE_LOG(LogAsdaLogin, Log, TEXT("Разрыв на выборе персонажа: перезагрузка сцены логина"));
				UGameplayStatics::OpenLevel(W.Get(), FName(TEXT("/Game/Map/Login")));
			}
		};
		const UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (MessageWinRelay && WinMgr && WinMgr->IsOpen(FName(TEXT("MESSAGE_WIN"))))
		{
			TFunction<void(int32)> Close = MessageWinRelay->Callback;   // закрытие окна — как у ShowMessageWin
			MessageWinRelay->Callback = [Close, Reload](int32 I) { if (Close) { Close(I); } Reload(); };
		}
		else
		{
			Reload();   // окна нет (не открылось) — ждать нечего
		}
	}
}

void AAsdaLoginGameMode::OnSaveIdChanged(bool bChecked)
{
	bSaveIdChecked = bChecked;   // «сохранить ID»: при логине пишем/чистим check.bin (loginSaveCredentials)
}

// ══════════════════════════ РЕЖИМ XyT ══════════════════════════════════════════════════════════
// Подключение к чужой сборке XyT. Её клиент наружу НЕ ходит: логин идёт в локальный прокси
//   `Launcher.exe` (127.0.0.1:17544), игровой канал — туда же (17505), а наружу пакеты уносит
//   уже сам лаунчер. Отличается и таблица ключа канального шифра. Полный разбор и метод
//   восстановления ключа — `Reports/xyt_traffic_capture.md`.
// ⚠ Переключатель строится В РАНТАЙМЕ: ассет `WBP_LOGIN_WIN` генерируется билдером, и ручные
//   правки в нём сносятся регенерацией (памятка `no-ui-regeneration-without-permission`).
void AAsdaLoginGameMode::BuildXytToggle()
{
	if (!LoginWidget || XytCheck) { return; }
	// Родитель и опора — поле адреса сервера: переключатель встаёт ПОД ним, в том же канвасе.
	UWidget* IpField = LoginWidget->GetWidgetFromName(TEXT("SERVER_IP_INPUT"));
	UCanvasPanelSlot* IpSlot = IpField ? Cast<UCanvasPanelSlot>(IpField->Slot) : nullptr;
	if (!IpSlot)
	{
		UE_LOG(LogAsdaLogin, Warning,
			TEXT("[XyT] SERVER_IP_INPUT не найден (или не в канвасе) — переключатель XyT не построен"));
		return;
	}
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(IpField->GetParent());
	if (!Canvas) { return; }

	const FVector2D Base = IpSlot->GetPosition();
	const FVector2D Size = IpSlot->GetSize();

	// Сам чекбокс — только ВИЗУАЛ (клик ловит кнопка на всю строку, как у «сохранить ID»).
	XytCheck = LoginWidget->WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("XYT_CHECK"));
	UCanvasPanelSlot* CbSlot = Canvas->AddChildToCanvas(XytCheck);
	CbSlot->SetPosition(FVector2D(Base.X, Base.Y + Size.Y + 3.f));
	CbSlot->SetSize(FVector2D(16.f, 16.f));
	CbSlot->SetZOrder(120);
	XytCheck->SetIsChecked(bXytMode);
	XytCheck->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Подпись. Своих игровых строк не выдумываем: «XyT» — техническое имя сборки, не перевод.
	UTextBlock* Txt = LoginWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("XYT_CHECK_TXT"));
	UCanvasPanelSlot* TSlot = Canvas->AddChildToCanvas(Txt);
	TSlot->SetPosition(FVector2D(Base.X + 20.f, Base.Y + Size.Y + 2.f));
	TSlot->SetSize(FVector2D(120.f, 18.f));
	TSlot->SetZOrder(120);
	Txt->SetText(FText::FromString(TEXT("XyT")));
	Txt->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (const UTextBlock* Src = Cast<UTextBlock>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_CHECK_TXT"))))
	{
		Txt->SetFont(Src->GetFont());          // шрифт и цвет — как у соседнего чекбокса (не выдумываем стиль)
		Txt->SetColorAndOpacity(Src->GetColorAndOpacity());
	}

	// Прозрачная кнопка на всю строку — она и ловит клик (у UCheckBox 16×16 попасть трудно).
	UButton* Hit = LoginWidget->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("XYT_CHECK_HIT"));
	FButtonStyle St = Hit->GetStyle();
	St.Normal.TintColor = FSlateColor(FLinearColor::Transparent);
	St.Hovered.TintColor = FSlateColor(FLinearColor::Transparent);
	St.Pressed.TintColor = FSlateColor(FLinearColor::Transparent);
	Hit->SetStyle(St);
	UCanvasPanelSlot* HSlot = Canvas->AddChildToCanvas(Hit);
	HSlot->SetPosition(FVector2D(Base.X, Base.Y + Size.Y + 2.f));
	HSlot->SetSize(FVector2D(140.f, 18.f));
	HSlot->SetZOrder(121);
	UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
	R->Callback = [this](int32) { ToggleXytMode(); };
	Hit->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
	AsdaUi::AnchorRelay(ButtonRelays, R, Hit);
	AsdaUi::DisableButtonFocusRing(Hit);

	UE_LOG(LogAsdaLogin, Log, TEXT("[XyT] переключатель построен под полем адреса (%.0f,%.0f)"),
		Base.X, Base.Y + Size.Y + 2.f);
}

void AAsdaLoginGameMode::ToggleXytMode()
{
	bXytMode = !bXytMode;
	if (XytCheck) { XytCheck->SetIsChecked(bXytMode); }
	// Запоминаем выбор между запусками (там же, где адрес мира).
	if (GConfig)
	{
		GConfig->SetBool(TEXT("AsdaLogin"), TEXT("XytMode"), bXytMode, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	ApplyXytMode();
	UE_LOG(LogAsdaLogin, Log, TEXT("[XyT] режим %s"), bXytMode ? TEXT("ВКЛЮЧЁН") : TEXT("выключен"));
}

void AAsdaLoginGameMode::ApplyXytMode()
{
	// ★ 16.10.2026: XyT — исследовательский режим (чужая сборка через её локальный прокси, скрыт с 31.08.2026):
	//   только в дев-сборках. В Shipping режим не включается ни при каком состоянии.
#if !ASDA_DEV_CONSOLE
	bXytMode = false;
#endif
	// 1) Таблица ключа канального шифра — своя у каждой сборки сервера.
	Asda::ChannelCipher::SetProfile(bXytMode ? Asda::EKeyProfile::XyT : Asda::EKeyProfile::Default);

	// 2) Адрес. В режиме XyT идём в ИХ локальный прокси; иначе — домен сервера (★ 16.10.2026).
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net) { Net->bXytMode = bXytMode; }

	if (bXytMode)
	{
		Server.IP   = UAsdaNetSubsystem::XytHost();
		Server.Port = Net ? Net->XytLoginPort : 17544;
		const FString Addr = FString::Printf(TEXT("%s:%d"), *Server.IP, Server.Port);
		if (UEditableText* E = Cast<UEditableText>(LoginWidget ? LoginWidget->GetWidgetFromName(TEXT("SERVER_IP_INPUT")) : nullptr))
		{
			E->SetText(FText::FromString(Addr));   // видно, куда пойдём — поле остаётся правдой
		}
	}
	else
	{
		// ★ Штатный сервер: хост — домен, порт — из таблицы (DT_ServerList/OPset = 15001). Порт берётся заново, иначе
		//   после XyT-режима в Server.Port «прилипал» 17544 (жалоба владельца 2026-08-31).
		//   ★ 16.10.2026: сохранённый адрес больше не читается (решение владельца).
		FAsdaLoginData::LoadLoginServer(Server);
		FillServerAddrField();                               // поле показывает домен
	}
	// Адрес/ключ сменились → старое соединение недействительно, рвём (реконнект будет в DoLogin).
	if (Net && Net->IsConnected()) { Net->Disconnect(); }
}

// ★ 16.10.2026: поле SERVER_IP_INPUT показывает домен сервера. Меняется ТОЛЬКО текст: вид, доступность и разметку поля
//   задаёт ассет (владелец выключил ввод в WBP_LOGIN_WIN). Дев-режим XyT пишет в поле свой адрес прокси сам.
void AAsdaLoginGameMode::FillServerAddrField()
{
	if (!LoginWidget) { return; }
	if (UEditableText* E = Cast<UEditableText>(LoginWidget->GetWidgetFromName(TEXT("SERVER_IP_INPUT"))))
	{
		E->SetText(FText::FromString(AsdaServerAddr::LoginHost()));
	}
}

// Тоггл «сохранить ID» по клику на строку: свап визуала чекбокса + НЕМЕДЛЕННАЯ персистентность (флаг+текущий ID).
//   Так игра запоминает логин, даже если сразу закрыть; при следующем запуске LoadSavedId префиллит поле и ставит галочку.
void AAsdaLoginGameMode::ToggleSaveId()
{
	if (!LoginWidget) return;
	bSaveIdChecked = !bSaveIdChecked;
	if (UCheckBox* Cb = Cast<UCheckBox>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_CHECK"))))
		Cb->SetIsChecked(bSaveIdChecked);   // свап кадра SS0(пусто)↔SS2(галочка)
	FString Id;
	if (UEditableText* E = Cast<UEditableText>(LoginWidget->GetWidgetFromName(TEXT("LOGIN_EDIT00"))))
		Id = E->GetText().ToString();
	FAsdaLoginData::SaveSavedId(bSaveIdChecked, bSaveIdChecked ? Id : FString());
	UE_LOG(LogAsdaLogin, Log, TEXT("Сохранить ID: %s (id='%s')"), bSaveIdChecked ? TEXT("ВКЛ") : TEXT("выкл"), *Id);
}
