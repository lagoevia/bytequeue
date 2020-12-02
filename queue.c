#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_MEMORY 2048
#define ENTRY_LEN 2
#define QUEUE_LEN 3
#define MONITOR_SEG_LEN 193
#define MAX_ACTIVE_QUEUES 64

#define ACTIVE_QUEUES data[0]
#define IS_ENTRY_VALID(e) ((e >> 8) & 0x1)
#define GET_QUEUE_LENGTH(q) (((q) >> 13) & 0x7FF)
#define GET_QUEUE_OFFSET(q) (((q) >> 2) & 0x7FF)

#define SET_QUEUE_OFFSET(q,v) ((q) |= ((v) << 2))
#define SET_QUEUE_LENGTH(q,v) ((q) |= ((v) << 13))
#define SET_ENTRY_VALID(e) ((e) |= (0x1 << 8))
#define SET_ENTRY_INVALID(e) ((e) |= (0x0 << 8))
#define GET_ENTRY_VALUE(e) ((e & 0x00FF))
#define SET_ENTRY_VALUE(e,v) ((e) |= (v))

// TODO: Change stuff to pointers for events and queues => they need to be pointing at the location in data to change it!

// queue: length(11) offset(11) pad(2) = 3B
// entry: pad(7) valid(1) value(8) = 2B // TODO: 2B is too big to fit average
// monitor: ACTIVE_QUERIES(6) pad(2) | queues... = 1B + 64(3B) = 193B

unsigned char data[MAX_MEMORY] = {0};
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
typedef uint32_t Queue;
typedef uint16_t Entry;



Queue * create_queue() {
    for(int i=0; i < MAX_ACTIVE_QUEUES; i++) {
        Queue * header = (Queue *)&data[(QUEUE_LEN * i) + 1];
        uint16_t base = GET_QUEUE_OFFSET(*header);
        if(base == 0) {
            // No offset set yet - find it and create the queue there
            for(int j=MONITOR_SEG_LEN; j < MAX_MEMORY - ENTRY_LEN; j+=ENTRY_LEN) {
                Entry *e = (Entry *)&data[j];
                if(!IS_ENTRY_VALID(*e)) {
                    SET_QUEUE_OFFSET(*header, j);
                    SET_QUEUE_LENGTH(*header, 0);
                    SET_ENTRY_VALID(*e);
                    printf("dbg: %u|%u %u|%u\n", GET_QUEUE_LENGTH(*header), GET_QUEUE_OFFSET(*header), IS_ENTRY_VALID(*e), GET_ENTRY_VALUE(*e));
                    return header;
                }
            }
        }
    }
    // no queue is available -> treat as illegal op
    printf("oopsie");
    on_illegal_operation();
}

void destroy_queue(Queue * q) {
    uint16_t base = GET_QUEUE_OFFSET(*q);
    uint16_t len = GET_QUEUE_LENGTH(*q);
    // invalidate all of queue's entries
    for(int i=base; i < len; i++) {
        Entry e = (Entry)data[ENTRY_LEN * i];
        SET_ENTRY_INVALID(e);
    }
    // "free up" the header
    SET_QUEUE_OFFSET(*q, 0);
    SET_QUEUE_LENGTH(*q, 0);
}

void enqueue_byte(Queue * q, unsigned char b) {
    uint16_t base = GET_QUEUE_OFFSET(*q);
    uint16_t len = GET_QUEUE_LENGTH(*q);
    // if queue is valid, try to insert
    if(IS_ENTRY_VALID((uint32_t)data[base])) {
        uint16_t curr = base + len - 1;
        // try next
        if(curr != MAX_MEMORY-1 && !IS_ENTRY_VALID((uint32_t)data[curr+1])) {
            // can insert normally to the right at next
            Entry e = (Entry)data[curr+1];
            SET_ENTRY_VALUE(e, b);
            uint16_t ql = GET_QUEUE_LENGTH(e);
            SET_QUEUE_LENGTH(e, ql+1);
        }
        // try right
        else if(curr <= MAX_MEMORY - ENTRY_LEN) {
            uint16_t shift_by = 0;
            for(int i=curr+ENTRY_LEN; i < MAX_MEMORY - ENTRY_LEN; i++) {
                   if(!IS_ENTRY_VALID((uint32_t)data[ENTRY_LEN * i])) {
                        shift_by = ENTRY_LEN * i;
                        // Found shift amount; need to move everything from
                        // curr to curr+shift_by-1 right by one, and insert b
                        // at curr
                        for(int j=curr+shift_by; j > curr; j--) {
                            // it's 2B we need to move
                            data[j+ENTRY_LEN] = data[j];
                            data[j+ENTRY_LEN+1] = data[j+1];
                        }
                        Entry e = (Entry)data[curr];
                        SET_ENTRY_VALUE(e, b);
                        uint16_t ql = GET_QUEUE_LENGTH(e);
                        SET_QUEUE_LENGTH(e, ql+1);
                   }
            }
            // no space to the right
        }
        // try left
        else if(curr >= MONITOR_SEG_LEN + ENTRY_LEN) {
            uint16_t shift_by = 0;
            for(int i=curr-ENTRY_LEN; i > MONITOR_SEG_LEN + ENTRY_LEN; i--) {
                   if(!IS_ENTRY_VALID((uint32_t)data[ENTRY_LEN * i])) {
                        shift_by = ENTRY_LEN * i;
                        // Found shift amount; need to move everything from
                        // curr to curr+shift_by-1 left by one, and insert b at
                        // curr
                        for(int j=curr-shift_by; j < curr; j++) {
                            // 2B we need to move
                            data[j] = data[j+ENTRY_LEN];
                            data[j+1] = data[j+ENTRY_LEN+1];
                        }
                        Entry e = (Entry)data[curr];
                        SET_ENTRY_VALUE(e, b);
                        uint16_t ql = GET_QUEUE_LENGTH(e);
                        SET_QUEUE_LENGTH(e, ql+1);
                   }
            }
            if(shift_by == 0) // no space to left either - out of memory
                on_out_of_memory();
        }
    }
    else // invalid queue
        on_illegal_operation();
}

unsigned char dequeue_byte(Queue * q) {
    uint16_t base = GET_QUEUE_OFFSET(*q);
    uint16_t len = GET_QUEUE_LENGTH(*q);
    if(len != 0) {
        // invalidate byte and update length
        Entry e = (Entry)data[base+len];
        SET_ENTRY_INVALID(e);
        SET_QUEUE_LENGTH(e, len-1);
        return GET_ENTRY_VALUE(e);
    }
    // empty queue
    on_illegal_operation();
}

int main(void) {
    Queue * a = create_queue();
    printf("a: %u\n", *a);
    enqueue_byte(a, 0);
    enqueue_byte(a, 1);
    Queue * b = create_queue();
    enqueue_byte(b, 3);
    enqueue_byte(a, 2);
    enqueue_byte(b, 4);
    printf("%d", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    enqueue_byte(a, 5);
    enqueue_byte(b, 6);
    printf("%d", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    destroy_queue(a);
    printf("%d", dequeue_byte(b));
    printf("%d", dequeue_byte(b));
    printf("%d\n", dequeue_byte(b));
    destroy_queue(b);
}
