#include "p101_fsm/errors.h"
#include "p101_fsm/fsm.h"
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

enum test_states
{
    STATE_A = P101_FSM_USER_START,
    STATE_B,
    STATE_C,
};

struct fixture
{
    struct p101_error    *app_err;
    struct p101_env      *app_env;
    struct p101_error    *fsm_err;
    struct p101_env      *fsm_env;
    struct p101_fsm_info *fsm;
};

struct callback_context
{
    struct p101_fsm_info         *fsm;
    struct p101_fsm_info        **fsm_pointer;
    struct p101_error            *fsm_err;
    struct p101_fsm_step_result   nested_result;
    p101_fsm_step_status          nested_status;
    int                           calls;
    int                           effects;
    int                           observations;
    size_t                        last_sequence;
    char                          effect_kind[16];
    int                           effect_value;
};

static int              failures;
static int              trace_entries;
static int              trace_exits;
static int              pause_calls;
static int              will_calls;
static int              did_calls;
static int              bad_calls;
static p101_fsm_state_t redirect_state;

#define EXPECT(condition)                                                                                                                                                                                                                                          \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        if(!(condition))                                                                                                                                                                                                                                           \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                                                                                                                   \
            failures++;                                                                                                                                                                                                                                            \
        }                                                                                                                                                                                                                                                          \
    } while(0)

static void state_to_b(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision);
static void state_exit(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision);

static const struct p101_fsm_transition basic_transitions[] = {
    {P101_FSM_INIT, STATE_A, state_to_b},
    {STATE_A,       STATE_B, state_exit},
};

static void fixture_create(struct fixture *fixture, const char *name, const struct p101_fsm_transition transitions[], size_t transition_count, p101_fsm_info_bad_change_state_handler_func handler)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->app_err = p101_error_create(false);
    fixture->app_env = p101_env_create(fixture->app_err, NULL);
    fixture->fsm_err = p101_error_create(false);
    fixture->fsm_env = p101_env_create(fixture->fsm_err, NULL);
    fixture->fsm     = p101_fsm_info_create(fixture->app_env, fixture->app_err, name, fixture->fsm_env, fixture->fsm_err, transitions, transition_count, handler);
}

static void fixture_destroy(struct fixture *fixture)
{
    p101_fsm_info_destroy(fixture->app_env, fixture->fsm_err, &fixture->fsm);
    p101_env_destroy(fixture->fsm_env);
    p101_error_destroy(fixture->fsm_err);
    p101_env_destroy(fixture->app_env);
    p101_error_destroy(fixture->app_err);
    memset(fixture, 0, sizeof(*fixture));
}

static void state_to_b(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    struct callback_context *context = (struct callback_context *)arg;

    (void)env;
    (void)err;
    (void)sink;
    if(context != NULL)
    {
        context->calls++;
    }
    p101_fsm_decide_transition(decision, STATE_B);
}

static void state_exit(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    struct callback_context *context = (struct callback_context *)arg;

    (void)env;
    (void)err;
    (void)sink;
    if(context != NULL)
    {
        context->calls++;
    }
    p101_fsm_decide_exit(decision);
}

static void state_pause_once(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    (void)env;
    (void)err;
    (void)arg;
    (void)sink;
    pause_calls++;
    if(pause_calls == 1)
    {
        p101_fsm_decide_pause(decision);
    }
    else
    {
        p101_fsm_decide_exit(decision);
    }
}

static void state_error(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    (void)env;
    (void)arg;
    (void)sink;
    p101_fsm_decide_transition(decision, STATE_B);
    P101_ERROR_RAISE_USER(err, "callback error", P101_FSM_ERROR_INVALID_ARGUMENT);
}

static void state_invalid(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    (void)env;
    (void)err;
    (void)arg;
    (void)sink;
    (void)decision;
}

