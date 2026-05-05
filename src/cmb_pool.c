/*
 * cmb_pool.c - Object pool implementation.
 *
 * Copyright (c) Asbjørn M. Bonvik 2025-26.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>

#include "cmb_pool.h"
#include "cmi_mempool.h"

struct cmb_pool *cmb_pool_create(const size_t obj_sz, const uint64_t initial_count)
{
    cmb_assert_release(obj_sz % 8u == 0);
    cmb_assert_release(initial_count > 0u);

    struct cmb_pool *pool = malloc(sizeof(*pool));
    if (pool == NULL) {
        return NULL;
    }

    cmi_mempool_initialize(&pool->mempool, obj_sz, initial_count);

    return pool;
}

void cmb_pool_destroy(struct cmb_pool *pool)
{
    if (pool != NULL) {
        cmi_mempool_destroy(&pool->mempool);
        free(pool);
    }
}
