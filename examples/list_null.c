typedef struct list_t {
    void *content;
    struct list_t *next;
} list_t;

void *list(void *head, void *tail) {
    list_t *ret = gc_alloc(sizeof (*ret));
    ret->content = head;
    ret->next = (list_t *) tail;
    return ret;
}

void *nil() {
    return (void *) 0;
}

void *head(void *list) {
    return list->head;
}

void *tail(void *list) {
    return list->tail;
}

void *has_tail(void *list) {
    return list->tail;
}
