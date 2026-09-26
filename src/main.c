#include <fileioc.h>
#include <graphx.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ti/getcsc.h>

#include "units.h"

#define INPUT_CAPACITY 65
#define HISTORY_CAPACITY 6
#define DB_VIEW_CAPACITY 12288
#define DB_LINE_CAPACITY 512
#define HISTORY_APPVAR "UNITHIST"
#define HISTORY_HEADER "UCH1"
#define SETTINGS_APPVAR "UNITCFG"
#define COLOR_BACKGROUND 0
#define COLOR_PANEL 1
#define COLOR_TEXT 2
#define COLOR_MUTED 3
#define COLOR_ACCENT 4
#define COLOR_SELECTION 5

typedef enum { PROMPT_HAVE, PROMPT_WANT } prompt_t;
typedef enum { PAGE_CALCULATOR, PAGE_VARIABLES, PAGE_DATABASE, PAGE_HELP, PAGE_SETTINGS, PAGE_CREDITS } page_t;
typedef enum { VARIABLE_NAME, VARIABLE_VALUE } variable_stage_t;
typedef struct {
	char have[INPUT_CAPACITY];
	char want[INPUT_CAPACITY];
	char result[UNITS_RESULT_CAPACITY];
} history_entry_t;

static history_entry_t history[HISTORY_CAPACITY];
static uint8_t history_count;
static int8_t selected_history = -1;
static char input[INPUT_CAPACITY];
static char pending_have[INPUT_CAPACITY];
static uint8_t input_length, cursor_position;
static bool alpha_mode = true, uppercase_once;
static bool database_ready;
static char database_error[UNITS_RESULT_CAPACITY];
static char prompt_error[UNITS_RESULT_CAPACITY];
static prompt_t prompt = PROMPT_HAVE;
static page_t page = PAGE_CALCULATOR;
static variable_stage_t variable_stage = VARIABLE_NAME;
static char variable_name[20];
static int8_t selected_variable = -1;
static char saved_input[INPUT_CAPACITY];
static uint8_t saved_length, saved_cursor;
static char database_text[DB_VIEW_CAPACITY];
static uint16_t database_lines[DB_LINE_CAPACITY], database_line_count, database_top;
static bool database_view_ready;
static bool light_theme, quit_requested;
static uint8_t setting_selection;

static void copy_text(char *out, size_t cap, const char *text) {
	strncpy(out, text, cap - 1);
	out[cap - 1] = '\0';
}

static void load_history(void) {
	uint8_t handle, count, index;
	uint16_t expected_size;
	char header[4];

	handle = ti_Open(HISTORY_APPVAR, "r");
	if (!handle)
		return;
	if (ti_Read(header, 1, sizeof(header), handle) != sizeof(header) ||
		memcmp(header, HISTORY_HEADER, sizeof(header)) ||
		ti_Read(&count, 1, 1, handle) != 1 || count > HISTORY_CAPACITY) {
		ti_Close(handle);
		return;
	}
	expected_size = 5 + (uint16_t)count * sizeof(history[0]);
	if (ti_GetSize(handle) != expected_size ||
		(count &&
		 ti_Read(history, sizeof(history[0]), count, handle) != count)) {
		ti_Close(handle);
		return;
	}
	ti_Close(handle);
	for (index = 0; index < count; index++) {
		if (history[index].have[INPUT_CAPACITY - 1] ||
			history[index].want[INPUT_CAPACITY - 1] ||
			history[index].result[UNITS_RESULT_CAPACITY - 1]) {
			memset(history, 0, sizeof(history));
			return;
		}
	}
	history_count = count;
}

static bool save_history(void) {
	uint8_t handle;

	handle = ti_Open(HISTORY_APPVAR, "w");
	if (!handle)
		return false;
	if (ti_Write(HISTORY_HEADER, 1, 4, handle) != 4 ||
		ti_Write(&history_count, 1, 1, handle) != 1 ||
		(history_count && ti_Write(history, sizeof(history[0]), history_count,
								   handle) != history_count)) {
		ti_Close(handle);
		return false;
	}
	ti_Close(handle);

	handle = ti_Open(HISTORY_APPVAR, "r");
	if (!handle)
		return false;
	ti_SetGCBehavior(NULL, NULL);
	if (!ti_SetArchiveStatus(true, handle) || !ti_IsArchived(handle)) {
		ti_Close(handle);
		return false;
	}
	ti_Close(handle);
	return true;
}

