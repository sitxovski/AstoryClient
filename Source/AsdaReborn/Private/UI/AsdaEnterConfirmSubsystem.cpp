// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEnterConfirmSubsystem.cpp — Enter нажимает безвредную кнопку согласия верхнего окна; ESC под модалкой реальной клавишей.
//   [ИСПРАВЛЕНИЕ] 25.10.2026 (воля владельца), редакция 2 (3.0.1) по ревизии `Reports/wave_1025c_review/REVIEW.md`.
//   Разбор оригинала и правила — в шапке `Public/UI/AsdaEnterConfirmSubsystem.h`; аудит окон — `Reports/enter_confirm/AUDIT.md`.
#include "UI/AsdaEnterConfirmSubsystem.h"
#include "AsdaPerf.h"                          // ★ R4-b 14.11.2026: области замера времени игрового потока
#include "AsdaDevConsole.h"                    // ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaEnterGuard.h"                 // пометка «без Enter» окон-вопросов и номер показа MESSAGE2
#include "UI/AsdaWindowSubsystem.h"            // реестр окон: видимость, z, модалки, эталонная ESC-таблица, очередь окон-сообщений
#include "UI/AsdaEscChain.h"                   // ★ 25.10.2026: шаг «ESC снимает цель» (под модалкой и из поля ввода)
#include "UI/AsdaChatWindowSubsystem.h"        // идёт ли набор в чате
#include "UI/AsdaDevMenuSubsystem.h"           // dev-меню Ctrl+N (свои клавиши)
#include "UI/AsdaSystemOptionSubsystem.h"      // захват клавиши в окне настроек 69500 (Enter — назначаемая клавиша)
#include "UI/AsdaUiVisibilitySubsystem.h"      // Alt+Z: интерфейс скрыт — невидимые окна не подтверждаются
#include "UI/AsdaNotice.h"                     // эхо консольных команд
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEnter, Log, All);

// Ключ фокуса поля 1412 окна количества (`asda.UI.NumPadFocus`, определён в AsdaWindowSubsystem_Dialogs.cpp).
bool AsdaNumPadFocusOn();
// Ключ ESC окна ввода текста 1580 (`asda.UI.InputDialogEsc`, определён там же).
bool AsdaInputDialogEscOn();

// Ключ отката: 0 — Enter идёт прежним путём (окна по Enter кнопок не жмут, у оригинала Enter открывает чат).
static ASDA_DEV_CVAR(int32, CVarEnterConfirm,
	TEXT("asda.UI.EnterConfirm"), 1,
	TEXT("1 — [ИСПРАВЛЕНИЕ] Enter (и Enter цифрового блока) нажимает БЕЗВРЕДНУЮ кнопку согласия верхнего окна (модального — первым), ")
	TEXT("на отпускании, одно нажатие — одно действие; вопросы «без Enter», окна младше 400 мс и модалки при открытой строке чата ")
	TEXT("не нажимаются. 0 — как у оригинала: Enter открывает чат (GameInput_DispatchEvent@0xC6DCA0), кнопки окон по Enter не нажимаются."),
	ECVF_Default);

// Порог «окно только что показано», мс: Enter не действует, пока окно (или его содержимое) показано меньше этого времени.
static ASDA_DEV_CVAR(int32, CVarEnterConfirmDelayMs,
	TEXT("asda.UI.EnterConfirmDelayMs"), 400,
	TEXT("Сколько миллисекунд после показа окна или смены его содержимого Enter его не подтверждает (защита от вопроса, пришедшего ")
	TEXT("от сервера во время набора/нажатия). 0 — без задержки."),
	ECVF_Default);

// Ключ отката ESC под модалкой: 0 — как до 25.10 (под модалкой bModal реальная клавиша ESC не доходила до прохода окон).
static ASDA_DEV_CVAR(int32, CVarEscModalKey,
	TEXT("asda.UI.EscModalKey"), 1,
	TEXT("1 — [ИСПРАВЛЕНИЕ] реальная клавиша ESC под модальным окном, забравшим ввод мира, идёт в эталонный проход «ESC закрывает ")
	TEXT("верхнее окно» (TWOBUT 1520 → «Отмена» 1524, ONEBUT 1500 — съедается), как у оригинала; 0 — ESC под такой модалкой не работает."),
	ECVF_Default);

// ══════ ПОМЕТКА «БЕЗ ENTER» (AsdaEnterGuard.h) ══════
namespace AsdaEnterGuard
{
	static bool GArmedNoEnter = false;     // взведена пометка для следующего показа окна-вопроса
	static bool GConfirmNoEnter = false;   // текущий показ MESSAGE2 помечен
	static uint32 GConfirmSerial = 0;      // номер текущего показа MESSAGE2

	void ArmNextNoEnter() { GArmedNoEnter = true; }
	bool ConsumeNextNoEnter() { const bool b = GArmedNoEnter; GArmedNoEnter = false; return b; }
	void NoteConfirmShown(bool bNoEnter) { GConfirmNoEnter = bNoEnter; ++GConfirmSerial; }
	bool IsConfirmNoEnter() { return GConfirmNoEnter; }
	uint32 GetConfirmSerial() { return GConfirmSerial; }
}

namespace AsdaEnterConfirm
{
	// ══════ ТАБЛИЦА «ОКНО → КНОПКА СОГЛАСИЯ» (редакция 3.0.1: ТОЛЬКО безвредное и обратимое согласие) ══════
	//   Окно — ключ реестра окон (= ID корневого контрола .JUI). Кнопки — idc .JUI (= имя UButton в WBP), ПО ПРИОРИТЕТУ:
	//   берётся первая ВИДИМАЯ (сама Visible, предки не скрыты). Видимая, но недоступная кнопка — «нажать нечего».
	//   ★ Правило отбора (ревизия ВАЖНО-1/2/4в): окна, где кнопка СРАЗУ делает необратимое или тратит деньги/предметы (роспуск и
	//     расширение клана, создание клана, разбор, знак зодиака, магазин NPC, склад, консигнация, обмен, лавка-покупатель, кузнец,
	//     перенос опций, ремесло, питомцы, премиум, гача, книга рыбака, знаки охоты, налог), и входящие от сервера (приглашение в
	//     отряд, запрос соулмейта) — ИСКЛЮЧЕНЫ: их Enter не трогает (прежний путь — чат). Полный список с причинами — AUDIT.md §3.
	//   Окна-вопросы TWOBUT/MESSAGE2 общие: необратимые и входящие вопросы помечены вызывающим «без Enter» (AsdaEnterGuard).
	//   Wbp/Wbp2 — ассет(ы) окна: самотест сверяет таблицу с ассетами (`VerifyTableAgainstAssets`).
	struct FRow
	{
		const TCHAR* Window;        // ключ реестра окон
		const TCHAR* Wbp;           // путь WBP (без суффикса класса)
		const TCHAR* Wbp2;          // второй вид того же окна (nullptr — нет)
		const TCHAR* Buttons[3];    // кнопки-кандидаты по приоритету (nullptr — конец)
	};

