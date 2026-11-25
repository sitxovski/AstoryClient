// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaHudSubsystem — реализация. Полный реверс-разбор трёх окон — в шапке AsdaHudSubsystem.h.
#include "UI/AsdaHudSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса

#include "UI/AsdaWindowSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaFloatingTextSubsystem.h"   // ★ ЕДИНЫЙ канал всплывающих надписей (цифры урона + опыт)
#include "GameFramework/PlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/RichTextBlock.h"   // центрирование нотисов после ApplyFormatted (CaptionAlign=5)
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "Styling/SlateTypes.h"
#include "UI/AsdaMarkup.h"   // ApplyFormatted: inline-теги <FC:>/<FC=> и базовый цвет строки
#include "UI/AsdaMessageSubsystem.h"   // ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)
#include "UI/AsdaPremiumGachaSubsystem.h"   // ★ гейт `sub_922D5D@0xB6F370`: EXPBar_WIn не рисуется при открытой сцене
#include "Items/AsdaGradeColor.h"      // палитра грейдов CItemManager_GetDropColor (имя предмета в анонсе)
#include "Misc/Parse.h"                // FParse::HexNumber — разбор цвета анонса из cvar
#include "HAL/IConsoleManager.h"       // TAutoConsoleVariable<FString> — настройка оформления без пересборки
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "UI/AsdaChangeJobBannerSubsystem.h" // ★ 06.10.2026 (волна 2-В): взаимоисключение с баннером смены профессии 18500

DEFINE_LOG_CATEGORY_STATIC(LogAsdaHud, Log, All);

// Пути импортированных WBP (генератор Tools/asda_jui_generic_umg.py, раскладка asda_player_ui_layout.py).
static const TCHAR* kExpBarWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_EXPBar_WIn.WBP_EXPBar_WIn_C");
static const TCHAR* kNoticeWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_NOTICE_WIN.WBP_NOTICE_WIN_C");
static const TCHAR* kHeroWbp   = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_HERO_NOTICE_WIN.WBP_HERO_NOTICE_WIN_C");

// ═════════════ ★ Оформление анонсов заточки (6131 тег 4/5) — настраивается БЕЗ пересборки ═════════
//   Итоговый вид (решение юзера, 2026-08-04):
//       <жёлтый>ИГРОК улучшил предмет [ <грейд>ИМЯ</F> ] <грейд>+45%</F></F>
//   • базовый текст анонса и СКОБКИ — жёлтый (успех) / красный (неудача);
//   • ИМЯ предмета — ВСЕГДА цветом грейда (эталон красит имя предмета по грейду:
//     item_inventory.c:45318 CControl_SetField923(nameCtrl, GetDropColor(itemRecord->itemGrade)));
//   • ПРОЦЕНТ — тоже цветом грейда, ВНЕ скобок, сразу после них.
//   Возврат к базовому цвету обеспечивает ApplyNoticeText: он подставляет базовый тег после
//   каждого </F> (см. ниже), поэтому вложенность <база><FC=грейд>..</F> хвост</F> корректна.
// ★★★ ПРАВКА ПО ЖИВОМУ ТЕСТУ 2026-08-06 (просьба юзера: «должно быть ЗЕЛЁНОГО текста сам текст
//   "ник улучшил []", остальное по грейду»). База анонса — ЗЕЛЁНАЯ, и это НЕ отступление, а
//   ВОЗВРАТ К ЭТАЛОНУ: оба места оригинала, показывающие анонс редкой добычи, задают ровно
//   −16711936 = 0xFF00FF00:
//     item_inventory.c:39568-39570  uiShowTimedNoticeText(g_pChatWnd, msgBuf, -16711936, 10000);
//                                   Chat_AddChatLine(g_pChatWnd, 10, msgBuf, -16711936, …);
//     network.c:3748-3750           те же два вызова на приёме 6131.
//   Золотой FFD306 был нашим выбором (собственного «эталонного жёлтого» у заточки нет —
//   Reports/hero_notice_format.md §2), теперь он отменён.
static ASDA_DEV_CVAR(FString, CVarHeroRefineSuccessColor,
	TEXT("asda.Hero.Announce.SuccessColor"),
	TEXT("00FF00"),
	TEXT("Базовый цвет анонса УСПЕШНОЙ заточки, sRGB RRGGBB. По умолчанию 00FF00 — ЭТАЛОННЫЙ зелёный "
	     "анонса (-16711936 в item_inventory.c:39568/network.c:3748). Красит обрамляющий текст и скобки; "
	     "имя предмета и процент идут цветом грейда независимо от этого ключа."),
	ECVF_Default);

static ASDA_DEV_CVAR(FString, CVarHeroRefineFailColor,
	TEXT("asda.Hero.Announce.FailColor"),
	TEXT("FF0000"),
	TEXT("Базовый цвет анонса НЕУДАЧНОЙ заточки, sRGB RRGGBB (по умолчанию FF0000 — красный)."),
	ECVF_Default);

// ⚠ ФАКТ ИЗ ДЕКОМПИЛА СЕРВЕРА (2026-08-06, GameServer.c sub_4FEE50 Recv_User_ShellItemUpgrade):
//   при НЕУДАЧЕ предмет для части свитков СБРАСЫВАЕТСЯ в ноль — 0x500009 `mov byte[ebp-0x208],0`
//   (item+24 ступень) и 0x500010 `mov word[ebp-0x205],0` (item+27 процент). Поэтому прежнее
//   утверждение «процент предмета не изменился» НЕВЕРНО: показываем процент ДО попытки — то, что
//   игрок потерял. Значение приходит с сервера в 6131 (+26 WORD).
static ASDA_DEV_CVAR(int32, CVarHeroRefineFailShowPercent,
	TEXT("asda.Hero.Announce.FailShowPercent"),
	1,
	TEXT("1 — показывать процент и в анонсе НЕУДАЧИ (процент ДО сорвавшейся попытки: видно, с какой "
	     "ступени слетело; при неудаче часть свитков сбрасывает предмет в +0). 0 — только имя."),
	ECVF_Default);

static ASDA_DEV_CVAR(FString, CVarHeroRefineBracketOpen,
	TEXT("asda.Hero.Announce.BracketOpen"),
	TEXT("[ "),
	TEXT("Открывающая скобка вокруг имени предмета в анонсе заточки (цвет — базовый цвет строки)."),
	ECVF_Default);

static ASDA_DEV_CVAR(FString, CVarHeroRefineBracketClose,
	TEXT("asda.Hero.Announce.BracketClose"),
	TEXT(" ]"),
	TEXT("Закрывающая скобка вокруг имени предмета в анонсе заточки (цвет — базовый цвет строки)."),
	ECVF_Default);

// ★★★ Гейт СЕТЕВОЙ половины анонса редкой добычи (C→S 6616). Локальный HERO_NOTICE и C→S 6129
//   «покажи предмет окружающим» им НЕ управляются — они были и остаются как в оригинале.
//   Зачем нужен: оригинальный клиент — ЕДИНСТВЕННЫЙ источник анонса, у стокового сервера
//   обработчика 6616 нет вообще (`rare-item-notice-6616-handler.md`). Наш живой сервер умеет
//   ОБА пути сразу: P01 ретранслирует клиентский 6616 в 6131, а P11 объявляет те же события САМ
//   (`ConfigSet\patches.ini`: `PickupEnabled` / `GachaEnabled` / `PremiumGachaEnabled`). Когда
//   включены оба, окружающие увидят анонс ДВАЖДЫ. Один из двух каналов надо выключить: либо этот
//   cvar в 0 (мгновенно, на клиенте), либо соответствующий ключ P11 в 0 (нужен рестарт сервера).
//   Умолчание 1 = поведение оригинального клиента; молча от эталона не отступаем.
static ASDA_DEV_CVAR(int32, CVarHeroClientSend6616,
	TEXT("asda.Hero.Announce.ClientSend6616"),
	1,
	TEXT("1 — слать серверу C→S 6616 «объяви мою редкую добычу» (как оригинальный клиент). "
	     "0 — не слать: анонс приходит только серверным путём (AsdaPatch P11), нет задвоения строки."),
	ECVF_Default);

// "RRGGBB"/"FFRRGGBB"/"#RRGGBB" → FColor (sRGB). Мусор в cvar → Fallback, чтобы строка не пропала.
static FColor ParseAnnounceColor(const FString& InHex, const FColor& Fallback)
{
	FString H = InHex.TrimStartAndEnd().Replace(TEXT("#"), TEXT(""));
	if (H.Len() == 8) { H = H.RightChop(2); }   // отбрасываем альфу
	bool bOk = (H.Len() == 6);
	for (int32 i = 0; bOk && i < H.Len(); ++i) { bOk = FChar::IsHexDigit(H[i]); }
	if (!bOk)
	{
		UE_LOG(LogAsdaHud, Warning, TEXT("[анонс] цвет '%s' не разобран (нужно RRGGBB) — беру #%02X%02X%02X"),
			*InHex, Fallback.R, Fallback.G, Fallback.B);
		return Fallback;
	}
	const uint32 V = (uint32)FParse::HexNumber(FStringView(*H, H.Len()));
	return FColor((uint8)((V >> 16) & 0xFF), (uint8)((V >> 8) & 0xFF), (uint8)(V & 0xFF), 255);
}

// Блок «[ ИМЯ ] +N%»: скобки — базовым цветом строки, имя и процент — цветом грейда.
static FString BuildRefineItemDeco(const FString& ItemName, int32 Grade, int32 Percent, bool bShowPercent)
{
	const FString G = FAsdaGradeColor::HexARGB(Grade);
	FString S = CVarHeroRefineBracketOpen.GetValueOnAnyThread()
		+ FString::Printf(TEXT("<FC=%s>%s</F>"), *G, *ItemName)
		+ CVarHeroRefineBracketClose.GetValueOnAnyThread();
	if (bShowPercent)
	{
		S += FString::Printf(TEXT(" <FC=%s>+%d%%</F>"), *G, Percent);
	}
	return S;
}

void UAsdaHudSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());

	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	HSelfExp = Net->OnSelfExp.AddUObject(this, &UAsdaHudSubsystem::OnSelfExp);
	HLevelUp = Net->OnSelfLevelUp.AddUObject(this, &UAsdaHudSubsystem::OnSelfLevelUp);
	// ★ Круг 7: серверный push 3113 идёт через ЕДИНЫЙ маршрутизатор (категория AdminNotice).
	//   Цвет — из пакета (перекрытие), режим — mode пакета. Куда попадёт — решает таблица маршрутов.
	HServerNotice = Net->OnServerNotice.AddLambda([this](int32 Mode, const FString& Text, FColor Rgb)
	{
		// ★★★ ПОРЯДОК ЭТАЛОНА (uiNoticeUpdate3Line_BC90@0xBABC90): сначала РАЗВЕРНУТЬ тело в готовую
		//   строку (режим 100 = число минут → loc 10221) и обнулить режим, и только ПОТОМ отдавать
		//   потребителям (в эталоне это uiChatLogAddLine + Begin, у нас — маршрутизатор).
		//   Раньше подстановка жила внутри ShowServerNotice, т.е. ПОСЛЕ Post ⇒ баннер показывал
		//   «Битва начнется через 5 мин.», а в ленту чата уходил сырой аргумент «5».
		const FString Ready    = ResolveNoticeText(Mode, Text);
		const int32   OutMode  = ResolvedNoticeMode(Mode);
		if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			FAsdaMsgParams P;
			P.Category = EAsdaMsgCategory::AdminNotice;
			P.Text = Ready;
			P.bUseColor = true;  P.Color = Rgb;      // RGB(0,0,0) = «не задан» → белый решает ShowServerNotice
			P.NoticeMode = OutMode;
			P.SrcOpcode = 3113;
			Msg->Post(P);
			return;
		}
		ShowServerNotice(OutMode, Ready, Rgb);       // фолбэк, если маршрутизатор недоступен
	});
	HItemAcquired = Net->OnItemAcquired.AddUObject(this, &UAsdaHudSubsystem::OnItemAcquired);
}

void UAsdaHudSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnSelfExp.Remove(HSelfExp);
			Net->OnSelfLevelUp.Remove(HLevelUp);
			Net->OnServerNotice.Remove(HServerNotice);
			Net->OnItemAcquired.Remove(HItemAcquired);
		}
	}
	ResetWidgets();
	Super::Deinitialize();
}

// ★★★ К-3 / находка №8 отчёта `11_hud.md` (ревизия подняла до ВАЖНО).
//   `ResetWidgets()` снимает все виджеты HUD, КРОМЕ баннера, а сам баннер создаётся
//   `CreateWidget` с миром на момент ПЕРВОГО показа. После травела поле не пустое ⇒ ветка
//   `if (!LevelUpBanner)` больше не выполняется, а `LevelUpBannerStartS` от СТАРОГО мира против
//   `GetTimeSeconds()` нового даёт ОТРИЦАТЕЛЬНОЕ T, так что ветка «истёк» не наступает никогда.
//   ★ НАШЕ ИСПРАВЛЕНИЕ ДЕФЕКТА ПОРТА (в оригинале травела нет, баннер живёт в оконном
//   менеджере, который пересобирается при смене состояния игры).
void UAsdaHudSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Баннер гасим при ЛЮБОЙ причине: виджет привязан к МИРУ, а мир умирает и при
	// смене карты, и при выходе в выбор персонажа.
	if (LevelUpBanner)
	{
		LevelUpBanner->RemoveFromParent();
		LevelUpBanner = nullptr;
	}
	LevelUpBannerActive = false;
	LevelUpBannerStartS = 0.0;
	LevelUpBannerHoldS  = 2.0f;

	// Строки нотисов и hero-нотис — тоже принадлежность сессии: после перезахода новый
	// персонаж видел бы серверные объявления предыдущего.
	if (AsdaIsFullSessionReset(Reason))
	{
		NoticeLines.Reset();
		ServStickyShownAt = -1.0;
		HeroShownAt = -1.0;
	}
	// Виджеты HUD привязаны к умершему миру — снимаем ссылки и признаки «разобрано»;
	// `Tick` сам поднимет окна заново на новой карте (самовосстановление окон).
	// ★ СМЯГЧЕНИЕ (решение владельца 2026-09-02): при РАЗРЫВЕ СВЯЗИ мир НЕ умирал — виджеты
	//   живы и валидны. Сбрасывать ссылки незачем: `Tick` тут же собрал бы их заново, а игрок
	//   увидел бы моргание HUD на ровном месте. См. `AsdaSessionResetKeepsWorldAlive`.
	if (!AsdaSessionResetKeepsWorldAlive(Reason))
	{
		ResetWidgets();
	}
}

