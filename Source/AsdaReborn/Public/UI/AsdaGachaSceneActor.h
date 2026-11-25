// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaGachaSceneActor — 3D-часть сцены премиум-гачи: автоматы, снятые в render-target,
// который потом показывается в окне 68000 как фон.
//
// ★ ПОЧЕМУ ОТДЕЛЬНЫЙ АКТОР, А НЕ UI. В оригинале «барабан» гачи — НЕ контрол интерфейса, а
//   настоящая 3D-модель: `md_gachya_{a..e}_00` (20 мешей, кости 33→21) с клипами в слотах,
//   отрисованная в render-target и выведенная в контейнер 68056 (`gachaScene_UpdateChildAnims`
//   @0xB6B0B0 крутит их из главного цикла). Поэтому здесь скелеталки + SceneCapture, а не картинки.
//
// ★ ВСЕ ЧИСЛА СЦЕНЫ — ИЗ ЭТАЛОНА, не подобраны на глаз:
//   `uiBuildPortraitCameraMatrices@0xB6EB50` (ui_core.c:7248-7290) на каждый объект i строит ДВЕ
//   матрицы (обе в оригинальных единицах, D3DX row-vector, сначала перенос, потом поворот):
//     · МИРОВАЯ МАТРИЦА АВТОМАТА    = T(0,0,−150) · RotY(θi)  → пишется в ctrl+492 (dword 123)
//     · ЯКОРЬ ДЛЯ КОНТРОЛОВ СЛОТА   = T(0,0,−350) · RotY(θi), затем m._42 += 200 → ctrl+556
//   θ = 0 · +0.47123852 рад (+27°) · −0.47123852 · +0.94247705 (+54°) · −0.94247705 для i=0..4.
//   `PetWindow_RenderToTarget@0xB6D5B0` (npc_mob_pet.c:56-181) — камера и цель:
//     глаз (0, eyeY, eyeZ): N=1 → (0,150,100) · N=2,3 → (0,250,150) · N=4,5 → (0,250,500) ·
//                           иначе (0,150,150);  цель (0,100,−200);  верх (0,1,0);
//     проекция FovY = 0.78539819 рад (45°), aspect = размер RT, near 10, far 4000;
//     RT = размер бэкбуфера, формат 21 (A8R8G8B8), очистка цветом **0x00000000** ⇒ ПРОЗРАЧНЫЙ ФОН.
//   ⚠ `mathBuildPerspectiveFovLH@0xFF617F` и `mathMatrixLookAt@0xFF5D11` — МИСНЕЙМЫ: по формулам
//     это PerspectiveFovRH (m22 = zf/(zn−zf), m23 = −1) и LookAtRH (zaxis = eye−at). Учтено.
//
// ★ ЕДИНИЦЫ. Экспортёр `asda_skinned_to_gltf.py` масштаб НЕ меняет, glTF-импорт UE множит на 100 ⇒
//   при MeshScale = 0.01 одна единица оригинала = одна единица UE. Замерено: узел `gacha` в .glb
//   (0, 69.795, 211.279) ↔ кость в UE (0, 21127.9, 6979.5).
//
// ★★★ ОСИ — ЧЕРЕЗ ОБЩУЮ ТОЧКУ `AsdaCoord::GameToUe3D`: игра (x, y, z) → UE (−z, x, y).
//   Именно эта формула (а не «мешевая» (x, −z, y)) сохраняет ЛЕВО/ПРАВО оригинала. Проверка:
//   экранное «вправо» у оригинала = `cross(up, eye−at)` (`mathMatrixLookAt@0xFF5D11` кладёт эту ось
//   в столбец 0 матрицы вида, а проекция RH оставляет знак x), у UE — `cross(up, at−eye)`, то есть
//   ПРОТИВОПОЛОЖНАЯ. Компенсирует разницу именно смена знака в переводе осей: у `GameToUe3D`
//   определитель −1, у мешевой (x,−z,y) — +1. С `GameToUe3D` автомат i=1 (θ=+27°, игровой x=−68.1)
//   встаёт СЛЕВА — как в оригинале.
// ★ Следствие: локальный кадр САМОГО меша пришёл из glTF по формуле (x, −z, y) и с мировой
//   формулой не сводится никаким поворотом (определители разные). Ближайший поворот —
//   `MeshYawOffset = −90°`; остаточное расхождение = зеркало автомата по его собственной
//   ширине (меняются местами `L_Gear`/`R_Gear`). Это НЕ дефект этой сцены, а свойство общего
//   конвейера моделей проекта — вынесено в отчёт `Reports/gacha_3d_impl.md` §7.
//
// ★ КЛИПЫ (длительности ЗАМЕРЕНЫ по импортированным ассетам и совпали с реверсом):
//     anim_01 = 2.000 с покой · anim_02 = 2.000 с прокрутка · anim_03 = 2.333 с выдача
//     anim_04 = 2.333 с вскрытие · anim_00 = 2.333 с (в оригинале не загружается — дефект).
//   Кроссфейда нет: флаг +464 в оригинале никогда не ставится в 1, склейка жёсткая.
//   ★ Скорость воспроизведения оригинала = РОВНО 1.0, доказано кодом:
//     `animSkeletonAdvanceTimeMtx@0x112AB50` делает `time += dt` БЕЗ множителя, а
//     `animSampleTransformAtTime@0x11D7D30` берёт кадр как `(uint)(time · 0.03)` с зажимом на
//     `keyCount − 1` (1/0.03 = 33.33 мс = 30 fps; время движка — в МИЛЛИСЕКУНДАХ, что сходится
//     с длительностью в заголовке `.jbb` @0x30: 2000.0 и 2333.33 мс). Переход между клипами —
//     строго по `Game_IsPausedOrMenu@0x99ED60` (`slot[13] >= slot[11]`, «клип доиграл»).
//   ⇒ длительность фазы обязана браться ИЗ КЛИПА, а не из констант: `GetClipLength`.
//
// ★★★ ОСВЕЩЕНИЯ У СЦЕНЫ НЕТ ВООБЩЕ — МНОЖИТЕЛЬ К ТЕКСТУРЕ ТОЖДЕСТВЕННО 1.0.
//   `PetWindow_RenderToTarget@0xB6D5B0` не ставит ни одного источника И выключает `.fx`-путь
//   (`g_bUseEffectPath = 0`), поэтому `system/EmptyProject.fx` (`tSkin`/`VShade`/`PShade`) к
//   автоматам НЕ ОТНОСИТСЯ. Модели рисуются вершинными шейдерами vs.1.1, зашитыми в
//   `system/AsdaStory.exe`, а какой именно — решает `renderApplyMeshState@0x1116FE0`
//   (render_core.c:39781) по биту `0x8` поля `mr+0x004` (`CMeshNode.pStateFlags`, узел +0x30):
//       meshType&0x100 → (mr4&8) ? g_pSkinShaderB   : g_pVtxShaderB
//       meshType&0x200 → (mr4&8) ? g_pVtxShaderC_Alt: g_pVtxShaderC
//   У ВСЕХ 20 подсеток ВСЕХ ПЯТИ `md_gachya_{a..e}_00.jbm` этот бит = 0 (прочитан из данных,
//   проверка `asda_gacha_light_node.assert_all_unlit()` падает, если это изменится), значит
//   берётся НЕОСВЕЩЁННЫЙ вариант, у которого весь блок света заменён на `mov oD0.xyz, c95.w`,
//   а `c95.w = 1.0` (`CRenderer_SetFogAndLightConstants@0x1124D30`). Пиксельная стадия —
//   фиксированный конвейер `COLOROP = MODULATE`, `COLORARG1 = TEXTURE`, `COLORARG2 = DIFFUSE`
//   (render_core.c:40994-40996) ⇒ **на экране ровно байты текстуры**.
//   ⇒ Мастера гачи собраны как Unlit с `Emissive = Tex` (`Tools/asda_gacha_light_node.py`),
//   параметров `Ambient`/`LightColor`/`LightDir` у них больше НЕТ, и собственные
//   `DirectionalLight` у актора не создаются. Плёночная тон-кривая капчера выключена
//   (`ToneCurveAmount = 0`, см. ApplyCamera) — иначе круг sRGB→lin→sRGB перестал бы быть точным.
//   ⚠ Прошлые заходы ставили сюда формулу `.fx`-пути (`0.6 + 0.8·NdotL`, затем плоский ×0.6):
//   числа были верные, но для шейдера, который у гачи выключен. Разбор — `Reports/gacha_color_fix.md`.
//
// ★★★ ПРОЕКЦИЯ КОСТИ. В `.JUI` все дети корня 68000 лежат в (0,0) — экранные позиции ставит КОД:
//   `deathRespawnUI_LayoutResurrectPanel@0xB6E480` (ui_core.c:7128-7180, мисномер — это раскладка
//   сцены гачи, зовётся каждый кадр из `PetWindow_RenderToTarget`):
//     · полоса слота i (68010+10i): проекция ЯКОРЯ (ctrl+556) → (sx − w/2, sy − h/2 + yOff),
//       где yOff = 0 · −40 при N=4 · −50 при N=5;
//     · шар 68057 и его напарник: проекция кости **Bone19** активного клипа выбранного слота
//       → (bx − w/2 + 15, by − h/2 + 5) и (bx − w/2, by − h/2 − 70).
//   Bone19 — кость ПРИЗОВОГО ШАРА: в клипах 0/1/2 она уведена на Y = −834.8 (шар спрятан ниже
//   автомата и проекция уходит за экран), в клипе 3 едет (0.41,70.6,242) → (0.41,12.7,98.1),
//   в клипе 4 стоит на (−0.41,12.7,67.1) — то есть UI-шар буквально следует за 3D-шаром.
//   Промах проекции оригинал помечает координатой −1125 (`worldToScreenProjectSetup@0xB6E360`
//   пишет 0xC48CA000 при z > 1) — здесь то же значение в `kOffScreen`.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Scene.h"
#include "Engine/EngineTypes.h"
#include "UI/AsdaPreviewSceneActor.h"   // ★ волна 9: общая инфраструктура RT (буфер у сцен РАЗНЫЙ)
#include "AsdaGachaSceneActor.generated.h"

class USkeletalMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;

// ★ ВОЛНА 9: базовый класс `AAsdaPreviewSceneActor` держит ОБЩУЮ инфраструктуру RT (капчер,
//   создание буфера, проверку альфы, кисть-материал, `NearClip`/`FarClip`). Общий у сцен только
//   МЕХАНИЗМ; САМ БУФЕР у гачи и у превью питомца РАЗНЫЙ — в оригинале это два независимых
//   render-target'а ("PetWindow2" и "PetWindow"), см. шапку базового класса.
UCLASS(Blueprintable)
class ASDAREBORN_API AAsdaGachaSceneActor : public AAsdaPreviewSceneActor
{
	GENERATED_BODY()
public:
	AAsdaGachaSceneActor();

	// ─────────────────────────── эталонные параметры (правятся в редакторе) ───────────────────────────

	/** Радиус дуги автоматов: `T(0,0,−150)` в `uiBuildPortraitCameraMatrices@0xB6EB50`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Расстановка")
	float MachineRadius = 150.f;

	/** Углы веера в градусах, i=0..4: 0 · +27 · −27 · +54 · −54 (радианы эталона ±0.47123852/±0.94247705). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Расстановка")
	TArray<float> FanAnglesDeg;

	/** Радиус дуги ЯКОРЕЙ для UI-полос: `T(0,0,−350)` (вторая матрица, ctrl+556). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Расстановка")
	float AnchorRadius = 350.f;

	/** Подъём якоря: `m._42 += 200` сразу после второго умножения матриц. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Расстановка")
	float AnchorHeight = 200.f;

	/** glTF-импорт UE множит на 100 ⇒ 0.01 возвращает ОРИГИНАЛЬНЫЕ единицы (замерено, см. шапку). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Расстановка")
	float MeshScale = 0.01f;

	/** Доворот меша: локальная ось «в глубину» пришла из glTF в UE-локальный +Y, мировая — +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Расстановка")
	float MeshYawOffset = -90.f;

	/** Точка, куда смотрит камера: `Vector3_Set(atVec, 0, 100.0, −200.0)`, координаты оригинала. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Камера")
	FVector CameraTargetOrig = FVector(0.f, 100.f, -200.f);

	/** Позиция глаза (Y,Z) при N=1..5 автоматах — пять веток `if (pScene[148] == N)`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Камера")
	TArray<FVector2D> EyeYZByCount;

	/** Позиция глаза (Y,Z), если N не попал в 1..5 (в эталоне обе координаты 150). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Камера")
	FVector2D EyeYZDefault = FVector2D(150.f, 150.f);

	/** ВЕРТИКАЛЬНЫЙ угол обзора: `mathBuildPerspectiveFovLH(…, 0.78539819, …)` = 45.000°. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Камера")
	float FovYDeg = 45.f;

	// ★ ВОЛНА 9: `NearClip` и `FarClip` переехали в базовый `AAsdaPreviewSceneActor` — у обеих
	//   сцен эталона они одинаковы (10 и 4000), и держать две копии значило бы разводить их.

	/** Размер render-target, если игрового вьюпорта нет (в оригинале RT всегда = размер бэкбуфера). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Камера")
	FIntPoint RenderTargetFallback = FIntPoint(1280, 720);

	/**
	 * ★★ СУПЕР-СЭМПЛИНГ КАДРА: кратность, с которой render-target больше вьюпорта; кадр затем
	 * ужимается до экрана кистью UMG (билинейно).
	 *
	 * ДЕФОЛТ 1.0 — как в оригинале (`PetWindow_RenderToTarget@0xB6D5B0`: RT = размер бэкбуфера,
	 * то есть 1 тексель кадра = 1 пиксель экрана). ЗАМЕРЕНО, почему именно 1.0: детализация
	 * рисунка ленты (дисперсия лапласиана в зоне узора, кадр 1280×720, `quality_*.png`) —
	 * 1× без временного сглаживания **6455**, 1× с временным сглаживанием 3976, 2× с ужатием
	 * до экрана 3299. То есть супер-сэмплинг здесь картинку НЕ обостряет, а МЫЛИТ: билинейное
	 * ужатие съедает ровно ту детализацию, на отсутствие которой жалуется владелец. Значение
	 * >1 оставлено как выбор «сглаженные края вместо резкой текстуры»; цена — площадь кадра
	 * растёт в квадрате кратности (при 1920×1080 и 2× это 3840×2160 на КАЖДЫЙ из двух капчеров).
	 *
	 * ⚠ Контракт проекции сохраняется при любой кратности: `ProjectWorldToRender`/`GetRenderSize`
	 * отдают координаты в пикселях ЭКРАНА (RT делится на кратность).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Резкость")
	float RenderSupersample = 1.f;

	/**
	 * ★★★ ГЛАВНАЯ ПРИЧИНА «МЫЛЬНОСТИ»: временное сглаживание (TAA/TSR) внутри капчера.
	 * На момент замера у проекта глобально стоял `r.AntiAliasingMethod` 2 (TAA; с 09.09.2026 в проекте 1 = FXAA), а капчер заведён с
	 * `bAlwaysPersistRenderingState = true` — то есть временной фильтр в нём РАБОТАЕТ и копит
	 * историю, хотя достоверных векторов движения у скиннованных автоматов в SceneCapture нет.
	 * ЗАМЕР: выключение поднимает детализацию рисунка ленты с 3976 до 6455 (+62 %) при том же
	 * размере кадра. В оригинале сглаживания нет вовсе (`PetWindow_RenderToTarget`), поэтому
	 * дефолт — ВЫКЛЮЧЕНО.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Резкость")
	bool bTemporalAAInCapture = false;

	/**
	 * ★★ РЕЗКОСТЬ 3: держать текстуры автоматов на ПОЛНОМ разрешении.
	 * Стриминг оценивает нужный мип по видимости в основном виде игрока, а автоматы видны только
	 * капчеру — резидентным может остаться мелкий мип (замер в headless: `blueprint_get_size_x`
	 * отдавал 32 вместо 1024). Флаг снимает эту зависимость для компонентов сцены.
	 * Глобальные `r.MaxAnisotropy` / `r.MipMapLODBias` из dev-меню (Ctrl+N) действуют и здесь —
	 * они общие для всего кадра, включая капчер, отдельной настройки не требуют.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Резкость")
	bool bForceFullTextureMips = true;

	/** Кость призового шара, по которой оригинал ставит шар 68057 (`List_FindIndexByName(…, "Bone19")`). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Проекция")
	FName BallBoneName = TEXT("Bone19");

	/** Смещение шара 68057 от центра кости: `+15.0` и `+5.0` в `CControl_SetField74_75`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Проекция")
	FVector2D BallScreenOffset = FVector2D(15.f, 5.f);

	/** Смещение второго контрола (взводится вместе с шаром): `+0.0` и `−70.0`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Проекция")
	FVector2D BallSecondScreenOffset = FVector2D(0.f, -70.f);

	// ★★★ Свойств `LightDirUe` / `Ambient` / `LightColor` / `LightGamma` здесь БОЛЬШЕ НЕТ.
	//   Они описывали освещение по `.fx`-пути `EmptyProject.fx`, который сцена гачи выключает
	//   (`g_bUseEffectPath = 0`). Реальный путь — неосвещённые вершинные шейдеры vs.1.1 из
	//   `AsdaStory.exe` (`mov oD0.xyz, c95.w` = 1.0), см. подробный вывод в шапке файла.
	//   Мастера гачи не имеют этих параметров вовсе, поэтому и писать в них нечего:
	//   любое ненулевое «освещение» здесь = отход от оригинала.


	/**
	 * ★★ ПЛЁНОЧНАЯ ТОН-КРИВАЯ UE ВЫКЛЮЧЕНА. У оригинала тонмаппинга нет вовсе: `PShade`
	 * пишет `Tex.xyz · Diffuse.xyz` прямо в A8R8G8B8-цель. Кривая ACES поднимает средние тона
	 * и снижает насыщенность — кадр выглядел ярче и «выцветшее» оригинала. 0 = кривой нет,
	 * 1 = штатная кривая UE (оставлено параметром на случай «красивого UE5»).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Освещение (эталон)")
	float ToneCurveAmount = 0.f;

	/**
	 * ★ Экспозиция капчера ФИКСИРУЕТСЯ. Кадр почти весь чёрный (в него попадают только автоматы),
	 * поэтому автоэкспозиция задирает яркость до полной засветки — первый прогон дал сплошной
	 * белый силуэт без текстур. Значение 1.0 = адаптация выключена (min = max).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Освещение (эталон)")
	float FixedExposure = 1.f;

	/**
	 * Источник ЦВЕТА И АЛЬФЫ — ОДИН капчер.
	 *
	 * ★★★ Раньше здесь стояло два капчера: цвет из `SCS_FinalColorLDR` (у него альфа была 255
	 *   везде) и отдельная маска из `SCS_SceneColorHDR`. Причина была в выключенном
	 *   `r.PostProcessing.PropagateAlpha`: без него альфа сцены не доживает до тонмаппера.
	 *   После включения `r.PostProcessing.PropagateAlpha=1` (`Config/DefaultEngine.ini`,
	 *   `[/Script/Engine.RendererSettings]`) замер по кадрам `sb_m1_color.png` / `sb_m1_mask.png`
	 *   дал альфу ДВУХ капчеров БИТ В БИТ одинаковой (см. `Reports/gacha_alpha_composite.md`
	 *   §0.1: A==0 у 47 194 пикселей, A==255 у 849 968, 0<A<255 у 24 438 — совпало у обоих).
	 *   ⇒ второй капчер убран, альфа берётся из этого же RT.
	 *
	 * Соглашение UE: альфа = ОБРАТНАЯ непрозрачность (0 на непрозрачной геометрии, 255 на фоне),
	 * поэтому мастер склейки считает `Opacity = 1 − SceneColor.A`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Гача|Камера")
	TEnumAsByte<ESceneCaptureSource> CaptureSourceMode = ESceneCaptureSource::SCS_FinalColorLDR;

	// ★ ВОЛНА 9: `SceneBrushMaterialPath` переехал в базовый `AAsdaPreviewSceneActor`: материал
	//   склейки ничего не знает о содержимом кадра и у обеих сцен один и тот же.

	// ─────────────────────────── API ───────────────────────────

	/** Собрать автоматы по числу слотов (у нашей `premium_gatya_data.bin` записей две). */
	UFUNCTION(BlueprintCallable, Category = "Гача")
	void BuildMachines(int32 Count);

