// Copyright (c) 2026 @sitxovski (Discord)
// Подсистема вкладки 1 «Приятель» окна COMMUNITY_WIN 6400 — окно `COMMUNITY_FRND_WIN` 14500,
//   эталонный класс `CCommunityFriendUI` (объект 0x2C01488).
//   ПОЛНЫЙ РАЗБОР-ИСТОЧНИК: `Reports/community_tabs_frnd_clan_who.md` РАЗДЕЛ 1 (§1.1…§1.9).
//   Ключевые эталонные адреса: `InitControls@0xCE6E80` · `OnCommand@0xCE9C20` (миснейм
//   `uiContextMenuOnCommand`) · `RefreshList@0xCE9170` (миснейм `uiTradeListRefreshPage`) ·
//   `FillRow@0xCE94F0` · `Send4156@0xCE8A30` · `Parse4157@0xCE8FE0` · `Recv4155@0xCE8840` ·
//   `Send4153@0xCE7F30` · `Send4154@0xCE8180` · антиспам `isWithinTimeout5000@0xCE9B00`.
//
// ★★★ ГЛАВНОЕ, ЧТО ЧИНИТ ЭТОТ ФАЙЛ (доказано в отчёте §1.7): список приятелей наполняет
//   ЕДИНСТВЕННЫЙ пакет 4157, а сервер шлёт его ТОЛЬКО в ответ на 4156. Порт 4156 не отправлял
//   ни при показе вкладки, ни по кнопке «Обновить» — оттого список был пуст ВСЕГДА, сколько бы
//   приятелей ни лежало в БД. Здесь 4156 уходит в обеих точках эталона (показ вкладки и кнопка
//   14511) с тем же клиентским антиспамом 5000 мс, а 4157 разбирается записями по 31 байту.
//
// Сеть (сборка/разбор конвертов) живёт в `UAsdaNetSubsystem` — здесь ТОЛЬКО потребление её API.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/Function.h"          // TFunction — колбэк привязки кнопок/строк
#include "UObject/WeakObjectPtr.h"       // TWeakObjectPtr — планы рамки выбора в FSelFrame
#include "Components/SlateWrapperTypes.h" // ESlateVisibility — возвращает `ShownVisibilityFor`
#include "UI/AsdaUiHelpers.h"              // ★ AsdaUi::ShownVisibilityFor — правило видимости переехало туда (14.09.2026)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaCommunityFriendSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UButton;
class UTextBlock;

// ═══════════════════════════ ОБЩИЕ UI-ПРИМИТИВЫ ТРЁХ ВКЛАДОК СООБЩЕСТВА ═══════════════════════
//   Три вкладки (14500 «Приятель», 14033 «Клан», 14600 «Кто?») устроены одинаково: строки-подложки
//   со «сплющенной» иерархией (закон `jui-flattened-hierarchy-geometry`), рамка выбора из НЕСКОЛЬКИХ
//   планов скина и подписи-контролы по idc. Чтобы одна и та же механика не появилась в трёх файлах
//   тремя копиями (закон дедупликации порта), она вынесена сюда; реализация — в
//   `AsdaCommunityFriendSubsystem.cpp`, два других файла вкладок её просто включают.
namespace AsdaCommunityUi
{
	// ★★★ Узел окна по idc из JUI. Импортёр называет ПО idc только ИНТЕРАКТИВНЫЕ и ТЕКСТОВЫЕ узлы
	//   (закон `jui-importer-widget-naming`); у контрола-картинки в ассете лежат `<idc>_<обход>_<план>`
	//   и группа поддерева `<idc>_grp`, а узла с именем `<idc>` НЕТ ВООБЩЕ. Поэтому поиск идёт
	//   цепочкой: точное имя → `<idc>_grp` → `<idc>_<цифры>` → любой узел поддерева `<idc>_*`.
	//   (Аудит 31.08.2026: без этой цепочки строки списка `CMTY_FRND_LIST1…6` не находились совсем.)
	ASDAREBORN_API UWidget* Find(UUserWidget* Host, const FString& Idc);

	// Надпись контрола: точное имя → `<idc>_Text` (так её называет импортёр) → первый `UTextBlock`
	//   поддерева `<idc>_*`. Возвращает nullptr, если надписи в ассете нет вовсе.
	ASDAREBORN_API UTextBlock* FindLabel(UUserWidget* Host, const FString& Idc);