	static const TCHAR* const kUi = TEXT("/Game/AsdaAssets/UI/");   // корень UI-ассетов (для читаемости строк ниже)

	static const FRow kRows[] =
	{
		// ── Общие окна-сообщения и ввод ──
		{ TEXT("ONEBUT_MSGBOX"),          TEXT("Player_UI/Common/WBP_ONEBUT_MSGBOX"),          nullptr, { TEXT("ONEBUT_BUT00") } },              // 1503 «ОК» (информационное)
		{ TEXT("TWOBUT_MSGBOX"),          TEXT("Player_UI/Common/WBP_TWOBUT_MSGBOX"),          nullptr, { TEXT("TWOBUT_BUT00") } },              // 1523 «ОК/Да» — кроме записей «без Enter»
		{ TEXT("NUMPAD_WIN"),             TEXT("Player_UI/Common/WBP_NUMPAD_WIN"),             nullptr, { TEXT("NUMPAD_OK") } },                 // 1415 подтверждение ввода количества
		{ TEXT("SHOUTING_WIN"),           TEXT("Player_UI/Social/WBP_SHOUTING_WIN"),           nullptr, { TEXT("Shouting_OKBtn") } },            // 1584 подтверждение ввода текста
		{ TEXT("COMMONWIN_MSGBOX"),       TEXT("Player_UI/Common/WBP_COMMONWIN_MSGBOX"),       TEXT("CommonWin/WBP_COMMONWIN_MSGBOX_CommonWin"), { TEXT("COMMONWIN_BUT") } },   // 8003 обрыв связи
		{ TEXT("BENCH_WIN"),              TEXT("CommonWin/WBP_BENCH_WIN"),                     nullptr, { TEXT("MESSAGE2_BUT00") } },            // автоподбор графики «ОК»
		// ── Отряд, соулмейт, о себе, сообщество (свои обратимые действия) ──
		{ TEXT("PARTYTAKE_WIN"),          TEXT("Player_UI/Social/WBP_PARTYTAKE_WIN"),          nullptr, { TEXT("PARTYTAKE_YES") } },             // 2252 создать свой отряд
		{ TEXT("SOULMATE_LINK_WIN"),      TEXT("Player_UI/Soulmate/WBP_SOULMATE_LINK_WIN"),    nullptr, { TEXT("SOULMATE_LINK_BTN1") } },        // 9104 отправить предложение (поле 9101 — своё правило)
		{ TEXT("My_Introduction_Win"),    TEXT("Player_UI/Character/WBP_My_Introduction_Win"), nullptr, { TEXT("My_Intro_OKBtn") } },            // 8304 сохранить «о себе»
		{ TEXT("Find_Party_WIn"),         TEXT("Player_UI/Social/WBP_Find_Party_WIn"),         nullptr, { TEXT("Find_Party_Edit") } },           // 8662 правка объявления отряда
		{ TEXT("COMMUNITY_WIN"),          TEXT("Player_UI/Social/WBP_COMMUNITY_WIN"),          nullptr,
			{ TEXT("Soul_Msg_OkBtn"), TEXT("CmtyClan_Sub2_ChangeBtn"), TEXT("CMTY_CLAN_NOITICE_OK") } },                                          // 6603 записка, 35590 звание, 14031 объявление
		// ── Информационные окна наград/итога ──
		{ TEXT("Premium_Pick_mssageBOX"), TEXT("Player_UI/Premium/WBP_Premium_Pick_mssageBOX"),nullptr, { TEXT("Premium_Pick_mssageBOX_ok") } }, // 68452 показ выпавшего приза
		{ TEXT("BOOSTING_WIN"),           TEXT("Player_UI/Trade/WBP_BOOSTING_WIN"),            nullptr, { TEXT("BOOSTER_OK") } },                // 10070 «Подтвердить» после приза (НЕ «Старт»)
		{ TEXT("EPISODEPOP_WIN"),         TEXT("Player_UI/Quest/WBP_EPISODEPOP_WIN"),          nullptr, { TEXT("EPISODEPOP_ENDBUT") } },         // 567 итог эпизода
		{ TEXT("QUEST_Confirm_Win"),      TEXT("Player_UI/Quest/WBP_QUEST_Confirm_Win"),       nullptr, { TEXT("QUEST_Confirm_OK") } },          // 780 принять задание (обратимо: отказ)
		{ TEXT("StreetStall_Open_Win"),   TEXT("Player_UI/Trade/WBP_StreetStall_Open_Win"),    nullptr, { TEXT("Stall_Open_OKBtn") } },          // 22500 открыть свою лавку (закрывается)
		{ TEXT("Title_Win"),              TEXT("Player_UI/Character/WBP_Title_Win"),           nullptr, { TEXT("Title_BtnOk") } },               // 18727 применить звание (меняется обратно)
		// ── Система и чат ──
		{ TEXT("SYSTEM_Option_NEW"),      TEXT("Player_UI/System/WBP_SYSTEM_Option_NEW"),      nullptr, { TEXT("SYSTEM_Option_OK") } },          // 70442
		{ TEXT("CHAT_TAB_WIN"),           TEXT("Player_UI/Chat/WBP_CHAT_TAB_WIN"),             nullptr, { TEXT("CHAT_TAB_WIN_OK") } },           // 2650
		{ TEXT("CHAT_Color_Select_Win"),  TEXT("Player_UI/Chat/WBP_CHAT_Color_Select_Win"),    nullptr, { TEXT("CHAT_Color_Select_OK") } },      // 2735
		// ── Сцена входа (только поведение кода; ассеты и Login.umap не трогаются) ──
		{ TEXT("LOGIN_WIN"),              TEXT("Login/WBP_LOGIN_WIN"),                         nullptr, { TEXT("LOGIN_CONFORM") } },             // вход (поле пароля — своё правило)
		{ TEXT("SERVER_SELECT_WIN"),      TEXT("Login/WBP_SERVER_SELECT_WIN"),                 nullptr, { TEXT("CHANNEL_SELECT_BTN1"), TEXT("SERVER_SELECT_BTN1") } },   // внешняя роспись
		{ TEXT("CHARSELECT_WIN"),         TEXT("Login/WBP_CharSelect"),                        nullptr, { TEXT("CARASTART_BUT00") } },           // войти в мир
		{ TEXT("CARAMAKE_WIN"),           TEXT("Login/WBP_CARAMAKE_WIN"),                      nullptr, { TEXT("CARAMAKE_OKBUT") } },            // «Создать»
		{ TEXT("MESSAGE_WIN"),            TEXT("Login/WBP_MESSAGE_WIN"),                       nullptr, { TEXT("MESSAGE_BUT") } },               // «ОК»
		{ TEXT("MESSAGE2_WIN"),           TEXT("Login/WBP_MESSAGE2_WIN"),                      TEXT("Login/WBP_MESSAGE2_WIN_NEUTRAL"), { TEXT("MESSAGE2_BUT00") } },   // «Да» — кроме «без Enter» (удаление персонажа)
	};

