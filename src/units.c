#include "units.h"
#include <ctype.h>
#include <fileioc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DB_CAPACITY 12288
#define MAX_UNITS 300
#define MAX_NESTING 16
#define MAX_PREFIXES 20
#define MAX_NAME 20
#define DIMENSIONS 8
#define DEFINITION_CAPACITY 48
typedef struct unit unit_t;
typedef struct {
	char name[MAX_NAME];
	double scale;
} prefix_t;
typedef struct {
	double scale;
	int8_t dim[DIMENSIONS];
} measure_t;
typedef struct {
	const char *at;
	char error[UNITS_RESULT_CAPACITY];
	uint8_t depth;
} parser_t;
struct unit {
	char name[MAX_NAME];
	char definition[DEFINITION_CAPACITY];
	measure_t value;
	uint8_t state;
};
static char db[DB_CAPACITY];
static unit_t units[MAX_UNITS];
static prefix_t prefixes[MAX_PREFIXES];
static char primitive_name[DIMENSIONS][MAX_NAME];
static uint16_t unit_count;
static uint8_t resolution_depth;
static uint8_t prefix_count, primitive_count;
static bool loaded;
typedef struct {
	char name[MAX_NAME];
	char definition[DEFINITION_CAPACITY];
	measure_t value;
} variable_t;
static variable_t variables[UNITS_VARIABLE_CAPACITY];
static uint8_t variable_count;
static uint8_t significant_digits = 7;
static void copy_text(char *out, size_t cap, const char *text) {
	if (cap) {
		strncpy(out, text, cap - 1);
		out[cap - 1] = '\0';
	}
}

static void fail(parser_t *p, const char *text) {
	if (!p->error[0])
		copy_text(p->error, sizeof(p->error), text);
}

static void spaces(parser_t *p) {
	while (isspace((unsigned char)*p->at))
		p->at++;
}

static const unit_t *find_exact(const char *name) {
	uint16_t i;
	for (i = 0; i < unit_count; i++)
		if (!strcmp(name, units[i].name))
			return &units[i];
	return NULL;
}

static bool parse_expression(parser_t *p, measure_t *out);
static bool resolve_unit_inner(unit_t *unit, char *error, size_t cap) {
	parser_t parser;
	uint8_t index;
	if (unit->state == 2)
		return true;
	if (unit->state == 1) {
		snprintf(error, cap, "Circular definition: %s", unit->name);
		return false;
	}
	unit->state = 1;
	unit->value.scale = 1;
	memset(unit->value.dim, 0, DIMENSIONS);
	if (!strcmp(unit->definition, "!dimensionless")) {
		unit->state = 2;
		return true;
	}
	if (!strcmp(unit->definition, "!")) {
		if (primitive_count == DIMENSIONS) {
			copy_text(error, cap, "Too many primitive units");
			return false;
		}
		index = primitive_count++;
		unit->value.dim[index] = 1;
		copy_text(primitive_name[index], MAX_NAME, unit->name);
		unit->state = 2;
		return true;
	}
	parser.at = unit->definition;
	parser.error[0] = '\0';
	parser.depth = 0;
	if (!parse_expression(&parser, &unit->value)) {
		copy_text(error, cap, parser.error);
		unit->state = 0;
		return false;
	}
	spaces(&parser);
	if (*parser.at) {
		snprintf(error, cap, "Bad definition: %s", unit->name);
		unit->state = 0;
		return false;
	}
	unit->state = 2;
	return true;
}

static bool resolve_unit(unit_t *unit, char *error, size_t cap) {
	bool okay;
	if (unit->state == 2)
		return true;
	if (resolution_depth >= MAX_NESTING) {
		copy_text(error, cap, "Unit definitions nested too deeply");
		return false;
	}
	resolution_depth++;
	okay = resolve_unit_inner(unit, error, cap);
	resolution_depth--;
	return okay;
}

static const unit_t *plural_unit(const char *name) {
	char singular[MAX_NAME];
	size_t length = strlen(name);
	const unit_t *unit;
	if (length >= MAX_NAME || length < 2)
		return NULL;
	copy_text(singular, sizeof(singular), name);
	if (length > 3 && !strcmp(name + length - 3, "ies")) {
		singular[length - 3] = 'y';
		singular[length - 2] = '\0';
		unit = find_exact(singular);
		if (unit)
			return unit;
	}
	if (length > 2 && !strcmp(name + length - 2, "es")) {
		singular[length - 2] = '\0';
		unit = find_exact(singular);
		if (unit)
			return unit;
	}
	if (name[length - 1] == 's') {
		singular[length - 1] = '\0';
		return find_exact(singular);
	}
	return NULL;
}

