// SPDX-License-Identifier: GPL-3.0-or-later

#include "parser.h"
#include "collectors/plugins.d/pluginsd_parser.h"

inline int find_first_keyword(const char *str, char *keyword, int max_size, int (*custom_isspace)(char))
{
    const char *s = str, *keyword_start;

    while (unlikely(custom_isspace(*s))) s++;
    keyword_start = s;

    while (likely(*s && !custom_isspace(*s)) && max_size > 1) {
        *keyword++ = *s++;
        max_size--;
    }
    *keyword = '\0';
    return max_size == 0 ? 0 : (int) (s - keyword_start);
}

/*
 * Initialize a parser 
 *     user   : as defined by the user, will be shared across calls
 *     input  : main input stream (auto detect stream -- file, socket, pipe)
 *     buffer : This is the buffer to be used (if null a buffer of size will be allocated)
 *     size   : buffer size either passed or will be allocated
 *              If the buffer is auto allocated, it will auto freed when the parser is destroyed
 *     
 * 
 */

PARSER *parser_init(RRDHOST *host, void *user, void *input, void *output, PARSER_INPUT_TYPE flags)
{
    PARSER *parser;

    parser = callocz(1, sizeof(*parser));
    parser->plugins_action = callocz(1, sizeof(PLUGINSD_ACTION));
    parser->user = user;
    parser->input = input;
    parser->output = output;
    parser->flags = flags;
    parser->host = host;
    parser->worker_job_next_id = WORKER_PARSER_FIRST_JOB;
    inflight_functions_init(parser);

#ifdef ENABLE_HTTPS
    parser->bytesleft = 0;
    parser->readfrom = NULL;
#endif

    if (unlikely(!(flags & PARSER_NO_PARSE_INIT))) {
        parser_add_keyword(parser, PLUGINSD_KEYWORD_FLUSH,          pluginsd_flush);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_CHART,          pluginsd_chart);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_DIMENSION,      pluginsd_dimension);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_DISABLE,        pluginsd_disable);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_VARIABLE,       pluginsd_variable);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_LABEL,          pluginsd_label);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_OVERWRITE,      pluginsd_overwrite);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_END,            pluginsd_end);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_CLABEL_COMMIT,  pluginsd_clabel_commit);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_CLABEL,         pluginsd_clabel);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_BEGIN,          pluginsd_begin);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_SET,            pluginsd_set);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_FUNCTION,       pluginsd_function);
        parser_add_keyword(parser, PLUGINSD_KEYWORD_FUNCTION_RESULT_BEGIN, pluginsd_function_result_begin);
        //parser_add_keyword(parser, PLUGINSD_KEYWORD_GAPS_REQUEST,   pluginsd_gaps_request);
    }

    if(unlikely(!(flags & PARSER_NO_ACTION_INIT))) {
        parser->plugins_action->begin_action            = &pluginsd_begin_action;
        parser->plugins_action->flush_action            = &pluginsd_flush_action;
        parser->plugins_action->end_action              = &pluginsd_end_action;
        parser->plugins_action->disable_action          = &pluginsd_disable_action;
        parser->plugins_action->variable_action         = &pluginsd_variable_action;
        parser->plugins_action->dimension_action        = &pluginsd_dimension_action;
        parser->plugins_action->label_action            = &pluginsd_label_action;
        parser->plugins_action->overwrite_action        = &pluginsd_overwrite_action;
        parser->plugins_action->chart_action            = &pluginsd_chart_action;
        parser->plugins_action->set_action              = &pluginsd_set_action;
    }

    return parser;
}


/*
 * Push a new line into the parsing stream
 *
 * This line will be the next one to process ie the next fetch will get this one
 *
 */

int parser_push(PARSER *parser, char *line)
{
    PARSER_DATA    *tmp_parser_data;
    
    if (unlikely(!parser))
        return 1;

    if (unlikely(!line))
        return 0;

    tmp_parser_data = callocz(1, sizeof(*tmp_parser_data));
    tmp_parser_data->line = strdupz(line);
    tmp_parser_data->next = parser->data;
    parser->data = tmp_parser_data;

    return 0;
}

/*
 * Add a keyword and the corresponding function that will be called
 * Multiple functions may be added
 * Input : keyword
 *       : callback function
 *       : flags
 * Output: > 0 registered function number
 *       : 0 Error
 */