void UAsdaHudSubsystem::ResetWidgets()
{
	ExpBarWidget = nullptr; NoticeWidget = nullptr; HeroWidget = nullptr;
	ExpFill = nullptr; ExpDigitClones.Reset();
	for (int32 i = 0; i < 3; ++i) { NoticeTxt[i] = nullptr; }
	ServNoticeText = nullptr; ServStickyShownAt = -1.0;
	HeroText = nullptr; HeroClick = nullptr;
	bExpControlsReady = false;
	for (int32 i = 0; i < 10; ++i) { bDigitBrushValid[i] = false; }
}

// ── Тик: самовосстановление окон + таймеры (строки notice 10 с, hero-нотис 10 с) ────────────────
void UAsdaHudSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Hud");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	// HUD живёт только В МИРЕ (эталон: CHUD создаётся при входе, g_GameState==1).
	if (!LM || LM->CurrentMapId < 0 || !WS || !IsValid(WS->LocalPlayerActor)) { return; }
	TickLevelUpBanner(DeltaTime);   // ★ ход баннера уровня — до гейта виджетов HUD, он своё окно
	if (!EnsureWidgets()) { return; }

	// ══════════════════════════════════════════════════════════════════════════════════════════════
	//  ★★★ EXP-БАР НЕ РИСУЕТСЯ, ПОКА ОТКРЫТА СЦЕНА ПРЕМИУМ-ГАЧИ.
	//  Эталон `CRenderer_RenderWorldFrame@0x9773B0` (`render_core.c:1512`):
	//      if ( !sub_922D5D(g_pGachaScene) )
	//      {   // ищет/рисует окно с именем "EXPBar_WIn" → g_pExpBarWindow
	//          if ( g_pExpBarWindow ) { gfxSetZWriteOffDraw(...); vtbl[13](g_pExpBarWindow, dev); }
	//          else { ...поиск контрола по строке "EXPBar_WIn" в списке dword_15CB768... }
	//      }
	//  ⚠ Уточнение против прежней записи в `gacha_ui_re.md` («часть мирового кадра не рисуется»):
	//  под гейтом стоит РОВНО отрисовка окна EXP-полосы, а не геометрия мира. Ниже по функции
	//  `UIRenderStage_DrawTopWindows(g_pWindowMgr)` вызывается БЕЗУСЛОВНО — остальной HUD и окна
	//  продолжают рисоваться. Поэтому здесь гасится только `EXPBar_WIn` (нотисы и герой-нотис — нет).
	//  Восстанавливаем ИСХОДНУЮ видимость `SelfHitTestInvisible` (см. `EnsureWidgets`), а не Visible:
	//  растянутый на вьюпорт корень с Visible съедает мировые клики.
	if (ExpBarWidget)
	{
		const UAsdaPremiumGachaSubsystem* Gacha = GI->GetSubsystem<UAsdaPremiumGachaSubsystem>();
		const bool bHide = Gacha && Gacha->IsSceneOpen();
		const ESlateVisibility Want = bHide ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible;
		if (ExpBarWidget->GetVisibility() != Want)
		{
			ExpBarWidget->SetVisibility(Want);
			UE_LOG(LogAsdaHud, Log, TEXT("[HUD] EXPBar_WIn %s (гейт сцены премиум-гачи, эталон render_core.c:1512)"),
				bHide ? TEXT("скрыт") : TEXT("показан"));
		}
	}

	TickNoticeExpiry();

	// HERO_NOTICE: таймаут жизни (objStartTimedAction 10 000 мс) → скрыть/показать следующий.
	if (HeroShownAt >= 0.0)
	{
		UWorld* W = GI->GetWorld();
		if (W && W->GetTimeSeconds() - HeroShownAt >= (double)HeroLife) { DismissHeroNotice(); }
	}

	// Липкая строка 2551 (mode 1): жизнь 15 000 мс (эталон uiNoticeTimeoutTick@0xBAD090).
	if (ServStickyShownAt >= 0.0)
	{
		UWorld* W = GI->GetWorld();
		if (W && W->GetTimeSeconds() - ServStickyShownAt >= 15.0)
		{
			ServStickyShownAt = -1.0;
			if (ServNoticeText) { ServNoticeText->SetVisibility(ESlateVisibility::Collapsed); }
		}
	}
}

bool UAsdaHudSubsystem::EnsureWidgets()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(ExpBarWidget))
	{
		return true;
	}
	ResetWidgets();

	// ★ SelfHitTestInvisible на корне: растянутый на вьюпорт UUserWidget с Visible съедает клики по миру
	//   (регрессия мини-карты 2026-08-01). Мышь получают только сами контролы.
	ExpBarWidget = WM->OpenWindowByPath(FName(TEXT("EXPBar_WIn")), kExpBarWbp, kExpBarZ, ESlateVisibility::SelfHitTestInvisible);
	NoticeWidget = WM->OpenWindowByPath(FName(TEXT("NOTICE_WIN")), kNoticeWbp, kNoticeZ, ESlateVisibility::SelfHitTestInvisible);
	HeroWidget   = WM->OpenWindowByPath(FName(TEXT("HERO_NOTICE_WIN")), kHeroWbp, kHeroZ, ESlateVisibility::SelfHitTestInvisible);
	if (!ExpBarWidget)
	{
		UE_LOG(LogAsdaHud, Error, TEXT("[HUD] WBP_EXPBar_WIn не загрузился (%s)"), kExpBarWbp);
		return false;
	}

	SetupExpControls();

	// NOTICE_WIN: контролы строк; окно инициализируется СКРЫТЫМ (эталон CChat_Init chat.c:450).
	if (NoticeWidget)
	{
		static const TCHAR* kTxt[3] = { TEXT("NOTICE_TXT00"), TEXT("NOTICE_TXT01"), TEXT("NOTICE_TXT02") };
		for (int32 i = 0; i < 3; ++i)
		{
			NoticeTxt[i] = NoticeWidget->GetWidgetFromName(kTxt[i]);
			if (UTextBlock* Tb = Cast<UTextBlock>(NoticeTxt[i])) { Tb->SetAutoWrapText(false); }   // закон autowrap
		}
		NoticeWidget->SetVisibility(ESlateVisibility::Collapsed);
		RefreshNoticeLines();   // строки могли пережить травел (данные в сабсистеме)
	}

	// HERO_NOTICE_WIN: текст + прозрачный клик-перехватчик (эталон: клик по 83401 = событие 0x18).
	if (HeroWidget)
	{
		HeroText = HeroWidget->GetWidgetFromName(TEXT("NOTICE_TXT00_C"));
		if (UTextBlock* Tb = Cast<UTextBlock>(HeroText)) { Tb->SetAutoWrapText(false); }
		if (UCanvasPanel* Root = Cast<UCanvasPanel>(HeroWidget->WidgetTree ? HeroWidget->WidgetTree->RootWidget : nullptr))
		{
			HeroClick = NewObject<UButton>(HeroWidget, UButton::StaticClass(), TEXT("HERO_NOTICE_ClickCatch"));
			FButtonStyle S;   // невидимая кнопка-реле (паттерн импортёра {idc}_OPT: планы=визуал, кнопка=ввод)
			S.Normal = FSlateNoResource(); S.Hovered = FSlateNoResource(); S.Pressed = FSlateNoResource();
			S.NormalPadding = FMargin(0); S.PressedPadding = FMargin(0);
			HeroClick->SetStyle(S);
			HeroClick->OnClicked.AddDynamic(this, &UAsdaHudSubsystem::OnHeroClicked);
			if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(HeroClick))
			{
				// Rect окна из JUI: (277, 139.3, 470, 44) на базе 1024×768 → span-анкор (доля экрана).
				CS->SetAnchors(FAnchors(277.f / 1024.f, 139.3f / 768.f, (277.f + 470.f) / 1024.f, (139.3f + 44.f) / 768.f));
				CS->SetOffsets(FMargin(0, 0, 0, 0));
			}
		}
		HeroWidget->SetVisibility(ESlateVisibility::Collapsed);
		if (HeroShownAt >= 0.0) { HeroShownAt = -1.0; }   // показ не переживает травел; очередь остаётся
		if (HeroQueue.Num() > 0)
		{
			FAsdaHeroNotice N = HeroQueue[0]; HeroQueue.RemoveAt(0);
			PresentHeroNotice(N);
		}
	}

	RefreshExpBar();
	UE_LOG(LogAsdaHud, Log, TEXT("[HUD] окна созданы: EXPBar_WIn(z=%d) NOTICE_WIN(z=%d) HERO_NOTICE_WIN(z=%d)"),
		kExpBarZ, kNoticeZ, kHeroZ);
	return true;
}

