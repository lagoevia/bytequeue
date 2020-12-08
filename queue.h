#ifndef QUEUE_H
#define QUEUE_H

/**
	A queue consists of the following:
	- length: leftmost 11 bits -> the number of current entries in this queue
	- base: next 11 bits -> the entry id for the first entry (i.e front) of the queue
	- padding bit (unused)
	- valid: rightmost bit -> whether this queue is valid or not

	A queue can thus be represented within 24bit (3B).
	Queues will reside in a segment of memory called the MONITOR SEGMENT; the remaining
	memory is called the ENTRY SEGMENT. An user interacts with queries via query handles:
	these are just 4B pointers to the corresponding section of the MONITOR SEGMENT.

	The MONITOR SEGMENT contains the following:
	- number of active queues: first byte of the monitor segment -> the number of active queues
	- the queues themselves: 3B each, following immediately after the number of active queues

	An entry consists of the following:
	- base: leftmost bit -> whether this entry is the base of a queue or not
	- valid: next bit -> whether this entry is valid or not
	- value: next 8bit -> the value this entry holds

	An entry can thus be represented within 10bit.
	Entries will reside in the ENTRY SEGMENT. 
	
	Unlike queues that "fit nicely" within byte bounds, entries need to be interleaved with each other.
	The approach this code considers is one where entries are read from their "entry block", modified
	separately, and then written back into that block. The bulk of this workaround is managed in the
	entry block <-> entry interactions, with entry-specific operations working under the assumption
	that the entry passed into them contains the elements all the way to the right, by themselves.

	Entries themselves can be in different configurations, with the "widest" one being 1-8-1. In it,
	the entry block needs to be 3B to hold the entire entry. Thus, an entry block should be 4B wide.
	Blocks are read from memory under the assumption that they have been written as big endian (but
	ultimately represented as little endian), so the get_entry_block* functions read the block from
	memory, and swap their endianness if appropriate. Despite the blocks being in correct order, they
	need to be swapped after a write takes place so that upon the next read the assumptions of big 
	endian data represented as little endian hold.

*/

#include <stdint.h>

// TODO: possible refactoring: on/off variants as "flags" with parameter?


/*
	The solution assumes that bytes in memory are laid out as they are assigned arithmetically:
	this doesn't hold true for little endian architectures; in those cases, we need to have helper
	functions to read-from and write-to memory correctly.
*/
#define IS_LITTLE_ENDIAN    (*(unsigned char *)&(uint16_t){1})

inline void __swap(uint8_t* _a, uint8_t* _b) {
	uint8_t tmp = *_a;
	*_a = *_b;
	*_b = tmp;
}

inline uint32_t* __swap_endianness(uint32_t* x) {
	__swap((uint8_t*)(x)+0, (uint8_t*)(x)+3);
	__swap((uint8_t*)(x)+1, (uint8_t*)(x)+2);
	return x;
}

typedef uint8_t byte;
typedef uint32_t queue_t;
typedef uint32_t entry_block_t;
typedef uint16_t entry_t;

#ifndef MAX_QUEUE_MEMORY
	#define MAX_QUEUE_MEMORY				(2048)
#endif
#define MAX_ACTIVE_QUEUES				(64)
#define NUM_ACTIVE_QUEUES				(data[0])
#define QUEUE_BYTE_SIZE					(3)
#define MONITOR_SEG_LEN					(1 + (QUEUE_BYTE_SIZE * MAX_ACTIVE_QUEUES))
#define ENTRY_SEG_LEN					(MAX_QUEUE_MEMORY - MONITOR_SEG_LEN)
#define ENTRY_BIT_SIZE					(10)
#define MAX_ENTRIES						(ENTRY_SEG_LEN * 8 / ENTRY_BIT_SIZE)

byte data[MAX_QUEUE_MEMORY] = { 0 };

// --- Queue

#define QUEUE_MASK						(0x7FF)
#define QUEUE_BIT_SIZE					(QUEUE_BYTE_SIZE * 8)
#define QUEUE_LENGTH_BIT_SIZE			(11)
#define QUEUE_LENGTH_SHIFT_ADJUSTMENT	(QUEUE_BIT_SIZE - QUEUE_LENGTH_BIT_SIZE)
#define QUEUE_BASE_BIT_SIZE				(11)
#define QUEUE_BASE_SHIFT_ADJUSTMENT		(QUEUE_BIT_SIZE - QUEUE_LENGTH_BIT_SIZE - QUEUE_BASE_BIT_SIZE)

inline void set_queue_length(queue_t* q, const uint16_t len) {
	*q = (*q & ~(QUEUE_MASK << QUEUE_LENGTH_BIT_SIZE)) | (len << QUEUE_LENGTH_SHIFT_ADJUSTMENT);
}

inline uint16_t get_queue_length(const queue_t* const q) {
	return ((*q >> QUEUE_LENGTH_SHIFT_ADJUSTMENT) & QUEUE_MASK);
}