	/** Проиграть клип по индексу слота (1 покой · 2 прокрутка · 3 выдача · 4 вскрытие). */
	UFUNCTION(BlueprintCallable, Category = "Гача")
	void PlayClip(int32 MachineIndex, int32 ClipSlot, bool bLoop);

	/** Тот же клип на всех автоматах (покой при открытии сцены). */
	UFUNCTION(BlueprintCallable, Category = "Гача")
	void PlayClipAll(int32 ClipSlot, bool bLoop);

	/**
	 * ★★★ ДЛИТЕЛЬНОСТЬ КЛИПА В СЕКУНДАХ — ЕДИНСТВЕННЫЙ ИСТОЧНИК ПРАВДЫ ДЛЯ ФАЗ.
	 * В оригинале фаза кончается не по таймеру, а по `Game_IsPausedOrMenu@0x99ED60`
	 * (`slot[13] >= slot[11]`, то есть «время клипа догнало его длительность»), причём скорость
	 * ровно 1.0 (`animSkeletonAdvanceTimeMtx@0x112AB50`: `time += dt`). Поэтому длительность фазы
	 * обязана браться ИЗ САМОГО КЛИПА, иначе константа и ассет разъезжаются молча.
	 * Возвращает 0, если клипа нет.
	 */
	UFUNCTION(BlueprintPure, Category = "Гача")
	float GetClipLength(int32 MachineIndex, int32 ClipSlot) const;

