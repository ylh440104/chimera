#ifndef CHM_SCENE_H
#define CHM_SCENE_H

#include "chm_session.h"

#define CHM_APPLICATION_ROLE "UIWindowSceneSessionRoleApplication"
#define CHM_SCENE_CONFIGS 32

typedef struct {
    char name[192];
    char delegate[192];
    char scene[192];
    char storyboard[192];
} chm_scene_entry;

typedef struct {
    chm_scene_entry entries[CHM_SCENE_CONFIGS];
    size_t count;
    int has_manifest;
    char identifier[192], executable[192], version[192], build[192];
} chm_scene_manifest;

int chm_scene_read(const char *directory, chm_scene_manifest *out);
void chm_scene_reset(void);
void *chm_scene_method(void *receiver, const char *selector);
int chm_scene_connect(chm_session *session, void *app, void *delegate);
extern void *chm_scene_application_role;

#endif