// ════════════════════════════════════ EXP-БАР ══════════════════════════════════════════════════
// Разбор контролов импортированного WBP_EXPBar_WIn (полный дамп окна — шапка .h):
//   MINISTATE_LEVELBAR_*_0 = план ES0 (подложка), _1 = план ES1 (зелёная заливка) → клипим _1;
//   MINISTATE_EXPTEXT = TextBlock процента; QUICK_EXPCount* = глифы цифр 0..9 (сопоставление по
//   ПОРЯДКУ узлов 1,2,3,4,5,6,7,8,9,0 — id 394 в JUI имеет опечатку idc), QUICK_EXPDot = точка (якорь).
void UAsdaHudSubsystem::SetupExpControls()
{
	bExpControlsReady = false;
	if (!ExpBarWidget || !ExpBarWidget->WidgetTree) { return; }

	TArray<UWidget*> All;
	ExpBarWidget->WidgetTree->GetAllWidgets(All);

	TArray<UImage*> BarPlanes;     // планы MINISTATE_LEVELBAR в порядке дерева (ES0, ES1)
	TArray<UImage*> DigitGlyphs;   // глифы QUICK_EXPCount в порядке дерева (1,2,3,4,5,6,7,8,9,0)
	TArray<UImage*> TickMarks;     // метки шкалы Box_DarkGrey_* (11 шт: каждые 10%)
	UImage* DotGlyph = nullptr;
	UImage* BaseImg = nullptr;     // ExpBar_Base_*_0 — тёмная подложка бара
	UImage* TopLineImg = nullptr;  // ExpBar_TopLine_*_0 — верхняя рамка

	for (UWidget* W : All)
	{
		const FString N = W->GetName();
		if (UImage* Im = Cast<UImage>(W))
		{
			if (N.StartsWith(TEXT("MINISTATE_LEVELBAR"))) { BarPlanes.Add(Im); }
			else if (N.StartsWith(TEXT("QUICK_EXPCount"))) { DigitGlyphs.Add(Im); }
			else if (N.StartsWith(TEXT("QUICK_EXPDot")) && !DotGlyph) { DotGlyph = Im; }
			else if (N.StartsWith(TEXT("ExpBar_Base")) && !BaseImg) { BaseImg = Im; }
			else if (N.StartsWith(TEXT("ExpBar_TopLine")) && !TopLineImg) { TopLineImg = Im; }
			else if (N.StartsWith(TEXT("Box_DarkGrey"))) { TickMarks.Add(Im); }
		}
		else if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("MINISTATE_EXPTEXT"))
			{
				// ★ КРУГ 3 (двоение цифр): этот TextBlock — caption скина Black_Ot125; в эталоне HUD
				//   текст процента сюда НЕ пишется (uiBar_SetPercent_Slot35 пишет в окно CHARAINFO).
				//   Процент в HUD = ТОЛЬКО атлас-цифры → caption гасим, остаётся чёрная подложка-image.
				Tb->SetVisibility(ESlateVisibility::Collapsed);
			}
			else if (N.StartsWith(TEXT("QUICK_EXPCount")) || N.StartsWith(TEXT("QUICK_EXPDot"))
				|| N.StartsWith(TEXT("QUICK_EXPPercent")))
			{
				// Caption-TextBlock скинов-глифов: визуал дают атлас-images, текст-дубли гасим.
				Tb->SetVisibility(ESlateVisibility::Collapsed);
			}
			else if (N == TEXT("CHAT_NEW_SERV_NOTICE"))
			{
				// ЛИПКАЯ строка серверного нотиса (id 2551): канал 3113 mode=1, 15 с
				//   (uiNoticeUpdate3Line ветка HIWORD==1 + uiNoticeTimeoutTick@0xBAD090).
				Tb->SetAutoWrapText(false);
				Tb->SetVisibility(ESlateVisibility::Collapsed);   // скрыта до первого mode=1
				ServNoticeText = Tb;
			}
		}
	}

	// ★★★ РАСТЯЖКА БАРА НА ШИРИНУ ЭКРАНА — ПЕРЕЕХАЛА В АССЕТ (2026-08-27, требование владельца
	//   «править в ассете, а не кодом»; аудит `Reports/audit_runtime_geometry.md`, класс П6).
	//   ЧТО БЫЛО ЗДЕСЬ. Лямбда `StretchFull` ставила подложке `ExpBar_Base` и рамке
	//   `ExpBar_TopLine` span-анкор X 0..1 с нулевыми отступами, а цикл раскладывал 11 меток
	//   `Box_DarkGrey_*` по point-анкорам i/(N−1) со сдвигом Left = 0 / −1 / −2.
	//   ПОЧЕМУ УБРАНО. Значения — чистые константы: они не зависят ни от данных сервера, ни от
	//   прокрутки, ни от разрешения (сама «доля экрана» и есть анкор). Выполнялось это один раз
	//   из `SetupExpControls()`, т.е. было отложенной правкой ассета в рантайме.
	//   ГДЕ ЖИВЁТ ТЕПЕРЬ. Прямо в `WBP_EXPBar_WIn`; запекает и восстанавливает после любой
	//   перегенерации UI идемпотентный шаг `Tools/asda_ui_geometry_bake.py` (блок G1),
	//   зарегистрированный в `Tools/asda_ui_post_import.STEPS`.
	//   ⚠ Требование эталона прежнее (HUD_UpdateExpBar, `ui_core.c:75284-75290`): бар обязан
	//   тянуться на всю ширину экрана — на ультрашироких мониторах это проверяется глазами.
	//   Списки `BaseImg` / `TopLineImg` / `TickMarks` выше оставлены как СТОРОЖ: если очередная
	//   перегенерация UI выкинет эти узлы, в логе это будет видно сразу, а не «бар едет странно».
	if (!BaseImg || !TopLineImg || TickMarks.Num() != 11)
	{
		UE_LOG(LogAsdaHud, Warning,
			TEXT("[EXP] состав WBP_EXPBar_WIn изменился: подложка=%d рамка=%d меток=%d (ждём 1/1/11) — "
			     "запусти Tools/asda_ui_geometry_bake.py (шаг G1)"),
			BaseImg ? 1 : 0, TopLineImg ? 1 : 0, TickMarks.Num());
	}

	// Заливка = ES1 (второй план; закон ui-progressbar-animate-existing: ES0=подложка).
	//   ★ Полная ширина берётся у ПОДЛОЖКИ ES0 (span 0..1): span заливки в импорте — статичный
	//     дизайн-процент (Rect 100/170 скина), а не «полный бар».
	ExpFill = (BarPlanes.Num() >= 2) ? BarPlanes[1] : (BarPlanes.Num() == 1 ? BarPlanes[0] : nullptr);
	if (ExpFill)
	{
		if (UCanvasPanelSlot* FS = AsdaUi::SlotOf(ExpFill))
		{
			// ★ КРУГ 3: полный диапазон анкора запоминается ЗДЕСЬ и навсегда (Min и Max). ApplyExpPct
			//   считает клип ТОЛЬКО от этих значений — прежний код читал span из текущих анкоров и
			//   после первого клипа-в-ноль (Min==Max) больше никогда не раскрывал полосу.
			FAnchors Full = FS->GetAnchors();
			if (BarPlanes.Num() >= 2)
			{
				if (UCanvasPanelSlot* BS = AsdaUi::SlotOf(BarPlanes[0]))
				{
					const FAnchors BA = BS->GetAnchors();   // подложка ES0 = полный бар (0..1 по X)
					Full.Minimum.X = BA.Minimum.X;
					Full.Maximum.X = BA.Maximum.X;
				}
			}
			ExpFillAnchorMinX = Full.Minimum.X;
			ExpFillAnchorMaxX = Full.Maximum.X;
			bExpFillSpan = (ExpFillAnchorMaxX > ExpFillAnchorMinX + KINDA_SMALL_NUMBER);
			ExpFillFullW = FS->GetOffsets().Right;          // фолбэк для point-анкора
			UE_LOG(LogAsdaHud, Log, TEXT("[EXP] заливка ES1='%s': span=%d анкорX=[%.3f..%.3f] fullW=%.0f"),
				*ExpFill->GetName(), bExpFillSpan ? 1 : 0, ExpFillAnchorMinX, ExpFillAnchorMaxX, ExpFillFullW);
		}
	}
	else
	{
		UE_LOG(LogAsdaHud, Warning, TEXT("[EXP] MINISTATE_LEVELBAR (заливка ES1) не найдена в WBP"));
	}

	// Брашы цифр 0..9: порядок узлов JUI = 1,2,3,4,5,6,7,8,9,0 (id 390..399, включая опечатку 394).
	static const int32 kNodeOrderDigit[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
	for (int32 i = 0; i < DigitGlyphs.Num() && i < 10; ++i)
	{
		const int32 D = kNodeOrderDigit[i];
		DigitBrush[D] = DigitGlyphs[i]->GetBrush();
		bDigitBrushValid[D] = true;
		DigitGlyphs[i]->SetVisibility(ESlateVisibility::Collapsed);   // эталон: все глифы погашены
	}

	// 4 клона цифр (эталон hudRenderNumberDigits: X-оффсеты {3,9,18,24} от id 35; клоны создавал
	//   CControl_InstantiateChild — у нас рантайм-UImage в том же канвасе, якорь = QUICK_EXPDot).
	ExpDigitClones.Reset();
	UCanvasPanelSlot* DotSlot = DotGlyph ? AsdaUi::SlotOf(DotGlyph) : nullptr;
	UCanvasPanel* Canvas = DotGlyph ? Cast<UCanvasPanel>(DotGlyph->GetParent()) : nullptr;
	if (DotSlot && Canvas)
	{
		// Дизайн JUI: точка (486,766); родитель id35 x=471 → оффсеты клонов от точки:
		//   десятки 474=dot−12, единицы 480=dot−6, десятые 489=dot+3, сотые 495=dot+9; y=766−4.
		static const float kDX[4] = { -12.f, -6.f, 3.f, 9.f };
		const FAnchors An = DotSlot->GetAnchors();
		const FMargin  Base = DotSlot->GetOffsets();
		for (int32 i = 0; i < 4; ++i)
		{
			UImage* C = NewObject<UImage>(ExpBarWidget, UImage::StaticClass());
			C->SetVisibility(ESlateVisibility::Collapsed);
			if (UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(C))
			{
				CS->SetAnchors(An);
				CS->SetAlignment(FVector2D(0.f, 0.f));
				CS->SetOffsets(FMargin(Base.Left + kDX[i], Base.Top - 4.f, 5.f, 5.f));   // глиф 5×5
			}
			ExpDigitClones.Add(C);
		}
	}
	else if (DigitGlyphs.Num() > 0)
	{
		UE_LOG(LogAsdaHud, Warning, TEXT("[EXP] QUICK_EXPDot не найден — атлас-цифры процента отключены"));
	}

	bExpControlsReady = true;
	ApplyExpPct(0.0);   // стартовое состояние: пустой бар (перекрывает статичный дизайн-процент импорта)
}

// Уровень/опыт: рантайм-поля NetSubsystem (4036/4038/4055/4056), сид — запись char-select.
bool UAsdaHudSubsystem::ResolveLevelExp(int32& OutLevel, int64& OutExp) const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	// ★ Дедуп №2, В2-A (U5c): уровень — общая точка `AsdaSelf::GetLevel` (то же правило: рантайм, иначе запись выбранного
	//   персонажа); опыт — тот же сид из записи выбранного персонажа (`AsdaSelf::FindSelectedCharRecord`). Поведение то же.
	OutLevel = AsdaSelf::GetLevel(Net);
	OutExp   = Net->SelfExp;
	if (OutExp < 0)
	{
		if (const FAsdaCharRecord* R = AsdaSelf::FindSelectedCharRecord(Net)) { OutExp = R->Exp; }
	}
	return OutLevel > 0 && OutExp >= 0;
}

// ★ int64 из двух половин строки (см. объявление): младшее слово — БЕЗЗНАКОВОЕ.
int64 UAsdaHudSubsystem::CharexpCumExp(const FAsdaCharexpRow& Row)
{
	return (int64)(((uint64)(uint32)Row.Zero2 << 32) | (uint64)(uint32)Row.CumExp);
}

// Формула эталона (HUD_UpdateExpBar / statBarUpdatePercent): pct=(Exp−CumExp[L−1])/Span·100,
//   Span=CumExp[L]−CumExp[L−1]. Таблица charexp: строка "L−1" = порог входа в уровень L.
bool UAsdaHudSubsystem::ComputeExpPct(int32 Level, int64 Exp, double& OutPct) const
{
	OutPct = 0.0;
	UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UDataTable* DT = Items ? Items->GetDataTable(FName(TEXT("charexp"))) : nullptr;
	if (!DT) { return false; }
	const FAsdaCharexpRow* Prev = DT->FindRow<FAsdaCharexpRow>(FName(*FString::FromInt(FMath::Max(Level - 1, 0))), TEXT("hud-exp"), false);
	const FAsdaCharexpRow* Cur  = DT->FindRow<FAsdaCharexpRow>(FName(*FString::FromInt(Level)), TEXT("hud-exp"), false);
	if (!Prev || !Cur) { return false; }   // 150 lvl = капа (следующей строки нет) → бар 0, как деление на 0 в эталоне
	// ★ Пороги — int64 (CharexpCumExp): с 64-го уровня младшее слово отрицательное.
	const double Base = (double)CharexpCumExp(*Prev);
	const double Span = (double)CharexpCumExp(*Cur) - Base;
	if (Span <= 0.0) { return false; }
	OutPct = FMath::Clamp(((double)Exp - Base) / Span * 100.0, 0.0, 100.0);
	return true;
}

// Диагностика EXP одной строкой (для asda.Hud.TestExp и разборов «почему 0%»).
FString UAsdaHudSubsystem::DebugExpState() const
{
	int32 L = 0; int64 E = 0;
	if (!ResolveLevelExp(L, E))
	{
		return FString::Printf(TEXT("нет данных: level=%d exp=%lld (SelfLevel/SelfExp не сидированы и char-запись пуста)"), L, (long long)E);
	}
	UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UDataTable* DT = Items ? Items->GetDataTable(FName(TEXT("charexp"))) : nullptr;
	const FAsdaCharexpRow* Prev = DT ? DT->FindRow<FAsdaCharexpRow>(FName(*FString::FromInt(FMath::Max(L - 1, 0))), TEXT("dbg"), false) : nullptr;
	const FAsdaCharexpRow* Cur  = DT ? DT->FindRow<FAsdaCharexpRow>(FName(*FString::FromInt(L)), TEXT("dbg"), false) : nullptr;
	if (!Prev || !Cur)
	{
		return FString::Printf(TEXT("level=%d exp=%lld: строки charexp %d/%d не найдены (таблица=%s)"),
			L, (long long)E, L - 1, L, DT ? TEXT("есть") : TEXT("НЕТ"));
	}
	double Pct = 0.0; ComputeExpPct(L, E, Pct);
	const int64 Lo = CharexpCumExp(*Prev);   // порог входа в уровень L (int64)
	const int64 Hi = CharexpCumExp(*Cur);    // порог следующего уровня (int64)
	FString Note;
	if (E < Lo) { Note = TEXT("  ★ exp НИЖЕ порога уровня (GM-левел без опыта?) → 0% как в оригинале"); }
	else if (E >= Hi) { Note = TEXT("  ★ exp ВЫШЕ порога следующего уровня (рассинхрон level?) → кламп 100%"); }
	return FString::Printf(TEXT("level=%d exp=%lld, диапазон уровня=[%lld..%lld), span=%lld -> %.2f%%%s"),
		L, (long long)E, (long long)Lo, (long long)Hi, (long long)(Hi - Lo), Pct, *Note);
}

// ★ Доля опыта текущего уровня (0..1) — общая точка для полосы HUD и полосы окна характеристик.
float UAsdaHudSubsystem::GetExpFraction() const
{
	int32 Level = 0; int64 Exp = 0;
	if (!ResolveLevelExp(Level, Exp)) { return -1.f; }
	double Pct = 0.0;
	if (!ComputeExpPct(Level, Exp, Pct)) { return -1.f; }
	return (float)FMath::Clamp(Pct / 100.0, 0.0, 1.0);
}

void UAsdaHudSubsystem::RefreshExpBar()
{
	int32 L = 0; int64 E = 0;
	if (!ResolveLevelExp(L, E)) { return; }
	double Pct = 0.0;
	ComputeExpPct(L, E, Pct);   // false → 0% (эталон: threshold<=0 → expPercent=0)
	ApplyExpPct(Pct);
	if (!FMath::IsNearlyEqual(Pct, LastExpPct, 0.005))
	{
		UE_LOG(LogAsdaHud, Log, TEXT("[EXP] %s"), *DebugExpState());
		LastExpPct = Pct;
	}
}

void UAsdaHudSubsystem::ApplyExpPct(double Pct)
{
	if (!bExpControlsReady) { return; }
	const float Frac = (float)(Pct / 100.0);

	// Заливка ES1 = клип ШИРИНЫ (эталон CProgressBar: Rect.right по значению; наш слот — анкорный
	//   span). ★ КРУГ 3: клип всегда от СОХРАНЁННОГО полного диапазона (не от текущих анкоров).
	if (ExpFill)
	{
		if (UCanvasPanelSlot* FS = AsdaUi::SlotOf(ExpFill))
		{
			if (bExpFillSpan)
			{
				FAnchors A = FS->GetAnchors();
				A.Minimum.X = ExpFillAnchorMinX;
				A.Maximum.X = ExpFillAnchorMinX + (ExpFillAnchorMaxX - ExpFillAnchorMinX) * Frac;
				FS->SetAnchors(A);
				FMargin Off = FS->GetOffsets();
				Off.Left = 0.f; Off.Right = 0.f;   // span: без пиксельных отступов
				FS->SetOffsets(Off);
			}
			else if (ExpFillFullW > 0.f)
			{
				FMargin Off = FS->GetOffsets();
				Off.Right = ExpFillFullW * Frac;   // point-анкор → клип пикселей (fullW·Frac)
				FS->SetOffsets(Off);
			}
		}
	}

	// ★ КРУГ 3: текст «%.2f %%» в HUD-баре НЕ выводится (эталон пишет его в окно CHARAINFO,
	//   uiBar_SetPercent_Slot35 this=&g_pCharInfoWnd) — иначе двоение с атлас-цифрами.

	UpdateExpDigits(Pct);
}