	// ★ R4-b 14.11.2026 [ПОРТ] (R4, доля b, B3): имена окон и кнопок таблицы — `FName` один раз на процесс. Прежде тик каждый кадр строил
	//   `FName` из строки для каждой из строк таблицы (и для кнопок видимых окон). Решения те же: `FName` из той же строки — то же имя.
	// ★ R4-b закрытие (15.11.2026): ключ `asda.Perf.EnterConfirmNames` — старый путь и ключ удалены — только новое (решение владельца).
	struct FRowNames { FName Window; FName Buttons[3]; };   // имена строки таблицы (кнопки: NAME_None — конец)
	static const TArray<FRowNames>& RowNames()
	{
		static const TArray<FRowNames> N = []()
		{
			TArray<FRowNames> A;
			for (const FRow& Rw : kRows)
			{
				FRowNames X;
				X.Window = FName(Rw.Window);
				for (int32 J = 0; J < 3; ++J) { X.Buttons[J] = Rw.Buttons[J] ? FName(Rw.Buttons[J]) : FName(NAME_None); }
				A.Add(X);
			}
			return A;
		}();
		return N;
	}

	static const FName kOneBut(TEXT("ONEBUT_MSGBOX"));   // окна очереди окон-сообщений (содержимое = номер записи)
	static const FName kTwoBut(TEXT("TWOBUT_MSGBOX"));
	static const FName kMsg2(TEXT("MESSAGE2_WIN"));      // окно подтверждений сцены входа и др. (содержимое = номер показа)

	// Окна, которые в «верхнее окно» не считаются: сумка — спутник магазинов/склада/ремесла (её щёлкают, перекладывая предметы,
	//   и она поднимается над окном согласия), подсказки — всплывают под курсором сами.
	static bool IsIgnoredForTop(FName Id)
	{
		static const FName kIgnored[] = { FName(TEXT("INVEN_WIN")), FName(TEXT("SOWEL_Tooltip")), FName(TEXT("Pet_Tooltips_Win")) };
		for (const FName& N : kIgnored) { if (N == Id) { return true; } }
		return false;
	}

	// Строка таблицы окна (−1 — окна в таблице нет).
	static int32 RowOf(FName Id)
	{
		for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(kRows); ++i)
		{
			if (Id == FName(kRows[i].Window)) { return i; }
		}
		return -1;
	}

	// Мышь могла бы нажать кнопку: сама Visible (хит-тестируется), ни один предок не скрыт и не прозрачен для мыши.
	static bool IsClickableChain(const UWidget* W)
	{
		if (!W || W->GetVisibility() != ESlateVisibility::Visible) { return false; }
		for (const UWidget* P = W->GetParent(); P; P = P->GetParent())
		{
			const ESlateVisibility V = P->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden || V == ESlateVisibility::HitTestInvisible) { return false; }
		}
		return true;
	}

	// Доступна ли кнопка: Slate гасит ввод, если выключена она сама или любой предок.
	static bool IsEnabledChain(const UWidget* W)
	{
		for (const UWidget* P = W; P; P = P->GetParent())
		{
			if (!P->GetIsEnabled()) { return false; }
		}
		return true;
	}

	// Фокус клавиатуры в поле ввода (правила Enter такого поля — у окна).
	static bool IsTextFieldFocused()
	{
		if (!FSlateApplication::IsInitialized()) { return false; }
		const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
		return F.IsValid() && F->GetTypeAsString().Contains(TEXT("EditableText"));
	}

	// Фокус клавиатуры — в поле 1412 окна количества 1410 (при ключе `asda.UI.NumPadFocus`; ESC — отмена окна, как у эталона).
	static bool IsNumPadFieldFocused(const UAsdaWindowSubsystem* WM)
	{
		if (!WM || !AsdaNumPadFocusOn() || !FSlateApplication::IsInitialized()) { return false; }
		const UUserWidget* Root = WM->FindWindow(FName(TEXT("NUMPAD_WIN")));
		const UWidget* Field = Root ? Root->GetWidgetFromName(TEXT("NUMPAD_NUMBOX")) : nullptr;
		const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
		return Field && F.IsValid() && F == Field->GetCachedWidget();
	}

	// Корень окна видим для игрока: виджет показан и его обёртка вьюпорта не спрятана Alt+Z.
	static bool IsRootShown(const UUserWidget* W)
	{
		if (!W || !W->IsInViewport()) { return false; }
		const ESlateVisibility V = W->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		return UAsdaUiVisibilitySubsystem::IsViewportHostVisible(W);
	}
}

