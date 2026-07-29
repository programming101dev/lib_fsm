#include "p101_fsm/errors.h"
#include "p101_fsm/fsm.h"
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <stdio.h>
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
    struct p101_fsm_info             *fsm;
    const struct p101_fsm_transition *transitions;
    size_t                            transition_count;
    int                               calls;
    p101_fsm_run_result               nested_result;
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

static void             fixture_create(struct fixture *fixture, const char *name);
static void             fixture_destroy(struct fixture *fixture);
static p101_fsm_state_t state_to_b(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t state_exit(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t state_pause_once(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t state_reenter(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t state_invalid(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t redirect_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
static p101_fsm_state_t cycling_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
static void             will_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
static void             did_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id, p101_fsm_state_t next_state_id);
static void             bad_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id);
static void             trace_enter(const struct p101_env *env, const char *file_name, const char *function_name, int line_number);
static void             trace_exit(const struct p101_env *env, const char *file_name, const char *function_name, int line_number);
static void             test_create_and_name(void);
static void             test_null_api(void);
static void             test_configuration_api(void);
static void             test_exit_is_persistent(void);
static void             test_pause_retries_current_state(void);
static void             test_unknown_transition(void);
static void             test_redirect_handler(void);
static void             test_handler_results(void);
static void             test_invalid_callback_result(void);
static void             test_invalid_tables(void);
static void             test_reentrant_run(void);
static void             test_balanced_tracing(void);

int main(void)
{
    test_create_and_name();
    test_null_api();
    test_configuration_api();
    test_exit_is_persistent();
    test_pause_retries_current_state();
    test_unknown_transition();
    test_redirect_handler();
    test_handler_results();
    test_invalid_callback_result();
    test_invalid_tables();
    test_reentrant_run();
    test_balanced_tracing();

    if(failures != 0)
    {
        fprintf(stderr, "%d lib_fsm test(s) failed\n", failures);
        return 1;
    }

    return 0;
}

static void fixture_create(struct fixture *fixture, const char *name)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->app_err = p101_error_create(false);
    fixture->fsm_err = p101_error_create(false);
    EXPECT(fixture->app_err != NULL);
    EXPECT(fixture->fsm_err != NULL);
    fixture->app_env = p101_env_create(fixture->app_err, NULL);
    fixture->fsm_env = p101_env_create(fixture->fsm_err, NULL);
    EXPECT(fixture->app_env != NULL);
    EXPECT(fixture->fsm_env != NULL);
    fixture->fsm = p101_fsm_info_create(fixture->app_env, fixture->app_err, name, fixture->fsm_env, fixture->fsm_err, NULL);
}

static void fixture_destroy(struct fixture *fixture)
{
    p101_fsm_info_destroy(fixture->app_env, &fixture->fsm);
    p101_env_destroy(fixture->fsm_env);
    p101_env_destroy(fixture->app_env);
    p101_error_destroy(fixture->fsm_err);
    p101_error_destroy(fixture->app_err);
}

static p101_fsm_state_t state_to_b(const struct p101_env *env, struct p101_error *err, void *arg)
{
    struct callback_context *context = (struct callback_context *)arg;

    (void)env;
    (void)err;
    context->calls++;
    return STATE_B;
}

static p101_fsm_state_t state_exit(const struct p101_env *env, struct p101_error *err, void *arg)
{
    struct callback_context *context = (struct callback_context *)arg;

    (void)env;
    (void)err;
    context->calls++;
    return P101_FSM_EXIT;
}

static p101_fsm_state_t state_pause_once(const struct p101_env *env, struct p101_error *err, void *arg)
{
    (void)env;
    (void)err;
    (void)arg;
    pause_calls++;
    return pause_calls == 1 ? P101_FSM_IGNORE : P101_FSM_EXIT;
}

static p101_fsm_state_t state_reenter(const struct p101_env *env, struct p101_error *err, void *arg)
{
    struct callback_context *context = (struct callback_context *)arg;

    (void)env;
    (void)err;
    context->nested_result = p101_fsm_run(context->fsm, NULL, NULL, context, context->transitions, context->transition_count);
    return P101_FSM_EXIT;
}

static p101_fsm_state_t state_invalid(const struct p101_env *env, struct p101_error *err, void *arg)
{
    (void)env;
    (void)err;
    (void)arg;
    return P101_FSM_INIT;
}

static p101_fsm_state_t redirect_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    (void)env;
    (void)err;
    (void)info;
    (void)from_state_id;
    (void)to_state_id;
    return redirect_state;
}

static p101_fsm_state_t cycling_handler(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    (void)env;
    (void)err;
    (void)info;
    (void)from_state_id;
    return to_state_id == STATE_B ? STATE_C : STATE_B;
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

static void bad_notifier(const struct p101_env *env, struct p101_error *err, const struct p101_fsm_info *info, p101_fsm_state_t from_state_id, p101_fsm_state_t to_state_id)
{
    (void)env;
    (void)err;
    (void)info;
    (void)from_state_id;
    (void)to_state_id;
    bad_calls++;
}

static void trace_enter(const struct p101_env *env, const char *file_name, const char *function_name, int line_number)
{
    (void)env;
    (void)file_name;
    (void)line_number;
    if(strncmp(function_name, "p101_fsm_", 9) == 0 || strncmp(function_name, "fsm_", 4) == 0)
    {
        trace_entries++;
    }
}

static void trace_exit(const struct p101_env *env, const char *file_name, const char *function_name, int line_number)
{
    (void)env;
    (void)file_name;
    (void)line_number;
    if(strncmp(function_name, "p101_fsm_", 9) == 0 || strncmp(function_name, "fsm_", 4) == 0)
    {
        trace_exits++;
    }
}

static void test_create_and_name(void)
{
    struct fixture        fixture;
    struct p101_fsm_info *fsm;

    fixture_create(&fixture, "unit-fsm");
    EXPECT(fixture.fsm != NULL);
    EXPECT(strcmp(p101_fsm_info_get_name(fixture.app_env, fixture.fsm), "unit-fsm") == 0);
    fixture_destroy(&fixture);

    fixture_create(&fixture, NULL);
    EXPECT(fixture.fsm == NULL);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_ARGUMENT));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "fallback-context");
    p101_fsm_info_destroy(fixture.app_env, &fixture.fsm);
    fsm = p101_fsm_info_create(fixture.app_env, fixture.app_err, "fallback-context", NULL, NULL, redirect_handler);
    EXPECT(fsm != NULL);
    EXPECT(p101_fsm_info_get_bad_change_state_handler(fsm) == redirect_handler);
    p101_fsm_info_destroy(fixture.app_env, &fsm);
    fixture_destroy(&fixture);
}

