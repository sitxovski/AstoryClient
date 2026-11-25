// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPossessScene.h — «ОДЕРЖИМОСТЬ» (possessed): персонаж, которым поделился соулмейт, на экране
//   ВЫБОРА ПЕРСОНАЖА. Здесь лежат ДВЕ вещи, которым не место в теле AAsdaLoginGameMode:
//     1) `FAsdaPossessData` — чистые правила без состояния: имя ПАРНОГО клипа по паре полов
//        (эталон `CEntity_LoadCreatureMorphMotions@0xF05BF0`) и сборка текста панели 114
//        `SOULINFO_WIN` из таблицы `DT_soulsoulmatetext` по полю `Type`;
//     2) `UAsdaScenePickCatcher` — полноэкранный ловец кликов по 3D-сцене под UI char-select.
//
// ★ ПОЧЕМУ ЛОВЕЦ, А НЕ `FInputModeGameAndUI`. Логин-экраны порта работают в `FInputModeUIOnly`
//   ОСОЗНАННО: в режиме GameAndUI поверх 3D-вьюпорта клики по кнопкам UMG терялись (см. коммент
//   в `AAsdaLoginGameMode::StartPlay`). Возвращать GameAndUI ради пика по персонажу = чинить
//   решённый дефект заново. Поэтому клик по сцене ловит обычный UMG-виджет, лежащий ПОД всеми
//   окнами (ZOrder 1): по кнопкам панели клик, как и раньше, забирает кнопка, а всё, что мимо
//   UI, доходит сюда и уходит в луч по эталонному правилу.
//
// Эталон тракта (сверено своими глазами, отчёт `fix_possess_scene.md`):
//   `charSelect_OnClickPick@0xF0AF00` — клик, `scene_handleLeftClickPick@0xF0A680` — наведение,
//   `charDealWndRefreshInfo@0xF086F0` — какая панель показана, `uiStatusPanel_ShowByMode@0xF083F0` —
//   режим панели 114, `charDealSendReq@0xF088E0` — вход в мир (1016, байт[0] = режим слота).
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AsdaPossessScene.generated.h"

// ★ ОБЩАЯ ТОЧКА базы пути ассетов превью-персонажа — `kAsdaCharPreviewBaseM/F` — живёт в
//   `World/AsdaPlayerCharacter.h` (владелец скелета — мировой персонаж; логин им пользуется).
//   Раньше константы стояли ЗДЕСЬ, но за пределами `Login` о них не знали и держали ещё четыре
//   копии литерала в `World/`; 02.09.2026 сведено в одну точку — см. комментарий там.

// ═══════════════════════ ПРАВИЛА БЕЗ СОСТОЯНИЯ ══════════════════════════════════════════════════
struct ASDAREBORN_API FAsdaPossessData
{
	// ── ПАРНЫЕ КЛИПЫ ───────────────────────────────────────────────────────────────────────────
	// Эталон `CEntity_LoadCreatureMorphMotions@0xF05BF0` (`RE/etalon/file_vfs.c:1489-1556`) грузит
	//   персонажу соулмейта РОВНО 5 клипов, а имя набора выбирает по ДВУМ полам сразу:
	//     мой M + его M → `cre_cremmman-a_00_{0..4}.jbb`
	//     мой M + его Ж → `cre_cremwwoman-a_00_{0..4}.jbb`
	//     мой Ж + его M → `cre_crewmman-a_00_{0..4}.jbb`
	//     мой Ж + его Ж → `cre_crewwwoman-a_00_{0..4}.jbb`
	//   То есть имя = `cre_cre` + буква МОЕГО пола + буква ЕГО пола + слово по ЕГО полу
	//   (`man`/`woman`). Все 20 файлов физически лежат в `Data\character\Motion\CharMotion.baz`
	//   (проверено `zipfile` на месте).
	//
	// ★★★ ЗАЧЕМ ЭТО ВООБЩЕ НУЖНО (доказано данными, а не догадкой). В эталоне персонажа соулмейта
	//   НИКТО не позиционирует: `channelSelectRender3D@0xF07BE0` ставит матрицу только МОЕМУ
	//   объекту (`posSetWithChildOffset`) и глифу пустого слота; поиск по всему срезу даёт ровно
	//   3 обращения к массиву `+1655664` — разбор 1023, разбор 1024 и `playerFindSubRecordByType`.
	//   Смещение «встал рядом» приходит из КОРНЕВОЙ КОСТИ САМОГО КЛИПА: у моего
	//   `cre_cremmy-a_00_0` корень в (-9.17, 82.65, 3.97), у парного `cre_cremmman-a_00_0` — в
	//   (15.82, 82.56, -35.19) на той же базе. Разница и есть «стоят рядом на одной подставке».
	//   ⇒ БЕЗ парного клипа партнёра ставить некуда: он встанет ровно в моего персонажа.
	//   Поэтому загрузка клипа — обязательное условие спавна, а не украшение.
	//
	// Буква пола: 1 = мужской, любое другое значение = женский (эталон сравнивает именно с 1).
	static bool IsMale(uint8 Gender) { return Gender == 1; }