	// То же, но при отсутствии узла СОЗДАЁТ его (`<idc>_Text`) по прямоугольнику плана контрола:
	//   импортёр не делает надпись контролу с пустым `CaptionText`, а эталон в него пишет
	//   (`CMTY_FRND_ONNAME%d` — имя приятеля в сети). Цвет — из `CaptionInfo` разметки,
	//   шрифт копируется у надписи-донора `FontDonorIdc`.
	ASDAREBORN_API UTextBlock* EnsureLabel(UObject* Owner, UUserWidget* Host, const FString& Idc,
		const FLinearColor& Color, const FString& FontDonorIdc);

	// Текст в подпись-контрол (UTextBlock). Молча пропускает отсутствующий узел — разметка вкладок
	//   неоднородна (у части строк контролы носят другое имя, см. `CmtyClan_PAPoints1`/`MEMSTUS2`).
	ASDAREBORN_API void SetTxt(UUserWidget* Host, const FString& Idc, const FString& S);

	// Текст С ТЕГАМИ игровой разметки (`<FC=AARRGGBB>…</F>`): цвет берётся из первого сегмента и
	//   ставится контролу, сам текст — очищенный. Так выводятся loc 12800/12801 «Вход»/«Выход»
	//   и loc 12227/12228 «В игре»/«Не в игре» — единственные цветные строки этих вкладок.
	ASDAREBORN_API void SetTaggedTxt(UUserWidget* Host, const FString& Idc, const FString& RawWithTags);

	// ★★★ ИНТЕРАКТИВЕН ЛИ ВИДЖЕТ — обязан ли он САМ ловить мышь.
	//   Кнопка, поле ввода, галочка, ползунок, спин-бокс, прокрутка и список — да;
	//   надписи, картинки, планы, прогресс-бары и канвасы — НЕТ (иначе съедят чужие клики).
	//   Движковый комбобокс — ОСОБЫЙ случай, его порт не показывает вовсе (см. `ShownVisibilityFor`).
	//   Список снят с импортёра JUI и с кода порта, мотивация каждого класса — в определении
	//   (`AsdaUiHelpers.cpp`, переезд 14.09.2026). Вынесен в API, потому что то же правило нужно
	//   КАЖДОМУ месту, которое показывает поддерево окна.
	// Переходник на единственную реализацию `AsdaUi::IsInteractiveWidget` (старое имя — для Login/NpcDialog).
	inline bool IsInteractiveWidget(const UWidget* W) { return AsdaUi::IsInteractiveWidget(W); }

	// ★★★ ЕДИНСТВЕННАЯ ТОЧКА ПРАВИЛА «с какой видимостью показывать узел окна».
	//   Интерактиву — полная `Visible`, декорациям — `SelfHitTestInvisible`
	//   (закон `selfhittest-vs-hittest-invisible`), движковому `UComboBoxString` — `Collapsed`.
	//   Откат к прежнему поведению порта («мышь ловит только UButton») — `asda.UI.InteractiveHitTest 0`
	//   (ветка комбобокса ключу НЕ подчиняется, обоснование — в определении).
	//   ⚠ ВТОРОЙ РЕАЛИЗАЦИИ НЕ ЗАВОДИТЬ (закон `check-existing-before-creating`). Сведены сюда
	//   (2026-09-02): `AsdaCommunitySubsystem::AsdaSetSubtreeVisibility`,
	//   `AAsdaLoginGameMode::ShowServerOrChannelView`, `UAsdaNpcDialogSubsystem::SetVis`,
	//   `UAsdaBoosterPanelSubsystem::SetVis` (+ ряд страниц окна «Вид»),
	//   `UAsdaBoosterShopSubsystem` (ряд «Профессия»), `UAsdaCharaInfoSubsystem::RevealSubtree`,
	//   `UAsdaAfterDeathSubsystem::SetSlotWidgetsVisible`.
	//   ★ 14.09.2026 реализация переехала в `AsdaUi::ShownVisibilityFor` (`AsdaUiHelpers.h`); здесь переходник.
	inline ESlateVisibility ShownVisibilityFor(const UWidget* W) { return AsdaUi::ShownVisibilityFor(W); }

	// Видимость узла И ВСЕГО ЕГО ПОДДЕРЕВА ПО ПРЕФИКСУ имени (закон `jui-uneven-node-layout`:
	//   импортёр расплющивает иерархию, поэтому «дети» лежат соседями и сами по себе не гаснут).
	ASDAREBORN_API void SetVisByPrefix(UUserWidget* Host, const FString& Prefix, bool bVisible);

