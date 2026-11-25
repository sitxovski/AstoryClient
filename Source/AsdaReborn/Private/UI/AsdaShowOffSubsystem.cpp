// Copyright (c) 2026 @sitxovski (Discord)
// AsdaShowOffSubsystem.cpp — «ХВАСТОВСТВО ПРЕДМЕТОМ»: меню 28000 + окно-приёмник 28100 + сеть 6129/6130.
//   Реверс, дамп контролов и разбор протокола — шапка `Public/UI/AsdaShowOffSubsystem.h` и
//   отчёт `Reports/showoff_system.md`. Ни одно число здесь не выдумано.
//
// ═══════════ ТАБЛИЦА КОНТРОЛОВ (gamemain_jui.ini + имена узлов WBP, снятые дампом) ═════════════
//  МЕНЮ `Open_Item_Win` — WBP_Open_Item_Win, 24 узла:
//   idnum  idc                скин            Origin   Size     узел WBP                    что делает
//   28000  Open_Item_Win      Popup_SmWin      75,132  158×100  Open_Item_Win_0_0…_0_8       9 планов рамки 3×3
//   28001  Open_Item_Move     NoImage           0,0    158×25   Open_Item_Move (+ _7_DRAG)   заголовок = нотис 102000,
//                                                                                            ручка EVS{6→0x20003}
//   28003  Open_Item_Btn00    SmWtBtn_W143      8,27   143×20   Open_Item_Btn00 + _Text      «Всем рядом» (102001) → 6129 target −1
//   28007  Open_Item_Btn04    SmWtBtn_W143      8,46   143×20   Open_Item_Btn04 + _Text      «Только выбранному» (102002) → 6129 target=цель
//   28005  Open_Item_Btn02    SmWtBtn_W143      8,65   143×20   Open_Item_Btn02 + _Text      Visible=0, обработчика НЕТ
//   28006  Open_Item_Btn03    SmWtBtn_W143      8,84   143×20   Open_Item_Btn03 + _Text      Visible=0, обработчика НЕТ
//   28004  Open_Item_Btn01    SmWtBtn_W143      8,103  143×20   Open_Item_Btn01 + _Text      Visible=0, обработчика НЕТ
//   28002  Open_Item_Btn05    SmGyBtn_W143      8,0    143×20   Open_Item_Btn05 + _Text      «Закрыть» (5035) → только скрыть
//
//  СПИСОК `Opend_Item_Win` — WBP_Opend_Item_Win, 17 узлов (якорь правый-нижний, 1.0/1.0):
//   idnum  idc                скин             Origin  Size     узел WBP                     что делает
//   28100  Opend_Item_Win     Box_BlackOt40_2  -4,0    394×75   Opend_Item_Win_0_0 / _Text    подложка + заголовок (102005, жирный),
//                                                                 _0_DRAG                     ручка EVS{6→0x20003}
//   28110  Opend_Item_Item0   Box_Black_Ot30    0,20   249×15   Opend_Item_Item0_grp:          строка 0: подложка _1_0,
//                                                                 Opend_Item_Item0 (текст)     имя предмета цветом грейда
//   28111  Opend_Item_User0   NoImage           0,0    115×15   Opend_Item_User0              имя игрока (NoSelect=1)
//   28120/28121  … строка 1 (Origin 0,37)  — Opend_Item_Item1_grp / _3_0 / Item1 / User1
//   28130/28131  … строка 2 (Origin 0,54)  — Opend_Item_Item2_grp / _5_0 / Item2 / User2
//   28101  Opend_Item_ClsBtn  CloseBtn_WinSm  -16,2    13×13    Opend_Item_ClsBtn             закрыть окно
//   28102  Opend_Item_Wheel   WheelWin_Nomal    5,20   378×49   —  (Visible=0 ⇒ импортёр узла НЕ создал)
//                                                               прокрутка = обработчик колеса, как у 494 в инвентаре
#include "UI/AsdaShowOffSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI

#include "AsdaNetSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"   // ★ общая точка cp1251 (Decode/EncodeCp1251)
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"      // ТОЛЬКО ради FAsdaItemInstance (запись 57 б) — читаем, не меняем
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б

DEFINE_LOG_CATEGORY_STATIC(LogAsdaShowOff, Log, All);

namespace
{
	const FName kMenuWindowId(TEXT("Open_Item_Win"));
	const FName kListWindowId(TEXT("Opend_Item_Win"));
	const FName kMenuClaimId(TEXT("SHOWOFF_MENU"));
	const TCHAR* kMenuWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_Open_Item_Win");
	const TCHAR* kListWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_Opend_Item_Win");

	// Нотисы (error list.bin, проверены построчно):
	//   102000 «Item Boast» — заголовок меню · 102001 «To all around» · 102002 «To Selected Person»
	//   102003 «No Selected Person.» · 102004 «Show the Item Boast window» (чекбокс 875) ·
	//   102005 «Item Boast» — заголовок окна-приёмника · 5035 «Close».
	constexpr int32 kLocMenuTitle  = 102000;
	constexpr int32 kLocBtnAll     = 102001;
	constexpr int32 kLocBtnTarget  = 102002;
	constexpr int32 kLocNoTarget   = 102003;
	constexpr int32 kLocListTitle  = 102005;
	constexpr int32 kLocClose      = 5035;

	// Флаг стиля текста строк списка: эталон ставит `CClass_SetField926(ctrl, 0x400000)` и для имени
	//   игрока, и для имени предмета — это ТЕНЬ (та же константа, что у ленты чата 2521). Импортёр
	//   тень из .JUI не переносит (в EXS её нет — её ставит КОД), поэтому включаем здесь.
	const FVector2D kRowShadowOffset(1.f, 1.f);
	const FLinearColor kRowShadowColor(0.f, 0.f, 0.f, 0.65f);
}

// ★ ОПЦИЯ 875 (нотис 102004 «Show the Item Boast window»). Окна настроек в порту пока нет, поэтому
//   единственная точка правды — этот CVar; будущее окно настроек обязано звать SetWindowEnabled().
//   Эталонное умолчание: `g_bOptShowOtherChars` читается из конфига, в поставке чекбокс включён.
static ASDA_DEV_CVAR(int32, CVarShowOffEnabled,
	TEXT("asda.ShowOff.Enabled"), 1,
	TEXT("Опция 875 «Показывать окно хвастовства» (эталонный g_bOptShowOtherChars, нотис 102004).\n")
	TEXT("  1 — окно Opend_Item_Win (28100) появляется при приходе 6130 (умолчание)\n")
	TEXT("  0 — пакеты принимаются и копятся, но окно не показывается и немедленно прячется"),
	ECVF_Default);

// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (закон fix-original-defects), включено по умолчанию.
//   В оригинале `CControl_SetField74_75(28000, курсор+32, курсор+32)` НЕ КЛАМПИТСЯ: при ПКМ у
//   правого/нижнего края экрана меню 158×100 уезжает за границу и часть кнопок недоступна.
//   Клампим прямоугольник меню во вьюпорт. Вернуть поведение оригинала: asda.ShowOff.MenuClamp 0.
static ASDA_DEV_CVAR(int32, CVarShowOffMenuClamp,
	TEXT("asda.ShowOff.MenuClamp"), 1,
	TEXT("Меню 28000 у курсора: 1 — не выпускать за край вьюпорта (наша правка дефекта оригинала), ")
	TEXT("0 — как в эталоне (без клампа, у края экрана меню обрезается)"),
	ECVF_Default);

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaShowOffSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RowItem.SetNum(kVisibleRows);
	RowUser.SetNum(kVisibleRows);
	RowHit.SetNum(kVisibleRows);
	UE_LOG(LogAsdaShowOff, Log, TEXT("[хвастовство] подсистема поднята: меню 28000, окно 28100, опкоды 6129/6130"));
}

void UAsdaShowOffSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			if (HShowOff.IsValid()) { Net->OnItemShowOff.Remove(HShowOff); }
		}
	}
	HShowOff.Reset();
	UnregisterZones();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->PopTopmost(kMenuClaimId);
			WM->CloseWindow(kMenuWindowId);
			WM->CloseWindow(kListWindowId);
		}
	}
	MenuWidget = nullptr; ListWidget = nullptr;
	Relays.Reset();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaShowOffSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseMenu();
	CloseListWindow();
	PendingBodies.Clear();
	SelContainer = -1; SelSlot = -1; SelItemCode = 0;
	StartRow = 0;
	bListVisible = false;
	(void)Reason;
}

void UAsdaShowOffSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.ShowOff");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// Подписка на 6130 — ЛЕНИВАЯ и ровно одна: сеть поднимается позже подсистем UI (та же схема,
	//   что у самоцветов); двойная подписка положила бы одну запись в список дважды.
	if (!bNetBound)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
			{
				HShowOff = Net->OnItemShowOff.AddUObject(this, &UAsdaShowOffSubsystem::OnShowOffPacket);
				bNetBound = true;
				UE_LOG(LogAsdaShowOff, Log, TEXT("[хвастовство] подписка на 6130 установлена"));
			}
		}
	}
	// ⚠ Всё «тяжёлое» делаем ЗДЕСЬ, а не в сетевом колбэке (закон no-reentrant-net-callbacks).
	DrainPending();
	// Опцию 875 в эталоне гасит сам обработчик чекбокса; у нас источник — CVar, поэтому сверяем.
	if (bListVisible && !IsWindowEnabled()) { ApplyListVisibility(); }
	TickRowTooltip();
}

// ═══════════════════════════════ ВИДЖЕТЫ ═══════════════════════════════════════════════════════
bool UAsdaShowOffSubsystem::EnsureMenu()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kMenuWindowId, kMenuWbp, kMenuZ).Ensure(GetGameInstance(), MenuWidget, bMenuReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.PopTopmost(kMenuClaimId);
			WM.CloseWindow(kMenuWindowId);
			MenuWidget = nullptr;
			bMenuReady = false;
			bMenuOpen = false;
		},
		[this](UAsdaWindowSubsystem&)
		{
			SetupMenuControls();
			RegisterZones();
			// Эталон держит окно скрытым до ПКМ (JUI Visible="0").
		},
		[]()
		{
			UE_LOG(LogAsdaShowOff, Error, TEXT("[хвастовство] WBP_Open_Item_Win не загрузился (%s)"), kMenuWbp);
		},
		[this](UAsdaWindowSubsystem&)
		{
			bMenuOpen = false;
		});
	return FAsdaJuiWindowHandle::Succeeded(R);
}

bool UAsdaShowOffSubsystem::EnsureList()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kListWindowId, kListWbp, kListZ).Ensure(GetGameInstance(), ListWidget, bListReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.CloseWindow(kListWindowId);
			ListWidget = nullptr;
			bListReady = false;
			bListVisible = false;
		},
		[this](UAsdaWindowSubsystem&)
		{
			SetupListControls();
			RegisterZones();
		},
		[]()
		{
			UE_LOG(LogAsdaShowOff, Error, TEXT("[хвастовство] WBP_Opend_Item_Win не загрузился (%s)"), kListWbp);
		},
		[this](UAsdaWindowSubsystem&)
		{
			// Скрытие окна (шаг каркаса): JUI Visible="0"; показывает только приход 6130
			bListVisible = false;
		});
	return FAsdaJuiWindowHandle::Succeeded(R);
}

// Найти узел по точному имени (импортёр адресует интерактив/текст ТОЧНЫМ idc — закон
//   jui-importer-widget-naming; имена «idc врут» относится к планам, а не к этим узлам).
// ★ СВЕДЕНО (`fix_helpers_dedup.md`): тонкий форвард к общей точке `AsdaUi::FindNodeExact`.
static UWidget* FindNode(const UUserWidget* Host, const TCHAR* Exact)
{
	return AsdaUi::FindNodeExact(Host, Exact);
}

