// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetCageSubsystem_Window.cpp — ★ R2 10.11.2026 (разрез AsdaPetCageSubsystem.cpp, дословный перенос): открыть/закрыть клетку, окно рядом с клеткой, панели действий/статуса.
//   Общее для частей — в AsdaPetCageSubsystemInternal.h; шапка и реверс — в AsdaPetCageSubsystem.cpp.
#include "UI/AsdaPetCageSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaCraftSubsystem.h"   // ★ гейты Г1/Г2: имена окон ремесла 76000/77500 (волна В2)

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaPetGrowthSubsystem.h"   // ★ волна 4: кнопка 43055 открывает окно 44200
#include "UI/AsdaPetEvolutionSubsystem.h" // ★ волна 5: кнопка 43056 открывает окно 43500
#include "UI/AsdaPetEvoTipSubsystem.h"   // ★ волна 4: наведение на слот показывает окно 43300
#include "UI/AsdaPetPreviewActor.h"      // ★ волна 9: превью 43052 — свой RT 240×170 и вращение
#include "Engine/TextureRenderTarget2D.h"   // дамп размера буфера превью
#include "Materials/MaterialInstanceDynamic.h"   //   кисть кадра для 43052 (и для 43576 окна 43500)
#include "Materials/MaterialInterface.h"        // ★ 24.09: мастер-материал кисти превью
#include "Kismet/KismetRenderingLibrary.h"      // ★ 24.09: выгрузка буфера превью в PNG (asda.Pet.PreviewDumpRt)
#include "Engine/World.h"                //   спавн актора сцены превью
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Net/NetOpcodes.h"          // OP_PET_SLOT_COUNT — диагностическая подача 6123
// ★ волна 6: SetSelfWeight — полоса веса из 6109/6122   ★ R3: комментарий снятого включения "AsdaNetSubsystem.h" (файл им не пользовался) — сохранён
#include "AsdaBytes.h"               // ★ волна 6: сборка тел 6109/6122 для стенда

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: лимиты ввода в байтах провода (TruncateToWireLimit)
#include "Test/AsdaSelfTestKit.h"   // ★ 21.10.2026: итог самотеста asda.Pet.SelfTestNameRule
#include "Text/AsdaLang.h"          // ★ 21.10.2026: английские названия пород (оверлей pettable) в самотесте кличек
#include "Text/AsdaLangRows.h"       // [ПОРТ] 09.10.2026 (мультиязычность): формат опции питомца на языке сессии
#include "AsdaPetCageSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPetCageSubsystem*.cpp

