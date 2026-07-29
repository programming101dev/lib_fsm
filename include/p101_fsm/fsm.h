#ifndef LIBP101_FSM_FSM_H
#define LIBP101_FSM_FSM_H

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

#include <p101_env/env.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct p101_fsm_info;
    struct p101_fsm_effect_sink;

    typedef enum
    {
        P101_FSM_STATE_NONE = -1,
        P101_FSM_INIT       = 0,
        P101_FSM_USER_START = 1,
    } p101_fsm_state;

    typedef int p101_fsm_state_t;

    typedef enum
    {
        P101_FSM_DECISION_INVALID = 0,
        P101_FSM_DECISION_TRANSITION,
        P101_FSM_DECISION_PAUSE,
        P101_FSM_DECISION_EXIT,
    } p101_fsm_decision_kind;

    struct p101_fsm_decision
    {
        p101_fsm_decision_kind kind;
        p101_fsm_state_t       next_state;
    };

    typedef enum
    {
        P101_FSM_REFUSAL_NONE = 0,
        P101_FSM_REFUSAL_UNKNOWN_TRANSITION,
        P101_FSM_REFUSAL_INVALID_CALLBACK_DECISION,
        P101_FSM_REFUSAL_INVALID_HANDLER_DECISION,
        P101_FSM_REFUSAL_REDIRECT_CYCLE,
        P101_FSM_REFUSAL_TERMINAL_MACHINE,
        P101_FSM_REFUSAL_REENTRANT_INVOCATION,
    } p101_fsm_refusal;

    typedef enum
    {
        P101_FSM_STEP_TRANSITIONED = 0,
        P101_FSM_STEP_PAUSED,
        P101_FSM_STEP_EXITED,
        P101_FSM_STEP_REFUSED,
        P101_FSM_STEP_ERROR,
    } p101_fsm_step_status;

    struct p101_fsm_step_result
    {
        p101_fsm_step_status status;
        size_t               sequence;
        p101_fsm_state_t     from_state;
        p101_fsm_state_t     attempted_state;
        p101_fsm_state_t     next_state;
        p101_fsm_refusal     refusal;
    };

    typedef enum
    {
        P101_FSM_RUN_ERROR   = -1,
        P101_FSM_RUN_PAUSED  = 0,
        P101_FSM_RUN_EXITED  = 1,
        P101_FSM_RUN_REFUSED = 2,
    } p101_fsm_run_result;

    struct p101_fsm_effect
    {
        const char *kind;
        const void *data;
        size_t      data_size;
    };

    typedef void (*p101_fsm_effect_handler_func)(const struct p101_env *env, struct p101_error *err, void *context, const struct p101_fsm_effect *effect);

    struct p101_fsm_effect_sink
    {
        p101_fsm_effect_handler_func handle;
        void                        *context;
    };

    typedef void (*p101_fsm_state_func)(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision);
    typedef void (*p101_fsm_info_will_change_state_notifier_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    typedef void (*p101_fsm_info_did_change_state_notifier_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id);
    typedef void (*p101_fsm_info_bad_change_state_notifier_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    typedef void (*p101_fsm_info_bad_change_state_handler_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, struct p101_fsm_effect_sink *sink,
                                                                struct p101_fsm_decision *decision);
    typedef void (*p101_fsm_step_observer_func)(const struct p101_env *env, const struct p101_fsm_info *info, const struct p101_fsm_step_result *result, void *user_data);

    struct p101_fsm_transition
    {
        p101_fsm_state_t    from_id;
        p101_fsm_state_t    to_id;
        p101_fsm_state_func perform;
    };

    /*
     * The machine copies and owns the validated transition table. env/err are
     * borrowed for application callbacks; fsm_env/fsm_err are borrowed for
     * FSM allocation, validation, notification, and policy. All borrowed
     * objects must outlive the machine. Pass the FSM error object to destroy
     * so a recursive destruction refusal has an explicit error destination.
     *
     * Exactly one transition must originate at P101_FSM_INIT. All executable
     * states are at least P101_FSM_USER_START. A machine is neither thread-safe
     * nor reentrant.
     */
    struct p101_fsm_info *p101_fsm_info_create(const struct p101_env *env, struct p101_error *err, const char *name, const struct p101_env *fsm_env, struct p101_error *fsm_err, const struct p101_fsm_transition transitions[], size_t transition_count,
                                               p101_fsm_info_bad_change_state_handler_func handler) P101_ATTR_MALLOC P101_ATTR_WARN_UNUSED_RESULT;
    void                  p101_fsm_info_destroy(const struct p101_env *env, struct p101_error *fsm_err, struct p101_fsm_info **pinfo);
    const char           *p101_fsm_info_get_name(const struct p101_env *env, const struct p101_fsm_info *info);
    p101_fsm_state_t      p101_fsm_info_get_current_state(const struct p101_fsm_info *info);
    size_t                p101_fsm_info_get_step_sequence(const struct p101_fsm_info *info);
    bool                  p101_fsm_info_is_terminal(const struct p101_fsm_info *info);

    void                                          p101_fsm_info_set_will_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_will_change_state_notifier_func notifier);
    void                                          p101_fsm_info_set_did_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_did_change_state_notifier_func notifier);
    void                                          p101_fsm_info_set_bad_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_notifier_func notifier);
    void                                          p101_fsm_info_set_bad_change_state_handler(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_handler_func handler);
    void                                          p101_fsm_info_set_step_observer(struct p101_fsm_info *info, p101_fsm_step_observer_func observer, void *user_data);
    p101_fsm_info_will_change_state_notifier_func p101_fsm_info_get_will_change_state_notifier(const struct p101_fsm_info *info);
    p101_fsm_info_did_change_state_notifier_func  p101_fsm_info_get_did_change_state_notifier(const struct p101_fsm_info *info);
    p101_fsm_info_bad_change_state_notifier_func  p101_fsm_info_get_bad_change_state_notifier(const struct p101_fsm_info *info);
    p101_fsm_info_bad_change_state_handler_func   p101_fsm_info_get_bad_change_state_handler(const struct p101_fsm_info *info);

    void p101_fsm_info_default_bad_change_state_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, struct p101_fsm_effect_sink *sink,
                                                        struct p101_fsm_decision *decision);
    void p101_fsm_info_default_bad_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    void p101_fsm_info_default_will_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    void p101_fsm_info_default_did_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id);

    void p101_fsm_decide_transition(struct p101_fsm_decision *decision, p101_fsm_state_t next_state);
    void p101_fsm_decide_pause(struct p101_fsm_decision *decision);
    void p101_fsm_decide_exit(struct p101_fsm_decision *decision);
    void p101_fsm_emit_effect(const struct p101_env *env, struct p101_error *err, struct p101_fsm_effect_sink *sink, const char *kind, const void *data, size_t data_size);

    /*
     * step executes exactly one state callback or one rejected-transition
     * policy decision. State is committed only after a callback returns a
     * valid decision, both error objects remain clear, and the did-change
     * notifier succeeds.
     *
     * run is only a convenience loop around step. last_result may be NULL.
     */
    p101_fsm_step_status p101_fsm_step(struct p101_fsm_info *info, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_step_result *result) P101_ATTR_WARN_UNUSED_RESULT;
    p101_fsm_run_result  p101_fsm_run(struct p101_fsm_info *info, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_step_result *last_result) P101_ATTR_WARN_UNUSED_RESULT;

    void p101_fsm_exit_immediately(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision);

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_FSM_FSM_H
