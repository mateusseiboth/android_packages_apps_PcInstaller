/* Who may drive the installer: root and the system app of any user (HSUM runs it as user 10). */
#include "check.h"
#include "util.h"

static void root_and_the_system_app_of_any_user(void)
{
	CHECK(is_trusted_uid(0));
	CHECK(is_trusted_uid(1000));
	CHECK(is_trusted_uid(1001000)); /* the system app of user 10 */
}

static void nobody_else(void)
{
	CHECK(!is_trusted_uid(2000));    /* shell */
	CHECK(!is_trusted_uid(1010123)); /* an app of user 10 */
	CHECK(!is_trusted_uid(10123));   /* an app of user 0 */
}

int main(void)
{
	RUN(root_and_the_system_app_of_any_user);
	RUN(nobody_else);
	return CHECK_DONE();
}
