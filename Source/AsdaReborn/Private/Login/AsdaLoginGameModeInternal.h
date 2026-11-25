// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginGameModeInternal.h — ★ 23.10.2026 (разнос AsdaLoginGameMode.cpp): ЗАКРЫТЫЙ заголовок частей AsdaLoginGameMode*.cpp.
//   Только нужное нескольким частям: категория журнала LogAsdaLogin (один экземпляр — DEFINE_LOG_CATEGORY в
//   AsdaLoginGameMode.cpp) и чистые static-константы/хелперы без состояния — текст байт в байт, у каждой части своя копия.
//   Подключать ПОСЛЕ #include пролога.
#pragma once

#include "CoreMinimal.h"
#include "Login/AsdaLoginGameMode.h"

// Категория журнала логина: объявление; определение — AsdaLoginGameMode.cpp (до разноса там был DEFINE_LOG_CATEGORY_STATIC).
DECLARE_LOG_CATEGORY_EXTERN(LogAsdaLogin, Log, All);


// ★★★ 16.10.2026 (решение владельца): АДРЕС СЕРВЕРА НЕ ВВОДИТСЯ. Логин-сервер — всегда домен AsdaServerAddr::LoginHost()
//   (Login/AsdaServerAddress.h), порт — из DT_ServerList. Поле SERVER_IP_INPUT выключено владельцем в ассете и только
//   показывает домен; сохранённый `[AsdaLogin] ServerIP` удаляется при старте. Прежний разбор ввода «host:port»
//   (EAsdaAddrParse / ParseServerHostPort) снят вместе с вводом.

#include "Text/AsdaLang.h"   // ★ 09.10.2026: язык сессии (подсказки помощника)
#include "UI/AsdaLangSelectSubsystem.h"   // ★ 09.10.2026: окно первого выбора языка (мультиязычность)
#include "Resources/AsdaBenchSubsystem.h"  // ★ 12.10.2026: автоподбор графики замером кадра при первом запуске
#include "Camera/CameraActor.h"   // ★ 13.09: TActorIterator<ACameraActor> — поиск LoginCamera для перелёта камерой
#include "AsdaSelfTestBatch.h"   // ★ 11.10.2026: выход самотеста PossessPickTest через `quit` движка (перехватывается пакетом asda.SelfTest.Batch)
// Трек перелёта сцены после логина (автоген из cre_creback-b_00_0.jbb, 101 ключ / 3.333с).
#include "Login/AsdaLoginFlightTrack.inl"

// ★★★ МАСШТАБ СЦЕНЫ ЛОГИНА (12.09.2026, жалоба владельца «масштабы огромные / Spot и Directional
//   не работают»). БЫЛО 100: сцена логина собиралась в ×100 (1 игровая единица = 100 юнитов UE) —
//   артефакт glTF-импорта задника и шкатулки (glTF трактует единицы файла как МЕТРЫ и домножает
//   на 100). Логин был ЕДИНСТВЕННОЙ локацией порта в ×100: все 24 игровые карты живут в ×1
//   (`Tools/asda_build_map_level.py SCALE = 1.0`, игровая единица = 1 см UE).
//   ЗАМЕР 12.09.2026: медианный габарит элемента логина 18 736 юн против 179.7 юн у alpen (×104);
//   превью персонажа 15 592 юн в высоту = 156 МЕТРОВ вместо 1.56 м. Следствие: каждая длина-
//   умолчание UE (радиус затухания точечного/конусного света 1000 юн, дальность каскадов теней,
//   контактные тени, дальности отсечения) на логине ничего не покрывала.
//   СТАЛО 1 — сцена в тех же единицах, что и карты.
//   ЕДИНЫЙ РЫЧАГ: здесь — рантайм-геометрия (камера форджинга, вынос превью, затемняющий квад,
//   масштаб эффекта шкатулки); ассеты — `ASDA_LOGIN_SCALE` у `asda_build_backdrop_elements.py` и
//   `asda_export_backdrop_anim.py`; трек перелёта — значения в `AsdaLoginFlightTrack.inl`.
//   Вернуть ×100 = поставить 100.f ЗДЕСЬ и пересобрать ассеты теми же тулами с ASDA_LOGIN_SCALE=100.
static constexpr float GLoginScale = 1.f;

