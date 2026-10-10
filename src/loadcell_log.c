#include <stdint.h>

#include "loadcell_log.h"

/*
 * Decimal text of v, written backwards from end. newlib-nano's printf, the
 * one stm-forge links, has no %lld, so the timestamp cannot go through PRId64.
 */
static const char *loadcell_format_i64(char *end, int64_t v)
{
	uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;

	*--end = '\0';
	do {
		*--end = (char)('0' + u % 10);
		u /= 10;
	} while (u);
	if (v < 0)
		*--end = '-';
	return end;
}

void loadcell_log_sample(const struct loadcell_dev *loadcell,
			 const struct loadcell_sample *sample)
{
	char t[21];	/* "-9223372036854775808" and its NUL */

	loadcell_info(loadcell, "t %s us  %8ld counts  %10.3f units%s",
		      loadcell_format_i64(t + sizeof(t), sample->t_us),
		      (long)sample->counts, (double)sample->units,
		      sample->saturated ? "  saturated" : "");
}
