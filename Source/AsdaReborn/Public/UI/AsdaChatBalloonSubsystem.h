// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaChatBalloonSubsystem — ОБЛАЧКО ЧАТА НАД ГОЛОВОЙ (порт класса «TalkBox» оригинала 1:1).
//
// ═══════════════════════════ ЭТАЛОН (сборка IDB 13 344 768, RE/etalon) ═════════════════════════
// В оригинале это ОТДЕЛЬНЫЙ маленький класс на 48 байт (vtable off_143BBF8), а не часть плашки имени.
// Три его метода и раскладка полей (ui_core.c):
//   ctor                       @0xF226F0  — обнуление; +40 (WORD) = 5000 мс жизни; +44 = bAutoHide = 1
//   uiWnd_BuildTripleChildWindow@0xF22A50 — КЛОНИРУЕТ три JUI-контрола по baseId и запоминает их
//                                            «шаблонные» размеры (+16/+20 = размер текста, +24/+28 = окна)
//   uiShowTooltipPlate         @0xF228E0  — показать: стиль шрифта, перенос+ресайз, позиция, цвет, t0 = now
//   uiWrapTooltipText          @0xF22D10  — ПЕРЕНОС СТРОК и пересчёт размеров окна/текста (см. ниже)
//   uiTargetPlateLayoutCenter  @0xF23660  — центровка окна по X и посадка «хвостика» под окно
//   uiControlAutoHideTimed     @0xF22C60  — таймер: now − t0 >= 5000 мс ⇒ спрятать
//   uiShowChildControl         @0xF23790  — показать снова (если не «погашено» таймером)
//   sub_F237E0                 @0xF237E0  — спрятать (bActive=0, SetVisible(0); при !bAutoHide bSuppressed=1)
//   uiTooltipPlateHide         @0xF22800  — полный сброс (используется при удалении сущности)
// Раскладка объекта: +0 vtable, +4 окно, +8 хвостик, +12 текст, +16/+20 размер-шаблон текста,
//   +24/+28 размер-шаблон окна, +32 t0 показа, +36 t «пинга» (пишется, нигде не читается),
//   +40 (u16) длительность = 5000, +42 bActive, +43 bSuppressed, +44 bAutoHide.
//
// ── ТРИ JUI-ШАБЛОНА (gamemain_jui.ini; ★ ИМЕННО baseId, baseId+1, baseId+2) ─────────────────────
//   вариант 0 → 9010 TALKBOXW_WIN («TalkBox1»)      + 9011 TALKBOXW_TAB   + 9012 TALKBOXW_TXT
//   вариант 1 → 9013 TALKBOXB_WIN («TalkBox2»)      + 9014 TALKBOXB_TAB   + 9015 TALKBOXB_TXT
//   вариант 2 → 9016 TALKBOX_Mon_WIN («TalkBox_Black») + 9017 TALKBOX_Mon_Tag + 9018 TALKBOX_Mon_Txt
//   Все три Visible=0, NoSelect=1, Size 36×38; хвостик 23×19 (Origin 3,38); текст Origin 7,7 Size 20×20.
//   Скины (JuiSkin.ini, тип ResizeWindow = 9 планов 3×3, углы 9×9, атлас NE_wheeltaget2.tga):
//     TalkBox1     Diffuse FFFFFFFF — БЕЛОЕ облачко   (текст по умолчанию FF000000)
//     TalkBox2     Diffuse FFFFD21B — ЗОЛОТОЕ облачко (текст по умолчанию FF222222)
//     TalkBox_Black Diffuse FF000000 — ЧЁРНОЕ облачко (текст по умолчанию FFFF3728)
//   В порт они уже импортированы 1:1 (Tools/asda_jui_generic_umg.py):
//     /Game/AsdaAssets/UI/Player_UI/Quest/WBP_TALKBOXW_WIN, WBP_TALKBOXB_WIN, WBP_TALKBOX_Mon_WIN
//   ⇒ вся графика/цвета/UV правятся в UMG-дизайнере (правила no-invented-ui + ui-editable-in-ue).
//
// ── КТО И ЧЕМ ЗОВЁТ (единственные точки показа; RE/etalon/network.c) ───────────────────────────
//   entityName_DrawOverhead @0xBAD1B0 (entity_char.c:44388) — для ПЕРСОНАЖЕЙ, два слота на сущности:
//       вариант 0 → объект по смещению +2312, вариант 1 → +2360;
//   uiDrawEntityNamePlate   @0xBAD3B0 (ui_core.c:13101)     — для МОБОВ, слот +18740, вариант 2.
//   опкод 5085 Recv_ChatNormal        → вариант 0, цвет −16777216 (FF000000), стиль 0
//   опкод 5087 Recv_ChatMessage       → вариант 0, цвет CChat+623/624/625 (FFFF0000 / FFF1EF07), стиль 1
//   опкод 5089 Recv_ChatWhisper       → вариант 1, цвет −14540254 (FF222222), стиль 0 (эхо) / 1 (входящий)
//   опкод 4121 Recv_SystemMessage     → вариант 0, цвет −16748079 (FF0071D1), стиль 0
//   опкод 5106 Recv_GM_PARTY_CHAT_RES → вариант 0, цвет −15829504 (FF0E7600), стиль 0
//   опкод 6719 Recv_ClanChat          → вариант 0, цвет канала 9 (FFA03C00) / офлайн −39906 (FFFF641E)
//   опкод 8008 recvCharChatBubble     → вариант 2 над МОБОМ, цвет −51416 (FFFF3728)
//   ★ Опкод 6561 (Recv_WhisperOrSystemMessage) облачко НЕ показывает — вызова в эталоне нет.
//   ★ В облачко идёт СЫРОЙ текст сообщения, БЕЗ префикса «[Имя]: » (в чат-лог — с префиксом).
//   ★ Своё сообщение тоже даёт облачко над СОБОЙ: в Recv_ChatNormal ветка «отправитель == я»
//     сходится на ту же метку LABEL_27 (network.c:10390-10475). Совпадает с законом юзера
//     self-nameplate-same-as-others.
//   ★★ 26.10.2026: свой персонаж ЛЕЖИТ в корзинах CharManager (создаётся `questCreateAndRegister@0x9D1490` с вставкой в
//     корзину, AsdaStory_game.c:10664), поэтому `CharManager_FindByName@0x9D13F0` / `FindByCharId@0x9D11B0` находят и СЕБЯ.
//     Сервер рассылает 5106 ВСЕМ членам отряда, отправителю тоже (`sub_451210`, GameServer.c:32675-32694, цикл по 6 слотам
//     без исключения себя). У порта поиск по имени (`FindPlayerActorByName`) смотрел только соседей — облачко отряда
//     (5106) и армии (6719) над СОБОЙ не появлялось (баг игроков). Исправлено в ResolveAndShow (ключ asda.Chat.BalloonSelfByName).
//   ★★ 26.10.2026: вертикальная центровка (бит 0x4) центрирует ВЕСЬ блок строк: `textFont_DrawRichText@0x12C9590`
//     top = (верх+низ)>>1 − высота_шрифта·число_строк/2. Порт центрировал как одну строку ⇒ при 2 строках текст уезжал вниз
//     на полстроки, при 3 — на целую строку («первая строка пустая», жалоба владельца). Ключ asda.Chat.BalloonVCenterBlock.
//   ★★ 26.10.2026: рендер оригинала после '\n' пропускает ОДИН пробел (тот же `textFont_DrawRichText`, case 3/4) — строка-
//     продолжение без отступа. Ключ asda.Chat.BalloonSkipWrapSpace.
//   ★★ 26.10.2026: перед переносом текст ОБРЕЗАЕТСЯ по ширине 580 px с «...» (`textTruncateWithEllipsis@0xBC6E60` в
//     `entityName_DrawOverhead`, entity_char.c:44425). Ключ asda.Chat.BalloonTruncate.
//   ★ Ещё один потребитель того же класса — квестовая подпись над NPC (questDrawTargetNameLabel
//     @0xD8C5C0, вариант 2, цвет −1, троттлинг 7500 мс). Он ЗДЕСЬ НЕ ПОДКЛЮЧЕН (квест-метки живут в
//     UAsdaQuestMarkerSubsystem), но API ShowBalloon покрывает и его.
//   ★ НЕ ПУТАТЬ: «плита с именем личного магазина» над головой — ДРУГАЯ система
//     (Chat_RenderHeadBubble @0xDE10E0 + gameUpdateTargetNameplates @0x97B700, свои контролы).
//
// ── ГЕОМЕТРИЯ (мировая точка и экранное смещение — покадровые циклы эталона) ────────────────────
//   персонажи  entity_drawOverheadNamePlates @0x9DAE40 (entity_char.c:9427):
//       worldY = helper_getSumF(ent+432) ± 30.0 (flt_1398260) + heightOffset надетого предмета
//                (CList_FindByTypeSub(ent+2552, 15, 2) → special_itemlist.HeightOffset);
//                знак «−» при ent+190204 == 8 (id действия; ветка редкая, см. отчёт);
//       экран: X −= g_nScreenTextOffsetX, Y −= 90.
//   мобы       nameplateUpdateAllEntities @0xF77A60 (entity_char.c:54208, тип сущности +464 == 23):
//       worldY = helper_getSumF(ent+424) + 160.0; экран: X −= g_nTargetPlateHalfWidth, Y как есть.
//   NPC        uiNameplateUpdateType14 @0xF92EA0 (ui_core.c:98170, тип +464 == 14):
//       worldY = helper_getSumF(ent+424) − 20.0; экран: X −= g_nTargetPlateHalfWidth, Y −= 110.
//   helper_getSumF @0x97D6D0 = p[4] + p[1] = centerY + halfY = ВЕРХ AABB сущности (та же величина,
//       что у плашки имени — она приходит ИЗ ДАННЫХ, а не из габаритов меша, см. nameplate-height-from-data).
//   g_nScreenTextOffsetX (0x2902F48) и g_nTargetPlateHalfWidth (0x2902F40) в живом дампе = 0, и
//       uiTargetPlateLayoutCenter прибавляет их обратно ⇒ по X сдвига нет (проверено по asda.dmp).
//   uiTargetPlateLayoutCenter: окно ставится в (X − ширина/2, Y); хвостик — в
//       (ширина_окна/2 − ширина_хвостика/2, высота_окна − 3) ОТНОСИТЕЛЬНО окна.
//
// ── ПЕРЕНОС СТРОК И РАЗМЕР (uiWrapTooltipText @0xF22D10 — воспроизведён посимвольно) ────────────
//   • ведущие пробелы отбрасываются; ширина символа в «колонках»: 1 (ASCII) или 2 (ведущий байт DBCS);
//     ★ при g_nClientLanguage == 11 DBCS не включается вовсе — а 11 это ЛЮБАЯ система, кроме
//       корейской (1042→12), японской (1041→14) и китайской-Тайвань (1028→15). В живом дампе = 11.
//   • перенос при lineColWidth > 18 колонок; откат к последнему ПРОБЕЛУ, если он был на этой строке
//     (курсор ставится НА пробел ⇒ следующая строка начинается с пробела — это оригинальное поведение);
//   • высота шрифта в измерителе — КОНСТАНТА 12 px (textFontMeasureLine @0x12C7330: v9 = 12);
//   • итоговые размеры (W — ширина самой длинной строки в пикселях):
//       нет переносов:            W>=20 ⇒ текст (W+8)×20, окно (W+21)×36;  иначе текст 20×20, окно 36×36
//       есть перенос и есть хвост: текст (Wmax+8)×(12·(n+1)+8),  окно (Wmax+21)×(12·(n+1)+22)
//       есть перенос, хвоста нет:  n==1 ⇒ текст (Wmax+4)×20, окно (Wmax+14)×24
//                                  n>1  ⇒ текст (Wmax+6)×(12n+8), окно (Wmax+16)×(12n+22)
//   • выравнивание текста (CControl_SetField924, маска 0x1 hCenter/0x2 hRight/0x4 vCenter/0x8 vBottom):
//       «хвоста нет» ⇒ 0; иначе длина сообщения 18..36 ⇒ 4 (vCenter); >36 ⇒ 0; <18 ⇒ 5 (h+vCenter).
//   • стиль шрифта (CClass_SetField926): 0x100000 при a6==0 — рисование обычное; 0x200000 при a6==1 —
//     глиф рисуется ДВАЖДЫ со сдвигом ±1 px по X (font_text.c:6832) = ЖИРНЫЙ.
//
// ── ЖИЗНЬ ─────────────────────────────────────────────────────────────────────────────────────
//   5000 мс от момента показа (поле +40, ctor). Гаснет МГНОВЕННО (CControl_SetVisible(0)), без фейда.
//   Новое сообщение того же варианта у того же персонажа ПЕРЕЗАПИСЫВАЕТ текст и обнуляет таймер.
//   Слотов на сущность ровно два (варианты 0 и 1) ⇒ обычный чат и шёпот видны ОДНОВРЕМЕННО.
//   Вне фрустума облачко скрывается (planeAccumulateDotTest), при возврате — показывается снова,
//   если таймер ещё не истёк. Дистанционного порога НЕТ. У МОБА облачко не рисуется в состоянии 10
//   (смерть) — гейт `*(ent+480) != 10` в uiDrawEntityNamePlate.
//
// ЗАКОН no-reentrant-net-callbacks: обработчик пакета только КЛАДЁТ заявку в очередь; создание
//   виджетов и работа со сценой — в Tick.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "AsdaNetQueue.h"   // ★ U7: TAsdaNetQueue — очередь пакетов из сетевого колбэка
#include "World/AsdaOverheadOcclusion.h"   // FAsdaOccludeState — облачко прячется за препятствиями (29.10.2026)
#include "AsdaChatBalloonSubsystem.generated.h"