static void test_null_api(void)
{
    struct p101_fsm_info *fsm = NULL;

    EXPECT(p101_fsm_info_get_name(NULL, NULL) == NULL);
    EXPECT(p101_fsm_info_get_will_change_state_notifier(NULL) == NULL);
    EXPECT(p101_fsm_info_get_did_change_state_notifier(NULL) == NULL);
    EXPECT(p101_fsm_info_get_bad_change_state_notifier(NULL) == NULL);
    EXPECT(p101_fsm_info_get_bad_change_state_handler(NULL) == NULL);
    EXPECT(p101_fsm_run(NULL, NULL, NULL, NULL, NULL, 0) == P101_FSM_RUN_ERROR);
    EXPECT(p101_fsm_info_default_bad_change_state_handler(NULL, NULL, NULL, STATE_A, STATE_B) == P101_FSM_EXIT);
    EXPECT(p101_fsm_exit_immediately(NULL, NULL, NULL) == P101_FSM_EXIT);

    p101_fsm_info_set_will_change_state_notifier(NULL, will_notifier);
    p101_fsm_info_set_did_change_state_notifier(NULL, did_notifier);
    p101_fsm_info_set_bad_change_state_notifier(NULL, bad_notifier);
    p101_fsm_info_set_bad_change_state_handler(NULL, redirect_handler);
    p101_fsm_info_destroy(NULL, NULL);
    p101_fsm_info_destroy(NULL, &fsm);
}

static void test_configuration_api(void)
{
    struct fixture                          fixture;
    struct callback_context                 context = {0};
    p101_fsm_run_result                     result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_exit},
    };

    fixture_create(&fixture, "configuration");
    p101_fsm_info_set_will_change_state_notifier(fixture.fsm, will_notifier);
    p101_fsm_info_set_did_change_state_notifier(fixture.fsm, did_notifier);
    p101_fsm_info_set_bad_change_state_notifier(fixture.fsm, bad_notifier);
    p101_fsm_info_set_bad_change_state_handler(fixture.fsm, redirect_handler);
    EXPECT(p101_fsm_info_get_will_change_state_notifier(fixture.fsm) == will_notifier);
    EXPECT(p101_fsm_info_get_did_change_state_notifier(fixture.fsm) == did_notifier);
    EXPECT(p101_fsm_info_get_bad_change_state_notifier(fixture.fsm) == bad_notifier);
    EXPECT(p101_fsm_info_get_bad_change_state_handler(fixture.fsm) == redirect_handler);

    p101_fsm_info_set_bad_change_state_handler(fixture.fsm, NULL);
    EXPECT(p101_fsm_info_get_bad_change_state_handler(fixture.fsm) == p101_fsm_info_default_bad_change_state_handler);

    will_calls = 0;
    did_calls  = 0;
    bad_calls  = 0;
    result     = p101_fsm_run(fixture.fsm, NULL, NULL, &context, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_EXITED);
    EXPECT(will_calls == 1);
    EXPECT(did_calls == 1);
    EXPECT(bad_calls == 0);

    p101_fsm_info_default_will_change_state_notifier(fixture.fsm_env, fixture.fsm_err, fixture.fsm, P101_FSM_INIT, STATE_A);
    p101_fsm_info_default_did_change_state_notifier(fixture.fsm_env, fixture.fsm_err, fixture.fsm, P101_FSM_INIT, STATE_A, P101_FSM_EXIT);
    p101_fsm_info_default_bad_change_state_notifier(fixture.fsm_env, fixture.fsm_err, fixture.fsm, STATE_A, STATE_B);
    EXPECT(p101_error_has_no_error(fixture.fsm_err));
    fixture_destroy(&fixture);
}

