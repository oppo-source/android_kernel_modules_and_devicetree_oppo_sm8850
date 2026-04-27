#include <kunit/test.h>
#include <kunit/fff.h>
#include <linux/module.h>

DEFINE_FFF_GLOBALS;


static void boot_test_func_kunit_test_case1(struct kunit *test)
{
	int res = 0;
	KUNIT_EXPECT_EQ(test, 0, res);
}

static struct kunit_case boot_test_test_cases[] = {
	KUNIT_CASE(boot_test_func_kunit_test_case1),
	{}
};

static struct kunit_suite boot_test_test_suite = {
	.name = "boot_test_test_cases",
	.test_cases = boot_test_test_cases,
};

kunit_test_suite(boot_test_test_suite);

MODULE_LICENSE("GPL v2");