int parser_add_keyword(PARSER *parser, char *keyword, keyword_function func)
{
    PARSER_KEYWORD  *tmp_keyword;

    if (strcmp(keyword, "_read") == 0) {
        parser->read_function = (void *) func;
        return 0;
    }

    if (strcmp(keyword, "_eof") == 0) {
        parser->eof_function = (void *) func;
        return 0;
    }

    if (strcmp(keyword, "_unknown") == 0) {
        parser->unknown_function = (void *) func;
        return 0;
    }

    uint32_t    keyword_hash = simple_hash(keyword);

    tmp_keyword = parser->keyword;

    while (tmp_keyword) {
        if (tmp_keyword->keyword_hash == keyword_hash && (!strcmp(tmp_keyword->keyword, keyword))) {
                if (tmp_keyword->func_no == PARSER_MAX_CALLBACKS)
                    return 0;
                tmp_keyword->func[tmp_keyword->func_no++] = (void *) func;
                return tmp_keyword->func_no;
        }
        tmp_keyword = tmp_keyword->next;
    }

    tmp_keyword = callocz(1, sizeof(*tmp_keyword));

    tmp_keyword->worker_job_id = parser->worker_job_next_id++;
    tmp_keyword->keyword = strdupz(keyword);
    tmp_keyword->keyword_hash = keyword_hash;
    tmp_keyword->func[tmp_keyword->func_no++] = (void *) func;

    worker_register_job_name(tmp_keyword->worker_job_id, tmp_keyword->keyword);

    tmp_keyword->next = parser->keyword;
    parser->keyword = tmp_keyword;
    return tmp_keyword->func_no;
}

/*
 * Cleanup a previously allocated parser
 */

void parser_destroy(PARSER *parser)
{
    if (unlikely(!parser))
        return;

    dictionary_destroy(parser->inflight.functions);

    PARSER_KEYWORD  *tmp_keyword, *tmp_keyword_next;
    PARSER_DATA     *tmp_parser_data, *tmp_parser_data_next;
    
    // Remove keywords
    tmp_keyword = parser->keyword;
    while (tmp_keyword) {
        tmp_keyword_next = tmp_keyword->next;
        freez(tmp_keyword->keyword);
        freez(tmp_keyword);
        tmp_keyword =  tmp_keyword_next;
    }
    
    // Remove pushed data if any
    tmp_parser_data = parser->data;
    while (tmp_parser_data) {
        tmp_parser_data_next = tmp_parser_data->next;
        freez(tmp_parser_data->line);
        freez(tmp_parser_data);
        tmp_parser_data =  tmp_parser_data_next;
    }

    freez(parser->plugins_action);
    freez(parser);
}


/*
 * Fetch the next line to process
 *
 */

int parser_next(PARSER *parser)
{
    char    *tmp = NULL;

    if (unlikely(!parser))
        return 1;

    parser->flags &= ~(PARSER_INPUT_PROCESSED);

    PARSER_DATA  *tmp_parser_data = parser->data;

    if (unlikely(tmp_parser_data)) {
        strncpyz(parser->buffer, tmp_parser_data->line, PLUGINSD_LINE_MAX);
        parser->data = tmp_parser_data->next;
        freez(tmp_parser_data->line);
        freez(tmp_parser_data);
        return 0;
    }

    if (unlikely(parser->read_function))
        tmp = parser->read_function(parser->buffer, PLUGINSD_LINE_MAX, parser->input);
    else
        tmp = fgets(parser->buffer, PLUGINSD_LINE_MAX, (FILE *)parser->input);

    if (unlikely(!tmp)) {
        if (unlikely(parser->eof_function)) {
            int rc = parser->eof_function(parser->input);
            error("read failed: user defined function returned %d", rc);
        }
        else {
            if (feof((FILE *)parser->input))
                error("read failed: end of file");
            else if (ferror((FILE *)parser->input))
                error("read failed: input error");
            else
                error("read failed: unknown error");
        }
        return 1;
    }
    return 0;
}


/*
* Takes an initialized parser object that has an unprocessed entry (by calling parser_next)
* and if it contains a valid keyword, it will execute all the callbacks
*
*/