	// Оригинальное имя файла клипа (для логов и для скрипта экспорта; в рантайме не грузится).
	static FString PairClipSourceName(bool bMyMale, bool bHisMale, int32 ClipIdx);

	// Индекс ассета `…previewanim_%02d` в порту. ★ ПОРТ-СОГЛАШЕНИЕ (нумерация занята: 0..28, 35,
	//   40..97, 100..204), парные клипы кладутся в свободный блок 205..224 по 5 на пару полов:
	//   MM 205..209 · MЖ 210..214 · ЖM 215..219 · ЖЖ 220..224.
	//   Возвращает -1 при ClipIdx вне 0..4.
	static int32 PairClipAnimIndex(bool bMyMale, bool bHisMale, int32 ClipIdx);

	// Полный путь ассета клипа. Скелет — ПАРТНЁРА (у `cre_cremwwoman` корень `G` и 54 кости —
	//   это женский набор; у `cre_cremmman` корень `M` и 50 костей — мужской), поэтому база пути
	//   выбирается по ЕГО полу, а не по моему.
	static FString PairClipAssetPath(bool bMyMale, bool bHisMale, int32 ClipIdx);

	// ── «ДУША» (голубой силуэт) ────────────────────────────────────────────────────────────────
	// Эталон держит на слот ТРИ объекта `CCharacter`; третий (`+946128`) — заглушка «здесь будет
	//   стоять персонаж твоего Друга». Она включается ровно тогда, когда у слота НЕТ записи
	//   соулмейта (`charDealWndSetSlotEnable@0xF08560`, `entity_char.c:51653-51663`), и именно
	//   наведение на неё даёт режим 1 панели 114.
	//   Модели — `cre_cresoulman/woman-a_00.jbm` + текстура `silhoutte_tex_blue.TEX` (строки на
	//   VA 0x14368E8 / 0x14368C8 / 0x14368AC, xref из `.text` 0xF054CD / 0xF054DF / 0xF0571C);
	//   клипы — `CEntity_LoadCreatureMorphModels_Cremmman@0xF05930` (`file_vfs.c:1447-1486`):
	//     мой пол М → `cre_cremmman-a_00_{0..3}`   + `cre_cremmsoul-a_00_4`
	//     мой пол Ж → `cre_crewwwoman-a_00_{0..3}` + `cre_crewwsoul-a_00_4`
	//   ★ Набор выбирает МОЙ пол (в эталоне это аргумент `a3` той же функции), партнёра здесь нет.
	//   В порту силуэт — самостоятельный скиннутый ассет `cre_cresoul{man|woman}_preview`
	//   (экспорт `Tools/export_char_possess.py`, импорт `Tools/import_char_possess.py`), клипы
	//   лежат в нём же как `anim_00..04`, то есть индекс клипа = состояние объекта 0..3.
	//   ★★★ `bOppositeSex` — ОТСТУПЛЕНИЕ ПО РЕШЕНИЮ ВЛАДЕЛЬЦА (02.09.2026): силуэт-заглушка
	//     показывается ПРОТИВОПОЛОЖНЫМ полом (мужчине — «подруга», женщине — «друг»). Тогда берётся
	//     набор смешанной пары `cre_cresoulmwwoman_preview` / `cre_cresoulwmman_preview`, собранный
	//     из АВТОРСКИХ клипов `cre_cremwwoman`+`cre_cremwsoul` / `cre_crewmman`+`cre_crewmsoul`
	//     (они есть в архиве, но эталонный загрузчик их не открывает). `false` = наборы эталона.
	//     Полное обоснование, замер зазора и ключ отката — в `AAsdaLoginGameMode::SpawnSoulInSlot`.
	static FString SoulMeshAssetPath(bool bMyMale, bool bOppositeSex);
	static FString SoulClipAssetPath(bool bMyMale, bool bOppositeSex, int32 ClipIdx);

