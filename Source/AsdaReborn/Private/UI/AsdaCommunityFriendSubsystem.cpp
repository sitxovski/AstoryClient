// Copyright (c) 2026 @sitxovski (Discord)
// Порт вкладки 1 «Приятель» (окно `COMMUNITY_FRND_WIN` 14500) + общие UI-примитивы трёх вкладок.
//   Разбор-источник: `Reports/community_tabs_frnd_clan_who.md` РАЗДЕЛ 1.
#include "UI/AsdaCommunityFriendSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaWorldSubsystem.h"   // ★ волна 3: AsdaCommunityUi::SelfClanId
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaMarkup.h"
#include "Text/AsdaTextSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"        // UImage — план контрола: по его слоту достраиваем надпись ONNAME
#include "Components/TextBlock.h"
// ── Классы ИНТЕРАКТИВНЫХ виджетов: их перечисляет `AsdaCommunityUi::IsInteractiveWidget`,
//    которой нужна ПОЛНАЯ видимость (`SelfHitTestInvisible` делает такой виджет мёртвым для мыши).
//    Первые четыре — ровно то, что строит импортёр JUI (`Tools/asda_jui_generic_umg.py`:
//    типы скина `PushButton`/`RadioButton` → `UButton`, `CheckButton` → `UCheckBox`,
//    `EditBox` → `UEditableText`, `ComboBox` → `UComboBoxString`); остальные — их движковые
//    близнецы и прокрутка/списки, которые порт создаёт кодом (см. комментарий у предиката).
#include "Components/CheckBox.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/ComboBoxKey.h"
#include "Components/Slider.h"
#include "Components/SpinBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBar.h"
#include "Components/ListViewBase.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Styling/SlateBrush.h"      // FSlateBrush  — кэш исходных кистей планов полосы-переключателя
#include "Styling/SlateTypes.h"      // FButtonStyle — кэш исходных стилей кнопок-опций
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFriend, Log, All);

// Ключ `asda.Community.Friend.ResultChannels` (эталонные каналы итога 4151): определён и описан в `AsdaPlayerMenuSubsystem.cpp`.
bool AsdaFriendResultChannelsOn();

namespace
{
	// ══════════════════════ КЛЮЧИ ОТКАТА (дефекты оригинала: чиним, но помечаем) ═══════════════
	//   Закон проекта: дефекты ИСПРАВЛЯЕМ и ПОМЕЧАЕМ; спорные — за ключом с дефолтом «как в оригинале».

	// D-F3 (★★★): эталонный `Parse4157@0xCE8FE0` не очищает вектор перед добавлением, поэтому
	//   после каждого «Обновить» список удваивается/утраивается. Это чистая ошибка — чиним по умолчанию.
	ASDA_DEV_CVAR(int32, CVarFixDupList,
		TEXT("asda.Community.Friend.FixDupList"), 1,
		TEXT("1 (по умолчанию) = 4157 ЗАМЕНЯЕТ список приятелей (дефект оригинала D-F3 исправлен).\n")
		TEXT("0 = как в оригинале: записи ДОБАВЛЯЮТСЯ, и после второго «Обновить» каждый приятель\n")
		TEXT("показан дважды."),
		ECVF_Default);

	// D-F4 (★★★): сервер шлёт 4160 (обновление одной записи), а в диспетчере клиента ветки 0x1040
	//   нет вовсе — онлайн-статус приятеля не обновляется в реальном времени. Чиним по умолчанию.
	ASDA_DEV_CVAR(int32, CVarHandle4160,
		TEXT("asda.Community.Friend.Handle4160"), 1,
		TEXT("1 (по умолчанию) = пакет 4160 обновляет одну запись списка (дефект D-F4 исправлен).\n")
		TEXT("0 = как в оригинале: 4160 молча роняется, статус обновляется только полной перезагрузкой."),
		ECVF_Default);

	// D-F1 (★★★): в эталоне при ВЫКЛЮЧЕННОМ звуке ветка else обработчика 4152 шлёт 4153 с ответом 2 —
	//   то есть ВСЕ входящие приглашения автоматически отклоняются, игрок их даже не видит.
	//   По умолчанию чиним: значок уведомления показываем всегда, звук — только если звук включён.
	ASDA_DEV_CVAR(int32, CVarAutoRejectWhenMuted,
		TEXT("asda.Community.Friend.AutoRejectWhenMuted"), 0,
		TEXT("0 (по умолчанию) = приглашение в приятели показывается ВСЕГДА (дефект D-F1 исправлен),\n")
		TEXT("звук 215 играет только при включённом звуке.\n")
		TEXT("1 = как в оригинале: при выключенном звуке приглашение АВТОМАТИЧЕСКИ отклоняется (4153 = 2)."),
		ECVF_Default);

	// D-F6 (★): код 6 пакета 4151 (сервер шлёт его при ответе 3 на приглашение) эталон роняет в
	//   `default: return` — игрок не видит ничего. Спорный (клиент ответ 3 не отправляет) ⇒ дефолт «как в оригинале».
	ASDA_DEV_CVAR(int32, CVarShowCode6,
		TEXT("asda.Community.Friend.ShowCode6"), 0,
		TEXT("0 (по умолчанию) = как в оригинале: код 6 пакета 4151 игнорируется (дефект D-F6).\n")
		TEXT("1 = показать нотис 20082 «Игрок отклонил твое предложение»."),
		ECVF_Default);

	// ★ 05.11.2026: код 6 пакета 4151 приходит и сырым флагом `UPFriendAdd` 6 («полны оба списка», `sub_406370`, GameServer.c:2542).

	// D-F9 (★): приём 4158 перезапускает 5-секундный антиспам 4156, из-за чего кнопка «Обновить»
	//   мертва ещё 5 секунд после каждого серверного «перерисуй». Спорный ⇒ дефолт «как в оригинале».
	ASDA_DEV_CVAR(int32, CVarKeepTimerOn4158,
		TEXT("asda.Community.Friend.KeepTimerOn4158"), 0,
		TEXT("0 (по умолчанию) = как в оригинале: 4158 перезапускает антиспам 4156 (дефект D-F9).\n")
		TEXT("1 = 4158 только перерисовывает список, таймер не трогает."),
		ECVF_Default);

	// ★ Ключ `asda.UI.InteractiveHitTest` переехал в `AsdaUiHelpers.cpp` вместе с правилом видимости (14.09.2026).

	// ── Константы разметки (отчёт §1.1): 6 РАБОЧИХ строк 14520…14570, шаг idc «+1..+6» внутри строки.
	//   7-я строка `CMTY_FRND_LIST7` в JUI есть, но у неё IDNum = −1 и Visible = 0 — мёртвая
	//   разметка (дефект оригинала D-F5); код эталона жёстко рассчитан на 6 строк, мы тоже.
	//   ★ 08.11.2026 (пачка 17, баг 7, решение владельца «friend-list — make 7») [ИСПРАВЛЕНИЕ]: строк на страницу 7 —
	//   мёртвая 7-я строка включена в АССЕТЕ шагом пост-импорта `asda_ui_friend_row7` (группа `CMTY_FRND_LIST7_grp`
	//   видима; её поддерево совпадает с 6-й узел в узел), листание/клик/выбор/удаление/шёпот считают по `FriendRows()`.
	constexpr int32 kMaxRows = 7;   // строк в разметке (привязка, очистка — всегда все семь)

	// Ключ числа строк страницы приятелей. 1 = 7 строк [ИСПРАВЛЕНИЕ]; 0 = 6, как в оригинале (7-я группа гасится кодом).
	ASDA_DEV_CVAR(int32, CVarFriendRows7,
		TEXT("asda.Community.FriendRows7"), 1,
		TEXT("1 (по умолчанию) = во вкладке «Друг» 7 приятелей на страницу [ИСПРАВЛЕНИЕ, решение владельца 08.11.2026]:\n")
		TEXT("включена 7-я строка разметки CMTY_FRND_LIST7, номер записи = строка + 7 × страница.\n")
		TEXT("0 = как в оригинале: 6 строк, страница = размер/6, 7-я строка скрыта (пустое место под 6-й)."),
		ECVF_Default);

	// Строк на странице сейчас (6 или 7 по ключу `asda.Community.FriendRows7`).
	int32 FriendRows() { return CVarFriendRows7.GetValueOnGameThread() != 0 ? 7 : 6; }

	// Показать/погасить 7-ю строку целиком: группу `CMTY_FRND_LIST7_grp` и все контролы строки по префиксу имени (общий
	//   показ вкладки `SetVisByPrefix("CMTY_FRND_")` зажигает их поимённо, поэтому и гасить их надо поимённо — иначе при
	//   расплющенной раскладке JUI вышли бы тексты-заглушки дизайнера). Список снят с имя-таблицы `WBP_COMMUNITY_WIN`;
	//   `CMTY_FRND_STAT7` покрывает и `STAT7On`/`STAT7Off`. Видимость ON/OFF-надписей при показе потом расставляют
	//   `ClearRow`/`FillRow`, как у строк 1…6.
	void ApplyFriendRow7(UUserWidget* Wnd, bool bShow)
	{
		static const TCHAR* const kRow7[] = {
			TEXT("CMTY_FRND_LIST7"), TEXT("CMTY_FRND_ONNAME7"), TEXT("CMTY_FRND_ONINFO7"),
			TEXT("CMTY_FRND_OFFNAME7"), TEXT("CMTY_FRND_LV7"), TEXT("CMTY_FRND_LVI7"),
			TEXT("CMTY_FRND_JOB7"), TEXT("CMTY_FRND_POSIT7"), TEXT("CMTY_FRND_STAT7"),
		};
		for (const TCHAR* Idc : kRow7) { AsdaCommunityUi::SetVisByPrefix(Wnd, Idc, bShow); }
		if (UWidget* G = AsdaUi::FindNodeExact(Wnd, TEXT("CMTY_FRND_LIST7_grp")))
		{
			G->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}

	// loc-идентификаторы вкладки (таблица «событие → loc → канал», отчёт §1.8).
	constexpr int32 kLocDelBtn      = 12804;   // «Удалить» — подпись 14508
	constexpr int32 kLocWhisperBtn  = 12805;   // «Шепот» — подпись 14509
	constexpr int32 kLocTitle       = 12806;   // «Список приятелей» — подпись 14512
	constexpr int32 kLocReloadBtn   = 12807;   // «Обновить» — подпись 14511
	constexpr int32 kLocAddedChat   = 20000;   // «Игрок [ss1] добавляется…» — ЧАТ
	constexpr int32 kLocRemovedChat = 20001;   // «Игрок [ss1] удален…» — ЧАТ
	constexpr int32 kLocDelConfirm  = 20002;   // «Удалить игрока [ss1]…?» — МОДАЛКА (код 47)
	constexpr int32 kLocInviteAsk   = 20081;   // «[ss1] предлагает стать приятелями…» — МОДАЛКА (0x4E71)
	constexpr int32 kLocRejected    = 20082;   // «Игрок отклонил твое предложение.» — нотис
	constexpr int32 kLocDelFailed   = 12814;   // «Не удалось удалить игрока…» — нотис
	constexpr int32 kLocPickFirst   = 12816;   // «Чтобы удалить персонажа, выбери его» — нотис
	constexpr int32 kLocStatIn      = 12800;   // «<FC=FF93FF93>Вход</F>» — надпись STAT
	constexpr int32 kLocStatOut     = 12801;   // «<FC=FFABABAB>Выход</F>» — надпись STAT
	constexpr int32 kLocChannel     = 12803;   // «Канал» — надпись POSIT
	constexpr int32 kLocMic         = 13927;   // «MIC» — канал 6
	constexpr int32 kLocJobBase     = 12300;   // loc 12300 + код класса — надпись JOB

	// ★★★ ЦВЕТ ПОДПИСИ «положение» (`CMTY_FRND_POSIT%d`, IDNum 1452x+5) — ИЗ КОДА, А НЕ ИЗ РАЗМЕТКИ.
	//   Эталонный `uiPanel_BindControlsByID@0xCE7480` (game.c:329358) на КАЖДОЙ строке делает
	//   `CControl_SetField923(row[5], -16777216)`, а поле 923 — базовый цвет текста контрола
	//   (`CControl_SetField923@0x97D760`: `*((DWORD*)this + 923) = a2`; та же пара «поле 923 = цвет»
	//   доказана в разборах инвентаря и подсказки предмета). −16777216 = 0xFF000000, ЧЁРНЫЙ.
	//   В разметке у этого контрола стоит `FF808080` (серый) — то есть оригинал ПЕРЕКРЫВАЕТ данные
	//   кодом (класс «рантайм против ассета», отчёт `Reports/audit_runtime_style.md`).
	//   Порт этого не делал: положение приятеля выводилось серым вместо чёрного.
	constexpr uint32 kPositTextColor = 0xFF000000u;   // = эталонный SetField923(−16777216)

	// Антиспам 4156: `isWithinTimeout5000@0xCE9B00` — `timeGetTime() − this[66] <= 0x1388` (5000 мс).
	constexpr double kReqCooldownS = 5.0;

	// Звук входящего приглашения — эталон играет 215 при показе значка уведомления.
	constexpr int32 kSoundInvite = 215;
}

// ═══════════════════════════ ОБЩИЕ UI-ПРИМИТИВЫ (namespace AsdaCommunityUi) ═══════════════════

namespace AsdaCommunityUi
{
	// Точное имя — как его понимает UMG (`GetWidgetFromName`).
	// ★ СВЕДЕНО (`fix_helpers_dedup.md`): тонкий форвард к общей точке `AsdaUi::FindNodeExact`.
	static UWidget* FindExact(UUserWidget* Host, const FString& Idc)
	{
		return AsdaUi::FindNodeExact(Host, *Idc);
	}

