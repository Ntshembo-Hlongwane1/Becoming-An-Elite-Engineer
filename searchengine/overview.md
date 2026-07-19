# Project 1 - Search Engine

## Objective

Build a search engine from first principles.

The goal is not to build a Google competitor.

The goal is to deeply understand:

* Information Retrieval
* Index Structures
* Search Architectures
* Data Structures
* Text Processing
* Ranking Systems
* Concurrency
* Storage Design

By the end of this project you should understand how a search engine transforms raw documents into searchable information.

---

# High Level Flow

Input Documents

↓

Document Parsing

↓

Tokenization

↓

Normalization

↓

Index Construction

↓

Search Query

↓

Search Results

---

# Project Structure

This project will be built in multiple versions.

Each version introduces a new problem.

Each problem naturally introduces a new data structure or algorithm.

Do not skip versions.

---

# Version 1 - Basic Document Search

## Goal

Build the smallest possible search engine.

No ranking.

No trie.

No fuzzy search.

Just search.

---

## Features

* Read text files
* Store documents
* Search for exact words
* Return matching documents

---

## Example Documents

redis.txt

```text
Redis is an in-memory database.
Redis supports caching.
```

kafka.txt

```text
Kafka is an event streaming platform.
```

golang.txt

```text
Go supports concurrency through goroutines.
```

---

## User Input

```bash
search redis
```

---

## Output

```text
Found 1 document

redis.txt
```

---

## Example 2

Input

```bash
search kafka
```

Output

```text
Found 1 document

kafka.txt
```

---

## Example 3

Input

```bash
search concurrency
```

Output

```text
Found 1 document

golang.txt
```

---

## Data Structures

Use:

* Array
* HashMap

---

## What You Learn

* File IO
* String Processing
* Basic Search
* Hashing

---

# Version 2 - Tokenization & Normalization

## Problem

These should all mean the same thing:

```text
Redis
redis
REDIS
```

But Version 1 treats them differently.

---

## Goal

Normalize documents before indexing.

---

## Features

* Lowercase conversion
* Remove punctuation
* Basic tokenization

---

## Example

Document

```text
Redis, Redis! REDIS.
```

Stored As

```text
redis
redis
redis
```

---

## Input

```bash
search redis
```

---

## Output

```text
redis.txt
```

---

## Example 2

Input

```bash
search Redis
```

Output

```text
redis.txt
```

---

## Example 3

Input

```bash
search REDIS
```

Output

```text
redis.txt
```

---

## Data Structures

* HashMap
* Arrays

---

## What You Learn

* Text normalization
* Tokenization
* Parsing pipelines

---

# Version 3 - Stop Word Removal

## Problem

Words like:

```text
the
and
is
of
a
```

appear in almost every document.

They pollute the index.

---

## Goal

Remove low value words.

---

## Example

Document

```text
Redis is a database.
```

Indexed As

```text
redis
database
```

---

## Input

```bash
search is
```

Output

```text
No Results
```

---

## Input

```bash
search database
```

Output

```text
redis.txt
```

---

## What You Learn

* Search optimization
* Index reduction

---

# Version 4 - Inverted Index

## Problem

Current search scans documents.

This becomes slow.

---

## Goal

Build an inverted index.

Instead of:

```text
Document -> Words
```

Store:

```text
Word -> Documents
```

---

## Example Index

```text
redis

    redis.txt
    cache.txt

database

    redis.txt
    postgres.txt
```

---

## Input

```bash
search database
```

Output

```text
redis.txt
postgres.txt
```

---

## Input

```bash
search redis
```

Output

```text
redis.txt
cache.txt
```

---

## What You Learn

* Inverted Indexes
* Information Retrieval
* Search Architecture

---

## Data Structures

* HashMap

Critical milestone.

This is the heart of every search engine.

---

# Version 5 - Multi-Term Search

## Goal

Search more than one word.

---

## Input

```bash
search distributed systems
```

---

## Output

```text
raft.txt
kafka.txt
```

---

## Example 2

Input

```bash
search redis cache
```

Output

```text
redis.txt
redis-cluster.txt
```

