// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumGachaSubsystem_Controls.cpp — ★ R2 10.11.2026 (разрез AsdaPremiumGachaSubsystem.cpp, дословный перенос): SetupControls и контролы окон (выбор/сетка/сообщение/клик/варианты).
//   Общее для частей — в AsdaPremiumGachaSubsystemInternal.h; шапка и реверс — в AsdaPremiumGachaSubsystem.cpp.
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaPremiumPostSubsystem.h"   // отложенный показ приза: сообщаем ящику итог выбора
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaGachaSceneActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"                          // TActorIterator — поиск актора сцены для диагностики
#include "Kismet/KismetRenderingLibrary.h"   // ★ 24.09: выгрузка буфера сцены гачи в PNG (asda.Gacha.DumpRt)
#include "Components/Image.h"
#include "Engine/World.h"
#include "Login/AsdaButtonRelay.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaAnimatedPlane.h"      // 68058 `Apple_Make3` — кадровый анимплан (RestartAnim = `CControl_Show`)
// общая плашка подсказки + владелец показа   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaInventorySubsystem.h"   // ★ общая точка модели контейнеров: ApplyExternalRecord (6582)
#include "UI/AsdaNotice.h"          // ★ 07.11.2026: окно-сообщение ONEBUT_MSGBOX (`CItem_BuildTooltipText(…, 0, -1, …)`)
#include "UI/AsdaUiHelpers.h"
#include "Components/AudioComponent.h"
#include "HAL/IConsoleManager.h"   // ★ 08.11.2026: ключ asda.Gacha.IdleLoop2D
#include "UI/AsdaJuiComboBox.h"   // ★ ОБЩАЯ точка: выпадающий список скина Bk_ComboBox
#include "UI/AsdaJuiWindowMeta.h"   // мета окон: ручка перетаскивания живёт в данных
#include "Framework/Application/SlateApplication.h"   // курсор для зоны колеса над окном 68310
#include "GameFramework/PlayerController.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaCoord.h"   // ★ единый перевод UE→игра: тайл входа в сцену (шаг 9)
#include "UI/AsdaCraftSubsystem.h"         // ★ гейт входа: открыты ли окна ремесла 76000/77500 (волна В2)
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б
#include "AsdaPremiumGachaSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPremiumGachaSubsystem*.cpp

namespace {
	// Скин `WkBtn_W100` (JuiSkin.ini): SS0 обычная, SS1 наведение, SS2 нажата — различаются ТОЛЬКО
	//   Diffuse (UV у всех трёх одинаков), плюс у SS2 подпись сдвинута на +1 px по Y.
	//   Значения из данных, не подобраны: FFF0F0F0 / FFFFFFFF / FFC0C0C0.
	const FLinearColor kWkBtnNormal (240.f / 255.f, 240.f / 255.f, 240.f / 255.f, 1.f);
	const FLinearColor kWkBtnHover  (1.f, 1.f, 1.f, 1.f);
	const FLinearColor kWkBtnPressed(192.f / 255.f, 192.f / 255.f, 192.f / 255.f, 1.f);

	constexpr int32 kLocNowGxp        = 12904;   // 68201 и 68102 «Сейчас GXP» (world_map.c:4590, :4610)
	constexpr int32 kLocGiftMoney     = 12959;   // 68203 «Деньги в подарок» (world_map.c:4615)
	// ── СТАТИЧЕСКИЕ ПОДПИСИ СЦЕНЫ (`premiumGatya_InitWindow@0xB6CB50`, world_map.c:4563-4667) ──
	//   Полный список из 17 вызовов `CControl_SetText`/`CWidget_SetTextAll4` разобран в
	//   `Reports/gacha_modal_texts.md` §1. Ниже — те 15, которых в порту не было: без них
	//   TextBlock'и остаются пустыми (импортёр их такими и создаёт), и окна выглядят «без надписей».
	constexpr int32 kLocClose         = 5035;    // 68076 и 68357 «Закрыть»            (:4570, :4636)
	constexpr int32 kLocOk            = 2288;    // 68452 и 68511 «ОК»                 (:4644, :4666)
	constexpr int32 kLocSpendConfirm  = 110622;  // 68101 «GXP будут потрачены. Продолжить?» (:4587)
	constexpr int32 kLocPrice         = 110661;  // 68104 «Стоимость»                  (:4594)
	constexpr int32 kLocRest          = 110662;  // 68106 «Остаток»                    (:4598)
	constexpr int32 kLocPlay          = 110663;  // 68108 «Играть»                     (:4602)
	constexpr int32 kLocCancel        = 109807;  // 68109 «Отмена»                     (:4605)
	constexpr int32 kLocSetInfo       = 110660;  // 68301 «Информация о наборе»        (:4626)
	constexpr int32 kLocBindWarn      = 8400;    // 68503 «'Если ты это наденешь…'»    (:4652)
	constexpr int32 kLocJobChosen     = 12936;   // 68504 «Выбрана профессия»          (:4655)
	constexpr int32 kLocLevelChosen   = 12937;   // 68505 «Выбран уровень»             (:4659)
	constexpr int32 kLocNoReturn      = 6961;    // 68506 «[Предмет нельзя вернуть…]»  (:4663)
}

