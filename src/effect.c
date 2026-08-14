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

#include "p101_fsm/errors.h"
#include "p101_fsm/fsm.h"
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_env/wrapper.h>
#include <stdint.h>

struct stored_effect
{
    size_t kind_offset;
    size_t data_offset;
    size_t data_size;
};

struct p101_fsm_effect_batch
{
    struct stored_effect           *effects;
    unsigned char                  *bytes;
    size_t                          maximum_effects;
    size_t                          maximum_bytes;
    size_t                          effect_count;
    size_t                          byte_count;
    uint64_t                        generation;
    struct p101_fsm_step_binding    admitted_binding;
    struct p101_fsm_step_result     admitted_result;
    p101_fsm_transition_disposition admitted_disposition;
    bool                            receipt_available;
};

static void                            batch_advance_generation(struct p101_fsm_effect_batch *batch);
static void                            batch_bind_receipt(struct p101_fsm_effect_batch *batch, const struct p101_fsm_step_receipt *receipt);
static void                            batch_effect_handler(const struct p101_env *env, struct p101_error *err, void *context, const struct p101_fsm_effect *effect);
static bool                            batch_receipt_matches(const struct p101_fsm_effect_batch *batch, const struct p101_fsm_step_receipt *receipt);
static void                            batch_reset(struct p101_fsm_effect_batch *batch);
static p101_fsm_transition_disposition step_disposition(const struct p101_fsm_step_result *result);

struct p101_fsm_effect_batch *p101_fsm_effect_batch_create(const struct p101_env *env, struct p101_error *err, size_t maximum_effects, size_t maximum_bytes)
{
    struct p101_fsm_effect_batch *batch;
    void                         *batch_storage;
    void                         *effect_storage;
    void                         *byte_storage;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, batch, NULL);
    batch = NULL;
    if(maximum_effects == 0U || maximum_bytes == 0U || maximum_effects > SIZE_MAX / sizeof(*batch->effects))
    {
        P101_ERROR_RAISE_USER(err, "Invalid FSM effect-batch capacity", P101_FSM_ERROR_EFFECT);
        goto done;
    }
    batch_storage = p101_calloc(env, err, 1U, sizeof(*batch));
    batch         = (struct p101_fsm_effect_batch *)batch_storage;
    if(batch == NULL)
    {
        goto done;
    }
    effect_storage = p101_calloc(env, err, maximum_effects, sizeof(*batch->effects));
    byte_storage   = p101_calloc(env, err, maximum_bytes, sizeof(*batch->bytes));
    batch->effects = (struct stored_effect *)effect_storage;
    batch->bytes   = (unsigned char *)byte_storage;
    if(batch->effects == NULL || batch->bytes == NULL)
    {
        p101_free(env, batch->bytes);
        p101_free(env, batch->effects);
        p101_free(env, batch);
        batch = NULL;
        goto done;
    }
    batch->maximum_effects = maximum_effects;
    batch->maximum_bytes   = maximum_bytes;

done:
    P101_WRAPPER_DONE(env);
    return batch;
}

void p101_fsm_effect_batch_destroy(const struct p101_env *env, struct p101_fsm_effect_batch **batch)
{
    P101_TRACE(env);
    if(batch != NULL && *batch != NULL)
    {
        p101_free(env, (*batch)->bytes);
        p101_free(env, (*batch)->effects);
        p101_free(env, *batch);
        *batch = NULL;
    }
    P101_TRACE_EXIT(env);
}

void p101_fsm_effect_batch_sink(struct p101_fsm_effect_batch *batch, struct p101_fsm_effect_sink *sink)
{
    if(sink != NULL)
    {
        batch_reset(batch);
        batch_advance_generation(batch);
        sink->handle  = batch == NULL ? NULL : batch_effect_handler;
        sink->context = batch;
    }
}

size_t p101_fsm_effect_batch_count(const struct p101_fsm_effect_batch *batch)
{
    return batch == NULL ? 0U : batch->effect_count;
}

