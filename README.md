# Byte Queue

## Problem Summary
Byte queue implementation for a programming challenge. Some of the info is below:
- Code cannot call malloc/other heap management routines; data (other than in local funcs) must be allocated from `unsigned char data[2048]`.
- The maximum number of active queues at any given time is 64.
- On average, there will be about 15 queues with an average 80 or so bytes in each queue; at times, there may be different combinations as well.

## Solution Summary
A fixed portion at the start of the available "pseudo memory" (`data`) will be reserved for the monitor section.
The monitor section will keep track of all 64 possible queues, as well as the number of currently active queues.
The remainder of the memory will be used for the entry data.
Approach allocates 8 bit per queue entry, and 24bit per queue handle, and assumes that queue entries are analogous.
The benefits of this is that it allows for memory to fit comfortably (unlike with a LL approach); the downside is that shifting will be required at times.
However, shifting is impacted both by how many things to move and how far away the available gap for insertion is. Since entries are analogous, a gap that is very far away
implies that there is already a large section of memory that isn't available - the worst case scenario would be having to either move a small number of (original) entries for a large distance or to move a large number of entries for a small distance, with the data in between having many of the queue bases.
Deletion overhead is minimized by just invalidating entries and queues, rather than clearing their memory entirely.

## Solution Details
### Architecture
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

### Enqueue
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
entry[j] = entry[j-1]
outside of loop, go through all queues and set entries to bases manually, only if their bases were modified (ie ON or AFTER start+len).

#S3:
different circumstances can lead to a left shift. Regardless of the amount, the addition is always at the (old) rightmost valid entry. The reason behind this comes from
how the gaps are found - as soon as one is met, it's done. There's no trying to find blocks/etc. So, many of the shifts can be treated in the same way.
Move here is from right to left, j on range [curr, base+len), incrementing by one.
check if j+1 is base of any queue q
    if yes, update queue itself base to move over by one. toggle base flag off.
entry[j] = entry[j+1]
outside of loop, go through all queues and set entries to bases manually, only if their bases were modified (ie ON or BEFORE base).

### Remaining Code
The remaining code has comments where appropriate within source to explain what is being done.