// Copyright (c) 2026 @sitxovski (Discord)
// Порт контейнера социальной панели COMMUNITY_WIN (6400). Разбор — `Reports/soulmate_6400.md` §2.
#include "UI/AsdaCommunitySubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ волна В3: `CObject_GetField254(g_pInventoryWnd)` = барабан гачи
#include "UI/AsdaTradeStateSubsystem.h"     // ★ ПД-1 (29.09.2026): признак «личная лавка открыта» (22000/23000) для 107022

namespace
{
	// Причина отказа «открыта личная лавка» (условие 107022 `communityUI_Open`, social.c:3782) — по ней `OpenWindow`
	//   печатает нотис. Одна константа на запись и сравнение (раньше строка «идёт обмен» стояла в двух местах).
	const TCHAR* const kBlockSelfStore = TEXT("открыта личная лавка (22000/23000)");
}
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaCommunityFriendSubsystem.h"
#include "UI/AsdaCommunityClanSubsystem.h"
#include "UI/AsdaCommunityWhoSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "World/AsdaPlayerController.h"
#include "UI/AsdaUiHelpers.h"                // AsdaUi::FindNodeExact / SrgbU32ToLinear (общие точки)
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "HAL/IConsoleManager.h"             // TAutoConsoleVariable — ключ отката цветов подписей
#include "Framework/Application/SlateApplication.h"   // ★ 10.10.2026: фокус игре при закрытии 6400 (`communityUI_Close`)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCommunity, Log, All);

namespace
{
	// ═══ ★★★ ЦВЕТА ПОДПИСЕЙ ВКЛАДОК ПОЛОСЫ 6401 `COMMUNITY_MENU` ════════════════════════════════
	//
	// ЭТАЛОН (закон `jui-caption-states`: состояние кнопки живёт в ПОДПИСИ, а не в картинке).
	//   Скин полосы — `TabBar_Cmt_4Btn`, тип `RadioButton`, 4 плана (по одному на вкладку),
	//   у каждого 4 кадра (`RE/docs/JuiSkin.ini`, строка 61517). `CaptionColor` по кадрам
	//   ОДИНАКОВ у всех четырёх планов:
	//       кадр 0 «покой»      FF646464
	//       кадр 1 «наведение»  FF969696
	//       кадр 2 «нажато»     FFFFFFFF   ← это и есть ВЫБРАННАЯ вкладка
	//       кадр 3              FFFFFFFF
	//   Кадр 0 перекрывает сам контрол: `LoadJUI_ParseNode` пишет его `CaptionInfo` в АКТИВНЫЙ
	//   кадр, а активен при загрузке нулевой. У 6401 `CaptionInfo = "0 -1 ff808080 5"`
	//   (`Data/Decoded/jui/gamemain_jui.ini:201353`), блок `EXS` ПУСТ — значит `EXS TextColor` здесь
	//   нет и приоритет закона `ui-text-color-exs` не применяется, цвет берётся из скина/CaptionInfo.
	//   ⇒ ОРИГИНАЛ: активная вкладка БЕЛАЯ (FFFFFFFF), неактивные СЕРЫЕ (ff808080).
	//
	// ★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (закон `fix-original-defects` — помечаем).
	//   Владелец 02.09.2026: «у активной цвет должен быть БЕЛЫЙ, у неактивных ЧЁРНЫЙ».
	//   Активная — ровно кадр 2 эталона, отступления нет. Неактивные у оригинала СЕРЫЕ
	//   (ff808080 из `CaptionInfo`, ff646464 в скине), а в ассете после шага
	//   `Tools/asda_ui_tabbar_captions.py` лежит ff4c4c4c — ни один из трёх не чёрный.
	//   Ставим ЧЁРНЫЙ по требованию владельца. Откат на цвет эталона — `asda.UI.CommunityTabCaptionColors 0`.
	//
	// ★ Гамма: значения записаны в sRGB ровно так, как в данных JUI; в линейное пространство их
	//   переводит ОБЩАЯ ТОЧКА `AsdaUi::SrgbU32ToLinear` (закон `ui-srgb-gamma-rule`; своей кривой
	//   не заводим — пара `SrgbToLinearChannel`/`LinearToSrgbChannel` уже живёт в `AsdaUiHelpers`).
	constexpr uint32 kTabCapActiveArgb        = 0xFFFFFFFFu;   // активная — кадр 2 скина, 1:1 эталон
	constexpr uint32 kTabCapInactiveArgb      = 0xFF000000u;   // неактивные — требование владельца
	constexpr uint32 kTabCapInactiveOrigArgb  = 0xFF808080u;   // неактивные у эталона (для отката)

	ASDA_DEV_CVAR(int32, CVarTabCaptionColors,
		TEXT("asda.UI.CommunityTabCaptionColors"), 1,
		TEXT("Цвет подписей вкладок окна 6400 «Сообщества».\n")
		TEXT("1 (по умолчанию) = требование владельца: активная БЕЛАЯ, неактивные ЧЁРНЫЕ.\n")
		TEXT("0 = как в оригинале: активная белая (кадр 2 скина TabBar_Cmt_4Btn),\n")
		TEXT("неактивные серые ff808080 (CaptionInfo контрола 6401)."),
		ECVF_Default);