static bool lookup(const char *name, measure_t *out, const unit_t **matched,
				   const prefix_t **used_prefix) {
	unit_t *unit = (unit_t *)find_exact(name);
	uint8_t variable_index;
	for (variable_index = 0; variable_index < variable_count; variable_index++)
		if (!strcmp(name, variables[variable_index].name)) {
			*out = variables[variable_index].value;
			if (matched)
				*matched = NULL;
			if (used_prefix)
				*used_prefix = NULL;
			return true;
		}
	const prefix_t *best_prefix = NULL;
	size_t best = 0;
	uint8_t i;
	char error[UNITS_RESULT_CAPACITY];
	if (!unit) {
		for (i = 0; i < prefix_count; i++) {
			size_t n = strlen(prefixes[i].name);
			unit_t *candidate;
			if (!n || n <= best || strncmp(name, prefixes[i].name, n) ||
				!name[n])
				continue;
			candidate = (unit_t *)find_exact(name + n);
			if (!candidate)
				candidate = (unit_t *)plural_unit(name + n);
			if (candidate) {
				best = n;
				unit = candidate;
				best_prefix = &prefixes[i];
			}
		}
	}
	if (!unit)
		unit = (unit_t *)plural_unit(name);
	if (!unit || !resolve_unit(unit, error, sizeof(error)))
		return false;
	*out = unit->value;
	if (best_prefix)
		out->scale *= best_prefix->scale;
	if (matched)
		*matched = unit;
	if (used_prefix)
		*used_prefix = best_prefix;
	return true;
}

static bool parse_primary(parser_t *p, measure_t *out) {
	char name[MAX_NAME], *end;
	uint8_t length = 0;
	double value;
	spaces(p);
	if (*p->at == '(') {
		if (p->depth >= MAX_NESTING) {
			fail(p, "Parentheses nested too deeply");
			return false;
		}
		p->depth++;
		p->at++;
		if (!parse_expression(p, out)) {
			p->depth--;
			return false;
		}
		spaces(p);
		if (*p->at != ')') {
			fail(p, "Missing closing parenthesis");
			p->depth--;
			return false;
		}
		p->at++;
		p->depth--;
		return true;
	}
	{
		const char *start = p->at, *scan = p->at, *exponent;
		char number_text[66];
		size_t number_length;
		if (*scan == '+' || *scan == '-')
			scan++;
		if (isdigit((unsigned char)*scan) ||
			(*scan == '.' && isdigit((unsigned char)scan[1]))) {
			while (isdigit((unsigned char)*scan))
				scan++;
			if (*scan == '.') {
				scan++;
				while (isdigit((unsigned char)*scan))
					scan++;
			}
			if (*scan == 'e' || *scan == 'E') {
				exponent = scan + 1;
				if (*exponent == '+' || *exponent == '-')
					exponent++;
				if (isdigit((unsigned char)*exponent)) {
					unsigned int exponent_value = 0;
					do {
						unsigned int digit = (unsigned int)(*exponent - '0');
						if (exponent_value > 30 ||
							(exponent_value == 30 && digit > 8)) {
							fail(p, "Scientific exponent too large");
							return false;
						}
						exponent_value = exponent_value * 10 + digit;
						exponent++;
					} while (isdigit((unsigned char)*exponent));
					scan = exponent;
				}
			}
			number_length = (size_t)(scan - start);
			if (number_length >= sizeof(number_text)) {
				fail(p, "Number is too long");
				return false;
			}
			memcpy(number_text, start, number_length);
			number_text[number_length] = '\0';
			value = strtod(number_text, &end);
			if (end != number_text) {
				if (!isfinite(value)) {
					fail(p, "Number is out of range");
					return false;
				}
				out->scale = value;
				memset(out->dim, 0, DIMENSIONS);
				p->at = scan;
				return true;
			}
		}
	}
	while (isalpha((unsigned char)*p->at)) {
		if (length == MAX_NAME - 1) {
			fail(p, "Unit name is too long");
			return false;
		}
		name[length++] = *p->at++;
	}
	name[length] = '\0';
	if (!length) {
		fail(p, "Expected a unit name");
		return false;
	}
	if (!lookup(name, out, NULL, NULL)) {
		snprintf(p->error, sizeof(p->error), "Unknown unit: %s", name);
		return false;
	}
	return true;
}