static void load_settings(void) {
	uint8_t handle = ti_Open(SETTINGS_APPVAR, "r");
	uint8_t bytes[6];
	if (!handle) return;
	if (ti_GetSize(handle) == sizeof(bytes) &&
		ti_Read(bytes, 1, sizeof(bytes), handle) == sizeof(bytes) &&
		!memcmp(bytes, "UCS1", 4) && bytes[4] <= 1 &&
		bytes[5] >= 2 && bytes[5] <= 7) {
		light_theme = bytes[4];
		units_set_significant_digits(bytes[5]);
	}
	ti_Close(handle);
}

static void save_settings(void) {
	uint8_t handle = ti_Open(SETTINGS_APPVAR, "w");
	uint8_t bytes[6] = {85, 67, 83, 49, 0, 0};
	if (!handle) return;
	bytes[4] = light_theme;
	bytes[5] = (uint8_t)units_significant_digits();
	if (ti_Write(bytes, 1, sizeof(bytes), handle) != sizeof(bytes)) { ti_Close(handle); return; }
	ti_Close(handle);
	handle = ti_Open(SETTINGS_APPVAR, "r");
	if (!handle) return;
	ti_SetGCBehavior(NULL, NULL);
	ti_SetArchiveStatus(true, handle);
	ti_Close(handle);
}

static char alpha_character(uint8_t key) {
	switch (key) {
	case sk_Math:
		return 'A';
	case sk_Apps:
		return 'B';
	case sk_Prgm:
		return 'C';
	case sk_Recip:
		return 'D';
	case sk_Sin:
		return 'E';
	case sk_Cos:
		return 'F';
	case sk_Tan:
		return 'G';
	case sk_Power:
		return 'H';
	case sk_Square:
		return 'I';
	case sk_Comma:
		return 'J';
	case sk_LParen:
		return 'K';
	case sk_RParen:
		return 'L';
	case sk_Div:
		return 'M';
	case sk_Log:
		return 'N';
	case sk_7:
		return 'O';
	case sk_8:
		return 'P';
	case sk_9:
		return 'Q';
	case sk_Mul:
		return 'R';
	case sk_Ln:
		return 'S';
	case sk_4:
		return 'T';
	case sk_5:
		return 'U';
	case sk_6:
		return 'V';
	case sk_Sub:
		return 'W';
	case sk_Store:
		return 'X';
	case sk_1:
		return 'Y';
	case sk_2:
		return 'Z';
	case sk_0:
		return ' ';
	default:
		return '\0';
	}
}

static char number_character(uint8_t key) {
	switch (key) {
	case sk_0:
		return '0';
	case sk_1:
		return '1';
	case sk_2:
		return '2';
	case sk_3:
		return '3';
	case sk_4:
		return '4';
	case sk_5:
		return '5';
	case sk_6:
		return '6';
	case sk_7:
		return '7';
	case sk_8:
		return '8';
	case sk_9:
		return '9';
	case sk_DecPnt:
		return '.';
	case sk_Add:
		return '+';
	case sk_Sub:
		return '-';
	case sk_Mul:
		return '*';
	case sk_Div:
		return '/';
	case sk_Power:
		return '^';
	case sk_LParen:
		return '(';
	case sk_RParen:
		return ')';
	default:
		return '\0';
	}
}

static void reset_input(void) {
	input[0] = '\0';
	input_length = 0;
	cursor_position = 0;
}

static void insert_character(char c) {
	if (c && input_length < INPUT_CAPACITY - 1) {
		memmove(&input[cursor_position + 1], &input[cursor_position],
				input_length - cursor_position + 1);
		input[cursor_position++] = c;
		input_length++;
	}
}

static void recall_history(void) {
	const char *text;
	if (selected_history < 0)
		return;
	text = prompt == PROMPT_HAVE ? history[(uint8_t)selected_history].have
								 : history[(uint8_t)selected_history].want;
	while (*text && input_length < INPUT_CAPACITY - 1)
		insert_character(*text++);
	selected_history = -1;
}

