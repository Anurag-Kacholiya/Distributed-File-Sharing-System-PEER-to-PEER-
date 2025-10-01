# Distributed File Sharing System

This project is a peer-to-peer (P2P) file sharing system implemented in C++. It consists of two main components: a `tracker` server and a `client` application. The system allows users to form groups and share files directly with each other within those groups. Two tracker servers work in tandem to provide redundancy and state synchronization.

---

## Architecture Overview

The system follows a hybrid P2P model. Centralized trackers manage metadata, while file transfers occur directly between decentralized clients (peers).

1.  **Tracker System**:
    * Consists of two synchronized servers.
    * Manages user accounts, authentication, and session state.
    * Manages group creation, memberships, and join requests.
    * Stores file metadata: file name, size, SHA1 hashes (for the full file and each piece), and a list of clients (seeders) who have the file.
    * Handles peer discovery by providing clients with a list of seeders for a requested file.

2.  **Client System**:
    * Acts as both a downloader and a seeder.
    * Communicates with a tracker for user/group management and to get file metadata.
    * Runs a server thread to listen for incoming connections from other peers requesting file pieces.
    * Downloads files by fetching pieces concurrently from multiple peers.
    * Verifies the integrity of each downloaded piece using its SHA1 hash.

3.  **File Management**:
    * Files are divided into 512KB pieces.
    * Data integrity is ensured using SHA1 hashes for each piece and the complete file. Corrupted pieces are re-downloaded.

---

## Network Protocol Design

All communication is over TCP/IP sockets using a custom, length-prefixed, space-delimited protocol.

* **Message Format**: Every message is prefixed with its total length as a fixed-size string, allowing the receiver to read the exact number of bytes.
    * Example: `create_user <user_id> <password>`
* **Responses**: The tracker sends responses that start with a status code (`SUCCESS` or `ERROR`) followed by relevant information.
* **Tracker Synchronization**: State-changing commands (e.g., creating a user, uploading a file) received by one tracker are forwarded to the other tracker using a special `SYNC` command prefix. This ensures both trackers maintain a consistent view of the network state.

---

## Key Algorithms & Data Structures

### Data Structures

* **Tracker**:
    * Uses `std::map` to store users, groups, and logged-in sessions.
    * `std::set` is used for group members and pending requests for efficient lookup and insertion.
    * All shared data structures are protected by `std::mutex` to ensure thread safety during concurrent client requests.
* **Client**:
    * `std::map` tracks ongoing downloads, mapping filenames to download progress and metadata.
    * A `std::vector<bool>` tracks which pieces of a file have been successfully downloaded.

### Algorithms

* **SHA1 Hashing**: A standard, self-contained SHA1 implementation is used for file and piece integrity verification. This avoids external library dependencies.
* **Piece Selection Strategy**: A simple "rarest first" approach is approximated. The client requests pieces sequentially from the beginning of the needed-pieces list. By connecting to different peers for different pieces, it naturally distributes the load and increases download speed.
* **Concurrency**: The system is heavily multi-threaded.
    * The tracker spawns a new thread for each connected client.
    * The client spawns a thread for its seeder server.
    * The client spawns a new thread for each file download, allowing multiple files to be downloaded simultaneously.

---

## Compilation and Execution

### Prerequisites

* A C++11 compliant compiler (e.g., `g++`).
* `make` build utility.

### Compilation

Navigate to each directory (`tracker/` and `client/`) and run `make`:

```bash
# In the tracker directory
cd tracker/
make

# In the client directory
cd ../client/
make