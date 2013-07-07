#include "../objgrind.h"
#include "tests/sys_mman.h"
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

static int pgsz;

static char *mm(char *addr, int size, int prot)
{
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	char *ret;

	if (addr)
		flags |= MAP_FIXED;

	ret = mmap(addr, size, prot, flags, -1, 0);
	if (ret == (char *)-1) {
		perror("mmap failed");
		exit(1);
	}

	return ret;
}

/* Case 1 - unwritable */
static void test1()
{
	char *m = mm(0, pgsz * 5, PROT_READ|PROT_WRITE);

	VALGRIND_MAKE_UNWRITABLE(m, pgsz*2); /* all unwritable */
        m[0] = 'x'; /* report error */
	assert(VALGRIND_CHECK_UNWRITABLE(&m[0]) == 1);
        m[pgsz*3] = 'x'; /* unreported */
	VALGRIND_MAKE_NOCHECK(m, pgsz*2); /* writable */
        m[0] = 'x'; /* unreported */
	assert(VALGRIND_CHECK_UNWRITABLE(&m[0]) == 0);
}

/* Case 2 - unreferable */
static void test2()
{
        int *unreferable;
	int **m = (int **)mm(0, pgsz * 5, PROT_READ|PROT_WRITE);

        unreferable = (int *)(((size_t)m)+pgsz);
	VALGRIND_ADD_REFCHECK_FIELD(m); /* define refcheck field */
	VALGRIND_MAKE_UNREFERABLE(unreferable, 8); /* unreferable */
        m[0] = (int *)2; /* unreported */
        m[0] = unreferable; /* error */
	VALGRIND_REMOVE_REFCHECK_FIELD(m);
        m[0] = unreferable; /* unreported */
	VALGRIND_ADD_REFCHECK_FIELD(m);
	VALGRIND_MAKE_NOCHECK(unreferable, 8);
        m[0] = unreferable; /* unreported */
}

static struct test {
	void (*test)(void);
	int faults;
} tests[] = {
	{ test1, 0 },
	{ test2, 0 },
};
static const int n_tests = sizeof(tests)/sizeof(*tests);
	
int main()
{
	static const struct rlimit zero = { 0, 0 };
	int i;

	pgsz = getpagesize();
	setvbuf(stdout, NULL, _IOLBF, 0);

	setrlimit(RLIMIT_CORE, &zero);

	for(i = 0; i < n_tests; i++) {
            (*tests[i].test)();
            printf("Test %d: PASS\n", i+1);
            fflush(stdout);
	}
	exit(0);
}