// hudRenderNumberDigits (ui_core.c:69470): разряды {сотые,десятые,единицы,десятки}; десятки при 0 скрыты.
void UAsdaHudSubsystem::UpdateExpDigits(double Pct)
{
	if (ExpDigitClones.Num() < 4) { return; }
	const int32 Digits[4] = {
		(int32)(Pct / 10.0) % 10,    // [0] десятки  (X −12 от точки; скрыт при 0)
		(int32)Pct % 10,             // [1] единицы  (X −6)
		(int32)(Pct * 10.0) % 10,    // [2] десятые  (X +3)
		(int32)(Pct * 100.0) % 10,   // [3] сотые    (X +9)
	};
	for (int32 i = 0; i < 4; ++i)
	{
		UImage* C = ExpDigitClones[i];
		if (!C) { continue; }
		const int32 D = Digits[i];
		const bool bShow = (i != 0 || D != 0) && bDigitBrushValid[D];
		if (bShow)
		{
			C->SetBrush(DigitBrush[D]);
			C->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			C->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

// Рантайм-дамп контролов EXP-бара — доказательные строки для asda.Hud.TestExp
//   (закон full-structural-analysis: видимость/геометрия КАЖДОГО контрола группы).
void UAsdaHudSubsystem::DumpExpWidgets() const
{
	auto Vis = [](const UWidget* W) -> const TCHAR*
	{
		if (!W) { return TEXT("NULL"); }
		switch (W->GetVisibility())
		{
		case ESlateVisibility::Visible:               return TEXT("Visible");
		case ESlateVisibility::Collapsed:             return TEXT("Collapsed");
		case ESlateVisibility::Hidden:                return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible:      return TEXT("HitTestInv");
		case ESlateVisibility::SelfHitTestInvisible:  return TEXT("SelfHitTestInv");
		default:                                      return TEXT("?");
		}
	};
	if (!ExpBarWidget || !ExpBarWidget->WidgetTree)
	{
		UE_LOG(LogAsdaHud, Display, TEXT("[EXP-дамп] окно EXPBar_WIn не создано"));
		return;
	}
	// Заливка: анкоры/offsets/видимость (главное доказательство ширины клипа).
	if (ExpFill)
	{
		if (const UCanvasPanelSlot* FS = AsdaUi::SlotOf(ExpFill))
		{
			const FAnchors A = FS->GetAnchors();
			const FMargin O = FS->GetOffsets();
			UE_LOG(LogAsdaHud, Display,
				TEXT("[EXP-дамп] заливка '%s': vis=%s анкорX=[%.3f..%.3f] (полный [%.3f..%.3f]) off=(%.0f,%.0f,%.0f,%.0f)"),
				*ExpFill->GetName(), Vis(ExpFill), A.Minimum.X, A.Maximum.X,
				ExpFillAnchorMinX, ExpFillAnchorMaxX, O.Left, O.Top, O.Right, O.Bottom);
		}
	}
	else { UE_LOG(LogAsdaHud, Display, TEXT("[EXP-дамп] заливка ES1: НЕ НАЙДЕНА")); }
	// Клоны цифр.
	for (int32 i = 0; i < ExpDigitClones.Num(); ++i)
	{
		const UImage* C = ExpDigitClones[i];
		const UCanvasPanelSlot* S = C ? AsdaUi::SlotOf(C) : nullptr;
		UE_LOG(LogAsdaHud, Display, TEXT("[EXP-дамп] клон-цифра[%d]: vis=%s x=%.0f"),
			i, Vis(C), S ? S->GetOffsets().Left : -1.f);
	}
	// Группа процента: каждый контрол с видимостью (ловля «двоения»).
	TArray<UWidget*> All;
	ExpBarWidget->WidgetTree->GetAllWidgets(All);
	for (const UWidget* W : All)
	{
		const FString N = W->GetName();
		if (N.StartsWith(TEXT("MINISTATE_EXPTEXT")) || N.StartsWith(TEXT("QUICK_EXP")))
		{
			UE_LOG(LogAsdaHud, Display, TEXT("[EXP-дамп] %s [%s]: vis=%s"),
				*N, *W->GetClass()->GetName(), Vis(W));
		}
	}
}

void UAsdaHudSubsystem::OnSelfExp(int32 Level, int64 Exp, int64 Gained, bool bFullExp, int32 SrcRenderId)
{
	// Эталон: ветка A обновляет бар ДО показа текста, ветки B/C — ПОСЛЕ (charTickExpBarUpdate).
	//   На результат порядок не влияет (бар и надпись независимы), поэтому бар считаем первым:
	//   так процент над квикбаром гарантированно свежий к моменту появления надписи.
	RefreshExpBar();
	if (Gained > 0)
	{
		ShowExpGain(Gained, bFullExp, SrcRenderId);
	}
}

// ══════════════════════════ ★★★ ПОКАЗ ПОЛУЧЕНИЯ ОПЫТА (порт 4038) ═════════════════════════════
// Эталон `netHandleHpExpUpdate@0xF427A0` (полный дизассемблер-разбор — `Reports/exp_gain_ui.md`).
// ТРИ ВЕТКИ, у каждой своя цель показа, свой стиль и свой набор строк чата:
//   A  flag==1            → над СВОИМ персонажем, стиль 0x13, чат loc 100900 + loc 2316+2317;
//   B  flag!=1, rid==−1   → над СВОИМ персонажем, стиль 0x13, чат loc 100900;
//   C  flag!=1, rid>=0    → над СУЩНОСТЬЮ rid (убитый моб), стиль 0x14, чат loc 100900.
// Каждая ветка спавнит ДВЕ надписи: число прибавки и глиф-слово «EXP» (в русском атласе «ОПЫТ+»).
// Канал показа — общий `UAsdaFloatingTextSubsystem` (тот же, что цифры урона): второй системы
// экранного текста не заводим. Строки чата — общий `UAsdaMessageSubsystem`, категория Progress
// (маршрут «журнал + буфер B» = эталонный `CChat_AddTokenizedLines(track=1)`).
void UAsdaHudSubsystem::ShowExpGain(int64 Gained, bool bFullExp, int32 SrcRenderId)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	// ── 1. Куда рисовать: свой павн или сущность по renderId ──
	const AActor* Over = nullptr;
	int32 Style = 0x13;
	if (!bFullExp && SrcRenderId >= 0)
	{
		// Ветка C: `Entity_FindByRenderId(packet+17)`. Единая развилка порта (закон «одна точка»).
		if (const UAsdaWorldSubsystem* W = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			Over = W->ActorByTarget(EAsdaTargetKind::Entity, SrcRenderId);
		}
		Style = 0x14;
	}
	else if (const APlayerController* PC = GI->GetFirstLocalPlayerController())
	{
		Over = PC->GetPawn();
	}

	// ── 2. Всплывающая надпись ──
	// Эталонные ЭКРАННЫЕ оффсеты от проекции центра сущности (константы 150/108/103 сняты
	//   по инструкциям 0xF427A0):
	//     ветка A: число (X−42, Y−2), «EXP» (X−150, Y);
	//     ветки B/C: число (X+103, Y−2), «EXP» (X, Y).
	// ★★★ ЯКОРЬ — ДОЛЕЙ, А НЕ ПИКСЕЛЯМИ (кадр живого PIE показал, почему это важно).
	//   Эталон задаёт ТОЧКУ (X, Y), от которой квады строятся так:
	//     цифры (`uiDrawTexturedQuad`): глиф i занимает [X + 2si − 1.5s , X + 2si + 0.5s]
	//                                   по X и [Y − 1.5s , Y + 0.5s] по Y;
	//     слово (`uiSpriteDrawDigitQuadD`): [X − 5s , X + 4s] × [Y − 1.5s , Y + 0.5s].
	//   Полуразмер `s` ЖИВЁТ (10 → 30 → 17), поэтому пиксельная компенсация верна только для
	//   одного кадра: с ней надписи налезали друг на друга на пике «поп-ина». Но ДОЛЯ точки
	//   внутри виджета от `s` не зависит: цифры → 1.5s/(2ns) = 0.75/n по X, слово → 5s/9s = 5/9;
	//   по Y обе → 1.5s/2s = 0.75. Их и передаём — позиция эталонная на ЛЮБОМ кадре анимации.
	if (Over)
	{
		if (UAsdaFloatingTextSubsystem* FT = GI->GetSubsystem<UAsdaFloatingTextSubsystem>())
		{
			const FString Digits = FString::Printf(TEXT("%lld"), (long long)Gained);
			const float NumX  = bFullExp ? -42.f : 103.f;   // эталонная X числа
			const float WordX = bFullExp ? -150.f : 0.f;    // эталонная X слова «ОПЫТ+»
			FT->SpawnOverActor(Style, Digits, Over, FVector2D(NumX, -2.f),
				FVector2D(0.75 / FMath::Max(1, Digits.Len()), 0.75));
			FT->SpawnOverActor(Style, UAsdaFloatingTextSubsystem::ExpWordKey(), Over,
				FVector2D(WordX, 0.f), FVector2D(5.0 / 9.0, 0.75));
		}
	}
	else
	{
		// Эталон в этом случае (сущность despawn'нулась) текста не рисует — только бар. Логируем,
		//   чтобы «надписи нет» не выглядело как пропажа канала.
		UE_LOG(LogAsdaHud, Log,
			TEXT("[EXP] +%lld: цель показа не найдена (rid=%d) — надпись не рисуется (как в эталоне)"),
			(long long)Gained, SrcRenderId);
	}

	// ── 3. Строки чата ──
	UAsdaMessageSubsystem* Msg = GI->GetSubsystem<UAsdaMessageSubsystem>();
	UAsdaTextSubsystem*    Txt = GI->GetSubsystem<UAsdaTextSubsystem>();
	if (Msg && Txt)
	{
		// loc 100900 «Получен опыт: %d.» — это printf-ФОРМАТ, а не слоты [ssN]:
		//   эталон `sprintf(buf, UI_FormatLocalizedNotice(ctx,100900,0,1), lo, hi)`.
		FAsdaMsgParams P;
		P.Category  = EAsdaMsgCategory::Progress;   // маршрут «журнал + буфер B», цвет Lime = −16711936
		P.SrcOpcode = 4038;
		P.LocId     = 100900;   // ← чтобы сработал Fallback, если строки в DT_GameText нет
		P.Text      = Txt->FormatPrintfById(100900, { FString::Printf(TEXT("%lld"), (long long)Gained) });
		// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки 100900 → «[#100900]» (общая точка UAsdaMessageSubsystem).
		Msg->Post(P);

		// Наградной опыт: вдобавок оранжевая строка `sprintf("%s%s", loc 2316, loc 2317)`
		//   (`HUD_UpdateExpBar`, ui_core.c:75299-75305; цвет −32768 = FFFF8000, TTL 10 с).
		//   ★ ДЕФЕКТ ОРИГИНАЛА: 2316 = «Получен опыт», 2317 = «Получены деньги» — склейка без
		//   разделителя даёт «Получен опытПолучены деньги». Исправлено разделителем (закон
		//   [[fix-original-defects]]); сами строки и цвет — эталонные.
		if (bFullExp)
		{
			const FString A = Txt->GetTextById(2316);
			const FString B = Txt->GetTextById(2317);
			const FString Joined = (A.IsEmpty() || B.IsEmpty()) ? (A + B) : (A + TEXT(". ") + B + TEXT("."));
			if (!Joined.IsEmpty())
			{
				FAsdaMsgParams R;
				R.Category  = EAsdaMsgCategory::Progress;
				R.SrcOpcode = 4038;
				R.Text      = Joined;
				R.bUseColor = true;
				R.Color     = FColor(0xFF, 0x80, 0x00, 0xFF);   // −32768 в sRGB
				R.TtlMs     = 10000.f;
				Msg->Post(R);
			}
		}
	}

	UE_LOG(LogAsdaHud, Log, TEXT("[EXP] +%lld опыта (%s), показ над %s стилем 0x%X; %s"),
		(long long)Gained, bFullExp ? TEXT("наградной") : TEXT("обычный"),
		Over ? *Over->GetName() : TEXT("<цели нет>"), Style, *DebugExpState());
}

// ★★★ 4036 — ПОВЫШЕНИЕ УРОВНЯ. Дословно `Recv_LevelUp@0xF424C0` (network.c:42303-42360):
//       posY += 10.0;                                            // эффект чуть выше ног
//       …обновление HUD/окон, только для СВОЕГО персонажа…
//       Audio_Play3DSound(posX, posY, posZ, "S_c_levelup.wav", 0);
//       CEffectMgr_CreateEffect_F561F0("eff_0_char_99_lvup01-a_00.jeb", 1, posX, posY, posZ, 0, 1.0f);
//   ★ Звук и эффект в эталоне идут ВНЕ гейта «свой игрок» — их видно и слышно и у соседей.
//     Здесь обрабатывается только СВОЙ (делегат `OnSelfLevelUp`); чужой уровень приходит своим
//     каналом в мировую подсистему.
//   ★ Смещение +10 — по игровой оси «вверх» (в эталоне это Y). У нас вверх — Z, поэтому 10 юнитов
//     кладём в Z. Единица та же: игровые юниты, масштаб мира 1:1.
// ★★★ БАННЕР ПОВЫШЕНИЯ УРОВНЯ — надпись на экране.
//   Эталон: `changeJobBannerShow(g_pChangeJobBanner, 1, 2000, 500, 500)` (ui_core.c:17985).
//       bannerType = 1  → надпись ПОВЫШЕНИЕ УРОВНЯ (тип 2 — получение звания);
//       posX  = 2000    → сколько держать; posY = 500 и param4 = 500 → интервалы движения.
//   Появляется сверху: `bannerStartY = якорь − высота экрана / 4`, дальше едет к якорю.
//
//   ⚠⚠ ГРАБЛИ, НАСТУПАЛ 24.08.2026: сперва я взял окно `WBP_ChangeJob_Banner` — это СМЕНА
//   ПРОФЕССИИ (текстура `n_07_changejob`, контролы `ChangeJob_*`), совсем другой баннер.
//   Нужный контейнер называется в эталоне `conBanner2`, и в проекте ему точно соответствует
//   `WBP_Con_Banner2` с тем же составом: рамка, по две шестерни слева и справа, три полосы и
//   ДВЕ надписи — `Con_Banner2_LevelUp` (уровень) и `Con_Banner2_GetTitle` (звание).
//   Тип баннера как раз и выбирает, какую из двух надписей показать.
//
// ★★★ 2026-08-30: ветка сделана ОБЩЕЙ ТОЧКОЙ на два типа. Прежде тип 1 был зашит, и надпись
//   `Con_Banner2_GetTitle` гасилась ВСЕГДА («звание — не наш случай»), поэтому баннер получения
//   звания (`changeJobBannerShow(…, 2, 500, 500, 500)` из `uiTitle_OnJobChangeResult@0xE9A9F0`)
//   в порту не показывался вовсе. Теперь тип и время показа — параметры.
// ★ Дедуп №2, В2-B (U11): высота, с которой въезжает баннер уровня/звания (четверть экрана, эталон) — одна функция для
//   тика и для строки лога показа (лог — доказательство «было/стало» при масштабе UI ≠ 1).
//   ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U11): высота экрана — в ЕДИНИЦАХ UI (`AsdaUi::ViewportSizeUnits`), а не в пикселях:
//   сдвиг рендер-трансформа баннера задаётся в единицах, и при масштабе UI ≠ 1 пиксельная четверть экрана уводила старт
//   выше (масштаб 1,5: 192 ед. = 288 px = 37,5 % экрана вместо 25 %). При масштабе 1 (сейчас у проекта) — то же число.
static float HudLevelUpRiseFrom(const UWorld* W)
{
	float ViewY = 1080.f;
	if (W)
	{
		const FVector2D Sz = AsdaUi::ViewportSizeUnits(W);
		if (Sz.Y > 1.f) { ViewY = (float)Sz.Y; }
	}
	return ViewY * 0.25f;
}

void UAsdaHudSubsystem::ShowBanner(int32 BannerType, float HoldSeconds)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (!W) { return; }
	// ★ 06.10.2026 (волна 2-В) [ОРИГИНАЛ]: `changeJobBannerShow` (ui_core.c:18018-18023) не стартует, пока идёт баннер
	//   СМЕНЫ ПРОФЕССИИ 18500 (`startActive == 1`) — оба баннера живут в одном `CChangeJobBanner` и не перекрываются.
	if (UAsdaChangeJobBannerSubsystem* Cj = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr)
	{
		if (Cj->IsStartActive())
		{
			UE_LOG(LogAsdaHud, Log, TEXT("[баннер] тип %d пропущен: идёт баннер смены профессии 18500 (startActive)"), BannerType);
			return;
		}
	}

	if (!LevelUpBanner)
	{
		const FString Path = TEXT("/Game/AsdaAssets/UI/Player_UI/Dungeon/WBP_Con_Banner2.WBP_Con_Banner2_C");
		UClass* Cls = LoadClass<UUserWidget>(nullptr, *Path);
		if (!Cls)
		{
			UE_LOG(LogAsdaHud, Warning, TEXT("[уровень] окно баннера не загрузилось: %s"), *Path);
			return;
		}
		LevelUpBanner = CreateWidget<UUserWidget>(W, Cls);
		if (!LevelUpBanner) { return; }
	}

	// Показ по типу 1: надпись уровня видна, надпись звания скрыта. Рамка и шестерни — видны,
	//   полосы эталон на этом шаге прячет (`SetVisible(bar, 0)`).
	auto SetByPrefix = [this](const TCHAR* Prefix, bool bVisible)
	{
		if (!LevelUpBanner || !LevelUpBanner->WidgetTree) { return; }
		TArray<UWidget*> All;
		LevelUpBanner->WidgetTree->GetAllWidgets(All);
		for (UWidget* Wg : All)
		{
			if (Wg && Wg->GetName().StartsWith(Prefix))
			{
				Wg->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible
				                           : ESlateVisibility::Collapsed);
			}
		}
	};
	const bool bTitle = (BannerType == 2);              // 2 = получение звания, 1 = повышение уровня
	SetByPrefix(TEXT("Con_Banner2_GetTitle"), bTitle);  // ★ надпись «звание получено»
	SetByPrefix(TEXT("Con_Banner2_Bar"),      false);   // полосы эталон здесь прячет
	SetByPrefix(TEXT("Con_Banner2_Gear"),     true);    // шестерни по обе стороны
	SetByPrefix(TEXT("Con_Banner2_LevelUp"),  !bTitle); // ★ надпись «повышение уровня»

	if (!LevelUpBanner->IsInViewport()) { LevelUpBanner->AddToViewport(900); }
	// ★ 21.10.2026 (Alt+Z): прячется вместе с интерфейсом — у оригинала это окно JUI блока бита 128 (render_core.c:1482).
	UAsdaUiVisibilitySubsystem::TrackFor(this, LevelUpBanner);
	LevelUpBanner->SetVisibility(ESlateVisibility::HitTestInvisible);
	LevelUpBannerStartS = W->GetTimeSeconds();
	LevelUpBannerHoldS  = FMath::Max(HoldSeconds, 0.f);
	LevelUpBannerActive = true;
	UE_LOG(LogAsdaHud, Log, TEXT("[баннер] тип %d (%s), показ %.2f с, въезд с высоты −%.1f ед. (масштаб UI %.2f)"),
		BannerType, bTitle ? TEXT("звание получено") : TEXT("повышение уровня"), LevelUpBannerHoldS,
		HudLevelUpRiseFrom(W), AsdaUi::ViewportDpiScale(W));
}

