/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/

#include <sdr_i.h>

IRX_ID("sdr_driver", 4, 1);
// Based on the module from SDK 3.1.0.

extern struct irx_export_table _exp_sdrdrv;
#ifdef PSBBN_QUIET_START
/* Retail startup must not depend on the IOP's optional debug console. */
#define START_LOG(...) ((void)0)
#else
#define START_LOG(...) Kprintf(__VA_ARGS__)
#endif
// Unofficial: wrap members for relative access
SdrInfo g_sdrInfo;

static int module_start(int ac, char **av)
{
	int code;
	int i;
	iop_thread_t thprarm;
	int state;
	// Unofficial: make local
	int initial_priority_main;

	CpuSuspendIntr(&state);
	code = RegisterLibraryEntries(&_exp_sdrdrv);
	CpuResumeIntr(state);
	if ( code )
		return 1;
	START_LOG("SDR driver version 4.0.1 (C) SCEI\n");
	initial_priority_main = 24;
	g_eeCBInfo.m_initial_priority_cb = 24;
	g_sdrInfo.m_thid_main = 0;
	g_eeCBInfo.m_thid_cb = 0;
	for ( i = 1; i < ac; i += 1 )
	{
		if ( !strncmp("thpri=", av[i], 6) )
		{
			const char *p;

			p = av[i] + 6;
			if ( isdigit(*p) )
			{
				initial_priority_main = strtol(p, 0, 10);
				if ( (unsigned int)(initial_priority_main - 9) >= 0x73 )
				{
					START_LOG(" SDR driver error: invalid priority %d\n", initial_priority_main);
					initial_priority_main = 24;
				}
			}
			while ( isdigit(*p) )
				p += 1;
			if ( *p == ',' && isdigit(p[1]) )
			{
				g_eeCBInfo.m_initial_priority_cb = strtol(&p[1], 0, 10);
				if ( (unsigned int)(g_eeCBInfo.m_initial_priority_cb - 9) >= 0x73 )
				{
						START_LOG(" SDR driver error: invalid priority %d\n", g_eeCBInfo.m_initial_priority_cb);
					g_eeCBInfo.m_initial_priority_cb = 24;
				}
			}
			if ( g_eeCBInfo.m_initial_priority_cb < initial_priority_main )
			{
				START_LOG(" SDR driver ERROR:\n");
				START_LOG("   callback th. priority is higher than main th. priority.\n");
				g_eeCBInfo.m_initial_priority_cb = initial_priority_main;
			}
			START_LOG(
				" SDR driver: thread priority: main=%d, callback=%d\n",
				initial_priority_main,
				g_eeCBInfo.m_initial_priority_cb);
		}
	}
	memset(&thprarm, 0, sizeof(thprarm));
	thprarm.attr = TH_C;
	thprarm.thread = sce_sdr_loop;
	// Unofficial: original stack size was 2048
	thprarm.stacksize = 4096;
	thprarm.option = 0;
	thprarm.priority = initial_priority_main;
	g_sdrInfo.m_thid_main = CreateThread(&thprarm);
	if ( g_sdrInfo.m_thid_main <= 0 )
	{
		ReleaseLibraryEntries(&_exp_sdrdrv);
		g_sdrInfo.m_thid_main = 0;
		return 1;
	}
	if ( StartThread(g_sdrInfo.m_thid_main, &g_sdrInfo) < 0 )
	{
		DeleteThread(g_sdrInfo.m_thid_main);
		g_sdrInfo.m_thid_main = 0;
		ReleaseLibraryEntries(&_exp_sdrdrv);
		return 1;
	}
	START_LOG(" Exit rsd_main \n");
	return 2;
}

static int module_stop(int ac, char **av)
{
	int code;
	int state;

	(void)ac;
	(void)av;

	CpuSuspendIntr(&state);
	code = ReleaseLibraryEntries(&_exp_sdrdrv);
	CpuResumeIntr(state);
	if ( code )
		return 2;
	sceSdSetTransIntrHandler(0, 0, 0);
	sceSdSetTransIntrHandler(1, 0, 0);
	sceSdSetSpu2IntrHandler(0, 0);
	if ( g_eeCBInfo.m_thid_cb > 0 )
	{
		TerminateThread(g_eeCBInfo.m_thid_cb);
		DeleteThread(g_eeCBInfo.m_thid_cb);
		g_eeCBInfo.m_thid_cb = 0;
	}
	if ( g_sdrInfo.m_thid_main > 0 )
	{
		sceSifRemoveRpc(g_sdrInfo.m_rpc_sd, g_sdrInfo.m_rpc_qd);
		sceSifRemoveRpcQueue(g_sdrInfo.m_rpc_qd);
		TerminateThread(g_sdrInfo.m_thid_main);
		DeleteThread(g_sdrInfo.m_thid_main);
		g_sdrInfo.m_thid_main = 0;
	}
	Kprintf(" sdrdrv: unloaded! \n");
	return 1;
}

int _start(int ac, char **av)
{
	return (ac >= 0) ? module_start(ac, av) : module_stop(-ac, av);
}

int sceSdrChangeThreadPriority(int priority_main, int priority_cb)
{
	int cur_priority;
	int ret;
	iop_thread_info_t thstatus;

	if ( (unsigned int)(priority_main - 9) >= 0x73 || (unsigned int)(priority_cb - 9) >= 0x73 )
		return -403;
	if ( priority_cb < priority_main )
	{
		Kprintf(" SDR driver ERROR:\n");
		Kprintf("   callback th. priority is higher than main th. priority.\n");
	}
	cur_priority = (priority_cb < priority_main) ? priority_main : priority_cb;
	ReferThreadStatus(TH_SELF, &thstatus);
	ChangeThreadPriority(TH_SELF, 8);
	ret = 0;
	if ( g_sdrInfo.m_thid_main > 0 )
		ret = ChangeThreadPriority(g_sdrInfo.m_thid_main, priority_main);
	if ( ret < 0 )
		return ret;
	if ( g_eeCBInfo.m_thid_cb > 0 )
		ret = ChangeThreadPriority(g_eeCBInfo.m_thid_cb, cur_priority);
	if ( ret < 0 )
		return ret;
	g_eeCBInfo.m_initial_priority_cb = cur_priority;
	ChangeThreadPriority(TH_SELF, thstatus.currentPriority);
	return 0;
}
