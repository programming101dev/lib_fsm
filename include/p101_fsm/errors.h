#ifndef LIBP101_FSM_ERRORS_H
#define LIBP101_FSM_ERRORS_H

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

typedef enum
{
    P101_FSM_ERROR_INVALID_ARGUMENT = 1,
    P101_FSM_ERROR_INVALID_TRANSITION_TABLE,
    P101_FSM_ERROR_UNKNOWN_TRANSITION,
    P101_FSM_ERROR_HANDLER_LOOP,
    P101_FSM_ERROR_INVALID_DECISION,
    P101_FSM_ERROR_REENTRANT_OPERATION,
    P101_FSM_ERROR_EFFECT,
    P101_FSM_ERROR_EFFECT_CAPACITY,
    P101_FSM_ERROR_SEQUENCE_EXHAUSTED,
} p101_fsm_error;

#endif    // LIBP101_FSM_ERRORS_H
