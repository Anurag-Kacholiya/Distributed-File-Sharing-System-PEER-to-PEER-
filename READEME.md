# AOS - Assignment 3

#### submitted by : Anurag Kacholiya (2025202025)

---

## Distributed File Sharing System (peer-to-peer)

This project is a peer-to-peer (P2P) file sharing system implemented in C++ using low-level system calls. It allows users to form groups, share files within those groups, and download files directly from other peers. The system is designed for robustness, featuring redundant tracker servers for metadata management and client-side failover.

## Overview

The system operates on a hybrid P2P model where two central **Tracker Servers** manage metadata, while file transfers occur directly between decentralized **Clients** (peers). This architecture avoids storing large files on a central server, distributing the load across the network.

- **Redundancy:** Two trackers work in tandem, synchronizing state information to ensure the system remains operational even if one tracker fails.
- **Efficiency:** The use of low-level system calls (`open`, `read`, `write`, `lseek`) for all file I/O ensures memory efficiency, allowing the system to handle large files (up to 1GB) without loading them entirely into memory.
- **Integrity:** All files are verified using SHA1 hashing at both the full-file and individual piece levels to prevent data corruption.

---

## System Architecture

### Tracker System
The tracker system consists of two synchronized servers responsible for:
-   **User Management:** Handling user registration (`create_user`) and authentication (`login`).
-   **Group Management:** Managing group creation, memberships, and join requests.
-   **Metadata Storage:** Storing information about shared files, including their size, piece hashes, and a list of clients (seeders) who currently have the file.
-   **Peer Discovery:** Providing clients with the IP addresses of seeders when a download is requested.
-   **Synchronization:** Forwarding any state-changing command to the other tracker to maintain a consistent state across the network.

### Client System
The client application is the user's interface to the network and has a dual role:
-   **Leecher (Downloader):** When downloading a file, the client fetches pieces from one or more seeders. The current implementation downloads pieces **sequentially**.
-   **Seeder (Uploader):** The client runs a background thread that listens for incoming connections from other peers and serves requested file pieces. Any client that has a complete file automatically becomes a seeder.
-   **Failover:** The client is designed to be resilient. If its connection to the primary tracker is lost, it will automatically attempt to connect to the secondary tracker and re-establish its session.

### Network Protocol
All communication uses TCP sockets with a custom, human-readable protocol:
-   **Format:** Messages are plain-text, space-delimited commands (e.g., `create_user userA pass123`).
-   **Reliability:** TCP ensures that all commands and responses are delivered reliably.
-   **Simplicity:** The text-based protocol simplifies debugging and implementation.

---

## Directory Structure
The project is organized into two main directories:
````
.
├── client/
│   ├── client.cpp
│   ├── client.h
│   ├── utils.cpp
│   ├── utils.h
│   └── Makefile
├── tracker/
│   ├── tracker.cpp
│   ├── tracker.h
│   ├── utils.cpp
│   ├── utils.h
│   └── Makefile
└── README.md
````
---

## Compilation and Execution

### Compilation
1.  Navigate to the `tracker` directory and compile:
    ```bash
    cd tracker/
    make clean && make
    ```
2.  Navigate to the `client` directory and compile:
    ```bash
    cd ../client/
    make clean && make
    ```

### Execution
1.  **Prepare `tracker_info.txt`:** In your main project directory, create a file named `tracker_info.txt` that contains the IP and port for each tracker on a new line.
    ```
    127.0.0.1:8080
    127.0.0.1:8081
    ```

2.  **Start Trackers:** Open two separate terminals.
    ```bash
    # In Terminal 1
    ./tracker/tracker tracker_info.txt 1

    # In Terminal 2
    ./tracker/tracker tracker_info.txt 2
    ```

3.  **Start Clients:** Open a new terminal for each client you wish to run.
    ```bash
    # The client only needs the path to the info file
    ./client/client tracker_info.txt
    ```

---

## Algorithms and Data Structures

### Data Structures

-   **Tracker:**
    -   `std::map`: Used extensively for efficient, key-based lookups ($O(\log N)$).
        -   `users`: `string user_id -> string password`
        -   `groups`: `string group_id -> Group struct`
        -   `logged_in_users`: `string user_id -> string ip:port`
    -   `std::set`: Used for group members and pending join requests to ensure uniqueness and provide fast insertion/lookup.
    -   `std::mutex`: Crucially, every shared data structure on the tracker is protected by a `std::mutex` to ensure thread safety against concurrent requests from multiple clients.

-   **Client:**
    -   `std::map`: Used to track ongoing downloads (`string filename -> DownloadState struct`) and local shared files (`string filename -> string filepath`).

### Algorithms

-   **SHA1 Hashing:** Integrity is the cornerstone of the file transfer process.
    -   During upload, the client reads the file in **512KB chunks**. For each chunk, it computes a SHA1 hash. It also maintains a streaming SHA1 context to compute the hash of the full file simultaneously. This memory-efficient approach avoids loading large files into RAM.
    -   During download, for every piece received, the client computes its hash and compares it against the official hash provided by the tracker. If they don't match, the piece is discarded and re-requested.

