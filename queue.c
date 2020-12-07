#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

// length here means number of entries; offset here means entry id, used to access individual entries from the data segment.
// queue: length(11) offset(11) pad(1) valid(1) = 3B
// entry: base(1) valid(1) value(8) = 2B
// monitor: ACTIVE_QUERIES(8) | queues... = 1B + 64(3B) = 193B

#define MAX_MEMORY                  2048
#define MONITOR_SEG_LEN             193
#define DATA_SEG_LEN                1855 // max - monitor
#define MAX_ENTRIES                 1484 // data_seg_len * 8 / 9

// Queue
#define MAX_QUEUES                   64
#define NUM_ACTIVE_QUEUES           data[0]
#define SET_QUEUE_LENGTH(q, n)      ( (q) = ( (q) & (~(0x7FF << 13)) ) | ((n) << 13) )
#define GET_QUEUE_LENGTH(q)         ( ((q) >> 13) & (0x7FF) )
#define SET_QUEUE_BASE(q, n)        ( (q) = ( (q) & (~(0x7FF << 13)) ) | ((n) << 2) )
#define GET_QUEUE_BASE(q)           ( ((q) >> 2) & (0x7FF) )
#define SET_QUEUE_VALID(q)          ( (q) |= 0x1 )
#define SET_QUEUE_INVALID(q)        ( (q) = (q) & (~(0 << 1)) )
#define IS_QUEUE_VALID(q)           ( (q) & 0x1 )
#define GET_QUEUE(i)                ( (queue_t*)&data[(3 * (i)) + 1] )

// Entry (Block)
#define ENTRY_MASK                  0x3FF
#define GET_ENTRY_BLOCK_PTR(i)          ( (entry_block_t *)&data[(10*((i))/8) + MONITOR_SEG_LEN] )
#define GET_ENTRY_BLOCK(i)          ( *(entry_block_t *)&data[(10*((i))/8) + MONITOR_SEG_LEN] )
inline uint8_t GET_REAL_START_BIT(i) { return (((i) == 0) ? 0 : 8 * (10 * (i) / 8)); }
inline uint8_t GET_REAL_END_BIT(i) { return (32 + 8 * ((10 * (i)) / 8)); }
inline uint8_t RSHIFT_ENTRY(i) { return ((GET_REAL_END_BIT(i)) - 10 - (10 * (i))); }
inline uint8_t LSHIFT_ENTRY(i) { return (10 * (i)-(GET_REAL_START_BIT((i)))); }
#define READ_ENTRY_FROM_BLOCK(b,i)  ( (entry_t)(( (b) >> RSHIFT_ENTRY((i)) ) & ENTRY_MASK ))
#define WRITE_ENTRY_TO_BLOCK(b,e,i) ( *((b)) = ( ( (*b) & ( ~(ENTRY_MASK << 22) ) ) | ( (e) << RSHIFT_ENTRY((i)) ) ) )
// read variants should NOT modify the underlying block
// they should just get the block's value, and use that to shift
#define READ_ENTRY(i)               ( (entry_t)(( ((GET_ENTRY_BLOCK((i)))) >> RSHIFT_ENTRY((i)) ) & ENTRY_MASK ))
#define WRITE_ENTRY(e,i)            ( *(GET_ENTRY_BLOCK_PTR((i))) = ( ((GET_ENTRY_BLOCK((i)))) & ( ~(ENTRY_MASK << 22) >> LSHIFT_ENTRY((i))) ) | ( (e) << RSHIFT_ENTRY((i)) ) )



// You'll read a LE number representation of the accurate BE number
// Get the block, and swap it (to BE)
// Then, the block will hold its accurate value


// You'll have a block, get its value in BE, and modify it accordingly
// Now you need to write this so that it's in BE in memory
// So, swap the end result of the assign to LE, so that it writes it ultimately as desired (BE) on disk