// ★★★ Баннер «звание получено» — эталонные 500/500/500 (`changeJobBannerShow(…, 2, 500, 500, 500)`).
//   Гейты «свой / получено / окно закрыто» стоят У ВЫЗЫВАЮЩЕГО (`UAsdaTitleSubsystem::HandleGranted`),
//   ровно как в эталоне: там они окружают вызов, а не живут внутри баннера.
void UAsdaHudSubsystem::ShowTitleBanner()
{
	ShowBanner(/*тип 2 = звание*/ 2, /*показ*/ 0.5f);
}

// Ход баннера: въезд сверху 0.5 с → показ 2 с → угасание 0.5 с. Числа — эталонные (2000/500/500).
void UAsdaHudSubsystem::TickLevelUpBanner(float /*DeltaSeconds*/)
{
	if (!LevelUpBannerActive || !LevelUpBanner) { return; }
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }
	const float T = (float)(W->GetTimeSeconds() - LevelUpBannerStartS);
	// ★ Показ (`HoldS`) задаёт вызывающий: 2.0 с у уровня (эталон 2000), 0.5 с у звания (эталон 500).
	//   Въезд и угасание у обоих типов одинаковы — 500/500 (второй и третий аргументы эталона).
	const float SlideS = 0.5f, HoldS = LevelUpBannerHoldS, FadeS = 0.5f;
	// Стартовая высота: четверть экрана вверх, как в эталоне.
	const float UpFrom = HudLevelUpRiseFrom(W);

	float OffsetY = 0.f, Alpha = 1.f;
	if (T < SlideS)                       { OffsetY = -UpFrom * (1.f - T / SlideS); }
	else if (T < SlideS + HoldS)          { OffsetY = 0.f; }
	else if (T < SlideS + HoldS + FadeS)  { Alpha = 1.f - (T - SlideS - HoldS) / FadeS; }
	else
	{
		LevelUpBanner->RemoveFromParent();
		LevelUpBannerActive = false;
		return;
	}
	FWidgetTransform Tr; Tr.Translation = FVector2D(0.f, OffsetY);
	LevelUpBanner->SetRenderTransform(Tr);
	LevelUpBanner->SetRenderOpacity(Alpha);
}

void UAsdaHudSubsystem::OnSelfLevelUp()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2D(FName(TEXT("S_c_levelup")));
	}
	// Эффект — через ту же единую точку спавна мировых эффектов, что и боевые импакты.
	if (UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		if (AActor* Me = WS->LocalPlayerActor)
		{
			const FVector At = Me->GetActorLocation() + FVector(0.f, 0.f, 10.f);
			WS->SpawnImpact(TEXT("BP_Fx_eff_0_char_99_lvup01_a_00"), At);
		}
	}
	ShowBanner(/*тип 1 = уровень*/ 1, /*показ, эталон 2000 мс*/ 2.0f);
	RefreshExpBar();
}

// ═══════════════════════════════════ NOTICE_WIN (3113) ═════════════════════════════════════════
// Сырой текст (inline-теги <FC:>/<FC=>) + базовый RGB пакета → форматированный показ в контроле.
//   Эталон: JUI-рендер парсит теги нативно (textParseColorTag), базовый цвет = SetField923(RGB);
//   </F> сбрасывает к базовому → обёртка <FC=FFRRGGBB> в начале + реставрация после каждого </F>.
UWidget* UAsdaHudSubsystem::ApplyNoticeText(UWidget* Ctrl, const FString& RawText, const FColor& Rgb,
	const TCHAR* StyleTag)
{
	if (!Ctrl) { return nullptr; }
	// ★★★ ИСПРАВЛЕНО 2026-08-10 (К1 аудита `code_dedup_audit_2.md`). БЫЛО: «скин NoImage_Clip
	//   TextOutLine=1 → обводка (<FO>)» — и обводка вешалась на ВСЕ четыре контрола нотисов.
	//   Прочтение ОТМЕНЕНО: `TextOutLine` = поле контрола **925**, и в `cControl_RenderLabel`
	//   (ui_core.c:110741-110746) оно выбирает ТОЛЬКО флаги вывода текста
	//   (`field925 == 1 ? …|0x2010 : …|0x100`, то есть перенос по словам + клип), а НЕ обводку.
	//   Стиль даёт поле **926** (`CClass_SetField926`), и эталон проставляет его ПОШТУЧНО:
	//     chat.c:442  NOTICE_TXT00 (229) → 0x200000 = ЖИРНЫЙ
	//     chat.c:443  NOTICE_TXT02 (231) → 0x200000 = ЖИРНЫЙ
	//     chat.c:444  CHAT_NOTICE  (227) → dword_800000 = ОБВОДКА
	//     chat.c:645  HeroText   (83401) → dword_800000 = ОБВОДКА
	//     NOTICE_TXT01 (230) — поле 926 НЕ ставится вовсе, стиля нет.
	//   Поэтому стиль приходит ОТ ВЫЗЫВАЮЩЕГО (`StyleTag`), а не подмешивается тут всем подряд.
	//   Базовый цвет = RGB пакета (<FC=FF..>); </F> сбрасывает к базовому.
	const FString BaseTag = FString::Printf(TEXT("<FC=FF%02X%02X%02X>%s"), Rgb.R, Rgb.G, Rgb.B,
		StyleTag ? StyleTag : TEXT(""));
	FString Wrapped = BaseTag + RawText.Replace(TEXT("</F>"), *(FString(TEXT("</F>")) + BaseTag));
	UWidget* W = UAsdaRichText::ApplyFormatted(Ctrl, Wrapped);
	// ★ КРУГ 3: JUI CaptionAlign=5 (0x1 hCenter | 0x4 vCenter) у ВСЕХ четырёх TXT — rich-замена
	//   теряла выравнивание импортированного TextBlock (рендерился слева) → центр возвращаем явно.
	if (URichTextBlock* R = Cast<URichTextBlock>(W))
	{
		R->SetJustification(ETextJustify::Center);
	}
	return W;
}

// ★★★ Эталон uiNoticeUpdate3Line_BC90@0xBABC90 (ui_core.c:12645-12652), шаг 1 из трёх:
//     if (mode == 100) { v6 = atoi(buf+1888); v2 = UI_FormatLocalizedNotice(ctx, 10221, 0, 1);
//                        sprintf(buf+1888, v2, v6); mode = 0; }
//   Тело пакета в режиме 100 несёт АРГУМЕНТ (число минут), а не текст. Разворот обязан идти ДО
//   раздачи потребителям — и в эталоне uiChatLogAddLine (дубль в чат) стоит СТРОКОЙ НИЖЕ sprintf.
FString UAsdaHudSubsystem::ResolveNoticeText(int32 Mode, const FString& RawText) const
{
	if (Mode != 100) { return RawText; }
	const UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	FString Fmt = Txt ? Txt->GetTextById(10221) : FString();
	if (Fmt.IsEmpty())
	{
		UE_LOG(LogAsdaHud, Warning,
			TEXT("[NOTICE 3113 mode=100] строки 10221 нет в DT_GameText — кодовый фолбэк «Битва начнется через %%d мин.»"));
		Fmt = TEXT("[#10221] %d");   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка (минуты остаются видны)
	}
	// %d в шаблоне обязателен: без него аргумент потерялся бы молча (строка «Битва начнется…» без минут).
	if (!Fmt.Contains(TEXT("%d")))
	{
		UE_LOG(LogAsdaHud, Warning,
			TEXT("[NOTICE 3113 mode=100] в строке 10221 «%s» НЕТ подстановки %%d — минуты (%s) не попадут в текст"),
			*Fmt, *RawText);
		return Fmt;
	}
	return Fmt.Replace(TEXT("%d"), *FString::FromInt(FCString::Atoi(*RawText)));
}

