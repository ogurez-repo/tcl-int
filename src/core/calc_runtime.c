#include <stdio.h>

#include "calc_runtime.h"

int calc_add(int left, int right) {
    return left + right;
}

void calc_print_result(int value) {
    printf("%d\n", value);
}

void yyerror(const char *msg) {
    fprintf(stderr, "Parse error: %s\n", msg);
}