	// ★ 25.10.2026 (ui_toggles, `Reports/ui_toggles/TOGGLE_AUDIT.md`) [ОРИГИНАЛ]. Клавиши 35 «Друг» (U) и 36 «Кто?» (J)
	//   у эталона (`input.c:4293-4297`) зовут `communityUI_OpenOrToggleTab@0xBD3C20` (social.c:3742): окно открыто и вкладка та
	//   же → `communityUI_Close`; открыто на другой вкладке → переключить; закрыто → переключить и открыть. Порт звал
	//   `OpenOnTab` — только «открыть»: каждое нажатие заново показывало вкладку и слало 5480, окно не закрывалось никогда
	//   (жалоба игроков «Menu fails to collapse when pressed repeatedly»). «Открыто» — по самому окну 6400 (`IsWindowOpen`, D1).
	ASDA_DEV_CVAR(int32, CVarHotkeyToggle,
		TEXT("asda.Community.HotkeyToggle"), 1,
		TEXT("1 (по умолчанию) = клавиши U/J (команды 35/36) — тумблер вкладки, как эталон communityUI_OpenOrToggleTab: повторное ")
		TEXT("нажатие на открытой той же вкладке закрывает окно 6400; 0 = прежнее (только открыть — окно не закрывалось)."),
		ECVF_Default);
}

const FName  UAsdaCommunitySubsystem::kWindowId(TEXT("COMMUNITY_WIN"));
const TCHAR* UAsdaCommunitySubsystem::kWindowWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN.WBP_COMMUNITY_WIN_C");
const TCHAR* UAsdaCommunitySubsystem::kTabBarIdc = TEXT("COMMUNITY_MENU");
// Полоса-двойник 13631: разбор — в объявлении (`AsdaCommunitySubsystem.h`).
const TCHAR* UAsdaCommunitySubsystem::kHiddenTabBarIdc = TEXT("COMMUNITY_MENU_2");
// Под-панели вкладок (JUI id): 0 соулмейт, 1 приятель, 2 клан, 3 «Кто?».
const TCHAR* UAsdaCommunitySubsystem::kTabPanelNames[4] =
{
	TEXT("COMMUNITY_SOUL_WIN"), TEXT("COMMUNITY_FRND_WIN"),
	TEXT("COMMUNITY_CLAN_WIN"), TEXT("COMMUNITY_Who_WIN"),
};
// Подписи вкладок в ассете (разбор имён — в объявлении). Нумерация суффикса идёт СО ВТОРОЙ вкладки.
const TCHAR* UAsdaCommunitySubsystem::kTabCaptionNames[4] =
{
	TEXT("COMMUNITY_MENU_Text"),  TEXT("COMMUNITY_MENU_Text2"),
	TEXT("COMMUNITY_MENU_Text3"), TEXT("COMMUNITY_MENU_Text4"),
};

void UAsdaCommunitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogAsdaCommunity, Log, TEXT("[сообщество] подсистема поднята (окно 6400, 4 вкладки)"));
}

void UAsdaCommunitySubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.RemoveDynamic(this, &UAsdaCommunitySubsystem::OnMenuCommand);
		}
	}
	if (AAsdaPlayerController* Pc = BoundController.Get())
	{
		Pc->OnGameCommand.RemoveDynamic(this, &UAsdaCommunitySubsystem::OnGameCommand);
	}
	Super::Deinitialize();
}

// ★★★ К-3 / находка Р5 отчёта `14_soulmate_ревизия.md`: окно 6400 «Сообщество» —
//   контейнер всех четырёх вкладок соц-блока. Если оно осталось открытым, следующий персонаж
//   видит его сразу после входа, ещё до единого пакета от сервера.
void UAsdaCommunitySubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	if (IsWindowOpen())
	{
		UE_LOG(LogAsdaCommunity, Log, TEXT("[сообщество] сброс сессии (%s): окно 6400 закрыто"),
			AsdaSessionResetReasonName(Reason));
		CloseWindow();
	}
}

void UAsdaCommunitySubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Community");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	// Кнопка меню 73405: команда (открыть/закрыть) + подсветка. Меню-подсистема существует сразу,
	//   но подписываемся один раз (флаг), как это делают журнал/характеристики.
	if (!bMenuBound)
	{
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.AddDynamic(this, &UAsdaCommunitySubsystem::OnMenuCommand);
			Menu->RegisterWindow(kMenuButtonIdNum);   // подсветка по видимости COMMUNITY_WIN
			bMenuBound = true;
			UE_LOG(LogAsdaCommunity, Log, TEXT("[сообщество] кнопка меню %d: команда + подсветка"), kMenuButtonIdNum);
		}
	}

	// Хоткеи 35/36 — контроллер появляется позже подсистемы.
	if (UWorld* W = GI->GetWorld())
	{
		AAsdaPlayerController* Pc = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
		if (Pc && Pc != BoundController.Get())
		{
			if (AAsdaPlayerController* Old = BoundController.Get())
			{
				Old->OnGameCommand.RemoveDynamic(this, &UAsdaCommunitySubsystem::OnGameCommand);
			}
			Pc->OnGameCommand.AddDynamic(this, &UAsdaCommunitySubsystem::OnGameCommand);
			BoundController = Pc;
		}
	}
}

// ═══════════════════════════════ ОКНО ═════════════════════════════════════════════════════════

