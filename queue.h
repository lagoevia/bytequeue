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
#include <stdbool.h>

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
	#define MAX_QUEUE_MEMORY			(2048)
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

inline queue_t* get_queue_ptr(const uint16_t qid) {
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


// --- Actual API
extern void on_out_of_memory();
extern void on_illegal_operation();

queue_t* create_queue() {
	for (uint16_t qid = 0; qid < MAX_ACTIVE_QUEUES; qid++) {
		queue_t* q = get_queue_ptr(qid);
		if (q && !is_queue_valid(q)) {
			set_queue_valid(q);
			NUM_ACTIVE_QUEUES += 1;
			for (uint16_t base = 0; base < MAX_ENTRIES; base++) {
				entry_t e = read_entry_from_id(base);
				if (!is_entry_valid(e)) {
					// TODO: should the space on creation be reserved?
					// For simplicity now, assume yes
					set_entry_valid(e);
					set_entry_queue_base_on(e);
					write_entry_to_id(e, base);
					set_queue_base(q, base);
					return q;
				}
			}
			// All space is taken, but the room for the queue itself was available
			// Mark this queue's base as inactive entry, then handle finding it a base on enqueue
			set_queue_base(q, INVALID_ENTRY);
			return q;
		}
	}
	// There were no queues available - illegal op
	on_illegal_operation();
}

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
			set_entry_queue_base_off(e);
		}
		set_entry_invalid(e);
		write_entry_to_id(e, i);
	}

	// mark queue length as 0, set queue base to invalid entry, mark queue invalid, set q to be NULL
	set_queue_length(q, 0);
	set_queue_base(q, INVALID_ENTRY);
	set_queue_invalid(q);
	q = NULL;
}

