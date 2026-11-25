// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaChatSubsystem — реализация. Полный реверс-разбор окон и правило маршрутизации — в шапке .h.
#include "UI/AsdaChatSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ D5: единый FindNodeByName (дедуп 2026-08-08)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaJuiSlot.h"   // ★ ЕДИНАЯ схема слота (POINT-анкор импортёра) — общая на весь UI
#include "UI/AsdaMarkup.h"
#include "UI/AsdaMessageSubsystem.h"   // ★★★ ЕДИНЫЙ маршрутизатор: команды asda.War.* — его обёртки
#include "Text/AsdaTextSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "HAL/PlatformTime.h"   // ★ волна В5: TTL строк 221 и буферов A/B — монотонные часы (эталон `timeGetTime`)
#include "Components/Button.h"
#include "Components/Border.h"          // прозрачная полоса-хват правой кромки (горизонт. ресайз)
#include "Components/Image.h"
#include "Components/TextWidgetTypes.h" // UTextLayoutWidget — общий предок UTextBlock и URichTextBlock
#include "Framework/Application/SlateApplication.h"   // GetCursorPos: позиция мыши БЕЗ зависимости от PlayerController   // приглушение планов рамки (SetColorAndOpacity)
#include "Blueprint/WidgetLayoutLibrary.h"   // GetViewportScale: экранные px → базовые единицы JUI
#include "GameFramework/PlayerController.h"
#include "Styling/SlateTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaChat, Log, All);

// Позиция курсора по вертикали в экранных px — из Slate (валидна всегда, в отличие от
//   APlayerController::GetMousePosition, который возвращает false при скрытом курсоре).
static float AsdaCursorY()
{
	return FSlateApplication::IsInitialized() ? (float)FSlateApplication::Get().GetCursorPos().Y : 0.f;
}

// ★ ПОДЛОЖКА ОКНА ЖУРНАЛА — регулируется без пересборки: `asda.Chat.BgAlpha <0..1>`.
//   0 = подложки не видно, 1 = сплошной чёрный. Значение применяется на следующем тике.
static ASDA_DEV_CVAR(float, CVarChatBgAlpha,
	TEXT("asda.Chat.BgAlpha"), 0.55f,
	TEXT("Непрозрачность подложки окна системного чата (0..1). По умолчанию 0.55."), ECVF_Default);

// Импортированные WBP (генератор Tools/asda_jui_generic_umg.py).
static const TCHAR* kSysWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_NEW_SYSTEM.WBP_CHAT_NEW_SYSTEM_C");
static const TCHAR* kWarWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_WARMESSAGE_WIN.WBP_WARMESSAGE_WIN_C");
// WBP_CHAT_NEW_SERV (2550) НЕ открывается: эталон гасит и само окно, и оба накопителя 2557/2558
//   (chat.c:1012/1017/1029, включения обратно нигде нет). Его планы ChatBase_Center/L/R лежат на
//   y = 754..798 базы 768 и накрывают полосу опыта EXPBar_WIn (310) @ y 758..768.

// Rect-ы контролов JUI (gamemain_jui.ini, база 1024×768) — источник анкоров (закон jui-align-anchors).
static constexpr float kJuiW = 1024.f, kJuiH = 768.f;
static constexpr float kListX = 22.f,  kListY = 516.f, kListW = 446.f, kListH = 57.f;   // 2572
static constexpr float kHomeX = 451.f, kHomeY = 519.f, kHomeW = 13.f,  kHomeH = 13.f;  // 2573
// Доли экрана, которые JUI назначил ВСЕМ контролам окна 2570 (см. compute_anchors):
//   X — POINT в 0.0 (левый край), Y — POINT в 1.0 (нижний край). Ровно это ставит импортёр.
static constexpr float kFxLeft = 0.f, kFyBottom = 1.f;

// ★★★ ЕДИНАЯ схема слота, ТА ЖЕ, что у импортёра (Tools/asda_jui_generic_umg.py::_apply_anchor).
//   JUI даёт каждому контролу ДОЛЮ ЭКРАНА (fx, fy) + пиксельный оффсет от неё:
//       не-span ось → POINT-анкор в fx/fy, offset = poz − f·база, размер в пикселях базы;
//       span ось    → анкор [poz/база .. (poz+size)/база], offsets 0.
//   Все контролы окна CHAT_NEW_SYSTEM (2570) в JUI — X: POINT fx=0.0 (лево), Y: POINT fy=1.0 (низ):
//       2572 listbox  off(L 22, T −252, W 446, H 57)
//       2573 HomeBtn  off(L 451, T −249, W 13,  H 13)
//   ★ РЕГРЕССИЯ (живой тест 2026-08-03): наши собственные панели ставились SPAN-анкором
//     (Y = 516/768 .. 573/768). На базовых 768 px обе схемы совпадают (y = 516), но при другой
//     высоте экрана расходятся: 1080 → 102 px, 1440 → 220 px. Список с полосой прокрутки
//     «отрывался» от плиты журнала и выглядел отдельным окном. Держим ОДНУ схему — POINT.
//   ★ 2026-08-05: реализация вынесена в общий заголовок UI/AsdaJuiSlot.h — той же схемой обязано
//   пользоваться КАЖДОЕ окно порта (основной чат 2500 и все следующие). Здесь оставлен тонкий
//   переходник, чтобы не трогать проверенные вызовы ниже.
static void SetJuiPointSlot(UCanvasPanelSlot* CS, float Fx, float Fy, float X, float Y, float W, float H)
{
	AsdaJui::SetPointSlot(CS, Fx, Fy, X, Y, W, H);
}

void UAsdaChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
}

// Id зоны интерфейса в общем реестре — имя окна оригинала (2570).
const FName UAsdaChatSubsystem::kZoneId(TEXT("CHAT_NEW_SYSTEM"));

void UAsdaChatSubsystem::Deinitialize()
{
	UnregisterZone();
	ResetWidgets();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaChatSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Виджеты умирают с миром; строки военного канала и системного журнала принадлежат сессии.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A п.0, близнец КРИТ К-1 окна 2500): журнал 2570 и оверлей военного канала
	//   снимаются с реестра окон. Было: сброс без смены мира (разрыв связи) забывал указатели, а окна оставались в реестре —
	//   `OpenWindowByPath` отдавал тот же виджет, и узлы кода (`ConstructWidget` с именами: лента журнала, зона растягивания
	//   `CHAT_NEW_SERV_ResizeRight`) пересоздавались поверх ЖИВЫХ объектов того же имени — тихая порча дерева. Стало: новый
	//   экземпляр, как после травела. Строки журнала и военного канала чистятся ниже, как и прежде.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindowSilently(FName(TEXT("CHAT_NEW_SYSTEM")));
		WM->CloseWindowSilently(FName(TEXT("WARMESSAGE_WIN")));
	}
	ResetWidgets();
	WarLines.Reset();
	LinesA.Reset(); LinesB.Reset();
	bResizing = false; bResizingW = false;
	ScrollHeldDir = 0; ScrollHeldTime = 0.f;
	Mouse.Reset();
	if (AsdaIsFullSessionReset(Reason))
	{
		LogHistory.Reset();   // журнал прошлого персонажа новому не показываем
	}
}

void UAsdaChatSubsystem::ResetWidgets()
{
	SystemWidget = nullptr;
	SysLog = nullptr; HomeBtn = nullptr; PlateImg = nullptr;
	ResizeSrc = nullptr; ScrollUpSrc = nullptr; ScrollDownSrc = nullptr; PressedNode = nullptr;
	ResizeRightSrc = nullptr;
	// ★ Аудит №3 (B3): `Mouse.Reset()` = прежнее `bPrevLmbDown = false` (латч в «было отпущено»).
	ScrollHeldDir = 0; ScrollHeldTime = 0.f; Mouse.Reset();
	NativeSlots.Reset(); bResizing = false; bResizingW = false; LinesWrapW = 0.f;
	bFrameShown = false; bHeaderShown = false; bPlateHidden = false;
	WarWidget = nullptr; WarBox = nullptr;
	// LinesA/LinesB/WarLines — чистые данные, виджетам не принадлежат: травел их не рвёт.
}

// ── Тик: самовосстановление окон после травела + гашение строк оверлеев по TTL ──────────────────
void UAsdaChatSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Chat");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	// Чат живёт только В МИРЕ (эталон: CChat строится при входе, g_GameState==1).
	if (!LM || LM->CurrentMapId < 0 || !WS || !IsValid(WS->LocalPlayerActor)) { return; }
	if (!EnsureWidgets()) { return; }
	EnsureWarWidget();
	TickBufferExpiry();

	// Плотность подложки правится на лету: asda.Chat.BgAlpha <0..1>.
	{
		const float A = FMath::Clamp(CVarChatBgAlpha.GetValueOnGameThread(), 0.f, 1.f);
		if (!FMath::IsNearlyEqual(A, AppliedBgAlpha, 0.001f)) { ApplyPlateBrush(A); }
	}

	// ★ Мышь по узлам управления (RESIZE / SCROLL_UP / SCROLL_DOWN) — единый хит-тест по
	//   фактической геометрии; работает с любым классом виджета (у пользователя это UImage).
	TickPointer(DeltaTime);
	// ★ Сцепка с окном чата 2500: низ журнала приклеен к его верхней кромке (см. TickDockToChat).
	TickDockToChat();
	TickWarExpiry();
}

