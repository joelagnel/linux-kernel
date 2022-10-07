#include <linux/string.h>
#include <linux/spinlock.h>

#ifdef CONFIG_RCU_LAZY_DEBUG

DEFINE_RAW_SPINLOCK(lazy_funcs_lock);

#define FUNC_SIZE 1024
static unsigned long lazy_funcs[FUNC_SIZE];
static int nr_funcs;

static void __find_func(unsigned long ip, int *B, int *E, int *N)
{
	unsigned long *p;
	int b, e, n;

	b = n = 0;
	e = nr_funcs - 1;

	while (b <= e) {
		n = (b + e) / 2;
		p = &lazy_funcs[n];
		if (ip > *p) {
			b = n + 1;
		} else if (ip < *p) {
			e = n - 1;
		} else
			break;
	}

	*B = b;
	*E = e;
	*N = n;

	return;
}

static __maybe_unused bool lazy_func_exists(void* ip_ptr)
{
	int b, e, n;
	unsigned long flags;
	unsigned long ip = (unsigned long)ip_ptr;

	raw_spin_lock_irqsave(&lazy_funcs_lock, flags);
	__find_func(ip, &b, &e, &n);
	raw_spin_unlock_irqrestore(&lazy_funcs_lock, flags);

	return b <= e;
}

static int lazy_func_add(void* ip_ptr)
{
	int b, e, n;
	unsigned long flags;
	unsigned long ip = (unsigned long)ip_ptr;

	raw_spin_lock_irqsave(&lazy_funcs_lock, flags);
	if (nr_funcs >= FUNC_SIZE) {
		raw_spin_unlock_irqrestore(&lazy_funcs_lock, flags);
		return -1;
	}

	__find_func(ip, &b, &e, &n);

	if (b > e) {
		if (n != nr_funcs)
			memmove(&lazy_funcs[n+1], &lazy_funcs[n],
				(sizeof(*lazy_funcs) * (nr_funcs - n)));

		lazy_funcs[n] = ip;
		nr_funcs++;
	}

	raw_spin_unlock_irqrestore(&lazy_funcs_lock, flags);
	return 0;
}

#else

static bool __maybe_unused lazy_func_exists(void* ip_ptr)
{
	return false;
}

static int lazy_func_add(void* ip_ptr)
{
	return -1;
}
#endif
