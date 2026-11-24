#include "chm_scene.h"
#include <stdlib.h>
#include <string.h>

typedef struct config {
    void *object, *name, *role, *scene_class, *delegate_class;
    const chm_scene_entry *entry;
    struct config *next;
} config;

void *chm_scene_application_role;
static chm_scene_manifest manifest;
static config *configs, *chosen;
static void *session_obj, *scene_obj, *scene_delegate;

void chm_scene_reset(void)
{
    while (configs) { config *next = configs->next; free(configs); configs = next; }
    memset(&manifest, 0, sizeof(manifest));
    chosen = NULL;
    session_obj = NULL;
    scene_obj = NULL;
    scene_delegate = NULL;
    chm_scene_application_role = NULL;
}

static config *lookup(void *obj)
{
    for (config *c = configs; c; c = c->next) if (c->object == obj) return c;
    return NULL;
}

static const chm_scene_entry *find_entry(void *name)
{
    if (!name) return manifest.count ? &manifest.entries[0] : NULL;
    const char *text = chm_foundation_cstr(name);
    for (size_t i = 0; i < manifest.count; i++)
        if (!strcmp(manifest.entries[i].name, text)) return &manifest.entries[i];
    return NULL;
}

static void *config_init(void *obj, const char *sel, void *name, void *role)
{
    (void)sel;
    if (!role || strcmp(chm_foundation_cstr(role), CHM_APPLICATION_ROLE)) return NULL;
    config *c = lookup(obj);
    if (!c) {
        c = calloc(1, sizeof(*c));
        if (!c) return NULL;
        c->next = configs; configs = c; c->object = obj;
    }
    c->entry = find_entry(name); c->role = role; c->name = name;
    c->scene_class = chm_objc_get_class(c->entry && c->entry->scene[0] ? c->entry->scene : "UIWindowScene");
    c->delegate_class = c->entry && c->entry->delegate[0] ? chm_objc_get_class(c->entry->delegate) : NULL;
    if (!name && c->entry) c->name = chm_foundation_string(c->entry->name);
    return obj;
}

static void *config_new(void *cls, const char *sel, void *name, void *role)
{
    void *c = chm_foundation_alloc_class(cls, 0);
    return c ? config_init(c, sel, name, role) : NULL;
}

static void *get_field(void *obj, const char *sel)
{
    if (obj == session_obj) {
        if (!strcmp(sel, "configuration")) return chosen ? chosen->object : NULL;
        if (!strcmp(sel, "scene")) return scene_obj;
        return chm_scene_application_role;
    }
    if (obj == scene_obj) return !strcmp(sel, "session") ? session_obj : scene_delegate;
    config *c = lookup(obj);
    if (!c) return NULL;
    if (!strcmp(sel, "name")) return c->name;
    if (!strcmp(sel, "role")) return c->role;
    return !strcmp(sel, "sceneClass") ? c->scene_class : c->delegate_class;
}

static void set_class(void *obj, const char *sel, void *cls)
{
    config *c = lookup(obj);
    if (c && !strcmp(sel, "setSceneClass:")) c->scene_class = cls;
    else if (c) c->delegate_class = cls;
}

void *chm_scene_method(void *obj, const char *sel)
{
    if (!obj || !sel) return NULL;
    if (obj == session_obj && (!strcmp(sel, "configuration") || !strcmp(sel, "scene") || !strcmp(sel, "role"))) return (void *)get_field;
    if (obj == scene_obj && (!strcmp(sel, "session") || !strcmp(sel, "delegate"))) return (void *)get_field;
    if (!strcmp(sel, "configurationWithName:sessionRole:") && obj == chm_foundation_lookup_class("UISceneConfiguration")) return (void *)config_new;
    if (!strcmp(sel, "initWithName:sessionRole:") && chm_objc_is_kind(obj, "UISceneConfiguration")) return (void *)config_init;
    if (lookup(obj)) {
        if (!strcmp(sel, "setSceneClass:") || !strcmp(sel, "setDelegateClass:")) return (void *)set_class;
        if (!strcmp(sel, "role") || !strcmp(sel, "name") || !strcmp(sel, "sceneClass") || !strcmp(sel, "delegateClass")) return (void *)get_field;
    }
    return NULL;
}

int chm_scene_connect(chm_session *s, void *app, void *delegate)
{
    const char *pick = "application:configurationForConnectingSceneSession:options:";
    const char *connect = "scene:willConnectToSession:options:";
    void *(*configure)(void *,const char *,void *,void *,void *) = chm_objc_find_imp(delegate, pick);
    int parsed = chm_scene_read(s->app_dir, &manifest);
    if (parsed < 0) { chm_session_log(s, "scene: invalid or unsupported Info.plist"); return -1; }
    if (!manifest.has_manifest && !configure) return 0;
    session_obj = chm_foundation_alloc_obj("UISceneSession", 0);
    void *options = chm_foundation_alloc_obj("UISceneConnectionOptions", 0);
    void *initial = config_new(chm_objc_get_class("UISceneConfiguration"), NULL, NULL, chm_scene_application_role);
    if (!session_obj || !options || !initial) return -1;
    chosen = lookup(initial);
    if (configure) chosen = lookup(configure(delegate, pick, app, session_obj, options));
    if (!chosen || !chosen->scene_class || !chosen->delegate_class) { chm_session_log(s, "scene: no usable configuration for %s", CHM_APPLICATION_ROLE); return -1; }
    if (chosen->entry && chosen->entry->storyboard[0]) { chm_session_log(s, "scene: unsupported storyboard configuration: %s", chosen->entry->storyboard); return -1; }
    const char *dn = chm_objc_class_name(chosen->delegate_class);
    scene_obj = chm_objc_new_object(chm_objc_class_name(chosen->scene_class));
    scene_delegate = dn ? chm_objc_new_object(dn) : NULL;
    void (*will_connect)(void *,const char *,void *,void *,void *) = chm_objc_find_imp(scene_delegate, connect);
    if (!scene_obj || !scene_delegate) { chm_session_log(s, "scene: allocation failed"); return -1; }
    chm_session_log(s, "scene: role=%s name=%s delegate=%s", CHM_APPLICATION_ROLE, chm_foundation_cstr(chosen->name), dn);
    if (!will_connect) {
        chm_session_log(s, "scene: selected delegate has no connection callback");
        return -1;
    }
    will_connect(scene_delegate, connect, scene_obj, session_obj, options);
    const char *events[] = {"sceneWillEnterForeground:", "sceneDidBecomeActive:"};
    for (size_t i = 0; i < 2; ++i) {
        void (*fn)(void *,const char *,void *) = chm_objc_find_imp(scene_delegate, events[i]);
        if (fn) fn(scene_delegate, events[i], scene_obj);
    }
    chm_session_log(s, "scene: connection callback=%s", will_connect ? "called" : "absent");
    return 1;
}