bool UAsdaChatSubsystem::EnsureWidgets()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(SystemWidget))
	{
		return true;
	}
	ResetWidgets();

	// ★ SelfHitTestInvisible на корне: эталон прокидывает клик по 2572 в мир
	//   (game_main.c:5748 whitelist) — окно не должно съедать игровой клик.
	SystemWidget = WM->OpenWindowByPath(FName(TEXT("CHAT_NEW_SYSTEM")), kSysWbp, kSysZ,
	                                    ESlateVisibility::SelfHitTestInvisible);
	// Страховка от прежней (ошибочной) регистрации окна 2550 — его плита накрывала полосу опыта.
	// ★ 2026-08-04: WBP_CHAT_NEW_SERV перенесён в /Game/AsdaAssets/UI/Player_UI/Unused/Chat/
	//   (визуальное легаси: эталон гасит 2550/2557/2558, подтверждено живым дампом клиента —
	//   UNUSED_REPORT.md §12). Строка ниже НЕ грузит ассет: CloseWindow ищет запись в реестре
	//   УЖЕ ОТКРЫТЫХ окон (UAsdaWindowSubsystem::CloseWindow → Windows.Find), путь не трогает,
	//   поэтому перенос ссылку не рвёт. Оставлено как страховка от старых сохранённых сессий.
	//   Невидимые буферы 2557/2558 живут дальше как ДАННЫЕ (SysA/SysB ниже) — это не легаси-код.
	WM->CloseWindow(FName(TEXT("CHAT_NEW_SERV")));
	// ★★★ Колесо мыши над журналом — через ЕДИНЫЙ реестр окон (см. OnWheelJournal).
	WM->RegisterWheelHandler(kZoneId,
		FAsdaUiWheelHandler::CreateUObject(this, &UAsdaChatSubsystem::OnWheelJournal));
	if (!SystemWidget)
	{
		UE_LOG(LogAsdaChat, Error, TEXT("[чат] WBP_CHAT_NEW_SYSTEM не загрузился (%s)"), kSysWbp);
		return false;
	}
	// ★ Зона перехвата мирового клика — в ОБЩИЙ реестр (одна точка правды на весь UI).
	RegisterZone();

	// ★★★ ПОРЯДОК КРИТИЧЕН (баг живого теста 2026-08-04): декорационный проход ниже гасит «все
	//   прочие кнопки» в HitTestInvisible. Раньше указатели ResizeBtn/ScrollUpBtn/ScrollDownBtn/HomeBtn
	//   разрешались ПОСЛЕ него и в момент прохода были nullptr — под гашение попадали ВСЕ кнопки,
	//   включая пользовательские. Поэтому интерактив ищем ПЕРВЫМ.
	UCanvasPanel* Root = Cast<UCanvasPanel>(SystemWidget->WidgetTree ? SystemWidget->WidgetTree->RootWidget : nullptr);
	{
		HomeBtn        = Cast<UButton>(SystemWidget->GetWidgetFromName(TEXT("CHAT_NEW_SERV_HomeBtn")));
		bHomeBtnNative = (HomeBtn != nullptr);
		// ★ Ищем среди ВСЕХ UWidget — класс узла не важен (у пользователя это UImage).
		ResizeSrc     = FindNodeByName(TEXT("RESIZE"),      TEXT("RESIZE"));
		ScrollUpSrc   = FindNodeByName(TEXT("SCROLL_UP"),   TEXT("SCROLL_UP"));
		ScrollDownSrc = FindNodeByName(TEXT("SCROLL_DOWN"), TEXT("SCROLL_DOWN"));
		auto Descr = [](UWidget* W) -> FString
		{
			return W ? FString::Printf(TEXT("%s(%s)"), *W->GetName(), *W->GetClass()->GetName())
			         : FString(TEXT("НЕТ"));
		};
		UE_LOG(LogAsdaChat, Log, TEXT("[чат] узлы управления: HomeBtn=%s RESIZE=%s SCROLL_UP=%s SCROLL_DOWN=%s"),
			*Descr(HomeBtn), *Descr(ResizeSrc), *Descr(ScrollUpSrc), *Descr(ScrollDownSrc));
		if (!ResizeSrc || !ScrollUpSrc || !ScrollDownSrc)
		{
			TArray<UWidget*> Ws;
			if (SystemWidget->WidgetTree) { SystemWidget->WidgetTree->GetAllWidgets(Ws); }
			FString List;
			for (UWidget* W : Ws) { if (W) { List += Descr(W) + TEXT(" "); } }
			UE_LOG(LogAsdaChat, Warning, TEXT("[чат] НЕ НАЙДЕНЫ узлы управления. ВСЁ дерево окна: %s"), *List);
		}
		// ★ Visible обязателен: хит-тест идёт по кэшированной геометрии, а Visible ещё и не даёт
		//   клику по этим узлам провалиться в мир.
		// ★ Перенесено в ассет (15.09, разрешение владельца): здесь стоял NormalizeControlSlot ×3 (RESIZE,
		//   SCROLL_UP, SCROLL_DOWN — перевод растягивающего анкора в POINT). В WBP_CHAT_NEW_SYSTEM и в снимке
		//   ручных узлов `Tools/data/ui_handmade_nodes.json` анкоры уже POINT — подпорка ничего не делала.
		//   ПОКАЗАНЫ ли узлы — решает ассет (стрелки показаны шагом Tools/asda_ui_chat_markup_visibility.py);
		//   код ставит только политику хит-теста показанного узла.
		if (ResizeSrc)
		{
			if (ResizeSrc->GetVisibility() != ESlateVisibility::Collapsed) { ResizeSrc->SetVisibility(ESlateVisibility::Visible); }
			ResizeSrc->SetCursor(EMouseCursor::ResizeUpDown);
		}
		if (ScrollUpSrc && ScrollUpSrc->GetVisibility() != ESlateVisibility::Collapsed)     { ScrollUpSrc->SetVisibility(ESlateVisibility::Visible); }
		if (ScrollDownSrc && ScrollDownSrc->GetVisibility() != ESlateVisibility::Collapsed) { ScrollDownSrc->SetVisibility(ESlateVisibility::Visible); }
	}

	// ── CHAT_NEW_SYSTEM (2570): заголовок «Система» (loc 12144) в скрытом баре 2574, скрытый
	//    разделитель 2575, кнопка сворачивания 2573 — ровно как в CChat_BuildServNoticeUI.
	if (UWidgetTree* Tree = SystemWidget->WidgetTree)
	{
		TArray<UWidget*> All;
		Tree->GetAllWidgets(All);
		UAsdaTextSubsystem* Txt = GI->GetSubsystem<UAsdaTextSubsystem>();
		const FString Header = Txt ? Txt->GetTextById(12144) : FString();
		for (UWidget* W : All)
		{
			const FString N = W->GetName();
			// ★ Перенесено в ассет (15.09, разрешение владельца): ПОКАЗАН ли узел журнала — решает WBP
			//   (шаг Tools/asda_ui_chat_markup_visibility.py). Свёрнутое в ассете остаётся свёрнутым; ниже —
			//   только текст/шрифт/тинт/кисть и политика хит-теста показанных узлов.
			if (W->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
			// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА — РЕШЕНИЕ ПОЛЬЗОВАТЕЛЯ от 2026-08-03. НЕ «чинить» обратно!
			//   В оригинале заголовок 2574 (JUI Visible=0, кода показа нет) и рамка 2575
			//   (`CControl_SetVisible(servSysLine0, 0)`, chat.c:1007) СКРЫТЫ, из-за чего окно выглядит
			//   голой полупрозрачной плитой. Пользователь потребовал показывать и рамку, и заголовок.
			if (N.StartsWith(TEXT("CHAT_NEW_SERV_SYSbottn")))
			{
				if (UTextBlock* Tb = Cast<UTextBlock>(W))
				{
					Tb->SetAutoWrapText(false);                      // закон ui-autowrap-canvas-gotcha
					if (!Header.IsEmpty()) { Tb->SetText(FText::FromString(Header)); }
					FSlateFontInfo F = Tb->GetFont();                // кегль оригинала: FONT_PX = 8
					F.Size = (int32)kFontPx;
					Tb->SetFont(F);
					Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);   // подпись не ловит мышь
				}
				else
				{
					W->SetVisibility(ESlateVisibility::HitTestInvisible);        // планы бара-заголовка
				}
				bHeaderShown = true;
			}
			else if (N.StartsWith(TEXT("CHAT_NEW_SERV_line0")))
			{
				// ★ РАМКА. Скин Box_White2Stroke_round = 9 планов: 8 кромок Diffuse C8FFFFFF
				//   (альфа 200 = 78 % белого — ярко) и ЦЕНТР (Rect 2 2 4 4) Diffuse FFFFFFFF —
				//   сплошная белая заливка на белом пикселе атласа. Центр нам не нужен (заливку даёт
				//   список), а кромки живой тест назвал слишком白/яркими.
				//   ★ ОТСТУПЛЕНИЕ (решение юзера 2026-08-03): центр гасим, кромки приглушаем
				//   до тёплого серого игрового UI (DCDCC8 — Diffuse кнопки MinimizeBtn_WinSm)
				//   с альфой ~0.35 вместо 0.78.
				// ★ Перенесено в ассет (15.09, разрешение владельца): центр-заливку гасил код по размеру слота,
				//   кромки показывал код. В WBP центрального плана нет (импортёр кладёт 8 кромок line0_4_0…7),
				//   кромки показаны в самом ассете; здесь остаются тинт (оформление) и хит-тест.
				if (UImage* Im = Cast<UImage>(W))
				{
					Im->SetColorAndOpacity(FLinearColor(0xDC / 255.f, 0xDC / 255.f, 0xC8 / 255.f, 0.45f));
				}
				W->SetVisibility(ESlateVisibility::HitTestInvisible);
				bFrameShown = true;
			}
			// ★ ПОДЛОЖКА ОКНА. Плита CHAT_NEW_SERV_back — скин Box_Black_Ot10: ОДИН план,
			//   Diffuse 19FFFFFF (альфа 25 ≈ 10 %), UV-свотч (0.879,0.039)-(0.898,0.059) атласа
			//   N_01.tga — сам свотч вертикально-градиентный, отсюда «сверху темнее, снизу светлее».
			//   Поверх него ещё и заливка списка (скин Inven_List: один план 32FFFFFF, альфа 50 ≈ 20 %,
			//   UV белого пикселя (0.996,0)-(1.0,0.004) — РОВНАЯ). Два слоя суммировались.
			//   ★ ОТСТУПЛЕНИЯ (решения юзера): 2026-08-03 «убрать чёрный градиент» + «сделать задник
			//   темнее» → у РОДНОЙ плиты подменяем кисть на СПЛОШНУЮ заливку (RoundedBox, без текстуры),
			//   поэтому градиента нет, а плотность задаётся CVar asda.Chat.BgAlpha.
			//   ★ 2026-08-04 «клик по окну не должен проходить в мир» → плита ПЕРЕХВАТЫВАЕТ мышь
			//   (Visible). Это ПРОТИВОРЕЧИТ эталону: game_main.c:5748 кладёт 2572 в whitelist
			//   прокидывания клика в мир (g_WorldInputFlags |= 1). Так решил пользователь — НЕ «чинить».
			else if (N.StartsWith(TEXT("CHAT_NEW_SERV_back")))
			{
				if (UImage* Im = Cast<UImage>(W))
				{
					PlateImg = Im;
					ApplyPlateBrush(CVarChatBgAlpha.GetValueOnGameThread());
					bPlateHidden = true;   // «градиент убран» (плита осталась, но ровная)
				}
				W->SetVisibility(ESlateVisibility::Visible);   // ★ перехват клика
			}
			else if (UTextBlock* Tb = Cast<UTextBlock>(W))
			{
				Tb->SetAutoWrapText(false);
				FSlateFontInfo F = Tb->GetFont();
				F.Size = (int32)kFontPx;
				Tb->SetFont(F);
				Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
			// ★ ВСЯ ОСТАЛЬНАЯ ДЕКОРАЦИЯ — CLICK-THROUGH. Импортёр отдаёт планы как UImage с
			//   Visibility=Visible, а Visible-картинка в Slate ПЕРЕХВАТЫВАЕТ мышь. Планы окна лежат
			//   на кромке (back 509..579, line0 508..578, TextMove 508..540) и накрывают хват
			//   растягивания (507..515). Эталон вообще прокидывает клик по этому окну в мир
			//   (whitelist game_main.c:5748), так что мышь должны получать ТОЛЬКО хват, кнопка 2573
			//   и область списка (для колеса).
			else if (W == ResizeSrc || W == ScrollUpSrc || W == ScrollDownSrc)
			{
				W->SetVisibility(ESlateVisibility::Visible);   // узлы управления мышь ЛОВЯТ
			}
			else if (Cast<UImage>(W) != nullptr)
			{
				W->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else if (W != HomeBtn && Cast<UButton>(W) != nullptr)
			{
				// Чужие кнопки импортёра (напр. OPT-планы заголовка) мышь не ловят.
				W->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}
	}

	SysLog = MakeScrollList(SystemWidget, TEXT("CHAT_NEW_SYSTEM_Lines"), kListX, kListY, kListW, kListH);

	// Кнопка 2573: если импортёр отдал её планом-картинкой, кладём прозрачное реле поверх rect.
	if (!HomeBtn && Root)
	{
		HomeBtn = NewObject<UButton>(SystemWidget, UButton::StaticClass(), TEXT("CHAT_NEW_SERV_HomeBtn_OPT"));
		FButtonStyle S;
		S.Normal = FSlateNoResource(); S.Hovered = FSlateNoResource(); S.Pressed = FSlateNoResource();
		S.NormalPadding = FMargin(0); S.PressedPadding = FMargin(0);
		HomeBtn->SetStyle(S);
		if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(HomeBtn))
		{
			SetJuiPointSlot(CS, kFxLeft, kFyBottom, kHomeX, kHomeY, kHomeW, kHomeH);
		}
	}
	if (HomeBtn)
	{
		HomeBtn->OnClicked.AddDynamic(this, &UAsdaChatSubsystem::OnHomeBtnClicked);
		HomeBtn->SetVisibility(ESlateVisibility::Visible);
		// ★★★ ПРИЧИНА «кнопка ничего не делает» — РАЗОБРАНА ПО АССЕТУ 2026-08-04.
		//   CHAT_NEW_SERV_HomeBtn лежит НЕ на корневом канвасе, а ВНУТРИ группы
		//   CHAT_NEW_SERV_listbox_grp (импортёр делает группу для узла с детьми). Поэтому прежний
		//   `SetZOrder(110)` менял порядок ВНУТРИ ГРУППЫ и никак не спорил с панелью строк, которая
		//   лежит на КОРНЕВОМ канвасе с z=5: группа (z=0) рисуется под ней целиком вместе с кнопкой.
		//   Решение: панель строк кладём В ТУ ЖЕ группу (см. MakeScrollList), а кнопке даём z выше.
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(HomeBtn)) { CS->SetZOrder(50); }
	}

	// ★ Мышь по узлам RESIZE / SCROLL_UP / SCROLL_DOWN ловится в TickPointer() хит-тестом
	//   по фактической геометрии — привязывать делегаты не к чему (это UImage, не кнопки).

	// ★ Хват ГОРИЗОНТАЛИ — своя полоса по правой кромке (в JUI такого контрола нет).
	//   Создаём ДО CaptureNativeSlots, но из общей классификации исключаем: её геометрию задаём явно.
	EnsureResizeRight();

	// Снять исходные слоты РОДНЫХ контролов WBP и применить сохранённую геометрию (переживает травел).
	CaptureNativeSlots();
	// ★★★ РАЗМЕР ЖУРНАЛА 2570 ИЗ ini ИГРЫ (решение юзера 2026-08-07 «все окна … сохраняют свои
	//   позиции и оттуда каждый раз достают»). ПОЗИЦИЯ у журнала не сохраняется — он не таскается
	//   (прямое требование юзера, `GDragDisabledWindows`) и приклеен низом к чату 2500; сохраняется
	//   именно РАЗМЕР по обеим осям. Читаем ДО первой раскладки, чтобы окно не прыгало в первом кадре.
	//   ★ Отступление от эталона: `ChatOpt.bin` размеров не хранит. «К эталону» не возвращать.
	if (WM)   // реестр окон получен выше по функции — им же читаем сохранённый размер
	{
		FVector2D Sz;
		if (WM->GetSavedWindowSize(FName(TEXT("CHAT_NEW_SYSTEM")), Sz, nullptr))
		{
			WinW = FMath::Clamp((float)Sz.X, kWinMinW, kWinMaxW);
			WinH = FMath::Clamp((float)Sz.Y, kWinMinH, kWinMaxH);
			UE_LOG(LogAsdaChat, Log, TEXT("[чат] размер журнала восстановлен из ini: %.0fx%.0f"), WinW, WinH);
		}
	}
	ApplyWindowGeometry(WinH, WinW);
	ApplyLinesWidth();

	UE_LOG(LogAsdaChat, Log,
		TEXT("[чат] окно создано: CHAT_NEW_SYSTEM(z=%d, журнал %d строк, высота %.0f [%.0f..%.0f], ширина %.0f [%.0f..%.0f]). Буферы A/B — невидимы (эталон)."),
		kSysZ, LogHistory.Num(), WinH, kWinMinH, kWinMaxH, WinW, kWinMinW, kWinMaxW);
	DumpWidgets();   // доказательная раскладка при каждом создании (full-structural-analysis)
	return true;
}

// Список журнала: UScrollBox на rect контрола 2572 (прокрутка = uiListSetScrollOffset + скроллбар).
UScrollBox* UAsdaChatSubsystem::MakeScrollList(UUserWidget* Owner, const TCHAR* Name, float X, float Y, float W, float H)
{
	if (!Owner || !Owner->WidgetTree) { return nullptr; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(Owner->WidgetTree->RootWidget);
	if (!Root) { return nullptr; }
	// ★ Панель строк кладём ВНУТРЬ родной группы списка CHAT_NEW_SERV_listbox_grp, если она есть:
	//   тогда она автоматически едет/растягивается вместе с группой, а кнопка 2573 (тоже в группе)
	//   может встать НАД ней по z. Иначе — на корневой канвас по POINT-схеме JUI.
	UCanvasPanel* Host = Cast<UCanvasPanel>(Owner->GetWidgetFromName(TEXT("CHAT_NEW_SERV_listbox_grp")));
	UScrollBox* Box = Owner->WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), FName(Name));
	if (!Box) { return nullptr; }
	Box->SetOrientation(Orient_Vertical);
	// ★★★ КОЛЕСО РАЗБИРАЕТ ЕДИНЫЙ ДИСПЕТЧЕР (2026-08-06, см. `OnWheelJournal`), а не Slate:
	//   иначе съеденное Slate событие не доходит до аксиса контроллера и остальные списки чата,
	//   ловящие мышь опросом геометрии, колеса не видят вовсе.
	Box->SetConsumeMouseWheel(EConsumeMouseWheel::Never);
	// Клик по списку прокидывается в мир (эталон whitelist 2572) — колесо/скролл при этом работают.
	Box->SetVisibility(ESlateVisibility::Visible);
	// ★ СКРОЛЛБАРА НЕТ ВООБЩЕ. У окна 2570 в JUI нет ни ползунка, ни кнопок листания: его дети —
	//   только плита, TextMove, заголовок, разделитель, список и кнопка сворачивания. Для сравнения,
	//   у основного чата ползунок и кнопки заведены ЯВНО отдельными контролами
	//   (CHAT_NEW_scr 2536 skin Nomal_Scr_Chat_hori; CHAT_U_BTN 263 / CHAT_D_BTN 264 в CHAT_BACKWIN).
	//   ⇒ листание системного журнала — КОЛЕСОМ мыши над списком + авто-скролл к последней строке.
	Box->SetAlwaysShowScrollbar(false);
	Box->SetScrollBarVisibility(ESlateVisibility::Collapsed);   // UE 5.8: SetScrollBarVisibility (заглавная B)
	if (Host)
	{
		if (UCanvasPanelSlot* CS = Host->AddChildToCanvas(Box))
		{
			CS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));   // ЛОКАЛЬНАЯ заливка группы (не экранный SPAN)
			CS->SetOffsets(FMargin(0, 0, 0, 0));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			CS->SetZOrder(0);                                // кнопка 2573 в той же группе идёт выше (z=50)
		}
		UE_LOG(LogAsdaChat, Log, TEXT("[чат] панель строк вложена в родную группу CHAT_NEW_SERV_listbox_grp"));
	}
	else if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(Box))
	{
		SetJuiPointSlot(CS, kFxLeft, kFyBottom, X, Y, W, H);   // ТА ЖЕ схема, что у планов импортёра
		CS->SetZOrder(5);
	}
	return Box;
}