void UAsdaShowOffSubsystem::SetupMenuControls()
{
	bMenuReady = false;
	if (!MenuWidget) { return; }

	MenuTitle = Cast<UTextBlock>(FindNode(MenuWidget, TEXT("Open_Item_Move")));
	BtnAll    = Cast<UButton>(FindNode(MenuWidget, TEXT("Open_Item_Btn00")));
	BtnParty  = Cast<UButton>(FindNode(MenuWidget, TEXT("Open_Item_Btn01")));
	BtnClan   = Cast<UButton>(FindNode(MenuWidget, TEXT("Open_Item_Btn02")));
	BtnSoul   = Cast<UButton>(FindNode(MenuWidget, TEXT("Open_Item_Btn03")));
	BtnTarget = Cast<UButton>(FindNode(MenuWidget, TEXT("Open_Item_Btn04")));
	BtnClose  = Cast<UButton>(FindNode(MenuWidget, TEXT("Open_Item_Btn05")));
	CapAll    = Cast<UTextBlock>(FindNode(MenuWidget, TEXT("Open_Item_Btn00_Text")));
	CapTarget = Cast<UTextBlock>(FindNode(MenuWidget, TEXT("Open_Item_Btn04_Text")));
	CapClose  = Cast<UTextBlock>(FindNode(MenuWidget, TEXT("Open_Item_Btn05_Text")));

	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	auto Loc = [Txt](int32 Id) -> FString { return Txt ? Txt->GetTextById(Id) : FString(); };

	// Эталон `InventoryWindow_InitControls@0xCFC8F0` (item_inventory.c:35642-35677) ставит ровно
	//   ЧЕТЫРЕ подписи: заголовок 102000, кнопки 102001 / 102002 и «Закрыть» 5035. Подписи трёх
	//   мёртвых кнопок он НЕ трогает — они остаются такими, какими пришли из .JUI. Делаем так же.
	auto SetCap = [](UTextBlock* T, const FString& S) { if (T && !S.IsEmpty()) { T->SetText(FText::FromString(S)); } };
	SetCap(MenuTitle, Loc(kLocMenuTitle));
	SetCap(CapAll,    Loc(kLocBtnAll));
	SetCap(CapTarget, Loc(kLocBtnTarget));
	SetCap(CapClose,  Loc(kLocClose));

	// ── кнопки: реле (UButton::OnClicked отправителя не передаёт) ──
	auto Bind = [this](UButton* Btn, int32 Index, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, Index, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	// ★ Волна 9 заточки: окна 28000/28100 — процедура инвентаря (группы 280/281); ветка 66561 играет звук id 5
	//   (ui_core.c:47649) ДО разбора 28003/28007/28002/28101 — общая точка `AsdaUi::PlayInvenButtonSfx`.
	// 28003 — «Всем рядом». Эталон СНАЧАЛА прячет меню, ПОТОМ шлёт (ui_core.c:47689-47695).
	Bind(BtnAll,    28003, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); CloseMenu(); SendShowOffToAll(); });
	// 28007 — «Только выбранному»: прячет меню, ищет цель, шлёт либо показывает нотис 102003.
	Bind(BtnTarget, 28007, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); CloseMenu(); SendShowOffToTarget(); });
	// 28002 — «Закрыть»: ТОЛЬКО скрыть окно, пакетов нет (ui_core.c:47732-47735).
	Bind(BtnClose,  28002, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); CloseMenu(); });
	// 28004/28005/28006 — обработчика в эталоне НЕТ и протокол этих режимов не выражает.
	//   Не оживляем (закон no-invented-ui): в .JUI они Visible="0" и такими остаются.
	auto MuteDead = [](UButton* B, const TCHAR* What)
	{
		if (!B) { return; }
		B->SetVisibility(ESlateVisibility::Collapsed);
		UE_LOG(LogAsdaShowOff, Verbose,
			TEXT("[хвастовство] кнопка «%s» скрыта: Visible=0 в .JUI, обработчика в эталоне нет, ")
			TEXT("протокол 6129 такого адресата не выражает"), What);
	};
	MuteDead(BtnParty, TEXT("Только группе (28004)"));
	MuteDead(BtnClan,  TEXT("Только клану (28005)"));
	MuteDead(BtnSoul,  TEXT("Только соулмейту (28006)"));

	bMenuReady = (BtnAll != nullptr && BtnTarget != nullptr && BtnClose != nullptr);
	UE_LOG(LogAsdaShowOff, Log,
		TEXT("[хвастовство] меню 28000 разобрано: заголовок=%s, 28003=%s, 28007=%s, 28002=%s"),
		MenuTitle ? TEXT("да") : TEXT("НЕТ"), BtnAll ? TEXT("да") : TEXT("НЕТ"),
		BtnTarget ? TEXT("да") : TEXT("НЕТ"), BtnClose ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaShowOffSubsystem::SetupListControls()
{
	bListReady = false;
	if (!ListWidget) { return; }

	ListTitle = Cast<UTextBlock>(FindNode(ListWidget, TEXT("Opend_Item_Win_Text")));
	ListClose = Cast<UButton>(FindNode(ListWidget, TEXT("Opend_Item_ClsBtn")));

	// Три строки. Текстовые узлы адресуются точным idc; подложка строки — единственный UImage
	//   внутри группы `Opend_Item_ItemN_grp` (её имя содержит индекс плана и различается по строкам:
	//   _1_0 / _3_0 / _5_0), поэтому ищем не по имени, а по типу внутри группы.
	for (int32 j = 0; j < kVisibleRows; ++j)
	{
		RowItem[j] = Cast<UTextBlock>(FindNode(ListWidget, *FString::Printf(TEXT("Opend_Item_Item%d"), j)));
		RowUser[j] = Cast<UTextBlock>(FindNode(ListWidget, *FString::Printf(TEXT("Opend_Item_User%d"), j)));
		RowHit[j]  = nullptr;
		if (UPanelWidget* Grp = Cast<UPanelWidget>(FindNode(ListWidget, *FString::Printf(TEXT("Opend_Item_Item%d_grp"), j))))
		{
			for (int32 i = 0; i < Grp->GetChildrenCount(); ++i)
			{
				if (UImage* Img = Cast<UImage>(Grp->GetChildAt(i))) { RowHit[j] = Img; break; }
			}
			if (!RowHit[j]) { RowHit[j] = Grp; }   // запасной вариант: зона = вся группа строки
		}
		// Тень текста — эталонный флаг 0x400000 (`CClass_SetField926`), см. константы выше.
		if (RowItem[j]) { RowItem[j]->SetShadowOffset(kRowShadowOffset); RowItem[j]->SetShadowColorAndOpacity(kRowShadowColor); }
		if (RowUser[j]) { RowUser[j]->SetShadowOffset(kRowShadowOffset); RowUser[j]->SetShadowColorAndOpacity(kRowShadowColor); }
	}

	// Заголовок: эталон формирует `sprintf(buf, "<FB>%s</F>", нотис 102005)` — ЖИРНЫЙ.
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const FString Title = Txt ? Txt->GetTextById(kLocListTitle) : FString();
	if (ListTitle && !Title.IsEmpty())
	{
		ListTitle->SetText(FText::FromString(Title));
		// ★ 2026-09-24: `<FB>` — ПСЕВДО-жирный (второй проход глифа ±1 px), а не Bold-гарнитура:
		//   жирного начертания в оригинале нет вовсе (разбор — `AsdaUi::ApplyPseudoBold`).
		AsdaUi::ApplyPseudoBold(ListTitle);
	}

	if (ListClose)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 28101;
		R->Callback = [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); CloseListWindow(); };   // звук id 5 — ветка 66561
		ListClose->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, ListClose);
	}

	// Строки при инициализации пусты (эталон: `CControl_SetText(ctrl, "")` для всех шести).
	for (int32 j = 0; j < kVisibleRows; ++j)
	{
		if (RowItem[j]) { RowItem[j]->SetText(FText::GetEmpty()); }
		if (RowUser[j]) { RowUser[j]->SetText(FText::GetEmpty()); }
		RowEntryIdx[j] = -1;
	}

	bListReady = (RowItem[0] != nullptr && RowUser[0] != nullptr);
	UE_LOG(LogAsdaShowOff, Log,
		TEXT("[хвастовство] окно 28100 разобрано: заголовок=%s, крестик=%s, строк=%d"),
		ListTitle ? TEXT("да") : TEXT("НЕТ"), ListClose ? TEXT("да") : TEXT("НЕТ"),
		(RowItem[0] ? 1 : 0) + (RowItem[1] ? 1 : 0) + (RowItem[2] ? 1 : 0));
}

// ═══════════════════════════════ ЗОНЫ И КОЛЕСО ═════════════════════════════════════════════════
void UAsdaShowOffSubsystem::RegisterZones()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	WM->RegisterUiZone(kMenuWindowId, FAsdaUiZoneRect::CreateWeakLambda(this,
		[this](FVector2D& Mn, FVector2D& Mx) { return GetMenuRect(Mn, Mx); }));
	WM->RegisterUiZone(kListWindowId, FAsdaUiZoneRect::CreateWeakLambda(this,
		[this](FVector2D& Mn, FVector2D& Mx) { return GetListRect(Mn, Mx); }));
	// Прокрутка списка — эталонное «колёсное окно» 28102 (`WheelWin_Nomal`, Visible=0), которое
	//   накрывает ровно три строки: событие 23/0x17 → `PageRows(start = значение − 2)`.
	WM->RegisterWheelHandler(kListWindowId,
		FAsdaUiWheelHandler::CreateUObject(this, &UAsdaShowOffSubsystem::OnListWheel));
}

void UAsdaShowOffSubsystem::UnregisterZones()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kMenuWindowId);
		WM->UnregisterUiZone(kListWindowId);
		WM->UnregisterWheelHandler(kListWindowId);
	}
}

// Прямоугольник по ЖИВОЙ геометрии узлов (абсолютные экранные координаты).
// ★ D1b (аудит `code_dedup_audit_2.md`): сам прямоугольник считает общая `AsdaUi::WidgetAbsRect`;
//   здесь остаётся ТОЛЬКО накопление объединения и своя фильтрация видимости (тут отсекается лишь
//   `Collapsed` — `Hidden` узел место в рамке занимает, в отличие от хит-теста).
static bool AccumRect(const UWidget* W, FVector2D& Mn, FVector2D& Mx, bool& bAny)
{
	// ★ Волна 3: тело — общая точка `FAsdaPopupMenu::AccumRect` (≡ меню 2000).
	return FAsdaPopupMenu::AccumRect(W, Mn, Mx, bAny);
}

