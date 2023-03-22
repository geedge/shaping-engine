#include "log.h"

void *g_default_logger = NULL;

// return  0 : success
// return -1 : error
int LOG_INIT(const char *profile)
{
    // if (0 != MESA_handle_runtime_log_creation(profile))
    // {
    //     fprintf(stderr, "FATAL: unable to create runtime logger\n");
    //     return -1;
    // }

    g_default_logger = MESA_create_runtime_log_handle("./log/shaping", RLOG_LV_DEBUG);
    if (g_default_logger == NULL)
    {
        fprintf(stderr, "FATAL: unable to create log handle\n");
        return -1;
    }

    return 0;
}

void LOG_CLOSE(void)
{
    MESA_destroy_runtime_log_handle(g_default_logger);
    MESA_handle_runtime_log_destruction();
}

void LOG_RELOAD(void)
{
    MESA_handle_runtime_log_reconstruction(NULL);
}