/*
		Enqueue needs to account for 3 general cases:
			1. base itself is open for insertion
			2. found space 1 or more units away (to the right) from the last valid entry
			3. found space 1 or more units away (to the left) from base

		For #2, the starting point is equal to base + len -> the entry to the right of the last valid entry
			gap = current point - starting point.
			Current will never be smaller than starting, only potentially equal.
		For #3, the starting point is equal to base - 1 -> the entry to the left of the first valid entry (the base, as it's accounted for in #1);
			gap = starting point - current point (since current point will be <= starting point for left).
			Starting will never be smaller than current, only potentially equal.

		#1)
			- check base. if available, just add the entry there (as the base matches).
		#2)
			- gap == 0, len == 0 -> 0 0 or curr == start. Since start is base + len, here it'd be base. However, we've already checked base in #1. Contradiction.
			- gap == 0, len != 0 -> 0 0 or curr == start. Since start is base + len, this is the entry to the right of the last entry. We can add the entry here normally.
			- gap != 0, len == 0 -> empty queue, but gap means curr > starting; since empty, this means the reported base needs to be relocated. Update old and new entries, and add.
			- gap != 0, len != 0 -> non-empty queue, but gap is 1 or greater
				-- gap == 1 -> the space right next to the first space to the right of the last valid space is available. This means only one shift is necessary.
				-- gap > 1 -> the entry is at least more than one unit away from the first space to the right of the last valid space - shifting of queue data will be necessary.
				However, also take into account other possible queues and maintain their respective bases. Shifting is addressed on #S2 below, for this and above.

		#3)
			- gap == 0, len == 0 -> 0 0 or curr == start. Since start is base-1, this would mean that the space immediately to the left of base is available. Since empty, only need to relocate
			and update bases.
			- gap == 0, len != 0 -> 0 0 or curr == start. Since start is base-1, this would mean that the space immediately to the left of the base is available. However, it's nonempty, so a left
			shift will be necessary (only once). Then add to (old) base + len.
			- gap != 0, len == 0 -> empty queue, but gap means curr < start, so need to relocate and update bases.
			- gap != 0, len != 0 -> non-empty queue, but gap is 1 or greater
				-- gap == 1 -> space is exactly one to the left of the first space to the left of the first valid space is available. Two shifts necessary. #S3.
				-- gap > 1 -> space is two or more units to the left of the first space to the left of the first valid space is available. More than two shifts necessary. #S3.

		#S2:
			only one circumstance when this happens, as marked above. Move here is from left to right, j on range (start, curr] incrementing by one.
			check if j is base of any queue q
				if yes, then update queue itself base to move over by one. toggle base flag off.
				TODO: could avoid checking already verified queues?
			entry[j] = entry[j-1]
			outside of loop, go through all queues and set entries to bases manually, only if their bases were modified (ie ON or AFTER start+len).

		#S3:
			different circumstances can lead to a left shift. Regardless of the amount, the addition is always at the (old) rightmost valid entry. The reason behind this comes from
			how the gaps are found - as soon as one is met, it's done. There's no trying to find blocks/etc. So, many of the shifts can be treated in the same way.
			Move here is from right to left, j on range [curr, base+len), incrementing by one.
			check if j+1 is base of any queue q
				if yes, update queue itself base to move over by one. toggle base flag off.
				TODO: could avoid checking already verified queues?
			entry[j] = entry[j+1]
			outside of loop, go through all queues and set entries to bases manually, only if their bases were modified (ie ON or BEFORE base).
*/

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
		set_entry_valid(e);
		set_entry_queue_base_on(e);
		set_entry_value(e, b);
		write_entry_to_id(e, base);
		set_queue_length(q, len + 1);
		isDone = true;
	}

	if (!isDone)
	{
		// try right
		uint16_t start = base + len;
		for (uint16_t i = start; i < MAX_ENTRIES; i++) {
			if (!is_entry_valid(read_entry_from_id(i))) {
				uint16_t gap = i - start;
				if (gap == 0) {
					// can add here normally (i)
					e = read_entry_from_id(i);
					set_entry_valid(e);
					set_entry_value(e, b);
					write_entry_to_id(e, i);
					set_queue_length(q, len + 1);
				}
				else if (len == 0) {
					// empty queue with gap - relocate and update entries
					entry_t old = read_entry_from_id(base);
					set_entry_queue_base_off(e);
					write_entry_to_id(e, base);
					e = read_entry_from_id(i);
					set_entry_valid(e);
					set_entry_queue_base_on(e);
					set_entry_value(e, b);
					write_entry_to_id(e, i);
					set_queue_length(q, len + 1);
					set_queue_base(q, i);
				}
				else
				{
					// nonempty queue with gap - shifting necessary
					for (uint16_t j = i; j > start; j--) {
						// j - 1's base will be equal to j's base
						// j - 1's value will be equal to j's value
						// so, copy the whole thing over
						e = read_entry_from_id(j - 1);
						write_entry_to_id(e, j);
					}
					// start is available now, but it still contains the "old" start contents - clear them (base) just in case
					e = read_entry_from_id(start);
					set_entry_queue_base_off(e);
					set_entry_value(e, b);
					write_entry_to_id(e, start);
					set_queue_length(q, len + 1);
				}
				isDone = true;
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
					if (gap == 0)
					{
						if (len == 0) {
							// empty queue, just need to adjust base/updae entries and insert at i
							entry_t old = read_entry_from_id(base);
							set_entry_queue_base_off(old);
							write_entry_to_id(e, base);
							e = read_entry_from_id(i);
							set_entry_valid(e);
							set_entry_queue_base_on(e);
							set_entry_value(e, b);
							write_entry_to_id(e, i);
							set_queue_base(q, i);
							set_queue_length(q, len + 1);
						}
						else {
							// non-empty queue found a spot immediately to the left of its current base
							// shift everything by one, update current queue base, and insert at the former "tail"
							for (int j = i; j < base + len - 1; j++) {
								// j + 1's info is now on j - copy it over
								e = read_entry_from_id(j + 1);
								write_entry_to_id(e, j);
							}
							// former tail's info is still on there, but no need to wipe as it is guaranteed not to be a head
							e = read_entry_from_id(base + len - 1);
							set_entry_value(e, b);
							write_entry_to_id(e, base + len - 1);
							set_queue_base(q, i);
							set_queue_length(q, len + 1);
						}
					}
					else if (len == 0) {
						// empty queue, need to adjust base/update entries and insert at i (just happens to be more than one gap away)
						// same as gap == 0 and len == 0 case - TODO: maybe refactor this if chain?
						entry_t old = read_entry_from_id(base);
						set_entry_queue_base_off(old);
						write_entry_to_id(e, base);
						e = read_entry_from_id(i);
						set_entry_valid(e);
						set_entry_queue_base_on(e);
						set_entry_value(e, b);
						write_entry_to_id(e, i);
						set_queue_base(q, i);
						set_queue_length(q, len + 1);
					}
					else {
						// non empty queue, but gap has some other queue(s)' info in the middle
						// this is the same as gap == 0 len != 0, just so happens that the initial shift is moving other queues' data as well
						// TODO: refactor this also, only really a need for two if statements on left search
						for (int j = i; j < base + len - 1; j++) {
							// j + 1's info is now on j - copy it over
							e = read_entry_from_id(j + 1);
							write_entry_to_id(e, j);
						}
						// former tail's info is still on there, but no need to wipe as it is guaranteed not to be a head
						e = read_entry_from_id(base + len - 1);
						set_entry_value(e, b);
						write_entry_to_id(e, base + len - 1);
						set_queue_base(q, i);
						set_queue_length(q, len + 1);
					}

					isDone = true;
				}
			}
		}
		
	}
	
	if (!isDone) {
		// we've tried everything and didn't succeed, so no memory left
		on_out_of_memory();
	}
}

byte dequeue_byte(queue_t* q) {
	return 0;
}

#endif