// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОПРОВЕРКА ВОЛНЫ 1 АУДИТА ДУБЛЕЙ (14.09.2026) — ПУТЁМ ИГРОКА (`-game`, закон `ui-check-game-path-geometry`).
//   Только консольные команды диагностики. Сами пробники пакетов не шлют и состояние систем не пишут;
//   `asda.UI.Seq` выполняет те консольные команды, которые ему передали (их последствия — на вызывающем),
//   `asda.UI.NoticeProbe` показывает окно 1500 и строку чата. Кадры и лог — `Reports/wave1_ui/`.
//   ★ Ревизия 14.09.2026: удалён `asda.UI.GachaFromPost` — он затирал запись приза сцены гачи выдуманной
//     записью и открывал настоящее окно 68500, чьё «ОК» отправило бы 6581 с выдуманной ячейкой.
//
//   asda.UI.Seq <шаг>|<шаг>|…   — шаги по таймеру (`-ExecCmds` выполняет всё в ОДНОМ кадре ДО отрисовки):
//                                 `wait <сек>`, `shot <имя>` (кадр с UI; следующий шаг ждёт, пока снимок
//                                 реально снят), `geom <узел>` (живая геометрия узла открытых окон),
//                                 остальное — консольная команда. Шаги строго по очереди.
//   asda.UI.IconScan [N]        — сколько предметов имеют клетку ЗА КРАЕМ логического атласа (там заворот
//                                 E3 меняет картинку) + первые N кодов.
//   asda.UI.IconProbe <код>     — кисть общей точки `AsdaUi::ApplyItemIcon` с заворотом и без: UV, размер.
//   asda.UI.NoticeProbe <loc>   — фасад `AsdaNotice`: окно 1500 (MsgBoxLoc) + строка чата цветом обработчика.
//   asda.UI.TreeGeom <метка> <подстрока класса окна> — (15.09.2026, перенос разметки чата в ассет) ВСЕ узлы
//                                 живых окон: своя видимость, видимость цепочкой и экранный прямоугольник Slate.
#include "UI/AsdaUiHelpers.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#if __has_include("UI/AsdaNotice.h")
#include "UI/AsdaNotice.h"   // фасад нотисов (есть только в сборке ПОСЛЕ волны 1)
#define ASDA_WAVE1_NEW 1
#else
#define ASDA_WAVE1_NEW 0
#endif
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/Package.h"

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): самотесты и стенды в Shipping не входят

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWave1, Log, All);