int p101_fsm_effect_batch_finish_receipt(const struct p101_env *env, struct p101_error *err, struct p101_fsm_effect_batch *batch, const struct p101_fsm_step_receipt *receipt, struct p101_fsm_effect_sink *target)
{
    int  deliver;
    int  return_value;
    bool error_present;
    bool finish_admitted;
    bool receipt_admitted;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, return_value, -1);
    receipt_admitted = batch_receipt_matches(batch, receipt);
    finish_admitted  = false;
    return_value     = -1;
    if(!receipt_admitted || target == NULL || target->handle == NULL)
    {
        P101_ERROR_RAISE_USER(err, "Invalid or stale FSM step receipt", P101_FSM_ERROR_EFFECT);
        goto done;
    }
    finish_admitted = true;

    deliver = receipt->disposition == P101_FSM_TRANSITION_APPLIED_CHANGED;
    if(deliver)
    {
        for(size_t index = 0U; index < batch->effect_count; ++index)
        {
            const struct stored_effect *stored;
            struct p101_fsm_effect      effect;

            stored           = &batch->effects[index];
            effect.kind      = (const char *)&batch->bytes[stored->kind_offset];
            effect.data      = stored->data_size == 0U ? NULL : &batch->bytes[stored->data_offset];
            effect.data_size = stored->data_size;
            target->handle(env, err, target->context, &effect);
            error_present = p101_error_has_error(err);
            if(error_present)
            {
                goto done;
            }
        }
    }
    return_value = 0;

done:
    if(finish_admitted)
    {
        batch_reset(batch);
        batch_advance_generation(batch);
    }
    P101_WRAPPER_DONE(env);
    return return_value;
}

bool p101_fsm_step_receipt_effect(const struct p101_fsm_step_receipt *receipt, size_t index, struct p101_fsm_effect *effect)
{
    const struct p101_fsm_effect_batch *batch;
    const struct stored_effect         *stored;
    bool                                found;
    bool                                receipt_admitted;

    found = false;
    if(receipt == NULL || effect == NULL || receipt->effect_batch == NULL)
    {
        goto done;
    }
    batch            = receipt->effect_batch;
    receipt_admitted = batch_receipt_matches(batch, receipt);
    if(!receipt_admitted || index >= receipt->effect_count)
    {
        goto done;
    }

    stored            = &batch->effects[index];
    effect->kind      = (const char *)&batch->bytes[stored->kind_offset];
    effect->data      = stored->data_size == 0U ? NULL : &batch->bytes[stored->data_offset];
    effect->data_size = stored->data_size;
    found             = true;

done:
    return found;
}

p101_fsm_step_status p101_fsm_step_with_receipt(struct p101_fsm_info *info, void *arg, struct p101_fsm_effect_batch *batch, struct p101_fsm_step_receipt *receipt)
{
    struct p101_fsm_effect_sink sink;
    p101_fsm_step_status        status;

    status = P101_FSM_STEP_ERROR;
    if(batch == NULL || receipt == NULL)
    {
        goto done;
    }

    p101_fsm_effect_batch_sink(batch, &sink);
    status                           = p101_fsm_step(info, arg, &sink, &receipt->result);
    receipt->binding.machine         = info;
    receipt->binding.argument        = arg;
    receipt->binding.sequence        = receipt->result.sequence;
    receipt->binding.from_state      = receipt->result.from_state;
    receipt->binding.attempted_state = receipt->result.attempted_state;
    receipt->disposition             = step_disposition(&receipt->result);
    receipt->effect_batch            = batch;
    receipt->effect_generation       = batch->generation;
    receipt->effect_count            = batch->effect_count;
    batch_bind_receipt(batch, receipt);

done:
    return status;
}

static void batch_effect_handler(const struct p101_env *env, struct p101_error *err, void *context, const struct p101_fsm_effect *effect)
{
    struct p101_fsm_effect_batch *batch;
    size_t                        kind_length;
    size_t                        kind_size;
    size_t                        required;
    struct stored_effect         *stored;

    batch = (struct p101_fsm_effect_batch *)context;
    if(batch == NULL || effect == NULL || effect->kind == NULL || (effect->data == NULL && effect->data_size != 0U))
    {
        P101_ERROR_RAISE_USER(err, "Invalid staged FSM effect", P101_FSM_ERROR_EFFECT);
        goto p101_single_exit_;
    }
    kind_length = p101_strlen(env, effect->kind);
    kind_size   = kind_length + 1U;
    if(effect->data_size > SIZE_MAX - kind_size)
    {
        P101_ERROR_RAISE_USER(err, "FSM effect size is not representable", P101_FSM_ERROR_EFFECT);
        goto p101_single_exit_;
    }
    required = kind_size + effect->data_size;
    if(batch->effect_count >= batch->maximum_effects || required > batch->maximum_bytes - batch->byte_count)
    {
        P101_ERROR_RAISE_USER(err, "FSM effect batch capacity exceeded", P101_FSM_ERROR_EFFECT_CAPACITY);
        goto p101_single_exit_;
    }

    stored              = &batch->effects[batch->effect_count];
    stored->kind_offset = batch->byte_count;
    p101_memcpy(env, &batch->bytes[batch->byte_count], effect->kind, kind_size);
    batch->byte_count += kind_size;
    stored->data_offset = batch->byte_count;
    stored->data_size   = effect->data_size;
    if(effect->data_size > 0U)
    {
        p101_memcpy(env, &batch->bytes[batch->byte_count], effect->data, effect->data_size);
        batch->byte_count += effect->data_size;
    }
    batch->effect_count++;

p101_single_exit_:
    return;
}

