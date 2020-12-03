#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

// length here means number of entries; offset here means entry id, used to access individual entries from the data segment.
// queue: length(11) offset(11) pad(1) valid(1) = 3B
// entry: valid(1) value(8) = 2B // entries are interleaved with each other; entry idx HUGE for access
// monitor: ACTIVE_QUERIES(8) | queues... = 1B + 64(3B) = 193B

#define MAX_MEMORY                  2048
#define MONITOR_SEG_LEN             194 // added one extra for padding so that 194B
#define DATA_SEG_LEN                1854 // max - monitor
#define MAX_ENTRIES                 1684 // data_seg_len * 8 / 9

// Queue related
// q is of type queue_t *, v is of type unsigned char
// queues are indexed in sequential fashion, are 3 bytes apart, starting from idx 1 of data
#define MAX_ACTIVE_QUEUES           64
#define QUEUE_MASK                  0x7FF
#define QUEUE_LEN                   3
#define ACTIVE_QUEUES               data[0]
#define GET_QUEUE_LENGTH(q)         (((*q) >> 13) & QUEUE_MASK)
#define GET_QUEUE_OFFSET(q)         (((*q) >> 2) & QUEUE_MASK)
#define QUEUE_ADJ_MASK_OFFSET(q)    ( (*q) & ( ~((QUEUE_MASK << 13) >> 11 ) ))
#define QUEUE_ADJ_MASK_LENGTH(q)    ( (*q) & ( ~(QUEUE_MASK << 13) ))
#define SET_QUEUE_OFFSET(q,v)       ((*q) = ( ( QUEUE_ADJ_MASK_OFFSET((q))) | ((v) << 2) ))
#define SET_QUEUE_LENGTH(q,v)       ((*q) = ( ( QUEUE_ADJ_MASK_LENGTH((q))) | ((v) << 13) ))
#define IS_QUEUE_VALID(q)           ((*q) & 0x1)
#define SET_QUEUE_VALID(q)          ((*q) |= 0x1)
#define SET_QUEUE_INVALID(q)        ((*q) &= (~0 << 1))

// Entry related (needs updating)
#define ENTRY_LEN_BIT               9
#define ENTRY_MASK                  0x1FF
#define GET_ENTRY_SHIFT(i)          (7 - ((i) % 8))
#define GET_ENTRY_BLOCK(i)          ((entry_block_t *)&data[(9*(i)/8) + MONITOR_SEG_LEN])
#define GET_ENTRY_FROM_BLOCK(b,i)   ((entry_t)(((*b) >> GET_ENTRY_SHIFT((i))) & ENTRY_MASK))
#define ENTRY_ADJUSTMENT_MASK(b,i)  ((*b) & ( ~((ENTRY_MASK << (32 - GET_ENTRY_SHIFT((i)))) >> ( ) )
//#define ENTRY_ADJUSTMENT_MASK(b,i)  ((*b) & ( ~((ENTRY_MASK << GET_ENTRY_SHIFT((i)))) ) )
#define SET_BLOCK_ENTRY(b,i,e)      ((*b) = ( (ENTRY_ADJUSTMENT_MASK(b,i)) | ((e) << ENTRY_LEN_BIT)))

// TODO: Fix set block entry
    
// All of below assume e is an entry_t that contains *only* the contents relevant to itself/its 9 bits
#define IS_ENTRY_VALID(e)           (((e) >> 8) & 0x1)
#define SET_ENTRY_VALID(e)          ((e) |= (0x1 << 8))
#define SET_ENTRY_INVALID(e)        ((e) &= (~(0x1 << 8)))
#define GET_ENTRY_VALUE(e)          (((e) & 0x00FF))
#define SET_ENTRY_VALUE(e,v)        ((e) |= ((v) & 0x00FF))


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
typedef uint32_t entry_block_t; // entry block should be wider than an entry for good shift

queue_t * create_queue() {
    for(uint16_t i=0; i < MAX_ACTIVE_QUEUES; i++) {
        queue_t * q = (queue_t *)&data[(QUEUE_LEN * i) + 1];
        if(!IS_QUEUE_VALID(q)) {
            // Free queue space - use this
            for (uint16_t j = 0; j < MAX_ENTRIES; j++) {
                entry_block_t * b = GET_ENTRY_BLOCK(j);
                entry_t e = GET_ENTRY_FROM_BLOCK(b, j);
        
                if (!IS_ENTRY_VALID(e)) {
                    // e can be the first entry to queue
                    // note: do not reserve the entry for the queue here
                    // only upon enqueue first value should the space be used, and offset adjusted if necessary
                    SET_QUEUE_LENGTH(q, 0);
                    SET_QUEUE_OFFSET(q, j);
                    SET_QUEUE_VALID(q);
                    return q;
                }
            }
            // there was no available space on entry segment, despite being queues available - out of mem
            on_out_of_memory();
        }
    }
    // no queue is available -> treat as illegal op
    on_illegal_operation();
}

void destroy_queue(queue_t * q) {

    // ensure valid queue
    if (!IS_QUEUE_VALID(q))
        on_illegal_operation();

    uint16_t base = GET_QUEUE_OFFSET(q);
    uint16_t len = GET_QUEUE_LENGTH(q);

    // invalidate all of queue's entries
    for(uint16_t i=base; i < len; i++) {
        entry_block_t* b = GET_ENTRY_BLOCK(i);
        entry_t e = GET_ENTRY_FROM_BLOCK(b, i);
        SET_ENTRY_INVALID(e);
        SET_BLOCK_ENTRY(b, i, e);
    }

    // "free/clean up" the header
    SET_QUEUE_OFFSET(q, 0);
    SET_QUEUE_LENGTH(q, 0);
}