class UUserWidget;
class UImage;
class UTextBlock;
class AActor;

// Вариант облачка = baseId JUI-шаблона (uiWnd_BuildTripleChildWindow, аргумент `variant`).
UENUM(BlueprintType)
enum class EAsdaTalkBox : uint8
{
	White = 0 UMETA(DisplayName = "0 — белое (TALKBOXW 9010): обычный/канальный/системный/отряд/клан"),
	Gold  = 1 UMETA(DisplayName = "1 — золотое (TALKBOXB 9013): шёпот"),
	Mon   = 2 UMETA(DisplayName = "2 — чёрное (TALKBOX_Mon 9016): моб/NPC"),
};

// Шаблон варианта: класс импортированного WBP + имена контролов внутри него.
//   Имена сгенерированы импортёром из idc JUI: девять планов «<idc>_0_<i>», хвостик «<tail>_1_0», текст «<idc текста>».
USTRUCT(BlueprintType)
struct FAsdaTalkBoxTemplate
{
	GENERATED_BODY()

	// Путь к Widget Blueprint (импорт 1:1 из JUI; правится в UMG-дизайнере).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	FString WidgetClassPath;

	// Префикс девяти планов подложки: «<Префикс>_0_0» … «<Префикс>_0_8».
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	FString PanelPrefix;

