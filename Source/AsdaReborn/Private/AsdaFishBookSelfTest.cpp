// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ В3 РЫБАЛКИ (08.10.2026): «КНИГА РЫБАКА» 74000 + окно регистрации 74200 + нумпад режима 10. [ПОРТ]
//   Отчёт — `Reports/owner_1008/fishing_impl_W3.md`. Реверс — `fishing_parts/ui_book.md`, `client_net.md` §1.5-§2.12.
//
// ЧТО ПРОВЕРЯЕТ (путь игрока — НАСТОЯЩИЕ события Slate `SimulateClickAt` по ЖИВОЙ геометрии, шаг раз в 0,4 с;
//   приём — штатный `DebugFeedPacket` → `HandlePacket` → очередь книги; отправка — БОЕВЫЕ отправители под перехватом):
//   A. Данные DT_FishBook: 9 томов; Р12 — сервер (награды тома 1 109/125/137/69678; ★ 09.10 книга тома 5 = 69682 — fishbooks_69682) и
//      контрольная пара `asda.FishBook.ClientTable 1` (128/76/…, книга 69682).
//   B. 6174 ×3 → тома 0/1/2 зарегистрированы, счёт/макс./мин.; Р6 — индекс 9 и −1 отвергнуты (контроль); 6175 — ничего;
//      6174 окно не открывает и не перерисовывает.
//   C. Доп-меню cmd 9 → окно открыто, сумка закрыта; повтор cmd 9 — не тоггл. Вкладки: 3 из 4, подписи = имена томов.
//   D. Клик по вкладке 2 → том 1, сетка 4 ячейки («?????» у непойманных), итоги 74102/74104.
//   E. ЛКМ по ячейке → выбор + красная рамка; двойной → окно 74200, стопки рыбы из рюкзака (контроль: не-рыба не попала).
//   F. ЛКМ по стопке → нумпад режима 10, максимум = min(стопка 3, 13 − 11) = 2; ввод 2 + «ОК» → к сдаче 2, курсор
//      74241 РОВНО на выбранной стопке (Р7/Д1); контроль `LegacyCursor 1` — курсор на ДРУГОЙ стопке со сдвигом.
//   G. «ОК» 74252 → РОВНО ОДИН 6180: 19 б (charId, ИНДЕКС тома 1, слот 14, контейнер 2, кол-во 2), конверт 'S';
//      контроль — без выбора стопки 6180 нет, msgbox 111060; модальность 74200 — кнопка награды под ним молчит.
//   H. 6181 код 1 → счёт тома, вес, запись рюкзака, окно регистрации закрыто; код 7 → msgbox «7».
//   I. Награда: выбор 3-й → рамка; 74090 при незавершённом томе → 111023, 6182 нет; завершённый → 6182 (id 1, код 137);
//      без выбора → 111071; Д2 — подпись 74085 не выбирает (контроль `LegacyReward5 1` — выбирает «5-ю»).
//      Наведение на награду → подсказка владельца FishBook, уход — снята.
//   J. 6183 код 1 → msgbox 111013 (имя тома, имя награды), том снят; код 4 → ничего.
//   K. Удаление: 74271 → диалог 77 → «ОК» → 6178 (id тома 0); 6179 код 1 → том снят + 111072; код 0 → ничего;
//      незарегистрированный том → 111069 без диалога.
//   L. Книга-предмет из сумки (подтип 210) → диалог 79 (111018: 1-й [..] = имя предмета, 2-й = своё имя) → «ОК» → 6176 15 б (charId + ref11), «ждём 6177»;
//      6177 код 1 → том зарегистрирован, книга ушла из сумки; код 5 → 111020.
//   M. FSM 101 (`BookRegisterProbe`): есть книга НЕзарегистрированного тома → диалог 79, повтор не плодит окно,
//      «Отмена» → автомат (0 остаётся 0); книги нет → false (автомат уйдёт в 100).
//   N. Улов 6172 (`OnFishCaught`): первая поимка → счёт 1, макс./мин.; повтор — только размеры.
//   O. Геометрия путём игрока (`GetCachedGeometry`): 30 ячеек и 30 стопок не пересекаются и лежат в окне, 4 вкладки
//      не пересекаются, панель 74270 в окне; ESC закрывает сначала 74200, затем книгу.
//   ★ Ревизия В3 (08.10.2026, отчёт `fishing_impl_W3fix_W6.md`) — добавлено:
//   E0 двойной щелчок ПОСЛЕ `GetDoubleClickTime` — одиночный (ВАЖН-6); E0a/E0b хит-рамка 74029 ±1 px и зазор (МЕЛК-5);
//   V1 нумпад с максимумом 0: «5» → «0» + чат 6835, «ОК» → ничего, 74252 → 111060 без 6180; контроль — обычный режим
//   общей точки при максимуме 0 по-прежнему «без предела» (ВАЖН-1); V3 сумка меняется под 74200 → выбор снят / кол-во
//   урезано, контроль `LiveRefresh 0` — как оригинал (ВАЖН-3); V2 окно сумки поверх ячейки — клик молчит, контроль без
//   окна — выбирает (ВАЖН-2); H3 6181 код 0; J3/J4 6183 по item_list и самоцветам; L5a чат 111073; N через пакет 6172;
//   M4 «Отмена» 79 при fishState ≠ 0 → 100; O6 рамки = иконки наград (МЕЛК-12); O7…O10 выравнивание/цвет текстов из
//   ассета (ВАЖН-5); T лента вкладок при 6 томах; S сброс сессии по 4 причинам; B2 пара границы `<` (МЕЛК-8);
//   L1 известное имя (МЕЛК-9); цифры нумпада и кнопки TWOBUT — настоящими кликами (МЕЛК-11).
//   ★ W3fix2 (09.10.2026, ревизия `fishing_impl_W3fix_W6_REVIEW.md`, отчёт `fishing_impl_W3fix2.md`): E0c медленный двойной
//   щелчок 0,35 с < Δt ≤ `GetDoubleClickTime` (решающий случай ВАЖН-6); V1c2 «−» с клавиатуры в строгом нумпаде отброшен,
//   V1h пара — «−» от MAX сохранён; V3e2 «ОК» 74252 в кадре уменьшения стопки — то же правило, что перерисовка; V2e/V2f
//   всплывающий слой чужого окна (`PushTopmost`) над ячейкой — клик молчит; M4k контроль M4 — тот же интервал без клика.
//   Состояние (charId, вес, сумка, экип-слот 10, навык рыбалки, ключи, очередь окон) восстанавливается в конце.
//
// Консоль: `asda.FishBook.SelfTest [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-КНИГА», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaFishBookSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "AsdaNetSubsystem.h"
// RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"   // ★ 07.11.2026: язык сессии — ожидания имён на EN   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "UI/AsdaWindowSubsystem.h"            // SimulateClickAt / нумпад / очередь окон
#include "UI/AsdaInventorySubsystem.h"         // подкладка предметов
#include "UI/AsdaItemTooltipSubsystem.h"       // владелец подсказки
#include "UI/AsdaTipPanel.h"                   // EAsdaTipOwner
#include "UI/AsdaExtraMenuSubsystem.h"         // cmd 9
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"                  // WidgetAbsRect / IsShown
#include "UI/AsdaMarkup.h"                     // FAsdaMarkup::StripTags
#include "UI/AsdaChatSubsystem.h"              // журнал чата: нотисы 6835 / 111073 (текст, цвет, буфер A)
#include "UI/AsdaSystemMenuSubsystem.h"        // fishfix 09.10: U3/U4 — клик сквозь книгу в панель 73400
#include "UI/AsdaPremiumShopSubsystem.h"       // ★ 11.10.2026 (В3 S1): штатное закрытие магазина 6000 в уборке U4
#include "Items/AsdaItemTableSubsystem.h"
#include "World/AsdaFishingSubsystem.h"
#include "World/AsdaWorldSubsystem.h"           // своё имя (ожидание 111018)
#include "Blueprint/UserWidget.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"          // fishfix 09.10: ширина разметки окна из слотов рамки
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"                 // fishfix 09.10: кнопки панели 73400 (U3/U4)
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"                     // FScreenshotRequest — кадр вместе со слоем Slate
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishBookTest, Log, All);

namespace AsdaFishBookTestLocal
{
	constexpr int32 kCharId    = 424242;   // свой charId на время прогона
	constexpr int32 kBook1     = 69677;    // книга-предмет тома 1 (подтип 210)
	constexpr int32 kBook3     = 69679;    // книга-предмет тома 3 (подтип 210)
	constexpr int32 kFishA     = 69577;    // Карась — рыба тома 0 и тома 1 [0]
	constexpr int32 kFishB     = 69582;    // рыба тома 1 [1]
	constexpr int32 kFishC     = 69587;    // рыба тома 1 [2] и тома 2 [2]
	constexpr int32 kNotFish   = 20571;    // не рыба (контроль)
	constexpr int32 kCellBook  = 5;        // книга тома 1
	constexpr int32 kCellBook3 = 6;        // книга тома 3 (шаг M)
	constexpr int32 kCellA1    = 10;       // Карась ×10
	constexpr int32 kCellA2    = 12;       // Карась ×4
	constexpr int32 kCellB1    = 14;       // рыба B ×3
	constexpr int32 kCellB2    = 16;       // рыба B ×1
	constexpr int32 kCellOther = 20;       // не рыба ×3
	constexpr int32 kCellCaught = 30;      // ячейка рыбы из 6172 (шаг N)
	constexpr int32 kSowel = 7001;         // самоцвет (sowell_list.csv, строка 0) — 6183 по таблице самоцветов (J3)
	static const TCHAR* kSelfName = TEXT("Рыбак");   // известное имя своего персонажа (второй %s строки 111018, МЕЛК-9)
	// Тело 6172 (121 б): @0 код, @1 i16 навык, @3 i16 вес, @5 i16 размер, @7 запись наживки 57, @64 запись рыбы 57.
	static TArray<uint8> B6172(uint8 Code, int32 Skill, int32 Weight, int32 Size, const TArray<uint8>& Bait, const TArray<uint8>& Fish);

	// Запись предмета 57 б: @0 код, @4 контейнер, @5 i16 слот, @7 i16 −1, @9 i32 кол-во.
	static TArray<uint8> Rec57(int32 Code, int32 Cont, int32 Slot, int32 Amount)
	{
		return FAsdaItemRecordWriter::Cell(Code, Cont, Slot, Amount);   // ★ U17
	}
	// Запись тома 305 б: @0 индекс, @4 тип, @5 i32[30] (не читается), @125/@185/@245 i16[30] счёт/макс./мин.
	static TArray<uint8> Page305(int32 Idx, int32 Type, const TArray<int32>& Cnt, const TArray<int32>& Mx, const TArray<int32>& Mn)
	{
		TArray<uint8> B; B.SetNumZeroed(305);
		AsdaBytes::WrI32(B.GetData(), Idx); B[4] = (uint8)Type;
		for (int32 i = 0; i < 30; ++i)
		{
			AsdaBytes::WrI16(B.GetData() + 125 + 2 * i, Cnt.IsValidIndex(i) ? Cnt[i] : 0);
			AsdaBytes::WrI16(B.GetData() + 185 + 2 * i, Mx.IsValidIndex(i) ? Mx[i] : 0);
			AsdaBytes::WrI16(B.GetData() + 245 + 2 * i, Mn.IsValidIndex(i) ? Mn[i] : 0);
		}
		return B;
	}
	// Ответ 6177/6181 365 б: @0 код, @1 i16 вес, @3 запись 57, @60 том 305.
	static TArray<uint8> Resp365(int32 Code, int32 Weight, const TArray<uint8>& Rec, const TArray<uint8>& Page)
	{
		TArray<uint8> B; B.SetNumZeroed(365);
		B[0] = (uint8)Code; AsdaBytes::WrI16(B.GetData() + 1, Weight);
		FMemory::Memcpy(B.GetData() + 3, Rec.GetData(), 57);
		FMemory::Memcpy(B.GetData() + 60, Page.GetData(), 305);
		return B;
	}
	// Ответ 6179 5 б: @0 код, @1 i32 id тома.
	static TArray<uint8> B6179(int32 Code, int32 Id)
	{
		TArray<uint8> B; B.SetNumZeroed(5); B[0] = (uint8)Code; AsdaBytes::WrI32(B.GetData() + 1, Id); return B;
	}
	// Ответ 6183 64 б: @0 код, @1 i32 id тома, @5 i16 вес, @7 запись 57.
	static TArray<uint8> B6183(int32 Code, int32 PageId, int32 Weight, const TArray<uint8>& Rec)
	{
		TArray<uint8> B; B.SetNumZeroed(64);
		B[0] = (uint8)Code; AsdaBytes::WrI32(B.GetData() + 1, PageId); AsdaBytes::WrI16(B.GetData() + 5, Weight);
		FMemory::Memcpy(B.GetData() + 7, Rec.GetData(), 57);
		return B;
	}
	static TArray<uint8> B6172(uint8 Code, int32 Skill, int32 Weight, int32 Size, const TArray<uint8>& Bait, const TArray<uint8>& Fish)
	{
		TArray<uint8> B; B.SetNumZeroed(121);
		B[0] = Code; AsdaBytes::WrI16(B.GetData() + 1, Skill); AsdaBytes::WrI16(B.GetData() + 3, Weight); AsdaBytes::WrI16(B.GetData() + 5, Size);
		FMemory::Memcpy(B.GetData() + 7, Bait.GetData(), 57);
		FMemory::Memcpy(B.GetData() + 64, Fish.GetData(), 57);
		return B;
	}
	// Поставить ключ консоли на время прогона.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): через `AsdaCVarSave` — исходные значение и SetBy запоминаются при первом
	//   касании и возвращаются `RestoreAll` в `Finish` (прежде — снимок 4 ключей `CVarsWas` и возврат с SetByConsole;
	//   ModalLock, Trunc13, SysMenu.LegacyClickThrough возвращались константой).
	static void SetCVar(const TCHAR* Name, int32 Val) { AsdaCVarSave::SetForTest(Name, Val); }
}