// ══════ ПРЕПРОЦЕССОР ВВОДА SLATE ══════
//   Видит клавишу раньше маршрутизации (фокус → виджет → вьюпорт → `UPlayerInput`), как Alt+Z (`UAsdaUiVisibilitySubsystem`) и
//   Ctrl+N (`UAsdaDevMenuSubsystem`). Работает в любом режиме ввода — и под модалкой `FInputModeUIOnly`, где вьюпорт клавиш
//   не получает. Только Enter и (под модалкой, забравшей ввод мира) ESC; остальное проходит нетронутым.
class FAsdaEnterConfirmKeyProc : public IInputProcessor
{
public:
	TWeakObjectPtr<UAsdaEnterConfirmSubsystem> Owner;   // владелец решений
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Ev) override
	{
		if (!Owner.IsValid()) { return false; }
		if (Ev.GetKey() == EKeys::Enter) { return Owner->HandleEnterDown(Ev); }
		if (Ev.GetKey() == EKeys::Escape) { return Owner->HandleEscDown(Ev); }
		return false;
	}
	virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Ev) override
	{
		return Ev.GetKey() == EKeys::Enter && Owner.IsValid() && Owner->HandleEnterUp(Ev);
	}
	virtual const TCHAR* GetDebugName() const override { return TEXT("AsdaEnterConfirm"); }
};

void UAsdaEnterConfirmSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (FSlateApplication::IsInitialized())
	{
		KeyProc = MakeShared<FAsdaEnterConfirmKeyProc>();
		KeyProc->Owner = this;
		FSlateApplication::Get().RegisterInputPreProcessor(KeyProc);
	}
}

void UAsdaEnterConfirmSubsystem::Deinitialize()
{
	if (KeyProc.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(KeyProc);
	}
	KeyProc.Reset();
	Externals.Reset();
	Shown.Reset();
	Super::Deinitialize();
}

const TCHAR* UAsdaEnterConfirmSubsystem::VerdictName(EVerdict V)
{
	switch (V)
	{
	case EVerdict::Off:       return TEXT("ключ выключен");
	case EVerdict::TextField: return TEXT("поле ввода");
	case EVerdict::Chat:      return TEXT("набор в чате");
	case EVerdict::DevMenu:   return TEXT("dev-меню/захват клавиши");
	case EVerdict::NoTarget:  return TEXT("окна согласия нет");
	case EVerdict::Disabled:  return TEXT("кнопка недоступна");
	case EVerdict::Forbidden: return TEXT("вопрос без Enter");
	case EVerdict::TooEarly:  return TEXT("окно только что показано");
	case EVerdict::Press:     return TEXT("нажать");
	}
	return TEXT("?");
}

bool UAsdaEnterConfirmSubsystem::IsWindowInTable(FName WindowId)
{
	return AsdaEnterConfirm::RowOf(WindowId) >= 0;
}

double UAsdaEnterConfirmSubsystem::GetShowDelaySec()
{
	return FMath::Max(0, CVarEnterConfirmDelayMs.GetValueOnGameThread()) / 1000.0;
}

void UAsdaEnterConfirmSubsystem::RegisterExternalWindow(const UObject* Ctx, FName WindowId, UUserWidget* Widget, int32 ZOrder)
{
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaEnterConfirmSubsystem* S = GI ? GI->GetSubsystem<UAsdaEnterConfirmSubsystem>() : nullptr;
	if (!S || WindowId.IsNone() || !Widget) { return; }
	FExternal& E = S->Externals.FindOrAdd(WindowId);
	E.Widget = Widget;
	E.ZOrder = ZOrder;
}

void UAsdaEnterConfirmSubsystem::UnregisterExternalWindow(const UObject* Ctx, FName WindowId)
{
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	if (UAsdaEnterConfirmSubsystem* S = GI ? GI->GetSubsystem<UAsdaEnterConfirmSubsystem>() : nullptr) { S->Externals.Remove(WindowId); }
}

UUserWidget* UAsdaEnterConfirmSubsystem::WindowWidget(FName WindowId) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM)
	{
		if (UUserWidget* R = WM->FindWindow(WindowId)) { return R; }
	}
	const FExternal* E = Externals.Find(WindowId);
	return E ? E->Widget.Get() : nullptr;
}

uint64 UAsdaEnterConfirmSubsystem::ContentKeyOf(FName WindowId) const
{
	using namespace AsdaEnterConfirm;
	if (WindowId == kOneBut || WindowId == kTwoBut)
	{
		const UGameInstance* GI = GetGameInstance();
		const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		return WM ? (uint64)WM->GetDisplayedMsgBoxSerial() : 0;
	}
	if (WindowId == kMsg2) { return (uint64)AsdaEnterGuard::GetConfirmSerial(); }
	return 0;
}

bool UAsdaEnterConfirmSubsystem::IsContentNoEnter(FName WindowId) const
{
	using namespace AsdaEnterConfirm;
	if (WindowId == kOneBut || WindowId == kTwoBut)
	{
		const UGameInstance* GI = GetGameInstance();
		const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		return WM && WM->IsDisplayedMsgBoxNoEnter();
	}
	if (WindowId == kMsg2) { return AsdaEnterGuard::IsConfirmNoEnter(); }
	return false;
}

double UAsdaEnterConfirmSubsystem::ShownForSec(FName WindowId, const UUserWidget* Root, uint64 Key, FName Button) const
{
	const FShown* S = Shown.Find(WindowId);
	if (!S || S->Widget.Get() != Root || S->ContentKey != Key || S->Button != Button) { return -1.0; }   // тик ещё не видел
	return FPlatformTime::Seconds() - S->Since;
}

bool UAsdaEnterConfirmSubsystem::ShownDuringChat(FName WindowId) const
{
	const FShown* S = Shown.Find(WindowId);
	return S && S->bDuringChat;
}