static void delete_selected_history(void) {
	uint8_t index = (uint8_t)selected_history;
	if (index + 1 < history_count)
		memmove(&history[index], &history[index + 1],
				sizeof(history[0]) * (history_count - index - 1));
	history_count--;
	if (!history_count)
		selected_history = -1;
	else if (index >= history_count)
		selected_history = (int8_t)history_count - 1;
}

static void submit_input(void) {
	history_entry_t *entry;
	char result[UNITS_RESULT_CAPACITY];

	if (prompt == PROMPT_HAVE) {
		if (!input_length)
			return;
		if (!units_validate_have(input, prompt_error, sizeof(prompt_error)))
			return;
		prompt_error[0] = '\0';
		copy_text(pending_have, sizeof(pending_have), input);
		reset_input();
		prompt = PROMPT_WANT;
		return;
	}

	if (input_length) {
		if (!units_convert(pending_have, input, result, sizeof(result))) {
			copy_text(prompt_error, sizeof(prompt_error), result);
			return;
		}
	} else if (!units_describe(pending_have, result, sizeof(result))) {
		copy_text(prompt_error, sizeof(prompt_error), result);
		return;
	}

	if (history_count == HISTORY_CAPACITY) {
		memmove(&history[0], &history[1],
				sizeof(history[0]) * (HISTORY_CAPACITY - 1));
		history_count--;
	}
	entry = &history[history_count++];
	copy_text(entry->have, sizeof(entry->have), pending_have);
	copy_text(entry->want, sizeof(entry->want), input);
	copy_text(entry->result, sizeof(entry->result), result);
	prompt_error[0] = '\0';
	reset_input();
	pending_have[0] = '\0';
	prompt = PROMPT_HAVE;
}

static void handle_calculator_key(uint8_t key) {
	char c;
	if (key == sk_2nd) {
		alpha_mode = true;
		uppercase_once = !uppercase_once;
		return;
	}
	if (key == sk_Alpha) {
		alpha_mode = !alpha_mode;
		uppercase_once = false;
		return;
	}
	if (key == sk_Left) {
		selected_history = -1;
		if (cursor_position)
			cursor_position--;
		return;
	}
	if (key == sk_Right) {
		selected_history = -1;
		if (cursor_position < input_length)
			cursor_position++;
		return;
	}
	if (key == sk_Up) {
		if (!history_count)
			return;
		if (selected_history < 0)
			selected_history = (int8_t)history_count - 1;
		else if (selected_history > 0)
			selected_history--;
		return;
	}
	if (key == sk_Down) {
		if (selected_history < 0)
			return;
		if (selected_history < (int8_t)history_count - 1)
			selected_history++;
		else
			selected_history = -1;
		return;
	}
	if (key == sk_Enter) {
		if (selected_history >= 0)
			recall_history();
		else
			submit_input();
		return;
	}
	if (key == sk_Clear) {
		if (selected_history >= 0) {
			delete_selected_history();
		} else {
			reset_input();
			prompt_error[0] = '\0';
			uppercase_once = false;
		}
		return;
	}
	if (key == sk_Del) {
		if (selected_history >= 0) {
			delete_selected_history();
			return;
		}
		if (cursor_position) {
			memmove(&input[cursor_position - 1], &input[cursor_position],
					input_length - cursor_position + 1);
			cursor_position--;
			input_length--;
			prompt_error[0] = '\0';
		}
		return;
	}
	selected_history = -1;
	c = alpha_mode ? alpha_character(key) : number_character(key);
	if (alpha_mode && c >= 'A' && c <= 'Z') {
		if (!uppercase_once)
			c += 'a' - 'A';
		uppercase_once = false;
	}
	if (c)
		prompt_error[0] = '\0';
	insert_character(c);
}

static void print_at(const char *text, int x, int y, uint8_t color) {
	gfx_SetTextFGColor(color);
	gfx_SetTextXY(x, y);
	gfx_PrintString(text);
}

static uint8_t wrapped_length(const char *text, unsigned int width) {
	char line[INPUT_CAPACITY + 1];
	uint8_t length = 0;
	while (text[length] && length < sizeof(line) - 1) {
		line[length] = text[length];
		line[length + 1] = '\0';
		if (gfx_GetStringWidth(line) > width)
			break;
		length++;
	}
	if (!length && *text)
		length = 1;
	return length;
}