	// Имя контрола-хвостика (JUI 9011/9014/9017).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	FName TailName;

	// Имя контрола-текста (JUI 9012/9015/9018).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	FName TextName;
};

// Один живой экземпляр облачка (аналог объекта-«TalkBox» на сущности оригинала).
USTRUCT()
struct FAsdaBalloonInst
{
	GENERATED_BODY()

	UPROPERTY(Transient) TWeakObjectPtr<AActor> Owner;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> Slices;   // девять планов подложки (порядок 0..8)
	UPROPERTY(Transient) TObjectPtr<UImage> Tail = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text = nullptr;
	// ★ Вторая копия строки для стиля 0x200000 (эталон рисует КАЖДЫЙ глиф дважды: penX+1 и penX−1).
	//   Создаётся рядом с основным контролом при первом использовании облачка.
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TextBold = nullptr;

	EAsdaTalkBox Kind = EAsdaTalkBox::White;
	bool bBold = false;          // a6 == 1 (CClass_SetField926(0x200000))

	// Геометрия шаблона, снятая с WBP один раз (аналог полей +16/+20/+24/+28 объекта эталона).
	FVector2D CornerTL = FVector2D(9.f, 9.f);      // размер левого-верхнего угла
	FVector2D CornerBR = FVector2D(9.f, 9.f);      // размер правого-нижнего угла
	FVector2D TailSize = FVector2D(23.f, 19.f);

