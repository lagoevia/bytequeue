#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stdint.h>

/* The solution assumes that bytes in memory are laid out as they are assigned
 * arithmetically: this doesn't hold true for little endian architectures; in
 * those cases, we need to have helper functions to read-from and write-to
 * memory correctly. */
#define IS_LITTLE_ENDIAN (*(unsigned char*)&(uint16_t){1})

inline void __swap(uint8_t* _a, uint8_t* _b) {
    uint8_t tmp = *_a;
    *_a = *_b;
    *_b = tmp;
}

inline uint32_t* __swap_endianness(uint32_t* x) {
    __swap((uint8_t*)(x) + 0, (uint8_t*)(x) + 3);
    __swap((uint8_t*)(x) + 1, (uint8_t*)(x) + 2);
    return x;
}

typedef uint8_t byte;
typedef uint32_t queue_t;
typedef uint32_t entry_block_t;
typedef uint16_t entry_t;

#ifndef MAX_QUEUE_MEMORY
#define MAX_QUEUE_MEMORY (2048)
#endif
#define MAX_ACTIVE_QUEUES (64)
#define NUM_ACTIVE_QUEUES (data[0])
#define QUEUE_BYTE_SIZE (3)
#define MONITOR_SEG_LEN (1 + (QUEUE_BYTE_SIZE * MAX_ACTIVE_QUEUES))
#define ENTRY_SEG_LEN (MAX_QUEUE_MEMORY - MONITOR_SEG_LEN)
#define ENTRY_BIT_SIZE (10)
#define MAX_ENTRIES (ENTRY_SEG_LEN * 8 / ENTRY_BIT_SIZE)
#define ENTRY_BLOCK_BIT_SIZE (32)

byte data[MAX_QUEUE_MEMORY] = {0};

// --- Queue

#define QUEUE_MASK (0x7FF)
#define QUEUE_BIT_SIZE (QUEUE_BYTE_SIZE * 8)
#define QUEUE_LENGTH_BIT_SIZE (11)
#define QUEUE_LENGTH_SHIFT_ADJUSTMENT (QUEUE_BIT_SIZE - QUEUE_LENGTH_BIT_SIZE)
#define QUEUE_BASE_BIT_SIZE (11)
#define QUEUE_BASE_SHIFT_ADJUSTMENT \
    (QUEUE_BIT_SIZE - QUEUE_LENGTH_BIT_SIZE - QUEUE_BASE_BIT_SIZE)

inline void set_queue_length(queue_t* q, const uint16_t len) {
    *q = (*q & ~(QUEUE_MASK << QUEUE_LENGTH_SHIFT_ADJUSTMENT)) |
         (len << QUEUE_LENGTH_SHIFT_ADJUSTMENT);
}

inline uint16_t get_queue_length(const queue_t* const q) {
    return ((*q >> QUEUE_LENGTH_SHIFT_ADJUSTMENT) & QUEUE_MASK);
}

inline void set_queue_base(queue_t* q, const uint16_t base) {
    *q = (*q & ~(QUEUE_MASK << QUEUE_BASE_SHIFT_ADJUSTMENT)) |
         (base << QUEUE_BASE_SHIFT_ADJUSTMENT);
}

inline uint16_t get_queue_base(const queue_t* const q) {
    return ((*q >> QUEUE_BASE_SHIFT_ADJUSTMENT) & QUEUE_MASK);
}

inline void set_queue_valid(queue_t* q) { *q |= 0x1; }

inline void set_queue_invalid(queue_t* q) { *q &= (~(0 << 1)); }

inline uint8_t is_queue_valid(const queue_t* const q) { return (*q & 0x1); }

inline queue_t* get_queue_ptr(const uint16_t qid) {
    // TODO: No shift of queue? Also, no memory issues with endianness?
    return (queue_t*)&data[QUEUE_BYTE_SIZE * qid + 1];
}

// --- Entry <-> Block

#define ENTRY_MASK (0x3FF)
#define ENTRY_REAL_END_BIT(eid) (ENTRY_BIT_SIZE * ((eid) + 1) - 1)
#define ENTRY_REAL_START_BIT(eid) \
    (ENTRY_REAL_END_BIT(eid) - ENTRY_BIT_SIZE + (((eid) == 0) ? 1 : 0))
#define ENTRY_BYTE_END_BIT(eid) (8 * ((eid) + 1) - 1)
#define ENTRY_BYTE_START_BIT(eid) \
    (ENTRY_BYTE_END_BIT(eid) - 8 + (((eid) == 0) ? 1 : 0))
#define ENTRY_BLOCK_START_BIT(eid) (ENTRY_BYTE_START_BIT(eid))
#define ENTRY_BLOCK_END_BIT(eid) \
    (ENTRY_BLOCK_START_BIT(eid) + ENTRY_BLOCK_BIT_SIZE - (((eid) == 0) ? 1 : 0))

