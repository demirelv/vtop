#ifndef __VTOP_SORT_H__
#define __VTOP_SORT_H__

#include "vtop.h"

void sort_rows(row_t *rows, int n, enum sort_field f, int reverse);
const char *sort_field_name(enum sort_field f);

#endif