// Entry ("isolated")
#define INVALID_ENTRY               MAX_ENTRIES
#define SET_ENTRY_VALID(e)          ( (e) |= (1 << 8) )
#define SET_ENTRY_INVALID(e)        ( (e) &= (1 << 8) )
#define IS_ENTRY_VALID(e)           ( ((e) >> 8) & (0x1) )
#define SET_ENTRY_VALUE(e,n)        ( (e) = ((e) & (~0xFF)) | (n) )
#define GET_ENTRY_VALUE(e)          ( (uint8_t)(( (e) & (0x00FF) ) ))
#define IS_ENTRY_QUEUE_BASE(e)     ( ((e) >> 9) & (0x1) )
#define SET_ENTRY_QUEUE_BASE_ON(e) ( (e) |= (1 << 9) )
#define SET_ENTRY_QUEUE_BASE_OFF(e) ( (e) &= (~(1 << 9)) )

// Misc
#define ADD_TO_QUEUE(q, l, e, i, b)  SET_ENTRY_VALID((e)); SET_ENTRY_VALUE((e), (b)); WRITE_ENTRY((e), (i)); SET_QUEUE_LENGTH((q), (l + 1));

unsigned char data[MAX_MEMORY] = {0};
enum { FALSE = 0, TRUE = 1};
//extern void on_out_of_memory();
//extern void on_illegal_operation();

void on_out_of_memory() {
    printf("BAD 1");
    exit(1);
}
void on_illegal_operation() {
    printf("BAD 2");
    exit(2);
}

typedef uint32_t queue_t;
typedef uint16_t entry_t;
typedef uint32_t entry_block_t;

queue_t * create_queue() {
    for (uint16_t qid; qid < MAX_QUEUES; qid++) {
        queue_t* q = GET_QUEUE(qid);
        if (!IS_QUEUE_VALID(*q)) {
            SET_QUEUE_VALID(*q);
            NUM_ACTIVE_QUEUES += 1;
            // find the next available base to mark it for this queue
            uint8_t foundBase = FALSE;
            for (uint16_t eid = 0; eid < MAX_ENTRIES; eid++) {
                entry_t e = READ_ENTRY(eid); // don't do this once it works
                if (!IS_ENTRY_VALID(e)) {
                    SET_QUEUE_BASE(*q, eid);
                    foundBase = TRUE;
                    // don't mark entry with queue base yet - the base may change on enqueue (if this turns busy by then)
                    // don't want to report something as a base until it's properly allocated as one
                    break;
                }
            }
            // we *may* get to here without an assigned base - if so, that doesn't necessarily mean we're out of memory
            // everything may be busy now but by the time the first entry is enqueued to this queue, there may be spaces available
            // so, don't fail for out of memory: rather, just mark the base to be invalid - at the time of enqueue, will handle finding space
            if (!foundBase) {
                SET_QUEUE_BASE(*q, INVALID_ENTRY);
            }

            return q;
        }
    }
    // all are taken -> interpret as illegal op
    on_illegal_operation();
}

void destroy_queue(queue_t * q) {
    if (q == NULL || !IS_QUEUE_VALID(*q)) // invalid queue
        on_illegal_operation();
    
    uint16_t base = GET_QUEUE_BASE(*q);
    uint16_t len = GET_QUEUE_LENGTH(*q);

    // invalidate all entries in queue
    for (uint16_t eid = base; eid < base + len; eid++) {
        entry_t e = READ_ENTRY(eid);
        SET_ENTRY_INVALID(e);
        WRITE_ENTRY(e, eid);
    }
    
    // clean up queue
    SET_QUEUE_BASE(*q, INVALID_ENTRY);
    SET_QUEUE_LENGTH(*q, 0);
    SET_QUEUE_INVALID(*q);
    q = NULL;
    NUM_ACTIVE_QUEUES -= 1;
}