static void state_reenter(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    struct callback_context *context = (struct callback_context *)arg;

    (void)env;
    (void)err;
    context->nested_status = p101_fsm_step(context->fsm, context, sink, &context->nested_result);
    p101_fsm_decide_exit(decision);
}

static void state_destroy(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    struct callback_context *context = (struct callback_context *)arg;

    (void)err;
    (void)sink;
    p101_fsm_info_destroy(env, context->fsm_err, context->fsm_pointer);
    p101_fsm_decide_exit(decision);
}

static void state_effect(const struct p101_env *env, struct p101_error *err, void *arg, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    static const int value = 42;

    (void)arg;
    p101_fsm_emit_effect(env, err, sink, "answer", &value, sizeof(value));
    p101_fsm_decide_exit(decision);
}

static void redirect_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    (void)env;
    (void)err;
    (void)info;
    (void)from_state_id;
    (void)to_state_id;
    (void)sink;
    p101_fsm_decide_transition(decision, redirect_state);
}

static void invalid_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, struct p101_fsm_effect_sink *sink, struct p101_fsm_decision *decision)
{
    (void)env;
    (void)err;
    (void)info;
    (void)from_state_id;
    (void)to_state_id;
    (void)sink;
    (void)decision;
}

static void will_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    (void)env;
    (void)err;
    (void)info;
    (void)from_state_id;
    (void)to_state_id;
    will_calls++;
}

static void did_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id)
{
    (void)env;
    (void)err;
    (void)info;
    (void)from_state_id;
    (void)to_state_id;
    (void)next_state_id;
    did_calls++;
}

static void did_error_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id)
{
    (void)env;
    (void)info;
    (void)from_state_id;
    (void)to_state_id;
    (void)next_state_id;
    P101_ERROR_RAISE_USER(err, "notifier error", P101_FSM_ERROR_INVALID_ARGUMENT);
}

static void bad_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    (void)env;
    (void)err;
    (void)info;
    (void)from_state_id;
    (void)to_state_id;
    bad_calls++;
}

static void effect_handler(const struct p101_env *env, struct p101_error *err, void *arg, const struct p101_fsm_effect *effect)
{
    struct callback_context *context = (struct callback_context *)arg;

    (void)env;
    (void)err;
    context->effects++;
    snprintf(context->effect_kind, sizeof(context->effect_kind), "%s", effect->kind);
    if(effect->data != NULL && effect->data_size == sizeof(context->effect_value))
    {
        memcpy(&context->effect_value, effect->data, effect->data_size);
    }
}

static void step_observer(const struct p101_env *env, const struct p101_fsm_info *info, const struct p101_fsm_step_result *result, void *arg)
{
    struct callback_context *context = (struct callback_context *)arg;

    (void)env;
    (void)info;
    context->observations++;
    context->last_sequence = result->sequence;
    if(context->fsm != NULL && context->observations == 1)
    {
        context->nested_status = p101_fsm_step(context->fsm, context, NULL, &context->nested_result);
    }
}

static void trace_enter(const struct p101_env *env, const char *file_name, const char *function_name, int line_number)
{
    (void)env;
    (void)file_name;
    (void)line_number;
    if(strncmp(function_name, "p101_fsm_", 9U) == 0 || strncmp(function_name, "fsm_", 4U) == 0)
    {
        trace_entries++;
    }
}

static void trace_exit(const struct p101_env *env, const char *file_name, const char *function_name, int line_number)
{
    (void)env;
    (void)file_name;
    (void)line_number;
    if(strncmp(function_name, "p101_fsm_", 9U) == 0 || strncmp(function_name, "fsm_", 4U) == 0)
    {
        trace_exits++;
    }
}