	// Текущее состояние (аналог +32/+42/+43 эталона).
	double  ShowTimeS = -1.0;    // время показа, сек. мира (t0)
	bool    bActive = false;     // +42
	bool    bSuppressed = false; // +43 — «погашено таймером», показ обратно запрещён

	// Последний посчитанный размер (uiWrapTooltipText).
	FVector2D PanelSize = FVector2D(36.f, 38.f);
	FVector2D TextSize = FVector2D(20.f, 20.f);
	int32     TextAlign = 0;     // маска CControl_SetField924
	// ★ 26.10.2026 [ОРИГИНАЛ]: число строк итогового текста (сегментов между '\n', без пустого хвоста) — для вертикальной
	//   центровки ВСЕГО блока строк (`textFont_DrawRichText@0x12C9590`: top = (верх+низ)/2 − высота_шрифта·строк/2).
	int32     LineCount = 1;
	FString   ShownText;         // строка, ушедшая в контрол текста (после обрезки и переноса) — для самотеста и дампа
	FColor    TextColorSrgb = FColor(0, 0, 0, 255);   // цвет текста, как передал вызывающий (dword эталона, sRGB)
	FVector   LastWorldPt = FVector::ZeroVector;      // ★ 30.10.2026: мировая точка привязки последнего расчёта (самотест отставания)
	// ★★★ [ОРИГИНАЛ] 29.10.2026 (баг игроков 3): перекрытие облачка геометрией мира — у оригинала планы облачка несут глубину его
	//   точки (game_main.c:4523 `matrixProjectZ` → поле 527) и проходят z-тест. Разбор и ключ — `World/AsdaOverheadOcclusion.h`.
	FAsdaOccludeState OccState;
	float     OccOpacity = -1.f; // последняя поставленная непрозрачность (−1 — не ставилась)
};

