/*
 * Copyright 2021-2021 D'Arcy Smith.
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
#include <p101_c/p101_string.h>
#include <stdio.h>

static p101_fsm_state_func fsm_transition(const struct p101_env *env, int from_id, int to_id, const struct p101_fsm_transition transitions[]);

struct p101_fsm_info
{
    char  *name;
    size_t name_length;
    int    from_state_id;
    int    current_state_id;
    void (*will_change_state)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, int from_state_id, int to_state_id);
    void (*did_change_state)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, int from_state_id, int to_state_id, int next_id);
    void (*bad_change_state)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, int from_state_id, int to_state_id);
};

struct p101_fsm_info *p101_fsm_info_create(const struct p101_env *env, struct p101_error *err, const char *name)
{
    struct p101_fsm_info *info;

    P101_TRACE(env);
    info = (struct p101_fsm_info *)p101_calloc(env, err, 1, sizeof(struct p101_fsm_info));

    if(p101_error_has_no_error(err))
    {
        info->from_state_id    = P101_FSM_INIT;
        info->current_state_id = P101_FSM_USER_START;
        info->name_length      = p101_strlen(env, name) + 1;
        info->name             = (char *)p101_malloc(env, err, info->name_length);

        if(p101_error_has_no_error(err))
        {
            p101_strncpy(env, info->name, name, info->name_length);
        }
        else
        {
            p101_free(env, info);
            info = NULL;
        }
    }

    return info;
}

const char *p101_fsm_info_get_name(const struct p101_fsm_info *info)
{
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

void p101_fsm_info_set_will_change_state(struct p101_fsm_info *info,
                                         void (*notifier)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, int from_state_id, int to_state_id))
{
    info->will_change_state = notifier;
}

void p101_fsm_info_set_did_change_state(struct p101_fsm_info *info, void (*notifier)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info,
                                                                                     int from_state_id, int to_state_id, int next_id))
{
    info->did_change_state = notifier;
}

void p101_fsm_info_set_bad_change_state(struct p101_fsm_info *info,
                                        void (*notifier)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, int from_state_id, int to_state_id))
{
    info->bad_change_state = notifier;
}

int p101_fsm_run(const struct p101_env *env, struct p101_error *err, struct p101_fsm_info *info, int *from_state_id, int *to_state_id, void *arg,
                 const struct p101_fsm_transition transitions[])
{
    int from_id;
    int to_id;

    P101_TRACE(env);

    from_id = info->from_state_id;
    to_id   = info->current_state_id;

    do
    {
        p101_fsm_state_func perform;
        int                 next_id;

        // notify moving to
        if(info->will_change_state)
        {
            info->will_change_state(env, err, info, from_id, to_id);
        }

        perform = fsm_transition(env, from_id, to_id, transitions);

        if(perform == NULL)
        {
            char  *error_message;
            size_t error_message_size;

            if(from_state_id)
            {
                *from_state_id = from_id;
            }

            if(to_state_id)
            {
                *to_state_id = to_id;
            }

            // notify error
            if(info->bad_change_state)
            {
                info->bad_change_state(env, err, info, from_id, to_id);
            }

            error_message_size = (size_t)snprintf(NULL, 0, "Unknown state transition: %d -> %d ", from_id, to_id);
            error_message      = (char *)p101_malloc(env, err, error_message_size);
            sprintf(error_message, "Unknown state transition: %d -> %d ", from_id, to_id);    // NOLINT(cert-err33-c)
            P101_ERROR_RAISE_USER(err, error_message, 1);
            p101_free(env, error_message);

            return -1;
        }

        info->from_state_id    = from_id;
        info->current_state_id = to_id;
        from_id                = to_id;
        next_id                = perform(env, err, arg);

        // notify moving from
        if(info->did_change_state)
        {
            info->did_change_state(env, err, info, info->from_state_id, info->current_state_id, next_id);
        }

        to_id = next_id;

        if(p101_error_has_error(err))
        {
            // sometimes states do want to pass errors along... maybe?
            // how do we sort out an internal error and an error from a state?
            // we had an issue that we can't cope with
            // break;
        }
    } while(to_id != P101_FSM_EXIT);

    // commenting this out will give us the last non-exit transition, probably more useful
    if(from_state_id)
    {
        *from_state_id = from_id;
    }

    if(to_state_id)
    {
        *to_state_id = to_id;
    }

    return 0;
}

static p101_fsm_state_func fsm_transition(const struct p101_env *env, int from_id, int to_id, const struct p101_fsm_transition transitions[])
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