bool UAsdaCommunitySubsystem::EnsureWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(Window) && bReady) { return true; }
	if (Window) { ResetWidget(); }

	Window = WM->OpenWindowByPath(kWindowId, kWindowWbp, kWinZ, ESlateVisibility::SelfHitTestInvisible);
	if (!Window)
	{
		// Эталон здесь показал бы модальный MessageBoxA (дефект D5) — мы просто честно логируем.
		UE_LOG(LogAsdaCommunity, Error, TEXT("[сообщество] WBP_COMMUNITY_WIN не загрузился (%s)"), kWindowWbp);
		return false;
	}
	SetupControls();
	// ★ Владелец сам объявляет, как закрыть себя по-игровому (крестик/Esc → CloseWindow).
	WM->RegisterWindowCloser(kWindowId, FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseWindow(); }));
	WM->SetWindowVisible(kWindowId, false);   // сначала скрыто; покажет OpenWindow после гейтов
	return bReady;
}

void UAsdaCommunitySubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindow(kWindowId);
	}
	Window = nullptr;
	TabButtons.Reset();
	TabCaptions.Reset();
	Relays.Reset();     // реле мёртвого дерева не держим (разбор — у объявления `Relays`)
	bReady = false;
}

void UAsdaCommunitySubsystem::SetupControls()
{
	if (!Window) { return; }
	TabButtons.Reset();
	TabButtons.SetNum(4);
	TabCaptions.Reset();
	TabCaptions.SetNum(4);
	// ★★★ Реле ПРОШЛОГО дерева окна ведут в уничтоженные виджеты (окно 6400 пересоздаётся при
	//   смене карты — `EnsureWindow` → `IsWindowLive`). Чистим перед новой привязкой.
	Relays.Reset();
	int32 nBound = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		const FString BtnName = FString::Printf(TEXT("%s_OPT%d"), kTabBarIdc, i);
		UButton* Btn = Cast<UButton>(Window->GetWidgetFromName(FName(*BtnName)));
		TabButtons[i] = Btn;
		if (!Btn)
		{
			UE_LOG(LogAsdaCommunity, Warning, TEXT("[сообщество] кнопка вкладки «%s» не найдена"), *BtnName);
			continue;
		}
		AsdaUi::BindButton(this, Btn, i, [this](int32 Tab) { OnTabButtonClicked(Tab); }, Relays);   // ★ U20: реле клика — общая точка   // ★★★ GC-якорь: делегат держит реле СЛАБО (разбор — у объявления `Relays`)
		++nBound;
	}

	// ★★★ КРЕСТИК ОКНА 6403 `COMMUNITY_CLOSE` — ДО 02.09.2026 НЕ БЫЛ ПРИВЯЗАН ВООБЩЕ.
	//   Проверено грепом по всему `Source/`: строки `COMMUNITY_CLOSE` не было ни в одном файле,
	//   общего «привязать любую кнопку с именем `*_CLOSE`» в порту тоже нет
	//   (`UAsdaWindowSubsystem::RegisterWindowCloser` обслуживает только массовое
	//   `CloseAllPlayerWindows`, а не клик по крестику). То есть окно «Сообщества» своим крестиком
	//   не закрывалось НИКОГДА — ровно жалоба владельца «окно не закрывается».
	//   ЭТАЛОН: контрол 6403 в разметке — обычная кнопка `CloseBtn_18` без `EVS`
	//   (`gamemain_jui.ini:184224`), её команда приходит в диспетчер окна и уходит в
	//   `communityUI_Close@0xBD3F20` — то же, что делает наш `CloseWindow()`
	//   (скрыть окно + снять вкладку соулмейта). Привязываем 1:1.
	if (UButton* Close = Cast<UButton>(AsdaUi::FindNodeExact(Window, TEXT("COMMUNITY_CLOSE"))))
	{
		AsdaUi::BindButton(this, Close, kCloseButtonIdNum, [this](int32) { CloseWindow(); }, Relays);   // ★ U20: реле клика — общая точка
	}
	else
	{
		UE_LOG(LogAsdaCommunity, Warning,
			TEXT("[сообщество] крестик «COMMUNITY_CLOSE» (%d) не найден — окно нечем закрыть"),
			kCloseButtonIdNum);
	}

	// Подписи вкладок ищем по СВОИМ именам (`kTabCaptionNames`), а не через `FindLabel(_OPT<i>)`:
	//   разбор — в объявлении `kTabCaptionNames`. Текст и цвет ставит `ApplyTabBarState`.
	int32 nCaps = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		UTextBlock* T = Cast<UTextBlock>(AsdaUi::FindNodeExact(Window, kTabCaptionNames[i]));
		TabCaptions[i] = T;
		if (T) { ++nCaps; }
		else
		{
			UE_LOG(LogAsdaCommunity, Warning, TEXT("[сообщество] подпись вкладки «%s» не найдена"),
				kTabCaptionNames[i]);
		}
	}

	bReady = (nBound > 0);
	UE_LOG(LogAsdaCommunity, Log, TEXT("[сообщество] контролы привязаны: вкладок-кнопок %d/4, подписей %d/4"),
		nBound, nCaps);
}

