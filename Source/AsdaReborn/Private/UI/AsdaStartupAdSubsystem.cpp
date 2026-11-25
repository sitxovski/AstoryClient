// Copyright (c) 2026 @sitxovski (Discord)
// AsdaStartupAdSubsystem.cpp — стартовое окно новостей `AD_Win` (5000).
//   ВЕСЬ реверс, адреса, раскладка `AD.bin`, доказательство «в эталонном клиенте окно не
//   появляется» — в шапке `Public/UI/AsdaStartupAdSubsystem.h`. Ни одно число здесь не выдумано.
//
// ═══════════ ТАБЛИЦА КОНТРОЛОВ (JUI IDNum → узел WBP_AD_Win) ══════════════════════════════════
//   5000 AD_Win        AD_Win_0_0…_0_8                    рамка 3×3 окна
//   5001 AD_ClsBtn     AD_ClsBtn                          крестик
//   5002 AD_Title_Move AD_Title_Move + _2_DRAG            заголовок (loc 99501) + ручка перетаскивания
//   5003 AD_Contents   AD_Contents_<n>_0                  картинка `start_ad_img0N`
//   5004 AD_PageBtn    AD_PageBtn_OPT0…OPT4 + планы       полоса номеров страниц (шаг 15 px)
//   5005 AD_TodayBtn   AD_TodayBtn + _Text                «не показывать сегодня» (loc 99500)
//   5006 AD_ClsBtn2    AD_ClsBtn2 + _Text                 «Закрыть» (loc 5035)
#include "UI/AsdaStartupAdSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "Login/AsdaButtonRelay.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Math/Box2D.h"            // FBox2f — UV-регион кисти (эталонная обрезка 1024×768)
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/SlateBrush.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStartupAd, Log, All);

namespace
{
	// Окно и его разметка (импорт `Login.JUI`, генератор `Tools/asda_jui_generic_umg.py`).
	const FName  kWindowId(TEXT("AD_Win"));
	const TCHAR* kWbpPath = TEXT("/Game/AsdaAssets/UI/Login/WBP_AD_Win");

	// Таблица с содержимым эталонного `System\AD.bin` (импортёр `Tools/asda_build_startup_ad_dt.py`).
	const TCHAR* kAdTablePath = TEXT("/Game/AsdaAssets/Data/DT_StartupAd.DT_StartupAd");
	const TCHAR* kAdTableRow  = TEXT("1");

	// ── Подписи окна (`error list.bin`, выписаны из `InitControls@0xF01A30`) ──
	constexpr int32 kLocTitle    = 99501;   // «Asda Story news»    → 5002
	constexpr int32 kLocToday    = 99500;   // «Close for today»    → 5005
	constexpr int32 kLocClose    = 5035;    // «Close»              → 5006

	// ── Числа эталона ──
	constexpr double kRotatePeriodSec = 3.0;    // 0xBB8 мс между сменами картинок (`RotateImage`)
	constexpr int32  kPageStepPx      = 15;     // шаг номера страницы (`Open`: ширина = count * 15)
	constexpr int32  kPageHeightPx    = 19;     // высота полосы номеров (`Open`: 0x13)
	constexpr int32  kAdImageW        = 1024;   // прямоугольник картинки в `CControl_DrawTextureRect`
	constexpr int32  kAdImageH        = 768;

	// ★ Файл состояния. Эталон: `fopen("<CurDir>\System\AD.bin", "rt"/"wt")` — рядом с .exe игры.
	//   В UE туда писать нельзя (нет прав в Program Files), поэтому берём то же место, что и
	//   `ChatOpt.bin` чата, — `FPaths::ProjectSavedDir()` (штатный каталог пользовательских настроек).
	FString AdBinPath() { return FPaths::ProjectSavedDir() / TEXT("AD.bin"); }
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaStartupAdSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Тикер — порт `Tick@0xF02020`: эталон опрашивает окно каждый кадр и сам сравнивает
	//   `timeGetTime() - +0x28` с 3000 мс. Таймером это не заменить: период эталона считается от
	//   МОМЕНТА ПОКАЗА и сбрасывается кликом по номеру страницы.
	RotateTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAsdaStartupAdSubsystem::TickRotate), 0.f);
	UE_LOG(LogAsdaStartupAd, Log,
		TEXT("[стартовая реклама] подсистема поднята; автопоказа НЕТ — в оригинале окно 5000 не зовёт никто "
		     "(см. шапку .h). Ручной показ: asda.UI.StartupAd"));
}