// ★ 26.10.2026: снимок одного облачка для самотеста `asda.Chat.BalloonSelfTest` (только чтение, боевой код не опирается).
struct FAsdaBalloonDebugInfo
{
	bool      bFound = false;            // облачко для пары (владелец, вариант) существует
	bool      bActive = false;           // +42 эталона: показано и таймер не истёк
	bool      bOnScreen = false;         // виджет видим (не Collapsed) — проекция попала в кадр
	float     Opacity = 1.f;             // ★ 29.10.2026: непрозрачность виджета (перекрытие препятствием, asda.Overhead.Occlude)
	FVector   WorldPt = FVector::ZeroVector;      // ★ 30.10.2026: мировая точка привязки последнего расчёта
	const UUserWidget* Widget = nullptr;          // ★ 30.10.2026: живой виджет облачка (нарисованная геометрия — самотест отставания)
	EAsdaTalkBox Kind = EAsdaTalkBox::White;
	FString   Text;                      // строка в контроле текста (с '\n' переноса)
	FColor    Color = FColor(0, 0, 0, 255);   // цвет текста (sRGB, как передан)
	FVector2D PanelSize = FVector2D::ZeroVector;   // окно (uiWrapTooltipText)
	FVector2D TextPos = FVector2D::ZeroVector;     // позиция контрола текста в окне (TextInset + вертикальная центровка)
	FVector2D TextSlotSize = FVector2D::ZeroVector;// размер контрола текста
	FVector2D TextDesired = FVector2D::ZeroVector; // желаемый размер TextBlock (реальная высота блока строк в UE)
	int32     TextAlign = 0;             // маска выравнивания
	int32     LineCount = 0;             // строк в блоке
	float     UeLineHeight = 0.f;        // высота строки шрифта контрола в UE (FontMeasure, px)
	FMargin   TextMargin;                // поля TextBlock из ассета (UTextLayoutWidget::Margin) — «отступ в ассете»
	float     LineHeightPct = 1.f;       // UTextLayoutWidget::LineHeightPercentage из ассета
};