static ASDA_DEV_CVAR(int32, CVarPanelClampToScreen,
	TEXT("asda.Pet.PanelClampToScreen"), 1,
	TEXT("1 (по умолчанию) — зажимать панель 43058 в экран (ИСПРАВЛЕНИЕ дефекта оригинала: ")
	TEXT("upgradeWindowShow@0xDCBF50 ставит её без зажима и у края она уезжает наружу). ")
	TEXT("0 — дословное поведение оригинала."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarPanelFollowCursor,
	TEXT("asda.Pet.PanelFollowCursor"), 1,
	TEXT("1 (по умолчанию) — панель 43058 встаёт под курсор, как в оригинале ")
	TEXT("(upgradeWindowShow@0xDCBF50 берёт точку у оконного менеджера). 0 — панель остаётся на ")
	TEXT("авторской позиции из .JUI (542,292); тем же путём идёт случай «курсора нет вовсе»."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarCageRaiseOnOpen,
	TEXT("asda.Pet.CageRaiseOnOpen"), 1,
	TEXT("1 (по умолчанию) — при открытии поднимать 43000 на верх обычного слоя, как эталон ")
	TEXT("(CClass_MapInsert в CRefineWnd_Open, item_enhance.c:3831). Без этого окно, вызванное ")
	TEXT("кнопкой 454 инвентаря, появляется ПОД инвентарём. 0 — прежнее поведение порта."),
	ECVF_Default);

// ★★★ ДЕФЕКТ ВЛАДЕЛЬЦА №4 (25.09.2026) — ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА, ДОКАЗАТЕЛЬСТВО И РАЗБОР
//   ПОЛНОСТЬЮ ВЫПИСАНЫ У ОБЪЯВЛЕНИЯ `CloseCageForChildWindow` в `AsdaPetCageSubsystem.h`.
//   Коротко: `uiPetDialog_Open@0xEDD450` (`ui_core.c:89976`) и `uiUpgradeDialog_Open@0xEE18E0`
//   (`ui_core.c:90627`) ОБА зовут `uiCloseWindowReset(g_bDeviceReloadPending)`, а тот гасит
//   контрол 43000. Владелец требует обратного — помечаем и даём откат.
static ASDA_DEV_CVAR(int32, CVarKeepCageOpen,
	TEXT("asda.Pet.KeepCageOpen"), 1,
	TEXT("1 (по умолчанию, ТРЕБОВАНИЕ ВЛАДЕЛЬЦА, ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА) — открытие окон ")
	TEXT("развития 44200 и эволюции 43500 НЕ закрывает окно питомца 43000. ")
	TEXT("0 — дословно оригиналу: оба открывателя гасят 43000 (uiCloseWindowReset, ui_core.c:89976 и :90627)."),
	ECVF_Default);

// ★ Спутник ключа выше: раскладка. Разбор — у определения `PlaceChildBesideCage`.
static ASDA_DEV_CVAR(int32, CVarChildBesideCage,
	TEXT("asda.Pet.ChildBesideCage"), 1,
	TEXT("1 (по умолчанию) — при живом asda.Pet.KeepCageOpen дочернее окно (43500/44200) ")
	TEXT("ставится СБОКУ от клетки 43000 и выше неё по z, чтобы три окна были видимы разом. ")
	TEXT("Перетащенное пользователем окно не трогается. 0 — окно остаётся на позиции .JUI ")
	TEXT("(в оригинале они перекрываются, потому что вместе никогда не видны)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarPetPrewarm,
	TEXT("asda.Pet.Prewarm"), 1,
	TEXT("1 (по умолчанию) — при открытии 43000 собрать СКРЫТЫМИ окна 43300 (подсказка цепочки, ")
	TEXT("грузилась по наведению) и 43058 (панель действий, грузилась по первому ПКМ): без этого ")
	TEXT("синхронная загрузка класса внутри нажатия съедает фронты мыши и ПЕРВЫЙ клик по питомцу ")
	TEXT("теряется. 0 — как было до волны 8 (ленивая сборка по первому обращению)."),
	ECVF_Default);

// ═══════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ (CRefineWnd_Open) ════════════════════════════
bool UAsdaPetCageSubsystem::OpenCage()
{
	if (!EnsureCage()) { return false; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }

	// ★ РАННЕЕ: окно уже видно ⇒ просто спрятать (`if (*(gap_0+8)+292 == 1) return SetVisible(0)`,
	//   item_enhance.c:3807-3808). Это и есть тумблер, и он ПЕРЕД гейтами.
	if (bCageOpen)
	{
		CloseCage();
		return false;
	}

	// ── Четыре гейта, строго по порядку (item_enhance.c:3809-3820) ──
	if (WM->IsWindowVisible(kGateCraftWindow))
	{
		UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] гейт Г1: открыто окно создания предмета — 43000 не открываем"));
		return false;
	}
	if (WM->IsWindowVisible(kGateQtyWindow))
	{
		UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] гейт Г2: открыто окно количества — 43000 не открываем"));
		return false;
	}
	if (WM->IsWindowVisible(kGateEvolutionWin))
	{
		UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] гейт Г3: открыт диалог развития 43500 — 43000 не открываем"));
		return false;
	}
	if (WM->IsWindowVisible(kGateLimitBreakWin))
	{
		UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] гейт Г4: открыт диалог роста 44200 — 43000 не открываем"));
		return false;
	}

	// Эталон при открытии: сбрасывает накопитель вращения превью, гасит «захват» и «выбор»,
	//   ставит признак открытия, показывает окно, ПОДСТАВЛЯЕТ ВЫБРАННЫМ ПРИЗВАННОГО
	//   (`aExpTableLo[29] = aExpTableLo[30]`, item_enhance.c:3825), снимает «6100 в полёте»,
	//   перерисовывает панель и открывает плашку 43200.
	Drag.Disarm();
	// ★ ВОЛНА 9: вот он, «сбрасывает накопитель вращения превью» — `g_fDragRotAccum = 0.0`
	//   (`item_enhance.c:3823`). До волны 9 строка была только в комментарии.
	if (AAsdaPetPreviewActor* Pv = EnsurePreview())
	{
		Pv->ResetYaw();
		Pv->SetCaptureEnabled(true);   // окно открыто — кадр нужен
	}
	bCageOpen = true;
	StatusIconPress.Clear();       // ★ поправка ревизии: и пара иконки 43205 тоже
	if (UAsdaPetSubsystem* P = Pets())
	{
		P->SetSelectedUniqueId(P->GetActiveUniqueId());
		// ★★★ ВОЛНА 8 (пункт 1 ревизии): ЗАСОВ «6100 В ПОЛЁТЕ» СНИМАЕТСЯ ПРИ ОТКРЫТИИ И ПРИ
		//   ЗАКРЫТИИ ОКНА. Кнопки призыва 43065 и отзыва 43066 живут в панели 43058 ЭТОГО окна,
		//   а засов снимался только приходом 6101, отказом очереди и сбросом сессии: промолчавший
		//   сервер делал обе кнопки мёртвыми навсегда (закон `no-state-latch-before-shown`).
		//   Общая точка снятия — `UAsdaPetSubsystem::ClearCallRequestInFlight`, там же гасится
		//   и таймер ожидания.
		P->ClearCallRequestInFlight(TEXT("окно 43000 открыто заново"));
	}
	NameShownForUid = -1;
	WM->SetWindowVisible(kCageWindowId, true);
	// ★★★ ДЕФЕКТ ВЛАДЕЛЬЦА №4: ПОДЪЁМ НАВЕРХ ПРИ ОТКРЫТИИ — порт `CClass_MapInsert(g_pWindowMgr, …)`
	//   из `CRefineWnd_Open` (`item_enhance.c:3831`). Разбор и доказательство — у ключа
	//   `asda.Pet.CageRaiseOnOpen`. Точка ОДНА, поэтому лечатся сразу все три входа в окно:
	//   кнопка 454 инвентаря, горячая клавиша «INTERFACE PETBAG» и пункт доп-меню.
	if (CVarCageRaiseOnOpen.GetValueOnGameThread() != 0)
	{
		WM->BringWindowToFront(kCageWindowId);
		// Панель 43058 обязана ехать ВМЕСТЕ с окном — она его часть, а не самостоятельное окно.
		//   Объявляем её дочерней: `RaiseOwnedAbove` внутри
		//   `BringWindowToFront` сам держит их на ступень выше владельца, и повторный подъём
		//   владельца их больше не накрывает.
		WM->SetWindowOwner(kPanelWindowId, kCageWindowId);
		WM->BringWindowToFront(kCageWindowId);   // второй вызов уже с учётом объявленного родства
	}
	// ★★★ ВОЛНА 8 (БЛОКЕР-1): ПРОГРЕВ ЛЕНИВЫХ ОКОН — разбор и доказательства у объявления
	//   `asda.Pet.Prewarm` выше. Делается ПОСЛЕ показа 43000 (окно игрок уже видит) и ДО первого
	//   наведения/ПКМ, то есть цена платится там, где фронта мыши никто не ждёт.
	if (CVarPetPrewarm.GetValueOnGameThread() != 0)
	{
		const double TWarm = FPlatformTime::Seconds();
		// 43300 — грузилось по НАВЕДЕНИЮ (см. `UAsdaPetEvoTipSubsystem::PrewarmWindow`).
		bool bTipWarm = false;
		if (UAsdaPetEvoTipSubsystem* EvoTip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetEvoTipSubsystem>() : nullptr)
		{
			bTipWarm = EvoTip->PrewarmWindow();
		}
		// 43058 — грузилось по первому ПКМ (`ShowActionPanel` → `EnsurePanel`). `EnsurePanel`
		//   оставляет окно скрытым сама (общая точка `FAsdaJuiWindowHandle::Ensure`), поэтому
		//   отдельного «спрятать» здесь не нужно: `bPanelShown` остаётся false и зона не ловит клики.
		const bool bPanelWarm = EnsurePanel();
		UE_LOG(LogAsdaPetCage, Log,
			TEXT("[питомец-окно] прогрев ленивых окон: 43300=%s, 43058=%s, всего %.1f мс (откат asda.Pet.Prewarm 0)"),
			bTipWarm ? TEXT("готово") : TEXT("НЕТ"), bPanelWarm ? TEXT("готово") : TEXT("НЕТ"),
			(FPlatformTime::Seconds() - TWarm) * 1000.0);
	}
	RefreshItemPanel();
	OpenStatusPanel();

	UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] 43000 открыто: питомцев %d, ячеек %d, показываем %d"),
		Pets() ? Pets()->GetSlots().Num() : 0, Pets() ? Pets()->GetPossessionCount() : 0, VisibleCount());
	return true;
}