void UAsdaStartupAdSubsystem::Deinitialize()
{
	if (RotateTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RotateTickHandle);
		RotateTickHandle.Reset();
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseWindow(kWindowId); }
	}
	Widget = nullptr;
	bReady = false;
	bShown = false;
	Super::Deinitialize();
}

FString UAsdaStartupAdSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

// ═══════════════════ ПОРТ `LoadConfig@0xF01BB0` / `SaveConfig@0xF01DC0` ════════════════════════
// ЭТАЛОННЫЙ ФОРМАТ (воспроизведён БАЙТ В БАЙТ): пять десятичных чисел, каждое своей строкой.
//   Запись идёт `sprintf("%d\n")` в файле, открытом ТЕКСТОВЫМ режимом (`"wt"`), то есть на Windows
//   каждый `\n` превращается в `\r\n` — ровно поэтому эталонный файл занимает 20 байт:
//     `30 0D 0A 35 0D 0A 32 30 30 38 0D 0A 31 30 0D 0A 33 31 0D 0A` = "0\r\n5\r\n2008\r\n10\r\n31\r\n".
//   Пишем те же `\r\n`, чтобы файл порта и файл оригинала совпадали побайтово.
// ИСТОЧНИКИ, В ПОРЯДКЕ ЭТАЛОНА: сначала файл, который игра сама себе пишет (`Saved/AD.bin`);
//   если его ещё нет (первый запуск) — таблица `DT_StartupAd` с содержимым файла эталонного
//   клиента. Числами в коде настройки НЕ ЗАДАЮТСЯ (данные — из таблицы, не из исходника).
void UAsdaStartupAdSubsystem::LoadConfig()
{
	CfgEnabled = CfgImageCount = CfgYear = CfgMonth = CfgDay = 0;
	CfgSource = TEXT("нет данных");

	FString Text;
	if (FFileHelper::LoadFileToString(Text, *AdBinPath()))
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);
		int32* const Fields[5] = { &CfgEnabled, &CfgImageCount, &CfgYear, &CfgMonth, &CfgDay };
		int32 Read = 0;
		for (int32 i = 0; i < 5 && i < Lines.Num(); ++i)
		{
			// `sscanf("%d")` эталона: ведущие пробелы пропускаются, мусор после числа не мешает.
			const FString S = Lines[i].TrimStartAndEnd();
			if (S.IsEmpty()) { continue; }
			*Fields[i] = FCString::Atoi(*S);
			++Read;
		}
		CfgSource = FString::Printf(TEXT("%s (строк прочитано %d)"), *AdBinPath(), Read);
		return;
	}

	if (const UDataTable* DT = LoadObject<UDataTable>(nullptr, kAdTablePath))
	{
		if (const FAsdaStartupAdRow* R = DT->FindRow<FAsdaStartupAdRow>(FName(kAdTableRow), TEXT("StartupAd"), false))
		{
			CfgEnabled    = R->Enabled;
			CfgImageCount = R->ImageCount;
			CfgYear       = R->SuppressYear;
			CfgMonth      = R->SuppressMonth;
			CfgDay        = R->SuppressDay;
			CfgSource     = TEXT("DT_StartupAd");
			return;
		}
		UE_LOG(LogAsdaStartupAd, Warning,
			TEXT("[стартовая реклама] в DT_StartupAd нет строки «%s» — настройки не прочитаны"), kAdTableRow);
		return;
	}
	UE_LOG(LogAsdaStartupAd, Warning,
		TEXT("[стартовая реклама] нет ни %s, ни таблицы %s — окно показать не из чего "
		     "(таблицу создаёт Tools/asda_build_startup_ad_dt.py)"), *AdBinPath(), kAdTablePath);
}

