// Copyright (c) 2026 @sitxovski (Discord)
// Реализация попапа обрыва связи `COMMONWIN_MSGBOX` (8000). Полный разбор эталона и таблица
//   «причина → нотис» — в шапке `Public/UI/AsdaDisconnectPopupSubsystem.h`, здесь не повторяется.
#include "UI/AsdaDisconnectPopupSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaNetSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"   // М2: `g_bLoadingInProgress` — гейт причины 255
#include "UI/AsdaNotice.h"              // AsdaNotice::Loc — строка нотиса с срезанными тегами
#include "UI/AsdaLocIds.h"              // AsdaLoc::kBtnOk — подпись «ОК» (нотис 2288)
#include "Login/AsdaButtonRelay.h"      // мостик UButton::OnClicked (у делегата нет отправителя)
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"   // GetPawn() — признак «мы в игре» вместо `g_GameState`
#include "Kismet/GameplayStatics.h"           // ★ 19.10.2026: OpenLevel — из мира без связи ОК ведёт на экран входа
#include "World/AsdaWorldKind.h"              // ★ 19.10.2026: IsLoginScene — «мы не на сцене входа»
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Components/CanvasPanelSlot.h"       // ★ 16.10 (EAC): ширина слота COMMONWIN_TEXT для переноса текста по словам
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDisc, Log, All);

const FName UAsdaDisconnectPopupSubsystem::kWindowId(TEXT("COMMONWIN_MSGBOX"));

// ★★★ КЛЮЧ ОТКАТА К БУКВАЛЬНОМУ ОРИГИНАЛУ. 1 = кнопка 8003 завершает процесс, как
//   `CommonWin_CommonEventFunc@0xC6EAE0` (`PostQuitMessage(0)`). По умолчанию 0 — закон
//   `original-quits-client-on-disconnect`: сброса состояния у оригинала нет, потому что он
//   умирает целиком; у нас сброс делает `UAsdaSessionResetSubsystem`, и клиент обязан жить,
//   чтобы вход можно было повторить.
static ASDA_DEV_CVAR(int32, CVarDisconnectPopupQuit,
	TEXT("asda.Net.DisconnectPopupQuit"), 0,
	TEXT("1 = кнопка попапа обрыва ЗАВЕРШАЕТ клиент (1:1 CommonWin_CommonEventFunc@0xC6EAE0); "
	     "0 (умолчание) = только закрыть попап, сессия уже сброшена и вход можно повторить."),
	ECVF_Default);

// z попапа. Окно модальное и обязано лежать ВЫШЕ игровых модалок (`ONEBUT_MSGBOX` кладётся на 0,
//   модалка смерти — на 800): после обрыва связи никакой диалог поверх него смысла не имеет.
static constexpr int32 kDisconnectPopupZ = 900;

// ── Ассеты обеих копий одного и того же .JUI-окна (см. п.2/4 шапки) ────────────────────────────
//   `gamemain`  — окно уже в наборе игровых окон (состояния 1/8/11/12/16/500);
//   `commonwin` — то, что эталон догружает как `./JUIData/CommonWin.JUI`.
static const TCHAR* kPathGameMain  = TEXT("/Game/AsdaAssets/UI/Player_UI/Common/WBP_COMMONWIN_MSGBOX");
static const TCHAR* kPathCommonWin = TEXT("/Game/AsdaAssets/UI/CommonWin/WBP_COMMONWIN_MSGBOX_CommonWin");

void UAsdaDisconnectPopupSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РЕВИЗИЯ 2026-09-17 (С5): ЗАВИСИМОСТЬ ОБЪЯВЛЯЕМ ЯВНО.
	//   Ниже мы берём `UAsdaNetSubsystem` ПРЯМО В `Initialize` и подписываемся на его делегат.
	//   Без `InitializeDependency` порядок подъёма подсистем коллекции не определён: если сетевая
	//   ещё не создана, `GetSubsystem` вернёт её же (коллекция поднимет по требованию), но при
	//   перестройке коллекции подписка может уйти в ДРУГОЙ экземпляр и молча пропасть. В проекте
	//   это уже оформлено как правило — см. `AsdaAfterDeathSubsystem.cpp:70`,
	//   `AsdaCharaInfoSubsystem.cpp:174`, `AsdaSkillFxSubsystem.cpp:158`, `AsdaSessionReset.cpp:34`.
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());

	// ★ ПОДПИСКА ТОЛЬКО НА ИГРОВОЙ ОБРЫВ. Кик на логине (`OnLoginKick`) сюда НЕ заводим намеренно:
	//   у эталона состояния 2/3/5/6/10 идут ДРУГОЙ веткой `Show_DisconnectPopup` — через
	//   `uiControl_setTextAndFlag`, то есть контролы ЛОГИН-окна, а не 8000. В порту эта ветка уже
	//   есть и работает: `AAsdaLoginGameMode::OnLoginKick` зовёт `SetLoginStatus` + `ShowMessageWin`.
	//   Вторая подписка дала бы ДВА окна на один кик.
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		HGameDisconnected = Net->OnGameDisconnected.AddUObject(this, &UAsdaDisconnectPopupSubsystem::OnGameDisconnected);
	}

	// ★ ДИАГНОСТИКА РЕАЛЬНЫМ ПУТЁМ (закон `diagnostic-must-use-real-path`): попап показывается той
	//   же функцией, что и при живом обрыве, — проверка оформления не требует падения сервера.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.Net.DisconnectPopup"),
		TEXT("Показать попап обрыва связи COMMONWIN_MSGBOX 8000. Аргумент — код нотиса текста "
		     "(например 1108 «Соединение с сервером разорвано»); без аргумента берётся 1108."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const int32 Id = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 1108;
			ShowDisconnectPopup(Id);
		}),
		ECVF_Default);
#endif // ASDA_DEV_CONSOLE
}

void UAsdaDisconnectPopupSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (HGameDisconnected.IsValid()) { Net->OnGameDisconnected.Remove(HGameDisconnected); }
	}
	HGameDisconnected.Reset();
	OkRelay = nullptr;
	Super::Deinitialize();
}

FString UAsdaDisconnectPopupSubsystem::Loc(int32 Id) const
{
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

// 1:1 таблица WndProc (`AsdaStory_game.c:23941-24085`). Значения см. в шапке .h.
int32 UAsdaDisconnectPopupSubsystem::ReasonToNoticeId(int32 Reason)
{
	switch (Reason)
	{
		case 0:   return 1002;    // «Сервер разорвал соединение.»
		case 1:   return 1098;    // сбой в сети
		case 2:   return 1099;    // смена точки входа (дубль-сессия)
		case 3:   return 1100;    // проблемы с безопасностью (MD5/secret)
		// ★ 4 — ветки НЕТ (`default: break`). Это не пропуск разбора: причина 4 ставится ПОСЛЕ
		//   собственного показа попапа с нотисом 1001 при неудаче коннекта к портал-серверу
		//   (`AsdaStory_game.c:31490-31493`), поэтому второй показ был бы дублем.
		case 5:   return 8509;    // недостаточно данных
		case 6:   return 1106;    // отключён за нечестную игру
		case 7:   return 1107;    // учётная запись заблокирована
		case 8:   return 9566;    // игрок выходит — подключение ограничено
		case 9:   return 9567;    // возрастное ограничение
		case 10:  return 12940;   // несовпадение версий
		case 11:  return 12941;   // ошибка авторизации
		case 12:  return 14000;   // HackShield не запустился
		case 13:  return 14103;
		// ★ 14: оригинал показывает попап ДВАЖДЫ — `g_GameState = 100` → показ → `= 11` → показ
		//   (`:24083-24085`). Второй показ существует только потому, что от состояния зависит,
		//   какую из двух веток выберет `Show_DisconnectPopup` (догружать .JUI или нет). У нас
		//   ветка выбирается в `PickWidgetPath`, а окно в реестре одно — второй вызов вернул бы
		//   тот же виджет. Поэтому здесь одна запись, а не две.
		case 14:  return 14150;
		case 15:  return 7034;    // блокировка за спам
		case 255: return 1108;    // «обрыв без 4101» — самый частый случай
		default:  return -1;      // причины нет в таблице эталона ⇒ попапа нет
	}
}

const TCHAR* UAsdaDisconnectPopupSubsystem::PickWidgetPath() const
{
	// Эталонная развилка по `g_GameState`: игровые состояния берут окно ИЗ НАБОРА (класс
	//   `gamemain`), «прочие» — догружают `./JUIData/CommonWin.JUI` (класс `commonwin`).
	//   Признак «мы в игре» у порта — живой игровой мир с павном; отдельного `g_GameState` нет.
	const UGameInstance* GI = GetGameInstance();
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	const bool bInGame = (W != nullptr) && (W->GetFirstPlayerController() != nullptr)
		&& (W->GetFirstPlayerController()->GetPawn() != nullptr);
	return bInGame ? kPathGameMain : kPathCommonWin;
}

bool UAsdaDisconnectPopupSubsystem::IsOpen() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsOpen(kWindowId);
}