static void test_create_and_bound_table(void)
{
    struct fixture             fixture;
    struct callback_context    context = {0};
    struct p101_fsm_step_result result;
    struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_exit},
    };

    fixture_create(&fixture, "unit-fsm", transitions, 1U, NULL);
    EXPECT(fixture.fsm != NULL);
    EXPECT(strcmp(p101_fsm_info_get_name(fixture.app_env, fixture.fsm), "unit-fsm") == 0);
    EXPECT(p101_fsm_info_get_current_state(fixture.fsm) == STATE_A);
    transitions[0].perform = NULL;
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_EXITED);
    EXPECT(context.calls == 1);
    EXPECT(p101_fsm_info_is_terminal(fixture.fsm));
    fixture_destroy(&fixture);
}

static void test_invalid_create(void)
{
    struct fixture fixture;
    static const struct p101_fsm_transition multiple_initial[] = {
        {P101_FSM_INIT, STATE_A, state_exit},
        {P101_FSM_INIT, STATE_B, state_exit},
    };
    static const struct p101_fsm_transition duplicate[] = {
        {P101_FSM_INIT, STATE_A, state_exit},
        {P101_FSM_INIT, STATE_A, state_to_b},
    };
    static const struct p101_fsm_transition null_callback[] = {
        {P101_FSM_INIT, STATE_A, NULL},
    };

    fixture_create(&fixture, NULL, basic_transitions, 2U, NULL);
    EXPECT(fixture.fsm == NULL);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_ARGUMENT));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "empty", NULL, 0U, NULL);
    EXPECT(fixture.fsm == NULL);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_TRANSITION_TABLE));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "oversized", basic_transitions, SIZE_MAX, NULL);
    EXPECT(fixture.fsm == NULL);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_TRANSITION_TABLE));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "multiple", multiple_initial, 2U, NULL);
    EXPECT(fixture.fsm == NULL);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_TRANSITION_TABLE));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "duplicate", duplicate, 2U, NULL);
    EXPECT(fixture.fsm == NULL);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_TRANSITION_TABLE));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "null", null_callback, 1U, NULL);
    EXPECT(fixture.fsm == NULL);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_TRANSITION_TABLE));
    fixture_destroy(&fixture);
}

static void test_step_commit_and_terminal_result(void)
{
    struct fixture              fixture;
    struct callback_context     context = {0};
    struct p101_fsm_step_result result;

    fixture_create(&fixture, "step", basic_transitions, 2U, NULL);
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_TRANSITIONED);
    EXPECT(result.sequence == 1U);
    EXPECT(result.from_state == P101_FSM_INIT);
    EXPECT(result.attempted_state == STATE_A);
    EXPECT(result.next_state == STATE_B);
    EXPECT(result.refusal == P101_FSM_REFUSAL_NONE);
    EXPECT(p101_fsm_info_get_current_state(fixture.fsm) == STATE_B);

    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_EXITED);
    EXPECT(result.sequence == 2U);
    EXPECT(result.from_state == STATE_A);
    EXPECT(result.attempted_state == STATE_B);
    EXPECT(result.next_state == P101_FSM_STATE_NONE);
    EXPECT(p101_fsm_info_get_current_state(fixture.fsm) == STATE_B);

    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_EXITED);
    EXPECT(result.sequence == 3U);
    EXPECT(result.refusal == P101_FSM_REFUSAL_TERMINAL_MACHINE);
    EXPECT(context.calls == 2);
    EXPECT(p101_fsm_info_get_step_sequence(fixture.fsm) == 3U);
    fixture_destroy(&fixture);
}

static void test_pause_does_not_commit(void)
{
    struct fixture              fixture;
    struct p101_fsm_step_result result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_pause_once},
    };

    pause_calls = 0;
    fixture_create(&fixture, "pause", transitions, 1U, NULL);
    EXPECT(p101_fsm_step(fixture.fsm, NULL, NULL, &result) == P101_FSM_STEP_PAUSED);
    EXPECT(p101_fsm_info_get_current_state(fixture.fsm) == STATE_A);
    EXPECT(p101_fsm_step(fixture.fsm, NULL, NULL, &result) == P101_FSM_STEP_EXITED);
    EXPECT(pause_calls == 2);
    fixture_destroy(&fixture);
}