static int draw_wrapped(const char *text, int x, int y, unsigned int width,
						uint8_t max_lines, uint8_t color) {
	char line[INPUT_CAPACITY + 1];
	uint8_t row = 0, length;
	while (*text && row < max_lines) {
		length = wrapped_length(text, width);
		memcpy(line, text, length);
		line[length] = '\0';
		print_at(line, x, y, color);
		text += length;
		y += 18;
		row++;
	}
	return y;
}

static void cursor_location(int *x, int *y) {
	const char *at = input;
	uint8_t consumed = 0, row = 0, length, prefix_length;
	char prefix[INPUT_CAPACITY + 1];
	while (row < 2) {
		length = wrapped_length(at, 292);
		if (cursor_position <= consumed + length || !at[length]) {
			prefix_length = cursor_position - consumed;
			memcpy(prefix, at, prefix_length);
			prefix[prefix_length] = '\0';
			*x = 20 + gfx_GetStringWidth(prefix);
			*y = 194 + row * 18;
			return;
		}
		consumed += length;
		at += length;
		row++;
	}
	*x = 20;
	*y = 212;
}

static uint8_t wrapped_line_count(const char *text, unsigned int width,
								  uint8_t maximum) {
	uint8_t lines = 0, length;
	do {
		length = wrapped_length(text, width);
		text += length;
		lines++;
	} while (*text && lines < maximum);
	return lines;
}

static void history_layout(uint8_t index, char *line, uint8_t *command_lines,
						   uint8_t *result_lines) {
	if (history[index].want[0])
		snprintf(line, INPUT_CAPACITY * 2 + 5, "%s -> %s", history[index].have,
				 history[index].want);
	else
		copy_text(line, INPUT_CAPACITY * 2 + 5, history[index].have);
	*command_lines = wrapped_line_count(line, 296, 3);
	*result_lines =
		wrapped_line_count(history[index].result, 296, 5 - *command_lines);
}

static uint8_t history_height(uint8_t index) {
	char line[INPUT_CAPACITY * 2 + 5];
	uint8_t command_lines, result_lines;
	history_layout(index, line, &command_lines, &result_lines);
	return (command_lines + result_lines) * 18 + 4;
}

static void draw_calculator(void) {
	uint8_t first = 0, last = 0, index, used = 0;
	int cursor_x, cursor_y, y = 28;
	if (history_count) {
		last = selected_history >= 0 ? (uint8_t)selected_history
									 : history_count - 1;
		first = last;
		used = history_height(first);
		while (last + 1 < history_count &&
			   used + history_height(last + 1) <= 108) {
			last++;
			used += history_height(last);
		}
		while (first && used + history_height(first - 1) <= 108) {
			first--;
			used += history_height(first);
		}
	}
	gfx_FillScreen(COLOR_BACKGROUND);
	gfx_SetColor(COLOR_PANEL);
	gfx_FillRectangle(0, 0, 320, 24);
	print_at("F1 Quit F2 Var F3 DB F4 ? F5 Set", 8, 8, COLOR_TEXT);
	print_at(uppercase_once ? "(ABC)" : (alpha_mode ? "(abc)" : "(123)"), 270,
			 8, COLOR_ACCENT);
	if (history_count)
		for (index = first; index <= last; index++) {
			char line[INPUT_CAPACITY * 2 + 5];
			uint8_t command_lines, result_lines, height;
			history_layout(index, line, &command_lines, &result_lines);
			height = (command_lines + result_lines) * 18 + 4;
			if ((int8_t)index == selected_history) {
				gfx_SetColor(COLOR_SELECTION);
				gfx_FillRectangle(4, y - 3, 312, height);
			}
			print_at(">", 8, y, COLOR_ACCENT);
			y = draw_wrapped(line, 20, y, 296, command_lines, COLOR_TEXT);
			y = draw_wrapped(history[index].result, 20, y, 296, result_lines,
							 COLOR_MUTED) +
				4;
		}
	if (!history_count) {
		if (database_ready)
			print_at("Enter a quantity and unit :P", 8, 48, COLOR_MUTED);
		else {
			print_at("Database error:", 8, 42, COLOR_ACCENT);
			draw_wrapped(database_error, 8, 63, 304, 3, COLOR_MUTED);
		}
	}
	gfx_SetColor(COLOR_PANEL);
	gfx_FillRectangle(0, 136, 320, 104);
	if (prompt_error[0])
		draw_wrapped(prompt_error, 8, 140, 304, 2, COLOR_ACCENT);
	else if (prompt == PROMPT_WANT) {
		char have_line[INPUT_CAPACITY + 7];
		snprintf(have_line, sizeof(have_line), "You have: %s", pending_have);
		draw_wrapped(have_line, 8, 140, 304, 2, COLOR_MUTED);
	}
	print_at(prompt == PROMPT_WANT ? "You want:" : "You have:", 8, 177,
			 COLOR_MUTED);
	print_at(">", 8, 197, COLOR_ACCENT);
	draw_wrapped(input, 20, 197, 292, 2, COLOR_TEXT);
	cursor_location(&cursor_x, &cursor_y);
	gfx_SetColor(COLOR_ACCENT);
	gfx_VertLine(cursor_x, cursor_y, 18);
}