static bool parse_factor(parser_t *p, measure_t *out) {
	const char *primary = p->at;
	long power = 1;
	uint8_t i;
	bool unit_primary;
	while (isspace((unsigned char)*primary))
		primary++;
	unit_primary = isalpha((unsigned char)*primary);
	if (!parse_primary(p, out))
		return false;
	if (unit_primary && isdigit((unsigned char)*p->at)) {
		char *end;
		power = strtol(p->at, &end, 10);
		if (power > 12) {
			fail(p, "Invalid exponent");
			return false;
		}
		p->at = end;
	} else {
		spaces(p);
		if (*p->at == '^') {
			char *end;
			p->at++;
			power = strtol(p->at, &end, 10);
			if (end == p->at || power < -12 || power > 12) {
				fail(p, "Invalid exponent");
				return false;
			}
			p->at = end;
		}
	}
	if (power != 1) {
		out->scale = pow(out->scale, power);
		for (i = 0; i < DIMENSIONS; i++)
			out->dim[i] *= power;
	}
	return true;
}

static bool combine(measure_t *left, const measure_t *right, bool divide) {
	uint8_t i;
	if (divide) {
		left->scale /= right->scale;
		for (i = 0; i < DIMENSIONS; i++)
			left->dim[i] -= right->dim[i];
	} else {
		left->scale *= right->scale;
		for (i = 0; i < DIMENSIONS; i++)
			left->dim[i] += right->dim[i];
	}
	return true;
}

static bool implicit_start(char c) {
	return isalpha((unsigned char)c) || isdigit((unsigned char)c) || c == 46 ||
		   c == 40;
}

static bool parse_implicit_product(parser_t *p, measure_t *out) {
	if (!parse_factor(p, out))
		return false;
	for (;;) {
		measure_t right;
		spaces(p);
		if (!implicit_start(*p->at))
			break;
		if (!parse_factor(p, &right))
			return false;
		combine(out, &right, false);
	}
	return true;
}

static bool parse_product(parser_t *p, measure_t *out) {
	if (!parse_implicit_product(p, out))
		return false;
	for (;;) {
		measure_t right;
		char operation;
		spaces(p);
		operation = *p->at;
		if (operation != 42 && operation != 47)
			break;
		p->at++;
		if (!parse_implicit_product(p, &right))
			return false;
		combine(out, &right, operation == 47);
	}
	return true;
}

static bool parse_expression(parser_t *p, measure_t *out) {
	if (!parse_product(p, out))
		return false;
	for (;;) {
		measure_t right;
		char operation;
		uint8_t i;
		spaces(p);
		operation = *p->at;
		if (operation != '+' && operation != '-')
			break;
		p->at++;
		if (!parse_product(p, &right))
			return false;
		for (i = 0; i < DIMENSIONS; i++)
			if (out->dim[i] != right.dim[i]) {
				fail(p, "Cannot add incompatible units");
				return false;
			}
		if (operation == '+')
			out->scale += right.scale;
		else
			out->scale -= right.scale;
	}
	return true;
}

static bool parse(const char *text, measure_t *out, char *error, size_t cap) {
	parser_t p = {text, "", 0};
	if (!parse_expression(&p, out)) {
		copy_text(error, cap, p.error);
		return false;
	}
	spaces(&p);
	if (*p.at) {
		copy_text(error, cap, "Unexpected unit syntax");
		return false;
	}
	return true;
}

static bool parse_quantity(const char *text, measure_t *out, char *error,
						   size_t cap) {
	const char *at = text;
	while (isspace((unsigned char)*at))
		at++;
	if (!*at) {
		copy_text(error, cap, "Enter a unit or quantity");
		return false;
	}
	return parse(at, out, error, cap);
}