inline void set_queue_base(queue_t* q, const uint16_t base) {
	*q = (*q & ~(QUEUE_MASK << QUEUE_BASE_BIT_SIZE)) | (base << QUEUE_BASE_SHIFT_ADJUSTMENT);
}

inline uint16_t get_queue_base(const queue_t* const q) {
	return ((*q >> QUEUE_BASE_SHIFT_ADJUSTMENT) & QUEUE_MASK);
}

inline void set_queue_valid(queue_t* q) {
	*q |= 0x1;
}

inline void set_queue_invalid(queue_t* q) {
	*q &= (~(0 << 1));
}

inline uint8_t is_queue_valid(const queue_t* const q) {
	return (*q & 0x1);
}

inline queue_t* get_queue(const uint16_t qid) {
	// TODO: No shift of queue? Also, no memory issues with endianness?
	return (queue_t*)&data[QUEUE_BYTE_SIZE * qid + 1];
}

// --- Entry <-> Block

#define ENTRY_MASK						(0x3FF)
#define ENTRY_REAL_START_BIT(eid)		(ENTRY_BIT_SIZE * (eid))
#define ENTRY_REAL_END_BIT(eid)			(ENTRY_REAL_START_BIT(eid) + ENTRY_BIT_SIZE - 1)
#define ENTRY_BYTE_START_BIT(eid)		(8 * (ENTRY_REAL_START_BIT(eid) / 8))
#define ENTRY_BYTE_END_BIT(eid)			(ENTRY_BYTE_START_BIT(eid+1) - 1)

inline entry_block_t* get_entry_block_ptr(const uint16_t eid) {
	entry_block_t* block = (entry_block_t*)&data[MONITOR_SEG_LEN + ENTRY_BYTE_START_BIT(eid)];
#ifdef IS_LITTLE_ENDIAN
	__swap_endianness(block);
#endif
	return block;
}

inline entry_block_t get_entry_block(const uint16_t eid) {
	entry_block_t block = *(entry_block_t*)&data[MONITOR_SEG_LEN + ENTRY_BYTE_START_BIT(eid)];
#ifdef IS_LITTLE_ENDIAN
	__swap_endianness(&block);
#endif
	return block;
}

inline entry_t read_entry_from_block(const entry_block_t block, const uint16_t eid) {
	// Block is "coming in well", so no need to adjust it
	uint16_t start_shift = ENTRY_REAL_END_BIT(eid) - ENTRY_BYTE_END_BIT(eid);
	return (entry_t)((block >> start_shift) & ENTRY_MASK);
}

inline void write_entry_to_block(entry_block_t* block, const entry_t entry, const uint16_t eid) {
	// Block is "coming in well", but after the assignment it must be adjusted for memory representation
	uint16_t start_shift = ENTRY_REAL_END_BIT(eid) - ENTRY_BYTE_END_BIT(eid);
	*block = (*block & ~(ENTRY_MASK << start_shift)) | (entry >> start_shift);
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

#define INVALID_ENTRY					(MAX_ENTRIES)
#define ENTRY_VALUE_BIT_SIZE			(8)
#define ENTRY_VALUE_MASK				(0x0FF)
#define ENTRY_VALID_BIT_SIZE			(1)
#define ENTRY_VALID_SHIFT_ADJUSTMENT	(ENTRY_VALUE_BIT_SIZE)
#define ENTRY_BASE_BIT_SIZE				(1)
#define ENTRY_BASE_SHIFT_ADJUSTMENT		(ENTRY_VALID_BIT_SIZE + ENTRY_VALUE_BIT_SIZE)

inline void set_entry_valid(entry_t e) {
	e |= (1 << ENTRY_VALID_SHIFT_ADJUSTMENT);
}

inline void set_entry_invalid(entry_t e) {
	e &= (~(1 << ENTRY_VALID_SHIFT_ADJUSTMENT));
}

inline uint8_t is_entry_valid(const entry_t e) {
	return ((e >> ENTRY_VALID_SHIFT_ADJUSTMENT) & 0x1);
}

inline byte get_entry_value(const entry_t e) {
	return (e & ENTRY_VALUE_MASK);
}

inline void set_entry_value(entry_t e, byte v) {
	e = (e & (~ENTRY_VALUE_MASK)) | v;
}

inline uint8_t is_entry_queue_base(const entry_t e) {
	return ((e >> ENTRY_BASE_SHIFT_ADJUSTMENT) & 0x1);
}

inline void set_entry_queue_base_on(entry_t e) {
	e |= 1 << ENTRY_BASE_SHIFT_ADJUSTMENT;
}

inline void set_entry_queue_base_off(entry_t e) {
	e &= (~(1 << ENTRY_BASE_SHIFT_ADJUSTMENT));
}


// Actual API
extern void on_out_of_memory();
extern void on_illegal_operation();

queue_t* create_queue() {
	return NULL;
}

void destroy_queue(queue_t* q) {
	;
}
void enqueue_byte(queue_t* q, byte b) {
	;
}
byte dequeue_byte(queue_t* q) {
	return 0;
}

#endif