static void set_theme(void) {
	gfx_palette[COLOR_BACKGROUND] = light_theme ? gfx_RGBTo1555(245, 247, 250) : gfx_RGBTo1555(18, 22, 30);
	gfx_palette[COLOR_PANEL] = light_theme ? gfx_RGBTo1555(222, 229, 238) : gfx_RGBTo1555(31, 38, 51);
	gfx_palette[COLOR_TEXT] = light_theme ? gfx_RGBTo1555(28, 37, 49) : gfx_RGBTo1555(235, 239, 245);
	gfx_palette[COLOR_MUTED] = light_theme ? gfx_RGBTo1555(80, 91, 106) : gfx_RGBTo1555(145, 156, 173);
	gfx_palette[COLOR_ACCENT] = light_theme ? gfx_RGBTo1555(0, 112, 149) : gfx_RGBTo1555(82, 189, 214);
	gfx_palette[COLOR_SELECTION] = light_theme ? gfx_RGBTo1555(192, 216, 229) : gfx_RGBTo1555(48, 65, 82);
}

static void load_database_view(void) {
	uint8_t handle = ti_Open("UNITDB", "r");
	uint16_t size, i;
	database_view_ready = false;
	database_line_count = database_top = 0;
	if (!handle) return;
	size = ti_GetSize(handle);
	if (!size || size >= DB_VIEW_CAPACITY ||
		ti_Read(database_text, 1, size, handle) != size) { ti_Close(handle); return; }
	ti_Close(handle);
	database_text[size] = 0;
	database_lines[database_line_count++] = 0;
	for (i = 0; i < size; i++) {
		if (database_text[i] == 13) database_text[i] = 0;
		if (database_text[i] == 10) {
			database_text[i] = 0;
			if (i + 1 < size && database_line_count < DB_LINE_CAPACITY)
				database_lines[database_line_count++] = i + 1;
		}
	}
	database_view_ready = true;
}

static bool contains_query(const char *line, const char *query) {
	const char *start;
	if (!*query) return true;
	for (start = line; *start; start++) {
		const char *a = start, *b = query;
		while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
		if (!*b) return true;
	}
	return false;
}

static unsigned database_match_count(void) {
	uint16_t i; unsigned count = 0;
	for (i = 0; i < database_line_count; i++)
		if (contains_query(database_text + database_lines[i], input)) count++;
	return count;
}

static void change_page(page_t target) {
	if (page == PAGE_CALCULATOR && target != PAGE_CALCULATOR) {
		copy_text(saved_input, sizeof(saved_input), input);
		saved_length = input_length; saved_cursor = cursor_position;
	}
	if (target == PAGE_CALCULATOR && page != PAGE_CALCULATOR) {
		copy_text(input, sizeof(input), saved_input);
		input_length = saved_length; cursor_position = saved_cursor;
	} else if (target != PAGE_CALCULATOR) reset_input();
	page = target; prompt_error[0] = 0;
	if (target == PAGE_DATABASE) load_database_view();
	if (target == PAGE_VARIABLES) { variable_stage = VARIABLE_NAME; selected_variable = -1; }
}