// ★ Превью-персонаж char-select = ОДИН SkeletalMesh на скелете cre_cremmy (50 гуманоид-костей):
//   части ch1_m_* (chst/pant/shoe/face/hair) скиннуты на этот скелет (charselect_render.md §4).
//   Экспорт Tools/export_char_cremmy.py → импорт Tools/import_char_cremmy.py. idle = anim_00 (клип
//   cre_cremmy-a_00_0, 2.0с). НЕ статичная T-поза: персонаж стоит естественно и «дышит».
// ★ ОБЩАЯ ТОЧКА (закон `port-shared-points`, 02.09.2026): сам путь объявлен ОДИН РАЗ —
//   `kAsdaCharPreviewBaseM/F` в `World/AsdaPlayerCharacter.h` (владелец скелета — мировой
//   персонаж). Раньше этот же литерал был выписан здесь ЧЕТЫРЕ раза (меш + база клипов на каждый
//   пол), ещё дважды в `AsdaPossessScene.cpp`, трижды в `World/AsdaPlayerCharacter.cpp` и один раз
//   в `World/AsdaWorldSubsystem.cpp`; при переименовании ассета часть копий отстала бы молча.
//   Меш и база клипов — ОДНА И ТА ЖЕ строка: имена ассетов различаются только суффиксом
//   `anim_%02d`, который добавляет вызывающий.
static const TCHAR* GCharPreviewMesh  = kAsdaCharPreviewBaseM;
// Женский вариант (Sex==2): те же части ch1_f_* на скелете cre_cremmy.
static const TCHAR* GCharPreviewMeshF = kAsdaCharPreviewBaseF;
// База имени клипов превью (anim_%02d): 00..04 = cre_cremmy (М) / cre_crewmy (Ж) — char-select,
//   05..11 = cre_creman/crewoman (набор CHAR-CREATE, эталон CCreature_LoadCreManAnims @0xF10050;
//   трек эталона N → ассет 05+N), 205..224 — парные клипы «одержимости».
static const TCHAR* GCharPreviewAnimBase  = kAsdaCharPreviewBaseM;
static const TCHAR* GCharPreviewAnimBaseF = kAsdaCharPreviewBaseF;
// Материал фейда появления превью и пакет окна выбора — одна точка для их использования и предзагрузки.
static const TCHAR* kPreviewFadeMaterial = AsdaMat::kPreviewFadePath;   // ★ В1-C F10: путь мастера — общая константа
static const TCHAR* kCharSelectWidgetPath = TEXT("/Game/AsdaAssets/UI/Login/WBP_CharSelect");

// Форвард-декларация (определение ниже): кость пьедестала слота по перестановке T0/T2/T3. Дефолт RotState — здесь.
static FName CarouselPedestalBone(int32 SlotI, int32 CurSlot, int32 RotState = 0);

// ★ R5 (12.11.2026, дубли): БАЗА СЛОТА ШКАТУЛКИ — мировой трансформ кости пьедестала Bone скелета шкатулки; кости нет —
//   трансформ самой шкатулки; шкатулки нет — единичный. CaskComp — скелетный компонент шкатулки (вызывающий берёт его сам:
//   он же нужен ему ниже для крепления к кости). До R5 эти три строки стояли четырьмя дословными копиями:
//   партнёр-соулмейт и «душа» слота (AsdaLoginGameMode_Soul.cpp), персонаж слота (AsdaLoginGameMode_Server.cpp), сцена
//   создания персонажа (AsdaLoginGameMode_Create.cpp). Порядок проверок и вызовы — те же (пункт K12 аудита 14.09.2026).
//   Слот персонажа = КОСТЬ CH_00N скелета шкатулки cre_creation01: базовая позиция = мировой трансформ этой кости, контент
//   слота крепится к ней и едет с анимацией слота; партнёр и «душа» берут ТУ ЖЕ кость (эталон двигает всю тройку объектов
//   слота одной матрицей `posSetWithChildOffset`).
static FTransform AsdaLoginPedestalBase(const AActor* Casket, const USkeletalMeshComponent* CaskComp, const FName& Bone)
{
	FTransform Base = FTransform::Identity;
	if (CaskComp && CaskComp->DoesSocketExist(Bone)) { Base = CaskComp->GetSocketTransform(Bone, RTS_World); }
	else if (Casket)                                 { Base = Casket->GetActorTransform(); }
	return Base;
}

// Единый менеджер моделей/анимаций (engine-lifetime кэш). Все скелет-клипы/меши логина — через него.
static UAsdaModelAnimSubsystem* GetAnimMgr()
{
	return GEngine ? GEngine->GetEngineSubsystem<UAsdaModelAnimSubsystem>() : nullptr;
}
static UAnimSequence* LoadClip(const FString& Path)
{
	UAsdaModelAnimSubsystem* M = GetAnimMgr();
	return M ? M->LoadAnimClip(Path) : nullptr;
}
static USkeletalMesh* LoadMesh(const FString& Path)
{
	UAsdaModelAnimSubsystem* M = GetAnimMgr();
	return M ? M->LoadModel(Path) : nullptr;
}