static bool add_line(char *line, char *error, size_t cap) {
	char *name, *definition, *comment;
	size_t length;
	while (isspace((unsigned char)*line))
		line++;
	comment = strchr(line, '#');
	if (comment)
		*comment = '\0';
	length = strlen(line);
	while (length && isspace((unsigned char)line[length - 1]))
		line[--length] = '\0';
	if (!*line)
		return true;
	if (*line == '!') {
		copy_text(error, cap, "Unsupported UNITDB directive");
		return false;
	}
	name = line;
	while (*line && !isspace((unsigned char)*line))
		line++;
	if (!*line) {
		copy_text(error, cap, "Definition missing");
		return false;
	}
	*line++ = '\0';
	while (isspace((unsigned char)*line))
		line++;
	definition = line;
	if (name[0] == '+')
		name++;
	length = strlen(name);
	if (length && name[length - 1] == '-') {
		double scale;
		char *end;
		uint8_t i;
		name[length - 1] = '\0';
		scale = strtod(definition, &end);
		while (isspace((unsigned char)*end))
			end++;
		if (*end || !*name || strlen(name) >= MAX_NAME) {
			copy_text(error, cap, "Invalid prefix definition");
			return false;
		}
		for (i = 0; i < prefix_count; i++)
			if (!strcmp(prefixes[i].name, name)) {
				prefixes[i].scale = scale;
				return true;
			}
		if (prefix_count == MAX_PREFIXES) {
			copy_text(error, cap, "Too many prefixes");
			return false;
		}
		copy_text(prefixes[prefix_count].name, MAX_NAME, name);
		prefixes[prefix_count++].scale = scale;
		return true;
	}
	if (strlen(name) >= MAX_NAME || strlen(definition) >= DEFINITION_CAPACITY) {
		copy_text(error, cap, "UNITDB definition too large");
		return false;
	}
	{
		unit_t *existing = (unit_t *)find_exact(name);
		if (existing) {
			copy_text(existing->definition, DEFINITION_CAPACITY, definition);
			existing->state = 0;
			return true;
		}
	}
	if (unit_count == MAX_UNITS) {
		copy_text(error, cap, "Too many units");
		return false;
	}
	copy_text(units[unit_count].name, MAX_NAME, name);
	copy_text(units[unit_count].definition, DEFINITION_CAPACITY, definition);
	units[unit_count].state = 0;
	unit_count++;
	return true;
}

static bool read_database(char *error, size_t cap) {
	char *line = db;
	unsigned int line_no = 0;
	while (*line) {
		char *next = line;
		line_no++;
		while (*next && *next != '\r' && *next != '\n')
			next++;
		if (*next) {
			*next++ = '\0';
			while (*next == '\r' || *next == '\n')
				next++;
		}
		if (!add_line(line, error, cap)) {
			char detail[UNITS_RESULT_CAPACITY];
			copy_text(detail, sizeof(detail), error);
			snprintf(error, cap, "Line %u: %s", line_no, detail);
			return false;
		}
		line = next;
	}
	if (!unit_count) {
		copy_text(error, cap, "UNITDB contains no units");
		return false;
	}
	return true;
}

bool units_load(char *error, size_t cap) {
	uint8_t handle = ti_Open("UNITDB", "r");
	uint16_t size, i;
	loaded = false;
	unit_count = prefix_count = primitive_count = resolution_depth = 0;
	memset(primitive_name, 0, sizeof(primitive_name));
	if (!handle) {
		copy_text(error, cap, "Missing UNITDB AppVar");
		return false;
	}
	size = ti_GetSize(handle);
	if (!size || size >= DB_CAPACITY) {
		ti_Close(handle);
		copy_text(error, cap, "UNITDB empty or too large");
		return false;
	}
	if (ti_Read(db, 1, size, handle) != size) {
		ti_Close(handle);
		copy_text(error, cap, "Could not read UNITDB");
		return false;
	}
	ti_Close(handle);
	db[size] = '\0';
	if (!read_database(error, cap))
		return false;
	for (i = 0; i < unit_count; i++)
		if (!resolve_unit(&units[i], error, cap))
			return false;
	loaded = true;
	return true;
}

bool units_validate_have(const char *have, char *error, size_t cap) {
	measure_t value;
	if (!loaded) {
		copy_text(error, cap, "UNITDB is not loaded");
		return false;
	}
	return parse_quantity(have, &value, error, cap);
}