	// ── ТЕКСТ ПАНЕЛИ 114 `SOULINFO_WIN` ────────────────────────────────────────────────────────
	// Эталон читает `soulsoulmatetext.bin` (25 записей по 204 б, тег `SST`, поля `i32 Type` +
	//   `char[200] Text`) и СКЛЕИВАЕТ строки ОДНОГО типа подряд (`std::string::operator+=`):
	//     тип 0 → контрол 115 `SOULINFO_TXT00` («здесь будет стоять персонаж твоего соулмейта»),
	//     тип 1 → контрол 116 `SOULINFO_TXT01` (в нашей сборке записей типа 1 НЕТ — строка пуста),
	//     тип 2 → контрол 117 `SOULINFO_TXT02` (объяснение состояния «одержимости»).
	//   Текст берётся ТОЛЬКО из таблицы `DT_soulsoulmatetext` (закон «данные — из таблиц»);
	//   в коде нет ни одной игровой строки. Возвращает СЫРУЮ строку с тегами `<FC=..>`/`</F>` —
	//   форматирование делает `UAsdaRichText::ApplyFormatted`.
	//   ★ 02.09.2026: таблица была ПОБИТА (25 записей, но всего 2 уникальные строки — панель
	//   показывала один и тот же обрывок 8 и 17 раз). Причина не в порту: вырожден сам
	//   `soulsoulmatetext.bin` в основной сборке клиента. Источник ЭТОЙ ОДНОЙ таблицы
	//   переопределён на русскую сборку «Фантазиум» в реестре форматов
	//   (`Tools/decode_item_db.py`, ключ `src_bbz`), формат записи там тот же.
	static FString SoulInfoText(const class UGameInstance* GI, int32 Type);
};

// Кто получает клик по 3D-сцене (владелец — режим логина). Объявлен ВНЕ класса: UHT разбирает тело
//   `UCLASS`, и макросы объявления делегатов там лучше не держать.
DECLARE_DELEGATE(FOnAsdaSceneClicked);

// ═══════════════════════ ЛОВЕЦ КЛИКОВ ПО 3D-СЦЕНЕ ══════════════════════════════════════════════
// Пустой полноэкранный виджет (ZOrder ниже всех окон char-select). Ничего не рисует; его
//   единственная работа — принять клик, не съеденный UI, и позвать `OnSceneClicked`.
//   ⚠ Видимость должна быть `Visible` (не `SelfHitTestInvisible`), иначе он мышь не ловит —
//   закон `selfhittest-vs-hittest-invisible`.
UCLASS()
class ASDAREBORN_API UAsdaScenePickCatcher : public UUserWidget
{
	GENERATED_BODY()
public:
	// Кто получает клик (привязывается владельцем через BindUObject, снимается в его EndPlay).
	FOnAsdaSceneClicked OnSceneClicked;

	// ★ Корень дерева ставится КОДОМ: у виджета нет ассета-«блюпринта», а виджет без корня рискует
	//   не попасть в хит-тест-сетку Slate. Корень — полностью прозрачная рамка на весь слот:
	//   она ничего не рисует (альфа 0), но геометрию и хит-тест даёт гарантированно.
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
};