	// ★★★ РАЗРЕШЕНИЕ УЗЛА ПО idc (АУДИТ 31.08.2026, находка A — было КРИТИЧНО).
	//   Прежняя реализация звала ТОЛЬКО `GetWidgetFromName(idc)`. Импортёр
	//   (`Tools/asda_jui_generic_umg.py`) кладёт в ассет имя `idc` ЛИШЬ для контролов, которым
	//   создаёт один интерактивный/текстовый виджет. У контрола-КАРТИНКИ имена другие:
	//     • планы скина      → `<idc>_<индекс обхода>_<номер плана>`  (`CMTY_FRND_LIST1_568_0`)
	//     • группа поддерева → `<idc>_grp`                            (`CMTY_FRND_LIST1_grp`)
	//   Проверено по имени-таблице собранного ассета
	//   `Content/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN.uasset`: узлов
	//   `CMTY_FRND_LIST1…LIST6` в нём НЕТ ВООБЩЕ, есть только `_568…_623` и `_grp`.
	//   ⇒ строки списка не находились, клик-подложка не создавалась, рамка выбора не ставилась:
	//   выбрать приятеля было НЕЧЕМ, и кнопки «Удалить»/«Шёпот» были мертвы всегда.
	//   ★ Группа `_grp` — правильный носитель геометрии строки: она лежит в ТОМ ЖЕ родителе
	//     (`CMTY_FRND_LISTBox_grp`), что и планы рамки выбора, а значит система координат общая —
	//     ровно как у эталона, где `Class_GetVec2_74(row)` и `SEL.SetPos(x−2, y−2)` — соседи.
	UWidget* Find(UUserWidget* Host, const FString& Idc)
	{
		if (UWidget* W = FindExact(Host, Idc)) { return W; }
		if (UWidget* G = FindExact(Host, Idc + TEXT("_grp"))) { return G; }
		if (!Host || !Host->WidgetTree) { return nullptr; }
		const FString Pref = Idc + TEXT("_");
		UWidget* Numbered = nullptr;   // `<idc>_<только цифры>` — сам контрол без поддерева
		UWidget* Any      = nullptr;   // любой узел поддерева (последний шанс)
		Host->WidgetTree->ForEachWidget([&Pref, &Numbered, &Any](UWidget* W)
		{
			if (!W) { return; }
			const FString N = W->GetName();
			if (!N.StartsWith(Pref, ESearchCase::IgnoreCase)) { return; }
			if (!Any) { Any = W; }
			if (Numbered) { return; }
			const FString Rest = N.RightChop(Pref.Len());
			if (Rest.IsEmpty()) { return; }
			for (const TCHAR C : Rest) { if (!FChar::IsDigit(C)) { return; } }
			Numbered = W;
		});
		return Numbered ? Numbered : Any;
	}

	// ★★★ НАДПИСЬ КОНТРОЛА (АУДИТ 31.08.2026, находка B — было КРИТИЧНО).
	//   Импортёр кладёт подпись НЕ-текстового контрола в отдельный узел `<idc>_Text`
	//   (`asda_jui_generic_umg.py`, ветка caption). Порт писал имя приятеля в `CMTY_FRND_ONNAME%d`
	//   и `CMTY_FRND_OFFNAME%d` — узлов с такими именами в ассете НЕТ, `SetTxt` молча выходил,
	//   и имя приятеля не показывалось НИ В ОДНОЙ строке. Ищем по порядку:
	//   точное имя → `<idc>_Text` → первый `UTextBlock` поддерева `<idc>_*`.
	UTextBlock* FindLabel(UUserWidget* Host, const FString& Idc)
	{
		if (UTextBlock* T = Cast<UTextBlock>(FindExact(Host, Idc))) { return T; }
		if (UTextBlock* T = Cast<UTextBlock>(FindExact(Host, Idc + TEXT("_Text")))) { return T; }
		if (!Host || !Host->WidgetTree) { return nullptr; }
		const FString Pref = Idc + TEXT("_");
		UTextBlock* Found = nullptr;
		Host->WidgetTree->ForEachWidget([&Pref, &Found](UWidget* W)
		{
			if (Found || !W) { return; }
			if (UTextBlock* T = Cast<UTextBlock>(W))
			{
				if (T->GetName().StartsWith(Pref, ESearchCase::IgnoreCase)) { Found = T; }
			}
		});
		return Found;
	}

