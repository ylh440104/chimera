#ifndef CHM_RUNTIME_H
#define CHM_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *(*chm_symbol_fn)(void);

void chm_runtime_init(void);
void *chm_runtime_lookup(const char *name);
const char *chm_runtime_kind(const char *name);
int chm_runtime_count(void);
const char *chm_runtime_name_at(int index);
int chm_runtime_resolved_count(void);
void chm_runtime_note_resolved(void);

int chm_symbols_unresolved_total(void);
void chm_symbols_note_demand(const char *name);
int chm_symbols_demand_count(void);
const char *chm_symbols_demand_name(int index);
int chm_symbols_demand_hits(int index);

#endif