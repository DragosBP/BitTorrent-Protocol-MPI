#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unordered_map>
#include <vector>
#include <utility>


#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 33
#define MAX_CHUNKS 100

#define NACK 0
#define ACK 1

#define REQUEST 10
#define UPDATE 20
#define DONE 30

#define TRACKER_TAG 100
#define INFO_TAG 200
#define DATA_TAG 300

std::unordered_map<std::string, std::vector<std::string>> files;
std::vector<std::string> want;

void *download_thread_func(void *arg)
{
    unsigned int rank = *(unsigned int*) arg;

    std::unordered_map<std::string, std::pair<std::vector<std::string>, long unsigned int>> file_segments;
    std::unordered_map<std::string, std::pair<std::vector<int>, unsigned int>> swarms;

    char action;
    action = REQUEST;

    int nr_wanted = want.size();
    // MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN);

    for (int i = 0; i < nr_wanted; i++) {
        // Make request to tracker
        MPI_Send(&action, 1, MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);

        // Send the file name
        MPI_Send(want[i].c_str(), want[i].size() + 1, MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);

        // Receive size
        int size = 0;
        MPI_Recv(&size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // Receive segments' hash
        file_segments[want[i]].second = 0;
        char hash[HASH_SIZE + 3] = {0};
        for (int j = 0; j < size; j++) {
            MPI_Recv(hash, HASH_SIZE + 2, MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            file_segments[want[i]].first.push_back(hash);
        }

        // Receive swarm size
        MPI_Recv(&size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        // Receive swarm
        swarms[want[i]].second = 0;
        int proc;
        for (int j = 0; j < size; j++) {
            MPI_Recv(&proc, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            swarms[want[i]].first.push_back(proc);
        }
    }

    auto count = 10;
    int nr = 0;

    while (want.size() != 0) {
        if (count == 0) {
            action = UPDATE;

            // Ask for update for every swarm
            for (long unsigned int i = 0; i < want.size(); i++) {
                // Check if a file is done
                if (file_segments[want[i]].second == file_segments[want[i]].first.size())
                    continue;
                
                // Send request
                MPI_Send(&action, 1, MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);

                // Send name
                MPI_Send(want[i].c_str(), want[i].size() + 1, MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);

                // Dump old swarm
                swarms[want[i]].first.clear();
                
                // Receive swarm size
                int size;
                MPI_Recv(&size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                
                // Receive swarm
                int proc;
                for (int j = 0; j < size; j++) {
                    MPI_Recv(&proc, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    swarms[want[i]].first.push_back(proc);
                }
            }

            // Reset update count
            count = 10;
        }

        // Choose a file
        std::string file_name = want[nr];


        while (true)
        {
            // Search who to ask for a segment
            int dest = swarms[file_name].first[swarms[file_name].second];

            // Go to the next user that has the file
            swarms[file_name].second = (swarms[file_name].second + 1) % swarms[file_name].first.size();
            if (swarms[file_name].second == rank) {
                swarms[file_name].second = (swarms[file_name].second + 1) % swarms[file_name].first.size();
            }

            // Send hash request
            MPI_Send(file_segments[file_name].first[file_segments[file_name].second].c_str(), HASH_SIZE + 1, MPI_CHAR, dest, INFO_TAG, MPI_COMM_WORLD);
            
            // Receive response
            char buff;
            MPI_Recv(&buff, 1, MPI_CHAR, dest, DATA_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (buff == ACK) {
                break;
            }
        }
        
        file_segments[file_name].second++;
        if (file_segments[file_name].second == file_segments[file_name].first.size()) {
            want.erase(want.begin() + nr);
            nr--;
        }

        if (want.size() == 0)
            break;

        nr = (nr + 1) % want.size();
        count--;
    }

    action = DONE;
    MPI_Send(&action, 1, MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);

    for (auto it = file_segments.begin(); it != file_segments.end(); it++) {
        // Open output file
        std::string file_name = "client__" + it->first;
        file_name[6] = rank + 48;
        FILE *fout = fopen(file_name.c_str(), "wt");

        for (long unsigned int i = 0; i < it->second.first.size(); i++) {
            fprintf(fout, "%s\n", it->second.first[i].c_str());
        }

        fclose(fout);
    }

    return NULL;
}

void *upload_thread_func(void *arg)
{
    // int rank = *(int*) arg;

    MPI_Status status;
    int source;

    while (true)
    {
        char hash[HASH_SIZE + 2] = {0};
        // Wait for a hash request
        MPI_Recv(hash, HASH_SIZE + 3, MPI_CHAR, MPI_ANY_SOURCE, INFO_TAG, MPI_COMM_WORLD, &status);
        source = status.MPI_SOURCE;
        if (source != 0) {
            // Search for the hash
            bool found = false;

            for (auto it = files.begin(); it != files.end() && !found; it++) {
                // Search for the value in the vector
                for (long unsigned int i = 0; i < it->second.size() && !found; i++) {
                    if(strcmp(it->second[i].c_str(), hash) == 0) {
                        found = true;
                    }
                }
            }
            // Check if it found the hash
            char buff;
            if (found) {
                buff = ACK;
            } else {
                buff = NACK;
            }

            // Send the result
            MPI_Send(&buff, 1, MPI_CHAR, source, DATA_TAG, MPI_COMM_WORLD);
        } else {
            // Not a hash - to stop the program
            return NULL;
        }
    }
}

void tracker(int numtasks, int rank) {
    std::unordered_map<std::string, std::vector<std::string>> file_segments;
    std::unordered_map<std::string, std::vector<int>> swarms;

    // Learn all files and segments from each user 
    for (int i = 1; i < numtasks; i++) {
        // Nr of files
        int nr_files;
        MPI_Recv(&nr_files, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for (int j = 0; j < nr_files; j++) {
            char name[MAX_FILENAME + 1];
            int size;

            // Name of the file
            MPI_Recv(name, MAX_FILENAME + 1, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            swarms[name].push_back(i);

            // Check if the tracker already has the file
            char buff;
            if (!file_segments.count(name)) {
                buff = ACK;
                MPI_Send(&buff, 1, MPI_CHAR, i, 0, MPI_COMM_WORLD);
            } else {
                buff = NACK;
                MPI_Send(&buff, 1, MPI_CHAR, i, 0, MPI_COMM_WORLD);
                continue;
            }


            // Nr of segments
            MPI_Recv(&size, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            for (int k = 0; k < size; k++) {
                // Hashes of segments
                char segment[HASH_SIZE + 2];
                MPI_Recv(segment, HASH_SIZE + 3, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                file_segments[name].push_back(segment);
            }
        }
    }

    // Send ACK's to each user so that they can start
    int ack = ACK;
    MPI_Bcast(&ack, 1, MPI_INT, rank, MPI_COMM_WORLD);

    MPI_Status status;
    char action;
    int source, size, buff;

    int done_download = 1;
    bool repeat = true;
    
    while (repeat)
    {
        char name[MAX_FILENAME + 2];
        
        // Action to do
        MPI_Recv(&action, 1, MPI_CHAR, MPI_ANY_SOURCE, TRACKER_TAG, MPI_COMM_WORLD, &status);
        source = status.MPI_SOURCE;
        switch (action)
        {
        case REQUEST:
            // Download requested info for a file
            // Name of the file
            MPI_Recv(name, MAX_FILENAME + 1, MPI_CHAR, source, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Add that user to the swarm
            swarms[name].push_back(source);

            // Send nr of segments
            size = file_segments[name].size();
            MPI_Send(&size, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD);

            // Send hashes
            for (int i = 0; i < size; i++) {
                MPI_Send(file_segments[name][i].c_str(), file_segments[name][i].size() + 1, MPI_CHAR, source, TRACKER_TAG, MPI_COMM_WORLD);
            }

            // Send size of swarm of the file
            size = swarms[name].size();
            MPI_Send(&size, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD);

            // Send the swarm
            for (int i = 0; i < size; i++) {
                int proc = swarms[name][i];
                MPI_Send(&proc, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD);
            }
            break;
        case UPDATE:
            // Update the swarm of a file
            // Name of the file
            MPI_Recv(name, MAX_FILENAME + 1, MPI_CHAR, source, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Send size of swarm of the file
            size = swarms[name].size();
            MPI_Send(&size, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD);

            // Send the swarm
            for (int i = 0; i < size; i++) {
                buff = swarms[name][i];
                MPI_Send(&buff, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD);
            }
            break;
        case DONE:
            // A user finished downloading
            done_download++;
            if (done_download == numtasks) {
                // Send to all uploads a message to stop
                for (int i = 1; i < numtasks; i++) {
                    MPI_Send(&buff, 1, MPI_INT, i, INFO_TAG, MPI_COMM_WORLD);
                }
                repeat = false;
            }
            break;
        default:
            break;
        }
    }
    
}

// Reads from the file
void read_from_file(int rank) {
    char file_name[] = "in_.txt";
    file_name[2] = rank + 48;
    FILE *fin = fopen(file_name, "rt");
    if (fin == NULL) {
        printf("Error reading file\n");
        return;
    }

    char name[MAX_FILENAME + 1];
    int nr_have;
    fscanf(fin, "%d", &nr_have);

    for (int i = 0; i < nr_have; i++) {
        int size;
        fscanf(fin, "%s", name);
        fscanf(fin, "%d", &size);

        for (int j = 0; j < size; j++) {
            char segment[HASH_SIZE + 1];
            fscanf(fin, "%s", segment);
            files[name].push_back(segment);
        }

    }

    int nr_need;
    fscanf(fin, "%d", &nr_need);

    for (int i = 0; i < nr_need; i++) {
        fscanf(fin, "%s", name);
        want.push_back(name);
    }
    fclose(fin);    
}

void send_files() {
    // Send nr of files
    int nr_files = files.size();
    MPI_Send(&nr_files, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
    
    // Send each file
    for (auto it = files.begin(); it != files.end(); it++) {
        // Send name
        MPI_Send(it->first.c_str(), it->first.size() + 1, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        // Await confirmation for segments
        char buff;
        MPI_Recv(&buff, 1, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (buff == NACK)
            continue;

        // Send nr of segments of the file
        int nr_segmnents = it->second.size();
        MPI_Send(&nr_segmnents, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
        
        // Send the hashes of the file
        for (int i = 0; i < nr_segmnents; i++) {
            MPI_Send(it->second[i].c_str(), it->second[i].size() + 1, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        }
    }
} 

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    read_from_file(rank);

    send_files();
    
    MPI_Bcast(&r, 1, MPI_INT, rank, MPI_COMM_WORLD);

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