static void edit_page_input(uint8_t key) {
	char c;
	if (key == sk_2nd) { alpha_mode = true; uppercase_once = !uppercase_once; return; }
	if (key == sk_Alpha) { alpha_mode = !alpha_mode; uppercase_once = false; return; }
	if (key == sk_Left) { if (cursor_position) cursor_position--; return; }
	if (key == sk_Right) { if (cursor_position < input_length) cursor_position++; return; }
	if (key == sk_Clear) { reset_input(); prompt_error[0] = 0; database_top = 0; return; }
	if (key == sk_Del) {
		if (cursor_position) {
			memmove(input + cursor_position - 1, input + cursor_position, input_length - cursor_position + 1);
			cursor_position--; input_length--;
		}
		database_top = 0; return;
	}
	c = alpha_mode ? alpha_character(key) : number_character(key);
	if (alpha_mode && c >= 65 && c <= 90) { if (!uppercase_once) c += 32; uppercase_once = false; }
	if (c) { insert_character(c); prompt_error[0] = 0; database_top = 0; }
}

static void handle_variable_key(uint8_t key) {
	unsigned count = units_variable_count();
	if (key == sk_Up && count) {
		if (selected_variable < 0) selected_variable = (int8_t)count - 1;
		else if (selected_variable > 0) selected_variable--;
		return;
	}
	if (key == sk_Down && selected_variable >= 0) {
		if ((unsigned)(selected_variable + 1) < count) selected_variable++; else selected_variable = -1;
		return;
	}
	if (selected_variable >= 0) {
		if (key == sk_Del || key == sk_Clear) {
			units_delete_variable((unsigned)selected_variable);
			selected_variable = -1; return;
		}
		if (key == sk_Enter) {
			copy_text(variable_name, sizeof(variable_name), units_variable_name((unsigned)selected_variable));
			copy_text(input, sizeof(input), units_variable_definition((unsigned)selected_variable));
			input_length = cursor_position = (uint8_t)strlen(input);
			variable_stage = VARIABLE_VALUE; selected_variable = -1; return;
		}
		selected_variable = -1;
	}
	if (key == sk_Enter) {
		if (variable_stage == VARIABLE_NAME) {
			if (!input_length || input_length >= sizeof(variable_name)) {
				copy_text(prompt_error, sizeof(prompt_error), "Name must be 1-19 letters"); return;
			}
			{ uint8_t i; for (i = 0; i < input_length; i++)
				if (!isalpha((unsigned char)input[i])) {
					copy_text(prompt_error, sizeof(prompt_error), "Name must use letters"); return;
				}
			}
			copy_text(variable_name, sizeof(variable_name), input);
			variable_stage = VARIABLE_VALUE; reset_input();
		} else if (units_set_variable(variable_name, input, prompt_error, sizeof(prompt_error))) {
			variable_stage = VARIABLE_NAME; reset_input(); prompt_error[0] = 0;
		}
		return;
	}
	edit_page_input(key);
}

