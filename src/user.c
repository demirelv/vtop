#include "user.h"

#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <stdlib.h>

#define USER_CACHE_SZ 256

typedef struct entry {
	uid_t uid;
	char  name[32];
	int   used;
} entry_t;

static entry_t cache[USER_CACHE_SZ];

const char *user_name(uid_t uid)
{
	unsigned idx = (unsigned)uid % USER_CACHE_SZ;
	for (unsigned probe = 0; probe < USER_CACHE_SZ; probe++) {
		entry_t *e = &cache[(idx + probe) % USER_CACHE_SZ];
		if (e->used && e->uid == uid)
			return e->name;
		if (!e->used) {
			struct passwd *pw = getpwuid(uid);
			e->uid  = uid;
			e->used = 1;
			if (pw && pw->pw_name)
				snprintf(e->name, sizeof(e->name), "%s", pw->pw_name);
			else
				snprintf(e->name, sizeof(e->name), "%u", (unsigned)uid);
			return e->name;
		}
	}
	static char fallback[16];
	snprintf(fallback, sizeof(fallback), "%u", (unsigned)uid);
	return fallback;
}

int user_lookup(const char *name, uid_t *out)
{
	struct passwd *pw = getpwnam(name);
	if (!pw)
		return -1;
	*out = pw->pw_uid;
	return 0;
}