// ★★★ ВИД ПОЛОСЫ ВКЛАДОК 6401 ЦЕЛИКОМ: выбор + текст + цвет подписей.
//   ЭТАЛОН делает это ДВУМЯ местами: `Community_BuildUI@0xBD38E0` заливает подписи из loc
//   (`CListBox_SetItemString(this+4, 0..3, 8014/8015/8016/8060)`) и ставит выбор на 0
//   (`CControl_SetSelectedIndex(this+4, 0)`), а `communityUI_SwitchTab@0xBD4040` переносит выбор.
//   Цвет подписи в оригинале никто не «ставит» — его даёт кадр-состояние плана скина; в UMG
//   кадр подписи не существует, поэтому цвет раскладываем сами (`ApplyTabCaptionColors`).
void UAsdaCommunitySubsystem::ApplyTabBarState(int32 Tab)
{
	if (!Window) { return; }
	Tab = FMath::Clamp(Tab, 0, 3);

	// 1) Текст подписей — из loc, как в `Community_BuildUI`. Пустую строку НЕ пишем: пока
	//    таблица текста не поднялась, лучше оставить то, что положил импортёр, чем стереть.
	for (int32 i = 0; i < 4; ++i)
	{
		UTextBlock* T = TabCaptions.IsValidIndex(i) ? TabCaptions[i] : nullptr;
		if (!T) { continue; }
		const FString S = Notice(kTabCaptionLoc[i]);
		if (!S.IsEmpty() && !T->GetText().ToString().Equals(S, ESearchCase::CaseSensitive))
		{
			T->SetText(FText::FromString(S));
		}
	}

	// 2) Выбранный пункт полосы: плашка кадра 2 + гашение кисти активной кнопки.
	//    ⚠ Подписи этот примитив НЕ красит: он ищет их как `<idc>_OPT<i>_Text`, а в ассете они
	//    называются `<idc>_Text<N>` — см. объявление `kTabCaptionNames`. Цвет ставим следом сами.
	AsdaCommunityUi::SetOptSelected(Window, kTabBarIdc, Tab);

	// 3) Цвет подписей.
	ApplyTabCaptionColors(Tab);
}

void UAsdaCommunitySubsystem::ApplyTabCaptionColors(int32 Tab)
{
	Tab = FMath::Clamp(Tab, 0, 3);
	const bool bOwnerColors = (CVarTabCaptionColors.GetValueOnGameThread() != 0);
	const FLinearColor Active   = AsdaUi::SrgbU32ToLinear(kTabCapActiveArgb);
	const FLinearColor Inactive = AsdaUi::SrgbU32ToLinear(
		bOwnerColors ? kTabCapInactiveArgb : kTabCapInactiveOrigArgb);

	for (int32 i = 0; i < 4; ++i)
	{
		UTextBlock* T = TabCaptions.IsValidIndex(i) ? TabCaptions[i] : nullptr;
		if (!T) { continue; }
		T->SetColorAndOpacity(FSlateColor(i == Tab ? Active : Inactive));
	}
}

// ★★★ ЗАКОН `jui-uneven-node-layout`: импортёр РАСПЛЮЩИВАЕТ иерархию JUI, поэтому гашение ОДНОГО
//   узла панели НЕ прячет её содержимое — дети лежат рядом, а не внутри. Собираем ВСЁ поддерево
//   по ПРЕФИКСУ имени (`N == P || N.StartsWith(P + "_")`) и правим видимость полным списком.
//   Точечные ссылки оставляем только для ЗАПОЛНЕНИЯ (тексты/иконки), не для видимости.
// ★★★ КРИТИЧНО (аудит 31.08.2026). Импортёр `Tools/asda_jui_generic_umg.py`:
//   • группа контрола называется **`<idc>_grp`** (строка 780: `san(n.idc or n.name) + "_grp"`),
//     а САМОГО узла с именем `<idc>` в ассете НЕТ;
//   • у окна с `Visible="0"` в JUI он помечает **COLLAPSED КАЖДЫЙ** виджет поддерева
//     (строки 788-803) — и контейнер, и каждый лист.
//   Прежняя версия правила видимость по ПРЕФИКСУ ИМЕНИ вкладки и находила лишь пару узлов ⇒
//   вкладки «Приятель»/«Клан»/«Кто?» не показывались вовсе (у «Друга» в JUI `Visible="1"`,
//   поэтому дефект там не проявлялся).
//   Правильный способ — взять группу `<Вкладка>_grp` и обойти ЕЁ РЕАЛЬНОЕ поддерево.
//   ⚠ Узлы, которые не должны показываться никогда (в JUI у них `Visible="0"`), гасит ПОСЛЕ
//     общего показа сама подсистема вкладки в `OnTabActivated` — закон `jui-uneven-node-layout`.
// ★★★ КРИТИЧНО (найдено 31.08.2026): при ПОКАЗЕ нельзя ставить `SelfHitTestInvisible`
//   ВСЕМУ поддереву: `UButton` с такой видимостью НЕ ЛОВИТ МЫШЬ — все кнопки
//   вкладки оказываются мёртвыми.
// ★★★ ДЕДУПЛИКАЦИЯ 2026-09-02. Здесь лежала СВОЯ копия правила
//   («`Cast<UButton>(Root) ? Visible : SelfHitTestInvisible`»), одна из восьми по проекту.
//   Копия знала только про кнопку, поэтому поле заметки соулмейта `Soul_Msg_Input` (6602) и
//   галочки `SOUL_CHECK00…03` (6543-6546) оставались мёртвыми для мыши: вкладка соулмейта
//   `AsdaCommunityUi::SetVisByPrefix` не зовёт вовсе, её поддерево показывает ИМЕННО эта функция.
//   Теперь правило одно на весь порт — `AsdaCommunityUi::ShownVisibilityFor`
//   (`UI/AsdaCommunityFriendSubsystem.h`), откат `asda.UI.InteractiveHitTest 0`.
static void AsdaSetSubtreeVisibility(UWidget* Root, bool bShow)
{
	if (!Root) { return; }
	const ESlateVisibility Vis = bShow
		? AsdaCommunityUi::ShownVisibilityFor(Root)
		: ESlateVisibility::Collapsed;
	Root->SetVisibility(Vis);
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Root))
	{
		const int32 N = Panel->GetChildrenCount();
		for (int32 i = 0; i < N; ++i)
		{
			AsdaSetSubtreeVisibility(Panel->GetChildAt(i), bShow);
		}
	}
}