void UAsdaEnterConfirmSubsystem::Tick(float)
{
	ASDA_PERF_SCOPE("EnterConfirm.Tick");   // ★ R4-b 14.11.2026: область замера (AsdaPerf.h; в Shipping пусто)
	using namespace AsdaEnterConfirm;
	// Каждый кадр: что видно у окон таблицы. Смена виджета, содержимого или видимой кнопки (и показ после скрытия) = новый отсчёт.
	const double Now = FPlatformTime::Seconds();
	const UGameInstance* GI = GetGameInstance();
	const UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
	// Набор в чате идёт сейчас или шёл только что: показ модалки сам закрывает строку чата (фокус уходит модалке) — в кадре, где
	//   тик впервые видит модалку, строка уже закрыта; поэтому учитываем и «был открыт в последние 0,5 с».
	if (Chat && Chat->IsInputActive()) { LastChatActiveAt = Now; }
	const bool bChat = LastChatActiveAt >= 0.0 && Now - LastChatActiveAt < 0.5;
	for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(kRows); ++i)
	{
		const FName Id = RowNames()[i].Window;   // ★ R4-b (B3): имена таблицы раз на процесс
		UUserWidget* Root = WindowWidget(Id);
		if (!Root || !IsRootShown(Root)) { Shown.Remove(Id); continue; }
		bool bEnabled = false;
		FName Btn;
		FindVisibleCandidate(Root, i, bEnabled, Btn);
		const uint64 Key = ContentKeyOf(Id);
		FShown& S = Shown.FindOrAdd(Id);
		if (S.Widget.Get() != Root || S.ContentKey != Key || S.Button != Btn || S.Since == 0.0)
		{
			S.Widget = Root;
			S.ContentKey = Key;
			S.Button = Btn;
			S.Since = Now;
			S.bDuringChat = bChat;
		}
		else if (bChat) { S.bDuringChat = true; }
	}
}

UButton* UAsdaEnterConfirmSubsystem::FindVisibleCandidate(UUserWidget* Root, int32 TableRow, bool& OutEnabled, FName& OutName) const
{
	OutEnabled = false;
	OutName = NAME_None;
	if (!Root || TableRow < 0) { return nullptr; }
	// ★ R4-b 14.11.2026 (B3): имена кнопок — раз на процесс; порядок и правило те же. ★ R4-b закрытие (15.11.2026): старый путь и ключ удалены — только новое (решение владельца).
	for (const FName& Nm : AsdaEnterConfirm::RowNames()[TableRow].Buttons)
	{
		if (Nm.IsNone()) { break; }
		UButton* B = Cast<UButton>(Root->GetWidgetFromName(Nm));
		if (!AsdaEnterConfirm::IsClickableChain(B)) { continue; }
		OutEnabled = AsdaEnterConfirm::IsEnabledChain(B);
		OutName = Nm;
		return B;
	}
	return nullptr;
}


UAsdaEnterConfirmSubsystem::FTarget UAsdaEnterConfirmSubsystem::ResolveTarget() const
{
	using namespace AsdaEnterConfirm;
	FTarget T;
	if (CVarEnterConfirm.GetValueOnGameThread() == 0) { T.Verdict = EVerdict::Off; T.Why = TEXT("ключ отката выключил механизм"); return T; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { T.Why = TEXT("нет оконной подсистемы"); return T; }

	// 1) Поле ввода с фокусом — Enter по правилам окна (нумпад, ввод текста, поле подарка, пароль…).
	if (IsTextFieldFocused()) { T.Verdict = EVerdict::TextField; T.Why = TEXT("фокус клавиатуры в поле ввода"); return T; }
	// Dev-меню Ctrl+N и захват клавиши в настройках (Enter там — назначаемая клавиша) — их собственный ввод.
	if (const UAsdaDevMenuSubsystem* Dev = GI->GetSubsystem<UAsdaDevMenuSubsystem>(); Dev && Dev->IsMenuOpen())
	{
		T.Verdict = EVerdict::DevMenu; T.Why = TEXT("открыто dev-меню"); return T;
	}
	if (const UAsdaSystemOptionSubsystem* Opt = GI->GetSubsystem<UAsdaSystemOptionSubsystem>(); Opt && Opt->IsCapturingKey())
	{
		T.Verdict = EVerdict::DevMenu; T.Why = TEXT("окно настроек ждёт клавишу для назначения"); return T;
	}
	// 2) Строка чата открыта (даже если фокус у неё отняла всплывшая модалка) — Enter чата; модальное окно НЕ жмём (ВАЖНО-4б).
	if (const UAsdaChatWindowSubsystem* Chat = GI->GetSubsystem<UAsdaChatWindowSubsystem>(); Chat && Chat->IsInputActive())
	{
		T.Verdict = EVerdict::Chat; T.Why = TEXT("строка чата открыта"); return T;
	}

	const double Delay = GetShowDelaySec();
	// Решение по окну с найденной (или не найденной) кнопкой.
	auto Decide = [&T, this, Delay](FName Id, UUserWidget* Root, int32 Row, bool bModal) -> bool
	{
		bool bEnabled = false;
		FName BtnName;
		UButton* B = FindVisibleCandidate(Root, Row, bEnabled, BtnName);
		if (!B) { return false; }
		T.WindowId = Id;
		T.ButtonName = BtnName;
		T.Button = B;
		T.bModal = bModal;
		T.ContentKey = ContentKeyOf(Id);
		const double Age = ShownForSec(Id, Root, T.ContentKey, BtnName);
		if (IsContentNoEnter(Id))       { T.Verdict = EVerdict::Forbidden; }
		else if (bModal && ShownDuringChat(Id)) { T.Verdict = EVerdict::Forbidden; }   // всплыло во время набора в чате (ВАЖНО-4б)
		else if (!bEnabled)             { T.Verdict = EVerdict::Disabled; }
		else if (Delay > 0.0 && Age < Delay) { T.Verdict = EVerdict::TooEarly; }
		else                            { T.Verdict = EVerdict::Press; }
		T.Why = FString::Printf(TEXT("%s окно «%s», кнопка «%s», содержимое %llu, показано %.2f с — %s"),
			bModal ? TEXT("модальное") : TEXT("верхнее"), *Id.ToString(), *BtnName.ToString(), (unsigned long long)T.ContentKey,
			Age, VerdictName(T.Verdict));
		return true;
	};

	// 3) Модальное окно — только оно (эталон отдаёт ввод модальному контролу `+0x1A0`).
	if (UUserWidget* Top = WM->GetTopModalWindow())
	{
		FName TopId = NAME_None;
		for (const FName& Id : WM->GetRegisteredWindowIds()) { if (WM->FindWindow(Id) == Top) { TopId = Id; break; } }
		const int32 Row = RowOf(TopId);
		if (Row >= 0 && IsRootShown(Top) && Decide(TopId, Top, Row, /*bModal*/true)) { return T; }
		T.Verdict = EVerdict::NoTarget;
		T.Why = FString::Printf(TEXT("модальное окно «%s» без видимой кнопки согласия в таблице"), *TopId.ToString());
		return T;
	}

	// Интерфейс скрыт Alt+Z — обычные окна невидимы, подтверждать нечего.
	if (!UAsdaUiVisibilitySubsystem::IsUiShownFor(GI)) { T.Why = TEXT("интерфейс скрыт (Alt+Z)"); return T; }

	// 4) Верхнее окно: окна таблицы и окна эталонной ESC-таблицы (настоящие окна игры), кроме спутников и подсказок.
	struct FCand { FName Id; int32 Z; UUserWidget* W; };
	TArray<FCand> Cands;
	for (const FName& Id : WM->GetRegisteredWindowIds())
	{
		if (IsIgnoredForTop(Id) || !WM->IsWindowVisible(Id)) { continue; }
		if (RowOf(Id) < 0 && !WM->IsEscCloseWindow(Id)) { continue; }
		UUserWidget* Root = WM->FindWindow(Id);
		if (!IsRootShown(Root)) { continue; }
		Cands.Add({ Id, WM->GetWindowZOrder(Id), Root });
	}
	for (const TPair<FName, FExternal>& Kv : Externals)
	{
		UUserWidget* Root = Kv.Value.Widget.Get();
		if (!IsRootShown(Root) || Cands.ContainsByPredicate([&Kv](const FCand& C) { return C.Id == Kv.Key; })) { continue; }
		Cands.Add({ Kv.Key, Kv.Value.ZOrder, Root });
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.Z != B.Z ? A.Z > B.Z : A.Id.Compare(B.Id) > 0; });
	for (const FCand& C : Cands)
	{
		const int32 Row = RowOf(C.Id);
		if (Row < 0)
		{
			T.Why = FString::Printf(TEXT("верхнее окно «%s» (z=%d) — без кнопки согласия для Enter"), *C.Id.ToString(), C.Z);
			return T;
		}
		if (Decide(C.Id, C.W, Row, /*bModal*/false)) { return T; }
		// Окно таблицы, но ни одна кнопка не видна (режим без кнопки) — смотрим окно ниже.
	}
	T.Why = TEXT("окон согласия нет");
	return T;
}