bool UAsdaShowOffSubsystem::GetMenuRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bMenuOpen || !MenuWidget) { return false; }
	// Корпус меню = центральная плита рамки 3×3 плюс её боковины (объединение ВСЕХ узлов раздуло бы
	//   зону — эту грабли уже ловили на чате).
	static const TCHAR* kBody[] = { TEXT("Open_Item_Win_0_3"), TEXT("Open_Item_Win_0_4"), TEXT("Open_Item_Win_0_5"),
	                                TEXT("Open_Item_Win_0_0"), TEXT("Open_Item_Win_0_2"),
	                                TEXT("Open_Item_Win_0_6"), TEXT("Open_Item_Win_0_8") };
	// ★ Волна 3: корпус меню — общая точка `FAsdaPopupMenu::BodyRect`; ★ E8: у меню 28000 фолбэка НЕТ — как было (не унифицировано; разбор — отчёт волны 3 и волны 1 §E8).
	return FAsdaPopupMenu::BodyRect(MenuWidget, MakeArrayView(kBody), /*bFallbackToWidgetRect*/ false, OutMin, OutMax);
}

bool UAsdaShowOffSubsystem::GetListRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bListVisible || !ListWidget) { return false; }
	bool bAny = false;
	FVector2D Mn(FLT_MAX, FLT_MAX), Mx(-FLT_MAX, -FLT_MAX);
	AccumRect(FindNode(ListWidget, TEXT("Opend_Item_Win_0_0")), Mn, Mx, bAny);   // подложка 394×75
	if (!bAny) { return false; }
	OutMin = Mn; OutMax = Mx;
	return true;
}

bool UAsdaShowOffSubsystem::OnListWheel(int32 Notches)
{
	if (!bListVisible) { return false; }
	FVector2D Mn, Mx;
	if (!GetListRect(Mn, Mx)) { return false; }
	const FVector2D Cur = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
	if (Cur.X < Mn.X || Cur.X > Mx.X || Cur.Y < Mn.Y || Cur.Y > Mx.Y) { return false; }
	SetStartRow(StartRow - Notches);   // «от себя» (+1) = вверх по списку
	return true;
}

// ═══════════════════════════════ МЕНЮ 28000 ════════════════════════════════════════════════════
// Текущий левый-верхний угол меню = минимум offsets прямых детей корневого канваса.
//   Все дети окна 28000 в .JUI имеют POINT-анкор (0,0), поэтому Left/Top — абсолютные базовые px.
bool UAsdaShowOffSubsystem::GetMenuTopLeft(FVector2D& Out) const
{
	// ★ Волна 3: угол меню — общая точка `FAsdaPopupMenu::GetTopLeft` (меню 2000 ≡ 28000, тело перенесено без изменений).
	return FAsdaPopupMenu::GetTopLeft(MenuWidget, Out);
}

// Эталон `CControl_SetField74_75(28000, x, y)` двигает КОРНЕВОЙ контрол — то есть всё окно.
//   У нас корень окна = набор прямых детей канваса, поэтому сдвигаем их все на одну дельту
//   (та же схема, что у `UAsdaWindowSubsystem::ShiftDragTarget` для `bTargetIsWindowRoot`).
void UAsdaShowOffSubsystem::PlaceMenuAt(const FVector2D& TopLeftUnits)
{
	// ★ Волна 3: перенос меню — общая точка `FAsdaPopupMenu::PlaceAt` (двигает все дети корневой канвы).
	FAsdaPopupMenu::PlaceAt(MenuWidget, TopLeftUnits);
}

bool UAsdaShowOffSubsystem::OpenMenuForCell(int32 ContainerType, int32 SlotIndex, int32 ItemCode)
{
	// Эталон: меню открывается ТОЛЬКО когда `CInventoryUI_OnSlotClick` вернул 1, то есть ячейка
	//   НЕ пуста (`if (bagItemPtr && *(int*)bagItemPtr > 0)`). Пустую ячейку молча игнорируем.
	if (ItemCode <= 0)
	{
		UE_LOG(LogAsdaShowOff, Verbose,
			TEXT("[хвастовство] ПКМ по пустой ячейке (контейнер=%d слот=%d) — меню не открывается (как в эталоне)"),
			ContainerType, SlotIndex);
		return false;
	}
	if (!EnsureMenu()) { return false; }

	UGameInstance* GI = GetGameInstance();

	SelContainer = ContainerType;
	SelSlot      = SlotIndex;
	SelItemCode  = ItemCode;

	// ★ Эталон уносит в 6129 НЕ индекс ячейки сетки, а поля +4/+5 САМОЙ 57-байтовой записи, снятой
	//   в `m_aSelItemSnapshot` (`*(BYTE*)(self+588)` и `*(WORD*)(self+589)`; item_inventory.c:35636).
	//   Сервер ищет предмет `sub_4D61E0(itemMgr, arg0, container, slot, …)` и при промахе МОЛЧИТ.
	//   Поэтому берём контейнер и слот из записи, а расхождение с ячейкой пишем в лог.
	if (const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		if (const FAsdaItemInstance* Rec = Inv->GetCell(ContainerType, SlotIndex))
		{
			if (Rec->ItemCode > 0)
			{
				const int32 RecCont = (int32)Rec->ContainerType;
				const int32 RecSlot = Rec->SlotIndex;
				if (RecCont != ContainerType || RecSlot != SlotIndex)
				{
					UE_LOG(LogAsdaShowOff, Warning,
						TEXT("[хвастовство] ячейка (контейнер=%d слот=%d) ≠ поля записи (+4=%d, +5=%d) — ")
						TEXT("в 6129 уходят поля ЗАПИСИ, как в эталоне"),
						ContainerType, SlotIndex, RecCont, RecSlot);
				}
				if (RecCont > 0 && RecSlot >= 0)
				{
					SelContainer = RecCont;
					SelSlot      = RecSlot;
				}
			}
		}
		else
		{
			UE_LOG(LogAsdaShowOff, Warning,
				TEXT("[хвастовство] записи для ячейки (контейнер=%d слот=%d) нет в модели инвентаря — ")
				TEXT("шлём то, что передал вызывающий"), ContainerType, SlotIndex);
		}
	}

	// `CControl_KillFocus(&g_ItemTooltip)` + `self[222] = 0` — тултип предмета гаснет при ПКМ.
	//   Гасим ТОЛЬКО если плашкой владеет тултип предмета (закон ui-shared-tip-ownership).
	if (UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::Item);
	}

	// Позиция: курсор + (32, 32) в единицах вьюпорта (эталонный `copyPair79` даёт курсор в той же
	//   системе, в которой лежат Origin контролов).
	FVector2D Cursor(0.f, 0.f);
	AsdaUi::CursorInViewportUnits(GI ? GI->GetWorld() : nullptr, Cursor);
	FVector2D Pos = Cursor + FVector2D(kMenuDX, kMenuDY);
	if (CVarShowOffMenuClamp.GetValueOnGameThread() != 0)
	{
		// ★ Дедуп №2, В1-B (U16, 11.10.2026): общая точка клампа попапа к экрану.
		Pos = AsdaUi::ClampPopupToViewport(Pos, FVector2D(kMenuW, kMenuH), AsdaUi::ViewportSizeUnits(GI ? GI->GetWorld() : nullptr));
	}
	PlaceMenuAt(Pos);

	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM)
	{
		WM->SetWindowVisible(kMenuWindowId, true);
		WM->BringWindowToFront(kMenuWindowId);   // эталонный `CClass_MapInsert(g_pWindowMgr, …)`
		FAsdaUiZoneRect D;
		D.BindWeakLambda(this, [this](FVector2D& Mn, FVector2D& Mx) { return GetMenuRect(Mn, Mx); });
		WM->PushTopmost(kMenuClaimId, kMenuWindowId, MoveTemp(D));
		// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: щелчок мимо гасит меню; разбор — `AsdaWindowSubsystem_TopPress.cpp`.
		WM->RegisterClickAwayMenu(kMenuClaimId, FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseMenu(); }));
	}
	bMenuOpen = true;
	UE_LOG(LogAsdaShowOff, Log,
		TEXT("[хвастовство] меню 28000 открыто у (%.0f,%.0f) для предмета %d: ячейка (контейнер=%d слот=%d) → ")
		TEXT("в 6129 уйдёт (контейнер=%d слот=%d)"),
		Pos.X, Pos.Y, ItemCode, ContainerType, SlotIndex, SelContainer, SelSlot);
	return true;
}

