# Escape the Abstraction

> A long-term engineering journey to understand computing from first principles—starting with software systems, descending through operating systems and kernels, and ultimately into firmware, embedded systems, reverse engineering, and security.

The objective isn't to recreate existing software.

The objective is to become the kind of engineer who can understand, build, debug, reverse engineer and secure any computing system.

---

# Philosophy

Modern computing is built upon layers of abstraction.

Frameworks hide distributed systems.

Distributed systems hide networking.

Operating systems hide hardware.

Hardware hides the processor.

Abstractions are incredibly useful—but they can also hide understanding.

This repository intentionally peels those layers away.

Every project exists to answer one question:

> **How does this system actually work?**

Instead of consuming technology, this repository is about engineering it.

---

# Roadmap

The journey is divided into four layers.

Each layer builds the foundation for the next.

```
Competitive Programming
          │
          ▼
Layer 1 ─ Systems Engineering
          │
          ▼
Layer 2 ─ Reverse Engineering
          │
          ▼
Layer 3 ─ Operating Systems & Kernels
          │
          ▼
Layer 4 ─ Firmware & Embedded Systems
```

---

# Competitive Programming

Alongside every layer is an ongoing competitive programming journey using C++.

The goal isn't only interview preparation.

It is to continuously improve:

- Algorithmic thinking
- Problem solving
- Data structures
- Mathematical reasoning
- Writing correct code under pressure

Solutions are organised by Codeforces rating.

```
CodeForces/
├── 800/
├── 900/
├── 1000/
└── ...
```

---

# Layer 1 — Systems Engineering

Understanding modern backend infrastructure by rebuilding production systems.

Projects include:

- Search Engine
- HTTP Server
- Database Engine
- Reverse Proxy & Load Balancer

Focus areas:

- C++
- Networking
- Storage Engines
- Concurrency
- Performance
- Distributed Systems (MAYBE?)
- System Design

---

# Layer 2 — Reverse Engineering

Learning to understand software without source code.

Focus areas:

- Assembly
- x86-64
- ARM64
- Binary Analysis
- Debugging
- Memory Corruption
- Exploit Development
- Program Analysis

---

# Layer 3 — Operating Systems & Kernels

Understanding how software executes.

Topics include:

- Boot Process
- Virtual Memory
- Scheduling
- Processes
- Threads
- System Calls
- File Systems
- Synchronisation
- Kernel Development

---

# Layer 4 — Firmware & Embedded Systems

Applying everything from the previous layers to connected devices.

Projects include:

- Secure Door Access Platform
- Industrial Monitoring System
- IoT Sensor Network
- Building Automation
- Embedded Security Labs

Focus areas:

- Embedded C
- Firmware
- Pico W
- Networking
- IoT
- Reverse Engineering
- Hardware Security

---

# Investigation Framework

Alongside the engineering projects is a reusable investigation framework.

Every investigation follows the same workflow:

```
Target
    │
Recon
    │
Architecture Model
    │
Hypothesis
    │
Experiment
    │
Evidence
    │
Finding
    │
Report
```

The framework is designed to become a long-term engineering notebook for:

- Bug bounty
- Reverse engineering
- Vulnerability research
- Embedded systems
- Security assessments

---

# Repository Structure

```
.
├── codeforces/
│
├── layer1/
│   ├── search-engine/
│   ├── http-server/
│   ├── distributed-cache/
│   ├── database-engine/
│   ├── object-storage/
│   └── ...
│
├── layer2/
│
├── layer3/
│
├── layer4/
│
└── README.md
```

---

# Learning Principles

- Build before reading production implementations.
- Read production implementations afterwards.
- Understand trade-offs before optimising.
- Measure performance instead of guessing.
- Document every important design decision.
- Learn from failures and iterate.

---

# Long-Term Goal - Specialization in REVERSE ENGINEERING

To develop the engineering depth required to confidently investigate, build, reverse engineer and secure systems ranging from:

- Backend Infrastructure
- Operating Systems
- Mobile Applications
- Desktop Software
- Firmware
- Embedded Devices
- Industrial Systems
- Automotive Systems
- Robotics

The destination isn't a specific job title.

The destination is becoming an engineer capable of understanding any computing system from first principles.

---

> "The best engineers don't just use abstractions—they understand what lies beneath them."