void UAsdaEnterConfirmSubsystem::PressButton(UButton* B, FName WindowId, FName ButtonName)
{
	if (!B) { return; }
	// Звук нажатия — тот же, что Slate играет при щелчке мышью (`SButton::PlayPressedSound`), если он у кнопки есть.
	const FSlateSound& Snd = B->GetStyle().PressedSlateSound;
	if (Snd.GetResourceObject() && FSlateApplication::IsInitialized()) { FSlateApplication::Get().PlaySound(Snd); }
	++PressCount;
	LastWindow = WindowId;
	LastButton = ButtonName;
	UE_LOG(LogAsdaEnter, Log, TEXT("[Enter] нажата кнопка согласия «%s» окна «%s» (рассылка OnClicked, делегатов %d) — №%d"),
		*ButtonName.ToString(), *WindowId.ToString(), B->OnClicked.IsBound() ? 1 : 0, PressCount);
	B->OnClicked.Broadcast();
}

bool UAsdaEnterConfirmSubsystem::HandleEnterDown(const FKeyEvent& Ev)
{
	// Автоповтор: наше удержание поглощает его целиком; чужое — не трогаем (одно нажатие — одно действие).
	if (Ev.IsRepeat()) { return Session == ESession::Ours || Session == ESession::Blocked; }
	Session = ESession::Pass;
	ArmedWindow = ArmedButtonName = NAME_None;
	ArmedButton.Reset();
	ArmedContentKey = 0;
	// Сочетания (Alt+Enter, Ctrl+Enter, Shift+Enter) — не наши.
	if (Ev.IsAltDown() || Ev.IsControlDown() || Ev.IsShiftDown()) { return false; }
	const FTarget T = ResolveTarget();
	if (T.Verdict == EVerdict::Press)
	{
		Session = ESession::Ours;
		ArmedWindow = T.WindowId;
		ArmedButtonName = T.ButtonName;
		ArmedButton = T.Button;
		ArmedContentKey = T.ContentKey;
		UE_LOG(LogAsdaEnter, Log, TEXT("[Enter] нажатие: %s — жму на отпускании"), *T.Why);
		return true;
	}
	// Модальное окно с кнопкой, которую Enter нажать не вправе (недоступна / вопрос без Enter / только что показано), — съедаем.
	if (T.bModal && (T.Verdict == EVerdict::Disabled || T.Verdict == EVerdict::Forbidden || T.Verdict == EVerdict::TooEarly))
	{
		Session = ESession::Blocked;
		UE_LOG(LogAsdaEnter, Log, TEXT("[Enter] нажатие съедено: %s — ничего не делаю"), *T.Why);
		return true;
	}
	UE_LOG(LogAsdaEnter, Verbose, TEXT("[Enter] нажатие прежним путём: %s (%s)"), VerdictName(T.Verdict), *T.Why);
	return false;
}

