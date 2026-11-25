# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_charinfo_live_check.py — ЖИВАЯ (PIE) проверка окна характеристик (CHARAINFO_WIN 900)
#   и его вкладки эмоций.
#
# ★★★ ЗАЧЕМ ЖИВАЯ (закон `false-negatives-method`): офлайн-дамп .uasset показывает только
#   разметку. Реально ли поднялась подсистема, нашлись ли все контролы, легли ли числа и
#   иконки эмоций — видно только в запущенном мире. Снимаем кадр и печатаем живую геометрию.
#
# ЗАПУСК:
#   UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=".../asda_charinfo_live_check.py"
#     -unattended -nopause -nosplash -nosound -stdout -FullStdOutLogOutput -abslog=<лог>
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/charinfo_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 90
MAX_TICKS = 40000

# Контролы, которые обязаны существовать и нести текст/картинку.
TEXTS = [
	"STATUS_NAME", "STATUS_LEVEL", "STATUS_JOB",
	"STATUS_B00", "STATUS_B01", "STATUS_B02", "STATUS_B03",
	"STATUS_B05", "STATUS_B06", "STATUS_B08", "STATUS_B09",
	"STATUS_THP", "STATUS_TMP", "STATUS_TEXP", "STATUS_TWEIGHT",
	"STATUS_STTT01", "STATUS_STTT06", "STATUS_STTT12",
	"STATUS_ST1", "STATUS_ST2", "STATUS_ST3", "STATUS_ST4", "STATUS_ST5",
	"STATUS_ST6", "STATUS_ST11", "STATUS_ST12",
	"STATUS_SB8", "CharaInfo_StEle3", "STATUS_AT10", "STATUS_AT22",
	"STATUS_fishing1", "STATUS_fishing2",
	"CHARAINFO_CAMPWAR_T1", "CHARAINFO_CAMPWAR_T2", "CHARAINFO_CAMPWAR_T3",
	"IMOTION_B00", "IMOTION_B01", "IMOTION_B02",
]
# Иконки эмоций (по одной на лоток) — должны стать видимыми и получить браш.
ICONS = ["IMOTION_NOMAL00_181_0", "IMOTION_PARTY00_204_0", "IMOTION_ABILITY00_217_0"]


def log(m):
	import unreal
	unreal.log("[характеристики-живой] " + str(m))