bool units_validate_variable_name(const char *name, char *error, size_t cap) {
	measure_t value;
	uint8_t index;
	size_t length = strlen(name);
	if (!length || length >= MAX_NAME) {
		copy_text(error, cap, "Variable name must be 1-19 letters");
		return false;
	}
	for (index = 0; index < length; index++)
		if (!isalpha((unsigned char)name[index])) {
			copy_text(error, cap, "Variable name must use letters");
			return false;
		}
	for (index = 0; index < variable_count; index++)
		if (!strcmp(name, variables[index].name))
			return true;
	if (lookup(name, &value, NULL, NULL)) {
		copy_text(error, cap, "Name already used by a unit");
		return false;
	}
	if (variable_count == UNITS_VARIABLE_CAPACITY) {
		copy_text(error, cap, "Variable list is full (8 max)");
		return false;
	}
	return true;
}

bool units_set_variable(const char *name, const char *definition, char *error,
						size_t cap) {
	measure_t value;
	uint8_t index;
	if (!units_validate_variable_name(name, error, cap))
		return false;
	if (strlen(definition) >= DEFINITION_CAPACITY) {
		copy_text(error, cap, "Variable value is too long");
		return false;
	}
	if (!parse_quantity(definition, &value, error, cap))
		return false;
	for (index = 0; index < variable_count; index++)
		if (!strcmp(name, variables[index].name))
			break;
	if (index == variable_count)
		variable_count++;
	copy_text(variables[index].name, MAX_NAME, name);
	copy_text(variables[index].definition, DEFINITION_CAPACITY, definition);
	variables[index].value = value;
	return true;
}

void units_delete_variable(unsigned index) {
	if (index >= variable_count)
		return;
	if (index + 1 < variable_count)
		memmove(&variables[index], &variables[index + 1],
				(variable_count - index - 1) * sizeof(variables[0]));
	variable_count--;
}
unsigned units_variable_count(void) { return variable_count; }
const char *units_variable_name(unsigned index) {
	return index < variable_count ? variables[index].name : "";
}
const char *units_variable_definition(unsigned index) {
	return index < variable_count ? variables[index].definition : "";
}
void units_set_significant_digits(unsigned digits) {
	if (digits >= 2 && digits <= 7)
		significant_digits = (uint8_t)digits;
}
unsigned units_significant_digits(void) { return significant_digits; }

static void trim_number(char *text) {
	char *dot = strchr(text, '.'), *end;
	if (!dot)
		return;
	end = text + strlen(text) - 1;
	while (end > dot && *end == '0')
		*end-- = '\0';
	if (end == dot)
		*end = '\0';
}

static void format_fixed(char *out, size_t cap, double value,
						 uint8_t precision) {
	switch (precision) {
	case 6:
		snprintf(out, cap, "%.6f", value);
		break;
	case 5:
		snprintf(out, cap, "%.5f", value);
		break;
	case 4:
		snprintf(out, cap, "%.4f", value);
		break;
	case 3:
		snprintf(out, cap, "%.3f", value);
		break;
	case 2:
		snprintf(out, cap, "%.2f", value);
		break;
	case 1:
		snprintf(out, cap, "%.1f", value);
		break;
	default:
		snprintf(out, cap, "%.0f", value);
		break;
	}
}

static bool has_nonzero_digit(const char *text) {
	while (*text) {
		if (*text >= '1' && *text <= '9')
			return true;
		text++;
	}
	return false;
}

