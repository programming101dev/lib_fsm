/*
 * Copyright 2021-2024 D'Arcy Smith.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "p101_fsm/fsm.h"
#include "p101_fsm/errors.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_posix/p101_string.h>

static p101_fsm_state_func fsm_transition(const struct p101_env *env, p101_fsm_state_t from_id, p101_fsm_state_t to_id, const struct p101_fsm_transition transitions[], size_t transition_count);
static int                 fsm_info_has_error(const struct p101_fsm_info *info);
static const char         *fsm_info_name_or_default(const struct p101_fsm_info *info);
static int                 fsm_validate_transitions(struct p101_fsm_info *info, const struct p101_fsm_transition transitions[], size_t transition_count);
static void                fsm_write_states(p101_fsm_state_t *from_state_id, p101_fsm_state_t *to_state_id, p101_fsm_state_t from_id, p101_fsm_state_t to_id);

struct p101_fsm_info
{
    const struct p101_env                        *sys_env;
    struct p101_error                            *sys_err;
    char                                         *name;
    const struct p101_env                        *fsm_env;
    struct p101_error                            *fsm_err;
    p101_fsm_state_t                              from_state_id;
    p101_fsm_state_t                              current_state_id;
    p101_fsm_info_will_change_state_notifier_func will_change_state_notifier;
    p101_fsm_info_did_change_state_notifier_func  did_change_state_notifier;
    p101_fsm_info_bad_change_state_notifier_func  bad_change_state_notifier;
    p101_fsm_info_bad_change_state_handler_func   bad_change_state_handler;
    bool                                          running;
};

struct p101_fsm_info *p101_fsm_info_create(const struct p101_env *env, struct p101_error *err, const char *name, const struct p101_env *fsm_env, struct p101_error *fsm_err, p101_fsm_info_bad_change_state_handler_func handler)
{
    const struct p101_env *target_env;
    struct p101_error     *target_err;
    struct p101_fsm_info  *info = NULL;

    P101_TRACE(env);
    target_env = fsm_env == NULL ? env : fsm_env;
    target_err = fsm_err == NULL ? err : fsm_err;

    if(p101_error_has_error(err) || p101_error_has_error(target_err))
    {
        goto done;
    }

    if(name == NULL)
    {
        P101_ERROR_RAISE_USER(target_err, "FSM name cannot be NULL", P101_FSM_ERROR_INVALID_ARGUMENT);
        goto done;
    }

    info = (struct p101_fsm_info *)p101_calloc(target_env, target_err, 1, sizeof(struct p101_fsm_info));

    if(info == NULL)
    {
        goto done;
    }

    info->name = p101_strdup(target_env, target_err, name);

    if(info->name == NULL || p101_error_has_error(target_err))
    {
        p101_free(target_env, info->name);
        p101_free(target_env, info);
        info = NULL;
        goto done;
    }

    info->from_state_id    = P101_FSM_INIT;
    info->current_state_id = P101_FSM_USER_START;
    info->sys_env          = env;
    info->sys_err          = err;

    info->fsm_env = target_env;
    info->fsm_err = target_err;

    if(handler == NULL)
    {
        info->bad_change_state_handler = p101_fsm_info_default_bad_change_state_handler;
    }
    else
    {
        info->bad_change_state_handler = handler;
    }

done:
    P101_TRACE_EXIT(env);
    return info;
}

const char *p101_fsm_info_get_name(const struct p101_env *env, const struct p101_fsm_info *info)
{
    P101_TRACE(env);

    if(info == NULL)
    {
        P101_TRACE_EXIT(env);
        return NULL;
    }

    P101_TRACE_EXIT(env);
    return info->name;
}

void p101_fsm_info_destroy(const struct p101_env *env, struct p101_fsm_info **pinfo)
{
    const struct p101_env *free_env;
    struct p101_fsm_info  *info;

    P101_TRACE(env);

    if(pinfo == NULL)
    {
        goto done;
    }

    info = *pinfo;

    if(info == NULL)
    {
        goto done;
    }

    free_env = info->fsm_env == NULL ? env : info->fsm_env;

    p101_free(free_env, info->name);
    p101_free(free_env, info);
    *pinfo = NULL;

done:
    P101_TRACE_EXIT(env);
}

void p101_fsm_info_set_will_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_will_change_state_notifier_func notifier)
{
    const struct p101_env *env;

    if(info == NULL)
    {
        return;
    }

    env = info->fsm_env;
    P101_TRACE(env);
    info->will_change_state_notifier = notifier;
    P101_TRACE_EXIT(env);
}

void p101_fsm_info_set_did_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_did_change_state_notifier_func notifier)
{
    const struct p101_env *env;

    if(info == NULL)
    {
        return;
    }

    env = info->fsm_env;
    P101_TRACE(env);
    info->did_change_state_notifier = notifier;
    P101_TRACE_EXIT(env);
}

void p101_fsm_info_set_bad_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_notifier_func notifier)
{
    const struct p101_env *env;

    if(info == NULL)
    {
        return;
    }

    env = info->fsm_env;
    P101_TRACE(env);
    info->bad_change_state_notifier = notifier;
    P101_TRACE_EXIT(env);
}

void p101_fsm_info_set_bad_change_state_handler(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_handler_func handler)
{
    const struct p101_env *env;

    if(info == NULL)
    {
        return;
    }

    env = info->fsm_env;
    P101_TRACE(env);

    info->bad_change_state_handler = handler == NULL ? p101_fsm_info_default_bad_change_state_handler : handler;
    P101_TRACE_EXIT(env);
}

p101_fsm_info_will_change_state_notifier_func p101_fsm_info_get_will_change_state_notifier(const struct p101_fsm_info *info)
{
    const struct p101_env *env;

    if(info == NULL)
    {
        return NULL;
    }

    env = info->fsm_env;
    P101_TRACE(env);

    P101_TRACE_EXIT(env);
    return info->will_change_state_notifier;
}

p101_fsm_info_did_change_state_notifier_func p101_fsm_info_get_did_change_state_notifier(const struct p101_fsm_info *info)
{
    const struct p101_env *env;

    if(info == NULL)
    {
        return NULL;
    }

    env = info->fsm_env;
    P101_TRACE(env);

    P101_TRACE_EXIT(env);
    return info->did_change_state_notifier;
}

p101_fsm_info_bad_change_state_notifier_func p101_fsm_info_get_bad_change_state_notifier(const struct p101_fsm_info *info)
{
    const struct p101_env *env;

    if(info == NULL)
    {
        return NULL;
    }

    env = info->fsm_env;
    P101_TRACE(env);

    P101_TRACE_EXIT(env);
    return info->bad_change_state_notifier;
}

p101_fsm_info_bad_change_state_handler_func p101_fsm_info_get_bad_change_state_handler(const struct p101_fsm_info *info)
{
    const struct p101_env *env;

    if(info == NULL)
    {
        return NULL;
    }

    env = info->fsm_env;
    P101_TRACE(env);

    P101_TRACE_EXIT(env);
    return info->bad_change_state_handler;
}

p101_fsm_state_t p101_fsm_info_default_bad_change_state_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    struct p101_error *target_err;

    P101_TRACE(env);

    if(err == NULL)
    {
        if(info == NULL)
        {
            target_err = NULL;
        }
        else
        {
            target_err = info->fsm_err;
        }
    }
    else
    {
        target_err = err;
    }

    if(target_err != NULL)
    {
        P101_ERROR_RAISE_USER_PRINTF(target_err, P101_FSM_ERROR_UNKNOWN_TRANSITION, "Unknown FSM state transition: %d -> %d", from_state_id, to_state_id);
    }

    P101_TRACE_EXIT(env);
    return P101_FSM_EXIT;
}

void p101_fsm_info_default_bad_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    P101_TRACE(env);
    (void)p101_printf(env, err, "%s: bad change state from %d to %d\n", fsm_info_name_or_default(info), from_state_id, to_state_id);
    P101_TRACE_EXIT(env);
}

void p101_fsm_info_default_will_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    P101_TRACE(env);
    (void)p101_printf(env, err, "%s: will attempt state transition %d -> %d\n", fsm_info_name_or_default(info), from_state_id, to_state_id);
    P101_TRACE_EXIT(env);
}

void p101_fsm_info_default_did_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id)
{
    P101_TRACE(env);
    (void)p101_printf(env, err, "%s: completed state transition %d -> %d; next state %d\n", fsm_info_name_or_default(info), from_state_id, to_state_id, next_state_id);
    P101_TRACE_EXIT(env);
}

p101_fsm_run_result p101_fsm_run(struct p101_fsm_info *info, p101_fsm_state_t *from_state_id, p101_fsm_state_t *to_state_id, void *arg, const struct p101_fsm_transition transitions[], size_t transition_count)
{
    p101_fsm_state_t       from_id;
    p101_fsm_state_t       to_id;
    p101_fsm_run_result    result;
    size_t                 redirects;
    bool                   started;
    const struct p101_env *env;
    struct p101_error     *err;

    if(info == NULL)
    {
        return P101_FSM_RUN_ERROR;
    }

    env = info->fsm_env;
    err = info->fsm_err;
    P101_TRACE(env);
    result    = P101_FSM_RUN_ERROR;
    redirects = 0;
    started   = false;

    if(fsm_info_has_error(info))
    {
        goto done;
    }

    if(info->running)
    {
        P101_ERROR_RAISE_USER(err, "Cannot run an FSM recursively", P101_FSM_ERROR_REENTRANT_RUN);
        goto done;
    }

    from_id = info->from_state_id;
    to_id   = info->current_state_id;

    if(to_id == P101_FSM_EXIT)
    {
        fsm_write_states(from_state_id, to_state_id, from_id, to_id);
        result = P101_FSM_RUN_EXITED;
        goto done;
    }

    if(!fsm_validate_transitions(info, transitions, transition_count))
    {
        goto done;
    }

    info->running = true;
    started       = true;

    do
    {
        p101_fsm_state_func perform;
        p101_fsm_state_t    next_id;

        fsm_write_states(from_state_id, to_state_id, from_id, to_id);

        if(info->will_change_state_notifier)
        {
            info->will_change_state_notifier(info->fsm_env, info->fsm_err, info, from_id, to_id);
        }

        if(fsm_info_has_error(info))
        {
            goto done;
        }

        perform = fsm_transition(info->fsm_env, from_id, to_id, transitions, transition_count);

        if(perform == NULL)
        {
            if(info->bad_change_state_notifier)
            {
                info->bad_change_state_notifier(info->fsm_env, info->fsm_err, info, from_id, to_id);
            }

            if(fsm_info_has_error(info))
            {
                goto done;
            }

            next_id = info->bad_change_state_handler(info->fsm_env, info->fsm_err, info, from_id, to_id);

            if(fsm_info_has_error(info))
            {
                goto done;
            }

            if(next_id == P101_FSM_IGNORE)
            {
                result = P101_FSM_RUN_PAUSED;
                goto done;
            }

            if(next_id == to_id)
            {
                P101_ERROR_RAISE_USER(err, "Bad-transition handler returned the rejected state", P101_FSM_ERROR_HANDLER_LOOP);
                goto done;
            }

            if(next_id == P101_FSM_EXIT)
            {
                info->from_state_id    = from_id;
                info->current_state_id = P101_FSM_EXIT;
                fsm_write_states(from_state_id, to_state_id, from_id, P101_FSM_EXIT);
                result = P101_FSM_RUN_EXITED;
                goto done;
            }

            if(next_id < P101_FSM_USER_START)
            {
                P101_ERROR_RAISE_USER(err, "Bad-transition handler returned an invalid state", P101_FSM_ERROR_INVALID_ARGUMENT);
                goto done;
            }

            redirects++;
            if(redirects > transition_count)
            {
                P101_ERROR_RAISE_USER(err, "Bad-transition handler entered a redirect cycle", P101_FSM_ERROR_HANDLER_LOOP);
                goto done;
            }
        }
        else
        {
            info->from_state_id    = from_id;
            info->current_state_id = to_id;
            from_id                = to_id;
            next_id                = perform(info->sys_env, info->sys_err, arg);

            if(fsm_info_has_error(info) || next_id == P101_FSM_IGNORE)
            {
                if(!fsm_info_has_error(info))
                {
                    result = P101_FSM_RUN_PAUSED;
                }
                goto done;
            }

            if(next_id != P101_FSM_EXIT && next_id < P101_FSM_USER_START)
            {
                P101_ERROR_RAISE_USER(info->sys_err, "FSM state callback returned an invalid state", P101_FSM_ERROR_INVALID_ARGUMENT);
                goto done;
            }

            if(info->did_change_state_notifier)
            {
                info->did_change_state_notifier(info->fsm_env, info->fsm_err, info, info->from_state_id, info->current_state_id, next_id);
            }

            if(fsm_info_has_error(info))
            {
                goto done;
            }

            redirects = 0;

            if(next_id == P101_FSM_EXIT)
            {
                info->from_state_id    = info->current_state_id;
                info->current_state_id = P101_FSM_EXIT;
                fsm_write_states(from_state_id, to_state_id, info->from_state_id, info->current_state_id);
                result = P101_FSM_RUN_EXITED;
                goto done;
            }
        }

        to_id = next_id;
    } while(to_id != P101_FSM_EXIT);

done:
    if(started)
    {
        info->running = false;
    }
    P101_TRACE_EXIT(env);
    return result;
}

static int fsm_info_has_error(const struct p101_fsm_info *info)
{
    const struct p101_error *app_err;
    const struct p101_error *fsm_err;

    if(info == NULL)
    {
        return 0;
    }

    app_err = info->sys_err;
    fsm_err = info->fsm_err;
    return p101_error_has_error(app_err) || p101_error_has_error(fsm_err);
}

static const char *fsm_info_name_or_default(const struct p101_fsm_info *info)
{
    if(info == NULL || info->name == NULL)
    {
        return "<unnamed>";
    }

    return info->name;
}

static int fsm_validate_transitions(struct p101_fsm_info *info, const struct p101_fsm_transition transitions[], size_t transition_count)
{
    const struct p101_env *env;
    struct p101_error     *err;
    int                    valid;

    env = info->fsm_env;
    err = info->fsm_err;
    P101_TRACE(env);
    valid = 0;

    if(transitions == NULL || transition_count == 0)
    {
        P101_ERROR_RAISE_USER(err, "FSM transition table cannot be empty", P101_FSM_ERROR_INVALID_TRANSITION_TABLE);
        goto done;
    }

    for(size_t i = 0; i < transition_count; ++i)
    {
        if((transitions[i].from_id != P101_FSM_INIT && transitions[i].from_id < P101_FSM_USER_START) || transitions[i].to_id < P101_FSM_USER_START || transitions[i].perform == NULL)
        {
            P101_ERROR_RAISE_USER_PRINTF(err, P101_FSM_ERROR_INVALID_TRANSITION_TABLE, "Invalid FSM transition table entry at index %zu", i);
            goto done;
        }

        for(size_t j = 0; j < i; ++j)
        {
            if(transitions[i].from_id == transitions[j].from_id && transitions[i].to_id == transitions[j].to_id)
            {
                P101_ERROR_RAISE_USER_PRINTF(err, P101_FSM_ERROR_INVALID_TRANSITION_TABLE, "Duplicate FSM transition %d -> %d", transitions[i].from_id, transitions[i].to_id);
                goto done;
            }
        }
    }

    valid = 1;

done:
    P101_TRACE_EXIT(env);
    return valid;
}

static void fsm_write_states(p101_fsm_state_t *from_state_id, p101_fsm_state_t *to_state_id, p101_fsm_state_t from_id, p101_fsm_state_t to_id)
{
    if(from_state_id != NULL)
    {
        *from_state_id = from_id;
    }

    if(to_state_id != NULL)
    {
        *to_state_id = to_id;
    }
}

static p101_fsm_state_func fsm_transition(const struct p101_env *env, p101_fsm_state_t from_id, p101_fsm_state_t to_id, const struct p101_fsm_transition transitions[], size_t transition_count)
{
    p101_fsm_state_func perform;

    P101_TRACE(env);
    perform = NULL;

    for(size_t i = 0; i < transition_count; ++i)
    {
        if(transitions[i].from_id == from_id && transitions[i].to_id == to_id)
        {
            perform = transitions[i].perform;
            break;
        }
    }

    P101_TRACE_EXIT(env);
    return perform;
}

p101_fsm_state_t p101_fsm_exit_immediately(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);
    (void)err;
    (void)arg;
    P101_TRACE_EXIT(env);
    return P101_FSM_EXIT;
}