void UAsdaPremiumGachaSubsystem::SetupControls()
{
	// ★ Имена сняты ДАМПОМ реальных WBP (Tools/asda_ui_dump_window.py), а не угаданы.
	//   Правило проекта [[jui-importer-widget-naming]]: точное имя есть только у интерактива и
	//   текста, планы/картинки получают idx-суффикс — их ищем по ПРЕФИКСУ.
	//   Соответствие узлов окна 68000 (131 узел):
	//     Premium_PickBAR_Win_1024window* — контейнер 1024-окна = контрол 68056 (сюда идёт 3D)
	//     Premium_PickBAR_back_1024*      — фон под 3D (68059)
	//     Premium_PickBAR_ballB*          — «золотой шар» 68057 (по нему кликают)
	//     Premium_PickBAR_gold_b_ani*     — вспышка 68058, это AsdaAnimatedPlane (кадровый анимплан)
	//     Premium_PickBAR_N_BN            — кнопка «крутить» N-го автомата (N = 1..5)
	//     Premium_PickBAR_daybtn          — кнопка выхода из сцены
	// ★★★ ИСКАТЬ УЗЕЛ НУЖНОГО КЛАССА, А НЕ ПРОСТО «ПЕРВЫЙ С ПРЕФИКСОМ» (закон `false-negatives-method`:
	//   «нашлось» ≠ «нашлось то самое»). Импортёр кладёт рядом с планом ОДНОИМЁННЫЙ `UTextBlock`
	//   (подпись контрола) и группу-канвас `…_grp`, и все трое подходят под один префикс:
	//     `Premium_PickBAR_ballB_grp` (CanvasPanel) · `Premium_PickBAR_ballB_74_0` (Image) ·
	//     `Premium_PickBAR_ballB` (TextBlock)  — дамп WBP, `Reports/headless_gacha_dump2.log`.
	//   Какой достанется поиску «по префиксу» — решает порядок обхода дерева, то есть случай.
	auto FindTyped = [](UUserWidget* Win, const TCHAR* Prefix, UClass* Cls) -> UWidget*
	{
		if (!Win || !Win->WidgetTree || !Cls) { return nullptr; }
		UWidget* Found = nullptr;
		Win->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			if (!Found && W && W->IsA(Cls) && W->GetName().StartsWith(Prefix)) { Found = W; }
		});
		return Found;
	};
	// План-НЕ-картинка (напр. кадровый `AsdaAnimatedPlane` вспышки 68058): отсекаем подпись и группу,
	// не заводя зависимости на класс анимплана.
	auto FindPlaneNode = [](UUserWidget* Win, const TCHAR* Prefix) -> UWidget*
	{
		if (!Win || !Win->WidgetTree) { return nullptr; }
		UWidget* Found = nullptr;
		Win->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			if (Found || !W || !W->GetName().StartsWith(Prefix)) { return; }
			if (W->IsA(UTextBlock::StaticClass()) || W->IsA(UPanelWidget::StaticClass())) { return; }
			Found = W;
		});
		return Found;
	};
	Ball      = FindTyped(RootWin, TEXT("Premium_PickBAR_ballB"), UImage::StaticClass());
	BallGroup = RootWin ? RootWin->GetWidgetFromName(TEXT("Premium_PickBAR_ballB_grp")) : nullptr;
	BallBang  = FindPlaneNode(RootWin, TEXT("Premium_PickBAR_gold_b_ani"));
	SceneHost = FindTyped(RootWin, TEXT("Premium_PickBAR_Win_1024window"), UImage::StaticClass());
	SceneBack = Cast<UImage>(FindTyped(RootWin, TEXT("Premium_PickBAR_back_1024"), UImage::StaticClass()));
	// ★★★ ИСПРАВЛЕНО. 68055 `Premium_PickBAR_Gtxt1` — подсказка «нажми на шар» (разбор в заголовке).
	//   Раньше сюда привязывался `Premium_PickBAR_CHA_G1` — это ДРУГОЙ контрол, **68071**
	//   (иконка 32×32 декоративного блока персонажа 68070, у которого в эталоне обработчика нет),
	//   и в него же писался баланс, которого 68055 не показывает никогда. Из-за подмены подсказка
	//   не показывалась вовсе, а клик по шару в эталонном гейте не мог пройти.
	//   Имя плана снято дампом WBP: `Premium_PickBAR_Gtxt1_73_0` (Image, 385×38, род=RootCanvas) —
	//   `Reports/headless_gacha_dump2.log`.
	HintPlane = FindPlaneNode(RootWin, TEXT("Premium_PickBAR_Gtxt1"));

	// ★★★ ПОЛОСЫ СЛОТОВ 68010+10i. В `.JUI` контрол 68010 лежит в (0,0) размером 76×62, а его дети
	//   (кнопка витрины `_B1`, «крутить» `Mac_Btn_2`, купон, подписи) — вокруг него, часть с
	//   ОТРИЦАТЕЛЬНЫМ смещением (`GOLD_roullete_FONT3` Origin «-128 17»). Импортёр обернул всю
	//   пятёрку в группу-канвас `Premium_PickBAR_{i+1}_grp` размером по union детей (267×148) и
	//   положил САМ контрол внутрь со сдвигом (128, 0). Оригинал двигает контрол — значит в UMG
	//   двигать надо ГРУППУ, а целевую точку пересчитывать через локальное смещение контрола.
	SlotBars.Reset();
	SlotBarInner.Reset();
	for (int32 i = 0; i < 5; ++i)
	{
		UWidget* Grp = RootWin ? RootWin->GetWidgetFromName(
			FName(*FString::Printf(TEXT("Premium_PickBAR_%d_grp"), i + 1))) : nullptr;
		SlotBars.Add(Grp);
		// Сам контрол-полоса = ПЕРВАЯ картинка внутри группы (по дампу это `Premium_PickBAR_{i+1}_{juiIdx}_0`).
		UWidget* Inner = nullptr;
		if (UPanelWidget* P = Cast<UPanelWidget>(Grp))
		{
			for (int32 c = 0; c < P->GetChildrenCount() && !Inner; ++c)
			{
				if (UImage* Img = Cast<UImage>(P->GetChildAt(c))) { Inner = Img; }
			}
		}
		SlotBarInner.Add(Inner);
	}

	// ★★★ ПРИЧИНА «У ПОЛОС 3, 4, 5 — ПУСТЫЕ ТЁМНЫЕ ПЛАШКИ БЕЗ ВЫВЕСКИ И КНОПОК».
	//   Найдена в ДАННЫХ, не подбором. В `gamemain_jui.ini` у полос стоит РАЗНЫЙ `Visible`:
	//       68010 `Premium_PickBAR_1` — Visible = "1"      68040 `Premium_PickBAR_4` — Visible = "0"
	//       68020 `Premium_PickBAR_2` — Visible = "1"      68050 `Premium_PickBAR_5` — Visible = "0"
	//       68030 `Premium_PickBAR_3` — Visible = "0"
	//   (в оригинале данных хватало на один-два автомата, поэтому лишние полосы лежали скрытыми —
	//   это ЗАГОТОВКА, а не запрет: раскладка `deathRespawnUI_LayoutResurrectPanel@0xB6E480`
	//   сама гасит все пять и показывает первые N через `CControl_SetVisible(bar, 1)`.)
	//   Импортёр (`Tools/asda_jui_generic_umg.py:669` `eff_hidden`) протаскивает `Visible = 0`
	//   РЕКУРСИЕЙ: `Collapsed` получает и union-группа полосы, и КАЖДЫЙ её потомок — вывеска
	//   `GOLD_roullete_FONT3` с планом цифры, торцы `TBAR_L/R`, подложка `PICK_BACK`, кнопка
	//   витрины 68031/68041/68051 и кнопка «крутить» 68063/68064/68065.
	//   ★ Собственный визуал узла-с-детьми импортёр НЕ сворачивает (см. ветку `if n.children:` —
	//   `COLLAPSED` ставится только контейнеру `cont`), поэтому у полос 3…5 оставался ровно ОДИН
	//   видимый элемент — сама планка `GOLD_roullete_TBAR_Middle`. Это и есть «пустая тёмная
	//   плашка», которую видел владелец.
	//   ЧЕГО НЕ ХВАТАЛО ПОРТУ: `LayoutByProjection` разворачивала только САМУ группу полосы, а
	//   потомки оставались `Collapsed` — у свёрнутого узла нет ни рисования, ни мыши.
	//   ЧИНИМ ПО ЭТАЛОНУ, А НЕ «ДОРИСОВЫВАЕМ»: приводим поддерево каждой полосы к состоянию
	//   ПЕРВОЙ полосы (68010) — единственной, которую импортёр не сворачивал, то есть к состоянию,
	//   пришедшему из `.JUI` без искажения. Так у всех пяти одинаково включается вывеска с цифрой
	//   и обе кнопки и одинаково остаётся скрытым купон 68077+i (`Visible = "0"` — его СОБСТВЕННЫЙ
	//   флаг, он скрыт и у полосы 1). Ничего не выдумано: источник состояния — сам оригинал.
	//   Делать это надо ДО создания оверлеев «крутить»: они добавляют детей и сломали бы
	//   поструктурное сопоставление.
	if (SlotBars.Num() == 5 && SlotBars[0])
	{
		int32 TotalChanged = 0, TotalMismatch = 0;
		FString Before;
		for (int32 i = 0; i < 5; ++i)
		{
			Before += FString::Printf(TEXT("%s%d:%d"), i ? TEXT(" ") : TEXT(""), i + 1,
				CountCollapsed(SlotBars[i]));
		}
		for (int32 i = 1; i < 5; ++i)
		{
			if (!SlotBars[i]) { continue; }
			int32 Changed = 0, Mismatch = 0;
			MirrorSubtreeVisibility(SlotBars[0], SlotBars[i], Changed, Mismatch);
			TotalChanged += Changed;
			TotalMismatch += Mismatch;
			if (Mismatch > 0)
			{
				UE_LOG(LogAsdaGacha, Error,
					TEXT("[премиум-гача] полоса %d: структура разошлась с полосой 1 в %d узлах — ")
					TEXT("часть контролов могла остаться свёрнутой"), i + 1, Mismatch);
			}
		}
		FString After;
		for (int32 i = 0; i < 5; ++i)
		{
			After += FString::Printf(TEXT("%s%d:%d"), i ? TEXT(" ") : TEXT(""), i + 1,
				CountCollapsed(SlotBars[i]));
		}
		// Доказательство в логе: до правки у полос 3…5 свёрнуто по десятку узлов, после —
		// столько же, сколько у полосы 1 (то есть только купон).
		UE_LOG(LogAsdaGacha, Log,
			TEXT("[премиум-гача] видимость полос выровнена по полосе 1: свёрнутых узлов было [%s], ")
			TEXT("стало [%s]; изменено %d, расхождений структуры %d"),
			*Before, *After, TotalChanged, TotalMismatch);
	}
	else
	{
		UE_LOG(LogAsdaGacha, Error,
			TEXT("[премиум-гача] полос слотов найдено %d вместо 5 — выровнять видимость не по чему"),
			SlotBars.Num());
	}

	Relays.Reset();

	// ★ ИСПРАВЛЕНО ПО ДАМПУ + ЭТАЛОНУ. Раньше `Premium_PickBAR_N_BN` считались кнопкой «крутить»;
	//   в эталоне (`GachaScene_OnControlMessage@0xB704C0`, msg 0x10401) это РАЗНЫЕ кнопки:
	//     a2 == 10·i + 68011  (idc `Premium_PickBAR_{i+1}_B{i+1}`) → `openItemGridIfReady(this, i)`
	//                                                                = ОТКРЫТЬ ВИТРИНУ ПРИЗОВ 68300;
	//     a2 == i + 68061     (у контрола `ID` ПУСТОЙ, скин `Mac_Btn_2`)  → `uiTradeConfirmMoney(this, i)`
	//                                                                = диалог оплаты 68100 («крутить»).
	//   Поэтому 68061+i биндится оверлеем над планом `Mac_Btn_2_{juiIdx}_0` (кнопки импортёр не сделал —
	//   у контрола нет `idc`, закон `jui-importer-widget-naming`).
	ViewButtons.Reset();
	SpinButtons.Reset();
	SlotDigits.Reset();
	for (int32 i = 0; i < 5; ++i)
	{
		const FString BtnName = FString::Printf(TEXT("Premium_PickBAR_%d_B%d"), i + 1, i + 1);
		UButton* B = RootWin ? Cast<UButton>(RootWin->GetWidgetFromName(*BtnName)) : nullptr;
		ViewButtons.Add(B);
		if (B)
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Callback = [this, i](int32) { OpenPrizeGrid(i); };
			B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(Relays, R, B);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		}
		// juiIdx планов `Mac_Btn_2` у пяти групп: 13, 27, 41, 55, 69 (шаг 14) — из дампа WBP
		//   (`Reports/headless_dump_gacha_bars.log`: род = `Mac_Btn_2_grp`…`Mac_Btn_2_grp_4`).
		static const int32 kSpinPlaneIdx[5] = { 13, 27, 41, 55, 69 };
		const FString SpinPlane = FString::Printf(TEXT("Mac_Btn_2_%d_0"), kSpinPlaneIdx[i]);
		UButton* Spin = nullptr;
		if (UWidget* Plane = RootWin ? RootWin->GetWidgetFromName(*SpinPlane) : nullptr)
		{
			Spin = AddPlaneButton(RootWin, Plane, *FString::Printf(TEXT("GACHA_SPIN_%d"), i),
				[this, i](int32) { OnSpinButton(i); });   // ★ 05.11.2026: гейт смены автомата — в `OnSpinButton`
		}
		SpinButtons.Add(Spin);
		// План-ЦИФРА автомата: `GOLD_roullete_FONT3_{i+1}` + idx-суффикс импортёра. Ищем по префиксу
		//   С РАЗДЕЛИТЕЛЕМ «_», иначе `GOLD_roullete_FONT3_1` поймал бы и `…_1_7_0`, и любой другой.
		//   Класс проверяем (закон `false-negatives-method`): рядом лежит одноимённая подпись-текст.
		UImage* Digit = nullptr;
		if (RootWin && RootWin->WidgetTree)
		{
			const FString Pfx = FString::Printf(TEXT("GOLD_roullete_FONT3_%d_"), i + 1);
			RootWin->WidgetTree->ForEachWidget([&Digit, &Pfx](UWidget* W)
			{
				if (!Digit && W && W->IsA(UImage::StaticClass()) && W->GetName().StartsWith(Pfx))
				{
					Digit = Cast<UImage>(W);
				}
			});
		}
		SlotDigits.Add(Digit);
	}

	// ★ Кнопка выхода из сцены — 68076 `Premium_PickBAR_daybtn_X_btn` и живёт она в ОКНЕ 68075,
	//   а не в 68000. Прежняя привязка шла на `Premium_PickBAR_daybtn` = 68074 — это подпись срока
	//   акции («10월21일부터~»), у которой в эталоне обработчика НЕТ вовсе (`Reports/gacha_ui_re.md`
	//   §2.1). Закрытие вешаем на настоящую 68076 (`GachaScene_OnControlMessage`: `a2 == 68076`
	//   → `inventoryBag_Close`).
	if (UButton* Exit = ExitWin ? Cast<UButton>(ExitWin->GetWidgetFromName(TEXT("Premium_PickBAR_daybtn_X_btn"))) : nullptr)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { RequestCloseScene(); };   // ★ 07.11.2026: запрет до забора приза
		Exit->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, Exit);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	// Подпись 68076 ← loc 5035 «Закрыть» (`world_map.c:4568-4570`). В `.ini` там корейская
	//   заглушка «나가기~» (импортёр перевёл её как «Выйти») — эталон её перезаписывает.
	SetLocText(ExitWin, TEXT("Premium_PickBAR_daybtn_X_btn_Text"), kLocClose);

	// ═══ КЛИК ПО «ЗОЛОТОМУ ШАРУ» 68057 ═══
	//   У контрола скин `NoImage_icon` (служебный прозрачный пиксель атласа) и ПУСТОЙ `ID`, поэтому
	//   импортёр кнопки не сделал, а сам план ничего не рисует: в оригинале 68057 — это ЗОНА 128×128,
	//   которую покадровая раскладка сажает на проекцию кости `Bone19`, то есть на 3D-шар. Кликают
	//   именно по ней (`GachaScene_OnControlMessage`, msg 0xB, `a2 == 68057`). Ставим оверлей.
	//   ★★★ ПРИЧИНА «КЛИК ПО ШАРУ НЕ РАБОТАЕТ» (найдена по данным импортёра, а не подбором):
	//     в `gamemain_jui.ini` у 68057 `Visible = 0`, а импортёр такие узлы помечает `Collapsed`
	//     ВМЕСТЕ С union-группой и всеми детьми (`asda_jui_generic_umg.py:669-706`, `eff_hidden`).
	//     Группа `Premium_PickBAR_ballB_grp` так и оставалась свёрнутой, а у свёрнутого родителя
	//     ребёнок не рисуется и НЕ ЛОВИТ МЫШЬ — сколько бы `SetVis(Ball, true)` мы ни звали.
	//     Разворачиваем ветку один раз при привязке (закон `ui-hierarchy-helper`), дальше видимостью
	//     управляют фазы. Ровно та же грабля была у подложки 499 самоцветов и у стрелок дерева навыков.
	ExpandBranch(BallGroup);
	if (BallGroup) { BallGroup->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	ExpandBranch(HintPlane);
	BallHit = nullptr;
	if (Ball)
	{
		// ★ Кнопку берём ВОЗВРАЩЁННУЮ: `WidgetTree->ConstructWidget` при занятом имени молча даёт
		//   `GACHA_BALL_HIT_1`, и прежний поиск `GetWidgetFromName("GACHA_BALL_HIT")` мог вернуть
		//   nullptr — тогда зона не ездила за шаром и не показывалась вовсе.
		BallHit = AddPlaneButton(RootWin, Ball, TEXT("GACHA_BALL_HIT"), [this](int32) { ClickBall(); });
	}

	SetupSelectWinControls();
	SetupGridWinControls();
	SetupMsgWinControls();
	SetupClickWinControls();
	SetupVariantWinControls();

	int32 BarsFound = 0;
	for (int32 i = 0; i < SlotBars.Num(); ++i) { if (SlotBars[i] && SlotBarInner[i]) { ++BarsFound; } }
	UE_LOG(LogAsdaGacha, Log,
		TEXT("[премиум-гача] контролы: шар=%s зона=%s подсказка=%s вспышка=%s 3D-контейнер=%s ")
		TEXT("подложка=%s полос=%d/5 обработчиков=%d"),
		Ball ? TEXT("да") : TEXT("НЕТ"), BallHit ? TEXT("да") : TEXT("НЕТ"),
		HintPlane ? TEXT("да") : TEXT("НЕТ"), BallBang ? TEXT("да") : TEXT("НЕТ"),
		SceneHost ? TEXT("да") : TEXT("НЕТ"), SceneBack ? TEXT("да") : TEXT("НЕТ"),
		BarsFound, Relays.Num());
}

