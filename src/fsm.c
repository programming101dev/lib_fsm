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
#include <p101_c/p101_stdlib.h>
#include <p101_posix/p101_string.h>
#include <stdio.h>

static p101_fsm_state_func fsm_transition(const struct p101_env *env, p101_fsm_state_t from_id, p101_fsm_state_t to_id, const struct p101_fsm_transition transitions[], size_t transitions_nbytes);
static int                 fsm_info_has_error(const struct p101_fsm_info *info);
static const char         *fsm_info_name_or_default(const struct p101_fsm_info *info);

enum
{
    P101_FSM_ERROR_MESSAGE_SIZE = 64
};

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
};

struct p101_fsm_info *p101_fsm_info_create(const struct p101_env *env, struct p101_error *err, const char *name, const struct p101_env *fsm_env, struct p101_error *fsm_err, p101_fsm_info_bad_change_state_handler_func handler)
{
    const struct p101_env *target_env;
    struct p101_error     *target_err;
    struct p101_fsm_info  *info;

    P101_TRACE(env);
    target_env = fsm_env == NULL ? env : fsm_env;
    target_err = fsm_err == NULL ? err : fsm_err;

    if(p101_error_has_error(err) || p101_error_has_error(target_err))
    {
        return NULL;
    }

    if(name == NULL)
    {
        P101_ERROR_RAISE_SYSTEM(target_err, "name cannot be NULL", 1);
        return NULL;
    }

    info = (struct p101_fsm_info *)p101_calloc(target_env, target_err, 1, sizeof(struct p101_fsm_info));

    if(info == NULL)
    {
        return NULL;
    }

    info->name = p101_strdup(target_env, target_err, name);

    if(info->name == NULL || p101_error_has_error(target_err))
    {
        p101_free(target_env, info->name);
        p101_free(target_env, info);

        return NULL;
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

    return info;
}

const char *p101_fsm_info_get_name(const struct p101_env *env, const struct p101_fsm_info *info)
{
    P101_TRACE(env);

    if(info == NULL)
    {
        return NULL;
    }

    return info->name;
}

void p101_fsm_info_destroy(const struct p101_env *env, struct p101_fsm_info **pinfo)
{
    const struct p101_env *free_env;
    struct p101_fsm_info  *info;

    P101_TRACE(env);

    if(pinfo == NULL)
    {
        return;
    }

    info = *pinfo;

    if(info == NULL)
    {
        return;
    }

    free_env = info->fsm_env == NULL ? env : info->fsm_env;

    p101_free(free_env, info->name);
    p101_free(free_env, info);
    *pinfo = NULL;
}

void p101_fsm_info_set_will_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_will_change_state_notifier_func notifier)
{
    if(info == NULL)
    {
        return;
    }

    P101_TRACE(info->fsm_env);
    info->will_change_state_notifier = notifier;
}

void p101_fsm_info_set_did_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_did_change_state_notifier_func notifier)
{
    if(info == NULL)
    {
        return;
    }

    P101_TRACE(info->fsm_env);
    info->did_change_state_notifier = notifier;
}

void p101_fsm_info_set_bad_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_notifier_func notifier)
{
    if(info == NULL)
    {
        return;
    }

    P101_TRACE(info->fsm_env);
    info->bad_change_state_notifier = notifier;
}

void p101_fsm_info_set_bad_change_state_handler(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_handler_func handler)
{
    if(info == NULL)
    {
        return;
    }

    P101_TRACE(info->fsm_env);

    if(handler == NULL)
    {
        P101_ERROR_RAISE_SYSTEM(info->fsm_err, "handler cannot be NULL", 1);
    }
    else
    {
        info->bad_change_state_handler = handler;
    }
}

p101_fsm_info_will_change_state_notifier_func p101_fsm_info_get_will_change_state_notifier(const struct p101_fsm_info *info)
{
    if(info == NULL)
    {
        return NULL;
    }

    P101_TRACE(info->fsm_env);

    return info->will_change_state_notifier;
}

p101_fsm_info_did_change_state_notifier_func p101_fsm_info_get_did_change_state_notifier(const struct p101_fsm_info *info)
{
    if(info == NULL)
    {
        return NULL;
    }

    P101_TRACE(info->fsm_env);

    return info->did_change_state_notifier;
}

p101_fsm_info_bad_change_state_notifier_func p101_fsm_info_get_bad_change_state_notifier(const struct p101_fsm_info *info)
{
    if(info == NULL)
    {
        return NULL;
    }

    P101_TRACE(info->fsm_env);

    return info->bad_change_state_notifier;
}

p101_fsm_info_bad_change_state_handler_func p101_fsm_info_get_bad_change_state_handler(const struct p101_fsm_info *info)
{
    if(info == NULL)
    {
        return NULL;
    }

    P101_TRACE(info->fsm_env);

    return info->bad_change_state_handler;
}