void UAsdaCommunitySubsystem::ApplyTabVisibility(int32 Tab)
{
	if (!Window) { return; }
	Tab = FMath::Clamp(Tab, 0, 3);

	for (int32 i = 0; i < 4; ++i)
	{
		const bool bShow = (i == Tab);
		const FString Base = kTabPanelNames[i];
		// Импортёр кладёт поддерево вкладки в группу `<idc>_grp`; узла `<idc>` в ассете нет.
		UWidget* Root = Window->GetWidgetFromName(FName(*(Base + TEXT("_grp"))));
		if (!Root) { Root = Window->GetWidgetFromName(FName(*Base)); }   // подстраховка
		if (!Root)
		{
			UE_LOG(LogAsdaCommunity, Warning,
				TEXT("[сообщество] поддерево вкладки «%s» не найдено (ни «%s_grp», ни «%s») — вкладка не переключится"),
				*Base, *Base, *Base);
			continue;
		}
		AsdaSetSubtreeVisibility(Root, bShow);
	}

	// Содержимое вкладок ведут отдельные подсистемы — они же гасят свои `AlwaysHidden`-узлы.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaSoulmateSubsystem* Soul = GI->GetSubsystem<UAsdaSoulmateSubsystem>())
		{
			Soul->OnTabActivated(Tab == 0, Window);
		}
		if (UAsdaCommunityFriendSubsystem* Frnd = GI->GetSubsystem<UAsdaCommunityFriendSubsystem>())
		{
			Frnd->OnTabActivated(Tab == 1, Window);
		}
		if (UAsdaCommunityClanSubsystem* Clan = GI->GetSubsystem<UAsdaCommunityClanSubsystem>())
		{
			Clan->OnTabActivated(Tab == 2, Window);
		}
		if (UAsdaCommunityWhoSubsystem* Who = GI->GetSubsystem<UAsdaCommunityWhoSubsystem>())
		{
			Who->OnTabActivated(Tab == 3, Window);
		}
	}
}

// ═══════════════════════════════ ГЕЙТЫ ════════════════════════════════════════════════════════

bool UAsdaCommunitySubsystem::CanOpen(FString& OutBlockReason) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { OutBlockReason = TEXT("нет оконной подсистемы"); return false; }

	// `communityUI_Open`: не открывать, пока видимы магазин/парикмахерская/барабан гачи/склад.
	//   ★ Волна В3 ремесла (26.09.2026): третий гейт эталона — `CObject_GetField254(g_pInventoryWnd)` (social.c:3780).
	//   Прежний порт читал его как «окно инвентаря видно» (`INVEN_WIN`) — это НЕВЕРНО: поле +0x3F8 окна сумки пишет
	//   `set_oFE@0xD1F7D0` — флаг «открыт барабан гачи 10050» (доказательство тремя путями — у гейта
	//   `UAsdaHotbarSubsystem::IsActivationBlocked`). Из-за ошибки сообщество не открывалось при открытой сумке, и
	//   восстановление снимка окон (слоты 0 и 14) не могло вернуть оба окна. Теперь — барабан гачи.
	static const TCHAR* kBlockers[] = {
		TEXT("SHOP_WIN"), TEXT("HAIRSHOP_WIN"), TEXT("WARE_WIN"),
	};
	if (const UAsdaBoosterPanelSubsystem* Booster = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>())
	{
		if (Booster->IsWindowOpen()) { OutBlockReason = TEXT("открыт барабан гачи (CObject_GetField254)"); return false; }
	}
	for (const TCHAR* WName : kBlockers)
	{
		if (WM->IsWindowVisible(FName(WName)))
		{
			OutBlockReason = FString::Printf(TEXT("открыто окно %s"), WName);
			return false;
		}
	}
	// ★★ ПД-1 (29.09.2026, `Reports/trade/trade_re_report.md` §2.3). [ОРИГИНАЛ] `communityUI_Open@0xBD3D10`
	//   (social.c:3782-3786): `if (sub_934E86(g_pSellWindow) || sub_929DC4(g_bSomeFeatureEnabled))` → loc 107022 в чат
	//   (цвет −11514, 10 с) и окно НЕ открывается. Это ЛИЧНАЯ ЛАВКА: окно подготовки 22000 (`CSellWindow+0x300`) или
	//   витрина 23000 (`CShopDlg+0x168`). Прежний порт проверял здесь видимость `EXCHANGE_WIN` — это АУКЦИОН 13000
	//   (`UAsdaExchangeSubsystem`), которого в условии эталона нет: открытый аукцион ложно запрещал сообщество.
	//   Окна обмена 2008 среди читателей этого условия тоже нет (скан читателей `CExchange+0x2C`, A §9).
	//   Признак лавки — общая точка `UAsdaTradeStateSubsystem::IsSelfStoreOpen` (пока лавки нет — false).
	if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI))
	{
		OutBlockReason = kBlockSelfStore;
		return false;
	}
	// ★ Волна В3 ремесла (ревизия В0–В2 С-1): `communityUI_Open@0xBD3D10` (social.c:3787) — показ окна под
	//   `!tileInteract_isControlVisible(g_pItemMainWnd) && !isBoundControlVisible(g_pItemQtyMakeDlg)` — МОЛЧА.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI))
	{
		OutBlockReason = TEXT("открыто окно ремесла 76000/77500 (молча, social.c:3787)");
		return false;
	}
	return true;
}