// Добавить строку в панель: TextBlock → ApplyFormatted (inline-теги + базовый цвет), перелив лимита
//   удаляет САМУЮ СТАРУЮ строку (эталон uiChatLogAddLine при m_lineList > m_maxLines).
void UAsdaChatSubsystem::PushLine(UPanelWidget* Panel, const FString& RawText, FColor Color, int32 MaxLines)
{
	if (!Panel) { return; }
	UUserWidget* Owner = Panel->GetTypedOuter<UUserWidget>();
	if (!Owner || !Owner->WidgetTree) { return; }

	UTextBlock* Tb = Owner->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	if (!Tb) { return; }
	Tb->SetAutoWrapText(false);
	Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Panel->AddChild(Tb);

	// ★ БАЗОВЫЙ ФОРМАТ СТРОКИ ЖУРНАЛА = кегль + начертание + цвет.
	//   • Кегль: в JUI размер шрифта per-control НЕ хранится — он глобальный, портовый импортёр
	//     (Tools/asda_jui_generic_umg.py) ставит всему UI FONT_PX = 8. Базовый стиль
	//     DT_AsdaRichStyle — 14 px, поэтому строки журнала явно приводим к 8 тегом <FS08>.
	//   • Начертание: эталон вешает на список ФЛАГ 0x400000
	//     (`sub_937235(chatSystemListBox, 0x400000)`, chat.c:1002; те же 0x400000 у sysMsgA/B).
	//     Расшифровка флагов доказана по font_text.c:
	//         <FB> → 0x200000 — ЖИРНЫЙ  (двойная отрисовка глифа в penX+1 и penX−1, строки 6832-6886)
	//         <FD> → 0x400000 — ТЕНЬ    (сдвиг +1/+1 цветом shadowColor, строки 6887+)
	//         <FO> → 0x800000 — ОБВОДКА (dword_800000, парсер font_text.c:7978-7983)
	//     ⇒ у системного журнала в оригинале именно ТЕНЬ 0x400000 (<FD>), а не 0x800000.
	//     Цвет тени/обводки в label-пути — константа 0xFF000000 (чёрный), поле 613009.
	//     Если понадобится настоящая 4-сторонняя обводка — меняется ровно этот тег на <FO>.
	//   • Цвет: обёртка <FC=FFRRGGBB> (базовый цвет строки = эталонный SetField923).
	//   `</F>` сбрасывает ВСЕ три атрибута сразу, поэтому после каждого сброса реставрируем префикс.
	const FString Base = FString::Printf(TEXT("<FS%02d><FD><FC=FF%02X%02X%02X>"),
		(int32)kFontPx, Color.R, Color.G, Color.B);
	FString Raw = RawText;
	Raw.ReplaceInline(TEXT("</F>"), *(FString(TEXT("</F>")) + Base), ESearchCase::CaseSensitive);
	// ★ ApplyFormatted ПОДМЕНЯЕТ UTextBlock на URichTextBlock (старый узел уходит из дерева), поэтому
	//   перенос ставим ВОЗВРАЩЁННОМУ виджету, а не исходному Tb — иначе правка уходит в пустоту.
	UWidget* Row = UAsdaRichText::ApplyFormatted(Tb, Base + Raw);
	// Новая строка сразу режется по ТЕКУЩЕЙ ширине ленты (окно могли уже растянуть вправо).
	if (LinesWrapW > 0.f)
	{
		if (UTextLayoutWidget* Tw = Cast<UTextLayoutWidget>(Row ? Row : (UWidget*)Tb))
		{
			Tw->SetAutoWrapText(true);
			Tw->SetWrapTextAt(LinesWrapW);
		}
	}

	while (Panel->GetChildrenCount() > MaxLines) { Panel->RemoveChildAt(0); }
}

// Запись в НЕВИДИМЫЙ буфер-накопитель (эталонные sysMsgAWin/sysMsgBWin: SetVisible(0), лимит 5,
//   строка с TTL). Виджетов нет — только данные; переполнение вытесняет САМУЮ СТАРУЮ строку.
void UAsdaChatSubsystem::PushBufferLine(EAsdaSysTrack Track, const FString& RawText, FColor Color, float LifeSec)
{
	TArray<FSysBufferLine>& Lines = (Track == EAsdaSysTrack::B) ? LinesB : LinesA;
	const double Now = FPlatformTime::Seconds();   // ★ волна В5: см. `TickWarExpiry`
	Lines.Add({ RawText, Color, Now, LifeSec });
	while (Lines.Num() > kBufferMax) { Lines.RemoveAt(0); }
}

// Гашение строк буферов: TTL из uiChatLogAddLine (timeGetTime()+a5) — строка исчезает сама.
void UAsdaChatSubsystem::TickBufferExpiry()
{
	const double Now = FPlatformTime::Seconds();   // ★ волна В5: см. `TickWarExpiry`
	auto Sweep = [&](TArray<FSysBufferLine>& Lines)
	{
		for (int32 i = Lines.Num() - 1; i >= 0; --i)
		{
			if (Now - Lines[i].Born >= (double)Lines[i].LifeSec) { Lines.RemoveAt(i); }
		}
	};
	Sweep(LinesA);
	Sweep(LinesB);
}

// ═══════════════════════════ WARMESSAGE_WIN (221) — верхний 3-строчный список ═══════════════════
// Эталон: окно Action_List без фона (план PlaneAlwaysInvisible=1), 464×45, центр X, y=192.5 базы
//   768, всегда видимо, AlwaysTop, NoSelect; строки центрированы (флаг row+2804|=1), тень 0x400000.
bool UAsdaChatSubsystem::EnsureWarWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(WarWidget))
	{
		return true;
	}
	WarWidget = nullptr; WarBox = nullptr;
	// ★ Волна В5 (ревизия В4 МЕЛК-14): запись реестра окон могла остаться от прежнего мира (виджет мёртв) — тогда
	//   `OpenWindowByPath` вернул бы её, а не новое окно. Снимаем её явно.
	if (UUserWidget* Stale = WM->FindWindow(FName(TEXT("WARMESSAGE_WIN"))))
	{
		if (!WM->IsWindowLive(Stale)) { WM->CloseWindow(FName(TEXT("WARMESSAGE_WIN"))); }
	}

	// NoSelect=1 + отсутствие интерактива: окно полностью прозрачно для мыши.
	WarWidget = WM->OpenWindowByPath(FName(TEXT("WARMESSAGE_WIN")), kWarWbp, kWarZ,
	                                 ESlateVisibility::HitTestInvisible);
	if (!WarWidget)
	{
		UE_LOG(LogAsdaChat, Error, TEXT("[warmsg] WBP_WARMESSAGE_WIN не загрузился (%s)"), kWarWbp);
		return false;
	}

	UCanvasPanel* Root = Cast<UCanvasPanel>(WarWidget->WidgetTree ? WarWidget->WidgetTree->RootWidget : nullptr);
	if (!Root)
	{
		UE_LOG(LogAsdaChat, Error, TEXT("[warmsg] корень WBP_WARMESSAGE_WIN не CanvasPanel"));
		WarWidget = nullptr;
		return false;
	}
	// ★ Волна В5 (ревизия В4 МЕЛК-14, «строки наслаиваются и висят дольше 10 с»): сброс сессии (`ResetWidgets`) снимает
	//   только указатели, а живое окно того же мира реестр возвращает прежним. Раньше в него добавлялся ВТОРОЙ список
	//   строк, первый оставался сиротой — его строки никто не перестраивал и не гасил (наложение на кадре
	//   `rv4_c_make_series`). Теперь список ОДИН: существующий берётся заново и перестраивается из данных.
	if (UVerticalBox* Existing = Cast<UVerticalBox>(WarWidget->WidgetTree->FindWidget(FName(TEXT("WARMESSAGE_Lines")))))
	{
		WarBox = Existing;
		WarBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		RebuildWarLines();
		UE_LOG(LogAsdaChat, Log, TEXT("[warmsg] окно подхвачено заново (список строк тот же), строк %d"), WarLines.Num());
		return true;
	}
	WarBox = WarWidget->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("WARMESSAGE_Lines"));
	if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(WarBox))
	{
		// ★ POINT-анкор «центр X, верх окна на 0.2507 высоты» = JUI Align 16/32 (режим центрирования).
		//   SPAN-анкор здесь дал бы растяжение на ультрашироком (грабли EXP-бара и полосы чата).
		CS->SetAnchors(FAnchors(0.5f, kWarAnchorY, 0.5f, kWarAnchorY));
		CS->SetAlignment(FVector2D(0.5f, 0.f));           // центр по X, рост списка ВНИЗ от y окна
		CS->SetAutoSize(true);                            // высота = фактические строки
		CS->SetPosition(FVector2D(0.f, 0.f));
		CS->SetZOrder(10);
	}
	WarBox->SetVisibility(ESlateVisibility::HitTestInvisible);

	RebuildWarLines();                                    // данные пережили травел — восстановить
	UE_LOG(LogAsdaChat, Log, TEXT("[warmsg] окно создано: WARMESSAGE_WIN(z=%d), строк %d"),
		kWarZ, WarLines.Num());
	return true;
}

void UAsdaChatSubsystem::DebugWarWidgetCounts(int32& OutBoxes, int32& OutRows) const
{
	OutBoxes = OutRows = 0;
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const UUserWidget* Win = WM ? WM->FindWindow(FName(TEXT("WARMESSAGE_WIN"))) : nullptr;
	if (!Win || !Win->WidgetTree) { return; }
	TArray<UWidget*> All;
	Win->WidgetTree->GetAllWidgets(All);
	for (const UWidget* W : All)
	{
		if (const UVerticalBox* B = Cast<UVerticalBox>(W)) { ++OutBoxes; OutRows += B->GetChildrenCount(); }
	}
}

// Перестроить виджеты-строки: каждая строка = rich-текст (inline-теги + базовый цвет), центр, тень.
void UAsdaChatSubsystem::RebuildWarLines()
{
	if (!WarBox || !WarWidget || !WarWidget->WidgetTree) { return; }
	WarBox->ClearChildren();
	for (const FWarLine& L : WarLines)
	{
		UTextBlock* Tb = WarWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		if (!Tb) { continue; }
		Tb->SetAutoWrapText(false);                       // эталон НЕ токенизирует warmsg-строку
		Tb->SetVisibility(ESlateVisibility::HitTestInvisible);
		WarBox->AddChildToVerticalBox(Tb);
		// ★ Базовый тег = цвет + СТИЛЬ СТРОКИ (`<FO>` обводка / `<FB>` жирный, если задан
		//   вызывающим). Та же композиция, что в `UAsdaHudSubsystem::ApplyNoticeText`: стиль
		//   идёт СРАЗУ ЗА цветом и повторяется после каждого `</F>`, иначе вложенная раскраска
		//   (например имя предмета по грейду) сбрасывала бы обводку у хвоста строки.
		const FString Base = FString::Printf(TEXT("<FC=FF%02X%02X%02X>%s"),
			L.Color.R, L.Color.G, L.Color.B, *L.Style);
		FString Raw = L.Raw;
		Raw.ReplaceInline(TEXT("</F>"), *(FString(TEXT("</F>")) + Base), ESearchCase::CaseSensitive);
		// ApplyFormatted для не-canvas родителя ПЕРЕСАЖИВАЕТ виджет (RemoveChild+AddChild в конец) —
		//   порядок строк сохраняется (строим последовательно), но слот новый: правим его ПОСЛЕ.
		UWidget* Applied = UAsdaRichText::ApplyFormatted(Tb, Base + Raw);
		if (URichTextBlock* Rich = Cast<URichTextBlock>(Applied))
		{
			Rich->SetAutoWrapText(false);                 // ApplyFormatted включает wrap — снять
			Rich->SetWrapTextAt(0.f);
			Rich->SetDefaultShadowOffset(FVector2D(1.0, 1.0));       // флаг 0x400000 = тень
			Rich->SetDefaultShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.5f));
			Rich->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		if (Applied)
		{
			if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Applied->Slot))
			{
				VS->SetHorizontalAlignment(HAlign_Center);    // флаг строки |=1 = центрирование
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));  // uiListSetScrollOffset(2)
			}
		}
	}
}

// Гашение строк по их TTL (эталон: метка timeGetTime()+ttl в полях +863/+864 каждой строки).
void UAsdaChatSubsystem::TickWarExpiry()
{
	if (WarLines.Num() == 0) { return; }
	// ★ Волна В5 (ревизия В4 МЕЛК-14): эталон ставит метку `timeGetTime() + ttl` — это МОНОТОННЫЕ часы процесса. Порт брал
	//   `World->GetTimeSeconds()`, а у нового мира они начинаются с нуля: строка, рождённая на 30-й секунде прежней карты,
	//   жила после смены карты ~40 с вместо 10. Теперь рождение и «сейчас» — `FPlatformTime::Seconds()`.
	const double Now = FPlatformTime::Seconds();
	bool bChanged = false;
	for (int32 i = WarLines.Num() - 1; i >= 0; --i)
	{
		if (Now - WarLines[i].Born >= (double)WarLines[i].LifeSec)
		{
			WarLines.RemoveAt(i);
			bChanged = true;
		}
	}
	if (bChanged) { RebuildWarLines(); }
}

// ═══════════════════════════ ПУБЛИЧНЫЙ API = CChat_AddTokenizedLines ═══════════════════════════
void UAsdaChatSubsystem::AddSystemMessage(const FString& RawText, FColor Color, EAsdaSysTrack Track, float LifeMs)
{
	if (RawText.IsEmpty()) { return; }

	// Журнал 2572: TTL 0 = строка вечная, лимит 500 (история переживает травел).
	LogHistory.Add({ RawText, Color });
	while (LogHistory.Num() > kSysLogMax) { LogHistory.RemoveAt(0); }

	if (SysLog)
	{
		// Автоскролл вниз только если пользователь СТОЯЛ внизу (эталон scrollAtBottomFlag).
		const bool bAtBottom = (SysLog->GetScrollOffset() >= SysLog->GetScrollOffsetOfEnd() - 1.f);
		PushLine(SysLog, RawText, Color, kSysLogMax);
		if (bAtBottom) { SysLog->ScrollToEnd(); }
	}

	// Невидимый буфер A/B: копия строки на 10 000 мс (для A — переданное время жизни).
	PushBufferLine(Track, RawText, Color, (Track == EAsdaSysTrack::B) ? kBufferLife : FMath::Max(0.f, LifeMs) / 1000.f);

	UE_LOG(LogAsdaChat, Log, TEXT("[сисчат %s] %s"),
		(Track == EAsdaSysTrack::B) ? TEXT("B") : TEXT("A"), *RawText);
}