void UAsdaDisconnectPopupSubsystem::OnGameDisconnected()
{
	// ★ Ревизия 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Load.DisconnectDropsScreen`, разбор — в AsdaLoadManagerSubsystem.cpp у ключа): обрыв или
	//   отказ сервера во время загрузки карты — экран загрузки (ZOrder 30000, выше модалок 20000) снимается СРАЗУ, иначе попап
	//   лежал бы под ним, а при причине 255 гасился бы гейтом ниже, и игрок до 120 с видел бы застывший экран, а потом мир без связи.
	{
		UGameInstance* LGI = GetGameInstance();
		UAsdaLoadManagerSubsystem* Load = LGI ? LGI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
		if (Load && Load->IsLoadingScreenVisible() && UAsdaLoadManagerSubsystem::IsDisconnectDropsScreenOn())
		{
			Load->AbortLoadingScreen(TEXT("обрыв игрового соединения во время загрузки карты — показываем попап обрыва"));
		}
	}
	// ★ 19.10.2026: кик оператором 6019 — эталон показывает 2113 сам (`Show_DisconnectPopup(2113)`), таблица причин не участвует.
	if (NextNoticeOverride > 0)
	{
		const int32 Id = NextNoticeOverride;
		NextNoticeOverride = 0;
		UE_LOG(LogAsdaDisc, Warning, TEXT("[обрыв] заданный текст попапа: нотис %d (кик оператором)"), Id);
		ShowDisconnectPopup(Id);
		return;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// Код последнего 4101; 255 = обрыв без 4101 (эталонное `g_nDisconnectReason = 255`).
	const int32 Reason = Net ? Net->GetLastDisconnectReason() : 255;
	// ★★★ РЕВИЗИЯ 2026-09-17 (М2): У ПРИЧИНЫ 255 ЭТАЛОН ИМЕЕТ ГЕЙТ «НЕ ИДЁТ ЗАГРУЗКА».
	//   `AsdaStory_game.c:24034` — `if (g_nDisconnectReason == 255 && sub_93C96F(g_pNetSession) == wParam
	//   && !g_bLoadingInProgress)`. То есть обрыв БЕЗ кода 4101 во время загрузки карты попап НЕ
	//   показывает: смена карты у оригинала рвёт старое соединение штатно, и окно 8000 поверх
	//   загрузочного экрана было бы ложной тревогой. Прочие причины (0…15) гейта не имеют — они
	//   приходят из тела 4101, то есть это осознанный отказ сервера, а не побочный разрыв.
	//   Порт-эквивалент `g_bLoadingInProgress` — поднятый загрузочный экран
	//   (`UAsdaLoadManagerSubsystem::IsLoadingScreenVisible`).
	if (Reason == 255)
	{
		const UGameInstance* GI = GetGameInstance();
		const UAsdaLoadManagerSubsystem* Load = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
		if (Load && Load->IsLoadingScreenVisible())
		{
			UE_LOG(LogAsdaDisc, Log,
				TEXT("[обрыв] причина 255 во время загрузки карты — эталон попап не показывает (гейт !g_bLoadingInProgress)"));
			return;
		}
	}
	const int32 NoticeId = ReasonToNoticeId(Reason);
	if (NoticeId < 0)
	{
		UE_LOG(LogAsdaDisc, Log,
			TEXT("[обрыв] причина %d — в таблице эталона попапа для неё нет (ветка default), окно не показываем"),
			Reason);
		return;
	}
	UE_LOG(LogAsdaDisc, Warning, TEXT("[обрыв] причина %d → нотис %d, показываем COMMONWIN_MSGBOX 8000"),
		Reason, NoticeId);
	ShowDisconnectPopup(NoticeId);
}

void UAsdaDisconnectPopupSubsystem::OnLoginKick(int32 Code, bool bClosed)
{
	// Оставлено объявленным ради полноты контракта: подписки нет (см. Initialize).
	UE_LOG(LogAsdaDisc, Verbose, TEXT("[обрыв] кик логина code=%d closed=%d — окном 8000 не обслуживается"),
		Code, bClosed ? 1 : 0);
}

UUserWidget* UAsdaDisconnectPopupSubsystem::ShowDisconnectPopup(int32 NoticeId)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return nullptr; }

	// Текст: `a2 == −1` → строка последней ошибки приложения (`sub_9236EA(g_pAppRoot)`), иначе нотис.
	//   ★ У порта «последней ошибки приложения» в смысле оригинала нет — есть последняя причина
	//     разрыва. Чтобы не выдумывать, при −1 берём нотис 1108 «Соединение с сервером разорвано»,
	//     то есть ровно ту строку, которой оригинал отвечает на обрыв без кода.
	const int32 EffId = (NoticeId == -1) ? 1108 : NoticeId;
	FString Message = Loc(EffId);
	if (Message.IsEmpty())
	{
		UE_LOG(LogAsdaDisc, Warning, TEXT("[обрыв] нотиса %d нет в DT_GameText — показываем пустой попап"), EffId);
	}
	return ShowPopupWithText(MoveTemp(Message), /*bWrapToSlot*/false, EffId);
}