void UAsdaShowOffSubsystem::CloseMenu()
{
	if (!bMenuOpen) { return; }
	bMenuOpen = false;
	FAsdaPopupMenu::HideAndPopTopmost(GetGameInstance(), kMenuWindowId, kMenuClaimId);   // ★ волна 3: скрыть → вернуть верхний слой
	UE_LOG(LogAsdaShowOff, Verbose, TEXT("[хвастовство] меню 28000 скрыто"));
}

// ═══════════════════════════════ ОТПРАВКА 6129 ═════════════════════════════════════════════════
bool UAsdaShowOffSubsystem::SendShowOff(int32 ContainerType, int32 SlotIndex, int32 TargetUserIdx)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->bInGameConnected)
	{
		UE_LOG(LogAsdaShowOff, Warning, TEXT("[хвастовство] 6129 не отправлен: нет соединения с игровым сервером"));
		return false;
	}
	if (ContainerType < 0 || SlotIndex < 0)
	{
		UE_LOG(LogAsdaShowOff, Warning,
			TEXT("[хвастовство] 6129 не отправлен: предмет не выбран (контейнер=%d слот=%d)"), ContainerType, SlotIndex);
		return false;
	}
	// ★★★ ПОЛЕ @0 = charId, НЕ сессионный userNum (дефект найден 2026-08-09 живым тестом).
	//   Эталон берёт `CharManager_GetLocalPlayer(...)->userIdx` — в структуре персонажа клиента это
	//   поле +1052 (`CharManager_FindByCharId` сверяет ИМЕННО +1052, entity_char.c:6264), тогда как
	//   СЕССИОННЫЙ индекс лежит в +190188 и он u16 (`CharManager_FindByUserIdx`, :6281).
	//   Сервер сверяет @0 с `sub_5324F0(this, arg0)` = `*(DWORD*)(CUser+372)` = iUserIndex = charId
	//   (GameServer.c:116575; лог :25118 печатает iUserNum и iUserIndex как РАЗНЫЕ значения).
	//   Наш charId — тот же, что уходит в 4001/5058: `UAsdaNetSubsystem::SelectedCharId` из 1017.
	const int32 SelfCharId = Net->SelectedCharId;
	if (SelfCharId == 0)
	{
		UE_LOG(LogAsdaShowOff, Warning,
			TEXT("[хвастовство] 6129 не отправлен: charId ещё не известен (1017 не пришёл) — ")
			TEXT("сервер отбросил бы пакет молча по анти-подмене отправителя"));
		return false;
	}
	const bool bOk = Net->SendItemInfoDisplay6129(SelfCharId, TargetUserIdx, (uint8)ContainerType, SlotIndex);
	UE_LOG(LogAsdaShowOff, Log,
		TEXT("[хвастовство] 6129 отправлен=%s: charId=%d (userNum=%u) цель=%d контейнер=%d слот=%d"),
		bOk ? TEXT("да") : TEXT("НЕТ"), SelfCharId, Net->SelfUserNum, TargetUserIdx, ContainerType, SlotIndex);
	if (bOk) { ++CntSent; }
	return bOk;
}

bool UAsdaShowOffSubsystem::SendShowOffToAll()
{
	// `netSendTsMaskedReq6129(self, localPlayer->userIdx, -1, maskWord, maskByte)` — target −1.
	return SendShowOff(SelContainer, SelSlot, -1);
}

bool UAsdaShowOffSubsystem::SendShowOffToTarget()
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	const AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	// Эталон: `if (!g_nInputControlState) target = CharManager_FindByUserIdx(g_nTargetUserIdx)`,
	//   затем `Target_GetSelectedUserIdx(target)`. У нас выбранная цель — `AAsdaPlayerController::TargetActor`.
	AActor* Tgt = PC ? PC->TargetActor.Get() : nullptr;
	const int32 TargetIdx = (Tgt && WS) ? WS->FindUserIdxByActor(Tgt) : -1;
	if (TargetIdx < 0)
	{
		// Эталон: нотис 102003 «Нет выбранной цели» через общую плашку; пакет НЕ шлётся.
		if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			Msg->PostLoc(EAsdaMsgCategory::ItemRefused, kLocNoTarget);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка («[#id]»)
		}
		UE_LOG(LogAsdaShowOff, Log,
			TEXT("[хвастовство] 28007: цели нет (или цель — не игрок) → нотис %d, пакет не отправлен"), kLocNoTarget);
		return false;
	}
	return SendShowOff(SelContainer, SelSlot, TargetIdx);
}

bool UAsdaShowOffSubsystem::SendShowOffForRecord(const TArray<uint8>& Rec57)
{
	// `CItemManager_NotifyItemInfo`: `v6 = запись[4]` (контейнер), `v5 = *(WORD*)(запись + 5)` (слот),
	//   адресат всегда −1.
	if (Rec57.Num() < FAsdaItemInstance::kRecordSize)
	{
		UE_LOG(LogAsdaShowOff, Warning, TEXT("[хвастовство] авто-анонс: запись короче 57 б (%d)"), Rec57.Num());
		return false;
	}
	const int32 Cont = (int32)Rec57[4];
	const int32 Slot = (int32)(int16)((uint16)Rec57[5] | ((uint16)Rec57[6] << 8));
	return SendShowOff(Cont, Slot, -1);
}

// ═══════════════════════════════ ПРИЁМ 6130 ════════════════════════════════════════════════════
void UAsdaShowOffSubsystem::OnShowOffPacket(const TArray<uint8>& Body81)
{
	// ⚠ ТОЛЬКО копим (закон no-reentrant-net-callbacks): касание виджетов из сетевого колбэка
	//   рвёт разбор входящего потока.
	// ★ Аудит №3 (B10): общая очередь — теперь и здесь под замком (раньше его тут не было вовсе).
	PendingBodies.Push(Body81);
	// Звено №3 цепочки: делегат ДОШЁЛ до подсистемы. Разбор — в Tick (DrainPending).
	UE_LOG(LogAsdaShowOff, Verbose,
		TEXT("[хвастовство] 6130 поставлен в очередь (тел в очереди: %d)"), PendingBodies.Num());
}