void UAsdaPetCageSubsystem::CloseCage()
{
	if (!bCageOpen) { return; }
	bCageOpen = false;
	// ★ ВОЛНА 8 (пункт 1 ревизии): засов «6100 в полёте» не переживает закрытие окна —
	//   разбор у парной строки в `OpenCage`.
	if (UAsdaPetSubsystem* PCall = Pets())
	{
		PCall->ClearCallRequestInFlight(TEXT("окно 43000 закрыто"));
	}
	HoverSlot = -1;
	TipBar = -1;
	Drag.Disarm();
	// ★ ВОЛНА 9: перестаём жечь GPU на превью. Сам буфер и актор ОСТАЮТСЯ — эталон при обычном
	//   закрытии окна render-target не освобождает (см. `ResetCageWidget`). Угол тоже остаётся:
	//   сбрасывает его ОТКРЫТИЕ окна (`item_enhance.c:3823`), а не закрытие.
	if (Preview && !bEvoUsesPreview) { Preview->SetCaptureEnabled(false); }
	StatusIconPress.Clear();       // ★ поправка ревизии: и пара иконки 43205 тоже
	// `uiCloseWindowReset@0xDC3500` (AsdaStory_game.c:411004-411010): снять признак, скрыть окно,
	//   СНЯТЬ фокус с поля имени, спрятать панель 43058 и последним — `sub_930FAC(g_PetUIDialog)`.
	// ★★★ ПОПРАВКА ВОЛНЫ 4 К КОММЕНТАРИЮ ВОЛНЫ 2. Здесь было написано, что `sub_930FAC@0xEE1000`
	//   гасит «контрол диалога развития 43500» и что исполнять его некому. Это НЕВЕРНО, и
	//   доказывается номером: `sub_930FAC` делает `CControl_SetVisible(petMgr[299], 0)`, а
	//   `petMgr[299]` — это `CPetMgr + 0x4AC` (299 × 4 = 1196 = 0x4AC); в это поле
	//   `uiUpgradeDialogBindControls@0xEE0E30` кладёт `CWindowMgr_FindControlByID(…, 43300)`
	//   (ui_core.c:90409), и по нему же `uiUpgradeDialog_Open_F50` показывает окно (:90435).
	//   ⇒ `sub_930FAC` = «СПРЯТАТЬ ОКНО 43300», и с волны 4 исполнять его ЕСТЬ КОМУ.
	// ★ 25.09.2026: здесь ИМЕННО ЖЁСТКИЙ `Hide`, а НЕ `HideOnEtalonEvent` — и это не описка.
	//   Закрытие 43000 гасит ВСЁ окно 43300, включая закреплённое «О питомце»: клетка —
	//   ВЛАДЕЛЕЦ этого окна (`SetWindowOwner`), и оставшийся без владельца потомок висел бы
	//   на экране сам по себе.
	// ⚠⚠ ЗДЕСЬ БЫЛО ЛОЖНОЕ «НЕТ», СНЯТОЕ РЕВИЗИЕЙ 25.09.2026 (закон `false-negatives-method`).
	//   Стояло: «43300 НЕТ в эталонной таблице `kEscCloseWindows` (там только 42000 и 43000),
	//   по ESC оно не закрывается и в оригинале». ЭТО НЕВЕРНО. 43300 В ТАБЛИЦЕ ЕСТЬ —
	//   `AsdaWindowSubsystem.cpp`, строка `{ 43300, TEXT("Pet_Tooltips_Win"),
	//   TEXT("uiUpgradeDialogBindControls@0xEE0E30") }`, рядом с 43450 `Pet_Inven_Tooltip`.
	//   Ошибка родилась от СЛИШКОМ УЗКОГО ОКНА ГРЕПА (смотрели 40 строк таблицы из 60+) —
	//   ровно тот способ получить ложное «нет», от которого предостерегает закон.
	//   ⇒ ESC окно ЗАКРЫВАЕТ, роспись `RegisterEscCloser` принимается, и закреплённое
	//   «О питомце» закрывается ТРЕМЯ способами: крестик, ESC, закрытие владельца 43000.
	if (UAsdaPetEvoTipSubsystem* EvoTip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetEvoTipSubsystem>() : nullptr)
	{
		EvoTip->Hide();
	}
	// ★★★ ФОКУС СНИМАЕТСЯ, А НЕ СТАВИТСЯ: эталон `uiCloseWindowReset@0xDC3500` делает
	//   `CControl_SetFocus(pNameEditBox, 0)` (AsdaStory_game.c:411008). С захваченным фокусом
	//   скрытое поле ввода съедало бы клавиатуру и движение ушло бы в текст.
	if (EdtName)
	{
		EdtName->SetUserFocus(nullptr);
		if (APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr)
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
			(void)PC;
		}
	}
	HideActionPanel();
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::PetCage);
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kCageWindowId, false);
	}
	UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] 43000 закрыто"));
}