static void handle_key(uint8_t key) {
	if (key == sk_Mode && page != PAGE_CALCULATOR) { change_page(PAGE_CALCULATOR); return; }
	if (key == sk_Window && page == PAGE_VARIABLES) { change_page(PAGE_CALCULATOR); return; }
	if (key == sk_Zoom && page == PAGE_DATABASE) { change_page(PAGE_CALCULATOR); return; }
	if (key == sk_Trace && page == PAGE_HELP) { change_page(PAGE_CALCULATOR); return; }
	if (key == sk_Graph && page == PAGE_SETTINGS) { change_page(PAGE_CALCULATOR); return; }
	if (key == sk_Yequ) { quit_requested = true; return; }
	if (key == sk_Window) { change_page(PAGE_VARIABLES); return; }
	if (key == sk_Zoom) { change_page(PAGE_DATABASE); return; }
	if (key == sk_Trace) { change_page(PAGE_HELP); return; }
	if (key == sk_Graph) { change_page(PAGE_SETTINGS); return; }
	if (key == sk_2nd && (page == PAGE_HELP || page == PAGE_SETTINGS || page == PAGE_CREDITS)) {
		uppercase_once = !uppercase_once;
		return;
	}
	if (page == PAGE_CALCULATOR) { handle_calculator_key(key); return; }
	if (page == PAGE_VARIABLES) { handle_variable_key(key); return; }
	if (page == PAGE_DATABASE) {
		if (key == sk_Up) { if (database_top) database_top--; return; }
		if (key == sk_Down) { if (database_top + 1 < database_match_count()) database_top++; return; }
		edit_page_input(key); return;
	}
	if (key == sk_Clear) { change_page(PAGE_CALCULATOR); return; }
	if (page == PAGE_CREDITS) { if (key == sk_Enter) change_page(PAGE_SETTINGS); return; }
	if (page == PAGE_SETTINGS) {
		if (key == sk_Up && setting_selection) setting_selection--;
		else if (key == sk_Down && setting_selection < 2) setting_selection++;
		else if (key == sk_Left || key == sk_Right || key == sk_Enter) {
			if (setting_selection == 0) { light_theme = !light_theme; set_theme(); }
			else if (setting_selection == 1) {
				unsigned digits = units_significant_digits();
				if (key == sk_Left) digits = digits == 2 ? 7 : digits - 1;
				else digits = digits == 7 ? 2 : digits + 1;
				units_set_significant_digits(digits);
			} else change_page(PAGE_CREDITS);
		}
	}
}

static void draw_page_header(const char *title) {
	gfx_FillScreen(COLOR_BACKGROUND);
	gfx_SetColor(COLOR_PANEL); gfx_FillRectangle(0, 0, 320, 24);
	print_at(title, 8, 8, COLOR_TEXT);
	print_at(uppercase_once ? "(ABC)" : (alpha_mode ? "(abc)" : "(123)"), 270, 8, COLOR_ACCENT);
}

static void draw_variable_page(void) {
	unsigned index, count = units_variable_count();
	char line[INPUT_CAPACITY + 32];
	int x, y;
	draw_page_header("F2 Variables");
	if (!count) print_at("No session variables yet.", 8, 34, COLOR_MUTED);
	for (index = 0; index < count; index++) {
		int row_y = 32 + (int)index * 17;
		if ((int)index == selected_variable) {
			gfx_SetColor(COLOR_SELECTION); gfx_FillRectangle(4, row_y - 2, 312, 17);
		}
		snprintf(line, sizeof(line), "%s = %s", units_variable_name(index), units_variable_definition(index));
		draw_wrapped(line, 8, row_y, 304, 1, COLOR_TEXT);
	}
	if (prompt_error[0]) draw_wrapped(prompt_error, 8, 169, 304, 1, COLOR_ACCENT);
	else print_at("Up/Down select; Enter edit; Del remove", 8, 169, COLOR_MUTED);
	if (variable_stage == VARIABLE_NAME) print_at("Name (letters only):", 8, 187, COLOR_MUTED);
	else { snprintf(line, sizeof(line), "Value of %s:", variable_name); print_at(line, 8, 187, COLOR_MUTED); }
	print_at(">", 8, 207, COLOR_ACCENT); draw_wrapped(input, 20, 207, 292, 2, COLOR_TEXT);
	cursor_location(&x, &y); gfx_SetColor(COLOR_ACCENT); gfx_VertLine(x, y + 10, 18);
}

static void draw_database_page(void) {
	uint16_t index; unsigned match = 0, shown = 0, total;
	char line[INPUT_CAPACITY + 16]; int x, y;
	draw_page_header("F3 UNITDB");
	if (!database_view_ready) print_at("UNITDB is unavailable.", 8, 42, COLOR_ACCENT);
	total = database_match_count();
	for (index = 0; index < database_line_count && shown < 8; index++) {
		const char *value = database_text + database_lines[index];
		if (!contains_query(value, input)) continue;
		if (match++ < database_top) continue;
		snprintf(line, sizeof(line), "%u %s", (unsigned)index + 1, value);
		draw_wrapped(line, 8, 29 + (int)shown * 19, 304, 1, COLOR_TEXT);
		shown++;
	}
	if (!total && database_view_ready) print_at("No matches.", 8, 46, COLOR_MUTED);
	snprintf(line, sizeof(line), "%u matches | Up/Down scroll", total);
	print_at(line, 8, 183, COLOR_MUTED);
	print_at("Search:", 8, 202, COLOR_MUTED);
	print_at(">", 8, 220, COLOR_ACCENT); draw_wrapped(input, 20, 220, 292, 1, COLOR_TEXT);
	cursor_location(&x, &y); gfx_SetColor(COLOR_ACCENT); gfx_VertLine(x, 217, 18);
}