static void test_exit_is_persistent(void)
{
    struct fixture                          fixture;
    struct callback_context                 context    = {0};
    p101_fsm_state_t                        from_state = -99;
    p101_fsm_state_t                        to_state   = -99;
    p101_fsm_run_result                     result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_to_b},
        {STATE_A,       STATE_B, state_exit},
    };

    fixture_create(&fixture, "exit");
    result = p101_fsm_run(fixture.fsm, &from_state, &to_state, &context, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_EXITED);
    EXPECT(context.calls == 2);
    EXPECT(from_state == STATE_B);
    EXPECT(to_state == P101_FSM_EXIT);
    result = p101_fsm_run(fixture.fsm, &from_state, &to_state, &context, NULL, 0);
    EXPECT(result == P101_FSM_RUN_EXITED);
    EXPECT(context.calls == 2);
    fixture_destroy(&fixture);
}

static void test_pause_retries_current_state(void)
{
    struct fixture                          fixture;
    p101_fsm_run_result                     result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_pause_once},
    };

    pause_calls = 0;
    fixture_create(&fixture, "pause");
    result = p101_fsm_run(fixture.fsm, NULL, NULL, NULL, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_PAUSED);
    result = p101_fsm_run(fixture.fsm, NULL, NULL, NULL, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_EXITED);
    EXPECT(pause_calls == 2);
    fixture_destroy(&fixture);
}

static void test_unknown_transition(void)
{
    struct fixture                          fixture;
    struct callback_context                 context = {0};
    p101_fsm_run_result                     result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_to_b},
    };

    fixture_create(&fixture, "unknown");
    result = p101_fsm_run(fixture.fsm, NULL, NULL, &context, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_ERROR);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_UNKNOWN_TRANSITION));
    fixture_destroy(&fixture);
}

static void test_redirect_handler(void)
{
    struct fixture                          fixture;
    struct callback_context                 context = {0};
    p101_fsm_run_result                     result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_to_b},
        {STATE_A,       STATE_C, state_exit},
    };

    fixture_create(&fixture, "redirect");
    redirect_state = STATE_C;
    p101_fsm_info_set_bad_change_state_handler(fixture.fsm, redirect_handler);
    result = p101_fsm_run(fixture.fsm, NULL, NULL, &context, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_EXITED);
    EXPECT(context.calls == 2);
    fixture_destroy(&fixture);

    fixture_create(&fixture, "loop");
    redirect_state = STATE_B;
    p101_fsm_info_set_bad_change_state_handler(fixture.fsm, redirect_handler);
    result = p101_fsm_run(fixture.fsm, NULL, NULL, &context, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_ERROR);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_HANDLER_LOOP));
    fixture_destroy(&fixture);
}

static void test_handler_results(void)
{
    struct fixture                          fixture;
    struct callback_context                 context = {0};
    p101_fsm_run_result                     result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_to_b},
    };

    fixture_create(&fixture, "handler-ignore");
    redirect_state = P101_FSM_IGNORE;
    p101_fsm_info_set_bad_change_state_notifier(fixture.fsm, bad_notifier);
    p101_fsm_info_set_bad_change_state_handler(fixture.fsm, redirect_handler);
    bad_calls = 0;
    result    = p101_fsm_run(fixture.fsm, NULL, NULL, &context, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_PAUSED);
    EXPECT(bad_calls == 1);
    fixture_destroy(&fixture);

    context.calls = 0;
    fixture_create(&fixture, "handler-exit");
    redirect_state = P101_FSM_EXIT;
    p101_fsm_info_set_bad_change_state_handler(fixture.fsm, redirect_handler);
    result = p101_fsm_run(fixture.fsm, NULL, NULL, &context, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_EXITED);
    fixture_destroy(&fixture);

    context.calls = 0;
    fixture_create(&fixture, "handler-invalid");
    redirect_state = P101_FSM_INIT;
    p101_fsm_info_set_bad_change_state_handler(fixture.fsm, redirect_handler);
    result = p101_fsm_run(fixture.fsm, NULL, NULL, &context, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_ERROR);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_ARGUMENT));
    fixture_destroy(&fixture);

    context.calls = 0;
    fixture_create(&fixture, "handler-cycle");
    p101_fsm_info_set_bad_change_state_handler(fixture.fsm, cycling_handler);
    result = p101_fsm_run(fixture.fsm, NULL, NULL, &context, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_ERROR);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_HANDLER_LOOP));
    fixture_destroy(&fixture);
}

