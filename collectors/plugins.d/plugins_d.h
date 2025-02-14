// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETDATA_PLUGINS_D_H
#define NETDATA_PLUGINS_D_H 1

#include "daemon/common.h"

#define PLUGINSD_FILE_SUFFIX ".plugin"
#define PLUGINSD_FILE_SUFFIX_LEN strlen(PLUGINSD_FILE_SUFFIX)
#define PLUGINSD_CMD_MAX (FILENAME_MAX*2)
#define PLUGINSD_STOCK_PLUGINS_DIRECTORY_PATH 0

#define PLUGINSD_KEYWORD_CHART                  "CHART"
#define PLUGINSD_KEYWORD_DIMENSION              "DIMENSION"
#define PLUGINSD_KEYWORD_BEGIN                  "BEGIN"
#define PLUGINSD_KEYWORD_SET                    "SET"
#define PLUGINSD_KEYWORD_END                    "END"
#define PLUGINSD_KEYWORD_FLUSH                  "FLUSH"
#define PLUGINSD_KEYWORD_DISABLE                "DISABLE"
#define PLUGINSD_KEYWORD_VARIABLE               "VARIABLE"
#define PLUGINSD_KEYWORD_LABEL                  "LABEL"
#define PLUGINSD_KEYWORD_OVERWRITE              "OVERWRITE"
#define PLUGINSD_KEYWORD_CLABEL                 "CLABEL"
#define PLUGINSD_KEYWORD_CLABEL_COMMIT          "CLABEL_COMMIT"
#define PLUGINSD_KEYWORD_FUNCTION               "FUNCTION"
#define PLUGINSD_KEYWORD_FUNCTION_RESULT_BEGIN  "FUNCTION_RESULT_BEGIN"
#define PLUGINSD_KEYWORD_FUNCTION_RESULT_END    "FUNCTION_RESULT_END"
#define PLUGINSD_KEYWORD_GUID                   "GUID"
#define PLUGINSD_KEYWORD_CONTEXT                "CONTEXT"
#define PLUGINSD_KEYWORD_TOMBSTONE              "TOMBSTONE"
#define PLUGINSD_KEYWORD_HOST                   "HOST"
//#define PLUGINSD_KEYWORD_GAPS_REQUEST           "GAPS_REQUEST" // child -> parent
//#define PLUGINSD_KEYWORD_CHART_GAP              "CHART_GAP"    // parent <- child

#define PLUGINS_FUNCTIONS_TIMEOUT_DEFAULT 10 // seconds

#define PLUGINSD_LINE_MAX_SSL_READ 512
#define PLUGINSD_MAX_WORDS 20

#define PLUGINSD_MAX_DIRECTORIES 20
extern char *plugin_directories[PLUGINSD_MAX_DIRECTORIES];

struct plugind {
    char id[CONFIG_MAX_NAME+1];         // config node id

    char filename[FILENAME_MAX+1];      // just the filename
    char fullfilename[FILENAME_MAX+1];  // with path
    char cmd[PLUGINSD_CMD_MAX+1];       // the command that it executes

    volatile pid_t pid;
    netdata_thread_t thread;

    size_t successful_collections;      // the number of times we have seen
                                        // values collected from this plugin

    size_t serial_failures;             // the number of times the plugin started
                                        // without collecting values

    int update_every;                   // the plugin default data collection frequency
    volatile sig_atomic_t obsolete;     // do not touch this structure after setting this to 1
    volatile sig_atomic_t enabled;      // if this is enabled or not

    time_t started_t;
    uint32_t capabilities;              // follows the same principles as streaming capabilities
    struct plugind *next;
};

extern struct plugind *pluginsd_root;

extern size_t pluginsd_process(RRDHOST *host, struct plugind *cd, FILE *fp_plugin_input, FILE *fp_plugin_output, int trust_durations);

extern int pluginsd_initialize_plugin_directories();



#define pluginsd_function_result_begin_to_buffer(wb, transaction, code, content_type, expires)      \
    buffer_sprintf(wb                                                                               \
                    , PLUGINSD_KEYWORD_FUNCTION_RESULT_BEGIN " \"%s\" %d \"%s\" %ld\n"              \
                    , (transaction) ? (transaction) : ""                                            \
                    , (int)(code)                                                                   \
                    , (content_type) ? (content_type) : ""                                          \
                    , (long int)(expires)                                                           \
    )

#define pluginsd_function_result_end_to_buffer(wb) \
    buffer_strcat(wb, "\n" PLUGINSD_KEYWORD_FUNCTION_RESULT_END "\n")

#define pluginsd_function_result_begin_to_stdout(transaction, code, content_type, expires)          \
    fprintf(stdout                                                                                  \
                    , PLUGINSD_KEYWORD_FUNCTION_RESULT_BEGIN " \"%s\" %d \"%s\" %ld\n"              \
                    , (transaction) ? (transaction) : ""                                            \
                    , (int)(code)                                                                   \
                    , (content_type) ? (content_type) : ""                                          \
                    , (long int)(expires)                                                           \
    )

#define pluginsd_function_result_end_to_stdout() \
    fprintf(stdout, "\n" PLUGINSD_KEYWORD_FUNCTION_RESULT_END "\n")

#endif /* NETDATA_PLUGINS_D_H */