UUserWidget* UAsdaDisconnectPopupSubsystem::ShowDisconnectPopupText(const FString& Message)
{
	return ShowPopupWithText(Message, /*bWrapToSlot*/true, /*LogId*/0);
}

FString UAsdaDisconnectPopupSubsystem::WrapWordsToWidth(const FString& Text, const FSlateFontInfo& Font, float WrapW)
{
	if (!FSlateApplication::IsInitialized() || WrapW <= 1.f) { return Text; }
	const TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	TArray<FString> Paras;
	Text.ParseIntoArray(Paras, TEXT("\n"), /*CullEmpty*/false);
	TArray<FString> Out;
	for (const FString& Para : Paras)
	{
		TArray<FString> Words;
		Para.ParseIntoArrayWS(Words);
		if (Words.Num() == 0) { Out.Add(FString()); continue; }   // пустая строка абзаца сохраняется
		FString Cur;
		for (const FString& W : Words)
		{
			const FString Try = Cur.IsEmpty() ? W : (Cur + TEXT(" ") + W);
			if (!Cur.IsEmpty() && FM->Measure(Try, Font).X > WrapW)
			{
				Out.Add(Cur);   // строка заполнена — слово уходит на следующую
				Cur = W;
			}
			else
			{
				Cur = Try;
			}
		}
		Out.Add(Cur);
	}
	return FString::Join(Out, TEXT("\n"));
}

