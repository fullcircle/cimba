# cmb_pool - Object Pool API

## Overview

`cmb_pool` is an object pool API for reusing allocated objects to avoid the overhead of repeated `malloc`/`free` calls. It wraps the internal `cmi_mempool` functionality to provide a simple get/return interface for fixed-size objects.

## API Reference

### Data Structures

#### `struct cmb_pool`

```c
struct cmb_pool {
    struct cmi_mempool mempool;  /* internal memory pool */
};
```

### Functions

#### `cmb_pool_create`

```c
struct cmb_pool *cmb_pool_create(size_t obj_sz, uint64_t initial_count);
```

Create a new object pool for objects of a fixed size.

**Parameters:**
- `obj_sz` - Size of each object in bytes. Must be a multiple of 8.
- `initial_count` - Minimum number of objects to pre-allocate.

**Returns:** Pointer to the created pool, or NULL on failure.

**Example:**
```c
struct cmb_pool *pool = cmb_pool_create(sizeof(struct visitor), 256);
```

---

#### `cmb_pool_destroy`

```c
void cmb_pool_destroy(struct cmb_pool *pool);
```

Destroy an object pool and free all associated memory. All objects previously allocated from this pool become invalid.

**Parameters:**
- `pool` - Pointer to the pool to destroy.

**Example:**
```c
cmb_pool_destroy(pool);
```

---

#### `cmb_pool_get` (inline)

```c
static inline void *cmb_pool_get(struct cmb_pool *pool);
```

Get an object from the pool. If no objects are available, the pool will allocate more memory automatically.

**Parameters:**
- `pool` - Pointer to the pool.

**Returns:** Pointer to an available object.

**Example:**
```c
struct visitor *vip = cmb_pool_get(visitor_pool);
```

---

#### `cmb_pool_return` (inline)

```c
static inline void cmb_pool_return(struct cmb_pool *pool, void *obj);
```

Return an object to the pool for later reuse. The object must have been obtained from the same pool via `cmb_pool_get()`.

**Parameters:**
- `pool` - Pointer to the pool.
- `obj` - Pointer to the object to return.

**Example:**
```c
cmb_pool_return(visitor_pool, vip);
```

---

## Usage Example

```c
#include <cimba.h>

#define MY_POOL_SIZE 256

struct my_object {
    int id;
    double value;
    // ... other fields
};

struct cmb_pool *my_pool;

void setup(void) {
    // Create pool for our objects
    my_pool = cmb_pool_create(sizeof(struct my_object), MY_POOL_SIZE);
}

void cleanup(void) {
    cmb_pool_destroy(my_pool);
}

struct my_object *create_object(int id) {
    struct my_object *obj = cmb_pool_get(my_pool);
    obj->id = id;
    obj->value = 0.0;
    return obj;
}

void destroy_object(struct my_object *obj) {
    cmb_pool_return(my_pool, obj);
}

int main(void) {
    setup();

    // Use objects from pool
    struct my_object *a = create_object(1);
    struct my_object *b = create_object(2);

    // Return to pool when done
    destroy_object(a);
    destroy_object(b);

    cleanup();
    return 0;
}
```

## Implementation Details

The `cmb_pool` is built on top of `cmi_mempool`, which:

- Allocates memory in large chunks aligned to system page size
- Maintains a free list of available objects
- Expands automatically when the free list is empty
- Uses the first 8 bytes of each object to store the next pointer (free list linkage)

## Memory Considerations

1. **Object size must be 8-byte aligned** - The pool uses the first 8 bytes of each object for the free list pointer.

2. **Pre-allocation** - The `initial_count` parameter specifies a minimum; actual pre-allocation may be larger due to page alignment requirements.

3. **No partial cleanup** - `cmb_pool_destroy()` frees all memory associated with the pool; any objects previously allocated from the pool are invalidated.

4. **Not thread-safe by default** - For multi-threaded use, ensure proper synchronization around pool access, or use separate pools per thread.

## See Also

- [cmb_process.h](cmb_process.html) - Process API
- [cmb_priorityqueue.h](cmb_priorityqueue.html) - Priority queue API
- [cmi_mempool.h](../src/cmi_mempool.h) - Internal memory pool implementation