static void test_errors_do_not_commit(void)
{
    struct fixture              fixture;
    struct p101_fsm_step_result result;
    static const struct p101_fsm_transition callback_error[] = {
        {P101_FSM_INIT, STATE_A, state_error},
    };
    static const struct p101_fsm_transition notifier_error[] = {
        {P101_FSM_INIT, STATE_A, state_to_b},
    };

    fixture_create(&fixture, "callback-error", callback_error, 1U, NULL);
    EXPECT(p101_fsm_step(fixture.fsm, NULL, NULL, &result) == P101_FSM_STEP_ERROR);
    EXPECT(p101_fsm_info_get_current_state(fixture.fsm) == STATE_A);
    EXPECT(p101_error_has_error(fixture.app_err));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "notifier-error", notifier_error, 1U, NULL);
    p101_fsm_info_set_did_change_state_notifier(fixture.fsm, did_error_notifier);
    EXPECT(p101_fsm_step(fixture.fsm, NULL, NULL, &result) == P101_FSM_STEP_ERROR);
    EXPECT(p101_fsm_info_get_current_state(fixture.fsm) == STATE_A);
    EXPECT(p101_error_has_error(fixture.fsm_err));
    fixture_destroy(&fixture);
}

static void test_typed_refusals(void)
{
    struct fixture              fixture;
    struct callback_context     context = {0};
    struct p101_fsm_step_result result;
    static const struct p101_fsm_transition unknown[] = {
        {P101_FSM_INIT, STATE_A, state_to_b},
        {STATE_A,       STATE_C, state_exit},
    };
    static const struct p101_fsm_transition invalid_callback[] = {
        {P101_FSM_INIT, STATE_A, state_invalid},
    };

    fixture_create(&fixture, "unknown-default", unknown, 2U, NULL);
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_TRANSITIONED);
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_ERROR);
    EXPECT(result.refusal == P101_FSM_REFUSAL_UNKNOWN_TRANSITION);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_UNKNOWN_TRANSITION));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "invalid-callback", invalid_callback, 1U, NULL);
    EXPECT(p101_fsm_step(fixture.fsm, NULL, NULL, &result) == P101_FSM_STEP_REFUSED);
    EXPECT(result.refusal == P101_FSM_REFUSAL_INVALID_CALLBACK_DECISION);
    EXPECT(p101_fsm_info_get_current_state(fixture.fsm) == STATE_A);
    fixture_destroy(&fixture);

    fixture_create(&fixture, "invalid-handler", unknown, 2U, invalid_handler);
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_TRANSITIONED);
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_REFUSED);
    EXPECT(result.refusal == P101_FSM_REFUSAL_INVALID_HANDLER_DECISION);
    fixture_destroy(&fixture);
}

static void test_redirect_is_one_step(void)
{
    struct fixture              fixture;
    struct callback_context     context = {0};
    struct p101_fsm_step_result result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_to_b},
        {STATE_A,       STATE_C, state_exit},
    };

    fixture_create(&fixture, "redirect", transitions, 2U, redirect_handler);
    redirect_state = STATE_C;
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_TRANSITIONED);
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_REFUSED);
    EXPECT(result.refusal == P101_FSM_REFUSAL_UNKNOWN_TRANSITION);
    EXPECT(result.next_state == STATE_C);
    EXPECT(context.calls == 1);
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_EXITED);
    EXPECT(context.calls == 2);
    fixture_destroy(&fixture);

    fixture_create(&fixture, "redirect-cycle", transitions, 2U, redirect_handler);
    redirect_state = STATE_B;
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_TRANSITIONED);
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_REFUSED);
    EXPECT(result.refusal == P101_FSM_REFUSAL_REDIRECT_CYCLE);
    fixture_destroy(&fixture);
}