namespace
{
	// Каталог кадров самопроверки: <проект>/../../_re/reports/wave1_ui/.
	FString Wave1ShotDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../../_re/reports/wave1_ui/"));
	}

	// Все живые узлы с именем `Name` во всех UUserWidget мира: имя, класс, видимость, экранный прямоугольник.
	void LogNodeGeometry(UWorld* W, const FString& Name)
	{
		int32 Found = 0;
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* UW = *It;
			if (!UW || UW->GetWorld() != W || !UW->WidgetTree) { continue; }
			UW->WidgetTree->ForEachWidget([&](UWidget* Wd)
			{
				if (!Wd || Wd->GetName() != Name) { return; }
				++Found;
				FVector2D Mn, Mx;
				const bool bRect = AsdaUi::WidgetAbsRect(Wd, Mn, Mx);
				FString Uv;
				if (const UImage* Img = Cast<UImage>(Wd))
				{
					const FBox2D R = Img->GetBrush().GetUVRegion();
					Uv = FString::Printf(TEXT(" UV=(%.4f,%.4f)-(%.4f,%.4f) кисть=%.0fx%.0f ресурс=%s"),
						R.Min.X, R.Min.Y, R.Max.X, R.Max.Y, Img->GetBrush().ImageSize.X, Img->GetBrush().ImageSize.Y,
						Img->GetBrush().GetResourceObject() ? *Img->GetBrush().GetResourceObject()->GetName() : TEXT("нет"));
				}
				UE_LOG(LogAsdaWave1, Log, TEXT("[волна1] geom %s (%s в %s): видимость=%d прямоугольник=%s (%.0f,%.0f)-(%.0f,%.0f)%s"),
					*Name, *Wd->GetClass()->GetName(), *UW->GetName(), (int32)Wd->GetVisibility(),
					bRect ? TEXT("да") : TEXT("НЕТ"), Mn.X, Mn.Y, Mx.X, Mx.Y, *Uv);
			});
		}
		if (Found == 0) { UE_LOG(LogAsdaWave1, Warning, TEXT("[волна1] geom %s: узла нет ни в одном окне"), *Name); }
	}

	// Состояние одной последовательности шагов (живёт в тикере, пока шаги не кончатся).
	struct FWave1Seq
	{
		TArray<FString> Steps;          // шаги по порядку
		int32 Index = 0;                // следующий шаг
		double NextTime = 0.0;          // раньше этого момента шаг не выполняется (`wait`)
		bool bAwaitShot = false;        // заказан снимок — ждём, пока движок его снимет
		int32 FramesAfterShot = 0;      // кадров после снятия снимка (запас в один кадр)
		TWeakObjectPtr<UWorld> World;   // мир, в котором выполнять команды
	};

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GWave1Seq(
		TEXT("asda.UI.Seq"),
		TEXT("asda.UI.Seq <шаг>|<шаг>|… — шаги по очереди: `wait <сек>`, `shot <имя>` (следующий шаг ждёт снятия кадра), `geom <узел>`, иначе консольная команда."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			TSharedRef<FWave1Seq> S = MakeShared<FWave1Seq>();
			FString::Join(Args, TEXT(" ")).ParseIntoArray(S->Steps, TEXT("|"), true);
			for (FString& St : S->Steps) { St.TrimStartAndEndInline(); }
			S->World = W;
			UE_LOG(LogAsdaWave1, Log, TEXT("[волна1] последовательность: %d шагов"), S->Steps.Num());
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool
			{
				// ★ Снимок снимается в конце отрисовки кадра ПОСЛЕ заказа. Пока он не снят (и ещё один кадр
				//   после), следующий шаг не выполняем — иначе закрытие окна успевало раньше снимка.
				if (S->bAwaitShot)
				{
					if (FScreenshotRequest::IsScreenshotRequested()) { return true; }
					if (S->FramesAfterShot++ < 1) { return true; }
					S->bAwaitShot = false;
				}
				if (FPlatformTime::Seconds() < S->NextTime) { return true; }
				if (!S->Steps.IsValidIndex(S->Index)) { return false; }
				const FString Step = S->Steps[S->Index++];
				UWorld* LiveW = S->World.Get();
				if (Step.StartsWith(TEXT("wait ")))
				{
					S->NextTime = FPlatformTime::Seconds() + FCString::Atod(*Step.Mid(5));
				}
				else if (Step.StartsWith(TEXT("shot ")))
				{
					const FString File = Wave1ShotDir() + Step.Mid(5).TrimStartAndEnd() + TEXT(".png");
					FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
					S->bAwaitShot = true;
					S->FramesAfterShot = 0;
					UE_LOG(LogAsdaWave1, Log, TEXT("[волна1] кадр заказан → %s"), *File);
				}
				else if (Step.StartsWith(TEXT("geom ")))
				{
					if (LiveW) { LogNodeGeometry(LiveW, Step.Mid(5).TrimStartAndEnd()); }
				}
				else if (GEngine)
				{
					UE_LOG(LogAsdaWave1, Log, TEXT("[волна1] exec: %s"), *Step);
					GEngine->Exec(LiveW, *Step);
				}
				return true;
			}));
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GWave1IconScan(
		TEXT("asda.UI.IconScan"),
		TEXT("asda.UI.IconScan [N] — предметы, у которых клетка иконки выходит за край ЛОГИЧЕСКОГО атласа (здесь заворот E3 меняет картинку)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
			if (!IT || !TexMgr) { UE_LOG(LogAsdaWave1, Warning, TEXT("[волна1] IconScan: нет таблицы предметов/менеджера текстур")); return; }
			const int32 ShowN = Args.Num() ? FCString::Atoi(*Args[0]) : 20;
			TMap<FString, FIntPoint> AtlasSize;
			int32 Total = 0, NoAtlas = 0, Out = 0, Shown = 0;
			for (const FAsdaItemRecord& R : IT->GetAllItems())
			{
				const FAsdaItemIcon Ic = IT->GetItemIcon(R.ItemCode, true);
				if (!Ic.IsValid()) { continue; }
				++Total;
				FIntPoint* Sz = AtlasSize.Find(Ic.Atlas);
				if (!Sz)
				{
					UTexture2D* Tex = TexMgr->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(Ic.Atlas));   // ★ волна 2 п. 7
					Sz = &AtlasSize.Add(Ic.Atlas, Tex ? AsdaUi::LogicalAtlasSize(Tex) : FIntPoint(0, 0));
				}
				if (Sz->X <= 1 || Sz->Y <= 1) { ++NoAtlas; continue; }
				if (Ic.CellX + Ic.CellSize > Sz->X || Ic.CellY + Ic.CellSize > Sz->Y)
				{
					++Out;
					if (Shown++ < ShowN)
					{
						UE_LOG(LogAsdaWave1, Log, TEXT("[волна1] за краем: код %d «%s» атлас %s %dx%d клетка (%d,%d) размер %d"),
							R.ItemCode, *R.ItemName, *Ic.Atlas, Sz->X, Sz->Y, Ic.CellX, Ic.CellY, Ic.CellSize);
					}
				}
			}
			UE_LOG(LogAsdaWave1, Log, TEXT("[волна1] IconScan: иконок %d, атлас не найден %d, клетка за краем %d (атласов %d)"),
				Total, NoAtlas, Out, AtlasSize.Num());
		}));