void debug_print_eblock_values(const uint16_t eid) {
    uint16_t info[] = {ENTRY_REAL_END_BIT(eid),
                       ENTRY_REAL_START_BIT(eid),
                       ENTRY_BYTE_END_BIT(eid),
                       ENTRY_BYTE_START_BIT(eid),
                       ENTRY_BLOCK_START_BIT(eid),
                       ENTRY_BLOCK_END_BIT(eid),
                       ENTRY_BLOCK_END_BIT(eid) - ENTRY_REAL_START_BIT(eid) -
                           ENTRY_BIT_SIZE + ((eid == 0) ? 1 : 0)};

    printf(
        "ENTRY_REAL_END_BIT\nENTRY_REAL_START_BIT\nENTRY_BYTE_END_BIT\nENTRY_"
        "BYTE_START_BIT\nENTRY_BLOCK_START_BIT\nENTRY_BLOCK_END_BIT\nSHIFT\n");
    printf("Debug info for entry %u below:\n", eid);
    for (int i = 0; i < 7; i++) {
        printf("%u ", info[i]);
    }
    puts("");
}

inline entry_block_t* get_entry_block_ptr(const uint16_t eid) {
    entry_block_t* block =
        (entry_block_t*)&data[MONITOR_SEG_LEN +
                              (ENTRY_REAL_START_BIT(eid) / 8)];
#ifdef IS_LITTLE_ENDIAN
    __swap_endianness(block);
#endif
    return block;
}

inline entry_block_t get_entry_block(const uint16_t eid) {
    entry_block_t block = *(
        entry_block_t*)&data[MONITOR_SEG_LEN + (ENTRY_REAL_START_BIT(eid) / 8)];
#ifdef IS_LITTLE_ENDIAN
    __swap_endianness(&block);
#endif
    return block;
}

inline entry_t read_entry_from_block(const entry_block_t block,
                                     const uint16_t eid) {
    // Block is "coming in well", so no need to adjust it
    uint16_t shift = ENTRY_BLOCK_END_BIT(eid) - ENTRY_REAL_START_BIT(eid) -
                     ENTRY_BIT_SIZE + ((eid == 0) ? 1 : 0);
    return (entry_t)((block >> shift) & ENTRY_MASK);
}

inline void write_entry_to_block(entry_block_t* block, const entry_t entry,
                                 const uint16_t eid) {
    // Block is "coming in well", but after the assignment it must be adjusted
    // for memory representation
    uint16_t shift = ENTRY_BLOCK_END_BIT(eid) - ENTRY_REAL_START_BIT(eid) -
                     ENTRY_BIT_SIZE + ((eid == 0) ? 1 : 0);
    *block =
        (*block & ~(ENTRY_MASK << shift)) | ((entry_block_t)entry << shift);
#ifdef IS_LITTLE_ENDIAN
    __swap_endianness(block);
#endif
}

inline entry_t read_entry_from_id(const uint16_t eid) {
    return read_entry_from_block(get_entry_block(eid), eid);
}

inline void write_entry_to_id(const entry_t entry, const uint16_t eid) {
    write_entry_to_block(get_entry_block_ptr(eid), entry, eid);
}

// --- Entry specific

#define INVALID_ENTRY (MAX_ENTRIES)
#define ENTRY_VALUE_BIT_SIZE (8)
#define ENTRY_VALUE_MASK (0x0FF)
#define ENTRY_VALID_BIT_SIZE (1)
#define ENTRY_VALID_SHIFT_ADJUSTMENT (ENTRY_VALUE_BIT_SIZE)
#define ENTRY_BASE_BIT_SIZE (1)
#define ENTRY_BASE_SHIFT_ADJUSTMENT \
    (ENTRY_VALID_BIT_SIZE + ENTRY_VALUE_BIT_SIZE)

inline void set_entry_valid(entry_t* e) {
    *e |= (1 << ENTRY_VALID_SHIFT_ADJUSTMENT);
}

inline void set_entry_invalid(entry_t* e) {
    *e &= (~(1 << ENTRY_VALID_SHIFT_ADJUSTMENT));
}

inline uint8_t is_entry_valid(const entry_t e) {
    return ((e >> ENTRY_VALID_SHIFT_ADJUSTMENT) & 0x1);
}

inline byte get_entry_value(const entry_t e) { return (e & ENTRY_VALUE_MASK); }

inline void set_entry_value(entry_t* e, byte v) {
    *e = ((*e) & (~ENTRY_VALUE_MASK)) | v;
}

inline uint8_t is_entry_queue_base(const entry_t e) {
    return ((e >> ENTRY_BASE_SHIFT_ADJUSTMENT) & 0x1);
}

