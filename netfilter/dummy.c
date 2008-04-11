/* Dummy to ensure even if everything is modular, we ahve
 * __start_module_init etc. */
#include <linux/init.h>

static int dummy_init(void)
{
	return 0;
}
static void dummy_exit(void)
{
}

module_init(dummy_init);
module_exit(dummy_exit);