void UAsdaChatSubsystem::AddSystemMessageById(int32 LocId, FColor Color, EAsdaSysTrack Track, const FString& Fallback)
{
	UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	FString S = Txt ? Txt->GetTextById(LocId) : FString();
	if (S.IsEmpty()) { S = Fallback; }
	if (S.IsEmpty())
	{
		UE_LOG(LogAsdaChat, Warning, TEXT("[сисчат] loc %d отсутствует в DT_GameText — строка пропущена"), LocId);
		return;
	}
	AddSystemMessage(S, Color, Track);
}

// ★ = эталонный CControl_SetTooltipText@0xBA9A30 (мисномер; фактически CChat::AddWarMessage):
//   строка в верхний 3-строчный список 221 БЕЗ переноса, переполнение вытесняет самую старую.
//   bAlsoSystemChat воспроизводит парный вызов эталона (104/316 сайтов: значимые события).
void UAsdaChatSubsystem::AddWarMessage(const FString& RawText, FColor Color, float LifeMs, bool bAlsoSystemChat,
                                       const FString& StyleTag)
{
	if (RawText.IsEmpty()) { return; }

	const double Now = FPlatformTime::Seconds();   // ★ волна В5: монотонные часы (см. `TickWarExpiry`)
	WarLines.Add({ RawText, Color, Now, FMath::Max(0.f, LifeMs) / 1000.f, StyleTag });
	while (WarLines.Num() > kWarMax) { WarLines.RemoveAt(0); }   // лимит 3: вытеснение старшей

	EnsureWarWidget();
	RebuildWarLines();

	UE_LOG(LogAsdaChat, Log, TEXT("[warmsg] строка %d: '%s' цвет=#%02X%02X%02X TTL=%.0fмс%s"),
		WarLines.Num(), *RawText, Color.R, Color.G, Color.B, LifeMs,
		bAlsoSystemChat ? TEXT(" (+сисчат)") : TEXT(""));

	if (bAlsoSystemChat)
	{
		// Вторая половина эталонной пары: CChat_AddTokenizedLines тем же цветом, дорожка A.
		AddSystemMessage(RawText, Color, EAsdaSysTrack::A, LifeMs);
	}
}

void UAsdaChatSubsystem::AddWarMessageById(int32 LocId, FColor Color, float LifeMs, bool bAlsoSystemChat,
                                           const FString& Fallback, const FString& StyleTag)
{
	UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	FString S = Txt ? Txt->GetTextById(LocId) : FString();
	if (S.IsEmpty()) { S = Fallback; }
	if (S.IsEmpty())
	{
		UE_LOG(LogAsdaChat, Warning, TEXT("[warmsg] loc %d отсутствует в DT_GameText — строка пропущена"), LocId);
		return;
	}
	AddWarMessage(S, Color, LifeMs, bAlsoSystemChat, StyleTag);
}

// ── ★ РАСТЯГИВАНИЕ ОКНА ВВЕРХ ─────────────────────────────────────────────────────────────────
// Снимок исходных слотов РОДНЫХ контролов WBP + классификация ролей. Тест «перекрывает ли контрол
//   окно по высоте» — детерминированный, по фактическим числам JUI. Окно в T-координатах (оффсет от
//   НИЖНЕЙ кромки экрана, отрицательный вверх): T = −260, H = 70, т.е. полоса −260..−190.
//     плита  CHAT_NEW_SERV_back    −259..−189  перекрывает → РАСТЯГИВАЕТСЯ
//     рамка  CHAT_NEW_SERV_line0   −260..−190  перекрывает → РАСТЯГИВАЕТСЯ
//     список CHAT_NEW_SERV_listbox −252..−195  перекрывает → РАСТЯГИВАЕТСЯ
//     TextMove 2576 −260..−228, SYSbottn 2574 −254..−229, HomeBtn 2573 −249..−236 → прижаты к ВЕРХУ
//   Та же математика сама раскладывает 9-slice планы списка: верхний ряд едет вверх, средний
//   растягивается, нижний остаётся на месте (его верх далеко от верхней кромки окна).
void UAsdaChatSubsystem::CaptureNativeSlots()
{
	NativeSlots.Reset();
	if (!SystemWidget || !SystemWidget->WidgetTree) { return; }
	const float WinTopT = kWinBottom - kWinMinH - kJuiH;   // 508 − 768 = −260
	const float WinBotT = kWinBottom - kJuiH;              // 578 − 768 = −190
	UCanvasPanel* Root = Cast<UCanvasPanel>(SystemWidget->WidgetTree->RootWidget);

	TArray<UWidget*> All;
	SystemWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		UCanvasPanelSlot* CS = AsdaUi::SlotOf(W);
		if (!CS) { continue; }
		// ★ Наша полоса-кромка справа в классификацию НЕ идёт: в JUI её нет, эвристикам отдавать
		//   нечего — её прямоугольник считается напрямую из текущей геометрии окна.
		if (W == ResizeRightSrc) { continue; }
		// ★ ВЕРТИКАЛЬ — ТОЛЬКО прямые дети КОРНЕВОГО канваса. У виджетов внутри групп (напр.
		//   CHAT_NEW_SERV_HomeBtn в CHAT_NEW_SERV_listbox_grp) координаты ЛОКАЛЬНЫЕ: по вертикали
		//   они едут вместе со своей группой, и считать их роли по экранным числам нельзя (прежняя
		//   версия так и делала и получала мусор).
		//   ★ ГОРИЗОНТАЛЬ (2026-08-05) — вложенные узлы БЕРЁМ ТОЖЕ, но роль считаем относительно
		//   их РОДИТЕЛЯ: группа списка растёт вширь, а кнопка 2573 внутри неё прижата к правому краю
		//   (JUI AlignXPos «2 −13 0» = «правый край родителя + a») ⇒ её локальный X обязан ехать.
		const bool bRoot = (W->GetParent() == Root);
		const FMargin O = CS->GetOffsets();                // POINT-анкор: (X, Y, Ширина, Высота)
		const float Top = O.Top, Bot = O.Top + O.Bottom;
		// ★★★ БЕЛЫЙ СПИСОК ПО ИМЕНИ (баг живого теста 2026-08-04: «SCROLL_DOWN растянулась вниз
		//   сильно»). Эвристика «перекрывает окно по высоте» ошибочно записывала высокую кнопку
		//   пользователя в «растягивается» и тянула её на всю высоту окна. Интерактив НИКОГДА не
		//   меняет размер — только СДВИГАЕТСЯ вместе с верхней кромкой, как заголовок.
		const bool bInteractive =
			(W == ResizeSrc || W == ScrollUpSrc || W == ScrollDownSrc || W == HomeBtn)
			|| W->GetName().Contains(TEXT("RESIZE"), ESearchCase::IgnoreCase)
			|| W->GetName().Contains(TEXT("SCROLL_UP"), ESearchCase::IgnoreCase)
			|| W->GetName().Contains(TEXT("SCROLL_DOWN"), ESearchCase::IgnoreCase)
			|| W->GetName().Contains(TEXT("HomeBtn"), ESearchCase::IgnoreCase);
		FNativeSlot N;
		N.W = W; N.Orig = O; N.bRoot = bRoot;
		N.bStretch = bRoot && !bInteractive && (Top <= WinTopT + 15.f) && (Bot >= WinBotT - 10.f);
		N.bTop     = bRoot && !N.bStretch && (Top <= WinTopT + 15.f);

		// ── ГОРИЗОНТАЛЬНЫЕ РОЛИ ──────────────────────────────────────────────────────────────────
		// Ширина РОДИТЕЛЯ в базовых px: для детей корневого канваса это ширина окна (474 = kLine0W),
		//   для детей группы — ширина слота самой группы (её оффсеты ещё авторские: захват идёт до
		//   первой перекладки). Порог 0.6 — тот же приём, что уже применён рядом для центрального
		//   плана рамки (bCenter, см. EnsureWidgets).
		float ParentW = kLine0W;
		if (!bRoot)
		{
			UCanvasPanelSlot* PS = AsdaUi::SlotOf(W->GetParent());
			// Только если у САМОЙ группы точечный анкор по X: у растягивающего Right — это отступ
			//   от правого края родителя, а не ширина, и порог получился бы бессмысленным.
			if (PS && FMath::IsNearlyEqual(PS->GetAnchors().Minimum.X, PS->GetAnchors().Maximum.X, 0.0001f)
			       && PS->GetOffsets().Right > 1.f)
			{
				ParentW = PS->GetOffsets().Right;
			}
		}
		// Узел с РАСТЯГИВАЮЩИМ анкором по X (наша панель строк лежит в группе с анкорами 0..1)
		//   ширину получает от родителя даром — трогать его нельзя, иначе оффсеты-отступы
		//   истолкуются как позиция и размер.
		const FAnchors A = CS->GetAnchors();
		const bool bSpanX = !FMath::IsNearlyEqual(A.Minimum.X, A.Maximum.X, 0.0001f);
		if (!bSpanX)
		{
			// Полноширинный (плита 472, шапка окна 474, верх/низ рамки 470, группа списка 446).
			N.bWide  = (O.Right > ParentW * 0.6f);
			// Прижатый к правому краю (правый ряд 9-slice x=473, кнопка 2573 локально x=429).
			N.bRight = !N.bWide && (O.Left > ParentW * 0.6f);
			// ★ ПОЧЕМУ ИНТЕРАКТИВ ЗДЕСЬ НЕ ИСКЛЮЧЁН (в отличие от вертикали). Запрет на bStretch
			//   возник из-за РАСТЯГИВАЮЩЕГО анкора по Y у SCROLL_DOWN: высота, выведенная из отступов,
			//   была мусорной. По горизонтали такой беды нет — узлы управления стоят в ассете
			//   на POINT-схеме (NormalizeControlSlot снят 15.09 — перенесено в ассет), и ширина у них честная
			//   (живой дамп: SCROLL_DOWN 8.9 px ⇒ «стоит»). Поэтому узел управления, который
			//   пользователь сделал полосой во всю ширину окна, обязан растягиваться вместе с ним —
			//   иначе за правую часть кромки было бы не взяться.
		}
		UE_LOG(LogAsdaChat, Log,
			TEXT("[чат] роль %s: %s | %s | родитель %s (ширина %.0f), rect(%.1f, %.1f, %.1f×%.1f)%s"),
			*W->GetName(),
			N.bStretch ? TEXT("вертикаль: растёт") : (N.bTop ? TEXT("вертикаль: едет с кромкой") : TEXT("вертикаль: стоит")),
			N.bWide ? TEXT("горизонт: растёт вширь") : (N.bRight ? TEXT("горизонт: едет вправо")
			                                                     : (bSpanX ? TEXT("горизонт: тянется анкором") : TEXT("горизонт: стоит"))),
			bRoot ? TEXT("корневой канвас") : TEXT("группа"), ParentW,
			O.Left, O.Top, O.Right, O.Bottom,
			bInteractive ? TEXT(" [интерактив — размер НЕ меняем]") : TEXT(""));
		NativeSlots.Add(N);
	}
	UE_LOG(LogAsdaChat, Log, TEXT("[чат] снято слотов контролов WBP: %d"), NativeSlots.Num());
}

// Применить ГЕОМЕТРИЮ окна.
//   ВЕРТИКАЛЬ: низ закреплён (POINT-анкор fy=1.0), верхняя кромка едет вверх на ΔH = NewH − 70.
//   ГОРИЗОНТАЛЬ: левый край закреплён (якорь kWinX = 1), правая кромка едет вправо на DW = NewW − 474.
//     Окно стоит в ЛЕВОМ нижнем углу базового экрана 1024×768 — свободное место справа, поэтому
//     растём вправо и якорь не «прыгает». Ширина хранится в БАЗОВЫХ px, как и вся прочая геометрия,
//     а раскладка идёт через POINT-схему импортёра ⇒ смена разрешения ничего не ломает.
void UAsdaChatSubsystem::ApplyWindowGeometry(float NewH, float NewW)
{
	WinH = FMath::Clamp(NewH, kWinMinH, kWinMaxH);
	WinW = FMath::Clamp(NewW, kWinMinW, kWinMaxW);
	const float D  = WinH - kWinMinH;
	const float DW = WinW - kWinMinW;
	// ★ СЦЕПКА: сдвиг ВСЕГО окна, когда его низ приклеен не к эталонным 578, а к кромке чата.
	//   Отрицательный = окно уехало вверх. При Shift = 0 поведение ровно прежнее.
	//   ★ Сцепка — ТОЛЬКО вертикальная: ширина журнала с шириной чата 2500 НЕ связана (требование юзера).
	const float Shift = DockBottom - kWinBottom;
	for (const FNativeSlot& N : NativeSlots)
	{
		UWidget* W = N.W.Get();
		UCanvasPanelSlot* CS = W ? AsdaUi::SlotOf(W) : nullptr;
		if (!CS) { continue; }
		FMargin M = N.Orig;
		if (N.bRoot)
		{
			M.Top += Shift;                                    // всё окно едет вместе с низом
			if (N.bTop || N.bStretch) { M.Top -= D; }          // верхняя кромка ещё и вверх от роста
			if (N.bStretch) { M.Bottom = N.Orig.Bottom + D; }  // и рост высоты
		}
		if (N.bWide)  { M.Right = N.Orig.Right + DW; }         // полноширинный — растёт вширь
		if (N.bRight) { M.Left  = N.Orig.Left  + DW; }         // прижатый вправо — едет вправо
		CS->SetOffsets(M);
	}

	// ★ Наша полоса-кромка справа: ставится ЯВНО (её нет в JUI). Верх окна = низ − высота.
	if (UWidget* RB = ResizeRightSrc)
	{
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(RB))
		{
			AsdaJui::SetPointSlot(CS, kFxLeft, kFyBottom,
				kWinX + WinW - kResizeBandOut, DockBottom - WinH, kResizeBandW, WinH);
		}
	}
}

void UAsdaChatSubsystem::ApplyWindowHeight(float NewH) { ApplyWindowGeometry(NewH, WinW); }
void UAsdaChatSubsystem::ApplyWindowWidth(float NewW)  { ApplyWindowGeometry(WinH, NewW); }

