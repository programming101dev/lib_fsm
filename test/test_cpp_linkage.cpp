#include "p101_fsm/errors.h"
#include "p101_fsm/fsm.h"

int main()
{
    p101_fsm_run_result result = P101_FSM_RUN_EXITED;
    p101_fsm_error      error  = P101_FSM_ERROR_INVALID_ARGUMENT;

    return result == P101_FSM_RUN_EXITED && error == P101_FSM_ERROR_INVALID_ARGUMENT ? 0 : 1;
}
