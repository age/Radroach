#include <radroach.h>

extern settings_t *settings;

extern void logstr (char *fmt, ...);
extern void raw (char *str);
extern void reply (message_t *msg, char *reply);
extern int plugin_load (char *name);
extern int plugins_load (char *path);
extern plugin_t * plugin_find (char *name);
extern int plugin_unload (char *name);
extern void plugins_unload (void);
extern void plugins_reload (void);
