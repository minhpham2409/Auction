/*
 * =====================================================
 * SERVER.C - ONLINE AUCTION SYSTEM SERVER (WITH ROOM MANAGEMENT)
 * =====================================================
 * Compile: gcc server.c -o server -lpthread
 * Run: ./server
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <signal.h>

#define PORT 8888
#define MAX_CLIENTS 100
#define BUFFER_SIZE 4096
#define MAX_USERS 1000
#define MAX_ROOMS 100
#define MAX_AUCTIONS 1000
#define MAX_BIDS 5000

// =====================================================
// DATA STRUCTURES
// =====================================================

typedef struct {
    int user_id;
    char username[50];
    char password[256];
    char email[100];
    char role[20];
    double balance;
    char status[20];
    time_t created_at;
} User;

typedef struct {
    int room_id;
    char room_name[100];
    char description[200];
    int max_participants;
    int current_participants;
    char status[20]; // "waiting", "active", "ended"
    time_t start_time;
    time_t end_time;
    int created_by;
    int total_auctions; // Số lượng auction trong room
} AuctionRoom;

typedef struct {
    int auction_id;
    int seller_id;
    int room_id; // Auction belongs to a room
    char title[200];
    char description[500];
    double start_price;
    double current_price;
    double buy_now_price;
    double min_bid_increment;
    time_t start_time;
    time_t end_time;
    char status[20];
    int winner_id;
    int total_bids;
} Auction;

typedef struct {
    int bid_id;
    int auction_id;
    int user_id;
    double bid_amount;
    time_t bid_time;
} Bid;

typedef struct {
    int socket;
    int user_id;
    char username[50];
    int is_active;
    time_t login_time;
    int current_room_id; // User can only join one room at a time
} ClientSession;

// =====================================================
// GLOBAL VARIABLES
// =====================================================

User g_users[MAX_USERS];
int g_user_count = 0;

AuctionRoom g_rooms[MAX_ROOMS];
int g_room_count = 0;

Auction g_auctions[MAX_AUCTIONS];
int g_auction_count = 0;

Bid g_bids[MAX_BIDS];
int g_bid_count = 0;

ClientSession g_clients[MAX_CLIENTS];
int g_client_count = 0;

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

int server_socket;
int server_running = 1;

// =====================================================
// FILE I/O FUNCTIONS
// =====================================================

void init_data_storage() {
    FILE *fp;

    // Load users
    fp = fopen("data/users.dat", "rb");
    if (fp != NULL) {
        g_user_count = fread(g_users, sizeof(User), MAX_USERS, fp);
        fclose(fp);
        printf("[INFO] Loaded %d users\n", g_user_count);
    } else {
        printf("[INFO] No users file found, starting fresh\n");
        g_user_count = 0;
    }

    // Load rooms
    fp = fopen("data/rooms.dat", "rb");
    if (fp != NULL) {
        g_room_count = fread(g_rooms, sizeof(AuctionRoom), MAX_ROOMS, fp);
        fclose(fp);
        printf("[INFO] Loaded %d rooms\n", g_room_count);
    } else {
        printf("[INFO] No rooms file found, starting fresh\n");
        g_room_count = 0;
    }

    // Load auctions
    fp = fopen("data/auctions.dat", "rb");
    if (fp != NULL) {
        g_auction_count = fread(g_auctions, sizeof(Auction), MAX_AUCTIONS, fp);
        fclose(fp);
        printf("[INFO] Loaded %d auctions\n", g_auction_count);
    } else {
        printf("[INFO] No auctions file found, starting fresh\n");
        g_auction_count = 0;
    }

    // Load bids
    fp = fopen("data/bids.dat", "rb");
    if (fp != NULL) {
        g_bid_count = fread(g_bids, sizeof(Bid), MAX_BIDS, fp);
        fclose(fp);
        printf("[INFO] Loaded %d bids\n", g_bid_count);
    } else {
        printf("[INFO] No bids file found, starting fresh\n");
        g_bid_count = 0;
    }
}

void save_all_data() {
    FILE *fp;

    system("mkdir -p data");

    fp = fopen("data/users.dat", "wb");
    if (fp != NULL) {
        fwrite(g_users, sizeof(User), g_user_count, fp);
        fclose(fp);
    }

    fp = fopen("data/rooms.dat", "wb");
    if (fp != NULL) {
        fwrite(g_rooms, sizeof(AuctionRoom), g_room_count, fp);
        fclose(fp);
    }

    fp = fopen("data/auctions.dat", "wb");
    if (fp != NULL) {
        fwrite(g_auctions, sizeof(Auction), g_auction_count, fp);
        fclose(fp);
    }

    fp = fopen("data/bids.dat", "wb");
    if (fp != NULL) {
        fwrite(g_bids, sizeof(Bid), g_bid_count, fp);
        fclose(fp);
    }

    printf("[INFO] All data saved to disk\n");
}

// =====================================================
// BUSINESS LOGIC FUNCTIONS
// =====================================================

User* find_user_by_username(const char *username) {
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0) {
            return &g_users[i];
        }
    }
    return NULL;
}

User* find_user_by_id(int user_id) {
    for (int i = 0; i < g_user_count; i++) {
        if (g_users[i].user_id == user_id) {
            return &g_users[i];
        }
    }
    return NULL;
}

Auction* find_auction_by_id(int auction_id) {
    for (int i = 0; i < g_auction_count; i++) {
        if (g_auctions[i].auction_id == auction_id) {
            return &g_auctions[i];
        }
    }
    return NULL;
}

AuctionRoom* find_room_by_id(int room_id) {
    for (int i = 0; i < g_room_count; i++) {
        if (g_rooms[i].room_id == room_id) {
            return &g_rooms[i];
        }
    }
    return NULL;
}

ClientSession* find_client_by_user_id(int user_id) {
    // NOTE: Caller must hold client_mutex!
    // Removed internal lock to prevent deadlock
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].is_active && g_clients[i].user_id == user_id) {
            return &g_clients[i];
        }
    }
    return NULL;
}

// =====================================================
// ROOM MANAGEMENT FUNCTIONS
// =====================================================

int create_room(int creator_id, const char *name, const char *desc, int max_participants, int duration_minutes) {
    pthread_mutex_lock(&data_mutex);

    if (g_room_count >= MAX_ROOMS) {
        pthread_mutex_unlock(&data_mutex);
        return -1; // Room database full
    }

    // Check for duplicate room name
    for (int i = 0; i < g_room_count; i++) {
        if (strcmp(g_rooms[i].room_name, name) == 0 &&
            strcmp(g_rooms[i].status, "ended") != 0) {
            pthread_mutex_unlock(&data_mutex);
            return -2; // Room name already exists
        }
    }

    AuctionRoom *room = &g_rooms[g_room_count];
    room->room_id = g_room_count + 1;
    strncpy(room->room_name, name, 99);
    room->room_name[99] = '\0';
    strncpy(room->description, desc, 199);
    room->description[199] = '\0';
    room->max_participants = max_participants;
    room->current_participants = 0;
    strcpy(room->status, "waiting");
    room->start_time = time(NULL);
    room->end_time = time(NULL) + (duration_minutes * 60);
    room->created_by = creator_id;
    room->total_auctions = 0;

    g_room_count++;

    save_all_data();
    pthread_mutex_unlock(&data_mutex);

    return room->room_id;
}

int join_room(int user_id, int room_id) {
    pthread_mutex_lock(&data_mutex);

    AuctionRoom *room = find_room_by_id(room_id);

    if (room == NULL) {
        pthread_mutex_unlock(&data_mutex);
        printf("[ERROR] join_room: Room %d not found\n", room_id);
        return -1; // Room not found
    }

    if (strcmp(room->status, "ended") == 0) {
        pthread_mutex_unlock(&data_mutex);
        printf("[ERROR] join_room: Room %d has ended\n", room_id);
        return -2; // Room has ended
    }

    if (room->current_participants >= room->max_participants) {
        pthread_mutex_unlock(&data_mutex);
        printf("[ERROR] join_room: Room %d is full (%d/%d)\n",
               room_id, room->current_participants, room->max_participants);
        return -3; // Room is full
    }

    // Check if user already in a room - need client_mutex
    pthread_mutex_lock(&client_mutex);
    ClientSession *client = find_client_by_user_id(user_id);

    if (client != NULL && client->current_room_id > 0 && client->current_room_id != room_id) {
        pthread_mutex_unlock(&client_mutex);
        pthread_mutex_unlock(&data_mutex);
        printf("[ERROR] join_room: User %d already in room %d\n", user_id, client->current_room_id);
        return -4; // Already in another room
    }

    // Join room
    if (client != NULL) {
        client->current_room_id = room_id;
        printf("[DEBUG] join_room: Set user %d current_room_id to %d\n", user_id, room_id);
    } else {
        printf("[WARNING] join_room: Client session not found for user %d\n", user_id);
    }
    pthread_mutex_unlock(&client_mutex);

    room->current_participants++;
    printf("[DEBUG] join_room: Room %d participants: %d/%d\n",
           room_id, room->current_participants, room->max_participants);

    // Activate room if it was waiting
    if (strcmp(room->status, "waiting") == 0) {
        strcpy(room->status, "active");
        printf("[DEBUG] join_room: Room %d activated\n", room_id);
    }

    save_all_data();
    pthread_mutex_unlock(&data_mutex);

    printf("[INFO] User %d successfully joined room %d\n", user_id, room_id);
    return 0; // Success
}

// Internal function - caller must hold BOTH data_mutex AND client_mutex!
int _leave_room_unsafe(int user_id) {
    ClientSession *client = find_client_by_user_id(user_id);

    if (client == NULL || client->current_room_id == 0) {
        printf("[DEBUG] _leave_room_unsafe: User %d not in any room\n", user_id);
        return -1; // Not in any room
    }

    int old_room_id = client->current_room_id;
    AuctionRoom *room = find_room_by_id(old_room_id);
    if (room != NULL) {
        room->current_participants--;
        printf("[DEBUG] _leave_room_unsafe: Room %d participants decreased to %d\n",
               old_room_id, room->current_participants);
    }

    client->current_room_id = 0;

    printf("[INFO] User %d left room %d (unsafe)\n", user_id, old_room_id);
    return 0; // Success
}

int leave_room(int user_id) {
    pthread_mutex_lock(&data_mutex);
    pthread_mutex_lock(&client_mutex);

    int result = _leave_room_unsafe(user_id);

    pthread_mutex_unlock(&client_mutex);

    if (result == 0) {
        save_all_data();
    }

    pthread_mutex_unlock(&data_mutex);

    return result;
}

// =====================================================
// USER MANAGEMENT FUNCTIONS
// =====================================================

int register_user(const char *username, const char *password, const char *email) {
    pthread_mutex_lock(&data_mutex);

    if (find_user_by_username(username) != NULL) {
        pthread_mutex_unlock(&data_mutex);
        return -1; // Username already exists
    }

    if (g_user_count >= MAX_USERS) {
        pthread_mutex_unlock(&data_mutex);
        return -2; // Database full
    }

    User *user = &g_users[g_user_count];
    user->user_id = g_user_count + 1;
    strncpy(user->username, username, 49);
    user->username[49] = '\0';
    strncpy(user->password, password, 255);
    user->password[255] = '\0';
    strncpy(user->email, email, 99);
    user->email[99] = '\0';
    strcpy(user->role, "user");
    user->balance = 1000000; // Starting balance
    strcpy(user->status, "active");
    user->created_at = time(NULL);

    g_user_count++;

    save_all_data();
    pthread_mutex_unlock(&data_mutex);

    return user->user_id;
}

int authenticate_user(const char *username, const char *password) {
    pthread_mutex_lock(&data_mutex);

    User *user = find_user_by_username(username);

    if (user == NULL) {
        pthread_mutex_unlock(&data_mutex);
        return -1; // User not found
    }

    if (strcmp(user->password, password) != 0) {
        pthread_mutex_unlock(&data_mutex);
        return -2; // Wrong password
    }

    if (strcmp(user->status, "active") != 0) {
        pthread_mutex_unlock(&data_mutex);
        return -3; // Account not active
    }

    int user_id = user->user_id;
    pthread_mutex_unlock(&data_mutex);

    return user_id;
}

// =====================================================
// AUCTION MANAGEMENT FUNCTIONS
// =====================================================

int create_auction(int seller_id, int room_id, const char *title, const char *desc,
                   double start_price, double buy_now_price,
                   double min_increment, int duration_minutes) {
    pthread_mutex_lock(&data_mutex);

    if (g_auction_count >= MAX_AUCTIONS) {
        pthread_mutex_unlock(&data_mutex);
        return -1; // Auction database full
    }

    // Validate room exists
    AuctionRoom *room = find_room_by_id(room_id);
    if (room == NULL) {
        pthread_mutex_unlock(&data_mutex);
        return -2; // Room not found
    }

    // Validate seller is in the room - need client_mutex
    pthread_mutex_lock(&client_mutex);
    ClientSession *client = find_client_by_user_id(seller_id);
    int seller_current_room = (client != NULL) ? client->current_room_id : 0;
    pthread_mutex_unlock(&client_mutex);

    if (seller_current_room != room_id) {
        pthread_mutex_unlock(&data_mutex);
        return -3; // Seller not in room
    }

    // CRITICAL: Only room creator can create auction
    if (room->created_by != seller_id) {
        pthread_mutex_unlock(&data_mutex);
        return -4; // Not room creator
    }

    Auction *auction = &g_auctions[g_auction_count];
    auction->auction_id = g_auction_count + 1;
    auction->seller_id = seller_id;
    auction->room_id = room_id;
    strncpy(auction->title, title, 199);
    auction->title[199] = '\0';
    strncpy(auction->description, desc, 499);
    auction->description[499] = '\0';
    auction->start_price = start_price;
    auction->current_price = start_price;
    auction->buy_now_price = buy_now_price;
    auction->min_bid_increment = min_increment;
    auction->start_time = time(NULL);
    auction->end_time = time(NULL) + (duration_minutes * 60);
    strcpy(auction->status, "active");
    auction->winner_id = 0;
    auction->total_bids = 0;

    g_auction_count++;
    room->total_auctions++;

    save_all_data();
    pthread_mutex_unlock(&data_mutex);

    return auction->auction_id;
}

int place_bid(int auction_id, int user_id, double bid_amount) {
    pthread_mutex_lock(&data_mutex);

    Auction *auction = find_auction_by_id(auction_id);

    if (auction == NULL) {
        pthread_mutex_unlock(&data_mutex);
        return -1; // Auction not found
    }

    if (strcmp(auction->status, "active") != 0) {
        pthread_mutex_unlock(&data_mutex);
        return -2; // Auction not active
    }

    // Validate user is in the same room as auction - need client_mutex
    pthread_mutex_lock(&client_mutex);
    ClientSession *client = find_client_by_user_id(user_id);
    int user_room_id = (client != NULL) ? client->current_room_id : 0;
    pthread_mutex_unlock(&client_mutex);

    if (user_room_id != auction->room_id) {
        pthread_mutex_unlock(&data_mutex);
        return -8; // Not in the same room
    }

    time_t now = time(NULL);
    if (now > auction->end_time) {
        pthread_mutex_unlock(&data_mutex);
        return -3; // Auction ended
    }

    if (bid_amount < auction->current_price + auction->min_bid_increment) {
        pthread_mutex_unlock(&data_mutex);
        return -4; // Bid too low
    }

    if (auction->seller_id == user_id) {
        pthread_mutex_unlock(&data_mutex);
        return -5; // Can't bid on own auction
    }

    User *user = find_user_by_id(user_id);
    if (user == NULL || user->balance < bid_amount) {
        pthread_mutex_unlock(&data_mutex);
        return -6; // Insufficient balance
    }

    // Create bid
    if (g_bid_count >= MAX_BIDS) {
        pthread_mutex_unlock(&data_mutex);
        return -7; // Bids database full
    }

    Bid *bid = &g_bids[g_bid_count];
    bid->bid_id = g_bid_count + 1;
    bid->auction_id = auction_id;
    bid->user_id = user_id;
    bid->bid_amount = bid_amount;
    bid->bid_time = time(NULL);

    g_bid_count++;

    // Update auction
    auction->current_price = bid_amount;
    auction->total_bids++;
    auction->winner_id = user_id;

    // Anti-snipe: If bid placed in last 30 seconds, extend by 30 seconds
    int time_remaining = auction->end_time - now;
    if (time_remaining < 30 && time_remaining > 0) {
        auction->end_time = now + 30;
        printf("[INFO] Anti-snipe: Auction %d extended by 30 seconds\n", auction_id);
    }

    save_all_data();

    int bid_id = bid->bid_id;

    pthread_mutex_unlock(&data_mutex);

    return bid_id;
}

int buy_now(int auction_id, int user_id) {
    pthread_mutex_lock(&data_mutex);

    Auction *auction = find_auction_by_id(auction_id);

    if (auction == NULL || strcmp(auction->status, "active") != 0) {
        pthread_mutex_unlock(&data_mutex);
        return -1; // Auction not available
    }

    // Validate user is in the same room
    ClientSession *client = find_client_by_user_id(user_id);
    if (client == NULL || client->current_room_id != auction->room_id) {
        pthread_mutex_unlock(&data_mutex);
        return -4; // Not in the same room
    }

    if (auction->buy_now_price <= 0) {
        pthread_mutex_unlock(&data_mutex);
        return -2; // Buy now not available
    }

    User *user = find_user_by_id(user_id);
    if (user == NULL || user->balance < auction->buy_now_price) {
        pthread_mutex_unlock(&data_mutex);
        return -3; // Insufficient balance
    }

    // Process buy now
    user->balance -= auction->buy_now_price;

    User *seller = find_user_by_id(auction->seller_id);
    if (seller != NULL) {
        seller->balance += auction->buy_now_price;
    }

    auction->winner_id = user_id;
    auction->current_price = auction->buy_now_price;
    strcpy(auction->status, "ended");

    save_all_data();
    pthread_mutex_unlock(&data_mutex);

    return 0;
}

// =====================================================
// CLIENT SESSION MANAGEMENT
// =====================================================

int is_user_logged_in(int user_id) {
    pthread_mutex_lock(&client_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].is_active && g_clients[i].user_id == user_id) {
            pthread_mutex_unlock(&client_mutex);
            return 1;
        }
    }

    pthread_mutex_unlock(&client_mutex);
    return 0;
}

void force_logout_user(int user_id) {
    pthread_mutex_lock(&client_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].is_active && g_clients[i].user_id == user_id) {
            char msg[] = "FORCE_LOGOUT|Another login detected\n";
            send(g_clients[i].socket, msg, strlen(msg), 0);
            g_clients[i].is_active = 0;
            g_client_count--;
            close(g_clients[i].socket);
            printf("[INFO] Force logout user %s from socket %d\n",
                   g_clients[i].username, g_clients[i].socket);
        }
    }

    pthread_mutex_unlock(&client_mutex);
}

void add_client(int socket, int user_id, const char *username) {
    pthread_mutex_lock(&client_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].is_active) {
            g_clients[i].socket = socket;
            g_clients[i].user_id = user_id;
            strncpy(g_clients[i].username, username, 49);
            g_clients[i].username[49] = '\0';
            g_clients[i].is_active = 1;
            g_clients[i].login_time = time(NULL);
            g_clients[i].current_room_id = 0; // Not in any room initially
            g_client_count++;
            break;
        }
    }

    pthread_mutex_unlock(&client_mutex);
}

void remove_client(int socket) {
    pthread_mutex_lock(&client_mutex);

    int user_id_to_remove = 0;
    int room_id_to_leave = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].is_active && g_clients[i].socket == socket) {
            user_id_to_remove = g_clients[i].user_id;
            room_id_to_leave = g_clients[i].current_room_id;

            g_clients[i].is_active = 0;
            g_client_count--;
            printf("[INFO] Client disconnected: socket=%d, user_id=%d\n", socket, user_id_to_remove);
            break;
        }
    }

    pthread_mutex_unlock(&client_mutex);

    // Auto leave room when disconnect
    if (user_id_to_remove > 0 && room_id_to_leave > 0) {
        pthread_mutex_lock(&data_mutex);
        pthread_mutex_lock(&client_mutex);

        _leave_room_unsafe(user_id_to_remove);
        save_all_data();

        pthread_mutex_unlock(&client_mutex);
        pthread_mutex_unlock(&data_mutex);

        printf("[INFO] User %d auto-left room %d on disconnect\n", user_id_to_remove, room_id_to_leave);
    }
}

void broadcast_message_to_room(const char *message, int room_id, int exclude_socket) {
    pthread_mutex_lock(&client_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].is_active &&
            g_clients[i].current_room_id == room_id &&
            g_clients[i].socket != exclude_socket) {
            send(g_clients[i].socket, message, strlen(message), 0);
        }
    }

    pthread_mutex_unlock(&client_mutex);
}

// =====================================================
// PROTOCOL HANDLERS
// =====================================================

void handle_register(int client_socket, char *data) {
    char username[50], password[256], email[100];
    sscanf(data, "%s %s %s", username, password, email);

    int user_id = register_user(username, password, email);

    char response[BUFFER_SIZE];
    if (user_id > 0) {
        sprintf(response, "REGISTER_SUCCESS|%d|%s\n", user_id, username);
    } else if (user_id == -1) {
        sprintf(response, "REGISTER_FAIL|Username already exists\n");
    } else {
        sprintf(response, "REGISTER_FAIL|Database full\n");
    }

    send(client_socket, response, strlen(response), 0);
}

void handle_login(int client_socket, char *data) {
    char username[50], password[256];
    sscanf(data, "%s %s", username, password);

    int user_id = authenticate_user(username, password);

    char response[BUFFER_SIZE];
    if (user_id > 0) {
        // Check if user is already logged in
        if (is_user_logged_in(user_id)) {
            force_logout_user(user_id);
            sleep(1); // Wait for force logout to complete
        }

        User *user = find_user_by_id(user_id);
        sprintf(response, "LOGIN_SUCCESS|%d|%s|%.2f\n",
                user_id, username, user->balance);
        add_client(client_socket, user_id, username);
        printf("[INFO] User %s logged in (socket %d)\n", username, client_socket);
    } else if (user_id == -1) {
        sprintf(response, "LOGIN_FAIL|User not found\n");
    } else if (user_id == -2) {
        sprintf(response, "LOGIN_FAIL|Wrong password\n");
    } else {
        sprintf(response, "LOGIN_FAIL|Account not active\n");
    }

    send(client_socket, response, strlen(response), 0);
}

void handle_create_room(int client_socket, char *data) {
    int creator_id, max_participants, duration;
    char name[100], desc[200];

    sscanf(data, "%d|%[^|]|%[^|]|%d|%d",
           &creator_id, name, desc, &max_participants, &duration);

    int room_id = create_room(creator_id, name, desc, max_participants, duration);

    char response[BUFFER_SIZE];
    if (room_id > 0) {
        // Auto-join creator to the room
        int join_result = join_room(creator_id, room_id);

        if (join_result == 0) {
            sprintf(response, "CREATE_ROOM_SUCCESS|%d|%s\n", room_id, name);
            printf("[INFO] Room created: ID=%d, Name=%s, by User=%d (auto-joined)\n",
                   room_id, name, creator_id);
        } else {
            sprintf(response, "CREATE_ROOM_SUCCESS|%d|%s\n", room_id, name);
            printf("[INFO] Room created: ID=%d, Name=%s, by User=%d (join failed: %d)\n",
                   room_id, name, creator_id, join_result);
        }

        // Broadcast NEW_ROOM notification to all logged-in users
        char notification[512];
        User *creator = find_user_by_id(creator_id);
        sprintf(notification, "NEW_ROOM|%d|%s|%s|%d\n",
                room_id, name, creator ? creator->username : "Unknown", max_participants);

        // Broadcast to all active clients EXCEPT creator (already knows)
        pthread_mutex_lock(&client_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].is_active && g_clients[i].socket != client_socket) {
                send(g_clients[i].socket, notification, strlen(notification), 0);
            }
        }
        pthread_mutex_unlock(&client_mutex);

    } else if (room_id == -2) {
        sprintf(response, "CREATE_ROOM_FAIL|Room name already exists\n");
    } else {
        sprintf(response, "CREATE_ROOM_FAIL|Database full\n");
    }

    send(client_socket, response, strlen(response), 0);
}

void handle_list_rooms(int client_socket) {
    pthread_mutex_lock(&data_mutex);

    char response[BUFFER_SIZE * 4] = "ROOM_LIST|";
    time_t now = time(NULL);

    for (int i = 0; i < g_room_count; i++) {
        // Only show active and waiting rooms
        if (strcmp(g_rooms[i].status, "ended") != 0 && g_rooms[i].end_time > now) {
            char room_info[512];
            int time_left = g_rooms[i].end_time - now;
            sprintf(room_info, "%d;%s;%s;%d;%d;%s;%d;%d|",
                    g_rooms[i].room_id,
                    g_rooms[i].room_name,
                    g_rooms[i].description,
                    g_rooms[i].current_participants,
                    g_rooms[i].max_participants,
                    g_rooms[i].status,
                    time_left,
                    g_rooms[i].total_auctions);

            strcat(response, room_info);
        }
    }

    pthread_mutex_unlock(&data_mutex);

    strcat(response, "\n");
    send(client_socket, response, strlen(response), 0);
}

void handle_join_room(int client_socket, char *data) {
    int user_id, room_id;
    sscanf(data, "%d|%d", &user_id, &room_id);

    printf("[DEBUG] handle_join_room: user_id=%d, room_id=%d\n", user_id, room_id);

    int result = join_room(user_id, room_id);

    printf("[DEBUG] handle_join_room: join_room returned %d\n", result);

    char response[BUFFER_SIZE];
    if (result == 0) {
        AuctionRoom *room = find_room_by_id(room_id);
        if (room != NULL) {
            sprintf(response, "JOIN_ROOM_SUCCESS|%d|%s\n", room_id, room->room_name);
            printf("[INFO] User %d joined room %d (%s)\n", user_id, room_id, room->room_name);

            // Broadcast to room
            User *user = find_user_by_id(user_id);
            if (user != NULL) {
                char notification[256];
                sprintf(notification, "USER_JOINED|%s|%d\n", user->username, room_id);
                broadcast_message_to_room(notification, room_id, client_socket);
            }
        } else {
            sprintf(response, "JOIN_ROOM_FAIL|Room not found after join\n");
        }
    } else {
        const char *error_msg;
        switch(result) {
            case -1: error_msg = "Room not found"; break;
            case -2: error_msg = "Room has ended"; break;
            case -3: error_msg = "Room is full"; break;
            case -4: error_msg = "Already in another room. Please leave first"; break;
            default: error_msg = "Unknown error"; break;
        }
        sprintf(response, "JOIN_ROOM_FAIL|%s\n", error_msg);
        printf("[ERROR] User %d failed to join room %d: %s\n", user_id, room_id, error_msg);
    }

    printf("[DEBUG] handle_join_room: Sending response: %s", response);
    int sent = send(client_socket, response, strlen(response), 0);
    printf("[DEBUG] handle_join_room: Sent %d bytes\n", sent);
}

void handle_leave_room(int client_socket, char *data) {
    int user_id;
    sscanf(data, "%d", &user_id);

    ClientSession *client = find_client_by_user_id(user_id);
    int old_room_id = (client != NULL) ? client->current_room_id : 0;

    int result = leave_room(user_id);

    char response[BUFFER_SIZE];
    if (result == 0) {
        sprintf(response, "LEAVE_ROOM_SUCCESS|\n");
        printf("[INFO] User %d left room %d\n", user_id, old_room_id);

        // Broadcast to room
        if (old_room_id > 0) {
            User *user = find_user_by_id(user_id);
            char notification[256];
            sprintf(notification, "USER_LEFT|%s|%d\n", user->username, old_room_id);
            broadcast_message_to_room(notification, old_room_id, client_socket);
        }
    } else {
        sprintf(response, "LEAVE_ROOM_FAIL|Not in any room\n");
    }

    send(client_socket, response, strlen(response), 0);
}

void handle_room_detail(int client_socket, char *data) {
    int room_id;
    sscanf(data, "%d", &room_id);

    pthread_mutex_lock(&data_mutex);

    AuctionRoom *room = find_room_by_id(room_id);

    char response[BUFFER_SIZE];
    if (room != NULL) {
        User *creator = find_user_by_id(room->created_by);
        char creator_name[50] = "Unknown";
        if (creator != NULL) {
            strcpy(creator_name, creator->username);
        }

        int time_left = room->end_time - time(NULL);
        if (time_left < 0) time_left = 0;

        sprintf(response, "ROOM_DETAIL|%d|%s|%s|%s|%d|%d|%s|%d|%d\n",
                room->room_id,
                room->room_name,
                room->description,
                creator_name,
                room->current_participants,
                room->max_participants,
                room->status,
                time_left,
                room->total_auctions);
    } else {
        sprintf(response, "ROOM_DETAIL_FAIL|Room not found\n");
    }

    pthread_mutex_unlock(&data_mutex);

    send(client_socket, response, strlen(response), 0);
}

void handle_my_room(int client_socket, char *data) {
    int user_id;
    sscanf(data, "%d", &user_id);

    ClientSession *client = find_client_by_user_id(user_id);

    char response[BUFFER_SIZE];
    if (client != NULL && client->current_room_id > 0) {
        pthread_mutex_lock(&data_mutex);
        AuctionRoom *room = find_room_by_id(client->current_room_id);

        if (room != NULL) {
            sprintf(response, "MY_ROOM|%d|%s|%d|%d\n",
                    room->room_id,
                    room->room_name,
                    room->current_participants,
                    room->total_auctions);
        } else {
            sprintf(response, "MY_ROOM|0|Not in any room|0|0\n");
        }

        pthread_mutex_unlock(&data_mutex);
    } else {
        sprintf(response, "MY_ROOM|0|Not in any room|0|0\n");
    }

    send(client_socket, response, strlen(response), 0);
}

void handle_list_auctions(int client_socket, char *data) {
    int user_id;
    sscanf(data, "%d", &user_id);

    // Get user's current room
    ClientSession *client = find_client_by_user_id(user_id);
    if (client == NULL || client->current_room_id == 0) {
        char response[] = "AUCTION_LIST_FAIL|Not in any room\n";
        send(client_socket, response, strlen(response), 0);
        return;
    }

    int room_id = client->current_room_id;

    pthread_mutex_lock(&data_mutex);

    char response[BUFFER_SIZE * 4] = "AUCTION_LIST|";
    time_t now = time(NULL);
    int count = 0;

    for (int i = 0; i < g_auction_count; i++) {
        if (g_auctions[i].room_id == room_id &&
            strcmp(g_auctions[i].status, "active") == 0 &&
            g_auctions[i].end_time > now) {

            char auction_info[512];
            int time_left = g_auctions[i].end_time - now;
            sprintf(auction_info, "%d;%s;%.2f;%.2f;%d;%d|",
                    g_auctions[i].auction_id,
                    g_auctions[i].title,
                    g_auctions[i].current_price,
                    g_auctions[i].buy_now_price,
                    time_left,
                    g_auctions[i].total_bids);

            strcat(response, auction_info);
            count++;
        }
    }

    pthread_mutex_unlock(&data_mutex);

    strcat(response, "\n");
    send(client_socket, response, strlen(response), 0);
}

void handle_auction_detail(int client_socket, char *data) {
    int auction_id, user_id;
    sscanf(data, "%d|%d", &auction_id, &user_id);

    // Validate user is in the same room
    ClientSession *client = find_client_by_user_id(user_id);

    pthread_mutex_lock(&data_mutex);

    Auction *auction = find_auction_by_id(auction_id);

    char response[BUFFER_SIZE];
    if (auction != NULL) {
        // Check room access
        if (client == NULL || client->current_room_id != auction->room_id) {
            sprintf(response, "AUCTION_DETAIL_FAIL|Not in the same room\n");
        } else {
            User *seller = find_user_by_id(auction->seller_id);
            char seller_name[50] = "Unknown";
            if (seller != NULL) {
                strcpy(seller_name, seller->username);
            }

            int time_left = auction->end_time - time(NULL);
            if (time_left < 0) time_left = 0;

            sprintf(response, "AUCTION_DETAIL|%d|%s|%s|%s|%.2f|%.2f|%.2f|%.2f|%d|%s|%d\n",
                    auction->auction_id,
                    auction->title,
                    auction->description,
                    seller_name,
                    auction->start_price,
                    auction->current_price,
                    auction->buy_now_price,
                    auction->min_bid_increment,
                    time_left,
                    auction->status,
                    auction->total_bids);
        }
    } else {
        sprintf(response, "AUCTION_DETAIL_FAIL|Auction not found\n");
    }

    pthread_mutex_unlock(&data_mutex);

    send(client_socket, response, strlen(response), 0);
}

void handle_create_auction(int client_socket, char *data) {
    int user_id, room_id;
    char title[200], desc[500];
    double start_price, buy_now_price, min_increment;
    int duration;

    sscanf(data, "%d|%d|%[^|]|%[^|]|%lf|%lf|%lf|%d",
           &user_id, &room_id, title, desc, &start_price, &buy_now_price,
           &min_increment, &duration);

    int auction_id = create_auction(user_id, room_id, title, desc, start_price,
                                     buy_now_price, min_increment, duration);

    char response[BUFFER_SIZE];
    if (auction_id > 0) {
        sprintf(response, "CREATE_AUCTION_SUCCESS|%d|%s\n", auction_id, title);

        // Broadcast to room
        Auction *new_auction = find_auction_by_id(auction_id);
        if (new_auction != NULL) {
            char notification[1024];
            int time_left = new_auction->end_time - time(NULL);
            sprintf(notification, "NEW_AUCTION|%d|%s|%.2f|%.2f|%.2f|%d\n",
                    auction_id, title, start_price, buy_now_price, min_increment, time_left);
            broadcast_message_to_room(notification, room_id, client_socket);
        }
    } else {
        const char *error_msg;
        switch(auction_id) {
            case -1: error_msg = "Database full"; break;
            case -2: error_msg = "Room not found"; break;
            case -3: error_msg = "You must be in the room to create auction"; break;
            case -4: error_msg = "Only room creator can create auction"; break;
            default: error_msg = "Unknown error"; break;
        }
        sprintf(response, "CREATE_AUCTION_FAIL|%s\n", error_msg);
    }

    send(client_socket, response, strlen(response), 0);
}

void handle_place_bid(int client_socket, char *data) {
    int auction_id, user_id;
    double bid_amount;

    sscanf(data, "%d|%d|%lf", &auction_id, &user_id, &bid_amount);

    int result = place_bid(auction_id, user_id, bid_amount);

    char response[BUFFER_SIZE];
    if (result > 0) {
        // Get auction details for response
        pthread_mutex_lock(&data_mutex);
        Auction *auction = find_auction_by_id(auction_id);
        int time_left = auction ? (auction->end_time - time(NULL)) : 0;
        int total_bids = auction ? auction->total_bids : 0;
        pthread_mutex_unlock(&data_mutex);

        sprintf(response, "BID_SUCCESS|%d|%.2f|%d|%d\n",
                auction_id, bid_amount, total_bids, time_left);

        // Broadcast to room with extended info
        if (auction != NULL) {
            User *bidder = find_user_by_id(user_id);
            char notification[512];

            if (time_left < 30 && time_left > 0) {
                // Warning + bid notification
                sprintf(notification, "NEW_BID_WARNING|%d|%s|%.2f|%d|%d\n",
                        auction_id, bidder ? bidder->username : "Unknown",
                        bid_amount, total_bids, time_left);
            } else {
                sprintf(notification, "NEW_BID|%d|%s|%.2f|%d\n",
                        auction_id, bidder ? bidder->username : "Unknown",
                        bid_amount, total_bids);
            }

            broadcast_message_to_room(notification, auction->room_id, client_socket);
        }
    } else {
        const char *error_msg;
        switch(result) {
            case -1: error_msg = "Auction not found"; break;
            case -2: error_msg = "Auction not active"; break;
            case -3: error_msg = "Auction ended"; break;
            case -4: error_msg = "Bid too low"; break;
            case -5: error_msg = "Cannot bid on own auction"; break;
            case -6: error_msg = "Insufficient balance"; break;
            case -8: error_msg = "Not in the same room"; break;
            default: error_msg = "Unknown error"; break;
        }
        sprintf(response, "BID_FAIL|%s\n", error_msg);
    }

    send(client_socket, response, strlen(response), 0);
}

void handle_buy_now(int client_socket, char *data) {
    int auction_id, user_id;
    sscanf(data, "%d|%d", &auction_id, &user_id);

    int result = buy_now(auction_id, user_id);

    char response[BUFFER_SIZE];
    if (result == 0) {
        sprintf(response, "BUY_NOW_SUCCESS|%d\n", auction_id);

        // Broadcast to room
        Auction *auction = find_auction_by_id(auction_id);
        if (auction != NULL) {
            char notification[512];
            sprintf(notification, "AUCTION_ENDED|%d|buy_now\n", auction_id);
            broadcast_message_to_room(notification, auction->room_id, client_socket);
        }
    } else {
        const char *error_msg;
        switch(result) {
            case -1: error_msg = "Auction not available"; break;
            case -2: error_msg = "Buy now not available"; break;
            case -3: error_msg = "Insufficient balance"; break;
            case -4: error_msg = "Not in the same room"; break;
            default: error_msg = "Unknown error"; break;
        }
        sprintf(response, "BUY_NOW_FAIL|%s\n", error_msg);
    }

    send(client_socket, response, strlen(response), 0);
}

void handle_bid_history(int client_socket, char *data) {
    int auction_id, user_id;
    sscanf(data, "%d|%d", &auction_id, &user_id);

    // Validate room access
    ClientSession *client = find_client_by_user_id(user_id);

    pthread_mutex_lock(&data_mutex);

    Auction *auction = find_auction_by_id(auction_id);

    if (auction == NULL || client == NULL || client->current_room_id != auction->room_id) {
        char response[] = "BID_HISTORY_FAIL|Not in the same room\n";
        send(client_socket, response, strlen(response), 0);
        pthread_mutex_unlock(&data_mutex);
        return;
    }

    char response[BUFFER_SIZE * 2] = "BID_HISTORY|";

    for (int i = g_bid_count - 1; i >= 0 && i >= g_bid_count - 20; i--) {
        if (g_bids[i].auction_id == auction_id) {
            User *bidder = find_user_by_id(g_bids[i].user_id);
            char bid_info[256];

            char time_str[64];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
                     localtime(&g_bids[i].bid_time));

            sprintf(bid_info, "%s;%.2f;%s|",
                    bidder ? bidder->username : "Unknown",
                    g_bids[i].bid_amount,
                    time_str);

            strcat(response, bid_info);
        }
    }

    pthread_mutex_unlock(&data_mutex);

    strcat(response, "\n");
    send(client_socket, response, strlen(response), 0);
}

void handle_my_auctions(int client_socket, char *data) {
    int user_id;
    sscanf(data, "%d", &user_id);

    pthread_mutex_lock(&data_mutex);

    char response[BUFFER_SIZE * 4] = "MY_AUCTIONS|";
    time_t now = time(NULL);

    for (int i = 0; i < g_auction_count; i++) {
        if (g_auctions[i].seller_id == user_id) {
            char auction_info[512];
            int time_left = g_auctions[i].end_time - now;
            if (time_left < 0) time_left = 0;

            sprintf(auction_info, "%d;%s;%.2f;%.2f;%d;%s;%d|",
                    g_auctions[i].auction_id,
                    g_auctions[i].title,
                    g_auctions[i].current_price,
                    g_auctions[i].buy_now_price,
                    time_left,
                    g_auctions[i].status,
                    g_auctions[i].total_bids);

            strcat(response, auction_info);
        }
    }

    pthread_mutex_unlock(&data_mutex);

    strcat(response, "\n");
    send(client_socket, response, strlen(response), 0);
}

void handle_auction_history(int client_socket, char *data) {
    int user_id;
    sscanf(data, "%d", &user_id);

    pthread_mutex_lock(&data_mutex);

    char response[BUFFER_SIZE * 4] = "AUCTION_HISTORY|";

    for (int i = 0; i < g_auction_count; i++) {
        if (strcmp(g_auctions[i].status, "ended") == 0) {
            char auction_info[512];
            char winner_name[50] = "No winner";
            char win_method[20] = "no_bids";

            if (g_auctions[i].winner_id > 0) {
                User *winner = find_user_by_id(g_auctions[i].winner_id);
                if (winner != NULL) {
                    strncpy(winner_name, winner->username, 49);
                    winner_name[49] = '\0';
                }

                // Determine win method
                if (g_auctions[i].current_price == g_auctions[i].buy_now_price
                    && g_auctions[i].buy_now_price > 0) {
                    strcpy(win_method, "buy_now");
                } else {
                    strcpy(win_method, "bid");
                }
            }

            sprintf(auction_info, "%d;%s;%.2f;%s;%s|",
                    g_auctions[i].auction_id,
                    g_auctions[i].title,
                    g_auctions[i].current_price,
                    winner_name,
                    win_method);

            strcat(response, auction_info);
        }
    }

    pthread_mutex_unlock(&data_mutex);

    strcat(response, "\n");
    send(client_socket, response, strlen(response), 0);
}

// =====================================================
// CLIENT HANDLER THREAD
// =====================================================

void* handle_client(void *arg) {
    int client_socket = *(int*)arg;
    free(arg);

    char buffer[BUFFER_SIZE];

    printf("[INFO] New client connected: socket %d\n", client_socket);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received <= 0) {
            break;
        }

        buffer[bytes_received] = '\0';

        // Remove newline
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';

        printf("[DEBUG] Received: %s\n", buffer);

        // Parse command
        char command[50];
        char *data = strchr(buffer, '|');

        if (data != NULL) {
            *data = '\0';
            data++;
            strcpy(command, buffer);
        } else {
            strcpy(command, buffer);
            data = "";
        }

        // Handle commands
        if (strcmp(command, "REGISTER") == 0) {
            handle_register(client_socket, data);
        } else if (strcmp(command, "LOGIN") == 0) {
            handle_login(client_socket, data);
        } else if (strcmp(command, "CREATE_ROOM") == 0) {
            handle_create_room(client_socket, data);
        } else if (strcmp(command, "LIST_ROOMS") == 0) {
            handle_list_rooms(client_socket);
        } else if (strcmp(command, "JOIN_ROOM") == 0) {
            handle_join_room(client_socket, data);
        } else if (strcmp(command, "LEAVE_ROOM") == 0) {
            handle_leave_room(client_socket, data);
        } else if (strcmp(command, "ROOM_DETAIL") == 0) {
            handle_room_detail(client_socket, data);
        } else if (strcmp(command, "MY_ROOM") == 0) {
            handle_my_room(client_socket, data);
        } else if (strcmp(command, "LIST_AUCTIONS") == 0) {
            handle_list_auctions(client_socket, data);
        } else if (strcmp(command, "MY_AUCTIONS") == 0) {
            handle_my_auctions(client_socket, data);
        } else if (strcmp(command, "AUCTION_DETAIL") == 0) {
            handle_auction_detail(client_socket, data);
        } else if (strcmp(command, "CREATE_AUCTION") == 0) {
            handle_create_auction(client_socket, data);
        } else if (strcmp(command, "PLACE_BID") == 0) {
            handle_place_bid(client_socket, data);
        } else if (strcmp(command, "BUY_NOW") == 0) {
            handle_buy_now(client_socket, data);
        } else if (strcmp(command, "BID_HISTORY") == 0) {
            handle_bid_history(client_socket, data);
        } else if (strcmp(command, "AUCTION_HISTORY") == 0) {
            handle_auction_history(client_socket, data);
        } else if (strcmp(command, "QUIT") == 0) {
            break;
        } else {
            char response[256];
            sprintf(response, "ERROR|Unknown command: %s\n", command);
            send(client_socket, response, strlen(response), 0);
        }
    }

    printf("[INFO] Client disconnected: socket %d\n", client_socket);
    remove_client(client_socket);
    close(client_socket);

    return NULL;
}

// =====================================================
// AUCTION TIMER THREAD
// =====================================================

void* auction_timer(void *arg) {
    while (server_running) {
        sleep(5); // Check every 5 seconds

        pthread_mutex_lock(&data_mutex);
        time_t now = time(NULL);

        for (int i = 0; i < g_auction_count; i++) {
            if (strcmp(g_auctions[i].status, "active") == 0) {
                int time_left = g_auctions[i].end_time - now;

                // Check if auction ended
                if (time_left <= 0) {
                    strcpy(g_auctions[i].status, "ended");

                    // Get winner information
                    char winner_name[50] = "No bids";
                    double final_price = g_auctions[i].current_price;
                    int total_bids = g_auctions[i].total_bids;

                    if (g_auctions[i].winner_id > 0) {
                        User *winner = find_user_by_id(g_auctions[i].winner_id);
                        if (winner != NULL) {
                            strcpy(winner_name, winner->username);
                        }
                    }

                    // Send detailed winner announcement
                    char notification[512];
                    sprintf(notification, "AUCTION_ENDED|%d|%s|%s|%.2f|%d\n",
                            g_auctions[i].auction_id,
                            g_auctions[i].title,
                            winner_name,
                            final_price,
                            total_bids);
                    broadcast_message_to_room(notification, g_auctions[i].room_id, -1);

                    printf("[INFO] Auction %d ended - Winner: %s, Price: %.2f, Bids: %d\n",
                           g_auctions[i].auction_id, winner_name, final_price, total_bids);
                }
                // ✅ NEW: Send warning when 30 seconds left
                else if (time_left <= 30 && time_left > 25) {
                    // Send warning in this 5-second window
                    char warning[512];
                    sprintf(warning, "AUCTION_WARNING|%d|%s|%.2f|%d\n",
                            g_auctions[i].auction_id,
                            g_auctions[i].title,
                            g_auctions[i].current_price,
                            time_left);
                    broadcast_message_to_room(warning, g_auctions[i].room_id, -1);

                    printf("[INFO] Auction %d warning: %d seconds left\n",
                           g_auctions[i].auction_id, time_left);
                }
            }
        }

        pthread_mutex_unlock(&data_mutex);
    }

    return NULL;
}

// =====================================================
// SIGNAL HANDLER
// =====================================================

void signal_handler(int sig) {
    printf("\n[INFO] Server shutting down...\n");
    server_running = 0;
    save_all_data();
    close(server_socket);
    exit(0);
}

// =====================================================
// MAIN FUNCTION
// =====================================================

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize data storage
    init_data_storage();

    // Initialize client sessions
    memset(g_clients, 0, sizeof(g_clients));

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("===========================================\n");
    printf("   ONLINE AUCTION SYSTEM SERVER (WITH ROOMS)\n");
    printf("===========================================\n");
    printf("[INFO] Server listening on port %d\n", PORT);
    printf("[INFO] Press Ctrl+C to stop server\n");
    printf("===========================================\n\n");

    // Start auction timer thread
    pthread_t timer_thread;
    pthread_create(&timer_thread, NULL, auction_timer, NULL);

    // Accept clients
    while (server_running) {
        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

        if (*client_socket < 0) {
            free(client_socket);
            continue;
        }

        // Create thread for client
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, client_socket);
        pthread_detach(thread_id);
    }

    // Cleanup
    save_all_data();
    close(server_socket);

    return 0;
}