// ★★★ ЗОНА ЗАХВАТА ПРАВОЙ КРОМКИ. В JUI у окна 2570 контрола растягивания нет вовсе (см. разбор
//   у поля bResizingW), поэтому полоса — добавка порта, ровно как CHAT_NEW_ResizeRight у окна 2500.
//   Ничего не рисует (кисть прозрачная), только ловит мышь. Лежит в дереве виджетов ⇒ видна и
//   редактируема в UMG (закон ui-editable-in-ue).
void UAsdaChatSubsystem::EnsureResizeRight()
{
	if (ResizeRightSrc || !SystemWidget || !SystemWidget->WidgetTree) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(SystemWidget->WidgetTree->RootWidget);
	if (!Root) { return; }
	UBorder* B = SystemWidget->WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), FName(TEXT("CHAT_NEW_SERV_ResizeRight")));
	if (!B) { return; }
	B->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));   // прозрачная — только хит-тест
	B->SetPadding(FMargin(0.f));
	B->SetVisibility(ESlateVisibility::Visible);
	B->SetCursor(EMouseCursor::ResizeLeftRight);
	if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(B))
	{
		SetJuiPointSlot(CS, kFxLeft, kFyBottom,
			kWinX + WinW - kResizeBandOut, DockBottom - WinH, kResizeBandW, WinH);
		CS->SetZOrder(60);   // выше плит окна, чтобы кромку было за что взять
	}
	ResizeRightSrc = B;
	UE_LOG(LogAsdaChat, Log,
		TEXT("[чат] создана зона растягивания правой кромки CHAT_NEW_SERV_ResizeRight (%.0f px, пределы ширины %.0f…%.0f)"),
		kResizeBandW, kWinMinW, kWinMaxW);
}

// ★★★ ПЕРЕНОС СТРОК ЖУРНАЛА ПО ТЕКУЩЕЙ ШИРИНЕ — главное место, где ломается горизонтальный ресайз:
//   окно стало шире, а строки остались порезаны по старой ширине.
//   В эталоне ширина переноса — ПАРАМЕТР `CChat+0x5D4` (`chatLineMaxWidth`), которым
//   `textTokenizer_SplitStringIntoRows` режет текст в `CChat_AddTokenizedLines` (chat.c:3285),
//   а не константа. У нас его роль играет фактическая ширина ленты 2572 (446 + надбавка).
//   ★ ГРАБЛЯ №7 (autowrap в CanvasPanel ломает раскладку) ЗДЕСЬ НЕ ДЕЙСТВУЕТ: строки лежат
//   в UScrollBox (вертикальный бокс), а не в канвасе — там перенос штатен и безопасен.
//   ★ ГРАБЛЯ: строка журнала — это URichTextBlock (UAsdaRichText::ApplyFormatted ПОДМЕНЯЕТ
//   исходный UTextBlock на rich-виджет), а URichTextBlock НЕ наследник UTextBlock — оба идут от
//   UTextLayoutWidget. Приводить надо именно к нему, иначе Cast<UTextBlock> молча не находит ничего.
void UAsdaChatSubsystem::ApplyLinesWidth()
{
	if (!SysLog) { return; }
	const float WrapW = FMath::Max(40.f, kListW + (WinW - kWinMinW) - 6.f);
	for (int32 i = 0; i < SysLog->GetChildrenCount(); ++i)
	{
		if (UTextLayoutWidget* Tw = Cast<UTextLayoutWidget>(SysLog->GetChildAt(i)))
		{
			Tw->SetAutoWrapText(true);
			Tw->SetWrapTextAt(WrapW);
		}
	}
	LinesWrapW = WrapW;
}

// ★★★ СЦЕПКА ЖУРНАЛА 2570 С ОКНОМ ЧАТА 2500 — ОДИН механизм, без перекрёстных зависимостей.
//   Окно чата после КАЖДОЙ перекладки публикует свою верхнюю кромку (в базовых px) в общий
//   реестр `UAsdaWindowSubsystem`. Здесь мы её читаем и ставим свой низ на «кромка − зазор».
//   • чат растянули вверх → журнал уехал вверх на ту же величину;
//   • чат свернули → журнал вернулся (сцепка двусторонняя, отдельного кода на это не нужно);
//   • журнал растягивают своей кнопкой RESIZE → он растёт ВВЕРХ от этого же низа, чат не двигается.
//   ★ Базовые px, а не экранные: анкор у обоих окон POINT в левый-нижний угол экрана, поэтому
//   сцепка не зависит от разрешения и DPI (расхождение схем ловится только выше 768).
void UAsdaChatSubsystem::TickDockToChat()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	float ChatTop = 0.f;
	if (!WM->GetWindowAnchorTop(FName(TEXT("CHAT_NEW")), ChatTop)) { return; }   // чат ещё не построен

	float WantBottom = ChatTop - kDockGap;
	// Крайний случай: журнал не должен уехать за верх экрана. Верх = низ − высота.
	WantBottom = FMath::Max(WantBottom, WinH);
	if (FMath::IsNearlyEqual(WantBottom, DockBottom, 0.25f)) { return; }

	const float Prev = DockBottom;
	DockBottom = WantBottom;
	ApplyWindowHeight(WinH);   // перекладка со свежим Shift
	UE_LOG(LogAsdaChat, Verbose,
		TEXT("[чат] сцепка с окном 2500: низ журнала %.0f → %.0f (верх чата %.0f, зазор %.0f), журнал y %.0f…%.0f"),
		Prev, DockBottom, ChatTop, kDockGap, DockBottom - WinH, DockBottom);
}

// ★★★ ПОЧЕМУ РАСТЯГИВАНИЕ НЕ РАБОТАЛО (разобрано 2026-08-04).
//   Прежний OnResizePressed начинался с `if (!PC->GetMousePosition(MX,MY)) return;` — то есть при
//   любой осечке PlayerController (курсор не показан, контроллера ещё нет, окно не в фокусе)
//   функция выходила ДО установки bResizing, и перетаскивание не начиналось ВООБЩЕ, хотя кнопка
//   нажималась. Теперь: (1) флаг взводится ПЕРВЫМ действием, (2) позиция мыши берётся у Slate
//   (`FSlateApplication::GetCursorPos()` — абсолютные px рабочего стола, всегда валидны;
//   для перетаскивания важна РАЗНОСТЬ, поэтому начало отсчёта роли не играет),
//   (3) каждый шаг логируется — живой лог теперь однозначно показывает, дошёл ли клик.
// ── ★ ЕДИНЫЙ МЕХАНИЗМ МЫШИ ПО УЗЛАМ УПРАВЛЕНИЯ ────────────────────────────────────────────────
// Работает с ЛЮБЫМ классом виджета (у пользователя это UImage, у которого нет OnPressed) и не
//   зависит от доставки событий Slate: каждый кадр берём позицию курсора у FSlateApplication и
//   сравниваем с ФАКТИЧЕСКОЙ геометрией узла. Нажатие «прилипает» к узлу, НА КОТОРОМ началось, —
//   протаскивание мимо соседнего узла ложно не срабатывает.
bool UAsdaChatSubsystem::IsUnderCursor(const UWidget* W, const FVector2D& CursorAbs) const
{
	if (!W) { return false; }
	const FGeometry& G = W->GetCachedGeometry();
	if (G.GetLocalSize().X <= 0.f || G.GetLocalSize().Y <= 0.f) { return false; }
	return G.IsUnderLocation(CursorAbs) && !AsdaUi::IsCoveredByHigherWindow(W, CursorAbs);   // ★ 06.11.2026: тело окна поверх чата
}

UWidget* UAsdaChatSubsystem::FindNodeByName(const TCHAR* Exact, const TCHAR* Needle) const
{
	// ★ D5 (дедуп UI 2026-08-08): единая реализация в AsdaUi::FindNodeByName.
	return AsdaUi::FindNodeByName(SystemWidget, Exact, Needle);
}

// ★ Перенесено в ассет (15.09, разрешение владельца): здесь было определение
//   UAsdaChatSubsystem::NormalizeControlSlot (рантайм-перевод SPAN-анкора RESIZE/SCROLL_UP/SCROLL_DOWN в POINT).
//   В ассете и в снимке ручных узлов анкоры POINT — функция удалена вместе с объявлением.

// Прямоугольник окна в АБСОЛЮТНЫХ экранных координатах: объединение фактических геометрий
//   подложки, рамки и группы списка. Пустой (false), пока кэш геометрии не заполнен.
bool UAsdaChatSubsystem::GetWindowScreenRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!SystemWidget || !SystemWidget->WidgetTree) { return false; }
	bool bAny = false;
	TArray<UWidget*> All;
	SystemWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W || W->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		// ★ Прямоугольник окна считаем ТОЛЬКО по узлам, которые его и образуют: подложка, рамка и
		//   группа списка. Брать объединение ВСЕХ узлов опасно: один случайно растянутый контрол
		//   (как SCROLL_DOWN до нормализации) раздул бы рамку и заблокировал клики по всему экрану.
		const FString N = W->GetName();
		if (!N.StartsWith(TEXT("CHAT_NEW_SERV_back"))
		    && !N.StartsWith(TEXT("CHAT_NEW_SERV_line0"))
		    && !N.StartsWith(TEXT("CHAT_NEW_SERV_listbox_grp"))) { continue; }
		// ★ Аудит №4 (A9/B1): прямоугольник — через общую точку `AsdaUi::WidgetAbsRect`
		//   (она же отсеивает узлы с нулевой геометрией). Накопитель min/max остаётся здесь:
		//   СПИСОК несущих узлов — знание про ЭТО окно (см. фильтр по имени выше).
		FVector2D P, PMax;
		if (!AsdaUi::WidgetAbsRect(W, P, PMax)) { continue; }
		const FVector2D Sz = PMax - P;
		if (!bAny) { OutMin = P; OutMax = P + Sz; bAny = true; }
		else
		{
			OutMin.X = FMath::Min(OutMin.X, P.X); OutMin.Y = FMath::Min(OutMin.Y, P.Y);
			OutMax.X = FMath::Max(OutMax.X, P.X + Sz.X); OutMax.Y = FMath::Max(OutMax.Y, P.Y + Sz.Y);
		}
	}
	return bAny;
}

bool UAsdaChatSubsystem::IsCursorOverWindow() const
{
	if (!SystemWidget || !FSlateApplication::IsInitialized()) { return false; }
	FVector2D Mn, Mx;
	if (!GetWindowScreenRect(Mn, Mx)) { return false; }
	const FVector2D Cur = FSlateApplication::Get().GetCursorPos();
	return (Cur.X >= Mn.X && Cur.X <= Mx.X && Cur.Y >= Mn.Y && Cur.Y <= Mx.Y);
}

// ★★★ 2026-08-04: окно переведено на ОБЩИЙ реестр зон интерфейса (UAsdaWindowSubsystem).
//   Раньше AAsdaPlayerController::OnLeftClick звал персонально IsCursorOverWindow() этой подсистемы.
//   Окон становится больше (основной чат 2500, вкладки, палитра, контекстные меню), поэтому зона
//   регистрируется в реестре, а контроллер спрашивает одно IsCursorOverAnyUiWindow().
//   Метод IsCursorOverWindow() СОХРАНЁН как есть — его логика и есть провайдер зоны.
// ★★★ 2026-08-06: РУЧНАЯ РЕГИСТРАЦИЯ ЗОНЫ УБРАНА — она стала ДУБЛЁМ.
//   `UAsdaWindowSubsystem::IsCursorOverAnyUiWindow` теперь сам хит-тестит КАЖДОЕ окно реестра по
//   живой геометрии его контролов (минус контролы с `NoSelect="1"` из данных .JUI), а окно 2570
//   регистрируется через `OpenWindowByPath` — значит покрыто автоматически и полнее (раньше зона
//   считалась по трём узлам подложки, теперь по всем видимым контролам окна).
//   Функция оставлена как точка расширения и чтобы не трогать проверенные места вызова;
//   `GetWindowScreenRect`/`IsCursorOverWindow` продолжают использоваться логикой самого чата.
void UAsdaChatSubsystem::RegisterZone()
{
}

void UAsdaChatSubsystem::UnregisterZone()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kZoneId);
		WM->UnregisterWheelHandler(kZoneId);
	}
}

