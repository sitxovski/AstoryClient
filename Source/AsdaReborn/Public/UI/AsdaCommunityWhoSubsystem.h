// Copyright (c) 2026 @sitxovski (Discord)
// Подсистема вкладки 3 «Кто?» окна COMMUNITY_WIN 6400 — окно `COMMUNITY_Who_WIN` 14600,
//   эталонный класс `CCommunityWhoUI` (объект `g_pCommunityWhoWnd`; исходник оригинала известен
//   точно — строка в бинаре `…\game source\interface who.cpp`).
//   ПОЛНЫЙ РАЗБОР-ИСТОЧНИК: `Reports/community_tabs_frnd_clan_who.md` РАЗДЕЛ 3 (§3.1…§3.9).
//
// ★★★ КОНТРОЛ 14780 = `CMTY_Who_Guide` (имя доказано строкой ошибки самого оригинала
//   «UI CMTY_Who_Guide Error» в `CCommunityWhoUI_InitControls@0xEBDC10`). В `.ini` у него пустой
//   `ID`, но `IDNum` ЕСТЬ, и эталон ищет его по числу. В порте узел опознаётся по имени S1a-подписи
//   `CMTY_Who_MapBase2_TXT00` (правило `anchored_names` импортёра), запасные признаки — префикс
//   якоря и скин `Box_Yellow_Op128` (409×20, `AlignYPos 8`). Эталон ставит в него ПУСТУЮ строку —
//   значит и порт его гасит; корейская заглушка из `.ini` показываться не должна. Дефект D-W6 закрыт.
//
// Назначение вкладки: найти игроков подходящего уровня. Фильтр целиком накладывает СЕРВЕР
//   (`Send_WhoCommand@0x54DB40`), клиент шлёт 5480 без полезной нагрузки и только листает результат.
//   ★★★ ПОПРАВКА 27.09.2026 (к «поправке аудита 31.08»): ОТБОР ПО ТЕКУЩЕЙ КАРТЕ В ОРИГИНАЛЕ ЕСТЬ —
//     он задан АРХИТЕКТУРОЙ сервера, а не сравнением в коде. Вывод 31.08 «отбора по карте нет
//     вообще» был ошибкой: верно лишь то, что в `Send_WhoCommand` нет сравнения номеров карт.
//     `Send_WhoCommand@0x54DB40` зовётся с менеджером пользователей ПОТОКА, принявшего пакет
//     (`*(CUser**)(*(this+1)+52)`, GameServer.c:19780), и обходит `m_iUserDirectLinkArray` ЭТОГО
//     менеджера (`sub_523E50`, 550 слотов). А у GameServer на КАЖДУЮ открытую полевую карту свой
//     поток со своим `CUserManager`: `SetUserInfo_ThreadID_Mapping` (GameServer.c:66191-66228) —
//     цикл по картам j, `CThreadRun` на карту, номер карты кладётся в менеджер (`sub_522850`:
//     `memcpy(this+2264, info, 44)` → `this+0x8E9` = номер карты, его читает `sub_40C960`).
//     Портал = переподключение клиента, и соединение принимает поток ЦЕЛЕВОЙ карты
//     (`AcceptExUser` → `Get_Empty_UserInfoArrayPointer`). Подтверждено ЖИВЫМИ логами сервера:
//     `Log\Log_UserMemory\Map8_*.txt` / `Map1_*.txt` — один и тот же игрок выходит из массива
//     менеджера карты 8 и входит в массив менеджера карты 1 (уровень лога = номер карты + 40).
//     ⇒ сервер отдаёт только игроков ТОЙ ЖЕ карты (того же экземпляра), и подпись 14610
//     «<карта> (N)» ВЕРНА. Подсказка 14780 в `.ini` говорит то же: «находит друзей схожего
//     уровня на той же карте, что и вы». Остальные условия записи (GameServer.c:127907-127911,
//     сверено дизассемблером ЖИВОГО `GameServer.exe` md5 E7B4AC38…): charId вне [0..5000] ·
//     charId ≠ свой · charId ≥ 0 · состояние +0x2E == 1 («в игре») · флаги +0x141 и +0x4F == 0 ·
//     |мой уровень − его уровень| ≤ 5 · не больше 42 записей. Клиент (приём 5481, отрисовка)
//     по карте не фильтрует и не может: в записи 35 б номера карты нет. Отчёт —
//     `Reports/owner_0927/who_clan.md` §1. Уверенность HIGH (три независимых метода).
//
// Эталонные адреса: `InitControls@0xEBDC10` · показ `uiCommunityWhoShow@0xEBE500` (внутри — 5480) ·
//   скрытие `uiCommunityWhoHide@0xEBE5E0` · отрисовка `uiCommunityWhoRefreshList@0xEBE630` ·
//   команды `friendListCmdHandler@0xEBF0E0` (миснейм) · выбор строки `uiSlotHoverHighlight@0xEBEBC0` ·
//   приём 5481 `communityWhoAppendListFromPacket@0xEBFC10` · 5482 `uiCommunityWhoUpdatePaging@0xEBFD40`.
//
// ★★★ ВАЖНО ПРО КНОПКУ 14607: это «ПРИГЛАСИТЬ В ОТРЯД» (loc 6800), а НЕ «добавить в приятели» —
//   имя idc `Cmty_Who_FaddBtn` («friend add») ВРЁТ; ветка обработчика зовёт окно приглашения отряда.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // общие UI-примитивы трёх вкладок (AsdaCommunityUi)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaCommunityWhoSubsystem.generated.h"

