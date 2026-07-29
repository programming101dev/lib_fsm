#ifndef LIBP101_FSM_FSM_H
#define LIBP101_FSM_FSM_H

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

#include <p101_env/env.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct p101_fsm_info;

    typedef enum
    {
        /*
         * Stop the current run without treating the value as a real target
         * state. Useful when a state callback or bad-transition handler wants
         * to ignore the requested transition without raising an error.
         */
        P101_FSM_IGNORE = -1,    // -1
        P101_FSM_INIT,           // 0
        P101_FSM_EXIT,           // 1
        P101_FSM_USER_START,     // 2
    } p101_fsm_state;

    typedef int p101_fsm_state_t;

    typedef enum
    {
        P101_FSM_RUN_ERROR  = -1,
        P101_FSM_RUN_PAUSED = 0,
        P101_FSM_RUN_EXITED = 1,
    } p101_fsm_run_result;

    typedef p101_fsm_state_t (*p101_fsm_state_func)(const struct p101_env *env, struct p101_error *err, void *arg);
    typedef void (*p101_fsm_info_will_change_state_notifier_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    typedef void (*p101_fsm_info_did_change_state_notifier_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id);
    typedef void (*p101_fsm_info_bad_change_state_notifier_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    typedef p101_fsm_state_t (*p101_fsm_info_bad_change_state_handler_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);

    struct p101_fsm_transition
    {
        // cppcheck-suppress unusedStructMember
        p101_fsm_state_t from_id;
        // cppcheck-suppress unusedStructMember
        p101_fsm_state_t to_id;
        // cppcheck-suppress unusedStructMember
        p101_fsm_state_func perform;
    };

    /*
     * env/err are borrowed and passed to user state callbacks. fsm_env/fsm_err
     * are also borrowed and are used by FSM-owned callbacks, validation, and
     * allocation. Passing NULL for fsm_env/fsm_err falls back to env/err. All
     * four borrowed objects must outlive the FSM.
     *
     * An FSM instance is not thread-safe or reentrant. A callback must not run
     * or destroy the same FSM instance.
     */
    struct p101_fsm_info                         *p101_fsm_info_create(const struct p101_env *env, struct p101_error *err, const char *name, const struct p101_env *fsm_env, struct p101_error *fsm_err,
                                                                       p101_fsm_info_bad_change_state_handler_func handler) P101_ATTR_MALLOC P101_ATTR_WARN_UNUSED_RESULT;
    void                                          p101_fsm_info_destroy(const struct p101_env *env, struct p101_fsm_info **pinfo);
    const char                                   *p101_fsm_info_get_name(const struct p101_env *env, const struct p101_fsm_info *info);
    void                                          p101_fsm_info_set_will_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_will_change_state_notifier_func notifier);
    void                                          p101_fsm_info_set_did_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_did_change_state_notifier_func notifier);
    void                                          p101_fsm_info_set_bad_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_notifier_func notifier);
    void                                          p101_fsm_info_set_bad_change_state_handler(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_handler_func handler);
    p101_fsm_info_will_change_state_notifier_func p101_fsm_info_get_will_change_state_notifier(const struct p101_fsm_info *info);
    p101_fsm_info_did_change_state_notifier_func  p101_fsm_info_get_did_change_state_notifier(const struct p101_fsm_info *info);
    p101_fsm_info_bad_change_state_notifier_func  p101_fsm_info_get_bad_change_state_notifier(const struct p101_fsm_info *info);
    p101_fsm_info_bad_change_state_handler_func   p101_fsm_info_get_bad_change_state_handler(const struct p101_fsm_info *info);
    p101_fsm_state_t                              p101_fsm_info_default_bad_change_state_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    void                                          p101_fsm_info_default_bad_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    void                                          p101_fsm_info_default_will_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    void p101_fsm_info_default_did_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id);
    /*
     * Run until a callback returns P101_FSM_EXIT, a callback/handler returns
     * P101_FSM_IGNORE, or an error is raised.
     *
     * transition_count is a number of array elements, not a byte size. Each
     * entry must have a non-NULL callback, a source of P101_FSM_INIT or a user
     * state, and a user-state destination. P101_FSM_EXIT and P101_FSM_IGNORE
     * are callback results and do not belong in the transition table.
     *
     * On P101_FSM_RUN_EXITED, the terminal state is persisted. Calling run
     * again is a no-op that returns P101_FSM_RUN_EXITED. On
     * P101_FSM_RUN_PAUSED, the current callback remains current and a later
     * run retries it. from_state_id/to_state_id are optional and receive the
     * final attempted edge.
     */
    p101_fsm_run_result p101_fsm_run(struct p101_fsm_info *info, p101_fsm_state_t *from_state_id, p101_fsm_state_t *to_state_id, void *arg, const struct p101_fsm_transition transitions[], size_t transition_count) P101_ATTR_WARN_UNUSED_RESULT;
    p101_fsm_state_t    p101_fsm_exit_immediately(const struct p101_env *env, struct p101_error *err, void *arg);

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_FSM_FSM_H