void UAsdaHudSubsystem::ShowServerNotice(int32 Mode, const FString& Text, const FColor& InRgb)
{
	// ★ КРУГ 4: RGB(0,0,0) в пакете = «цвет не задан» → БЕЛЫЙ. Наш кампвар-контроллер шлёт нули
	//   (сервер лишь ретранслирует тело: sendNoticeClient@0x547A80 memcpy 0x6B), клиентский код
	//   сентинела не имеет (v34 = байты|FF000000 → чёрный), но юзер подтверждает: в оригинале
	//   текст войны БЕЛЫЙ (скрин = ground truth). Явно заданные цвета (..notice) — как в пакете.
	const FColor Rgb = (InRgb.R == 0 && InRgb.G == 0 && InRgb.B == 0) ? FColor::White : InRgb;
	// ★ КРУГ 4: режимы ≠ 0/1/100 в эталоне НЕ обрабатываются (Begin: только SHIWORD==1;
	//   Line: только HIWORD==0/1; BC90: только ==100) → игнор, как оригинал.
	if (Mode != 0 && Mode != 1 && Mode != 100)
	{
		UE_LOG(LogAsdaHud, Warning, TEXT("[NOTICE 3113] неизвестный режим %d — игнор (эталон)"), Mode);
		return;
	}
	if (Mode == 1)
	{
		// ★ КРУГ 2 (до-реверс): mode 1 — НЕ «убрать всё», а ЛИПКАЯ строка CHAT_NEW_SERV_NOTICE
		//   (id 2551, uiNoticeUpdate3Line ветка HIWORD==1: SetText+SetField923(RGB)+SetVisible 1),
		//   жизнь 15 000 мс (uiNoticeTimeoutTick@0xBAD090).
		ShowServSticky(Text, Rgb);
		return;
	}
	// Режим 100 → подстановка минут в loc 10221, дальше строка живёт как режим 0 (BC90 обнуляет
	//   mode-word). ★ На тракте 3113 разворот УЖЕ сделан в обработчике пакета (сюда приходит
	//   Mode 0 с готовым текстом) — вызов здесь идемпотентен и обслуживает фолбэк-путь и консоль
	//   `asda.Hud.Notice`, которые зовут ShowServerNotice напрямую, минуя маршрутизатор.
	const FString T = ResolveNoticeText(Mode, Text);
	FAsdaNoticeLine L;
	L.Text = T;
	L.Rgb  = FColor(Rgb.R, Rgb.G, Rgb.B, 255);   // базовый цвет строки (sRGB; linear делает рендер тегов)
	// ★ Волна В6 ремесла (ревизия В5 МЕЛК-6): монотонные часы, как плашка 221 (`AsdaChatSubsystem::TickWarExpiry`).
	//   Строки переживают смену карты (сбрасываются только полным сбросом сессии), а `World->GetTimeSeconds()` у нового
	//   мира начинается с нуля — рождение от старого мира давало отрицательный возраст, и строка висела. Эталон тоже
	//   мерит время системными часами (`timeGetTime`), а не часами мира.
	L.Born = FPlatformTime::Seconds();
	NoticeLines.Add(L);
	// ★ 10.10.2026 (`Reports/owner_1010/overlaps_fixes.md`, МЕЛК-1): буферов строк в эталоне три (+2056/+2156/+2256), но
	//   при третьем объявлении `uiNoticeUpdate3LineBegin` (ветка v36 == 2, ui_core.c:12780-12795) сдвигает последнюю строку
	//   в +2156, новую кладёт в +2256, а `uiNoticeUpdate3Line` (a3 == 2, ui_core.c:12843-12866) обнуляет старшую +2056 —
	//   старшая строка ВЫБРАСЫВАЕТСЯ. Живых строк не больше двух (было: 3, и на экране стояли все три внахлёст).
	while (NoticeLines.Num() > 2) { NoticeLines.RemoveAt(0); }
	RefreshNoticeLines();
	UE_LOG(LogAsdaHud, Log, TEXT("[NOTICE 3113] mode=%d «%s» rgb=(%d,%d,%d)"), Mode, *T, Rgb.R, Rgb.G, Rgb.B);
}

// mode 1: липкая строка 2551 (15 с; повторный нотис перезапускает таймер — эталон Begin сбрасывает
//   буфер +2356 и заново SetVisible).
void UAsdaHudSubsystem::ShowServSticky(const FString& Text, const FColor& Rgb)
{
	if (!ServNoticeText)
	{
		UE_LOG(LogAsdaHud, Warning, TEXT("[NOTICE 3113 mode=1] CHAT_NEW_SERV_NOTICE не найден — липкий нотис пропущен: %s"), *Text);
		return;
	}
	// 2551 CHAT_NEW_SERV_NOTICE: эталон `CClass_SetField926(self->chatServNotice, dword_800000)` — ОБВОДКА.
	if (UWidget* Nw = ApplyNoticeText(ServNoticeText, Text, Rgb, TEXT("<FO>"))) { ServNoticeText = Nw; }
	ServNoticeText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	ServStickyShownAt = W ? W->GetTimeSeconds() : 0.0;
	UE_LOG(LogAsdaHud, Log, TEXT("[NOTICE 3113 mode=1] липкая строка 2551 «%s» rgb=(%d,%d,%d), 15 с"),
		*Text, Rgb.R, Rgb.G, Rgb.B);
}

void UAsdaHudSubsystem::TickNoticeExpiry()
{
	if (NoticeLines.Num() == 0) { return; }
	const double Now = FPlatformTime::Seconds();   // ★ волна В6: те же монотонные часы, что у `Born`
	bool bChanged = false;
	for (int32 i = NoticeLines.Num() - 1; i >= 0; --i)
	{
		if (Now - NoticeLines[i].Born >= kNoticeLineLifeS) { NoticeLines.RemoveAt(i); bChanged = true; }
	}
	if (bChanged) { RefreshNoticeLines(); }
}

// Раскладка строк по контролам (uiNoticeUpdate3Line, JUI-геометрия: TXT01 y99 верх, TXT00 y107
//   центр, TXT02 y116 низ): 1 строка → центр; 2 → верх(старая)+низ(новая).
//   ★ 10.10.2026 (МЕЛК-1): TXT00 эталон НИКОГДА не показывает вместе с TXT01/TXT02 — каждая ветка `uiNoticeUpdate3Line`
//   (a3 = 0 / 1 / 2, ui_core.c:12820-12890) сначала гасит все три, затем включает либо TXT00, либо пару TXT01+TXT02.
//   Было: при трёх объявлениях — все три строки сразу, с шагом 8–9 px внахлёст (TXT00 y107 между y99 и y116).
void UAsdaHudSubsystem::RefreshNoticeLines()
{
	if (!NoticeWidget) { return; }
	auto Set = [this](int32 Slot, const FAsdaNoticeLine* L)
	{
		UWidget* T = NoticeTxt[Slot];
		if (!T) { return; }
		if (L)
		{
			// Форматированный показ (inline-теги + базовый RGB пакета); виджет мог смениться на rich.
			// ★ Стиль поля 926 ПОШТУЧНО, как в эталоне (chat.c:442-443): TXT00 и TXT02 — ЖИРНЫЙ
			//   (0x200000), TXT01 — без стиля (926 ему не ставят вовсе).
			const TCHAR* StyleTag = (Slot == 1) ? TEXT("") : TEXT("<FB>");
			if (UWidget* Nw = ApplyNoticeText(T, L->Text, L->Rgb, StyleTag)) { NoticeTxt[Slot] = Nw; T = Nw; }
			T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			T->SetVisibility(ESlateVisibility::Collapsed);
		}
	};
	const int32 N = NoticeLines.Num();
	switch (N)
	{
	case 0:  Set(0, nullptr); Set(1, nullptr); Set(2, nullptr); break;
	case 1:  Set(0, &NoticeLines[0]); Set(1, nullptr); Set(2, nullptr); break;
	case 2:  Set(0, nullptr); Set(1, &NoticeLines[0]); Set(2, &NoticeLines[1]); break;
	default: Set(0, nullptr); Set(1, &NoticeLines[N - 2]); Set(2, &NoticeLines[N - 1]); break;   // две последние (эталон a3 == 2)
	}
	AsdaUi::SetShownDecor(NoticeWidget, N > 0);
}

// ═════════════════════════════════ HERO_NOTICE_WIN ═════════════════════════════════════════════
void UAsdaHudSubsystem::ShowHeroNotice(const FString& Text, FLinearColor Color, float LifeSec)
{
	FAsdaHeroNotice N; N.Text = Text; N.Color = Color; N.LifeSec = LifeSec;
	if (HeroShownAt >= 0.0 || !HeroWidget || !HeroText)
	{
		HeroQueue.Add(MoveTemp(N));   // окно занято/не создано → очередь (эталон queuePushBackAlloc)
		if (HeroQueue.Num() > 32) { HeroQueue.RemoveAt(0); }
		return;
	}
	PresentHeroNotice(N);
}

void UAsdaHudSubsystem::PresentHeroNotice(const FAsdaHeroNotice& N)
{
	if (!HeroWidget || !HeroText) { HeroQueue.Insert(N, 0); return; }
	// Цвет = базовый (CControl_SetField923); inline-теги текста поддержаны тем же путём, что 3113.
	// 83401 HeroText: эталон `CClass_SetField926(pChat->heroText, dword_800000)` (chat.c:645) — ОБВОДКА.
	if (UWidget* Nw = ApplyNoticeText(HeroText, N.Text, N.Color.ToFColor(/*sRGB*/true), TEXT("<FO>"))) { HeroText = Nw; }
	HeroWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	HeroLife = N.LifeSec;
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	HeroShownAt = W ? W->GetTimeSeconds() : 0.0;
	UE_LOG(LogAsdaHud, Log, TEXT("[HERO_NOTICE] «%s» (%.0f с)"), *N.Text, N.LifeSec);
}

// Клик/таймаут (эталон ui_core.c:12408-12425): скрыть; очередь не пуста → показать следующий
//   uiShowTimedNoticeText(next, зелёный, 10 000).
void UAsdaHudSubsystem::DismissHeroNotice()
{
	HeroShownAt = -1.0;
	if (HeroWidget) { HeroWidget->SetVisibility(ESlateVisibility::Collapsed); }
	if (HeroQueue.Num() > 0)
	{
		FAsdaHeroNotice N = HeroQueue[0];
		HeroQueue.RemoveAt(0);
		PresentHeroNotice(N);
	}
}

void UAsdaHudSubsystem::OnHeroClicked()
{
	if (HeroShownAt >= 0.0) { DismissHeroNotice(); }
}