// ═══════ ДЕФЕКТ ВЛАДЕЛЬЦА №4: ЕДИНАЯ ТОЧКА «дочернее окно просит закрыть клетку» ══════════════
//   Полное доказательство того, что оригинал 43000 ЗАКРЫВАЕТ, и обоснование отступления —
//   у объявления в `AsdaPetCageSubsystem.h`; ключ отката — `asda.Pet.KeepCageOpen`.
void UAsdaPetCageSubsystem::CloseCageForChildWindow(const TCHAR* Who)
{
	if (CVarKeepCageOpen.GetValueOnGameThread() == 0)
	{
		// Дословно оригиналу: `uiCloseWindowReset(g_bDeviceReloadPending)`.
		CloseCage();
		return;
	}
	if (!bCageOpen) { return; }   // клетка и так закрыта — отступлению нечего беречь
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] окно %s открыто, 43000 ОСТАВЛЕНО ОТКРЫТЫМ — отступление от оригинала ")
		TEXT("по требованию владельца (оригинал гасит 43000: ui_core.c:89976 и :90627). ")
		TEXT("Откат: asda.Pet.KeepCageOpen 0"),
		Who ? Who : TEXT("(дочернее)"));
}

// ═══════ РАСКЛАДКА ТРЁХ ОКОН РАЗОМ (спутник отступления выше) ═════════════════════════════════
//
// ЗАЧЕМ. У оригинала 43000 и его дочерние окна ВМЕСТЕ НИКОГДА НЕ ВИДНЫ, поэтому их авторские
//   координаты .JUI (база 1024×768, дампы `Reports/pet/ui_dump/`) спокойно накладываются:
//     43000 `334,223 512×385`   43500 `205,126 390×558`   44200 `329,185 366×398`.
//   Как только клетка остаётся открытой (ключ `asda.Pet.KeepCageOpen`), дочернее окно садится
//   ей ровно на середину и закрывает список питомцев целиком. Владелец потребовал, чтобы три
//   окна были видимы разом и не лезли друг на друга целиком — это и делает функция.
//
// ЧТО ДЕЛАЕТ. Ставит дочернее окно ВПЛОТНУЮ СПРАВА от корпуса клетки (зазор `kGapPx`), а если
//   справа не помещается — слева; если и там нет места, окно не трогается вовсе (лучше наложение,
//   чем окно за краем экрана). Двигаются, как и у всех окон порта с расплющенной иерархией .JUI,
//   ВСЕ дети корневой канвы одним общим смещением (`FAsdaPopupMenu::PlaceAt`) — взаимная
//   раскладка контролов не меняется.
//
// ★ ЧЕГО НЕ ДЕЛАЕТ — НЕ ТРОГАЕТ ОКНО, КОТОРОЕ ПОЛЬЗОВАТЕЛЬ УЖЕ ПЕРЕТАСКИВАЛ. Об этой грабле
//   прямо предупреждает шапка `UAsdaWindowSubsystem::HasSavedWindowOffset`: подсистема, считающая
//   позицию окна своей формулой, обязана спросить про сохранённый сдвиг ПЕРЕД тем, как перебить
//   позицию, иначе восстановленная из ini позиция затирается в первом же кадре.
//
// ОТСТУПЛЕНИЕ ЛИ ЭТО. Да, и помечено: оригинал позиции .JUI не меняет. Откат —
//   `asda.Pet.ChildBesideCage 0` (тогда окно встаёт на авторское место и перекрывает клетку,
//   как в .JUI).
bool UAsdaPetCageSubsystem::PlaceChildBesideCage(FName ChildWindowId, UUserWidget* ChildWidget,
	double MinReserveWidth)
{
	if (CVarChildBesideCage.GetValueOnGameThread() == 0) { return false; }
	if (!bCageOpen || !ChildWidget || !Cage) { return false; }

	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }

	// Пользователь уже ставил это окно рукой (или мы поставили его в прошлый раз и записали
	//   сдвиг ниже) — его выбор сильнее нашей формулы. Отсюда же идемпотентность.
	if (WM->HasSavedWindowOffset(ChildWindowId)) { return false; }

	// ★★★ ГЕОМЕТРИЮ БЕРЁМ У ЗАРЕГИСТРИРОВАННЫХ ЗОН, А НЕ У ОФФСЕТОВ КАНВЫ (поправка живого
	//   прогона 25.09.2026). Первая редакция складывала оффсеты слотов корневой канвы и
	//   сравнивала их с размером экрана — и получила «клетка −256,−204..256,204»: у окна 43000
	//   анкор ЦЕНТРАЛЬНЫЙ (его ставит `CenterWindowKeepingSavedOffset`), у 43500 и 44200 —
	//   свой из .JUI, то есть три РАЗНЫЕ системы отсчёта в одной формуле. Это ровно тот класс
	//   ошибок, который запрещает закон `jui-align-anchors`, и лечится он тем же способом:
	//   СЧИТАТЬ в одной системе (живые абсолютные прямоугольники корпусов), а ДВИГАТЬ дельтой —
	//   дельта одинакова в любой системе отсчёта.
	//   Прямоугольник корпуса уже есть у оконной подсистемы: это зона окна, которую все окна
	//   набора регистрируют через `FAsdaJuiWindowHandle::RegisterFrameZone` по планам рамки.
	FVector2D CageMin, CageMax, ChildMin, ChildMax;
	if (!WM->GetUiZoneRect(kCageWindowId, CageMin, CageMax)
	 || !WM->GetUiZoneRect(ChildWindowId, ChildMin, ChildMax))
	{
		// ★ ГЕОМЕТРИИ ЕЩЁ НЕТ — ЭТО НОРМА, А НЕ ОТКАЗ (закон `no-state-latch-before-shown`).
		//   Окно показали в этом же кадре, а Slate считает геометрию в конце кадра. Повторим
		//   на следующем тике; постановку делает `TickPendingBeside`.
		PendingBesideId = ChildWindowId;
		PendingBesideWidget = ChildWidget;
		PendingBesideReserveW = MinReserveWidth;
		PendingBesideFrames = kPendingBesideMaxFrames;
		return false;
	}

	const FVector2D ChildSize = ChildMax - ChildMin;
	if (ChildSize.X <= 0.0 || ChildSize.Y <= 0.0) { return false; }

	const FVector2D Screen = AsdaUi::ViewportSizeUnits(GetWorld());
	if (Screen.X <= 0.0 || Screen.Y <= 0.0) { return false; }

	constexpr double kGapPx = 6.0;   // зазор между корпусами, чтобы рамки не слипались

	// ★ СТОРОНА У КАЖДОГО ОКНА СВОЯ И ПОСТОЯННАЯ — иначе два дочерних окна, открытые разом,
	//   сели бы в одно и то же место и накрыли друг друга (владелец требует видеть ТРИ окна).
	//     43500 «Эволюция» (390×558, высокое) — СЛЕВА от клетки;
	//     44200 «Развитие» (366×398)          — СПРАВА.
	//   Не влезло на своей стороне — пробуем противоположную; не влезло нигде — не трогаем
	//   (лучше наложение, чем окно за краем экрана).
	// ★ ЗАПАС ПОД САМУЮ ШИРОКУЮ ФОРМУ ОКНА (см. `MinReserveWidth` в объявлении): считаем место
	//   по НЕЙ, а ставим по текущему левому краю — тогда разворот страницы не наедет на клетку.
	const double EffW = FMath::Max((double)ChildSize.X, MinReserveWidth);

	// Куда в итоге встанет окно. Объявлено ЗДЕСЬ, потому что точку выбирают ДВА места: своё
	//   направление четвёртого окна (сразу ниже) и общая лестница ступеней (дальше). Двигает же
	//   окно ОДИН общий хвост внизу функции — второй копии `PlaceAt`/`SyncSavedWindowOffset`
	//   в этой функции быть не должно.
	double WantX = 0.0;
	double WantY = 0.0;
	const TCHAR* Side = nullptr;

	// ★★★ ЧЕТВЁРТОЕ ОКНО НАБОРА (25.09.2026): «О питомце» 43300 садится ПОД клетку.
	//   ПРИЧИНА. Боковины заняты: 43500 слева, 44200 справа (см. ниже), и пятого свободного
	//   направления у прямоугольника нет. Низ подходит лучше всего: окно 43300 низкое
	//   (376×320 против 512×385 у клетки) и, встав под ней, не перекрывает ни одно из боковых —
	//   владелец требует видеть все окна разом.
	//   ★ ЭТО НЕ ВТОРАЯ СХЕМА, А ЕЩЁ ОДНА СТУПЕНЬ ТОЙ ЖЕ ЛЕСТНИЦЫ, И ОНА ПЕРВАЯ ТОЛЬКО ДЛЯ 43300:
	//   здесь выбирается ТОЧКА, а не делается постановка. Не влезло вниз (клетка у нижней кромки
	//   экрана) ⇒ управление идёт в общие ступени «слева/справа», вплоть до прижатия к кромке.
	bool bBelowChosen = false;
	if (ChildWindowId == FName(TEXT("Pet_Tooltips_Win")))
	{
		const double BelowY = CageMax.Y + kGapPx;
		if (BelowY + ChildSize.Y <= Screen.Y)
		{
			WantX = FMath::Clamp(CageMin.X, 0.0, FMath::Max(0.0, (double)Screen.X - ChildSize.X));
			WantY = BelowY;
			Side = TEXT("ПОД клеткой (своё направление четвёртого окна набора)");
			bBelowChosen = true;
		}
		else
		{
			UE_LOG(LogAsdaPetCage, Log,
				TEXT("[питомец-окно] «О питомце» 43300 под клетку не влезло (низ клетки %.0f, ")
				TEXT("экран %.0f, окну нужно %.0f) — иду по общим ступеням, как все окна набора"),
				CageMax.Y, Screen.Y, ChildSize.Y);
		}
	}

	const bool bPreferLeft = (ChildWindowId == FName(TEXT("Pet_Evolution_Win")));
	const double LeftX  = CageMin.X - kGapPx - EffW;
	const double RightX = CageMax.X + kGapPx;
	const bool bLeftFits  = (LeftX >= 0.0);
	const bool bRightFits = (RightX + EffW <= Screen.X);

	// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026 (КРИТ-3): ЛЕСТНИЦА ЗАПАСНЫХ ВАРИАНТОВ ВМЕСТО «СДАЁМСЯ».
	//
	//   БОЛЕЗНЬ. Место считалось ТОЛЬКО по запасу `MinReserveWidth` (у 43500 это 776 — ширина
	//   развёрнутой страницы). Значит боковина требовала 776 + 6, и всё работало лишь при ширине
	//   экрана ≥ 2076 px. Я проверил только 2560×1440 и этого не заметил. Живьём:
	//     2560×1440 — «поставлено слева (242,538)» ✓
	//     1920×1080 — «не хватило места ни справа, ни слева» ⇒ окно оставалось на месте .JUI
	//     1280×720  — то же, и там 43500 (205,126)-(595,684) РЕАЛЬНО накрывает клетку (384,178)-(896,564)
	//   Владелец вполне может играть на 1920×1080, то есть требование «три окна видны разом»
	//   не выполнялось на самом ходовом разрешении.
	//
	//   ЛЕЧЕНИЕ — ЧЕТЫРЕ СТУПЕНИ, ОТ ЛУЧШЕГО К ДОПУСТИМОМУ, И НИ ОДНОГО МОЛЧАЛИВОГО ОТКАЗА:
	//     1) своя сторона с запасом под развёрнутую страницу  — идеал, ничего не пересекается;
	//     2) противоположная сторона с тем же запасом;
	//     3) своя/противоположная сторона по ТЕКУЩЕЙ ширине окна (без запаса): узкая страница
	//        влезает почти всегда, а разворот лишь частично наедет на клетку — это допустимо,
	//        требование владельца звучит «не лезть друг на друга ЦЕЛИКОМ»;
	//     4) прижать к своей кромке экрана: перекрытие сводится к нескольким пикселям и окно
	//        гарантированно остаётся на экране целиком.
	//   Проверено на трёх разрешениях (см. отчёт волны).
	const double LeftXNarrow  = CageMin.X - kGapPx - ChildSize.X;
	const double RightXNarrow = CageMax.X + kGapPx;
	const double EdgeX = bPreferLeft ? 0.0 : FMath::Max(0.0, (double)Screen.X - ChildSize.X);

	// ★ Первая ступень — «точка уже выбрана выше» (см. `bBelowChosen`). Так своё направление
	//   четвёртого окна встраивается в ЭТУ лестницу, а не обходит её стороной.
	if (bBelowChosen)                                               { /* точка выбрана выше */ }
	else if (bPreferLeft && bLeftFits)                              { WantX = LeftX;        Side = TEXT("слева, с запасом под разворот"); }
	else if (!bPreferLeft && bRightFits)                            { WantX = RightX;       Side = TEXT("справа, с запасом под разворот"); }
	else if (bRightFits)                                            { WantX = RightX;       Side = TEXT("справа с запасом (своя сторона не влезла)"); }
	else if (bLeftFits)                                             { WantX = LeftX;        Side = TEXT("слева с запасом (своя сторона не влезла)"); }
	else if (bPreferLeft && LeftXNarrow >= 0.0)                     { WantX = LeftXNarrow;  Side = TEXT("слева по текущей ширине (запас не влез)"); }
	else if (!bPreferLeft && RightXNarrow + ChildSize.X <= Screen.X) { WantX = RightXNarrow; Side = TEXT("справа по текущей ширине (запас не влез)"); }
	else if (RightXNarrow + ChildSize.X <= Screen.X)                { WantX = RightXNarrow; Side = TEXT("справа по текущей ширине (своя сторона не влезла)"); }
	else if (LeftXNarrow >= 0.0)                                    { WantX = LeftXNarrow;  Side = TEXT("слева по текущей ширине (своя сторона не влезла)"); }
	else
	{
		WantX = EdgeX;
		Side = bPreferLeft ? TEXT("прижато к ЛЕВОЙ кромке экрана (места рядом с клеткой нет)")
		                   : TEXT("прижато к ПРАВОЙ кромке экрана (места рядом с клеткой нет)");
		UE_LOG(LogAsdaPetCage, Warning,
			TEXT("[питомец-окно] окну «%s» не хватило места рядом с 43000 ни с запасом, ни по ")
			TEXT("текущей ширине (экран %.0f×%.0f, клетка %.0f..%.0f, ширина окна %.0f, запас %.0f) ")
			TEXT("— прижимаю к кромке, частичное перекрытие возможно"),
			*ChildWindowId.ToString(), Screen.X, Screen.Y, CageMin.X, CageMax.X, ChildSize.X, EffW);
	}
	// По вертикали — по верхней кромке клетки, но не выше нуля и не ниже низа экрана.
	//   ★ У окна, вставшего ПОД клетку, вертикаль уже своя (низ клетки + зазор) — не перебиваем.
	if (!bBelowChosen)
	{
		WantY = FMath::Clamp(CageMin.Y, 0.0, FMath::Max(0.0, (double)Screen.Y - ChildSize.Y));
	}

	// ДВИГАЕМ ДЕЛЬТОЙ: она одна и та же в абсолютных координатах и в оффсетах канвы
	//   (DPI-кривая проекта = 1.0, юнит канвы = экранный пиксель), поэтому анкор окна значения
	//   не имеет. Иерархия .JUI расплющена ⇒ сдвигаем ВСЕХ детей корневой канвы разом.
	const FVector2D Delta = FVector2D(WantX, WantY) - ChildMin;
	FVector2D TopLeft(0.0, 0.0);
	if (!FAsdaPopupMenu::GetTopLeft(ChildWidget, TopLeft)) { return false; }
	FAsdaPopupMenu::PlaceAt(ChildWidget, TopLeft + Delta);

	// ★ БАЗА НАКОПЛЕНИЯ СДВИГОВ — К ФАКТУ. Требование шапки `SyncSavedWindowOffset`: окно,
	//   которое двигает собственный код, обязано привести учёт общего механизма к «текущая
	//   позиция − дизайн-позиция .JUI», иначе следующее перетаскивание мышью ляжет на неверную
	//   базу и при запуске окно восстановится мимо места (так в ini попадал мусор у 56000).
	FVector2D Applied(0.0, 0.0);
	WM->GetAppliedWindowOffset(ChildWindowId, NAME_None, Applied);
	WM->SyncSavedWindowOffset(ChildWindowId, NAME_None, Applied + Delta);

	// ★★★ У 43500 ЕСТЬ СВОЙ СНИМОК АВТОРСКОЙ РАСКЛАДКИ, И ЕГО НАДО СДВИНУТЬ ВМЕСТЕ С ОКНОМ.
	//   Окно эволюции переключает узкую/широкую страницу, а `ApplyPage` пишет в слоты АБСОЛЮТНЫЕ
	//   числа из `LayoutBase` — без этой строки первое же переключение вернуло бы часть узлов на
	//   авторское место, и окно разъехалось бы надвое (живой прогон 25.09 это и поймал; полный
	//   разбор — у `UAsdaPetEvolutionSubsystem::OnWindowMovedExternally`).
	//   Связь ЯВНАЯ и узкая: два окна одного набора питомцев, а не общий механизм.
	if (ChildWindowId == FName(TEXT("Pet_Evolution_Win")))
	{
		if (UAsdaPetEvolutionSubsystem* Evo = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UAsdaPetEvolutionSubsystem>() : nullptr)
		{
			Evo->OnWindowMovedExternally(Delta);
		}
	}

	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] окно «%s» поставлено %s от 43000: корпус был (%.0f,%.0f)-(%.0f,%.0f), ")
		TEXT("сдвиг (%.0f,%.0f) ⇒ станет (%.0f,%.0f); клетка (%.0f,%.0f)-(%.0f,%.0f), экран %.0f×%.0f ")
		TEXT("(откат asda.Pet.ChildBesideCage 0)"),
		*ChildWindowId.ToString(), Side, ChildMin.X, ChildMin.Y, ChildMax.X, ChildMax.Y,
		Delta.X, Delta.Y, WantX, WantY, CageMin.X, CageMin.Y, CageMax.X, CageMax.Y, Screen.X, Screen.Y);
	PendingBesideId = NAME_None;
	PendingBesideWidget = nullptr;
	PendingBesideFrames = 0;
	PendingBesideReserveW = 0.0;
	return true;
}