// ★★★ Развернуть ветку импортёра (разбор — в заголовке). Идём ВВЕРХ по родителям и снимаем
//   `Collapsed`/`Hidden`; родителям даём `SelfHitTestInvisible` — они всего лишь union-группы и
//   мышь перехватывать не должны (закон `ui-button-hover-click-rule`: клик ловят дети-кнопки).
void UAsdaPremiumGachaSubsystem::ExpandBranch(UWidget* W)
{
	for (UWidget* Cur = W ? W->GetParent() : nullptr; Cur; Cur = Cur->GetParent())
	{
		const ESlateVisibility V = Cur->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden)
		{
			Cur->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
}

// ★★★ Скопировать состояние видимости ПОДДЕРЕВА эталонной полосы на другую полосу.
//   Полосы 68010…68050 в `gamemain_jui.ini` — пять ОДИНАКОВЫХ по структуре поддеревьев (различаются
//   только `ID`, `IDNum` и номером плана-цифры `GOLD_roullete_FONT3_{1..5}`), поэтому импортёр
//   строит их одним и тем же обходом и порядок детей у всех пяти совпадает. Значит соответствие
//   узлов задаётся ПУТЁМ ИНДЕКСОВ от корня группы — имена для этого не годятся: `uniq_name`
//   выдаёт планам разные idx-суффиксы (`Mac_Btn_2_13_0` против `Mac_Btn_2_41_0`).
//   Сопоставление проверяется классом узла: расхождение — не молчаливый пропуск, а счётчик,
//   который вызывающая сторона печатает ошибкой (закон `false-negatives-method`).
int32 UAsdaPremiumGachaSubsystem::MirrorSubtreeVisibility(UWidget* Src, UWidget* Dst,
	int32& OutChanged, int32& OutMismatch)
{
	UPanelWidget* SP = Cast<UPanelWidget>(Src);
	UPanelWidget* DP = Cast<UPanelWidget>(Dst);
	if (!SP || !DP) { return 0; }
	const int32 SN = SP->GetChildrenCount();
	const int32 DN = DP->GetChildrenCount();
	if (SN != DN) { OutMismatch += FMath::Abs(SN - DN); }
	int32 Nodes = 0;
	for (int32 i = 0; i < FMath::Min(SN, DN); ++i)
	{
		UWidget* SC = SP->GetChildAt(i);
		UWidget* DC = DP->GetChildAt(i);
		if (!SC || !DC) { continue; }
		if (SC->GetClass() != DC->GetClass()) { ++OutMismatch; continue; }
		if (DC->GetVisibility() != SC->GetVisibility())
		{
			DC->SetVisibility(SC->GetVisibility());
			++OutChanged;
		}
		++Nodes;
		Nodes += MirrorSubtreeVisibility(SC, DC, OutChanged, OutMismatch);
	}
	return Nodes;
}

// Сколько узлов поддерева спрятано (`Collapsed`/`Hidden`) — мера «полоса пустая», печатается
// в лог до и после выравнивания как ДОКАЗАТЕЛЬСТВО, а не как «на глаз».
int32 UAsdaPremiumGachaSubsystem::CountCollapsed(UWidget* Root)
{
	int32 N = 0;
	if (UPanelWidget* P = Cast<UPanelWidget>(Root))
	{
		for (int32 i = 0; i < P->GetChildrenCount(); ++i)
		{
			UWidget* C = P->GetChildAt(i);
			if (!C) { continue; }
			const ESlateVisibility V = C->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { ++N; }
			N += CountCollapsed(C);
		}
	}
	return N;
}

// ═══════════════ ПРОЗРАЧНАЯ КЛИК-КНОПКА ПОВЕРХ ПЛАНА-КАРТИНКИ ═══════════════
//   ЗАЧЕМ. Часть PushButton-контролов гачи объявлена в `gamemain_jui.ini` с ПУСТЫМ `ID`
//   (68061…68065 «крутить» и шар 68057; 68357 «Закрыть» и 68511 «ОК» ушли отсюда 2026-08-27 —
//   они статичны и живут узлами В АССЕТЕ). Импортёр делает `UButton`
//   только для контролов с непустым `idc` (`asda_jui_generic_umg.py`, `is_btn1 = bool(node.idc) …`),
//   поэтому в WBP от них остались одни `UImage`-планы, а кликать не по чему.
//   ★ ЗАКОН `ui-button-hover-click-rule`: кнопка обязана иметь состояния наведения и нажатия.
//   Берём их ИЗ ДАННЫХ скина: у `WkBtn_W100`/`Mac_Btn`/`Mac_Btn_2` кадры SS0/SS1/SS2 отличаются
//   только `Diffuse` (UV совпадают), поэтому состояние = смена тинта САМОГО плана, а не своя графика
//   (закон `no-invented-ui` + `ui-progressbar-animate-existing`: анимируем существующий виджет).
UButton* UAsdaPremiumGachaSubsystem::AddPlaneButton(UUserWidget* Win, UWidget* Plane,
	const TCHAR* Name, TFunction<void(int32)> OnClick)
{
	if (!Win || !Win->WidgetTree || !Plane) { return nullptr; }
	UCanvasPanelSlot* PS = AsdaUi::SlotOf(Plane);
	UPanelWidget* Parent = Plane->GetParent();
	if (!PS || !Parent) { return nullptr; }

	UButton* Btn = Win->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
	FButtonStyle St = Btn->GetStyle();
	St.Normal.TintColor = St.Hovered.TintColor = St.Pressed.TintColor = FSlateColor(FLinearColor(1, 1, 1, 0));
	Btn->SetStyle(St);
	if (UCanvasPanelSlot* NS = Cast<UCanvasPanelSlot>(Parent->AddChild(Btn)))
	{
		NS->SetAnchors(PS->GetAnchors());
		NS->SetOffsets(PS->GetOffsets());
		NS->SetAlignment(PS->GetAlignment());
		NS->SetZOrder(PS->GetZOrder() + 5);
	}

	UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
	R->Callback = MoveTemp(OnClick);
	// Состояния кадрами скина: наведение — SS1, нажатие — SS2, уход/отпускание — SS0.
	TWeakObjectPtr<UWidget> WeakPlane(Plane);
	auto Tint = [WeakPlane](const FLinearColor& C)
	{
		if (UImage* Img = Cast<UImage>(WeakPlane.Get())) { Img->SetColorAndOpacity(C); }
	};
	R->HoverCallback   = [Tint]() { Tint(kWkBtnHover); };
	R->UnhoverCallback = [Tint]() { Tint(kWkBtnNormal); };
	R->PressCallback   = [Tint](int32, bool bPressed) { Tint(bPressed ? kWkBtnPressed : kWkBtnHover); };
	Btn->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
	Btn->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
	Btn->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
	Btn->OnPressed.AddDynamic(R, &UAsdaButtonRelay::OnPressed);
	Btn->OnReleased.AddDynamic(R, &UAsdaButtonRelay::OnReleased);
	AsdaUi::AnchorRelay(Relays, R, Btn);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	return Btn;
}

// ═══════════════ СТАТИЧЕСКАЯ ПОДПИСЬ ИЗ LOC-ТАБЛИЦЫ (разбор — в заголовке) ═══════════════
//   Эталонная пара: `v = UI_FormatLocalizedNotice(g_pLocNoticeCtx, id, 0, 1); CControl_SetText(ctrl, v)`
//   (у кнопок — `CWidget_SetTextAll4`, то есть та же строка на все 4 состояния; в UMG состояние
//   рисует стиль кнопки, а подпись одна, поэтому вызов один и тот же).
//   Строка берётся ТОЛЬКО из таблицы игры (`UAsdaTextSubsystem::GetTextById` → DT_GameText),
//   литералов здесь нет и быть не может (закон `no-invented-ui`).
bool UAsdaPremiumGachaSubsystem::SetLocText(UUserWidget* Win, const TCHAR* WidgetName, int32 LocId)
{
	if (!Win || !WidgetName) { return false; }
	UTextBlock* Tb = Cast<UTextBlock>(Win->GetWidgetFromName(FName(WidgetName)));
	if (!Tb)
	{
		UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] loc: виджет %s не найден (окно %s, id %d)"),
			WidgetName, *Win->GetName(), LocId);
		return false;
	}
	UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const FString S = Txt ? Txt->GetTextById(LocId) : FString();
	if (S.IsEmpty())
	{
		UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] loc: строка %d пуста — подпись %s осталась бы пустой"),
			LocId, WidgetName);
		return false;
	}
	Tb->SetText(FText::FromString(S));
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] loc: %s ← %d «%s»"), WidgetName, LocId, *S);
	return true;
}

