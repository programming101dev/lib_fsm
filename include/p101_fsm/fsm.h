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

#ifndef P101_ATTR_MALLOC
    #if defined(__GNUC__) || defined(__clang__)
        #define P101_ATTR_MALLOC __attribute__((malloc))
    #else
        #define P101_ATTR_MALLOC
    #endif
#endif

#ifndef P101_ATTR_WARN_UNUSED_RESULT
    #if defined(__GNUC__) || defined(__clang__)
        #define P101_ATTR_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
    #else
        #define P101_ATTR_WARN_UNUSED_RESULT
    #endif
#endif

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
     * env/err are passed to user state callbacks. fsm_env/fsm_err are used by
     * FSM-owned callbacks such as transition notifiers and bad-transition
     * handlers, and by FSM creation/internal allocation. Passing NULL for
     * fsm_env/fsm_err falls back to env/err.
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
    void             p101_fsm_info_default_did_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id);
    void             p101_fsm_run(struct p101_fsm_info *info, p101_fsm_state_t *from_state_id, p101_fsm_state_t *to_state_id, void *arg, const struct p101_fsm_transition transitions[], size_t transitions_nbytes);
    p101_fsm_state_t p101_fsm_exit_immediately(const struct p101_env *env, struct p101_error *err, void *arg);

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_FSM_FSM_H