// Повтор постановки, когда в кадре открытия геометрии ещё не было. Зовётся из `Tick`.
void UAsdaPetCageSubsystem::TickPendingBeside()
{
	if (PendingBesideId.IsNone() || PendingBesideFrames <= 0) { return; }
	--PendingBesideFrames;
	UUserWidget* W = PendingBesideWidget.Get();
	const FName Id = PendingBesideId;
	if (!W || !bCageOpen)
	{
		PendingBesideId = NAME_None; PendingBesideWidget = nullptr; PendingBesideFrames = 0;
		return;
	}
	if (PendingBesideFrames <= 0)
	{
		UE_LOG(LogAsdaPetCage, Warning,
			TEXT("[питомец-окно] окно «%s» не удалось поставить рядом с 43000: живой геометрии ")
			TEXT("корпуса так и не появилось за %d кадров — оставлено на месте .JUI"),
			*Id.ToString(), kPendingBesideMaxFrames);
		PendingBesideId = NAME_None; PendingBesideWidget = nullptr;
		return;
	}
	// Рекурсии не будет: успех гасит заявку сам, неудача только уменьшает счётчик кадров.
	const int32 FramesLeft = PendingBesideFrames;
	const double Reserve = PendingBesideReserveW;
	PendingBesideId = NAME_None; PendingBesideWidget = nullptr; PendingBesideFrames = 0;
	if (!PlaceChildBesideCage(Id, W, Reserve) && !PendingBesideId.IsNone())
	{
		PendingBesideFrames = FramesLeft;   // заявка переставлена внутри — вернуть остаток кадров
	}
}

