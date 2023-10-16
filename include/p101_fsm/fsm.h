#ifndef LIBP101_FSM_FSM_H
#define LIBP101_FSM_FSM_H

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

#include <p101_env/env.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct p101_fsm_info;

    typedef enum
    {
        P101_FSM_IGNORE = -1,    // -1
        P101_FSM_INIT,           // 0
        P101_FSM_EXIT,           // 1
        P101_FSM_USER_START,     // 2
    } p101_fsm_state;

    typedef int p101_fsm_state_t;

    typedef p101_fsm_state_t (*p101_fsm_state_func)(const struct p101_env *env, struct p101_error *err, void *arg);
    typedef void             (*p101_fsm_info_will_change_state_notifier_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    typedef void             (*p101_fsm_info_did_change_state_notifier_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id);
    typedef void             (*p101_fsm_info_bad_change_state_notifier_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
    typedef p101_fsm_state_t (*p101_fsm_info_bad_change_state_handler_func)(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);

    struct p101_fsm_transition
    {
        p101_fsm_state_t    from_id;
        p101_fsm_state_t    to_id;
        p101_fsm_state_func perform;
    };

    /**
     *
     * @param env
     * @param err
     * @param name
     * @param fsm_env
     * @param fsm_err
     * @return
     */
    struct p101_fsm_info *p101_fsm_info_create(const struct p101_env *env, struct p101_error *err, const char *name, const struct p101_env *fsm_env, struct p101_error *fsm_err, p101_fsm_info_bad_change_state_handler_func handler);

    /**
     *
     * @param env
     * @param pinfo
     */
    void p101_fsm_info_destroy(const struct p101_env *env, struct p101_fsm_info **pinfo);

    /**
     *
     * @param info
     * @return
     */
    const char *p101_fsm_info_get_name(const struct p101_env *env, const struct p101_fsm_info *info);

    /**
     *
     * @param info
     * @param notifier
     */
    void p101_fsm_info_set_will_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_will_change_state_notifier_func notifier);

    /**
     *
     * @param info
     * @param notifier
     */
    void p101_fsm_info_set_did_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_did_change_state_notifier_func notifier);
    /**
     *
     * @param info
     * @param notifier
     */
    void p101_fsm_info_set_bad_change_state_notifier(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_notifier_func notifier);

    /**
     *
     * @param env
     * @param info
     * @param handler
     */
    void p101_fsm_info_set_bad_change_state_handler(struct p101_fsm_info *info, p101_fsm_info_bad_change_state_handler_func handler);

    /**
     *
     * @param info
     * @param notifier
     */
    p101_fsm_info_will_change_state_notifier_func p101_fsm_info_get_will_change_state_notifier(const struct p101_fsm_info *info);

    /**
     *
     * @param info
     * @param notifier
     */
    p101_fsm_info_did_change_state_notifier_func p101_fsm_info_get_did_change_state_notifier(const struct p101_fsm_info *info);
    /**
     *
     * @param info
     * @param notifier
     */
    p101_fsm_info_bad_change_state_notifier_func p101_fsm_info_get_bad_change_state_notifier(const struct p101_fsm_info *info);

    /**
     *
     * @param env
     * @param info
     * @param handler
     */
    p101_fsm_info_bad_change_state_handler_func p101_fsm_info_get_bad_change_state_handler(const struct p101_fsm_info *info);

    /**
     *
     * @param env
     * @param err
     * @param info
     * @param from_state_id
     * @param to_state_id
     * @return
     */
    p101_fsm_state_t p101_fsm_info_default_bad_change_state_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);

    /**
     *
     * @param env
     * @param err
     * @param info
     * @param from_state_id
     * @param to_state_id
     */
    void p101_fsm_info_default_bad_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);

    /**
     *
     * @param env
     * @param err
     * @param info
     * @param from_state_id
     * @param to_state_id
     */
    void p101_fsm_info_default_will_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);

    /**
     *
     * @param env
     * @param err
     * @param info
     * @param from_state_id
     * @param to_state_id
     * @param next_state_id
     */
    void p101_fsm_info_default_did_change_state_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id);

    /**
     *
     * @param env
     * @param err
     * @param info
     * @param from_state_id
     * @param to_state_id
     * @param arg
     * @param transitions
     */
    void p101_fsm_run(struct p101_fsm_info *info, p101_fsm_state_t *from_state_id, p101_fsm_state_t *to_state_id, void *arg, const struct p101_fsm_transition transitions[]);

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_FSM_FSM_H
