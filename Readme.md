# Becoming an Elite Engineer

> Escape the Abstraction is a long-term engineering journey to understand how modern software systems actually work—from low-level programming and operating systems to distributed systems, databases, search engines, compilers, networking, and large-scale infrastructure.

This repository documents that journey.

The goal isn't simply to learn another programming language or complete coding challenges.

The goal is to build the depth required to understand and engineer software systems from first principles.

---

## Philosophy

Modern software development is built on layers of abstraction.

Frameworks hide operating systems.

Operating systems hide hardware.

Databases hide storage engines.

Cloud platforms hide distributed systems.

This project intentionally peels back those layers.

Instead of only using technology, the objective is to understand how and why it works.

Every project in this repository is designed to recreate a production-grade system from scratch while studying the computer science concepts behind it.

---

# Repository Structure

```
.
├── CodeForces/
│   ├── 800/
│   ├── 900/
│   ├── 1000/
│   └── ...
│
├── searchengine/
│
├── redis/
│
├── git/
│
├── httpserver/
│
├── ...
│
└── README.md
```


---

# Escape the Abstraction Roadmap

The projects gradually move lower into the software stack.

| Project | Focus |
|----------|------|
| Search Engine | Information Retrieval, Indexing, Ranking |
| Redis Clone | Memory Management, Networking, Data Structures |
| Git Clone | Content Addressing, Storage, Hashing |
| HTTP Server | TCP/IP, HTTP, Concurrency |
| Reverse Proxy & Load Balancer | Networking, Scheduling |
| Database Engine | Storage Engines, B-Trees, Transactions |
| Kafka Clone | Distributed Logs, Replication |
| Distributed Cache | Consistency, Partitioning |
| Raft Consensus | Distributed Consensus |
| Matching Engine | Low-Latency Systems |
| Programming Language | Parsing, ASTs, Compilation |
| Virtual Machine | Bytecode, Execution |

---

# Competitive Programming

Alongside the systems projects, I solve Codeforces problems in C++ to continuously improve problem solving, algorithmic thinking, and data structure intuition.

Solutions are organised by difficulty.

---

# Learning Goals

This repository focuses on developing deep understanding of:

- Data Structures
- Algorithms
- Operating Systems
- Computer Networks
- Information Retrieval
- Distributed Systems
- Database Internals
- Concurrency
- Memory Management
- Compilers
- Storage Engines
- Search Engines
- Performance Engineering
- System Design

---

# Principles

- Learn by building.
- Read production code.
- Prefer understanding over memorisation.
- Build systems from first principles.
- Write simple, maintainable code.
- Continuously refactor with new knowledge.
- Document the reasoning behind design decisions.

---

# Tech Stack

- C++
- CMake
- Catch2 / GoogleTest
- Docker
- Linux
- Git

---

# Progress

- [ ] Search Engine
- [ ] Redis Clone
- [ ] Git Clone
- [ ] HTTP Server
- [ ] Reverse Proxy
- [ ] Database Engine
- [ ] Kafka Clone
- [ ] Distributed Cache
- [ ] Raft Consensus
- [ ] Matching Engine
- [ ] Programming Language
- [ ] Virtual Machine

---

# Why?

The objective isn't to reinvent existing software.

It's to understand the engineering trade-offs behind the software that powers modern computing.

Every project is an opportunity to answer questions like:

- Why is Redis so fast?
- Why does Lucene use immutable segments?
- How does Git store history?
- Why are B-Trees used in databases?
- How does Raft guarantee consensus?
- Why are search engines built the way they are?
- How do operating systems schedule work?
- How do distributed systems remain available?

Understanding these systems makes it possible to design better software regardless of language, framework, or industry.

---

> "The best engineers don't just know how to use abstractions—they know what's underneath them."