inline void set_entry_queue_base_on(entry_t* e) {
    *e |= 1 << ENTRY_BASE_SHIFT_ADJUSTMENT;
}

inline void set_entry_queue_base_off(entry_t* e) {
    *e &= (~(1 << ENTRY_BASE_SHIFT_ADJUSTMENT));
}

// --- Actual API

/** User function called when a queue operation fails because of insufficient
 * memory. Note: This function should not return. */
extern void on_out_of_memory();
/** User function called when a queue operation fails because of an illegal
 * request (popping an empty queue, etc). Note: This function should not return.
 */
extern void on_illegal_operation();

/** Creates a queue and returns the handle to it. Note: there is no guarantee
 * that the queue actually has an entry available for it. Fails if: a) all
 * MAX_QUEUES are already taken. */
queue_t* create_queue() {
    for (uint16_t qid = 0; qid < MAX_ACTIVE_QUEUES; qid++) {
        queue_t* q = get_queue_ptr(qid);
        if (q && !is_queue_valid(q)) {
            set_queue_valid(q);
            NUM_ACTIVE_QUEUES += 1;
            for (uint16_t base = 0; base < MAX_ENTRIES; base++) {
                entry_t e = read_entry_from_id(base);
                if (!is_entry_valid(e) && !is_entry_queue_base(e)) {
                    set_entry_queue_base_on(&e);
                    write_entry_to_id(e, base);
                    set_queue_base(q, base);
                    return q;
                }
            }
            // All space is taken, but the room for the queue itself was
            // available Mark this queue's base as inactive entry, then handle
            // finding it a base on enqueue
            set_queue_base(q, INVALID_ENTRY);
            return q;
        }
    }
    // There were no queues available - illegal op
    on_illegal_operation();
}

/** Destroys a queue and marks all of its entries as invalid. Note: deletion
 * here means invalidating the queue, and setting it length to 0 and its base to
 * INVALID_ENTRY. Fails if: a) q is NULL. b) the queue handle pointed at by q is
 * not for a valid queue. */
void destroy_queue(queue_t* q) {
    // ensure queue is valid
    if (!q || !is_queue_valid(q)) {
        on_illegal_operation();
    }

    // mark all elements in queue inactive, also mark first as non-head
    uint16_t base = get_queue_base(q);
    uint16_t len = get_queue_length(q);

    for (uint16_t i = base; i < base + len; i++) {
        entry_t e = read_entry_from_id(i);
        if (i == base) {
            set_entry_queue_base_off(&e);
        }
        set_entry_invalid(&e);
        write_entry_to_id(e, i);
    }

    // mark queue length as 0, set queue base to invalid entry, mark queue
    // invalid, set q to be NULL
    set_queue_length(q, 0);
    set_queue_base(q, INVALID_ENTRY);
    set_queue_invalid(q);
    q = NULL;
}

/** Enqueues (adds) a byte into a specific queue. Note: (internal) shifting may
 * be necessary under some conditions; bases may be assigned to queues here.
 * Fails if: a) q is NULL. b) the queue handle pointed at by q is not for a
 * valid queue. c) looked at base, then right, then left, and still didn't
 * insert: out of memory. */