bool UAsdaEnterConfirmSubsystem::HandleEnterUp(const FKeyEvent& Ev)
{
	const ESession Was = Session;
	Session = ESession::None;
	if (Was == ESession::Blocked) { return true; }
	if (Was != ESession::Ours) { return false; }
	// Кнопка жмётся, только если окно, кнопка и СОДЕРЖИМОЕ всё ещё те же: окно не закрылось, поверх не встало другое, в том же
	//   окне не показан другой вопрос (номер записи очереди / показа MESSAGE2), кнопка видна и доступна (ВАЖНО-4а).
	const FTarget T = ResolveTarget();
	UButton* B = ArmedButton.Get();
	if (T.Verdict == EVerdict::Press && T.WindowId == ArmedWindow && T.Button.Get() == B && B && T.ContentKey == ArmedContentKey)
	{
		PressButton(B, ArmedWindow, ArmedButtonName);
	}
	else
	{
		UE_LOG(LogAsdaEnter, Log, TEXT("[Enter] отпускание: цель сменилась (%s; ждали «%s»/«%s», содержимое %llu) — кнопка не нажата"),
			*T.Why, *ArmedWindow.ToString(), *ArmedButtonName.ToString(), (unsigned long long)ArmedContentKey);
	}
	ArmedButton.Reset();
	return true;
}