static void compact_number(double value, char *out, size_t cap) {
	char candidate[32], mantissa[24], exponent_text[12];
	double absolute = fabs(value), scaled;
	int precision, exponent;
	if (!isfinite(value)) {
		copy_text(out, cap, value < 0 ? "-inf" : "inf");
		return;
	}
	if (!absolute) {
		copy_text(out, cap, "0");
		return;
	}
	{
		int preferred =
			(int)significant_digits - 1 - (int)floor(log10(absolute));
		if (preferred > 6)
			preferred = 6;
		for (precision = preferred; precision >= 0; precision--) {
			format_fixed(candidate, sizeof(candidate), value,
						 (uint8_t)precision);
			trim_number(candidate);
			if (strlen(candidate) <= 8 &&
				(!value || has_nonzero_digit(candidate))) {
				copy_text(out, cap, candidate);
				return;
			}
		}
	}
	exponent = (int)floor(log10(absolute));
	scaled = value / pow(10.0, exponent);
	format_fixed(candidate, sizeof(candidate), scaled, significant_digits - 1);
	{
		const char *rounded = candidate[0] == '-' ? candidate + 1 : candidate;
		if (rounded[0] == '1' && rounded[1] == '0' &&
			(rounded[2] == '\0' || rounded[2] == '.')) {
			exponent++;
			scaled /= 10.0;
		}
	}
	snprintf(exponent_text, sizeof(exponent_text), "e%d", exponent);
	for (precision = significant_digits - 1; precision >= 0; precision--) {
		format_fixed(mantissa, sizeof(mantissa), scaled, (uint8_t)precision);
		trim_number(mantissa);
		if (strlen(mantissa) + strlen(exponent_text) <= 8) {
			copy_text(candidate, sizeof(candidate), mantissa);
			strncat(candidate, exponent_text,
					sizeof(candidate) - strlen(candidate) - 1);
			copy_text(out, cap, candidate);
			return;
		}
	}
	snprintf(out, cap, "%.1fe%d", scaled, exponent);
}

static void primitive_form(const measure_t *value, char *out, size_t cap) {
	char part[28], number[32];
	uint8_t i;
	compact_number(value->scale, number, sizeof(number));
	copy_text(out, cap, number);
	for (i = 0; i < DIMENSIONS; i++)
		if (value->dim[i]) {
			if (value->dim[i] == 1)
				snprintf(part, sizeof(part), " %s", primitive_name[i]);
			else
				snprintf(part, sizeof(part), " %s^%d", primitive_name[i],
						 value->dim[i]);
			strncat(out, part, cap - strlen(out) - 1);
		}
}

bool units_convert(const char *have, const char *want, char *result,
				   size_t cap) {
	measure_t source, target;
	double answer;
	uint8_t i;
	if (!parse_quantity(have, &source, result, cap) ||
		!parse_quantity(want, &target, result, cap))
		return false;
	for (i = 0; i < DIMENSIONS; i++)
		if (source.dim[i] != target.dim[i]) {
			copy_text(result, cap, "Units are not compatible");
			return false;
		}
	answer = source.scale / target.scale;
	if (fabs(answer) < 1e-12)
		answer = 0;
	{
		char number[32];
		compact_number(answer, number, sizeof(number));
		snprintf(result, cap, "%s %s", number, want);
	}
	return true;
}

bool units_describe(const char *have, char *result, size_t cap) {
	measure_t value;
	char normalized[UNITS_RESULT_CAPACITY], token[MAX_NAME];
	const char *at = have;
	uint8_t n = 0;
	const unit_t *unit = NULL;
	const prefix_t *prefix = NULL;
	if (!parse_quantity(have, &value, result, cap))
		return false;
	while (isspace((unsigned char)*at))
		at++;
	while (isalpha((unsigned char)*at) && n < MAX_NAME - 1)
		token[n++] = *at++;
	token[n] = '\0';
	while (isspace((unsigned char)*at))
		at++;
	primitive_form(&value, normalized, sizeof(normalized));
	if (n && !*at && lookup(token, &value, &unit, &prefix)) {
		if (prefix) {
			const char *display = prefix->name;
			uint8_t i;
			for (i = 0; i < prefix_count; i++)
				if (prefixes[i].scale == prefix->scale &&
					strlen(prefixes[i].name) > strlen(display))
					display = prefixes[i].name;
			snprintf(result, cap, "%s %s = %s", display, unit->name,
					 normalized);
		} else if (!unit) {
			uint8_t i;
			for (i = 0; i < variable_count; i++)
				if (!strcmp(token, variables[i].name))
					break;
			if (i < variable_count)
				snprintf(result, cap, "%s = %s = %s", token,
						 variables[i].definition, normalized);
			else
				snprintf(result, cap, "%s = %s", token, normalized);
		} else if (strcmp(token, unit->name))
			snprintf(result, cap, "%s = %s = %s", unit->name, unit->definition,
					 normalized);
		else if (!strcmp(unit->definition, "!"))
			snprintf(result, cap, "%s = %s", unit->name, normalized);
		else
			snprintf(result, cap, "%s = %s = %s", unit->name, unit->definition,
					 normalized);
	} else
		snprintf(result, cap, "%s = %s", have, normalized);
	return true;
}