	// Видимость ОДНОГО узла (без поддерева).
	ASDAREBORN_API void SetVis(UUserWidget* Host, const FString& Idc, bool bVisible);

	// Подпись кнопки: импортёр кладёт текст кнопки в узел `<idc>_Text`; если его нет — пробуем сам idc.
	ASDAREBORN_API void SetButtonCaption(UUserWidget* Host, const FString& Idc, const FString& S);

	// ★★★ ВЫБРАННЫЙ ПУНКТ ПОЛОСЫ-ПЕРЕКЛЮЧАТЕЛЯ (порт `CControl_SetSelectedIndex`).
	//   ЗАЧЕМ. Подрежимы вкладки «Клан» переключает контрол 14000 `CMTY_CLAN_3BTN`
	//   (скин `BoxBar_W69_3Btn`, в данных это `RadioButton`), и эталон
	//   `uiSwitchPanelMode@0xBC2A10` ДВАЖДЫ синхронизирует его выбор с фактическим подрежимом:
	//     · при входе `a2 == −1` ⇒ `CControl_SetSelectedIndex(modeTabCombo, 0)`;
	//     · при отказе гейта прав ⇒ `CControl_SetSelectedIndex(modeTabCombo, currentSubTab)`.
	//   Без этого полоса «залипает» на пункте, который игрок нажал, хотя подрежим не сменился
	//   (находка M3 аудита `Reports/audit_clan.md`).
	//   КАК. Импортёр `RadioButton` даёт по кнопке на пункт (`<idc>_OPT<i>`), картинку-план на
	//   пункт (`<idc>_<обход>_<план>`) и подпись `<idc>_OPT<i>_Text`. Состояние «выбран» — это
	//   кадр 2 скина, который импортёр УЖЕ положил в `FButtonStyle::Pressed` кнопки-опции.
	//   Перенос кадра на план, гашение кисти активной кнопки и цвет подписи делает ОБЩАЯ точка
	//   порта `AsdaUi::ApplyTabState` (закон дедупликации; та же функция обслуживает вкладки
	//   аукциона 13408/13409 и окно званий) — здесь только сбор узлов по idc и кэш исходных
	//   кистей, без которого нечего возвращать неактивным пунктам.
	//   Молча выходит, если полосы с таким idc в дереве нет (разметка вкладок неоднородна).
	ASDAREBORN_API void SetOptSelected(UUserWidget* Host, const FString& BarIdc, int32 Index);

	// Привязать обработчик к кнопке по idc. Owner — объект-владелец реле (подсистема).
	//   Возвращает false, если кнопки с таким именем в дереве нет (тогда вызывающий пишет варнинг).
	// ★★★ `OutRelays` — GC-ЯКОРЬ, ПАРАМЕТР ОБЯЗАТЕЛЬНЫЙ (регрессия 02.09.2026, разбор ниже).
	//   `UButton::OnClicked` — ДИНАМИЧЕСКИЙ мультикаст: его запись (`FScriptDelegate`) держит
	//   приёмник СЛАБО (`TWeakObjectPtr` + имя функции). Реле `UAsdaButtonRelay` — обычный
	//   `UObject`; `NewObject<…>(Owner)` его НЕ УДЕРЖИВАЕТ (внешний объект не якорит внутренний),
	//   поэтому первый же проход сборщика мусора убивает реле, и кнопка МОЛЧА перестаёт работать
	//   в произвольный момент. Ровно об этом уже написано у `EditLimiters`
	//   (`AsdaCommunityClanSubsystem.h`), и так же поступают все прочие окна порта
	//   (`UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays` — 30+ подсистем).
	//   Параметр сделан ОБЯЗАТЕЛЬНЫМ намеренно: со значением по умолчанию про якорь снова забудут.
	// ★ U20 (В1 дедупа №2): сама привязка — `AsdaUi::BindButton` (`AsdaUiHelpers.h`); здесь — поиск кнопки по idc вкладки.
	ASDAREBORN_API bool BindButton(UObject* Owner, UUserWidget* Host, const FString& Idc,
		int32 Tag, TFunction<void(int32)> Fn, TArray<TObjectPtr<UObject>>& OutRelays);