inline int parser_action(PARSER *parser, char *input)
{
    PARSER_RC rc = PARSER_RC_OK;
    char *words[PLUGINSD_MAX_WORDS] = { NULL };
    char command[PLUGINSD_LINE_MAX + 1];
    keyword_function action_function;
    keyword_function *action_function_list = NULL;

    if (unlikely(!parser)) {
        internal_error(true, "parser is NULL");
        return 1;
    }

    parser->recover_location[0] = 0x0;

    // if not direct input check if we have reprocessed this
    if (unlikely(!input && parser->flags & PARSER_INPUT_PROCESSED))
        return 0;

    PARSER_KEYWORD *tmp_keyword = parser->keyword;
    if (unlikely(!tmp_keyword)) {
        internal_error(true, "called without a keyword");
        return 1;
    }

    if (unlikely(!input))
        input = parser->buffer;

    if(unlikely(parser->flags & PARSER_DEFER_UNTIL_KEYWORD)) {
        bool has_keyword = find_first_keyword(input, command, PLUGINSD_LINE_MAX, pluginsd_space);

        if(!has_keyword || strcmp(command, parser->defer.end_keyword) != 0) {
            if(parser->defer.response) {
                buffer_strcat(parser->defer.response, input);
                if(buffer_strlen(parser->defer.response) > 10 * 1024 * 1024) {
                    // more than 10MB of data
                    // a bad plugin that did not send the end_keyword
                    internal_error(true, "Deferred response is too big (%zu bytes). Stopping this plugin.", buffer_strlen(parser->defer.response));
                    return 1;
                }
            }
            return 0;
        }
        else {
            // call the action
            parser->defer.action(parser, parser->defer.action_data);

            // empty everything
            parser->defer.action = NULL;
            parser->defer.action_data = NULL;
            parser->defer.end_keyword = NULL;
            parser->defer.response = NULL;
            parser->flags &= ~PARSER_DEFER_UNTIL_KEYWORD;
        }
        return 0;
    }

    if (unlikely(!find_first_keyword(input, command, PLUGINSD_LINE_MAX, pluginsd_space)))
        return 0;

    if ((parser->flags & PARSER_INPUT_KEEP_ORIGINAL) == PARSER_INPUT_KEEP_ORIGINAL)
        pluginsd_split_words(input, words, PLUGINSD_MAX_WORDS, parser->recover_input, parser->recover_location, PARSER_MAX_RECOVER_KEYWORDS);
    else
        pluginsd_split_words(input, words, PLUGINSD_MAX_WORDS, NULL, NULL, 0);

    uint32_t command_hash = simple_hash(command);

    size_t worker_job_id = WORKER_UTILIZATION_MAX_JOB_TYPES + 1; // set an invalid value by default
    while(tmp_keyword) {
        if (command_hash == tmp_keyword->keyword_hash &&
                (!strcmp(command, tmp_keyword->keyword))) {
                    action_function_list = &tmp_keyword->func[0];
                    worker_job_id = tmp_keyword->worker_job_id;
                    break;
        }
        tmp_keyword = tmp_keyword->next;
    }

    if (unlikely(!action_function_list)) {
        if (unlikely(parser->unknown_function))
            rc = parser->unknown_function(words, parser->user, NULL);
        else
            rc = PARSER_RC_ERROR;

        internal_error(rc != PARSER_RC_OK, "Unknown keyword [%s]", input);
    }
    else {
        worker_is_busy(worker_job_id);
        while ((action_function = *action_function_list) != NULL) {
                rc = action_function(words, parser->user, parser->plugins_action);
                if (unlikely(rc == PARSER_RC_ERROR || rc == PARSER_RC_STOP)) {
                    internal_error(true, "action_function() failed with rc = %u", rc);
                    break;
                }
                action_function_list++;
        }
        worker_is_idle();
    }

    if (likely(input == parser->buffer))
        parser->flags |= PARSER_INPUT_PROCESSED;

    internal_error(rc == PARSER_RC_ERROR, "parser_action() failed.");
    return (rc == PARSER_RC_ERROR);
}

inline int parser_recover_input(PARSER *parser)
{
    if (unlikely(!parser))
        return 1;

    for(int i=0; i < PARSER_MAX_RECOVER_KEYWORDS && parser->recover_location[i]; i++)
        *(parser->recover_location[i]) = parser->recover_input[i];

    parser->recover_location[0] = 0x0;

    return 0;
}