class UUserWidget;

// ═══════════════════ ЗАПИСЬ «КТО» — 35 БАЙТ (отчёт §3.5) ══════════════════════════════════════
//   Сведено из клиента (`uiCommunityWhoRefreshList`, `communityWhoAppendListFromPacket`) и сервера
//   (ctor `sub_558BC0`, заполнение `Send_WhoCommand@0x54DB40`); расхождений нет.
struct FAsdaWhoRecord
{
	static constexpr int32 kSize = 35;
	uint8 B[kSize] = {};

	uint8   Level()    const { return B[0]; }   // [0]  уровень
	uint8   Job1()     const { return B[1]; }   // [1]  код класса 1-й профессии (гейт: ≤ 0x21)
	uint8   Job2()     const { return B[2]; }   // [2]  код класса 2-й профессии — эталон его НЕ читает (D-W2)
	FString Name()     const;                   // [3..22] имя, cp1251
	int32   PartyId()  const;                   // [23..26] i32 id отряда (−1 = не в отряде)
	bool    bBoss()    const { return B[27] != 0; }   // [27] флаг «глава отряда»
	int32   UserNum()  const;                   // [28..29] i16 userIndex (−1 = запись пустая, конец списка)
	int32   CharId()   const;                   // [30..33] i32 CharUniqueIndex (−1 = конец списка)
	// [34] — код класса (`Nick_name_[8]` сервера), клиентом не читается.
};

UCLASS()
class ASDAREBORN_API UAsdaCommunityWhoSubsystem : public UGameInstanceSubsystem,
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

	// Вкладка 3 показана/скрыта. Сигнатура — как у `UAsdaSoulmateSubsystem::OnTabActivated`.
	//   Показ = эталонный `uiCommunityWhoShow`: страница 0, выбор −1, очистка записей и ОТПРАВКА 5480.
	void OnTabActivated(bool bVisible, UUserWidget* CommunityWindow);

	// ── Команды `friendListCmdHandler@0xEBF0E0` ──
	void PageNext();          // 14603 CMTY_Who_NEXTBTN1
	void PageLast();          // 14604 CMTY_Who_ENDBTN1
	void PagePrev();          // 14605 CMTY_Who_RELOADBTN — ★ имя врёт: это «предыдущая страница» (D-W7)
	void PageFirst();         // 14606 CMTY_Who_STATBTN1
	void ActionInviteParty(); // 14607 Cmty_Who_FaddBtn — «Пригласить в ОТРЯД» (loc 6800)
	void ActionWhisper();     // 14608 Cmty_Who_WhisBtn — «Шепот» (loc 6921)
	void SelectRow(int32 Row);// выбор строки (рамка 14770 CMTY_Who_SEL)

	int32 GetEntryCount() const { return Entries.Num(); }
	FString DumpState() const;

