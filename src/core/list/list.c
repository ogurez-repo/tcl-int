#include "list.h"

List *new_list(void *data)
{
    List *list = (List *)malloc(sizeof(List));
    if (!list)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    list->data = data;
    list->next = NULL;
    return list;
}

List *append_list(List *list, void *data)
{
    List *new_node = new_list(data);

    if (!list)
    {
        return new_node;
    }

    list->next = new_node;
    return new_node;
}

void free_list(List *head, void (*free_data)(void *))
{
    while (head)
    {
        List *next = head->next;
        if (free_data)
        {
            free_data(head->data);
        }
        free(head);
        head = next;
    }
}