// ═════════════════════════ ПАНЕЛЬ ДЕЙСТВИЙ 43058: ПОКАЗ И СКРЫТИЕ ═════════════════════════════
void UAsdaPetCageSubsystem::ShowActionPanel()
{
	if (!EnsurePanel()) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// `upgradeWindowShow@0xDCBF50` (AsdaStory_game.c:414096-414108): показать, поднять в реестре,
	//   поставить в ТОЧКУ КУРСОРА и последним шагом — `return sub_930FAC(g_PetUIDialog)`.
	//   (`copyPair79(g_pWindowMgr, v2)` — последняя позиция мыши оконного менеджера).
	// ★★★ ВОЛНА 8 (пункт 2 сквозной ревизии): ПОСЛЕДНИЙ ШАГ ЭТАЛОНА ИСПОЛНЕН.
	//   Здесь стоял комментарий «исполнять здесь пока нечему» и толкование `sub_930FAC` как
	//   «гашение контрола диалога 43500». Оба утверждения ПРОТУХЛИ ещё в волну 4:
	//   `sub_930FAC` делает `CControl_SetVisible(petMgr[299], 0)`, а `petMgr[299]` = `CPetMgr + 0x4AC`,
	//   куда `uiUpgradeDialogBindControls@0xEE0E30` кладёт контрол 43300 (ui_core.c:90409) —
	//   то есть это «СПРЯТАТЬ 43300», и исполнитель у него есть: `UAsdaPetEvoTipSubsystem`
	//   (его же зовёт `CloseCage` сороком строк выше и по тому же основанию).
	//   ПОЧЕМУ ЭТО ВИДНО ИГРОКУ, А НЕ ТОЛЬКО В КОДЕ: ПКМ по слоту ВСЕГДА идёт после
	//   наведения на этот же слот, а наведение ПОКАЗЫВАЕТ 43300 — значит без этого шага
	//   подсказка цепочки оставалась висеть под панелью в обычной игре, каждый раз.
	//   Шаг стоит ПЕРЕД показом панели, а не после: у эталона он последний, но порядок
	//   здесь не наблюдаем (оба окна перерисовываются одним и тем же кадром), а раннее
	//   гашение гарантирует, что панель не поставится под ещё живую подсказку.
	if (UAsdaPetEvoTipSubsystem* EvoTip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetEvoTipSubsystem>() : nullptr)
	{
		// ★ ПОПРАВКА 25.09.2026: эталонный шаг зовёт `HideOnEtalonEvent`, а не `Hide`.
		//   Закреплённое окно «О питомце» открывает САМА эта панель своей шестой кнопкой, и гасить
		//   его открытием панели значило бы закрывать окно ровно тем действием, которым его
		//   открывают. Во всём остальном шаг прежний, а при `asda.Pet.InfoWindow 0` закреплённого
		//   режима не бывает вовсе и `HideOnEtalonEvent` вырождается в дословный `Hide`.
		EvoTip->HideOnEtalonEvent();          // `return sub_930FAC(g_PetUIDialog)` (:414105)
	}
	bPanelShown = true;
	WM->SetWindowVisible(kPanelWindowId, true);

	// Корень JUI расплющен: сдвигаем ВСЕХ детей одним общим смещением, чтобы левый верхний угол
	//   корпуса встал под курсор (тем же приёмом, что `FAsdaPopupMenu` у меню игрока 2000).
	// ★ ДВА УСЛОВИЯ, БЕЗ КОТОРЫХ ПАНЕЛЬ ПРОПАДАЕТ (найдено проверкой путём игрока 21.09.2026:
	//   первый прогон дал у ВСЕХ девяти планов «прям=нет», то есть нулевую геометрию):
	//     1) курсор надо брать В ЕДИНИЦАХ ВЬЮПОРТА (`CursorInViewportUnits`), а не в абсолютных
	//        десктопных координатах Slate — вторые в офскрин-прогоне уводят панель за канву;
	//     2) ★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА, ПОМЕЧЕНО (закон `fix-original-defects`): положение
	//        ЗАЖИМАЕТСЯ в экран. Эталон `upgradeWindowShow@0xDCBF50` (AsdaStory_game.c:414110-414124)
	//        ставит панель БЕЗ зажима, и у правого/нижнего края она уезжает наружу — это дефект
	//        оригинала. Ключ отката к дословному поведению — `asda.Pet.PanelClampToScreen 0`.
	//   Курсора нет вовсе (headless, окно не в фокусе) ⇒ панель остаётся на своём месте из `.JUI`
	//   (542,292) — это ровно то, что делает оригинал, когда оконный менеджер не дал точки.
	// ★ ВОЛНА 4 (ревизия, МЕЛК-7): инициализация добавлена ради предупреждения компилятора
	//   C4701 «potentially uninitialized local variable 'Cur'». При `bFollow == false`
	//   `CursorInViewportUnits` не зовётся вовсе, и `Cur` уходила в ветку логирования
	//   неинициализированной. Поведение не меняется: в этой ветке значение не читается.
	//   Код принадлежит ВОЛНЕ 2 — правка точечная и помечена.
	FVector2D Cur(0.0, 0.0);
	const bool bFollow = (CVarPanelFollowCursor.GetValueOnGameThread() != 0);
	const bool bHaveCursor = bFollow && AsdaUi::CursorInViewportUnits(GetWorld(), Cur);
	if (!bHaveCursor)
	{
		// Курсора нет (окно не в фокусе, офскрин-прогон) либо слежение выключено ключом —
		//   панель остаётся там, где она нарисована в `.JUI` (542,292). Ровно это делает
		//   оригинал, когда оконный менеджер не дал точки.
		UE_LOG(LogAsdaPetCage, Log,
			TEXT("[питомец-окно] панель 43058: %s — оставлена на авторской позиции .JUI"),
			bFollow ? TEXT("курсор недоступен") : TEXT("слежение выключено ключом asda.Pet.PanelFollowCursor"));
	}
	else
	{
		FVector2D Mn(0.0, 0.0), Mx(0.0, 0.0);
		bool bAny = false;
		if (Panel && Panel->WidgetTree)
		{
			TArray<UWidget*> All;
			Panel->WidgetTree->GetAllWidgets(All);
			for (UWidget* W : All)
			{
				FVector2D P, S;
				if (W && AsdaUi::SlotRect(W, P, S) && S.X > 0.0 && S.Y > 0.0)
				{
					if (!bAny) { Mn = P; Mx = P + S; bAny = true; }
					else { Mn = FVector2D::Min(Mn, P); Mx = FVector2D::Max(Mx, P + S); }
				}
			}
		}
		const FVector2D Size = bAny ? (Mx - Mn) : FVector2D(158.0, 119.0);   // корпус 43058 из `.JUI`
		if (CVarPanelClampToScreen.GetValueOnGameThread() != 0)
		{
			// ★ Дедуп №2, В1-B (U16, 11.10.2026): общая точка клампа попапа к экрану (экран не готов — без изменений, как было).
			Cur = AsdaUi::ClampPopupToViewport(Cur, Size, AsdaUi::ViewportSizeUnits(GetWorld()));
		}
		FAsdaPopupMenu::PlaceAt(Panel, Cur);
		UE_LOG(LogAsdaPetCage, Log, TEXT("[питомец-окно] панель 43058 поставлена в (%.0f,%.0f), корпус %.0fx%.0f"),
			Cur.X, Cur.Y, Size.X, Size.Y);
	}
}

void UAsdaPetCageSubsystem::HideActionPanel()
{
	if (!bPanelShown) { return; }
	bPanelShown = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kPanelWindowId, false);
	}
}

// ═════════════════════════════ ПЛАШКА 43200: ПОКАЗ И СКРЫТИЕ ══════════════════════════════════
void UAsdaPetCageSubsystem::OpenStatusPanel()
{
	if (!EnsureStatus()) { return; }
	// `refineWindow_Open@0xDC6FE0`: нет ПРИЗВАННОГО ⇒ плашку прячем и выходим.
	if (Active() == nullptr) { HideStatusPanel(); return; }
	bStatusShown = true;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kStatusWindowId, true);
	}
	RefreshStatusPanel();
}

void UAsdaPetCageSubsystem::HideStatusPanel()
{
	if (!bStatusShown) { return; }
	bStatusShown = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kStatusWindowId, false);
	}
}