private:
	// ★★★ «ПРИВЯЗАНА ЛИ ВКЛАДКА К ИМЕННО ЭТОМУ ВИДЖЕТУ ОКНА» (дефект ПОРТА, правка 02.09.2026).
	//   Окно 6400 ПЕРЕСОЗДАЁТСЯ: `UAsdaCommunitySubsystem::EnsureWindow` выбрасывает виджет и строит
	//   новый, как только `UAsdaWindowSubsystem::IsWindowLive` вернул false (мир сменился при травеле,
	//   либо виджет вынут из вьюпорта). Прежний гейт `if (!bTabBound || !CommWnd)` НОВЫЙ виджет
	//   отличить не мог: `CommWnd` присваивается строкой выше, поэтому `!CommWnd` не выполнялось
	//   НИКОГДА, а `bTabBound` оставался взведённым от ПРЕДЫДУЩЕГО виджета. После первой же смены
	//   карты `BindTabControls` не звался, и на новом дереве оставались ЗАГЛУШКИ ДИЗАЙНЕРА из
	//   разметки: заголовок 14609 (кор. «비슷한 레벨의 동료 찾기»), подсказка 14780 (кор.),
	//   подписи кнопок 14607/14608 — вместо loc 6800/6921; кнопки 14603…14608 были мертвы
	//   (реле висели на уничтоженных виджетах), клик-подложек строк не было (выбрать игрока нечем),
	//   планы рамки 14770 в `Sel` указывали на уничтоженные виджеты.
	//   Признак привязки — слабая ссылка `BoundWnd` на тот виджет, на котором её ставили
	//   (обоснование выбора слабой ссылки — в определении). Рецепт общий с вкладкой «Приятель».
	bool IsBoundTo(const UUserWidget* W) const;
	void BindTabControls(UUserWidget* CommunityWindow);
	void RequestWho();                     // 5480 (тело пустое; счётчик записей сбрасывается)
	void RefreshList();                    // порт `uiCommunityWhoRefreshList@0xEBE630`
	void FillRow(int32 Row, const FAsdaWhoRecord* Rec);
	void ClearRow(int32 Row);
	void UpdatePaging();                   // порт `uiCommunityWhoUpdatePaging@0xEBFD40`

	void OnWhoResult(const TArray<uint8>& Body);   // 5481 — порция записей по 35 байт
	void OnWhoNotice(const TArray<uint8>& Body);   // 5482 — «конец списка», пересчёт страниц

	// ★★★ ЕДИНСТВЕННАЯ ТОЧКА ЭТАЛОННОГО ГЕЙТА «СТРОКА ЖИВАЯ» (переразбор 02.09.2026).
	//   Эталон проверяет ОДНО И ТО ЖЕ условие в двух местах:
	//     · отрисовка `uiCommunityWhoRefreshList@0xEBE630` (game.c:505242):
	//         `*(__int16 *)(v12 + 35*n + 328) >= 0 && *(uint8 *)(v12 + 35*n + 301) <= 0x21`
	//         — то есть `userNum >= 0 && job1 <= 0x21`; иначе строка ОЧИЩАЕТСЯ (:505274-505280);
	//     · выбор строки `uiSlotHoverHighlight@0xEBEBC0` — через видимость подложки 14701,
	//       а её ставит ровно тот же гейт (:505246 против :505275).
	//   Раньше условие было выписано в порту ТРИЖДЫ и по-разному: `RefreshList` и `SelectRow`
	//   проверяли его целиком, а `ActionInviteParty`/`ActionWhisper` и гейт рамки выбора —
	//   только `Entries.IsValidIndex()`. Из-за этого выбранная строка, ставшая пустой после
	//   прихода нового 5481, оставалась «выбранной»: рамка висела на пустой строке, а кнопки
	//   «В отряд»/«Шепот» работали по записи, которой на экране уже нет.
	//   AbsIndex — АБСОЛЮТНЫЙ индекс записи (`7 * Page + Row`), а не номер строки.
	bool IsRowLive(int32 AbsIndex) const;

	FString Loc(int32 Id) const;

	UPROPERTY(Transient) UUserWidget* CommWnd = nullptr;

	// Виджет окна, на котором СТОИТ текущая привязка (подписи, кнопки, клик-подложки, планы рамки).
	//   Слабая ссылка намеренно: держать пересозданное окно живым нельзя, а «протух» —
	//   законный ответ, по нему `IsBoundTo` и решает переприязаться.
	TWeakObjectPtr<UUserWidget> BoundWnd;

	// ── Модель (поля объекта эталона, отчёт §3.2) ──
	TArray<FAsdaWhoRecord> Entries;   // `this+300`: элемент 35 байт (в эталоне ёмкость 40 — см. D-W1)
	int32 Page       = 0;             // `this[70]` — текущая страница (0-based)
	int32 TotalPages = 1;             // `this[71]`
	int32 SelRow     = -1;            // `this[74]` — выбранная строка (−1 = нет)

	AsdaCommunityUi::FSelFrame Sel;   // рамка 14770

	/** ★★★ GC-ЯКОРЯ ОБРАБОТЧИКОВ КНОПОК ВКЛАДКИ (регрессия 02.09.2026).
	 *  Динамический делегат `UButton::OnClicked` держит реле СЛАБО (`TWeakObjectPtr` внутри
	 *  `FScriptDelegate`), а `NewObject<…>(this)` внутренний объект НЕ ЯКОРИТ. Без этого массива
	 *  первый же проход сборщика мусора убивает все реле, и кнопки вкладки МОЛЧА перестают
	 *  работать в произвольный момент (жалоба владельца «окно умирает в моменте»).
	 *  Тот же приём уже применён к `EditLimiters` и к `Relays` 30+ окон порта.
	 *  Чистится в `BindTabControls`: реле мёртвого дерева держать нельзя. */
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Relays;
	bool bTabBound = false;
	bool bTabVisible = false;

	FDelegateHandle HResult, HNotice;
};