	// ★ КЛИК ПО СТРОКЕ СПИСКА. В UMG картинка кликов не ловит, поэтому над строкой-подложкой
	//   создаётся ПРОЗРАЧНАЯ кнопка с `ZOrder = -1` (под всеми узлами строки: тексты и планы —
	//   `SelfHitTestInvisible`, клик проходит к ней насквозь; `ShiftChild` порядок НЕ меняет —
	//   закон `umg-shiftchild-does-not-reorder`).
	//   Прямоугольник берётся ПО ЖИВОЙ ГЕОМЕТРИИ строки (закон `ui-check-game-path-geometry`),
	//   а пока геометрии нет (первый кадр) — по слоту канваса; на каждом обновлении списка
	//   положение пересинхронизируется.
	//   ★★★ Если прямоугольник строки не определяется НИ ОДНИМ из двух способов — подложка НЕ
	//     СОЗДАЁТСЯ вовсе (правка 02.09.2026). Раньше возврат `RowRectInParent` не проверялся, а
	//     `FVector2D` в UE конструируется НЕИНИЦИАЛИЗИРОВАННЫМ ⇒ в слот уезжал мусор со стека, и
	//     прозрачная кнопка-подложка могла лечь произвольным прямоугольником поверх всего окна,
	//     съедая клики по вкладкам и крестику.
	//   ★★★ `OutRelays` — GC-якорь реле, обязателен (разбор — у `BindButton`).
	ASDAREBORN_API UButton* EnsureRowHit(UObject* Owner, UUserWidget* Host, const FString& RowIdc,
		int32 RowIndex, TFunction<void(int32)> Fn, TArray<TObjectPtr<UObject>>& OutRelays);

	// Пересинхронизировать прямоугольник клик-подложки строки по её живой геометрии.
	ASDAREBORN_API void SyncRowHit(UUserWidget* Host, const FString& RowIdc);

	// ── Рамка выбора строки (скин `Box_*Stroke` = НЕСКОЛЬКО планов: импортёр даёт узлы
	//    `<idc>_<n>_0`). Двигаем пачкой, сохраняя взаимные смещения. ──
	struct ASDAREBORN_API FSelFrame
	{
		TArray<TWeakObjectPtr<UWidget>> Parts;   // все планы рамки
		TArray<FVector2D>               Deltas;  // смещение плана относительно плана 0

		void Collect(UUserWidget* Host, const FString& Prefix);   // собрать по префиксу имени
		void Hide();                                              // спрятать все планы
		// Поставить рамку на строку `RowIdc` со смещением `Offset` (эталон: −2,−2 у «Приятеля»).
		void MoveToRow(UUserWidget* Host, const FString& RowIdc, const FVector2D& Offset);
		bool IsValid() const { return Parts.Num() > 0; }
	};
}

// ★ Волна 3 аудита дублей (15.09.2026): «мой клан» — одна точка вместо трёх копий (клан, сообщество, вкладка клана).
class UGameInstance;
namespace AsdaCommunityUi
{
	// Id клана своего персонажа — зеркало `CCharacter+0x968` (`UAsdaWorldSubsystem::GetSelfClanId`); −1 = клана нет.
	ASDAREBORN_API int32 SelfClanId(const UGameInstance* GI);
}

// ═══════════════════════════ ЗАПИСЬ ПРИЯТЕЛЯ — 31 БАЙТ (отчёт §1.5) ═══════════════════════════
//   Раскладка выведена ДВУМЯ независимыми путями (клиент `FillRow@0xCE94F0` + сервер
//   `sub_4069F0@0x4069F0`), расхождений нет, уверенность HIGH. Держим запись СЫРОЙ — так же,
//   как эталонный `std::vector` элементов по 31 байту (`CCommunityFriendUI+59`).
struct FAsdaFriendRecord
{
	static constexpr int32 kSize = 31;   // шаг записи в 4157 и её размер в 4151/4155/4160
	uint8 B[kSize] = {};

	int32 CharId()   const;              // [0..3]  i32 CharUniqueIndex — ключ пары «charId+класс»
	uint8 ClassKey() const { return B[4];  }   // [4]  код класса (2-я половина ключа дедупликации)
	uint8 Channel()  const { return B[5];  }   // [5]  канал (0-based; значение 6 ⇒ строка loc 13927 «MIC»)
	uint8 MapGroup() const { return B[6];  }   // [6]  id карты (для имени карты в строке POSIT)
	uint8 Level()    const { return B[7];  }   // [7]  УРОВЕНЬ (миснейм «EquippedFlag» тут ни при чём)
	uint8 Job1()     const { return B[8];  }   // [8]  класс 1-й профессии
	uint8 Job2()     const { return B[9];  }   // [9]  класс 2-й профессии (приоритетный, если ≠ 0)
	bool  bOnline()  const { return B[10] == 1; }  // [10] онлайн-флаг: эталон сверяет именно с 1
	FString Name()   const;              // [11..30] имя, cp1251 до NUL