void UAsdaStartupAdSubsystem::SaveConfig() const
{
	const FString Text = FString::Printf(TEXT("%d\r\n%d\r\n%d\r\n%d\r\n%d\r\n"),
		CfgEnabled, CfgImageCount, CfgYear, CfgMonth, CfgDay);
	if (FFileHelper::SaveStringToFile(Text, *AdBinPath(), FFileHelper::EEncodingOptions::ForceAnsi))
	{
		UE_LOG(LogAsdaStartupAd, Log, TEXT("[стартовая реклама] AD.bin записан (%d %d %d-%d-%d) → %s"),
			CfgEnabled, CfgImageCount, CfgYear, CfgMonth, CfgDay, *AdBinPath());
	}
	else
	{
		// Эталон об ошибке записи молчит (`if (Stream)` и всё) — у нас хотя бы строка в журнале.
		UE_LOG(LogAsdaStartupAd, Error, TEXT("[стартовая реклама] НЕ УДАЛОСЬ записать %s"), *AdBinPath());
	}
}

// ═════════════════════════ ОКНО: СОЗДАНИЕ И РАЗБОР ДЕРЕВА ═════════════════════════════════════
bool UAsdaStartupAdSubsystem::EnsureWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(Widget) && bReady) { return true; }
	if (Widget)
	{
		WM->CloseWindow(kWindowId);
		Widget = nullptr; bReady = false; bShown = false;
	}
	Widget = WM->OpenWindowByPath(kWindowId, kWbpPath, /*ZOrder*/ 0, ESlateVisibility::SelfHitTestInvisible);
	if (!Widget)
	{
		UE_LOG(LogAsdaStartupAd, Error, TEXT("[стартовая реклама] не загрузился виджет %s"), kWbpPath);
		return false;
	}
	SetupControls();
	// `AlwaysTop="1"` у 5000 в `Login.JUI` — окно живёт в приоритетной полосе, а не поднимается кликом.
	WM->SetWindowPriority(kWindowId, true);
	// Эталон держит окно спрятанным (`Visible="0"` в .JUI) до самого `Open`.
	WM->SetWindowVisible(kWindowId, false);
	bShown = false;
	return bReady;
}

