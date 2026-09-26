#ifndef UNITS_H
#define UNITS_H
#include <stdbool.h>
#include <stddef.h>
#define UNITS_RESULT_CAPACITY 96
#define UNITS_VARIABLE_CAPACITY 8
bool units_set_variable(const char *name, const char *definition, char *error, size_t capacity);
void units_delete_variable(unsigned index);
unsigned units_variable_count(void);
const char *units_variable_name(unsigned index);
const char *units_variable_definition(unsigned index);
void units_set_significant_digits(unsigned digits);
unsigned units_significant_digits(void);
bool units_load(char *error, size_t capacity);
bool units_validate_have(const char *have, char *error, size_t capacity);
bool units_convert(const char *have, const char *want, char *result,
				   size_t capacity);
bool units_describe(const char *have, char *result, size_t capacity);
#endif
