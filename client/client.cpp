#include "client.h"
#include "utils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <random>
#include <iomanip>
#include <openssl/sha.h>

#define MSG_SIZE 512*1024

using namespace std;

Client::Client(const string& tracker_info_file) {
    ifstream file(tracker_info_file);
    if (!file.is_open()) {
        perror("Failed to open tracker_info.txt");
        exit(EXIT_FAILURE);
    }
    string line;
    while(getline(file, line)) {
        if(!line.empty()) {
            tracker_addresses.push_back(line);
        }
    }
    if (tracker_addresses.size() < 2) {
        cerr << "FATAL: tracker_info.txt must contain at least two tracker addresses." << endl;
        exit(EXIT_FAILURE);
    }
}

void Client::run() {
    thread seeder_thread(&Client::start_seeder_service, this);
    seeder_thread.detach();
    
    this_thread::sleep_for(chrono::milliseconds(100));

    if (!connect_to_available_tracker()) {
        return;
    }

    process_user_input();
    
    if (tracker_socket != -1) {
        close(tracker_socket);
    }
}

bool Client::try_connect_to(const string& addr) {
    size_t delim_pos = addr.find(':');
    if (delim_pos == string::npos) return false;
    string ip = addr.substr(0, delim_pos);
    int port = stoi(addr.substr(delim_pos + 1));

    tracker_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tracker_socket < 0) return false;

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

    if (connect(tracker_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(tracker_socket);
        tracker_socket = -1;
        return false;
    }
    
    log_message("Successfully connected to tracker at " + addr);
    return true;
}

bool Client::connect_to_available_tracker() {
    if (try_connect_to(tracker_addresses[current_tracker_idx])) {
        return true;
    }

    log_message("Could not connect to primary tracker. Failing over...");
    current_tracker_idx = (current_tracker_idx + 1) % tracker_addresses.size();
    
    if (try_connect_to(tracker_addresses[current_tracker_idx])) {
        return true;
    }

    cerr << "FATAL: Both trackers appear to be down." << endl;
    return false;
}

string Client::send_to_tracker(const string& command, bool is_retry) {
    if (tracker_socket < 0) {
        return "ERROR: Not connected to any tracker.";
    }

    auto attempt_failover_and_retry = [&]() -> string {
        if (is_retry) {
            return "ERROR: Failed to send command to the secondary tracker.";
        }

        log_message("Connection lost. Attempting to reconnect and retry...");
        close(tracker_socket);
        tracker_socket = -1;

        if (!connect_to_available_tracker()) {
            return "ERROR: All trackers are down.";
        }

        if (is_logged_in) {
            log_message("Re-authenticating session with new tracker...");
            string login_cmd = "login " + user_id + " " + password + " " + to_string(seeder_port);
            
            send(tracker_socket, login_cmd.c_str(), login_cmd.length(), 0);
            
            char login_buffer[MSG_SIZE] = {0};
            read(tracker_socket, login_buffer, MSG_SIZE);
            string login_response(login_buffer);

            if(login_response.find("SUCCESS") == string::npos) {
                log_message("Warning: Re-login failed. You may need to login manually.");
                is_logged_in = false;
            } else {
                 log_message("Re-authentication successful.");
            }
        }
        
        return send_to_tracker(command, true);
    };

    if (send(tracker_socket, command.c_str(), command.length(), 0) < 0) {
        return attempt_failover_and_retry();
    }

    char buffer[MSG_SIZE] = {0};
    ssize_t bytes_read = read(tracker_socket, buffer, MSG_SIZE);
    
    if (bytes_read <= 0) {
        return attempt_failover_and_retry();
    }

    return string(buffer);
}


void Client::process_user_input() {
    string line;
    while (cout << "> " && getline(cin, line)) {
        auto args = split_string(line, " ");
        if (args.empty()) continue;

        const string& command = args[0];
        if (command == "quit") break;

        if (command == "login") {
            handle_login(args);
        } else if (command == "upload_file") {
            handle_upload(args);
        } else if (command == "download_file") {
            handle_download(args);
        } else if (command == "show_downloads") {
            show_downloads();
        } else {
            string response = send_to_tracker(line);
            cout << response << endl;
            if (command == "logout" && response.find("SUCCESS") != string::npos) {
                is_logged_in = false;
                user_id = "";
                password = "";
            }
        }
    }
}

void Client::handle_login(const vector<string>& args) {
    if (args.size() != 3) {
        cout << "Usage: login <user_id> <password>" << endl;
        return;
    }
    string command = args[0] + " " + args[1] + " " + args[2] + " " + to_string(seeder_port);
    string response = send_to_tracker(command);
    cout << response << endl;

    if (response.find("SUCCESS") != string::npos) {
        is_logged_in = true;
        user_id = args[1];
        password = args[2];
    }
}