static void test_invalid_callback_result(void)
{
    struct fixture                          fixture;
    p101_fsm_run_result                     result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_invalid},
    };

    fixture_create(&fixture, "invalid-callback-result");
    result = p101_fsm_run(fixture.fsm, NULL, NULL, NULL, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_ERROR);
    EXPECT(p101_error_is_error(fixture.app_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_ARGUMENT));
    EXPECT(p101_error_has_no_error(fixture.fsm_err));
    fixture_destroy(&fixture);
}

static void test_invalid_tables(void)
{
    struct fixture                          fixture;
    p101_fsm_run_result                     result;
    static const struct p101_fsm_transition null_callback[] = {
        {P101_FSM_INIT, STATE_A, NULL},
    };
    static const struct p101_fsm_transition reserved_state[] = {
        {P101_FSM_INIT, P101_FSM_EXIT, state_exit},
    };
    static const struct p101_fsm_transition duplicate[] = {
        {P101_FSM_INIT, STATE_A, state_exit},
        {P101_FSM_INIT, STATE_A, state_to_b},
    };

    fixture_create(&fixture, "empty");
    result = p101_fsm_run(fixture.fsm, NULL, NULL, NULL, NULL, 0);
    EXPECT(result == P101_FSM_RUN_ERROR);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_TRANSITION_TABLE));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "null-callback");
    result = p101_fsm_run(fixture.fsm, NULL, NULL, NULL, null_callback, sizeof(null_callback) / sizeof(null_callback[0]));
    EXPECT(result == P101_FSM_RUN_ERROR);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_TRANSITION_TABLE));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "reserved-state");
    result = p101_fsm_run(fixture.fsm, NULL, NULL, NULL, reserved_state, sizeof(reserved_state) / sizeof(reserved_state[0]));
    EXPECT(result == P101_FSM_RUN_ERROR);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_TRANSITION_TABLE));
    fixture_destroy(&fixture);

    fixture_create(&fixture, "duplicate");
    result = p101_fsm_run(fixture.fsm, NULL, NULL, NULL, duplicate, sizeof(duplicate) / sizeof(duplicate[0]));
    EXPECT(result == P101_FSM_RUN_ERROR);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_INVALID_TRANSITION_TABLE));
    fixture_destroy(&fixture);
}

static void test_reentrant_run(void)
{
    struct fixture                          fixture;
    struct callback_context                 context = {0};
    p101_fsm_run_result                     result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_reenter},
    };

    fixture_create(&fixture, "reentrant");
    context.fsm              = fixture.fsm;
    context.transitions      = transitions;
    context.transition_count = sizeof(transitions) / sizeof(transitions[0]);
    result                   = p101_fsm_run(fixture.fsm, NULL, NULL, &context, transitions, context.transition_count);
    EXPECT(result == P101_FSM_RUN_ERROR);
    EXPECT(context.nested_result == P101_FSM_RUN_ERROR);
    EXPECT(p101_error_is_error(fixture.fsm_err, P101_ERROR_USER, P101_FSM_ERROR_REENTRANT_RUN));
    fixture_destroy(&fixture);
}

static void test_balanced_tracing(void)
{
    struct fixture                          fixture;
    struct callback_context                 context = {0};
    p101_fsm_run_result                     result;
    static const struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT, STATE_A, state_exit},
    };

    fixture_create(&fixture, "trace");
    trace_entries = 0;
    trace_exits   = 0;
    p101_env_set_tracer(fixture.fsm_env, trace_enter);
    p101_env_set_exit_tracer(fixture.fsm_env, trace_exit);
    result = p101_fsm_run(fixture.fsm, NULL, NULL, &context, transitions, sizeof(transitions) / sizeof(transitions[0]));
    EXPECT(result == P101_FSM_RUN_EXITED);
    EXPECT(trace_entries > 0);
    EXPECT(trace_entries == trace_exits);
    fixture_destroy(&fixture);
}
