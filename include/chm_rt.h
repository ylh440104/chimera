#ifndef CHM_RT_H
#define CHM_RT_H

#include <stddef.h>

#include "chm.h"

void chm_install_fault_handler(void);
int chm_run_guarded(chm_image *img, int argc, char **argv, char *err, size_t cap);
int chm_missing_count(void);
const char *chm_missing_at(int index);
int chm_is_encrypted(const char *path);

#endif