	// ★★★ НЕДОСТАЮЩАЯ НАДПИСЬ (АУДИТ 31.08.2026, находка B, вторая половина).
	//   У `CMTY_FRND_ONNAME%d` (IDNum 14521 и далее) в разметке `CaptionText = ""`
	//   (`gamemain_jui.ini`, узел CMTY_FRND_ONNAME1), поэтому импортёр текстового узла НЕ СОЗДАЁТ
	//   вовсе — а эталон `TradeWnd_FillInfo@0xCE94F0` (game.c:330100) пишет туда ИМЯ приятеля
	//   (`CWidget_SetTextAll4(row[1], &stateFlags+3, 0)`). Достраиваем узел в рантайме по
	//   прямоугольнику ПЛАНА контрола (закон `tools-no-import-side-effects`: правку в ассет не
	//   запекаем, она переживает перегенерацию JUI). Цвет — из `CaptionInfo` разметки.
	//   Шрифт копируем у соседней надписи-донора (`CMTY_FRND_OFFNAME%d_Text`), чтобы кегль
	//   совпал с оригинальным, а не пришёл движковым дефолтом.
	UTextBlock* EnsureLabel(UObject* Owner, UUserWidget* Host, const FString& Idc,
		const FLinearColor& Color, const FString& FontDonorIdc)
	{
		if (UTextBlock* T = FindLabel(Host, Idc)) { return T; }
		if (!Host || !Host->WidgetTree || !Owner) { return nullptr; }

		// Прямоугольник = слот ПЛАНА контрола (`<idc>_<обход>_<план>`); он и есть rect контрола.
		const FString Pref = Idc + TEXT("_");
		UWidget* Plane = nullptr;
		Host->WidgetTree->ForEachWidget([&Pref, &Plane](UWidget* W)
		{
			if (Plane || !W) { return; }
			if (W->IsA<UImage>() && W->GetName().StartsWith(Pref, ESearchCase::IgnoreCase)) { Plane = W; }
		});
		UWidget* Anchor = Plane ? Plane : Find(Host, Idc);
		UPanelWidget* Parent = Anchor ? Anchor->GetParent() : nullptr;
		UCanvasPanelSlot* AS = Anchor ? AsdaUi::SlotOf(Anchor) : nullptr;
		if (!Parent || !AS) { return nullptr; }

		UTextBlock* New = Host->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(*(Idc + TEXT("_Text"))));
		if (UTextBlock* Donor = FindLabel(Host, FontDonorIdc)) { New->SetFont(Donor->GetFont()); }
		New->SetColorAndOpacity(FSlateColor(Color));
		New->SetJustification(ETextJustify::Left);
		New->SetAutoWrapText(false);   // закон ui-autowrap-canvas-gotcha: в канвасе перенос = баг
		New->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Parent->AddChild(New)))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			S->SetAlignment(FVector2D(0.f, 0.f));
			S->SetPosition(AS->GetPosition());
			S->SetSize(AS->GetSize());
			S->SetZOrder(AS->GetZOrder() + 1);   // строго НАД планом контрола
		}
		AsdaUi::SetTextOutline(New, 0);   // обводка не положена: эталон 926 этому контролу не ставит
		return New;
	}

	void SetTxt(UUserWidget* Host, const FString& Idc, const FString& S)
	{
		if (UTextBlock* T = FindLabel(Host, Idc)) { T->SetText(FText::FromString(S)); }
	}

	void SetTaggedTxt(UUserWidget* Host, const FString& Idc, const FString& RawWithTags)
	{
		UTextBlock* T = FindLabel(Host, Idc);
		if (!T) { return; }
		// Разбираем игровую разметку на сегменты (единая точка тегов порта — `FAsdaMarkup`).
		//   Строки этих вкладок одноцветные целиком, поэтому цвет берём из первого сегмента,
		//   а текст склеиваем. Так контрол остаётся UTextBlock: кегль/обводка/выравнивание из JUI
		//   не теряются (подмена на URichTextBlock их сбрасывает — см. `FAsdaRichApplyOpts`).
		TArray<FAsdaTextSegment> Segs;
		FAsdaMarkup::ToSegments(RawWithTags, Segs);
		FString Plain;
		for (const FAsdaTextSegment& Sg : Segs) { Plain += Sg.Text; }
		T->SetText(FText::FromString(Plain));
		if (Segs.Num() > 0) { T->SetColorAndOpacity(FSlateColor(Segs[0].Color)); }
	}

	// ★ ПЕРЕЕЗД 14.09.2026 (волна 1 аудита дублей, В3): `IsInteractiveWidget`, `ShownVisibilityFor` и ключ
	//   `asda.UI.InteractiveHitTest` живут в `AsdaUiHelpers.cpp` (namespace `AsdaUi`). Тела перенесены
	//   без изменений; имена `AsdaCommunityUi::` оставлены встроенными переходниками в заголовке.
	void SetVis(UUserWidget* Host, const FString& Idc, bool bVisible)
	{
		if (UWidget* W = Find(Host, Idc))
		{
			W->SetVisibility(bVisible ? ShownVisibilityFor(W) : ESlateVisibility::Collapsed);
		}
	}

	void SetVisByPrefix(UUserWidget* Host, const FString& Prefix, bool bVisible)
	{
		if (!Host || !Host->WidgetTree) { return; }
		Host->WidgetTree->ForEachWidget([&Prefix, bVisible](UWidget* W)
		{
			if (W && W->GetName().StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				W->SetVisibility(bVisible ? ShownVisibilityFor(W) : ESlateVisibility::Collapsed);
			}
		});
	}

	void SetButtonCaption(UUserWidget* Host, const FString& Idc, const FString& S)
	{
		// Импортёр кладёт подпись кнопки в узел `<idc>_Text`; у части кнопок подпись — сам контрол.
		//   Обе ветки закрывает `FindLabel` (точное → `_Text` → первый UTextBlock поддерева).
		SetTxt(Host, Idc, S);
	}

	// ── ВЫБРАННЫЙ ПУНКТ ПОЛОСЫ-ПЕРЕКЛЮЧАТЕЛЯ (порт `CControl_SetSelectedIndex`) ──
	//   Разбор и мотивация — в объявлении (`AsdaCommunityFriendSubsystem.h`).
	//   ★ Исходные кисти планов, стили кнопок и цвета подписей снимаются ОДИН РАЗ и хранятся
	//     здесь: `AsdaUi::ApplyTabState` возвращает их неактивным пунктам, поэтому «забыть» их
	//     нельзя. Кэш привязан к КОНКРЕТНОМУ виджету окна: при пересоздании окна 6400 запись
	//     сбрасывается целиком, чтобы ни одна кисть мёртвого дерева не пережила его.
	struct FOptBarIdle
	{
		TWeakObjectPtr<UUserWidget> Host;        // владелец, для которого сняты кисти
		TArray<FSlateBrush>         PlaneIdle;   // кадр 0 каждого плана «как в ассете»
		TArray<FButtonStyle>        BtnIdle;     // стиль каждой кнопки-опции «как в ассете»
		TArray<FLinearColor>        CapIdle;     // базовый цвет каждой подписи
	};
	static TMap<FString, FOptBarIdle> GOptBars;

	void SetOptSelected(UUserWidget* Host, const FString& BarIdc, int32 Index)
	{
		if (!Host || !Host->WidgetTree || BarIdc.IsEmpty()) { return; }

		// ── Сбор узлов полосы: кнопки `<idc>_OPT<i>` и планы `<idc>_<обход>_<план>` ──
		TArray<TObjectPtr<UButton>>    Btns;
		TArray<TObjectPtr<UImage>>     Planes;
		const FString OptPref = BarIdc + TEXT("_OPT");
		Host->WidgetTree->ForEachWidget([&Btns, &Planes, &OptPref, &BarIdc](UWidget* W)
		{
			if (!W) { return; }
			const FString N = W->GetName();
			if (UButton* B = Cast<UButton>(W))
			{
				if (!N.StartsWith(OptPref, ESearchCase::IgnoreCase)) { return; }
				const FString Tail = N.RightChop(OptPref.Len());
				if (Tail.IsEmpty()) { return; }
				// Только ЧИСТО ЧИСЛОВОЙ хвост: `_OPT0`, но не `_OPT0_HIT` и не `_OPT0_Text`.
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
				if (!Idc.Equals(BarIdc, ESearchCase::IgnoreCase) || Plane < 0 || Plane >= 32) { return; }
				if (Planes.Num() <= Plane) { Planes.SetNum(Plane + 1); }
				Planes[Plane] = Img;
			}
		});
		if (Btns.Num() == 0) { return; }   // полосы с таким idc в дереве нет — молча выходим

		// ── ПОДПИСИ ПУНКТОВ ──
		// ★★★ ДЕФЕКТ ПОРТА (общий примитив), найден 02.09.2026. БЫЛО: подписи искались как
		//   `FindLabel("<idc>_OPT<i>")`, то есть по узлу `<idc>_OPT<i>_Text`. ТАКИХ УЗЛОВ НЕТ —
		//   массив `Caps` получался целиком пустым, и ни установка цвета в `ApplyTabCaptionState`,
		//   ни страховка «цвет кадра 2» внутри `AsdaUi::ApplyTabState` не срабатывали НИКОГДА:
		//   подпись активной вкладки не отличалась от неактивных ни в одном окне порта.
		//   ПОЧЕМУ ИМЕННО `<idc>_Text<N>` (перепроверено ДВУМЯ методами, 02.09.2026):
		//     · шаг пост-импорта `Tools/asda_ui_tabbar_captions.py:333` называет подписи
		//       `name = (idc + "_Text") if i == 0 else "%s_Text%d" % (idc, i + 1)`;
		//     · имя-таблица собранного `Content/.../WBP_COMMUNITY_WIN.uasset`: есть
		//       `COMMUNITY_MENU_OPT0…OPT3` и `COMMUNITY_MENU_Text/_Text2/_Text3/_Text4`,
		//       `CMTY_CLAN_3BTN_OPT0…2` и `CMTY_CLAN_3BTN_Text/_Text2/_Text3`;
		//       узлов `*_OPT<i>_Text` в ассете НЕТ НИ ОДНОГО.
		//   Правка лечит ОБЕ полосы окна 6400 — таб-бар контейнера `COMMUNITY_MENU` (4 вкладки)
		//   и полосу подрежимов вкладки «Клан» `CMTY_CLAN_3BTN` (3 пункта), а также любую другую
		//   полосу, прошедшую через шаг подписей.
		//   ★ Прежний поиск оставлен ЗАПАСНЫМ: полосы, до которых шаг подписей не добрался, могут
		//     иметь подпись прямо в узле кнопки (`SetButtonCaption` ищет её той же цепочкой).
		//     Полоса с подписями в ОТДЕЛЬНЫХ контролах (`CmtyClan_SubMenuBtn` → `CmtyClan_SubMenuTxt0N`)
		//     не найдётся ни тем, ни другим — это законно: цвет ей ставит её собственный владелец.
		TArray<TObjectPtr<UTextBlock>> Caps;
		Caps.SetNum(Btns.Num());
		for (int32 I = 0; I < Btns.Num(); ++I)
		{
			const FString CapIdc = (I == 0)
				? (BarIdc + TEXT("_Text"))
				: FString::Printf(TEXT("%s_Text%d"), *BarIdc, I + 1);
			Caps[I] = Cast<UTextBlock>(FindExact(Host, CapIdc));
			if (!Caps[I]) { Caps[I] = FindLabel(Host, FString::Printf(TEXT("%s_OPT%d"), *BarIdc, I)); }
		}

		// ── Кэш исходного вида: своя запись на пару «окно + полоса» ──
		//   ⚠ ЧИСТКА ИДЁТ ДО `FindOrAdd`: `RemoveCurrent` перестраивает карту и обесценил бы
		//     уже взятую ссылку. Записи умерших окон не держим — в них лежат кисти виджетов,
		//     которых больше нет.
		for (auto It = GOptBars.CreateIterator(); It; ++It)
		{
			if (!It->Value.Host.IsValid()) { It.RemoveCurrent(); }
		}
		const FString Key = Host->GetPathName() + TEXT("|") + BarIdc;
		FOptBarIdle& S = GOptBars.FindOrAdd(Key);
		if (S.Host.Get() != Host)
		{
			// Окно пересоздано (или запись новая): всё, что лежало от прошлого дерева, — мусор.
			S.Host = Host;
			S.PlaneIdle.Reset();
			S.BtnIdle.Reset();
			S.CapIdle.Reset();
		}

		const int32 Active = FMath::Clamp(Index, 0, Btns.Num() - 1);
		AsdaUi::ApplyTabState(Planes, Caps, Btns, Active, S.PlaneIdle, S.BtnIdle, S.CapIdle);
	}

	bool BindButton(UObject* Owner, UUserWidget* Host, const FString& Idc, int32 Tag,
		TFunction<void(int32)> Fn, TArray<TObjectPtr<UObject>>& OutRelays)
	{
		// ★ U20 (В1 дедупа №2): привязка — общей точкой `AsdaUi::BindButton` (там же GC-якорь в OutRelays); здесь — поиск по idc.
		return AsdaUi::BindButton(Owner, Cast<UButton>(Find(Host, Idc)), Tag, MoveTemp(Fn), OutRelays) != nullptr;
	}

	// Прямоугольник строки в системе координат её родителя: сперва живая геометрия, затем слот.
	static bool RowRectInParent(UWidget* Row, FVector2D& OutPos, FVector2D& OutSize)
	{
		if (!Row) { return false; }
		UPanelWidget* Parent = Row->GetParent();
		if (Parent)
		{
			const FGeometry PG = Parent->GetCachedGeometry();
			const FGeometry RG = Row->GetCachedGeometry();
			const FVector2D PSize = PG.GetLocalSize();
			const FVector2D RSize = RG.GetLocalSize();
			if (PSize.X > 0.f && RSize.X > 0.f)
			{
				// ЖИВАЯ ГЕОМЕТРИЯ (закон ui-check-game-path-geometry): экранный прямоугольник строки
				//   переводим в локальные координаты её родителя — так работает и при масштабе DPI,
				//   и когда импортёр расплющил иерархию.
				const FVector2D Local = PG.AbsoluteToLocal(RG.GetAbsolutePosition());
				OutPos  = Local;
				OutSize = RSize;
				return true;
			}
		}
		// Первый кадр: живой геометрии ещё нет — берём слот канваса (тот же родитель ⇒ те же координаты).
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Row))
		{
			OutPos  = S->GetPosition();
			OutSize = S->GetSize();
			return OutSize.X > 0.f;
		}
		return false;
	}

	UButton* EnsureRowHit(UObject* Owner, UUserWidget* Host, const FString& RowIdc, int32 RowIndex,
		TFunction<void(int32)> Fn, TArray<TObjectPtr<UObject>>& OutRelays)
	{
		if (!Host || !Host->WidgetTree) { return nullptr; }
		UWidget* Row = Find(Host, RowIdc);
		if (!Row) { return nullptr; }
		UPanelWidget* Parent = Row->GetParent();
		if (!Parent) { return nullptr; }

		const FString HitName = RowIdc + TEXT("_HIT");
		if (UButton* Old = Cast<UButton>(Find(Host, HitName))) { SyncRowHit(Host, RowIdc); return Old; }

		// ★★★ ПРЯМОУГОЛЬНИК СЧИТАЕМ ДО СОЗДАНИЯ ВИДЖЕТА (правка 02.09.2026, регрессия «окно умирает»).
		//   БЫЛО: `FVector2D Pos, Size;` + вызов `RowRectInParent` БЕЗ проверки возврата. В UE
		//   `FVector2D` конструируется НЕИНИЦИАЛИЗИРОВАННЫМ, поэтому когда строка не давала ни живой
		//   геометрии, ни слота канваса, в слот подложки уезжал МУСОР СО СТЕКА: прозрачная кнопка
		//   `Visible` ложилась произвольным прямоугольником — вплоть до всего экрана — и съедала
		//   клики по полосе вкладок и крестику (закон `selfhittest-vs-hittest-invisible`).
		//   СТАЛО: возврат проверяем. Прямоугольник не определился ⇒ подложку всё равно создаём,
		//   но СХЛОПНУТОЙ (`Collapsed`): она не ловит мышь и никому не мешает, а `SyncRowHit`
		//   поднимет её, как только у строки появится геометрия (он зовётся на каждом обновлении
		//   списка). Так путь самовосстанавливается и повторной привязки кнопок не требует.
		FVector2D Pos(FVector2D::ZeroVector), Size(FVector2D::ZeroVector);
		const bool bRectOk = RowRectInParent(Row, Pos, Size) && Size.X > 0.f && Size.Y > 0.f;

		UButton* Hit = Host->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*HitName));
		FButtonStyle St = Hit->GetStyle();
		St.Normal.TintColor = St.Hovered.TintColor = St.Pressed.TintColor =
			FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.f));   // полностью прозрачная — визуал даёт сама строка
		Hit->SetStyle(St);
		AsdaUi::BindButton(Owner, Hit, RowIndex, MoveTemp(Fn), OutRelays);   // ★ U20: реле клика — общая точка   // ★★★ GC-якорь: без него клик по строке умрёт на первой сборке мусора
		// Пока прямоугольник неизвестен — подложка СХЛОПНУТА и мышь не ловит (разбор выше).
		Hit->SetVisibility(bRectOk ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Parent->AddChild(Hit)))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			S->SetAlignment(FVector2D(0.f, 0.f));
			S->SetPosition(Pos);
			S->SetSize(Size);
			// ★★★ ZOrder −1 кладёт подложку ПОД все узлы строки: `SConstraintCanvas` сортирует детей
			//   по ZOrder, а хит-тест идёт в обратном порядке. `ShiftChild` порядок НЕ меняет
			//   (закон `umg-shiftchild-does-not-reorder`) — полагаться на него нельзя.
			S->SetZOrder(-1);
		}
		return Hit;
	}

	void SyncRowHit(UUserWidget* Host, const FString& RowIdc)
	{
		UWidget* Row = Find(Host, RowIdc);
		UWidget* Hit = Find(Host, RowIdc + TEXT("_HIT"));
		if (!Row || !Hit) { return; }
		// ★★★ Тот же разбор, что и в `EnsureRowHit`: `FVector2D` неинициализирован, а прямоугольник
		//   может не определиться (строка ещё не отрисована). Нет прямоугольника — подложку ГАСИМ,
		//   иначе она осталась бы висеть там, где её положил прошлый показ, и ловила бы чужие клики.
		FVector2D Pos(FVector2D::ZeroVector), Size(FVector2D::ZeroVector);
		if (!RowRectInParent(Row, Pos, Size) || Size.X <= 0.f || Size.Y <= 0.f)
		{
			Hit->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Hit))
		{
			S->SetPosition(Pos);
			S->SetSize(Size);
			S->SetZOrder(-1);
		}
		Hit->SetVisibility(ESlateVisibility::Visible);   // подложка обязана ловить мышь
	}

	// ── FSelFrame ──
	void FSelFrame::Collect(UUserWidget* Host, const FString& Prefix)
	{
		Parts.Reset();
		Deltas.Reset();
		if (!Host || !Host->WidgetTree) { return; }
		Host->WidgetTree->ForEachWidget([this, &Prefix](UWidget* W)
		{
			if (W && W->GetName().StartsWith(Prefix, ESearchCase::IgnoreCase)) { Parts.Add(W); }
		});
		if (Parts.Num() == 0) { return; }
		// Взаимные смещения планов рамки (скин `Box_*Stroke` — это несколько картинок, не группа).
		FVector2D Base = FVector2D::ZeroVector;
		if (UCanvasPanelSlot* S0 = AsdaUi::SlotOf(Parts[0].Get()))
		{
			Base = S0->GetPosition();
		}
		for (const TWeakObjectPtr<UWidget>& W : Parts)
		{
			FVector2D P = Base;
			if (UCanvasPanelSlot* S = AsdaUi::SlotOf(W.Get()))
			{
				P = S->GetPosition();
			}
			Deltas.Add(P - Base);
		}
	}

	void FSelFrame::Hide()
	{
		for (const TWeakObjectPtr<UWidget>& W : Parts)
		{
			if (W.IsValid()) { W->SetVisibility(ESlateVisibility::Collapsed); }
		}
	}

	void FSelFrame::MoveToRow(UUserWidget* Host, const FString& RowIdc, const FVector2D& Offset)
	{
		if (Parts.Num() == 0) { return; }
		UWidget* Row = Find(Host, RowIdc);
		FVector2D Pos, Size;
		if (!Row || !RowRectInParent(Row, Pos, Size)) { Hide(); return; }
		const FVector2D Base = Pos + Offset;
		for (int32 k = 0; k < Parts.Num(); ++k)
		{
			UWidget* W = Parts[k].Get();
			UCanvasPanelSlot* S = W ? AsdaUi::SlotOf(W) : nullptr;
			if (!S) { continue; }
			const FVector2D D = Deltas.IsValidIndex(k) ? Deltas[k] : FVector2D::ZeroVector;
			S->SetPosition(Base + D);
			// Рамка не должна съедать клики по строке под ней (закон selfhittest-vs-hittest-invisible).
			W->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

// ═══════════════════════════ ЗАПИСЬ ПРИЯТЕЛЯ ══════════════════════════════════════════════════

int32 FAsdaFriendRecord::CharId() const
{
	return AsdaBytes::RdI32(B);   // [0..3] i32 CharUniqueIndex
}

FString FAsdaFriendRecord::Name() const
{
	// [11..30] char[20], cp1251 (единый декодер порта — `FAsdaLoginData::DecodeCp1251`).
	return FAsdaLoginData::DecodeCp1251(B + 11, 20);
}

// ═══════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═══════════════════════════════════════════════════

void UAsdaCommunityFriendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			HList    = Net->OnFriendList.AddUObject(this, &UAsdaCommunityFriendSubsystem::OnList);
			HRefresh = Net->OnFriendListRefresh.AddUObject(this, &UAsdaCommunityFriendSubsystem::OnListRefresh);
			HEntry   = Net->OnFriendEntryUpdate.AddUObject(this, &UAsdaCommunityFriendSubsystem::OnEntryUpdate);
			HInvite  = Net->OnFriendInvite.AddUObject(this, &UAsdaCommunityFriendSubsystem::OnInvite);
			HDelete  = Net->OnFriendDeleteResult.AddUObject(this, &UAsdaCommunityFriendSubsystem::OnDeleteResult);
			HMenu    = Net->OnPlayerMenuResult.AddUObject(this, &UAsdaCommunityFriendSubsystem::OnPlayerMenuResult);
		}
		// Клик по значку уведомления типа 2 — эталон `uiNoticeButtonDispatch@0xDF4F10`, case 2.
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			HNotice = Hb->OnNoticeIconClicked.AddUObject(this, &UAsdaCommunityFriendSubsystem::OnNoticeIcon);
		}
	}
	UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] подсистема поднята (вкладка 14500; опкоды 4150…4160)"));
}

void UAsdaCommunityFriendSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnFriendList.Remove(HList);
			Net->OnFriendListRefresh.Remove(HRefresh);
			Net->OnFriendEntryUpdate.Remove(HEntry);
			Net->OnFriendInvite.Remove(HInvite);
			Net->OnFriendDeleteResult.Remove(HDelete);
			Net->OnPlayerMenuResult.Remove(HMenu);
		}
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			Hb->OnNoticeIconClicked.Remove(HNotice);
		}
	}
	Super::Deinitialize();
}

// ★★★ К-3 / находка Р5 отчёта `14_soulmate_ревизия.md`: список приятелей и очередь входящих
//   приглашений принадлежат СЕССИИ. Особенно опасна очередь: у записи приглашения ключом
//   дедупликации служит `UserNum` — НОМЕР СЕССИИ, который после перезахода достаётся другому
//   игроку, и ответ ушёл бы не тому.
void UAsdaCommunityFriendSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Очередь приглашений и защёлка запроса — при любой причине: соединение пересоздано,
	// сессионные номера в очереди больше ничего не значат.
	InviteQueue.Reset();
	bReqSent = false;
	LastReqS = -1000.0;
	if (!AsdaIsFullSessionReset(Reason)) { return; }

	if (Friends.Num() > 0)
	{
		UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] сброс сессии (%s): снято записей %d"),
			AsdaSessionResetReasonName(Reason), Friends.Num());
	}
	Friends.Reset();
	Page = 0;
	SelRow = -1;
	LastPage = 0;
	if (bTabVisible) { RefreshList(); }
}

// ═══════════════════════════ ВКЛАДКА ══════════════════════════════════════════════════════════

// ★★★ ПРИВЯЗАНА ЛИ ВКЛАДКА К ЭТОМУ КОНКРЕТНОМУ ВИДЖЕТУ ОКНА (разбор — в объявлении).
bool UAsdaCommunityFriendSubsystem::IsBoundTo(const UUserWidget* W) const
{
	if (!W || !bTabBound) { return false; }
	// ★ Сравнение идёт через `TWeakObjectPtr`, а не через сырой адрес, И ЭТО ВАЖНО: старый виджет
	//   мы намеренно не держим, его может собрать сборщик мусора, а индекс объекта — переиспользовать
	//   под новое окно. Слабая ссылка на такой «переиспользованный» индекс отдаёт nullptr
	//   (у записи меняется serial number), то есть привязка честно считается протухшей.
	//   ⚠ Проверять дополнительно наличие наших узлов (клик-подложек строк) НЕЛЬЗЯ: если строка в
	//   разметке не нашлась, подложки не будет НИКОГДА, и такая проверка вызывала бы `BindTabControls`
	//   на КАЖДЫЙ показ вкладки — реле кнопок копились бы, и одно нажатие давало бы два действия.
	return BoundWnd.Get() == W;
}