static void batch_reset(struct p101_fsm_effect_batch *batch)
{
    if(batch != NULL)
    {
        batch->effect_count      = 0U;
        batch->byte_count        = 0U;
        batch->receipt_available = false;
    }
}

static void batch_advance_generation(struct p101_fsm_effect_batch *batch)
{
    if(batch != NULL)
    {
        batch->generation++;
        if(batch->generation == 0U)
        {
            batch->generation = 1U;
        }
    }
}

static void batch_bind_receipt(struct p101_fsm_effect_batch *batch, const struct p101_fsm_step_receipt *receipt)
{
    if(batch != NULL && receipt != NULL)
    {
        batch->admitted_binding     = receipt->binding;
        batch->admitted_result      = receipt->result;
        batch->admitted_disposition = receipt->disposition;
        batch->receipt_available    = true;
    }
}

static bool batch_receipt_matches(const struct p101_fsm_effect_batch *batch, const struct p101_fsm_step_receipt *receipt)
{
    bool matches;

    matches = false;
    if(batch != NULL && receipt != NULL && batch->receipt_available && receipt->effect_batch == batch && receipt->effect_generation == batch->generation && receipt->effect_count == batch->effect_count &&
       receipt->binding.machine == batch->admitted_binding.machine && receipt->binding.argument == batch->admitted_binding.argument && receipt->binding.sequence == batch->admitted_binding.sequence &&
       receipt->binding.from_state == batch->admitted_binding.from_state && receipt->binding.attempted_state == batch->admitted_binding.attempted_state && receipt->disposition == batch->admitted_disposition &&
       receipt->result.status == batch->admitted_result.status && receipt->result.sequence == batch->admitted_result.sequence && receipt->result.from_state == batch->admitted_result.from_state &&
       receipt->result.attempted_state == batch->admitted_result.attempted_state && receipt->result.next_state == batch->admitted_result.next_state && receipt->result.refusal == batch->admitted_result.refusal)
    {
        matches = true;
    }

    return matches;
}

static p101_fsm_transition_disposition step_disposition(const struct p101_fsm_step_result *result)
{
    p101_fsm_transition_disposition disposition;

    disposition = P101_FSM_TRANSITION_ERROR;
    if(result == NULL)
    {
        goto done;
    }

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch(result->status)    // GCOVR_EXCL_BR_LINE: default protects against an invalid enum representation.
    {
        case P101_FSM_STEP_TRANSITIONED:
            disposition = P101_FSM_TRANSITION_APPLIED_CHANGED;
            break;
        case P101_FSM_STEP_PAUSED:
            disposition = P101_FSM_TRANSITION_APPLIED_NO_CHANGE;
            break;
        case P101_FSM_STEP_EXITED:
            disposition = result->refusal == P101_FSM_REFUSAL_NONE ? P101_FSM_TRANSITION_APPLIED_CHANGED : P101_FSM_TRANSITION_REFUSED;
            break;
        case P101_FSM_STEP_REFUSED:
            if(result->refusal == P101_FSM_REFUSAL_UNKNOWN_TRANSITION && result->next_state != result->attempted_state)
            {
                disposition = P101_FSM_TRANSITION_APPLIED_CHANGED;
            }
            else
            {
                disposition = P101_FSM_TRANSITION_REFUSED;
            }
            break;
        case P101_FSM_STEP_ERROR:
        default:
            disposition = P101_FSM_TRANSITION_ERROR;
            break;
    }
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

done:
    return disposition;
}
