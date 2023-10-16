/*
 * Copyright 2021-2023 D'Arcy Smith.
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

static p101_fsm_state_func fsm_transition(const struct p101_env *env, p101_fsm_state_t from_id, p101_fsm_state_t to_id, const struct p101_fsm_transition transitions[]);

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
    struct p101_fsm_info *info;

    P101_TRACE(env);
    info = (struct p101_fsm_info *)p101_calloc(env, err, 1, sizeof(struct p101_fsm_info));

    if(p101_error_has_no_error(err))
    {
        info->name = p101_strdup(env, err, name);

        if(p101_error_has_error(err))
        {
            p101_free(env, info);
            info = NULL;
        }
        else
        {
            info->from_state_id    = P101_FSM_INIT;
            info->current_state_id = P101_FSM_USER_START;
            info->sys_env          = env;
            info->sys_err          = err;
            info->fsm_env          = fsm_env;
            info->fsm_err          = fsm_err;

            if(handler == NULL)
            {
                info->bad_change_state_handler = p101_fsm_info_default_bad_change_state_handler;
            }
            else
            {
                info->bad_change_state_handler = handler;
            }
        }
    }

    return info;
}

const char *p101_fsm_info_get_name(const struct p101_env *env, const struct p101_fsm_info *info)
{
    P101_TRACE(env);
    return info->name;
}

void p101_fsm_info_destroy(const struct p101_env *env, struct p101_fsm_info **pinfo)
{
    struct p101_fsm_info *info;

    P101_TRACE(env);
    info = *pinfo;
    p101_free(env, info->name);
    p101_free(env, info);
    *pinfo = NULL;
}

void p101_fsm_info_set_will_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_will_change_state_notifier_func notifier)
{
    P101_TRACE(info->fsm_env);
    info->will_change_state_notifier = notifier;
}

void p101_fsm_info_set_did_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_did_change_state_notifier_func notifier)
{
    P101_TRACE(info->fsm_env);
    info->did_change_state_notifier = notifier;
}

void p101_fsm_info_set_bad_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_notifier_func notifier)
{
    P101_TRACE(info->fsm_env);
    info->bad_change_state_notifier = notifier;
}

void p101_fsm_info_set_bad_change_state_handler(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_handler_func handler)
{
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
    P101_TRACE(info->fsm_env);

    return info->will_change_state_notifier;
}

p101_fsm_info_did_change_state_notifier_func p101_fsm_info_get_did_change_state_notifier(const struct p101_fsm_info *info)
{
    P101_TRACE(info->fsm_env);

    return info->did_change_state_notifier;
}

p101_fsm_info_bad_change_state_notifier_func p101_fsm_info_get_bad_change_state_notifier(const struct p101_fsm_info *info)
{
    P101_TRACE(info->fsm_env);

    return info->bad_change_state_notifier;
}

p101_fsm_info_bad_change_state_handler_func p101_fsm_info_get_bad_change_state_handler(const struct p101_fsm_info *info)
{
    P101_TRACE(info->fsm_env);

    return info->bad_change_state_handler;
}

p101_fsm_state_t p101_fsm_info_default_bad_change_state_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    char  *error_message;
    size_t error_message_size;

    P101_TRACE(env);
    error_message_size = (size_t)snprintf(NULL, 0, "Unknown state transition: %d -> %d ", from_state_id, to_state_id);
    error_message      = (char *)p101_malloc(env, err, error_message_size);
    sprintf(error_message, "Unknown state transition: %d -> %d ", from_state_id, to_state_id);    // NOLINT(cert-err33-c)
    P101_ERROR_RAISE_USER(info->fsm_err, error_message, 1);
    p101_free(env, error_message);

    return P101_FSM_EXIT;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void p101_fsm_info_default_bad_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    P101_TRACE(env);
    printf("%s: bad change state from %d to %d\n", info->name, from_state_id, to_state_id);
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void p101_fsm_info_default_will_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    P101_TRACE(env);
    printf("%s: will change state from %d and %d to <not determined yet>\n", info->name, from_state_id, to_state_id);
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void p101_fsm_info_default_did_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id)
{
    P101_TRACE(env);
    printf("%s: did change state from %d to %d and going from %d to %d\n", info->name, from_state_id, to_state_id, to_state_id, next_state_id);
}

#pragma GCC diagnostic pop

void p101_fsm_run(struct p101_fsm_info *info, p101_fsm_state_t *from_state_id, p101_fsm_state_t *to_state_id, void *arg, const struct p101_fsm_transition transitions[])
{
    p101_fsm_state_t from_id;
    p101_fsm_state_t to_id;

    P101_TRACE(info->fsm_env);
    from_id = info->from_state_id;
    to_id   = info->current_state_id;

    do
    {
        p101_fsm_state_func perform;
        p101_fsm_state_t    next_id;

        // notify moving to
        if(info->will_change_state_notifier)
        {
            info->will_change_state_notifier(info->sys_env, info->sys_err, info, from_id, to_id);
        }

        if(from_state_id)
        {
            *from_state_id = from_id;
        }

        if(to_state_id)
        {
            *to_state_id = to_id;
        }

        perform = fsm_transition(info->fsm_env, from_id, to_id, transitions);

        if(perform == NULL)
        {
            // notify error
            if(info->bad_change_state_notifier)
            {
                info->bad_change_state_notifier(info->sys_env, info->sys_err, info, from_id, to_id);
            }

            next_id = info->bad_change_state_handler(info->fsm_env, info->fsm_err, info, from_id, to_id);
        }
        else
        {
            info->from_state_id    = from_id;
            info->current_state_id = to_id;
            from_id                = to_id;
            next_id                = perform(info->sys_env, info->sys_err, arg);

            // notify moving from
            if(info->did_change_state_notifier)
            {
                info->did_change_state_notifier(info->sys_env, info->sys_err, info, info->from_state_id, info->current_state_id, next_id);
            }
        }

        to_id = next_id;

        // internal FSM error
        if(p101_error_has_error(info->fsm_err))
        {
            // if they are not exiting reset the error
            if(to_id != P101_FSM_EXIT)
            {
                p101_error_reset(info->fsm_err);
            }
        }

        // error in the provided transition functions
        if(p101_error_has_error(info->sys_err))
        {
            // if they are not exiting reset the error
            if(to_id != P101_FSM_EXIT)
            {
                p101_error_reset(info->sys_err);
            }
        }
    } while(to_id != P101_FSM_EXIT);
}

static p101_fsm_state_func fsm_transition(const struct p101_env *env, p101_fsm_state_t from_id, p101_fsm_state_t to_id, const struct p101_fsm_transition transitions[])
{
    const struct p101_fsm_transition *transition;

    P101_TRACE(env);
    transition = &transitions[0];

    while(transition->from_id != P101_FSM_IGNORE)
    {
        if(transition->from_id == from_id && transition->to_id == to_id)
        {
            return transition->perform;
        }

        transition = transitions++;
    }

    return NULL;
}