	// Класс для подписи JOB: эталон `ui_GetLocalizedNotice(rec[9] ? rec[9] : rec[8])` → loc 12300+код.
	uint8 JobForCaption() const { return B[9] ? B[9] : B[8]; }
};

// Входящее приглашение в приятели (4152). Эталон держит их в контейнере `CCommunityFriendUI+0x110`
//   (он же `g_pIdListContainer`) и раскрывает по клику на значке уведомления типа 2.
struct FAsdaFriendInvite
{
	int32   UserNum = -1;   // [20..21] i16 userIndex пригласившего — ключ дедупликации очереди
	FString Name;           // [0..19] имя пригласившего (cp1251)
};

UCLASS()
class ASDAREBORN_API UAsdaCommunityFriendSubsystem : public UGameInstanceSubsystem,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Находка Р5 отчёта 14_soulmate: подсистема жила на GameInstance и переживала
	// разрыв, смену персонажа и травел вместе со списками и защёлками сессии.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Вкладка 1 показана/скрыта. Сигнатура — как у `UAsdaSoulmateSubsystem::OnTabActivated`.
	//   Показ = эталонный `communityUI_SwitchTab` case 1: привязать контролы, ОТПРАВИТЬ 4156,
	//   перерисовать список. ★ Состояние «вкладка активна» защёлкивается ТОЛЬКО здесь, после
	//   реального показа (закон `no-state-latch-before-shown`).
	void OnTabActivated(bool bVisible, UUserWidget* CommunityWindow);

	// ── Действия эталонного `OnCommand@0xCE9C20` ──
	void ActionDelete();              // 14508 CMTY_FRND_DELBTN → нотис 12816 либо модалка 20002 (код 47)
	void ActionWhisper();             // 14509 CMTY_FRND_WHISPBTN → «/w имя » в поле чата
	void RequestList(bool bFromButton);  // 14511 CMTY_FRND_RELOADBTN и показ вкладки → 4156 (антиспам 5000 мс)
	void PageNext();                  // 14580 CMTY_FRND_NEXTBTN1
	void PagePrev();                  // 14581 CMTY_FRND_PREBTN1
	void PageLast();                  // 14582 CMTY_FRND_ENDBTN1 (миснейм «ResetSelection»: это ПОСЛЕДНЯЯ страница)
	void PageFirst();                 // 14583 CMTY_FRND_STATBTN1
	void SelectRow(int32 Row);        // клик по строке 14520…14570 (эталон: `(id − 14520)/10`)

	// Ответ на входящее приглашение (4153): true = принять (1), false = отклонить (2).
	void AnswerInvite(bool bAccept);

	// Тип значка уведомления на панели быстрых слотов: эталон `uiNoticeButtonDispatch` case 2.
	static constexpr int32 kNoticeTypeFriend = 2;

	// Число приятелей в модели (для самопроверки и консольного дампа).
	int32 GetFriendCount() const { return Friends.Num(); }
	FString DumpState() const;

