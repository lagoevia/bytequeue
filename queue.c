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
#define GET_ENTRY_BLOCK(i)          ( (entry_block_t *)&data[(10*((i)+1)/8) + MONITOR_SEG_LEN] )
#define RSHIFT_ENTRY(i)             ( 6 - ((i) % 8) )
#define LSHIFT_ENTRY(i)             ( (i) % 8 )
#define READ_ENTRY_FROM_BLOCK(b,i)  ( (entry_t)(( (*b) >> RSHIFT_ENTRY((i)) ) & 0x1FF ))
#define WRITE_ENTRY_TO_BLOCK(b,e,i) ( ( (*b) & ( ~(0x1FF << 6) >> LSHIFT_ENTRY((i)) ) ) | ( (e) << RSHIFT_ENTRY((i)) ) )
#define READ_ENTRY(i)               ( (entry_t)(( (*(GET_ENTRY_BLOCK((i)))) >> RSHIFT_ENTRY((i)) ) & 0x1FF ))
#define WRITE_ENTRY(e,i)            ( ( (*(GET_ENTRY_BLOCK((i)))) & ( ~(0x1FF << 6) >> LSHIFT_ENTRY((i)) ) ) | ( (e) << RSHIFT_ENTRY((i)) ) )

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
#define ADD_TO_QUEUE(q, l, e, i, b)  SET_ENTRY_VALID(e); SET_ENTRY_VALUE(e, b); WRITE_ENTRY(e, i); SET_QUEUE_LENGTH(q, l + 1);

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
typedef uint16_t entry_block_t;

queue_t * create_queue() {
    for (uint16_t qid; qid < MAX_QUEUES; qid++) {
        queue_t* q = GET_QUEUE(qid);
        if (!IS_QUEUE_VALID(*q)) {
            SET_QUEUE_VALID(*q);
            // find the next available base to mark it for this queue
            uint8_t foundBase = FALSE;
            for (uint16_t eid = 0; eid < MAX_ENTRIES; eid++) {
                if (!IS_ENTRY_VALID(READ_ENTRY(eid))) {
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
            
            if (gap == 0) {
                // base matches, add to eid (e)
                // base flag adjusted on dequeue and not set on creation - no need to turn "older" base off
                SET_ENTRY_QUEUE_BASE_ON(e);
                ADD_TO_QUEUE(*q, len, e, eid, b);
            }
            else if(len == 0) {
                // base differs, but empty - update it and add to eid (e)
                // base flag adjusted on dequeue and not set on creation - no need to turn "older" base off
                SET_QUEUE_BASE(*q, eid);
                SET_ENTRY_QUEUE_BASE_ON(e);
                ADD_TO_QUEUE(*q, len, e, eid, b);
            }
            else {
                if (gap > 1) {
                    // shift right is necessary
                    uint8_t previousAdjustment = FALSE;
                    for (uint16_t j = end + 1; j < eid; j++) {
                        entry_t ej = READ_ENTRY(j);
                        if (IS_ENTRY_QUEUE_BASE(ej)) {
                            // find the queue that this entry is a front of, and update it to the right
                            for (uint64_t qid = 0; qid < NUM_ACTIVE_QUEUES; qid++) {
                                queue_t * qt = GET_QUEUE(qid);
                                if (GET_QUEUE_BASE(*qt) == j) {
                                    SET_QUEUE_BASE(*qt, j + 1);
                                    entry_t new_base = READ_ENTRY(j + 1);
                                    SET_ENTRY_QUEUE_BASE_ON(new_base);
                                    SET_ENTRY_QUEUE_BASE_OFF(ej);
                                }
                            }
                        }
                        WRITE_ENTRY(ej, j + 1);
                    }
                }
                // have shifted if necessary; now end+1 is available - add to it
                e = READ_ENTRY(end + 1);
                ADD_TO_QUEUE(*q, len, e, end+1, b);
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

                if (gap != 0) {
                    if (len == 0) {
                        // update base to eid, add to eid (e)
                        // base flag adjusted on dequeue and not set on creation - no need to turn "older" base off
                        SET_QUEUE_BASE(*q, eid);
                        SET_ENTRY_QUEUE_BASE_ON(e);
                        ADD_TO_QUEUE(*q, len, e, eid, b);
                    }
                    else {
                        // shift works no matter what - for gap == 1 it just does one iter
                        for (uint16_t j = eid; j < end; j++) {
                            entry_t ej = READ_ENTRY(j + 1);
                            if (IS_ENTRY_QUEUE_BASE(ej)) {
                                // find the queue that the next entry is a front of, and update it to the left
                                for (uint64_t qid = 0; qid < NUM_ACTIVE_QUEUES; qid++) {
                                    queue_t * qt = GET_QUEUE(qid);
                                    if (GET_QUEUE_BASE(*qt) == j) {
                                        SET_QUEUE_BASE(*qt, j);
                                        entry_t new_base = READ_ENTRY(j);
                                        SET_ENTRY_QUEUE_BASE_ON(new_base);
                                        SET_ENTRY_QUEUE_BASE_OFF(ej);
                                    }
                                }
                            }
                            WRITE_ENTRY(ej, j);
                        }
                        // update base to base - 1, add to end
                        // entry base on flag is adjusted above on first iter
                        SET_QUEUE_BASE(*q, len - 1);
                        e = READ_ENTRY(end);
                        ADD_TO_QUEUE(*q, len, e, eid, b);
                    }
                }
                // gap == 0 covered when looking to the right
                // TODO: gap == 0 seems to be called when a queue without space is created - what to do? Set base to 0, try again?

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
    enqueue_byte(a, 0);
    enqueue_byte(a, 4);
    /*
    queue_t * b = create_queue();
    enqueue_byte(b, 3);
    enqueue_byte(a, 2);
    enqueue_byte(b, 4);
    */
    printf("%d ", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    /*
    enqueue_byte(a, 5);
    enqueue_byte(b, 6);
    printf("%d", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    destroy_queue(a);
    printf("%d", dequeue_byte(b));
    printf("%d", dequeue_byte(b));
    printf("%d\n", dequeue_byte(b));
    destroy_queue(b);
    */
}