---

## Example 3

Input

```bash
search golang concurrency
```

Output

```text
golang.txt
```

---

## Algorithms

* Set Intersection
* Set Union

---

## What You Learn

* Query Engines
* Multi-Term Search

---

# Version 6 - Ranking

## Problem

Every result currently has equal importance.

Real search engines rank results.

---

## Goal

Return best matches first.

---

## Example

Input

```bash
search redis
```

Output

```text
1. redis.txt
2. redis-cluster.txt
3. caching-guide.txt
```

---

## Example 2

Input

```bash
search database
```

Output

```text
1. postgres.txt
2. redis.txt
3. mysql.txt
```

---

## Data Structures

* Heap

---

## Algorithms

* TF
* TF-IDF
* Sorting

---

## What You Learn

* Ranking Systems
* Relevance Scoring

---

# Version 7 - Prefix Search

## Problem

User types:

```text
red
```

Instead of:

```text
redis
```

---

## Goal

Support partial matches.

---

## Input

```bash
search red
```

Output

```text
redis
redis-cluster
redis-cache
```

---

## Example 2

Input

```bash
search kaf
```

Output

```text
kafka
```

---

## Example 3

Input

```bash
search gor
```

Output

```text
goroutine
```

---

## Data Structures

* Trie

---

## What You Learn

* Prefix Trees
* Auto Completion Systems

---

# Version 8 - Auto Complete

## Goal

Suggest searches.

---

## Input

```bash
search red
```

Output

```text
Suggestions

redis
redis cache
redis cluster
```

---

## Example 2

Input

```bash
search pos
```

Output

```text
postgres
postgres indexing
postgres replication
```

---

## Example 3

Input

```bash
search gor
```

Output

```text
goroutine
goroutine scheduler
goroutine channels
```

---

## Data Structures

* Trie

---

## What You Learn

* Search UX
* Predictive Search

---

# Version 9 - Fuzzy Search

## Problem

Users make mistakes.

---

## Input

```bash
search redsi
```

---

## Output

```text
Did you mean

redis
```

---

## Example 2

Input

```bash
search kafak
```

Output

```text
Did you mean

kafka
```

---

## Example 3

Input

```bash
search gorutine
```

Output

```text
Did you mean

goroutine
```

---

## Algorithms

* Levenshtein Distance
* Dynamic Programming

---

## What You Learn

* Approximate Matching
* Edit Distance Algorithms

---

# Version 10 - Concurrent Indexing

## Problem

Large document sets index slowly.

---

## Goal

Use goroutines.

---

## Features

* Worker Pool
* Concurrent Parsing
* Concurrent Index Construction

---

## Input

```bash
index documents/
```

Output

```text
Workers: 8

Indexed 1000 files

Time: 2.3s
```

---

## Example 2

Input

```bash
index documents/
```

Output

```text
Workers: 16

Indexed 5000 files

Time: 7.2s
```

---

## Example 3

Input

```bash
index documents/
```

Output

```text
Indexed 10000 files

Unique Terms: 85000
```

---

## Data Structures

* Queue
* Worker Pool

---

## What You Learn

* Concurrency
* Synchronization
* Throughput Optimization

---

# Final Data Structures Covered

* Arrays
* HashMaps
* Sets
* Heap
* Trie
* Queue

---

# Final Algorithms Covered

* Hashing
* Sorting
* String Matching
* Set Operations
* Ranking Algorithms
* Dynamic Programming
* Prefix Search

---

# Final Systems Concepts Covered

* Search Architecture
* Information Retrieval
* Index Structures
* Query Processing
* Relevance Scoring
* Concurrent Processing

---

# Final Outcome

After completing Search Engine you should understand:

✓ How search engines organize information

✓ Why inverted indexes exist

✓ How ranking systems work

✓ How autocomplete works

✓ How fuzzy matching works

✓ How concurrent indexing works

✓ When to use HashMaps

✓ When to use Tries

✓ When to use Heaps

✓ Why search is fundamentally an indexing problem rather than a scanning problem

This project forms the foundation for every subsequent project in the Systems Engineer Roadmap.