// 6131 Recv_ItemFound: «[ss1] получает предмет: [ss2]» (loc 20250) — только ЧУЖОЙ игрок
//   (свой редкий подбор нотисится локальным путём 5013), только грейд ≥3 и «нотисные» типы
//   (эталон network.c:3720-3752: itemType>9 && !=11 → выход).
//   ★ Src (байт +24 тела) шлёт наш сервер, AsdaPatch.dll / P11:
//       0 legacy или клиентский 6616 — поведение как в эталоне (фильтр грейда, зелёный);
//       1 подбор, 2 гача, 3 премиум-гача — сервер УЖЕ отфильтровал по грейду;
//       4 заточка-успех  — loc 900001, ЗЕЛЁНЫЙ (эталонный −16711936), показываем ПРОЦЕНТ (item+27);
//       5 заточка-неудача — loc 900002, КРАСНЫЙ (эталона у неудачи нет — наше решение).
//   Заточку по грейду не фильтруем: анонсируется любая экипировка (решение юзера).
//   Раскраска строки (обе точки показа — HERO_NOTICE и дубль в ленте — берут ОДИН И ТОТ ЖЕ текст
//   и один и тот же базовый цвет, поэтому выглядят одинаково): обрамление «ник улучшил предмет»
//   и скобки — базовым цветом, ИМЯ предмета и ПРОЦЕНТ — цветом грейда (inline <FC=FFRRGGBB>).
void UAsdaHudSubsystem::OnItemAcquired(const FString& CharName, int32 ItemId,
                                       int32 Src, int32 RefineLevel, int32 RefinePercent)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	const bool bRefine = (Src == 4 || Src == 5);
	// ★★★ ГЕЙТ «СВОЙ НИК» — ТОЛЬКО ДЛЯ ДОБЫЧИ (теги 0..3), НЕ для заточки (4/5).
	//   Эталон (network.c:3728) отбрасывает свой ник потому, что СВОЙ редкий подбор клиент уже
	//   показал сам — локальным путём CItemManager_NotifyItemInfo (item_inventory.c:39563), и без
	//   гейта строка задвоилась бы. У заточки такого локального пути НЕТ ни в эталоне (там анонса
	//   заточки нет вовсе), ни у нас: единственный источник — серверный 6131 (патч P11). Гейт на
	//   тегах 4/5 означал бы, что САМ точильщик своей строки не видит, а видят все вокруг.
	if (!bRefine)
	{
		if (const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			if (!WS->GetSelfCharName().IsEmpty() && CharName == WS->GetSelfCharName())
			{
				// ★ Log, а не Verbose: при живой проверке цепи это ЕДИНСТВЕННОЕ доказательство,
				//   что сервер ретранслировал 6131 обратно отправителю (значит, рассылка дошла).
				UE_LOG(LogAsdaHud, Log,
					TEXT("[6131] свой ник '%s' (тег=%d, код=%d) — гашу (эталон network.c:3728: свой подбор "
					     "показывает NotifyItemInfo). Сам факт строки = рассылка сервера ДОШЛА."),
					*CharName, Src, ItemId);
				return;                                                              // strcmp self
			}
		}
	}
	UAsdaItemTableSubsystem* Items = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	// ★ Приёмная сторона эталона (`Recv_ItemFound@0x9744B0`, network.c:3704-3733) выбирает таблицу
	//   тем же `CItemManager_SelectTableByCode@0x9C3AA0`: самоцвет берёт грейд из
	//   `dropColorIndex[0]` и фильтра по типу не применяет. В порту ветки самоцветов не было —
	//   анонс о чужом редком самоцвете (в т.ч. приз гачи) молча пропадал.
	const FAsdaItemRecord* R = Items ? Items->GetItem(ItemId) : nullptr;
	FAsdaSowellRow SowR;
	const bool bSowelItem = !R && Items && Items->IsSowelCode(ItemId) && Items->GetSowell(ItemId, SowR);
	if (!R && !bSowelItem)
	{
		UE_LOG(LogAsdaHud, Warning, TEXT("[6131] предмет %d не найден ни в item-БД, ни в таблице самоцветов"), ItemId);
		return;
	}
	const int32 RecGrade = R ? R->ItemGrade : SowR.GradeColorIndex;

	if (!bRefine)
	{
		// эталонный фильтр приёмной стороны (network.c:3720-3752).
		// ★ Эталон отбрасывает МОЛЧА. Мы пишем строку: это последнее звено цепи анонса
		//   (6616 → сервер → 6131 → сюда), и без неё «пакет пришёл, но ничего не видно»
		//   неотличимо от «пакет не пришёл». Поведение не меняется — только диагностика.
		if (RecGrade < 3)
		{
			UE_LOG(LogAsdaHud, Log,
				TEXT("[6131] ОТСЕЯНО приёмной стороной: код=%d (%s) грейд=%d < 3, тег=%d, от '%s' "
				     "(эталонный фильтр Recv_ItemFound@0x9744B0)"),
				ItemId, bSowelItem ? TEXT("самоцвет") : TEXT("предмет"), RecGrade, Src, *CharName);
			return;
		}
		if (R && R->ItemType > 9 && R->ItemType != 11)
		{
			UE_LOG(LogAsdaHud, Log,
				TEXT("[6131] ОТСЕЯНО приёмной стороной: код=%d тип=%d (>9 и !=11), грейд=%d, тег=%d, от '%s' "
				     "(эталонный фильтр Recv_ItemFound@0x9744B0)"),
				ItemId, R->ItemType, RecGrade, Src, *CharName);
			return;
		}
	}

	const FString ItemName = Items->GetItemName(ItemId);
	UAsdaTextSubsystem* Txt = GI->GetSubsystem<UAsdaTextSubsystem>();
	FString Msg;
	bool   bColorOver = false;                  // заточка задаёт цвет сама (зелёный/красный), минуя таблицу
	FColor BaseColor  = FColor(0, 255, 0, 255); // эталонный зелёный подбора (для лога/фолбэка)
	const int32 Grade = RecGrade;               // эталонный индекс drop-палитры (item_inventory.c:45318)

	if (Src == 4 || Src == 5)   // ── заточка: единое оформление «ИГРОК … [ ИМЯ ] +N%» ──
	{
		const bool bFail = (Src == 5);
		bColorOver = true;
		BaseColor  = bFail
			? ParseAnnounceColor(CVarHeroRefineFailColor.GetValueOnAnyThread(),    FColor(0xFF, 0x00, 0x00, 0xFF))
			: ParseAnnounceColor(CVarHeroRefineSuccessColor.GetValueOnAnyThread(), FColor(0x00, 0xFF, 0x00, 0xFF));

		// Процент: у успеха всегда; у неудачи — по cvar (процент ДО сорвавшейся попытки: сервер шлёт
		//   его в 6131 +26, а сам предмет при неудаче частью свитков сбрасывается в +0 — GameServer
		//   sub_4FEE50, 0x500009/0x500010).
		const bool    bShowPct = !bFail || (CVarHeroRefineFailShowPercent.GetValueOnAnyThread() != 0);
		const FString Deco     = BuildRefineItemDeco(ItemName, Grade, RefinePercent, bShowPct);
		const int32   LocId    = bFail ? 900002 : 900001;

		// В шаблон уходит ОДИН строковый слот [ss2] — уже собранный блок с разметкой: локализатор
		//   двигает «[ имя ] +N%» целиком, висячих пробелов при отключённом проценте не остаётся.
		if (Txt) { Msg = Txt->FormatById(LocId, { CharName, Deco }, {}); }
		if (Msg.IsEmpty())   // кодовый фолбэк (строк 900001/900002 может не быть в DT_GameText)
		{
			Msg = bFail
				? FString::Printf(TEXT("[#900002] %s %s"), *CharName, *Deco)   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
				: FString::Printf(TEXT("[#900001] %s %s"), *CharName, *Deco);
		}
	}
	else                    // подбор / гача / премиум-гача / legacy — эталонная строка
	{
		if (Txt) { Msg = Txt->FormatById(20250, { CharName, ItemName }, {}); }
		if (Msg.IsEmpty()) { Msg = FString::Printf(TEXT("[#20250] %s %s"), *CharName, *ItemName); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
	}

	const FColor GC = FAsdaGradeColor::Raw(Grade);
	UE_LOG(LogAsdaHud, Log,
		TEXT("[анонс] код=%d грейд=%d цвет=#%02X%02X%02X pct=%d ступень=%d тег=%d → строка='%s'"),
		ItemId, Grade, GC.R, GC.G, GC.B, RefinePercent, RefineLevel, Src, *Msg);

	// ★ Круг 7: через маршрутизатор (категория HeroLoot). Эталонный зелёный -16711936 задан таблицей;
	//   у заточки базовый цвет перекрывается (bUseColor): жёлтый на успех, красный на неудачу.
	if (UAsdaMessageSubsystem* Router = GI->GetSubsystem<UAsdaMessageSubsystem>())
	{
		FAsdaMsgParams P;
		P.Category = EAsdaMsgCategory::HeroLoot;
		P.Text = Msg;
		P.SrcOpcode = 6131;
		P.bUseColor = bColorOver;
		P.Color = BaseColor;
		Router->Post(P);
		return;
	}
	ShowHeroNotice(Msg, FLinearColor::FromSRGBColor(BaseColor), 10.f);   // фолбэк без маршрутизатора
}

// ★ CItemManager_NotifyItemInfo@0xD0C8F0 (item_inventory.c:39497) — единая точка «я получил предмет».
//   Эталонная логика построчно:
//     grade   = itemRecord->itemGrade                      (для sowel-таблицы = dropColorIndex[0])
//     если itemType > 9 → анонс только при (itemType == 11 && itemSubType == 200)
//     если grade >= 3 и анонсируемо:
//         HERO_NOTICE loc 20250 «[своё имя] получает предмет: [имя]» (-16711936, 10 с)
//         + строка в чат-канал 10
//         + netSendTsMaskedReq6129(...)         ← «показать предмет» (нужны plate/pos слота — см. ниже)
//         + netSendSignedPacket6616(имя, code)  ← анонс всем через сервер
void UAsdaHudSubsystem::NotifyItemInfo(int32 ItemId, bool bForce, int32 ContainerType, int32 SlotIndex)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	UAsdaItemTableSubsystem* Items = GI->GetSubsystem<UAsdaItemTableSubsystem>();

	// ★★★ ВЫБОР ТАБЛИЦЫ — порт `CItemManager_SelectTableByCode@0x9C3AA0` (item_inventory.c:1565):
	//       код в item_list                 → 0 (ветка предметов: грейд + фильтр по типу);
	//       код в sowell_list               → 1 (ветка САМОЦВЕТОВ: грейд = `GradeColorIndex`,
	//                                            фильтра по типу НЕТ — эталон `isNotifiable` не трогает);
	//       код в special_itemlist (Table2) → 2 (эталон НИЧЕГО не читает: грейд остаётся −1 ⇒ выход);
	//       иначе                           → −1 (то же самое, выход).
	//   ★ Ветки самоцветов в порту НЕ БЫЛО — а призом гачи самоцвет выпадает штатно (живой лог
	//     сервера 13.08: `Recv_GachaItemUse() … Code=9640` = «Предмет из волчьего клыка»,
	//     sowell_list). Из 3 925 самоцветов 859 имеют `GradeColorIndex >= 3`, т.е. анонсируемы.
	const FAsdaItemRecord* R = Items ? Items->GetItem(ItemId) : nullptr;
	FAsdaSowellRow Sow;
	const bool bSowel = !R && Items && Items->IsSowelCode(ItemId) && Items->GetSowell(ItemId, Sow);
	if (!R && !bSowel)
	{
		// ★ ЯВНЫЙ ответ: раньше молчали и юзер не понимал, почему «не работает».
		// ★ 24.10.2026 [ПОРТ]: код ВТОРОЙ таблицы (special_itemlist — функциональные предметы, маунты) — НЕ ошибка: эталон
		//   (`SelectTableByCode` == 2) ничего не читает, грейд −1 ⇒ анонса нет, как и у нас. Прежний Warning «НЕТ НИ В
		//   item-БД…» был ложной тревогой (решение координатора: поведение как у оригинала, правится только журнал).
		if (Items && Items->IsSpecialItemCode(ItemId))
		{
			UE_LOG(LogAsdaHud, Verbose, TEXT("[notify] itemId=%d: вторая таблица (special_itemlist) — оригинал не анонсирует, как и мы"), ItemId);
		}
		else
		{
			UE_LOG(LogAsdaHud, Warning, TEXT("[notify] itemId=%d: НЕТ НИ В item-БД, НИ В таблице самоцветов, НИ ВО ВТОРОЙ таблице → отброшено%s"),
				ItemId, Items ? TEXT("") : TEXT(" (подсистема предметов не загружена)"));
		}
		if (!bForce) { return; }
	}

	const int32 Grade   = R ? R->ItemGrade   : (bSowel ? Sow.GradeColorIndex : -1);
	const int32 Type    = R ? R->ItemType    : -1;
	const int32 SubType = R ? R->ItemSubType : -1;

	// Фильтр эталона: itemType>9 → анонс только при (itemType==11 && itemSubType==200); затем грейд>=3.
	//   У самоцветов проверки типа нет вовсе — только грейд (эталон item_inventory.c:39524-39536).
	const bool bNotifiable = bSowel || (R && ((Type <= 9) || (Type == 11 && SubType == 200)));
	const bool bGradeOk    = (R || bSowel) && (Grade >= 3);
	if (!bForce && (!bNotifiable || !bGradeOk))
	{
		UE_LOG(LogAsdaHud, Warning,
			TEXT("[notify] itemId=%d (%s): грейд=%d тип=%d подтип=%d → ОТБРОШЕНО (%s). Нужен грейд>=3 И (тип<=9 или 11/200). Форс: asda.Item.Notify %d 1"),
			ItemId, bSowel ? TEXT("самоцвет") : TEXT("предмет"), Grade, Type, SubType,
			!bNotifiable ? TEXT("тип не анонсируемый") : TEXT("грейд < 3"), ItemId);
		return;
	}

	const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	const FString SelfName = WS ? WS->GetSelfCharName() : FString();
	if (SelfName.IsEmpty())
	{
		UE_LOG(LogAsdaHud, Warning, TEXT("[notify] itemId=%d: своё имя персонажа ещё не известно (не вошли в мир?) → отброшено"), ItemId);
		return;
	}

	const FString ItemName = Items ? Items->GetItemName(ItemId) : FString::Printf(TEXT("item %d"), ItemId);
	FString Msg;
	if (UAsdaTextSubsystem* Txt = GI->GetSubsystem<UAsdaTextSubsystem>())
	{
		Msg = Txt->FormatById(20250, { SelfName, ItemName }, {});
	}
	if (Msg.IsEmpty()) { Msg = FString::Printf(TEXT("[#20250] %s %s"), *SelfName, *ItemName); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка

	// Локальный показ — как в оригинале, СВОЙ анонс виден сразу, не дожидаясь ретрансляции сервера.
	if (UAsdaMessageSubsystem* Router = GI->GetSubsystem<UAsdaMessageSubsystem>())
	{
		FAsdaMsgParams P;
		P.Category = EAsdaMsgCategory::HeroLoot;
		P.Text = Msg;
		P.SrcOpcode = 6616;
		Router->Post(P);
	}
	else
	{
		ShowHeroNotice(Msg, FLinearColor::FromSRGBColor(FColor(0, 255, 0, 255)), 10.f);
	}

	// Анонс остальным: C→S 6616 → сервер ретранслирует 6131 всем (нужен серверный патч 6616).
	//   ★ Отключаемо через `asda.Hero.Announce.ClientSend6616 0`, когда те же события объявляет
	//     сам сервер (AsdaPatch P11) — иначе окружающие получат две одинаковые строки.
	const bool bAllowSend = (CVarHeroClientSend6616.GetValueOnAnyThread() != 0);
	bool bSent = false;
	if (bAllowSend)
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			bSent = Net->SendRareItemNotice6616(SelfName, ItemId);
		}
	}
	UE_LOG(LogAsdaHud, Log,
		TEXT("[notify] itemId=%d: грейд=%d тип=%d подтип=%d → %s 6616 (имя '%s', предмет «%s»)%s"),
		ItemId, Grade, Type, SubType,
		!bAllowSend ? TEXT("ПРОПУЩЕНО (asda.Hero.Announce.ClientSend6616=0)")
		            : (bSent ? TEXT("ОТПРАВЛЕНО") : TEXT("НЕ УДАЛОСЬ ОТПРАВИТЬ")),
		*SelfName, *ItemName, bForce ? TEXT(" [ФОРС — фильтр обойдён]") : TEXT(""));

	// ★★★ ТРЕТИЙ пакет ветки — «похвастаться» добычей всем вокруг: `netSendTsMaskedReq6129(self,
	//   LocalPlayer->userIdx, −1, слот, контейнер)` (item_inventory.c:39572-39575). Адресат −1 =
	//   весь сектор видимости, включая самого отправителя (сервер `Recv_ItemInfoDisplay@0x55BF80`
	//   рассылает через `SendPacket_ClippingSectorInAllUser`). Сервер проверяет, что предмет
	//   ДЕЙСТВИТЕЛЬНО лежит в указанном контейнере и слоте, — поэтому без них слать нельзя.
	if (ContainerType < 0 || SlotIndex < 0)
	{
		UE_LOG(LogAsdaHud, Verbose,
			TEXT("[notify] itemId=%d: контейнер/слот не переданы (%d/%d) — 6129 «похвастаться» не шлём"),
			ItemId, ContainerType, SlotIndex);
		return;
	}
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	// ★★★ ПОЛЕ @0 пакета 6129 — charId, а НЕ сессионный userNum: сервер сверяет его с
	//   `sub_5324F0(this, arg0)` = `*(DWORD*)(CUser+372)` = iUserIndex и при несовпадении выходит
	//   МОЛЧА (см. шапку `SendItemInfoDisplay6129`). Наш charId = `SelectedCharId` из 1017.
	if (Net->SelectedCharId == 0)
	{
		UE_LOG(LogAsdaHud, Warning,
			TEXT("[notify] itemId=%d: charId ещё не известен — 6129 «похвастаться» не шлём"), ItemId);
		return;
	}
	const bool bShown = Net->SendItemInfoDisplay6129(
		Net->SelectedCharId, /*TargetUserIdx*/ -1, (uint8)ContainerType, SlotIndex);
	UE_LOG(LogAsdaHud, Log,
		TEXT("[notify] itemId=%d: 6129 «похвастаться» (charId=%d, контейнер %d, слот %d, всем вокруг) — %s"),
		ItemId, Net->SelectedCharId, ContainerType, SlotIndex, bShown ? TEXT("отправлено") : TEXT("НЕ УДАЛОСЬ"));
}

