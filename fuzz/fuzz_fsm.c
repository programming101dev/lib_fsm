#include "p101_fsm/fsm.h"
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <stddef.h>
#include <stdint.h>

enum fuzz_states
{
    FUZZ_A = P101_FSM_USER_START,
    FUZZ_B,
};

struct fuzz_context
{
    uint8_t choice;
};

static void choose_next(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    const struct fuzz_context *context = (const struct fuzz_context *)arg;

    (void)env;
    (void)err;
    (void)sink;
    switch(context->choice % 4U)
    {
        case 0:
            p101_fsm_decide_transition(decision, FUZZ_B);
            break;
        case 1:
            p101_fsm_decide_exit(decision);
            break;
        case 2:
            p101_fsm_decide_pause(decision);
            break;
        default:
            p101_fsm_decide_transition(decision, P101_FSM_INIT);
            break;
    }
}

static void exit_state(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    (void)env;
    (void)err;
    (void)arg;
    (void)sink;
    p101_fsm_decide_exit(decision);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct p101_error          *app_err;
    struct p101_env            *app_env;
    struct p101_error          *fsm_err;
    struct p101_env            *fsm_env;
    struct p101_fsm_info       *fsm;
    struct fuzz_context         context;
    struct p101_fsm_step_result last_step;
    struct p101_fsm_transition  transitions[] = {
        {P101_FSM_INIT, FUZZ_A, choose_next},
        {FUZZ_A,        FUZZ_B, exit_state },
    };
    size_t transition_count;

    if(size == 0)
    {
        return 0;
    }

    context.choice = data[0];
    if(size > 1)
    {
        switch(data[1] % 5U)
        {
            case 0:
                transitions[0].perform = NULL;
                break;
            case 1:
                transitions[0].to_id = P101_FSM_STATE_NONE;
                break;
            case 2:
                transitions[1] = transitions[0];
                break;
            case 3:
                transitions[0].from_id = P101_FSM_STATE_NONE;
                break;
            default:
                break;
        }
    }
    transition_count = size > 2 ? (size_t)(data[2] % 3U) : sizeof(transitions) / sizeof(transitions[0]);

    app_err = p101_error_create(false);
    fsm_err = p101_error_create(false);
    if(app_err == NULL || fsm_err == NULL)
    {
        p101_error_destroy(fsm_err);
        p101_error_destroy(app_err);
        return 0;
    }

    app_env = p101_env_create(app_err, NULL);
    fsm_env = p101_env_create(fsm_err, NULL);
    fsm     = p101_fsm_info_create(app_env, app_err, "fuzz", fsm_env, fsm_err, transitions, transition_count, NULL);
    if(fsm != NULL)
    {
        (void)p101_fsm_run(fsm, &context, NULL, &last_step);
    }

    p101_fsm_info_destroy(app_env, fsm_err, &fsm);
    p101_env_destroy(fsm_env);
    p101_env_destroy(app_env);
    p101_error_destroy(fsm_err);
    p101_error_destroy(app_err);
    return 0;
}