p101_fsm_state_t p101_fsm_info_default_bad_change_state_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    char               error_message[P101_FSM_ERROR_MESSAGE_SIZE];
    struct p101_error *target_err;

    P101_TRACE(env);

    (void)snprintf(error_message, sizeof(error_message), "Unknown state transition: %d -> %d", from_state_id, to_state_id);

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
        P101_ERROR_RAISE_USER(target_err, error_message, 1);
    }

    return P101_FSM_EXIT;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void p101_fsm_info_default_bad_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    P101_TRACE(env);
    printf("%s: bad change state from %d to %d\n", fsm_info_name_or_default(info), from_state_id, to_state_id);
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void p101_fsm_info_default_will_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    P101_TRACE(env);
    printf("%s: will change state from %d and %d to <not determined yet>\n", fsm_info_name_or_default(info), from_state_id, to_state_id);
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void p101_fsm_info_default_did_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id)
{
    P101_TRACE(env);
    printf("%s: did change state from %d to %d and going from %d to %d\n", fsm_info_name_or_default(info), from_state_id, to_state_id, to_state_id, next_state_id);
}

#pragma GCC diagnostic pop

void p101_fsm_run(struct p101_fsm_info *info, p101_fsm_state_t *from_state_id, p101_fsm_state_t *to_state_id, void *arg, const struct p101_fsm_transition transitions[], size_t transitions_nbytes)
{
    p101_fsm_state_t from_id;
    p101_fsm_state_t to_id;

    if(info == NULL)
    {
        return;
    }

    P101_TRACE(info->fsm_env);

    if(fsm_info_has_error(info))
    {
        return;
    }

    from_id = info->from_state_id;
    to_id   = info->current_state_id;

    do
    {
        p101_fsm_state_func perform;
        p101_fsm_state_t    next_id;

        // notify moving to
        if(info->will_change_state_notifier)
        {
            info->will_change_state_notifier(info->fsm_env, info->fsm_err, info, from_id, to_id);
        }

        if(fsm_info_has_error(info))
        {
            break;
        }

        if(from_state_id)
        {
            *from_state_id = from_id;
        }

        if(to_state_id)
        {
            *to_state_id = to_id;
        }

        perform = fsm_transition(info->fsm_env, from_id, to_id, transitions, transitions_nbytes);

        if(perform == NULL)
        {
            // notify error
            if(info->bad_change_state_notifier)
            {
                info->bad_change_state_notifier(info->fsm_env, info->fsm_err, info, from_id, to_id);
            }

            if(fsm_info_has_error(info))
            {
                break;
            }

            if(info->bad_change_state_handler)
            {
                next_id = info->bad_change_state_handler(info->fsm_env, info->fsm_err, info, from_id, to_id);
            }
            else
            {
                next_id = P101_FSM_EXIT;
            }

            if(fsm_info_has_error(info) || next_id == P101_FSM_IGNORE)
            {
                break;
            }

            if(next_id == to_id)
            {
                P101_ERROR_RAISE_USER(info->fsm_err, "bad change state handler returned same state", 1);
                next_id = P101_FSM_EXIT;
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
                break;
            }

            // notify moving from
            if(info->did_change_state_notifier)
            {
                info->did_change_state_notifier(info->fsm_env, info->fsm_err, info, info->from_state_id, info->current_state_id, next_id);
            }

            if(fsm_info_has_error(info))
            {
                break;
            }
        }

        to_id = next_id;
    } while(to_id != P101_FSM_EXIT);
}

static int fsm_info_has_error(const struct p101_fsm_info *info)
{
    if(info == NULL)
    {
        return 0;
    }

    return p101_error_has_error(info->sys_err) || p101_error_has_error(info->fsm_err);
}

static const char *fsm_info_name_or_default(const struct p101_fsm_info *info)
{
    if(info == NULL || info->name == NULL)
    {
        return "<unnamed>";
    }

    return info->name;
}

static p101_fsm_state_func fsm_transition(const struct p101_env *env, p101_fsm_state_t from_id, p101_fsm_state_t to_id, const struct p101_fsm_transition transitions[], size_t transitions_nbytes)
{
    size_t elem_size;
    size_t n;

    P101_TRACE(env);

    if(transitions == NULL)
    {
        return NULL;
    }

    elem_size = sizeof transitions[0];

    /* Caller must pass a whole-number multiple of the element size. */
    if((transitions_nbytes % elem_size) != 0)
    {
        return NULL;
    }

    n = transitions_nbytes / elem_size;

    for(size_t i = 0; i < n; ++i)
    {
        if(transitions[i].from_id == from_id && transitions[i].to_id == to_id)
        {
            return transitions[i].perform;
        }
    }
    return NULL;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t p101_fsm_exit_immediately(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return P101_FSM_EXIT;
}

#pragma GCC diagnostic pop