void Client::start_seeder_service() {
    int seeder_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in seeder_addr;
    seeder_addr.sin_family = AF_INET;
    seeder_addr.sin_addr.s_addr = INADDR_ANY;
    
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> distrib(10000, 65000);
    
    while(true) {
        seeder_port = distrib(gen);
        seeder_addr.sin_port = htons(seeder_port);
        if (bind(seeder_socket, (struct sockaddr*)&seeder_addr, sizeof(seeder_addr)) == 0) {
            break;
        }
    }
    
    listen(seeder_socket, 10);
    log_message("Seeder listening on port " + to_string(seeder_port));
    
    while (true) {
        int peer_sock = accept(seeder_socket, nullptr, nullptr);
        if (peer_sock < 0) continue;
        thread peer_handler(&Client::handle_peer_connection, this, peer_sock);
        peer_handler.detach();
    }
    close(seeder_socket);
}

void Client::handle_peer_connection(int peer_socket) {
    char buffer[1024] = {0};
    read(peer_socket, buffer, 1024);
    
    auto args = split_string(string(buffer), " ");
    if (args.size() == 3 && args[0] == "get_piece") {
        const string& filename = args[1];
        int piece_index = stoi(args[2]);

        string file_path;
        {
            lock_guard<mutex> lock(shared_files_mutex);
            if (shared_files.count(filename)) {
                file_path = shared_files.at(filename);
            }
        }

        if (!file_path.empty()) {
            ifstream file(file_path, ios::binary);
            if (file.is_open()) {
                file.seekg((long long)piece_index * PIECE_SIZE, ios::beg);
                char piece_buffer[PIECE_SIZE];
                file.read(piece_buffer, PIECE_SIZE);
                streamsize bytes_read = file.gcount();
                if (bytes_read > 0) {
                    send(peer_socket, piece_buffer, bytes_read, 0);
                }
            }
        }
    }
    close(peer_socket);
}

void Client::handle_upload(const vector<string>& args) {
    if (args.size() != 3) {
        cout << "Usage: upload_file <group_id> <file_path>" << endl;
        return;
    }
    if (!is_logged_in) {
        cout << "You must be logged in to upload files." << endl;
        return;
    }

    const string& group_id = args[1];
    const string& file_path = args[2];
    string filename = file_path.substr(file_path.find_last_of("/\\") + 1);

    ifstream file(file_path, ios::binary | ios::ate);
    if (!file.is_open()) {
        cout << "ERROR: Cannot open file " << file_path << endl;
        return;
    }
    
    long long file_size = file.tellg();
    file.seekg(0, ios::beg);
    
    vector<string> piece_hashes;
    char piece_buffer[PIECE_SIZE];

    SHA_CTX sha_context;
    SHA1_Init(&sha_context);

    while(file.read(piece_buffer, PIECE_SIZE) || file.gcount() > 0) {
        streamsize bytes_read = file.gcount();
        if (bytes_read > 0) {
            piece_hashes.push_back(calculate_sha1(piece_buffer, bytes_read));
            SHA1_Update(&sha_context, piece_buffer, bytes_read);
        }
    }

    unsigned char full_hash_raw[SHA_DIGEST_LENGTH];
    SHA1_Final(full_hash_raw, &sha_context);

    stringstream ss;
    ss << hex << setfill('0');
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        ss << setw(2) << static_cast<unsigned int>(full_hash_raw[i]);
    }
    string full_file_hash = ss.str();
    
    stringstream command_stream;
    command_stream << "upload_file " << group_id << " " << filename << " " << file_size << " " << full_file_hash;
    for(const auto& p_hash : piece_hashes) {
        command_stream << " " << p_hash;
    }

    string response = send_to_tracker(command_stream.str());
    cout << response << endl;

    if (response.find("SUCCESS") != string::npos) {
        lock_guard<mutex> lock(shared_files_mutex);
        shared_files[filename] = file_path;
    }
}

void Client::handle_download(const vector<string>& args) {
    if (args.size() != 4) {
        cout << "Usage: download_file <group_id> <file_name> <destination_path>" << endl;
        return;
    }
    if (!is_logged_in) {
        cout << "You must be logged in." << endl;
        return;
    }

    string command = "download_file " + args[1] + " " + args[2];
    string response = send_to_tracker(command);
    
    auto metadata = split_string(response, " ");
    if (metadata[0] == "SUCCESS") {
        log_message("Starting download for " + args[2]);
        thread downloader(&Client::download_manager, this, args[1], args[2], args[3], metadata);
        downloader.detach();
    } else {
        cout << response << endl;
    }
}