static void test_run_is_step_loop(void)
{
    struct fixture              fixture;
    struct callback_context     context = {0};
    struct p101_fsm_step_result result;
    p101_fsm_run_result         run_result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_to_b},
        {STATE_A,       STATE_C, state_exit},
    };

    fixture_create(&fixture, "run", transitions, 2U, redirect_handler);
    redirect_state = STATE_C;
    run_result     = p101_fsm_run(fixture.fsm, &context, NULL, &result);
    EXPECT(run_result == P101_FSM_RUN_EXITED);
    EXPECT(result.status == P101_FSM_STEP_EXITED);
    EXPECT(result.sequence == 3U);
    EXPECT(context.calls == 2);
    fixture_destroy(&fixture);
}

static void test_reentrant_operations_are_rejected(void)
{
    struct fixture              fixture;
    struct callback_context     context = {0};
    struct p101_fsm_step_result result;
    static const struct p101_fsm_transition reenter[] = {
        {P101_FSM_INIT, STATE_A, state_reenter},
    };
    static const struct p101_fsm_transition destroy[] = {
        {P101_FSM_INIT, STATE_A, state_destroy},
    };

    fixture_create(&fixture, "reenter", reenter, 1U, NULL);
    context.fsm = fixture.fsm;
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_ERROR);
    EXPECT(context.nested_status == P101_FSM_STEP_REFUSED);
    EXPECT(context.nested_result.refusal == P101_FSM_REFUSAL_REENTRANT_INVOCATION);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_REENTRANT_OPERATION));
    fixture_destroy(&fixture);

    memset(&context, 0, sizeof(context));
    fixture_create(&fixture, "destroy", destroy, 1U, NULL);
    context.fsm_pointer = &fixture.fsm;
    context.fsm_err     = fixture.fsm_err;
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_ERROR);
    EXPECT(fixture.fsm != NULL);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_REENTRANT_OPERATION));
    fixture_destroy(&fixture);
}

static void test_effects_and_step_observer(void)
{
    struct fixture              fixture;
    struct callback_context     context = {0};
    struct p101_fsm_step_result result;
    struct p101_fsm_effect_sink sink;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_effect},
    };

    fixture_create(&fixture, "effects", transitions, 1U, NULL);
    sink.handle  = effect_handler;
    sink.context = &context;
    p101_fsm_info_set_step_observer(fixture.fsm, step_observer, &context);
    EXPECT(p101_fsm_step(fixture.fsm, &context, &sink, &result) == P101_FSM_STEP_EXITED);
    EXPECT(context.effects == 1);
    EXPECT(strcmp(context.effect_kind, "answer") == 0);
    EXPECT(context.effect_value == 42);
    EXPECT(context.observations == 1);
    EXPECT(context.last_sequence == 1U);
    fixture_destroy(&fixture);
}

static void test_step_observer_cannot_reenter(void)
{
    struct fixture              fixture;
    struct callback_context     context = {0};
    struct p101_fsm_step_result result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_exit},
    };

    fixture_create(&fixture, "observer-reentry", transitions, 1U, NULL);
    context.fsm = fixture.fsm;
    p101_fsm_info_set_step_observer(fixture.fsm, step_observer, &context);
    EXPECT(p101_fsm_step(fixture.fsm, &context, NULL, &result) == P101_FSM_STEP_EXITED);
    EXPECT(context.observations == 1);
    EXPECT(context.nested_status == P101_FSM_STEP_REFUSED);
    EXPECT(context.nested_result.refusal == P101_FSM_REFUSAL_REENTRANT_INVOCATION);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_REENTRANT_OPERATION));
    fixture_destroy(&fixture);
}