void enqueue_byte(queue_t * q, unsigned char b) {
    if (q == NULL || !IS_QUEUE_VALID(*q)) // invalid queue
        on_illegal_operation();

    uint16_t base = GET_QUEUE_BASE(*q);
    uint16_t len = GET_QUEUE_LENGTH(*q);
    uint16_t end = base + len;
    uint8_t isDone = FALSE;

    // try to look right for a spot
    for (uint16_t eid = base; eid < MAX_ENTRIES; eid++) {
        entry_t e = READ_ENTRY(eid);
        if (!IS_ENTRY_VALID(e)) {
            uint16_t gap = eid - end;

            // gap is made from the distance to the end
            // if len == 0, then end = base, and so gap == 0 (an addition)
            // HOWEVER if len != 0, end = base + len. IF base == len, then gap == 0 but nonzero number (ie addition immediately to right)
            // len == 0 seems to be determining factor

            if (len == 0) {
                if (gap == 0) {
                    // len = 0, gap = 0 -> this means that the base held by the empty queue matches
                    // make entry here, no need to mark as base since already matches, add it
                    SET_ENTRY_QUEUE_BASE_ON(e);
                    ADD_TO_QUEUE(*q, len, e, eid, b);
                }
                else {
                    // gap != 0, but len = 0 -> this means that the empty queue base is not accurate
                    // we've found an appropriate place to insert in eid'th entry, need to update the base on query
                    entry_t eb = READ_ENTRY(base);
                    SET_ENTRY_QUEUE_BASE_OFF(eb);
                    WRITE_ENTRY(eb, base);
                    SET_ENTRY_QUEUE_BASE_ON(e);
                    SET_QUEUE_BASE(*q, eid);
                    ADD_TO_QUEUE(*q, len, e, eid, b);
                }
            }
            else {
                if (gap == 0) {
                    // len != 0, but gap = 0 -> this means that we're at end = eid (we're at end) OR both are 0
                    // and we can safely add here to eid
                    // no need to update base flag/etc
                    // TODO: this broke stuff? after this, the 0'th id was incorrectly reported as valid
                    // eid other than 0 seems to cause issues
                    ADD_TO_QUEUE(*q, len, e, eid, b);
                }
                else {
                    // len != 0 and gap != 0 -> this means that we've found eid available, but at a gap that we must shift towards
                    // however, don't move the entire queue - shift everything right from the end to the gap, and then insert at end
                    // we may run into other queues' bases -> need to update those as well
                    for (uint16_t j = end + 1; j < eid; j++) {
                        entry_t ej = READ_ENTRY(j);
                        if (IS_ENTRY_QUEUE_BASE(ej)) {
                            // find the queue that this entry is a base of, and update it to the right
                            for (uint64_t qid = 0; qid < NUM_ACTIVE_QUEUES; qid++) {
                                queue_t* qt = GET_QUEUE(qid);
                                if (GET_QUEUE_BASE(*qt) == j) {
                                    SET_QUEUE_BASE(*qt, j + 1);
                                    entry_t new_base = READ_ENTRY(j + 1);
                                    SET_ENTRY_QUEUE_BASE_ON(new_base);
                                    WRITE_ENTRY(new_base, j + 1);
                                    SET_ENTRY_QUEUE_BASE_OFF(ej);
                                }
                            }
                        }
                        // Regardless of whether base of another queue or not, shift entry right by one
                        WRITE_ENTRY(ej, j + 1);
                    }
                    // end is available now, add to it
                    ADD_TO_QUEUE(*q, len, e, eid, b);
                }
            }

            isDone = TRUE;
            break;
        }
    }

    if (!isDone) {
        // didn't find to the right, try left
        for (int16_t i = base; i > 0; i--) {
            uint16_t eid = (uint16_t)i; // we're not using nowhere close near to the range of int, so no problem
            entry_t e = READ_ENTRY(eid);
            if (!IS_ENTRY_VALID(e)) {
                uint16_t gap = base - i;

                if (len == 0) {
                    if (gap != 0) {
                        // len = 0, gap = 0 starting from base accounted for on right lookup
                        // len == 0, gap != 0 -> we're an empty list but found a different base
                        // just start the base there, and update the queue (and older base)
                        entry_t eb = READ_ENTRY(base);
                        SET_ENTRY_QUEUE_BASE_OFF(eb);
                        WRITE_ENTRY(eb, base);
                        SET_ENTRY_QUEUE_BASE_ON(e);
                        SET_QUEUE_BASE(*q, eid);
                        ADD_TO_QUEUE(*q, len, e, eid, b);
                    }
                }
                else {
                    if (gap != 0) {
                        // len != 0, gap = 0 -> a non empty list finds a space on the last valid element?? this cannot happen.
                        // len != 0, gap != 0 -> non empty list finds a space more than one space to the left of the base
                        // need to move everything - base will at the end be moved left by len spaces -> update to new, unmark old, add at
                        // base(older) + len
                        for (uint16_t j = eid; j < end; j++) {
                            entry_t ej = READ_ENTRY(j + 1);
                            if (IS_ENTRY_QUEUE_BASE(ej)) {
                                // find the queue that the next entry is a front of, and update it to the left
                                // adjust any bases that are necessary along the way
                                // here we WILL run into q's own base - will work as is
                                for (uint64_t qid = 0; qid < NUM_ACTIVE_QUEUES; qid++) {
                                    queue_t* qt = GET_QUEUE(qid);
                                    if (GET_QUEUE_BASE(*qt) == j) {
                                        SET_QUEUE_BASE(*qt, j);
                                        entry_t new_base = READ_ENTRY(j);
                                        SET_ENTRY_QUEUE_BASE_ON(new_base);
                                        WRITE_ENTRY(new_base, j);
                                        SET_ENTRY_QUEUE_BASE_OFF(ej);
                                    }
                                }
                            }
                            // Regardless of whether or not we found the base of another queue or not, shift entry left by one
                            WRITE_ENTRY(ej, j);
                        }
                        // base + len (ie end) is available now - add to it
                        ADD_TO_QUEUE(*q, len, e, end, b);
                    }
                }

                isDone = TRUE;
                break;
            }
        }
    }

    if (!isDone) // didn't find right or left = no space -> out of memory
        on_out_of_memory();
}