void UAsdaCommunityFriendSubsystem::OnTabActivated(bool bVisible, UUserWidget* CommunityWindow)
{
	CommWnd = CommunityWindow;
	bTabVisible = bVisible;
	if (!bVisible) { return; }
	// ★★★ ДЕФЕКТ ПОРТА (не оригинала), найден 02.09.2026. БЫЛО: `if (!bTabBound || !CommWnd)`.
	//   `CommWnd` присваивается СТРОКОЙ ВЫШЕ, поэтому второе условие не выполнялось НИКОГДА, а
	//   `bTabBound` оставался взведённым от ПРЕДЫДУЩЕГО виджета окна. Окно 6400 пересоздаётся
	//   (`UAsdaCommunitySubsystem::EnsureWindow` → `IsWindowLive` = мир совпал И виджет во вьюпорте),
	//   то есть после КАЖДОЙ смены карты вкладка приходила на НОВОЕ дерево виджетов без привязки:
	//   подписи 14512/14511/14508/14509 не ставились и на экране висели корейские заглушки
	//   дизайнера из разметки, кнопки «Удалить»/«Шепот»/«Обновить»/пагинация были мертвы,
	//   клик-подложек строк не было (выбрать приятеля нечем), а планы рамки 14510 в `Sel`
	//   указывали на уничтоженные виджеты. СТАЛО: привязка проверяется по САМОМУ виджету.
	if (!IsBoundTo(CommWnd)) { BindTabControls(CommunityWindow); }

	// ★★★ ПОКАЗ ПОДДЕРЕВА (находка C аудита закрыта в `ApplyTabVisibility`, но ОДНА ПОЛОВИНА
	//   ОСТАЁТСЯ ЗДЕСЬ — и это НЕ дубликат).
	//   Контейнер 6400 теперь обходит РЕАЛЬНОЕ поддерево `COMMUNITY_FRND_WIN_grp` и раскрывает
	//   вкладку целиком — то, чего делал прежний обход по префиксу имени, больше не требуется.
	//   НО он ставит ВСЕМУ поддереву `SelfHitTestInvisible`, включая `UButton`, а такая кнопка
	//   мышь НЕ ЛОВИТ (закон `selfhittest-vs-hittest-invisible`): «Удалить», «Шепот», «Обновить»
	//   и пагинация оказались бы мертвы. `SetVisByPrefix` этого файла различает узлы —
	//   кнопке `Visible`, всему остальному `SelfHitTestInvisible` — поэтому проход по префиксам
	//   вкладки оставлен: он чинит именно хит-тест. (Правильное место лечения — сам
	//   `ApplyTabVisibility`, см. «ТРЕБУЕТСЯ ОТ ОРКЕСТРАТОРА».)
	AsdaCommunityUi::SetVisByPrefix(CommWnd, TEXT("CMTY_FRND_"), true);   // IgnoreCase ⇒ и `Cmty_Frnd_*`
	// ★ Узлы, которые не должны показываться НИКОГДА (у них `Visible = "0"` в САМОЙ разметке;
	//   гасит их владелец вкладки ПОСЛЕ общего показа — закон `jui-uneven-node-layout`):
	//   `CMTY_FRND_LIST7` — мёртвая 7-я строка (IDNum −1, дефект оригинала D-F5, 1:1) и
	//   `CMTY_FRND_SEL` — рамка выбора, которую показывает только `RefreshList` через `FSelFrame`.
	// ★★★ 7-я строка гасится ПО ВСЕМ СВОИМ КОНТРОЛАМ, а не только по имени подложки.
	//   ЗАЧЕМ. Показ выше идёт ПО ПРЕФИКСУ ИМЕНИ и зажигает в том числе узлы
	//   `CMTY_FRND_ONNAME7…STAT7`, в которых лежит ТЕКСТ-ЗАГЛУШКА ДИЗАЙНЕРА из разметки
	//   (`CMTY_FRND_OFFNAME7` cap «추가된», `CMTY_FRND_LV7` txt «5», `CMTY_FRND_STAT7` txt «얼…»).
	//   Сегодня они не видны только потому, что импортёр вложил их в группу
	//   `CMTY_FRND_LIST7_grp`, а свёрнутая группа прячет поддерево. Но раскладка JUI в порту
	//   неоднородна и после перегенерации бывает расплющенной (закон `jui-uneven-node-layout`),
	//   и тогда заглушки вышли бы на экран. Гасим тем же ключом, каким показывали, — по префиксу
	//   имени КАЖДОГО контрола строки. Список снят с имя-таблицы собранного `WBP_COMMUNITY_WIN`.
	//   (`CMTY_FRND_STAT7` покрывает и `STAT7On`/`STAT7Off` — они начинаются с него же.)
	// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (решение владельца «friend-list — make 7»): при 7 строках (ключ asda.Community.FriendRows7)
	//   7-я строка живая — гасится только при ключе 0 (6 строк оригинала). Общая точка — `ApplyFriendRow7`.
	ApplyFriendRow7(CommWnd, FriendRows() == 7);
	AsdaCommunityUi::SetVisByPrefix(CommWnd, TEXT("CMTY_FRND_SEL"), false);

	// ★★★ ЭТО И ЕСТЬ ЛЕЧЕНИЕ ПУСТОГО СПИСКА. `communityUI_SwitchTab` case 1 (game.c:241742)
	//   отправляет 4156 ровно здесь; порт этого не делал, поэтому 4157 никогда не приходил.
	RequestList(/*bFromButton=*/false);
	RefreshList();
}

void UAsdaCommunityFriendSubsystem::BindTabControls(UUserWidget* CommunityWindow)
{
	CommWnd = CommunityWindow;
	if (!CommWnd) { return; }
	using namespace AsdaCommunityUi;

	// ★★★ Реле ПРОШЛОГО дерева окна держать нельзя: они ведут в уничтоженные виджеты (окно 6400
	//   пересоздаётся при каждой смене карты — см. `IsBoundTo`). Чистим ровно здесь, перед новой
	//   привязкой, иначе массив рос бы на каждое пересоздание.
	Relays.Reset();

	// Подписи из таблицы текста (эталонный `InitControls@0xCE6E80`, отчёт §1.2).
	SetButtonCaption(CommWnd, TEXT("CMTY_FRND_DELBTN"),    Loc(kLocDelBtn));      // 14508
	SetButtonCaption(CommWnd, TEXT("CMTY_FRND_WHISPBTN"),  Loc(kLocWhisperBtn));  // 14509
	SetButtonCaption(CommWnd, TEXT("CMTY_FRND_RELOADBTN"), Loc(kLocReloadBtn));   // 14511
	SetTxt(CommWnd,           TEXT("CMTY_FRND_TITLETXT"),  Loc(kLocTitle));       // 14512

	// Кнопки (эталонный `OnCommand@0xCE9C20`).
	int32 nBound = 0;
	auto B = [this, &nBound](const TCHAR* Idc, int32 Tag, TFunction<void(int32)> Fn)
	{
		if (AsdaCommunityUi::BindButton(this, CommWnd, Idc, Tag, MoveTemp(Fn), Relays)) { ++nBound; }
		else { UE_LOG(LogAsdaFriend, Warning, TEXT("[приятели] кнопка «%s» не найдена"), Idc); }
	};
	B(TEXT("CMTY_FRND_DELBTN"),    14508, [this](int32) { ActionDelete();  });
	B(TEXT("CMTY_FRND_WHISPBTN"),  14509, [this](int32) { ActionWhisper(); });
	B(TEXT("CMTY_FRND_RELOADBTN"), 14511, [this](int32) { RequestList(/*bFromButton=*/true); });
	B(TEXT("CMTY_FRND_NEXTBTN1"),  14580, [this](int32) { PageNext();  });
	B(TEXT("CMTY_FRND_PREBTN1"),   14581, [this](int32) { PagePrev();  });
	B(TEXT("CMTY_FRND_ENDBTN1"),   14582, [this](int32) { PageLast();  });   // ★ миснейм «Reset…»: это ПОСЛЕДНЯЯ
	B(TEXT("CMTY_FRND_STATBTN1"),  14583, [this](int32) { PageFirst(); });

	// Клик по строкам. Эталон делит `(id − 14520)/10`, поэтому клик по ЛЮБОМУ ребёнку строки
	//   (имя/уровень/класс/статус) выбирает строку. В UMG то же даёт прозрачная подложка под всей
	//   строкой: тексты и планы — SelfHitTestInvisible, клик проходит к ней насквозь.
	for (int32 i = 0; i < kMaxRows; ++i)   // ★ 08.11.2026: все строки разметки (7-я — по ключу FriendRows7, гейт в SelectRow)
	{
		const FString RowIdc = FString::Printf(TEXT("CMTY_FRND_LIST%d"), i + 1);
		if (!EnsureRowHit(this, CommWnd, RowIdc, i, [this](int32 Row) { SelectRow(Row); }, Relays))
		{
			UE_LOG(LogAsdaFriend, Warning, TEXT("[приятели] строка «%s» в разметке не найдена"), *RowIdc);
		}
	}

	// ★★★ ЦВЕТ ПОДПИСИ «положение» — из кода, как в эталонном `uiPanel_BindControlsByID` (разбор
	//   у константы `kPositTextColor`). Ставится РОВНО ОДИН РАЗ при привязке — так же, как эталон
	//   ставит поле 923 в `InitControls`, а не при каждой перерисовке строки.
	//   ⚠ Цвет разметки — sRGB, движку нужен линейный (закон `ui-srgb-gamma-rule`).
	for (int32 i = 0; i < kMaxRows; ++i)
	{
		if (UTextBlock* T = FindLabel(CommWnd, FString::Printf(TEXT("CMTY_FRND_POSIT%d"), i + 1)))
		{
			T->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbU32ToLinear(kPositTextColor)));
		}
	}

	// Рамка выбора 14510 (эталон при инициализации снимает у неё ловлю мыши — у нас HitTestInvisible).
	Sel.Collect(CommWnd, TEXT("CMTY_FRND_SEL"));
	Sel.Hide();

	BoundWnd = CommWnd;   // ★ на ЭТОМ виджете стоит привязка — по нему решает `IsBoundTo`
	bTabBound = true;
	UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] контролы вкладки 1 привязаны: кнопок %d, строк %d, планов рамки %d"),
		nBound, FriendRows(), Sel.Parts.Num());
}

// ═══════════════════════════ ОТПРАВКА 4156 ════════════════════════════════════════════════════

void UAsdaCommunityFriendSubsystem::RequestList(bool bFromButton)
{
	// Порт `netSendReqOp4156@0xCE8A30`: вся функция обёрнута в `if (!isWithinTimeout5000(this))`,
	//   то есть чаще одного раза в 5 секунд запрос НЕ уходит (клиентский антиспам, 1:1).
	const double Now = FPlatformTime::Seconds();
	if (bReqSent && (Now - LastReqS) <= kReqCooldownS)
	{
		UE_LOG(LogAsdaFriend, Verbose,
			TEXT("[приятели] 4156 не отправлен: антиспам 5000 мс (прошло %.0f мс, источник %s)"),
			(Now - LastReqS) * 1000.0, bFromButton ? TEXT("кнопка 14511") : TEXT("показ вкладки"));
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const bool bOk = Net->SendFriendListReq4156();
	if (bOk)
	{
		LastReqS = Now;    // `timerStart_9B90@0xCE9B90`: this[66] = timeGetTime()
		bReqSent = true;   // this[67] = 1
	}
	// ★ АУДИТ 31.08.2026: гейт антиспама стоит ДВАЖДЫ — здесь и в `UAsdaNetSubsystem::
	//   SendFriendListReq4156` (там же он перезапускается приёмом 4158). Ведущий — сетевой:
	//   эталонный `isWithinTimeout5000` один, и именно сетевой отказ виден в проводе. Поэтому
	//   `false` здесь означает «нет соединения ИЛИ сетевой антиспам», а не только обрыв —
	//   прежняя формулировка лога врала.
	UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4156 (%s): %s"),
		bFromButton ? TEXT("кнопка «Обновить» 14511") : TEXT("показ вкладки"),
		bOk ? TEXT("отправлен") : TEXT("НЕ отправлен (нет соединения либо сетевой антиспам 5000 мс)"));
}

// ═══════════════════════════ ОТРИСОВКА СПИСКА ═════════════════════════════════════════════════