// ═══════════════ 68100 `Premium_Pick_select` — диалог подтверждения ═══════════════
void UAsdaPremiumGachaSubsystem::SetupSelectWinControls()
{
	if (!SelectWin) { return; }
	// ★ Имена сняты ДАМПОМ WBP: текстовые контролы называются со СТРОЧНОЙ `t`
	//   (`Premium_Pick_select_t3/_t5/_t7`), как и `ID` в `gamemain_jui.ini`. Прежние `_T3/_T5/_T7`
	//   не находились вовсе — цены в диалоге не заполнялись.
	TotalText = Cast<UTextBlock>(SelectWin->GetWidgetFromName(TEXT("Premium_Pick_select_t3")));  // 68103 всего
	PriceText = Cast<UTextBlock>(SelectWin->GetWidgetFromName(TEXT("Premium_Pick_select_t5")));  // 68105 цена
	RestText  = Cast<UTextBlock>(SelectWin->GetWidgetFromName(TEXT("Premium_Pick_select_t7")));  // 68107 остаток

	// 68108 «Играть» (loc 110663) → `premiumGatya_CheckGoldAndSend@0xB70350`; 68109 «Отмена» (109807).
	if (UButton* Ok = Cast<UButton>(SelectWin->GetWidgetFromName(TEXT("Premium_Pick_select_ok"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { ConfirmRoll(); };
		Ok->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, Ok);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	if (UButton* No = Cast<UButton>(SelectWin->GetWidgetFromName(TEXT("Premium_Pick_select_no"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { CancelRoll(); };
		No->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, No);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	// ★★★ ПОДПИСИ И ЗАГОЛОВОК ОКНА — ИХ НЕ БЫЛО ВОВСЕ (`Reports/gacha_modal_texts.md`).
	//   Симптом владельца: «нет тайтла модалки и надписей "Текущая валюта"/"сколько нужно"» —
	//   в окне стояли ТРИ ГОЛЫХ ЧИСЛА (68103/68105/68107 заполняет `RefreshConfirmDialogTexts`),
	//   а подписи слева от них и сообщение сверху оставались пустыми.
	//   Причина НЕ в импорте: виджеты в WBP есть и не свёрнуты (дамп `headless_gacha_dump.log:1986-2005`,
	//   48 узлов, `Premium_Pick_select_t1/t2/t4/t6` = текст«»). Импортёр по замыслу оставляет
	//   Text-контролы пустыми, а текст обязан поставить код — как `premiumGatya_InitWindow`:
	//     68101 ← 110622, 68102 ← 12904, 68104 ← 110661, 68106 ← 110662 (world_map.c:4586-4600).
	SetLocText(SelectWin, TEXT("Premium_Pick_select_t1"), kLocSpendConfirm);  // 68101 сообщение-заголовок
	SetLocText(SelectWin, TEXT("Premium_Pick_select_t2"), kLocNowGxp);        // 68102 «Сейчас GXP»
	SetLocText(SelectWin, TEXT("Premium_Pick_select_t4"), kLocPrice);         // 68104 «Стоимость»
	SetLocText(SelectWin, TEXT("Premium_Pick_select_t6"), kLocRest);          // 68106 «Остаток»

	// ★★★ ОБРЕЗАННАЯ ПОДПИСЬ ЛЕВОЙ КНОПКИ — ТОТ ЖЕ КОРЕНЬ, а не отдельная беда раскладки.
	//   В `gamemain_jui.ini` у 68108 `CaptionText = "상품뽑기~"` — ДИЗАЙН-ЗАГЛУШКА, которую импортёр
	//   статически перевёл в «Розыгрыш товара». Она шире поля подписи (97 px) и вылезает влево,
	//   под иконку-галочку `WkBtn_IconCheck_5_0`: та добавлена в канву ПОСЛЕ текста, значит рисуется
	//   поверх и съедает левые ~16 px — отсюда «зыгрыш товара».
	//   Эталон (`world_map.c:4601-4606`) заглушку ПЕРЕЗАПИСЫВАЕТ: 68108 ← loc 110663 «Играть»,
	//   68109 ← loc 109807 «Отмена». Короткая строка встаёт по центру между иконкой и правым краем —
	//   переполнения нет. Правим ПРИЧИНУ (не поставленный текст), а не следствие (ширину/клип).
	SetLocText(SelectWin, TEXT("Premium_Pick_select_ok_Text"), kLocPlay);     // 68108 «Играть»
	SetLocText(SelectWin, TEXT("Premium_Pick_select_no_Text"), kLocCancel);   // 68109 «Отмена»

	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] 68100: всего=%s цена=%s остаток=%s"),
		TotalText ? TEXT("да") : TEXT("НЕТ"), PriceText ? TEXT("да") : TEXT("НЕТ"),
		RestText ? TEXT("да") : TEXT("НЕТ"));
}

// ═══════════════ 68300 `Premium_Pick_View_Win` — витрина призов 5×5 ═══════════════
void UAsdaPremiumGachaSubsystem::SetupGridWinControls()
{
	if (!ViewWin) { return; }

	// 25 ячеек 68311…68335. Иконки рисует ОБЩИЙ контроллер сетки `UAsdaIconCellGrid` — тот же,
	//   что у инвентаря/магазина/char-create (itemCode → item-таблица → ячейка атласа JUI).
	//   Своего рендера иконок здесь нет и быть не должно (закон `port-shared-points`).
	TArray<FName> Cells;
	Cells.Reserve(kGridCellCount);
	for (const TCHAR* N : kGridCells) { Cells.Add(FName(N)); }

	PrizeGrid = NewObject<UAsdaIconCellGrid>(this);
	PrizeGrid->SetupNamed(ViewWin, Cells, /*bMale*/ true,
		// Провайдер кода ячейки: эталон `uiRefreshItemGridPage` пропускает `5 * scrollPage` записей
		//   и дальше идёт подряд по списку призов выбранного слота.
		[this](int32 Cell) -> int32
		{
			const int32 Idx = GridScrollRow * kGridPageStep + Cell;
			return Prizes.IsValidIndex(Idx) ? Prizes[Idx] : 0;
		},
		// Клик по ячейке: в эталоне по ячейкам витрины отдельного обработчика нет, но есть оверлей
		//   выбора 68356 `Slot_AniSelect` (4 кадра / 300 мс) — его и переставляем на выбранную ячейку.
		[this](int32 Cell) { GridSelectedCell = Cell; RefreshPrizeGridPage(); });

	GridSelectMark = ViewWin->GetWidgetFromName(TEXT("Premium_Pick_View_click_69_0"));   // 68356
	SetVis(GridSelectMark, false);

	// 68359 крестик (`Premium_Pick_X`) — единственная кнопка витрины с непустым `idc`.
	if (UButton* X = Cast<UButton>(ViewWin->GetWidgetFromName(TEXT("Premium_Pick_X"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { CloseGridWindow(); };
		X->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, X);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	// 68357 «Закрыть» (loc 5035): `ID` у контрола пустой.
	//   ★★★ 2026-08-27. БЫЛО: `AddPlaneButton(ViewWin, "WkBtn_W100_74_0", "GACHA_VIEW_CLOSE", …)` —
	//     прозрачный оверлей поверх плана-картинки, состояния имитировались тинтом плана.
	//     ПОЧЕМУ УБРАНО: контрол СТАТИЧЕН — существование, класс (`PushButton`) и геометрия
	//     (100x23 в неподвижном всплывающем окне 68300) целиком заданы `gamemain_jui.ini`.
	//     ГДЕ ЖИВЁТ ТЕПЕРЬ: узел `Premium_Pick_View_Win_BTN00` в
	//     `Content/AsdaAssets/UI/Player_UI/Premium/WBP_Premium_Pick_View_Win` (шаг
	//     `Tools/asda_ui_missing_interactive.py`; кадры SS0/SS1/SS2 скина `WkBtn_W100` даёт сама
	//     кнопка, план погашен, подпись `WkBtn_W100_74_txt` оставлена НАД кнопкой — z = -1).
	if (UButton* CloseBtn = Cast<UButton>(ViewWin->GetWidgetFromName(TEXT("Premium_Pick_View_Win_BTN00"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { CloseGridWindow(); };
		CloseBtn->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, CloseBtn);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	else
	{
		UE_LOG(LogAsdaGacha, Error,
			TEXT("[премиум-гача] 68357: узла `Premium_Pick_View_Win_BTN00` нет в WBP — витрину нечем закрыть"));
	}

	// ★★ ПОДПИСИ ОКНА — тот же дефект, что в 68100: в WBP лежат ЗАГЛУШКИ из `CaptionText`
	//   («Состав набора (жирный)» и «Нажми»), а эталон ставит строки из loc-таблицы:
	//     68301 ← 110660 «Информация о наборе» (world_map.c:4625-4626),
	//     68357 ← 5035   «Закрыть»             (world_map.c:4635-4636).
	//   У 68357 `ID` пуст, поэтому подпись импортёр назвал по скину — `WkBtn_W100_74_txt`
	//   (дамп `headless_gacha_dump.log`, окно `WBP_Premium_Pick_View_Win`).
	SetLocText(ViewWin, TEXT("Premium_Pick_View_bb1_Text"), kLocSetInfo);   // 68301 таб «Информация о наборе»
	SetLocText(ViewWin, TEXT("WkBtn_W100_74_txt"),          kLocClose);     // 68357 «Закрыть»

	BuildGridScrollBar();
}

// ═══════════════ 68450 `Premium_Pick_mssageBOX` — показ выпавшего приза ═══════════════
void UAsdaPremiumGachaSubsystem::SetupMsgWinControls()
{
	if (!MsgWin) { return; }
	PrizeText = Cast<UTextBlock>(MsgWin->GetWidgetFromName(TEXT("Premium_Pick_mssageBOX_t")));   // 68451
	// 68453 `Premium_Pick_mssageBOX_mark` — контрол типа Text со скином `NoImage_icon`: импортёр
	//   сделал ему И план-картинку (`…_mark_8_0`), И `UTextBlock` с точным именем. Иконка = ПЛАН.
	PrizeIcon = Cast<UImage>(MsgWin->GetWidgetFromName(TEXT("Premium_Pick_mssageBOX_mark_8_0")));
	if (UButton* Ok = Cast<UButton>(MsgWin->GetWidgetFromName(TEXT("Premium_Pick_mssageBOX_ok"))))  // 68452
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { TakePrize(); };
		Ok->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, Ok);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	// 68452 «ОК» ← loc 2288 (`world_map.c:4643-4644`). В WBP у подписи лежала заглушка «Нажми»
	//   из `CaptionText` (корейское «누르기~»), а эталон её перезаписывает — как и у 68108.
	SetLocText(MsgWin, TEXT("Premium_Pick_mssageBOX_ok_Text"), kLocOk);

	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] 68450: текст=%s иконка=%s"),
		PrizeText ? TEXT("да") : TEXT("НЕТ"), PrizeIcon ? TEXT("да") : TEXT("НЕТ"));
}

// ═══════════════ 68200 `Premium_Pick_Click_Win` — итог/баланс и «пополнить» ═══════════════
void UAsdaPremiumGachaSubsystem::SetupClickWinControls()
{
	if (!ClickWin) { return; }
	ClickCashLabel    = Cast<UTextBlock>(ClickWin->GetWidgetFromName(TEXT("Premium_Pick_Click_Win_t1")));  // 68201
	ClickCashText     = Cast<UTextBlock>(ClickWin->GetWidgetFromName(TEXT("Premium_Pick_Click_Win_t2")));  // 68202
	ClickGiftLabel    = Cast<UTextBlock>(ClickWin->GetWidgetFromName(TEXT("Premium_Pick_Click_Win_t3")));  // 68203
	ClickGiftCashText = Cast<UTextBlock>(ClickWin->GetWidgetFromName(TEXT("Premium_Pick_Click_Win_t4")));  // 68204

	// ★★★ «ПОЧЕМУ ДВЕ ВАЛЮТЫ» — ТАК В ЭТАЛОНЕ. `premiumGatya_InitWindow` (world_map.c:4608-4617)
	//   ставит подписи ОБЕИМ строкам: 68201 = loc 12904 «Сейчас GXP», 68203 = loc 12959
	//   «Деньги в подарок». Значения (68202/68204) печатает `inventoryBag_RefreshGoldUi@0xB6DE60`
	//   (item_inventory.c:5398-5419) из ДВУХ РАЗНЫХ полей — см. `RefreshClickWinTexts`.
	//   Без подписей игрок видел два голых числа с иконкой `Cash_Icon` и читал их как «две валюты»;
	//   подписи в порту не ставились вовсе — это и было расхождение с оригиналом.
	//   ★ Ставим через ту же общую точку `SetLocText`, что и остальные 15 подписей сцены — иначе
	//     промах (нет виджета / пустая строка) снова остался бы НЕМЫМ.
	SetLocText(ClickWin, TEXT("Premium_Pick_Click_Win_t1"), kLocNowGxp);     // 68201
	SetLocText(ClickWin, TEXT("Premium_Pick_Click_Win_t3"), kLocGiftMoney);  // 68203

	// 68206 → `net_SendVerifiedRequest5466` (повторный запрос баланса).
	if (UButton* Ok = Cast<UButton>(ClickWin->GetWidgetFromName(TEXT("Premium_Pick_Click_Win_ok"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { RefreshBalanceButton(); };
		Ok->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, Ok);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	// 68205 → `UI_OpenDonatePage(g_pShopWnd)` (пополнить премиум-валюту).
	if (UButton* Donate = Cast<UButton>(ClickWin->GetWidgetFromName(TEXT("Premium_Pick_Click_Win_char"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { OpenDonatePage(); };
		Donate->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, Donate);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	// 68209 крестик → `inventoryBag_Close` = закрыть ВСЮ сцену (так в эталоне, не только это окно).
	if (UButton* X = Cast<UButton>(ClickWin->GetWidgetFromName(TEXT("Premium_Pick_Click_X"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { RequestCloseScene(); };   // ★ 07.11.2026: запрет до забора приза
		X->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, X);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
}

// ═══════════════ 68500 `Premium_Pick_select_2` — выбор варианта приза ═══════════════
void UAsdaPremiumGachaSubsystem::SetupVariantWinControls()
{
	if (!Select2Win) { return; }
	// ★ Кто есть кто (доказано раскладкой и кодом):
	//   SS1 = 68507, стоит НАПРОТИВ подписи T3 (loc 12936 «Выбрана профессия») — в эталоне это
	//     `wnd[104]`, и `uiItemFilterListPopulate@0xB70BB0` заполняет его значениями поля записи
	//     предмета +92 (UsageCategory), подписывая loc 11836/11837/11838/12596/12597/12598.
	//   SS2 = 68508, напротив T4 (loc 12937 «Выбран уровень») — `wnd[106]`, значения поля +80
	//     (RequiredLevel), подпись — само число (`sprintf(buf, "%d", subType)`).
	//   ★★★ ВИД СПИСКОВ — ОБЩАЯ ТОЧКА `UAsdaJuiComboBox` (замечания владельца №4 и №5).
	//   До этой правки здесь стояли сырые `UComboBoxString`, то есть ДВИЖКОВЫЙ белый дропдаун с
	//   чёрным текстом, а в подписи висела дизайнерская рыба `CaptionText = "ABC123"` из
	//   `gamemain_jui.ini`. Оригинальный вид даёт скин `Bk_ComboBox` + список `DarkGrey_List`;
	//   он собран ОДИН раз в `UAsdaJuiComboBox` (разбор скина — в шапке того заголовка), а не
	//   четвёртой копией рядом с чатом, картой мира и созвездием логина.
	//   Список раскрывается ВНИЗ: низ контролов 68507/68508 в базе 1024×768 — 64 и 91 от верха
	//   группы `Premium_Pick_n1` высотой 210, места под ним хватает.
	VarJobCombo   = NewObject<UAsdaJuiComboBox>(this);
	VarLevelCombo = NewObject<UAsdaJuiComboBox>(this);
	VarJobCombo->Setup(Select2Win, TEXT("Premium_Pick_select_2_SS1"), FName(kSel2.Id), /*bOpenUp*/ false);
	VarLevelCombo->Setup(Select2Win, TEXT("Premium_Pick_select_2_SS2"), FName(kSel2.Id), /*bOpenUp*/ false);
	VarIcon  = Cast<UImage>(Select2Win->GetWidgetFromName(TEXT("Premium_Pick_select_mark_24_0")));   // 68512
	VarTitle = Cast<UTextBlock>(Select2Win->GetWidgetFromName(TEXT("Premium_Pick_select_2_move")));  // 68501
	VarDesc  = Cast<UTextBlock>(Select2Win->GetWidgetFromName(TEXT("Premium_Pick_select_2_T1")));    // 68502

	// 68511 «ОК» (loc 2288): `ID` у контрола пустой.
	//   ★★★ 2026-08-27. БЫЛО: `AddPlaneButton(Select2Win, "WkBtn_W100_13_0", "GACHA_VAR_OK", …)`.
	//     ПОЧЕМУ УБРАНО: та же статика, что у 68357 — 100x23 в неподвижном окне 68500.
	//     ГДЕ ЖИВЁТ ТЕПЕРЬ: узел `Premium_Pick_n1_BTN00` в
	//     `Content/AsdaAssets/UI/Player_UI/Premium/WBP_Premium_Pick_select_2`; подпись
	//     `WkBtn_W100_13_txt` осталась над кнопкой (z = -1).
	if (UButton* Ok = Cast<UButton>(Select2Win->GetWidgetFromName(TEXT("Premium_Pick_n1_BTN00"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { ConfirmVariant(); };
		Ok->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, Ok);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	else
	{
		UE_LOG(LogAsdaGacha, Error,
			TEXT("[премиум-гача] 68511: узла `Premium_Pick_n1_BTN00` нет в WBP — вариант нечем подтвердить"));
	}

	// ★★ ЧЕТЫРЕ СТАТИЧЕСКИЕ ПОДПИСИ ОКНА + подпись кнопки — их тоже не было (все пустые в WBP,
	//   дамп `WBP_Premium_Pick_select_2`). Эталон `world_map.c:4650-4667`:
	//     68503 ← 8400  (предупреждение о привязке при надевании)  — виджет `_T2`
	//     68504 ← 12936 «Выбрана профессия»                        — виджет `_T3`
	//     68505 ← 12937 «Выбран уровень»                           — виджет `_T4`
	//     68506 ← 6961  «[Предмет нельзя вернуть или обменять]»    — виджет `_T5`
	//     68511 ← 2288  «ОК»                                       — подпись плана `WkBtn_W100_13_txt`
	//   ⚠ Имя виджета СДВИНУТО относительно IDNum на единицу (`_T2` = 68503, `_T3` = 68504 …) —
	//     так в `gamemain_jui.ini`; идти надо по IDNum, а не по имени (закон jui-importer-widget-naming).
	SetLocText(Select2Win, TEXT("Premium_Pick_select_2_T2"), kLocBindWarn);     // 68503
	SetLocText(Select2Win, TEXT("Premium_Pick_select_2_T3"), kLocJobChosen);    // 68504
	SetLocText(Select2Win, TEXT("Premium_Pick_select_2_T4"), kLocLevelChosen);  // 68505
	SetLocText(Select2Win, TEXT("Premium_Pick_select_2_T5"), kLocNoReturn);     // 68506
	SetLocText(Select2Win, TEXT("WkBtn_W100_13_txt"),        kLocOk);           // 68511
	// 68519 крестик: в эталоне обработчика по id НЕТ (`Reports/gacha_ui_re.md` §2.7) и сам он
	//   объявлен скрытым (Visible=0). Вешаем закрытие окна — иначе окно нечем закрыть, если вариант
	//   не подобрался (дефект оригинала, чиним по закону `fix-original-defects`).
	if (UButton* X = Cast<UButton>(Select2Win->GetWidgetFromName(TEXT("Premium_Pick_select_2_X"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		// ★ Закрытие КРЕСТИКОМ = выбор отменён (одна точка — `AbortVariantChoice`).
		R->Callback = [this](int32) { AbortVariantChoice(); };
		X->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		X->SetVisibility(ESlateVisibility::Visible);
		AsdaUi::AnchorRelay(Relays, R, X);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
}

// ★ Волна 1 (14.09.2026, В3): общая точка `AsdaUi::SetShownHit` — режим прежний (`Visible`: узел ловит мышь).
//   ⚠ Сверка по классу узлов (шары, вспышки, рамка выбора, подсказка — картинки/группы) показывает, что по
//   общему правилу им положен `SelfHitTestInvisible`; эталонного доказательства, что сцена гачи не держит
//   на них клики, нет — режим НЕ меняется, кандидат записан в отчёт волны 1.
void UAsdaPremiumGachaSubsystem::SetVis(UWidget* W, bool bVisible)
{
	AsdaUi::SetShownHit(W, bVisible);
}
