#include <stdio.h>
#include <stdlib.h>
#include "queue.h"


// Misc
#define ADD_TO_QUEUE(q, l, e, i, b)  SET_ENTRY_VALID((e)); SET_ENTRY_VALUE((e), (b)); WRITE_ENTRY((e), (i)); SET_QUEUE_LENGTH((q), (l + 1));

void on_out_of_memory() {
    printf("BAD 1");
    exit(1);
}
void on_illegal_operation() {
    printf("BAD 2");
    exit(2);
}

/*
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
*/

int main(void) {
    uint8_t t = ~(1 << 3);
    queue_t * a = create_queue();
    //printf("a: %u\n", *a);
    enqueue_byte(a, 5); 
    enqueue_byte(a, 1);
    enqueue_byte(a, 3);
    //queue_t * b = create_queue();
    //enqueue_byte(b, 3);
    
    /*
    uint16_t a_len = GET_QUEUE_LENGTH(*a);
    uint16_t a_base = GET_QUEUE_BASE(*a);
    uint16_t b_len = GET_QUEUE_LENGTH(*b);
    uint16_t b_base = GET_QUEUE_BASE(*b);
    dequeue_byte(a); // here it's dequeuing b! doesn't distinguish between one queue and the other
    dequeue_byte(b);
    dequeue_byte(a);
    */
    
    enqueue_byte(a, 2); // this overwrote 3? did not
    //enqueue_byte(b, 4);
    printf("%d ", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a)); // this doesn't clear in valid places for 3, but rather adds 10 after? maybe the shift is off
    enqueue_byte(a, 5);
    //enqueue_byte(b, 6);
    printf("%d ", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    destroy_queue(a);
    /*
    printf("%d ", dequeue_byte(b));
    printf("%d ", dequeue_byte(b));
    printf("%d\n", dequeue_byte(b));
    destroy_queue(b);
    */
}