unsigned char dequeue_byte(queue_t * q) {
    if (q == NULL || !IS_QUEUE_VALID(*q)) // invalid queue
        on_illegal_operation();

    uint16_t base = GET_QUEUE_BASE(*q);
    uint16_t len = GET_QUEUE_LENGTH(*q);

    if (len == 0) // empty queue
        on_illegal_operation();

    entry_t e = READ_ENTRY(base);
    uint8_t value = GET_ENTRY_VALUE(e);
    SET_ENTRY_INVALID(e);
    SET_ENTRY_QUEUE_BASE_OFF(e);
    WRITE_ENTRY(e, base);
    if (len > 1) {
        // if there's more entries that this queue owns to the right, adjust
        SET_QUEUE_BASE(*q, base + 1);
        entry_t next = READ_ENTRY(base + 1);
        SET_ENTRY_QUEUE_BASE_ON(next);
        WRITE_ENTRY(next, base + 1);
    }
    else {
        // this was the sole entry of the queue
        SET_QUEUE_BASE(*q, INVALID_ENTRY);
    }
    SET_QUEUE_LENGTH(*q, len - 1);
    return value;
}

int main(void) {
    uint8_t t = ~(1 << 3);
    queue_t * a = create_queue();
    printf("a: %u\n", *a);
    enqueue_byte(a, 5); // e is 768 at the end of this (after ADD_TO_QUEUE call)
    enqueue_byte(a, 1); // this validly detects first thing as taken, but after writing next ones don't work
    //enqueue_byte(a, 3); // this overwrote things? it just saw first eid as valid
    /*
    queue_t * b = create_queue();
    enqueue_byte(b, 3);
    
    uint16_t a_len = GET_QUEUE_LENGTH(*a);
    uint16_t a_base = GET_QUEUE_BASE(*a);
    uint16_t b_len = GET_QUEUE_LENGTH(*b);
    uint16_t b_base = GET_QUEUE_BASE(*b);
    dequeue_byte(a); // here it's dequeuing b! doesn't distinguish between one queue and the other
    dequeue_byte(b);
    dequeue_byte(a);
    */
    
    //enqueue_byte(a, 2);
    //enqueue_byte(b, 4);
    printf("%d ", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    /*
    enqueue_byte(a, 5);
    enqueue_byte(b, 6);
    printf("%d ", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    destroy_queue(a);
    printf("%d ", dequeue_byte(b));
    printf("%d ", dequeue_byte(b));
    printf("%d\n", dequeue_byte(b));
    destroy_queue(b);
    */
}