int32 UAsdaCommunitySubsystem::SelfClanId() const
{
	// ★ Волна 3: общая точка `AsdaCommunityUi::SelfClanId` — зеркало `CCharacter+0x968` в мире (−1 = клана нет).
	return AsdaCommunityUi::SelfClanId(GetGameInstance());
}

// ═══════════════════════════════ API ══════════════════════════════════════════════════════════

void UAsdaCommunitySubsystem::OpenWindow()
{
	FString Block;
	if (!CanOpen(Block))
	{
		UE_LOG(LogAsdaCommunity, Log, TEXT("[сообщество] открытие заблокировано: %s"), *Block);
		// [ОРИГИНАЛ] social.c:3784-3785 (ПД-1: лавка, не аукцион). ★ Ревизия С-1: канал обработчика — чат цветом −11514
		//   (0xFFFFD306), 10 с; прежний `PostChat` шёл категорией Social (белым).
		if (Block == kBlockSelfStore)
		{
			AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System, AsdaNotice::LocRaw(GetGameInstance(), 107022),
				AsdaNoticeColor::Gold, 10000.f);
		}
		return;
	}
	if (!EnsureWindow()) { return; }

	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }

	// ★ R5-fix6-c (13.11.2026) (уточнение комментария, код тот же): эталон `communityUI_Open@0xBD3D10` (social.c:3774-3801) вкладку НЕ
	//   трогает — окно открывается на последней выбранной (`this+10`, панели `this+6…8` хранят видимость от прошлого
	//   `communityUI_SwitchTab`); 0 — только после сборки окна (`Community_BuildUI`). Порт делает то же: `ActiveTab`.
	//   Клавиши 35/36 идут через `communityUI_OpenOrToggleTab` (сначала вкладка, затем открытие) — у порта `OpenOnTab`.
	ApplyTabVisibility(ActiveTab);
	WM->SetWindowVisible(kWindowId, true);
	// ★★★ Состояние «открыто» защёлкиваем ТОЛЬКО здесь — после реального показа (no-state-latch-before-shown).
	// ★ ВИД ПОЛОСЫ ВКЛАДОК — ТОЖЕ ЗДЕСЬ, И ТОЛЬКО ПОСЛЕ ПОКАЗА.
	//   Эталон ставит выбор в `Community_BuildUI` (`CControl_SetSelectedIndex(this+4, 0)`), а порт
	//   до этой правки звал `SetOptSelected` ЛИШЬ из `SwitchTab`. Открытие кнопкой меню 73405 или
	//   хоткеем в `SwitchTab` не заходит, поэтому полоса оставалась вообще без выделения: все
	//   четыре подписи одного цвета — ровно то, что видел владелец.
	ApplyTabBarState(ActiveTab);
	UE_LOG(LogAsdaCommunity, Log, TEXT("[сообщество] окно открыто, вкладка %d"), ActiveTab);
}

void UAsdaCommunitySubsystem::CloseWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM && IsWindowOpen())
	{
		WM->SetWindowVisible(kWindowId, false);
		if (UAsdaSoulmateSubsystem* Soul = GI->GetSubsystem<UAsdaSoulmateSubsystem>())
		{
			Soul->OnTabActivated(false, Window);   // вкладка соулмейта больше не видна (`sub_92F599`: 6605 + фокус 6602)
		}
		// ★ 10.10.2026 [ОРИГИНАЛ] (barber_minors, М-2 ревизии `esc_ranks_REVIEW.md`). Эталон `communityUI_Close@0xBD3F20`
		//   (social.c:3805-3826) после `sub_92F599` зовёт ещё два шага, которых в порту не было:
		//   1) `uiHideControlIfSet(g_pSoulmateWnd)` (ui_core.c:73414) — скрыть `this+78` = `m_pOldSoulWin`, под-окно
		//      6640 `OldSoulmate_Win` (structs_ru.h:1605). В ассете это узел `OldSoulmate_Win_grp` окна 6400;
		const bool bOldSoulShown = HideOldSoulmateWin();
		//   2) `sub_91F5D6(g_pCommunityWnd)` → `sub_BC8150@0xBC8150` — снять фокус с полей клана 14029/14021
		//      (общая точка порта — `UAsdaCommunityClanSubsystem::ClearEditFocus`). Окно уже скрыто, поэтому фокус, если
		//      он остался внутри окна 6400 (поля клана, заметка 6602), возвращается игре — как `SetFocus(поле, 0)` эталона.
		if (UAsdaCommunityClanSubsystem* Clan = GI->GetSubsystem<UAsdaCommunityClanSubsystem>()) { Clan->ClearEditFocus(); }
		const bool bFocusInside = Window && (Window->HasKeyboardFocus() || Window->HasFocusedDescendants());
		if (bFocusInside && FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		UE_LOG(LogAsdaCommunity, Log, TEXT("[сообщество] окно закрыто (6640 %s, фокус внутри окна %s)"),
			bOldSoulShown ? TEXT("было открыто — скрыто") : TEXT("закрыто"), bFocusInside ? TEXT("был — снят") : TEXT("не было"));
	}
}

