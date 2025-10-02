# Performance Analysis Report

## 1. Overview

This report details the design choices, implementation approach, and performance characteristics of the distributed file-sharing system.

The primary goal was to create a **robust, memory-efficient, and resilient P2P network** using only **low-level system calls** as per the project constraints.

The architecture consists of:

* Two synchronizing **tracker servers** for metadata management.
* Multiple **clients (peers)** that handle file transfers directly.

---

## 2. Implementation Approach & Design Justification

### 2.1. System Architecture

A **hybrid peer-to-peer model** was chosen:

* Central **tracker servers** manage metadata (user accounts, groups, file locations).
* Clients handle **direct file transfers**.

**Justification:**

* Centralized trackers simplify peer discovery.
* Decentralized file transfer avoids server bottlenecks and enables scalability.

---

### 2.2. Network Protocol

A **custom text-based protocol over TCP** was implemented.

* **TCP** ensures reliable, in-order delivery—critical for commands like `create_user`.
* **Text-based format** (e.g., `login userA pass123`):

  * Satisfies the "no high-level libraries" constraint.
  * Human-readable for easier debugging.
  * Flexible without requiring complex binary formats.

---

### 2.3. Concurrency Model

* **Tracker**: Uses a **thread-per-client model**.

  * Main thread listens for connections.
  * A new thread handles each client independently.
  * A slow client cannot block others.

* **Client**: Uses a **thread-per-download model**.

  * Each download runs in its own thread.
  * The user console remains responsive.
  * A background **seeder thread** serves file pieces to peers.

---

### 2.4. State Management & Synchronization

* **Tracker State**: Stored **in-memory** with `std::map` and `std::set` → fast metadata access.
* **Thread Safety**: Enforced with `std::mutex` to prevent race conditions.
* **Synchronization**:

  * A **one-way command forwarding model** is used.
  * State-changing commands are forwarded as `SYNC_` messages to the other tracker.
  * Simplifies synchronization—no need for complex consensus algorithms.

---

### 2.5. File Handling & Integrity

* **System Calls Used**: `open()`, `read()`, `write()`, `lseek()`, `fstat()`.
* Files are processed in **512KB chunks** → memory-efficient for large files.

**SHA1 Hashing Workflow:**

* **Upload**: Each chunk is hashed; a global SHA1 hash is computed in a streaming fashion.
* **Download**: Each chunk’s hash is verified before acceptance.

  * Mismatches trigger retries → guarantees integrity.

---

### 2.6. Client-Side Failover

* Failover logic is inside `send_to_tracker()`.
* **Failure Detection**: `send()` or `read()` returns `< 0` or `0`.
* **Recovery Process**:

  1. Close dead socket.
  2. Connect to next tracker from `tracker_info.txt`.
  3. If logged in before, auto-login again.
  4. Re-send failed command.

---

## 3. Performance Analysis

### 3.1. File Size vs. Download Time

* **Small Files (< 512KB)**: Latency-bound (network round-trips dominate).
* **Large Files (> 512KB)**: Bandwidth-bound, with download time scaling **linearly** with file size.

---

### 3.2. Impact of Multiple Seeders

* Current algorithm: **Sequential piece download**.
* More seeders **do not increase speed**, but improve **availability**.
* **Future Improvement**: Parallel chunk downloading using thread pools.

---

### 3.3. Tracker Performance

* Metadata lookups: `std::map` → **O(log N)**.
* For the project scale (dozens of users, hundreds of files), lookups are near-instant.
* Tracker is **not a bottleneck**—it spends most time waiting for I/O.

---

## 4. Conclusion

The implemented system:

* Meets all project requirements.
* Provides a **robust, memory-efficient, and resilient** distributed file-sharing network.
* Design choices (hybrid architecture, system-call-based I/O, simple sync model) are justified by constraints.
* Performance scales predictably:

  * Small files → latency-dominated.
  * Large files → bandwidth-dominated.
* Current sequential download model works well, but the system is ready for extension to **parallel piece downloading**.