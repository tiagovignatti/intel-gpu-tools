#include "igt_core.h"
#include <unistd.h>

igt_simple_main
{
	igt_set_timeout(1, "Testcase");
	sleep(5);
}