-   **Tracker Synchronization:** A simple and effective **one-way command forwarding** model is used.
    -   When a tracker receives a state-changing command (e.g., `create_user`), it first applies the change to its own in-memory data structures.
    -   It then immediately forwards the same command to the other tracker.
    -   This is justified by the project constraint that a downed tracker never comes back online, which eliminates the risk of split-brain or state reconciliation problems.

-   **File Handling (System Calls):** To meet the project constraints and ensure memory efficiency, all file I/O related to uploading, seeding, and downloading is handled using low-level POSIX system calls:
    -   `open()`: To get a file descriptor for reading or writing.
    -   `read()`: To read a file in chunks into a buffer.
    -   `write()`: To write a received piece from a buffer to a file.
    -   `lseek()`: To move the file pointer to the correct position for writing a specific piece.
    -   `fstat()`: To get the total file size without reading the file.
    -   `close()`: To release the file descriptor.

-   **Piece Selection:** The download algorithm is **sequential**. The `download_manager` contains a `for` loop that requests piece 0, then piece 1, then piece 2, and so on, in strict order.

---

## Performance Analysis Report

### Implementation Approach

The system was designed with a focus on simplicity, robustness, and adherence to the project constraints (no high-level libraries, memory efficiency).

1.  **Networking & Protocol:** A custom, text-based protocol over **TCP** was chosen. TCP guarantees reliable, in-order delivery of commands, which is essential for a stateful system. A simple, space-delimited text format was selected for ease of implementation, debugging, and parsing without external libraries.

2.  **Concurrency Model:** The system is heavily multi-threaded to ensure responsiveness.
    -   **Tracker:** A **thread-per-client** model is used. The main thread listens for new connections, and each accepted connection is handed off to a new thread. This isolates clients from one another and is simple to implement.
    -   **Client:** The client utilizes three types of threads: the main thread for user input, a single background thread for the seeder service (listening for piece requests), and a **thread-per-download** for file transfers. This allows a user to initiate multiple downloads concurrently while still being able to type new commands.

3.  **State Management & Synchronization:**
    -   All tracker state is stored **in-memory** using standard C++ data structures (`std::map`, `std::set`). This provides extremely fast access but means state is lost if both trackers shut down. This is an acceptable trade-off given the project scope.
    -   All access to these shared data structures is protected by `std::mutex` to prevent race conditions.
    -   The **one-way command forwarding** for synchronization was chosen for its simplicity. It perfectly fits the constraint that a downed tracker never returns, thus avoiding the need for complex consensus algorithms like Paxos or Raft.

4.  **Client-Side Failover:** The client is designed to be resilient. The `send_to_tracker` function includes robust error handling. If a `send` or `read` call fails (indicating a dead connection), the client automatically tries to connect to the secondary tracker and re-authenticates its session by sending a `login` command again. This makes the failover process seamless for the user.

### Performance Metrics and Analysis

Performance was evaluated based on the design and expected behavior under different conditions.

-   **File Size vs. Download Time:**
    -   **Small Files (< 512KB):** For single-piece files, performance is dominated by **network latency**. The overhead of TCP handshakes, tracker communication (requesting download info), and peer-to-peer connection setup takes more time than the actual data transfer.
    -   **Large Files (> 512KB):** For multi-piece files, performance is dominated by **network bandwidth**. The initial setup latency becomes negligible, and the total download time scales linearly with the file size. For example, a 100MB file is expected to take roughly 10 times longer than a 10MB file on the same network connection.

-   **Effect of Multiple Peers:**
    -   The current implementation uses a **sequential piece selection** algorithm. It downloads piece 0, then piece 1, and so on. Even if multiple seeders are available, the downloader only connects to one peer at a time for each piece.
    -   Therefore, in its current state, having more seeders **does not increase the download speed for a single client**. It only provides redundancy if one seeder goes offline. To achieve a speedup from multiple peers, a parallel download algorithm (requiring a thread pool on the client) would be necessary.

-   **Tracker Performance:**
    -   Tracker operations are extremely fast. User and group lookups rely on `std::map`, which provides logarithmic time complexity ($O(\log N)$). In a system with a few dozen users and groups, response times are effectively instantaneous.
    -   The primary performance consideration for the tracker is the overhead of mutex locking. Under very heavy concurrent load, there could be some contention, but for the scale of this project, it is not a bottleneck.

### Design Justification Summary

The design choices were driven by the project's constraints and the goal of creating a functional, robust system.
-   The use of **low-level system calls** for file I/O directly addresses the memory efficiency requirement for large files.
-   The **in-memory state with mutex protection** on the tracker provides a fast and simple solution for metadata management without violating the "no database" rule.
-   The **thread-per-download** and **client-side failover** logic make the client application responsive and resilient.
-   The **one-way command forwarding** for synchronization is the simplest possible model that satisfies the project's redundancy requirement under the given assumptions.