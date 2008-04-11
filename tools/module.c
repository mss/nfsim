/*

Copyright (c) 2004 Rusty Russell

This file is part of nfsim.

nfsim is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

nfsim is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with nfsim; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Module handling functions. */
#include <core.h>
#include <tui.h>
#include <log.h>
#include <utils.h>
#include <list.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <dirent.h>

struct nfsim_module
{
	struct list_head list;
	char *name;
	int (*init)(void);
	void (*fini)(void);
	unsigned int use;
	void *handle;
};

static LIST_HEAD(modules);
char *module_path;

static struct nfsim_module *find_module(const char *name)
{
	struct nfsim_module *i;

	list_for_each_entry(i, &modules, list)
		if (streq(i->name, name))
			return i;
	return NULL;
}

void module_put(struct module *module)
{
	struct nfsim_module *mod;
	if (!module)
		return;

	mod = find_module(module->name);
	if (!mod)
		barf("Unknown module put on %s\n", module->name);
	if (mod->use == 0)
		barf("Module put too far on %s\n", module->name);
	mod->use--;
}

int try_module_get(struct module *module)
{
	struct nfsim_module *mod;

	if (should_i_fail_once(__func__))
		return 0;

	if (!module)
		return 1;

	mod = find_module(module->name);
	if (!mod)
		barf("Unknown module get on %s\n", module->name);
	mod->use++;
	return 1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
void __MOD_INC_USE_COUNT(struct module *module)
{
	struct nfsim_module *mod;

	if (!module)
		return;

	mod = find_module(module->name);
	if (!mod)
		barf("Unknown module inc on %s\n", module->name);
	mod->use++;
}

void __MOD_DEC_USE_COUNT(struct module *module)
{
	struct nfsim_module *mod;

	if (!module)
		return;

	mod = find_module(module->name);
	if (!mod)
		barf("Unknown module dec on %s\n", module->name);
	if (mod->use == 0)
		barf("Module put too far on %s\n", module->name);
	mod->use--;
}
#endif /* < 2.5 */

static void lsmod_help(int argc, char **argv)
{
#include "module-help:lsmod"
/*** XML Help:
    <section id="c:lsmod">
     <title><command>lsmod</command></title>
     <para>List modules (both available and loaded)</para>
     <cmdsynopsis>
      <command>lsmod</command>
     </cmdsynopsis>

     <para><command>lsmod</command> lists all the loaded modules, one to a
     line.</para>

    </section>
*/
}

static bool lsmod(int argc, char *argv[])
{
	struct nfsim_module *i;

	list_for_each_entry(i, &modules, list)
		nfsim_log(LOG_ALWAYS, "%s %u", i->name, i->use);

	return true;
}

static void insmod_help(int argc, char **argv)
{
#include "module-help:insmod"
/*** XML Help:
    <section id="c:insmod">
     <title><command>insmod</command></title>
     <para>Insert a module</para>
     <cmdsynopsis>
      <command>insmod</command>
      <arg choice="opt"><replaceable>modulename</replaceable></arg>
     <cmdsynopsis>
     </cmdsynopsis>
      <command>insmod</command>
      <arg choice="req">-a</arg>
     </cmdsynopsis>

     <para><command>insmod</command> loads a module, or all modules.
    The caller must ensure that any other module this one expects are
    already loaded.</para> </section>
*/
}

static int destroy_mod(void *_mod)
{
	struct nfsim_module *mod = _mod;
	dlclose(mod->handle);
	list_del(&mod->list);
	return 0;
}

static int no_init(void)
{
	return 0;
}

/* Returns module. */
static struct nfsim_module *load_mod(const char *name, bool report)
{
	struct nfsim_module *mod;
	char *path;

	mod = talloc(NULL, struct nfsim_module);
	path = talloc_asprintf(mod, "%s/%s.so", module_path, name);

	mod->handle = dlopen(path, RTLD_NOW|RTLD_GLOBAL);
	if (!mod->handle) {
		talloc_free(mod);
		if (report)
			nfsim_log(LOG_UI, "%s", dlerror());
		return NULL;
	}

	mod->name = talloc_strdup(mod, name);
	mod->use = 0;
	mod->init = dlsym(mod->handle, "__module_init");
	if (!mod->init)
		mod->init = no_init;
	mod->fini = dlsym(mod->handle, "__module_exit");
	list_add(&mod->list, &modules);
	talloc_set_destructor(mod, destroy_mod);
	return mod;
}

bool load_all_modules(void)
{
       DIR *dir;
       struct dirent *d;
       unsigned int num_succeeded, num_tried;

       dir = opendir(module_path);
       if (!dir) {
	       nfsim_log(LOG_UI,
			 "Could not opendir %s: %s",
			 module_path, strerror(errno));
	       return false;
       }

       /* We don't know the ordering... keep trying while we make progress. */
       do {
	       num_succeeded = num_tried = 0;
	       while ((d = readdir(dir)) != NULL) {
		       struct nfsim_module *mod;
		       int ret;
		       char name[strlen(d->d_name)];

		       if (!strends(d->d_name, ".so"))
			       continue;

		       memcpy(name, d->d_name, strlen(d->d_name) - 3);
		       name[strlen(d->d_name) - 3] = '\0';
		       if (find_module(name))
			       continue;

		       num_tried++;
		       mod = load_mod(name, false);
		       if (!mod)
			       continue;

		       ret = mod->init();
		       if (ret != 0) {
			       talloc_free(mod);
			       closedir(dir);
			       nfsim_log(LOG_UI, "Module %s init failed: %i",
					 name, ret);
			       return false;
		       }
		       num_succeeded++;
	       }
	       rewinddir(dir);
       } while (num_succeeded != 0);

       closedir(dir);
       return (num_tried == 0);
}

static bool insmod(int argc, char *argv[])
{
	struct nfsim_module *mod;
	int ret;

	if (argc != 2) {
		insmod_help(argc, argv);
		return false;
	}

	if (streq(argv[1], "-a"))
		return load_all_modules();

	if (find_module(argv[1])) {
		nfsim_log(LOG_UI, "Module %s already loaded.", argv[1]);
		return false;
	}

	mod = load_mod(argv[1], true);
	if (!mod)
		return false;

	ret = mod->init();
	if (ret != 0) {
		talloc_free(mod);
		nfsim_log(LOG_UI, "Module %s init failed: %i", argv[1], ret);
		return false;
	}
	return true;
}

static void rmmod_help(int argc, char **argv)
{
#include "module-help:rmmod"
/*** XML Help:
    <section id="c:rmmod">
     <title><command>rmmod</command></title>
     <para>Remove a module</para>
     <cmdsynopsis>
      <command>rmmod</command>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>insmod</command>
      <arg choice="req">-a</arg>
     </cmdsynopsis>

     <para><command>rmmod</command> removes a loaded module.  The
     caller must ensure that any module which is using this is already
     unloaded. </para>

    </section>
*/
}

static void module_unload(struct nfsim_module *mod)
{
	/* Make sure "running" timers are actually run */
	do_running_timers();
	if (mod->fini)
		mod->fini();
	talloc_free(mod);
}

bool unload_all_modules(void)
{
	struct nfsim_module *mod, *tmp;

	/* Modules are listed in reverse order. */
	list_for_each_entry_safe(mod, tmp, &modules, list) {
		if (mod->use) {
			nfsim_log(LOG_UI, "Module use count on %s still %u\n",
				  mod->name, mod->use);
			return false;
		}
		module_unload(mod);
	}
	return true;
}

static bool rmmod(int argc, char *argv[])
{
	struct nfsim_module *mod;

	if (argc != 2) {
		rmmod_help(argc, argv);
		return false;
	}

	if (streq(argv[1], "-a"))
		return unload_all_modules();

	mod = find_module(argv[1]);
	if (!mod) {
		nfsim_log(LOG_UI, "Module %s not found.", argv[1]);
		return false;
	}

	module_unload(mod);
	return true;
}

static void init(void)
{
	tui_register_command("lsmod", lsmod, lsmod_help);
	tui_register_command("insmod", insmod, insmod_help);
	tui_register_command("rmmod", rmmod, rmmod_help);
}

init_call(init);