void enqueue_byte(queue_t * q, unsigned char b) {

    // ensure valid queue
    if (!IS_QUEUE_VALID(q))
        on_illegal_operation();

    uint16_t base = GET_QUEUE_OFFSET(q);
    uint16_t len = GET_QUEUE_LENGTH(q);

    uint8_t isDone = FALSE;
    uint16_t curr_idx = (len == 0) ? base : base + len - 1;

    //try to look right initially
    for (uint16_t i = curr_idx; i < MAX_ENTRIES; i++) {
        entry_block_t* block = GET_ENTRY_BLOCK(i);
        entry_t e = GET_ENTRY_FROM_BLOCK(block, i); // e here on second time doesn't report invalid - WRONG

        if (!IS_ENTRY_VALID(e)) {
            if (i - curr_idx >= 1) {
                if (len == 0) {
                    // base offset is outdated, need to update it
                    SET_ENTRY_VALID(e);
                    SET_ENTRY_VALUE(e, b);
                    SET_BLOCK_ENTRY(block, i, e);
                    SET_QUEUE_OFFSET(q, i);
                    SET_QUEUE_LENGTH(q, len + 1);
                }
                else {
                    if (i - curr_idx > 1) {
                        // more than one separation, needs shift
                        for (uint16_t j = i; j < curr_idx; j++) { // TODO: can use while i > curr_idx here also
                            block = GET_ENTRY_BLOCK(j-1);
                            e = GET_ENTRY_FROM_BLOCK(GET_ENTRY_BLOCK(j), j);
                            SET_BLOCK_ENTRY(block, j-1, e);
                        }
                    }
                    block = GET_ENTRY_BLOCK(curr_idx + 1);
                    e = GET_ENTRY_FROM_BLOCK(block, curr_idx + 1);
                    //add: validate, set value, update q len
                    SET_ENTRY_VALID(e);
                    SET_ENTRY_VALUE(e, b);
                    SET_BLOCK_ENTRY(block, curr_idx + 1, e);
                    SET_QUEUE_LENGTH(q, len + 1);
                }
            }
            else if (i - curr_idx == 0) {
                // directly on base, applies for empty queues that are not outdated, instert to i's entry
                // add: validate, set value, update q len
                SET_ENTRY_VALID(e);
                SET_ENTRY_VALUE(e, b);
                SET_BLOCK_ENTRY(block, curr_idx + 1, e);
                SET_QUEUE_LENGTH(q, len + 1);
            }
            isDone = TRUE;
            break;
        }
    }
    if (!isDone) {
        // try to look left now, all space right is occupied
        for (int i = curr_idx; i > 0; i--) {
            entry_block_t* block = GET_ENTRY_BLOCK(i);
            entry_t e = GET_ENTRY_FROM_BLOCK(block, i);

            if (!IS_ENTRY_VALID(e)) {
                if (curr_idx - i >= 1) {
                    if (len == 0) {
                        // base offset is outdated, need to update it
                        SET_ENTRY_VALID(e);
                        SET_ENTRY_VALUE(e, b);
                        SET_BLOCK_ENTRY(block, i, e);
                        SET_QUEUE_OFFSET(q, i);
                        SET_QUEUE_LENGTH(q, len + 1);
                    }
                    else {
                        if (curr_idx - i > 1) {
                            // more than one separation, needs shift
                            for (int j = i; j > curr_idx; j++) { // TODO: can use while i > curr_idx here also
                                block = GET_ENTRY_BLOCK(j);
                                e = GET_ENTRY_FROM_BLOCK(GET_ENTRY_BLOCK(j - 1), j - 1);
                                SET_BLOCK_ENTRY(block, j, e);
                            }
                        }
                        block = GET_ENTRY_BLOCK(curr_idx + 1);
                        e = GET_ENTRY_FROM_BLOCK(block, curr_idx + 1);
                        //add: validate, set value, update q len
                        SET_ENTRY_VALID(e);
                        SET_ENTRY_VALUE(e, b);
                        SET_BLOCK_ENTRY(block, curr_idx + 1, e);
                        SET_QUEUE_LENGTH(q, len + 1);
                    }
                }
                // curr_idx - i == 0 already addressed when going right
                isDone = TRUE;
                break;
            }
        }
    }
    // couldn't find to the right, couldn't find to the left - out of mem
    if (!isDone)
        on_out_of_memory();
}

unsigned char dequeue_byte(queue_t * q) {
    uint16_t base = GET_QUEUE_OFFSET(q);
    uint16_t len = GET_QUEUE_LENGTH(q);

    if(len != 0) {
        // invalidate byte, set new base, and update length
        entry_block_t* block = GET_ENTRY_BLOCK(base);
        entry_t e = GET_ENTRY_FROM_BLOCK(block, base);
        unsigned char value = GET_ENTRY_VALUE(e);
        SET_ENTRY_INVALID(e);
        SET_BLOCK_ENTRY(block, base, e);
        SET_QUEUE_OFFSET(q, base + 1);
        SET_QUEUE_LENGTH(q, len-1);
        return value;
    }

    // empty queue
    on_illegal_operation();
}

int main(void) {
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
