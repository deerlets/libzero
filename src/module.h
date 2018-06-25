#ifndef __ZERO_MODULE_H
#define __ZERO_MODULE_H

#include <limits.h>
#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODULE_NAME_SIZE 256
#define MODULE_PARAM_SIZE 1024

/*
 * errno
 */
enum MODULE_ERROR {
	MOD_ERELOAD,
	MOD_EOPEN,
	MOD_ESYM,
	MOD_EINIT,
	MOD_EEXIT,
};

int mod_errno(void);
char *mod_error(void);

/*
 * module init & exit
 */
typedef int (*module_init_func_t)(void);
typedef void (*module_exit_func_t)(void);

#define module_init(init_func) \
	int __module_init(void) { return init_func(); }
#define module_exit(exit_func) \
	void __module_exit(void) { exit_func(); }

struct module *module_self(void *address);
extern int __module_init(void);
#define THIS_MODULE module_self(__module_init)

/*
 * module & param
 */
struct module {
	char name[MODULE_NAME_SIZE];
	char param[MODULE_PARAM_SIZE];
	char fpath[PATH_MAX];
	void *handle;
	module_init_func_t init_fn;
	struct list_head node;
};

struct module *load_module(const char *fpath, const char *param);
int unload_module(struct module *m);

int load_modules_from_dir(const char *dirname);
int unload_all_modules(void);

struct module *find_module(const char *name);
struct list_head *get_modules();

int param_get_int(const char *name, int *value, const char *param);
int param_get_string(const char *name, void *buf,
                     size_t size, const char *param);

#define MODULE_PARAM_GET_INT(name, value) \
	param_get_int(name, value, THIS_MODULE->param)
#define MODULE_PARAM_GET_STRING(name, buf, size) \
	param_get_string(name, buf, size, THIS_MODULE->param)

#ifdef __cplusplus
}
#endif
#endif
