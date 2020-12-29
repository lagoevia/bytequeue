#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

void on_out_of_memory() {
    fputs("Error: Out of memory", stderr);
    exit(1);
}
void on_illegal_operation() {
    fputs("Error: Illegal operation", stderr);
    exit(2);
}

int main(void) {
    queue_t* a = create_queue();
    enqueue_byte(a, 0);
    enqueue_byte(a, 1);
    queue_t* b = create_queue();
    enqueue_byte(b, 3);
    enqueue_byte(a, 2);
    enqueue_byte(b, 4);
    printf("%d ", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    enqueue_byte(a, 5);
    enqueue_byte(b, 6);
    printf("%d ", dequeue_byte(a));
    printf("%d\n", dequeue_byte(a));
    destroy_queue(a);
    printf("%d ", dequeue_byte(b));
    printf("%d ", dequeue_byte(b));
    printf("%d\n", dequeue_byte(b));
    destroy_queue(b);
}