struct FAsdaFishBookSelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено

	struct FCap { uint16 Op = 0; TArray<uint8> P; TCHAR Env = 0; };   // перехваченный исходящий пакет
	TSharedRef<TArray<FCap>> Caps = MakeShared<TArray<FCap>>();     // перехват (весь трафик)
	uint32 Gen = 0;                                                 // поколение перехватчика
	TArray<TFunction<void(UWorld*)>> Steps;                         // шаги (по одному за тик планировщика)
	int32 CharIdWas = 0;                                            // свой charId до прогона
	int32 WeightWas = -1;                                           // вес до прогона
	TArray<TArray<uint8>> BagWas;                                   // записи контейнера 2 до прогона
	FString Res;                                                    // метка разрешения (журнал)
	bool bShot = false;                                             // снимать кадры окна (аргумент `shot`)
	TArray<uint8> Eq10Was;                                          // экип-слот 10 (наживка) до прогона — его трогает запись 6172
	bool bSkillKnownWas = false;                                    // навык рыбалки был известен до прогона (6172 его меняет)
	int32 SkillWas = 0;                                             // навык рыбалки до прогона
	TWeakObjectPtr<UUserWidget> BagShifted;                         // окно сумки, сдвинутое поверх ячейки (ВАЖН-2), — вернуть
	int32 CtlNumPadOk = -1;                                         // контроль ВАЖН-1: ОК обычного нумпада (−1 — не было, 0/1)
	int32 CtlNumPadQty = -1;                                        // контроль ВАЖН-1: количество обычного нумпада
	FString CtlNumPadText;                                          // W3fix2 МЕЛК-1: поле нумпада после MAX (строгий режим, максимум < 0)
	int32 SelBefore = -1;                                           // выбор ячейки до клика (ВАЖН-2 / МЕЛК-5)
	int32 M4CtlState = -1;                                          // W3fix2 МЕЛК-5: состояние автомата в начале контрольного интервала M4k
	int32 M4CancelWas = 0;                                          // ★ R5-fix8-c (14.11.2026): счётчик переходов «Отмена 79 → 100» перед щелчком M4
	int32 M4ClickState = -1;                                        // ★ R5-fix8-c (14.11.2026): состояние автомата в момент щелчка «Отмена» M4
	// N пустых шагов (по 0,4 с) — ожидание, например, конца окна двойного щелчка.
	void Wait(int32 N) { for (int32 i = 0; i < N; ++i) { Steps.Add([](UWorld*) {}); } }
	// Масштаб окна книги: пикселей экрана на 1 px разметки. ★ fishfix 09.10 (МЕЛК-4): ширина разметки берётся из СЛОТОВ рамки
	//   (план 0 — левый край, план 8 — правый), а не константой 810: окно расширено шагом `asda_ui_widen_windows` (810 → 1010).
	static float LayoutWidth(UAsdaFishBookSubsystem* B)
	{
		const UCanvasPanelSlot* L = Node(B, TEXT("FISH_BOOK_WIN_0_0")) ? Cast<UCanvasPanelSlot>(Node(B, TEXT("FISH_BOOK_WIN_0_0"))->Slot) : nullptr;
		const UCanvasPanelSlot* R = Node(B, TEXT("FISH_BOOK_WIN_0_8")) ? Cast<UCanvasPanelSlot>(Node(B, TEXT("FISH_BOOK_WIN_0_8"))->Slot) : nullptr;
		if (!L || !R) { return 810.f; }
		const float W = (R->GetOffsets().Left + R->GetOffsets().Right) - L->GetOffsets().Left;
		return W > 1.f ? W : 810.f;
	}
	static float Scale(UAsdaFishBookSubsystem* B)
	{
		FBox2D A(ForceInit), Z(ForceInit);
		return (RectOfW(Node(B, TEXT("FISH_BOOK_WIN_0_0")), A) && RectOfW(Node(B, TEXT("FISH_BOOK_WIN_0_8")), Z)) ? (Z.Max.X - A.Min.X) / LayoutWidth(B) : 1.f;
	}
	// Клик настоящими событиями Slate в точку экрана.
	static void ClickAt(UAsdaWindowSubsystem* WM, const FVector2D& P) { if (WM) { WM->SimulateClickAt(P, /*HoldFrames*/ 3); } }
	// Узел окна нумпада 1410 по имени.
	static UWidget* NumPadNode(UAsdaWindowSubsystem* WM, const TCHAR* Name)
	{
		UUserWidget* NW = WM ? WM->FindWindow(FName(TEXT("NUMPAD_WIN"))) : nullptr;
		return NW ? NW->GetWidgetFromName(FName(Name)) : nullptr;
	}
	// Текст поля нумпада 1412.
	static FString NumPadText(UAsdaWindowSubsystem* WM)
	{
		const UEditableText* E = Cast<UEditableText>(NumPadNode(WM, TEXT("NUMPAD_NUMBOX")));
		return E ? E->GetText().ToString() : FString(TEXT("<нет>"));
	}
	// Кнопка цифры нумпада: имена СМЕЩЕНЫ (`NUMPAD_NO09` = «0», `NUMPAD_NO00` = «1» … `NUMPAD_NO08` = «9»).
	static const TCHAR* DigitBtn(int32 D) { static const TCHAR* K[10] = { TEXT("NUMPAD_NO09"), TEXT("NUMPAD_NO00"), TEXT("NUMPAD_NO01"), TEXT("NUMPAD_NO02"),
		TEXT("NUMPAD_NO03"), TEXT("NUMPAD_NO04"), TEXT("NUMPAD_NO05"), TEXT("NUMPAD_NO06"), TEXT("NUMPAD_NO07"), TEXT("NUMPAD_NO08") }; return K[FMath::Clamp(D, 0, 9)]; }
	// Узел окна-сообщения TWOBUT (OK `TWOBUT_BUT00` / Отмена `TWOBUT_BUT01`).
	static UWidget* MsgNode(UAsdaWindowSubsystem* WM, const TCHAR* Name)
	{
		UUserWidget* W = WM ? WM->FindWindow(FName(TEXT("TWOBUT_MSGBOX"))) : nullptr;
		return W ? W->GetWidgetFromName(FName(Name)) : nullptr;
	}
	// Кадр экрана вместе со слоем Slate (только при `shot`): Saved*/Screenshots/<имя>.png.
	void Shot(const TCHAR* Name) const
	{
		if (bShot) { FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("asda_fishbook_%s_%s"), Name, *Res), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false); }
	}

	// Одна проверка.
	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaFishBookTest, Log, TEXT("САМОТЕСТ-КНИГА %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	// Сколько перехвачено пакетов опкода; последний — в Out.
	int32 Count(uint16 Op, FCap* Out = nullptr) const
	{
		int32 N = 0;
		for (const FCap& C : *Caps) { if (C.Op == Op) { ++N; if (Out) { *Out = C; } } }
		return N;
	}
	// Подсистемы мира.
	struct FSubs
	{
		UGameInstance* GI = nullptr; UAsdaNetSubsystem* Net = nullptr; UAsdaFishBookSubsystem* Book = nullptr;
		UAsdaWindowSubsystem* WM = nullptr; UAsdaInventorySubsystem* Inv = nullptr; UAsdaItemTableSubsystem* IT = nullptr;
		UAsdaItemTooltipSubsystem* Tip = nullptr; UAsdaFishingSubsystem* Fish = nullptr; UAsdaExtraMenuSubsystem* Etc = nullptr;
		bool Ok() const { return GI && Net && Book && WM && Inv && IT && Tip && Fish && Etc; }
	};
	static FSubs Get(UWorld* W)
	{
		FSubs S;
		S.GI = W ? W->GetGameInstance() : nullptr;
		if (!S.GI) { return S; }
		S.Net = S.GI->GetSubsystem<UAsdaNetSubsystem>(); S.Book = S.GI->GetSubsystem<UAsdaFishBookSubsystem>();
		S.WM = S.GI->GetSubsystem<UAsdaWindowSubsystem>(); S.Inv = S.GI->GetSubsystem<UAsdaInventorySubsystem>();
		S.IT = S.GI->GetSubsystem<UAsdaItemTableSubsystem>(); S.Tip = S.GI->GetSubsystem<UAsdaItemTooltipSubsystem>();
		S.Fish = S.GI->GetSubsystem<UAsdaFishingSubsystem>(); S.Etc = S.GI->GetSubsystem<UAsdaExtraMenuSubsystem>();
		return S;
	}
	// Живой прямоугольник виджета (`GetCachedGeometry`). false — нет узла/геометрии.
	static bool RectOfW(const UWidget* W, FBox2D& Out)
	{
		FVector2D Mn, Mx;
		if (!W || !AsdaUi::WidgetAbsRect(W, Mn, Mx) || (Mx - Mn).IsNearlyZero()) { return false; }
		Out = FBox2D(Mn, Mx);
		return true;
	}
	// Клик по виджету настоящими событиями Slate (центр живого прямоугольника).
	bool ClickW(UWorld* W, const UWidget* Wd, const TCHAR* What)
	{
		const FSubs S = Get(W);
		FBox2D R(ForceInit);
		if (!S.WM || !RectOfW(Wd, R)) { Expect(false, FString::Printf(TEXT("клик по %s"), What), TEXT("нет живой геометрии")); return false; }
		S.WM->SimulateClickAt(R.GetCenter(), /*HoldFrames*/ 3);
		return true;
	}
	// ★ fishfix 09.10 (U3/U4): клик настоящими событиями Slate в центр кнопки 73407 панели 73400, если она лежит под книгой
	//   (`bSysUnderBook` — так ли это; иначе проверка теряет смысл и проваливается сама).
	bool bSysUnderBook = false;
	bool bSysSkip = false;   // пара U3/U4 пропущена: при этом разрешении кнопка 73407 вне книги
	void ClickSysMenuUnderBook(UWorld* W, const TCHAR* Tag)
	{
		const FSubs S = Get(W);
		const UAsdaSystemMenuSubsystem* SM = S.GI ? S.GI->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr;
		const FAsdaSysMenuButton* Btn = SM ? SM->FindButton(73407) : nullptr;
		FBox2D R(ForceInit), Win(ForceInit), A(ForceInit), Z(ForceInit);
		const bool bWin = S.Book && RectOfW(Node(S.Book, TEXT("FISH_BOOK_WIN_0_0")), A) && RectOfW(Node(S.Book, TEXT("FISH_BOOK_WIN_0_8")), Z);
		if (bWin) { Win = FBox2D(A.Min, Z.Max); }
		bSysUnderBook = Btn && Btn->Box && RectOfW(Btn->Box.Get(), R) && bWin && Win.IsInside(R.GetCenter());
		if (!bSysUnderBook || !S.WM) { UE_LOG(LogAsdaFishBookTest, Warning, TEXT("САМОТЕСТ-КНИГА %s: кнопка 73407 не под книгой — клик не делается"), Tag); return; }
		S.WM->SimulateClickAt(R.GetCenter(), /*HoldFrames*/ 3);
	}
	// ★ fishfix2 09.10 (U5/U6, ревизия МЕЛК-2): наведение настоящим движением Slate (`ProcessMouseMoveEvent`) в центр кнопки 73407
	//   панели 73400 под книгой — его получает тот, кого выберет хит-тест Slate (фон окна мышь не ловит). Офлайн-окно неактивно,
	//   и синтетическое движение Slate в конце каждого кадра (`SynthesizeMouseMove` по курсору ОС) наведение снимает, поэтому
	//   движение повторяется каждый кадр `Frames` кадров (тикер ядра идёт ДО тика мира, где `TickHover` панели читает наведение).
	//   Перед каждым повтором снимается, чью подсказку панель показала в прошлом кадре (`HoverTipSeen`), после — дошло ли
	//   наведение до чекбокса (`bHoverReached`). `bAway` — увести курсор в угол экрана (уборка, один раз).
	int32 HoverTipSeen = -1;        // подсказка панели, замеченная за серию наведения (−1 — не было)
	bool bHoverReached = false;     // наведение Slate дошло до чекбокса 73407 хотя бы в одном кадре серии
	FString HoverDiag;              // что видит хит-тест Slate в точке наведения (для журнала U5)
	void HoverSysMenuUnderBook(UWorld* W, bool bAway, int32 Frames = 8)
	{
		const FSubs S = Get(W);
		const UAsdaSystemMenuSubsystem* SM = S.GI ? S.GI->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr;
		const FAsdaSysMenuButton* Btn = SM ? SM->FindButton(73407) : nullptr;
		FBox2D R(ForceInit);
		FVector2D P(2.f, 2.f);
		if (!bAway)
		{
			// Лежит ли кнопка под книгой (тот же расчёт, что у клика U3): иначе пара U5/U6 пропускается (не 1024×768) или падает.
			FBox2D Win(ForceInit), A(ForceInit), Z(ForceInit);
			const bool bWin = S.Book && RectOfW(Node(S.Book, TEXT("FISH_BOOK_WIN_0_0")), A) && RectOfW(Node(S.Book, TEXT("FISH_BOOK_WIN_0_8")), Z);
			if (bWin) { Win = FBox2D(A.Min, Z.Max); }
			bSysUnderBook = Btn && Btn->Box && RectOfW(Btn->Box.Get(), R) && bWin && Win.IsInside(R.GetCenter());
			bSysSkip = !bSysUnderBook && Res != TEXT("1024x768");
			if (!bSysUnderBook) { return; }
			P = R.GetCenter();
		}
		auto Move = [P]()
		{
			FSlateApplication& App = FSlateApplication::Get();
			App.SetCursorPos(P);
			const FPointerEvent Mv(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
			App.ProcessMouseMoveEvent(Mv);
		};
		HoverTipSeen = -1; bHoverReached = false; HoverDiag.Reset();
		Move();
		if (bAway) { return; }
		TWeakObjectPtr<UWorld> WW(W);
		TSharedRef<int32> Left = MakeShared<int32>(Frames);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, WW, Move, Left](float) -> bool
		{
			UWorld* W2 = WW.Get();
			const FSubs S2 = Get(W2);
			const UAsdaSystemMenuSubsystem* SM2 = S2.GI ? S2.GI->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr;
			const FAsdaSysMenuButton* B2 = SM2 ? SM2->FindButton(73407) : nullptr;
			if (!SM2 || !B2 || !B2->Box) { return false; }
			if (SM2->GetTipShownFor() >= 0) { HoverTipSeen = SM2->GetTipShownFor(); }
			if (--(*Left) < 0) { return false; }
			Move();
			bHoverReached |= B2->Box->IsHovered();
			if (HoverDiag.IsEmpty())
			{
				// Что видит хит-тест Slate в этой точке: длина пути, последний виджет, есть ли в пути чекбокс, захват мыши.
				FSlateApplication& App = FSlateApplication::Get();
				const FVector2D Cur = App.GetCursorPos();
				const FWidgetPath Path = App.LocateWindowUnderMouse(Cur, App.GetInteractiveTopLevelWindows(), false, 0);
				const TSharedPtr<SWidget> Cb = B2->Box->GetCachedWidget();
				FString Types;
				for (int32 k = 0; k < Path.Widgets.Num(); ++k) { Types += Path.Widgets[k].Widget->GetTypeAsString() + TEXT(">"); }
				HoverDiag = FString::Printf(TEXT("курсор (%.0f,%.0f) [%s] путь %d, последний %s, чекбокс в пути %d, захват мыши %d"), Cur.X, Cur.Y, *Types, Path.Widgets.Num(),
					Path.Widgets.Num() ? *Path.Widgets.Last().Widget->GetTypeAsString() : TEXT("-"),
					(Cb.IsValid() && Path.ContainsWidget(Cb.Get())) ? 1 : 0, App.HasAnyMouseCaptor() ? 1 : 0);
			}
			return true;
		}), 0.f);
	}
	// Двойной щелчок по ячейке книги настоящими кликами: второй симклик заказывается, только когда первый ДОСТАВЛЕН
	//   (`IsSimClickPending` — иначе два удержания сливаются в одно нажатие), и не позже `GetDoubleClickTime` − 0,1 с.
	//   `SecondAfter` > 0 (W3fix2 МЕЛК-3) — второй заказывается не раньше, чем через столько секунд после НАЖАТИЯ первого
	//   (★ R5-fix6-c (13.11.2026): по часам самой книги `LastClickTime`; прежде — после ДОСТАВКИ, и при кадре ≥ 40 мс интервал нажатий
	//   выходил за `GetDoubleClickTime`: Δt 0,555–0,574 с при окне 0,50 с — E0c красная на EN и в baseline3).
	void DoubleClickCell(UWorld* W, int32 Cell, double SecondAfter = 0.0)
	{
		const FSubs S = Get(W);
		if (!S.Book || !S.Book->CellUi.IsValidIndex(Cell)) { return; }
		// Удержание 1 кадр (не 3): при медленных кадрах офлайн-прогона два клика по 3 кадра не укладываются в окно 0xD.
		FBox2D R(ForceInit);
		if (!S.WM || !RectOfW(S.Book->CellUi[Cell].Root, R)) { Expect(false, TEXT("двойной щелчок по ячейке"), TEXT("нет живой геометрии")); return; }
		const FVector2D P = R.GetCenter();
		S.WM->SimulateClickAt(P, /*HoldFrames*/ 1);
		TWeakObjectPtr<UWorld> WW(W);
		const double T0 = FPlatformTime::Seconds();
		TSharedRef<double> TDelivered = MakeShared<double>(-1.0);   // момент, когда первый клик доставлен (−1 — ещё нет)
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, WW, P, T0, SecondAfter, TDelivered, Cell](float) -> bool
		{
			UWorld* W2 = WW.Get();
			const FSubs S2 = Get(W2);
			if (!W2 || !S2.WM) { return false; }
			if (S2.WM->IsSimClickPending()) { return true; }   // первый ещё не доставлен — второй заказ затёр бы его
			if (*TDelivered < 0.0) { *TDelivered = FPlatformTime::Seconds(); }
			// Медленный двойной (E0c): отсчёт от нажатия первого щелчка, как его записала книга (нет записи — от доставки).
			const double TPress1 = (S2.Book && S2.Book->LastClickCell == Cell && S2.Book->LastClickTime >= T0) ? S2.Book->LastClickTime : *TDelivered;
			// Книга видит нажатие позже заказа (кадр до нажатия + кадр до опроса мыши): упреждаем на задержку, измеренную у
			//   первого щелчка (заказ `T0` → запись книги) — иначе при 18 кадрах/с Δt выходил 0,501 с. Рывок кадра на первом
			//   щелчке упреждение не раздувает: не больше двух текущих кадров и не больше 0,2 с.
			const double Lat1 = (TPress1 == *TDelivered) ? 0.0 : FMath::Clamp(TPress1 - T0, 0.0, FMath::Min(0.2, 2.0 * (double)W2->GetDeltaSeconds()));
			if ((FPlatformTime::Seconds() - TPress1) + Lat1 < SecondAfter) { return true; }
			S2.WM->SimulateClickAt(P, /*HoldFrames*/ 1);
			UE_LOG(LogAsdaFishBookTest, Log, TEXT("САМОТЕСТ-КНИГА: второй щелчок заказан через %.2f с после первого"), FPlatformTime::Seconds() - T0);
			return false;
		}), 0.f);
	}
	// Узел окна книги по имени.
	static UWidget* Node(UAsdaFishBookSubsystem* B, const TCHAR* Name)
	{
		return (B && B->DebugWidget()) ? B->DebugWidget()->GetWidgetFromName(FName(Name)) : nullptr;
	}
	// Текст TextBlock (или «<нет>»).
	static FString TextOf(const UTextBlock* T) { return T ? T->GetText().ToString() : FString(TEXT("<нет>")); }
	// Текст верхней записи очереди окон без тегов.
	static FString TopText(UAsdaWindowSubsystem* WM) { return WM ? FAsdaMarkup::StripTags(WM->GetMsgBoxQueueTopText()) : FString(); }
	// Ожидаемая loc-строка без тегов (с подстановкой).
	static FString Want(UGameInstance* GI, int32 Id, const TArray<FString>& Args = TArray<FString>())
	{
		return FAsdaMarkup::StripTags(AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(GI, Id, true), Args));
	}
	// ★ 09.10.2026: содержимое N-й пары «[…]» в видимом тексте (0 — первая); нет такой пары — «<нет>».
	//   Для 111018: [0] = название регистрируемого предмета (первый %s), [1] = своё имя (второй %s).
	static FString BracketArg(const FString& Text, int32 N)
	{
		int32 From = 0;   // позиция поиска очередной «[»
		for (int32 i = 0; ; ++i)
		{
			const int32 L = Text.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);   // начало пары
			const int32 R = (L >= 0) ? Text.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, L + 1) : INDEX_NONE;   // конец пары
			if (L < 0 || R < 0) { return FString(TEXT("<нет>")); }
			if (i == N) { return Text.Mid(L + 1, R - L - 1); }
			From = R + 1;
		}
	}
	// Ввести число в поле нумпада 1412.
	static bool NumPadType(UAsdaWindowSubsystem* WM, int32 Qty)
	{
		UUserWidget* NW = WM ? WM->FindWindow(FName(TEXT("NUMPAD_WIN"))) : nullptr;
		UEditableText* E = NW ? Cast<UEditableText>(NW->GetWidgetFromName(TEXT("NUMPAD_NUMBOX"))) : nullptr;
		if (!E) { return false; }
		E->SetText(FText::FromString(FString::FromInt(Qty)));
		return true;
	}
	// Подать пакет штатным разбором и разобрать очередь книги.
	void Feed(const FSubs& S, int32 Op, const TArray<uint8>& Body) { S.Net->DebugFeedPacket(Op, Body); S.Book->PumpPending(); }
	// Кол-во в ячейке контейнера 2 (−1 — пусто).
	static int32 BagAmount(UAsdaInventorySubsystem* Inv, int32 Cell)
	{
		const FAsdaItemInstance* It = Inv ? Inv->GetCell(2, Cell) : nullptr;
		return (It && !It->IsEmpty()) ? It->Amount : -1;
	}
	// Выравнивание TextBlock (свойство `Justification` базового `UTextLayoutWidget` — геттера у класса нет); −1 — нет узла.
	static int32 JustOf(const UTextBlock* T)
	{
		const FProperty* P = T ? UTextLayoutWidget::StaticClass()->FindPropertyByName(TEXT("Justification")) : nullptr;
		return P ? (int32)*P->ContainerPtrToValuePtr<uint8>(T) : -1;
	}
	// Сколько томов зарегистрировано (чтение поля подсистемы).
	static int32 RegCount(UAsdaFishBookSubsystem* B) { int32 N = 0; for (const auto& St0 : B->St) { N += St0.bReg ? 1 : 0; } return N; }
	// Счёт тома/ячейки (чтение поля подсистемы).
	static int32 Cnt(UAsdaFishBookSubsystem* B, int32 Tome, int32 J) { return B->St.IsValidIndex(Tome) ? B->St[Tome].Cnt[J] : -999; }

	// ═══ Подготовка ═══
	void Setup(UWorld* W)
	{
		using namespace AsdaFishBookTestLocal;
		FSubs S = Get(W);
		if (!S.Ok()) { Expect(false, TEXT("нет подсистем")); return; }
		if (UGameViewportClient* VC = W->GetGameViewport()) { FVector2D V; VC->GetViewportSize(V); Res = FString::Printf(TEXT("%.0fx%.0f"), V.X, V.Y); }
		UE_LOG(LogAsdaFishBookTest, Log, TEXT("САМОТЕСТ-КНИГА: вьюпорт %s"), *Res);
		CharIdWas = S.Net->SelectedCharId; WeightWas = S.Net->SelfWeightCur;
		S.Net->SelectedCharId = kCharId;
		SetCVar(TEXT("asda.FishBook.ClientTable"), 0); SetCVar(TEXT("asda.FishBook.LegacyCursor"), 0);
		SetCVar(TEXT("asda.FishBook.LegacyReward5"), 0); SetCVar(TEXT("asda.FishBook.LiveRefresh"), 1);
		BagWas.Reset();
		for (int32 i = 0; i < 60; ++i) { const FAsdaItemInstance* It = S.Inv->GetCell(2, i); BagWas.Add(It ? It->Raw : TArray<uint8>()); }
		if (const FAsdaItemInstance* It = S.Inv->GetCell(3, 10)) { Eq10Was = It->Raw; }
		bSkillKnownWas = S.Fish->IsFishingSkillKnown(); SkillWas = S.Fish->GetFishingSkill();
		for (int32 i = 0; i < 60; ++i) { S.Inv->DebugPutItem(2, i, 0, 0); }
		S.Inv->DebugPutItem(2, kCellBook, kBook1, 1);
		S.Inv->DebugPutItem(2, kCellA1, kFishA, 10);
		S.Inv->DebugPutItem(2, kCellA2, kFishA, 4);
		S.Inv->DebugPutItem(2, kCellB1, kFishB, 3);
		S.Inv->DebugPutItem(2, kCellB2, kFishB, 1);
		S.Inv->DebugPutItem(2, kCellOther, kNotFish, 3);
		if (S.Inv->IsWindowOpen() == false) { S.Inv->OpenWindow(); }   // cmd 9 обязан закрыть сумку
		S.Book->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		S.WM->ClearMsgBoxQueue(TEXT("самотест книги: старт"));
		Caps->Reset();
		TSharedRef<TArray<FCap>> C = Caps;
		Gen = AsdaSelfTestKit::ArmCapture(S.Net, [C](uint16 Op, const uint8* P, int32 Len, bool, TCHAR Env)
		{
			FCap X; X.Op = Op; X.Env = Env; if (P && Len > 0) { X.P.Append(P, Len); } C->Add(MoveTemp(X));
		}, 0, TEXT("asda.FishBook.SelfTest"));

		// A. Данные (Р12).
		UAsdaFishBookSubsystem* B = S.Book;
		B->EnsureData();
		Expect(B->Rows.Num() == 9 && B->Tpl.Num() == 9 && B->St.Num() == 9, TEXT("A1 DT_FishBook: 9 томов (потеряно 0)"),
			FString::Printf(TEXT("строк %d"), B->Rows.Num()));
		Expect(B->Tpl.Num() > 5 && B->Tpl[1].Reward[0] == 109 && B->Tpl[1].Reward[2] == 137 && B->Tpl[1].Reward[3] == 69678
			&& B->Tpl[5].BookItemId == 69682 && B->Tpl[8].BookItemId == 69685 && B->Tpl[5].Fish[0] == 68067 && B->Tpl[1].BookItemId == kBook1,
			TEXT("A2 Р12 сервер: награды тома 1 = 109/125/137/69678, книга тома 5 = 69682 и тома 8 = 69685 (fishbooks_69682: сервер приведён к клиенту), рыбы 680xx"));
		SetCVar(TEXT("asda.FishBook.ClientTable"), 1); B->EnsureData();
		Expect(B->Tpl.Num() > 5 && B->Tpl[1].Reward[0] == 128 && B->Tpl[5].BookItemId == 69682 && B->Tpl[5].Fish[0] == 69579,
			TEXT("A3 контроль Р12: ClientTable 1 → клиентская таблица (награда 128, книга 69682, рыба 69579)"));
		SetCVar(TEXT("asda.FishBook.ClientTable"), 0); B->EnsureData();
		Expect(B->Tpl[1].Reward[0] == 109, TEXT("A4 ClientTable 0 → снова серверная"));

		// B. 6174 / 6175.
		Feed(S, 6174, Page305(0, 0, { 2 }, { 30 }, { 12 }));
		Feed(S, 6174, Page305(1, 0, { 5, 11, 0, 0 }, { 40, 50, 0, 0 }, { 10, 20, 0, 0 }));
		Feed(S, 6174, Page305(2, 0, {}, {}, {}));
		Feed(S, 6174, Page305(9, 0, { 1 }, {}, {}));
		Feed(S, 6174, Page305(-1, 0, { 1 }, {}, {}));
		Feed(S, 6175, TArray<uint8>());
		int32 RegN = 0; for (const auto& St0 : B->St) { RegN += St0.bReg ? 1 : 0; }
		Expect(B->St[0].bReg && B->St[1].bReg && B->St[2].bReg && RegN == 3 && B->St[1].Id == 1 && Cnt(B, 1, 1) == 11
			&& B->St[1].MaxSz[1] == 50 && B->St[1].MinSz[1] == 20 && Cnt(B, 0, 0) == 2,
			TEXT("B1 6174 ×3 → тома 0/1/2: счёт/макс./мин. записаны"), FString::Printf(TEXT("зарегистрировано %d"), RegN));
		// ★ МЕЛК-8 ревизии В3: B2 больше не повторяет B1 — граница `<` проверяется ПАРОЙ: последний индекс (число томов − 1)
		//   принимается, индекс = числу томов и −1 — нет (выше они уже поданы, тома 8 при этом не было).
		const bool bNo8Before = B->St.IsValidIndex(8) && !B->St[8].bReg;
		Feed(S, 6174, Page305(B->St.Num() - 1, 0, { 4 }, {}, {}));
		const bool bLastOk = B->St.IsValidIndex(8) && B->St[8].bReg && Cnt(B, 8, 0) == 4;
		if (B->St.IsValidIndex(8)) { B->St[8].bReg = false; B->St[8].Id = -1; B->St[8].Cnt[0] = 0; }   // вернуть том 8 (дальше его нет)
		Expect(B->St.Num() == 9 && bNo8Before && bLastOk, TEXT("B2 Р6 пара: 6174 с ПОСЛЕДНИМ индексом 8 принят, с индексом 9 (= числу томов) и −1 — отвергнуты (эталон `<=` писал бы за массив)"),
			FString::Printf(TEXT("томов %d, том 8 до %d, после 6174(8) %d"), B->St.Num(), bNo8Before ? 0 : 1, bLastOk ? 1 : 0));
		Expect(!B->IsOpen(), TEXT("B3 6174/6175 окно не открывают"));

		// C. Доп-меню cmd 9.
		S.Etc->DispatchCommand(9, TEXT("самотест книги"));
		Expect(B->IsOpen() && !S.Inv->IsWindowOpen(), TEXT("C1 доп-меню cmd 9 → книга открыта, сумка закрыта (`sowelWindowClose`)"),
			FString::Printf(TEXT("книга=%d сумка=%d"), B->IsOpen() ? 1 : 0, S.Inv->IsWindowOpen() ? 1 : 0));
		S.Etc->DispatchCommand(9, TEXT("самотест книги"));
		Expect(B->IsOpen(), TEXT("C2 повтор cmd 9 — не тоггл (окно открыто)"));
	}

	void BuildSteps()
	{
		using namespace AsdaFishBookTestLocal;
		// C3/D. Вкладки и сетка тома 0 → клик по вкладке 2.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			UE_LOG(LogAsdaFishBookTest, Log, TEXT("САМОТЕСТ-КНИГА структура окна:\n%s\n%s"), *B->DumpControls(), *B->DumpState());
			const bool bTabs = B->TabCount == 3 && AsdaUi::IsShown(B->TabTexts[0]) && AsdaUi::IsShown(B->TabTexts[2]) && !AsdaUi::IsShown(B->TabTexts[3])
				&& TextOf(B->TabTexts[0]) == UAsdaFishBookSubsystem::CaptionCut(B->St[0].Name, B->TabTexts[0], UAsdaFishBookSubsystem::kTabCaptionPad) && TextOf(B->TabTexts[1]) == UAsdaFishBookSubsystem::CaptionCut(B->St[1].Name, B->TabTexts[1], UAsdaFishBookSubsystem::kTabCaptionPad);
			Expect(bTabs, TEXT("C3 вкладки: 3 видимы, 4-я скрыта, подписи = имена томов (целиком или по ширине поля + «...»; `asda.FishBook.Trunc13 1` — ≤13 + «...»)"),
				FString::Printf(TEXT("вкладок %d: «%s» «%s» «%s»"), B->TabCount, *TextOf(B->TabTexts[0]), *TextOf(B->TabTexts[1]), *TextOf(B->TabTexts[2])));
			Expect(B->Page == 0 && AsdaUi::IsShown(B->CellUi[0].Root) && !AsdaUi::IsShown(B->CellUi[1].Root)
				&& TextOf(B->CellUi[0].Name) == S.IT->GetItemName(kFishA) && TextOf(B->CellUi[0].Cnt) == TEXT("2/5") && TextOf(B->CellUi[0].No) == TEXT("001"),
				TEXT("C4 том 0: одна ячейка — имя Карася, «001», «2/5»"),
				FString::Printf(TEXT("«%s» «%s» «%s»"), *TextOf(B->CellUi[0].Name), *TextOf(B->CellUi[0].No), *TextOf(B->CellUi[0].Cnt)));
			// Итоги по ВСЕМ зарегистрированным: (2/5 + 5/17 + 11/13 + 0 + 0 + 8×0) / 13 × 100 = 11.8 %; видов поймано 3.
			Expect(TextOf(B->TopText[1]) == TEXT("11.8%") && TextOf(B->TopText[3]) == TEXT("3"),
				TEXT("D1 итоги 74102 «11.8%» / 74104 «3»"), FString::Printf(TEXT("«%s» / «%s»"), *TextOf(B->TopText[1]), *TextOf(B->TopText[3])));
			ClickW(W, B->TabButtons[1], TEXT("вкладка 2 (OPT1)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->Page == 1 && B->TabSel == 1, TEXT("D2 клик по вкладке 2 → том 1"), FString::Printf(TEXT("том %d вкладка %d"), B->Page, B->TabSel));
			bool bGrid = true;
			for (int32 i = 0; i < 30; ++i) { bGrid &= (AsdaUi::IsShown(B->CellUi[i].Root) == (i < 4)); }
			Expect(bGrid && TextOf(B->CellUi[0].Cnt) == TEXT("5/17") && TextOf(B->CellUi[2].Name) == TEXT("?????") && TextOf(B->CellUi[2].No) == TEXT("003")
				&& TextOf(B->CellUi[2].Cnt).IsEmpty(), TEXT("D3 сетка тома 1: 4 ячейки; непойманная — «?????», «003», счёт пуст"),
				FString::Printf(TEXT("[0] «%s» [2] «%s» «%s» «%s»"), *TextOf(B->CellUi[0].Cnt), *TextOf(B->CellUi[2].Name), *TextOf(B->CellUi[2].No), *TextOf(B->CellUi[2].Cnt)));
			Expect(!TextOf(B->RewardUi[0].Name).IsEmpty() && !TextOf(B->RewardUi[3].Name).IsEmpty() && !AsdaUi::IsShown(B->RewardUi[2].Frame),
				TEXT("D4 награды тома 1 подписаны, рамки скрыты"),
				FString::Printf(TEXT("«%s» «%s» «%s» «%s»"), *TextOf(B->RewardUi[0].Name), *TextOf(B->RewardUi[1].Name), *TextOf(B->RewardUi[2].Name), *TextOf(B->RewardUi[3].Name)));
			// ★ ВАЖН-6 ревизии В3 — контрольная пара: второй щелчок по ячейке 1 ПОСЛЕ системного интервала двойного щелчка
			//   (`GetDoubleClickTime`) + 0,25 с — это снова одиночный 0xB, окно 74200 не открывается.
			ClickW(W, B->CellUi[0].Root, TEXT("ячейка 1"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, WW](float) -> bool
			{
				if (UWorld* W2 = WW.Get()) { const FSubs S2 = Get(W2); if (S2.Book) { ClickW(W2, S2.Book->CellUi[0].Root, TEXT("ячейка 1 (второй щелчок после интервала)")); } }
				return false;
			}), (float)UAsdaFishBookSubsystem::DoubleClickSec() + 0.25f);
		});
		const int32 DblSteps = FMath::CeilToInt32(((float)UAsdaFishBookSubsystem::DoubleClickSec() + 0.6f) / 0.4f);
		Wait(DblSteps);
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->Sel == 0 && !B->IsJoinOpen(), FString::Printf(TEXT("E0 ВАЖН-6 пара: второй щелчок ПОСЛЕ системного интервала (%.2f с, `GetDoubleClickTime`) — одиночный, 74200 не открыт"),
				UAsdaFishBookSubsystem::DoubleClickSec()), FString::Printf(TEXT("выбор %d окно %d"), B->Sel, B->IsJoinOpen() ? 1 : 0));
			// ★ МЕЛК-5 (проверено геометрией): группа клона ячейки, которую строит импортёр, — это рамка 74029 (−1,−1 / 102×92),
			//   а рамка 74020 (100×90) лежит в ней с (+1,+1). Хит-зона опроса — объединение 8 планов 74029; сверяем её с
			//   эталонными размерами разметки и с группой, рамку 74020 — с 100×90 от (+1,+1).
			const float Sc = Scale(B);
			auto Union = [](const TArray<TObjectPtr<UWidget>>& Ws, FBox2D& Out) -> int32
			{
				int32 N = 0; Out = FBox2D(ForceInit);
				for (const TObjectPtr<UWidget>& X : Ws) { FBox2D R(ForceInit); if (RectOfW(X, R)) { Out += R; ++N; } }
				return N;
			};
			FBox2D Hit(ForceInit), Grp(ForceInit), Frm(ForceInit);
			TArray<TObjectPtr<UWidget>> Frames;
			for (int32 k = 0; k < 8; ++k) { Frames.Add(Node(B, *FString::Printf(TEXT("FISH_BOOK_CELL01_FRAME_%d"), k))); }
			const int32 NHit = Union(B->CellUi[1].Click, Hit);
			const int32 NFrm = Union(Frames, Frm);
			const bool bGrp = RectOfW(B->CellUi[1].Root, Grp);
			const bool bHit = NHit == 8 && bGrp && FMath::Abs(Hit.GetSize().X / Sc - 102.f) < 0.6f && FMath::Abs(Hit.GetSize().Y / Sc - 92.f) < 0.6f
				&& (Hit.Min - Grp.Min).GetAbsMax() < 0.6f && (Hit.Max - Grp.Max).GetAbsMax() < 0.6f;
			const bool bFrm = NFrm == 8 && FMath::Abs(Frm.GetSize().X / Sc - 100.f) < 0.6f && FMath::Abs(Frm.GetSize().Y / Sc - 90.f) < 0.6f
				&& FMath::Abs((Frm.Min.X - Hit.Min.X) / Sc - 1.f) < 0.6f && FMath::Abs((Frm.Min.Y - Hit.Min.Y) / Sc - 1.f) < 0.6f;
			Expect(bHit && bFrm, TEXT("E0a МЕЛК-5: хит-зона ячейки = 8 планов клона 74029 = 102×92 от (−1,−1) (= группа импортёра), рамка 74020 = 100×90 от (+1,+1)"),
				FString::Printf(TEXT("74029 %d планов %.1f×%.1f, группа (%.0f,%.0f)-(%.0f,%.0f), 74020 %.1f×%.1f сдвиг (%.1f,%.1f), масштаб %.2f"),
					NHit, Hit.GetSize().X / Sc, Hit.GetSize().Y / Sc, Grp.Min.X, Grp.Min.Y, Grp.Max.X, Grp.Max.Y, Frm.GetSize().X / Sc, Frm.GetSize().Y / Sc,
					(Frm.Min.X - Hit.Min.X) / Sc, (Frm.Min.Y - Hit.Min.Y) / Sc, Sc));
			// Хит-тест опроса НАСТОЯЩИМ курсором Slate: последний пиксель хит-рамки ячейки 2 слева — попадает; пиксель зазора
			//   между хит-рамками ячеек 1 и 2 (2 px разметки) — мимо обеих.
			bool bEdge = false, bGap0 = true, bGap1 = true;
			FVector2D P1(0, 0), P2(0, 0);
			if (bHit)
			{
				FSlateApplication::Get().SetCursorPos(FVector2D(FMath::CeilToFloat(Hit.Min.X), Hit.GetCenter().Y));
				P1 = FSlateApplication::Get().GetCursorPos();
				bEdge = B->IsCursorOverCellHit(1);
				FSlateApplication::Get().SetCursorPos(FVector2D(FMath::RoundToFloat(Hit.Min.X - 1.f * Sc), Hit.GetCenter().Y));
				P2 = FSlateApplication::Get().GetCursorPos();
				bGap1 = B->IsCursorOverCellHit(1);
				bGap0 = B->IsCursorOverCellHit(0);
				FSlateApplication::Get().SetCursorPos(FVector2D(4.f, 4.f));
			}
			Expect(bEdge && !bGap0 && !bGap1, TEXT("E0b хит-тест опроса: край хит-рамки ячейки 2 — попадание; контроль — зазор между хит-рамками ячеек 1 и 2 — мимо обеих"),
				FString::Printf(TEXT("край (%.0f,%.0f)=%d; зазор (%.0f,%.0f): ячейка1=%d ячейка2=%d"), P1.X, P1.Y, bEdge ? 1 : 0, P2.X, P2.Y, bGap0 ? 1 : 0, bGap1 ? 1 : 0));
		});
		// ═══ E0c. ★ W3fix2 (ревизия `fishing_impl_W3fix_W6_REVIEW.md`, МЕЛК-3): РЕШАЮЩИЙ случай исходного дефекта — второе
		//   нажатие через 0,35 с < Δt ≤ `GetDoubleClickTime` (у оригинала 0xD, у старого порта с порогом 0,35 с — одиночный 0xB).
		//   Ячейка 1 тома 1 (Карась 5/17, поймана) → окно 74200; Δt проверяется по самой книге (`DebugLastClickDelta`).
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			const double Dcs = UAsdaFishBookSubsystem::DoubleClickSec();
			// ★ R5-fix6-c (13.11.2026): второе нажатие целим на 0,40 с после НАЖАТИЯ первого (с упреждением на задержку заказа), но не дальше
			//   середины окна (0,35; Dcs] — запас в обе стороны. Смысл и границы проверки ниже прежние.
			DoubleClickCell(W, 0, FMath::Min(0.40, (0.35 + Dcs) / 2.0));
		});
		Wait(FMath::CeilToInt32(((float)UAsdaFishBookSubsystem::DoubleClickSec() + 0.5f) / 0.4f));
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			const double Dcs = UAsdaFishBookSubsystem::DoubleClickSec();
			const double Dt = B->DebugLastClickDelta;
			if (Dcs <= 0.36)
			{
				Expect(true, TEXT("E0c W3fix2 МЕЛК-3: системный интервал ≤ 0,35 с — решающего случая на этой машине нет"), FString::Printf(TEXT("%.2f с"), Dcs));
			}
			else
			{
				Expect(Dt > 0.35 && Dt <= Dcs && B->IsJoinOpen() && B->JoinFish == kFishA,
					TEXT("E0c W3fix2 МЕЛК-3: второе нажатие через 0,35 с < Δt ≤ `GetDoubleClickTime` — ДВОЙНОЙ (0xD): окно 74200 рыбы ячейки 1 (старый порог 0,35 с дал бы одиночный)"),
					FString::Printf(TEXT("Δt %.3f с (окно %.2f с), 74200 %d рыба %d"), Dt, Dcs, B->IsJoinOpen() ? 1 : 0, B->JoinFish));
			}
			B->CloseJoin();
			B->Sel = 0; B->RefreshAll();
		});
		Wait(DblSteps);
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			DoubleClickCell(W, 1);   // второе НАЖАТИЕ — сразу после доставки первого клика (в пределах `GetDoubleClickTime`)
		});
		Wait(1);
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->Sel == 1 && AsdaUi::IsShown(B->CellUi[1].Red) && !AsdaUi::IsShown(B->CellUi[0].Red) && TextOf(B->InfoVal[3]) == TEXT("11/13")
				&& TextOf(B->InfoVal[1]) == TEXT("50cm") && TextOf(B->InfoVal[2]) == TEXT("20cm") && TextOf(B->CardNo) == TEXT("002")
				&& TextOf(B->CardName) == S.IT->GetItemName(kFishB),
				TEXT("E1 ЛКМ по ячейке → выбор, красная рамка, инфо «11/13», «50cm»/«20cm», карточка «002»"),
				FString::Printf(TEXT("выбор %d, «%s» «%s» «%s» «%s»"), B->Sel, *TextOf(B->InfoVal[3]), *TextOf(B->InfoVal[1]), *TextOf(B->InfoVal[2]), *TextOf(B->CardName)));
			Expect(B->IsJoinOpen() && B->JoinFish == kFishB && TextOf(B->JoinR1) == TEXT("11") && TextOf(B->JoinR3) == TEXT("13") && TextOf(B->JoinR2) == TEXT("0")
				&& B->JoinTitle.LastRaw.Contains(S.IT->GetItemName(kFishB)),
				TEXT("E2 двойной щелчок → окно 74200: «11» сдано, «13» требуется, «0» к сдаче, заголовок 111014 с именем рыбы"),
				FString::Printf(TEXT("окно=%d R1 «%s» R2 «%s» R3 «%s» заголовок «%s»"), B->IsJoinOpen() ? 1 : 0, *TextOf(B->JoinR1), *TextOf(B->JoinR2), *TextOf(B->JoinR3), *B->JoinTitle.LastRaw));
			Expect(B->InvCell[0] == kCellB1 && B->InvCell[1] == kCellB2 && B->InvCell[2] == -1 && TextOf(B->InvUi[0].Label) == TEXT("3") && TextOf(B->InvUi[1].Label) == TEXT("1")
				&& !AsdaUi::IsShown(B->JoinCheck), TEXT("E3 стопки: только рыба 69582 (14 ×3, 16 ×1), не-рыба и Карась не попали; курсор скрыт"),
				FString::Printf(TEXT("[%d %d %d] «%s» «%s»"), B->InvCell[0], B->InvCell[1], B->InvCell[2], *TextOf(B->InvUi[0].Label), *TextOf(B->InvUi[1].Label)));
			ClickW(W, B->InvUi[0].Icon, TEXT("стопка 74211"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.WM->IsNumPadOpen() && S.Book->DebugLastNumPadMax == 2, TEXT("F1 ЛКМ по стопке → нумпад режима 10, максимум min(3, 13−11) = 2"),
				FString::Printf(TEXT("нумпад=%d максимум=%d"), S.WM->IsNumPadOpen() ? 1 : 0, S.Book->DebugLastNumPadMax));
			// ★ МЕЛК-11: ввод — кнопкой цифры нумпада (путь игрока), а не записью в поле.
			ClickW(W, NumPadNode(S.WM, DigitBtn(2)), TEXT("цифра «2» нумпада"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(NumPadText(S.WM) == TEXT("2"), TEXT("F2 кнопка «2» нумпада → в поле «2»"), NumPadText(S.WM));
			ClickW(W, NumPadNode(S.WM, TEXT("NUMPAD_OK")), TEXT("NUMPAD_OK"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			FBox2D Cur(ForceInit), Ic(ForceInit);
			const bool bGeo = RectOfW(B->JoinCheck, Cur) && RectOfW(B->InvUi[0].Icon, Ic);
			Expect(!S.WM->IsNumPadOpen() && B->Qty == 2 && B->ChosenCell == kCellB1 && TextOf(B->JoinR2) == TEXT("2"),
				TEXT("F3 «ОК» нумпада → к сдаче 2 (74206), выбрана стопка 14; пакета нет"),
				FString::Printf(TEXT("кол-во %d стопка %d «%s»"), B->Qty, B->ChosenCell, *TextOf(B->JoinR2)));
			Expect(AsdaUi::IsShown(B->JoinCheck) && bGeo && (Cur.Min - Ic.Min).GetAbsMax() <= 1.5 && (Cur.Max - Ic.Max).GetAbsMax() <= 1.5 && Count(6180) == 0,
				TEXT("F4 Р7/Д1: курсор 74241 РОВНО на выбранной стопке (живые прямоугольники)"),
				FString::Printf(TEXT("курсор (%.0f,%.0f)-(%.0f,%.0f) стопка (%.0f,%.0f)-(%.0f,%.0f)"), Cur.Min.X, Cur.Min.Y, Cur.Max.X, Cur.Max.Y, Ic.Min.X, Ic.Min.Y, Ic.Max.X, Ic.Max.Y));
			SetCVar(TEXT("asda.FishBook.LegacyCursor"), 1);
			B->RefreshJoin(B->JoinFish);
			S.WM->ClearMsgBoxQueue(TEXT("книга: G0"));
			ClickW(W, B->BtnReward, TEXT("74090 под окном регистрации (модальность)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			FBox2D Cur(ForceInit), I1(ForceInit);
			const bool bGeo = RectOfW(B->JoinCheck, Cur) && RectOfW(B->InvUi[1].Icon, I1);
			Expect(AsdaUi::IsShown(B->JoinCheck) && bGeo && Cur.Min.X > I1.Min.X + 1.f && Cur.Min.Y > I1.Min.Y + 1.f,
				TEXT("F5 контроль LegacyCursor 1: курсор на ДРУГОЙ стопке (16) со сдвигом (+10,+7), как оригинал"),
				FString::Printf(TEXT("курсор (%.0f,%.0f) стопка16 (%.0f,%.0f)"), Cur.Min.X, Cur.Min.Y, I1.Min.X, I1.Min.Y));
			Expect(S.WM->GetMsgBoxQueueNum() == 0 && Count(6182) == 0, TEXT("G0 модальность 74200: кнопка 74090 под ним молчит"));
			SetCVar(TEXT("asda.FishBook.LegacyCursor"), 0);
			B->RefreshJoin(B->JoinFish);
			Caps->Reset();
			ClickW(W, B->BtnJoinOk, TEXT("74252 «ОК»"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			FCap C;
			const int32 N = Count(6180, &C);
			const bool bBytes = C.P.Num() == 19 && AsdaBytes::RdI32(C.P.GetData()) == kCharId && AsdaBytes::RdI32(C.P.GetData() + 4) == 1
				&& AsdaBytes::RdI16(C.P.GetData() + 8) == kCellB1 && AsdaBytes::RdI16(C.P.GetData() + 10) == -1 && C.P[12] == 2
				&& AsdaBytes::RdI32(C.P.GetData() + 13) == 2 && AsdaBytes::RdI16(C.P.GetData() + 17) == 0;
			FString Hex; for (uint8 X : C.P) { Hex += FString::Printf(TEXT("%02X "), X); }
			Expect(N == 1 && C.Env == TEXT('S') && bBytes,
				TEXT("G1 74252 → РОВНО ОДИН 6180, конверт 'S', 19 б: charId, ИНДЕКС тома 1, слот 14, второй −1, контейнер 2, кол-во 2 (введённое), вес 0"),
				FString::Printf(TEXT("×%d '%c' [%s]"), N, C.Env ? C.Env : TEXT('?'), *Hex));
			// H. 6181.
			Feed(S, 6181, Resp365(1, 77, Rec57(kFishB, 2, kCellB1, 1), Page305(1, 0, { 5, 13, 0, 0 }, { 40, 50, 0, 0 }, { 10, 20, 0, 0 })));
			Expect(Cnt(B, 1, 1) == 13 && !B->IsJoinOpen() && BagAmount(S.Inv, kCellB1) == 1 && S.Net->SelfWeightCur == 77
				&& TextOf(B->CellUi[1].Cnt) == TEXT("13/13") && AsdaUi::IsShown(B->CellUi[1].Done),
				TEXT("H1 6181 код 1 → счёт 13/13 + «собрано», стопка 14 → 1, вес 77, окно регистрации закрыто"),
				FString::Printf(TEXT("счёт %d окно %d стопка %d вес %d «%s»"), Cnt(B, 1, 1), B->IsJoinOpen() ? 1 : 0, BagAmount(S.Inv, kCellB1), S.Net->SelfWeightCur, *TextOf(B->CellUi[1].Cnt)));
			S.WM->ClearMsgBoxQueue(TEXT("книга: H2"));
			Feed(S, 6181, Resp365(7, 0, Rec57(0, 0, 0, 0), Page305(1, 0, {}, {}, {})));
			Expect(TopText(S.WM) == TEXT("7") && Cnt(B, 1, 1) == 13 && S.Net->SelfWeightCur == 77,
				TEXT("H2 6181 код 7 → msgbox «7» ([ОРИГИНАЛ] Д7), том и вес не тронуты"), TopText(S.WM));
			S.WM->ClearMsgBoxQueue(TEXT("книга: G2"));
			B->OnCellDoubleClick(0);
			Caps->Reset();
			B->OnJoinOk();
			Expect(B->IsJoinOpen() && Count(6180) == 0 && TopText(S.WM) == Want(S.GI, 111060),
				TEXT("G2 контроль: «ОК» без выбранной стопки → 6180 нет, msgbox 111060"), TopText(S.WM));
			S.WM->ClearMsgBoxQueue(TEXT("книга: H3"));
			// ★ МЕЛК-10: 6181 код 0 — эталон молчит (ни msgbox, ни записи), но ВСЕГДА перерисовывает книгу и закрывает 74200.
			Feed(S, 6181, Resp365(0, 0, Rec57(0, 0, 0, 0), Page305(1, 0, {}, {}, {})));
			Expect(!B->IsJoinOpen() && S.WM->GetMsgBoxQueueNum() == 0 && Cnt(B, 1, 1) == 13 && S.Net->SelfWeightCur == 77,
				TEXT("H3 6181 код 0 → окно регистрации закрыто, msgbox нет, том и вес не тронуты"),
				FString::Printf(TEXT("окно %d очередь %d счёт %d вес %d"), B->IsJoinOpen() ? 1 : 0, S.WM->GetMsgBoxQueueNum(), Cnt(B, 1, 1), S.Net->SelfWeightCur));
			B->OnCellDoubleClick(0);   // окно регистрации снова — для 74253 (G3)
			S.WM->ClearMsgBoxQueue(TEXT("книга: G3"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			ClickW(W, S.Book->BtnJoinNo, TEXT("74253 «Отмена»"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(!B->IsJoinOpen() && B->IsOpen(), TEXT("G3 74253 «Отмена» → закрыто только окно регистрации"));
			Shot(TEXT("grid"));   // кадр сетки тома 1 (после двойного щелчка — рывок кадра снимка не ломает его окно)
			// ═══ V1. ВАЖН-1: рыба уже собрана (13/13) — нумпад с максимумом 0 (двойной щелчок по ячейке 2 настоящими кликами) ═══
			DoubleClickCell(W, 1);   // двойной щелчок по собранной ячейке 2 настоящими кликами
		});
		Wait(2);   // оба симклика доставлены (их очередь последовательна) до проверки
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->IsJoinOpen() && B->JoinFish == kFishB && TextOf(B->JoinR1) == TEXT("13") && TextOf(B->JoinR3) == TEXT("13") && B->InvCell[0] == kCellB1,
				TEXT("V1a рыба собрана 13/13: двойной щелчок всё равно открывает 74200 (эталон: `cnt > 0`), стопка 14 ×1"),
				FString::Printf(TEXT("окно %d R1 «%s» R3 «%s» стопка %d"), B->IsJoinOpen() ? 1 : 0, *TextOf(B->JoinR1), *TextOf(B->JoinR3), B->InvCell[0]));
			ClickW(W, B->InvUi[0].Icon, TEXT("стопка 74211 (собранная рыба)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.WM->IsNumPadOpen() && S.Book->DebugLastNumPadMax == 0, TEXT("V1b ЛКМ по стопке → нумпад режима 10 с максимумом min(1, 13−13) = 0"),
				FString::Printf(TEXT("нумпад %d максимум %d"), S.WM->IsNumPadOpen() ? 1 : 0, S.Book->DebugLastNumPadMax));
			ClickW(W, NumPadNode(S.WM, DigitBtn(5)), TEXT("цифра «5» нумпада"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaChatSubsystem* Chat = S.GI->GetSubsystem<UAsdaChatSubsystem>();
			const FString Note = AsdaNotice::LocRaw(S.GI, 6835, true);
			FColor C(0, 0, 0, 0);
			const bool bNote = Chat && Chat->DebugFindLastSystemLineColor(Note, C) && C == UAsdaChatSubsystem::ColGold() && Chat->DebugBufferHas(EAsdaSysTrack::A, Note);
			Expect(NumPadText(S.WM) == TEXT("0") && bNote,
				TEXT("V1c строгий максимум (`uiClampQuantityToMax`): «5» срезано в «0», нотис 6835 в чат цветом −11514 (буфер A, 10 с)"),
				FString::Printf(TEXT("поле «%s», 6835 «%s» %s"), *NumPadText(S.WM), *Note, bNote ? TEXT("есть") : TEXT("НЕТ")));
			// ★ W3fix2 МЕЛК-1: «−» с ФИЗИЧЕСКОЙ клавиатуры (символьные события Slate в поле с фокусом) — в поле эталона его не набрать.
			if (UEditableText* E = Cast<UEditableText>(NumPadNode(S.WM, TEXT("NUMPAD_NUMBOX"))))
			{
				E->SetText(FText::GetEmpty());
				E->SetKeyboardFocus();
				FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('-'), FModifierKeysState(), 0, false));
				FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('5'), FModifierKeysState(), 0, false));
			}
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(NumPadText(S.WM) == TEXT("0"),
				TEXT("V1c2 W3fix2 МЕЛК-1: строгий режим, клавиатура «−», «5» → «−» отброшен, «5» срезано в «0» (прежде в поле оставалось «−5»)"),
				NumPadText(S.WM));
			ClickW(W, NumPadNode(S.WM, TEXT("NUMPAD_OK")), TEXT("NUMPAD_OK (0)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(!S.WM->IsNumPadOpen() && B->ChosenCell == -1 && B->Qty == 0 && TextOf(B->JoinR2) == TEXT("0"),
				TEXT("V1d «ОК» нумпада с 0 → `qty > 0` ложно: стопка не выбрана, к сдаче 0"),
				FString::Printf(TEXT("стопка %d кол-во %d R2 «%s»"), B->ChosenCell, B->Qty, *TextOf(B->JoinR2)));
			Caps->Reset();
			S.WM->ClearMsgBoxQueue(TEXT("книга: V1e"));
			ClickW(W, B->BtnJoinOk, TEXT("74252 «ОК» (собранная рыба)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(Count(6180) == 0 && TopText(S.WM) == Want(S.GI, 111060), TEXT("V1e 74252 → 6180 НЕ уходит, msgbox 111060 (как оригинал; прежде порт слал 6180 с введённым 5 → сервер код 7)"),
				FString::Printf(TEXT("6180 ×%d «%s»"), Count(6180), *TopText(S.WM)));
			S.WM->ClearMsgBoxQueue(TEXT("книга: V1f"));
			// Контроль общей точки: ОБЫЧНЫЙ режим (прочие режимы нумпада) при максимуме 0 — по-прежнему «без предела».
			CtlNumPadOk = -1; CtlNumPadQty = -1;
			S.WM->ShowNumPad(0, [this](bool bOk, int32 Q) { CtlNumPadOk = bOk ? 1 : 0; CtlNumPadQty = Q; }, /*bCenterOnScreen*/ false);
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			ClickW(W, NumPadNode(S.WM, DigitBtn(5)), TEXT("цифра «5» (обычный режим)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(NumPadText(S.WM) == TEXT("5"), TEXT("V1f контроль: обычный режим, максимум 0 — поле «5» не срезано (поведение прочих режимов не изменилось)"), NumPadText(S.WM));
			ClickW(W, NumPadNode(S.WM, TEXT("NUMPAD_OK")), TEXT("NUMPAD_OK (обычный режим)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(CtlNumPadOk == 1 && CtlNumPadQty == 5, TEXT("V1g контроль: обычный режим ОК(5) при максимуме 0 → (true, 5), как прежде"),
				FString::Printf(TEXT("ок %d кол-во %d"), CtlNumPadOk, CtlNumPadQty));
			// ★ W3fix2 МЕЛК-1, пара: «−» от кнопки MAX при отрицательном максимуме строгого режима сохраняется (эталон пишет
			//   `sprintf("%d", max)`), и дописанная цифра его не снимает (`atoi("-35") > -3` ложно — срезки нет).
			S.WM->ShowNumPad(-3, [](bool, int32) {}, /*bCenterOnScreen*/ false, /*bStrictMax*/ true);
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			ClickW(W, NumPadNode(S.WM, TEXT("NUMPAD_MAX")), TEXT("NUMPAD_MAX (строгий, максимум −3)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			CtlNumPadText = NumPadText(S.WM);
			ClickW(W, NumPadNode(S.WM, DigitBtn(5)), TEXT("цифра «5» после MAX (строгий, максимум −3)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(CtlNumPadText == TEXT("-3") && NumPadText(S.WM) == TEXT("-35"),
				TEXT("V1h W3fix2 МЕЛК-1 пара: строгий режим, MAX при максимуме −3 → «-3» (минус от MAX сохранён), «5» → «-35»"),
				FString::Printf(TEXT("после MAX «%s», после «5» «%s»"), *CtlNumPadText, *NumPadText(S.WM)));
			S.WM->CloseNumPad(false);
			// ═══ V3. ВАЖН-3: стопка в сумке меняется под немодальным 74200 ═══
			B->CloseJoin();
			B->OnCellDoubleClick(0);   // том 1, Карась 5/17: стопки 10 (×10) и 12 (×4)
			ClickW(W, B->InvUi[0].Icon, TEXT("стопка 74211 (Карась ×10)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.WM->IsNumPadOpen() && S.Book->DebugLastNumPadMax == 10, TEXT("V3a стопка ×10 → нумпад, максимум min(10, 17−5) = 10"),
				FString::Printf(TEXT("максимум %d"), S.Book->DebugLastNumPadMax));
			ClickW(W, NumPadNode(S.WM, DigitBtn(4)), TEXT("цифра «4»"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			ClickW(W, NumPadNode(S.WM, TEXT("NUMPAD_OK")), TEXT("NUMPAD_OK (4)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->ChosenCell == kCellA1 && B->Qty == 4, TEXT("V3b выбрана стопка 10, к сдаче 4"), FString::Printf(TEXT("стопка %d кол-во %d"), B->ChosenCell, B->Qty));
			// В ячейке 10 теперь ДРУГАЯ рыба того же тома (69582) — игрок переложил стопки в сумке под немодальным 74200.
			S.Inv->DebugPutItem(2, kCellA1, kFishB, 3);
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->ChosenCell == -1 && B->Qty == 0 && TextOf(B->JoinR2) == TEXT("0") && !AsdaUi::IsShown(B->JoinCheck),
				TEXT("V3c [ПОРТ] LiveRefresh: в выбранной ячейке другая рыба → выбор [466]/[467] снят, курсор скрыт"),
				FString::Printf(TEXT("стопка %d кол-во %d R2 «%s»"), B->ChosenCell, B->Qty, *TextOf(B->JoinR2)));
			Caps->Reset();
			S.WM->ClearMsgBoxQueue(TEXT("книга: V3d"));
			ClickW(W, B->BtnJoinOk, TEXT("74252 «ОК» (выбор снят)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(Count(6180) == 0 && TopText(S.WM) == Want(S.GI, 111060), TEXT("V3d 74252 → 6180 с ЧУЖОЙ рыбой не уходит, msgbox 111060"),
				FString::Printf(TEXT("6180 ×%d «%s»"), Count(6180), *TopText(S.WM)));
			S.WM->ClearMsgBoxQueue(TEXT("книга: V3e"));
			// Стопка уменьшилась меньше введённого: выбрать 4 из ×10, затем ×2.
			S.Inv->DebugPutItem(2, kCellA1, kFishA, 10);
			B->RefreshJoin(B->JoinFish);
			B->OnInvClick(0);
			NumPadType(S.WM, 4);
			S.WM->CloseNumPad(true);
			S.Inv->DebugPutItem(2, kCellA1, kFishA, 2);
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->ChosenCell == kCellA1 && B->Qty == 2 && TextOf(B->JoinR2) == TEXT("2"),
				TEXT("V3e [ПОРТ] LiveRefresh: стопка уменьшилась до 2 < введённых 4 → к сдаче 2"),
				FString::Printf(TEXT("стопка %d кол-во %d R2 «%s»"), B->ChosenCell, B->Qty, *TextOf(B->JoinR2)));
			// ★ W3fix2 МЕЛК-2: «ОК» 74252 в ТОМ ЖЕ кадре, что уменьшение стопки (перерисовка `Tick` ещё не прошла) — правило то
			//   же, что у перерисовки: количество урезается до стопки, 6180 уходит с урезанным (прежде выбор снимался → 111060).
			S.Inv->DebugPutItem(2, kCellA1, kFishA, 10);
			B->RefreshJoin(B->JoinFish);
			B->OnInvClick(0);
			NumPadType(S.WM, 4);
			S.WM->CloseNumPad(true);
			const int32 QtyBefore = B->Qty;
			S.Inv->DebugPutItem(2, kCellA1, kFishA, 3);
			Caps->Reset();
			S.WM->ClearMsgBoxQueue(TEXT("книга: V3e2"));
			B->OnJoinOk();
			{
				FCap C2;
				const int32 N2 = Count(6180, &C2);
				Expect(QtyBefore == 4 && B->ChosenCell == kCellA1 && B->Qty == 3 && N2 == 1 && C2.P.Num() == 19 && AsdaBytes::RdI32(C2.P.GetData() + 13) == 3
					&& S.WM->GetMsgBoxQueueNum() == 0,
					TEXT("V3e2 W3fix2 МЕЛК-2: стопка 4 → 3 и «ОК» 74252 в том же кадре → к сдаче 3 (как перерисовка), 6180 с 3, без 111060"),
					FString::Printf(TEXT("было %d, стопка %d кол-во %d, 6180 ×%d кол-во %d, окон %d"), QtyBefore, B->ChosenCell, B->Qty, N2,
						C2.P.Num() == 19 ? AsdaBytes::RdI32(C2.P.GetData() + 13) : -1, S.WM->GetMsgBoxQueueNum()));
			}
			S.Inv->DebugPutItem(2, kCellA1, kFishA, 2);
			B->RefreshJoin(B->JoinFish);   // к сдаче 2 — исходное состояние контроля V3f
			// Контроль ключа: LiveRefresh 0 — как оригинал (сверки нет): другая рыба в ячейке, выбор остаётся, 6180 уходит.
			SetCVar(TEXT("asda.FishBook.LiveRefresh"), 0);
			S.Inv->DebugPutItem(2, kCellA1, kFishB, 3);
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Caps->Reset();
			B->OnJoinOk();
			FCap C;
			const int32 N = Count(6180, &C);
			Expect(B->ChosenCell == kCellA1 && N == 1 && C.P.Num() == 19 && AsdaBytes::RdI16(C.P.GetData() + 8) == kCellA1 && AsdaBytes::RdI32(C.P.GetData() + 13) == 2,
				TEXT("V3f контроль LiveRefresh 0 — как оригинал: выбор не сверяется, 6180 уходит по ячейке 10 (кол-во 2)"),
				FString::Printf(TEXT("стопка %d 6180 ×%d"), B->ChosenCell, N));
			SetCVar(TEXT("asda.FishBook.LiveRefresh"), 1);
			S.Inv->DebugPutItem(2, kCellA1, kFishA, 10);
			B->CloseJoin();
			// ═══ V2. ВАЖН-2: окно сумки ПОВЕРХ ячейки книги — клик по ячейке книгу не трогает ═══
			B->Sel = 0; B->RefreshAll();
			S.Inv->OpenWindow();   // показ = подъём поверх (сумка выше книги)
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			// Точка над сумкой (опрос той же общей проверкой, что у книги) → сдвиг окна сумки так, чтобы она легла на ячейку 2.
			FBox2D Cell(ForceInit);
			UUserWidget* Bag = S.WM->FindWindow(FName(TEXT("INVEN_WIN")));
			FVector2D P(-1.f, -1.f);
			FVector2D V(1024.f, 768.f);
			if (UGameViewportClient* VC = W->GetGameViewport()) { VC->GetViewportSize(V); }
			const FVector2D Win0 = FSlateApplication::Get().GetCursorPos();
			for (float Y = 8.f; Y < V.Y && P.X < 0.f; Y += 12.f)
			{
				for (float X = 8.f; X < V.X && P.X < 0.f; X += 12.f)
				{
					FSlateApplication::Get().SetCursorPos(FVector2D(X, Y));
					if (S.WM->IsCursorOverHigherWindow(FName(TEXT("FISH_BOOK_WIN")))) { P = FVector2D(X, Y); }
				}
			}
			FSlateApplication::Get().SetCursorPos(Win0);
			if (Bag && P.X >= 0.f && RectOfW(B->CellUi[1].Root, Cell))
			{
				const FGeometry& G = Bag->GetCachedGeometry();
				const FVector2D D = G.AbsoluteToLocal(Cell.GetCenter()) - G.AbsoluteToLocal(P);
				Bag->SetRenderTranslation(Bag->GetRenderTransform().Translation + D);
				BagShifted = Bag;
			}
			Expect(Bag && P.X >= 0.f, TEXT("V2a сумка открыта поверх книги (точка над ней найдена общей проверкой `IsCursorOverHigherWindow`)"),
				FString::Printf(TEXT("сумка %s точка (%.0f,%.0f)"), Bag ? TEXT("есть") : TEXT("НЕТ"), P.X, P.Y));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			FBox2D Cell(ForceInit);
			bool bCovered = false;
			if (RectOfW(B->CellUi[1].Root, Cell))
			{
				FSlateApplication::Get().SetCursorPos(Cell.GetCenter());
				bCovered = S.WM->IsCursorOverHigherWindow(FName(TEXT("FISH_BOOK_WIN")));
			}
			Expect(bCovered, TEXT("V2b предусловие: центр ячейки 2 книги закрыт окном сумки"));
			SelBefore = B->Sel;
			ClickW(W, B->CellUi[1].Root, TEXT("ячейка 2 ПОД сумкой"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->Sel == SelBefore && B->Sel == 0 && !B->IsJoinOpen(), TEXT("V2c ВАЖН-2: ЛКМ по ячейке книги ПОД окном сумки — книга молчит (эталон `uiWindowMgrDispatchMouse` — только верхнему окну)"),
				FString::Printf(TEXT("выбор %d (был %d)"), B->Sel, SelBefore));
			if (UUserWidget* Bag = BagShifted.Get()) { Bag->SetRenderTranslation(FVector2D::ZeroVector); }
			BagShifted = nullptr;
			S.Inv->CloseWindow();
		});
		// ═══ V2e/V2f. ★ W3fix2 (ревизия `fishing_impl_W3fix_W6_REVIEW.md`, МЕЛК-4): вторая строка ВАЖН-2 — всплывающий слой ЧУЖОГО
		//   окна (заявка общей точки `PushTopmost`, как у раскрытого списка/справки/контекстного меню) поверх ячейки 2 → клик молчит.
		//   Обычного окна выше книги при этом нет (изолирована именно `IsPointerBlockedByTopmost`). Пара — V2d (без слоя). ═══
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			TWeakObjectPtr<UWidget> CellRoot(B->CellUi[1].Root);
			S.WM->PushTopmost(FName(TEXT("SELFTEST_FISHBOOK_POPUP")), FName(TEXT("SELFTEST_FISHBOOK_POPUP_OWNER")),
				FAsdaUiZoneRect::CreateLambda([CellRoot](FVector2D& Mn, FVector2D& Mx) -> bool
				{
					FBox2D R(ForceInit);
					if (!RectOfW(CellRoot.Get(), R)) { return false; }
					Mn = R.Min; Mx = R.Max;
					return true;
				}));
			FBox2D Cell(ForceInit);
			bool bTop = false, bHigher = true;
			if (RectOfW(B->CellUi[1].Root, Cell))
			{
				FSlateApplication::Get().SetCursorPos(Cell.GetCenter());
				bTop = S.WM->IsPointerBlockedByTopmost(FName(TEXT("FISH_BOOK_WIN")));
				bHigher = S.WM->IsCursorOverHigherWindow(FName(TEXT("FISH_BOOK_WIN")));
			}
			Expect(bTop && !bHigher, TEXT("V2e предусловие: над ячейкой 2 — всплывающий слой чужого окна, обычного окна выше книги нет"),
				FString::Printf(TEXT("слой %d окно выше %d"), bTop ? 1 : 0, bHigher ? 1 : 0));
			SelBefore = B->Sel;
			ClickW(W, B->CellUi[1].Root, TEXT("ячейка 2 ПОД всплывающим слоем"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->Sel == SelBefore && B->Sel == 0 && !B->IsJoinOpen(),
				TEXT("V2f W3fix2 МЕЛК-4: ЛКМ по ячейке ПОД всплывающим слоем чужого окна (`IsPointerBlockedByTopmost`) — книга молчит"),
				FString::Printf(TEXT("выбор %d (был %d)"), B->Sel, SelBefore));
			S.WM->PopTopmost(FName(TEXT("SELFTEST_FISHBOOK_POPUP")));
		});
		Wait(DblSteps);
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			ClickW(W, S.Book->CellUi[1].Root, TEXT("ячейка 2 (сумка закрыта)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->Sel == 1, TEXT("V2d контроль: тот же клик без окна поверх выбирает ячейку 2"), FString::Printf(TEXT("выбор %d"), B->Sel));
			ClickW(W, B->RewardUi[2].Icon, TEXT("награда 3 (74083)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->RewardSel == 2 && AsdaUi::IsShown(B->RewardUi[2].Frame) && !AsdaUi::IsShown(B->RewardUi[0].Frame),
				TEXT("I1 ЛКМ по награде 3 → выбрана, рамка 74113"), FString::Printf(TEXT("выбор %d"), B->RewardSel));
			Caps->Reset();
			// Курсор стоит на награде — её подсказка у курсора; уводим курсор (подсказка снимается в Tick), щелчок — через 0,15 с.
			FSlateApplication::Get().SetCursorPos(FVector2D(4.f, 4.f));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, WW](float) -> bool
			{
				if (UWorld* W2 = WW.Get()) { const FSubs S2 = Get(W2); if (S2.Book) { ClickW(W2, S2.Book->BtnReward, TEXT("74090 «Получение награды»")); } }
				return false;
			}), 0.15f);
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(Count(6182) == 0 && TopText(S.WM) == Want(S.GI, 111023), TEXT("I2 74090 при незавершённом томе → 111023, 6182 нет"), TopText(S.WM));
			S.WM->ClearMsgBoxQueue(TEXT("книга: I3"));
			Feed(S, 6174, Page305(1, 0, { 17, 13, 7, 7 }, { 40, 50, 30, 30 }, { 10, 20, 30, 30 }));
			B->RefreshAll();
			ClickW(W, B->BtnReward, TEXT("74090 «Получение награды» (том завершён)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			FCap C;
			const int32 N = Count(6182, &C);
			Expect(N == 1 && C.Env == TEXT('S') && C.P.Num() == 12 && AsdaBytes::RdI32(C.P.GetData()) == kCharId && AsdaBytes::RdI32(C.P.GetData() + 4) == 1
				&& AsdaBytes::RdI32(C.P.GetData() + 8) == 137, TEXT("I3 74090 при завершённом томе → РОВНО ОДИН 6182 (charId, id 1, код 137 — серверная таблица), без подтверждения"),
				FString::Printf(TEXT("×%d %d б"), N, C.P.Num()));
			B->RewardSel = -1;
			B->OnRewardButton();
			Expect(Count(6182) == 1 && TopText(S.WM) == Want(S.GI, 111071), TEXT("I4 контроль: без выбора → 111071, 6182 нет"), TopText(S.WM));
			S.WM->ClearMsgBoxQueue(TEXT("книга: I5"));
			ClickW(W, B->RewardLabel5, TEXT("подпись 74085"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->RewardSel == -1, TEXT("I5 Р7/Д2: ЛКМ по подписи 74085 награду не выбирает"), FString::Printf(TEXT("выбор %d"), B->RewardSel));
			SetCVar(TEXT("asda.FishBook.LegacyReward5"), 1);
			ClickW(W, B->RewardLabel5, TEXT("подпись 74085 (оригинал)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->RewardSel == 4, TEXT("I6 контроль LegacyReward5 1: подпись 74085 выбирает «5-ю награду», как оригинал"), FString::Printf(TEXT("выбор %d"), B->RewardSel));
			SetCVar(TEXT("asda.FishBook.LegacyReward5"), 0);
			B->OnTabSelected(1);
			FBox2D R(ForceInit);
			if (RectOfW(B->RewardUi[0].Icon, R)) { FSlateApplication::Get().SetCursorPos(R.GetCenter()); }
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.Tip->GetTipOwner() == EAsdaTipOwner::FishBook && S.Tip->IsTooltipShown(), TEXT("I7 наведение на награду 74081 → подсказка владельца FishBook"));
			FSlateApplication::Get().SetCursorPos(FVector2D(4.f, 4.f));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(S.Tip->GetTipOwner() != EAsdaTipOwner::FishBook, TEXT("I8 уход курсора → подсказка книги снята"));
			// J. 6183.
			S.WM->ClearMsgBoxQueue(TEXT("книга: J"));
			FAsdaSpecialitemTooltiplistRow Tip;
			const FString RewName = S.IT->GetSpecialItemTooltip(137, Tip) ? Tip.Name : FString();
			Feed(S, 6183, B6183(1, 1, 90, Rec57(137, 1, 3, 1)));
			Expect(!B->St[1].bReg && S.Net->SelfWeightCur == 90 && TopText(S.WM) == Want(S.GI, 111013, { B->St[1].Name, RewName }),
				TEXT("J1 6183 код 1 → msgbox 111013 (том, награда), том 1 снят, вес 90"), TopText(S.WM));
			const int32 Q = S.WM->GetMsgBoxQueueNum();
			Feed(S, 6183, B6183(4, 2, 1, Rec57(0, 0, 0, 0)));
			Expect(S.WM->GetMsgBoxQueueNum() == Q && B->St[2].bReg && S.Net->SelfWeightCur == 90, TEXT("J2 контроль: 6183 код 4 → ничего"));
			// ★ МЕЛК-10: 6183 по ДВУМ другим таблицам — item_list (имя предмета) и самоцветы (имя самоцвета); том 7 не
			//   зарегистрирован (имя тома — по индексу, состояние не трогается).
			S.WM->ClearMsgBoxQueue(TEXT("книга: J3"));
			Feed(S, 6183, B6183(1, 7, 91, Rec57(kNotFish, 2, kCellOther, 3)));
			const FString J3a = TopText(S.WM);
			S.WM->ClearMsgBoxQueue(TEXT("книга: J3b"));
			FAsdaSowellRow Sw;
			const FString SwName = S.IT->GetSowell(kSowel, Sw) ? Sw.Name : FString();
			// Код награды 6183 = код записи @7; кол-во < −1 — запись эталон в сумку не кладёт (g:611747-611788).
			Feed(S, 6183, B6183(1, 7, 92, Rec57(kSowel, 2, 59, -2)));
			const FString J3b = TopText(S.WM);
			Expect(J3a == Want(S.GI, 111013, { B->St[7].Name, S.IT->GetItemName(kNotFish) }),
				TEXT("J3 6183 код 1, код награды из item_list → 111013 (имя тома 7 по индексу, имя предмета)"), J3a);
			Expect(!SwName.IsEmpty() && J3b == Want(S.GI, 111013, { B->St[7].Name, SwName }) && S.Net->SelfWeightCur == 92 && BagAmount(S.Inv, 59) == -1 && !B->St[7].bReg,
				TEXT("J4 6183 код 1, код награды — самоцвет 7001 → 111013 с именем самоцвета; вес 92; запись (кол-во −2) не применена"), J3b);
			S.Net->SetSelfWeight(90, -1, TEXT("самотест книги: J4"));
			S.WM->ClearMsgBoxQueue(TEXT("книга: K"));
			// K. Удаление тома 0.
			B->Page = 0; B->TabSel = 0; B->TabOfs = 0;
			B->RefreshAll();
			Caps->Reset();
			ClickW(W, B->BtnDel, TEXT("74271 «Удалить книгу»"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(S.WM->IsMsgBoxQueued(1, 77) && TopText(S.WM) == Want(S.GI, 111017, { B->St[0].Name }), TEXT("K1 74271 → диалог 77 (111017 с именем тома 0)"), TopText(S.WM));
			ClickW(W, MsgNode(S.WM, TEXT("TWOBUT_BUT00")), TEXT("«ОК» диалога 77"));   // МЕЛК-11: кнопка, а не CloseConfirm
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			FCap C;
			const int32 N = Count(6178, &C);
			Expect(N == 1 && C.Env == TEXT('S') && C.P.Num() == 8 && AsdaBytes::RdI32(C.P.GetData()) == kCharId && AsdaBytes::RdI32(C.P.GetData() + 4) == 0,
				TEXT("K2 «ОК» диалога 77 → РОВНО ОДИН 6178 (charId, id тома 0), 8 б"), FString::Printf(TEXT("×%d %d б"), N, C.P.Num()));
			S.WM->ClearMsgBoxQueue(TEXT("книга: K3"));
			Feed(S, 6179, B6179(1, 0));
			Expect(!B->St[0].bReg && TopText(S.WM) == Want(S.GI, 111072), TEXT("K3 6179 код 1 → том 0 снят, msgbox 111072"), TopText(S.WM));
			S.WM->ClearMsgBoxQueue(TEXT("книга: K4"));
			Feed(S, 6179, B6179(0, 2));
			Expect(B->St[2].bReg && S.WM->GetMsgBoxQueueNum() == 0, TEXT("K4 контроль: 6179 код 0 → ничего"));
			B->Page = 0;
			B->OnDeleteClicked();
			Expect(!S.WM->IsMsgBoxQueued(1, 77) && TopText(S.WM) == Want(S.GI, 111069), TEXT("K5 незарегистрированный том → 111069, диалога 77 нет"), TopText(S.WM));
			S.WM->ClearMsgBoxQueue(TEXT("книга: L"));
			// L. Книга-предмет из сумки.
			Caps->Reset();
			S.Inv->UseFromBagWindow(2, kCellBook);
			// ★ МЕЛК-9: имя игрока — ИЗВЕСТНАЯ константа офлайн-спавна (`kSelfName`), а не тот же геттер, что у кода.
			// ★ 09.10.2026 [ОРИГИНАЛ] Порядок по дизасму case 'O': первый %s — имя предмета (книги), второй — своё имя.
			Expect(S.WM->IsMsgBoxQueued(1, 79) && TopText(S.WM) == Want(S.GI, 111018, { S.IT->GetItemName(kBook1), kSelfName })
				&& BracketArg(TopText(S.WM), 0) == S.IT->GetItemName(kBook1) && BracketArg(TopText(S.WM), 1) == kSelfName,
				TEXT("L1 использование книги (подтип 210) из сумки → диалог 79 (111018: [предмет-книга] в руководстве [«Рыбак»]), пакета нет"), TopText(S.WM));
			Expect(Count(6176) == 0 && Count(5028) == 0, TEXT("L2 до «ОК» ни 6176, ни 5028 (обычное использование) не ушло"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			ClickW(W, MsgNode(S.WM, TEXT("TWOBUT_BUT00")), TEXT("«ОК» диалога 79"));   // МЕЛК-11: кнопка, а не CloseConfirm
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			FCap C2;
			const int32 N2 = Count(6176, &C2);
			FString Hex; for (uint8 X : C2.P) { Hex += FString::Printf(TEXT("%02X "), X); }
			Expect(N2 == 1 && C2.Env == TEXT('S') && C2.P.Num() == 15 && AsdaBytes::RdI32(C2.P.GetData()) == kCharId && AsdaBytes::RdI16(C2.P.GetData() + 4) == kCellBook
				&& AsdaBytes::RdI16(C2.P.GetData() + 6) == -1 && C2.P[8] == 2 && AsdaBytes::RdI32(C2.P.GetData() + 9) == 1 && B->bAwaitRegist,
				TEXT("L3 «ОК» диалога 79 → РОВНО ОДИН 6176, 15 б (charId, слот 5, второй −1, контейнер 2, кол-во 1, вес 0), «ждём 6177»"),
				FString::Printf(TEXT("×%d [%s]"), N2, *Hex));
			Expect(B->BookRegisterProbe() && !S.WM->IsMsgBoxQueued(1, 79), TEXT("L4 FSM 101 при «ждём 6177» — остаётся в 101, диалога нет"));
			Feed(S, 6177, Resp365(1, 55, Rec57(kBook1, 2, kCellBook, -1), Page305(1, 0, {}, {}, {})));
			Expect(B->St[1].bReg && !B->bAwaitRegist && BagAmount(S.Inv, kCellBook) == -1 && S.Net->SelfWeightCur == 55 && !B->IsOpen(),
				TEXT("L5 6177 код 1 → том 1 зарегистрирован, книга ушла из сумки, вес 55; книга спрятана (`itemCollectUiShowOrToggle`)"),
				FString::Printf(TEXT("рег %d ждём %d сумка %d вес %d окно %d"), B->St[1].bReg ? 1 : 0, B->bAwaitRegist ? 1 : 0, BagAmount(S.Inv, kCellBook), S.Net->SelfWeightCur, B->IsOpen() ? 1 : 0));
			{
				// ★ МЕЛК-10: чат 111073 (%d = индекс тома @60) — цвет −11514, невидимый буфер A (TTL 10 с), msgbox нет.
				UAsdaChatSubsystem* Chat = S.GI->GetSubsystem<UAsdaChatSubsystem>();
				const FString Line = AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(S.GI, 111073, true), { TEXT("1") });
				FColor C(0, 0, 0, 0);
				const bool bLine = Chat && Chat->DebugFindLastSystemLineColor(Line, C) && C == UAsdaChatSubsystem::ColGold() && Chat->DebugBufferHas(EAsdaSysTrack::A, Line);
				Expect(bLine && S.WM->GetMsgBoxQueueNum() == 0, TEXT("L5a 6177 код 1 → чат 111073 «[1]…» цветом −11514 (буфер A, 10 с), msgbox нет"),
					FString::Printf(TEXT("«%s» %s"), *Line, bLine ? TEXT("есть") : TEXT("НЕТ")));
			}
			S.WM->ClearMsgBoxQueue(TEXT("книга: L6"));
			Feed(S, 6177, Resp365(5, 0, Rec57(0, 0, 0, 0), Page305(1, 0, {}, {}, {})));
			Expect(TopText(S.WM) == Want(S.GI, 111020), TEXT("L6 6177 код 5 → msgbox 111020"), TopText(S.WM));
			S.WM->ClearMsgBoxQueue(TEXT("книга: M"));
			// M. FSM 101.
			S.Inv->DebugPutItem(2, kCellBook3, kBook3, 1);
			const bool bP1 = B->BookRegisterProbe();
			const bool bQ1 = S.WM->IsMsgBoxQueued(1, 79);
			const bool bP2 = B->BookRegisterProbe();
			Expect(bP1 && bQ1 && bP2 && S.WM->GetMsgBoxQueueNum() == 1 && B->BookCell == kCellBook3,
				TEXT("M1 FSM 101: книга тома 3 в сумке → диалог 79 (книга [469] = ячейка 6), повтор не плодит окно"),
				FString::Printf(TEXT("окон %d книга %d"), S.WM->GetMsgBoxQueueNum(), B->BookCell));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			SelBefore = S.Fish->GetFishState();
			Caps->Reset();
			ClickW(W, MsgNode(S.WM, TEXT("TWOBUT_BUT01")), TEXT("«Отмена» диалога 79 (fishState 0)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(SelBefore == 0 && S.Fish->GetFishState() == 0 && Count(6167) == 0 && !S.WM->IsMsgBoxQueued(1, 79),
				TEXT("M2 контроль: «Отмена» 79 при fishState 0 → автомат не тронут, 6167 нет (эталон: 100 только при fishState ≠ 0)"),
				FString::Printf(TEXT("состояние %d → %d, 6167 ×%d"), SelBefore, S.Fish->GetFishState(), Count(6167)));
			S.Inv->DebugPutItem(2, kCellBook3, 0, 0);
			Expect(!B->BookRegisterProbe() && S.WM->GetMsgBoxQueueNum() == 0, TEXT("M3 книги нет → false (автомат уйдёт в 100), диалога нет"));
			// ═══ N. Улов ПАКЕТОМ 6172 (штатный разбор → автомат В1 → `OnFishCaught` → книга), МЕЛК-10 ═══
			const TArray<uint8> Bait = (Eq10Was.Num() >= 57) ? Eq10Was : Rec57(0, 3, 10, -1);   // слот наживки — как был
			S.Net->DebugFeedPacket(6172, B6172(1, 5, 60, 33, Bait, Rec57(kFishC, 2, kCellCaught, 1)));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(Cnt(B, 1, 2) == 1 && B->St[1].MaxSz[2] == 33 && B->St[1].MinSz[2] == 33 && Cnt(B, 2, 2) == 1 && BagAmount(S.Inv, kCellCaught) == 1,
				TEXT("N1 6172 код 1 (улов 69587, 33 см) → первая поимка: счёт 1, макс.=мин.=33 (тома 1 и 2), рыба в сумке"),
				FString::Printf(TEXT("счёт %d макс %d мин %d том2 %d сумка %d"), Cnt(B, 1, 2), B->St[1].MaxSz[2], B->St[1].MinSz[2], Cnt(B, 2, 2), BagAmount(S.Inv, kCellCaught)));
			const TArray<uint8> Bait = (Eq10Was.Num() >= 57) ? Eq10Was : Rec57(0, 3, 10, -1);
			S.Net->DebugFeedPacket(6172, B6172(1, 5, 60, 40, Bait, Rec57(kFishC, 2, kCellCaught, 2)));
			S.Net->DebugFeedPacket(6172, B6172(1, 5, 60, 20, Bait, Rec57(kFishC, 2, kCellCaught, 3)));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(Cnt(B, 1, 2) == 1 && B->St[1].MaxSz[2] == 40 && B->St[1].MinSz[2] == 20,
				TEXT("N2 6172 ×2 (40 и 20 см) → счёт не растёт, только макс. 40 / мин. 20"),
				FString::Printf(TEXT("счёт %d макс %d мин %d"), Cnt(B, 1, 2), B->St[1].MaxSz[2], B->St[1].MinSz[2]));
			// ═══ M4. Основная ветка «Отмена 79 при fishState ≠ 0 → 100» (g:380708): после 6172 автомат НЕ в 0 ═══
			SelBefore = S.Fish->GetFishState();
			S.Inv->DebugPutItem(2, kCellBook3, kBook3, 1);
			B->BookRegisterProbe();   // диалог 79 (книга тома 3)
		});
		// ★ W3fix2 (ревизия `fishing_impl_W3fix_W6_REVIEW.md`, МЕЛК-5): контроль M4 — тот же интервал (один шаг 0,4 с) БЕЗ
		//   клика: автомат сам в {0, 100, 107, 128} не уходит, диалог 79 висит. Без этой пары M4 не отличает ветку «Отмены» от
		//   самостоятельного хода автомата (офлайн дизъюнкт «зоны нет» истинен всегда).
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			// ★ R5-fix8-c (14.11.2026): «Отмена» снова идёт ВО ВРЕМЯ клипа вытаскивания (автомат 113) — именно эту ветку проверяет M4.
			//   Ожидание «автомат вышел из 113/119» (четвёртая запись доли b) снято по её же ревизии: оно переносило «Отмену» в 126
			//   при любом кадре. Красное при ~18 к/с было не дефектом игры: `OnFishingClipEnd` (код 0x1D) ставит 126 из любого
			//   состояния, как `Fishing_OnStateMessage@0xFC56E0` эталона, и если конец клипа приходит сразу за «Отменой», ход
			//   113 → 100 → 126 законный. Поэтому M4 ниже сверяется с самим переходом «Отмена 79 → 100», а не с конечным состоянием.
			Caps->Reset();
			M4CtlState = S.Fish->GetFishState();
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			const int32 Fs = S.Fish->GetFishState();
			const bool bIdle = (Fs != 0 && Fs != 100 && Fs != 107 && Fs != 128);
			Expect(bIdle && S.WM->IsMsgBoxQueued(1, 79) && Count(6167) == 0,
				TEXT("M4k контроль: тот же интервал без «Отмены» — автомат НЕ в 0/100/107/128, диалог 79 на месте, 6167 нет"),
				FString::Printf(TEXT("состояние %d → %d, диалог %d, 6167 ×%d"), M4CtlState, Fs, S.WM->IsMsgBoxQueued(1, 79) ? 1 : 0, Count(6167)));
			Caps->Reset();
			M4CancelWas = S.Fish->GetBookCancelCountForCheck();
			M4ClickState = Fs;
			ClickW(W, MsgNode(S.WM, TEXT("TWOBUT_BUT01")), TEXT("«Отмена» диалога 79 (fishState ≠ 0)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			// Состояние 100 автомат тут же отрабатывает: зона выбрана — `Send6167` → 107; зоны нет (офлайн, без 109) — Р1 → 128 → 0.
			//   ★ R4-b хвосты (15.11.2026) (ревизия R5 `REVIEW8.md` п. 7): признак ветки — само событие «Отмена 79 → 100» (ниже, R5-fix8-c);
			//   конечное состояние лишь отсекает «автомат остался в 119/113» (0/100/107/128 или 126/127 — клип кончился сразу за «Отменой»).
			const int32 Fs = S.Fish->GetFishState();
			// ★ R5-fix8-c (14.11.2026): ветка «Отмены» — по самому событию: ровно один переход «Отмена 79 → 100» после щелчка и именно из того
			//   состояния, в котором щёлкнули (не 0 и не 100). Прежний признак — конечное состояние из {0, 100, 107, 128} — был лишь
			//   заменителем события и давал ложный отказ, когда сразу за «Отменой» кончался клип вытаскивания (100 → 126).
			const int32 NCancel = S.Fish->GetBookCancelCountForCheck() - M4CancelWas;   // переходов «Отмена 79 → 100» после щелчка
			const int32 From = S.Fish->GetBookCancelFromStateForCheck();                // из какого состояния
			//   (четвёртая запись, ревизия) Исходное состояние с состоянием на момент ЗАКАЗА щелчка не сравнивается: щелчок доходит
			//   через 3–4 кадра, клип за них может кончиться (113 → 126), и «Отмена» из 126 так же законна. Конечное состояние:
			//   автомат отработал 100 (0/100/107/128) либо сразу за «Отменой» кончился клип вытаскивания (126/127) — «остался в
			//   113/119» по-прежнему ловится.
			const bool bAfter100 = (Fs == 0 || Fs == 100 || Fs == 107 || Fs == 128 || Fs == 126 || Fs == 127);
			const bool bBranch = NCancel == 1 && From != 0 && From != 100 && bAfter100 && (Count(6167) == 1 || S.Fish->GetPickedSpot() < 0);
			Expect(SelBefore != 0 && SelBefore != 100 && bBranch && !S.WM->IsMsgBoxQueued(1, 79),
				TEXT("M4 «Отмена» 79 при fishState ≠ 0 → автомат в 100 (дальше 6167 или Р1 → 0), диалог закрыт"),
				FString::Printf(TEXT("состояние до %d, при щелчке %d, переходов «Отмена → 100» %d из состояния %d, сейчас %d, зона %d, 6167 ×%d"),
					SelBefore, M4ClickState, NCancel, From, Fs, S.Fish->GetPickedSpot(), Count(6167)));
			S.Inv->DebugPutItem(2, kCellBook3, 0, 0);
			S.Fish->ResetSessionState(EAsdaSessionResetReason::LeftWorld);   // автомат В1 → 0 (навык вернёт Finish)
			// O. Геометрия: открыть книгу, показать все 30 ячеек и окно регистрации.
			B->Open();
			B->Page = 1; B->Sel = 0;
			B->OpenJoin(kFishA);
			for (int32 i = 0; i < 30; ++i) { AsdaUi::SetShownDecor(B->CellUi[i].Root, true); }
			for (int32 n = 0; n < 4; ++n) { AsdaUi::SetShownDecor(B->TabPlanes[n], true); AsdaUi::SetShown(B->TabButtons[n], true); }
			for (int32 t = 0; t < 4; ++t) { AsdaUi::SetShownDecor(B->RewardUi[t].Frame, true); }   // МЕЛК-12: рамки — для сверки с иконками
			Shot(TEXT("cells30_join"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			FBox2D Win(ForceInit), A(ForceInit), Z(ForceInit);
			const bool bWin = RectOfW(Node(B, TEXT("FISH_BOOK_WIN_0_0")), A) && RectOfW(Node(B, TEXT("FISH_BOOK_WIN_0_8")), Z);
			if (bWin) { Win = FBox2D(A.Min, Z.Max); }
			auto Inside = [&Win](const FBox2D& R) { return R.Min.X >= Win.Min.X - 0.5f && R.Min.Y >= Win.Min.Y - 0.5f && R.Max.X <= Win.Max.X + 0.5f && R.Max.Y <= Win.Max.Y + 0.5f; };
			auto Overlap = [](const FBox2D& P, const FBox2D& Q) { return P.Min.X < Q.Max.X - 0.5f && Q.Min.X < P.Max.X - 0.5f && P.Min.Y < Q.Max.Y - 0.5f && Q.Min.Y < P.Max.Y - 0.5f; };
			TArray<FBox2D> Cells; int32 NoGeo = 0, Out = 0, Ov = 0;
			for (int32 i = 0; i < 30; ++i) { FBox2D R(ForceInit); if (RectOfW(B->CellUi[i].Root, R)) { Cells.Add(R); Out += Inside(R) ? 0 : 1; } else { ++NoGeo; } }
			for (int32 i = 0; i < Cells.Num(); ++i) { for (int32 j = i + 1; j < Cells.Num(); ++j) { Ov += Overlap(Cells[i], Cells[j]) ? 1 : 0; } }
			Expect(bWin && NoGeo == 0 && Out == 0 && Ov == 0, FString::Printf(TEXT("O1 [%s] 30 ячеек сетки: геометрия есть, в окне, не пересекаются"), *Res),
				FString::Printf(TEXT("окно (%.0f,%.0f)-(%.0f,%.0f), без геометрии %d, вне окна %d, пересечений %d"), Win.Min.X, Win.Min.Y, Win.Max.X, Win.Max.Y, NoGeo, Out, Ov));
			if (Cells.Num() == 30)
			{
				const float Sx = (Cells[1].Min.X - Cells[0].Min.X), Sy = (Cells[5].Min.Y - Cells[0].Min.Y);
				const float W0 = Win.GetSize().X / LayoutWidth(B);   // масштаб окна (ширина разметки из слотов рамки; fishfix 09.10)
				Expect(FMath::Abs(Sx / W0 - 104.f) < 1.5f && FMath::Abs(Sy / W0 - 96.f) < 1.5f,
					FString::Printf(TEXT("O2 [%s] шаг сетки = эталонный (104, 96) разметки — 5 колонок × 6 рядов"), *Res), FString::Printf(TEXT("шаг (%.1f, %.1f)"), Sx / W0, Sy / W0));
			}
			TArray<FBox2D> Inv; int32 InvNo = 0, InvOv = 0, InvOut = 0;
			FBox2D Jb(ForceInit);
			const bool bJ = RectOfW(B->Join, Jb);
			for (int32 k = 0; k < 30; ++k) { FBox2D R(ForceInit); if (RectOfW(B->InvUi[k].Icon, R)) { Inv.Add(R); InvOut += (bJ && R.Min.X >= Jb.Min.X && R.Max.X <= Jb.Max.X && R.Min.Y >= Jb.Min.Y && R.Max.Y <= Jb.Max.Y) ? 0 : 1; } else { ++InvNo; } }
			for (int32 i = 0; i < Inv.Num(); ++i) { for (int32 j = i + 1; j < Inv.Num(); ++j) { InvOv += Overlap(Inv[i], Inv[j]) ? 1 : 0; } }
			Expect(bJ && Inside(Jb) && InvNo == 0 && InvOv == 0 && InvOut == 0, FString::Printf(TEXT("O3 [%s] окно 74200 в книге, 30 стопок в нём, не пересекаются"), *Res),
				FString::Printf(TEXT("без геометрии %d, пересечений %d, вне 74200 %d"), InvNo, InvOv, InvOut));
			TArray<FBox2D> Tabs; int32 TabOv = 0, TabOut = 0;
			for (int32 n = 0; n < 4; ++n) { FBox2D R(ForceInit); if (RectOfW(B->TabPlanes[n], R)) { Tabs.Add(R); TabOut += Inside(R) ? 0 : 1; } }
			for (int32 i = 0; i < Tabs.Num(); ++i) { for (int32 j = i + 1; j < Tabs.Num(); ++j) { TabOv += Overlap(Tabs[i], Tabs[j]) ? 1 : 0; } }
			FBox2D Dl(ForceInit), Lt(ForceInit);
			const bool bDl = RectOfW(B->DelPanel, Dl);
			const bool bLt = RectOfW(B->BtnLeft, Lt);
			Expect(Tabs.Num() == 4 && TabOv == 0 && TabOut == 0 && bLt && Tabs[3].Max.X <= Lt.Min.X + 0.5f,
				FString::Printf(TEXT("O4 [%s] 4 вкладки не пересекаются, в окне, левее стрелок 74106"), *Res), FString::Printf(TEXT("вкладок %d пересечений %d"), Tabs.Num(), TabOv));
			Expect(bDl && Inside(Dl) && Cells.Num() == 30 && Dl.Min.Y >= Cells[25].Max.Y - 0.5f,
				FString::Printf(TEXT("O5 [%s] панель 74270 «Удалить книгу» в окне, под сеткой (415,703)"), *Res),
				FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), Dl.Min.X, Dl.Min.Y, Dl.Max.X, Dl.Max.Y));
			// ★ МЕЛК-12: соответствие иконка ↔ номер награды — ГЕОМЕТРИЕЙ: рамка 74111+t (однозначное имя `MARK_A1…A4`) лежит ровно
			//   на иконке 74081+t (у 74083/74084 в `.JUI` один idc — порядок импорта тут не доказательство).
			{
				const float Sc = Scale(B);
				int32 Match = 0; FString Det;
				for (int32 t = 0; t < 4; ++t)
				{
					FBox2D Ic(ForceInit), Fr(ForceInit);
					const bool bG = RectOfW(B->RewardUi[t].Icon, Ic) && RectOfW(B->RewardUi[t].Frame, Fr);
					const bool bEq = bG && (Ic.Min - Fr.Min).GetAbsMax() <= 0.6f * Sc + 0.5f && (Ic.Max - Fr.Max).GetAbsMax() <= 0.6f * Sc + 0.5f;
					Match += bEq ? 1 : 0;
					Det += FString::Printf(TEXT(" [%d] (%.0f,%.0f)/(%.0f,%.0f)"), t, Ic.Min.X, Ic.Min.Y, Fr.Min.X, Fr.Min.Y);
				}
				Expect(Match == 4, FString::Printf(TEXT("O6 [%s] МЕЛК-12: рамка 74111+t ровно на иконке 74081+t (4 из 4) — иконки наград сопоставлены верно"), *Res), Det);
				// ★ ВАЖН-5: выравнивание/цвет, которые эталон ставит кодом, запечены в ассет шагом `asda_ui_fishbook_clones`.
				int32 StOk = 0; FString StDet;
				for (int32 k = 0; k < 30; ++k)
				{
					const UTextBlock* L = B->InvUi[k].Label;
					FBox2D Lr(ForceInit), Ir(ForceInit);
					const bool bG = L && RectOfW(L, Lr) && RectOfW(B->InvUi[k].Icon, Ir);
					const FLinearColor C = L ? L->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Black;
					// ★ 05.11.2026: шаг `asda_ui_item_count_outline` (26.10) дал числам обводку 1 px (у эталона она есть, ui_core.c:100239)
					//   и сдвиг отрисовки (0, −OutlineSize), чтобы ЗАЛИВКА цифры осталась на месте эталона; слот не менялся. Геометрия
					//   кэша включает сдвиг отрисовки ⇒ сверяем позицию СЛОТА: вычитаем его (Y = 18 — как было до обводки).
					const float TrY = L ? L->GetRenderTransform().Translation.Y : 0.f;
					const bool bOk = bG && JustOf(L) == ETextJustify::Center && C.Equals(FLinearColor(1.f, 1.f, 0.f, 1.f), 0.01f)
						&& FMath::Abs((Lr.Min.Y - Ir.Min.Y) / Sc - TrY - 18.f) < 1.f;
					StOk += bOk ? 1 : 0;
					if (k == 0)
					{
						// Диагностика (05.11.2026): после шага обводки чисел `asda_ui_item_count_outline` (26.10) бокс растёт на OutlineSize.
						const UCanvasPanelSlot* Cs = L ? Cast<UCanvasPanelSlot>(L->Slot) : nullptr;
						StDet = FString::Printf(TEXT("стопка 0: текст «%s», сдвиг вниз %.1f px разметки, низ %.1f, цвет (%.2f,%.2f,%.2f); обводка %d, сдвиг отрисовки (%.1f,%.1f), слот: авторазмер %d, выравнивание (%.2f,%.2f), позиция Y %.1f, размер Y %.1f"),
							*TextOf(L), bG ? (Lr.Min.Y - Ir.Min.Y) / Sc : -1.f, bG ? (Lr.Max.Y - Ir.Min.Y) / Sc : -1.f, C.R, C.G, C.B,
							L ? L->GetFont().OutlineSettings.OutlineSize : -1,
							L ? L->GetRenderTransform().Translation.X : 0.f, L ? L->GetRenderTransform().Translation.Y : 0.f,
							Cs ? (int32)Cs->GetAutoSize() : -1, Cs ? Cs->GetAlignment().X : -1.f, Cs ? Cs->GetAlignment().Y : -1.f,
							Cs ? Cs->GetPosition().Y : -1.f, Cs ? Cs->GetSize().Y : -1.f);
					}
				}
				Expect(StOk == 30, FString::Printf(TEXT("O7 [%s] ВАЖН-5: числа 30 стопок 74211…74240 — align 9 (низ строки 14 px, центр) и жёлтые FFFFFF00, как `SetField924(9)`/`SetField923(-256)`"), *Res),
					FString::Printf(TEXT("%d/30; %s"), StOk, *StDet));
				int32 RwOk = 0;
				for (int32 t = 0; t < 4; ++t)
				{
					const UTextBlock* Q = B->RewardUi[t].Qty;
					FBox2D Qr(ForceInit), Ir(ForceInit);
					const float TrQ = Q ? Q->GetRenderTransform().Translation.Y : 0.f;   // ★ 05.11.2026: сдвиг обводки — см. O7
					RwOk += (Q && RectOfW(Q, Qr) && RectOfW(B->RewardUi[t].Icon, Ir) && JustOf(Q) == ETextJustify::Center
						&& FMath::Abs((Qr.Min.Y - Ir.Min.Y) / Sc - TrQ - 50.f) < 1.f) ? 1 : 0;
				}
				Expect(RwOk == 4, FString::Printf(TEXT("O8 [%s] ВАЖН-5: количества наград 74081…74084 — align 9 (низ 64-px иконки, центр)"), *Res), FString::Printf(TEXT("%d/4"), RwOk));
				int32 NmOk = 0;
				for (int32 i = 0; i < 30; ++i)
				{
					const UTextBlock* Nm = B->CellUi[i].Name;
					const UTextBlock* Cn = B->CellUi[i].Cnt;
					FBox2D Nr(ForceInit), Nh(ForceInit), Cr(ForceInit), Ch(ForceInit);
					const bool bNm = Nm && RectOfW(Nm, Nr) && RectOfW(Node(B, *FString::Printf(TEXT("FISH_BOOK_CELL%02d_NAME_HIT"), i)), Nh)
						&& JustOf(Nm) == ETextJustify::Center && FMath::Abs((Nr.Min.Y - Nh.Min.Y) / Sc) < 1.f;
					const bool bCn = Cn && RectOfW(Cn, Cr) && RectOfW(Node(B, *FString::Printf(TEXT("FISH_BOOK_CELL%02d_CNT_HIT"), i)), Ch)
						&& JustOf(Cn) == ETextJustify::Left && FMath::Abs((Cr.Min.Y - Ch.Min.Y) / Sc) < 1.f;
					NmOk += (bNm && bCn) ? 1 : 0;
				}
				Expect(NmOk == 30, FString::Printf(TEXT("O9 [%s] ВАЖН-5: клоны 74021 (имя) — align 1 (верх, центр), 74026 (счёт) — align 0 (верх, лево), 30 из 30"), *Res),
					FString::Printf(TEXT("%d/30"), NmOk));
				Expect(B->JoinK && JustOf(B->JoinK) == ETextJustify::Center,
					TEXT("O10 контроль: текст вне перечня эталона (74204 «%d/%d», align 5 `.JUI`) шагом не тронут — центр"));
				// ★ 09.10.2026 (ревизия мультиязычности СРЕДН-1): обводка клонов. Эталон ставит стиль 0x800000 (обводка) КАЖДОМУ
				//   клону шаблонов 74021 имя / 74025 номер / 74026 счёт (`ui_core.c:100104/100124/100133`); шаг
				//   `asda_ui_text_style926` раньше не узнавал клоны и повторным прогоном снимал её у всех 90 (прежний самотест
				//   этого не ловил: ok=132). Живой шрифт узла — `OutlineSettings.OutlineSize == 1`.
				int32 OlOk = 0;       // клонов с обводкой 1
				FString OlBad;        // первые узлы без обводки (для деталей)
				for (int32 i = 0; i < 30; ++i)
				{
					const UTextBlock* const Tx[3] = { B->CellUi[i].Name, B->CellUi[i].No, B->CellUi[i].Cnt };   // 74021 / 74025 / 74026
					for (const UTextBlock* T : Tx)
					{
						const bool bOl = T && T->GetFont().OutlineSettings.OutlineSize == 1;
						OlOk += bOl ? 1 : 0;
						if (!bOl && OlBad.Len() < 160) { OlBad += (T ? T->GetName() : FString(TEXT("<нет>"))) + TEXT(" "); }
					}
				}
				Expect(OlOk == 90, TEXT("O11 СРЕДН-1: обводка у 90 клонов 74021/74025/74026 (имя/номер/счёт × 30 ячеек), как `ui_core.c:100104/100124/100133`"),
					FString::Printf(TEXT("%d/90 %s"), OlOk, *OlBad));
			}
			// P. ESC — штатный проход менеджера окон (`CloseTopmostOnEsc`).
			//   ★ 10.10.2026 (замок ввода, `owner_1010/modal_lock.md`): 74200 эталон показывает МОДАЛЬНО (`fishCollectionOpenWindow` →
			//   `helper_setupFields(g_pWindowMgr, 74200, 1)`), а `uiContainerHandleKey@0x12AF390` при модальном контроле смотрит только
			//   его; признака 0x228 у 74200 нет ⇒ ESC ничего не закрывает и не съедается. Ветка «сначала 74200» у
			//   `itemCollectUiShowOrToggle` достижима крестиком/ответом 6177, но не ESC. Прежнее поведение порта — ключом
			//   `asda.UI.ModalLock 0` (контрольная пара: тогда 74200 закрывается роспись книги).
			IConsoleVariable* CvLock = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.ModalLock"));
			const bool bE1 = S.WM->CloseTopmostOnEsc();
			const bool bP1 = B->IsJoinOpen() && B->IsOpen();
			if (CvLock) { AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalLock"), 0); }
			const bool bE2 = S.WM->CloseTopmostOnEsc();
			const bool bP2 = !B->IsJoinOpen() && B->IsOpen();
			if (CvLock) { AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalLock"), 1); }
			S.WM->NotifyModalStateChanged();
			const bool bE3 = S.WM->CloseTopmostOnEsc();
			Expect(CvLock && !bE1 && bP1 && bE2 && bP2 && bE3 && !B->IsOpen(),
				TEXT("P1 ESC: при 74200 (модальный контрол эталона) ничего не закрыто и не съедено; контроль `asda.UI.ModalLock 0` — ESC закрывает 74200; затем ESC закрывает книгу"),
				FString::Printf(TEXT("ESC1 съеден=%d, 74200 и книга на месте=%d; ключ 0: ESC2=%d, 74200 закрыт и книга на месте=%d; ESC3=%d книга=%d"),
					bE1 ? 1 : 0, bP1 ? 1 : 0, bE2 ? 1 : 0, bP2 ? 1 : 0, bE3 ? 1 : 0, B->IsOpen() ? 1 : 0));
			for (int32 t = 0; t < 4; ++t) { AsdaUi::SetShownDecor(B->RewardUi[t].Frame, false); }
			// ═══ T. Лента вкладок при > 4 томах (МЕЛК-10): тома 0…5 зарегистрированы → 4 вкладки, стрелки 74106/74107 ═══
			for (int32 i = 0; i < 6; ++i) { Feed(S, 6174, Page305(i, 0, { 1 }, { 10 }, { 10 })); }
			B->Open();
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			const FString N3 = UAsdaFishBookSubsystem::CaptionCut(B->St[3].Name, B->TabTexts[3], UAsdaFishBookSubsystem::kTabCaptionPad);
			Expect(B->ActiveNum == 6 && B->TabCount == 4 && AsdaUi::IsShown(B->TabTexts[3]) && TextOf(B->TabTexts[3]) == N3 && B->TabOfs == 0,
				TEXT("T1 6 томов → 4 вкладки (`gameUi_refreshPagedListBox` доращивает до 4), 4-я подписана томом 3, сдвиг 0"),
				FString::Printf(TEXT("томов %d вкладок %d «%s»"), B->ActiveNum, B->TabCount, *TextOf(B->TabTexts[3])));
			// ★ fishfix 09.10 (ревизия МЕЛК-4): подписи вкладок и наград. Ширина выведенной строки (замер Slate тем же шрифтом)
			//   против ширины поля в разметке: каждая обязана войти; имя, которое входит целиком, выводится целиком; не входящее —
			//   по ширине поля + «...» (`textTruncateWithEllipsis` эталона). Тома 1…3 («…, том N») обязаны войти ЦЕЛИКОМ — иначе
			//   вкладки снова неразличимы (окно расширено шагом `asda_ui_widen_windows`). Контроль: `asda.FishBook.Trunc13 1`.
			{
				const float Sc = FMath::Max(Scale(B), 0.01f);
				int32 Fit = 0, Ell = 0, Bad = 0; FString Det;
				// ★ fishfix2 (МЕЛК-1): Pad — внутреннее поле с каждой стороны: и целая, и обрезанная подпись обязаны входить в поле
				//   МИНУС 2·Pad (обрезанная не залезает на скругление скина вкладки).
				auto CheckCap = [&](UTextBlock* T, const FString& Full, float Pad, bool& bOutWhole)
				{
					bOutWhole = false;
					if (!T || !AsdaUi::IsShown(T)) { return; }
					const FString Tx = TextOf(T);
					if (Tx.IsEmpty()) { return; }
					FBox2D R(ForceInit);
					if (!RectOfW(T, R)) { ++Bad; Det += FString::Printf(TEXT(" «%s» нет геометрии;"), *Tx); return; }
					const float FieldW = R.GetSize().X / Sc - 2.f * Pad;   // полезная ширина (fishfix2 МЕЛК-1)
					const float TextW = AsdaUi::MeasureTextWidth(Tx, T->GetFont(), 0.f);
					const float FullW = AsdaUi::MeasureTextWidth(Full, T->GetFont(), 0.f);
					const bool bFits = TextW <= FieldW + 0.5f;
					bOutWhole = bFits && Tx == Full;
					// Целиком обязано быть всё, что целиком входит; обрезанное — только с «...».
					const bool bRight = bFits && (bOutWhole || (FullW > FieldW + 0.5f && Tx.EndsWith(TEXT("..."))));
					Fit += bOutWhole ? 1 : 0; Ell += (bRight && !bOutWhole) ? 1 : 0; Bad += bRight ? 0 : 1;
					Det += FString::Printf(TEXT(" «%s» %.0f/%.0f%s;"), *Tx, TextW, FieldW, bRight ? (bOutWhole ? TEXT("") : TEXT(" (по ширине)")) : TEXT(" ОШИБКА"));
				};
				bool bVols = true;
				for (int32 n = 0; n < 4; ++n)
				{
					const int32 V = n + B->TabOfs;
					const bool bName = V >= 0 && V < B->ActiveNum && B->St.IsValidIndex(B->ActiveList[V]);
					bool bW = false;
					CheckCap(B->TabTexts[n], bName ? B->St[B->ActiveList[V]].Name : FString(), UAsdaFishBookSubsystem::kTabCaptionPad, bW);
					if (n >= 1 && n <= 3) { bVols &= bW; }
				}
				for (int32 t = 0; t < 4; ++t)
				{
					bool bW = false;
					// Полное имя награды — по таблице предметов (тот же источник, что `RefreshRewards`); не обычный предмет — выведенное.
					const auto* Tp = B->TplAt(B->Page);
					const int32 Id = Tp ? Tp->Reward[t] : -1;
					const FString Full = (Id > 0 && S.IT && S.IT->GetItem(Id)) ? S.IT->GetItemName(Id) : TextOf(B->RewardUi[t].Name);
					CheckCap(B->RewardUi[t].Name, Full, UAsdaFishBookSubsystem::kRewardCaptionPad, bW);
				}
				// ★ 07.11.2026: EN-имена томов длиннее поля, «целиком» — условие только RU; `Bad` проверяет обрезку на любом языке.
				Expect(Bad == 0 && (!FAsdaLang::IsRu() || (bVols && Fit >= 3)), TEXT("U1 МЕЛК-4: подписи вкладок/наград входят в поле; что входит — целиком (тома «…, том 1/2/3»), длиннее поля — по ширине + «...»"),
					FString::Printf(TEXT("целиком %d, по ширине %d, ошибок %d:%s"), Fit, Ell, Bad, *Det));
				IConsoleVariable* CvT13 = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.FishBook.Trunc13"));
				if (CvT13) { AsdaCVarSave::SetForTest(TEXT("asda.FishBook.Trunc13"), 1); }
				B->RebuildTabs(-1);
				const FString L1 = TextOf(B->TabTexts[1]);
				if (CvT13) { AsdaCVarSave::SetForTest(TEXT("asda.FishBook.Trunc13"), 0); }
				B->RebuildTabs(-1);
				Expect(CvT13 && L1 == UAsdaFishBookSubsystem::Trunc13(B->St[B->ActiveList[1]].Name) && L1.EndsWith(TEXT("..."))
					&& TextOf(B->TabTexts[1]) == UAsdaFishBookSubsystem::CaptionCut(B->St[B->ActiveList[1]].Name, B->TabTexts[1], UAsdaFishBookSubsystem::kTabCaptionPad),
					TEXT("U2 контроль МЕЛК-4: asda.FishBook.Trunc13 1 — как оригинал «13 знаков + ...», 0 — снова целиком"),
					FString::Printf(TEXT("ключ 1 «%s», ключ 0 «%s»"), *L1, *TextOf(B->TabTexts[1])));
			}
			ClickW(W, B->BtnRight, TEXT("74107 стрелка вправо"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->TabOfs == 1 && TextOf(B->TabTexts[0]) == UAsdaFishBookSubsystem::CaptionCut(B->St[1].Name, B->TabTexts[0], UAsdaFishBookSubsystem::kTabCaptionPad) && TextOf(B->TabTexts[3]) == UAsdaFishBookSubsystem::CaptionCut(B->St[4].Name, B->TabTexts[3], UAsdaFishBookSubsystem::kTabCaptionPad),
				TEXT("T2 74107 → сдвиг ленты 1: вкладки показывают тома 1…4"), FString::Printf(TEXT("сдвиг %d «%s» … «%s»"), B->TabOfs, *TextOf(B->TabTexts[0]), *TextOf(B->TabTexts[3])));
			ClickW(W, B->TabButtons[3], TEXT("4-я вкладка (OPT3)"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->TabSel == 3 && B->Page == 4, TEXT("T3 клик по 4-й вкладке (клон шага ассета) при сдвиге 1 → том 4"), FString::Printf(TEXT("вкладка %d том %d"), B->TabSel, B->Page));
			ClickW(W, B->BtnRight, TEXT("74107 ещё раз"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->TabOfs == 2, TEXT("T4 74107 → сдвиг 2 (6 − 4 = 2 — предел)"), FString::Printf(TEXT("сдвиг %d"), B->TabOfs));
			ClickW(W, B->BtnRight, TEXT("74107 за предел"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->TabOfs == 2, TEXT("T5 контроль: 74107 за пределом — сдвиг остаётся 2"), FString::Printf(TEXT("сдвиг %d"), B->TabOfs));
			ClickW(W, B->BtnLeft, TEXT("74106 стрелка влево"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			Expect(B->TabOfs == 1, TEXT("T6 74106 → сдвиг 1"), FString::Printf(TEXT("сдвиг %d"), B->TabOfs));
		});
		// ═══ U5/U6 (fishfix2 09.10, ревизия МЕЛК-2): НАВЕДЕНИЕ на книгу в точке над кнопкой 73407 — подсказка панели «Магазин»
		//   не всплывает (тот же гейт, что у клика: эталон отдаёт мышь, и наведение тоже, только верхнему корню); контроль —
		//   `asda.SysMenu.LegacyClickThrough 1`: та же точка, подсказка кнопки снова показана (прежний порт). Наведение
		//   до чекбокса Slate доходит в обоих случаях (иначе пара ничего не доказывает). ═══
		Steps.Add([this](UWorld* W) { HoverSysMenuUnderBook(W, /*bAway*/true); });
		Steps.Add([this](UWorld* W) { HoverSysMenuUnderBook(W, /*bAway*/false); });
		Wait(1);
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			if (bSysSkip)
			{
				UE_LOG(LogAsdaFishBookTest, Log, TEXT("САМОТЕСТ-КНИГА пропуск U5/U6 [%s]: кнопка 73407 панели 73400 вне книги — перекрытия нет"), *Res);
				return;
			}
			const UAsdaSystemMenuSubsystem* SM = S.GI->GetSubsystem<UAsdaSystemMenuSubsystem>();
			const bool bHov = bHoverReached;
			const int32 Tip = SM ? FMath::Max(HoverTipSeen, SM->GetTipShownFor()) : -2;
			Expect(bSysUnderBook && bHov && Tip == -1 && S.Book->IsOpen(),
				TEXT("U5 МЕЛК-2: курсор на книге над кнопкой 73407 панели 73400 — подсказка панели не всплывает (наведение Slate до кнопки дошло)"),
				FString::Printf(TEXT("кнопка под книгой %d, наведение Slate %d, подсказка %d (ждём −1); хит-тест: %s"), bSysUnderBook ? 1 : 0, bHov ? 1 : 0, Tip, *HoverDiag));
			AsdaCVarSave::SetForTest(TEXT("asda.SysMenu.LegacyClickThrough"), 1);
			HoverSysMenuUnderBook(W, /*bAway*/false);
		});
		Wait(1);
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			const int32 Tip = HoverTipSeen;
			AsdaCVarSave::SetForTest(TEXT("asda.SysMenu.LegacyClickThrough"), 0);
			if (bSysSkip) { return; }
			Expect(Tip == 73407, TEXT("U6 контроль: asda.SysMenu.LegacyClickThrough 1 — та же точка, подсказка кнопки 73407 всплывает сквозь книгу (прежний порт)"),
				FString::Printf(TEXT("подсказка %d (ждём 73407)"), Tip));
			HoverSysMenuUnderBook(W, /*bAway*/true);
		});
		Wait(1);
		Steps.Add([this](UWorld* W)
		{
			// ═══ U3/U4 (fishfix 09.10, найдено проверкой МЕЛК-4): книга лежит над рядом кнопок панели 73400 (588…860 × 634…666 при
			//   1024×768). Клик по книге в точке над кнопкой 73407 «премиум-магазин» НЕ уходит в панель (эталон
			//   `uiWindowMgrDispatchMouse`: мышь только верхнему корню); контроль — `asda.SysMenu.LegacyClickThrough 1` (прежний порт). ═══
			ClickSysMenuUnderBook(W, TEXT("U3"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			const UAsdaSystemMenuSubsystem* SM = S.GI->GetSubsystem<UAsdaSystemMenuSubsystem>();
			const bool bStore = S.WM->IsWindowVisible(FName(TEXT("SHELLITEM_STORE_WIN")));
			const bool bChk = SM && SM->IsButtonChecked(73407);
			// Перекрытие книги и ряда кнопок панели — свойство экрана 1024×768 (панель прижата к правому низу); при другом
			//   разрешении кнопка может лежать вне книги — тогда пара не проверяется (пропуск с записью в журнал), при 1024×768 — обязана.
			bSysSkip = !bSysUnderBook && Res != TEXT("1024x768");
			if (bSysSkip)
			{
				UE_LOG(LogAsdaFishBookTest, Log, TEXT("САМОТЕСТ-КНИГА пропуск U3/U4 [%s]: кнопка 73407 панели 73400 вне книги — перекрытия нет"), *Res);
				return;
			}
			Expect(bSysUnderBook && !bStore && !bChk && B->IsOpen(),
				TEXT("U3 клик по книге над кнопкой 73407 панели 73400: панель команды не получает (магазин закрыт, кнопка не нажата), книга открыта"),
				FString::Printf(TEXT("кнопка под книгой %d, магазин %d, кнопка нажата %d, книга %d"), bSysUnderBook ? 1 : 0, bStore ? 1 : 0, bChk ? 1 : 0, B->IsOpen() ? 1 : 0));
			AsdaCVarSave::SetForTest(TEXT("asda.SysMenu.LegacyClickThrough"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.UI.TopWindowPress"), 0);   // общий гейт нажатий тоже снят: иначе прежнего поведения нет
			ClickSysMenuUnderBook(W, TEXT("U4"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			UAsdaSystemMenuSubsystem* SM = S.GI->GetSubsystem<UAsdaSystemMenuSubsystem>();
			const bool bStore = S.WM->IsWindowVisible(FName(TEXT("SHELLITEM_STORE_WIN")));
			const bool bChk = SM && SM->IsButtonChecked(73407);
			AsdaCVarSave::SetForTest(TEXT("asda.SysMenu.LegacyClickThrough"), 0);
			AsdaCVarSave::SetForTest(TEXT("asda.UI.TopWindowPress"), 1);
			if (bSysSkip) { return; }   // пропуск пары (см. U3)
			Expect(bStore || bChk, TEXT("U4 контроль: asda.SysMenu.LegacyClickThrough 1 — тот же клик проходит сквозь книгу в панель (прежний порт)"),
				FString::Printf(TEXT("магазин %d, кнопка нажата %d"), bStore ? 1 : 0, bChk ? 1 : 0));
			// Уборка: магазин закрыть, кнопку отжать, предупреждение магазина снять; книгу — открытой для S (её закрывает CloseOrToggle).
			// ★ 11.10.2026 (В3 S1, загрязнение пакета): магазин закрывается ШТАТНО (`CloseWindow` подсистемы: снимок окон и HUD
			//   возвращаются — хвост `uiInventoryResetAndLeave`). Прежнее закрытие одного окна оставляло игровые окна убранными —
			//   следующий самотест (окно 900) получал отказ «активна полноэкранная сцена» и падал на пустой странице.
			if (UAsdaPremiumShopSubsystem* Ps = S.GI->GetSubsystem<UAsdaPremiumShopSubsystem>(); Ps && Ps->IsOpen()) { Ps->CloseWindow(); }
			if (S.WM->IsWindowVisible(FName(TEXT("SHELLITEM_STORE_WIN")))) { S.WM->CloseWindow(FName(TEXT("SHELLITEM_STORE_WIN"))); }
			if (SM && SM->IsButtonChecked(73407)) { SM->ClickButton(73407); }
			S.WM->ClearMsgBoxQueue(TEXT("книга: U4"));
			if (!B->IsOpen()) { B->Open(); }
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			B->CloseOrToggle();
			// ═══ S. Сброс сессии (МЕЛК-10, МЕЛК-2) ═══
			S.WM->ClearMsgBoxQueue(TEXT("книга: S"));
			B->BookCell = kCellBook; B->ShowConfirm(79, TEXT("самотест: диалог 79 до портала"));
			B->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);
			const int32 RegE = RegCount(B);
			B->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			const int32 RegM = RegCount(B);
			Expect(RegE == 6 && RegM == 6 && S.WM->IsMsgBoxQueued(1, 79),
				TEXT("S1 вход в мир / смена карты: тома сохранены (ранние 6174 не теряются); диалог 79 в очереди НЕ снят (МЕЛК-2: оригинал очередь при портале не трогает)"),
				FString::Printf(TEXT("томов после входа %d, после портала %d, диалог 79 %d"), RegE, RegM, S.WM->IsMsgBoxQueued(1, 79) ? 1 : 0));
			S.WM->ClearMsgBoxQueue(TEXT("книга: S2"));
			B->ResetSessionState(EAsdaSessionResetReason::Disconnected);
			const int32 RegD = RegCount(B);
			for (int32 i = 0; i < 3; ++i) { Feed(S, 6174, Page305(i, 0, {}, {}, {})); }
			B->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			Expect(RegD == 0 && RegCount(B) == 0 && !B->bAwaitRegist && B->BookCell == -1,
				TEXT("S2 разрыв / выход в выбор персонажа: тома, «ждём 6177» и книга [469] очищены"),
				FString::Printf(TEXT("после разрыва %d, после выхода %d"), RegD, RegCount(B)));
		});
		BuildStepsFishBooks5to8();
	}

	// ═══ Q. ★ fishbooks_69682 (09.10.2026, решение владельца): 69682…69685 — книги рыбака томов 5…8 (подтип 210), как в
	//   таблицах клиента; сервер приведён к клиенту staging-пакетом `AstoryServer/Docs/fishbooks_69682`, имена —
	//   рецептом имён (source=owner). Отчёт `Reports/owner_1009/fishbooks_69682.md`.
	//   Q1 данные: тип 11 / подтип 210 / LinkCode = том; имя русское = имя тома; книга тома в ОБОИХ наборах (Р12).
	//   Q2 описание 69685 русское (без латиницы), «Инфероса» — как у 69682…69684.
	//   Q3 контроль: 69686 (леска, подтип 204) — развилка книги отказывает, диалога 79 нет, 6176 нет.
	//   Q4 FSM 101: книга тома 5 в сумке, тома не зарегистрированы → диалог 79, книга [469] = её ячейка.
	//   Q5…Q7 ×4 путь игрока: использование из окна сумки → диалог 79 (111018: русское имя книги, «Рыбак») → «ОК» кнопкой →
	//   РОВНО ОДИН 6176 (слот книги) → 6177 код 1 с томом 5…8 → том зарегистрирован, книга ушла из сумки.
	void BuildStepsFishBooks5to8()
	{
		using namespace AsdaFishBookTestLocal;
		static const int32 kInfBook[4] = { 69682, 69683, 69684, 69685 };   // книги-предметы томов 5…8
		static const TCHAR* kInfName[4] = { TEXT("Рыбалка в Инферно, том 1"), TEXT("Рыбалка в Инферно, том 2"),
			TEXT("Рыбалка в Инферно, том 3"), TEXT("Рыбалка в Инферно для опытных") };   // ожидаемые имена (= имена томов)
		static constexpr int32 kInfCell = 7;   // ячейка сумки для книги тома 5…8
		static constexpr int32 kLineCell = 8;  // ячейка лески 69686 (контроль P3)
		static constexpr int32 kLine = 69686;  // леска — подтип 204 (контроль)
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			UAsdaFishBookSubsystem* B = S.Book;
			S.WM->ClearMsgBoxQueue(TEXT("книга: Q"));
			B->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			SetCVar(TEXT("asda.FishBook.ClientTable"), 0); B->EnsureData();
			for (int32 k = 0; k < 4; ++k)
			{
				const int32 Code = kInfBook[k], Tome = 5 + k;
				const FAsdaItemRecord* R = S.IT->GetItem(Code);
				const FString Nm = S.IT->GetItemName(Code);
				const bool bSrv = B->Tpl.IsValidIndex(Tome) && B->Tpl[Tome].BookItemId == Code;
				SetCVar(TEXT("asda.FishBook.ClientTable"), 1); B->EnsureData();
				const bool bCli = B->Tpl.IsValidIndex(Tome) && B->Tpl[Tome].BookItemId == Code;
				SetCVar(TEXT("asda.FishBook.ClientTable"), 0); B->EnsureData();
				// ★ 07.11.2026: «имя книги = имя тома» — решение владельца для RU. EN-донор зовёт книгу Fishing Log, а том Fish Guide.
				bool bCyr = false;
				for (TCHAR Ch : Nm) { bCyr |= Ch >= 0x0400 && Ch <= 0x04FF; }
				const bool bNameOk = FAsdaLang::IsRu() ? (Nm == kInfName[k] && B->St[Tome].Name == Nm) : (!Nm.IsEmpty() && !bCyr && !B->St[Tome].Name.IsEmpty());
				Expect(R && R->ItemType == 11 && R->ItemSubType == 210 && R->LinkCode == Tome && bNameOk && bSrv && bCli,
					*FString::Printf(TEXT("Q1 %d: тип 11 / подтип 210 / том %d; имя «%s» = имя тома; книга тома и в серверном, и в клиентском наборе"), Code, Tome, kInfName[k]),
					FString::Printf(TEXT("тип %d подтип %d том %d имя «%s» том «%s» сервер %d клиент %d"), R ? R->ItemType : -1, R ? R->ItemSubType : -1,
						R ? R->LinkCode : -1, *Nm, B->St.IsValidIndex(Tome) ? *B->St[Tome].Name : TEXT("—"), bSrv ? 1 : 0, bCli ? 1 : 0));
			}
			FAsdaItemTooltipRow T85, T84;
			const bool bT = S.IT->GetItemTooltip(69685, T85) && S.IT->GetItemTooltip(69684, T84);
			bool bLatin = false, bCyrDesc = false;
			for (TCHAR Ch : T85.Description) { bLatin |= ((Ch >= TEXT('A') && Ch <= TEXT('Z')) || (Ch >= TEXT('a') && Ch <= TEXT('z'))); bCyrDesc |= Ch >= 0x0400 && Ch <= 0x04FF; }
			const bool bDescOk = FAsdaLang::IsRu() ? (!bLatin && T85.Description.Contains(TEXT("Инфероса")) && T84.Description.Contains(TEXT("Инфероса")))
				: (!bCyrDesc && T85.Description.Contains(TEXT("Inferno")) && T84.Description.Contains(TEXT("Inferno")));
			Expect(bT && bDescOk,
				TEXT("Q2 описание 69685 русское (латиницы нет), топоним «Инфероса» как у 69684"), T85.Description);
			// Q3 контроль: леска 69686 (подтип 204) — не книга. Развилка книги (`OnUseBookItem`, её зовёт ветка расходников окна
			//   сумки) проверяется напрямую: `UseFromBagWindow` для наживки отправил бы 5000/5028 и защёлкнул гейт `sub_93723F`
			//   (ответа офлайн нет) — следующие использования книг молча гасли бы.
			Caps->Reset();
			S.Inv->DebugPutItem(2, kLineCell, kLine, 1);
			const bool bLineBook = B->OnUseBookItem(2, kLineCell);
			Expect(!bLineBook && !S.WM->IsMsgBoxQueued(1, 79) && Count(6176) == 0, TEXT("Q3 контроль: 69686 (подтип 204) — развилка книги отказывает, диалога 79 нет, 6176 нет"),
				FString::Printf(TEXT("развилка %d, диалог %d, 6176 ×%d"), bLineBook ? 1 : 0, S.WM->IsMsgBoxQueued(1, 79) ? 1 : 0, Count(6176)));
			S.Inv->DebugPutItem(2, kLineCell, 0, 0);
			S.WM->ClearMsgBoxQueue(TEXT("книга: Q4"));
			// P4 FSM 101: книга тома 5 — единственная книга в сумке.
			S.Inv->DebugPutItem(2, kInfCell, kInfBook[0], 1);
			const bool bP = B->BookRegisterProbe();
			Expect(bP && S.WM->IsMsgBoxQueued(1, 79) && B->BookCell == kInfCell,
				TEXT("Q4 FSM 101 (серверный набор): книга тома 5 (69682) в сумке → диалог 79, книга [469] = её ячейка"),
				FString::Printf(TEXT("проба %d диалог %d книга %d"), bP ? 1 : 0, S.WM->IsMsgBoxQueued(1, 79) ? 1 : 0, B->BookCell));
			// ★ 09.10.2026: ветка FSM 101 — тот же текст, что и при использовании из сумки: [имя книги] … [своё имя].
			const FString Nm0 = S.IT->GetItemName(kInfBook[0]);   // ★ 07.11.2026: имя на языке сессии (Q1 сверяет его с RU)
			Expect(BracketArg(TopText(S.WM), 0) == Nm0 && BracketArg(TopText(S.WM), 1) == kSelfName && TopText(S.WM) == Want(S.GI, 111018, { Nm0, kSelfName }),
				TEXT("Q4b FSM 101 → текст 111018: первый [..] — имя предмета «Рыбалка в Инферно, том 1», второй — «Рыбак»"), TopText(S.WM));
			S.WM->ClearMsgBoxQueue(TEXT("книга: Q5"));
			B->BookCell = -1;
		});
		for (int32 k = 0; k < 4; ++k)
		{
			Steps.Add([this, k](UWorld* W)
			{
				FSubs S = Get(W); if (!S.Ok()) { return; }
				S.WM->ClearMsgBoxQueue(TEXT("книга: Q5"));
				S.Inv->DebugPutItem(2, kInfCell, kInfBook[k], 1);
				Caps->Reset();
				S.Inv->UseFromBagWindow(2, kInfCell);
				const FString NmK = S.IT->GetItemName(kInfBook[k]);   // ★ 07.11.2026: имя на языке сессии
				const FString Exp = Want(S.GI, 111018, { NmK, kSelfName });   // ★ 09.10: предмет первым, игрок вторым
				Expect(S.WM->IsMsgBoxQueued(1, 79) && TopText(S.WM) == Exp && BracketArg(TopText(S.WM), 0) == NmK && BracketArg(TopText(S.WM), 1) == kSelfName && Count(6176) == 0,
					*FString::Printf(TEXT("Q5 %d из сумки → диалог 79 (111018: «%s», «Рыбак»), пакета ещё нет"), kInfBook[k], kInfName[k]), TopText(S.WM));
				if (k == 0) { Shot(TEXT("fishbook69682_dialog79")); }
			});
			Steps.Add([this](UWorld* W)
			{
				FSubs S = Get(W); if (!S.Ok()) { return; }
				ClickW(W, MsgNode(S.WM, TEXT("TWOBUT_BUT00")), TEXT("«ОК» диалога 79 (книга тома 5…8)"));
			});
			Steps.Add([this, k](UWorld* W)
			{
				FSubs S = Get(W); if (!S.Ok()) { return; }
				UAsdaFishBookSubsystem* B = S.Book;
				const int32 Tome = 5 + k;
				FCap C2;
				const int32 N2 = Count(6176, &C2);
				Expect(N2 == 1 && C2.P.Num() == 15 && AsdaBytes::RdI32(C2.P.GetData()) == kCharId && AsdaBytes::RdI16(C2.P.GetData() + 4) == kInfCell
					&& C2.P[8] == 2 && B->bAwaitRegist,
					*FString::Printf(TEXT("Q6 %d: «ОК» → РОВНО ОДИН 6176 (charId, слот %d, контейнер 2), «ждём 6177»"), kInfBook[k], kInfCell),
					FString::Printf(TEXT("×%d, %d б"), N2, C2.P.Num()));
				Feed(S, 6177, Resp365(1, 55, Rec57(kInfBook[k], 2, kInfCell, -1), Page305(Tome, 0, {}, {}, {})));
				Expect(B->St[Tome].bReg && !B->bAwaitRegist && BagAmount(S.Inv, kInfCell) == -1,
					*FString::Printf(TEXT("Q7 6177 код 1 → том %d «%s» зарегистрирован, книга ушла из сумки"), Tome, kInfName[k]),
					FString::Printf(TEXT("рег %d ждём %d сумка %d"), B->St[Tome].bReg ? 1 : 0, B->bAwaitRegist ? 1 : 0, BagAmount(S.Inv, kInfCell)));
				S.WM->ClearMsgBoxQueue(TEXT("книга: Q7"));
			});
		}
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			S.Book->ResetSessionState(EAsdaSessionResetReason::LeftWorld);   // тома 5…8 сняты (дальше — восстановление)
		});
	}

	// Восстановление.
	void Finish(UWorld* W)
	{
		using namespace AsdaFishBookTestLocal;
		FSubs S = Get(W);
		if (!S.Ok()) { AsdaCVarSave::RestoreAll(TEXT("asda.FishBook.SelfTest")); return; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и на этом выходе
		AsdaSelfTestKit::DisarmCapture(S.Net, Gen);
		if (S.WM->IsNumPadOpen()) { S.WM->CloseNumPad(false); }
		S.Book->CloseOrToggle(); S.Book->CloseOrToggle();
		S.Book->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		for (int32 i = 0; i < 60; ++i)
		{
			S.Inv->DebugPutItem(2, i, 0, 0);
			if (FAsdaItemInstance* It = S.Inv->GetCellMutable(2, i)) { if (BagWas.IsValidIndex(i) && BagWas[i].Num() >= 57) { It->SetRaw(BagWas[i].GetData(), BagWas[i].Num()); } }
		}
		if (UUserWidget* Bag = BagShifted.Get()) { Bag->SetRenderTranslation(FVector2D::ZeroVector); }
		if (S.Inv->IsWindowOpen()) { S.Inv->CloseWindow(); }
		if (FAsdaItemInstance* It = S.Inv->GetCellMutable(3, 10)) { if (Eq10Was.Num() >= 57) { It->SetRaw(Eq10Was.GetData(), Eq10Was.Num()); } else { It->Clear(); } }
		// Автомат рыбалки (6172 шага N/M4): в 0; навык — как был (6164 штатным разбором).
		S.Fish->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		if (bSkillKnownWas) { TArray<uint8> B4; B4.SetNumZeroed(4); AsdaBytes::WrI32(B4.GetData(), SkillWas); S.Net->DebugFeedPacket(6164, B4); }
		AsdaCVarSave::RestoreAll(TEXT("asda.FishBook.SelfTest"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): все ключи прогона — значение + SetBy
		S.Net->SelectedCharId = CharIdWas;
		S.Net->SetSelfWeight(WeightWas, -1, TEXT("самотест книги: восстановление"));
		S.WM->ClearMsgBoxQueue(TEXT("самотест книги: конец"));
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaFishBookSelfTest(
		TEXT("asda.FishBook.SelfTest"),
		TEXT("asda.FishBook.SelfTest [delay=N] [quit] [shot] — ОФЛАЙН-самотест книги рыбака 74000 / окна 74200 / нумпада 10: данные (Р12), 6174…6183, ")
		TEXT("6176/6178/6180/6182 байт в байт, клики по живой геометрии, Р6/Р7, предмет 210 → диалог 79, FSM 101, геометрия"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			// ★ 11.10.2026 (В3 S1): аргументы, офлайн-гейт со вторым барьером и итог отказа с `quit` — общий каркас.
			const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
			const float Delay = KA.Delay;
			const bool bQuit = KA.bQuit;
			const bool bShotArg = KA.Has(TEXT("shot"));
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.FishBook.SelfTest"), KA)) { return; }
			AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.FishBook.SelfTest"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, bShotArg](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, TEXT("asda.FishBook.SelfTest"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
				// Свой персонаж офлайн (имя для 111018): штатные крюки, как у самотеста В1.
				// ★ 11.10.2026 (В3 S1, загрязнение пакета): павн уже есть от прошлого самотеста, но с ДРУГИМ именем — 111018
				//   сверялась бы с чужим именем (в пакете «[]»). Спавн с известным именем и тогда (тот же крюк, что у одиночного прогона).
				if (AsdaSelfTestKit::NeedSelfSpawn(W2, AsdaFishBookTestLocal::kSelfName) && GEngine)
				{
					GEngine->Exec(W2, TEXT("Asda.Load.CurrentMap 3"));
					// ★ МЕЛК-9 ревизии В3: ИЗВЕСТНОЕ имя своего персонажа — L1 сверяет 111018 с константой, а не с тем же геттером.
					GEngine->Exec(W2, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 139 77 %s"), AsdaFishBookTestLocal::kSelfName));
				}
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, bShotArg](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { return false; }
					TSharedRef<FAsdaFishBookSelfTest> T = MakeShared<FAsdaFishBookSelfTest>();
					T->bShot = bShotArg;
					T->Setup(W3);
					T->BuildSteps();
					TSharedRef<int32> Idx = MakeShared<int32>(0);
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit](float) -> bool
					{
						UWorld* W4 = WW.Get();
						if (!W4) { AsdaCVarSave::RestoreAll(TEXT("asda.FishBook.SelfTest")); return false; }   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернуть и на этом выходе
						if (*Idx < T->Steps.Num()) { T->Steps[(*Idx)++](W4); return true; }   // (★ R5-fix8-c (14.11.2026): шаг-ожидание четвёртой записи доли b снят вместе с её ожиданием в M4k)
						T->Finish(W4);
						UE_LOG(LogAsdaFishBookTest, Log, TEXT("САМОТЕСТ-КНИГА ИТОГ [%s]: ok=%d fail=%d"), *T->Res, T->Ok, T->Fail);
						AsdaSelfTestKit::Quit(W4, bQuit);
						return false;
					}), 0.4f);
					return false;
				}), 3.0f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaFishBookTest, Log, TEXT("САМОТЕСТ-КНИГА: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