void UAsdaChatSubsystem::TickPointer(float DeltaTime)
{
	if (!FSlateApplication::IsInitialized()) { return; }
	// ★ Аудит №3 (B3): опрос ЛКМ, фронты и курсор — общая точка, ровно на месте прежнего опроса.
	Mouse.Update();
	const FVector2D Cur = Mouse.Cursor;
	const bool bDown = Mouse.bLmb;

	// ★★★ СЛОЙ «ПОВЕРХ ВСЕГО» (живой тест 2026-08-05). Выпадающий список каналов окна чата 2500
	//   раскрывается ВВЕРХ и заходит на это окно. Мышь у нас ловится ОПРОСОМ геометрии (`UImage`
	//   клик не поглощает — грабля №2), поэтому одного z-order мало: без этой проверки журнал
	//   отработал бы клик, адресованный списку поверх него (например, «прокрутка вниз» вместо
	//   выбора канала). Спрашиваем ЕДИНЫЙ реестр всплывающих слоёв оконного менеджера.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			// ★★★ МОДАЛЬНОЕ ОКНО ЗАБИРАЕТ МЫШЬ ЦЕЛИКОМ (эталон `uiIsAnyModalWindowOpen@0xB79520`,
			//   §B.4: захваченный контрол `pManager+104` получает ВЕСЬ диспетч, дерево минуется).
			//   Мышь у нас ловится ОПРОСОМ геометрии, поэтому прозрачный блокер оконного менеджера
			//   (он живёт в Slate) журнал не останавливает — гейт нужен здесь, как в окне чата 2500.
			//   ★★★ П8-9, ВОЛНА 9: условие переехало в ОБЩУЮ ТОЧКУ `AsdaUi::IsModalDispatchBlocked`
			//   (ключ отката `asda.UI.ModalGatePolling`), а к нему добавлен СБРОС ЗАЩЁЛОК.
			//   ⚠ ЗАЧЕМ СБРОС. Прежний голый `return` оставлял `PressedNode`, `bResizing`,
			//   `bResizingW` и `ScrollHeldDir` взведёнными: нажатие ДО нотиса и отпускание ПОСЛЕ
			//   его закрытия сходились в действие, которого игрок не делал (кромка «доезжала»
			//   до нового положения курсора, листание продолжалось). Отдаём те же ветки, что и
			//   штатное отпускание, — для окна модалка и есть «кнопку отпустили».
			if (AsdaUi::IsModalDispatchBlocked(GI))
			{
				if (bResizing)          { OnResizeReleased(); }
				if (bResizingW)         { OnResizeRightReleased(); }
				if (ScrollHeldDir != 0) { OnScrollReleased(); }
				PressedNode = nullptr;
				Mouse.Reset();
				return;
			}
			if (WM->IsPointerBlockedByTopmost(kZoneId))
			{
				if (Mouse.bLmbDown)
				{
					UE_LOG(LogAsdaChat, Log,
						TEXT("[чат-журнал] клик в (%.0f,%.0f) отдан слою «поверх всего» — журнал его не берёт"),
						Cur.X, Cur.Y);
				}
				return;
			}
		}
	}

	// ФРОНТ НАЖАТИЯ: определить узел под курсором и запустить его действие.
	if (Mouse.bLmbDown)
	{
		PressedNode = nullptr;
		if (IsUnderCursor(ResizeSrc, Cur))
		{
			PressedNode = ResizeSrc;
			UE_LOG(LogAsdaChat, Log, TEXT("[чат] НАЖАТ узел RESIZE (%s) в (%.0f,%.0f)"),
				*ResizeSrc->GetClass()->GetName(), Cur.X, Cur.Y);
			OnResizePressed();
		}
		else if (IsUnderCursor(ScrollUpSrc, Cur))
		{
			PressedNode = ScrollUpSrc;
			UE_LOG(LogAsdaChat, Log, TEXT("[чат] НАЖАТ узел SCROLL_UP в (%.0f,%.0f)"), Cur.X, Cur.Y);
			OnScrollUpPressed();
		}
		else if (IsUnderCursor(ScrollDownSrc, Cur))
		{
			PressedNode = ScrollDownSrc;
			UE_LOG(LogAsdaChat, Log, TEXT("[чат] НАЖАТ узел SCROLL_DOWN в (%.0f,%.0f)"), Cur.X, Cur.Y);
			OnScrollDownPressed();
		}
		// ★ ПРАВАЯ КРОМКА — горизонталь. Проверяется ПОСЛЕДНЕЙ: полоса лежит поверх правого края и не
		//   должна перебивать узлы управления пользователя, если те окажутся в той же точке.
		else if (IsUnderCursor(ResizeRightSrc, Cur))
		{
			PressedNode = ResizeRightSrc;
			UE_LOG(LogAsdaChat, Log, TEXT("[чат] НАЖАТА правая кромка в (%.0f,%.0f)"), Cur.X, Cur.Y);
			OnResizeRightPressed();
		}
	}

	// УДЕРЖАНИЕ: перетаскивание кромки либо автоповтор листания.
	if (bDown && PressedNode)
	{
		if (bResizing)
		{
			UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			const float Scale = FMath::Max(0.01f, Wd ? UWidgetLayoutLibrary::GetViewportScale(Wd) : 1.f);
			const float Want = DragStartH + (DragStartMouseY - Cur.Y / Scale);
			if (!FMath::IsNearlyEqual(Want, WinH, 0.5f)) { ApplyWindowHeight(Want); }
		}
		else if (bResizingW)
		{
			// Курсор вправо (X растёт) → ширина растёт: ЛЕВЫЙ край на месте, правый едет вправо.
			UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			const float Scale = FMath::Max(0.01f, Wd ? UWidgetLayoutLibrary::GetViewportScale(Wd) : 1.f);
			const float Want = ResizeStartW + (Cur.X / Scale - ResizeStartX);
			if (!FMath::IsNearlyEqual(Want, WinW, 0.5f))
			{
				const bool bWasAtEnd = SysLog
					&& FMath::IsNearlyEqual(SysLog->GetScrollOffset(), SysLog->GetScrollOffsetOfEnd(), 1.f);
				ApplyWindowWidth(Want);
				// ★ Перенос строк ленты пересчитывается по НОВОЙ ширине — иначе строки остались бы
				//   разорванными по старой (главная ловушка горизонтального ресайза).
				ApplyLinesWidth();
				// Строк стало меньше/больше по высоте — держим низ, если он и был внизу (эталонный
				//   автоскролл chat.c:3317/3333 «прокручивать, только если скроллбар стоял внизу»).
				if (bWasAtEnd && SysLog) { SysLog->ScrollToEnd(); }
			}
		}
		else if (ScrollHeldDir != 0)
		{
			// Автоповтор только пока курсор остаётся НАД тем же узлом.
			if (IsUnderCursor(PressedNode, Cur))
			{
				ScrollHeldTime += DeltaTime;
				if (ScrollHeldTime >= kRepeatDelay)
				{
					ScrollHeldTime -= kRepeatEvery;
					ScrollByLines(ScrollHeldDir * kScrollStepLines);
				}
			}
		}
	}

	// ОТПУСКАНИЕ.
	if (Mouse.bLmbUp)
	{
		if (bResizing) { OnResizeReleased(); }
		if (bResizingW) { OnResizeRightReleased(); }
		if (ScrollHeldDir != 0) { OnScrollReleased(); }
		if (PressedNode)
		{
			UE_LOG(LogAsdaChat, Log, TEXT("[чат] ОТПУЩЕН узел %s"), *PressedNode->GetName());
			PressedNode = nullptr;
		}
	}
	// ★ Аудит №3 (B3): запись латча делает `Mouse.Update()` в начале функции.
}

void UAsdaChatSubsystem::OnResizePressed()
{
	UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const float Scale = FMath::Max(0.01f, Wd ? UWidgetLayoutLibrary::GetViewportScale(Wd) : 1.f);
	bResizing = true;
	DragStartH = WinH;
	DragStartMouseY = AsdaCursorY() / Scale;                   // экранные px → базовые единицы JUI
	UE_LOG(LogAsdaChat, Log, TEXT("[чат] старт растягивания: высота %.0f, курсорY %.0f (масштаб %.2f)"),
		WinH, DragStartMouseY, Scale);
}

// ★★★ Запись размера журнала в ini игры (единый механизм `UAsdaWindowSubsystem::SaveWindowSize`,
//   секция `[AsdaWindowLayout]`). Точек фиксации две — верхняя и правая кромки, — и обе обязаны
//   писать одно и то же, поэтому запись вынесена в общую функцию.
void UAsdaChatSubsystem::PersistWindowSize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SaveWindowSize(FName(TEXT("CHAT_NEW_SYSTEM")), FVector2D(WinW, WinH), false);
		}
	}
}

void UAsdaChatSubsystem::OnResizeReleased()
{
	if (!bResizing) { return; }
	bResizing = false;
	PersistWindowSize();
	UE_LOG(LogAsdaChat, Log, TEXT("[чат] RESIZE отпущена: высота %.0f (пределы %.0f..%.0f)"),
		WinH, kWinMinH, kWinMaxH);
}

// ── ★ ГОРИЗОНТАЛЬ: захват правой кромки ───────────────────────────────────────────────────────
//   Отсчёт ведём в БАЗОВЫХ единицах JUI (экранные px делим на масштаб вьюпорта), потому что вся
//   геометрия окна хранится в базе 1024×768. Позиция курсора берётся у Slate, а не у
//   PlayerController: GetMousePosition отдаёт false при скрытом курсоре (та самая причина «А»,
//   из-за которой вертикальный ресайз когда-то не запускался вовсе).
void UAsdaChatSubsystem::OnResizeRightPressed()
{
	UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const float Scale = FMath::Max(0.01f, Wd ? UWidgetLayoutLibrary::GetViewportScale(Wd) : 1.f);
	bResizingW = true;                                     // флаг — ПЕРВЫМ действием
	ResizeStartW = WinW;
	ResizeStartX = (FSlateApplication::IsInitialized()
		? (float)FSlateApplication::Get().GetCursorPos().X : 0.f) / Scale;
	UE_LOG(LogAsdaChat, Log,
		TEXT("[чат] старт растягивания вправо: ширина %.0f, курсорX %.0f (масштаб %.2f, пределы %.0f…%.0f)"),
		WinW, ResizeStartX, Scale, kWinMinW, kWinMaxW);
}

void UAsdaChatSubsystem::OnResizeRightReleased()
{
	if (!bResizingW) { return; }
	bResizingW = false;
	PersistWindowSize();
	UE_LOG(LogAsdaChat, Log,
		TEXT("[чат] правая кромка отпущена: ширина %.0f, окно x %.0f…%.0f, перенос строк по %.0f px"),
		WinW, kWinX, kWinX + WinW, LinesWrapW);
}

// Клик по 2573 — «к последней строке»: прокрутить журнал в самый низ и вернуть автоскролл
//   (эталонная идиома Field153 = CClass_GetField153(scroll); uiScrollbarSetPos(scroll, Field153)).
// Подменить кисть родной плиты на СПЛОШНУЮ заливку (RoundedBox без текстуры) — убирает
//   градиентный UV-свотч Box_Black_Ot10 и даёт регулируемую плотность задника.
void UAsdaChatSubsystem::ApplyPlateBrush(float Alpha)
{
	AppliedBgAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	UImage* Im = PlateImg.Get();
	if (!Im) { return; }
	FSlateBrush B;
	B.DrawAs = ESlateBrushDrawType::RoundedBox;
	B.OutlineSettings.CornerRadii = FVector4(2.f, 2.f, 2.f, 2.f);
	B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	B.OutlineSettings.Width = 0.f;
	B.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, AppliedBgAlpha));
	Im->SetBrush(B);
	UE_LOG(LogAsdaChat, Log, TEXT("[чат] подложка: сплошная чёрная, альфа %.2f (asda.Chat.BgAlpha)"), AppliedBgAlpha);
}

// ── ★ ЛИСТАНИЕ КНОПКАМИ SCROLL_UP / SCROLL_DOWN ───────────────────────────────────────────────
// ★★★ ИСПРАВЛЕНО 2026-08-06: прежняя ссылка на эталон была НЕВЕРНОЙ. `uiListSetScrollOffset(list, 2)`
//   (@0x12EA420) — это НЕ «шаг листания 2 строки», а **межстрочный интервал 2 px**: функция кладёт
//   аргумент в `list+0x4E8` и пересчитывает высоту элемента как `2 + getFontGlyphHeight()`
//   (ui_core.c:111621, правильное имя — `uiListSetLineSpacing`). Сам интервал 2 px у нас применён
//   отдельно (`SetPadding(0,0,0,2)`). Шага листания кнопками оригинал НЕ задаёт вовсе (у
//   CHAT_U_BTN 263 / CHAT_D_BTN 264 своего обработчика по ID нет) ⇒ **шаг 2 строки — НАШЕ решение**;
//   оставлено как есть (проверено живым тестом), но выдавать его за данные эталона нельзя.
// Высота строки измеряется по факту (десайр-сайз первой строки), фолбэк 11 px при пустом журнале.
float UAsdaChatSubsystem::MeasureLineHeight() const
{
	if (SysLog && SysLog->GetChildrenCount() > 0)
	{
		if (UWidget* First = SysLog->GetChildAt(0))
		{
			const float Hgt = (float)First->GetDesiredSize().Y;
			if (Hgt > 3.f) { return Hgt; }
		}
	}
	return kFontPx * 1.375f;   // кегль 8 → межстрочный ≈ 11
}

void UAsdaChatSubsystem::ScrollByLines(int32 Lines)
{
	if (!SysLog || Lines == 0) { return; }
	const float Step = MeasureLineHeight() * (float)Lines;
	const float Cur = SysLog->GetScrollOffset();
	const float End = SysLog->GetScrollOffsetOfEnd();
	const float New = FMath::Clamp(Cur + Step, 0.f, FMath::Max(0.f, End));
	SysLog->SetScrollOffset(New);
	// Автоскролл включается сам: AddSystemMessage гонит список вниз только если он СТОЯЛ внизу.
	UE_LOG(LogAsdaChat, Log, TEXT("[чат] листание %+d стр.: смещение %.1f → %.1f (конец %.1f, автоскролл=%s)"),
		Lines, Cur, New, End, FMath::IsNearlyEqual(New, End, 1.f) ? TEXT("вкл") : TEXT("выкл"));
}

// ★★★ КОЛЕСО НАД ЖУРНАЛОМ 2570 (список 2572). У окна 2570 в .JUI НЕТ ни ползунка, ни кнопок
//   листания вовсе — прокрутка колесом здесь особенно нужна (кнопки SCROLL_UP/SCROLL_DOWN —
//   тоже наше добавление, нарисованное юзером в редакторе). Шаг тот же, что у кнопок.
bool UAsdaChatSubsystem::OnWheelJournal(int32 Notches)
{
	if (!SysLog || !SystemWidget || Notches == 0) { return false; }
	if (!FSlateApplication::IsInitialized()) { return false; }
	const FVector2D Cur = FSlateApplication::Get().GetCursorPos();
	{
		UGameInstance* GI = GetGameInstance();
		if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			// Над нами может висеть раскрытый список каналов чата — там колесо не наше.
			if (WM->IsPointerBlockedByTopmost(kZoneId)) { return false; }
		}
	}
	if (!IsUnderCursor(SysLog, Cur)) { return false; }
	ScrollByLines(-Notches * kScrollStepLines);
	return true;
}

void UAsdaChatSubsystem::OnScrollUpPressed()
{
	UE_LOG(LogAsdaChat, Log, TEXT("[чат] КЛИК: SCROLL_UP нажата"));
	ScrollHeldDir = -1; ScrollHeldTime = 0.f;
	ScrollByLines(-kScrollStepLines);
}

void UAsdaChatSubsystem::OnScrollDownPressed()
{
	UE_LOG(LogAsdaChat, Log, TEXT("[чат] КЛИК: SCROLL_DOWN нажата"));
	ScrollHeldDir = +1; ScrollHeldTime = 0.f;
	ScrollByLines(+kScrollStepLines);
}

void UAsdaChatSubsystem::OnScrollReleased()
{
	ScrollHeldDir = 0; ScrollHeldTime = 0.f;
}

void UAsdaChatSubsystem::OnHomeBtnClicked()
{
	UE_LOG(LogAsdaChat, Log, TEXT("[чат] КЛИК: HomeBtn (2573) нажата"));
	if (!SysLog) { return; }
	SysLog->ScrollToEnd();
	UE_LOG(LogAsdaChat, Log, TEXT("[чат] кнопка 2573: журнал прокручен в конец (%d строк)"), LogHistory.Num());
}