void UAsdaCommunityFriendSubsystem::RefreshList()
{
	// ★ Рисуем ТОЛЬКО в то дерево виджетов, на котором стоит НАША привязка: `RefreshList` зовут и
	//   сетевые обработчики (4157/4158/4160/4155/4151), а окно 6400 к тому моменту могло быть
	//   пересоздано. Писать в чужое/протухшее дерево бессмысленно — перерисует `OnTabActivated`.
	if (!IsBoundTo(CommWnd)) { return; }
	using namespace AsdaCommunityUi;

	// Порт `RefreshList@0xCE9170`: последняя страница = size/6, и при кратности — минус один.
	const int32 Size = Friends.Num();
	const int32 Rows = FriendRows();   // ★ 08.11.2026 [ИСПРАВЛЕНИЕ]: 7 строк (ключ asda.Community.FriendRows7; 0 = 6 оригинала)
	LastPage = Size / Rows;
	if ((Size % Rows) == 0 && Size > 0) { --LastPage; }
	Page = FMath::Clamp(Page, 0, FMath::Max(0, LastPage));

	// ★★★ ПЕРВЫЙ ПРОХОД — ОЧИСТКА ВСЕХ ШЕСТИ СТРОК, как в эталоне (`uiTradeListRefreshPage`,
	//   game.c:330015-330016: `for (i = 0; i < 6; ++i) tradeWnd_ClearItemSlots(row[i], 0);` — и
	//   только ПОТОМ второй цикл с заполнением). Порт этот проход пропускал и чистил лишь строки,
	//   на которые не хватило записей. Разница видна там, где `FillRow` пишет ТОЛЬКО в показанную
	//   надпись имени: у приятеля в сети текст `CMTY_FRND_OFFNAME%d` оставался от прошлого показа
	//   (в первый раз — ЗАГЛУШКА ДИЗАЙНЕРА из разметки), и стоило строке снова стать офлайновой
	//   до прихода нового 4157, как этот текст всплывал. Восстанавливаем эталонный порядок.
	// ★ 08.11.2026: 7-я строка видима только при 7 строках (в ассете группа включена шагом `asda_ui_friend_row7`; ключ 0 —
	//   откат к 6 строкам оригинала). Переключаем, только если состояние группы не совпадает с ключом (ключ сменили на ходу).
	if (const UWidget* Row7 = AsdaUi::FindNodeExact(CommWnd, TEXT("CMTY_FRND_LIST7_grp")))
	{
		const bool bShown = Row7->GetVisibility() != ESlateVisibility::Collapsed && Row7->GetVisibility() != ESlateVisibility::Hidden;
		if (bShown != (Rows == 7)) { ApplyFriendRow7(CommWnd, Rows == 7); }
	}
	for (int32 j = 0; j < kMaxRows; ++j) { ClearRow(j); }

	const int32 Base = Rows * Page;
	for (int32 j = 0; j < Rows; ++j)
	{
		const int32 Abs = Base + j;
		if (Friends.IsValidIndex(Abs)) { FillRow(j, &Friends[Abs]); }
		else                           { ClearRow(j); }
		SyncRowHit(CommWnd, FString::Printf(TEXT("CMTY_FRND_LIST%d"), j + 1));
	}

	// Рамка выбора: эталон ставит её на строку `this[64]` со сдвигом (−2, −2), иначе прячет.
	if (SelRow < 0 || !Friends.IsValidIndex(Base + SelRow)) { Sel.Hide(); }
	else { Sel.MoveToRow(CommWnd, FString::Printf(TEXT("CMTY_FRND_LIST%d"), SelRow + 1), FVector2D(-2.f, -2.f)); }

	// Счётчики страниц 14502/14503 — эталон печатает 1-based.
	SetTxt(CommWnd, TEXT("CMTY_FRND_PAGE1"), FString::FromInt(Page + 1));
	SetTxt(CommWnd, TEXT("CMTY_FRND_PAGE2"), FString::FromInt(LastPage + 1));
}

void UAsdaCommunityFriendSubsystem::ClearRow(int32 Row)
{
	// Порт `tradeWnd_ClearItemSlots@0xCE9430`: гасим ONNAME (со всем поддеревом), показываем OFFNAME,
	//   очищаем LV/JOB/POSIT/STAT.
	using namespace AsdaCommunityUi;
	const int32 N = Row + 1;
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_FRND_ONNAME%d"), N), false);
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_FRND_ONINFO%d"), N), false);
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_FRND_STAT%dOn"), N), false);
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_FRND_OFFNAME%d"), N), true);
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_FRND_STAT%dOff"), N), true);
	// ★ Эталон чистит текст ОБЕИХ надписей имени (`CWidget_SetTextAll4(row[1]…)` И `row[2]`,
	//   game.c:330056-330060) — раньше порт чистил только OFFNAME.
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_ONNAME%d"),  N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_OFFNAME%d"), N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_LV%d"),    N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_JOB%d"),   N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_POSIT%d"), N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_STAT%d"),  N), FString());
}

void UAsdaCommunityFriendSubsystem::FillRow(int32 Row, const FAsdaFriendRecord* Rec)
{
	if (!Rec) { ClearRow(Row); return; }
	using namespace AsdaCommunityUi;
	const int32 N = Row + 1;
	const bool bOnline = Rec->bOnline();
	const FString Name = Rec->Name();

	// Порт `FillRow@0xCE94F0` (game.c:330096-330126). ★ ИСПРАВЛЕНО ПО АУДИТУ 31.08.2026:
	//   эталон пишет имя ТОЛЬКО в ту надпись, которую показывает (онлайн — `row[1]` ONNAME,
	//   офлайн — `row[2]` OFFNAME), а второй контрол не трогает. Порт писал в ОБА — правило
	//   «поля, которые эталон не заполняет, порт заполнять не должен».
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_FRND_ONNAME%d"),  N), bOnline);
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_FRND_ONINFO%d"),  N), bOnline);
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_FRND_STAT%dOn"),  N), bOnline);
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_FRND_OFFNAME%d"), N), !bOnline);
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_FRND_STAT%dOff"), N), !bOnline);
	if (bOnline)
	{
		// ★★★ Надписи `CMTY_FRND_ONNAME%d` в ассете НЕТ (в разметке `CaptionText = ""`, поэтому
		//   импортёр её не создал) — достраиваем её здесь. Цвет из `CaptionInfo = "0 0 ffe9d6bc 4"`
		//   узла CMTY_FRND_ONNAME1 (`gamemain_jui.ini`), шрифт — от соседней OFFNAME-надписи.
		const FString OnIdc = FString::Printf(TEXT("CMTY_FRND_ONNAME%d"), N);
		if (UTextBlock* T = EnsureLabel(this, CommWnd, OnIdc,
			AsdaUi::SrgbU32ToLinear(0xFFE9D6BCu), FString::Printf(TEXT("CMTY_FRND_OFFNAME%d"), N)))
		{
			T->SetText(FText::FromString(Name));
			T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
	else
	{
		SetTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_OFFNAME%d"), N), Name);
	}

	// Уровень — байт [7] («%d»).
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_LV%d"), N), FString::FromInt((int32)Rec->Level()));
	// Класс — loc 12300 + код (вторая профессия приоритетна).
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_JOB%d"), N), Loc(kLocJobBase + (int32)Rec->JobForCaption()));

	// POSIT: онлайн → «<канал> - <карта>», офлайн → пусто.
	//   Канал 6 печатается как loc 13927 «MIC», прочие — как «N+1 <loc 12803 Канал>».
	FString Posit;
	if (bOnline)
	{
		FString MapName;
		if (UGameInstance* GI = GetGameInstance())
		{
			if (const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>())
			{
				MapName = LM->GetMapDisplayName((int32)Rec->MapGroup()).ToString();
			}
		}
		Posit = (Rec->Channel() == 6)
			? FString::Printf(TEXT("%s - %s"), *Loc(kLocMic), *MapName)
			: FString::Printf(TEXT("%d %s - %s"), (int32)Rec->Channel() + 1, *Loc(kLocChannel), *MapName);
	}
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_POSIT%d"), N), Posit);

	// STAT: цветные строки таблицы — берём СЫРОЙ текст с тегами и красим контрол первым сегментом.
	SetTaggedTxt(CommWnd, FString::Printf(TEXT("CMTY_FRND_STAT%d"), N), LocTagged(bOnline ? kLocStatIn : kLocStatOut));
}

// ═══════════════════════════ ДЕЙСТВИЯ ═════════════════════════════════════════════════════════

int32 UAsdaCommunityFriendSubsystem::AbsSelected() const
{
	// Порт `sub_CE9BD0`: абсолютный индекс = строка на странице + 6 × страница; −1 = выбора нет.
	if (SelRow < 0) { return -1; }
	const int32 Abs = SelRow + FriendRows() * Page;   // ★ 08.11.2026: строк на странице — 7 (ключ FriendRows7)
	return Friends.IsValidIndex(Abs) ? Abs : -1;
}

void UAsdaCommunityFriendSubsystem::SelectRow(int32 Row)
{
	// Порт ветки `a3 == 0xB`: индекс = (id − 14520)/10; если абсолютный ≥ size — выбор снимается.
	//
	// ★★★ НОВЫЙ ДЕФЕКТ ОРИГИНАЛА, найден аудитом 31.08.2026 — **D-F10**: ШЕСТУЮ СТРОКУ СПИСКА
	//   В ОРИГИНАЛЕ ВЫБРАТЬ НЕЛЬЗЯ. Доказательство двумя фактами эталона:
	//   (1) `uiContextMenuOnCommand@0xCE9C20` (game.c:330353) принимает клик ТОЛЬКО при
	//       `a2 >= 0x38B8 && a2 <= 0x38EA`, то есть IDNum 14520…**14570**;
	//   (2) `uiPanel_BindControlsByID@0xCE7480` (game.c:329337-329338) снимает ловлю мыши у
	//       ПОДЛОЖКИ строки (`CObject_SetField166(row[0], 0)` + `CObject_SetFlag668(row[0], 0)`),
	//       поэтому клик всегда приходит от ДЕТЕЙ строки.
	//   У строк 1…5 дети имеют IDNum 14521…14566 — все ≤ 14570, деление `(a2−14520)/10` даёт
	//   верный номер. У строки 6 дети имеют 14571…14576 — они ВЫШЕ границы 14570 и отбрасываются,
	//   а сама подложка 14570 мышь не ловит ⇒ строка 6 не выбирается ничем.
	//   ★ В порту клик ловит ОДНА прозрачная подложка на всю строку, поэтому дефект не
	//     воспроизводится: выбираются все 6 строк. Помечено, ключа отката не заводим — это
	//     чистая ошибка границы у автора оригинала (закон `fix-original-defects`).
	if (Row < 0 || Row >= FriendRows()) { return; }   // ★ 08.11.2026: 7-я строка выбирается при 7 строках
	const int32 Abs = Row + FriendRows() * Page;
	SelRow = Friends.IsValidIndex(Abs) ? Row : -1;
	RefreshList();
}

void UAsdaCommunityFriendSubsystem::ActionDelete()
{
	// Порт ветки 14508: нет выбора → нотис 12816; иначе модальное YES/NO (код 47) с текстом 20002,
	//   в слот 0 подставляется имя выбранного приятеля (`запись+11`).
	const int32 Abs = AbsSelected();
	if (Abs < 0) { NoticeBox(Loc(kLocPickFirst)); return; }

	const FAsdaFriendRecord Rec = Friends[Abs];
	const FString Name = Rec.Name();
	const int32   CharId = Rec.CharId();

	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(LocSlotStr(kLocDelConfirm, Name), [this, CharId, Name](bool bYes)
	{
		if (!bYes) { return; }   // «Нет» — эталон не делает ничего
		// Модальный код 47 → `netSendTimeSyncMask49@0xCE8180` = опкод 4154.
		UGameInstance* G = GetGameInstance();
		UAsdaNetSubsystem* Net = G ? G->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const bool bOk = Net && Net->SendFriendDelete4154(CharId);
		UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4154 удаление «%s» (charId=%d): %s"),
			*Name, CharId, bOk ? TEXT("отправлено") : TEXT("НЕ отправлено"));
	}, 0.f, 2288, 2289, /*очередь: эталон (1, 47) ui_core.c:44984*/47, 1);
}

void UAsdaCommunityFriendSubsystem::ActionWhisper()
{
	// Порт ветки 14509: при наличии выбора — `Chat_BeginWhisperTo(g_pChatWnd, имя)`.
	const int32 Abs = AbsSelected();
	if (Abs < 0) { return; }   // эталон здесь молчит: нотиса на «нет выбора» у «Шёпота» нет
	const FString Name = Friends[Abs].Name();
	UGameInstance* GI = GetGameInstance();
	if (UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr)
	{
		Chat->BeginWhisperTo(Name);
	}
}

void UAsdaCommunityFriendSubsystem::PageNext()
{
	// Порт `uiTradeListPageNext@0xCE9860`.
	if (Page < LastPage) { ++Page; SelRow = -1; RefreshList(); }
}

void UAsdaCommunityFriendSubsystem::PagePrev()
{
	// Порт `uiTradeListPrevPage@0xCE98D0`.
	if (Page > 0) { --Page; SelRow = -1; RefreshList(); }
}

void UAsdaCommunityFriendSubsystem::PageLast()
{
	// Порт `uiTradeListResetSelection@0xCE9980` — МИСНЕЙМ: это переход на ПОСЛЕДНЮЮ страницу.
	Page = LastPage; SelRow = -1; RefreshList();
}

void UAsdaCommunityFriendSubsystem::PageFirst()
{
	// Порт `uiTradeListResetToFirstPage@0xCE9930`.
	Page = 0; SelRow = -1; RefreshList();
}

// ═══════════════════════════ ПРИЁМ СЕТИ ═══════════════════════════════════════════════════════

int32 UAsdaCommunityFriendSubsystem::IndexOf(int32 CharId, uint8 ClassKey) const
{
	// Порт `listContainsIdTypePair`: ключ пары — i32 charId + байт класса.
	for (int32 i = 0; i < Friends.Num(); ++i)
	{
		if (Friends[i].CharId() == CharId && Friends[i].ClassKey() == ClassKey) { return i; }
	}
	return INDEX_NONE;
}

// 4157 — ЕДИНСТВЕННЫЙ наполнитель списка. Порт `pktParseListRecords31@0xCE8FE0`.
void UAsdaCommunityFriendSubsystem::OnList(const TArray<uint8>& Body)
{
	// Тело: подряд идущие записи по 31 байту. Делегат отдаёт payload (без 9-байтовой шапки), но
	//   если сетевой слой когда-нибудь отдаст пакет целиком, кратность подскажет смещение — тогда
	//   мы не разъедемся молча, а честно возьмём тело с +9 и напишем об этом в лог.
	int32 Ofs = 0;
	if ((Body.Num() % FAsdaFriendRecord::kSize) != 0
		&& Body.Num() > 9 && ((Body.Num() - 9) % FAsdaFriendRecord::kSize) == 0)
	{
		Ofs = 9;
		UE_LOG(LogAsdaFriend, Warning, TEXT("[приятели] 4157: тело пришло с шапкой — читаю с +9 (len=%d)"), Body.Num());
	}

	// ★ ЛЕЧЕНИЕ D-F3: эталон вектор НЕ чистит, поэтому каждый ответ на 4156 ДОБАВЛЯЛ весь список
	//   заново (после второго «Обновить» каждый приятель показан дважды). 4157 у сервера цельный,
	//   порционирования нет (отчёт §1.6), поэтому замена списка безопасна.
	const bool bFix = CVarFixDupList.GetValueOnGameThread() != 0;
	if (bFix) { Friends.Reset(); }

	int32 Added = 0;
	for (; Ofs + FAsdaFriendRecord::kSize <= Body.Num(); Ofs += FAsdaFriendRecord::kSize)
	{
		FAsdaFriendRecord R;
		FMemory::Memcpy(R.B, Body.GetData() + Ofs, FAsdaFriendRecord::kSize);
		Friends.Add(R);
		++Added;
	}

	// ★ АУДИТ 31.08.2026: эталонный `pktParseListRecords31@0xCE8FE0` (game.c:329939) выбор НЕ
	//   снимает — он только ДОБАВЛЯЕТ записи, поэтому индексы у него не едут. Снимать выбор
	//   осмысленно РОВНО когда мы заменили список (лечение D-F3); в режиме «как в оригинале»
	//   ведём себя 1:1 и выбор не трогаем.
	if (bFix) { SelRow = -1; }
	UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4157: записей %d, в списке %d (режим %s)"),
		Added, Friends.Num(), bFix ? TEXT("замена, D-F3 исправлен") : TEXT("добавление, как в оригинале"));
	RefreshList();
}

