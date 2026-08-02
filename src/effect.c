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
#include <stdint.h>

struct stored_effect
{
    size_t kind_offset;
    size_t data_offset;
    size_t data_size;
};

struct p101_fsm_effect_batch
{
    struct stored_effect *effects;
    unsigned char        *bytes;
    size_t                maximum_effects;
    size_t                maximum_bytes;
    size_t                effect_count;
    size_t                byte_count;
};

static void batch_effect_handler(const struct p101_env *env, struct p101_error *err, void *context, const struct p101_fsm_effect *effect);
static void batch_reset(struct p101_fsm_effect_batch *batch);

struct p101_fsm_effect_batch *p101_fsm_effect_batch_create(const struct p101_env *env, struct p101_error *err, size_t maximum_effects, size_t maximum_bytes)
{
    struct p101_fsm_effect_batch *batch;

    P101_TRACE(env);
    batch = NULL;
    if(maximum_effects == 0U || maximum_bytes == 0U || maximum_effects > SIZE_MAX / sizeof(*batch->effects))
    {
        P101_ERROR_RAISE_USER(err, "Invalid FSM effect-batch capacity", P101_FSM_ERROR_EFFECT);
        goto done;
    }
    batch = (struct p101_fsm_effect_batch *)p101_calloc(env, err, 1U, sizeof(*batch));
    if(batch == NULL)
    {
        goto done;
    }
    batch->effects = (struct stored_effect *)p101_calloc(env, err, maximum_effects, sizeof(*batch->effects));
    batch->bytes   = (unsigned char *)p101_calloc(env, err, maximum_bytes, sizeof(*batch->bytes));
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
    P101_TRACE_EXIT(env);
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

struct p101_fsm_effect_sink p101_fsm_effect_batch_sink(struct p101_fsm_effect_batch *batch)
{
    struct p101_fsm_effect_sink sink;

    batch_reset(batch);
    sink.handle  = batch == NULL ? NULL : batch_effect_handler;
    sink.context = batch;
    return sink;
}

size_t p101_fsm_effect_batch_count(const struct p101_fsm_effect_batch *batch)
{
    return batch == NULL ? 0U : batch->effect_count;
}

int p101_fsm_effect_batch_finish_step(const struct p101_env *env, struct p101_error *err, struct p101_fsm_effect_batch *batch, const struct p101_fsm_step_result *result, struct p101_fsm_effect_sink *target)
{
    int deliver;
    int return_value;

    P101_TRACE(env);
    return_value = -1;
    if(batch == NULL || result == NULL || target == NULL || target->handle == NULL)
    {
        P101_ERROR_RAISE_USER(err, "Invalid FSM effect-batch delivery", P101_FSM_ERROR_EFFECT);
        goto done;
    }
    deliver = result->status == P101_FSM_STEP_TRANSITIONED || result->status == P101_FSM_STEP_EXITED;
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
            if(p101_error_has_error(err))
            {
                goto done;
            }
        }
    }
    return_value = 0;

done:
    batch_reset(batch);
    P101_TRACE_EXIT(env);
    return return_value;
}

static void batch_effect_handler(const struct p101_env *env, struct p101_error *err, void *context, const struct p101_fsm_effect *effect)
{
    struct p101_fsm_effect_batch *batch;
    size_t                        kind_size;
    size_t                        required;
    struct stored_effect         *stored;

    batch = (struct p101_fsm_effect_batch *)context;
    if(batch == NULL || effect == NULL || effect->kind == NULL || (effect->data == NULL && effect->data_size != 0U))
    {
        P101_ERROR_RAISE_USER(err, "Invalid staged FSM effect", P101_FSM_ERROR_EFFECT);
        return;
    }
    kind_size = p101_strlen(env, effect->kind) + 1U;
    if(kind_size == 0U || effect->data_size > SIZE_MAX - kind_size)
    {
        P101_ERROR_RAISE_USER(err, "FSM effect size is not representable", P101_FSM_ERROR_EFFECT);
        return;
    }
    required = kind_size + effect->data_size;
    if(batch->effect_count >= batch->maximum_effects || required > batch->maximum_bytes - batch->byte_count)
    {
        P101_ERROR_RAISE_USER(err, "FSM effect batch capacity exceeded", P101_FSM_ERROR_EFFECT_CAPACITY);
        return;
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
}

static void batch_reset(struct p101_fsm_effect_batch *batch)
{
    if(batch != NULL)
    {
        batch->effect_count = 0U;
        batch->byte_count   = 0U;
    }
}