void UAsdaShowOffSubsystem::DrainPending()
{
	// ★ Аудит №3 (B10): `Drain` = прежнее «пусто ⇒ выход; иначе забрать всё разом и очистить».
	TArray<TArray<uint8>> Local;
	if (!PendingBodies.Drain(Local)) { return; }
	for (const TArray<uint8>& B : Local)
	{
		if (B.Num() < 81) { continue; }
		FAsdaShowOffEntry E;
		E.RawUserIdx = (int32)((uint32)B[0] | ((uint32)B[1] << 8) | ((uint32)B[2] << 16) | ((uint32)B[3] << 24));
		// Имя: cp1251, ровно 20 б, обрыв по нулю (эталонный `strcpy(&v8[4], a2 + 4)`).
		// ★ СВЕДЕНО (`fix_helpers_dedup.md`): прежний инлайн-декодер был НЕПОЛНОЙ копией общей точки
		//   (не знал пунктуацию « » – — … №). Общая точка — `FAsdaLoginData::DecodeCp1251`.
		E.CharName = FAsdaLoginData::DecodeCp1251(B.GetData() + 4, 20);
		E.Rec57.SetNumUninitialized(FAsdaItemInstance::kRecordSize);
		FMemory::Memcpy(E.Rec57.GetData(), B.GetData() + 24, FAsdaItemInstance::kRecordSize);
		E.ItemCode = (int32)((uint32)E.Rec57[0] | ((uint32)E.Rec57[1] << 8)
			| ((uint32)E.Rec57[2] << 16) | ((uint32)E.Rec57[3] << 24));
		PushEntry(E);
		++CntRecv;
	}
}

void UAsdaShowOffSubsystem::PushEntry(const FAsdaShowOffEntry& E)
{
	Entries.Add(E);
	// Эталон `itemListWnd_FillFromPacket`: `if (size > 20) { pop_front; }` — потолок ровно 20.
	while (Entries.Num() > kMaxEntries) { Entries.RemoveAt(0); }
	// Показ ПОСЛЕДНИХ трёх: `PageRows(this, max(count − 1 − 2, 0))`.
	StartRow = MaxStartRow();

	if (!EnsureList())
	{
		// Звено №4 цепочки: пакет разобран, но окна нет — тут же говорим почему.
		UE_LOG(LogAsdaShowOff, Error,
			TEXT("[хвастовство] 6130 разобран, но окно 28100 не поднялось (WBP не загрузился либо нет ")
			TEXT("UAsdaWindowSubsystem) — запись легла в список, показать нечем"));
		return;
	}
	RefreshRows();

	// `if (opt == 1 && !ctrl->bVisible) { SetVisible(1); BringToFront(); }` — уже открытое окно
	//   повторный приход НЕ дёргает (не поднимает наверх и не переоткрывает).
	if (IsWindowEnabled() && !bListVisible)
	{
		bListVisible = true;
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
			{
				WM->SetWindowVisible(kListWindowId, true);
				WM->BringWindowToFront(kListWindowId);
			}
		}
	}
	ApplyListVisibility();
	UE_LOG(LogAsdaShowOff, Log,
		TEXT("[хвастовство] 6130: «%s» показывает предмет %d; записей %d, первая строка %d, окно %s ")
		TEXT("(разобрано=%s, строк найдено=%d)"),
		*E.CharName, E.ItemCode, Entries.Num(), StartRow,
		bListVisible ? TEXT("показано") : TEXT("скрыто (опция 875 выключена)"),
		bListReady ? TEXT("да") : TEXT("НЕТ"),
		(RowItem[0] ? 1 : 0) + (RowItem[1] ? 1 : 0) + (RowItem[2] ? 1 : 0));
}

// ═══════════════════════════════ РЕНДЕР СТРОК ══════════════════════════════════════════════════
// Порт `itemListWnd_PageRows@0xD33080` (item_inventory.c:45448): сперва ВСЕ три строки чистятся и
//   их индексы сбрасываются в −1, затем заполняется min(3, count − start) строк.
void UAsdaShowOffSubsystem::RefreshRows()
{
	if (!bListReady) { return; }
	for (int32 j = 0; j < kVisibleRows; ++j)
	{
		if (RowItem[j]) { RowItem[j]->SetText(FText::GetEmpty()); }
		if (RowUser[j]) { RowUser[j]->SetText(FText::GetEmpty()); }
		RowEntryIdx[j] = -1;
	}
	const int32 Start = FMath::Clamp(StartRow, 0, MaxStartRow());
	const int32 N = FMath::Clamp(Entries.Num() - Start, 0, kVisibleRows);
	for (int32 j = 0; j < N; ++j)
	{
		const FAsdaShowOffEntry& E = Entries[Start + j];
		if (RowUser[j]) { RowUser[j]->SetText(FText::FromString(E.CharName)); }
		FString Name; uint32 Argb = 0xFFFFFFFF;
		if (ResolveItemLine(E.ItemCode, Name, Argb) && RowItem[j])
		{
			RowItem[j]->SetText(FText::FromString(Name));
			RowItem[j]->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbU32ToLinear(Argb)));
		}
		else if (RowItem[j])
		{
			// Записи нет ни в одной из трёх таблиц — эталон в этом случае просто оставляет строку
			//   пустой (ни одна из трёх веток `if` не сработала). Пишем в лог, чтобы было видно.
			UE_LOG(LogAsdaShowOff, Warning,
				TEXT("[хвастовство] предмет %d не найден ни в item_list, ни в special_itemlist, ни в sowell_list"),
				E.ItemCode);
		}
		RowEntryIdx[j] = Start + j;
	}
}

// Три таблицы В ТОМ ЖЕ ПОРЯДКЕ, что и эталонный PageRows: item_list → special_itemlist → sowell_list.
//   Пространства кодов не пересекаются, поэтому срабатывает ровно одна ветка.
bool UAsdaShowOffSubsystem::ResolveItemLine(int32 ItemCode, FString& OutName, uint32& OutArgb) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }

	if (const FAsdaItemRecord* R = IT->GetItem(ItemCode))
	{
		// `GetDropColor(itemGrade)` + `ItemData_FindByCode(itemCode)`.
		OutArgb = UAsdaItemTooltipSubsystem::GradeColorArgb(R->ItemGrade);
		OutName = IT->GetItemName(ItemCode);
		if (OutName.IsEmpty()) { OutName = R->ItemName; }
		return !OutName.IsEmpty();
	}
	FAsdaSpecialItemlistRow Sp;
	if (IT->GetSpecialItem(ItemCode, Sp))
	{
		// `GetDropColor(Table2Record->dropColorIndex[0])` + имя ПРЯМО из записи второй таблицы.
		OutArgb = UAsdaItemTooltipSubsystem::GradeColorArgb(Sp.DropColorIndex);
		OutName = Sp.Name;
		return !OutName.IsEmpty();
	}
	FAsdaSowellRow Sw;
	if (IT->GetSowell(ItemCode, Sw))
	{
		// `GetDropColor(SowelRecord->dropColorIndex[0])` + `sowelTableFindRecordByCode(code)`.
		OutArgb = UAsdaItemTooltipSubsystem::GradeColorArgb(Sw.GradeColorIndex);
		OutName = Sw.Name;
		return !OutName.IsEmpty();
	}
	return false;
}

void UAsdaShowOffSubsystem::SetStartRow(int32 Row)
{
	const int32 New = FMath::Clamp(Row, 0, MaxStartRow());
	if (New == StartRow) { return; }
	StartRow = New;
	RefreshRows();
}

