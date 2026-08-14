/*
 * Copyright 2021-2026 D'Arcy Smith.
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
#include <p101_c/p101_string.h>
#include <p101_env/wrapper.h>
#include <p101_text/p101_ctype.h>
#include <p101_text/p101_regex.h>
#include <p101_text/p101_stdlib.h>
#include <p101_text/p101_string.h>
#include <p101_text/p101_strings.h>
#include <p101_text/p101_unistd.h>
#include <p101_text/p101_wchar.h>
#include <p101_text/p101_wctype.h>
#include <p101_text/p101_wordexp.h>
#include <p101_transition/transition.h>
#include <stdint.h>

static void                fsm_complete_step(struct p101_fsm_info *info, struct p101_fsm_step_result *result, bool started);
static bool                fsm_has_error(const struct p101_error *app_err, const struct p101_error *fsm_err);
static const char         *fsm_info_name_or_default(const struct p101_fsm_info *info);
static void                fsm_prepare_result(struct p101_fsm_step_result *result);
static p101_fsm_state_func fsm_transition(const struct p101_fsm_info *info, p101_fsm_state_id from_id, p101_fsm_state_id to_id);

struct p101_fsm_transition_map
{
    struct p101_transition_rule *rules;
    p101_fsm_state_func         *performers;
    struct p101_transition_slot *slots;
    struct p101_transition_table table;
};

static int                    fsm_transition_map_create(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_transition transitions[], size_t transition_count, struct p101_fsm_transition_map *map, p101_fsm_state_id *initial_state);
static void                   fsm_transition_map_destroy(const struct p101_env *env, struct p101_fsm_transition_map *map);
static p101_transition_status fsm_transition_map_lookup(const struct p101_fsm_info *info, p101_fsm_state_id from_id, p101_fsm_state_id to_id, struct p101_transition_result *result);

struct p101_fsm_info
{
    const struct p101_env                        *app_env;
    struct p101_error                            *app_err;
    char                                         *name;
    const struct p101_env                        *fsm_env;
    struct p101_error                            *fsm_err;
    struct p101_fsm_transition_map                transitions;
    p101_fsm_state_id                             from_state_id;
    p101_fsm_state_id                             current_state_id;
    size_t                                        sequence;
    size_t                                        redirect_count;
    p101_fsm_info_will_change_state_notifier_func will_change_state_notifier;
    p101_fsm_info_did_change_state_notifier_func  did_change_state_notifier;
    p101_fsm_info_bad_change_state_notifier_func  bad_change_state_notifier;
    p101_fsm_info_bad_change_state_handler_func   bad_change_state_handler;
    p101_fsm_step_observer_func                   step_observer;
    void                                         *step_observer_data;
    bool                                          terminal;
    bool                                          operating;
    bool                                          notifying;
};

struct p101_fsm_info *p101_fsm_info_create(const struct p101_env *env, struct p101_error *err, const char *name, const struct p101_env *fsm_env, struct p101_error *fsm_err, const struct p101_fsm_transition transitions[], size_t transition_count,
                                           p101_fsm_info_bad_change_state_handler_func handler)
{
    const struct p101_env         *target_env;
    struct p101_error             *target_err;
    struct p101_fsm_info          *info;
    struct p101_fsm_transition_map transition_map;
    p101_fsm_state_id              initial_state;
    bool                           primary_error_present;
    bool                           target_error_present;
    int                            map_created;
    void                          *info_storage;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, info, NULL);
    target_env                      = fsm_env == NULL ? env : fsm_env;
    target_err                      = fsm_err == NULL ? err : fsm_err;
    info                            = NULL;
    transition_map.rules            = NULL;
    transition_map.performers       = NULL;
    transition_map.slots            = NULL;
    transition_map.table.rules      = NULL;
    transition_map.table.slots      = NULL;
    transition_map.table.rule_count = 0U;
    transition_map.table.capacity   = 0U;
    initial_state                   = P101_FSM_STATE_NONE;

    primary_error_present = p101_error_has_error(err);
    target_error_present  = p101_error_has_error(target_err);
    if(primary_error_present || target_error_present)
    {
        goto done;
    }

    if(name == NULL)
    {
        P101_ERROR_RAISE_USER(target_err, "FSM name cannot be NULL", P101_FSM_ERROR_INVALID_ARGUMENT);
        goto done;
    }

    map_created = fsm_transition_map_create(target_env, target_err, transitions, transition_count, &transition_map, &initial_state);
    if(!map_created)
    {
        goto done;
    }

    info_storage = p101_calloc(target_env, target_err, 1U, sizeof(*info));
    info         = (struct p101_fsm_info *)info_storage;
    if(info == NULL)
    {
        fsm_transition_map_destroy(target_env, &transition_map);
        goto done;
    }

    info->name = p101_strdup(target_env, target_err, name);
    if(info->name == NULL)
    {
        fsm_transition_map_destroy(target_env, &transition_map);
        p101_free(target_env, info);
        info = NULL;
        goto done;
    }

    info->transitions              = transition_map;
    info->from_state_id            = P101_FSM_INIT;
    info->current_state_id         = initial_state;
    info->app_env                  = env;
    info->app_err                  = err;
    info->fsm_env                  = target_env;
    info->fsm_err                  = target_err;
    info->bad_change_state_handler = handler == NULL ? p101_fsm_info_default_bad_change_state_handler : handler;

done:
    P101_WRAPPER_DONE(env);
    return info;
}

void p101_fsm_info_destroy(const struct p101_env *env, struct p101_error *fsm_err, struct p101_fsm_info **pinfo)
{
    const struct p101_env *free_env;
    struct p101_fsm_info  *info;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN_VOID(env, fsm_err);
    if(pinfo == NULL || *pinfo == NULL)
    {
        goto done;
    }

    info = *pinfo;
    if(info->operating || info->notifying)
    {
        P101_ERROR_RAISE_USER(fsm_err, "Cannot destroy an FSM during a state operation", P101_FSM_ERROR_REENTRANT_OPERATION);
        goto done;
    }

    free_env = info->fsm_env == NULL ? env : info->fsm_env;
    fsm_transition_map_destroy(free_env, &info->transitions);
    p101_free(free_env, info->name);
    p101_free(free_env, info);
    *pinfo = NULL;

done:
    P101_WRAPPER_DONE(env);
}

const char *p101_fsm_info_get_name(const struct p101_env *env, const struct p101_fsm_info *info)
{
    const char *name;

    P101_TRACE(env);
    name = info == NULL ? NULL : info->name;
    P101_TRACE_EXIT(env);
    return name;
}

p101_fsm_state_id p101_fsm_info_get_current_state(const struct p101_env *env, const struct p101_fsm_info *info)
{
    p101_fsm_state_id state_id;

    P101_TRACE(env);
    state_id = info == NULL ? P101_FSM_STATE_NONE : info->current_state_id;
    P101_TRACE_EXIT(env);
    return state_id;
}

size_t p101_fsm_info_get_step_sequence(const struct p101_env *env, const struct p101_fsm_info *info)
{
    size_t sequence;

    P101_TRACE(env);
    sequence = info == NULL ? 0U : info->sequence;
    P101_TRACE_EXIT(env);
    return sequence;
}

bool p101_fsm_info_is_terminal(const struct p101_env *env, const struct p101_fsm_info *info)
{
    bool terminal;

    P101_TRACE(env);
    terminal = false;
    if(info != NULL)
    {
        terminal = info->terminal;
    }
    P101_TRACE_EXIT(env);
    return terminal;
}

void p101_fsm_info_set_will_change_state_notifier(const struct p101_env *env, struct p101_fsm_info *info, p101_fsm_info_will_change_state_notifier_func notifier)
{
    P101_TRACE(env);
    if(info != NULL)
    {
        info->will_change_state_notifier = notifier;
    }
    P101_TRACE_EXIT(env);
}

void p101_fsm_info_set_did_change_state_notifier(const struct p101_env *env, struct p101_fsm_info *info, p101_fsm_info_did_change_state_notifier_func notifier)
{
    P101_TRACE(env);
    if(info != NULL)
    {
        info->did_change_state_notifier = notifier;
    }
    P101_TRACE_EXIT(env);
}

void p101_fsm_info_set_bad_change_state_notifier(const struct p101_env *env, struct p101_fsm_info *info, p101_fsm_info_bad_change_state_notifier_func notifier)
{
    P101_TRACE(env);
    if(info != NULL)
    {
        info->bad_change_state_notifier = notifier;
    }
    P101_TRACE_EXIT(env);
}

void p101_fsm_info_set_bad_change_state_handler(const struct p101_env *env, struct p101_fsm_info *info, p101_fsm_info_bad_change_state_handler_func handler)
{
    P101_TRACE(env);
    if(info != NULL)
    {
        info->bad_change_state_handler = handler == NULL ? p101_fsm_info_default_bad_change_state_handler : handler;
    }
    P101_TRACE_EXIT(env);
}

// cppcheck-suppress funcArgNamesDifferentUnnamed
void p101_fsm_info_set_step_observer(const struct p101_env *env, struct p101_fsm_info *info, p101_fsm_step_observer_func observer, void *user_data)
{
    P101_TRACE(env);
    if(info != NULL)
    {
        info->step_observer      = observer;
        info->step_observer_data = user_data;
    }
    P101_TRACE_EXIT(env);
}

p101_fsm_info_will_change_state_notifier_func p101_fsm_info_get_will_change_state_notifier(const struct p101_env *env, const struct p101_fsm_info *info)
{
    p101_fsm_info_will_change_state_notifier_func notifier;

    P101_TRACE(env);
    notifier = info == NULL ? NULL : info->will_change_state_notifier;
    P101_TRACE_EXIT(env);
    return notifier;
}

p101_fsm_info_did_change_state_notifier_func p101_fsm_info_get_did_change_state_notifier(const struct p101_env *env, const struct p101_fsm_info *info)
{
    p101_fsm_info_did_change_state_notifier_func notifier;

    P101_TRACE(env);
    notifier = info == NULL ? NULL : info->did_change_state_notifier;
    P101_TRACE_EXIT(env);
    return notifier;
}

p101_fsm_info_bad_change_state_notifier_func p101_fsm_info_get_bad_change_state_notifier(const struct p101_env *env, const struct p101_fsm_info *info)
{
    p101_fsm_info_bad_change_state_notifier_func notifier;

    P101_TRACE(env);
    notifier = info == NULL ? NULL : info->bad_change_state_notifier;
    P101_TRACE_EXIT(env);
    return notifier;
}

p101_fsm_info_bad_change_state_handler_func p101_fsm_info_get_bad_change_state_handler(const struct p101_env *env, const struct p101_fsm_info *info)
{
    p101_fsm_info_bad_change_state_handler_func handler;

    P101_TRACE(env);
    handler = info == NULL ? NULL : info->bad_change_state_handler;
    P101_TRACE_EXIT(env);
    return handler;
}

void p101_fsm_info_default_bad_change_state_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_id from_state_id, p101_fsm_state_id to_state_id, struct p101_fsm_effect_sink *sink,
                                                    struct p101_fsm_decision *decision)
{
    struct p101_error *target_err;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN_VOID(env, err);
    (void)sink;
    target_err = err == NULL && info != NULL ? info->fsm_err : err;
    p101_fsm_decide_exit(decision);
    if(target_err != NULL)
    {
        P101_ERROR_RAISE_USER_PRINTF(target_err, P101_FSM_ERROR_UNKNOWN_TRANSITION, "Unknown FSM state transition: %d -> %d", from_state_id, to_state_id);
    }
    P101_WRAPPER_DONE(env);
}

void p101_fsm_info_default_bad_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_id from_state_id, p101_fsm_state_id to_state_id)
{
    const char *name;
    int         written;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN_VOID(env, err);
    name    = fsm_info_name_or_default(info);
    written = p101_printf(env, err, "%s: refused state transition %d -> %d\n", name, from_state_id, to_state_id);
    (void)written;
    P101_WRAPPER_DONE(env);
}

void p101_fsm_info_default_will_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_id from_state_id, p101_fsm_state_id to_state_id)
{
    const char *name;
    int         written;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN_VOID(env, err);
    name    = fsm_info_name_or_default(info);
    written = p101_printf(env, err, "%s: will attempt state transition %d -> %d\n", name, from_state_id, to_state_id);
    (void)written;
    P101_WRAPPER_DONE(env);
}

void p101_fsm_info_default_did_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_id from_state_id, p101_fsm_state_id to_state_id, p101_fsm_state_id next_state_id)
{
    const char *name;
    int         written;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN_VOID(env, err);
    name    = fsm_info_name_or_default(info);
    written = p101_printf(env, err, "%s: completed state transition %d -> %d; next state %d\n", name, from_state_id, to_state_id, next_state_id);
    (void)written;
    P101_WRAPPER_DONE(env);
}

void p101_fsm_decide_transition(struct p101_fsm_decision *decision, p101_fsm_state_id next_state)
{
    if(decision != NULL)
    {
        decision->kind       = P101_FSM_DECISION_TRANSITION;
        decision->next_state = next_state;
    }
}

void p101_fsm_decide_pause(struct p101_fsm_decision *decision)
{
    if(decision != NULL)
    {
        decision->kind       = P101_FSM_DECISION_PAUSE;
        decision->next_state = P101_FSM_STATE_NONE;
    }
}

void p101_fsm_decide_exit(struct p101_fsm_decision *decision)
{
    if(decision != NULL)
    {
        decision->kind       = P101_FSM_DECISION_EXIT;
        decision->next_state = P101_FSM_STATE_NONE;
    }
}

void p101_fsm_emit_effect(const struct p101_env *env, struct p101_error *err, struct p101_fsm_effect_sink *sink, const char *kind, const void *data, size_t data_size)
{
    struct p101_fsm_effect effect;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN_VOID(env, err);
    if(sink == NULL || sink->handle == NULL)
    {
        goto done;
    }
    if(kind == NULL || (data == NULL && data_size != 0U))
    {
        P101_ERROR_RAISE_USER(err, "Invalid FSM effect", P101_FSM_ERROR_EFFECT);
        goto done;
    }

    effect.kind      = kind;
    effect.data      = data;
    effect.data_size = data_size;
    sink->handle(env, err, sink->context, &effect);

done:
    P101_WRAPPER_DONE(env);
}

p101_fsm_step_status p101_fsm_step(struct p101_fsm_info *info, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_step_result *result)
{
    p101_fsm_step_status     p101_single_result_;
    const struct p101_env   *env;
    struct p101_error       *err;
    p101_fsm_state_func      perform;
    struct p101_fsm_decision decision;
    bool                     started;
    bool                     has_error;
    bool                     app_effect_capacity_error;
    bool                     fsm_effect_capacity_error;

    fsm_prepare_result(result);
    if(info == NULL)
    {
        p101_single_result_ = P101_FSM_STEP_ERROR;
        goto p101_single_exit_;
    }

    env     = info->fsm_env;
    err     = info->fsm_err;
    started = false;
    P101_TRACE(env);
    if(result == NULL)
    {
        P101_ERROR_RAISE_USER(err, "FSM step result cannot be NULL", P101_FSM_ERROR_INVALID_ARGUMENT);
        P101_TRACE_EXIT(env);
        p101_single_result_ = P101_FSM_STEP_ERROR;
        goto p101_single_exit_;
    }

    if(info->sequence == SIZE_MAX)
    {
        result->status   = P101_FSM_STEP_REFUSED;
        result->sequence = SIZE_MAX;
        result->refusal  = P101_FSM_REFUSAL_SEQUENCE_EXHAUSTED;
        P101_ERROR_RAISE_USER(err, "FSM step sequence is exhausted", P101_FSM_ERROR_SEQUENCE_EXHAUSTED);
        goto done;
    }
    info->sequence++;
    result->sequence        = info->sequence;
    result->from_state      = info->from_state_id;
    result->attempted_state = info->current_state_id;
    result->next_state      = info->current_state_id;

    has_error = fsm_has_error(info->app_err, info->fsm_err);
    if(has_error)
    {
        result->status = P101_FSM_STEP_ERROR;
        goto done;
    }
    if(info->operating || info->notifying)
    {
        result->status  = P101_FSM_STEP_REFUSED;
        result->refusal = P101_FSM_REFUSAL_REENTRANT_INVOCATION;
        P101_ERROR_RAISE_USER(err, "Cannot operate an FSM recursively", P101_FSM_ERROR_REENTRANT_OPERATION);
        goto done;
    }
    if(info->terminal)
    {
        result->status     = P101_FSM_STEP_EXITED;
        result->next_state = P101_FSM_STATE_NONE;
        result->refusal    = P101_FSM_REFUSAL_TERMINAL_MACHINE;
        goto done;
    }

    info->operating = true;
    started         = true;
    perform         = fsm_transition(info, info->from_state_id, info->current_state_id);
    if(perform == NULL)
    {
        if(info->bad_change_state_notifier != NULL)
        {
            info->bad_change_state_notifier(info->fsm_env, info->fsm_err, info, info->from_state_id, info->current_state_id);
        }
        has_error = fsm_has_error(info->app_err, info->fsm_err);
        if(has_error)
        {
            result->status  = P101_FSM_STEP_ERROR;
            result->refusal = P101_FSM_REFUSAL_UNKNOWN_TRANSITION;
            goto done;
        }

        decision.kind       = P101_FSM_DECISION_INVALID;
        decision.next_state = P101_FSM_STATE_NONE;
        info->bad_change_state_handler(info->fsm_env, info->fsm_err, info, info->from_state_id, info->current_state_id, sink, &decision);
        has_error = fsm_has_error(info->app_err, info->fsm_err);
        if(has_error)
        {
            result->status  = P101_FSM_STEP_ERROR;
            result->refusal = P101_FSM_REFUSAL_UNKNOWN_TRANSITION;
            goto done;
        }

        result->status  = P101_FSM_STEP_REFUSED;
        result->refusal = P101_FSM_REFUSAL_UNKNOWN_TRANSITION;
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
        switch(decision.kind)    // GCOVR_EXCL_BR_LINE: default protects against an invalid enum representation.
        {
            case P101_FSM_DECISION_TRANSITION:
                if(decision.next_state < P101_FSM_USER_START)
                {
                    result->refusal = P101_FSM_REFUSAL_INVALID_HANDLER_DECISION;
                    P101_ERROR_RAISE_USER(err, "Bad-transition handler selected an invalid state", P101_FSM_ERROR_INVALID_DECISION);
                    break;
                }
                if(decision.next_state == info->current_state_id || info->redirect_count >= info->transitions.table.rule_count)
                {
                    result->refusal = P101_FSM_REFUSAL_REDIRECT_CYCLE;
                    P101_ERROR_RAISE_USER(err, "Bad-transition handler entered a redirect cycle", P101_FSM_ERROR_HANDLER_LOOP);
                    break;
                }
                info->redirect_count++;
                info->current_state_id = decision.next_state;
                result->next_state     = decision.next_state;
                break;
            case P101_FSM_DECISION_PAUSE:
                break;
            case P101_FSM_DECISION_EXIT:
                info->terminal     = true;
                result->status     = P101_FSM_STEP_EXITED;
                result->next_state = P101_FSM_STATE_NONE;
                break;
            case P101_FSM_DECISION_INVALID:
            default:
                result->refusal = P101_FSM_REFUSAL_INVALID_HANDLER_DECISION;
                P101_ERROR_RAISE_USER(err, "Bad-transition handler did not produce a valid decision", P101_FSM_ERROR_INVALID_DECISION);
                break;
        }
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
        goto done;
    }

    if(info->will_change_state_notifier != NULL)
    {
        info->will_change_state_notifier(info->fsm_env, info->fsm_err, info, info->from_state_id, info->current_state_id);
    }
    has_error = fsm_has_error(info->app_err, info->fsm_err);
    if(has_error)
    {
        result->status = P101_FSM_STEP_ERROR;
        goto done;
    }

    decision.kind       = P101_FSM_DECISION_INVALID;
    decision.next_state = P101_FSM_STATE_NONE;
    perform(info->app_env, info->app_err, arg, sink, &decision);
    has_error = fsm_has_error(info->app_err, info->fsm_err);
    if(has_error)
    {
        app_effect_capacity_error = p101_error_is_error(info->app_err, P101_ERROR_USER, P101_FSM_ERROR_EFFECT_CAPACITY);
        fsm_effect_capacity_error = p101_error_is_error(info->fsm_err, P101_ERROR_USER, P101_FSM_ERROR_EFFECT_CAPACITY);
        if(app_effect_capacity_error || fsm_effect_capacity_error)
        {
            result->status  = P101_FSM_STEP_REFUSED;
            result->refusal = P101_FSM_REFUSAL_EFFECT_CAPACITY;
        }
        else
        {
            result->status = P101_FSM_STEP_ERROR;
        }
        goto done;
    }

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch(decision.kind)    // GCOVR_EXCL_BR_LINE: default protects against an invalid enum representation.
    {
        case P101_FSM_DECISION_TRANSITION:
            if(decision.next_state < P101_FSM_USER_START)
            {
                result->status  = P101_FSM_STEP_REFUSED;
                result->refusal = P101_FSM_REFUSAL_INVALID_CALLBACK_DECISION;
                P101_ERROR_RAISE_USER(info->app_err, "FSM callback selected an invalid state", P101_FSM_ERROR_INVALID_DECISION);
                break;
            }
            if(info->did_change_state_notifier != NULL)
            {
                info->did_change_state_notifier(info->fsm_env, info->fsm_err, info, info->from_state_id, info->current_state_id, decision.next_state);
            }
            has_error = fsm_has_error(info->app_err, info->fsm_err);
            if(has_error)
            {
                result->status = P101_FSM_STEP_ERROR;
                break;
            }
            info->from_state_id    = info->current_state_id;
            info->current_state_id = decision.next_state;
            info->redirect_count   = 0U;
            result->status         = P101_FSM_STEP_TRANSITIONED;
            result->next_state     = decision.next_state;
            break;
        case P101_FSM_DECISION_PAUSE:
            result->status = P101_FSM_STEP_PAUSED;
            break;
        case P101_FSM_DECISION_EXIT:
            if(info->did_change_state_notifier != NULL)
            {
                info->did_change_state_notifier(info->fsm_env, info->fsm_err, info, info->from_state_id, info->current_state_id, P101_FSM_STATE_NONE);
            }
            has_error = fsm_has_error(info->app_err, info->fsm_err);
            if(has_error)
            {
                result->status = P101_FSM_STEP_ERROR;
                break;
            }
            info->from_state_id  = info->current_state_id;
            info->redirect_count = 0U;
            info->terminal       = true;
            result->status       = P101_FSM_STEP_EXITED;
            result->next_state   = P101_FSM_STATE_NONE;
            break;
        case P101_FSM_DECISION_INVALID:
        default:
            result->status  = P101_FSM_STEP_REFUSED;
            result->refusal = P101_FSM_REFUSAL_INVALID_CALLBACK_DECISION;
            P101_ERROR_RAISE_USER(info->app_err, "FSM callback did not produce a valid decision", P101_FSM_ERROR_INVALID_DECISION);
            break;
    }
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

done:
    fsm_complete_step(info, result, started);
    P101_TRACE_EXIT(env);
    p101_single_result_ = result->status;
    goto p101_single_exit_;

p101_single_exit_:
    return p101_single_result_;
}

p101_fsm_run_result p101_fsm_run(struct p101_fsm_info *info, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_step_result *last_result)
{
    const struct p101_env      *env;
    struct p101_fsm_step_result current;
    p101_fsm_run_result         run_result;

    env        = info == NULL ? NULL : info->fsm_env;
    run_result = P101_FSM_RUN_ERROR;
    P101_TRACE(env);
    for(;;)    // GCOVR_EXCL_BR_LINE: the loop exits through the typed step outcomes below.
    {
        p101_fsm_step_status status;

        status = p101_fsm_step(info, arg, sink, &current);
        if(last_result != NULL)
        {
            *last_result = current;
        }
        if(info == NULL)
        {
            break;
        }

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
        // GCOVR_EXCL_BR_START: p101_fsm_step returns only declared status values.
        switch(status)
        {
            case P101_FSM_STEP_TRANSITIONED:
                break;
            case P101_FSM_STEP_PAUSED:
                run_result = P101_FSM_RUN_PAUSED;
                goto done;
            case P101_FSM_STEP_EXITED:
                run_result = P101_FSM_RUN_EXITED;
                goto done;
            case P101_FSM_STEP_REFUSED:
                if(current.refusal != P101_FSM_REFUSAL_UNKNOWN_TRANSITION || current.next_state == current.attempted_state)
                {
                    run_result = P101_FSM_RUN_REFUSED;
                    goto done;
                }
                break;
            case P101_FSM_STEP_ERROR:
            default:
                goto done;
        }
            // GCOVR_EXCL_BR_STOP
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
    }

done:
    P101_TRACE_EXIT(env);
    return run_result;
}

void p101_fsm_exit_immediately(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN_VOID(env, err);
    (void)arg;
    (void)sink;
    p101_fsm_decide_exit(decision);
    P101_WRAPPER_DONE(env);
}

static void fsm_complete_step(struct p101_fsm_info *info, struct p101_fsm_step_result *result, bool started)
{
    if(started)
    {
        info->operating = false;
    }
    if(info->step_observer != NULL && !info->notifying)
    {
        info->notifying = true;
        info->step_observer(info->fsm_env, info, result, info->step_observer_data);
        info->notifying = false;
    }
}

static bool fsm_has_error(const struct p101_error *app_err, const struct p101_error *fsm_err)
{
    bool result;
    bool app_error_present;
    bool fsm_error_present;

    app_error_present = p101_error_has_error(app_err);
    fsm_error_present = p101_error_has_error(fsm_err);
    result            = false;
    if(app_error_present || fsm_error_present)
    {
        result = true;
    }

    return result;
}

static const char *fsm_info_name_or_default(const struct p101_fsm_info *info)
{
    return info == NULL ? "<unnamed>" : info->name;
}

static void fsm_prepare_result(struct p101_fsm_step_result *result)
{
    if(result != NULL)
    {
        result->status          = P101_FSM_STEP_ERROR;
        result->sequence        = 0U;
        result->from_state      = P101_FSM_STATE_NONE;
        result->attempted_state = P101_FSM_STATE_NONE;
        result->next_state      = P101_FSM_STATE_NONE;
        result->refusal         = P101_FSM_REFUSAL_NONE;
    }
}

static p101_fsm_state_func fsm_transition(const struct p101_fsm_info *info, p101_fsm_state_id from_id, p101_fsm_state_id to_id)
{
    p101_fsm_state_func           p101_single_result_;
    struct p101_transition_result result;
    p101_transition_status        status;

    p101_single_result_ = NULL;
    status              = fsm_transition_map_lookup(info, from_id, to_id, &result);
    if(status == P101_TRANSITION_OK && result.rule_index < info->transitions.table.rule_count)
    {
        p101_single_result_ = info->transitions.performers[result.rule_index];
    }

    return p101_single_result_;
}

static int fsm_transition_map_create(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_transition transitions[], size_t transition_count, struct p101_fsm_transition_map *map, p101_fsm_state_id *initial_state)
{
    int                    p101_single_result_;
    size_t                 capacity;
    size_t                 initial_count;
    void                  *rule_storage;
    void                  *performer_storage;
    void                  *slot_storage;
    p101_transition_status transition_status;

    P101_TRACE(env);
    p101_single_result_ = 0;
    if(transitions == NULL || transition_count == 0U || transition_count > SIZE_MAX / sizeof(*transitions) || map == NULL || initial_state == NULL)
    {
        P101_ERROR_RAISE_USER(err, "FSM transition table cannot be empty", P101_FSM_ERROR_INVALID_TRANSITION_TABLE);
        goto p101_single_exit_;
    }

    capacity = p101_transition_table_capacity(transition_count);
    if(capacity == 0U || capacity > SIZE_MAX / sizeof(*map->slots) || transition_count > SIZE_MAX / sizeof(*map->rules) || transition_count > SIZE_MAX / sizeof(*map->performers))
    {
        P101_ERROR_RAISE_USER(err, "FSM transition table is too large", P101_FSM_ERROR_INVALID_TRANSITION_TABLE);
        goto p101_single_exit_;
    }

    rule_storage = p101_calloc(env, err, transition_count, sizeof(*map->rules));
    map->rules   = (struct p101_transition_rule *)rule_storage;
    if(map->rules == NULL)
    {
        goto p101_single_exit_;
    }
    performer_storage = p101_calloc(env, err, transition_count, sizeof(*map->performers));
    map->performers   = (p101_fsm_state_func *)performer_storage;
    if(map->performers == NULL)
    {
        goto invalid;
    }
    slot_storage = p101_calloc(env, err, capacity, sizeof(*map->slots));
    map->slots   = (struct p101_transition_slot *)slot_storage;
    if(map->slots == NULL)
    {
        goto invalid;
    }

    initial_count = 0U;
    for(size_t i = 0U; i < transition_count; ++i)
    {
        if((transitions[i].from_id != P101_FSM_INIT && transitions[i].from_id < P101_FSM_USER_START) || transitions[i].to_id < P101_FSM_USER_START || transitions[i].perform == NULL)
        {
            P101_ERROR_RAISE_USER_PRINTF(err, P101_FSM_ERROR_INVALID_TRANSITION_TABLE, "Invalid FSM transition table entry at index %zu", i);
            goto invalid;
        }
        if(transitions[i].from_id == P101_FSM_INIT)
        {
            initial_count++;
            *initial_state = transitions[i].to_id;
        }
        map->rules[i].state      = transitions[i].from_id;
        map->rules[i].event      = transitions[i].to_id;
        map->rules[i].next_state = transitions[i].to_id;
        map->rules[i].value      = i;
        map->performers[i]       = transitions[i].perform;
    }

    if(initial_count != 1U)
    {
        P101_ERROR_RAISE_USER(err, "FSM transition table must contain exactly one initial transition", P101_FSM_ERROR_INVALID_TRANSITION_TABLE);
        goto invalid;
    }

    transition_status = p101_transition_table_initialize(&map->table, map->rules, transition_count, map->slots, capacity);
    if(transition_status != P101_TRANSITION_OK)
    {
        P101_ERROR_RAISE_USER(err, "FSM transition table contains a duplicate transition", P101_FSM_ERROR_INVALID_TRANSITION_TABLE);
        goto invalid;
    }
    p101_single_result_ = 1;
    goto p101_single_exit_;

invalid:
    fsm_transition_map_destroy(env, map);

p101_single_exit_:
    P101_TRACE_EXIT(env);
    return p101_single_result_;
}

static void fsm_transition_map_destroy(const struct p101_env *env, struct p101_fsm_transition_map *map)
{
    if(map != NULL)
    {
        p101_free(env, map->slots);
        p101_free(env, (void *)map->performers);
        p101_free(env, map->rules);
        map->slots            = NULL;
        map->performers       = NULL;
        map->rules            = NULL;
        map->table.rules      = NULL;
        map->table.slots      = NULL;
        map->table.rule_count = 0U;
        map->table.capacity   = 0U;
    }
}

static p101_transition_status fsm_transition_map_lookup(const struct p101_fsm_info *info, p101_fsm_state_id from_id, p101_fsm_state_id to_id, struct p101_transition_result *result)
{
    p101_transition_status p101_single_result_;

    p101_single_result_ = P101_TRANSITION_INVALID_ARGUMENT;
    if(info != NULL)
    {
        p101_single_result_ = p101_transition_table_find(&info->transitions.table, from_id, to_id, result);
    }

    return p101_single_result_;
}

#ifdef P101_FSM_TESTING
void p101_fsm_test_set_step_sequence(struct p101_fsm_info *info, size_t sequence)
{
    if(info != NULL)
    {
        info->sequence = sequence;
    }
}

size_t p101_fsm_test_transition_probe_count(const struct p101_fsm_info *info, p101_fsm_state_id from_id, p101_fsm_state_id to_id)
{
    size_t                        p101_single_result_;
    struct p101_transition_result result;
    p101_transition_status        status;

    p101_single_result_ = 0U;
    if(info == NULL)
    {
        goto p101_single_exit_;
    }
    status = fsm_transition_map_lookup(info, from_id, to_id, &result);
    (void)status;
    p101_single_result_ = result.probes;

p101_single_exit_:
    return p101_single_result_;
}
#endif