def main():
	import unreal

	# ★ ГРАБЛЯ (живой прогон 23.08.2026): при запуске через `-ExecCmds=py …` python-биндинги модуля
	#   UMG ещё не сгенерированы, и `unreal.WidgetBlueprintLibrary` отсутствует — ВСЕ виджет-дампы
	#   молча падали в «окна нет» (ложное «нет», закон `false-negatives-method`). Грузим модуль явно.
	for m in ("UMG", "UMGEditor"):
		try:
			unreal.load_module(m)
		except Exception as e:
			unreal.log_warning("[характеристики-живой] load_module(%s): %s" % (m, e))

	if os.path.isdir(SS_DIR):
		shutil.rmtree(SS_DIR, ignore_errors=True)
	if os.path.isdir(OUT_DIR):
		shutil.rmtree(OUT_DIR, ignore_errors=True)
	os.makedirs(OUT_DIR)

	les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
	ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
	unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
	les.editor_request_begin_play()
	log("PIE запущен")

	st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None}

	import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
	_ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
	if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
	import asda_pie_harness as PH
	world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

	cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

	def win():
		w = world()
		if not w:
			return None
		try:
			all_w = unreal.WidgetBlueprintLibrary.get_all_widgets_of_class(w, unreal.UserWidget, False)
		except Exception as e:
			log("!! get_all_widgets_of_class: %s" % e)
			return None
		for uw in all_w:
			if "CHARAINFO_WIN" in uw.get_class().get_name():
				return uw
		log("!! окно CHARAINFO_WIN среди %d виджетов не найдено" % len(all_w))
		return None

	def dump(tag):
		# ★ ГРАБЛЯ 23.08.2026: python-биндингов UMG при `-ExecCmds=py …` НЕТ (`unreal` без
		#   `WidgetBlueprintLibrary`), поэтому живые виджеты печатает САМ движок командой
		#   `asda.CharInfo.Widgets` — она видит дерево окна напрямую. Python-путь оставлен как
		#   резервный: если биндинги появятся, дамп станет подробнее.
		log("── ЖИВОЕ СОСТОЯНИЕ (%s) ──" % tag)
		try:
			cmd("asda.CharInfo.Widgets")
		except Exception as e:
			log("!! asda.CharInfo.Widgets: %s" % e)
		uw = win()
		if not uw:
			log("(python-дамп недоступен — см. вывод asda.CharInfo.Widgets выше)")
			return
		log("── ЖИВОЕ СОСТОЯНИЕ (%s) ──" % tag)
		miss = []
		for n in TEXTS:
			tb = uw.get_widget_from_name(n)
			if not tb:
				miss.append(n)
				continue
			try:
				txt = str(tb.get_text())
			except Exception:
				txt = "<не текст>"
			try:
				vis = str(tb.get_visibility()).split(".")[-1]
			except Exception:
				vis = "?"
			log("%-24s vis=%-24s текст=%r" % (n, vis, txt))
		for n in ICONS:
			im = uw.get_widget_from_name(n)
			if not im:
				miss.append(n)
				continue
			try:
				vis = str(im.get_visibility()).split(".")[-1]
				br = im.get_editor_property("brush")
				res = br.get_editor_property("resource_object")
				log("%-24s vis=%-24s браш=%s" % (n, vis, res.get_name() if res else "НЕТ"))
			except Exception as e:
				log("%-24s ошибка чтения: %s" % (n, e))
		if miss:
			log("!! КОНТРОЛОВ НЕ НАЙДЕНО: %s" % miss)
		else:
			log("все проверяемые контролы найдены")

	def dump_tab_caption_colors():
		# ПУНКТ 2 владельца: подписи под-вкладок должны быть ЧЁРНЫМИ, а состояния — работать.
		#   Клоны подписей называются CHARAINFO_STMENU_Cap0..2 (см. BuildStatTabCaptions).
		uw = win()
		if not uw:
			log("!! окна нет (цвета подписей)")
			return
		for i in range(3):
			n = "CHARAINFO_STMENU_Cap%d" % i
			tb = uw.get_widget_from_name(n)
			if not tb:
				log("%-24s НЕТ" % n)
				continue
			try:
				col = tb.get_editor_property("color_and_opacity").get_editor_property("specified_color")
				log("%-24s текст=%r цвет=(%.3f %.3f %.3f %.3f)"
					% (n, str(tb.get_text()), col.r, col.g, col.b, col.a))
			except Exception as e:
				log("%-24s ошибка чтения цвета: %s" % (n, e))

	def dump_stat_node_classes():
		# ПУНКТ 1 владельца: строки статов после разбора тегов должны стать URichTextBlock.
		uw = win()
		if not uw:
			log("!! окна нет (классы строк статов)")
			return
		for n in ["STATUS_ST6", "STATUS_ST7", "STATUS_ST8", "STATUS_ST9", "STATUS_ST10", "STATUS_ST11",
		          "CHARAINFO_CAMPWART1_S"]:
			w2 = uw.get_widget_from_name(n)
			if not w2:
				log("%-24s НЕТ" % n)
				continue
			log("%-24s класс=%-18s текст=%r" % (n, w2.get_class().get_name(), str(w2.get_text())))

	def intro_win():
		w = world()
		if not w:
			return None
		for uw in unreal.WidgetBlueprintLibrary.get_all_widgets_of_class(w, unreal.UserWidget, False):
			if "My_Introduction_Win" in uw.get_class().get_name():
				return uw
		return None

	def dump_intro():
		# ПУНКТ 4 владельца: окно «Обо мне» 8300 реально поднялось и собрано.
		uw = intro_win()
		if not uw:
			log("!! окно My_Introduction_Win НЕ найдено")
			return
		for n in ["My_Intro_Move", "My_Intro_Guide", "My_Intro_EditBox",
		          "My_Intro_OKBtn_Text", "My_Intro_CancelBtn_Text", "My_Intro_OKBtn",
		          "My_Intro_CancelBtn", "My_Intro_ClsBtn"]:
			w2 = uw.get_widget_from_name(n)
			if not w2:
				log("%-24s НЕТ" % n)
				continue
			try:
				txt = str(w2.get_text())
			except Exception:
				txt = "<не текст>"
			log("%-24s класс=%-18s vis=%-22s текст=%r"
				% (n, w2.get_class().get_name(), str(w2.get_visibility()).split(".")[-1], txt))

	def take_shot(tag):
		cmd("shot showui")
		st["_shot_tag"] = tag

	def harvest(tag):
		pngs = glob.glob(os.path.join(SS_DIR, "**", "*.png"), recursive=True)
		if not pngs:
			log("⚠ снимок '%s' НЕ появился" % tag)
			return
		pngs.sort(key=os.path.getmtime)
		src = pngs[-1]
		dst = os.path.join(OUT_DIR, "%s.png" % tag)
		shutil.copy2(src, dst)
		log("снимок '%s' → %s (%d Б)" % (tag, dst, os.path.getsize(dst)))

	STEPS = [
		("travel",    lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),
		("открыть",   lambda: cmd("asda.CharInfo.Open"), SETTLE),
		("дамп-0",    lambda: dump("вкладка Информация"), 5),
		("снимок-0",  lambda: take_shot("page0_basic"), SETTLE),
		("сбор-0",    lambda: harvest("page0_basic"), 5),
		("стр-1",     lambda: cmd("asda.CharInfo.Page 1"), SETTLE),
		("дамп-1",    lambda: dump("вкладка Параметры"), 5),
		("снимок-1",  lambda: take_shot("page1_data"), SETTLE),
		("сбор-1",    lambda: harvest("page1_data"), 5),
		# ★ Страница стихий по умолчанию НЕДОСТИЖИМА (как в эталоне, отчёт §10) — для кадра
		#   включаем третью под-вкладку флагом, потом возвращаем эталонное состояние.
		("стихии-вкл", lambda: cmd("asda.CharInfo.EleTab 1"), 10),
		("перерисовка", lambda: cmd("asda.CharInfo.Refresh"), SETTLE),
		("стр-2",     lambda: cmd("asda.CharInfo.Page 2"), SETTLE),
		("дамп-2",    lambda: dump("вкладка Характеристики"), 5),
		("снимок-2",  lambda: take_shot("page2_element"), SETTLE),
		("сбор-2",    lambda: harvest("page2_element"), 5),
		# ── ПУНКТ 3 владельца: кликабельны ли ВСЕ три под-вкладки (проверка ЖИВЫМ кликом) ──
		("вкладки-дамп", lambda: cmd("asda.CharInfo.TabProbe"), 20),
		("клик-0",    lambda: cmd("asda.CharInfo.TabClick 0"), SETTLE),
		("клик-1",    lambda: cmd("asda.CharInfo.TabClick 1"), SETTLE),
		("клик-2",    lambda: cmd("asda.CharInfo.TabClick 2"), SETTLE),
		("клик-0б",   lambda: cmd("asda.CharInfo.TabClick 0"), SETTLE),
		("снимок-вкл", lambda: take_shot("subtabs"), SETTLE),
		("сбор-вкл",  lambda: harvest("subtabs"), 5),
		# ── ПУНКТ 5 владельца: зона окна не во весь экран (иначе орбит-камера заблокирована) ──
		("зоны",      lambda: cmd("asda.UI.DumpZones"), 20),
		# ── ПУНКТ 2 владельца: рамка подсказки по контенту (короткая / длинная / многострочная) ──
		("тип-корот", lambda: cmd("asda.ItemTip.Text Сила"), 20),
		("тип-длин",  lambda: cmd("asda.ItemTip.Text Сведения о персонаже"), 20),
		("тип-много", lambda: cmd("asda.ItemTip.Text Сила|Увеличивает физическую атаку бойца и лучника, а также немного повышает переносимый вес."), 20),
		("тип-предм", lambda: cmd("asda.ItemTip.Code 20551"), 20),
		("тип-снимок", lambda: take_shot("tooltip_item"), SETTLE),
		("тип-сбор",  lambda: harvest("tooltip_item"), 5),
		("тип-скрыть", lambda: cmd("asda.ItemTip.Hide"), 10),
		("стихии-вкл2", lambda: cmd("asda.CharInfo.EleTab 1"), 10),
		("эмоции",    lambda: cmd("asda.CharInfo.Tab 1"), SETTLE),
		("дамп-э",    lambda: dump("вкладка Эмоции"), 5),
		("снимок-э",  lambda: take_shot("tab_emotions"), SETTLE),
		("сбор-э",    lambda: harvest("tab_emotions"), 5),
		("состояние", lambda: cmd("asda.CharInfo.Dump"), 20),
		# ══ ДОВОДКА 2026-08-23 (char_info_fix2.md) ══
		# ПУНКТ 1: теги <FC=…> в строках статов разобраны (узел стал URichTextBlock, разметки не видно)
		("тег-стр1",   lambda: cmd("asda.CharInfo.Tab 0"), 20),
		("тег-стр2",   lambda: cmd("asda.CharInfo.Page 1"), SETTLE),
		("тег-классы", lambda: cmd("asda.CharInfo.Widgets"), 20),
		("тег-дамп",   lambda: cmd("asda.CharInfo.Dump"), 20),
		("тег-снимок", lambda: take_shot("page1_tags_parsed"), SETTLE),
		("тег-сбор",   lambda: harvest("page1_tags_parsed"), 5),
		# ПУНКТ 2: подписи под-вкладок чёрные, состояния наведения/нажатия живы
		("цвет-покой", lambda: cmd("asda.CharInfo.Widgets"), 20),
		("цвет-клик",  lambda: cmd("asda.CharInfo.TabClick 2"), SETTLE),
		("цвет-после", lambda: cmd("asda.CharInfo.Widgets"), 20),
		("цвет-назад", lambda: cmd("asda.CharInfo.TabClick 1"), SETTLE),
		("цвет-снимок", lambda: take_shot("subtabs_black"), SETTLE),
		("цвет-сбор",  lambda: harvest("subtabs_black"), 5),
		# ПУНКТ 4: окно «Обо мне» 8300 открывается кнопкой 987 и собрано целиком
		("обо-открыть", lambda: cmd("asda.CharInfo.Intro"), SETTLE),
		("обо-дамп",   lambda: dump_intro(), 10),
		("обо-состояние", lambda: cmd("asda.CharInfo.IntroDump"), 20),
		("обо-снимок", lambda: take_shot("intro_win"), SETTLE),
		("обо-сбор",   lambda: harvest("intro_win"), 5),
		("обо-ввод",   lambda: cmd("asda.CharInfo.IntroSet Проверка текста о себе"), 30),
		("обо-после",  lambda: cmd("asda.CharInfo.IntroDump"), 20),
		("эмоция",    lambda: cmd("asda.CharInfo.Emote 0"), 60),
		# ── ПУНКТ 1 владельца: эмоция ложится в слот панели быстрого доступа (все ТРИ лотка) ──
		("укладка-об", lambda: cmd("asda.CharInfo.EmoteDrop 0 0"), 20),
		("укладка-от", lambda: cmd("asda.CharInfo.EmoteDrop 100 1"), 20),
		("укладка-нв", lambda: cmd("asda.CharInfo.EmoteDrop 200 2"), 20),
		("укладка-ор", lambda: cmd("asda.CharInfo.EmoteDrop 1 12"), 20),
		("укладка-т",  lambda: cmd("asda.CharInfo.EmoteDrop 2 65"), 20),
		("панель",     lambda: cmd("asda.Quick.Dump"), 20),
		# ПУНКТ 3: сначала ДАННЫЕ — длина клипа sitdown01 и границы сегментов (хвост «встаёт» не пуст)
		("сид-клип",   lambda: cmd("asda.Player.SitClipInfo"), 20),
		# ПУНКТ 3: «сесть/встать» — три сегмента клипа sitdown01 (состояния 7 → 8 → 9 → 0)
		("сесть",      lambda: cmd("asda.CharInfo.Emote 200"), 30),
		("сесть-фаза7", lambda: cmd("asda.CharInfo.Dump"), 90),
		("сесть-фаза8", lambda: cmd("asda.CharInfo.Dump"), 20),
		("сесть-снимок", lambda: take_shot("sit_idle"), SETTLE),
		("сесть-сбор", lambda: harvest("sit_idle"), 5),
		("встать",     lambda: cmd("asda.CharInfo.Emote 200"), 30),
		("встать-фаза9", lambda: cmd("asda.CharInfo.Dump"), 20),
		("встать-снимок", lambda: take_shot("stand_up"), 120),
		("встать-сбор", lambda: harvest("stand_up"), 5),
		("встать-конец", lambda: cmd("asda.CharInfo.Dump"), 20),
		("снимок-п",   lambda: take_shot("hotbar_emotions"), SETTLE),
		("сбор-п",     lambda: harvest("hotbar_emotions"), 5),
		("конец",     lambda: None, 5),
	]

	def tick(dt):
		st["tick"] += 1
		if st["tick"] < st["next_at"]:
			if st["tick"] > MAX_TICKS:
				log("!! таймаут")
				stop()
			return
		st["step"] += 1
		if st["step"] >= len(STEPS):
			stop()
			return
		name, fn, wait = STEPS[st["step"]]
		try:
			fn()
		except Exception as e:
			log("!! шаг '%s': %s" % (name, e))
		st["next_at"] = st["tick"] + wait

	def stop():
		import unreal as U
		if st["handle"]:
			U.unregister_slate_post_tick_callback(st["handle"])
			st["handle"] = None
		log("ЗАВЕРШЕНО")
		try:
			les.editor_request_end_play()
		except Exception:
			pass

	st["handle"] = unreal.register_slate_post_tick_callback(tick)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
	main()