// ★★★ ОБЩАЯ ТОЧКА «ячейки экипировки char-select → набор носимых частей» (закон `port-shared-points`).
//   ЗАЧЕМ ОТДЕЛЬНО. Раскладку читают ДВА показа одной и той же сцены: мой персонаж (экип из 0x3EF)
//   и персонаж соулмейта (экип из 0x400). Ячейки в обоих пакетах индексируются ОДИНАКОВО, и если
//   маппинг разъедется, чужой персонаж наденет штаны на голову — а заметить это будет некому, потому
//   что фича включается только при живой паре соулмейтов. Поэтому таблица соответствия ОДНА.
//
//   Индекс ячейки = itemType (эталон `playerCharObjectLoadColored@0xF0A2B0` грузит РОВНО носимые
//   типы, без оружия и причёски): 0 шлем, 1 тело, 2 штаны, 3 обувь, 4 перчатки.
//   Причёска/цвет/лицо приходят отдельным триплетом внешности и кладутся сюда же.
namespace AsdaLoginPossess
{
	static FAsdaWornParts WornPartsFrom(const TArray<FAsdaEquipItem>& Equip, uint8 Hair, uint8 HairColor, uint8 Face)
	{
		auto Eq = [&Equip](int32 Type) -> int32
		{
			return (Equip.IsValidIndex(Type) && Equip[Type].ItemId > 0) ? Equip[Type].ItemId : 0;
		};
		FAsdaWornParts P;
		P.Helmet = Eq(0);
		P.Chest  = Eq(1);
		P.Pants  = Eq(2);
		P.Shoes  = Eq(3);
		P.Gloves = Eq(4);
		P.FaceIndex = (int32)Face;
		P.HairStyle = (int32)Hair;
		P.HairColor = (int32)HairColor;
		// Оружие в сцене выбора персонажа НЕ рендерится (эталон его в этот конвейер не отдаёт).
		return P;
	}

	// ★ Синтетическая внешность регресс-прогона ASDA_TEST_FLOW=1 (слот 0 без сервера) — одна точка для сборки
	//   превью (`ApplyEquipToSlot`) и её предзагрузки (`RequestCharSelectPreload`).
	static FAsdaWornParts TestFlowWornParts()
	{
		FAsdaWornParts P;
		P.FaceIndex = 0;
		P.HairStyle = 1;
		P.HairColor = 0;
		return P;
	}
}

// ★ Карусель char-select (glyph_carousel_fix.md, эталон Game_GetChannelEntryName@0xF9EE10 = CharSelect_
//   GetPedestalBoneMatrix): контент слота i стоит на кости пьедестала по РОЛИ относительно выбранного слота
//   (front/next/prev), НЕ по фикс-индексу CH_00{i+1}. Таблица T0 (rotState=0 idle) индексируется [3*curSlot+role].
//   Так выбранный слот всегда во фронте, боковые — по бокам, никто не «уезжает назад» за активный (иначе глиф
//   оказывается за активным слотом + боковые перепутаны местами уже в покое). curSlot=SelectedSlot.
// Кость пьедестала слота по перестановке эталона Game_GetChannelEntryName@0xF9EE10 / game_main.c:13260-13305.
//   RotState: 0=idle(T0), 2=поворот влево(T2), 3=поворот вправо(T3). Индекс = [3*curSlot + role], role 0/1/2 = front/next/prev.
//   Во время клипа поворота слот едет на кость T2/T3 (иначе «основной персонаж на неверной кости» — баг скролла).
static FName CarouselPedestalBone(int32 SlotI, int32 CurSlot, int32 RotState)   // дефолт RotState=0 в форвард-декларации выше
{
	static const TCHAR* const T0[9] = {
		TEXT("CH_001"), TEXT("CH_003"), TEXT("CH_002"),   // curSlot 0: front / next / prev
		TEXT("CH_002"), TEXT("CH_001"), TEXT("CH_003"),   // curSlot 1
		TEXT("CH_003"), TEXT("CH_002"), TEXT("CH_001"),   // curSlot 2
	};
	static const TCHAR* const T2[9] = {                   // поворот ВЛЕВО (rotState 2)
		TEXT("CH_003"), TEXT("CH_002"), TEXT("CH_001"),
		TEXT("CH_002"), TEXT("CH_001"), TEXT("CH_003"),
		TEXT("CH_001"), TEXT("CH_003"), TEXT("CH_002"),
	};
	static const TCHAR* const T3[9] = {                   // поворот ВПРАВО (rotState 3)
		TEXT("CH_001"), TEXT("CH_003"), TEXT("CH_002"),
		TEXT("CH_003"), TEXT("CH_002"), TEXT("CH_001"),
		TEXT("CH_002"), TEXT("CH_001"), TEXT("CH_003"),
	};
	CurSlot = ((CurSlot % 3) + 3) % 3;
	const int32 Next = (CurSlot + 1) % 3;
	const int32 Role = (SlotI == CurSlot) ? 0 : (SlotI == Next) ? 1 : 2;   // 0=front,1=next,2=prev
	const TCHAR* const* Tbl = (RotState == 2) ? T2 : (RotState == 3) ? T3 : T0;
	return FName(Tbl[3 * CurSlot + Role]);
}
