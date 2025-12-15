#import "@local/basic:0.1.0": *

#show: project.with(
    title: "Tiered Hash Map",
    authors: ("Joyal Mathew",),
    date: today,
)

= Introduction

Modern in-memory key-value stores (such as Redis) typically assume uniform
memory access to DRAM. Now, as SSD performance improves and CXL powered
compressed memory becomes viable, this assumption fails. Storing solely in DRAM
leaves optimizations on the table as data structures may more efficiently be
represented across multiple memory hierarchies. This project will investigate
this idea as it applies to hash maps and introduce a new tiered-memory hash map
data structure. We will investigate the effect of placement policy on throughput
and memory usage.

= Literature Review

= Implementation

== Memory Simulation

Due to the nature of compressed memory, it cannot be directly written to and
from. Compression requires a large context, and writing directly into a
compressed memory block is a difficult task. As such, compressed memory must be
interacted with through explicit acquire and flush operations at the block-level
that compress/decompress on the fly.

To simulate this memory hierarchy in user-space with no hardware changes, the
three levels of memory are represented differently. RAM is represented with a
chunk of memory acquired through a call to `malloc`. CXL compressed memory is
represented also with a large chunk of memory but is additionally partitioned
into smaller chunks which are individually compressed when writing a block and
decompressed when reading a block. SSD is represented with a chunk of memory
backed by a file and bypassing the page cache using `msync/mprotect`.

Access to all three types of memory are made symmetric from an API standpoint
because memory is only interacted with on a block level. This greatly simplifies
implementation of the memory system but complicates the writing of application
code. This trade-off, however, is acceptable in the limited scope of this
project.

= Results

= Conclusion