	/** ★ 05.11.2026: номер клипа (1…4), последним запущенного на автомате `MachineIndex` через `PlayClip`/`PlayClipAll`
	 *  (порт поля `+620` ребёнка сцены, его читает эталонный `sub_B6ACB0`); −1 — автомата нет или клип не запускался. */
	int32 GetPlayingClip(int32 MachineIndex) const;

	/** Проиграл ли текущий клип автомата до конца (порт `Game_IsPausedOrMenu`). */
	UFUNCTION(BlueprintPure, Category = "Гача")
	bool IsClipFinished(int32 MachineIndex) const;

	// ★ ВОЛНА 9: `GetRenderTarget()` и `GetSceneBrushMaterial()` переехали в базовый
	//   `AAsdaPreviewSceneActor`. Правило прежнее и важное: в кисть контейнера 68056 кладётся
	//   `GetSceneBrushMaterial()`, а НЕ голый render-target — иначе фон будет чёрным.

	/** Сколько автоматов собрано (в эталоне это `pScene[148]`, от него зависит камера и yOffset). */
	UFUNCTION(BlueprintPure, Category = "Гача")
	int32 GetMachineCount() const { return Machines.Num(); }

	/**
	 * ★ Привести render-target к ТЕКУЩЕМУ размеру вьюпорта и пересчитать аспект камеры.
	 * В оригинале RT создаётся размером бэкбуфера (`premiumGatya_CreateRenderTargets@0xB6CA40`:
	 * `sub_93632B`×`sub_91EBD1`) и пересоздаётся вместе со сменой режима устройства, поэтому
	 * зафиксировать размер один раз на открытии сцены нельзя — смена разрешения на открытой сцене
	 * оставила бы кадр старого размера, а проекция контролов считает пиксели ИМЕННО этого кадра.
	 * Возвращает true, если размер изменился (значит кисть контейнера 68056 надо переподставить:
	 * RT и материал пересозданы).
	 */
	UFUNCTION(BlueprintCallable, Category = "Гача")
	bool SyncRenderTargetToViewport();