// 4158 — «перерисуй список». Порт `recvTradeListPageRefresh@0xCE9140`.
void UAsdaCommunityFriendSubsystem::OnListRefresh()
{
	// ★ Дефект оригинала D-F9: эталон здесь зовёт `timerStart_9B90`, то есть ПЕРЕЗАПУСКАЕТ
	//   5-секундный антиспам 4156 — после каждого серверного «перерисуй» кнопка «Обновить» мертва
	//   ещё 5 секунд. По умолчанию воспроизводим 1:1; ключ `KeepTimerOn4158 1` отключает.
	if (CVarKeepTimerOn4158.GetValueOnGameThread() == 0) { LastReqS = FPlatformTime::Seconds(); bReqSent = true; }
	UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4158: перерисовка списка (записей %d)"), Friends.Num());
	RefreshList();
}

// 4160 — обновление ОДНОЙ записи. Порт отсутствующей в эталоне ветки (дефект D-F4).
void UAsdaCommunityFriendSubsystem::OnEntryUpdate(const TArray<uint8>& Body)
{
	if (CVarHandle4160.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4160 отброшен (asda.Community.Friend.Handle4160 0 — как в оригинале)"));
		return;
	}
	// Тело 33 байта: `[0..1]` i16 (сервер кладёт 0, `sub_40BDC0@0x40BDC0`) + запись 31 байт с +2.
	int32 Ofs = 2;
	if (Body.Num() < Ofs + FAsdaFriendRecord::kSize)
	{
		// Терпим и «голую» запись без двухбайтовой шапки — размер решает однозначно.
		if (Body.Num() < FAsdaFriendRecord::kSize) { return; }
		Ofs = 0;
	}
	FAsdaFriendRecord R;
	FMemory::Memcpy(R.B, Body.GetData() + Ofs, FAsdaFriendRecord::kSize);

	const int32 Idx = IndexOf(R.CharId(), R.ClassKey());
	if (Idx != INDEX_NONE) { Friends[Idx] = R; }
	else                   { Friends.Add(R); }   // приятель, которого ещё нет в модели (вошёл в игру)
	UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4160: запись «%s» %s (онлайн=%d)"),
		*R.Name(), Idx != INDEX_NONE ? TEXT("обновлена") : TEXT("добавлена"), R.bOnline() ? 1 : 0);
	RefreshList();
}

// 4152 — входящее приглашение. Порт `questTrackerAddUniqueEntry@0xDF6BA0` (миснейм).
void UAsdaCommunityFriendSubsystem::OnInvite(const TArray<uint8>& Body)
{
	if (Body.Num() < 22) { return; }
	FAsdaFriendInvite Inv;
	Inv.Name    = FAsdaLoginData::DecodeCp1251(Body.GetData(), 20);   // [0..19] имя пригласившего
	Inv.UserNum = AsdaBytes::RdI16(Body.GetData() + 20);              // [20..21] i16 userIndex

	// Линейный поиск по очереди: повторное приглашение от того же userIndex эталон молча игнорирует.
	for (const FAsdaFriendInvite& E : InviteQueue)
	{
		if (E.UserNum == Inv.UserNum)
		{
			UE_LOG(LogAsdaFriend, Verbose, TEXT("[приятели] 4152 от «%s» уже в очереди — пропуск (1:1)"), *Inv.Name);
			return;
		}
	}
	InviteQueue.Add(Inv);

	const bool bSound = IsSoundEnabled();
	UGameInstance* GI = GetGameInstance();

	// ★★★ ДЕФЕКТ ОРИГИНАЛА D-F1 (★★★, исправлен). Эталон:
	//     if (g_bOptSoundEnable == 1) { значок + звук 215 } else { netSendPacket1039(…, 2) }
	//   — то есть при ВЫКЛЮЧЕННОМ звуке приглашение АВТОМАТИЧЕСКИ отклоняется, игрок его даже
	//   не видит. Ветка `else` явно перепутана автором: звук не должен решать судьбу приглашения.
	if (!bSound && CVarAutoRejectWhenMuted.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaFriend, Warning,
			TEXT("[приятели] 4152 от «%s»: ОРИГИНАЛЬНОЕ поведение (D-F1) — звук выключен ⇒ автоотказ 4153=2"), *Inv.Name);
		AnswerInvite(/*bAccept=*/false);
		return;
	}

	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->AddNoticeIcon(kNoticeTypeFriend);   // значок типа 2 на панели быстрых слотов
	}
	if (bSound)
	{
		if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
		{
			Snd->PlaySound2DById(kSoundInvite);   // эталонный звук 215 — только при включённом звуке
		}
	}
	UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4152: приглашение от «%s» (userNum=%d), в очереди %d"),
		*Inv.Name, Inv.UserNum, InviteQueue.Num());
}

// Клик по значку уведомления типа 2 — эталон раскрывает первое приглашение очереди.
void UAsdaCommunityFriendSubsystem::OnNoticeIcon(int32 NoticeType)
{
	if (NoticeType != kNoticeTypeFriend) { return; }
	if (InviteQueue.Num() == 0) { return; }

	// Модальное YES/NO с кодом 20081 и текстом loc 20081, слот 0 = имя пригласившего.
	const FString Name = InviteQueue[0].Name;
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(LocSlotStr(kLocInviteAsk, Name), [this](bool bYes)
	{
		AnswerInvite(bYes);   // Yes → 4153 ответ 1, No → 4153 ответ 2 (эталонные ветки 0x4E71)
	}, 0.f, 2288, 2289, /*очередь: эталон (1, 20081) ui_core.c:69405*/20081, 1);
}