// ★ 10.10.2026 (barber_minors): `uiHideControlIfSet(g_pSoulmateWnd)` — скрыть под-окно 6640 `OldSoulmate_Win`
//   (узел `OldSoulmate_Win_grp`). Возврат — было ли оно показано.
bool UAsdaCommunitySubsystem::HideOldSoulmateWin()
{
	UWidget* W = Window ? AsdaUi::FindNodeExact(Window, TEXT("OldSoulmate_Win_grp")) : nullptr;
	if (!W || !W->IsVisible()) { return false; }
	W->SetVisibility(ESlateVisibility::Collapsed);   // вид импортёра для скрытого узла .JUI
	return true;
}

// ★ 10.10.2026 (barber_minors): показать под-окно 6640 — ТОЛЬКО для самотеста `asda.UI.SelfTestEscOnTop` (без сети и данных).
//   ★ 24.10.2026 (sm-offline): штатный показ 6640 по ответу 2243 на кнопку 6584 перенесён — `UAsdaSoulmateSubsystem::RenderOldSoulGrid`
//   (порт `uiRefreshSlotGrid3x3@0xE6C2D0`, ключ `asda.Soulmate.OldSoulGrid`); закрытие — 6642 и `communityUI_Close` (выше).
bool UAsdaCommunitySubsystem::DebugShowOldSoulmateWin()
{
	UWidget* W = Window ? AsdaUi::FindNodeExact(Window, TEXT("OldSoulmate_Win_grp")) : nullptr;
	if (!W) { return false; }
	W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	return true;
}

// ★ 10.10.2026 (barber_minors): под-окно 6640 видно (самотест).
bool UAsdaCommunitySubsystem::IsOldSoulmateWinShown() const
{
	const UWidget* W = Window ? AsdaUi::FindNodeExact(Window, TEXT("OldSoulmate_Win_grp")) : nullptr;
	return W && W->IsVisible();
}

bool UAsdaCommunitySubsystem::IsWindowOpen() const
{
	// ★★★ D1: видимость по САМОМУ окну 6400, а не по вкладке 6402 (дефект оригинала не повторяем).
	UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsWindowVisible(kWindowId);
}

void UAsdaCommunitySubsystem::SwitchTab(int32 Tab)
{
	Tab = FMath::Clamp(Tab, 0, 3);
	if (!Window || !bReady) { return; }

	// ★ Гейт вкладки 2 (клан): при отсутствии клана переключение ОТМЕНЯЕТСЯ с возвратом на прошлую.
	if (Tab == 2 && SelfClanId() == -1)
	{
		UE_LOG(LogAsdaCommunity, Log, TEXT("[сообщество] вкладка «Клан» недоступна (клана нет) — остаёмся на %d"), ActiveTab);
		// ★ 1:1 `communityUI_SwitchTab` case 2: в ветке отказа эталон ВИДИМОСТЬ ПАНЕЛЕЙ НЕ ТРОГАЕТ,
		//   а только возвращает выбор полосы на ТЕКУЩУЮ вкладку (`CControl_SetSelectedIndex(combo, *(this+10))`).
		//   Прежний `ApplyTabVisibility(ActiveTab)` заново дёргал `OnTabActivated(true)` ⇒ на КАЖДЫЙ клик
		//   по недоступной вкладке переотправлялись запросы списков (найдено аудитом).
		ApplyTabBarState(ActiveTab);
		return;
	}

	PrevTab = ActiveTab;
	ActiveTab = Tab;
	ApplyTabVisibility(Tab);
	// Полоса вкладок: эталон ставит выбранный индекс самим контролом-списком;
	//   в UMG кнопка состояния не держит, поэтому зовём общую точку явно (и красим подписи).
	ApplyTabBarState(ActiveTab);
	UE_LOG(LogAsdaCommunity, Log, TEXT("[сообщество] вкладка %d → %d"), PrevTab, ActiveTab);
}

void UAsdaCommunitySubsystem::OpenOnTab(int32 Tab)
{
	Tab = FMath::Clamp(Tab, 0, 3);
	// Заранее НЕ защёлкиваем: сначала открыть (с гейтами), затем — если открылось — переключить.
	ActiveTab = Tab;
	OpenWindow();
	if (IsWindowOpen()) { SwitchTab(Tab); }
}