// ★ Порт `InitControls@0xF01A30`. Отличие только в способе поиска: у эталона это
//   `CWindowMgr_FindControlByID(login-менеджер, IDNum)`, у нас — имена узлов импортёра
//   (закон `jui-importer-widget-naming`: интерактив носит точное имя idc, планы — `{idc}_{i}_{p}`).
void UAsdaStartupAdSubsystem::SetupControls()
{
	bReady = false;
	Relays.Reset();
	PageButtons.Reset();
	PagePlanes.Reset();
	PageCaptions.Reset();
	PageIdleBrush.Reset();
	PageIdleStyle.Reset();
	PageIdleCaption.Reset();
	if (!Widget || !Widget->WidgetTree) { return; }

	CloseBtn  = Cast<UButton>(AsdaUi::FindNodeExact(Widget, TEXT("AD_ClsBtn")));
	TodayBtn  = Cast<UButton>(AsdaUi::FindNodeExact(Widget, TEXT("AD_TodayBtn")));
	CloseBtn2 = Cast<UButton>(AsdaUi::FindNodeExact(Widget, TEXT("AD_ClsBtn2")));
	TitleText = Cast<UTextBlock>(AsdaUi::FindNodeExact(Widget, TEXT("AD_Title_Move")));
	// 5003: у контрола-картинки точного имени нет — импортёр даёт ему имя плана `AD_Contents_<i>_0`.
	ContentsImage = Cast<UImage>(AsdaUi::FindNodeByPrefix(Widget, TEXT("AD_Contents_")));

	// ── Полоса номеров 5004: кнопки `AD_PageBtn_OPT<i>` и планы `AD_PageBtn_<i>_<план>` ──
	//   Сбор ровно тем же способом, что у прочих OPT-полос порта (`SetOptSelected` окна друзей):
	//   у кнопки хвост после `_OPT` обязан быть ЧИСТО ЧИСЛОВЫМ, иначе в список попали бы
	//   `_OPT0_HIT` и `_OPT0_Text`.
	{
		TArray<TObjectPtr<UButton>> Btns;
		TArray<TObjectPtr<UImage>>  Planes;
		const FString OptPref = TEXT("AD_PageBtn_OPT");
		Widget->WidgetTree->ForEachWidget([&Btns, &Planes, &OptPref](UWidget* W)
		{
			if (!W) { return; }
			const FString N = W->GetName();
			if (UButton* B = Cast<UButton>(W))
			{
				if (!N.StartsWith(OptPref, ESearchCase::IgnoreCase)) { return; }
				const FString Tail = N.RightChop(OptPref.Len());
				if (Tail.IsEmpty()) { return; }
				for (const TCHAR C : Tail) { if (!FChar::IsDigit(C)) { return; } }
				const int32 I = FCString::Atoi(*Tail);
				if (I < 0 || I >= 32) { return; }
				if (Btns.Num() <= I) { Btns.SetNum(I + 1); }
				Btns[I] = B;
				return;
			}
			if (UImage* Img = Cast<UImage>(W))
			{
				FString Idc; int32 JuiIdx = 0, Plane = 0;
				if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane)) { return; }
				if (!Idc.Equals(TEXT("AD_PageBtn"), ESearchCase::IgnoreCase)) { return; }
				if (Plane < 0 || Plane >= 32) { return; }
				if (Planes.Num() <= Plane) { Planes.SetNum(Plane + 1); }
				Planes[Plane] = Img;
			}
		});
		PageButtons = MoveTemp(Btns);
		PagePlanes  = MoveTemp(Planes);
		// Подписей у номеров страниц в `Login.JUI` нет (`MdNum_*` — картинки цифр), список остаётся
		//   пустым НАМЕРЕННО: `ApplyTabState` принимает пустой массив подписей.
	}

	// ── Подписи из loc (эталон кладёт их ровно в эти три контрола) ──
	if (TitleText)
	{
		const FString S = Loc(kLocTitle);
		if (!S.IsEmpty()) { TitleText->SetText(FText::FromString(S)); }
		TitleText->SetAutoWrapText(false);   // закон `ui-autowrap-canvas-gotcha`
	}
	auto SetCaption = [this](const TCHAR* Node, int32 LocId)
	{
		if (UTextBlock* T = Cast<UTextBlock>(AsdaUi::FindNodeExact(Widget, Node)))
		{
			const FString S = Loc(LocId);
			if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
			T->SetAutoWrapText(false);
		}
	};
	SetCaption(TEXT("AD_TodayBtn_Text"), kLocToday);
	SetCaption(TEXT("AD_ClsBtn2_Text"),  kLocClose);

	// ── Кнопки → мостики ──
	auto Bind = [this](UButton* B, int32 Idc, TFunction<void(int32)> Cb)
	{
		if (!B) { return; }
		AsdaUi::BindButton(this, B, Idc, MoveTemp(Cb), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(CloseBtn,  5001, [this](int32) { CloseStartupAd(); });
	Bind(TodayBtn,  5005, [this](int32) { SuppressForToday(); });
	Bind(CloseBtn2, 5006, [this](int32) { CloseStartupAd(); });
	for (int32 i = 0; i < PageButtons.Num(); ++i)
	{
		// Клик по номеру страницы — порт `SetImageIndex@0xF02150` (он же сбрасывает отсчёт 3 с).
		Bind(PageButtons[i].Get(), i, [this](int32 Idx) { SetImageIndex(Idx); });
	}

	// Готовность = эталонный флаг +0x44: без картинки и кнопки закрытия окно бессмысленно.
	bReady = (ContentsImage != nullptr) && (CloseBtn != nullptr || CloseBtn2 != nullptr);
	if (!bReady)
	{
		UE_LOG(LogAsdaStartupAd, Error,
			TEXT("[стартовая реклама] узлы окна 5000 не собраны: 5003 %s, 5001 %s, 5006 %s, номеров %d"),
			ContentsImage ? TEXT("есть") : TEXT("НЕТ"), CloseBtn ? TEXT("есть") : TEXT("НЕТ"),
			CloseBtn2 ? TEXT("есть") : TEXT("НЕТ"), PageButtons.Num());
	}
}

// ═══════════════════════════ ПОРТ `Open@0xF02220` (оба гейта) ═════════════════════════════════
bool UAsdaStartupAdSubsystem::ShowStartupAd()
{
	if (!EnsureWidget()) { return false; }

	LoadConfig();                       // эталон: `LoadConfig(this, this + 0x30)`
	ImageCount = CfgImageCount;         // эталон: `+0x48 = +0x34`
	ApplyPageCount(ImageCount);         // эталон: полосе номеров `SetSize(count * 15, 19)`

	const FDateTime Now = FDateTime::Now();   // эталон: `GetLocalTime(&st)` — ЛОКАЛЬНОЕ время

	// ГЕЙТ 1 — признак показа (эталон читает МЛАДШИЙ БАЙТ поля +0x30: `movzx ecx, byte ptr [eax+0x30]`).
	if ((CfgEnabled & 0xFF) == 0)
	{
		UE_LOG(LogAsdaStartupAd, Log,
			TEXT("[стартовая реклама] окно не показано: признак показа = %d (источник настроек: %s). "
			     "Это поведение эталонного клиента — в его AD.bin стоит 0."), CfgEnabled, *CfgSource);
		return false;
	}
	// ГЕЙТ 2 — «сегодня уже отказались» (эталон сравнивает Y, M, D по отдельности).
	if (CfgYear == Now.GetYear() && CfgMonth == Now.GetMonth() && CfgDay == Now.GetDay())
	{
		UE_LOG(LogAsdaStartupAd, Log,
			TEXT("[стартовая реклама] окно не показано: на %04d-%02d-%02d уже нажимали «не показывать сегодня»"),
			CfgYear, CfgMonth, CfgDay);
		return false;
	}

	bShown = true;                                  // эталон: `+0x2C = 1`
	LastRotateSec = FPlatformTime::Seconds();       // эталон: `+0x28 = timeGetTime()`
	SetImageIndex(0);                               // эталон: `SetImageIndex(this, 0)`

	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, true);      // эталон: `CControl_SetVisible(window, 1)`
		WM->BringWindowToFront(kWindowId);          // эталон: регистрация окна в менеджере (`sub_12AFBB0`)
	}
	UE_LOG(LogAsdaStartupAd, Log,
		TEXT("[стартовая реклама] окно 5000 показано: картинок %d, источник настроек %s"),
		ImageCount, *CfgSource);
	return true;
}