// 4153 — ответ на приглашение.
void UAsdaCommunityFriendSubsystem::AnswerInvite(bool bAccept)
{
	if (InviteQueue.Num() == 0) { return; }
	const FAsdaFriendInvite Inv = InviteQueue[0];
	InviteQueue.RemoveAt(0);   // эталон удаляет элемент из очереди сразу после отправки

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// ⚠ ВТОРОЙ ПАРАМЕТР API НАЗВАН `CharId`, но по телу пакета (отчёт §1.6, 4153) там лежит
	//   i16 **userIndex** пригласившего — сервер `Recv_UserFriendOK@0x537BF0` работает именно по нему,
	//   имя из payload не использует. Передаём userNum и помечаем расхождение имени явно.
	const bool bOk = Net && Net->SendFriendAnswer4153(bAccept, Inv.UserNum, Inv.Name);

	// ★★★ ЗАКРЫТ ОТКРЫТЫЙ ВОПРОС АУДИТА (находка N: «вызова снятия значка не нашёл»).
	//   «Не нашёл» было ошибкой метода (закон `false-negatives-method`) — снятие есть, просто
	//   живёт НЕ в отправителе ответа и не в этой подсистеме:
	//     · `netSendPacket1039@0xCE7F30` (это и есть 4153) действительно значок не трогает —
	//       он только СТИРАЕТ элемент очереди приглашений (`sub_935E71(this+68, &v13)` берёт
	//       итератор на начало, `sub_93A660(this+68, …, v13)` в конце его удаляет);
	//     · значок снимает ОБЩИЙ обработчик клика по иконке уведомления
	//       `uiNoticeButtonHandler@0xDF4DB0`: `uiNoticeButtonDispatch(this, тип)` (тип 2 —
	//       показать модалку 20081) → `buffSlotCompact(this, тип)` → `MsgNotify_RefreshIcons`.
	//   Порт делает ровно это: снятие уже произошло в `UAsdaHotbarSubsystem::TickNoticeClick`
	//   в момент клика по значку, поэтому здесь снимать нечего.
	// ★ ДЕФЕКТ ОРИГИНАЛА D-F10 (исправлен и помечен). `statusBarAddIcon@0xDF3F80` отсеивает
	//   повторы только для типов 11/13/15/16 — для типа 2 каждое приглашение вешает СВОЙ значок,
	//   и после ответа на первое остальные видны. В порту `AddNoticeIcon` держит по одному значку
	//   на тип (`UAsdaHotbarSubsystem`), поэтому при двух и более приглашениях единственный значок
	//   исчезал вместе с первым ответом, и до остальных было уже не добраться. Возвращаем значок,
	//   пока очередь не пуста, — это лечение, а не эталон.
	if (InviteQueue.Num() > 0)
	{
		if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
		{
			if (!Hb->HasNoticeIcon(kNoticeTypeFriend)) { Hb->AddNoticeIcon(kNoticeTypeFriend); }
			UE_LOG(LogAsdaFriend, Log,
				TEXT("[приятели] в очереди осталось %d приглашение(й) — значок уведомления возвращён (лечение D-F10)"),
				InviteQueue.Num());
		}
	}
	UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4153 «%s» от «%s» (userNum=%d): %s"),
		bAccept ? TEXT("принять") : TEXT("отклонить"), *Inv.Name, Inv.UserNum,
		bOk ? TEXT("отправлено") : TEXT("НЕ отправлено"));
}

// 4155 — результат удаления. Порт `uiTradeListRemoveEntryByItem@0xCE8840`.
void UAsdaCommunityFriendSubsystem::OnDeleteResult(const TArray<uint8>& Body)
{
	if (Body.Num() < 6) { return; }
	const int32 Code   = (int32)Body[0];
	const int32 CharId = AsdaBytes::RdI32(Body.GetData() + 1);
	const uint8 Cls    = Body[5];

	if (Code != 1)
	{
		NoticeBox(Loc(kLocDelFailed));   // 12814 — тултип-нотис
		UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4155: отказ, код %d → нотис %d"), Code, kLocDelFailed);
		return;
	}

	const int32 Idx = IndexOf(CharId, Cls);
	if (Idx == INDEX_NONE)
	{
		// ★ Дефект оригинала D-F2 (воспроизведён 1:1): при коде 1 без совпадения в векторе эталон
		//   (`uiTradeListRemoveEntryByItem@0xCE8840`) не делает НИЧЕГО — ни удаления, ни нотиса,
		//   ни снятия выбора, ни перерисовки; строка висит до перезагрузки списка.
		//   ★ АУДИТ 31.08.2026: раньше порт здесь ДОПОЛНИТЕЛЬНО снимал выбор и перерисовывал список
		//   — это видимое расхождение с эталоном без ключа отката. Оставляем только запись в лог.
		UE_LOG(LogAsdaFriend, Warning,
			TEXT("[приятели] 4155 код 1, но записи (charId=%d, класс=%d) в списке нет — дефект D-F2, ")
			TEXT("эталон в этом случае не делает ничего"), CharId, (int32)Cls);
		return;
	}

	const FString Name = Friends[Idx].Name();
	Friends.RemoveAt(Idx);
	SelRow = -1;                                   // эталон: this[64] = −1
	ChatSocial(LocSlotStr(kLocRemovedChat, Name)); // 20001 — В ЧАТ (не тултип)
	UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4155: «%s» удалён, осталось %d"), *Name, Friends.Num());
	RefreshList();
}

// 4151 — результат приглашения. Приходит общим делегатом меню по игроку.
void UAsdaCommunityFriendSubsystem::OnPlayerMenuResult(int32 Opcode, const TArray<uint8>& Body)
{
	if (Opcode != 4151 || Body.Num() < 1) { return; }
	const int32 Code = (int32)Body[0];

	if (Code == 1)
	{
		// Эталон `uiItemNoticeDispatch@0xCE7D00`: код 1 ДОБАВЛЯЕТ запись (31 байт с +1) в вектор,
		//   печатает в чат loc 20000 и перерисовывает список.
		//   ★ Нотисы/чат по ВСЕМ кодам 4151 уже печатает `UAsdaPlayerMenuSubsystem` (он подписан на
		//     тот же делегат) — второй раз их не даём, иначе игрок увидит дубль. Здесь только МОДЕЛЬ.
		if (Body.Num() >= 1 + FAsdaFriendRecord::kSize)
		{
			FAsdaFriendRecord R;
			FMemory::Memcpy(R.B, Body.GetData() + 1, FAsdaFriendRecord::kSize);
			const int32 Idx = IndexOf(R.CharId(), R.ClassKey());
			if (Idx != INDEX_NONE) { Friends[Idx] = R; } else { Friends.Add(R); }
			UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4151 код 1: «%s» в списке, всего %d"), *R.Name(), Friends.Num());
			RefreshList();
		}
		else
		{
			UE_LOG(LogAsdaFriend, Warning, TEXT("[приятели] 4151 код 1: тело короче 32 байт (%d) — запись не добавлена"),
				Body.Num());
		}
		return;
	}

	// ★ 05.11.2026: при ключе `asda.Community.Friend.ResultChannels` 1 код 6 уже показывает обработчик меню игрока — окном
	//   12813 «Список приятелей переполнен» (на живом сервере 6 = флаг `UPFriendAdd` «полны оба списка», а не ответ 3).
	//   Здесь окно дало бы дубль, поэтому старый показ 20082 работает только при ключе 0.
	if (Code == 6 && CVarShowCode6.GetValueOnGameThread() != 0 && !AsdaFriendResultChannelsOn())
	{
		// ★ Дефект оригинала D-F6: код 6 (сервер шлёт его при ответе 3 на приглашение) эталон
		//   роняет в `default: return`. Показ включается ключом — сам клиент ответ 3 не отправляет.
		NoticeBox(Loc(kLocRejected));
		UE_LOG(LogAsdaFriend, Log, TEXT("[приятели] 4151 код 6 → нотис %d (D-F6 исправлен ключом)"), kLocRejected);
	}
}

// ═══════════════════════════ УТИЛИТЫ ══════════════════════════════════════════════════════════

FString UAsdaCommunityFriendSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

FString UAsdaCommunityFriendSubsystem::LocTagged(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocTagged` (поведение то же).
	return AsdaNotice::LocTagged(GetGameInstance(), Id);
}

FString UAsdaCommunityFriendSubsystem::LocSlotStr(int32 Id, const FString& S) const
{
	// Маркер `[ss1]` — общая подстановка порта (закон loc-string-placeholders).
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocSlot` (слот 1 + FormatNotice, как было).
	return AsdaNotice::LocSlot(GetGameInstance(), Id, S);
}

void UAsdaCommunityFriendSubsystem::ChatSocial(const FString& Text) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	AsdaNotice::ChatText(GetGameInstance(), EAsdaMsgCategory::Social, Text);   // пустой текст не отправляется — как было
}

void UAsdaCommunityFriendSubsystem::NoticeBox(const FString& Text) const
{
	// Эталонный «тултип-нотис» вкладки — `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст)` = `ONEBUT_MSGBOX` 1500.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::Social, Text, 2288);
}

bool UAsdaCommunityFriendSubsystem::IsSoundEnabled() const
{
	// Эталонный `g_bOptSoundEnable` — чекбокс звука окна настроек; само окно настроек в порту
	//   ещё не построено, поэтому источник флага один: консольный ключ звука, если он заведён.
	//   Нет ключа ⇒ считаем звук включённым (это и есть состояние по умолчанию у эталона).
	// ★ АУДИТ 31.08.2026: ключа `asda.Sound.Enabled` в порту ПОКА НЕ СУЩЕСТВУЕТ (grep по Source
	//   даёт только эту строку) — значит функция всегда возвращает true, а ключ
	//   `asda.Community.Friend.AutoRejectWhenMuted 1` (воспроизведение дефекта D-F1) сейчас
	//   недостижим. Это не ошибка вкладки: флаг появится вместе с окном настроек, и обе ветки
	//   оживут без правок здесь. Отмечено в `Reports/audit_friend.md`.
	// ★ 14.09.2026 (реестр настроек Tools/asda_settings_registry.py): перепроверено — регистрации `asda.Sound.Enabled`
	//   по-прежнему нет ни в Source, ни в Plugins, функция всегда возвращает true (звук «включён», как у эталона по умолчанию).
	//   Ключ сознательно НЕ заводим: по замыслу флаг даёт окно настроек (g_bOptSoundEnable), а временная регистрация с умолчанием 1
	//   поведения не изменила бы. Появится окно настроек — регистрировать ровно это имя.
	if (IConsoleVariable* CV = ASDA_FIND_DEV_CVAR(TEXT("asda.Sound.Enabled")))
	{
		return CV->GetInt() != 0;
	}
	return true;
}

FString UAsdaCommunityFriendSubsystem::DumpState() const
{
	return FString::Printf(
		TEXT("приятелей=%d страница=%d/%d выбор=%d очередь приглашений=%d вкладка=%s запрос(с назад)=%.1f"),
		Friends.Num(), Page + 1, LastPage + 1, SelRow, InviteQueue.Num(),
		bTabVisible ? TEXT("видна") : TEXT("скрыта"),
		bReqSent ? (FPlatformTime::Seconds() - LastReqS) : -1.0);
}

// ★ Волна 3 аудита дублей (15.09.2026): «мой клан» — перенос одинаковых тел `SelfClanId` клана, сообщества и вкладки клана.
namespace AsdaCommunityUi
{
	int32 SelfClanId(const UGameInstance* GI)
	{
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		return WS ? WS->GetSelfClanId() : -1;   // зеркало `CCharacter+0x968`
	}
}
