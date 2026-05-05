/**
 * @file cmb_pool.h
 * @brief A simple object pool for reusing allocated objects to avoid the
 *        overhead of repeated malloc/free calls.
 *
 * The pool allocates objects of a fixed size in large chunks to minimize
 * memory allocation overhead. Objects are acquired from the pool using
 * cmb_pool_get() and returned with cmb_pool_return().
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

#ifndef CIMBA_CMB_POOL_H
#define CIMBA_CMB_POOL_H

#include <stdint.h>

#include "cmb_assert.h"

#include "cmi_mempool.h"

/**
 * @brief A simple object pool for reusing allocated objects.
 *
 * The pool wraps a memory pool internally and provides get/return semantics
 * for objects of a fixed size.
 */
struct cmb_pool {
    struct cmi_mempool mempool;
};

/**
 * @brief Create a new object pool.
 *
 * @memberof cmb_pool
 * @param obj_sz Size of each object in bytes. Must be a multiple of 8.
 * @param initial_count Minimum number of objects to pre-allocate.
 * @return Pointer to the created pool.
 */
extern struct cmb_pool *cmb_pool_create(size_t obj_sz, uint64_t initial_count);

/**
 * @brief Destroy an object pool and free all associated memory.
 *
 * @memberof cmb_pool
 * @param pool Pointer to the pool to destroy.
 */
extern void cmb_pool_destroy(struct cmb_pool *pool);

/**
 * @brief Get an object from the pool.
 *
 * If no objects are available, the pool will allocate more memory.
 *
 * @memberof cmb_pool
 * @param pool Pointer to the pool.
 * @return Pointer to an available object.
 */
static inline void *cmb_pool_get(struct cmb_pool *pool)
{
    cmb_assert_debug(pool != NULL);
    return cmi_mempool_alloc(&pool->mempool);
}

/**
 * @brief Return an object to the pool for later reuse.
 *
 * @memberof cmb_pool
 * @param pool Pointer to the pool.
 * @param obj Pointer to the object to return.
 */
static inline void cmb_pool_return(struct cmb_pool *pool, void *obj)
{
    cmb_assert_debug(pool != NULL);
    cmi_mempool_free(&pool->mempool, obj);
}

#endif /* CIMBA_CMB_POOL_H */