UUserWidget* UAsdaDisconnectPopupSubsystem::ShowPopupWithText(FString Message, bool bWrapToSlot, int32 LogId)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return nullptr; }
	const int32 EffId = LogId;
	// ★ В таблице строк перевод строки лежит ДВУМЯ символами «\n» (нотис 1108 —
	//   «Соединение с сервером разорвано,\nигра отключается.\n»), потому что это escape-
	//   последовательность исходного .BIN, а не байт 0x0A. Разворачиваем её ЗДЕСЬ, у показа:
	//   трогать общую точку `AsdaNotice::Loc` нельзя — на её сырой вид опираются другие окна.
	Message = Message.Replace(TEXT("\\n"), TEXT("\n"));

	const TCHAR* Path = PickWidgetPath();
	UUserWidget* Box = WM->OpenWindowByPath(kWindowId, Path, kDisconnectPopupZ,
		ESlateVisibility::Visible, /*bModal*/true);
	if (!Box)
	{
		UE_LOG(LogAsdaDisc, Error, TEXT("[обрыв] %s не загрузился — попап показать нечем; текст: %s"),
			Path, *Message);
		return nullptr;
	}

	// 8002 `COMMONWIN_TEXT` — тело сообщения. AutoWrap в канвасе ломает раскладку
	//   (закон `ui-autowrap-canvas-gotcha`), перенос уже есть в самой строке нотиса.
	if (UTextBlock* Tb = Cast<UTextBlock>(Box->GetWidgetFromName(TEXT("COMMONWIN_TEXT"))))
	{
		Tb->SetAutoWrapText(false);
		Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (bWrapToSlot)
		{
			// ★ 16.10 (EAC): ширина переноса = ширина авторского слота контрола (разметку не трогаем, только текст).
			const UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Tb->Slot);
			const float WrapW = CS ? (float)CS->GetSize().X : 0.f;
			Message = WrapWordsToWidth(Message, Tb->GetFont(), WrapW);
		}
		Tb->SetText(FText::FromString(Message));
	}
	else
	{
		// Эталон в таких местах показывает MessageBoxA «Point NULL» — у нас честная строка в лог.
		UE_LOG(LogAsdaDisc, Error, TEXT("[обрыв] COMMONWIN_TEXT 8002 не найден — текст ставить некуда"));
	}

	// 8003 `COMMONWIN_BUT` — подпись из нотиса 2288 (`Collection_AddString`, item_inventory.c:64360).
	//   Пусто в таблице ⇒ оставляем подпись из ассета (закон `no-invented-ui`).
	if (UTextBlock* BT = Cast<UTextBlock>(Box->GetWidgetFromName(TEXT("COMMONWIN_BUT_Text"))))
	{
		const FString S = Loc(AsdaLoc::kBtnOk);
		if (!S.IsEmpty()) { BT->SetText(FText::FromString(S)); }
		BT->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	// Единственная активная кнопка окна.
	if (UButton* B = Cast<UButton>(Box->GetWidgetFromName(TEXT("COMMONWIN_BUT"))))
	{
		B->OnClicked.Clear();
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { OnOkClicked(); };
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		OkRelay = R;
	}
	else
	{
		UE_LOG(LogAsdaDisc, Error, TEXT("[обрыв] кнопка COMMONWIN_BUT 8003 не найдена — окно нечем закрыть"));
	}

	UE_LOG(LogAsdaDisc, Warning, TEXT("[обрыв] попап 8000 показан (%s, нотис %d): %s"),
		(Path == kPathGameMain) ? TEXT("gamemain") : TEXT("commonwin"), EffId,
		*Message.Replace(TEXT("\n"), TEXT(" / ")));
	return Box;
}

void UAsdaDisconnectPopupSubsystem::OnOkClicked()
{
	const bool bQuit = (CVarDisconnectPopupQuit.GetValueOnGameThread() != 0);
	// ★★★ ИСПРАВЛЕНИЕ 19.10.2026 (жалоба владельца: «сервер дисконнектит игрока, он нажимает ОК, окно закрывается и он
	//   продолжает ходить»). Прежде ОК только закрывал попап: игрок оставался в мире без связи (сервер его уже забыл), и с
	//   уходом модальности ввод возвращался. Оригинал здесь закрывает клиент целиком; порт оставляет клиент живым, поэтому
	//   из МИРА ОК теперь ведёт на экран входа — тот же переход, что «Вернуться к выбору персонажа» ESC-меню
	//   (`UAsdaSystemOptionSubsystem::DoReturnToCharSelect`), но без пакетов: связи нет. Экран входа при загрузке сам
	//   подключается к логин-серверу заново (`AAsdaLoginGameMode`, ранний коннект первым тиком), состояние сессии уже
	//   сброшено подписчиком `OnGameDisconnected`. Вне мира (сцена входа) — по-прежнему только закрыть попап.
	const UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	const bool bToLogin = !bQuit && W && !AsdaWorldKind::IsLoginScene(W);
	UE_LOG(LogAsdaDisc, Warning, TEXT("[обрыв] нажат ОК (8003): %s"),
		bQuit ? TEXT("1:1 оригинал — завершаем клиент (PostQuitMessage)")
		      : (bToLogin ? TEXT("из мира без связи — переход на экран входа, вход можно повторить")
		                  : TEXT("закрываем попап; сессия сброшена, вход можно повторить")));
	ClosePopup();
	if (bQuit)
	{
		// 1:1 `CommonWin_CommonEventFunc@0xC6EAE0`: `Net_RecvUiTick(); PostQuitMessage(0);`.
		//   `Net_RecvUiTick` у нас не нужен — очередь приёма уже остановлена закрытием сокета.
		FPlatformMisc::RequestExit(false);
		return;
	}
	if (bToLogin)
	{
		// Карта входа = `GameDefaultMap` проекта (как у `DoReturnToCharSelect`).
		UGameplayStatics::OpenLevel(W, FName(TEXT("/Game/Map/Login")));
	}
}

void UAsdaDisconnectPopupSubsystem::ClosePopup()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindow(kWindowId);
	}
	OkRelay = nullptr;
}