private:
	// ── UI ──
	// ★★★ «ПРИВЯЗАНА ЛИ ВКЛАДКА К ИМЕННО ЭТОМУ ВИДЖЕТУ ОКНА» (дефект порта, правка 02.09.2026).
	//   Окно 6400 ПЕРЕСОЗДАЁТСЯ: `UAsdaCommunitySubsystem::EnsureWindow` выбрасывает виджет и строит
	//   новый, как только `UAsdaWindowSubsystem::IsWindowLive` вернул false (мир сменился при травеле,
	//   либо виджет вынут из вьюпорта). Прежний гейт `if (!bTabBound || !CommWnd)` НОВЫЙ виджет
	//   отличить не мог, поэтому после первой же смены карты подписи вкладки не ставились и на экране
	//   оставались КОРЕЙСКИЕ ЗАГЛУШКИ ДИЗАЙНЕРА из разметки (14512 «친구», 14511 «새로고침»,
	//   14508 «친구삭제», 14509 «귓속말»), а кнопки и выбор строки были мертвы.
	//   Признак привязки — слабая ссылка `BoundWnd` на тот виджет, на котором её ставили
	//   (обоснование выбора слабой ссылки — в определении).
	bool IsBoundTo(const UUserWidget* W) const;
	void BindTabControls(UUserWidget* CommunityWindow);
	void RefreshList();                       // порт `RefreshList@0xCE9170`: 6 строк, пагинация, рамка
	void FillRow(int32 Row, const FAsdaFriendRecord* Rec);   // порт `FillRow@0xCE94F0`
	void ClearRow(int32 Row);                 // порт `tradeWnd_ClearItemSlots@0xCE9430`

	// ── Приём сети ──
	void OnList(const TArray<uint8>& Body);          // 4157 — ЕДИНСТВЕННЫЙ наполнитель списка
	void OnListRefresh();                            // 4158 — «перерисуй» + перезапуск антиспама
	void OnEntryUpdate(const TArray<uint8>& Body);   // 4160 — обновление ОДНОЙ записи (D-F4)
	void OnInvite(const TArray<uint8>& Body);        // 4152 — входящее приглашение (D-F1)
	void OnDeleteResult(const TArray<uint8>& Body);  // 4155 — результат удаления
	void OnPlayerMenuResult(int32 Opcode, const TArray<uint8>& Body);   // 4151 — результат приглашения
	void OnNoticeIcon(int32 NoticeType);             // клик по значку уведомления типа 2

	// ── Утилиты ──
	FString Loc(int32 Id) const;                     // текст из таблицы, теги срезаны
	FString LocTagged(int32 Id) const;                  // текст из таблицы С тегами (цветные строки) (★ Дедуп №2, В1-B (U10a): было `LocRaw` — по смыслу это `AsdaNotice::LocTagged` (теги сохранены, маркеры [ss1]/[sd1] не тронуты))
	FString LocSlotStr(int32 Id, const FString& S) const;   // подстановка маркера `[ss1]`
	void    ChatSocial(const FString& Text) const;   // канал чата (эталон: цвет −11514, канал 10000)
	void    NoticeBox(const FString& Text) const;    // тултип-нотис эталона = ONEBUT_MSGBOX 1500
	bool    IsSoundEnabled() const;                  // порт `g_bOptSoundEnable` (см. D-F1 в .cpp)
	int32   AbsSelected() const;                     // порт `sub_CE9BD0`: this[64] + 6*this[63]
	int32   IndexOf(int32 CharId, uint8 ClassKey) const;    // порт `listContainsIdTypePair`

	// Окно-владелец вкладки (то же, что у соулмейта: контролы вкладки живут в дереве 6400).
	UPROPERTY(Transient) UUserWidget* CommWnd = nullptr;

	// Виджет окна, на котором СТОИТ текущая привязка (подписи, кнопки, клик-подложки, планы рамки).
	//   Слабая ссылка намеренно: держать пересозданное окно живым нельзя, а «протух» —
	//   законный ответ, по нему `IsBoundTo` и решает переприязаться.
	TWeakObjectPtr<UUserWidget> BoundWnd;

	// ── Модель (эталонные поля объекта, отчёт §1.2) ──
	TArray<FAsdaFriendRecord> Friends;   // `this+59` — вектор записей по 31 байту
	int32   Page      = 0;               // `this+63` — текущая страница (0-based)
	int32   SelRow    = -1;              // `this+64` — выбранная строка на странице (−1 = нет)
	int32   LastPage  = 0;               // `this+65` — индекс последней страницы (0-based)
	double  LastReqS  = -1000.0;         // `this+66` — метка времени последнего 4156 (секунды)
	bool    bReqSent  = false;           // `this+67` — «запрос был»
	TArray<FAsdaFriendInvite> InviteQueue;   // `this+68` — очередь входящих приглашений

	AsdaCommunityUi::FSelFrame Sel;      // рамка 14510 CMTY_FRND_SEL

	/** ★★★ GC-ЯКОРЯ ОБРАБОТЧИКОВ КНОПОК ВКЛАДКИ (регрессия 02.09.2026).
	 *  Динамический делегат `UButton::OnClicked` держит реле СЛАБО, а `NewObject<…>(this)` внутренний
	 *  объект не якорит — без этого массива сборщик мусора убивает все реле вкладки, и кнопки
	 *  «Удалить»/«Шёпот»/«Обновить»/пагинация и выбор строки перестают работать в произвольный
	 *  момент. Тот же приём уже применён к `EditLimiters` вкладки «Клан» и к `Relays` 30+ окон порта.
	 *  Чистится в `BindTabControls`: реле мёртвого дерева держать нельзя. */
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Relays;

	bool bTabBound = false;
	bool bTabVisible = false;

	FDelegateHandle HList, HRefresh, HEntry, HInvite, HDelete, HMenu, HNotice;
};
