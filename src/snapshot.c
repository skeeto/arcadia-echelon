#include <windows.h>
#include <string.h>
#include "snapshot.h"

static CRITICAL_SECTION s_cs;
static RenderSnapshot    s_buf;
static int               s_ready;
static int               s_inited;

void snap_init(void)
{
    if (s_inited) return;
    InitializeCriticalSection(&s_cs);
    s_ready = 0;
    s_inited = 1;
}

void snap_destroy(void)
{
    if (!s_inited) return;
    DeleteCriticalSection(&s_cs);
    s_inited = 0;
    s_ready = 0;
}

void snap_publish(const RenderSnapshot *s)
{
    if (!s_inited) return;
    EnterCriticalSection(&s_cs);
    s_buf = *s;
    s_ready = 1;
    LeaveCriticalSection(&s_cs);
}

int snap_consume(RenderSnapshot *out)
{
    int ok = 0;
    if (!s_inited) return 0;
    EnterCriticalSection(&s_cs);
    if (s_ready) { *out = s_buf; ok = 1; }
    LeaveCriticalSection(&s_cs);
    return ok;
}