// ═════════════════════════ ПОРТ `Close@0xF02390` (кнопки 5001 / 5006) ═════════════════════════
void UAsdaStartupAdSubsystem::CloseStartupAd()
{
	bShown = false;                                 // эталон: `+0x2C = 0`
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);     // эталон: `CControl_SetVisible(window, 0)`
	}
	UE_LOG(LogAsdaStartupAd, Log, TEXT("[стартовая реклама] окно 5000 закрыто"));
}

// ═══════════════════ ПОРТ `CloseForToday@0xF02320` (кнопка 5005) ══════════════════════════════
// ★ 1:1: оригинал ставит признак показа в 1, записывает СЕГОДНЯШНЮЮ дату и сохраняет файл —
//   и НЕ прячет окно. Закрывает его игрок сам крестиком или «Закрыть». Не «улучшаем».
void UAsdaStartupAdSubsystem::SuppressForToday()
{
	const FDateTime Now = FDateTime::Now();
	CfgEnabled = 1;                                 // эталон: `*(BYTE*)(this + 0x30) = 1`
	CfgYear  = Now.GetYear();
	CfgMonth = Now.GetMonth();
	CfgDay   = Now.GetDay();
	SaveConfig();
	UE_LOG(LogAsdaStartupAd, Log,
		TEXT("[стартовая реклама] «не показывать сегодня»: дата %04d-%02d-%02d записана; окно, как в оригинале, остаётся открытым"),
		CfgYear, CfgMonth, CfgDay);
}

// ═══════════════════════ ПОРТ `SetImageIndex@0xF02150` ════════════════════════════════════════
void UAsdaStartupAdSubsystem::SetImageIndex(int32 Index)
{
	// Эталонный кламп ровно в этом порядке: сначала «больше или равно счётчику → 0», потом
	//   «меньше нуля → 0». При `ImageCount == 0` оба условия дают 0 — как в оригинале.
	CurrentIndex = Index;
	if (CurrentIndex >= ImageCount) { CurrentIndex = 0; }
	if (CurrentIndex < 0)           { CurrentIndex = 0; }

	ApplyImage(CurrentIndex);

	// `CControl_SetSelectedIndex(page, index)` — у нас это общая точка подсветки OPT-полосы.
	if (PageButtons.Num() > 0)
	{
		AsdaUi::ApplyTabState(PagePlanes, PageCaptions, PageButtons,
			FMath::Clamp(CurrentIndex, 0, PageButtons.Num() - 1),
			PageIdleBrush, PageIdleStyle, PageIdleCaption);
	}
	// Отсчёт до автоматической смены начинается заново (эталон перевзводит `+0x28` в `Open`,
	//   а `RotateImage` — на каждой смене; ручной выбор страницы попадает в ту же логику).
	LastRotateSec = FPlatformTime::Seconds();
}