bool UAsdaEnterConfirmSubsystem::HandleEscDown(const FKeyEvent& Ev)
{
	if (Ev.IsRepeat()) { return false; }
	if (Ev.IsAltDown() || Ev.IsControlDown() || Ev.IsShiftDown()) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// ── ★ 25.10.2026 [ОРИГИНАЛ] ESC ИЗ ПОЛЯ ВВОДА ВНЕ МОДАЛКИ (строка чата, поля окон). Win32-поле эталона пересылает отпускание
	//   менеджеру окон (`ctrlEditImeWndProc`, ui_core.c:116210 → `CWindowMgr_QueueKeyUp`), и нажатие идёт ВСЕЙ цепочкой: верхнее
	//   окно с признаком ESC, затем выбранная цель (`GameInput_DispatchEvent` case 2), и лишь потом — закрытие строки чата
	//   (`uiNameInputConfirmShow`, game_main.c:9353). У поля Slate клавишу получает только оно, поэтому цепочку зовём здесь.
	//   Окно с флагом 0x215 или снятая цель тратят нажатие — строка чата остаётся открытой. Иначе, если идёт набор в строке чата,
	//   она закрывается этим же нажатием (ветка `sub_9250EE(g_pChatWnd)` эталона — и после закрытого окна без флага 0x215), а меню
	//   69400 не открывается. Поле не строки чата и тратить нечего — ESC получает поле прежним путём (дальше — опрос мира, меню).
	//   Ключ отката — `asda.UI.EscFromTextField 0`.
	if (!WM->IsWorldInputCapturedByModal())
	{
		if (!AsdaEscChain::FromTextFieldOn() || !AsdaEnterConfirm::IsTextFieldFocused()) { return false; }
		if (const UAsdaDevMenuSubsystem* Dev = GI->GetSubsystem<UAsdaDevMenuSubsystem>(); Dev && Dev->IsMenuOpen()) { return false; }
		if (const UAsdaSystemOptionSubsystem* Opt = GI->GetSubsystem<UAsdaSystemOptionSubsystem>(); Opt && Opt->IsCapturingKey()) { return false; }
		const bool bAnswered = WM->CloseTopmostOnEsc();
		const bool bStop = AsdaEscChain::RunAfterWindowPass(GI, bAnswered, TEXT("поле ввода"));
		// ★ 25.10.2026: ветки (в) — состояние персонажа (в бою/на бегу строка чата НЕ закрывается), затем строка чата (стереть, закрыть).
		const bool bState = !bStop && AsdaEscChain::RunStateBranches(GI, TEXT("поле ввода"));
		if (!bAnswered && !bStop && !bState) { return false; }
		UE_LOG(LogAsdaEnter, Log, TEXT("[ESC] из поля ввода: %s — нажатие съедено, поле его не получает"),
			bStop ? TEXT("потрачено окном с флагом 0x215, разговором или снятием цели")
				  : (bState ? TEXT("ветка состояния персонажа или строки чата") : TEXT("ответило верхнее окно")));
		return true;
	}
	if (CVarEscModalKey.GetValueOnGameThread() == 0) { return false; }
	// Дальше — только модалка, которая ЗАБРАЛА ввод мира (вьюпорт клавиш не получает): в остальных случаях ESC доходит до опроса
	//   контроллера (`UAsdaSystemOptionSubsystem::PollHotkeys`) прежним путём, и второго прохода здесь быть не должно.
	// Поле ввода с фокусом и dev-меню — их ESC. ★ 25.10.2026 [ОРИГИНАЛ] (`asda.UI.NumPadFocus`): кроме поля 1412 окна количества —
	//   у поля Slate ESC ничего не делает (нет выделения и отката — `FSlateEditableTextLayout::HandleEscape` вернёт «не обработал»),
	//   а Win32-поле эталона пересылает клавишу менеджеру окон (`CWindowMgr_QueueKeyUp`, ui_core.c:116203-116210) — и ESC идёт
	//   в общий проход: 1410 → отмена (`uiQuantityNumPadDialogProc` 0x15). Прежде поле фокуса не имело и ESC шёл проходом сам.
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.InputDialogEsc`): верхняя модалка — окно ввода текста 1580 → ESC = «Отмена» 1585 (из поля 1583
	//   и вне его). У оригинала признака «ESC закрывает» у 1580 нет (`kEscCloseWindows`), поэтому общий проход его не закрыл бы.
	UUserWidget* InputDlg = WM->FindWindow(FName(TEXT("SHOUTING_WIN")));
	const bool bInputDlgTop = AsdaInputDialogEscOn() && InputDlg && WM->GetTopModalWindow() == InputDlg && AsdaEnterConfirm::IsRootShown(InputDlg);
	if (AsdaEnterConfirm::IsTextFieldFocused() && !AsdaEnterConfirm::IsNumPadFieldFocused(WM) && !bInputDlgTop) { return false; }
	if (const UAsdaDevMenuSubsystem* Dev = GI->GetSubsystem<UAsdaDevMenuSubsystem>(); Dev && Dev->IsMenuOpen()) { return false; }
	if (bInputDlgTop)
	{
		WM->CloseInputDialog(false);
		++EscModalCount;
		UE_LOG(LogAsdaEnter, Log, TEXT("[ESC] реальная клавиша при окне ввода текста 1580 → «Отмена» [ИСПРАВЛЕНИЕ]"));
		// ★ 25.10.2026 [ОРИГИНАЛ] шаг цели: у 1580 признака ESC нет — эталон окна не трогает (проход пуст) и снимает цель.
		if (!AsdaEscChain::RunAfterWindowPass(GI, false, TEXT("окно ввода 1580"), /*bAllowDialog*/false))
		{
			AsdaEscChain::RunStateBranches(GI, TEXT("окно ввода 1580"));
		}
		return true;
	}
	const bool bTaken = WM->CloseTopmostOnEsc();   // эталонный проход `uiContainerHandleKey@0x12AF390`: ветка модального контрола
	++EscModalCount;
	UE_LOG(LogAsdaEnter, Log, TEXT("[ESC] реальная клавиша под модалкой → проход «ESC закрывает верхнее окно»: %s"),
		bTaken ? TEXT("окно ответило (съедено)") : TEXT("у модалки признака ESC нет — не тронуто"));
	// ★ 25.10.2026 [ОРИГИНАЛ] шаг цели и под модалкой: колбэк `GameInput_DispatchEvent` эталон зовёт после КАЖДОГО события очереди
	//   (`CControl_DispatchMessage@0x12B2440`), модальность его не гейтит. TWOBUT 1520 / нумпад 1410 ставят 0x215 — цель остаётся;
	//   ONEBUT 1500 («съедено») и модалки без признака ESC — цель снимается.
	//   ★ Разговор NPC под модалкой не закрываем (`bAllowDialog = false`): верхнее — модалка. Ветки (в) — и под модалкой (колбэк).
	const bool bTgt = AsdaEscChain::RunAfterWindowPass(GI, bTaken, TEXT("модалка"), /*bAllowDialog*/false)
		|| AsdaEscChain::RunStateBranches(GI, TEXT("модалка"));
	return bTaken || bTgt;
}

FString UAsdaEnterConfirmSubsystem::DumpState() const
{
	using namespace AsdaEnterConfirm;
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	FString S = FString::Printf(TEXT("[Enter] ключ отката=%d, задержка %d мс, окон в таблице %d, нажатий %d (последнее «%s»/«%s»)\n"),
		CVarEnterConfirm.GetValueOnGameThread(), CVarEnterConfirmDelayMs.GetValueOnGameThread(), (int32)UE_ARRAY_COUNT(kRows),
		PressCount, *LastWindow.ToString(), *LastButton.ToString());
	for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(kRows); ++i)
	{
		const FName Id(kRows[i].Window);
		UUserWidget* Root = WindowWidget(Id);
		if (!Root) { continue; }   // окна сейчас нет — строку не печатаем (полный список — в AUDIT.md)
		bool bEnabled = false;
		FName Btn;
		const UButton* B = FindVisibleCandidate(Root, i, bEnabled, Btn);
		S += FString::Printf(TEXT("  %-24s %-8s z=%-6d кнопка %s%s\n"), kRows[i].Window,
			IsRootShown(Root) ? TEXT("ВИДНО") : TEXT("скрыто"), WM ? WM->GetWindowZOrder(Id) : 0,
			B ? *FString::Printf(TEXT("«%s» %s"), *Btn.ToString(), bEnabled ? TEXT("доступна") : TEXT("НЕДОСТУПНА")) : TEXT("не видна"),
			IsContentNoEnter(Id) ? TEXT(", вопрос БЕЗ ENTER") : TEXT(""));
	}
	const FTarget T = ResolveTarget();
	S += FString::Printf(TEXT("  РЕШЕНИЕ: %s — %s\n"), VerdictName(T.Verdict), *T.Why);
	return S;
}

int32 UAsdaEnterConfirmSubsystem::VerifyTableAgainstAssets(TArray<FString>& OutLines) const
{
	using namespace AsdaEnterConfirm;
	int32 Bad = 0;
#if !UE_BUILD_SHIPPING
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	for (const FRow& R : kRows)
	{
		for (const TCHAR* Wbp : { R.Wbp, R.Wbp2 })
		{
			if (!Wbp) { continue; }
			const FString Asset = FString(kUi) + Wbp;
			const FString ClassPath = Asset + TEXT(".") + FPaths::GetBaseFilename(Asset) + TEXT("_C");
			UClass* Cls = LoadClass<UUserWidget>(nullptr, *ClassPath);
			UUserWidget* Tmp = (Cls && W) ? CreateWidget<UUserWidget>(W, Cls) : nullptr;   // не в вьюпорт: только дерево виджетов
			if (!Tmp)
			{
				++Bad;
				OutLines.Add(FString::Printf(TEXT("FAIL %s: ассет %s не загрузился"), R.Window, *Asset));
				continue;
			}
			for (const TCHAR* Btn : R.Buttons)
			{
				if (!Btn) { break; }
				const UWidget* Wd = Tmp->GetWidgetFromName(FName(Btn));
				const bool bOk = Cast<UButton>(Wd) != nullptr;
				if (!bOk) { ++Bad; }
				OutLines.Add(FString::Printf(TEXT("%s %s: %s — %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), R.Window,
					*FPaths::GetBaseFilename(Asset), *FString::Printf(TEXT("«%s» %s"), Btn,
						bOk ? TEXT("UButton") : (Wd ? *FString::Printf(TEXT("не кнопка (%s)"), *Wd->GetClass()->GetName()) : TEXT("НЕТ в ассете")))));
			}
		}
	}
#endif
	return Bad;
}

// ── Консоль ──
#if ASDA_DEV_CONSOLE   // дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaEnterConfirmDump(
	TEXT("asda.UI.EnterConfirm.Dump"),
	TEXT("Enter → кнопка согласия: таблица открытых окон (видимость, z, найденная кнопка и её доступность, «без Enter») и решение сейчас"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		const UAsdaEnterConfirmSubsystem* S = GI ? GI->GetSubsystem<UAsdaEnterConfirmSubsystem>() : nullptr;
		AsdaNotice::ConsoleEcho(W, S ? S->DumpState() : FString(TEXT("[Enter] подсистемы нет")), LogAsdaEnter);
	}));
#endif // ASDA_DEV_CONSOLE