void enqueue_byte(queue_t* q, byte b) {
    // ensure queue is valid
    if (!q || !is_queue_valid(q)) {
        on_illegal_operation();
    }

    uint16_t base = get_queue_base(q);
    uint16_t len = get_queue_length(q);
    bool isDone = false;

    // try the base itself
    entry_t e = read_entry_from_id(base);
    if (!is_entry_valid(e)) {
        // base is available
        set_entry_valid(&e);
        set_entry_value(&e, b);
        write_entry_to_id(e, base);
        set_queue_length(q, len + 1);
        isDone = true;
    }

    if (!isDone) {
        // try right
        uint16_t start = base + len;
        for (uint16_t i = start; i < MAX_ENTRIES; i++) {
            entry_t t = read_entry_from_id(i);
            if (!is_entry_valid(read_entry_from_id(i))) {
                uint16_t gap = i - start;
                if (gap == 0) {
                    // can add here normally (i)
                    e = read_entry_from_id(i);
                    set_entry_valid(&e);
                    set_entry_value(&e, b);
                    write_entry_to_id(e, i);
                    set_queue_length(q, len + 1);
                } else if (len == 0) {
                    // empty queue with gap - relocate and update entries
                    entry_t old = read_entry_from_id(base);
                    set_entry_queue_base_off(&e);
                    write_entry_to_id(e, base);
                    e = read_entry_from_id(i);
                    set_entry_valid(&e);
                    set_entry_queue_base_on(&e);
                    set_entry_value(&e, b);
                    write_entry_to_id(e, i);
                    set_queue_length(q, len + 1);
                    set_queue_base(q, i);
                } else {
                    // nonempty queue with gap - shifting necessary
                    for (uint16_t j = i; j > start; j--) {
                        // j - 1's base will be equal to j's base
                        // j - 1's value will be equal to j's value
                        // so, copy the whole thing over
                        e = read_entry_from_id(j - 1);
                        if (is_entry_queue_base(e)) {
                            // if this was a head, update it's queue base
                            for (uint16_t qid = 0; qid < MAX_ACTIVE_QUEUES;
                                 qid++) {
                                queue_t* other = get_queue_ptr(qid);
                                if (get_queue_base(other) == (j - 1)) {
                                    set_queue_base(other, j);
                                    break;
                                }
                            }
                        }
                        write_entry_to_id(e, j);
                    }
                    // start is available now, but it still contains the "old"
                    // start contents - clear them (base) just in case
                    e = read_entry_from_id(start);
                    set_entry_queue_base_off(&e);
                    set_entry_value(&e, b);
                    write_entry_to_id(e, start);
                    set_queue_length(q, len + 1);
                }
                isDone = true;
                break;
            }
        }
    }

    // try left
    if (!isDone) {
        if (base != 0) {
            uint16_t start = base - 1;
            for (int i = start; i >= 0; i--) {
                if (!is_entry_valid(read_entry_from_id(i))) {
                    uint16_t gap = start - i;
                    if (len == 0) {
                        // case shared between gap 0 len 0 and gap != 0 and len
                        // 0 (gap == 0 and len == 0) empty queue, just need to
                        // adjust base/updae entries and insert at i (gap != 0
                        // and len == 0) empty queue, need to adjust base/update
                        // entries and insert at i (just happens to be more than
                        // one gap away)
                        entry_t old = read_entry_from_id(base);
                        set_entry_queue_base_off(&old);
                        write_entry_to_id(old, base);
                        e = read_entry_from_id(i);
                        set_entry_valid(&e);
                        set_entry_queue_base_on(&e);
                        set_entry_value(&e, b);
                        write_entry_to_id(e, i);
                        set_queue_base(q, i);
                        set_queue_length(q, len + 1);
                    } else {
                        // case shared between gap 0 len != 0 and gap != 0 and
                        // len != 0 gap 0 len != 0: non-empty queue found a spot
                        // immediately to the left of its current base shift
                        // everything by one, update current queue base, and
                        // insert at the former "tail" gap != 0 len != 0: non
                        // empty queue, but gap has some other queue(s)' info in
                        // the middle this is the same as gap == 0 len != 0,
                        // just so happens that the initial shift is moving
                        // other queues' data as well
                        for (int j = i; j < base + len - 1; j++) {
                            // j + 1's info is now on j - copy it over
                            e = read_entry_from_id(j + 1);
                            if (is_entry_queue_base(e)) {
                                // if this was a head, update it's queue base
                                for (uint16_t qid = 0; qid < MAX_ACTIVE_QUEUES;
                                     qid++) {
                                    queue_t* other = get_queue_ptr(qid);
                                    if (get_queue_base(other) == (j + 1)) {
                                        set_queue_base(other, j);
                                        break;
                                    }
                                }
                            }
                            write_entry_to_id(e, j);
                        }
                        // former tail's info is still on there, but no need to
                        // wipe as it is guaranteed not to be a head
                        e = read_entry_from_id(base + len - 1);
                        set_entry_value(&e, b);
                        write_entry_to_id(e, base + len - 1);
                        set_queue_base(q, i);
                        set_queue_length(q, len + 1);
                    }
                    isDone = true;
                    break;
                }
            }
        }
    }

    if (!isDone) {
        // we've tried everything and didn't succeed, so no memory left
        on_out_of_memory();
    }
}

/** Dequeues (removes) a byte from a specific queue. Note: this operation is
 * really just updating the base entry and the queue handle. Fails if: a) q is
 * NULL. b) the queue handle pointed at by q is not for a valid queue. */
byte dequeue_byte(queue_t* q) {
    // ensure queue is valid
    if (!q || !is_queue_valid(q)) {
        on_illegal_operation();
    }

    uint16_t base = get_queue_base(q);
    uint16_t len = get_queue_length(q);

    if (len == 0)    // empty queue
        on_illegal_operation();

    entry_t e = read_entry_from_id(base);
    uint8_t value = get_entry_value(e);
    set_entry_invalid(&e);
    set_entry_queue_base_off(&e);
    write_entry_to_id(e, base);
    if (len > 1) {
        // if there's more entries that this queue owns to the right, adjust
        set_queue_base(q, base + 1);
        entry_t next = read_entry_from_id(base + 1);
        set_entry_queue_base_off(&next);
        write_entry_to_id(next, base + 1);
    } else {
        // this was the sole entry of the queue
        set_queue_base(q, INVALID_ENTRY);
    }

    set_queue_length(q, len - 1);
    return value;
}

#endif