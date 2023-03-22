#ifndef _LOG_H
#define _LOG_H

#ifdef __cpluscplus
extern "C"
{
#endif

#include <stdio.h>
#include <pthread.h>
#include <MESA/MESA_handle_logger.h>

extern void *g_default_logger;

int LOG_INIT(const char *profile);
void LOG_CLOSE(void);
void LOG_RELOAD(void);

// __FUNCTION__
#define LOG_DEBUG(format, ...)                                                                           \
	do                                                                                                   \
	{                                                                                                    \
		if (g_default_logger)                                                                            \
		{                                                                                                \
			char __tid_buff[16] = {0};                                                                   \
			snprintf(__tid_buff, 16, "tid:%ld", pthread_self());                                         \
			MESA_handle_runtime_log(g_default_logger, RLOG_LV_DEBUG, __tid_buff, format, ##__VA_ARGS__); \
		}                                                                                                \
		else                                                                                             \
		{                                                                                                \
			fprintf(stdout, "DEBUG " format "\n", ##__VA_ARGS__);                                        \
		}                                                                                                \
	} while (0)

#define LOG_INFO(format, ...)                                                                           \
	do                                                                                                  \
	{                                                                                                   \
		if (g_default_logger)                                                                           \
		{                                                                                               \
			char __tid_buff[16] = {0};                                                                  \
			snprintf(__tid_buff, 16, "tid:%ld", pthread_self());                                        \
			MESA_handle_runtime_log(g_default_logger, RLOG_LV_INFO, __tid_buff, format, ##__VA_ARGS__); \
		}                                                                                               \
		else                                                                                            \
		{                                                                                               \
			fprintf(stdout, "INFOR " format "\n", ##__VA_ARGS__);                                       \
		}                                                                                               \
	} while (0)

#define LOG_ERROR(format, ...)                                                                           \
	do                                                                                                   \
	{                                                                                                    \
		if (g_default_logger)                                                                            \
		{                                                                                                \
			char __tid_buff[16] = {0};                                                                   \
			snprintf(__tid_buff, 16, "tid:%ld", pthread_self());                                         \
			MESA_handle_runtime_log(g_default_logger, RLOG_LV_FATAL, __tid_buff, format, ##__VA_ARGS__); \
		}                                                                                                \
		else                                                                                             \
		{                                                                                                \
			fprintf(stderr, "ERROR " format "\n", ##__VA_ARGS__);                                        \
		}                                                                                                \
	} while (0)

#ifdef __cpluscplus
}
#endif

#endif