UCLASS()
class ASDAREBORN_API UAsdaChatBalloonSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// UTickableWorldSubsystem: покадровое обновление (аналог циклов отрисовки плашек эталона).
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaChatBalloonSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return false; }

	// ── ЕДИНАЯ ТОЧКА ПОКАЗА (эталонный uiShowTooltipPlate) ─────────────────────────────────────
	// Owner  — сущность-владелец (персонаж/моб/NPC); nullptr ⇒ эталон ничего не делает.
	// Kind   — вариант JUI-шаблона (0/1/2).
	// Text   — СЫРОЙ текст сообщения (без «[Имя]: »).
	// Color  — цвет ТЕКСТА в sRGB, ровно dword эталона (в UE переводится в linear).
	// bBold  — a6: false ⇒ 0x100000 (обычный), true ⇒ 0x200000 (двойная прорисовка = жирный).
	UFUNCTION(BlueprintCallable, Category = "Asda|TalkBox")
	void ShowBalloon(AActor* Owner, EAsdaTalkBox Kind, const FString& Text, FColor Color, bool bBold);

	// Полный сброс всех облачек владельца (эталонный uiTooltipPlateHide при удалении сущности).
	UFUNCTION(BlueprintCallable, Category = "Asda|TalkBox")
	void HideBalloonsFor(AActor* Owner);

	// Состояние для консольного дампа.
	FString DebugState() const;

	// ★ 26.10.2026: снимок облачка (владелец, вариант) для самотеста; false — облачка нет.
	bool DebugGetBalloon(const AActor* Owner, EAsdaTalkBox Kind, FAsdaBalloonDebugInfo& Out) const;

	// ★ 26.10.2026 [ОРИГИНАЛ]: обрезка текста облачка с «...» (`textTruncateWithEllipsis@0xBC6E60`, порог 580 px, зовёт
	//   `entityName_DrawOverhead`). Публична ради самотеста (ожидаемая строка считается тем же замером шрифта).
	FString TruncateLikeOriginal(EAsdaTalkBox Kind, const FString& In) const;

	// Порог обрезки текста облачка, px (эталон `entityName_DrawOverhead`: `textTruncateWithEllipsis(…, 580)`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	int32 TruncateWidthPx = 580;

	// ── НАСТРОЙКИ (все значения — из эталона; правятся без пересборки) ─────────────────────────

	// Мастер-тумблер оригинала g_bShowEntityNames (0x15CB4D8, настройки +1139; в живом дампе = 1).
	//   Гейтит ИМЕННО этот класс: чат-облачко, облачко моба и квестовую подпись NPC.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	bool bShowBalloons = true;

	// Длительность показа, мс (эталон: ctor @0xF226F0, поле +40 = 5000; ни одна ветка его не меняет).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	int32 LifetimeMs = 5000;

	// Порог переноса в «колонках» (эталон uiWrapTooltipText: `if (lineColWidth > 18)`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	int32 WrapColumns = 18;

	// Высота строки измерителя (эталон textFontMeasureLine @0x12C7330: константа 12).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	int32 FontLineHeight = 12;

	// Однобайтовый режим: g_nClientLanguage == 11 (всё, кроме ko/ja/zh-TW) ⇒ DBCS-ветка выключена.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	bool bSingleByteLanguage = true;

	// Отступ текста внутри окна (JUI: TALKBOX*_TXT Origin = «7 7»).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	FVector2D TextInset = FVector2D(7.f, 7.f);

	// Наложение хвостика на окно (эталон uiTargetPlateLayoutCenter: y = высота_окна − 3).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	float TailOverlap = 3.f;

	// Экранные оффсеты оригинала (в живом дампе оба 0; прибавляются обратно в раскладке ⇒ ноль).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	float ScreenTextOffsetX = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	float TargetPlateHalfWidth = 0.f;

	// Мировая добавка к ВЕРХУ AABB и экранный сдвиг по Y — по типу сущности (см. шапку).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	float PlayerWorldZ = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	float PlayerScreenY = -90.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	float MobWorldZ = 160.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	float MobScreenY = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	float NpcWorldZ = -20.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	float NpcScreenY = -110.f;

	// Z-order виджета во вьюпорте: −1 = под игровым UI, как у плашек имён.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	int32 ViewportZOrder = -1;

	// Три шаблона (индексы совпадают с EAsdaTalkBox). Заполняются в конструкторе значениями импорта.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|TalkBox")
	TArray<FAsdaTalkBoxTemplate> Templates;

	UAsdaChatBalloonSubsystem();

