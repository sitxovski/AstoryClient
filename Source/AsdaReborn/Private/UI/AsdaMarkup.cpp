// Copyright (c) 2026 @sitxovski (Discord)
#include "UI/AsdaMarkup.h"
#include "AsdaDevConsole.h"      // ★ 30.10.2026: ASDA_DEV_CVAR — ключ asda.Text.CompoundTags
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт
#include "UI/AsdaRichDecorator.h"
#include "UI/AsdaUiHelpers.h"   // ★ D1: единая sRGB→linear конверсия (дедуп 2026-08-08)
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"   // CaptureFmt: стиль JUI-строки до подмены на rich
#include "Components/RichTextBlockDecorator.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/DataTable.h"

// Совпадает ли подстрока T начиная с S[i].
static bool AsdaMkMatch(const FString& S, int32 i, const TCHAR* T)
{
	for (int32 k = 0; T[k] != 0; ++k)
	{
		if (i + k >= S.Len() || S[i + k] != T[k]) return false;
	}
	return true;
}

// Нормализовать hex-цвет тега FC. В данных 8 hex = AARRGGBB (эталон: color &= 0x00FFFFFF|alpha) → берём RRGGBB.
static FString AsdaMkColor(const FString& In)
{
	FString H;
	for (TCHAR c : In) if (FChar::IsHexDigit(c)) H.AppendChar(c);
	if (H.Len() > 6) H = H.Right(6);   // AARRGGBB → RRGGBB
	if (H.Len() < 6) return FString();
	return H.ToUpper();
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★ [ОРИГИНАЛ] 30.10.2026 — СОСТАВНОЙ ТЕГ `<FC=AARRGGBB,FB>` (баг игроков: «сырые теги в текстах»).
//   ЭТАЛОН `textParseColorTag@0x12C8700` (font_text.c:7924) разбирает ВНУТРИ одних угловых скобок ЦЕПОЧКУ
//   подтегов: после каждого (FC — 8 hex со смещения +3, FS — 2 цифры, FB/FO/FD) проверяет `'>'`, а иначе
//   сдвигается на символ и ищет следующий подтег — разделитель (в данных `,`) ничего не значит.
//   В данных такие теги есть: soulsoulmatetext (RU/EN), loginhelpertext (EN) — `<FC=FFCC0055,FB>` и др.
//   ДЕФЕКТ ПОРТА: ветка `<FC=` брала ВСЁ до `>` как hex — из «FFCC0055,FB» выходило «FFCC0055FB» → правые
//   6 = «0055FB» (СИНИЙ вместо малинового) и без жирного. Перехватываем ТОЛЬКО теги из ≥2 подтегов —
//   одиночные идут прежними ветками без изменений. Ключ `asda.Text.CompoundTags` (0 = прежний разбор).
static ASDA_DEV_CVAR(int32, CVarCompoundTags,
	TEXT("asda.Text.CompoundTags"), 1,
	TEXT("1 = составной тег разметки <FC=AARRGGBB,FB> разбирается по подтегам, как эталон textParseColorTag; "
	     "0 = прежний разбор порта (весь хвост до '>' считался цветом)."),
	ECVF_Default);

// Разобранный составной тег (что он меняет в состоянии формата).
struct FAsdaMkTag
{
	bool    bColor = false;       // был подтег FC
	FString Color;                // RRGGBB (AsdaMkColor) — пусто, если hex неполный
	bool    bSize = false;        // был подтег FS
	int32   Size = 0;             // кегль FS
	int32   StyleBits = 0;        // 1 FB | 2 FO | 4 FD (биты как в ToRichMarkup)
	int32   LastStyle = 0;        // последний стиль-подтег (для bitmap-сегмента, у него стиль один)
	bool    bOutlineColor = false;// FO=RRGGBB (расширение порта)
	FString OutlineColor;         // цвет обводки FO=
	int32   End = 0;              // индекс сразу после '>'
};

// Попытка разобрать составной тег, начинающийся с `S[i] == '<'`. true — только для ≥2 подтегов,
//   закрытых `>` (одиночные теги не трогаем — их разбирают прежние ветки).
static bool AsdaMkParseCompound(const FString& S, int32 i, FAsdaMkTag& T)
{
	if (CVarCompoundTags.GetValueOnAnyThread() == 0) { return false; }
	const int32 N = S.Len();
	if (i + 2 >= N || S[i] != TEXT('<') || S[i + 1] != TEXT('F')) { return false; }
	T = FAsdaMkTag();
	int32 j = i + 1;
	int32 Parts = 0;
	auto ReadHex = [&](int32 k, FString& Hex) -> int32
	{
		if (k < N && (S[k] == TEXT('=') || S[k] == TEXT(':'))) { ++k; }   // эталон разделитель не проверяет
		while (k < N && Hex.Len() < 8 && FChar::IsHexDigit(S[k])) { Hex.AppendChar(S[k]); ++k; }
		return k;
	};
	while (j < N)
	{
		if (S[j] == TEXT('>')) { break; }
		if (S[j] == TEXT('<')) { return false; }        // следующий тег раньше '>' — это не тег
		const TCHAR a = S[j];
		const TCHAR b = (j + 1 < N) ? S[j + 1] : 0;
		if (a == TEXT('F') && b == TEXT('C'))
		{
			FString Hex; j = ReadHex(j + 2, Hex);
			T.bColor = true; T.Color = AsdaMkColor(Hex); ++Parts; continue;
		}
		if (a == TEXT('F') && b == TEXT('S'))
		{
			int32 k = j + 2; FString Num;
			if (k < N && (S[k] == TEXT('=') || S[k] == TEXT(':'))) { ++k; }
			while (k < N && FChar::IsDigit(S[k])) { Num.AppendChar(S[k]); ++k; }
			T.bSize = true; T.Size = FCString::Atoi(*Num); j = k; ++Parts; continue;
		}
		if (a == TEXT('F') && b == TEXT('O') && j + 2 < N && (S[j + 2] == TEXT('=') || S[j + 2] == TEXT(':')))
		{
			FString Hex; j = ReadHex(j + 2, Hex);
			T.StyleBits |= 2; T.LastStyle = 2; T.bOutlineColor = true; T.OutlineColor = AsdaMkColor(Hex); ++Parts; continue;
		}
		if (a == TEXT('F') && (b == TEXT('B') || b == TEXT('O') || b == TEXT('D')))
		{
			const int32 Bit = (b == TEXT('B')) ? 1 : (b == TEXT('O')) ? 2 : 4;
			T.StyleBits |= Bit; T.LastStyle = Bit; j += 2; ++Parts; continue;
		}
		++j;                                             // разделитель (эталон: `++*a3` и снова поиск подтега)
	}
	if (j >= N || Parts < 2) { return false; }
	T.End = j + 1;
	return true;
}

FString FAsdaMarkup::ToRichMarkup(const FString& Raw)
{
	FString Out, Seg, Color;
	int32 Size = 0;
	// ★★★ БЫЛО: одно число 0/1/2/3 — стили ВЗАИМНО ИСКЛЮЧАЛИ друг друга, хотя в эталоне это БИТЫ
	//   одного поля стиля шрифта (font_text.c): 0x200000 жирный, 0x400000 тень, 0x800000 обводка —
	//   и они складываются. Стало: БИТОВАЯ МАСКА 1 bold(FB) | 2 outline(FO) | 4 shadow(FD).
	//   Понадобилось для строк чата: они несут тень 0x400000 (флаг списка 2521) И, по требованию
	//   юзера, обводку цветом канала одновременно.
	int32 Style = 0;
	FString OutlineColor;               // <FO=RRGGBB> — ЦВЕТ обводки; пусто ⇒ чёрная (как <FO>)
	const int32 N = Raw.Len();

	auto EscapeAppend = [](FString& Dst, const FString& Src)
	{
		for (TCHAR c : Src)
		{
			if      (c == TEXT('&')) Dst += TEXT("&amp;");
			else if (c == TEXT('<')) Dst += TEXT("&lt;");
			else if (c == TEXT('>')) Dst += TEXT("&gt;");
			else if (c == TEXT('"')) Dst += TEXT("&quot;");
			else Dst.AppendChar(c);
		}
	};
	auto HasFmt = [&]() { return !Color.IsEmpty() || Size > 0 || Style != 0; };
	auto Flush = [&]()
	{
		if (Seg.IsEmpty()) return;
		if (!HasFmt())
		{
			EscapeAppend(Out, Seg);
		}
		else
		{
			Out += TEXT("<a");
			if (!Color.IsEmpty()) Out += FString::Printf(TEXT(" c=\"%s\""), *Color);
			if (Style & 1) Out += TEXT(" b=\"1\"");
			if (Style & 2)
			{
				Out += TEXT(" o=\"1\"");
				if (!OutlineColor.IsEmpty()) Out += FString::Printf(TEXT(" oc=\"%s\""), *OutlineColor);
			}
			if (Style & 4) Out += TEXT(" d=\"1\"");
			if (Size > 0) Out += FString::Printf(TEXT(" s=\"%d\""), Size);
			Out += TEXT(">");
			EscapeAppend(Out, Seg);
			Out += TEXT("</>");
		}
		Seg.Reset();
	};

	int32 i = 0;
	while (i < N)
	{
		const TCHAR ch = Raw[i];
		if (ch == TEXT('<'))
		{
			if (AsdaMkMatch(Raw, i, TEXT("</F>"))) { Flush(); Color.Reset(); Size = 0; Style = 0; OutlineColor.Reset(); i += 4; continue; }
			// ★ 30.10.2026 [ОРИГИНАЛ]: составной тег `<FC=…,FB>` (шапка `AsdaMkParseCompound`).
			if (FAsdaMkTag T; AsdaMkParseCompound(Raw, i, T))
			{
				Flush();
				if (T.bColor) { Color = T.Color; }
				if (T.bSize)  { Size = T.Size; }
				Style |= T.StyleBits;
				if (T.bOutlineColor) { OutlineColor = T.OutlineColor; }
				else if (T.StyleBits & 2) { OutlineColor.Reset(); }
				i = T.End; continue;
			}
			if (AsdaMkMatch(Raw, i, TEXT("<FC=")) || AsdaMkMatch(Raw, i, TEXT("<FC:")))
			{
				int32 j = i + 4; FString Hex;
				while (j < N && Raw[j] != TEXT('>')) { Hex.AppendChar(Raw[j]); ++j; }
				Flush(); Color = AsdaMkColor(Hex);
				i = (j < N) ? j + 1 : j; continue;
			}
			if (AsdaMkMatch(Raw, i, TEXT("<FS")))
			{
				int32 j = i + 3; FString Num;
				while (j < N && FChar::IsDigit(Raw[j])) { Num.AppendChar(Raw[j]); ++j; }
				while (j < N && Raw[j] != TEXT('>')) ++j;   // до закрывающего '>'
				Flush(); Size = FCString::Atoi(*Num);
				i = (j < N) ? j + 1 : j; continue;
			}
			// ★ РАСШИРЕНИЕ ГРАММАТИКИ (2026-08-06): <FO=RRGGBB> — обводка ЗАДАННЫМ цветом.
			//   Голый <FO> (эталонный тег 0x800000) по-прежнему даёт ЧЁРНУЮ обводку — поведение
			//   не изменилось. Цветная форма нужна строкам чата: сообщение белое, обводка — цветом
			//   канала (требование юзера, отступление от оригинала, см. chat_main_window.md Д21 п.6).
			if (AsdaMkMatch(Raw, i, TEXT("<FO=")) || AsdaMkMatch(Raw, i, TEXT("<FO:")))
			{
				int32 j = i + 4; FString Hex;
				while (j < N && Raw[j] != TEXT('>')) { Hex.AppendChar(Raw[j]); ++j; }
				Flush(); Style |= 2; OutlineColor = AsdaMkColor(Hex);
				i = (j < N) ? j + 1 : j; continue;
			}
			if (AsdaMkMatch(Raw, i, TEXT("<FB>"))) { Flush(); Style |= 1; i += 4; continue; }
			if (AsdaMkMatch(Raw, i, TEXT("<FO>"))) { Flush(); Style |= 2; OutlineColor.Reset(); i += 4; continue; }
			if (AsdaMkMatch(Raw, i, TEXT("<FD>"))) { Flush(); Style |= 4; i += 4; continue; }
			Seg.AppendChar(ch); ++i; continue;      // одиночный '<' — не тег
		}
		if (ch == TEXT('\\') && i + 1 < N)
		{
			const TCHAR n = Raw[i + 1];
			if (n == TEXT('n') || n == TEXT('N')) { Flush(); Out += TEXT("\n"); i += 2; while (i < N && Raw[i] == TEXT(' ')) ++i; continue; }
			if (n == TEXT('t')) { Seg.AppendChar(TEXT('\t')); i += 2; continue; }
			if (n == TEXT('r')) { i += 2; continue; }
		}
		if (ch == TEXT('\n')) { Flush(); Out += TEXT("\n"); ++i; while (i < N && Raw[i] == TEXT(' ')) ++i; continue; }   // trim ведущих пробелов строки (отступ в данных)
		if (ch == TEXT('\r')) { ++i; continue; }
		Seg.AppendChar(ch); ++i;
	}
	Flush();
	return Out;
}

void FAsdaMarkup::ToSegments(const FString& Raw, TArray<FAsdaTextSegment>& Out)
{
	Out.Reset();
	FString Seg;
	FLinearColor   Color = FLinearColor::White;
	EAsdaTextStyle Style = EAsdaTextStyle::None;
	int32 Size = 0;
	const int32 N = Raw.Len();

	auto Flush = [&]()
	{
		if (Seg.IsEmpty()) return;
		FAsdaTextSegment S; S.Text = Seg; S.Color = Color; S.Style = Style; S.Size = Size;
		Out.Add(MoveTemp(S));
		Seg.Reset();
	};
	auto SetColor = [&](const FString& Hex)
	{
		const FString H = AsdaMkColor(Hex);           // AARRGGBB → RRGGBB (эталон маскирует альфу)
		// ★ D1 (дедуп UI 2026-08-08): единая AsdaUi::SrgbHexToLinear (6-hex ⇒ альфа=1). Вид не меняется.
		Color = (H.Len() == 6) ? AsdaUi::SrgbHexToLinear(H) : FLinearColor::White;
	};

	int32 i = 0;
	while (i < N)
	{
		const TCHAR ch = Raw[i];
		if (ch == TEXT('<'))
		{
			if (AsdaMkMatch(Raw, i, TEXT("</F>"))) { Flush(); Color = FLinearColor::White; Size = 0; Style = EAsdaTextStyle::None; i += 4; continue; }
			// ★ 30.10.2026 [ОРИГИНАЛ]: составной тег `<FC=…,FB>` (шапка `AsdaMkParseCompound`); стиль у сегмента один — последний.
			if (FAsdaMkTag T; AsdaMkParseCompound(Raw, i, T))
			{
				Flush();
				if (T.bColor) { Color = (T.Color.Len() == 6) ? AsdaUi::SrgbHexToLinear(T.Color) : FLinearColor::White; }
				if (T.bSize)  { Size = T.Size; }
				if (T.LastStyle == 1) { Style = EAsdaTextStyle::Bold; }
				else if (T.LastStyle == 2) { Style = EAsdaTextStyle::Outline; }
				else if (T.LastStyle == 4) { Style = EAsdaTextStyle::Shadow; }
				i = T.End; continue;
			}
			if (AsdaMkMatch(Raw, i, TEXT("<FC=")) || AsdaMkMatch(Raw, i, TEXT("<FC:")))
			{
				int32 j = i + 4; FString Hex;
				while (j < N && Raw[j] != TEXT('>')) { Hex.AppendChar(Raw[j]); ++j; }
				Flush(); SetColor(Hex);
				i = (j < N) ? j + 1 : j; continue;
			}
			if (AsdaMkMatch(Raw, i, TEXT("<FS")))
			{
				int32 j = i + 3; FString Num;
				while (j < N && FChar::IsDigit(Raw[j])) { Num.AppendChar(Raw[j]); ++j; }
				while (j < N && Raw[j] != TEXT('>')) ++j;
				Flush(); Size = FCString::Atoi(*Num);
				i = (j < N) ? j + 1 : j; continue;
			}
			// ★ <FO=RRGGBB> — та же обводка, но с цветом. Bitmap-виджет цвет обводки не поддерживает
			//   (FAsdaTextSegment его не хранит), поэтому здесь тег ТОЛЬКО распознаётся, чтобы hex
			//   не утёк в видимый текст; стиль ставится тот же — Outline.
			if (AsdaMkMatch(Raw, i, TEXT("<FO=")) || AsdaMkMatch(Raw, i, TEXT("<FO:")))
			{
				int32 j = i + 4;
				while (j < N && Raw[j] != TEXT('>')) ++j;
				Flush(); Style = EAsdaTextStyle::Outline;
				i = (j < N) ? j + 1 : j; continue;
			}
			if (AsdaMkMatch(Raw, i, TEXT("<FB>"))) { Flush(); Style = EAsdaTextStyle::Bold;    i += 4; continue; }
			if (AsdaMkMatch(Raw, i, TEXT("<FO>"))) { Flush(); Style = EAsdaTextStyle::Outline; i += 4; continue; }
			if (AsdaMkMatch(Raw, i, TEXT("<FD>"))) { Flush(); Style = EAsdaTextStyle::Shadow;  i += 4; continue; }
			Seg.AppendChar(ch); ++i; continue;
		}
		if (ch == TEXT('\\') && i + 1 < N)
		{
			const TCHAR nn = Raw[i + 1];
			if (nn == TEXT('n') || nn == TEXT('N')) { Seg.AppendChar(TEXT('\n')); i += 2; continue; }
			if (nn == TEXT('t')) { Seg.AppendChar(TEXT('\t')); i += 2; continue; }
			if (nn == TEXT('r')) { i += 2; continue; }
		}
		if (ch == TEXT('\r')) { ++i; continue; }
		Seg.AppendChar(ch); ++i;   // '\n' попадает сюда как есть → перенос делает виджет
	}
	Flush();
}

FString FAsdaMarkup::StripTags(const FString& Raw)
{
	FString Out;
	Out.Reserve(Raw.Len());
	const int32 N = Raw.Len();
	int32 i = 0;
	while (i < N)
	{
		const TCHAR ch = Raw[i];
		if (ch == TEXT('<'))
		{
			if (AsdaMkMatch(Raw, i, TEXT("</F>"))) { i += 4; continue; }
			if (FAsdaMkTag T; AsdaMkParseCompound(Raw, i, T)) { i = T.End; continue; }   // ★ 30.10.2026: составной тег
			if (AsdaMkMatch(Raw, i, TEXT("<FC=")) || AsdaMkMatch(Raw, i, TEXT("<FC:")))
			{
				int32 j = i + 4;
				while (j < N && Raw[j] != TEXT('>')) ++j;
				i = (j < N) ? j + 1 : j; continue;
			}
			if (AsdaMkMatch(Raw, i, TEXT("<FS")))
			{
				int32 j = i + 3;
				while (j < N && FChar::IsDigit(Raw[j])) ++j;
				while (j < N && Raw[j] != TEXT('>')) ++j;
				i = (j < N) ? j + 1 : j; continue;
			}
			// ★ <FO=RRGGBB> — обводка с цветом: без этой ветки hex «FF0000>» оставался бы В ТЕКСТЕ
			//   (тот же класс дефекта, что уже ловили с <FC=> до 2026-08-01).
			if (AsdaMkMatch(Raw, i, TEXT("<FO=")) || AsdaMkMatch(Raw, i, TEXT("<FO:")))
			{
				int32 j = i + 4;
				while (j < N && Raw[j] != TEXT('>')) ++j;
				i = (j < N) ? j + 1 : j; continue;
			}
			if (AsdaMkMatch(Raw, i, TEXT("<FB>"))) { i += 4; continue; }
			if (AsdaMkMatch(Raw, i, TEXT("<FO>"))) { i += 4; continue; }
			if (AsdaMkMatch(Raw, i, TEXT("<FD>"))) { i += 4; continue; }
			Out.AppendChar(ch); ++i; continue;          // одиночный '<' — не тег, сохраняем
		}
		if (ch == TEXT('\\') && i + 1 < N)
		{
			const TCHAR n = Raw[i + 1];
			if (n == TEXT('n') || n == TEXT('N')) { Out.AppendChar(TEXT('\n')); i += 2; continue; }
			if (n == TEXT('t'))                   { Out.AppendChar(TEXT('\t')); i += 2; continue; }
			if (n == TEXT('r'))                   { i += 2; continue; }   // "\r" в данных — выбрасываем
		}
		Out.AppendChar(ch); ++i;                        // реальные '\n'/'\r' остаются как есть
	}
	return Out;
}

// ★ Снять стиль строки из JUI до первой подмены на rich (перенесено 25.09.2026 из окна 900 — общая точка).
void UAsdaRichText::CaptureFmt(FAsdaFmtText& F, UTextBlock* T)
{
	F = FAsdaFmtText();
	F.Node = T;
	if (!T) { return; }
	F.Style = FTextBlockStyle()
		.SetFont(T->GetFont())
		.SetColorAndOpacity(T->GetColorAndOpacity())
		.SetShadowOffset(T->GetShadowOffset())
		.SetShadowColorAndOpacity(T->GetShadowColorAndOpacity());
	F.Justify  = AsdaUi::ReadJustification(T);   // общая точка: свойство protected, геттера нет
	F.bWrap    = T->GetAutoWrapText();           // однострочным JUI-строкам false (закон ui-autowrap-canvas-gotcha)
	F.bStyleOk = true;
}

// ★ Сырой текст с тегами → строка `F` (единая точка `ApplyFormatted`) + переприсвоение живого узла.
void UAsdaRichText::SetFmt(FAsdaFmtText& F, const FString& Raw)
{
	if (!F.Node) { return; }
	FAsdaRichApplyOpts Opts;
	if (F.bStyleOk)
	{
		Opts.BaseStyle   = &F.Style;
		Opts.bSetJustify = true;
		Opts.Justify     = F.Justify;
		Opts.bWrap       = F.bWrap;
	}
	F.LastRaw = Raw;
	if (UWidget* New = ApplyFormatted(F.Node.Get(), Raw, F.bStyleOk ? &Opts : nullptr))
	{
		F.Node = New;
	}
}

UWidget* UAsdaRichText::ApplyFormatted(UWidget* ExistingTextBlock, const FString& RawText,
                                       const FAsdaRichApplyOpts* Opts)
{
	if (!ExistingTextBlock) return nullptr;
	const FString Markup = FAsdaMarkup::ToRichMarkup(RawText);

	// Уже заменён на URichTextBlock (повторный вызов) — только обновить текст, не плодить виджеты.
	//   Размер под экран даёт DPI-масштаб проекта (UIScaleRule=ShortestSide, curve SS/768), отдельно не масштабируем.
	if (URichTextBlock* Already = Cast<URichTextBlock>(ExistingTextBlock))
	{
		// ★ Базовый стиль — ДО SetText: `SRichTextBlock::SetTextStyle` (SRichTextBlock.cpp:188) только
		//   инвалидирует раскладку, уже разобранные run'ы стиль не перечитывают.
		if (Opts && Opts->BaseStyle) { Already->SetDefaultTextStyle(*Opts->BaseStyle); }
		const bool bWrap = (!Opts || Opts->bWrap);
		Already->SetAutoWrapText(bWrap);
		// ★ Жёсткий перенос по авторской ширине пузыря ANI_TXT (228, login_jui.ini). Без явного WrapTextAt
		//   canvas-слот с AutoSize «раздувается» под текст и перенос не срабатывает (текст уезжает вправо).
		float WrapW = 228.f;
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Already))
		{
			if (S->GetSize().X > 1.f) WrapW = S->GetSize().X;
			S->SetAutoSize(false);
		}
		if (Opts && Opts->WrapAt > 1.f) { WrapW = Opts->WrapAt; }
		Already->SetWrapTextAt(bWrap ? WrapW : 0.f);
		if (Opts && Opts->bSetJustify) { Already->SetJustification(Opts->Justify); }
		Already->SetText(FText::FromString(Markup));
		return Already;
	}

	UPanelWidget* Parent = ExistingTextBlock->GetParent();
	UWidgetTree*  Tree   = ExistingTextBlock->GetTypedOuter<UWidgetTree>();
	if (!Parent || !Tree) return nullptr;

	// Сохранить геометрию canvas-слота и имя старого контрола (чтобы GetWidgetFromName продолжил находить).
	UCanvasPanelSlot* OldSlot = AsdaUi::SlotOf(ExistingTextBlock);
	FVector2D Pos(0, 0), Sz(200, 50), Align(0, 0);
	FAnchors  Anch;
	float     ZOrder = 0.f;
	if (OldSlot)
	{
		Pos    = OldSlot->GetPosition();
		Sz     = OldSlot->GetSize();
		Align  = OldSlot->GetAlignment();
		Anch   = OldSlot->GetAnchors();
		ZOrder = OldSlot->GetZOrder();
	}
	const FName OldName = ExistingTextBlock->GetFName();

	// Освободить имя: убрать из родителя + переименовать старый в транзиент, чтобы новый rich взял имя.
	Parent->RemoveChild(ExistingTextBlock);
	ExistingTextBlock->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);

	URichTextBlock* Rich = Tree->ConstructWidget<URichTextBlock>(URichTextBlock::StaticClass(), OldName);
	if (!Rich) return nullptr;

	// Стиль Default = шрифт UI порта `AsdaUIFontRT` (обёртка над arial/arialbd — доказательство в шапке
	//   `AsdaUmgBuilder::GetOrCreateAsdaUIFont`; прежнее «Epilepsy Sans» здесь ВРАЛО) + декоратор
	//   всех 6 тегов (цвет/размер/жирный/…).
	// ★ Аудит №4 (B2/A29): таблицу стилей читает ещё и окно чата — до сведения она грузилась
	//   ДВАЖДЫ по общему литералу пути. Теперь загрузка одна, через `AsdaDt::Cached`.
	if (UDataTable* StyleSet = AsdaDt::Cached(kAsdaRichStyleDT))
	{
		Rich->SetTextStyleSet(StyleSet);
	}
	TArray<TSubclassOf<URichTextBlockDecorator>> Decs;
	Decs.Add(UAsdaRichDecorator::StaticClass());
	Rich->SetDecorators(Decs);
	// ★ Базовый стиль КОНТРОЛА (кегль/цвет/обводка из JUI) — иначе rich возьмёт строку `Default`
	//   таблицы DT_AsdaRichStyle (белый, 10 px) и потеряет всё, что импортёр взял из `EXS`.
	if (Opts && Opts->BaseStyle) { Rich->SetDefaultTextStyle(*Opts->BaseStyle); }
	const bool bWrapNew = (!Opts || Opts->bWrap);
	Rich->SetAutoWrapText(bWrapNew);
	// ★ Явный перенос по ширине слота (авторские 228 для ANI_TXT) — иначе AutoSize даёт «строку в одну линию».
	const float WrapNew = (Opts && Opts->WrapAt > 1.f) ? Opts->WrapAt : (Sz.X > 1.f ? Sz.X : 228.f);
	Rich->SetWrapTextAt(bWrapNew ? WrapNew : 0.f);
	if (Opts && Opts->bSetJustify) { Rich->SetJustification(Opts->Justify); }

	// Вернуть в родителя на ту же геометрию (не ломать позицию пузыря ANI_TXT).
	if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(Parent))
	{
		if (UCanvasPanelSlot* NewSlot = Canvas->AddChildToCanvas(Rich))
		{
			NewSlot->SetAnchors(Anch);
			NewSlot->SetPosition(Pos);
			NewSlot->SetSize(Sz);
			NewSlot->SetAlignment(Align);
			NewSlot->SetZOrder(ZOrder);
			NewSlot->SetAutoSize(false);   // ★ не AutoSize: фикс.ширина слота нужна для переноса текста помощника
		}
	}
	else
	{
		Parent->AddChild(Rich);
	}

	Rich->SetText(FText::FromString(Markup));
	return Rich;
}