// ★ ПОЛНЫЙ структурный дамп (закон full-structural-analysis-always): по КАЖДОМУ узлу дерева
//   чат-виджетов — имя, класс, видимость, анкоры, позиция и размер слота, родитель. Именно этим
//   был пойман «лишний тёмный бар поверх полосы опыта»: планы ChatBase_Center/L/R окна 2550,
//   которое эталон держит скрытым (y 754..798 базы 768 против EXP-бара 758..768).
void UAsdaChatSubsystem::DumpWidgets() const
{
	// ══════════ 0. РЕЖИМ ВВОДА И КУРСОР (проверяется ПЕРВЫМ: если UMG не получает мышь,
	//     ни z-порядок, ни геометрия значения не имеют) ══════════
	{
		UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		APlayerController* PC = Wd ? Wd->GetFirstPlayerController() : nullptr;
		const float Scale = Wd ? UWidgetLayoutLibrary::GetViewportScale(Wd) : -1.f;
		FVector2D Cur(0, 0);
		if (FSlateApplication::IsInitialized()) { Cur = FSlateApplication::Get().GetCursorPos(); }
		UE_LOG(LogAsdaChat, Display,
			TEXT("═══ ВВОД: PlayerController=%s, bShowMouseCursor=%s, курсор Slate=(%.0f,%.0f), масштаб вьюпорта=%.3f, ЛКМ зажата=%s"),
			PC ? *PC->GetName() : TEXT("НЕТ"),
			PC ? (PC->bShowMouseCursor ? TEXT("ДА") : TEXT("НЕТ — UMG не получит мышь!")) : TEXT("?"),
			Cur.X, Cur.Y, Scale,
			(FSlateApplication::IsInitialized()
			 && FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
				? TEXT("ДА") : TEXT("нет"));
		if (PC)
		{
			float MX = 0.f, MY = 0.f;
			const bool bOk = PC->GetMousePosition(MX, MY);
			UE_LOG(LogAsdaChat, Display,
				TEXT("    PC->GetMousePosition=%s (%.0f,%.0f) — если НЕТ, значит курсор скрыт/окно не в фокусе"),
				bOk ? TEXT("ДА") : TEXT("НЕТ"), MX, MY);
		}
		if (SystemWidget)
		{
			const FGeometry& G = SystemWidget->GetCachedGeometry();
			const FVector2D P = G.GetAbsolutePosition();
			const FVector2D S2 = G.GetLocalSize();
			UE_LOG(LogAsdaChat, Display,
				TEXT("    КОРЕНЬ окна: vis=%s, абс.поз=(%.0f,%.0f), лок.размер=(%.0f,%.0f) — если размер = весь экран, Visible на корне ставить НЕЛЬЗЯ"),
				*UEnum::GetValueAsString(SystemWidget->GetVisibility()), P.X, P.Y, S2.X, S2.Y);
		}
	}


	static const auto VisName = [](ESlateVisibility V) -> const TCHAR*
	{
		switch (V)
		{
		case ESlateVisibility::Visible:               return TEXT("Visible");
		case ESlateVisibility::Collapsed:             return TEXT("Collapsed");
		case ESlateVisibility::Hidden:                return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible:      return TEXT("HitTestInvisible");
		case ESlateVisibility::SelfHitTestInvisible:  return TEXT("SelfHitTestInvisible");
		default:                                      return TEXT("?");
		}
	};

	UE_LOG(LogAsdaChat, Display, TEXT("═══ ДАМП ЧАТ-ВИДЖЕТОВ ═══ %s"), *DebugState());
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			// Окно 2550 не должно существовать вовсе — эталон его гасит (регрессия «полоса поверх EXP»).
			UE_LOG(LogAsdaChat, Display, TEXT("  окно CHAT_NEW_SERV (2550): %s"),
				WM->FindWindow(FName(TEXT("CHAT_NEW_SERV"))) ? TEXT("★ ЖИВО — ЭТО БАГ") : TEXT("не создано (верно)"));
		}
	}
	// Общий обход одного WBP: имя/класс/видимость/анкоры (SPAN/POINT — критично на ультрашироком).
	static const auto DumpTree = [](UUserWidget* Widget, const TCHAR* Title, int32 Z)
	{
		if (!Widget || !Widget->WidgetTree)
		{
			UE_LOG(LogAsdaChat, Display, TEXT("  %s: не создан"), Title);
			return;
		}
		TArray<UWidget*> All;
		Widget->WidgetTree->GetAllWidgets(All);
		UE_LOG(LogAsdaChat, Display, TEXT("  %s (z=%d), узлов %d, корень видимость=%s:"),
			Title, Z, All.Num(), VisName(Widget->GetVisibility()));
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			FString Geom = TEXT("слот: не canvas");
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W))
			{
				const FAnchors A = CS->GetAnchors();
				Geom = FString::Printf(
					TEXT("анкор [%.4f..%.4f]x[%.4f..%.4f] %s, поз(%.1f,%.1f) разм(%.1f,%.1f) z=%d"),
					A.Minimum.X, A.Maximum.X, A.Minimum.Y, A.Maximum.Y,
					(A.Minimum != A.Maximum) ? TEXT("SPAN") : TEXT("POINT"),
					CS->GetPosition().X, CS->GetPosition().Y, CS->GetSize().X, CS->GetSize().Y, CS->GetZOrder());
			}
			const FGeometry& Gm = W->GetCachedGeometry();
			const FVector2D AP = Gm.GetAbsolutePosition();
			const FVector2D LS = Gm.GetLocalSize() * Gm.Scale;
			UE_LOG(LogAsdaChat, Display,
				TEXT("    %-40s %-22s vis=%-22s род=%-28s ФАКТ абс(%.0f,%.0f) разм(%.0f,%.0f) | %s"),
				*W->GetName(), *W->GetClass()->GetName(), VisName(W->GetVisibility()),
				W->GetParent() ? *W->GetParent()->GetName() : TEXT("<root>"),
				AP.X, AP.Y, LS.X, LS.Y, *Geom);
		}
	};
	DumpTree(SystemWidget, TEXT("WBP_CHAT_NEW_SYSTEM"), kSysZ);
	DumpTree(WarWidget,    TEXT("WBP_WARMESSAGE_WIN"),  kWarZ);

	// ★ СВЕРКА СХЕМ АНКОРОВ: наш журнал обязан жить в ТОЙ ЖЕ системе координат, что планы импортёра
	//   (JUI: X POINT 0.0 «лево», Y POINT 1.0 «низ»). Расхождение SPAN↔POINT совпадает только на
	//   базовых 768 px высоты и расползается на других разрешениях (1080 → 102 px, 1440 → 220 px) —
	//   именно так журнал «отрывался» от своей плиты и выглядел отдельным окном.
	if (SystemWidget && SystemWidget->WidgetTree && SysLog)
	{
		UCanvasPanelSlot* Mine = AsdaUi::SlotOf(SysLog);
		UCanvasPanelSlot* Ref  = nullptr;
		FString RefName;
		TArray<UWidget*> All;
		SystemWidget->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)   // эталон-сосед: любой план самого списка 2572
		{
			if (W && W->GetName().StartsWith(TEXT("CHAT_NEW_SERV_listbox")))
			{
				if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W))
				{
					Ref = CS; RefName = W->GetName(); break;
				}
			}
		}
		if (Mine && Ref)
		{
			const FAnchors AM = Mine->GetAnchors(), AR = Ref->GetAnchors();
			const bool bSame = AM.Minimum.Equals(AR.Minimum, 0.001f) && AM.Maximum.Equals(AR.Maximum, 0.001f);
			UE_LOG(LogAsdaChat, Display,
				TEXT("  СВЕРКА: журнал CHAT_NEW_SYSTEM_Lines анкор [%.3f,%.3f]-[%.3f,%.3f] %s | план %s анкор [%.3f,%.3f]-[%.3f,%.3f] %s → %s"),
				AM.Minimum.X, AM.Minimum.Y, AM.Maximum.X, AM.Maximum.Y,
				(AM.Minimum != AM.Maximum) ? TEXT("SPAN") : TEXT("POINT"),
				*RefName, AR.Minimum.X, AR.Minimum.Y, AR.Maximum.X, AR.Maximum.Y,
				(AR.Minimum != AR.Maximum) ? TEXT("SPAN") : TEXT("POINT"),
				bSame ? TEXT("СОВПАДАЮТ (верно)") : TEXT("★ РАСХОДЯТСЯ — ЭТО БАГ"));
		}
		else
		{
			UE_LOG(LogAsdaChat, Display, TEXT("  СВЕРКА: не с чем сравнить (план списка 2572 не найден в WBP)"));
		}

	// ══════════ КТО ПОД КУРСОРОМ ══════════
	//   Перебираем ВСЕ узлы окна и печатаем те, чья ФАКТИЧЕСКАЯ (кэшированная) геометрия накрывает
	//   позицию мыши. Первым мышь получает узел, который: (а) хит-тестится (Visible/HitTestInvisible
	//   у родителя не отменяет ребёнка), (б) выше по порядку отрисовки. Строки идут в порядке дерева.
	if (SystemWidget && SystemWidget->WidgetTree && FSlateApplication::IsInitialized())
	{
		const FVector2D Cur = FSlateApplication::Get().GetCursorPos();
		TArray<UWidget*> UnderCur;
		SystemWidget->WidgetTree->GetAllWidgets(UnderCur);
		UE_LOG(LogAsdaChat, Display, TEXT("  ── ПОД КУРСОРОМ (%.0f,%.0f) ──"), Cur.X, Cur.Y);
		int32 Hits = 0;
		for (UWidget* W : UnderCur)
		{
			if (!W) { continue; }
			const FGeometry& G = W->GetCachedGeometry();
			const FVector2D P = G.GetAbsolutePosition();
			const FVector2D Sz = G.GetLocalSize() * G.Scale;
			if (Sz.X <= 0.f || Sz.Y <= 0.f) { continue; }
			if (Cur.X < P.X || Cur.Y < P.Y || Cur.X > P.X + Sz.X || Cur.Y > P.Y + Sz.Y) { continue; }
			const ESlateVisibility V = W->GetVisibility();
			const bool bHit = (V == ESlateVisibility::Visible);
			int32 Z = -1;
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W)) { Z = CS->GetZOrder(); }
			UE_LOG(LogAsdaChat, Display,
				TEXT("      %-40s %-20s vis=%-24s абс(%.0f,%.0f) разм(%.0f,%.0f) z=%d род=%s %s"),
				*W->GetName(), *W->GetClass()->GetName(), *UEnum::GetValueAsString(V),
				P.X, P.Y, Sz.X, Sz.Y, Z,
				W->GetParent() ? *W->GetParent()->GetName() : TEXT("<root>"),
				bHit ? TEXT("← ЛОВИТ МЫШЬ") : TEXT(""));
			++Hits;
		}
		if (Hits == 0)
		{
			UE_LOG(LogAsdaChat, Display, TEXT("      (курсор вне окна — наведи на журнал и повтори команду)"));
		}
	}
		// Что именно взято из WBP, а что создано кодом (закон ui-editable-in-ue).
		UE_LOG(LogAsdaChat, Display,
			TEXT("  РОДНЫЕ КОНТРОЛЫ WBP: кнопка сворачивания=%s | плита/рамка/список/заголовок — из WBP (слотов снято %d)"),
			bHomeBtnNative ? TEXT("CHAT_NEW_SERV_HomeBtn (РОДНАЯ)") : TEXT("реле CHAT_NEW_SERV_HomeBtn_OPT поверх плана"),
			NativeSlots.Num());
		UE_LOG(LogAsdaChat, Display,
			TEXT("  СОЗДАНО КОДОМ (в WBP нет аналога): CHAT_NEW_SYSTEM_Lines (строки списка), CHAT_NEW_SYSTEM_Resize_Cur (хват %g px по верхней кромке), CHAT_NEW_SERV_ResizeRight (хват %g px по правой кромке)"),
			kGrabH, kResizeBandW);
		UE_LOG(LogAsdaChat, Display,
			TEXT("  РАЗМЕР ОКНА: высота %.0f базовых px, пределы %.0f..%.0f, верх на y=%.0f, низ y=%.0f")
			TEXT(" (эталон %.0f, сдвиг сцепки с чатом 2500 = %+.0f); растягивание=%s"),
			WinH, kWinMinH, kWinMaxH, DockBottom - WinH, DockBottom,
			kWinBottom, DockBottom - kWinBottom, bResizing ? TEXT("ИДЁТ") : TEXT("нет"));
		UE_LOG(LogAsdaChat, Display,
			TEXT("  ШИРИНА ОКНА: %.0f базовых px, пределы %.0f..%.0f, окно x=%.0f..%.0f, надбавка DW=%+.0f;")
			TEXT(" перенос строк ленты по %.0f px; растягивание вправо=%s")
			TEXT(" (★ ширина НЕЗАВИСИМА от окна чата 2500 — требование юзера)"),
			WinW, kWinMinW, kWinMaxW, kWinX, kWinX + WinW, WinW - kWinMinW,
			LinesWrapW, bResizingW ? TEXT("ИДЁТ") : TEXT("нет"));
		UE_LOG(LogAsdaChat, Display,
			TEXT("  РАМКА/ЗАГОЛОВОК (★ отступление от эталона, решение юзера 2026-08-03): рамка CHAT_NEW_SERV_line0=%s, заголовок CHAT_NEW_SERV_SYSbottn=%s (loc 12144)"),
			bFrameShown ? TEXT("ПОКАЗАНА (приглушена DCDCC8 a=0.45, центр-заливка скрыта)") : TEXT("скрыта"),
			bHeaderShown ? TEXT("ПОКАЗАН") : TEXT("скрыт"));
		UE_LOG(LogAsdaChat, Display,
			TEXT("  ФОН: плита CHAT_NEW_SERV_back (Box_Black_Ot10, свотч-градиент 19FFFFFF) = %s; заливка списка (Inven_List, ровная 32FFFFFF) = активна"),
			bPlateHidden ? TEXT("РОВНАЯ ЗАЛИВКА ★ градиент убран") : TEXT("исходный свотч"));
		UE_LOG(LogAsdaChat, Display,
			TEXT("  ПОДЛОЖКА: чёрная, альфа %.2f (менять: asda.Chat.BgAlpha <0..1>) | КЛИК ПО ОКНУ: %s"),
			AppliedBgAlpha,
			(PlateImg.IsValid() && PlateImg->GetVisibility() == ESlateVisibility::Visible)
				? TEXT("ПЕРЕХВАТЫВАЕТСЯ ★ отступление от эталона (решение юзера 2026-08-04)")
				: TEXT("проходит в мир"));
		{
			auto BtnInfo = [](const TCHAR* Title, UButton* B) -> FString
			{
				if (!B) { return FString::Printf(TEXT("%s = НЕ НАЙДЕНА"), Title); }
				FString G = TEXT("слот: не canvas");
				if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(B))
				{
					const FAnchors A = CS->GetAnchors();
					const FMargin O = CS->GetOffsets();
					G = FString::Printf(TEXT("анкор [%.3f,%.3f]-[%.3f,%.3f] %s off(L%.1f T%.1f R%.1f B%.1f) z=%d"),
						A.Minimum.X, A.Minimum.Y, A.Maximum.X, A.Maximum.Y,
						(A.Minimum != A.Maximum) ? TEXT("SPAN") : TEXT("POINT"),
						O.Left, O.Top, O.Right, O.Bottom, CS->GetZOrder());
				}
				return FString::Printf(TEXT("%s = %s (%s), род=%s, %s"), Title, *B->GetName(),
					*B->GetClass()->GetName(),
					B->GetParent() ? *B->GetParent()->GetName() : TEXT("<root>"), *G);
			};
			auto NodeInfo = [](const TCHAR* Title, UWidget* W) -> FString
			{
				if (!W) { return FString::Printf(TEXT("%s = НЕ НАЙДЕН"), Title); }
				const FGeometry& G = W->GetCachedGeometry();
				const FVector2D P = G.GetAbsolutePosition();
				const FVector2D Sz = G.GetLocalSize() * G.Scale;
				int32 Z = -1;
				if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W)) { Z = CS->GetZOrder(); }
				return FString::Printf(TEXT("%s = %s(%s) vis=%s ФАКТ абс(%.0f,%.0f) разм(%.0f,%.0f) z=%d род=%s"),
					Title, *W->GetName(), *W->GetClass()->GetName(),
					*UEnum::GetValueAsString(W->GetVisibility()), P.X, P.Y, Sz.X, Sz.Y, Z,
					W->GetParent() ? *W->GetParent()->GetName() : TEXT("<root>"));
			};
			UE_LOG(LogAsdaChat, Display, TEXT("  УЗЛЫ УПРАВЛЕНИЯ: %s"), *NodeInfo(TEXT("RESIZE"), ResizeSrc));
			UE_LOG(LogAsdaChat, Display, TEXT("                   %s"), *NodeInfo(TEXT("SCROLL_UP"), ScrollUpSrc));
			UE_LOG(LogAsdaChat, Display, TEXT("                   %s"), *NodeInfo(TEXT("SCROLL_DOWN"), ScrollDownSrc));
			UE_LOG(LogAsdaChat, Display, TEXT("                   %s"), *NodeInfo(TEXT("HomeBtn(2573)"), HomeBtn));
			UE_LOG(LogAsdaChat, Display, TEXT("                   %s"), *NodeInfo(TEXT("ПЕРЕХВАТЧИК окна (плита)"), PlateImg.Get()));
			UE_LOG(LogAsdaChat, Display, TEXT("                   нажатый сейчас узел = %s"),
				PressedNode ? *PressedNode->GetName() : TEXT("нет"));
			FVector2D Mn, Mx;
			const bool bRect = GetWindowScreenRect(Mn, Mx);
			UE_LOG(LogAsdaChat, Display,
				TEXT("  ПРЯМОУГОЛЬНИК ОКНА (абс. экран): %s (%.0f,%.0f)-(%.0f,%.0f) | курсор над окном=%s "
				     "→ AAsdaPlayerController::OnLeftClick мировой клик %s"),
				bRect ? TEXT("есть") : TEXT("НЕТ (геометрия ещё не посчитана)"),
				Mn.X, Mn.Y, Mx.X, Mx.Y,
				IsCursorOverWindow() ? TEXT("ДА") : TEXT("нет"),
				IsCursorOverWindow() ? TEXT("БЛОКИРУЕТ") : TEXT("пропускает"));
			UE_LOG(LogAsdaChat, Display,
				TEXT("  ЛИСТАНИЕ: шаг %d стр. (эталон uiListSetScrollOffset=2), высота строки %.1f px, смещение %.1f/%.1f, удержание=%d"),
				kScrollStepLines, MeasureLineHeight(),
				SysLog ? SysLog->GetScrollOffset() : -1.f, SysLog ? SysLog->GetScrollOffsetOfEnd() : -1.f,
				ScrollHeldDir);
			UE_LOG(LogAsdaChat, Display,
				TEXT("  РАСТЯГИВАНИЕ: вверх идёт=%s (высота %.0f [%.0f..%.0f]), вправо идёт=%s (ширина %.0f [%.0f..%.0f]), хост панели строк=%s"),
				bResizing ? TEXT("ДА") : TEXT("нет"), WinH, kWinMinH, kWinMaxH,
				bResizingW ? TEXT("ДА") : TEXT("нет"), WinW, kWinMinW, kWinMaxW,
				(SysLog && SysLog->GetParent()) ? *SysLog->GetParent()->GetName() : TEXT("?"));
		}
		UE_LOG(LogAsdaChat, Display,
			TEXT("  МЫШЬ ловят только: RESIZE (z=120), SCROLL_UP/DOWN (z=115), CHAT_NEW_SERV_ResizeRight (z=60, правая кромка), HomeBtn (z=50 в группе), панель строк (колесо). Вся декорация — HitTestInvisible."));
		UE_LOG(LogAsdaChat, Display,
			TEXT("  ШРИФТ: кегль %d px (оригинал: глобальный FONT_PX импортёра) | НАЧЕРТАНИЕ: <FD> = тень 0x400000 (эталон sub_937235(2572, 0x400000); <FO>=0x800000 обводка, <FB>=0x200000 жирный)"),
			(int32)kFontPx);
		UE_LOG(LogAsdaChat, Display, TEXT("  ПРИМЕР RICH-СТРОКИ: %s"),
			*(FString::Printf(TEXT("<FS%02d><FD><FC=FFFFD306>"), (int32)kFontPx) + TEXT("Недоступная область")));
		UE_LOG(LogAsdaChat, Display, TEXT("  СКРОЛЛБАР: %s"),
			SysLog && SysLog->GetScrollBarVisibility() == ESlateVisibility::Collapsed
				? TEXT("скрыт (верно — в JUI окна 2570 ползунка нет)") : TEXT("★ ВИДЕН — ЭТО БАГ"));
	}
}