private:
	// ── Приём сети (только постановка в очередь — закон no-reentrant-net-callbacks) ────────────
	struct FAsdaPendingBalloon
	{
		int32   UserIdx = -1;      // адресация 5085/5087
		int32   CharId = -1;       // адресация 5089/4121
		FString ByName;            // адресация 6719/5106
		int32   MobRid = -1;       // адресация 8008
		EAsdaTalkBox Kind = EAsdaTalkBox::White;
		FString Text;
		FColor  Color = FColor(0, 0, 0, 255);
		bool    bBold = false;
	};
	TAsdaNetQueue<FAsdaPendingBalloon> Pending;

	FDelegateHandle HChatLine;
	FDelegateHandle HMobBubble;
	// ★★★ [ОРИГИНАЛ] 30.10.2026 (asda.Overhead.LateProject, `World/AsdaOverheadLate.h`): подписка «после обновления камеры».
	//   Подсистема МИРА тикает до `UpdateCameraManager` — облачко проецировалось камерой прошлого кадра и «плыло» за поворотом.
	FDelegateHandle HPostCamera;
	// Тело кадра (очередь заявок + позиция и видимость облачков): из Tick при ключе 0, из подписки «после камеры» при ключе 1.
	void TickBalloons();

	UPROPERTY(Transient)
	TArray<FAsdaBalloonInst> Balloons;

	// Найти/создать облачко для пары (владелец, вариант). nullptr, если WBP не загрузился.
	FAsdaBalloonInst* AcquireBalloon(AActor* Owner, EAsdaTalkBox Kind);
	// Разобрать заявку очереди: найти актора и показать.
	void ResolveAndShow(const FAsdaPendingBalloon& P);
	// Перенос строк + пересчёт размеров (эталонный uiWrapTooltipText), плюс применение к виджету.
	void WrapAndResize(FAsdaBalloonInst& B, const FString& InText);
	// Разложить девять планов, хвостик и текст по посчитанным размерам (uiPanel9SliceLayout + центровка).
	void LayoutBalloon(FAsdaBalloonInst& B);
	// Покадровое обновление: проекция, таймер, видимость (эталонные покадровые циклы).
	void UpdateBalloon(FAsdaBalloonInst& B, double NowS);
	// Мировая точка «верх AABB» сущности + добавка по типу; false — не удалось определить.
	bool ResolveAnchor(const AActor* Owner, FVector& OutWorld, float& OutScreenYOffset) const;
	// Ширина строки в шрифте контрола-текста облачка (аналог textFontMeasureLineWidth).
	float MeasureWidth(const FAsdaBalloonInst& B, const FString& Line) const;
	// ★ 26.10.2026: обрезка с «...» по TruncateWidthPx шрифтом облачка B (эталонный textTruncateWithEllipsis).
	FString TruncateWithFont(const FAsdaBalloonInst& B, const FString& In) const;
	// ★ 26.10.2026: высота строки шрифта контрола текста в UE (FontMeasure × LineHeightPercentage), px.
	float LineHeightOf(const FAsdaBalloonInst& B) const;
};