static void draw_help_page(void) {
	draw_page_header("F4 Help");
	print_at("Have: quantity and unit", 8, 30, COLOR_TEXT);
	print_at("Want: target unit; blank = definition", 8, 48, COLOR_TEXT);
	print_at("Example: 3 ft + 6 inch -> m", 8, 66, COLOR_TEXT);
	print_at("Order: powers, implicit products,", 8, 90, COLOR_MUTED);
	print_at("then * or /, then + or -", 8, 108, COLOR_MUTED);
	print_at("a / b c means a / (b*c)", 8, 126, COLOR_TEXT);
	print_at("cm3 means cm^3; cm 3 means 3*cm", 8, 144, COLOR_TEXT);
	print_at("F2: session variables; F3: UNITDB", 8, 168, COLOR_MUTED);
	print_at("Alpha toggles letters/operators", 8, 186, COLOR_MUTED);
	print_at("2nd: one capital; 2nd+On: quit", 8, 204, COLOR_MUTED);
	print_at("Mode or Clear: calculator", 8, 222, COLOR_ACCENT);
}

static void draw_settings_page(void) {
	char line[64]; int row;
	draw_page_header("F5 Settings");
	for (row = 0; row < 3; row++) if (setting_selection == row) {
		gfx_SetColor(COLOR_SELECTION); gfx_FillRectangle(4, 35 + row * 33, 312, 28);
	}
	snprintf(line, sizeof(line), "Theme: %s", light_theme ? "Light" : "Dark");
	print_at(line, 12, 43, COLOR_TEXT);
	snprintf(line, sizeof(line), "Significant digits: %u", units_significant_digits());
	print_at(line, 12, 76, COLOR_TEXT);
	print_at("Credits", 12, 109, COLOR_TEXT);
	print_at("Up/Down select; Left/Right change", 8, 162, COLOR_MUTED);
	print_at("Enter opens credits or changes value", 8, 182, COLOR_MUTED);
	print_at("Mode or Clear: calculator", 8, 220, COLOR_ACCENT);
}

static void draw_credits_page(void) {
	draw_page_header("Credits");
	print_at("Inspired by GNU Units", 8, 42, COLOR_TEXT);
	print_at("Built with the CE C Toolchain", 8, 64, COLOR_TEXT);
	print_at("Database: data/units.dat", 8, 86, COLOR_MUTED);
	print_at("Enter: Settings", 8, 192, COLOR_ACCENT);
	print_at("Mode or Clear: calculator", 8, 215, COLOR_ACCENT);
}

static void draw_screen(void) {
	switch (page) {
	case PAGE_VARIABLES: draw_variable_page(); break;
	case PAGE_DATABASE: draw_database_page(); break;
	case PAGE_HELP: draw_help_page(); break;
	case PAGE_SETTINGS: draw_settings_page(); break;
	case PAGE_CREDITS: draw_credits_page(); break;
	default: draw_calculator(); break;
	}
}

int main(void) {
	uint8_t key;
	database_ready = units_load(database_error, sizeof(database_error));
	load_history();
	load_settings();
	gfx_Begin();
	gfx_SetDrawBuffer();
	gfx_SetTextScale(1, 2);
	gfx_SetTextBGColor(COLOR_BACKGROUND);
	gfx_SetTextTransparentColor(COLOR_BACKGROUND);
	set_theme();
	draw_screen();
	gfx_SwapDraw();
	for (;;) {
		if (boot_CheckOnPressed() && uppercase_once)
			break;
		key = os_GetCSC();
		if (key) {
			handle_key(key);
			if (quit_requested) break;
			draw_screen();
			gfx_SwapDraw();
		}
	}
	gfx_End();
	save_history();
	save_settings();
	return 0;
}