void UAsdaShowOffSubsystem::ApplyListVisibility()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !ListWidget) { return; }
	const bool bWant = bListVisible && IsWindowEnabled();
	if (!bWant && bListVisible)
	{
		// Эталон в обработчике чекбокса 875: `g_bOptShowOtherChars = 0; SetVisible(28100, 0)`.
		bListVisible = false;
		UE_LOG(LogAsdaShowOff, Log, TEXT("[хвастовство] окно 28100 скрыто (опция 875 выключена)"));
	}
	WM->SetWindowVisible(kListWindowId, bWant);
	if (!bWant)
	{
		// Плашку, которую держала строка, забираем с собой.
		if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
		{
			Tip->HideTooltipFor(EAsdaTipOwner::ShowOff);
		}
		RowHovered = -1;
	}
}

void UAsdaShowOffSubsystem::CloseListWindow()
{
	if (!bListVisible) { return; }
	bListVisible = false;
	ApplyListVisibility();
	UE_LOG(LogAsdaShowOff, Log, TEXT("[хвастовство] окно 28100 закрыто крестиком 28101 (список НЕ очищается — как в эталоне)"));
}

void UAsdaShowOffSubsystem::SetWindowEnabled(bool bEnabled)
{
	CVarShowOffEnabled->Set(bEnabled ? 1 : 0, ECVF_SetByConsole);
	UE_LOG(LogAsdaShowOff, Log, TEXT("[хвастовство] опция 875 «показывать окно» = %s"), bEnabled ? TEXT("вкл") : TEXT("выкл"));
	ApplyListVisibility();
}

bool UAsdaShowOffSubsystem::IsWindowEnabled() const
{
	return CVarShowOffEnabled.GetValueOnGameThread() != 0;
}

// ═══════════════════════════════ ТУЛТИП СТРОКИ ═════════════════════════════════════════════════
// Эталон (`ui_core.c:47894-47912`, событие 8 = курсор ВОШЁЛ в контрол 28110/28120/28130):
//     idx = this[1082 + (cmdId − 28110)/10];   если idx >= 0:
//       код = запись(idx) + 24;                          // itemCode
//       код вне [SowelLo..SowelHi] → ItemTooltip_Build(&g_ItemTooltip, запись, −1, 0, 1, 1, 0, 0)
//       иначе, если GetSowelRecord(код) → CControl_SetState(&g_ItemTooltip, 1, код, кол-во, 1, −1, флагПривязки)
// У нас маршрутизацию (полный / простой / самоцвет / вторая таблица) делает сам
//   `UAsdaItemTooltipSubsystem::ShowItemTooltip` — поэтому обе ветки сводятся к одному вызову.
void UAsdaShowOffSubsystem::TickRowTooltip()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }
	if (!bListVisible || !bListReady)
	{
		if (RowHovered >= 0) { Tip->HideTooltipFor(EAsdaTipOwner::ShowOff); RowHovered = -1; }
		return;
	}
	const FVector2D Cur = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
	int32 Hit = -1;
	for (int32 j = 0; j < kVisibleRows; ++j)
	{
		if (RowEntryIdx[j] < 0 || !RowHit[j]) { continue; }
		bool bAny = false;
		FVector2D Mn(FLT_MAX, FLT_MAX), Mx(-FLT_MAX, -FLT_MAX);
		AccumRect(RowHit[j], Mn, Mx, bAny);
		if (bAny && Cur.X >= Mn.X && Cur.X <= Mx.X && Cur.Y >= Mn.Y && Cur.Y <= Mx.Y && !AsdaUi::IsCoveredByHigherWindow(RowHit[j], Cur)) { Hit = j; break; }
	}
	if (Hit == RowHovered) { return; }
	RowHovered = Hit;
	if (Hit < 0)
	{
		// ★ Гасим ТОЛЬКО свою плашку (закон ui-shared-tip-ownership).
		Tip->HideTooltipFor(EAsdaTipOwner::ShowOff);
		return;
	}
	const int32 Idx = RowEntryIdx[Hit];
	if (!Entries.IsValidIndex(Idx)) { return; }
	FAsdaItemInstance It;
	It.SetRaw(Entries[Idx].Rec57.GetData(), Entries[Idx].Rec57.Num());
	// `ItemTooltip_Build(…, −1, 0, …)` — ПОЛНЫЙ билдер, сравнение с надетым НЕ подавляется
	//   (третий аргумент эталона = 0). SourceKind 1 = «полный путь», как у вкладки «Снаряжение».
	Tip->ShowItemTooltip(It, /*bSuppressCompare=*/false, EAsdaTipOwner::ShowOff, /*SourceKind=*/1);
}

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════
FString UAsdaShowOffSubsystem::DumpControls() const
{
	FString S;
	S += FString::Printf(TEXT("ХВАСТОВСТВО ПРЕДМЕТОМ — контролы\n"));
	S += FString::Printf(TEXT("  МЕНЮ 28000 (%s): виджет=%s, разобрано=%s, открыто=%s\n"),
		kMenuWbp, MenuWidget ? TEXT("есть") : TEXT("НЕТ"),
		bMenuReady ? TEXT("да") : TEXT("НЕТ"), bMenuOpen ? TEXT("да") : TEXT("нет"));
	auto Row = [&S](const TCHAR* Id, int32 IdNum, const UWidget* W, const TCHAR* What)
	{
		S += FString::Printf(TEXT("    %-22s %-6d %-8s %s\n"), Id, IdNum,
			W ? TEXT("НАЙДЕН") : TEXT("нет"), What);
	};
	Row(TEXT("Open_Item_Move"),  28001, MenuTitle, TEXT("заголовок = нотис 102000 + ручка перетаскивания"));
	Row(TEXT("Open_Item_Btn00"), 28003, BtnAll,    TEXT("«Всем рядом» (102001) → 6129 target −1"));
	Row(TEXT("Open_Item_Btn04"), 28007, BtnTarget, TEXT("«Только выбранному» (102002) → 6129 target=цель"));
	Row(TEXT("Open_Item_Btn05"), 28002, BtnClose,  TEXT("«Закрыть» (5035) — только скрыть"));
	Row(TEXT("Open_Item_Btn01"), 28004, BtnParty,  TEXT("Visible=0, обработчика НЕТ ни в клиенте, ни в протоколе"));
	Row(TEXT("Open_Item_Btn02"), 28005, BtnClan,   TEXT("Visible=0, обработчика НЕТ"));
	Row(TEXT("Open_Item_Btn03"), 28006, BtnSoul,   TEXT("Visible=0, обработчика НЕТ"));
	S += FString::Printf(TEXT("  ОКНО 28100 (%s): виджет=%s, разобрано=%s, показано=%s, опция 875=%s\n"),
		kListWbp, ListWidget ? TEXT("есть") : TEXT("НЕТ"), bListReady ? TEXT("да") : TEXT("НЕТ"),
		bListVisible ? TEXT("да") : TEXT("нет"), IsWindowEnabled() ? TEXT("вкл") : TEXT("выкл"));
	Row(TEXT("Opend_Item_Win_Text"), 28100, ListTitle, TEXT("заголовок = <FB>нотис 102005</F>"));
	Row(TEXT("Opend_Item_ClsBtn"),   28101, ListClose, TEXT("крестик — скрыть окно"));
	for (int32 j = 0; j < kVisibleRows; ++j)
	{
		Row(*FString::Printf(TEXT("Opend_Item_Item%d"), j), 28110 + 10 * j, RowItem[j], TEXT("имя предмета цветом грейда"));
		Row(*FString::Printf(TEXT("Opend_Item_User%d"), j), 28111 + 10 * j, RowUser[j], TEXT("имя игрока (NoSelect=1)"));
		Row(*FString::Printf(TEXT("Opend_Item_Item%d_grp"), j), 0, RowHit[j], TEXT("зона наведения строки (тултип)"));
	}
	S += FString::Printf(TEXT("    %-22s %-6d %-8s %s\n"), TEXT("Opend_Item_Wheel"), 28102, TEXT("—"),
		TEXT("Visible=0 в .JUI ⇒ узла в WBP нет; прокрутка сделана обработчиком колеса"));
	return S;
}