FString UAsdaChatSubsystem::DebugState() const
{
	return FString::Printf(
		TEXT("CHAT_NEW_SYSTEM: %s, журнал %d/%d строк (виджетов %d)%s | буфер A %d/%d, B %d/%d (невидимы) | WARMESSAGE: %s, строк %d/%d"),
		SystemWidget ? TEXT("создано") : TEXT("НЕТ"),
		LogHistory.Num(), kSysLogMax, SysLog ? SysLog->GetChildrenCount() : 0,
		TEXT(""),
		LinesA.Num(), kBufferMax, LinesB.Num(), kBufferMax,
		WarWidget ? TEXT("создано") : TEXT("НЕТ"), WarLines.Num(), kWarMax)
		+ FString::Printf(TEXT(" | окно %.0f×%.0f (высота [%.0f..%.0f], ширина [%.0f..%.0f]), растягивание: вверх=%s вправо=%s,")
			TEXT(" перенос строк %.0f px, кнопка 2573=%s, слотов WBP %d"),
			WinW, WinH, kWinMinH, kWinMaxH, kWinMinW, kWinMaxW,
			bResizing ? TEXT("ИДЁТ") : TEXT("нет"), bResizingW ? TEXT("ИДЁТ") : TEXT("нет"), LinesWrapW,
			bHomeBtnNative ? TEXT("РОДНАЯ WBP") : TEXT("реле поверх плана"), NativeSlots.Num());
}

// ═══════════════════════════ Консольные команды проверки (PIE) ═════════════════════════════════
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatSys(
	TEXT("asda.Chat.Sys"),
	TEXT("Системное сообщение: asda.Chat.Sys <A|B> <текст> — A=дорожка 2557, B=дорожка 2558"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatSubsystem* C = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
		if (!C) { return; }
		int32 Start = 0;
		EAsdaSysTrack Tr = EAsdaSysTrack::A;
		if (Args.Num() > 0 && (Args[0].Equals(TEXT("B"), ESearchCase::IgnoreCase))) { Tr = EAsdaSysTrack::B; Start = 1; }
		else if (Args.Num() > 0 && Args[0].Equals(TEXT("A"), ESearchCase::IgnoreCase)) { Start = 1; }
		FString Text;
		for (int32 i = Start; i < Args.Num(); ++i) { if (i > Start) Text += TEXT(" "); Text += Args[i]; }
		if (Text.IsEmpty()) { Text = TEXT("Тестовое системное сообщение"); }
		C->AddSystemMessage(Text, UAsdaChatSubsystem::ColGold(), Tr);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatLoc(
	TEXT("asda.Chat.Loc"),
	TEXT("Системное сообщение по loc-id: asda.Chat.Loc <id> [A|B]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatSubsystem* C = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
		if (!C || Args.Num() < 1) { return; }
		const EAsdaSysTrack Tr = (Args.Num() > 1 && Args[1].Equals(TEXT("B"), ESearchCase::IgnoreCase))
			? EAsdaSysTrack::B : EAsdaSysTrack::A;
		C->AddSystemMessageById(FCString::Atoi(*Args[0]), UAsdaChatSubsystem::ColGold(), Tr);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatDump(
	TEXT("asda.Chat.Dump"),
	TEXT("Состояние системного чата: журнал/оверлеи/видимость"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaChatSubsystem* C = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
		if (!C) { return; }
		UE_LOG(LogAsdaChat, Display, TEXT("%s"), *C->DebugState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatDumpWidgets(
	TEXT("asda.Chat.DumpWidgets"),
	TEXT("Полный структурный дамп чат-виджетов: имя/класс/видимость/анкоры/позиция/размер/родитель"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaChatSubsystem* C = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
		if (!C) { return; }
		C->DumpWidgets();
	}));
#endif // ASDA_DEV_CONSOLE

// ── WARMESSAGE_WIN ──────────────────────────────────────────────────────────────────────────────
// Цвет по имени/hex: gold red yellow white green amber | RRGGBB (sRGB, как в эталоне).
static FColor AsdaWarParseColor(const FString& S, bool& bOk)
{
	bOk = true;
	if (S.Equals(TEXT("gold"),   ESearchCase::IgnoreCase)) { return UAsdaChatSubsystem::ColGold(); }
	if (S.Equals(TEXT("red"),    ESearchCase::IgnoreCase)) { return UAsdaChatSubsystem::ColRed(); }
	if (S.Equals(TEXT("yellow"), ESearchCase::IgnoreCase)) { return FColor(0xFF, 0xFF, 0x00); }
	if (S.Equals(TEXT("white"),  ESearchCase::IgnoreCase)) { return UAsdaChatSubsystem::ColWhite(); }
	if (S.Equals(TEXT("green"),  ESearchCase::IgnoreCase)) { return UAsdaChatSubsystem::ColGreen(); }
	if (S.Equals(TEXT("amber"),  ESearchCase::IgnoreCase)) { return UAsdaChatSubsystem::ColAmber(); }
	if (S.Len() == 6 && FCString::Strspn(*S, TEXT("0123456789abcdefABCDEF")) == 6)
	{
		const uint32 V = FParse::HexNumber(*S);
		return FColor((V >> 16) & 0xFF, (V >> 8) & 0xFF, V & 0xFF);
	}
	bOk = false;
	return UAsdaChatSubsystem::ColGold();
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaWarMsg(
	TEXT("asda.War.Msg"),
	TEXT("WARMESSAGE (верхний 3-строчный список): asda.War.Msg <текст> [цвет: gold|red|yellow|white|green|amber|RRGGBB] [ttlMs]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatSubsystem* C = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
		if (!C) { return; }
		TArray<FString> Words = Args;
		float LifeMs = 10000.f;
		FColor Col = UAsdaChatSubsystem::ColGold();     // −11514: основной цвет канала (229/316 вызовов)
		// Хвостовые аргументы: [ttlMs] (число), затем [цвет].
		if (Words.Num() > 1 && FCString::IsNumeric(*Words.Last()))
		{
			LifeMs = FCString::Atof(*Words.Last()); Words.Pop();
		}
		if (Words.Num() > 1)
		{
			bool bOk = false;
			const FColor Parsed = AsdaWarParseColor(Words.Last(), bOk);
			if (bOk) { Col = Parsed; Words.Pop(); }
		}
		FString Text;
		for (int32 i = 0; i < Words.Num(); ++i) { if (i > 0) Text += TEXT(" "); Text += Words[i]; }
		if (Text.IsEmpty()) { Text = TEXT("Тестовое war-сообщение"); }
		// ★ Круг 7: обёртка над единым маршрутизатором — принудительно ТОЛЬКО окно 221
		//   (команда существует ради проверки самого окна, а не маршрута категории War).
		if (UAsdaMessageSubsystem* M = W->GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>())
		{
			FAsdaMsgParams P;
			P.Category = EAsdaMsgCategory::War;
			P.Text = Text;
			P.bUseColor = true; P.Color = Col;
			P.TtlMs = LifeMs;
			P.Targets = (int32)EAsdaMsgTarget::WarWindow;
			M->Post(P);
			return;
		}
		C->AddWarMessage(Text, Col, LifeMs);   // фолбэк без маршрутизатора
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaWarLoc(
	TEXT("asda.War.Loc"),
	TEXT("WARMESSAGE по loc-id: asda.War.Loc <id> [цвет]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatSubsystem* C = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
		if (!C || Args.Num() < 1) { return; }
		bool bOk = false;
		const FColor Col = (Args.Num() > 1) ? AsdaWarParseColor(Args[1], bOk) : UAsdaChatSubsystem::ColGold();
		// ★ Круг 7: обёртка над маршрутизатором с принудительным приёмником «окно 221».
		if (UAsdaMessageSubsystem* M = W->GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>())
		{
			FAsdaMsgParams P;
			P.Category = EAsdaMsgCategory::War;
			P.LocId = FCString::Atoi(*Args[0]);
			P.bUseColor = true; P.Color = Col;
			P.Targets = (int32)EAsdaMsgTarget::WarWindow;
			M->Post(P);
			return;
		}
		C->AddWarMessageById(FCString::Atoi(*Args[0]), Col);   // фолбэк
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ДУБЛЬ КОНСОЛЬНОГО КЛЮЧА `asda.War.Dump` СНЯТ (аудит 2026-09-02).
//   Было: здесь регистрировалась команда `asda.War.Dump` с телом `C->DumpWidgets()` — БАЙТ В БАЙТ
//   то же тело, что у соседней `asda.Chat.DumpWidgets` (выше в этом же файле), а `DumpWidgets()`
//   и так печатает поддерево `WBP_WARMESSAGE_WIN` и счётчик строк WARMESSAGE. То есть это был
//   не отдельный дамп, а второе имя для уже существующего.
//   Чем вредило: под тем же именем `asda.War.Dump` в `AsdaNetSubsystem.cpp` живёт СОВСЕМ ДРУГАЯ
//   диагностика — принадлежность и ставки трёх городов из последнего принятого 6743.
//   `FConsoleManager::AddConsoleObject` для КОМАНД делает `ConsoleObjects.Add(Name, Cmd);
//   ExistingCmd->Release();` — побеждает зарегистрированная ПОСЛЕДНЕЙ. По порядку .obj в rsp
//   линкера `AsdaNetSubsystem` идёт раньше `AsdaChatSubsystem`, значит дамп войны за города был
//   УНИЧТОЖЕН этим алиасом и никогда не отрабатывал.
//   Стало: алиас убран — `asda.War.Dump` снова показывает состояние войны за города; дамп виджетов
//   WARMESSAGE по-прежнему доступен штатным ключом `asda.Chat.DumpWidgets`.
