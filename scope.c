#include <stdlib.h>
#include <string.h>

#include "scope.h"
#include "error.h"
#include "debug.h"

/*
 * Guess we'll use a linked list of linked lists for now, though that certainly
 * isn't the best option and should at some point be replaced by a hash table or
 * something.
 * TODO
 */

void free_layer(scope_layer *layer);

void free_layer(scope_layer *layer) {
    for (scope_entry *entry = layer->head; entry;) {
        scope_entry *x = entry;
        entry = entry->prev;
        free(x->id);
        free(x);
    }

    free(layer);
}

void scope_push_layer(scope *sc) {
    scope_layer *new_layer = malloc(sizeof (*new_layer));
    if (!new_layer) error(GENERAL_ERROR, "cannot malloc?");

    new_layer->prev = *sc;
    new_layer->head = 0;

    *sc = new_layer;
}

void scope_pop_layer(scope *sc) {
    scope new_sc = (**sc).prev;
    free_layer(*sc);
    *sc = new_sc;
}

void scope_add_entry(scope sc, char *id, LLVMValueRef val, LLVMTypeRef type) {
    scope_entry *new_entry = malloc(sizeof (*new_entry));
    if (!new_entry) error(GENERAL_ERROR, "cannot malloc?");

    new_entry->id = strdup(id);
    new_entry->value = val;
    new_entry->type = type;

    scope_layer *layer = sc;
    new_entry->prev = layer->head;
    layer->head = new_entry;
}

scope_entry *scope_find(scope sc, char *id) {
    for (scope_layer *layer = sc; layer; layer = layer->prev) {
        for (scope_entry *entry = layer->head; entry; entry = entry->prev) {
            if (!strcmp(entry->id, id)) {
                return entry;
            }
        }
    }

    return 0;
}