static void test_configuration_and_null_api(void)
{
    struct fixture              fixture;
    struct p101_fsm_step_result result;
    struct p101_fsm_decision    decision;

    EXPECT(p101_fsm_info_get_name(NULL, NULL) == NULL);
    EXPECT(p101_fsm_info_get_current_state(NULL) == P101_FSM_STATE_NONE);
    EXPECT(p101_fsm_info_get_step_sequence(NULL) == 0U);
    EXPECT(!p101_fsm_info_is_terminal(NULL));
    EXPECT(p101_fsm_step(NULL, NULL, NULL, &result) == P101_FSM_STEP_ERROR);
    EXPECT(p101_fsm_run(NULL, NULL, NULL, &result) == P101_FSM_RUN_ERROR);

    fixture_create(&fixture, "configuration", basic_transitions, 2U, redirect_handler);
    p101_fsm_info_set_will_change_state_notifier(fixture.fsm, will_notifier);
    p101_fsm_info_set_did_change_state_notifier(fixture.fsm, did_notifier);
    p101_fsm_info_set_bad_change_state_notifier(fixture.fsm, bad_notifier);
    EXPECT(p101_fsm_info_get_will_change_state_notifier(fixture.fsm) == will_notifier);
    EXPECT(p101_fsm_info_get_did_change_state_notifier(fixture.fsm) == did_notifier);
    EXPECT(p101_fsm_info_get_bad_change_state_notifier(fixture.fsm) == bad_notifier);
    EXPECT(p101_fsm_info_get_bad_change_state_handler(fixture.fsm) == redirect_handler);
    p101_fsm_info_set_bad_change_state_handler(fixture.fsm, NULL);
    EXPECT(p101_fsm_info_get_bad_change_state_handler(fixture.fsm) == p101_fsm_info_default_bad_change_state_handler);

    decision.kind = P101_FSM_DECISION_INVALID;
    p101_fsm_info_default_bad_change_state_handler(NULL, NULL, NULL, STATE_A, STATE_B, NULL, &decision);
    EXPECT(decision.kind == P101_FSM_DECISION_EXIT);
    p101_fsm_exit_immediately(NULL, NULL, NULL, NULL, &decision);
    EXPECT(decision.kind == P101_FSM_DECISION_EXIT);

    will_calls = 0;
    did_calls  = 0;
    bad_calls  = 0;
    p101_fsm_info_default_will_change_state_notifier(fixture.fsm_env, fixture.fsm_err, fixture.fsm, P101_FSM_INIT, STATE_A);
    p101_fsm_info_default_did_change_state_notifier(fixture.fsm_env, fixture.fsm_err, fixture.fsm, P101_FSM_INIT, STATE_A, STATE_B);
    p101_fsm_info_default_bad_change_state_notifier(fixture.fsm_env, fixture.fsm_err, fixture.fsm, STATE_A, STATE_C);
    EXPECT(p101_error_has_no_error(fixture.fsm_err));
    fixture_destroy(&fixture);
}

static void test_balanced_tracing(void)
{
    struct fixture              fixture;
    struct callback_context     context = {0};
    struct p101_fsm_step_result result;

    fixture_create(&fixture, "trace", basic_transitions, 2U, NULL);
    trace_entries = 0;
    trace_exits   = 0;
    p101_env_set_tracer(fixture.fsm_env, trace_enter);
    p101_env_set_exit_tracer(fixture.fsm_env, trace_exit);
    EXPECT(p101_fsm_run(fixture.fsm, &context, NULL, &result) == P101_FSM_RUN_EXITED);
    EXPECT(trace_entries > 0);
    EXPECT(trace_entries == trace_exits);
    fixture_destroy(&fixture);
}

int main(void)
{
    test_create_and_bound_table();
    test_invalid_create();
    test_step_commit_and_terminal_result();
    test_pause_does_not_commit();
    test_errors_do_not_commit();
    test_typed_refusals();
    test_redirect_is_one_step();
    test_run_is_step_loop();
    test_reentrant_operations_are_rejected();
    test_effects_and_step_observer();
    test_step_observer_cannot_reenter();
    test_configuration_and_null_api();
    test_balanced_tracing();

    if(failures != 0)
    {
        fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return 1;
    }
    return 0;
}