	// ★ ВОЛНА 9: `GetRenderSize()` переехал в базовый `AAsdaPreviewSceneActor` — тело было
	//   дословно тем же («пиксели ЭКРАНА, а не тексели RT»). Здесь оставлять нельзя: UHT
	//   запрещает повторное `UFUNCTION()` над переопределением.

	/**
	 * ★★★ Экранная позиция кости Bone19 автомата (в пикселях кадра = пикселях экрана).
	 * Порт `deathRespawnUI_LayoutResurrectPanel@0xB6E480`, ветка `List_FindIndexByName(…,"Bone19")`.
	 * Возвращает false и `kOffScreen`, если кости нет или она за пределами near/far.
	 */
	UFUNCTION(BlueprintPure, Category = "Гача|Проекция")
	bool GetBoneScreenPos(int32 MachineIndex, FVector2D& OutPos) const;

	/**
	 * Экранная позиция ЯКОРЯ слота (вторая матрица, ctrl+556) — по ней оригинал ставит полосу
	 * 68010+10i и её кнопки. Возвращает false при промахе проекции.
	 */
	UFUNCTION(BlueprintPure, Category = "Гача|Проекция")
	bool GetSlotScreenPos(int32 SlotIndex, FVector2D& OutPos) const;

	/** Поправка Y для полос слотов: 0 · −40 при 4 автоматах · −50 при 5. */
	UFUNCTION(BlueprintPure, Category = "Гача|Проекция")
	float GetSlotScreenYOffset() const;