FString UAsdaShowOffSubsystem::DumpModel() const
{
	FString S = FString::Printf(
		TEXT("ХВАСТОВСТВО: записей %d (потолок %d), первая строка %d, принято 6130 = %d, отправлено 6129 = %d\n"),
		Entries.Num(), kMaxEntries, StartRow, CntRecv, CntSent);
	S += FString::Printf(TEXT("  выбрано в инвентаре: контейнер=%d слот=%d предмет=%d\n"),
		SelContainer, SelSlot, SelItemCode);
	// ★ Оба индекса рядом: @0 пакета 6129 обязан быть charId, @4 — сессионный userNum адресата.
	{
		const UGameInstance* GI = GetGameInstance();
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		S += FString::Printf(
			TEXT("  сеть: charId(@0 пакета 6129)=%d, сессионный userNum=%u, игровое соединение=%s, подписка на 6130=%s\n"),
			Net ? Net->SelectedCharId : -1, Net ? Net->SelfUserNum : 0,
			(Net && Net->bInGameConnected) ? TEXT("есть") : TEXT("НЕТ"),
			bNetBound ? TEXT("есть") : TEXT("НЕТ"));
	}
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		FString Name; uint32 Argb = 0;
		ResolveItemLine(Entries[i].ItemCode, Name, Argb);
		S += FString::Printf(TEXT("  [%2d] %-20s предмет %6d «%s» цвет %08X%s\n"),
			i, *Entries[i].CharName, Entries[i].ItemCode, *Name, Argb,
			(i >= StartRow && i < StartRow + kVisibleRows) ? TEXT("  ← на экране") : TEXT(""));
	}
	return S;
}

void UAsdaShowOffSubsystem::SimulateIncomingRecord(const FString& CharName, const TArray<uint8>& Rec57)
{
	// Собираем ровно то тело, которое кладёт сервер: [i32][char[20] cp1251][57 б записи].
	TArray<uint8> Body;
	Body.SetNumZeroed(81);
	const uint32 Self = GetGameInstance() && GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>()
		? (uint32)GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>()->SelfUserNum : 0u;
	Body[0] = (uint8)(Self & 0xFF); Body[1] = (uint8)((Self >> 8) & 0xFF);
	// ★ СВЕДЕНО (`fix_helpers_dedup.md`): инлайн-кодер cp1251 → общая точка `FAsdaLoginData::EncodeCp1251`
	//   (≤19 б, терминатор уже стоит от SetNumZeroed). Ставит имя байт в байт как раньше.
	FAsdaLoginData::EncodeCp1251(CharName, Body.GetData() + 4, 19);
	const int32 N = FMath::Min(Rec57.Num(), (int32)FAsdaItemInstance::kRecordSize);
	if (N > 0) { FMemory::Memcpy(Body.GetData() + 24, Rec57.GetData(), N); }
	OnShowOffPacket(Body);   // дальше — штатный путь: очередь → Tick → DrainPending
}

void UAsdaShowOffSubsystem::SimulateIncoming(const FString& CharName, int32 ItemCode)
{
	// ★ U17: код, контейнер 1 (снаряжение) — только для правдоподобия дампа, количество 1.
	const TArray<uint8> Rec = FAsdaItemRecordWriter().Code(ItemCode).Container(1).Amount(1).ToArray();
	SimulateIncomingRecord(CharName, Rec);
}

// ═══════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (проверка без второго игрока) ══════════════════
namespace
{
	UAsdaShowOffSubsystem* Sub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaShowOffSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShowOffMenu(
	TEXT("asda.ShowOff.Menu"),
	TEXT("asda.ShowOff.Menu <контейнер> <слот> <кодПредмета> — открыть меню 28000 у курсора "
	     "(как ПКМ по ячейке инвентаря)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaShowOffSubsystem* S = Sub(W);
		if (!S) { UE_LOG(LogAsdaShowOff, Warning, TEXT("подсистема недоступна")); return; }
		const int32 C = A.Num() > 0 ? FCString::Atoi(*A[0]) : 1;
		const int32 Sl = A.Num() > 1 ? FCString::Atoi(*A[1]) : 0;
		const int32 Code = A.Num() > 2 ? FCString::Atoi(*A[2]) : 1;
		UE_LOG(LogAsdaShowOff, Log, TEXT("asda.ShowOff.Menu: контейнер=%d слот=%d предмет=%d → %s"),
			C, Sl, Code, S->OpenMenuForCell(C, Sl, Code) ? TEXT("открыто") : TEXT("отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShowOffFake(
	TEXT("asda.ShowOff.Fake"),
	TEXT("asda.ShowOff.Fake <имя> <кодПредмета> — сымитировать приход 6130 от другого игрока "
	     "(сервер не нужен): запись ляжет в список окна 28100 штатным путём"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaShowOffSubsystem* S = Sub(W);
		if (!S) { UE_LOG(LogAsdaShowOff, Warning, TEXT("подсистема недоступна")); return; }
		const FString Name = A.Num() > 0 ? A[0] : TEXT("Тестовый");
		const int32 Code = A.Num() > 1 ? FCString::Atoi(*A[1]) : 1;
		S->SimulateIncoming(Name, Code);
		UE_LOG(LogAsdaShowOff, Log, TEXT("asda.ShowOff.Fake: «%s» предмет %d поставлен в очередь"), *Name, Code);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShowOffSend(
	TEXT("asda.ShowOff.Send"),
	TEXT("asda.ShowOff.Send [цель] — отправить 6129 по ТЕКУЩЕМУ выбору меню. "
	     "цель отсутствует/−1 = всем рядом (сервер вернёт 6130 ВСЕМ в секторе, включая нас); "
	     "иначе — сессионный userNum адресата"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaShowOffSubsystem* S = Sub(W);
		if (!S) { UE_LOG(LogAsdaShowOff, Warning, TEXT("подсистема недоступна")); return; }
		// Без аргумента (или −1) — ветка кнопки 28003 «всем рядом». С явным userNum адресата —
		//   низкоуровневая отправка по ТЕКУЩЕМУ выбору: так адресную ветку можно проверить, не
		//   имея выбранной цели в мире (ветка кнопки 28007 остаётся нетронутой).
		if (A.Num() == 0 || FCString::Atoi(*A[0]) < 0) { S->SendShowOffToAll(); }
		else { S->SendShowOff(S->GetSelContainer(), S->GetSelSlot(), FCString::Atoi(*A[0])); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShowOffDump(
	TEXT("asda.ShowOff.Dump"),
	TEXT("asda.ShowOff.Dump — дамп КАЖДОГО контрола обоих окон + содержимое списка"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaShowOffSubsystem* S = Sub(W);
		if (!S) { UE_LOG(LogAsdaShowOff, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaShowOff, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpModel());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShowOffClose(
	TEXT("asda.ShowOff.Close"),
	TEXT("asda.ShowOff.Close — закрыть меню 28000 и окно 28100"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaShowOffSubsystem* S = Sub(W)) { S->CloseMenu(); S->CloseListWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE
