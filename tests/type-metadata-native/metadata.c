#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "core/types.h"

static void compact_derived_types(cc_arena_t *arena) {
    size_t before = arena->used;
    for (int i = 0; i < 1000; i++) {
        type_t *pointer = type_make_ptr(&ty_int);
        type_t *array = type_make_array(pointer, 3);
        assert(pointer->base == &ty_int && array->base == pointer);
        assert(pointer->params == NULL && pointer->fields == NULL);
        assert(array->params == NULL && array->fields == NULL);
    }
    size_t used = arena->used - before;
    assert(sizeof(type_t) <= 256);
    assert(used == 2000 * sizeof(type_t));
    assert(used < 512 * 1024);
    printf("type metadata: 2000 pointer/array nodes use %zu bytes\n", used);
}

static void independent_function_payload(void) {
    type_t *function = type_make_function(&ty_long);
    assert(function->params == NULL && function->fields == NULL);
    assert(function->nparams == 0);
    type_ensure_params(function);
    func_param_t *parameters = function->params;
    type_ensure_params(function);
    assert(function->params == parameters);
    strcpy(parameters[0].name, "original");
    parameters[0].type = &ty_int;
    function->nparams = 1;
    type_t *copy = type_dup(function);
    assert(copy->params != parameters && copy->params[0].type == &ty_int);
    assert(copy->ret == function->ret && copy->nparams == 1);
    copy->params[0].name[0] = 'X';
    copy->params[0].type = &ty_uint;
    copy->params[0].is_used = true;
    assert(parameters[0].name[0] == 'o' && parameters[0].type == &ty_int);
    assert(!parameters[0].is_used);
    type_t *qualified = type_make_const(function);
    assert(qualified->is_const && !function->is_const);
    assert(qualified->params != parameters && qualified->params != copy->params);
}

static void forward_completion_and_record_copy(cc_arena_t *arena) {
    type_t *record = cc_arena_alloc(arena, sizeof(type_t), 8);
    record->kind = TY_STRUCT;
    strcpy(record->tag, "forward");
    tags_init();
    tags_install("forward", record);
    type_t *pointer = type_make_ptr(record);
    type_t *pointer_copy = type_dup(pointer);
    assert(record->fields == NULL && !record->is_complete);
    assert(pointer_copy->base == record);
    type_ensure_fields(record);
    struct_field_t *fields = record->fields;
    type_ensure_fields(record);
    assert(record->fields == fields);
    strcpy(fields[0].name, "next");
    fields[0].type = pointer;
    record->nfields = 1;
    record->size = record->align = 8;
    record->is_complete = true;
    assert(tags_lookup("forward") == record);
    assert(pointer_copy->base->is_complete && pointer_copy->base->fields == fields);
    type_t *copy = type_dup(record);
    type_t *qualified = type_make_const(record);
    assert(copy->fields != fields && qualified->fields != fields);
    assert(copy->fields[0].type == pointer && qualified->fields[0].type == pointer);
    copy->fields[0].name[0] = 'X';
    copy->fields[0].offset = 99;
    qualified->fields[0].type = &ty_int;
    assert(fields[0].name[0] == 'n' && fields[0].offset == 0);
    assert(fields[0].type == pointer && !record->is_const);
}

int main(void) {
    cc_arena_t arena;
    cc_arena_init(&arena, 2 * 1024 * 1024);
    types_init(&arena);
    compact_derived_types(&arena);
    independent_function_payload();
    forward_completion_and_record_copy(&arena);
    cc_arena_free(&arena);
    puts("native type metadata: PASS");
    return 0;
}