// ═══════════════════════ ПОРТ `RotateImage@0xF02060` + `Tick@0xF02020` ════════════════════════
bool UAsdaStartupAdSubsystem::TickRotate(float /*DeltaSec*/)
{
	// Эталон: `if (+0x44 && +0x2C) RotateImage();` — тикает только готовое и показанное окно.
	if (!bReady || !bShown) { return true; }
	if (ImageCount <= 0)    { return true; }   // делить нечего: цикл `++index >= count` вырождается
	const double Now = FPlatformTime::Seconds();
	if (Now - LastRotateSec <= kRotatePeriodSec) { return true; }

	LastRotateSec = Now;                              // эталон: `+0x28 = timeGetTime()`
	if (++CurrentIndex >= ImageCount) { CurrentIndex = 0; }
	ApplyImage(CurrentIndex);
	if (PageButtons.Num() > 0)
	{
		AsdaUi::ApplyTabState(PagePlanes, PageCaptions, PageButtons,
			FMath::Clamp(CurrentIndex, 0, PageButtons.Num() - 1),
			PageIdleBrush, PageIdleStyle, PageIdleCaption);
	}
	return true;
}

// ═════════════════ ПОЛОСА НОМЕРОВ: ширина `count * 15`, высота 19 (порт `Open`) ═══════════════
void UAsdaStartupAdSubsystem::ApplyPageCount(int32 Count)
{
	// ЧТО ДЕЛАЕТ ЭТАЛОН: `SetSize(count * 15, 19)` контролу 5004. Смысл именно такой — полоса
	//   нарисована на ПЯТЬ цифр с шагом 15 px (`Login.JUI`: `MdNum_1…MdNum_5` в позициях
	//   3, 18, 33, 48, 63), и сужение до `count * 15` ОБРЕЗАЕТ лишние цифры справа.
	// ЧТО ДЕЛАЕМ МЫ: у импортёра пять цифр — это пять ОТДЕЛЬНЫХ узлов (планы `AD_PageBtn_<i>_0…_4`
	//   и кнопки `AD_PageBtn_OPT0…OPT4`), обрезать их шириной родителя нечем. Поэтому гасим ровно
	//   те, что эталон оставил бы за правым краем: видимый результат тот же.
	//   ⚠ Ширину какого-либо соседнего узла НЕ трогаем: точного соответствия контролу 5004 среди
	//   узлов виджета нет, а «похожий по имени» узел — та самая эвристика, которой быть не должно
	//   (закон `never-invent-only-original`). Серая подложка полосы (77 px) в оригинале тоже
	//   не сужается — сужается только контрол цифр.
	for (int32 i = 0; i < PageButtons.Num(); ++i)
	{
		if (PageButtons[i])
		{
			PageButtons[i]->SetVisibility(i < Count ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}
	for (int32 i = 0; i < PagePlanes.Num(); ++i)
	{
		if (PagePlanes[i])
		{
			PagePlanes[i]->SetVisibility(i < Count ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
	if (PageButtons.Num() > 0 && Count > PageButtons.Num())
	{
		// Данные обещают больше страниц, чем нарисовано в `Login.JUI`. Эталон в этом случае просто
		//   расширил бы контрол, но рисовать ему всё равно было бы нечем (цифр в скине пять).
		UE_LOG(LogAsdaStartupAd, Warning,
			TEXT("[стартовая реклама] в настройках %d картинок, а полоса номеров в Login.JUI рассчитана на %d "
			     "(шаг %d px, высота %d px) — лишние номера показать нечем"),
			Count, PageButtons.Num(), kPageStepPx, kPageHeightPx);
	}
}

// ═════════════ КАРТИНКА `start_ad_img0N` В КОНТРОЛ 5003 (порт `CControl_DrawTextureRect`) ═════
void UAsdaStartupAdSubsystem::ApplyImage(int32 Index)
{
	if (!ContentsImage) { return; }
	// Эталон: `sprintf(buf, "start_ad_img0%d", index)` → `TexturePath_Format` добавляет «.tex» →
	//   текстура ищется в паке интерфейса. У нас та же точка — менеджер текстур по короткому имени.
	const FString Name = FString::Printf(TEXT("start_ad_img0%d"), Index);
	UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(FName(*Name)) : nullptr;
	if (!Tex)
	{
		// ⚠ В эталонном клиенте таких текстур НЕТ НИ ОДНОЙ (строка `start_ad_img` встречается только
		//   внутри .exe). Это не поломка порта, а состояние исходных данных — говорим прямо.
		UE_LOG(LogAsdaStartupAd, Warning,
			TEXT("[стартовая реклама] нет текстуры «%s» — в эталонном клиенте картинок start_ad_img тоже нет"),
			*Name);
		return;
	}

	// Эталон рисует ПРЯМОУГОЛЬНИК 0,0 … 1024,768 текстуры (`planeFrame_setUV` в пикселях), а не
	//   всю текстуру. Совпадает с «растянуть целиком» только у картинки ровно 1024×768; для любой
	//   другой воспроизводим именно обрезку — через UV-регион кисти.
	const float W = FMath::Max(1.f, (float)Tex->GetSizeX());
	const float H = FMath::Max(1.f, (float)Tex->GetSizeY());
	FSlateBrush Br;
	Br.SetResourceObject(Tex);
	Br.DrawAs = ESlateBrushDrawType::Image;
	Br.ImageSize = FVector2D(W, H);
	Br.SetUVRegion(FBox2f(FVector2f(0.f, 0.f),
		FVector2f(FMath::Min(1.f, kAdImageW / W), FMath::Min(1.f, kAdImageH / H))));
	ContentsImage->SetBrush(Br);
}

// ═════════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════
FString UAsdaStartupAdSubsystem::DebugState() const
{
	return FString::Printf(
		TEXT("[стартовая реклама] источник настроек: %s | признак показа=%d картинок=%d дата отказа=%04d-%02d-%02d\n")
		TEXT("  виджет %s, узлы собраны=%s, окно на экране=%s, текущая картинка=%d из %d\n")
		TEXT("  узлы: 5003 %s | 5001 %s | 5005 %s | 5006 %s | 5002 %s | номеров кнопок %d, планов %d"),
		*CfgSource, CfgEnabled, CfgImageCount, CfgYear, CfgMonth, CfgDay,
		Widget ? TEXT("создан") : TEXT("НЕТ"), bReady ? TEXT("да") : TEXT("нет"),
		bShown ? TEXT("да") : TEXT("нет"), CurrentIndex, ImageCount,
		ContentsImage ? TEXT("есть") : TEXT("НЕТ"), CloseBtn ? TEXT("есть") : TEXT("НЕТ"),
		TodayBtn ? TEXT("есть") : TEXT("НЕТ"), CloseBtn2 ? TEXT("есть") : TEXT("НЕТ"),
		TitleText ? TEXT("есть") : TEXT("НЕТ"), PageButtons.Num(), PagePlanes.Num());
}

// ═══════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ — ★ НАША ДОБАВКА ══════════════════════════════════
// В оригинале точки показа НЕТ (см. шапку .h), поэтому единственный способ увидеть окно живьём —
//   позвать его руками. Автопоказа эти команды НЕ включают и в игровой поток не встраиваются.
namespace
{
	UAsdaStartupAdSubsystem* ConsAdSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaStartupAdSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GConsStartupAdShow(
	TEXT("asda.UI.StartupAd"),
	TEXT("asda.UI.StartupAd — показать стартовое окно новостей 5000 тем же путём, что и Open@0xF02220 "
	     "(оба гейта эталона работают: при признаке показа 0 окно не появится)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaStartupAdSubsystem* S = ConsAdSub(W);
		if (!S) { UE_LOG(LogAsdaStartupAd, Warning, TEXT("подсистема недоступна")); return; }
		S->ShowStartupAd();
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GConsStartupAdClose(
	TEXT("asda.UI.StartupAdClose"),
	TEXT("asda.UI.StartupAdClose — закрыть окно 5000 (ветка кнопок 5001/5006)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaStartupAdSubsystem* S = ConsAdSub(W)) { S->CloseStartupAd(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GConsStartupAdDump(
	TEXT("asda.UI.StartupAdDump"),
	TEXT("asda.UI.StartupAdDump — состояние стартовой рекламы: откуда настройки, что нашлось в окне"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaStartupAdSubsystem* S = ConsAdSub(W);
		if (!S) { UE_LOG(LogAsdaStartupAd, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaStartupAd, Log, TEXT("%s"), *S->DebugState());
	}));
#endif // ASDA_DEV_CONSOLE
