#include "p101_fsm/errors.h"
#include "p101_fsm/fsm.h"

int main()
{
    p101_fsm_step_status status = P101_FSM_STEP_EXITED;
    p101_fsm_error       error  = P101_FSM_ERROR_INVALID_ARGUMENT;

    return status == P101_FSM_STEP_EXITED && error == P101_FSM_ERROR_INVALID_ARGUMENT ? 0 : 1;
}
