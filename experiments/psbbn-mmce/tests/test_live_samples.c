#include <assert.h>
#include <stdio.h>
#include "real_checks.h"
int main(void)
{
    struct real_session_samples session = {{0}, {0}};
    /* Filesystem writes can change a factory sample between boots. */
    const unsigned int factory = 0x37005e89, live = 0x681d16fa;
    assert(factory != live && !real_session_matches(&session, 21, live));
    assert(real_session_learn(&session, 21, live, live));
    assert(real_session_matches(&session, 21, live));
    assert(!real_session_matches(&session, 21, factory));
    /* A bad initial read cannot silently become the backend reference. */
    assert(!real_session_learn(&session, 21, live, live ^ 1));
    assert(!real_session_matches(&session, 21, live));
    assert(real_session_learn(&session, 21, live, live));
    /* All downstream routes must return the same current bytes. */
    assert(!real_session_matches(&session, 21, live ^ 1));
    assert(!real_session_matches(&session, 22, live));
    /* Zero is a valid CRC, but never a substitute for an unread sample. */
    assert(!real_session_matches(&session, 0, 0));
    assert(real_session_learn(&session, 0, 0, 0));
    assert(real_session_matches(&session, 0, 0));
    assert(!real_session_learn(&session, REAL_MAX_SAMPLES, live, live));
    assert(!real_session_matches(&session, REAL_MAX_SAMPLES, live));
    puts("Live samples: writable image accepted after repeat read; unstable, changed, missing and out-of-range backend samples rejected");
    return 0;
}