// ═══════════════════════════ Консольные команды проверки (PIE) ═════════════════════════════════

// Ручная проверка цепочки анонса: локальный HERO_NOTICE + C→S 6616 (сервер должен вернуть 6131 всем).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaItemNotify(
	TEXT("asda.Item.Notify"),
	TEXT("asda.Item.Notify <itemId> [force] — прогнать CItemManager_NotifyItemInfo (грейд>=3 → HERO_NOTICE + 6616). "
	     "force=1 — послать 6616 минуя фильтр (отладка серверного тракта). При живом соединении — только с аргументом `online`"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaHudSubsystem* Hud = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaHudSubsystem>() : nullptr;
		if (!Hud) { UE_LOG(LogAsdaHud, Warning, TEXT("[notify] HUD-подсистема недоступна")); return; }
		if (Args.Num() < 1) { UE_LOG(LogAsdaHud, Warning, TEXT("[notify] использование: asda.Item.Notify <itemId> [1=форс]")); return; }
		// ★ С-Р1 (26.09.2026): при живом соединении команда шлёт НАСТОЯЩИЙ 6616 (сервер рассылает 6131 ВСЕМ игрокам
		//   карты) с любым itemId — поддельный анонс. Поэтому по умолчанию только офлайн; для намеренной отладки
		//   серверного тракта (назначение команды) — явный аргумент `online`: asda.Item.Notify <itemId> [1] online.
		const bool bOnlineOptIn = FAsdaOutgoingGate::HasOnlineOptIn(Args);   // ред. 1: общая точка разбора `online`
		static const uint16 kGuardOps[] = { 6616, 6129 };
		if (!bOnlineOptIn && FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Item.Notify (для живого сервера — аргумент online)"), 5.f, MakeArrayView(kGuardOps))) { return; }
		const int32 Id = FCString::Atoi(*Args[0]);
		const bool bForce = (Args.Num() >= 2) && (FCString::Atoi(*Args[1]) != 0);
		Hud->NotifyItemInfo(Id, bForce);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Проверка ОФОРМЛЕНИЯ анонса без похода в игру: прогоняет приёмную ветку 6131 с любым
//   кодом предмета / процентом / тегом источника. Тег 4 = успех (жёлтый), 5 = неудача (красный),
//   1/2/3 = подбор/гача/премиум-гача (эталонная зелёная строка, фильтр грейда >= 3 действует).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaHeroTestAnnounce(
	TEXT("asda.Hero.TestAnnounce"),
	TEXT("asda.Hero.TestAnnounce <itemId> [процент=45] [тег=4] [имя=ТестИгрок] — показать анонс 6131 "
	     "как будто он пришёл с сервера (4 успех заточки, 5 неудача, 1/2/3 подбор/гача/премиум-гача)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaHudSubsystem* Hud = GI ? GI->GetSubsystem<UAsdaHudSubsystem>() : nullptr;
		if (!Hud) { UE_LOG(LogAsdaHud, Warning, TEXT("[анонс] HUD-подсистема недоступна")); return; }
		if (Args.Num() < 1)
		{
			UE_LOG(LogAsdaHud, Warning,
				TEXT("[анонс] использование: asda.Hero.TestAnnounce <itemId> [процент] [тег] [имя]"));
			return;
		}
		const int32   Id   = FCString::Atoi(*Args[0]);
		const int32   Pct  = (Args.Num() >= 2) ? FCString::Atoi(*Args[1]) : 45;
		const int32   Tag  = (Args.Num() >= 3) ? FCString::Atoi(*Args[2]) : 4;
		const FString Name = (Args.Num() >= 4) ? Args[3] : FString(TEXT("ТестИгрок"));
		// Ступень (item+25) на оформление не влияет — шлём 1 для полноты пакета.
		Hud->OnItemAcquired(Name, Id, Tag, /*RefineLevel=*/1, Pct);
	}));
#endif // ASDA_DEV_CONSOLE

// Список предметов грейда >= 3, проходящих фильтр анонса — чтобы не гадать с id при проверке.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaItemRareList(
	TEXT("asda.Item.RareList"),
	TEXT("asda.Item.RareList [сколько] — вывести коды предметов грейда>=3, годных для анонса"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!Items) { UE_LOG(LogAsdaHud, Warning, TEXT("[rarelist] item-подсистема недоступна")); return; }
		const int32 Want = (Args.Num() >= 1) ? FMath::Max(1, FCString::Atoi(*Args[0])) : 20;
		int32 Shown = 0, Total = 0;
		for (int32 Code = 20551; Code <= 90000 && Shown < Want; ++Code)
		{
			const FAsdaItemRecord* R = Items->GetItem(Code);
			if (!R) { continue; }
			const bool bNotifiable = (R->ItemType <= 9) || (R->ItemType == 11 && R->ItemSubType == 200);
			if (R->ItemGrade < 3 || !bNotifiable) { continue; }
			++Total;
			UE_LOG(LogAsdaHud, Log, TEXT("[rarelist] %d  грейд=%d тип=%d/%d  «%s»"),
				Code, R->ItemGrade, R->ItemType, R->ItemSubType, *Items->GetItemName(Code));
			++Shown;
		}
		UE_LOG(LogAsdaHud, Log, TEXT("[rarelist] показано %d (asda.Item.Notify <код> — проверить)"), Shown);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaHudTestServerNotice(
	TEXT("asda.Hud.TestServerNotice"),
	TEXT("Симуляция 3113: asda.Hud.TestServerNotice <mode> <текст> — mode 0=строка, 1=убрать, 100=loc10221"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaHudSubsystem* Hud = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaHudSubsystem>() : nullptr;
		if (!Hud) { return; }
		const int32 Mode = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		FString Text;
		for (int32 i = 1; i < Args.Num(); ++i) { if (i > 1) Text += TEXT(" "); Text += Args[i]; }
		if (Text.IsEmpty()) { Text = TEXT("Тестовое серверное уведомление"); }
		Hud->ShowServerNotice(Mode, Text, FColor(255, 255, 0));
		// ★ 10.10.2026 (МЕЛК-1): офлайн окна HUD не созданы — поднимаем и печатаем, какие строки видны
		if (!FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Hud.TestServerNotice (состояние строк)")))
		{
			UE_LOG(LogAsdaHud, Log, TEXT("[NOTICE тест] %s"), *Hud->DebugNoticeState());
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ★ 10.10.2026 (МЕЛК-1): см. объявление. Видимость — своя у каждой строки (родитель — окно объявления).
FString UAsdaHudSubsystem::DebugNoticeState()
{
	if (!NoticeWidget) { EnsureWidgets(); RefreshNoticeLines(); }
	FString S = FString::Printf(TEXT("строк в буфере %d; видны:"), NoticeLines.Num());
	static const TCHAR* kNames[3] = { TEXT("TXT00"), TEXT("TXT01"), TEXT("TXT02") };
	for (int32 i = 0; i < 3; ++i)
	{
		const UWidget* T = NoticeTxt[i];
		if (!T || !T->IsVisible()) { continue; }
		// ★ 10.10.2026 (ревизия overlaps_fixes МЕЛК-5): строки объявления — `URichTextBlock`; печатаем и их текст, чтобы журнал
		//   показывал, какое объявление легло в TXT01, а какое в TXT02 (порядок «старшая сверху»).
		FString Tx;   // текст строки (обычной или rich)
		if (const UTextBlock* Tb = Cast<UTextBlock>(T)) { Tx = Tb->GetText().ToString(); }
		else if (const URichTextBlock* Rb = Cast<URichTextBlock>(T)) { Tx = Rb->GetText().ToString(); }
		else { Tx = TEXT("(?)"); }
		S += FString::Printf(TEXT(" %s «%s»"), kNames[i], *Tx);
	}
	return S;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaHudTestHeroNotice(
	TEXT("asda.Hud.TestHeroNotice"),
	TEXT("Симуляция hero-нотиса: asda.Hud.TestHeroNotice <текст>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaHudSubsystem* Hud = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaHudSubsystem>() : nullptr;
		if (!Hud) { return; }
		FString Text = FString::Join(Args, TEXT(" "));
		if (Text.IsEmpty()) { Text = TEXT("Тестовый hero-нотис (клик закрывает)"); }
		Hud->ShowHeroNotice(Text, FLinearColor::FromSRGBColor(FColor(0, 255, 0, 255)));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaHudTestExp(
	TEXT("asda.Hud.TestExp"),
	TEXT("Пересчитать EXP-бар: без аргументов — из живых данных; asda.Hud.TestExp <level> <exp> — подстановка"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: подменяет SelfLevel/SelfExp живой сессии.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Hud.TestExp"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaHudSubsystem* Hud = GI ? GI->GetSubsystem<UAsdaHudSubsystem>() : nullptr;
		if (!Hud) { return; }
		if (Args.Num() >= 2)
		{
			if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
			{
				Net->SelfLevel = (uint8)FCString::Atoi(*Args[0]);
				Net->SelfExp   = FCString::Atoi64(*Args[1]);
				UE_LOG(LogAsdaHud, Display, TEXT("[TestExp] подставлено level=%d exp=%lld"),
					(int32)Net->SelfLevel, (long long)Net->SelfExp);
			}
		}
		Hud->RefreshExpBar();
		// ★ Фидбек ВСЕГДА (Display = видно в консоли): что применили и почему такой процент.
		UE_LOG(LogAsdaHud, Display, TEXT("[TestExp] %s"), *Hud->DebugExpState());
		Hud->DumpExpWidgets();   // рантайм-доказательство: видимость/клип заливки и контролы группы процента
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Проверка ПОКАЗА прибавки опыта без сервера (все три ветки 4038).
//   asda.Hud.TestExpGain <прибавка> [flag 0/1] [renderId]
//     flag=1            → ветка A (наградной опыт: над собой + оранжевая строка);
//     flag=0, rid<0     → ветка B (над собой);
//     flag=0, rid>=0    → ветка C (над сущностью с этим renderId).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaHudTestExpGain(
	TEXT("asda.Hud.TestExpGain"),
	TEXT("Показ прибавки опыта: asda.Hud.TestExpGain <прибавка> [flag 0/1] [renderId]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaHudSubsystem* Hud = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaHudSubsystem>() : nullptr;
		if (!Hud) { return; }
		const int64 Gain  = Args.Num() > 0 ? FCString::Atoi64(*Args[0]) : 217;
		const bool  bFull = Args.Num() > 1 ? (FCString::Atoi(*Args[1]) == 1) : false;
		const int32 Rid   = Args.Num() > 2 ? FCString::Atoi(*Args[2]) : -1;
		UE_LOG(LogAsdaHud, Display, TEXT("[TestExpGain] +%lld flag=%d rid=%d"), (long long)Gain, (int32)bFull, Rid);
		Hud->ShowExpGain(Gain, bFull, Rid);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ТЕСТОВЫЙ ПРОГОН ПОВЫШЕНИЯ УРОВНЯ — `asda.Hud.TestLevelUp`.
//   Гоняет ровно тот же путь, что и настоящий пакет 4036: звук, эффект у персонажа и баннер
//   (`Con_Banner2`, надпись уровня). Ничего не шлёт на сервер и не меняет ни уровень, ни опыт.
//   Нужен потому, что ждать настоящего уровня ради проверки показа дорого, а довести показ до
//   вида оригинала надо за несколько прогонов подряд.
//   ⚠ Имя в семье `asda.Hud.Test*` — рядом с остальными тестами HUD. Первая попытка называлась
//   `asda.Test.LevelUp` и стояла в середине файла; её снёс мой же переписанный блок баннера,
//   и команда молча перестала существовать («Command not recognized»). Здесь, в конце файла
//   рядом с соседями, она под такую правку не попадёт.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GAsdaHudTestLevelUp(
	TEXT("asda.Hud.TestLevelUp"),
	TEXT("Показ повышения уровня: звук + эффект + баннер. Сервер и данные не трогает."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		UAsdaHudSubsystem* Hud = W && W->GetGameInstance()
			? W->GetGameInstance()->GetSubsystem<UAsdaHudSubsystem>() : nullptr;
		if (!Hud)
		{
			UE_LOG(LogAsdaHud, Warning, TEXT("[уровень] HUD недоступен — команда работает только в мире"));
			return;
		}
		UE_LOG(LogAsdaHud, Display, TEXT("[уровень] ТЕСТОВЫЙ ПРОГОН (asda.Hud.TestLevelUp)"));
		Hud->OnSelfLevelUp();
	}));
#endif // ASDA_DEV_CONSOLE