#endif // ASDA_DEV_CONSOLE

	// ★ 15.09.2026 (перенос статической видимости окна чата в ассет): ПОЛНОЕ ЖИВОЕ ДЕРЕВО окон, чей класс
	//   содержит подстроку, — для сравнения «до/после» путём игрока (закон `ui-check-game-path-geometry`).
	//   Строка на узел: метка, класс окна, имя, класс узла, собственная видимость (номер ESlateVisibility),
	//   видимость цепочкой (сам узел и все родители не Collapsed/Hidden), прямоугольник Slate, прозрачность.
	//   Только чтение: ничего не меняет.
	// ★★★ ИМЕНОВАННЫЙ КАДР ВМЕСТЕ СО СЛОЕМ SLATE (24.09.2026).
	//   ЗАЧЕМ. Движковая `shot showui` в `-RenderOffscreen` кладёт кадр В ТРАССУ («Tracing Screenshot
	//   "showui" taken»), файла PNG не появляется — то есть посмотреть глазами нечего. А закон
	//   `full-1to1-reproduction` требует именно КАДРА: дефект владельца «превью питомца пустое»
	//   прожил волну ровно потому, что проверяли лог, а не картинку.
	//   ЧТО ДЕЛАЕТ: `FScreenshotRequest::RequestScreenshot(имя, bShowUI=true)` — единственный путь,
	//   который пишет PNG СО СЛОЕМ SLATE (`HighResShot` интерфейс не пишет; разбор уже был сделан
	//   в `AsdaTitleSubsystem.cpp:440-442` и здесь не повторяется, а переиспользуется).
	//   Файл ложится в `<Saved>/Screenshots/WindowsEditor/<имя>.png`, и при `-saveddirsuffix` это
	//   изолированная папка прогона.
	//   ! ПОПРАВКА РЕВИЗИИ (МЕЛКОЕ): это верно ДЛЯ ЭТОЙ КОМАНДЫ, но НЕ для шага `shot` внутри
	//   `asda.UI.Seq` — тот пишет в жёстко зашитый `Reports/wave1_ui/` (см. `:46-49`, `:122`)
	//   и `-saveddirsuffix` его не изолирует. Прогоны должны звать `asda.UI.Shot`, а не `shot`.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GWave1Shot(
		TEXT("asda.UI.Shot"),
		TEXT("asda.UI.Shot <имя> — кадр вьюпорта ВМЕСТЕ со слоем UI в <Saved>/Screenshots/<имя>.png. ")
		TEXT("Движковая `shot showui` под -RenderOffscreen пишет только в трассу и файла не даёт."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogAsdaWave1, Warning, TEXT("нужно: asda.UI.Shot <имя>"));
				return;
			}
			// bAddFilenameSuffix=false — имя остаётся ровно тем, что заказали: прогон ищет файл по нему.
			FScreenshotRequest::RequestScreenshot(Args[0], /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			UE_LOG(LogAsdaWave1, Display, TEXT("[кадр] заказан снимок «%s» (со слоем UI)"), *Args[0]);
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GWave1TreeGeom(
		TEXT("asda.UI.TreeGeom"),
		TEXT("asda.UI.TreeGeom <метка> <подстрока класса окна> — все узлы живых окон: видимость (своя и цепочкой) и экранный прямоугольник."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			if (!W || Args.Num() < 2)
			{
				UE_LOG(LogAsdaWave1, Warning, TEXT("нужно: asda.UI.TreeGeom <метка> <подстрока класса окна>"));
				return;
			}
			const FString Tag = Args[0];        // метка снимка (состояние сценария)
			const FString ClassSub = Args[1];   // подстрока имени класса окна (напр. WBP_CHAT_NEW)
			int32 Windows = 0;                  // сколько живых окон попало под фильтр
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* UW = *It;
				if (!UW || UW->IsTemplate() || UW->GetWorld() != W || !UW->WidgetTree) { continue; }
				const FString Cls = UW->GetClass()->GetName();
				if (!Cls.Contains(ClassSub)) { continue; }
				++Windows;
				UE_LOG(LogAsdaWave1, Log, TEXT("[дерево] %s ОКНО %s вид=%d в_вьюпорте=%d"),
					*Tag, *Cls, (int32)UW->GetVisibility(), UW->IsInViewport() ? 1 : 0);
				UW->WidgetTree->ForEachWidget([&](UWidget* Wd)
				{
					if (!Wd) { return; }
					bool bChain = true;   // узел и вся цепочка родителей не свёрнуты/не скрыты
					for (const UWidget* P = Wd; P; P = P->GetParent())
					{
						const ESlateVisibility V = P->GetVisibility();
						if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { bChain = false; break; }
					}
					FVector2D Mn(0.f, 0.f), Mx(0.f, 0.f);
					const bool bRect = AsdaUi::WidgetAbsRect(Wd, Mn, Mx);
					UE_LOG(LogAsdaWave1, Log,
						TEXT("[дерево] %s %s | %s | %s | вид=%d цепочкой=%d | прям=%s (%.1f,%.1f)-(%.1f,%.1f) | прозр=%.2f"),
						*Tag, *Cls, *Wd->GetName(), *Wd->GetClass()->GetName(), (int32)Wd->GetVisibility(), bChain ? 1 : 0,
						bRect ? TEXT("да") : TEXT("нет"), Mn.X, Mn.Y, Mx.X, Mx.Y, Wd->GetRenderOpacity());
				});
			}
			UE_LOG(LogAsdaWave1, Log, TEXT("[дерево] %s: окон с «%s» — %d"), *Tag, *ClassSub, Windows);
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_WAVE1_NEW   // пробник общей точки иконки — только в сборке ПОСЛЕ волны 1
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GWave1IconProbe(
		TEXT("asda.UI.IconProbe"),
		TEXT("asda.UI.IconProbe <код> — кисть общей точки AsdaUi::ApplyItemIcon с заворотом и без (UV, размер)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			if (!GI || Args.Num() < 1) { UE_LOG(LogAsdaWave1, Warning, TEXT("нужно: asda.UI.IconProbe <код>")); return; }
			const int32 Code = FCString::Atoi(*Args[0]);
			for (const bool bWrap : { false, true })
			{
				UImage* Img = NewObject<UImage>(GetTransientPackage());
				const bool bOk = AsdaUi::ApplyItemIcon(GI, Img, Code, FVector2D::ZeroVector, true, bWrap);
				const FBox2D R = Img->GetBrush().GetUVRegion();
				UE_LOG(LogAsdaWave1, Log, TEXT("[волна1] IconProbe код %d заворот=%s: %s UV=(%.4f,%.4f)-(%.4f,%.4f) кисть=%.0fx%.0f"),
					Code, bWrap ? TEXT("да") : TEXT("нет"), bOk ? TEXT("кисть поставлена") : TEXT("НЕТ данных/атласа"),
					R.Min.X, R.Min.Y, R.Max.X, R.Max.Y, Img->GetBrush().ImageSize.X, Img->GetBrush().ImageSize.Y);
			}
		}));
#endif // ASDA_DEV_CONSOLE

#endif

#if ASDA_WAVE1_NEW   // пробник фасада нотисов — только в сборке ПОСЛЕ волны 1
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GWave1NoticeProbe(
		TEXT("asda.UI.NoticeProbe"),
		TEXT("asda.UI.NoticeProbe <loc> — фасад AsdaNotice: окно 1500 (MsgBoxLoc, ItemRefused) + строка чата цветом обработчика (System)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			if (!GI || Args.Num() < 1) { UE_LOG(LogAsdaWave1, Warning, TEXT("нужно: asda.UI.NoticeProbe <loc>")); return; }
			const int32 LocId = FCString::Atoi(*Args[0]);
			const bool bWin = AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::ItemRefused, LocId);
			const bool bChat = AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, AsdaNotice::LocRaw(GI, LocId, true), AsdaNoticeColor::Olive);
			UE_LOG(LogAsdaWave1, Log, TEXT("[волна1] NoticeProbe %d: окно=%s чат=%s текст «%s»"), LocId,
				bWin ? TEXT("да") : TEXT("нет"), bChat ? TEXT("да") : TEXT("нет"), *AsdaNotice::Loc(GI, LocId));
		}));
#endif // ASDA_DEV_CONSOLE
#endif
}

#endif // !UE_BUILD_SHIPPING