	/** Мировая точка якоря слота (для отладки и для проекции). */
	UFUNCTION(BlueprintPure, Category = "Гача|Проекция")
	FVector GetSlotAnchorWorld(int32 SlotIndex) const;

	/** Общая проекция «мир → пиксели кадра» тем же базисом, что и капчер. */
	UFUNCTION(BlueprintPure, Category = "Гача|Проекция")
	bool ProjectWorldToRender(const FVector& World, FVector2D& OutPos) const;

	/** Промах проекции: оригинал пишет −1125.0 в обе координаты (0xC48CA000). */
	static constexpr float kOffScreen = -1125.f;

	/** Координаты оригинала → UE. Своей формулы НЕТ: общая точка проекта `AsdaCoord::GameToUe3D`. */
	static FVector OrigToUe(const FVector& O);

private:
	/** Пересчитать положение/поворот/FOV капчера под текущее число автоматов и размер RT. */
	void ApplyCamera();
	/** Создать (или пересоздать под новый размер) render-target. */
	void EnsureRenderTarget();
	/** Позиция глаза в координатах ОРИГИНАЛА для N автоматов. */
	FVector EyeOrigForCount(int32 Count) const;
	/** Раздать материалам автомата параметры света оригинала (создаёт MID на слот). */

	// ★ ВОЛНА 9: `Capture`, `RT`, `BrushMID`, `RtSize`, `ScreenSize` переехали в базовый
	//   `AAsdaPreviewSceneActor` (имена сохранены намеренно — это общая инфраструктура, а не
	//   общий буфер). Здесь остаётся только то, что есть у гачи и чего нет у превью питомца.
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> Machines;
	// ★ 05.11.2026: последний запущенный клип каждого автомата (индекс = автомат; −1 — не запускался), см. `GetPlayingClip`.
	TArray<int32> MachineClip;
};