void Client::download_manager(const string& group_id, const string& filename, const string& dest_path, const vector<string>& metadata) {
    DownloadState state;
    state.group_id = group_id;
    state.filename = filename;
    state.destination_path = dest_path;
    state.file_size = stoll(metadata[1]);
    state.status = "Downloading";
    state.total_pieces = (state.file_size + PIECE_SIZE - 1) / PIECE_SIZE;
    state.pieces_downloaded.resize(state.total_pieces, false);
    
    size_t seeder_start_index = 3 + state.total_pieces;
    for (int i = 0; i < state.total_pieces; ++i) {
        state.piece_hashes[i] = metadata[3 + i];
    }
    
    vector<string> seeders;
    for (size_t i = seeder_start_index; i < metadata.size(); ++i) {
        seeders.push_back(metadata[i]);
    }

    {
        lock_guard<mutex> lock(downloads_mutex);
        ongoing_downloads[filename] = state;
    }

    ofstream out_file(dest_path, ios::binary | ios::out);
    if (!out_file.is_open()) {
        log_message("Failed to create destination file: " + dest_path);
        lock_guard<mutex> lock(downloads_mutex);
        ongoing_downloads[filename].status = "Failed";
        return;
    }
    out_file.close(); 

    int pieces_to_get = state.total_pieces;
    int seeder_idx = 0;
    
    for (int i = 0; i < state.total_pieces; ++i) {
        bool piece_ok = false;
        while(!piece_ok) {
            if (seeders.empty()) {
                 log_message("No more seeders. Download failed for " + filename);
                 lock_guard<mutex> lock(downloads_mutex);
                 ongoing_downloads[filename].status = "Failed";
                 return;
            }
            string seeder_addr = seeders[seeder_idx % seeders.size()];
            seeder_idx++;
            
            int peer_sock = socket(AF_INET, SOCK_STREAM, 0);
            size_t delim_pos = seeder_addr.find(':');
            string peer_ip = seeder_addr.substr(0, delim_pos);
            int peer_port = stoi(seeder_addr.substr(delim_pos + 1));
            
            sockaddr_in peer_server_addr;
            peer_server_addr.sin_family = AF_INET;
            peer_server_addr.sin_port = htons(peer_port);
            inet_pton(AF_INET, peer_ip.c_str(), &peer_server_addr.sin_addr);

            if (connect(peer_sock, (struct sockaddr*)&peer_server_addr, sizeof(peer_server_addr)) < 0) {
                log_message("Failed to connect to seeder " + seeder_addr);
                close(peer_sock);
                continue;
            }

            log_message("Downloading piece " + to_string(i) + " from seeder " + seeder_addr);

            string request = "get_piece " + filename + " " + to_string(i);
            send(peer_sock, request.c_str(), request.length(), 0);
            
            // --- THIS IS THE FINAL BUG FIX ---
            // We must loop on read() to get the entire piece from the stream.

            char piece_buf[PIECE_SIZE];
            long long piece_size_to_expect = PIECE_SIZE;
            // The last piece might be smaller
            if (i == state.total_pieces - 1) {
                piece_size_to_expect = state.file_size % PIECE_SIZE;
                if (piece_size_to_expect == 0) {
                    piece_size_to_expect = PIECE_SIZE;
                }
            }
            
            long long total_bytes_read = 0;
            while (total_bytes_read < piece_size_to_expect) {
                ssize_t bytes_read_now = read(peer_sock, piece_buf + total_bytes_read, piece_size_to_expect - total_bytes_read);
                if (bytes_read_now <= 0) {
                    // Seeder disconnected prematurely
                    log_message("Seeder disconnected while reading piece " + to_string(i));
                    total_bytes_read = -1; // Signal an error
                    break;
                }
                total_bytes_read += bytes_read_now;
            }

            // --- END OF BUG FIX ---

            if (total_bytes_read > 0) {
                string received_hash = calculate_sha1(piece_buf, total_bytes_read);
                if (received_hash == state.piece_hashes[i]) {
                    fstream file(dest_path, ios::binary | ios::in | ios::out);
                    file.seekp((long long)i * PIECE_SIZE, ios::beg);
                    file.write(piece_buf, total_bytes_read);
                    file.close();
                    
                    piece_ok = true;
                    pieces_to_get--;

                    lock_guard<mutex> lock(downloads_mutex);
                    ongoing_downloads[filename].pieces_downloaded[i] = true;
                } else {
                    log_message("Hash mismatch for piece " + to_string(i) + ". Retrying.");
                }
            }
            close(peer_sock);
        }
    }

    if (pieces_to_get == 0) {
        log_message("Download completed for " + filename);
        lock_guard<mutex> lock(downloads_mutex);
        ongoing_downloads[filename].status = "Completed";
        
        {
            lock_guard<mutex> share_lock(shared_files_mutex);
            shared_files[filename] = dest_path;
        }

        string command = "i_am_seeder " + group_id + " " + filename;
        send_to_tracker(command);
    }
}

void Client::show_downloads() {
    lock_guard<mutex> lock(downloads_mutex);
    if (ongoing_downloads.empty()) {
        cout << "No active or completed downloads." << endl;
        return;
    }
    for (const auto& pair : ongoing_downloads) {
        const auto& state = pair.second;
        if (state.status == "Completed") {
            cout << "[C] [" << state.group_id << "] " << state.filename << endl;
        } else {
            cout << "[D] [" << state.group_id << "] " << state.filename << endl;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./client <tracker_info.txt>" << endl;
        return 1;
    }

    string tracker_info_file = argv[1];

    Client client(tracker_info_file);
    client.run();

    return 0;
}