void UAsdaCommunitySubsystem::OnTabButtonClicked(int32 Tab)
{
	SwitchTab(Tab);
}

// ═══════════════════════════════ КОМАНДЫ ══════════════════════════════════════════════════════

void UAsdaCommunitySubsystem::OnMenuCommand(int32 ButtonIdNum, int32 /*TargetWindowId*/, bool bOpen)
{
	if (ButtonIdNum != kMenuButtonIdNum) { return; }
	if (bOpen) { if (!IsWindowOpen()) { OpenWindow(); } }
	else       { if (IsWindowOpen())  { CloseWindow(); } }
}

void UAsdaCommunitySubsystem::OnGameCommand(int32 Cmd)
{
	if (Cmd != kCmdCommunitySoul && Cmd != kCmdCommunityWho) { return; }
	const int32 Tab = (Cmd == kCmdCommunitySoul) ? 0 : 3;   // 35 → соулмейт, 36 → «Кто?»
	if (CVarHotkeyToggle.GetValueOnGameThread() == 0) { OpenOnTab(Tab); return; }   // прежнее: только открыть
	// [ОРИГИНАЛ] `communityUI_OpenOrToggleTab@0xBD3C20` (разбор — у ключа `asda.Community.HotkeyToggle`).
	//   Ветка «открыто, другая вкладка» эталона гасит общую подсказку (`this+11` → `CControl_KillFocus(&g_ItemTooltip)`) и зовёт
	//   `communityUI_SwitchTab` — у порта это `SwitchTab`, своей подсказки у окна 6400 нет. Ветка «закрыто» — `OpenOnTab`
	//   (гейты `communityUI_Open`, вкладка ставится после реального показа — закон `no-state-latch-before-shown`).
	if (IsWindowOpen())
	{
		if (Tab == ActiveTab)
		{
			UE_LOG(LogAsdaCommunity, Log, TEXT("[сообщество] клавиша cmd%d: окно открыто на вкладке %d — закрыть (communityUI_OpenOrToggleTab)"), Cmd, Tab);
			CloseWindow();
			return;
		}
		SwitchTab(Tab);
		return;
	}
	OpenOnTab(Tab);
}

// ═══════════════════════════════ УТИЛИТЫ ══════════════════════════════════════════════════════

FString UAsdaCommunitySubsystem::Notice(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaCommunitySubsystem::PostChat(const FString& Text) const
{
	if (Text.IsEmpty()) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		Msg->PostText(EAsdaMsgCategory::Social, Text);
	}
}

FString UAsdaCommunitySubsystem::DumpState() const
{
	FString S = FString::Printf(TEXT("окно=%s вкладка=%d(прошлая %d) готово=%d клан=%d"),
		IsWindowOpen() ? TEXT("открыто") : TEXT("закрыто"), ActiveTab, PrevTab, bReady ? 1 : 0, SelfClanId());
	// Подписи вкладок — живой текст и ЖИВОЙ цвет (в sRGB-hex, как в данных JUI): по нему сразу
	//   видно, применились ли цвета состояний, и не пришлось ли ловить их скриншотом.
	for (int32 i = 0; i < 4; ++i)
	{
		const UTextBlock* T = TabCaptions.IsValidIndex(i) ? TabCaptions[i] : nullptr;
		if (!T) { S += FString::Printf(TEXT("\n  вкладка %d: подписи НЕТ"), i); continue; }
		const FLinearColor C = T->GetColorAndOpacity().GetSpecifiedColor();
		S += FString::Printf(TEXT("\n  вкладка %d: «%s» цвет=%s%s"), i, *T->GetText().ToString(),
			*C.ToFColor(true).ToHex(), (i == ActiveTab) ? TEXT(" ← активная") : TEXT(""));
	}
	// ★ Проверка правок 02.09.2026 прямо из игры (жалобы «задвоение» и «окно не закрывается»).
	if (Window)
	{
		const UWidget* Close = AsdaUi::FindNodeExact(Window, TEXT("COMMUNITY_CLOSE"));
		S += FString::Printf(TEXT("\n  крестик %d: %s, реле-якорей %d"), kCloseButtonIdNum,
			Close ? TEXT("найден") : TEXT("НЕ НАЙДЕН"), Relays.Num());
		// Полоса-двойник 13631: любой её узел, оставшийся не-Collapsed, — это задвоение на экране.
		int32 nAlive = 0;
		FString FirstAlive;
		const FString Pref = FString(kHiddenTabBarIdc) + TEXT("_");
		if (Window->WidgetTree)
		{
			Window->WidgetTree->ForEachWidget([&Pref, &nAlive, &FirstAlive](UWidget* W)
			{
				if (!W || !W->GetName().StartsWith(Pref, ESearchCase::IgnoreCase)) { return; }
				if (W->GetVisibility() == ESlateVisibility::Collapsed) { return; }
				++nAlive;
				if (FirstAlive.IsEmpty()) { FirstAlive = W->GetName(); }
			});
		}
		S += FString::Printf(TEXT("\n  полоса-двойник %s: живых узлов %d%s"), kHiddenTabBarIdc,
			nAlive, nAlive ? *FString::Printf(TEXT(" (первый: %s)"), *FirstAlive) : TEXT(" — погашена"));
	}
	return S;
}
