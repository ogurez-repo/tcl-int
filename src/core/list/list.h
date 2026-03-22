#ifndef LIST_H
#define LIST_H
#include <stdio.h>
#include <stdlib.h>

typedef struct List
{
    void *data;
    struct List *next;
} List;

List *new_list(void *data);
List *append_list(List *list, void *data);
List *split_list_mid(List *head);
void free_list(List *head, void (*free_data)(void *));
#endif
